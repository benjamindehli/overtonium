#include "Voice.h"

#include <type_traits>

#include <algorithm>

#include "SineTable.h"

namespace ovt {

namespace {
/// Drift is meant to read as gentle chorusing rather than vibrato, so the
/// rates sit well below anything you would hear as a pitch wobble. Each
/// partial of each note draws its own rate from this range, log distributed.
constexpr double kDriftMinHz = 0.08;
constexpr double kDriftMaxHz = 1.10;

/// Corner of the one-pole pair that tilts the noise. Around a kilohertz splits
/// it into a usable "body" and "air" either side.
constexpr double kNoiseTiltHz = 1000.0;
} // namespace

void Voice::setRenderRate(double newSampleRate) noexcept {
  // Drift is the one thing not recomputed here. Its step is latched at
  // note-on, so a note already sounding when the rate changes wanders at the
  // old speed until it is played again. Re-deriving it would need the rate
  // knob's value per partial, which is a lot of plumbing for an artefact that
  // lasts one note and only on a setting nobody changes mid-phrase.
  sampleRate = std::max(1.0, newSampleRate);

  for (auto &pt : partials)
    pt.env.setSampleRate(sampleRate);

  noise.env.setSampleRate(sampleRate);

  // Roughly a 15 ms time constant, stepped once per control block.
  pressureCoef =
      1.0f - (float)std::exp(-(double)kControlBlock / (0.015 * sampleRate));

  lowpassCoef =
      1.0f - (float)std::exp(-6.283185307179586 * kNoiseTiltHz / sampleRate);
}

void Voice::prepare(double newSampleRate, uint32_t seed) noexcept {
  setRenderRate(newSampleRate);

  rng.reseed(seed);
  noise.rng.reseed(seed ^ 0x5bf03635u);

  reset();
}

void Voice::reset() noexcept {
  for (auto &pt : partials) {
    pt.env.reset();
    pt.drift.reset();
    pt.phase = 0.0;
    pt.pitchLfoPhase = 0.0;
    pt.ampLfoPhase = 0.0;
    pt.lastGain = 0.0f;
    pt.gainPrimed = false;
  }

  noise.env.reset();
  noise.lowpassState = 0.0f;
  noise.lastGain = 0.0f;
  noise.gainPrimed = false;
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;

  active = false;
  released = false;
  midiNote = -1;
  midiChannel = 0;
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;
  slide = 0.0f;
  pressureSmoothed = 0.0f;
}

void Voice::noteOn(int channel, int note, float velocity,
                   const SynthParams &p) noexcept {
  midiNote = note;
  midiChannel = channel;
  baseFreq = noteFrequency(note, p.global.temperament, p.global.tuningRoot,
                           p.global.referenceHz);

  const float vel = std::clamp(velocity, 0.0f, 1.0f);

  // A fresh note starts unpressed and unbent, and ramps in if the key is
  // already leaned on. A retrigger lands here too, which is what stops the
  // previous note's bend carrying into the new one on a channel being reused.
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;
  slide = 0.0f;
  pressureSmoothed = 0.0f;

  active = true;
  released = false;

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &pt = partials[(size_t)i];
    const auto &op = p.osc[(size_t)i];

    // Each strip decides for itself how much of the key velocity it takes,
    // latched here so a velocity change cannot alter a note already sounding.
    //
    // Both halves give 1 at zero amount and reach vel and 1 - vel at the
    // extremes, so a negative setting makes the partial loudest when played
    // softly rather than hardest.
    const float amount = std::clamp(op.velAmount, -1.0f, 1.0f);
    pt.velGain =
        amount >= 0.0f ? 1.0f - amount * (1.0f - vel) : 1.0f + amount * vel;

    pt.liftAmount = std::clamp(op.liftAmount, -1.0f, 1.0f);

    pt.env.configure(op.delay, op.attack, op.decay, op.sustain, op.swell,
                     op.offLevel, op.release);
    pt.env.noteOn(p.global.phaseReset);

    // A fresh rate per partial per note. Reusing one rate would turn 32
    // independent wanders into a single detune.
    const double rate = kDriftMinHz * std::pow(kDriftMaxHz / kDriftMinHz,
                                               (double)rng.unipolar());
    pt.drift.restart(rng, rate, sampleRate / (double)kControlBlock);

    if (p.global.phaseReset) {
      pt.phase = (double)std::clamp(op.startPhase, 0.0f, 1.0f);
      pt.pitchLfoPhase = 0.0;
      pt.ampLfoPhase = 0.0;
    }

    // The gain ramp restarts from whatever this partial's strip currently asks
    // for rather than from the previous note's trailing value.
    pt.gainPrimed = false;
  }

