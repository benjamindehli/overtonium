#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
/// Linear below the threshold, smoothly compressed above it, bounded at 1.0.
inline float softClip(float x) noexcept {
  constexpr float threshold = 0.7f;

  const float a = std::abs(x);
  if (a <= threshold)
    return x;

  const float over = (a - threshold) / (1.0f - threshold);
  const float y = threshold + (1.0f - threshold) * std::tanh(over);

  return x < 0.0f ? -y : y;
}
} // namespace

void SynthEngine::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);
  renderRate = sampleRate;

  // Distinct seeds so two voices never draw the same drift contour.
  for (size_t i = 0; i < voices.size(); ++i)
    voices[i].prepare(sampleRate, (uint32_t)(i + 1) * 2654435761u);

  echo.prepare(sampleRate);
  reverb.prepare(sampleRate);

  reset();
}

void SynthEngine::reset() noexcept {
  for (auto &v : voices)
    v.reset();

  echo.reset();
  reverb.reset();

  heldBySustain.fill(false);
  sustainDown = false;
  ageCounter = 0;
  smoothedMasterGain = -1.0f;

  // Past 1 so the first host sample of the next block draws a fresh frame
  // rather than a stale held one.
  resamplePhase = 1.0;
  heldL = heldR = 0.0f;

  for (auto &level : partialLevels)
    level.store(0.0f, std::memory_order_relaxed);

  noiseLevel.store(0.0f, std::memory_order_relaxed);
  outputLevelL.store(0.0f, std::memory_order_relaxed);
  outputLevelR.store(0.0f, std::memory_order_relaxed);
}

void SynthEngine::setPolyphony(int n) noexcept {
  polyphony = std::clamp(n, 1, kMaxPolyphony);
}

int SynthEngine::countSounding() const noexcept {
  int c = 0;
  for (const auto &v : voices)
    if (v.isActive() && !v.isReleasing())
      ++c;

  return c;
}

int SynthEngine::getActiveVoiceCount() const noexcept {
  int c = 0;
  for (const auto &v : voices)
    if (v.isActive())
      ++c;

  return c;
}

Voice *SynthEngine::findFreeVoice() noexcept {
  for (auto &v : voices)
    if (!v.isActive())
      return &v;

  return nullptr;
}

Voice *SynthEngine::findOldestSounding() noexcept {
  Voice *oldest = nullptr;

  for (auto &v : voices)
    if (v.isActive() && !v.isReleasing())
      if (oldest == nullptr || v.getAge() < oldest->getAge())
        oldest = &v;

  return oldest;
}

void SynthEngine::noteOn(int note, float velocity,
                         const SynthParams &p) noexcept {
  // One key, one voice. The instrument is polyphonic across the keyboard and
  // monophonic within a key, because a string or a tine or a bar is one
  // object: striking it again takes over whatever it was already doing rather
  // than starting a second copy of it beside the first.
  //
  // Tails count. A key with a long release that is tapped repeatedly used to
  // leave every tap ringing and sum them, which no physical instrument does
  // and which is also the quickest way to reach the clipper.
  Voice *held = nullptr;
  size_t heldIndex = 0;

  for (size_t i = 0; i < voices.size(); ++i) {
    auto &v = voices[i];

    if (!v.isActive() || v.getNote() != note)
      continue;

    if (v.isReleasing()) {
      // Cut with the same short fade a stolen voice gets. Fast enough to read
      // as instant, slow enough not to click.
      v.steal();
    } else {
      held = &v;
      heldIndex = i;
    }
  }

  // A key still down, or held by the pedal, is retriggered where it stands.
  // That keeps two things the fade would lose: a legato retrigger continues
  // from the level the envelope is at rather than restarting from silence,
  // and re-striking a pedalled note takes it back off the pedal.
  if (held != nullptr) {
    heldBySustain[heldIndex] = false;
    held->noteOn(note, velocity, p);
    held->setAge(++ageCounter);
    return;
  }

  if (countSounding() >= polyphony)
    if (auto *victim = findOldestSounding())
      victim->steal();

  auto *target = findFreeVoice();

  if (target == nullptr) {
    // Pool exhausted (every surplus voice is mid-fade). Take the oldest
    // outright.
    target = &voices[0];
    for (auto &v : voices)
      if (v.getAge() < target->getAge())
        target = &v;

    target->reset();
  }

  const auto index = (size_t)std::distance(voices.data(), target);
  heldBySustain[index] = false;

  target->noteOn(note, velocity, p);
  target->setAge(++ageCounter);
}

