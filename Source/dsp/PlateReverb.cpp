#include "PlateReverb.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// The lengths, in samples at the rate the shape was written for, scaled to the
/// host rate. They are Dattorro's figures, chosen mutually prime so a hit
/// spreads rather than rings on a pitch.
constexpr double kDesignRate = 29761.0;

constexpr int kDiffuserLengths[] = {142, 107, 379, 277};
constexpr float kDiffuserGains[] = {0.75f, 0.75f, 0.625f, 0.625f};

/// Per branch: the modulated allpass, the delay after it, the second allpass,
/// and the delay that closes the branch.
constexpr int kFirstAllpass[] = {672, 908};
constexpr int kFirstDelay[] = {4453, 4217};
constexpr int kSecondAllpass[] = {1800, 2656};
constexpr int kSecondDelay[] = {3720, 3163};

/// Where each output is heard from: seven places across both branches, with
/// the signs Dattorro gives them.
///
/// Reading across the whole tank rather than from one line is what keeps a
/// long tail a wash rather than echoes of one circulating signal, and the
/// alternating signs stop the seven summing into a pulse.
struct Tap {
  /// Which half of the figure of eight.
  int branch;

  /// Which line in it: the delay after the first allpass, the second allpass's
  /// own line, or the delay that closes the branch.
  enum Line { FirstDelay, SecondAllpass, SecondDelay };
  Line line;

  /// How far into it, at the rate the shape was written for.
  int position;

  float sign;
};

constexpr Tap kTapsL[] = {
    {1, Tap::FirstDelay, 266, 1.0f},      {1, Tap::FirstDelay, 2974, 1.0f},
    {1, Tap::SecondAllpass, 1913, -1.0f}, {1, Tap::SecondDelay, 1996, 1.0f},
    {0, Tap::FirstDelay, 1990, -1.0f},    {0, Tap::SecondAllpass, 187, -1.0f},
    {0, Tap::SecondDelay, 1066, -1.0f},
};

constexpr Tap kTapsR[] = {
    {0, Tap::FirstDelay, 353, 1.0f},      {0, Tap::FirstDelay, 3627, 1.0f},
    {0, Tap::SecondAllpass, 1228, -1.0f}, {0, Tap::SecondDelay, 2673, 1.0f},
    {1, Tap::FirstDelay, 2111, -1.0f},    {1, Tap::SecondAllpass, 335, -1.0f},
    {1, Tap::SecondDelay, 121, -1.0f},
};

/// Seven taps summed, brought back to the room's level, so switching machines
/// under one MIX knob does not change the level.
constexpr float kTapScale = 0.53f;

/// How far the modulated allpasses wander, and how fast.
///
/// A length of time rather than a sample count, so it holds at any host rate.
/// A millisecond breaks up the repeating pattern of a long tail. The pitch
/// deviation is 2*pi*rate*depth, about twelve cents here, and twice the depth
/// would be heard as vibrato.
constexpr double kModDepthSeconds = 0.001;
constexpr double kModRateL = 1.10, kModRateR = 1.37;

inline int scaled(int length, double sampleRate) {
  return std::max(1,
                  (int)std::lround((double)length * sampleRate / kDesignRate));
}

inline float onePole(float hz, double sr) noexcept {
  return (float)std::exp(-kTwoPi * (double)hz / sr);
}
} // namespace

void PlateReverb::Line::resize(int length) {
  buffer.assign((size_t)std::max(1, length), 0.0f);
  write = 0;
}

void PlateReverb::Line::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
}

float PlateReverb::Line::at(int back) const noexcept {
  const auto length = (int)buffer.size();
  auto index = write - 1 - std::clamp(back, 0, length - 1);

  while (index < 0)
    index += length;

  return buffer[(size_t)index];
}

