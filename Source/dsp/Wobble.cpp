#include "Wobble.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
/// Once round a 33 rpm record is about 1.8 seconds, so the warp sits near
/// that. The wobble is fast enough not to read as a tune and slow enough not
/// to read as vibrato.
constexpr double kWarpRateHz = 0.55;
constexpr double kWobbleRateHz = 2.7;

/// How much of the travel each part gets. They are summed and then held
/// inside the line, so these are shares rather than absolutes.
constexpr float kWarpShare = 0.62f;
constexpr float kWobbleShare = 0.20f;
constexpr float kNudgeShare = 0.45f;

/// How often a slip happens at full amount, and how much rarer it gets on the
/// way down. Squared, so the bottom of the knob is a wander with the odd
/// stumble and the top is a transport falling over itself.
constexpr double kNudgesPerSecond = 3.2;

/// How long a slip takes to arrive and how long it takes to let go.
constexpr double kNudgeChaseSeconds = 0.020;
constexpr double kNudgeDecaySeconds = 0.130;

/// A change of amount slides in over this, so the read point never steps.
constexpr double kOffsetGlideSeconds = 0.050;
} // namespace

void Wobble::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  // Room for the read point to sit a full travel either side of centre, plus
  // slack for the interpolator.
  length = (int)(2.2 * kMaxDepthSeconds * sampleRate) + 8;

  lineL.assign((size_t)length, 0.0f);
  lineR.assign((size_t)length, 0.0f);

  nudgeChase =
      1.0f - (float)std::exp(-1.0 / (kNudgeChaseSeconds * sampleRate));
  nudgeDecay = (float)std::exp(-1.0 / (kNudgeDecaySeconds * sampleRate));

  reset();
}

void Wobble::reset() noexcept {
  std::fill(lineL.begin(), lineL.end(), 0.0f);
  std::fill(lineR.begin(), lineR.end(), 0.0f);

  write = 0;
  smoothedOffset = 0.0f;
  nudgeTarget = 0.0f;
  nudge = 0.0f;
  wasRunning = false;

  rng.reseed(0x2545f491u);
  warp.restart(rng, kWarpRateHz, sampleRate);
  wobble.restart(rng, kWobbleRateHz, sampleRate);
}

float Wobble::read(const std::vector<float> &line, int at,
                   float delaySamples) const noexcept {
  if (length <= 2)
    return 0.0f;

  // Zero is allowed, and reads back exactly the sample just written. That
  // matters at the moment the control leaves zero: the line holds nothing yet,
  // and anything further back than the write head would be the silence it was
  // cleared to. The glide is what keeps it safe from there, since the read
  // point recedes more slowly than the line fills.
  const auto clamped = std::clamp(delaySamples, 0.0f, (float)(length - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  auto index = at - whole;
  while (index < 0)
    index += length;

  auto previous = index - 1;
  if (previous < 0)
    previous += length;

  const auto a = line[(size_t)index];
  const auto b = line[(size_t)previous];

  return a + (b - a) * frac;
}

void Wobble::process(float *left, float *right, int numSamples,
                     float amount) noexcept {
  if (numSamples <= 0 || length <= 2)
    return;

  const auto depth = std::clamp(amount, 0.0f, 1.0f);

  if (depth <= 0.0f) {
    // Nothing to do, and nothing left behind: a line still holding the last
    // few milliseconds would replay them the moment the control came back up.
    if (wasRunning)
      reset();

    return;
  }

  wasRunning = true;

  // Centre the read point at the travel, so the modulation can pull it right
  // down to the write head and out to twice the travel. Both grow with the
  // amount, which is what lets the control come up from zero without a step.
  const auto travel = depth * kMaxDepthSeconds * (float)sampleRate;

  const auto glide =
      (float)std::exp(-1.0 / (kOffsetGlideSeconds * sampleRate));

  // Slips get both more frequent and larger as the control comes up.
  const auto nudgeChance =
      (float)(kNudgesPerSecond * (double)depth * (double)depth / sampleRate);

  for (int n = 0; n < numSamples; ++n) {
    if (rng.unipolar() < nudgeChance)
      nudgeTarget = rng.bipolar();

    nudgeTarget *= nudgeDecay;
    nudge += (nudgeTarget - nudge) * nudgeChase;

    const auto modulation =
        std::clamp(kWarpShare * warp.advance(rng) +
                       kWobbleShare * wobble.advance(rng) + kNudgeShare * nudge,
                   -1.0f, 1.0f);

    const auto target = travel * (1.0f + modulation);
    smoothedOffset = target + (smoothedOffset - target) * glide;

    lineL[(size_t)write] = left[n];
    lineR[(size_t)write] = right[n];

    left[n] = read(lineL, write, smoothedOffset);
    right[n] = read(lineR, write, smoothedOffset);

    if (++write >= length)
      write = 0;
  }
}

} // namespace ovt
