#include "PlateReverb.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// The lengths, in samples at the rate the shape was written for.
///
/// Dattorro's figures, scaled to whatever rate the host is running at. They
/// are what they are because they are mutually prime and because the ratios
/// between them are what makes a hit spread rather than repeat: change one and
/// the tank either rings on a pitch or thins out.
constexpr double kDesignRate = 29761.0;

constexpr int kDiffuserLengths[] = {142, 107, 379, 277};
constexpr float kDiffuserGains[] = {0.75f, 0.75f, 0.625f, 0.625f};

/// Per branch: the modulated allpass, the delay after it, the second allpass,
/// and the delay that closes the branch.
constexpr int kFirstAllpass[] = {672, 908};
constexpr int kFirstDelay[] = {4453, 4217};
constexpr int kSecondAllpass[] = {1800, 2656};
constexpr int kSecondDelay[] = {3720, 3163};

/// Where each output is taken from, which is the other branch rather than its
/// own. Several taps each, so a channel is a sum of places on the plate rather
/// than one point on it.
constexpr int kTapsL[] = {266, 2974, 1913, 1996};
constexpr int kTapsR[] = {353, 3627, 1228, 2673};

/// How far the modulated allpasses wander, and how fast. Slow and shallow:
/// enough that nothing settles on a pitch, not enough to hear as movement.
constexpr float kModDepth = 8.0f;
constexpr double kModRateL = 0.70, kModRateR = 0.83;

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

void PlateReverb::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  preDelay.assign((size_t)(kMaxPreDelaySeconds * sampleRate) + 4, 0.0f);
  preWrite = 0;

  for (size_t i = 0; i < diffusers.size(); ++i) {
    diffusers[i].line.resize(scaled(kDiffuserLengths[i], sampleRate));
    diffusers[i].gain = kDiffuserGains[i];
  }

  for (size_t b = 0; b < tank.size(); ++b) {
    auto &branch = tank[b];

    // Room for the wander on top of the nominal length.
    branch.first.line.resize(scaled(kFirstAllpass[b], sampleRate) +
                             (int)kModDepth + 4);
    branch.first.nominal = (float)scaled(kFirstAllpass[b], sampleRate);
    branch.first.depth = kModDepth;
    branch.first.gain = 0.7f;
    branch.first.rateHz = b == 0 ? kModRateL : kModRateR;

    branch.firstDelay.resize(scaled(kFirstDelay[b], sampleRate));

    branch.second.line.resize(scaled(kSecondAllpass[b], sampleRate));
    branch.second.gain = 0.5f;

    branch.secondDelay.resize(scaled(kSecondDelay[b], sampleRate));
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

  // The tank's gain rather than a room's size, since a plate has no size.
  //
  // What sets it is how much has to be left after one circuit of the figure of
  // eight. Two things about that circuit are easy to get wrong and both were:
  //
  // The gain is applied twice in each branch, at the cross and again after the
  // damping, so a full circuit multiplies by it four times and each one has to
  // be the fourth root of what the circuit may keep. Sizing it for a single
  // application gave a two-and-a-half second setting a tail gone in half of
  // one.
  //
  // And the circuit is every line it passes through, allpasses included.
  // Counting only the four delays leaves the loop a third shorter than it is
  // and the tail a third too long: 3.0 s where the room gave 2.2.
  //
  // With both right the two machines agree about what the knob means, which is
  // the point, since one knob serves all three. Measured from a click, with
  // damping open:
  //
  //   asked  |  plate  |  room
  //    1.0 s |  1.17 s | 0.96 s
  //    2.5 s |  2.27 s | 2.23 s
  //    6.0 s |  4.90 s | 4.89 s
  //
  // Both fall short at six seconds for the same reason: the damping filter is
  // never fully open, so the top of the spectrum decays faster than the figure
  // asks and the measurement follows the whole band. A tail that long is held
  // rather than counted, so neither was corrected for it.
  double loop = 0.0;

  for (const auto &branch : tank)
    loop += (double)(branch.first.line.buffer.size() +
                     branch.firstDelay.buffer.size() +
                     branch.second.line.buffer.size() +
                     branch.secondDelay.buffer.size());

  loop /= sampleRate;

  const auto gain = std::clamp(
      (float)std::pow(0.001, loop / (4.0 * (double)decay)), 0.0f, 0.97f);

  // A plate loses its top end to the air, which is what stops a bright tank
  // from ringing like a cymbal. Wide open it is still not a bright room.
  const auto damping = std::clamp(p.damping, 0.0f, 1.0f);
  const auto dampHz = 12000.0f - damping * 10500.0f;
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

    // Taken across the other branch rather than from the end of its own, at
    // several places each, which is what gives the two channels a plate's
    // width rather than one being a delayed copy of the other.
    for (const auto tap : kTapsL)
      out[0] += tank[1].firstDelay.at(std::min(
          scaled(tap, sampleRate), (int)tank[1].firstDelay.buffer.size() - 1));

    for (const auto tap : kTapsR)
      out[1] += tank[0].firstDelay.at(std::min(
          scaled(tap, sampleRate), (int)tank[0].firstDelay.buffer.size() - 1));

    // Four taps summed, so the level is brought back to one of them.
    constexpr float kTapScale = 0.25f;

    outL[n] = dryL + (out[0] * kTapScale - dryL) * mix;
    outR[n] = dryR + (out[1] * kTapScale - dryR) * mix;
  }
}

} // namespace ovt