void SynthEngine::noteOff(int note, float velocity) noexcept {
  for (size_t i = 0; i < voices.size(); ++i) {
    auto &v = voices[i];

    if (v.isActive() && !v.isReleasing() && v.getNote() == note) {
      if (sustainDown)
        heldBySustain[i] = true;
      else
        v.noteOff(velocity);
    }
  }
}

void SynthEngine::setSustainPedal(bool down) noexcept {
  sustainDown = down;

  if (down)
    return;

  for (size_t i = 0; i < voices.size(); ++i) {
    if (heldBySustain[i]) {
      voices[i].noteOff();
      heldBySustain[i] = false;
    }
  }
}

void SynthEngine::setPolyPressure(int note, float pressure) noexcept {
  for (auto &v : voices)
    if (v.isActive() && v.getNote() == note)
      v.setPolyPressure(pressure);
}

void SynthEngine::allNotesOff() noexcept {
  for (size_t i = 0; i < voices.size(); ++i) {
    voices[i].noteOff();
    heldBySustain[i] = false;
  }

  sustainDown = false;
}

void SynthEngine::allSoundOff() noexcept {
  for (auto &v : voices)
    v.reset();

  heldBySustain.fill(false);
  sustainDown = false;
}

namespace {
/// Rounds to the nearest of 2^(bits-1) steps either side of zero.
///
/// Deliberately does not clamp. This sits ahead of the master fader and the
/// safety clipper, which is where clipping belongs, and a quantiser that also
/// clipped would turn a bit-depth setting into a distortion the panel does not
/// mention.
inline void quantise(float *left, float *right, int n, int bits) noexcept {
  const float levels = (float)(1 << (bits - 1));
  const float step = 1.0f / levels;

  for (int i = 0; i < n; ++i) {
    left[i] = std::round(left[i] * levels) * step;
    right[i] = std::round(right[i] * levels) * step;
  }
}
} // namespace

double SynthEngine::lofiRenderRate(const SynthParams &p) const noexcept {
  if (p.lofi.rateHz <= 0.0)
    return sampleRate;

  // Asking for more than the host is running at is not a thing anyone can
  // have, so it reads as off rather than as an upsample.
  return std::clamp(p.lofi.rateHz, 1000.0, sampleRate);
}

void SynthEngine::setRenderRate(double rate) noexcept {
  if (rate == renderRate)
    return;

  renderRate = rate;

  for (auto &v : voices)
    v.setRenderRate(rate);
}

void SynthEngine::sumVoices(float *left, float *right, int numFrames,
                            const SynthParams &p,
                            std::array<float, kNumHarmonics> &peaks,
                            float &noisePeak) noexcept {
  std::fill(left, left + numFrames, 0.0f);
  std::fill(right, right + numFrames, 0.0f);

  for (auto &v : voices) {
    if (!v.isActive())
      continue;

    v.render(left, right, numFrames, p);

    const auto &voicePeaks = v.getPartialPeaks();
    for (size_t i = 0; i < peaks.size(); ++i)
      peaks[i] = std::max(peaks[i], voicePeaks[i]);

    noisePeak = std::max(noisePeak, v.getNoisePeak());
  }
}

