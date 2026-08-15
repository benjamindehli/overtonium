#pragma once

#include <array>
#include <cstdint>

#include "Drift.h"
#include "Envelope.h"
#include "Harmonics.h"
#include "Params.h"

namespace ovt {

/// Fades a partial out as it approaches Nyquist.
///
/// Without this, playing high notes folds the upper partials back down as
/// aliasing: the 32nd harmonic of C7 lands at ~67 kHz. The fade starts well
/// below Nyquist so the partial disappears smoothly instead of blinking out.
inline float nyquistGain(double freq, double sampleRate) noexcept {
  const double fadeStart = sampleRate * 0.42;
  const double fadeEnd = sampleRate * 0.49;

  if (freq <= fadeStart)
    return 1.0f;
  if (freq >= fadeEnd)
    return 0.0f;

  const double t = (freq - fadeStart) / (fadeEnd - fadeStart);
  return (float)(0.5 * (1.0 + std::cos(3.14159265358979324 * t)));
}

/// One polyphonic voice: 32 independently tuned, enveloped and modulated sine
/// partials.
class Voice {
public:
  /// LFOs, envelope-driven gain interpolation and the Nyquist guard update once
  /// per control block rather than once per sample. 32 frames is ~0.7 ms at
  /// 44.1 kHz.
  static constexpr int kControlBlock = 32;

  /// @param seed  distinguishes this voice's random stream from its siblings.
  void prepare(double newSampleRate, uint32_t seed) noexcept;
  void reset() noexcept;

  /// Moves the voice to a different render rate without disturbing it.
  ///
  /// Everything rate-dependent here is a coefficient recomputed from a stored
  /// rate, so this can happen under a sounding note: phases, envelope levels
  /// and stages all carry on where they were, and a one-second attack is still
  /// a second. Used by the lo-fi setting, which renders the whole voice pool
  /// slower rather than filtering the result.
  void setRenderRate(double newSampleRate) noexcept;

  void noteOn(int note, float velocity, const SynthParams &p) noexcept;
  void noteOff() noexcept;
  void steal() noexcept; ///< fast fade-out so the voice can be reused

  /// Polyphonic aftertouch for this voice. Channel pressure arrives separately
  /// through SynthParams, and whichever is higher wins.
  void setPolyPressure(float v) noexcept { polyPressure = v; }

  bool isActive() const noexcept { return active; }
  bool isReleasing() const noexcept { return released; }
  int getNote() const noexcept { return midiNote; }

  uint64_t getAge() const noexcept { return startOrder; }
  void setAge(uint64_t v) noexcept { startOrder = v; }

  /// Peak output amplitude of each partial during the last render call, for
  /// metering. Sampled once per control block, which is all a meter can show.
  const std::array<float, kNumHarmonics> &getPartialPeaks() const noexcept {
    return partialPeaks;
  }

  float getNoisePeak() const noexcept { return noisePeak; }

  /// Adds this voice into the (already-sized) stereo buffers. Master gain is
  /// applied downstream by the engine.
  void render(float *left, float *right, int numSamples,
              const SynthParams &p) noexcept;

private:
  void renderNoise(float *left, float *right, int len, const SynthParams &,
                   float pressure) noexcept;

  struct Partial {
    double phase = 0.0;
    double pitchLfoPhase = 0.0;
    double ampLfoPhase = 0.0;
    Envelope env;
    SmoothRandom drift;
    /// Carried across control blocks so gain never steps.
    float lastGain = 0.0f;
    /// Latched at note-on from this strip's own velocity sensitivity.
    float velGain = 1.0f;
    bool gainPrimed = false;
  };

  /// The noise channel runs alongside the partials with its own envelope and
  /// its own random stream, so each note gets its own noise rather than every
  /// voice layering the identical signal.
  struct Noise {
    Envelope env;
    Xorshift rng;
    float lowpassState = 0.0f;
    float velGain = 1.0f;
    float lastGain = 0.0f;
    bool gainPrimed = false;
  };

  std::array<Partial, kNumHarmonics> partials{};
  std::array<float, kNumHarmonics> partialPeaks{};
  Noise noise;
  double noiseAmPhase = 0.0;
  float noisePeak = 0.0f;
  float lowpassCoef = 0.1f;

  double sampleRate = 44100.0;
  double baseFreq = 440.0;
  int midiNote = -1;

  Xorshift rng;
  float polyPressure = 0.0f;
  /// Aftertouch drives gain directly, so it needs its own smoothing. Seven bits
  /// arriving at MIDI rate would otherwise step audibly.
  float pressureSmoothed = 0.0f;
  float pressureCoef = 1.0f;

  bool active = false;
  bool released = false;
  uint64_t startOrder = 0;
};

} // namespace ovt
