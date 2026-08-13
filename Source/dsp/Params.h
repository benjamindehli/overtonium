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

  /// False when muted, or when some other strip is soloed.
  bool audible = true;
};

struct GlobalParams {
  float masterGain = 0.25f;  ///< linear
  float stereoSpread = 0.0f; ///< 0 = mono, 1 = partials fanned across the field
  float bendSemitones = 0.0f; ///< current pitch-bend offset
  float aftertouch = 0.0f;    ///< current channel pressure, 0..1
  bool phaseReset = true; ///< reset partial phase on note-on (coherent attack)
  /// Soft-clip the sum; 32 faders make it very easy to overshoot.
  bool safetyClip = true;
};

struct SynthParams {
  std::array<OscParams, kNumHarmonics> osc{};
  GlobalParams global{};
};

} // namespace ovt
