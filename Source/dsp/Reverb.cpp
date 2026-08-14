#include "Reverb.h"

#include <algorithm>
#include <cmath>

#include "SineTable.h"

namespace ovt {

namespace {
constexpr float kTwoPi = 6.283185307179586f;

/// Nominal line lengths in milliseconds. Deliberately not in any simple ratio:
/// lengths that share factors put their echoes on top of one another, which is
/// what a flutter is.
constexpr float kLineMs[Reverb::kLines] = {23.1f, 29.3f, 34.7f, 41.3f,
                                           47.9f, 53.3f, 61.7f, 67.1f};

/// One modulation rate per line, all different, none of them a multiple of
/// another, so the tail never breathes in step with itself.
constexpr double kModHz[Reverb::kLines] = {0.31, 0.43, 0.57, 0.69,
                                           0.83, 0.97, 1.13, 1.27};

/// The input scatter. Short, and different on the two sides so the wash does
/// not start out mono.
constexpr float kDiffuseLeftMs[Reverb::kAllpasses] = {4.77f, 3.59f, 12.73f,
                                                      9.31f};
constexpr float kDiffuseRightMs[Reverb::kAllpasses] = {5.13f, 3.91f, 13.61f,
                                                       10.07f};

constexpr float kDiffuseCoefficient = 0.65f;

/// Where the input is cut off before it reaches the network.
///
/// Fixed rather than offered as a control. A fundamental at full level feeding
/// a long tail floods everything above it, so this is never wanted open, and
/// far enough down that it never eats the note itself. On an instrument built
/// from 32 partials the interest is above here anyway.
constexpr float kLowCutHz = 175.0f;

/// Sets the wet level so that mix at 1 lands in the same country as the dry
/// signal it replaced. Chosen by measuring, not by taste.
constexpr float kOutputScale = 1.0f;

inline float onePole(float cutoffHz, double sampleRate) noexcept {
  const auto f = std::clamp((double)cutoffHz, 5.0, sampleRate * 0.45);
  return (float)std::exp(-kTwoPi * f / sampleRate);
}
} // namespace

// ---- the pieces -------------------------------------------------------------

void Reverb::Line::resize(int maxLength) {
  buffer.assign((size_t)std::max(4, maxLength), 0.0f);
  write = 0;
  damp = 0.0f;
}

void Reverb::Line::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
  damp = 0.0f;
}

float Reverb::Line::read(float delaySamples) const noexcept {
  const auto size = (int)buffer.size();
  const auto clamped = std::clamp(delaySamples, 1.0f, (float)(size - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  auto index = write - whole;
  while (index < 0)
    index += size;

  auto previous = index - 1;
  if (previous < 0)
    previous += size;

  const auto a = buffer[(size_t)index];
  const auto b = buffer[(size_t)previous];

  return a + (b - a) * frac;
}

void Reverb::Line::push(float x) noexcept {
  buffer[(size_t)write] = x;

  if (++write >= (int)buffer.size())
    write = 0;
}

void Reverb::Allpass::resize(int lengthSamples) {
  buffer.assign((size_t)std::max(1, lengthSamples), 0.0f);
  write = 0;
}

void Reverb::Allpass::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
}

float Reverb::Allpass::process(float x, float coefficient) noexcept {
  const auto delayed = buffer[(size_t)write];
  const auto stored = x + delayed * coefficient;

  buffer[(size_t)write] = stored;

  if (++write >= (int)buffer.size())
    write = 0;

  return delayed - stored * coefficient;
}

// ---- the network ------------------------------------------------------------

void Reverb::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  for (int i = 0; i < kLines; ++i) {
    baseSamples[(size_t)i] = (float)(kLineMs[i] * 0.001 * sampleRate);

    // Room for the longest cut, the modulation swinging past it and a little
    // slack for the interpolator.
    lines[(size_t)i].resize((int)(baseSamples[(size_t)i] * kMaxScale * 1.05f) +
                            8);
  }

  for (int i = 0; i < kAllpasses; ++i) {
    diffusionL[(size_t)i].resize((int)(kDiffuseLeftMs[i] * 0.001 * sampleRate) +
                                 1);
    diffusionR[(size_t)i].resize(
        (int)(kDiffuseRightMs[i] * 0.001 * sampleRate) + 1);
  }

  preDelayLength = (int)(kMaxPreDelaySeconds * sampleRate) + 4;
  preDelayL.assign((size_t)preDelayLength, 0.0f);
  preDelayR.assign((size_t)preDelayLength, 0.0f);

  reset();
}

void Reverb::reset() noexcept {
  for (auto &l : lines) {
    l.clear();
    l.modPhase = 0.0;
  }

  for (auto &a : diffusionL)
    a.clear();
  for (auto &a : diffusionR)
    a.clear();

  std::fill(preDelayL.begin(), preDelayL.end(), 0.0f);
  std::fill(preDelayR.begin(), preDelayR.end(), 0.0f);
  preDelayWrite = 0;

  lowCutL = 0.0f;
  lowCutR = 0.0f;
  smoothedScale = -1.0f;
  wasEnabled = false;
}

float Reverb::tailSeconds(const ReverbParams &p) const noexcept {
  if (!p.enabled || p.mix <= 0.0f)
    return 0.0f;

  return p.preDelaySeconds + std::clamp(p.decaySeconds, 0.1f, 30.0f);
}

