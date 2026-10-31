#pragma once

#include "OutputStage.h"

#include <array>

#include "Character.h"
#include "Harmonics.h"
#include "Lfo.h"
#include "Temperament.h"

namespace ovt {

/// Where MPE slide goes. Brightness is the default. Tuning slides that note
/// alone between equal temperament and just intonation.
///
/// Declared here because the voice branches on it and the DSP core does not
/// include JUCE.
enum class SlideDestination { Off = 0, Brightness, Tuning };

/// When a new note glides from the one before it. Always glides from the
/// last note played even after its key is released. Legato glides only while
/// another key is still held.
enum class GlideTrigger { Always = 0, Legato };

/// What a channel's glide time means. Rate is the time an octave takes, so a
/// wide leap takes longer. Time is the time any interval takes.
enum class GlideMode { Rate = 0, Time };

inline const char *glideTriggerName(GlideTrigger t) {
  return t == GlideTrigger::Legato ? "Legato" : "Always";
}

inline const char *glideModeName(GlideMode m) {
  return m == GlideMode::Time ? "Fixed time" : "Fixed rate";
}

/// A per-block snapshot of one channel strip. Plain data, since the DSP core
/// does not use JUCE.
struct OscParams {
  // Pitch
  float tuneBlend = 0.0f; ///< 0 = equal temperament, 1 = just intonation
  float pmRateHz = 4.0f;
  LfoShape pmShape = LfoShape::Sine;
  float pmDepthCents = 0.0f;
  /// Depth of the smooth random pitch wander, in cents. Each partial of each
  /// note gets its own rate, so nothing locks together.
  float driftCents = 0.0f;
  /// How long this partial takes to reach a new note's pitch, in seconds, or
  /// in seconds per octave under GlideMode::Rate. Zero is no glide. Per
  /// partial, so the series can arrive at different times. See
  /// Voice::startGlide.
  float glideSeconds = 0.0f;

  // Amplitude
  /// How much strike velocity moves the front of the envelope, -1 to 1.
  /// Positive makes a harder note arrive sooner and faster, negative inverts
  /// it, zero ignores velocity. See strikeAttackScale and strikeDelayScale.
  float strikeAmount = 0.0f;
  /// Held silent before the attack starts, in seconds.
  float delay = 0.0f;
  /// Seconds, or below zero where in its cycle the partial starts with no
  /// attack at all, -1 being its peak. See onsetOctaves.
  float attack = 0.005f;
  float decay = 0.400f;
  float sustain = 1.0f; ///< 0..1
  float release = 0.400f;
  /// Where the envelope goes when the key is released, and how long it takes
  /// to get there. Above the sustain is a release click or bloom, below it is
  /// a fast drop into a long tail. Zero skips the stage.
  float swell = 0.005f;
  float offLevel = 0.0f;
  float amRateHz = 4.0f;
  LfoShape amShape = LfoShape::Sine;
  float amDepth = 0.0f; ///< 0..1 tremolo depth
  /// How strongly key velocity scales this partial, -1 to 1. Positive means
  /// harder is louder, negative means softer is louder.
  float velAmount = 0.7f;
  /// How much channel or polyphonic aftertouch moves this partial's level,
  /// -1 to 1. It adds to the fader rather than scaling it, so a strip at zero
  /// can be faded in by pressure.
  float atAmount = 0.0f;
  /// Linear gain, mute/solo already folded in by the caller.
  float volume = 0.0f;
  /// Where this partial sits in the field, -1 hard left to +1 hard right.
  float pan = 0.0f;

  /// False when muted, or when some other strip is soloed.
  bool audible = true;
};

/// The noise channel. Like a strip but with no pitch, so no tuning, pitch
/// modulation or drift. Colour tilts the spectrum from dark to bright.
struct NoiseParams {
  float colour = 0.5f; ///< 0 dark, 0.5 flat, 1 bright

  float strikeAmount = 0.0f;
  float delay = 0.0f;
  float attack = 0.005f;
  float decay = 0.600f;
  float sustain = 1.0f;
  float swell = 0.005f;
  float offLevel = 0.0f;
  float release = 0.400f;
  float amRateHz = 4.0f;
  LfoShape amShape = LfoShape::Sine;
  float amDepth = 0.0f;
  float velAmount = 0.7f;
  float atAmount = 0.0f;
  float volume = 0.0f;
  float pan = 0.0f;

