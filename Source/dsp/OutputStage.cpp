#include "OutputStage.h"

namespace ovt {

namespace {
/// Where the shapers stop being linear.
///
/// Seven tenths for Soft, which is where it has always been. Hard has no knee
/// to place, so it has no threshold either, and Fold is told where to turn
/// back rather than where to bend.
constexpr float kSoftKnee = 0.7f;

/// Where the biased stage lets each half of the wave go before it leans.
///
/// A single-ended stage is not symmetric about zero: one direction runs out of
/// supply before the other. Half against nine tenths is enough to hear as a
/// different flavour rather than as a fault, and it is what puts a second
/// harmonic where Soft and Hard put a third.
constexpr float kBiasUp = 0.5f;
constexpr float kBiasDown = 0.9f;

/// The ceiling the limiter works to, a hair under one.
///
/// Under rather than at, because the gain it applies is one block behind what
/// the signal is doing, so a transient can slip past before the gain has moved.
/// The headroom absorbs that, and a hard clip behind it catches whatever the
/// headroom does not: this has to be bounded like the rest of them, and a
/// limiter without lookahead cannot promise that on its own.
constexpr float kLimitCeiling = 0.98f;

/// A tanh knee above the threshold, bounded by one.
float softKnee(float a, float threshold) {
  const float over = (a - threshold) / (1.0f - threshold);

  return threshold + (1.0f - threshold) * std::tanh(over);
}

float shapeSoft(float x) noexcept {
  const float a = std::abs(x);

  if (a <= kSoftKnee)
    return x;

  const float y = softKnee(a, kSoftKnee);

  return x < 0.0f ? -y : y;
}

float shapeHard(float x) noexcept { return std::clamp(x, -1.0f, 1.0f); }

float shapeBias(float x) noexcept {
  // The two halves get the same knee at different heights, so the shape is
  // continuous at zero and the asymmetry is in how soon each side gives way.
  const float threshold = x >= 0.0f ? kBiasUp : kBiasDown;
  const float a = std::abs(x);

  if (a <= threshold)
    return x;

  const float y = softKnee(a, threshold);

  return x < 0.0f ? -y : y;
}

float shapeFold(float x) noexcept {
  // Reflected about one as many times as it takes, which for a triangle fold
  // is the same as folding the value into the window of width four that runs
  // from minus one. Done in closed form rather than by looping, so a sample
  // that arrives at a hundred costs what one at a half costs.
  const float wrapped =
      std::fmod(x + 1.0f, 4.0f) + (x + 1.0f < 0.0f ? 4.0f : 0.0f);

  return 1.0f - std::abs(wrapped - 2.0f);
}
} // namespace

void OutputStage::prepare(double sampleRate) noexcept {
  rate = std::max(1.0, sampleRate);

  // One millisecond to pull down and a hundred to let back up. Fast enough
  // that the hard clip behind it rarely has anything to do, slow enough that
  // it does not pump on every note.
  attack = (float)std::exp(-1.0 / (0.001 * rate));
  release = (float)std::exp(-1.0 / (0.100 * rate));

  reset();
}

void OutputStage::reset() noexcept { gain = 1.0f; }

void OutputStage::process(float *left, float *right, int numSamples,
                          ClipType type) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  if (type == ClipType::Limiter) {
    processLimiter(left, right, numSamples);
    return;
  }

  auto *shape = type == ClipType::Hard   ? shapeHard
                : type == ClipType::Bias ? shapeBias
                : type == ClipType::Fold ? shapeFold
                                         : shapeSoft;

  for (int n = 0; n < numSamples; ++n) {
    left[n] = shape(left[n]);
    right[n] = shape(right[n]);
  }
}

void OutputStage::processLimiter(float *left, float *right,
                                 int numSamples) noexcept {
  for (int n = 0; n < numSamples; ++n) {
    // Linked, from whichever channel is louder, so the image holds still.
    const float peak = std::max(std::abs(left[n]), std::abs(right[n]));
    const float wanted = peak > kLimitCeiling ? kLimitCeiling / peak : 1.0f;

    // Down quickly, up slowly, which is what stops it breathing on every note
    // while still catching the front of one.
    const float coef = wanted < gain ? attack : release;
    gain = wanted + (gain - wanted) * coef;

    left[n] *= gain;
    right[n] *= gain;

    // The backstop. The gain is always a little behind the signal, so a fast
    // enough transient arrives before the reduction does, and this stage has
    // to be bounded whichever type is chosen.
    left[n] = shapeHard(left[n]);
    right[n] = shapeHard(right[n]);
  }
}

} // namespace ovt
