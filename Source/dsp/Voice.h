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

/// Where the keyboard tracking rolloff sits, in Hz.
///
/// Instruments lose their top as you play up the keyboard, and not because
/// anything about the note changes. The body has a rolloff that stays where it
/// is, and playing higher walks the partials up through it. A kilohertz is
/// about C6, which puts the crossover in the middle of where anyone plays.
inline constexpr double kTrackingCornerHz = 1000.0;

/// How much of a partial survives at the pitch it is being played at.
///
/// Measured against the fundamental rather than absolutely, so the keyboard
/// stays even and only the spectrum thins. Without that it would be a shelf:
/// high notes quieter rather than duller, which is not the thing worth having.
///
/// The consequence is that a bass note keeps almost all of its series, since
/// most of it is below the corner, while a treble note whose fundamental is
/// already above the corner loses the full slope across every partial.
///
/// @param dbPerOctave  0 switches it off entirely.
/// How far MPE slide moves the keyboard tracking, in dB per octave.
///
/// Half the range of the control itself, so a finger has real authority
/// without a small movement crossing the whole of it.
inline constexpr float kSlideTrackDb = 6.0f;

inline float trackingGain(double partialHz, double fundamentalHz,
                          double dbPerOctave) noexcept {
  if (dbPerOctave <= 0.0)
    return 1.0f;

  const auto above = [](double hz) {
    return hz > kTrackingCornerHz ? std::log2(hz / kTrackingCornerHz) : 0.0;
  };

  const auto octaves = above(partialHz) - above(fundamentalHz);

  if (octaves <= 0.0)
    return 1.0f;

  // 6.0206 dB is a factor of two in amplitude.
  return (float)std::exp2(-dbPerOctave * octaves / 6.020599913279624);
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

  /// @param channel  the MIDI channel the note arrived on. Carried so the
  ///                  engine can tell two voices apart when a controller is
  ///                  giving every note its own channel. Ignored otherwise.
  void noteOn(int channel, int note, float velocity,
              const SynthParams &p) noexcept;
  /// @param velocity  how fast the key came up, 0 to 1. Scales each partial's
  ///                   key-off level by its own lift amount.
  void noteOff(float velocity = 0.5f) noexcept;
  void steal() noexcept; ///< fast fade-out so the voice can be reused

  /// Polyphonic aftertouch for this voice. Channel pressure arrives separately
  /// through SynthParams, and whichever is higher wins.
  void setPolyPressure(float v) noexcept { polyPressure = v; }

  /// MPE slide for this note, bipolar. Zero is the rest position, which is
  /// also what a controller that never sends CC74 leaves it at, since JUCE
  /// starts a note's timbre at the centre value.
  void setSlide(float v) noexcept { slide = v; }

  /// Pitch bend belonging to this note alone, in semitones.
  ///
  /// Added to whatever the wheel is doing rather than replacing it, so the two
  /// can coexist: an ordinary keyboard leaves this at zero and bends every
  /// voice together, and a controller that bends each finger separately leaves
  /// the wheel at zero and moves this. Nothing has to know which is in use.
  void setNoteBend(float semitones) noexcept { noteBendSemitones = semitones; }

  bool isActive() const noexcept { return active; }
  bool isReleasing() const noexcept { return released; }
  int getNote() const noexcept { return midiNote; }
  int getChannel() const noexcept { return midiChannel; }

  uint64_t getAge() const noexcept { return startOrder; }
  void setAge(uint64_t v) noexcept { startOrder = v; }

  /// Peak output amplitude of each partial during the last render call, for
  /// metering. Sampled once per control block, which is all a meter can show.
  const std::array<float, kNumHarmonics> &getPartialPeaks() const noexcept {
    return partialPeaks;
  }

  float getNoisePeak() const noexcept { return noisePeak; }

  /// Where each partial's envelope stands, signed by which half of it is
  /// running: positive on the way in, negative once the key is up.
  ///
  /// The sign carries the stage because the two are only ever read together,
  /// and a level of zero is a lamp that is off either way, so the one value
  /// nothing can be said about is also the one nothing needs to be said about.
  const std::array<float, kNumHarmonics> &getPartialEnvelopes() const noexcept {
    return partialEnvelopes;
  }

  /// How far the tremolo has pulled each partial down, 0 to 1. Zero is a
  /// partial with no tremolo on it as well as one at the top of its cycle,
  /// which is the same thing to look at.
  const std::array<float, kNumHarmonics> &getPartialTremolos() const noexcept {
    return partialTremolos;
  }

  /// Where pitch modulation and drift have this partial now, in cents.
  const std::array<float, kNumHarmonics> &getPartialPitches() const noexcept {
    return partialPitches;
  }

  float getNoiseEnvelope() const noexcept { return noiseEnvelope; }
  float getNoiseTremolo() const noexcept { return noiseTremolo; }

  /// Roughly how loud this voice was during the last render, for deciding
  /// which one it costs least to take away.
  ///
  /// The peaks are already to hand from the metering, so this is free. It is
  /// the loudest partial rather than the sum, which is the same choice the
  /// channel meters make and for the same reason: what you would notice going
  /// missing is the loudest thing in it.
  float lastLevel() const noexcept {
    auto loudest = noisePeak;

    for (auto peak : partialPeaks)
      loudest = std::max(loudest, peak);

    return loudest;
  }

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
    /// Likewise for the release, since the amount belongs to the note and the
    /// speed belongs to the gesture that ends it.
    float liftAmount = 0.0f;
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
    float liftAmount = 0.0f;
    float lastGain = 0.0f;
    bool gainPrimed = false;
  };

  std::array<Partial, kNumHarmonics> partials{};
  std::array<float, kNumHarmonics> partialPeaks{};

  /// Read by the strip lamps rather than by anything that makes sound. All
  /// three are values the render loop has already worked out for its own
  /// purposes, so capturing them is a store and nothing else.
  std::array<float, kNumHarmonics> partialEnvelopes{};
  std::array<float, kNumHarmonics> partialTremolos{};
  std::array<float, kNumHarmonics> partialPitches{};

  Noise noise;
  double noiseAmPhase = 0.0;
  float noisePeak = 0.0f;
  float noiseEnvelope = 0.0f;
  float noiseTremolo = 0.0f;
  float lowpassCoef = 0.1f;

  double sampleRate = 44100.0;
  double baseFreq = 440.0;
  int midiNote = -1;
  int midiChannel = 0;

  Xorshift rng;
  float noteBendSemitones = 0.0f;
  float polyPressure = 0.0f;
  float slide = 0.0f;

  /// A partial's tuning blend, with this note's slide folded in when slide is
  /// aimed there. Clamped, since the blend has no meaning outside nought to
  /// one: past either end the partial is no longer between the two tunings.
  float blendOf(const SynthParams &p, int i) const noexcept {
    const auto base = p.osc[(size_t)i].tuneBlend;

    if (p.global.slideDest != SlideDestination::Tuning)
      return base;

    return std::clamp(base + slide, 0.0f, 1.0f);
  }
  /// Aftertouch drives gain directly, so it needs its own smoothing. Seven bits
  /// arriving at MIDI rate would otherwise step audibly.
  float pressureSmoothed = 0.0f;
  float pressureCoef = 1.0f;

  bool active = false;
  bool released = false;
  uint64_t startOrder = 0;
};

} // namespace ovt