  /// The string the noise strikes, 0 for the noise alone to 1 for the string
  /// alone. See KarplusString.
  float stringAmount = 0.0f;
  float stringDecay = 1.5f;  ///< seconds, how long it rings with the key down
  float stringDamp = 0.4f;   ///< 0 bright to 1 dark
  float stringPoint = 0.25f; ///< 0 struck near the end to 1 in the middle

  bool audible = true;
};

/// A per-block snapshot of everything that is not a channel strip.
struct GlobalParams {
  float masterGain = 0.25f; ///< linear

  /// How the keyboard is tuned, as opposed to how the partials above each note
  /// are. See Temperament.h.
  Temperament temperament = Temperament::Equal;
  int tuningRoot = 0;         ///< pitch class the temperament is built on
  double referenceHz = 440.0; ///< where A sits, whatever the temperament
  /// How long a held note takes to slide to its new version when the adaptive
  /// temperament retunes it. See SynthEngine::settleAdaptive.
  double adaptiveRetuneSeconds = 0.03;
  /// Whether Adaptive tunes a minor seventh as the harmonic seventh, 7:4 above
  /// its root, where nothing else in the chord is spoiled by it. Off, it takes
  /// the size the chain of fifths gives it.
  bool harmonicSevenths = true;
  /// Whether Adaptive keeps the held chord as near home as it can before
  /// keeping held notes still, so the keyboard does not drift.
  bool adaptiveHome = false;

  float bendSemitones = 0.0f; ///< current pitch-bend offset
  float aftertouch = 0.0f;    ///< current channel pressure, 0..1
  /// Whether a partial starting from silence starts its cycle at a rising zero
  /// crossing, the softest onset. Off, it carries on from wherever its phase
  /// was.
  bool phaseReset = true;
  /// Inharmonicity, as cents of displacement on the 32nd partial. Zero is the
  /// plain harmonic series. See inharmonicCents.
  float stretchCents = 0.0f;
  /// Keyboard tracking, in dB per octave above the rolloff. Thins the series
  /// as you play up. Zero is off. See trackingGain.
  float trackDbPerOctave = 0.0f;

  /// Which oscillator every partial is, one for all 32. See Character.h.
  Character character = Character::Pure;

  /// Where a note's MPE slide goes.
  SlideDestination slideDest = SlideDestination::Brightness;
  /// A warped record under the whole instrument, 0 to 1. Sits before the echo,
  /// so the repeats inherit it. See Wobble.h.
  float wobbleAmount = 0.0f;

  /// How hard the summed series runs into the bus stage. Not a parameter: the
  /// plugin fills it from BusDrive::amountFor. A field so that tests can turn
  /// it off.
  float busDrive = 0.0f;
  /// Whether the output stage shapes the sum. See OutputStage.
  bool safetyClip = true;

  /// Whether the output stage may look ahead. Off, the default, saves two
  /// milliseconds of latency at the price of a rougher Limiter.
  bool lookahead = false;

  /// Which output stage type, when it is on.
  ClipType clipType = ClipType::Soft;
  /// Whether striking a key that is already sounding takes over its voice
  /// rather than starting a second one. See SynthEngine::noteOnImpl.
  bool oneVoicePerKey = true;

  /// Whether a channel's modulator is shared by the whole keyboard rather
  /// than one per note. On, a note joins the phase already running, so a chord
  /// moves as one. One switch for each of the two modulators. See
  /// SynthEngine::advanceSharedModulators.
  bool pitchModInPhase = false;
  bool ampModInPhase = false;

  /// When a note glides, and what the glide times on the channels mean. See
  /// GlideTrigger and GlideMode.
  GlideTrigger glideTrigger = GlideTrigger::Always;
  GlideMode glideMode = GlideMode::Rate;
};

/// Which delay the repeats come out of.
///
/// All types are declared, since a host stores a choice as a fraction of
/// the range and adding an entry later would move every automation lane.
enum class EchoType { Tape, Bucket, Digital, NumTypes };

inline const char *echoTypeName(EchoType t) {
  switch (t) {
  case EchoType::Tape:
    return "Tape";
  case EchoType::Bucket:
    return "Bucket brigade";
  case EchoType::Digital:
    return "Digital";

  // Listed rather than left to a default, so adding one is a compiler error
  // here until it has a name.
  case EchoType::NumTypes:
    break;
  }

  return "Tape";
}

/// The name in one word, for the button on the bar.
inline const char *echoTypeShortName(EchoType t) {
  return t == EchoType::Bucket ? "BBD" : echoTypeName(t);
}

/// The echo, which sits across the whole instrument.
struct EchoParams {
  bool enabled = false;

