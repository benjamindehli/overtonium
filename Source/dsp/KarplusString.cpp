#include "KarplusString.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// DAMP's travel, as the lowpass coefficient. Nought is no lowpass at all, a
/// string with nothing but its decay taking the top away, and a half is the
/// classic average of two neighbours, as dark as the loop gets.
constexpr float kDampMax = 0.5f;

/// PICK's travel, as a share of the string's length. Struck near the end
/// keeps nearly every harmonic, struck in the middle loses every even one.
constexpr float kPickNearest = 0.02f;
constexpr float kPickFurthest = 0.5f;

std::size_t nextPowerOfTwo(std::size_t n) {
  std::size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}
} // namespace

void KarplusString::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  // A period at the lowest pitch, and room either side for the interpolation.
  const auto longest =
      nextPowerOfTwo((std::size_t)std::ceil(sampleRate / kLowestHz) + 8);

  line.assign(longest, 0.0f);
  struck.assign(longest, 0.0f);
  mask = longest - 1;

  touched = false;
  reset();
}

void KarplusString::setRate(double rate) noexcept {
  sampleRate = std::max(1.0, rate);
}

void KarplusString::reset() noexcept {
  if (touched) {
    std::fill(line.begin(), line.end(), 0.0f);
    std::fill(struck.begin(), struck.end(), 0.0f);
    touched = false;
  }

  write = 0;
  lowpassPrevious = 0.0f;
  primed = false;
  fade = 0.0f;
  fadeStep = 0.0f;
}

double KarplusString::lowpassDelay(double omega, double coeff) noexcept {
  // y = (1 - b) x + b x[n-1], whose phase at omega is the angle of
  // (1 - b) + b e^(-i omega).
  if (omega <= 0.0)
    return coeff;

  const auto re = (1.0 - coeff) + coeff * std::cos(omega);
  const auto im = coeff * std::sin(omega);
  return std::atan2(im, re) / omega;
}

void KarplusString::setBlock(double hz, double decaySeconds, float damp,
                             float pick, int len) noexcept {
  if (line.empty() || len <= 0)
    return;

  b = kDampMax * std::clamp(damp, 0.0f, 1.0f);

  const auto audibleLow = hz >= kLowestHz;
  const auto f = std::max(hz, kLowestHz);
  const auto period = sampleRate / f;
  const auto omega = kTwoPi * f / sampleRate;

  // The whole loop is one period, and the lowpass is part of it.
  const auto target = std::clamp(period - lowpassDelay(omega, (double)b), 2.0,
                                 (double)(mask - 4));

  // Ringing to a thousandth over the decay time, one loop pass per period.
  // The lowpass takes some of the fundamental each pass as well, so the gain
  // gives that back, which keeps DECAY the time the note itself takes however
  // dark DAMP makes the string.
  const auto decay = std::max(1.0e-3, decaySeconds);
  const auto passLoss =
      std::hypot((1.0 - (double)b) + (double)b * std::cos(omega),
                 (double)b * std::sin(omega));
  feedback = (float)std::min(0.99999, std::pow(10.0, -3.0 / (f * decay)) /
                                          std::max(1.0e-6, passLoss));

  // White noise through a loop with this gain comes out stronger by
  // 1 / sqrt(1 - g^2), whatever the loop's length. Taking that back off the
  // way in keeps a string that is breathed into as loud as the noise
  // breathing into it, however long it is set to ring.
  inputGain = std::sqrt(std::max(0.0f, 1.0f - feedback * feedback));

  pickDelay = std::max(
      1, (int)std::lround(period *
                          (kPickNearest + (kPickFurthest - kPickNearest) *
                                              std::clamp(pick, 0.0f, 1.0f))));

  // Heard while the loop is long enough to tune, faded out below that.
  const auto targetFade =
      audibleLow ? (float)std::clamp((target - kFadeShortest) /
                                         (kFadeLongest - kFadeShortest),
                                     0.0, 1.0)
                 : 0.0f;

  // The first block of a note starts where it is going rather than sliding
  // there from wherever the last note left the string.
  if (!primed) {
    delay = target;
    fade = targetFade;
    primed = true;
  }

  delayStep = (target - delay) / (double)len;
  fadeStep = (targetFade - fade) / (float)len;
}

float KarplusString::read(double samples) const noexcept {
  // Third-order Lagrange through the four samples around the point, which
  // stays in tune and smooth as the point moves, as a glide moves it.
  const auto whole = (std::size_t)samples;
  const auto d = (float)(samples - (double)whole);

  const auto at = [this, whole](std::size_t back) {
    return line[(write - whole - back + 1 + mask + 1) & mask];
  };

  const auto x0 = at(0), x1 = at(1), x2 = at(2), x3 = at(3);

  // d is how far past `whole` the point lies, between x1 and x2.
  const auto c0 = -d * (d - 1.0f) * (d - 2.0f) / 6.0f;
  const auto c1 = (d + 1.0f) * (d - 1.0f) * (d - 2.0f) / 2.0f;
  const auto c2 = -(d + 1.0f) * d * (d - 2.0f) / 2.0f;
  const auto c3 = (d + 1.0f) * d * (d - 1.0f) / 6.0f;

  return c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3;
}

float KarplusString::tick(float excitation) noexcept {
  if (line.empty())
    return 0.0f;

  touched = true;

  // Struck at a point along it, which cancels the harmonics with a node
  // there: the strike and its reflection from the near end, a pick's length
  // apart.
  struck[write] = excitation;
  const auto strike =
      excitation - struck[(write - (std::size_t)pickDelay + mask + 1) & mask];

  const auto out = read(delay);
  const auto damped = (1.0f - b) * out + b * lowpassPrevious;
  lowpassPrevious = out;

  line[write] = inputGain * strike + feedback * damped;
  write = (write + 1) & mask;

  delay += delayStep;
  fade = std::clamp(fade + fadeStep, 0.0f, 1.0f);

  return damped;
}

} // namespace ovt
