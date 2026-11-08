#include "TapeEcho.h"
#include "DelayLine.h"
#include "dm/dsp/Filters.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr float kTwoPi = 6.283185307179586f;

/// The two motors. The rates are close but share no common factor, so the two
/// paths never fall into step, and the depths differ a little so one side is
/// looser than the other.
constexpr double kWowRateL = 0.70, kWowRateR = 0.83;
constexpr float kFlutterRateL = 6.3f, kFlutterRateR = 5.31f;
constexpr float kWowDepthL = 0.0060f, kWowDepthR = 0.0072f;
constexpr float kFlutterDepthL = 0.0012f, kFlutterDepthR = 0.0009f;

/// Soft compression above a threshold.
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

/// A one-pole coefficient for a cutoff that may come from a knob.
inline float clampedPole(float cutoffHz, double sampleRate) noexcept {
  return dm::onePole(std::clamp((double)cutoffHz, 10.0, sampleRate * 0.45),
                     sampleRate);
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

// sr rather than sampleRate: Head is nested inside TapeEcho, which has a
// sampleRate of its own, and a parameter of that name hides it.
void TapeEcho::Head::restartMotor(double sr, uint32_t seed,
                                  float startPhase) noexcept {
  rng.reseed(seed);
  wow.restart(rng, wowRateHz, sr);
  flutterPhase = startPhase;
}

float TapeEcho::Head::wander(float delaySamples, float age,
                             double sr) noexcept {
  flutterPhase += flutterRateHz / (float)sr;
  if (flutterPhase >= 1.0f)
    flutterPhase -= 1.0f;

  const auto flutter = std::sin(kTwoPi * flutterPhase);

  // In proportion to the head distance, since a longer loop wanders further,
  // and to the wear, since holding speed is what a machine in good order does.
  return age * delaySamples *
         (wowDepth * wow.advance(rng) + flutterDepth * flutter);
}

float TapeEcho::Head::read(float delaySamples) const noexcept {
  return readBack(buffer, write, delaySamples, 1.0f);
}

void TapeEcho::prepare(double newSampleRate) {
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
  return echo::tailSeconds(p, EchoType::Tape);
}

void TapeEcho::process(float *outL, float *outR, int numSamples,
                       const EchoParams &p) noexcept {
  if (numSamples <= 0 || bufferLength <= 0)
    return;

  if (!p.enabled || p.type != EchoType::Tape) {
    // Emptying the loop on the way out means switching back on starts from
    // silence rather than replaying whatever was going round at the time.
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto s = echo::settingsFor(p, sampleRate);
  const auto mix = s.mix, feedback = s.feedback, age = s.age;
  const auto targetDelay = s.targetDelay, glide = s.glide;

  if (smoothedDelay < 0.0f)
    smoothedDelay = targetDelay;

  // The repeats darken from the top and thin from the bottom. A new machine
  // keeps nearly everything, a worn one hands back very little. This filter is
  // almost all of what wear does to a repeat.
  const auto brightness = 1.0f - age;
  const auto toneHz = 900.0f + brightness * brightness * 14000.0f;
  const auto lpCoef = clampedPole(toneHz, sampleRate);
  const auto hpCoef = clampedPole(90.0f, sampleRate);

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

    // Two stages. The first is character and follows the wear, so a new
    // machine passes its repeats through untouched. The second is a backstop
    // that is always there, since at 95% feedback a steady tone can otherwise
    // pile up to twenty times what went in. It sits at 1.6 rather than under
    // one because the summed bus feeding the echo runs above one on a bright
    // patch, and a lower ceiling would compress every note.
    const auto agedL = lean(worn(left.damp - left.dc, age), 1.6f);
    const auto agedR = lean(worn(right.damp - right.dc, age), 1.6f);

    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // The output is the aged signal, not what was written, so the first repeat
    // is coloured like every later one.

    // Each loop takes its own channel, feeds only itself, and comes back on
    // the side it went out on. Nothing crosses over at any point, so wherever
    // the mixer put a partial is where its repeats stay.
    left.buffer[(size_t)left.write] = dryL + agedL * feedback;
    right.buffer[(size_t)right.write] = dryR + agedR * feedback;

    if (++left.write >= bufferLength)
      left.write = 0;
    if (++right.write >= bufferLength)
      right.write = 0;

    outL[n] = dryL + (agedL - dryL) * mix;
    outR[n] = dryR + (agedR - dryR) * mix;
  }
}

} // namespace ovt
