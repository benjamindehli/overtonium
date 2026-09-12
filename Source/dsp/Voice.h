#pragma once

#include <array>
#include <cstdint>

#include "Drift.h"
#include "Lfo.h"
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

/// How far the strike amount can move a time, in octaves.
///
/// Eight, which is a range of 256 to 1, and it has to be that wide because the
/// attacks worth stretching are short. A partial set to 2 ms attacks in 2 ms
/// under a hard blow and takes half a second under the softest, which is the
/// difference between a struck note and a swelled one. Four octaves reached
/// only 32 ms from the same setting, and 2 ms against 32 ms is two kinds of
/// snap rather than two kinds of note: to hear the onset genuinely soften you
/// had to set an attack long enough that the hard note was no longer punchy,
/// which is the wrong trade to have to make.
///
/// The delay takes the same figure in the other direction, so 400 ms of it
/// comes in to under two milliseconds under a hard blow, which is the same
/// thing said at the front: hit it hard and the partial is simply there.
inline constexpr float kStrikeOctaves = 8.0f;

/// How much of the strike amount a blow of this speed earns, 0 to 1.
///
/// The shape both halves are built on. Zero amount earns nothing whatever the
/// velocity, and the sign decides which end of the keyboard's travel the full
/// amount lands on, which is how the velocity row already reads.
inline float strikeReach(float amount, float velocity) noexcept {
  const auto a = std::clamp(amount, -1.0f, 1.0f);
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return a >= 0.0f ? a * (1.0f - v) : -a * v;
}

/// What the strike amount does to a partial's attack time.
///
/// Velocity only ever lengthens the attack, never shortens it past what the
/// knob says, so the ATTACK row goes on meaning the fastest this partial gets
/// and turning the amount up cannot outrun it. A positive amount spends that
/// on the quiet end: a note at full velocity attacks exactly as set, and the
/// onset softens the lighter it is played. A negative amount is the mirror,
/// anchored at the quiet end instead.
///
/// Octaves rather than a straight multiply, because attack time is heard in
/// ratios: the step from 5 to 10 ms is the audible change that the step from
/// 2 to 2.005 s is not.
///
/// The figure this returns is a ratio and can be large. What it is allowed to
/// do to a real attack is bounded by struckAttack, which is where the two meet.
inline float strikeAttackScale(float amount, float velocity) noexcept {
  return std::exp2(kStrikeOctaves * strikeReach(amount, velocity));
}

/// A partial's attack once the blow has been folded in, in seconds.
///
/// Stopped at the top of the ATTACK row's own range rather than left to run.
/// Two hundred and fifty-six times a short attack is a long one and exactly
/// what the control is for, but the same ratio on an attack that was already
/// long is half a minute, which is not a setting anybody reached for and not
/// one the knob alone could have produced. So the blow can take the onset
/// anywhere the knob could have gone and no further.
///
/// The cost is that a full amount on an attack above about 20 ms gives the very
/// softest notes the same ceiling rather than a longer one each. That is the
/// right end to lose resolution at: they are already slower than anything the
/// patch was built around.
inline float struckAttack(float attack, float scale) noexcept {
  return std::min(attack * scale, kMaxAttackSeconds);
}

/// What the same amount does to the delay before that attack.
///
/// The other way round, and deliberately so. Velocity only ever pulls the
/// delay in, never pushes it out, so the DELAY row goes on meaning the latest
/// this partial ever arrives. The two then say the same thing about a hard
/// blow from both ends: it arrives sooner and it arrives faster, which is what
/// striking anything harder does, while a light touch lets the partial come in
/// late and open slowly.
///
/// Built from the same reach read from the opposite end of the travel, since a
/// positive amount has to spend itself on hard notes here where the attack
/// spends it on soft ones. It needs no ceiling of its own: shortening runs
/// towards nothing, and nothing is a delay the knob can already be set to.
inline float strikeDelayScale(float amount, float velocity) noexcept {
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return std::exp2(-kStrikeOctaves * strikeReach(amount, 1.0f - v));
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
  void noteOff() noexcept;

  /// Moves a sounding note to a different key without starting it again.
  ///
  /// Legato: the envelopes, the phases and the drift all carry on where they
  /// were, and only the pitch changes. The velocity of the note that began the
  /// phrase is kept, since the key that would set a new one was never
  /// released, and taking a fresh one would make a legato run change timbre
  /// under the fingers.
  void noteOnLegato(int channel, int note, const SynthParams &p) noexcept;

  /// The same, for a caller that has worked out the frequency already. A
  /// note-off has no parameters to hand and still has to move the phrase to
  /// whichever key is left holding it.
  void retune(int channel, int note, double frequency) noexcept;
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
    Lfo pitchLfo;
    Lfo ampLfo;
    Envelope env;
    SmoothRandom drift;
    /// Carried across control blocks so gain never steps.
    float lastGain = 0.0f;
    /// Latched at note-on from this strip's own velocity sensitivity.
    float velGain = 1.0f;
    /// What the strike amount made of that same velocity, as multipliers on
    /// the delay and the attack. Latched for the same reason the gain is: the
    /// blow has already landed, and moving the knob afterwards cannot change
    /// how hard it was.
    float delayScale = 1.0f;
    float attackScale = 1.0f;
    bool gainPrimed = false;
  };

  /// The noise channel runs alongside the partials with its own envelope and
  /// its own random stream, so each note gets its own noise rather than every
  /// voice layering the identical signal.
  struct Noise {
    Envelope env;
    Xorshift rng;
    Lfo ampLfo;
    float lowpassState = 0.0f;
    float velGain = 1.0f;
    float delayScale = 1.0f;
    float attackScale = 1.0f;
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
