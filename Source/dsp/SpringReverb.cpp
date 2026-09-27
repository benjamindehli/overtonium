#include "SpringReverb.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// How long a wave takes to cross each spring, in seconds.
///
/// A real tank is around thirty milliseconds end to end. Three of them, at
/// lengths that share no small ratio, so the three trains of repeats never
/// land together: a tray of identical springs would be one spring three times
/// as loud, which is why nobody builds one.
constexpr double kTransits[] = {0.0291, 0.0330, 0.0371};

/// Where each spring is heard from, per channel.
///
/// One tray, driven at one end, heard at two points along it. The short spring
/// is mostly on the left and the long one mostly on the right, with the middle
/// one shared, so the two sides differ in which chirp arrives first rather than
/// in being a delayed copy of one another.
constexpr float kWeightsL[] = {0.80f, 0.35f, 0.10f};
constexpr float kWeightsR[] = {0.10f, 0.35f, 0.80f};

/// Three springs summed, brought back to about the level of one.
constexpr float kTapScale = 0.62f;

/// How many allpasses each spring disperses through, at the rate they were
/// counted for.
///
/// It is a count here but a length of time in the tray, so it has to follow
/// the host rate or a session at 96 kHz would boing half as long as the same
/// session at 48.
///
/// Sixty of them, not the hundred this started at, because a chain is the one
/// expensive thing in the file and a steeper coefficient buys back most of
/// what a shorter chain gives up. How far a click's bottom end lands behind
/// its top, against what the three springs cost on one core:
///
///   sections | coefficients | chirp   | cost
///      100   | 0.60 to 0.70 | 7.3 ms  | 4.95 %
///       70   | 0.70 to 0.79 | 6.9 ms  | 3.61 %
///       60   | 0.74 to 0.82 | 6.5 ms  | 2.23 %
///       50   | 0.78 to 0.86 | 5.7 ms  | 1.92 %
///
/// Sixty is where the curve turns: it keeps nine tenths of the boing for less
/// than half the work, and below it the chirp starts going faster than the
/// saving. For scale, eight voices come to around eight per cent, so the
/// hundred-section version cost as much as five voices and this one costs two.
constexpr int kSections = 60;
constexpr double kSectionRate = 48000.0;

/// The dispersion itself, spread a little across the chain.
///
/// Every section on the same coefficient gives a chirp so pure it reads as a
/// sound effect rather than a room. Walking it across the chain smears the
/// sweep into something a spring would actually do.
constexpr float kLowestCoefficient = -0.74f;
constexpr float kHighestCoefficient = -0.82f;

/// What a transducer and a pickup will pass, whatever DAMP is set to.
///
/// Two poles at the top, so the corner is the -6 dB point rather than the -3,
/// which puts the usable top of the band under three kilohertz. That is where
/// a tank actually stops: brighter than this and it reads as a narrow plate
/// instead. Measured against a thousand cycles, twelve kilohertz comes back
/// 13.2 dB down with the corner at 4500, 14.9 at 4000 and 16.5 at 3600, while
/// a plate at the same settings is 0.3 dB up.
constexpr float kRumbleHz = 80.0f;
constexpr float kOpenHz = 3600.0f;
constexpr float kClosedHz = 1300.0f;

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

  dark = darker = rumble = tail = 0.0f;
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

  // A spring's top end is what tells you how far down the tray you are
  // listening. Closed right down it is still nowhere near dark enough to be a
  // room, because the band it works over never opened that far to begin with.
  const auto damping = std::clamp(p.damping, 0.0f, 1.0f);
  const auto darkCoef =
      onePole(kOpenHz - damping * (kOpenHz - kClosedHz), sampleRate);
  const auto rumbleCoef = onePole(kRumbleHz, sampleRate);

  // Each spring has to lose the same proportion per second rather than per
  // bounce, or the short one would run out while the long one was still going
  // and the tail would change its colour as it faded.
  //
  // Its loop is the transit plus the dispersion, and the dispersion's length
  // depends on the frequency, which looks like it makes this unanswerable. It
  // does not: the mean group delay of an allpass across the band is exactly
  // its order, whatever its coefficient, so a chain of a hundred first-order
  // sections averages a hundred samples however steep the chirp is. That mean
  // is the right figure to size a decay by, and it leaves the bottom of the
  // band ringing slightly longer than the top, which is what a tray does.
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

    // One transducer on one tray, as with the plate's single driver.
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

      // Dispersed on the way in, so what reaches the far end has already been
      // smeared once and is smeared again on every pass back.
      for (auto &section : spring.dispersion)
        v = section.process(v);

      // The line is read where it is written, before the write, which makes
      // the delay exactly its length. A spring has one transit and no taps
      // along it.
      const auto arrived = spring.line[(size_t)spring.write];

      spring.line[(size_t)spring.write] = v;

      if (++spring.write >= (int)spring.line.size())
        spring.write = 0;

      spring.dark = arrived + (spring.dark - arrived) * darkCoef;
      spring.darker = spring.dark + (spring.darker - spring.dark) * darkCoef;

      auto heard = spring.darker;

      spring.rumble = heard + (spring.rumble - heard) * rumbleCoef;
      heard -= spring.rumble;

      spring.tail = heard;

      wetL += heard * kWeightsL[i];
      wetR += heard * kWeightsR[i];
    }

    outL[n] = dryL + (wetL * kTapScale - dryL) * mix;
    outR[n] = dryR + (wetR * kTapScale - dryR) * mix;
  }
}

} // namespace ovt
