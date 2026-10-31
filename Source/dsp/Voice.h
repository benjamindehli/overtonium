#pragma once

#include <array>
#include <cstdint>

#include "Drift.h"
#include "Envelope.h"
#include "Harmonics.h"
#include "KarplusString.h"
#include "Lfo.h"
#include "Params.h"
#include "Velocity.h"

namespace ovt {

/// Fades a partial out as it approaches Nyquist, so high partials of high
/// notes fade smoothly instead of aliasing.
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

/// Where the keyboard tracking rolloff sits, in Hz. A fixed corner that
/// higher notes walk their partials up through, like an instrument's body.
inline constexpr double kTrackingCornerHz = 1000.0;

/// How far MPE slide moves the keyboard tracking, in dB per octave. Half the
/// range of the control itself.
inline constexpr float kSlideTrackDb = 6.0f;

/// How much of a partial survives at the pitch it is being played at.
/// Measured against the fundamental, so the keyboard stays even in level and
/// only the spectrum thins.
///
/// @param dbPerOctave  0 switches it off entirely.
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

/// Where a slide axis has moved to, against where this note found it, in
/// -1 to 1.
///
/// One scale for both directions, the larger of the two distances from the
/// rest. Scaling each direction on its own would make the short side a
/// hair-trigger when the rest sits near one end of the axis.
inline float slideDisplacement(float rest, float now) noexcept {
  const auto from = std::clamp(rest, -1.0f, 1.0f);
  const auto to = std::clamp(now, -1.0f, 1.0f);

  // Between 1 and 2, so never near zero and never in need of a guard.
  const auto room = std::max(1.0f - from, from + 1.0f);

  return (to - from) / room;
}

/// How far ahead the amplitude reads its shape, in turns. A quarter turns
/// Sine into a cosine, so a note begins at full level and dips.
///
/// Here rather than in the voice because the engine reads the same shapes
/// when a channel's modulator is shared, and the two have to agree.
inline constexpr double kAmpShapeOffset = 0.25;

/// The modulator values every voice on a channel reads, when that channel's
/// modulator is shared rather than one per note.
///
/// Worked out by the engine before any voice runs, since each voice renders
/// a whole buffer and cannot step a shared phase itself. One value per
/// control block boundary, so the gain ramp works as it does off a voice's
/// own modulator. A null pointer means sharing is off. See
/// SynthEngine::advanceSharedModulators.
struct SharedModulation {
  const float *pitch = nullptr;
  const float *amp = nullptr;

  /// How many boundaries each channel holds, so channel i starts at i * stride.
  /// The noise channel comes after the 32 partials.
  int stride = 0;

  /// Where the tonewheel organ's motor is in its turn at the start of this
  /// chunk, and how far it turns each sample. One motor drives every wheel, so
  /// every voice reads the same one. Only Wheel listens. See kWheelMotorHz.
  double motorTurns = 0.0;
  double motorStep = 0.0;
};

/// What a voice needs to know to glide. The engine keeps a copy so a
/// note-off, which carries no parameters, can start one too.
struct GlideSettings {
  std::array<float, kNumHarmonics> seconds{};
  GlideMode mode = GlideMode::Rate;

  static GlideSettings of(const SynthParams &p) noexcept {
    GlideSettings g;

    for (int i = 0; i < kNumHarmonics; ++i)
      g.seconds[(size_t)i] = p.osc[(size_t)i].glideSeconds;

    g.mode = p.global.glideMode;
    return g;
  }
};

/// One polyphonic voice: 32 independently tuned, enveloped and modulated sine
/// partials.
class Voice {
public:
  /// Where each partial's note sounds, in semitones above 1 Hz. Absolute
  /// rather than against the note, so a glide can start from a voice playing
  /// a different one.
  using Pitches = std::array<double, kNumHarmonics>;
  /// LFOs, envelope-driven gain interpolation and the Nyquist guard update once
  /// per control block rather than once per sample. 32 frames is ~0.7 ms at
  /// 44.1 kHz.
  static constexpr int kControlBlock = 32;

