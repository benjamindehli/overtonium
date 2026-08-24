#pragma once

#include <array>

#include "Harmonics.h"
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

/// Where each channel's own output goes, when a host has asked for one.
///
/// Mono and dry on purpose. A tap carries the channel after its envelope,
/// tremolo, velocity, pressure and fader, but before the pan, which belongs to
/// the stereo mix, and before the master effects, which is the whole reason to
/// route a channel out: you want to treat it yourself, not inherit a reverb
/// tail somebody else chose.
///
/// A null entry means nobody asked for that channel, and costs nothing.
struct ChannelTaps {
  /// One per partial, then the noise channel last.
  std::array<float *, kNumHarmonics + 1> out{};

  static constexpr int noiseIndex = kNumHarmonics;

  bool any() const noexcept {
    for (auto *p : out)
      if (p != nullptr)
        return true;

    return false;
  }
};

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
  float pmDepthCents = 0.0f;
  /// Depth of the smooth random pitch wander, in cents. Each partial of each
  /// note gets its own rate, so nothing ever locks together.
  float driftCents = 0.0f;

  // Amplitude
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
  /// How much the speed you let the key go at scales the key-off level, -1 to
  /// 1. Positive means snatching the key back gives the loudest key-off, which
  /// is what a jack or a damper does. Negative inverts it. Zero ignores the
  /// release entirely, which is what it did before it had this.
  float liftAmount = 0.0f;
  float amRateHz = 4.0f;
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

  float delay = 0.0f;
  float attack = 0.005f;
  float decay = 0.600f;
  float sustain = 1.0f;
  float swell = 0.005f;
  float offLevel = 0.0f;
  float release = 0.400f;
  float liftAmount = 0.0f;
  float amRateHz = 4.0f;
  float amDepth = 0.0f;
  float velAmount = 0.7f;
  float atAmount = 0.0f;
  float volume = 0.0f;
  float pan = 0.0f;

  bool audible = true;
};

struct GlobalParams {
  float masterGain = 0.25f;   ///< linear

  /// How the keyboard is tuned, as opposed to how the partials above each note
  /// are. See Temperament.h.
  Temperament temperament = Temperament::Equal;
  int tuningRoot = 0;          ///< pitch class the temperament is built on
  double referenceHz = 440.0;  ///< where A sits, whatever the temperament

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

  /// Where a note's MPE slide goes. Stored as the raw choice so the voice can
  /// branch on it without the DSP core knowing what a parameter is.
  SlideDestination slideDest = SlideDestination::Brightness;
  /// A warped record under the whole instrument, 0 to 1. Sits between the
  /// voices and the echo, so the repeats inherit whatever it did rather than
  /// wobbling on their own. See Wobble.h.
  float wobbleAmount = 0.0f;
  /// Soft-clip the sum; 32 faders make it very easy to overshoot.
  bool safetyClip = true;
};

/// The tape echo, which sits across the whole instrument rather than on any one
/// partial.
struct EchoParams {
  bool enabled = false;
  float mix = 0.25f;         ///< 0 dry, 1 fully wet
  float timeSeconds = 0.35f; ///< distance between the heads
  float feedback = 0.35f;    ///< 0..0.95, how much goes round again
  /// How worn the machine is, 0 to 1.
  ///
  /// One control for the three things that go together on a tape delay as it
  /// ages: the top end it loses on every pass, how far the motor wanders, and
  /// how hard the tape leans over when it is driven. New is clean and bright,
  /// old is dark, unsteady and compressed. Separating them meant three knobs
  /// that were nearly always turned together.
  float age = 0.35f;
};

/// The reverb: a feedback delay network, sized and damped from the panel.
struct ReverbParams {
  bool enabled = false;
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
