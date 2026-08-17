#include "TapeEcho.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr float kTwoPi = 6.283185307179586f;

/// The two motors.
///
/// Slow enough to hear as drift rather than as vibrato, and deliberately not
/// the same on both sides: the rates are close but share no common factor, so
/// the two paths never fall into step and never repeat a relationship. The
/// depths differ a little too, so one side is the loose take and the other the
/// tighter one.
///
/// Fixed rather than offered as controls. What matters is that they differ,
/// not by how much, and a pair of knobs whose only wrong setting is "equal" is
/// a pair of knobs nobody needs.
constexpr double kWowRateL = 0.70, kWowRateR = 0.83;
constexpr float kFlutterRateL = 6.3f, kFlutterRateR = 5.31f;
constexpr float kWowDepthL = 0.0060f, kWowDepthR = 0.0072f;
constexpr float kFlutterDepthL = 0.0012f, kFlutterDepthR = 0.0009f;

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

void TapeEcho::Head::restartMotor(double sampleRate, uint32_t seed,
                                  float startPhase) noexcept {
  rng.reseed(seed);
  wow.restart(rng, wowRateHz, sampleRate);
  flutterPhase = startPhase;
}

float TapeEcho::Head::wander(float delaySamples, float age,
                             double sampleRate) noexcept {
  flutterPhase += flutterRateHz / (float)sampleRate;
  if (flutterPhase >= 1.0f)
    flutterPhase -= 1.0f;

  const auto flutter = std::sin(kTwoPi * flutterPhase);

  // In proportion to the head distance, since a longer loop wanders further,
  // and to the wear, since holding speed is what a machine in good order does.
  return age * delaySamples *
         (wowDepth * wow.advance(rng) + flutterDepth * flutter);
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

  left.wowRateHz = kWowRateL;
  left.flutterRateHz = kFlutterRateL;
  left.wowDepth = kWowDepthL;
  left.flutterDepth = kFlutterDepthL;

  right.wowRateHz = kWowRateR;
  right.flutterRateHz = kFlutterRateR;
  right.wowDepth = kWowDepthR;
  right.flutterDepth = kFlutterDepthR;

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

  // Different seeds and different starting phases, so the two are already
  // apart before either has turned once.
  left.restartMotor(sampleRate, 0x51ed270bu, 0.0f);
  right.restartMotor(sampleRate, 0x9e3779b9u, 0.37f);

  smoothedDelay = -1.0f;
  wasEnabled = false;
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

  for (int n = 0; n < numSamples; ++n) {
    smoothedDelay = targetDelay + (smoothedDelay - targetDelay) * glide;

    // Two motors, each wandering off the same nominal head distance by its own
    // amount. This is the whole of the stereo: everything downstream keeps the
    // two paths apart rather than mixing them.
    const auto wetL =
        left.read(smoothedDelay + left.wander(smoothedDelay, age, sampleRate));

    const auto wetR = right.read(smoothedDelay +
                                 right.wander(smoothedDelay, age, sampleRate));

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

    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // Each loop takes its own channel, feeds only itself, and comes back on
    // the side it went out on. Nothing crosses over at any point, so wherever
    // the mixer put a partial is where its repeats stay.
    left.buffer[(size_t)left.write] = dryL + agedL * feedback;
    right.buffer[(size_t)right.write] = dryR + agedR * feedback;

    if (++left.write >= bufferLength)
      left.write = 0;
    if (++right.write >= bufferLength)
      right.write = 0;

    outL[n] = dryL + (wetL - dryL) * mix;
    outR[n] = dryR + (wetR - dryR) * mix;
  }
}

} // namespace ovt