  /// @param seed  distinguishes this voice's random stream from its siblings.
  void prepare(double newSampleRate, uint32_t seed) noexcept;
  void reset() noexcept;

  /// Moves the voice to a different render rate without disturbing it. Every
  /// rate-dependent value is a coefficient recomputed from the stored rate, so
  /// this can happen under a sounding note. Used by the lo-fi rate setting.
  void setRenderRate(double newSampleRate) noexcept;

  /// @param channel  the MIDI channel the note arrived on, so the engine can
  ///                  tell voices apart under MPE.
  ///
  /// @param frequency  where the engine has tuned this key, or zero to tune it
  ///                   from the temperament in the parameters. Adaptive
  ///                   depends on what else is sounding, which only the
  ///                   engine can see.
  void noteOn(int channel, int note, float velocity, const SynthParams &p,
              double frequency = 0.0) noexcept;
  /// @param lift  see Envelope::noteOff.
  void noteOff(float lift = 1.0f) noexcept;

  /// Moves a sounding note to a different key without starting it again, for
  /// legato. Envelopes, phases and drift carry on. The phrase's first velocity
  /// is kept, so a legato run does not change timbre.
  void retune(int channel, int note, double frequency) noexcept;

  /// Moves a sounding note to another version of the same key, sliding there
  /// over the time given, or at once for none. For the adaptive temperament.
  /// The slide starts where the note is, and the frequency the engine reads
  /// back is the target. See SynthEngine::settleAdaptive.
  void retuneTo(double frequency, double seconds) noexcept;
  void steal() noexcept; ///< fast fade-out so the voice can be reused

  /// The same fade over a time of the caller's choosing, for a note that has
  /// handed its phrase on and should go quietly.
  void fade(float seconds) noexcept;

  /// Carries on where another voice is: every envelope at its stage and level,
  /// the modulators and drift mid-cycle, and the phrase's velocity. Called on
  /// a voice that has just been given a note. The pitch is left to
  /// startGlide.
  void takeOver(const Voice &from) noexcept;

  /// Where every partial's note is sounding right now, glide included, so a
  /// note taking over from this one can start from exactly here.
  Pitches notePitches() const noexcept;

  /// The same pitch for all of them, for a note gliding from one that has
  /// finished and left nothing but its frequency behind.
  static Pitches uniformPitches(double frequency) noexcept;

  /// Sets every partial gliding from where it was to this voice's note, each
  /// over its own channel's time. Call after the note has been set. A channel
  /// with no glide time sounds at the note.
  void startGlide(const Pitches &from, const GlideSettings &glide) noexcept;

  double getFrequency() const noexcept { return baseFreq; }

  /// Polyphonic aftertouch for this voice. Channel pressure arrives separately
  /// through SynthParams, and whichever is higher wins.
  void setPolyPressure(float v) noexcept { polyPressure = v; }
  float getPolyPressure() const noexcept { return polyPressure; }

  /// MPE slide for this note, as a displacement from where this note's slide
  /// began rather than as a position on the controller.
  ///
  /// Relative because controllers disagree about the rest position. Some rest
  /// at the centre and others at one end, and read absolutely the second kind
  /// would jump to the far end of the slide as soon as the axis engages. The
  /// first value a note is given becomes its zero. See slideDisplacement.
  void setSlide(float v) noexcept {
    if (!slideRested) {
      slideRest = std::clamp(v, -1.0f, 1.0f);
      slideRested = true;
    }

    slide = slideDisplacement(slideRest, v);
  }

  /// Pitch bend belonging to this note alone, in semitones. Added to the
  /// wheel, so an ordinary keyboard and a per-note controller both work.
  void setNoteBend(float semitones) noexcept { noteBendSemitones = semitones; }
  float getNoteBend() const noexcept { return noteBendSemitones; }

  bool isActive() const noexcept { return active; }
  bool isReleasing() const noexcept { return released; }
  int getNote() const noexcept { return midiNote; }
  int getChannel() const noexcept { return midiChannel; }

  /// The order the note started in, which the engine counts up, for finding
  /// the oldest and newest voices.
  uint64_t getAge() const noexcept { return startOrder; }
  void setAge(uint64_t v) noexcept { startOrder = v; }

