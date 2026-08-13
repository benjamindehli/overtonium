#include "Voice.h"

#include <algorithm>

#include "SineTable.h"

namespace ovt {

void Voice::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  for (auto &pt : partials)
    pt.env.setSampleRate(sampleRate);

  // Roughly a 15 ms time constant, stepped once per control block.
  pressureCoef =
      1.0f - (float)std::exp(-(double)kControlBlock / (0.015 * sampleRate));

  reset();
}

void Voice::reset() noexcept {
  for (auto &pt : partials) {
    pt.env.reset();
    pt.phase = 0.0;
    pt.pitchLfoPhase = 0.0;
    pt.ampLfoPhase = 0.0;
    pt.lastGain = 0.0f;
    pt.gainPrimed = false;
  }

  active = false;
  released = false;
  midiNote = -1;
  polyPressure = 0.0f;
  pressureSmoothed = 0.0f;
}

void Voice::noteOn(int note, float velocity, const SynthParams &p) noexcept {
  midiNote = note;
  baseFreq = 440.0 * std::exp2((double)(note - 69) / 12.0);

  const float vel = std::clamp(velocity, 0.0f, 1.0f);

  // A fresh note starts unpressed and ramps in if the key is already leaned on.
  polyPressure = 0.0f;
  pressureSmoothed = 0.0f;

  active = true;
  released = false;

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &pt = partials[(size_t)i];
    const auto &op = p.osc[(size_t)i];

    // Each strip decides for itself how much of the key velocity it takes,
    // latched here so a velocity change cannot alter a note already sounding.
    const float amount = std::clamp(op.velAmount, 0.0f, 1.0f);
    pt.velGain = 1.0f - amount + amount * vel;

    pt.env.configure(op.attack, op.decay, op.sustain, op.release);
    pt.env.noteOn(p.global.phaseReset);

    if (p.global.phaseReset) {
      pt.phase = 0.0;
      pt.pitchLfoPhase = 0.0;
      pt.ampLfoPhase = 0.0;
    }

    // The gain ramp restarts from whatever this partial's strip currently asks
    // for rather than from the previous note's trailing value.
    pt.gainPrimed = false;
  }
}

void Voice::noteOff() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.noteOff();
}

void Voice::steal() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.forceRelease(0.004f);
}

void Voice::render(float *left, float *right, int numSamples,
                   const SynthParams &p) noexcept {
  if (!active || numSamples <= 0)
    return;

  const auto &sine = SineTable::instance();

  // Equal-power pan positions.
  //
  // Partials are placed in symmetric pairs: 1 and 2 in the centre, then 3 and 4
  // opposite each other, and so on out to 31 and 32 at the edges. Pairing
  // matters for two reasons. Neighbouring partials have near-identical levels
  // in any normal spectrum, so putting them on opposite sides keeps the image
  // centred whatever shape is dialled in, and every position has a mirror, so
  // nothing ends up hard panned with no counterpart on the other side.
  //
  // The width grows as a square root rather than linearly, because a spectrum
  // that rolls off puts almost all of its energy in the first few partials. A
  // linear fan leaves exactly those bunched in the middle and the control does
  // nothing audible.
  //
  // Which member of a pair takes the left side alternates, so the louder one
  // does not always land on the same side.
  std::array<float, kNumHarmonics> panL{}, panR{};
  {
    const float spread = std::clamp(p.global.stereoSpread, 0.0f, 1.0f);
    constexpr int lastPair = kNumHarmonics / 2 - 1;

    for (int i = 0; i < kNumHarmonics; ++i) {
      const int pairIndex = i / 2;
      const bool second = (i % 2) != 0;
      const bool flip = (pairIndex % 2) != 0;

      const float magnitude = std::sqrt((float)pairIndex / (float)lastPair);
      const float pos = (second != flip ? 1.0f : -1.0f) * magnitude;
      const float pan = std::clamp(spread * pos, -1.0f, 1.0f);

      panL[(size_t)i] = std::sqrt(0.5f * (1.0f - pan));
      panR[(size_t)i] = std::sqrt(0.5f * (1.0f + pan));
    }
  }

  const float pressureTarget =
      std::max(std::clamp(p.global.aftertouch, 0.0f, 1.0f), polyPressure);

  for (int start = 0; start < numSamples; start += kControlBlock) {
    const int len = std::min(kControlBlock, numSamples - start);

    pressureSmoothed += (pressureTarget - pressureSmoothed) * pressureCoef;
    const float pressure = pressureSmoothed;

    for (int i = 0; i < kNumHarmonics; ++i) {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];

      pt.env.configure(op.attack, op.decay, op.sustain, op.release);

      if (!pt.env.isActive())
        continue;

      // ---- pitch ------------------------------------------------------------
      const double pmPhaseInc = (double)op.pmRateHz / sampleRate;
      const double pmCents =
          op.pmDepthCents > 0.0f
              ? (double)(sine(pt.pitchLfoPhase) * op.pmDepthCents)
              : 0.0;

      const double semis = semitoneOffset(i, (double)op.tuneBlend) +
                           pmCents * 0.01 + (double)p.global.bendSemitones;

      const double freq = baseFreq * std::exp2(semis / 12.0);
      const double inc = freq / sampleRate;

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

      const float nyq = nyquistGain(freq, sampleRate);

      // Aftertouch adds to the fader instead of scaling it, which is what lets
      // a strip sitting at zero be brought in by pressure alone. Velocity only
      // ever touches the fader's own contribution.
      const float level = std::clamp(
          op.volume * pt.velGain + op.atAmount * pressure, 0.0f, 1.0f);
      const float base = op.audible ? level * nyq : 0.0f;
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

      if (g <= 1.0e-7f && gEnd <= 1.0e-7f) {
        // Inaudible right now (muted, faded out above Nyquist, or fader at
        // zero). Keep the envelope and phase moving so unmuting mid-note picks
        // up in the right place, but skip the oscillator entirely.
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
  }

  active = std::any_of(partials.begin(), partials.end(),
                       [](const Partial &pt) { return pt.env.isActive(); });

  if (!active) {
    released = false;
    midiNote = -1;
  }
}

} // namespace ovt
