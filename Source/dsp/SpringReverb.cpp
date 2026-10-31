#include "SpringReverb.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// How long a wave takes to cross each spring, in seconds. The lengths share no
/// small ratio so the three trains of repeats never land together.
constexpr double kTransits[] = {0.0291, 0.0330, 0.0371};

/// Where each spring is heard from, per channel.
///
/// One tray, driven at one end, heard at two points along it. The short spring
/// is mostly on the left and the long one mostly on the right, with the middle
/// one shared, so the two sides differ in which chirp arrives first rather than
/// in being a delayed copy of one another.
constexpr float kWeightsL[] = {0.80f, 0.35f, 0.10f};
constexpr float kWeightsR[] = {0.10f, 0.35f, 0.80f};

/// Three springs summed, brought back to the level of the room and the plate,
/// so switching machines under one MIX knob does not change the level.
constexpr float kTapScale = 1.44f;

/// How many allpasses each spring disperses through, at kSectionRate.
///
/// The count is scaled to the host rate, since the chirp is a length of time.
/// Sixty keeps most of the chirp of a longer chain at well under half the cost,
/// the chain being the expensive part of this file.
constexpr int kSections = 60;
constexpr double kSectionRate = 48000.0;

/// The dispersion coefficients, walked across the chain so the chirp is
/// smeared rather than a single pure sweep.
constexpr float kLowestCoefficient = -0.74f;
constexpr float kHighestCoefficient = -0.82f;

/// What the tray passes, and where DAMP moves within it.
///
/// kPickupHz is the transducer, outside the loop and met once. kOpenHz and
/// kClosedHz are the spring's own loss, inside it and met on every bounce,
/// which is what DAMP sets. kOpenHz can sit high because the pickup already
/// bounds the band.
constexpr float kRumbleHz = 80.0f;
constexpr float kPickupHz = 5000.0f;
constexpr float kOpenHz = 10000.0f;
constexpr float kClosedHz = 900.0f;

inline float onePole(float hz, double sr) noexcept {
  return (float)std::exp(-kTwoPi * (double)hz / sr);
}
} // namespace

void SpringReverb::Spring::resize(int transitSamples, int sections,
                                  float lowest, float highest) {
  line.assign((size_t)std::max(1, transitSamples), 0.0f);
  write = 0;

  dispersion.assign((size_t)std::max(1, sections), Allpass{});

  const auto last = std::max<size_t>(1, dispersion.size() - 1);

  for (size_t i = 0; i < dispersion.size(); ++i)
    dispersion[i].a = lowest + (highest - lowest) * ((float)i / (float)last);
}

void SpringReverb::Spring::clear() noexcept {
  std::fill(line.begin(), line.end(), 0.0f);
  write = 0;

  for (auto &section : dispersion)
    section.state = 0.0f;

  dark = rumble = tail = 0.0f;
}

void SpringReverb::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  preDelay.assign((size_t)(kMaxPreDelaySeconds * sampleRate) + 4, 0.0f);
  preWrite = 0;

  const auto sections =
      std::max(1, (int)std::lround(kSections * sampleRate / kSectionRate));

  for (size_t i = 0; i < springs.size(); ++i)
    springs[i].resize((int)std::lround(kTransits[i] * sampleRate), sections,
                      kLowestCoefficient, kHighestCoefficient);

  reset();
}

void SpringReverb::reset() noexcept {
  std::fill(preDelay.begin(), preDelay.end(), 0.0f);
  preWrite = 0;

  for (auto &spring : springs)
    spring.clear();

  pickupL.clear();
  pickupR.clear();
  wasEnabled = false;
}

float SpringReverb::tailSeconds(const ReverbParams &p) const noexcept {
  if (!p.enabled || p.type != ReverbType::Spring || p.mix <= 0.0f)
    return 0.0f;

  return std::min(30.0f, p.decaySeconds + p.preDelaySeconds);
}

void SpringReverb::process(float *outL, float *outR, int numSamples,
                           const ReverbParams &p) noexcept {
  if (numSamples <= 0 || preDelay.empty())
    return;

  if (!p.enabled || p.type != ReverbType::Spring) {
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto decay = std::clamp(p.decaySeconds, 0.1f, 20.0f);

  const auto damping = std::clamp(p.damping, 0.0f, 1.0f);
  // DAMP moves the loop corner geometrically, so it moves evenly in octaves.
  const auto darkCoef =
      onePole(kOpenHz * std::pow(kClosedHz / kOpenHz, damping), sampleRate);
  const auto rumbleCoef = onePole(kRumbleHz, sampleRate);
  const auto pickupCoef = onePole(kPickupHz, sampleRate);

  // Each spring loses the same proportion per second rather than per bounce,
  // so the tail keeps its colour as it fades. The loop length counts the
  // dispersion chain at its order in samples, which is the mean group delay of
  // an allpass chain whatever its coefficients.
  std::array<float, 3> gains{};

  for (size_t i = 0; i < springs.size(); ++i) {
    const auto loop =
        (double)(springs[i].line.size() + springs[i].dispersion.size()) /
        sampleRate;

    gains[i] =
        std::clamp((float)std::pow(0.001, loop / (double)decay), 0.0f, 0.995f);
  }

  const auto preLength = std::max(
      1, (int)(std::clamp(p.preDelaySeconds, 0.0f, kMaxPreDelaySeconds) *
               sampleRate));

  for (int n = 0; n < numSamples; ++n) {
    const auto dryL = outL[n];
    const auto dryR = outR[n];

    // One transducer on one tray, so the input is mono.
    preDelay[(size_t)preWrite] = 0.5f * (dryL + dryR);

    auto readAt = preWrite - preLength;
    while (readAt < 0)
      readAt += (int)preDelay.size();

    const auto x = preDelay[(size_t)readAt];

    if (++preWrite >= (int)preDelay.size())
      preWrite = 0;

    float wetL = 0.0f, wetR = 0.0f;

    for (size_t i = 0; i < springs.size(); ++i) {
      auto &spring = springs[i];

      auto v = x + spring.tail * gains[i];

      // Dispersed on every pass round the loop.
      for (auto &section : spring.dispersion)
        v = section.process(v);

      // Read before the write, which makes the delay exactly its length.
      const auto arrived = spring.line[(size_t)spring.write];

      spring.line[(size_t)spring.write] = v;

      if (++spring.write >= (int)spring.line.size())
        spring.write = 0;

      spring.dark = arrived + (spring.dark - arrived) * darkCoef;

      auto heard = spring.dark;

      spring.rumble = heard + (spring.rumble - heard) * rumbleCoef;
      heard -= spring.rumble;

      spring.tail = heard;

      wetL += heard * kWeightsL[i];
      wetR += heard * kWeightsR[i];
    }

    // The pickup, once, on the way out. See Pickup.
    const auto heardL = pickupL.process(wetL, pickupCoef);
    const auto heardR = pickupR.process(wetR, pickupCoef);

    outL[n] = dryL + (heardL * kTapScale - dryL) * mix;
    outR[n] = dryR + (heardR * kTapScale - dryR) * mix;
  }
}

} // namespace ovt
