#pragma once

#include <array>
#include <cstdint>

#include "Envelope.h"
#include "Harmonics.h"
#include "Params.h"

namespace ovt
{

/** Fades a partial out as it approaches Nyquist.

    Without this, playing high notes folds the upper partials back down as aliasing:
    the 32nd harmonic of C7 lands at ~67 kHz. The fade starts well below Nyquist so the
    partial disappears smoothly instead of blinking out.
*/
inline float nyquistGain (double freq, double sampleRate) noexcept
{
    const double fadeStart = sampleRate * 0.42;
    const double fadeEnd   = sampleRate * 0.49;

    if (freq <= fadeStart) return 1.0f;
    if (freq >= fadeEnd)   return 0.0f;

    const double t = (freq - fadeStart) / (fadeEnd - fadeStart);
    return (float) (0.5 * (1.0 + std::cos (3.14159265358979324 * t)));
}

/** One polyphonic voice: 32 independently tuned, enveloped and modulated sine partials. */
class Voice
{
public:
    /** LFOs, envelope-driven gain interpolation and the Nyquist guard update once per
        control block rather than once per sample. 32 frames is ~0.7 ms at 44.1 kHz. */
    static constexpr int kControlBlock = 32;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void noteOn (int note, float velocity, const SynthParams& p) noexcept;
    void noteOff() noexcept;
    void steal() noexcept;              ///< fast fade-out so the voice can be reused

    bool isActive()    const noexcept { return active; }
    bool isReleasing() const noexcept { return released; }
    int  getNote()     const noexcept { return midiNote; }

    uint64_t getAge()      const noexcept { return startOrder; }
    void     setAge (uint64_t v) noexcept { startOrder = v; }

    /** Adds this voice into the (already-sized) stereo buffers. Master gain is applied
        downstream by the engine. */
    void render (float* left, float* right, int numSamples, const SynthParams& p) noexcept;

private:
    struct Partial
    {
        double   phase         = 0.0;
        double   pitchLfoPhase = 0.0;
        double   ampLfoPhase   = 0.0;
        Envelope env;
        float    lastGain = 0.0f; ///< carried across control blocks so gain never steps
        bool     gainPrimed = false;
    };

    std::array<Partial, kNumHarmonics> partials {};

    double   sampleRate = 44100.0;
    double   baseFreq   = 440.0;
    int      midiNote   = -1;
    float    velGain    = 1.0f;
    bool     active     = false;
    bool     released   = false;
    uint64_t startOrder = 0;
};

} // namespace ovt
