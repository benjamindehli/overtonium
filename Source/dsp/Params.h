#pragma once

#include <array>

#include "Harmonics.h"

namespace ovt {

/// A per-block snapshot of one channel strip. Deliberately plain data: the DSP
/// core never touches JUCE, which keeps it unit-testable and portable.
struct OscParams {
  // Pitch
  float tuneBlend = 0.0f; ///< 0 = equal temperament, 1 = just intonation
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
  float bendSemitones = 0.0f; ///< current pitch-bend offset
  float aftertouch = 0.0f;    ///< current channel pressure, 0..1
  bool phaseReset = true; ///< reset partial phase on note-on (coherent attack)
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
  float width = 1.0f;
};

struct SynthParams {
  std::array<OscParams, kNumHarmonics> osc{};
  NoiseParams noise{};
  GlobalParams global{};
  EchoParams echo{};
  ReverbParams reverb{};
};

} // namespace ovt
