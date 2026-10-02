#pragma once

#include "OutputStage.h"

#include <array>

#include "Character.h"
#include "Harmonics.h"
#include "Lfo.h"
#include "Temperament.h"

namespace ovt {

/// Where MPE slide goes, which is the forward and back axis under a finger on
/// a Seaboard or a Linnstrument.
///
/// Brightness is what a player's hands expect from it, so it is the default.
/// Tuning is the one only this instrument can offer: push a finger forward and
/// that note alone slides between equal temperament and just intonation while
/// the rest of the chord stays where it is.
///
/// Declared here rather than beside the parameter it comes from, because the
/// voice branches on it and the DSP core is not allowed to include JUCE.
enum class SlideDestination { Off = 0, Brightness, Tuning };

/// A per-block snapshot of one channel strip. Deliberately plain data: the DSP
/// core never touches JUCE, which keeps it unit-testable and portable.
struct OscParams {
  // Pitch
  float tuneBlend = 0.0f; ///< 0 = equal temperament, 1 = just intonation
  /// Where in its own cycle this partial starts, 0 to 1 of a turn, when phase
  /// reset is on. Zero is a rising zero crossing, which is the softest onset
  /// available: the partial cannot reach its own peak until a quarter of its
  /// period has passed, which below about 500 Hz is longer than any attack
  /// setting. A quarter turn starts it at the peak instead.
  float startPhase = 0.0f;
  float pmRateHz = 4.0f;
  LfoShape pmShape = LfoShape::Sine;
  float pmDepthCents = 0.0f;
  /// Depth of the smooth random pitch wander, in cents. Each partial of each
  /// note gets its own rate, so nothing ever locks together.
  float driftCents = 0.0f;

  // Amplitude
  /// How much the speed you strike the key at moves the front of the envelope,
  /// -1 to 1. Positive brings the delay in and stretches the attack as the blow
  /// softens, so a hard note arrives sooner and faster, which is what striking
  /// anything harder does. Negative inverts it. Zero ignores velocity, which is
  /// what the envelope did before it had this. See strikeAttackScale and
  /// strikeDelayScale.
  float strikeAmount = 0.0f;
  /// Held silent before the attack starts, in seconds. Staggering this across
  /// the series makes the spectrum unfold rather than arrive all at once.
  float delay = 0.0f;
  float attack = 0.005f; ///< seconds
  float decay = 0.400f;
  float sustain = 1.0f; ///< 0..1
  float release = 0.400f;
  /// Where the envelope goes when the key is let go, and how long it takes to
  /// get there. A level above the sustain is a release click or a bloom, below
  /// it is the fast initial drop into a long tail that a struck string has.
  /// Zero skips the stage, which is what it did before it had one.
  float swell = 0.005f;
  float offLevel = 0.0f;
  float amRateHz = 4.0f;
  LfoShape amShape = LfoShape::Sine;
  float amDepth = 0.0f; ///< 0..1 tremolo depth
  /// How strongly key velocity scales this partial, -1 to 1. Positive means
  /// harder is louder, the way most acoustic instruments behave. Negative
  /// inverts it, so the partial is loudest when played softly. Setting some
  /// partials positive and others negative crossfades between two timbres
  /// across the velocity range.
  float velAmount = 0.7f;
  /// How much channel or polyphonic aftertouch moves this partial's level,
  /// -1 to 1. It adds to the fader rather than scaling it, so a strip parked at
  /// zero can be faded in entirely by leaning on the key, and a negative amount
  /// fades an open strip out again.
  float atAmount = 0.0f;
  /// Linear gain, mute/solo already folded in by the caller.
  float volume = 0.0f;
  /// Where this partial sits in the field, -1 hard left to +1 hard right.
  float pan = 0.0f;

  /// False when muted, or when some other strip is soloed.
  bool audible = true;
};

/// The noise channel.
///
/// It behaves like a strip in every respect except pitch, of which it has
/// none, so it carries no tuning, pitch modulation or drift. Colour takes
/// their place: it tilts the spectrum from dark rumble through flat to bright
/// hiss, which is the difference between adding breath and adding fizz.
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

  bool audible = true;
};

struct GlobalParams {
  float masterGain = 0.25f; ///< linear

  /// How the keyboard is tuned, as opposed to how the partials above each note
  /// are. See Temperament.h.
  Temperament temperament = Temperament::Equal;
  int tuningRoot = 0;         ///< pitch class the temperament is built on
  double referenceHz = 440.0; ///< where A sits, whatever the temperament

  float bendSemitones = 0.0f; ///< current pitch-bend offset
  float aftertouch = 0.0f;    ///< current channel pressure, 0..1
  bool phaseReset = true; ///< reset partial phase on note-on (coherent attack)
  /// Inharmonicity, as cents of displacement on the 32nd partial. Zero is the
  /// plain harmonic series. See inharmonicCents.
  float stretchCents = 0.0f;
  /// Keyboard tracking, in dB per octave above the rolloff. Thins the series
  /// as you play up rather than turning high notes down. Zero is off. See
  /// trackingGain.
  float trackDbPerOctave = 0.0f;

  /// Which oscillator every partial is. One choice for all 32, because an
  /// instrument is built out of one circuit repeated rather than out of a
  /// different one per channel. See Character.h.
  Character character = Character::Pure;

  /// Where a note's MPE slide goes. Stored as the raw choice so the voice can
  /// branch on it without the DSP core knowing what a parameter is.
  SlideDestination slideDest = SlideDestination::Brightness;
  /// A warped record under the whole instrument, 0 to 1. Sits between the
  /// voices and the echo, so the repeats inherit whatever it did rather than
  /// wobbling on their own. See Wobble.h.
  float wobbleAmount = 0.0f;

