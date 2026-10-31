#include "OutputStage.h"

namespace ovt {

namespace {
/// Where Soft stops being linear.
constexpr float kSoftKnee = 0.7f;

/// Where the biased stage lets each half of the wave go before it bends.
/// The two halves give way at different levels, which gives a second
/// harmonic where Soft and Hard give a third.
constexpr float kBiasUp = 0.5f;
constexpr float kBiasDown = 0.9f;

/// The ceiling the limiter works to, a little under one, so a transient that
/// gets past the gain has headroom before the hard clip behind it.
constexpr float kLimitCeiling = 0.98f;

/// The console's limiter's ceiling, -0.1 dBFS, as the measured chain is set.
constexpr float kConsoleCeiling = 0.98855309f;

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

float shapeAsymmetric(float x) noexcept {
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
  // A triangle fold, done in closed form rather than by looping, so a sample
  // of a hundred costs the same as one of a half.
  const float wrapped =
      std::fmod(x + 1.0f, 4.0f) + (x + 1.0f < 0.0f ? 4.0f : 0.0f);

  return 1.0f - std::abs(wrapped - 2.0f);
}
} // namespace

void OutputStage::prepare(double sampleRate) noexcept {
  rate = std::max(1.0, sampleRate);

  fullDelay = lookaheadSamples(rate);
  fullWindow = std::max(1, fullDelay / 2);
  fullBox = std::max(1, fullDelay / 2);

  // Sized for the window whether or not it is wanted, so that turning it on
  // later allocates nothing on the audio thread. The active lengths follow in
  // applyLookahead.
  delay = fullDelay;
  window = fullWindow;
  box = fullBox;
  boxScale = 1.0 / (double)box;
  looking = true;

  // A hundred milliseconds to let back up, slow enough not to pump on every
  // note. There is no attack coefficient: the lookahead and the two box
  // filters set how fast it pulls down.
  release = (float)std::exp(-1.0 / (0.100 * rate));

  lineLeft.assign((size_t)std::max(1, fullDelay), 0.0f);
  lineRight.assign((size_t)std::max(1, fullDelay), 0.0f);

  // One longer than the window, so the wedge can hold a full window and the
  // sample displacing its oldest at the same moment.
  wedgeValue.assign((size_t)fullWindow + 1, 0.0f);
  wedgeIndex.assign((size_t)fullWindow + 1, 0);

  boxOne.assign((size_t)fullBox, 1.0f);
  boxTwo.assign((size_t)fullBox, 1.0f);

  console.prepare(rate);
  cassette.prepare(rate);
  printConsole.prepare(rate);
  printCassette.prepare(rate);
  printLimiter.release = release;
  fadeLength = std::max(1, (int)(kFadeSeconds * rate + 0.5));

  reset();
}

void OutputStage::applyLookahead(bool ahead) noexcept {
  if (ahead == looking)
    return;

  looking = ahead;

  delay = ahead ? fullDelay : 0;
  window = ahead ? fullWindow : 1;
  box = ahead ? fullBox : 1;
  boxScale = 1.0 / (double)box;

  reset();
}

void OutputStage::reset() noexcept {
  held = 1.0f;

  std::fill(lineLeft.begin(), lineLeft.end(), 0.0f);
  std::fill(lineRight.begin(), lineRight.end(), 0.0f);
  writeIndex = 0;

  wedgeHead = wedgeTail = 0;
  seen = 0;

  // Primed at unity, so the first sample after a reset leaves at the level it
  // arrived at instead of fading in from silence.
  std::fill(boxOne.begin(), boxOne.end(), 1.0f);
  std::fill(boxTwo.begin(), boxTwo.end(), 1.0f);
  boxOneAt = boxTwoAt = 0;
  boxOneSum = boxTwoSum = (double)box;

  console.reset();
  cassette.reset();
  printConsole.reset();
  printCassette.reset();
  printLimiter.held = 1.0f;

  // Whatever is chosen next starts from here, with nothing to fade from.
  started = false;
  leaving = Running::None;
  fadeAt = fadeLength;
}

void OutputStage::InstantLimiter::process(float &left, float &right,
                                          float ceiling) noexcept {
  const float peak = std::max(std::abs(left), std::abs(right));
  const float wanted = peak > ceiling ? ceiling / peak : 1.0f;

  held = wanted < held ? wanted : wanted + (held - wanted) * release;

  left *= held;
  right *= held;
}

void OutputStage::run(Running which, float &left, float &right) noexcept {
  if (which == Running::Console) {
    console.process(left, right);
  } else if (which == Running::Cassette) {
    cassette.process(left, right);
  } else if (which == Running::Print) {
    printConsole.process(left, right);
    printLimiter.process(left, right, kConsoleCeiling);
    printCassette.process(left, right);
  }
}

float OutputStage::delayed(std::vector<float> &line, float in) noexcept {
  const float out = line[(size_t)writeIndex];
  line[(size_t)writeIndex] = in;

  return out;
}

float OutputStage::gainFor(float peak, float ceiling) noexcept {
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

  const float wanted = worst > ceiling ? ceiling / worst : 1.0f;

  // Straight down to whatever is needed, and back up on the release. The
  // boxes below smooth the step.
  held = wanted < held ? wanted : wanted + (held - wanted) * release;

  // Two box filters. One alone turns the step into a straight ramp with a
  // corner at each end, and the second rounds the corners off.
  boxOneSum += held - boxOne[(size_t)boxOneAt];
  boxOne[(size_t)boxOneAt] = held;
  boxOneAt = (boxOneAt + 1) % box;

  // Scaled by the reciprocal worked out in applyLookahead rather than
  // divided on every sample.
  const float once = (float)(boxOneSum * boxScale);

  boxTwoSum += once - boxTwo[(size_t)boxTwoAt];
  boxTwo[(size_t)boxTwoAt] = once;
  boxTwoAt = (boxTwoAt + 1) % box;

  return (float)(boxTwoSum * boxScale);
}

void OutputStage::process(float *left, float *right, int numSamples,
                          ClipType type, bool shaping, bool ahead) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  // Once per block. It does nothing unless the setting changed.
  applyLookahead(ahead);