  /// Which machine the repeats come from. The on switch is separate, so a
  /// preset without a type gets Tape.
  EchoType type = EchoType::Tape;
  float mix = 0.25f;         ///< 0 dry, 1 fully wet
  float timeSeconds = 0.35f; ///< distance between the heads
  float feedback = 0.35f;    ///< 0..0.95, how much goes round again
  /// How worn the machine is, 0 to 1.
  ///
  /// On tape: less top end on every pass, more motor wander and more
  /// compression when driven. On a bucket brigade: darker, dirtier on every
  /// pass, and more compander hiss.
  float age = 0.35f;
};

/// Which reverb the tail comes out of. All types are declared, for the same
/// reason as EchoType.
enum class ReverbType { Room, Plate, Spring, NumTypes };

inline const char *reverbTypeName(ReverbType t) {
  switch (t) {
  case ReverbType::Room:
    return "Room";
  case ReverbType::Plate:
    return "Modulated Plate";
  case ReverbType::Spring:
    return "Spring";

  case ReverbType::NumTypes:
    break;
  }

  return "Room";
}

/// The name for the button on the bar, shortened where the menu name is too
/// long.
inline const char *reverbTypeShortName(ReverbType t) {
  return t == ReverbType::Plate ? "Plate" : reverbTypeName(t);
}

/// The reverb, which is one of three machines sized and damped from the panel.
struct ReverbParams {
  bool enabled = false;

  /// Which machine the tail comes from. The on switch is separate, so a preset
  /// without a type gets Room.
  ReverbType type = ReverbType::Room;
  float mix = 0.25f;
  /// RT60. The room size follows from it.
  float decaySeconds = 2.0f;
  float damping = 0.5f; ///< how fast the top end dies away in the tail
  float preDelaySeconds = 0.0f;
};

/// Converter quality, downwards. Both default to off, meaning the host's
/// rate and no quantiser. See SynthEngine::renderVoices.
struct LofiParams {
  /// Render rate in Hz, or 0 for the host's own.
  double rateHz = 0.0;

  /// Quantiser resolution in bits, or 0 for none. Signed, so 8 bits is 256
  /// codes across the range.
  int bits = 0;
};

/// What the resonator's strings are tuned to. See SympatheticStrings.
///
/// A new mode goes on the end, since the index is what a preset stores.
enum class ResonatorMode {
  Off,
  /// A piano's: one string a key, the held keys' undamped, every one with the
  /// sustain pedal down.
  Piano,
  /// Piano with the sustain pedal always down, so every string is undamped.
  Open,
  /// One for each note of the octave from C3, always undamped.
  Octave,
  /// The tuning root's harmonic series from its second octave, pure, always
  /// undamped.
  Series,
  NumModes
};

inline const char *resonatorModeName(ResonatorMode m) {
  switch (m) {
  case ResonatorMode::Off:
    return "Off";
  case ResonatorMode::Piano:
    return "Piano";
  case ResonatorMode::Open:
    return "Open";
  case ResonatorMode::Octave:
    return "Octave";
  case ResonatorMode::Series:
    return "Series";

  case ResonatorMode::NumModes:
    break;
  }

  return "Off";
}

/// The resonator: 88 sympathetic strings that resonate with what is played.
struct ResonatorParams {
  ResonatorMode mode = ResonatorMode::Off;
  float mix = 0.5f;          ///< the instrument alone at 0, the resonator at 1
  float decaySeconds = 4.0f; ///< how long an undamped one rings
  float damping = 0.4f;      ///< 0 bright to 1 dark
};

/// Everything the engine reads for one block.
struct SynthParams {
  std::array<OscParams, kNumHarmonics> osc{};
  NoiseParams noise{};
  GlobalParams global{};
  EchoParams echo{};
  ReverbParams reverb{};
  LofiParams lofi{};
  ResonatorParams resonator{};

  /// Whether a sustain pedal is down on any channel. Set by whoever handles
  /// the MIDI, since under MPE the pedal never reaches the engine as a pedal.
  bool pedalDown = false;
};

} // namespace ovt