  /// How hard the summed series runs into the bus stage.
  ///
  /// Not a parameter and not reachable from the panel: the plugin fills this
  /// from BusDrive::amountFor, which is a property of the character rather
  /// than a control. It is a field rather than a lookup inside the stage so
  /// that a test measuring one part of the instrument can put the rest of it
  /// out of the way, which is what the whole DSP suite does.
  float busDrive = 0.0f;
  /// Shape the sum on its way out. 32 faders make it very easy to overshoot,
  /// which is what this began as, and it is five machines now, so it is part
  /// of the sound as well as a guard. The master fader sits in front of it, so
  /// how hard it is driven is a thing a patch decides. See OutputStage.
  bool safetyClip = true;

  /// Which of the five, when it is on. Soft is what every patch had before
  /// there was a choice.
  ClipType clipType = ClipType::Soft;
  /// Whether striking a key that is already sounding takes over the voice it
  /// is already using, rather than starting a second one beside it. See
  /// SynthEngine::noteOnImpl.
  bool oneVoicePerKey = true;

  /// Whether a channel's modulator is one circuit the whole keyboard hears
  /// rather than one per note.
  ///
  /// Off, every note carries its own, so two keys struck a moment apart are a
  /// moment apart in their vibrato for as long as they sound, which is what a
  /// digital instrument does. On, the phase belongs to the channel and a note
  /// joins whatever is already running, which is what an instrument with one
  /// tremolo circuit in it does: a chord breathes as one thing.
  ///
  /// Two switches because the two modulators are two circuits. See
  /// SynthEngine::advanceSharedModulators.
  bool pitchModInPhase = false;
  bool ampModInPhase = false;
};

/// The tape echo, which sits across the whole instrument rather than on any one
/// partial.
/// Which delay the repeats come out of.
///
/// Declared in full from the start even though they arrive one at a time,
/// because the length of a list a host can automate is part of its contract:
/// a choice is stored as a fraction of the range, so adding an entry later
/// would move every lane ever written against it. See the list-length test.
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

/// The name in one word, for the button on the bar, which has a button's
/// worth of room rather than a menu's. The menu says what each one is; the
/// button only has to say which.
inline const char *echoTypeShortName(EchoType t) {
  return t == EchoType::Bucket ? "BBD" : echoTypeName(t);
}

struct EchoParams {
  bool enabled = false;

  /// Which machine the repeats come from. The switch that turns the thing on
  /// is separate and older: a preset saved before there was a choice says
  /// nothing about one, and gets the tape it was made on.
  EchoType type = EchoType::Tape;
  float mix = 0.25f;         ///< 0 dry, 1 fully wet
  float timeSeconds = 0.35f; ///< distance between the heads
  float feedback = 0.35f;    ///< 0..0.95, how much goes round again
  /// How worn the machine is, 0 to 1, and what that means depends on which
  /// machine it is.
  ///
  /// On tape it is the three things that go together as a deck ages: the top
  /// end it loses on every pass, how far the motor wanders, and how hard the
  /// tape leans over when driven. New is clean and bright, old is dark,
  /// unsteady and compressed.
  ///
  /// On a bucket brigade it is three others that go together just as tightly:
  /// darker, dirtier on every pass, and the hiss its compander cannot quite
  /// hide, which swells up behind a chord and ducks away as the repeats die.
  ///
  /// One knob either way, because separating any of them meant three that
  /// were always turned together.
  float age = 0.35f;
};

/// Which reverb the tail comes out of.
///
/// Declared in full from the start, for the reason the echo's types are: the
/// length of a list a host can automate is part of what a stored choice
/// means.
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

/// What the bar has room to shout, where the menu has room to be accurate.
///
/// Only the plate needs one. Its two allpasses wander further than a sheet of
/// steel ever did, which is what keeps a long decay from repeating itself, and
/// the menu says so. On a button the word that matters is which machine it is.
inline const char *reverbTypeShortName(ReverbType t) {
  return t == ReverbType::Plate ? "Plate" : reverbTypeName(t);
}

/// The reverb, which is one of three machines sized and damped from the panel.
struct ReverbParams {
  bool enabled = false;

  /// Which machine the tail comes from. The switch that turns it on is
  /// separate and older, so a preset saved before there was a choice says
  /// nothing about one and gets the room it was made in.
  ReverbType type = ReverbType::Room;
  float mix = 0.25f;
  /// RT60. The room is sized from it rather than set separately: a long tail in
  /// a small room is a spring, not a place, and nobody was reaching for that.
  float decaySeconds = 2.0f;
  float damping = 0.5f; ///< how fast the top end dies away in the tail
  float preDelaySeconds = 0.0f;
};

/// Converter quality, downwards.
///
/// Both settings default to off, which means whatever the host is running at
/// and no quantiser, and both are cuts rather than effects: turning them down
/// does less work, not more. See SynthEngine::renderVoices for why the rate is
/// a genuine saving rather than a filter over the top.
struct LofiParams {
  /// Render rate in Hz, or 0 for the host's own.
  double rateHz = 0.0;

  /// Quantiser resolution in bits, or 0 for none. Signed, so 8 bits is 256
  /// codes across the range.
  int bits = 0;
};

struct SynthParams {
  std::array<OscParams, kNumHarmonics> osc{};
  NoiseParams noise{};
  GlobalParams global{};
  EchoParams echo{};
  ReverbParams reverb{};
  LofiParams lofi{};
};

} // namespace ovt