  {
    const auto &np = p.noise;
    const float amount = std::clamp(np.velAmount, -1.0f, 1.0f);

    noise.velGain =
        amount >= 0.0f ? 1.0f - amount * (1.0f - vel) : 1.0f + amount * vel;

    noise.liftAmount = std::clamp(np.liftAmount, -1.0f, 1.0f);

    noise.env.configure(np.delay, np.attack, np.decay, np.sustain, np.swell,
                        np.offLevel, np.release);
    noise.env.noteOn(p.global.phaseReset);
    noise.gainPrimed = false;
  }
}

void Voice::noteOff(float velocity) noexcept {
  if (!active)
    return;

  released = true;

  // The same shape the note-on velocity uses, so the two rows read the same
  // way: zero ignores the gesture, positive means faster is louder, negative
  // inverts it.
  const auto scaleFor = [v = std::clamp(velocity, 0.0f, 1.0f)](float amount) {
    return amount >= 0.0f ? 1.0f - amount * (1.0f - v) : 1.0f + amount * v;
  };

  for (auto &pt : partials)
    pt.env.noteOff(scaleFor(pt.liftAmount));

  noise.env.noteOff(scaleFor(noise.liftAmount));
}

void Voice::steal() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.forceRelease(0.004f);

  noise.env.forceRelease(0.004f);
}

