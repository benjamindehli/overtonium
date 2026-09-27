#include "DigitalEcho.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
/// What AGE costs the repeats, at the top of the knob.
///
/// Eight samples held is the host rate divided by eight, which at 48 kHz is
/// six, and six bits is sixty-four steps. Both are well past tasteful and that
/// is the point of the end of a knob: the useful settings are the ones on the
/// way there, where a repeat is recognisably the signal and audibly older
/// than it.
///
/// Nothing at all at the bottom, rather than a great many bits and a small
/// hold. A digital delay's whole claim is that a repeat is a copy, so the
/// clean end of this control has to be exactly that and not nearly that.
constexpr int kMaxHold = 8;
constexpr float kFewestBits = 6.0f;
constexpr float kMostBits = 16.0f;
} // namespace

void DigitalEcho::Side::resize(int length) {
  buffer.assign((size_t)std::max(1, length), 0.0f);
  write = 0;
}

void DigitalEcho::Side::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
  held = 0.0f;
  remaining = 0;
}

float DigitalEcho::Side::read(int delaySamples) const noexcept {
  const auto length = (int)buffer.size();

  if (length <= 1)
    return 0.0f;

  auto index = write - std::clamp(delaySamples, 1, length - 1);

  if (index < 0)
    index += length;

  return buffer[(size_t)index];
}

float DigitalEcho::roughen(Side &s, float x, int hold, float levels) noexcept {
  if (hold <= 1 && levels <= 0.0f)
    return x;

  // The rate first, then the depth, which is the order a converter meets them
  // and the order that leaves the steps where the ear expects: quantising and
  // then holding would smear a step across the hold rather than landing on it.
  if (hold > 1) {
    if (--s.remaining <= 0) {
      s.held = x;
      s.remaining = hold;
    }

    x = s.held;
  }

  if (levels > 0.0f)
    x = std::round(x * levels) / levels;

  return x;
}

void DigitalEcho::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);
  bufferLength = (int)(kMaxTimeSeconds * sampleRate) + 4;

  left.resize(bufferLength);
  right.resize(bufferLength);

  reset();
}

void DigitalEcho::reset() noexcept {
  left.clear();
  right.clear();

  smoothedDelay = -1.0f;
  wasEnabled = false;
}

float DigitalEcho::tailSeconds(const EchoParams &p) const noexcept {
  if (!p.enabled || p.type != EchoType::Digital || p.mix <= 0.0f)
    return 0.0f;

  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);

  if (feedback <= 0.0f)
    return p.timeSeconds;

  const auto passes = std::log(0.001f) / std::log(feedback);

  return std::min(30.0f, p.timeSeconds * passes);
}

void DigitalEcho::process(float *outL, float *outR, int numSamples,
                          const EchoParams &p) noexcept {
  if (numSamples <= 0 || bufferLength <= 0)
    return;

  if (!p.enabled || p.type != EchoType::Digital) {
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);
  const auto age = std::clamp(p.age, 0.0f, 1.0f);
  const auto time = std::clamp(p.timeSeconds, 0.01f, kMaxTimeSeconds);

  const auto targetDelay = (float)(time * sampleRate);

  if (smoothedDelay < 0.0f)
    smoothedDelay = targetDelay;

  const auto glide = (float)std::exp(-1.0 / (0.09 * sampleRate));

  // Rounded, because a converter runs at a rate rather than between two, and
  // held at one when there is no wear to speak of so the copy is a copy.
  const auto hold = 1 + (int)std::lround((double)age * (kMaxHold - 1));

  const auto bits = kMostBits - age * (kMostBits - kFewestBits);
  const auto levels =
      age <= 0.0f ? 0.0f : (float)(1 << (int)std::lround((double)bits - 1.0));

  for (int n = 0; n < numSamples; ++n) {
    smoothedDelay = targetDelay + (smoothedDelay - targetDelay) * glide;

    const auto spacing = (int)std::lround(smoothedDelay);

    const auto wetL = roughen(left, left.read(spacing), hold, levels);
    const auto wetR = roughen(right, right.read(spacing), hold, levels);

    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // The input arrives on one side only, and every repeat crosses. That is
    // the whole of the ping-pong: what the right side holds is what the left
    // one handed it a delay ago, so the repeats alternate rather than the two
    // sides each carrying their own copy.
    //
    // Summed to the middle on the way in, because a repeat that crosses has
    // to start somewhere, and starting it on one side of the stereo field
    // would put the first repeat there rather than the dry signal.
    left.buffer[(size_t)left.write] = 0.5f * (dryL + dryR) + wetR * feedback;
    right.buffer[(size_t)right.write] = wetL * feedback;

    if (++left.write >= bufferLength)
      left.write = 0;
    if (++right.write >= bufferLength)
      right.write = 0;

    outL[n] = dryL + (wetL - dryL) * mix;
    outR[n] = dryR + (wetR - dryR) * mix;
  }
}

} // namespace ovt