  /// Peak output amplitude of each partial during the last render call, for
  /// metering. Sampled once per control block.
  const std::array<float, kNumHarmonics> &getPartialPeaks() const noexcept {
    return partialPeaks;
  }

  float getNoisePeak() const noexcept { return noisePeak; }

  /// Where each partial's envelope stands, signed by which half of it is
  /// running: positive on the way in, negative once the key is up.
  const std::array<float, kNumHarmonics> &getPartialEnvelopes() const noexcept {
    return partialEnvelopes;
  }

  /// How far the tremolo has pulled each partial down, 0 to 1.
  const std::array<float, kNumHarmonics> &getPartialTremolos() const noexcept {
    return partialTremolos;
  }

  /// Where pitch modulation and drift have this partial now, in cents.
  const std::array<float, kNumHarmonics> &getPartialPitches() const noexcept {
    return partialPitches;
  }

  /// What the VELOCITY row has left of each partial, 0 to 1. Full is a
  /// partial the velocity has not reduced. Latched at note-on.
  const std::array<float, kNumHarmonics> &
  getPartialVelocities() const noexcept {
    return partialVelocities;
  }

  /// Room for one power per control block of the longest call the engine
  /// makes. See SynthEngine::kModChunk.
  static constexpr int kMaxPowerBlocks = 2048 / kControlBlock + 1;

  /// How loud this note's partials were together in each control block of
  /// the last call, as the sum of their squared amplitudes. Filled only on FM
  /// and PD, whose noise the engine sizes from every note at once. See
  /// SynthEngine::renderHiss.
  const std::array<float, kMaxPowerBlocks> &getBlockPowers() const noexcept {
    return blockPowers;
  }

  /// What the AFTERTOUCH row is putting into each partial, 0 to 1, from the
  /// row's amount against the pressure. Unsigned.
  const std::array<float, kNumHarmonics> &getPartialPressures() const noexcept {
    return partialPressures;
  }

  float getNoiseEnvelope() const noexcept { return noiseEnvelope; }
  float getNoiseTremolo() const noexcept { return noiseTremolo; }

  /// The same two for the noise channel, which has a velocity and an
  /// aftertouch amount of its own.
  float getNoiseVelocity() const noexcept { return noiseVelocity; }
  float getNoisePressure() const noexcept { return noisePressure; }

  /// Roughly how loud this voice was during the last render, for choosing
  /// which one to steal. The loudest partial, as the channel meters use.
  float lastLevel() const noexcept {
    auto loudest = noisePeak;

    for (auto peak : partialPeaks)
      loudest = std::max(loudest, peak);

    return loudest;
  }

  /// Adds this voice into the (already-sized) stereo buffers. Master gain is
  /// applied downstream by the engine.
  /// @param shared  the modulator values this channel's whole keyboard is
  /// reading, where it is reading any. See SharedModulation.
  void render(float *left, float *right, int numSamples, const SynthParams &p,
              const SharedModulation &shared = {}) noexcept;

private:
  /// Adds the noise channel and its string into the buffers.
  void renderNoise(float *left, float *right, int len, const SynthParams &,
                   float pressure, const SharedModulation &shared,
                   int block) noexcept;

  /// Adds the leaking wheels on Wheel. foldAliases is true on the lo-fi rate,
  /// where the Nyquist fade is skipped.
  void renderLeaks(float *left, float *right, int len, double keyFreq,
                   double power, bool foldAliases) noexcept;

  /// Everything one partial of this note carries from block to block.
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
    /// the delay and the attack. Latched at note-on like the gain.
    float delayScale = 1.0f;
    float attackScale = 1.0f;

    /// The attack time the knob and that velocity come to, and the knob value
    /// it was worked out for, so it is recomputed only when the knob moves.
    float onsetFor = 0.0f;
    float onsetSeconds = 0.0f;
    bool onsetStale = true;
    bool gainPrimed = false;

    /// The pitch the lamp has settled at, in semitones off the note, which is
    /// where the Bulb character measures its amplitude error from. Primed on
    /// the first block of a note so one cannot begin already sagging. See
    /// Voice::render.
    double bulbSettled = 0.0;
    bool semisPrimed = false;