void Voice::render(float *left, float *right, int numSamples,
                   const SynthParams &p) noexcept {
  if (!active || numSamples <= 0)
    return;

  const auto &sine = SineTable::instance();

  // Equal-power pan positions, one per partial.
  //
  // Placing them by hand rather than fanning them from one control is the
  // difference between a width setting and an arrangement: a partial can be
  // put opposite the one a semitone away from it, or the octaves left and the
  // sevenths right, which is not a shape any single knob could have produced.
  std::array<float, kNumHarmonics> panL{}, panR{};

  for (int i = 0; i < kNumHarmonics; ++i) {
    const float pan = std::clamp(p.osc[(size_t)i].pan, -1.0f, 1.0f);

    panL[(size_t)i] = std::sqrt(0.5f * (1.0f - pan));
    panR[(size_t)i] = std::sqrt(0.5f * (1.0f + pan));
  }

  // How much of each partial the keyboard tracking leaves at this pitch.
  //
  // Taken from the nominal position of the partial rather than from the
  // frequency it is momentarily at, so vibrato and drift do not modulate the
  // brightness, and worked out once per call rather than once per control
  // block, since none of it moves while a note sounds.
  std::array<float, kNumHarmonics> track{};

  // MPE slide, where it is aimed at brightness. Tracking thins the top of the
  // series, so a finger pushed forward takes tracking away and pulled back
  // adds more. It cannot brighten past what the patch already is: there is no
  // negative tracking, since trackingGain returns 1 for anything at or below
  // zero. A patch with tracking off is therefore as bright as it gets, and
  // slide can only darken it.
  const auto tracking =
      p.global.slideDest == SlideDestination::Brightness
          ? std::max(0.0f, p.global.trackDbPerOctave - slide * kSlideTrackDb)
          : p.global.trackDbPerOctave;

  if (tracking > 0.0f) {
    for (int i = 0; i < kNumHarmonics; ++i) {
      const auto semis = semitoneOffset(i, (double)blendOf(p, i),
                                        (double)p.global.stretchCents);

      track[(size_t)i] = trackingGain(baseFreq * std::exp2(semis / 12.0),
                                      baseFreq, (double)tracking);
    }
  } else {
    track.fill(1.0f);
  }

  partialPeaks.fill(0.0f);
  partialEnvelopes.fill(0.0f);
  partialTremolos.fill(0.0f);
  partialPitches.fill(0.0f);
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;

  const float pressureTarget =
      std::max(std::clamp(p.global.aftertouch, 0.0f, 1.0f), polyPressure);

  // With the render rate turned down, the guard that keeps partials from
  // folding is exactly the wrong thing to have on: folding is the sound being
  // asked for. A converter running at 8 kHz does not quietly mute everything
  // above 4 kHz, it wraps it back down, and so does this.
  const bool foldAliases = p.lofi.rateHz > 0.0;

  for (int start = 0; start < numSamples; start += kControlBlock) {
    const int len = std::min(kControlBlock, numSamples - start);

    pressureSmoothed += (pressureTarget - pressureSmoothed) * pressureCoef;
    const float pressure = pressureSmoothed;

    for (int i = 0; i < kNumHarmonics; ++i) {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];

      pt.env.configure(op.delay, op.attack, op.decay, op.sustain, op.swell,
                       op.offLevel, op.release);

      if (!pt.env.isActive())
        continue;

      // ---- pitch ------------------------------------------------------------
      const double pmPhaseInc = (double)op.pmRateHz / sampleRate;
      const double pmCents =
          op.pmDepthCents > 0.0f
              ? (double)(sine(pt.pitchLfoPhase) * op.pmDepthCents)
              : 0.0;

      // Advanced unconditionally so that turning the knob up mid-note joins the
      // wander already in progress instead of jumping.
      const double driftCents = (double)(pt.drift.advance(rng) * op.driftCents);

      const double semis =
          semitoneOffset(i, (double)blendOf(p, i),
                         (double)p.global.stretchCents) +
          (pmCents + driftCents) * 0.01 +
          (double)(p.global.bendSemitones + noteBendSemitones);

      const double freq = baseFreq * std::exp2(semis / 12.0);

      // Reduced into the first turn of the table. Sampling a sinusoid above
      // the rate produces the same numbers as sampling the one it folds to, so
      // this is not an approximation of aliasing, it is the aliasing, and the
      // inner loop keeps its single compare instead of a modulo per sample.
      double inc = freq / sampleRate;
      inc -= std::floor(inc);

      // ---- amplitude --------------------------------------------------------
      const double amPhaseInc = (double)op.amRateHz / sampleRate;

      float amEnd = 1.0f;
      if (op.amDepth > 0.0f) {
        // cos() so the tremolo starts at full level on note-on and dips
        // downwards.
        const double endPhase =
            wrapPhase(pt.ampLfoPhase + amPhaseInc * (double)len);
        amEnd = 1.0f - op.amDepth * 0.5f * (1.0f - sine.cosine(endPhase));
      }

      const float nyq = foldAliases ? 1.0f : nyquistGain(freq, sampleRate);

      // Aftertouch adds to the fader instead of scaling it, which is what lets
      // a strip sitting at zero be brought in by pressure alone. Velocity only
      // ever touches the fader's own contribution.
      // Aftertouch adds, so a negative amount subtracts and the clamp floors
      // it at silence. No special case needed for the inverted direction.
      const float level =
          std::clamp(op.volume * pt.velGain +
                         std::clamp(op.atAmount, -1.0f, 1.0f) * pressure,
                     0.0f, 1.0f);
      const float base =
          op.audible ? level * nyq * track[(size_t)i] : 0.0f;
      const float gEnd = base * amEnd;

      if (!pt.gainPrimed) {
        float amStart = 1.0f;
        if (op.amDepth > 0.0f)
          amStart =
              1.0f - op.amDepth * 0.5f * (1.0f - sine.cosine(pt.ampLfoPhase));

        pt.lastGain = base * amStart;
        pt.gainPrimed = true;
      }

      // Ramping from the previous block's gain to this one covers tremolo,
      // volume moves, mute/solo and the Nyquist fade with a single
      // interpolation.
      float g = pt.lastGain;
      const float gInc = (gEnd - g) / (float)len;

      pt.pitchLfoPhase = wrapPhase(pt.pitchLfoPhase + pmPhaseInc * (double)len);
      pt.ampLfoPhase = wrapPhase(pt.ampLfoPhase + amPhaseInc * (double)len);
      pt.lastGain = gEnd;

      // One multiply and one compare per partial per control block. Both terms
      // are already to hand, so metering costs essentially nothing here.
      partialPeaks[(size_t)i] =
          std::max(partialPeaks[(size_t)i], pt.env.getLevel() * gEnd);

      // The same again for the lamps: everything here was worked out above for
      // the oscillator's own use, so this is three stores and a compare.
      //
      // The envelope carries its stage in its sign. Swell and release are the
      // two that run after the key is up, and they are the ones the second
      // lamp takes over.
      const auto stage = pt.env.getStage();
      const auto afterKeyOff = stage == Envelope::Stage::Swell ||
                               stage == Envelope::Stage::Release;

      partialEnvelopes[(size_t)i] =
          afterKeyOff ? -pt.env.getLevel() : pt.env.getLevel();

      // What the tremolo has taken off, rather than what it has left. A
      // partial with no tremolo on it then reads zero instead of full, which
      // is a lamp that is dark rather than one that is on and never moves.
      partialTremolos[(size_t)i] = 1.0f - amEnd;
      partialPitches[(size_t)i] = (float)(pmCents + driftCents);

      if ((g <= 1.0e-7f && gEnd <= 1.0e-7f) || pt.env.isSilentlyHolding()) {
        // Inaudible right now: muted, faded out above Nyquist, fader at zero,
        // or decayed to a sustain of nothing and waiting for the key to come
        // up. Keep the envelope and phase moving so unmuting or letting go
        // mid-note picks up in the right place, but skip the oscillator.
        for (int n = 0; n < len; ++n)
          pt.env.tick();

        pt.phase = wrapPhase(pt.phase + inc * (double)len);
        continue;
      }

      float *l = left + start;
      float *r = right + start;

      double ph = pt.phase;
      const float pl = panL[(size_t)i];
      const float pr = panR[(size_t)i];

      for (int n = 0; n < len; ++n) {
        const float s = sine(ph) * pt.env.tick() * g;

        l[n] += s * pl;
        r[n] += s * pr;

        ph += inc;
        if (ph >= 1.0)
          ph -= 1.0;

        g += gInc;
      }

      pt.phase = ph;
    }

    renderNoise(left + start, right + start, len, p, pressure);
  }

  active = noise.env.isActive() ||
           std::any_of(partials.begin(), partials.end(),
                       [](const Partial &pt) { return pt.env.isActive(); });

  if (!active) {
    released = false;
    midiNote = -1;
  }
}

