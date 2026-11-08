#include "KarplusString.h"

#include <algorithm>
#include <cmath>

#include "Exact.h"

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// DAMP's travel, as the lowpass coefficient. Nought is no lowpass at all, a
/// string with nothing but its decay taking the top away, and a half is the
/// classic average of two neighbours, as dark as the loop gets.
constexpr float kDampMax = 0.5f;

/// POINT's travel, as a share of the string's length. Struck near the end
/// keeps nearly every harmonic, struck in the middle loses every even one.
constexpr float kPointNearest = 0.02f;
constexpr float kPointFurthest = 0.5f;

std::size_t nextPowerOfTwo(std::size_t n) {
  std::size_t p = 1;
  while (p < n)
    p <<= 1;
  return p;
}
} // namespace

void KarplusString::prepare(double newSampleRate, double lowestHz) {
  sampleRate = std::max(1.0, newSampleRate);
  lowest = std::max(1.0, lowestHz);

  // A period at the lowest pitch, and room either side for the interpolation.
  const auto longest =
      nextPowerOfTwo((std::size_t)std::ceil(sampleRate / lowest) + 8);

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
  for (int i = 0; i < kStiffSections; ++i)
    apIn[i] = apOut[i] = 0.0f;
  stiffOn = false;
  allpass = 0.0;
  apCoeff = 0.0f;
  apStep = 0.0f;
  primed = false;
  fade = 0.0f;
  fadeStep = 0.0f;
  feedbackStep = 0.0f;
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

double KarplusString::allpassDelay(double omega, double a) noexcept {
  // (a + z^-1) / (1 + a z^-1), whose phase is -omega + 2 atan2(a sin, 1 + a
  // cos). One sample at a = 0.
  if (omega <= 0.0)
    return (1.0 - a) / (1.0 + a);

  return 1.0 - 2.0 *
                   std::atan2(a * std::sin(omega), 1.0 + a * std::cos(omega)) /
                   omega;
}

double KarplusString::fitStiffness(double hz, double stiffness,
                                   double longest) noexcept {
  if (exactly(stiffness, 0.0))
    return 0.0;

  if (exactly(hz, fittedHz) && exactly(stiffness, fittedStiffness))
    return allpass;

  fittedHz = hz;
  fittedStiffness = stiffness;

  // Mode k of the law, and the last one under the top of the band, which is
  // the one matched along with the first.
  const auto modeHz = [hz, stiffness](int k) {
    return (double)k * hz *
           std::sqrt(std::max(1.0e-6, 1.0 + stiffness * (double)(k * k)));
  };

  int top = 1;
  for (int k = 2; k <= kStiffFitMode && modeHz(k) < 0.45 * sampleRate; ++k)
    top = k;

  if (top < 2)
    return 0.0;

  // How much more delay the loop needs at the first mode than at the matched
  // one, which is all the allpasses' to give, since a plain loop has the same
  // delay at both. The lowpass is left out, so the string keeps whatever it
  // does to the series without stiffness and gains only what the law adds.
  const auto w1 = kTwoPi * modeHz(1) / sampleRate;
  const auto wk = kTwoPi * modeHz(top) / sampleRate;
  const auto want =
      sampleRate / modeHz(1) - (double)top * sampleRate / modeHz(top);

  const auto gap = [w1, wk](double a) {
    return (double)kStiffSections * (allpassDelay(w1, a) - allpassDelay(wk, a));
  };

  // Negative coefficients delay the bottom more and stretch the series,
  // positive ones the top and squeeze it. Bounded so the line is never asked
  // to be shorter than the interpolation needs.
  double lo = -0.95, hi = 0.95;
  while (lo < 0.0 &&
         (double)kStiffSections * allpassDelay(w1, lo) > longest - 4.0)
    lo *= 0.5;

  if (gap(lo) <= want)
    return lo;
  if (gap(hi) >= want)
    return hi;

  for (int i = 0; i < 40; ++i) {
    const auto mid = 0.5 * (lo + hi);
    (gap(mid) > want ? lo : hi) = mid;
  }

  return 0.5 * (lo + hi);
}

void KarplusString::engageStiffness() noexcept {
  // At nought each allpass passes on its last input, so the pair holds the
  // last two values the line was given. Moving the line back by two puts
  // what is already in it where a loop that had always had them would have,
  // and the next two writes are those same values again.
  const auto last = [this](std::size_t back) {
    return line[(write - back + mask + 1) & mask];
  };

  apIn[0] = last(1);
  apOut[0] = last(2);
  apIn[1] = last(2);
  apOut[1] = last(3);

  write = (write - (std::size_t)kStiffSections + mask + 1) & mask;
  delay -= (double)kStiffSections;
  apCoeff = 0.0f;
  stiffOn = true;
}

void KarplusString::disengageStiffness() noexcept {
  // The two values the pair still holds go into the line, where they would
  // have gone a sample each from now, and the line goes on from there.
  line[write] = apIn[1];
  line[(write + 1) & mask] = apIn[0];
  write = (write + (std::size_t)kStiffSections) & mask;
  delay += (double)kStiffSections;
  stiffOn = false;
}

void KarplusString::setBlock(double hz, double decaySeconds, float damp,
                             float point, int len, double stiffness) noexcept {
  if (line.empty() || len <= 0)
    return;

  b = kDampMax * std::clamp(damp, 0.0f, 1.0f);

  const auto audibleLow = hz >= lowest;
  const auto f = std::max(hz, lowest);
  const auto period = sampleRate / f;
  const auto omega = kTwoPi * f / sampleRate;

  // The whole loop is one period, and the lowpass is part of it.
  const auto plainTarget = std::clamp(period - lowpassDelay(omega, (double)b),
                                      2.0, (double)(mask - 4));

  // A stiff string's first mode moves up with the law, and the allpasses take
  // their own delay at it out of the line's. They are only in the loop while
  // there is stiffness, or a ramp back to none to finish.
  double target = plainTarget;

  if (stiffable) {
    // Where the last block's ramp was headed, exactly, so a coefficient on
    // its way back to nought gets there.
    apCoeff = (float)allpass;

    allpass = audibleLow && plainTarget >= 2.0 + 2.0 * kStiffSections
                  ? fitStiffness(f, stiffness, plainTarget)
                  : 0.0;

    if (!primed)
      stiffOn = !exactly(allpass, 0.0);
    else if (!exactly(allpass, 0.0) && !stiffOn)
      engageStiffness();
    else if (exactly(allpass, 0.0) && stiffOn && exactly(apCoeff, 0.0f))
      disengageStiffness();

    if (stiffOn) {
      const auto first =
          f * std::sqrt(std::max(
                  1.0e-6, 1.0 + (exactly(allpass, 0.0) ? 0.0 : stiffness)));
      const auto w1 = kTwoPi * first / sampleRate;
      target =
          std::clamp(sampleRate / first - lowpassDelay(w1, (double)b) -
                         (double)kStiffSections * allpassDelay(w1, allpass),
                     2.0, (double)(mask - 4));
    }
  }

  // Ringing to a thousandth over the decay time, one loop pass per period.
  // The lowpass takes some of the fundamental each pass as well, so the gain
  // gives that back, which keeps DECAY the time the note itself takes however
  // dark DAMP makes the string.
  const auto decay = std::max(1.0e-3, decaySeconds);
  const auto passLoss =
      std::hypot((1.0 - (double)b) + (double)b * std::cos(omega),
                 (double)b * std::sin(omega));
  const auto targetFeedback = (float)std::min(
      0.99999, std::pow(10.0, -3.0 / (f * decay)) / std::max(1.0e-6, passLoss));

  // A fixed input gain, so a pluck rings at about the level of the noise that
  // struck it. The 1/sqrt(2) takes back the power POINT's comb adds.
  inputGain = 0.70710678f;

  pointDelay =
      point < 0.0f
          ? 0
          : std::max(1, (int)std::lround(period *
                                         (kPointNearest +
                                          (kPointFurthest - kPointNearest) *
                                              std::clamp(point, 0.0f, 1.0f))));

  // Heard while the loop is long enough to tune, faded out below that.
  const auto targetFade =
      audibleLow ? (float)std::clamp((plainTarget - kFadeShortest) /
                                         (kFadeLongest - kFadeShortest),
                                     0.0, 1.0)
                 : 0.0f;

  // The first block of a note starts where it is going rather than sliding
  // there from wherever the last note left the string.
  if (!primed) {
    delay = target;
    fade = targetFade;
    feedback = targetFeedback;
    apCoeff = (float)allpass;
    primed = true;
  }

  apStep = ((float)allpass - apCoeff) / (float)len;

  delayStep = (target - delay) / (double)len;
  fadeStep = (targetFade - fade) / (float)len;

  // Moved across the block too, since a loop gain changed in one step leaves
  // a step in the line, which is a click.
  feedbackStep = (targetFeedback - feedback) / (float)len;
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
  // there: the strike and its reflection from the near end, POINT's
  // length apart.
  struck[write] = excitation;
  const auto strike =
      pointDelay > 0
          ? excitation -
                struck[(write - (std::size_t)pointDelay + mask + 1) & mask]
          : excitation;

  const auto out = read(delay);
  const auto damped = (1.0f - b) * out + b * lowpassPrevious;
  lowpassPrevious = out;

  auto in = inputGain * strike + feedback * damped;

  // The stiffness, on everything that goes into the line, the strike as well
  // as the loop, so a coefficient of nought is a delay of exactly what the
  // line is read short by.
  if (stiffOn) {
    for (int i = 0; i < kStiffSections; ++i) {
      const auto y = apCoeff * (in - apOut[i]) + apIn[i];
      apIn[i] = in;
      apOut[i] = y;
      in = y;
    }

    apCoeff += apStep;
  }

  line[write] = in;
  write = (write + 1) & mask;

  delay += delayStep;
  fade = std::clamp(fade + fadeStep, 0.0f, 1.0f);
  feedback += feedbackStep;

  return damped;
}

} // namespace ovt