void SynthEngine::renderVoices(float *left, float *right, int numSamples,
                               const SynthParams &p) noexcept {
  const auto target = lofiRenderRate(p);
  const int bits = std::clamp(p.lofi.bits, 0, 24);

  std::array<float, kNumHarmonics> peaks{};
  float noisePeak = 0.0f;

  if (target >= sampleRate) {
    setRenderRate(sampleRate);
    sumVoices(left, right, numSamples, p, peaks, noisePeak);

    if (bits > 0)
      quantise(left, right, numSamples, bits);
  } else {
    // The whole pool renders slowly and the result is held between frames.
    //
    // This is where the setting pays for itself. The obvious way to build a
    // rate reducer is to render everything at the host rate and then hold the
    // output, but sampling a sinusoid at 8 kHz gives one particular sequence
    // of numbers whatever rate you were nominally computing at, so the samples
    // that survive holding are the only ones worth computing. Thirty-two
    // oscillators and their envelopes are what this instrument costs, and
    // against a 48 kHz host they now run a sixth as often.
    //
    // Not quite identical to the expensive way: the per-control-block work,
    // the LFOs and the gain ramps, still lands every 32 frames, which is now
    // 4 ms rather than 0.7. Modulation is coarser, at the rate the rest of it
    // is coarser. Everything is still in the right place in real time,
    // because the coefficients are all derived from the rate being rendered
    // at.
    setRenderRate(target);

    const double ratio = target / sampleRate;

    for (int done = 0; done < numSamples;) {
      const int len = std::min(kLofiChunk, numSamples - done);

      // How many frames this chunk of output will draw from. Counted first
      // rather than estimated, so the expansion below never runs off the end
      // of what was rendered.
      int frames = 0;
      {
        double ph = resamplePhase;

        for (int n = 0; n < len; ++n) {
          ph += ratio;

          if (ph >= 1.0) {
            ph -= 1.0;
            ++frames;
          }
        }
      }

      sumVoices(lofiScratchL.data(), lofiScratchR.data(), frames, p, peaks,
                noisePeak);

      if (bits > 0)
        quantise(lofiScratchL.data(), lofiScratchR.data(), frames, bits);

      int src = 0;

      for (int n = 0; n < len; ++n) {
        resamplePhase += ratio;

        if (resamplePhase >= 1.0) {
          resamplePhase -= 1.0;
          heldL = lofiScratchL[(size_t)src];
          heldR = lofiScratchR[(size_t)src];
          ++src;
        }

        left[done + n] = heldL;
        right[done + n] = heldR;
      }

      done += len;
    }
  }

  for (size_t i = 0; i < peaks.size(); ++i)
    partialLevels[i].store(peaks[i], std::memory_order_relaxed);

  noiseLevel.store(noisePeak, std::memory_order_relaxed);
}

void SynthEngine::render(float *left, float *right, int numSamples,
                         const SynthParams &p) noexcept {
  if (numSamples <= 0)
    return;

  renderVoices(left, right, numSamples, p);

  // ---- master effects, ahead of the fader ----------------------------------
  // The channel meters above read the partials themselves, so they are taken
  // before this point. The output meter is taken after it, which is why the
  // two disagree once a tail is ringing: that is the effects, and it should
  // show.
  echo.process(left, right, numSamples, p.echo);
  reverb.process(left, right, numSamples, p.reverb);

  // ---- master gain, smoothed over ~10 ms so fader moves do not zipper -------
  const float target = std::max(0.0f, p.global.masterGain);

  if (smoothedMasterGain < 0.0f)
    smoothedMasterGain = target;

  const auto coef = (float)std::exp(-1.0 / (0.01 * sampleRate));

  if (std::abs(target - smoothedMasterGain) < 1.0e-6f) {
    smoothedMasterGain = target;

    for (int n = 0; n < numSamples; ++n) {
      left[n] *= target;
      right[n] *= target;
    }
  } else {
    float g = smoothedMasterGain;

    for (int n = 0; n < numSamples; ++n) {
      g = target + (g - target) * coef;
      left[n] *= g;
      right[n] *= g;
    }

    smoothedMasterGain = g;
  }

  if (p.global.safetyClip) {
    for (int n = 0; n < numSamples; ++n) {
      left[n] = softClip(left[n]);
      right[n] = softClip(right[n]);
    }
  }

  float peakL = 0.0f, peakR = 0.0f;
  for (int n = 0; n < numSamples; ++n) {
    peakL = std::max(peakL, std::abs(left[n]));
    peakR = std::max(peakR, std::abs(right[n]));
  }

  outputLevelL.store(peakL, std::memory_order_relaxed);
  outputLevelR.store(peakR, std::memory_order_relaxed);
}

} // namespace ovt