void Voice::renderNoise(float *left, float *right, int len,
                        const SynthParams &p, float pressure) noexcept {
  const auto &np = p.noise;

  noise.env.configure(np.delay, np.attack, np.decay, np.sustain, np.swell,
                      np.offLevel, np.release);

  if (!noise.env.isActive())
    return;

  const double amPhaseInc = (double)np.amRateHz / sampleRate;

  float amEnd = 1.0f;
  if (np.amDepth > 0.0f) {
    const double endPhase = wrapPhase(noiseAmPhase + amPhaseInc * (double)len);
    amEnd = 1.0f -
            np.amDepth * 0.5f * (1.0f - SineTable::instance().cosine(endPhase));
  }

  const float level =
      std::clamp(np.volume * noise.velGain +
                     std::clamp(np.atAmount, -1.0f, 1.0f) * pressure,
                 0.0f, 1.0f);
  const float gEnd = (np.audible ? level : 0.0f) * amEnd;

  if (!noise.gainPrimed) {
    noise.lastGain = np.audible ? level : 0.0f;
    noise.gainPrimed = true;
  }

  float g = noise.lastGain;
  const float gInc = (gEnd - g) / (float)len;

  noiseAmPhase = wrapPhase(noiseAmPhase + amPhaseInc * (double)len);
  noise.lastGain = gEnd;

  noisePeak = std::max(noisePeak, noise.env.getLevel() * gEnd);

  {
    const auto stage = noise.env.getStage();
    const auto afterKeyOff = stage == Envelope::Stage::Swell ||
                             stage == Envelope::Stage::Release;

    noiseEnvelope =
        afterKeyOff ? -noise.env.getLevel() : noise.env.getLevel();
    noiseTremolo = 1.0f - amEnd;
  }

  if (g <= 1.0e-7f && gEnd <= 1.0e-7f) {
    for (int n = 0; n < len; ++n)
      noise.env.tick();

    return;
  }

  // Colour tilts between the two halves of a complementary one-pole pair.
  // Both at unity reconstructs the original white noise, so the centre of the
  // knob is genuinely flat rather than merely filtered less.
  const float colour = std::clamp(np.colour, 0.0f, 1.0f);
  const float lowMix = colour < 0.5f ? 1.0f : 1.0f - (colour - 0.5f) * 2.0f;
  const float highMix = colour < 0.5f ? colour * 2.0f : 1.0f;

  // Placed the same way a partial is, so the one channel that is not part of
  // the series still sits somewhere rather than always up the middle.
  const float pan = std::clamp(np.pan, -1.0f, 1.0f);
  const float panL = std::sqrt(0.5f * (1.0f - pan));
  const float panR = std::sqrt(0.5f * (1.0f + pan));

  for (int n = 0; n < len; ++n) {
    const float white = noise.rng.bipolar();

    noise.lowpassState += lowpassCoef * (white - noise.lowpassState);
    const float high = white - noise.lowpassState;

    const float s =
        (lowMix * noise.lowpassState + highMix * high) * noise.env.tick() * g;

    left[n] += s * panL;
    right[n] += s * panR;

    g += gInc;
  }
}

} // namespace ovt