  // Nothing is allocated from here on, so a stage that was never prepared, or
  // a rate where two milliseconds is under a sample, runs without its delay.
  const bool delaying = delay > 0 && !lineLeft.empty();

  auto *shape = type == ClipType::Hard         ? shapeHard
                : type == ClipType::Asymmetric ? shapeAsymmetric
                : type == ClipType::Fold       ? shapeFold
                                               : shapeSoft;

  const bool consoling = shaping && type == ClipType::Console;
  const bool taping = shaping && type == ClipType::Cassette;
  const bool printing = shaping && type == ClipType::Print;

  // The recorder and the print end in the same limiter as the console.
  const float ceiling =
      consoling || taping || printing ? kConsoleCeiling : kLimitCeiling;

  // Which machine with memory this block wants, and whether that is a change.
  // Once per block, as a host changes the type between blocks.
  const auto wanted = consoling  ? Running::Console
                      : taping   ? Running::Cassette
                      : printing ? Running::Print
                                 : Running::None;

  if (!started) {
    running = wanted;
    started = true;
  } else if (wanted != running) {
    leaving = running;
    running = wanted;
    fadeAt = 0;

    if (running == Running::Console) {
      console.reset();
    } else if (running == Running::Cassette) {
      cassette.reset();
    } else if (running == Running::Print) {
      printConsole.reset();
      printCassette.reset();
      printLimiter.held = 1.0f;
    }
  }

  for (int n = 0; n < numSamples; ++n) {
    // Ahead of the delay, so the limiter after it looks ahead at the
    // machine's output.
    const float dryLeft = left[n], dryRight = right[n];
    float inLeft = dryLeft, inRight = dryRight;
    run(running, inLeft, inRight);

    if (fadeAt < fadeLength) {
      float wasLeft = dryLeft, wasRight = dryRight;
      run(leaving, wasLeft, wasRight);

      const float t = (float)fadeAt / (float)fadeLength;
      inLeft = wasLeft + (inLeft - wasLeft) * t;
      inRight = wasRight + (inRight - wasRight) * t;

      if (++fadeAt == fadeLength)
        leaving = Running::None;
    }

    // Linked, from whichever channel is louder, so the image holds still. Run
    // for every type, so choosing the limiter part way through finds a detector
    // that already knows the last window.
    const float gain =
        gainFor(std::max(std::abs(inLeft), std::abs(inRight)), ceiling);

    const float outLeft = delaying ? delayed(lineLeft, inLeft) : inLeft;
    const float outRight = delaying ? delayed(lineRight, inRight) : inRight;

    if (delaying)
      writeIndex = (writeIndex + 1) % delay;

    if (!shaping) {
      left[n] = outLeft;
      right[n] = outRight;
      continue;
    }

    if (type == ClipType::Limiter || type == ClipType::Console ||
        type == ClipType::Cassette || type == ClipType::Print) {
      // The backstop that keeps the stage bounded whatever arrives, such as a
      // rate so low that the window is a couple of samples.
      left[n] = shapeHard(outLeft * gain);
      right[n] = shapeHard(outRight * gain);
      continue;
    }

    left[n] = shape(outLeft);
    right[n] = shape(outRight);
  }
}

} // namespace ovt
