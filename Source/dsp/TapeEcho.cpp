#include "TapeEcho.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr float kTwoPi = 6.283185307179586f;

/// Where the wow sits. Slow enough to hear as drift rather than as vibrato.
constexpr double kWowRateHz = 0.7;
constexpr float kFlutterRateHz = 6.3f;

/// Where the heads sit relative to each other. Fixed rather than offered: the
/// useful part of the range is a lean, and every setting either side of it was
/// either inaudible or a gimmick.
constexpr float kCrossfeed = 0.7f;

/// Soft compression above a threshold. Used twice, for two different jobs.
inline float lean(float x, float threshold) noexcept {
  const float a = std::abs(x);
  if (a <= threshold)
    return x;

  const float over = (a - threshold) / (1.0f - threshold);
  const float y = threshold + (1.0f - threshold) * std::tanh(over);

  return x < 0.0f ? -y : y;
}

/// The character stage, in proportion to how worn the machine is.
inline float worn(float x, float age) noexcept {
  return x + age * (lean(x, 0.6f) - x);
}

/// One-pole coefficient for a given cutoff.
inline float onePole(float cutoffHz, double sampleRate) noexcept {
  const auto f = std::clamp((double)cutoffHz, 10.0, sampleRate * 0.45);
  return (float)std::exp(-kTwoPi * f / sampleRate);
}
} // namespace

void TapeEcho::Head::resize(int length) {
  buffer.assign((size_t)std::max(1, length), 0.0f);
  write = 0;
  damp = 0.0f;
  dc = 0.0f;
}

void TapeEcho::Head::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
  damp = 0.0f;
  dc = 0.0f;
}

float TapeEcho::Head::read(float delaySamples) const noexcept {
  const auto length = (int)buffer.size();

  if (length <= 1)
    return 0.0f;

  const auto clamped = std::clamp(delaySamples, 1.0f, (float)(length - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  // Reading backwards from the write head. The two taps either side of the
  // fractional position are linearly blended, which loses a little top end at
  // the extremes of the wander and is exactly what the medium does anyway.
  auto index = write - whole;
  while (index < 0)
    index += length;

  auto previous = index - 1;
  if (previous < 0)
    previous += length;

  const auto a = buffer[(size_t)index];
  const auto b = buffer[(size_t)previous];

  return a + (b - a) * frac;
}

void TapeEcho::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  // Room for the longest head distance, the motor wandering past it and a
  // couple of samples of interpolation slack.
  bufferLength = (int)(kMaxTimeSeconds * 1.15 * sampleRate) + 4;

  left.resize(bufferLength);
  right.resize(bufferLength);

  reset();
}

void TapeEcho::reset() noexcept {
  left.clear();
  right.clear();

  smoothedDelay = -1.0f;
  flutterPhase = 0.0f;
  wasEnabled = false;

  rng.reseed(0x51ed270bu);
  wow.restart(rng, kWowRateHz, sampleRate);
}

float TapeEcho::tailSeconds(const EchoParams &p) const noexcept {
  if (!p.enabled || p.mix <= 0.0f)
    return 0.0f;

  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);

  if (feedback < 0.01f)
    return p.timeSeconds;

  // How many passes it takes to fall 60 dB, which is where a repeat stops
  // being audible under anything else.
  const auto passes = std::log(0.001f) / std::log(feedback);

  return std::min(30.0f, p.timeSeconds * passes);
}

void TapeEcho::process(float *outL, float *outR, int numSamples,
                       const EchoParams &p) noexcept {
  if (numSamples <= 0 || bufferLength <= 0)
    return;

  if (!p.enabled) {
    // Emptying the loop on the way out means switching back on starts from
    // silence rather than replaying whatever was going round at the time.
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);
  const auto age = std::clamp(p.age, 0.0f, 1.0f);

  const auto targetDelay =
      (float)(std::clamp(p.timeSeconds, 0.01f, kMaxTimeSeconds) * sampleRate);

  if (smoothedDelay < 0.0f)
    smoothedDelay = targetDelay;

  // ~90 ms to wind to a new head distance. Fast enough to feel like a knob,
  // slow enough that the pitch slide is the point rather than a glitch.
  const auto glide = (float)std::exp(-1.0 / (0.09 * sampleRate));

  // The repeats darken from the top and thin from the bottom. A new machine
  // keeps nearly everything, a worn one hands back very little.
  const auto brightness = 1.0f - age;
  const auto toneHz = 700.0f + brightness * brightness * 11000.0f;
  const auto lpCoef = onePole(toneHz, sampleRate);
  const auto hpCoef = onePole(90.0f, sampleRate);

  const auto flutterStep = kFlutterRateHz / (float)sampleRate;

  for (int n = 0; n < numSamples; ++n) {
    smoothedDelay = targetDelay + (smoothedDelay - targetDelay) * glide;

    // The motor: a slow wander plus a periodic flutter, both in proportion to
    // the head distance, since a longer loop wanders further.
    const auto wowValue = wow.advance(rng);

    flutterPhase += flutterStep;
    if (flutterPhase >= 1.0f)
      flutterPhase -= 1.0f;

    const auto flutter = std::sin(kTwoPi * flutterPhase);
    const auto modulation =
        age * smoothedDelay * (0.006f * wowValue + 0.0012f * flutter);

    const auto delaySamples = smoothedDelay + modulation;

    const auto wetL = left.read(delaySamples);
    const auto wetR = right.read(delaySamples);

    // Each pass loses its top and its bottom, then leans over if it is loud.
    left.damp = wetL + (left.damp - wetL) * lpCoef;
    right.damp = wetR + (right.damp - wetR) * lpCoef;

    left.dc = left.damp + (left.dc - left.damp) * hpCoef;
    right.dc = right.damp + (right.dc - right.damp) * hpCoef;

    // Two stages, and they are not the same thing. The first is character and
    // follows the wear, so a new machine passes its repeats through untouched.
    // The second is a backstop that is always there: at 95% feedback a steady
    // tone can otherwise pile up to twenty times what went in.
    const auto agedL = lean(worn(left.damp - left.dc, age), 0.95f);
    const auto agedR = lean(worn(right.damp - right.dc, age), 0.95f);

    // Crossfeed sends each head into the other, so the repeats walk across the
    // image instead of sitting where they landed.
    const auto fedL = agedL * (1.0f - kCrossfeed) + agedR * kCrossfeed;
    const auto fedR = agedR * (1.0f - kCrossfeed) + agedL * kCrossfeed;

    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // Crossfeed alone does nothing to a centred source, since swapping two
    // identical signals changes neither. What makes the repeats move is feeding
    // them in unevenly and letting the crossfeed carry them over on each pass.
    const auto injectL =
        dryL * (1.0f - 0.5f * kCrossfeed) + dryR * (0.5f * kCrossfeed);
    const auto injectR = dryR * (1.0f - kCrossfeed);

    left.buffer[(size_t)left.write] = injectL + fedL * feedback;
    right.buffer[(size_t)right.write] = injectR + fedR * feedback;

    if (++left.write >= bufferLength)
      left.write = 0;
    if (++right.write >= bufferLength)
      right.write = 0;

    outL[n] = dryL + (wetL - dryL) * mix;
    outR[n] = dryR + (wetR - dryR) * mix;
  }
}

} // namespace ovt