    /// The table this partial was reading last block, so a block that changes
    /// tables can cross-fade rather than step. Null until it has read one.
    const Wave *lastWave = nullptr;

    /// How far this partial still is from its note, in semitones, and how much
    /// of that it closes each control block. Straight lines in pitch, stepped
    /// per block like the modulators.
    double glideSemis = 0.0;
    double glideStep = 0.0;

    /// Where the shaft under this partial's tonewheel is in its turn. Drawn
    /// when a note starts, and only read on Wheel. See wheelShaftHz.
    double shaftTurns = 0.0;

    /// How far this partial's oscillator has wandered from its pitch, in
    /// cents, on VCO. Kept from note to note, since it is the oscillator that
    /// wanders and not the note. See kVcoWanderCents.
    double wanderCents = 0.0;
  };

  /// The noise channel, with its own envelope and random stream, so each note
  /// gets its own noise rather than every voice layering the same signal.
  struct Noise {
    Envelope env;
    KarplusString string;

    /// Whether the string is still sounding. It rings on after the noise that
    /// struck it has gone idle, and the voice stays alive while this is true.
    bool stringAlive = false;

    /// A damper brought down on the string by a steal or a fade, in seconds,
    /// or zero for none.
    float damperSeconds = 0.0f;
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

  /// Read by the strip lamps, not by anything that makes sound. Each is a
  /// value the render loop already has, so capturing it is only a store.
  std::array<float, kNumHarmonics> partialEnvelopes{};
  std::array<float, kNumHarmonics> partialTremolos{};
  std::array<float, kNumHarmonics> partialPitches{};
  std::array<float, kNumHarmonics> partialVelocities{};
  std::array<float, kNumHarmonics> partialPressures{};

  Noise noise;

  /// The wheels leaking into this note on Wheel, one per entry of
  /// kWheelLeaks. Each has its own phase, and its level slides across a block
  /// like a partial's does.
  struct Leak {
    double phase = 0.0;
    float lastGain = 0.0f;
  };

  std::array<Leak, kWheelLeaks.size()> leaks{};

  std::array<float, kMaxPowerBlocks> blockPowers{};

  float noisePeak = 0.0f;
  float noiseEnvelope = 0.0f;
  float noiseTremolo = 0.0f;
  float noiseVelocity = 0.0f;
  float noisePressure = 0.0f;
  float lowpassCoef = 0.1f;

  double sampleRate = 44100.0;
  double baseFreq = 440.0;

  /// How far a retune by the adaptive temperament still has to slide, in
  /// semitones, and how far it goes in a sample. The same for every partial.
  /// See retuneTo.
  double retuneSemis = 0.0;
  double retuneStep = 0.0;
  int midiNote = -1;
  int midiChannel = 0;

  Xorshift rng;

  /// Where each note's tonewheel shafts start, on a stream of their own so
  /// that drawing them moves nothing that rng draws for every other character.
  Xorshift wheelRng;

  /// The VCO's wander, on a stream of its own for the same reason, and only
  /// drawn on VCO.
  Xorshift vcoRng;

  float noteBendSemitones = 0.0f;
  float polyPressure = 0.0f;
  float slide = 0.0f;

  /// Where this note's slide axis started, and whether it has been set yet.
  /// See setSlide.
  float slideRest = 0.0f;
  bool slideRested = false;

  /// A partial's tuning blend, with this note's slide folded in when slide is
  /// aimed there. Clamped to 0 to 1.
  float blendOf(const SynthParams &p, int i) const noexcept {
    const auto base = p.osc[(size_t)i].tuneBlend;

    if (p.global.slideDest != SlideDestination::Tuning)
      return base;

    return std::clamp(base + slide, 0.0f, 1.0f);
  }
  /// Aftertouch drives gain directly, so it needs its own smoothing, or
  /// 7-bit MIDI values would step audibly.
  float pressureSmoothed = 0.0f;
  float pressureCoef = 1.0f;

  bool active = false;
  bool released = false;
  uint64_t startOrder = 0;
};

} // namespace ovt