float PlateReverb::Line::atFraction(float back) const noexcept {
  const auto length = (int)buffer.size();
  const auto clamped = std::clamp(back, 0.0f, (float)(length - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  const auto a = at(whole);
  const auto b = at(whole + 1);

  return a + (b - a) * frac;
}

float PlateReverb::ModulatedAllpass::process(float x, double sr) noexcept {
  phase += rateHz / sr;

  if (phase >= 1.0)
    phase -= 1.0;

  const auto length = nominal + depth * (float)std::sin(kTwoPi * phase);
  const auto delayed = line.atFraction(length);
  const auto v = x + delayed * gain;

  line.push(v);

  return delayed - v * gain;
}

const PlateReverb::Line &PlateReverb::lineFor(const Branch &b,
                                              int which) const noexcept {
  switch ((Tap::Line)which) {
  case Tap::FirstDelay:
    return b.firstDelay;
  case Tap::SecondAllpass:
    return b.second.line;
  case Tap::SecondDelay:
    break;
  }

  return b.secondDelay;
}

void PlateReverb::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  preDelay.assign((size_t)(kMaxPreDelaySeconds * sampleRate) + 4, 0.0f);
  preWrite = 0;

  for (size_t i = 0; i < diffusers.size(); ++i) {
    diffusers[i].line.resize(scaled(kDiffuserLengths[i], sampleRate));
    diffusers[i].gain = kDiffuserGains[i];
  }

  const auto modDepth = (float)(kModDepthSeconds * sampleRate);

  for (size_t b = 0; b < tank.size(); ++b) {
    auto &branch = tank[b];

    // Room for the wander on top of the nominal length.
    branch.first.line.resize(scaled(kFirstAllpass[b], sampleRate) +
                             (int)modDepth + 4);
    branch.first.nominal = (float)scaled(kFirstAllpass[b], sampleRate);
    branch.first.depth = modDepth;
    branch.first.gain = 0.7f;
    branch.first.rateHz = b == 0 ? kModRateL : kModRateR;

    branch.firstDelay.resize(scaled(kFirstDelay[b], sampleRate));

    branch.second.line.resize(scaled(kSecondAllpass[b], sampleRate));
    branch.second.gain = 0.5f;

    branch.secondDelay.resize(scaled(kSecondDelay[b], sampleRate));
  }

  const auto resolve = [this](const Tap &tap) {
    const auto &line = lineFor(tank[(size_t)tap.branch], tap.line);

    return std::min(scaled(tap.position, sampleRate),
                    (int)line.buffer.size() - 1);
  };

  for (size_t i = 0; i < tapAtL.size(); ++i) {
    tapAtL[i] = resolve(kTapsL[i]);
    tapAtR[i] = resolve(kTapsR[i]);
  }

  reset();
}

void PlateReverb::reset() noexcept {
  std::fill(preDelay.begin(), preDelay.end(), 0.0f);
  preWrite = 0;

  for (auto &d : diffusers)
    d.line.clear();

  for (auto &b : tank) {
    b.first.line.clear();
    b.first.phase = 0.0;
    b.firstDelay.clear();
    b.second.line.clear();
    b.secondDelay.clear();
    b.damp = 0.0f;
  }

  crossed = {};
  wasEnabled = false;
}

float PlateReverb::tailSeconds(const ReverbParams &p) const noexcept {
  if (!p.enabled || p.type != ReverbType::Plate || p.mix <= 0.0f)
    return 0.0f;

  return std::min(30.0f, p.decaySeconds + p.preDelaySeconds);
}

void PlateReverb::process(float *outL, float *outR, int numSamples,
                          const ReverbParams &p) noexcept {
  if (numSamples <= 0 || preDelay.empty())
    return;

  if (!p.enabled || p.type != ReverbType::Plate) {
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto decay = std::clamp(p.decaySeconds, 0.1f, 20.0f);

  // The tank's gain, applied four times per circuit of the figure of eight
  // (twice in each branch), so each application is the fourth root of what
  // one circuit may keep. The circuit counts every line, allpasses included,
  // so DECAY means about the same here as on the room.
  double loop = 0.0;

  for (const auto &branch : tank)
    loop += (double)(branch.first.line.buffer.size() +
                     branch.firstDelay.buffer.size() +
                     branch.second.line.buffer.size() +
                     branch.secondDelay.buffer.size());

  loop /= sampleRate;

  const auto gain = std::clamp(
      (float)std::pow(0.001, loop / (4.0 * (double)decay)), 0.0f, 0.97f);

  // The damping filter is met twice per circuit, so its open corner sits at
  // 18 kHz, high enough not to dull a long tail and below Nyquist.
  const auto damping = std::clamp(p.damping, 0.0f, 1.0f);
  const auto dampHz = 18000.0f - damping * 16500.0f;
  const auto dampCoef = onePole(dampHz, sampleRate);

  const auto preLength = std::max(
      1, (int)(std::clamp(p.preDelaySeconds, 0.0f, kMaxPreDelaySeconds) *
               sampleRate));

  for (int n = 0; n < numSamples; ++n) {
    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // One plate, driven from the middle of the stereo field: a sheet has one
    // driver on it however many pickups are listening.
    preDelay[(size_t)preWrite] = 0.5f * (dryL + dryR);

    auto readAt = preWrite - preLength;
    while (readAt < 0)
      readAt += (int)preDelay.size();

    auto x = preDelay[(size_t)readAt];

    if (++preWrite >= (int)preDelay.size())
      preWrite = 0;

    // Scattered before it reaches the tank, so what goes round is already a
    // wash rather than a hit.
    for (auto &d : diffusers)
      x = d.process(x);

    std::array<float, 2> out{};

    for (size_t b = 0; b < tank.size(); ++b) {
      auto &branch = tank[b];

      // Each branch is fed the input plus what the other branch handed back,
      // which is the figure of eight.
      auto v = branch.first.process(x + crossed[1 - b] * gain, sampleRate);

      branch.firstDelay.push(v);
      v = branch.firstDelay.at((int)branch.firstDelay.buffer.size() - 1);

      branch.damp = v + (branch.damp - v) * dampCoef;
      v = branch.damp * gain;

      v = branch.second.process(v);

      branch.secondDelay.push(v);
      crossed[b] =
          branch.secondDelay.at((int)branch.secondDelay.buffer.size() - 1);
    }

    // Read across the whole tank, at seven places for each channel. See Tap.
    for (size_t i = 0; i < tapAtL.size(); ++i) {
      const auto &tap = kTapsL[i];
      out[0] +=
          tap.sign * lineFor(tank[(size_t)tap.branch], tap.line).at(tapAtL[i]);
    }

    for (size_t i = 0; i < tapAtR.size(); ++i) {
      const auto &tap = kTapsR[i];
      out[1] +=
          tap.sign * lineFor(tank[(size_t)tap.branch], tap.line).at(tapAtR[i]);
    }

    outL[n] = dryL + (out[0] * kTapScale - dryL) * mix;
    outR[n] = dryR + (out[1] * kTapScale - dryR) * mix;
  }
}

} // namespace ovt
