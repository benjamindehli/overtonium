#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace ovt
{

namespace
{
    /** Linear below the threshold, smoothly compressed above it, bounded at 1.0. */
    inline float softClip (float x) noexcept
    {
        constexpr float threshold = 0.7f;

        const float a = std::abs (x);
        if (a <= threshold)
            return x;

        const float over = (a - threshold) / (1.0f - threshold);
        const float y    = threshold + (1.0f - threshold) * std::tanh (over);

        return x < 0.0f ? -y : y;
    }
}

void SynthEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::max (1.0, newSampleRate);

    for (auto& v : voices)
        v.prepare (sampleRate);

    reset();
}

void SynthEngine::reset() noexcept
{
    for (auto& v : voices)
        v.reset();

    heldBySustain.fill (false);
    sustainDown        = false;
    ageCounter         = 0;
    smoothedMasterGain = -1.0f;
}

void SynthEngine::setPolyphony (int n) noexcept
{
    polyphony = std::clamp (n, 1, kMaxPolyphony);
}

int SynthEngine::countSounding() const noexcept
{
    int c = 0;
    for (const auto& v : voices)
        if (v.isActive() && ! v.isReleasing())
            ++c;

    return c;
}

int SynthEngine::getActiveVoiceCount() const noexcept
{
    int c = 0;
    for (const auto& v : voices)
        if (v.isActive())
            ++c;

    return c;
}

Voice* SynthEngine::findFreeVoice() noexcept
{
    for (auto& v : voices)
        if (! v.isActive())
            return &v;

    return nullptr;
}

Voice* SynthEngine::findOldestSounding() noexcept
{
    Voice* oldest = nullptr;

    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing())
            if (oldest == nullptr || v.getAge() < oldest->getAge())
                oldest = &v;

    return oldest;
}

void SynthEngine::noteOn (int note, float velocity, const SynthParams& p) noexcept
{
    // Retrigger a still-held instance of the same note rather than stacking a second voice.
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];

        if (v.isActive() && ! v.isReleasing() && v.getNote() == note)
        {
            heldBySustain[i] = false;
            v.noteOn (note, velocity, p);
            v.setAge (++ageCounter);
            return;
        }
    }

    if (countSounding() >= polyphony)
        if (auto* victim = findOldestSounding())
            victim->steal();

    auto* target = findFreeVoice();

    if (target == nullptr)
    {
        // Pool exhausted (every surplus voice is mid-fade). Take the oldest outright.
        target = &voices[0];
        for (auto& v : voices)
            if (v.getAge() < target->getAge())
                target = &v;

        target->reset();
    }

    const auto index = (size_t) std::distance (voices.data(), target);
    heldBySustain[index] = false;

    target->noteOn (note, velocity, p);
    target->setAge (++ageCounter);
}

void SynthEngine::noteOff (int note) noexcept
{
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];

        if (v.isActive() && ! v.isReleasing() && v.getNote() == note)
        {
            if (sustainDown)
                heldBySustain[i] = true;
            else
                v.noteOff();
        }
    }
}

void SynthEngine::setSustainPedal (bool down) noexcept
{
    sustainDown = down;

    if (down)
        return;

    for (size_t i = 0; i < voices.size(); ++i)
    {
        if (heldBySustain[i])
        {
            voices[i].noteOff();
            heldBySustain[i] = false;
        }
    }
}

void SynthEngine::allNotesOff() noexcept
{
    for (size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].noteOff();
        heldBySustain[i] = false;
    }

    sustainDown = false;
}

void SynthEngine::allSoundOff() noexcept
{
    for (auto& v : voices)
        v.reset();

    heldBySustain.fill (false);
    sustainDown = false;
}

void SynthEngine::render (float* left, float* right, int numSamples, const SynthParams& p) noexcept
{
    if (numSamples <= 0)
        return;

    std::fill (left,  left  + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    for (auto& v : voices)
        if (v.isActive())
            v.render (left, right, numSamples, p);

    // ---- master gain, smoothed over ~10 ms so fader moves do not zipper --------------
    const float target = std::max (0.0f, p.global.masterGain);

    if (smoothedMasterGain < 0.0f)
        smoothedMasterGain = target;

    const auto coef = (float) std::exp (-1.0 / (0.01 * sampleRate));

    if (std::abs (target - smoothedMasterGain) < 1.0e-6f)
    {
        smoothedMasterGain = target;

        for (int n = 0; n < numSamples; ++n)
        {
            left[n]  *= target;
            right[n] *= target;
        }
    }
    else
    {
        float g = smoothedMasterGain;

        for (int n = 0; n < numSamples; ++n)
        {
            g = target + (g - target) * coef;
            left[n]  *= g;
            right[n] *= g;
        }

        smoothedMasterGain = g;
    }

    if (p.global.safetyClip)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            left[n]  = softClip (left[n]);
            right[n] = softClip (right[n]);
        }
    }
}

} // namespace ovt