void Reverb::process(float *outL, float *outR, int numSamples,
                     const ReverbParams &p) noexcept {
  if (numSamples <= 0 || lines[0].buffer.empty())
    return;

  if (!p.enabled) {
    // Leaving a tail sitting in the buffers would mean switching back on
    // replayed whatever was ringing when it was switched off.
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto width = std::clamp(p.width, 0.0f, 1.0f);

  const auto rt60 = std::clamp(p.decaySeconds, 0.1f, 30.0f);

  // The room is sized from the decay rather than set separately. A long tail in
  // a small room is a spring rather than a place, and a short one in a hall is
  // a gate, so the two were always turned together anyway. Logarithmic, since
  // that is how the decay control itself is scaled.
  const auto size = std::clamp((std::log(rt60) - std::log(0.2f)) /
                                   (std::log(20.0f) - std::log(0.2f)),
                               0.0f, 1.0f);

  const auto targetScale = 0.35f + size * (kMaxScale - 0.35f);

  if (smoothedScale < 0.0f)
    smoothedScale = targetScale;

  // Half a second to walk from one room to another.
  const auto sizeGlide = (float)std::exp(-1.0 / (0.5 * sampleRate));

  // Damping runs the loop filter from wide open down to something quite dark,
  // which is the difference between a plate and a room full of curtains.
  const auto damping = std::clamp(p.damping, 0.0f, 1.0f);
  const auto dampHz = 800.0f + (1.0f - damping) * (1.0f - damping) * 15000.0f;
  const auto dampCoef = onePole(dampHz, sampleRate);
  const auto lowCutCoef = onePole(kLowCutHz, sampleRate);

  const auto preDelaySamples =
      std::clamp((float)(p.preDelaySeconds * sampleRate), 1.0f,
                 (float)(preDelayLength - 2));

  const auto &sine = SineTable::instance();

  std::array<float, kLines> delay{}, gain{}, modDepth{};
  std::array<double, kLines> modStep{};

  for (int i = 0; i < kLines; ++i) {
    delay[(size_t)i] = baseSamples[(size_t)i] * targetScale;
    modDepth[(size_t)i] = 3.0f + 0.0035f * delay[(size_t)i];
    modStep[(size_t)i] = kModHz[i] / sampleRate;

    // 60 dB over the requested time, given how often this line goes round.
    const auto seconds = delay[(size_t)i] / (float)sampleRate;
    gain[(size_t)i] = std::pow(10.0f, -3.0f * seconds / rt60);
  }

  for (int n = 0; n < numSamples; ++n) {
    const auto dryL = outL[n];
    const auto dryR = outR[n];

    smoothedScale = targetScale + (smoothedScale - targetScale) * sizeGlide;

    // ---- pre-delay --------------------------------------------------------
    preDelayL[(size_t)preDelayWrite] = dryL;
    preDelayR[(size_t)preDelayWrite] = dryR;

    auto readIndex = preDelayWrite - (int)preDelaySamples;
    while (readIndex < 0)
      readIndex += preDelayLength;

    auto inL = preDelayL[(size_t)readIndex];
    auto inR = preDelayR[(size_t)readIndex];

    if (++preDelayWrite >= preDelayLength)
      preDelayWrite = 0;

    // ---- low cut, so the fundamental does not flood the tail ---------------
    lowCutL = inL + (lowCutL - inL) * lowCutCoef;
    lowCutR = inR + (lowCutR - inR) * lowCutCoef;

    inL -= lowCutL;
    inR -= lowCutR;

    // ---- scatter ----------------------------------------------------------
    for (int i = 0; i < kAllpasses; ++i) {
      inL = diffusionL[(size_t)i].process(inL, kDiffuseCoefficient);
      inR = diffusionR[(size_t)i].process(inR, kDiffuseCoefficient);
    }

    // ---- the network ------------------------------------------------------
    std::array<float, kLines> tap{};
    float sum = 0.0f;

    for (int i = 0; i < kLines; ++i) {
      auto &line = lines[(size_t)i];

      line.modPhase = wrapPhase(line.modPhase + modStep[(size_t)i]);

      const auto length = baseSamples[(size_t)i] * smoothedScale +
                          modDepth[(size_t)i] * sine(line.modPhase);

      const auto raw = line.read(length);

      // The damping filter sits in the loop, so every pass round costs the
      // tail a little more of its top end rather than all of it at once.
      line.damp = raw + (line.damp - raw) * dampCoef;

      tap[(size_t)i] = line.damp;
      sum += line.damp;
    }

    // Householder: y = x - (2/N) sum(x). Orthogonal, so the matrix itself
    // neither adds nor removes energy, and one multiply covers all eight.
    const auto fold = sum * (2.0f / (float)kLines);

    for (int i = 0; i < kLines; ++i) {
      const auto injected = (i % 2 == 0) ? inL : inR;

      lines[(size_t)i].push((tap[(size_t)i] - fold) * gain[(size_t)i] +
                            injected * 0.5f);
    }

    // ---- out --------------------------------------------------------------
    // Alternating signs on the taps, so the two sides are built from different
    // lines in different polarities and the wash is wide by construction.
    const auto wetL = (tap[0] - tap[2] + tap[4] - tap[6]) * kOutputScale;
    const auto wetR = (tap[1] - tap[3] + tap[5] - tap[7]) * kOutputScale;

    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR) * width;

    outL[n] = dryL + ((mid + side) - dryL) * mix;
    outR[n] = dryR + ((mid - side) - dryR) * mix;
  }
}

} // namespace ovt
