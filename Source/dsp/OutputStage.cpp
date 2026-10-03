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

  delay = lookaheadSamples(rate);
  window = std::max(1, delay / 2);
  box = std::max(1, delay / 2);
  boxScale = 1.0 / (double)box;

  // A hundred milliseconds to let back up, which is slow enough not to pump
  // on every note. There is no attack time any more: the gain is told what is
  // coming while it is still in the delay line, so what used to be an attack
  // is now the two box filters, and how fast it can pull down is set by how
  // far ahead it is allowed to look rather than by a coefficient.
  release = (float)std::exp(-1.0 / (0.100 * rate));

  lineLeft.assign((size_t)std::max(1, delay), 0.0f);
  lineRight.assign((size_t)std::max(1, delay), 0.0f);

  // One longer than the window, so the wedge can hold a full window and the
  // sample displacing its oldest at the same moment.
  wedgeValue.assign((size_t)window + 1, 0.0f);
  wedgeIndex.assign((size_t)window + 1, 0);

  boxOne.assign((size_t)box, 1.0f);
  boxTwo.assign((size_t)box, 1.0f);

  reset();
}

void OutputStage::reset() noexcept {
  held = 1.0f;

  std::fill(lineLeft.begin(), lineLeft.end(), 0.0f);
  std::fill(lineRight.begin(), lineRight.end(), 0.0f);
  writeIndex = 0;

  wedgeHead = wedgeTail = 0;
  seen = 0;

  // Primed at unity rather than at nothing, so the first sample after a reset
  // leaves at the level it arrived at. Filled with zeros these would spend a
  // window climbing out of silence, which is a fade-in on every transport
  // stop and start.
  std::fill(boxOne.begin(), boxOne.end(), 1.0f);
  std::fill(boxTwo.begin(), boxTwo.end(), 1.0f);
  boxOneAt = boxTwoAt = 0;
  boxOneSum = boxTwoSum = (double)box;
}

float OutputStage::delayed(std::vector<float> &line, float in) noexcept {
  const float out = line[(size_t)writeIndex];
  line[(size_t)writeIndex] = in;

  return out;
}

float OutputStage::gainFor(float peak) noexcept {
  // Drop everything the newcomer is at least as loud as: none of it can be
  // the maximum of any window this one is in.
  const auto capacity = (int)wedgeValue.size();

  while (wedgeTail > wedgeHead &&
         wedgeValue[(size_t)((wedgeTail - 1) % capacity)] <= peak)
    --wedgeTail;

  wedgeValue[(size_t)(wedgeTail % capacity)] = peak;
  wedgeIndex[(size_t)(wedgeTail % capacity)] = seen;
  ++wedgeTail;

  // And drop the front once it has fallen out of the window behind us.
  if (wedgeIndex[(size_t)(wedgeHead % capacity)] <= seen - (std::int64_t)window)
    ++wedgeHead;

  ++seen;

  const float worst =
      wedgeTail > wedgeHead ? wedgeValue[(size_t)(wedgeHead % capacity)] : 0.0f;

  const float wanted = worst > kLimitCeiling ? kLimitCeiling / worst : 1.0f;

  // Straight down to whatever is needed, and back up on the release. The
  // boxes below are what stop that step being heard as a step.
  held = wanted < held ? wanted : wanted + (held - wanted) * release;

  // Two box filters. One alone turns the step into a straight ramp with a
  // corner at each end, and the second rounds the corners off, which is the
  // difference between a gain that arrives in time and one that arrives in
  // time without being audible on its way.
  boxOneSum += held - boxOne[(size_t)boxOneAt];
  boxOne[(size_t)boxOneAt] = held;
  boxOneAt = (boxOneAt + 1) % box;

  // Scaled rather than divided. The length never changes between prepares, so
  // the reciprocal is worked out once instead of twice on every sample.
  const float once = (float)(boxOneSum * boxScale);

  boxTwoSum += once - boxTwo[(size_t)boxTwoAt];
  boxTwo[(size_t)boxTwoAt] = once;
  boxTwoAt = (boxTwoAt + 1) % box;

  return (float)(boxTwoSum * boxScale);
}

void OutputStage::process(float *left, float *right, int numSamples,
                          ClipType type, bool shaping) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  // Nothing is allocated from here on, so a stage that was never prepared
  // runs without its delay rather than reaching for one. The same path covers
  // a rate low enough that two milliseconds is not a whole sample.
  const bool delaying = delay > 0 && !lineLeft.empty();

  auto *shape = type == ClipType::Hard   ? shapeHard
                : type == ClipType::Bias ? shapeBias
                : type == ClipType::Fold ? shapeFold
                                         : shapeSoft;

  for (int n = 0; n < numSamples; ++n) {
    // Linked, from whichever channel is louder, so the image holds still.
    // Run whatever the type is, so that choosing the limiter half way through
    // a note finds a detector that already knows what the last window held
    // rather than one starting from silence.
    const float gain = gainFor(std::max(std::abs(left[n]), std::abs(right[n])));

    const float outLeft = delaying ? delayed(lineLeft, left[n]) : left[n];
    const float outRight = delaying ? delayed(lineRight, right[n]) : right[n];

    if (delaying)
      writeIndex = (writeIndex + 1) % delay;

    if (!shaping) {
      left[n] = outLeft;
      right[n] = outRight;
      continue;
    }

    if (type == ClipType::Limiter) {
      // The backstop. It has nothing to do now that the gain arrives with the
      // peak rather than after it, and it stays because this stage has to be
      // bounded whatever arrives: a rate so low that the window is a couple of
      // samples, or a patch that moves faster than the window is long.
      left[n] = shapeHard(outLeft * gain);
      right[n] = shapeHard(outRight * gain);
      continue;
    }

    left[n] = shape(outLeft);
    right[n] = shape(outRight);
  }
}

} // namespace ovt
