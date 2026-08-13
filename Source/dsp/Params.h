#pragma once

#include <array>

#include "Harmonics.h"

namespace ovt
{

/** A per-block snapshot of one channel strip. Deliberately plain data: the DSP core
    never touches JUCE, which keeps it unit-testable and portable. */
struct OscParams
{
    // Pitch
    float tuneBlend    = 0.0f;  ///< 0 = equal temperament, 1 = just intonation
    float pmRateHz     = 4.0f;
    float pmDepthCents = 0.0f;

    // Amplitude
    float attack  = 0.005f;     ///< seconds
    float decay   = 0.400f;
    float sustain = 1.0f;       ///< 0..1
    float release = 0.400f;
    float amRateHz = 4.0f;
    float amDepth  = 0.0f;      ///< 0..1 tremolo depth
    float volume   = 0.0f;      ///< linear gain, mute/solo already folded in by the caller

    /** False when muted, or when some other strip is soloed. */
    bool audible = true;
};

struct GlobalParams
{
    float masterGain    = 0.25f; ///< linear
    float stereoSpread  = 0.0f;  ///< 0 = mono, 1 = partials fanned across the field
    float velAmount     = 0.7f;  ///< 0 = velocity ignored, 1 = fully velocity sensitive
    float bendSemitones = 0.0f;  ///< current pitch-bend offset
    bool  phaseReset    = true;  ///< reset partial phase on note-on (coherent attack)
    bool  safetyClip    = true;  ///< soft-clip the sum; 32 faders make it very easy to overshoot
};

struct SynthParams
{
    std::array<OscParams, kNumHarmonics> osc {};
    GlobalParams                         global {};
};

} // namespace ovt
