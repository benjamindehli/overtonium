#include "Console.h"
#include "Filters.h"

#include <algorithm>

namespace ovt {

namespace {
using filters::kPi;
using filters::warped;

/// The gain the compressor takes off, in dB, for a level in dB: nothing below
/// the knee, the ratio above it, and a quadratic across it.
double staticReduction(double levelDb) noexcept {
  using namespace console;

  const double over = levelDb - kCompThresholdDb;
  const double slope = 1.0 - 1.0 / kCompRatio;

  if (over <= -kCompKneeDb / 2.0)
    return 0.0;

  if (over >= kCompKneeDb / 2.0)
    return over * slope;

  const double into = over + kCompKneeDb / 2.0;
  return slope * into * into / (2.0 * kCompKneeDb);
}
} // namespace

void Console::prepare(double sampleRate) noexcept {
  using namespace console;

  const double sr = std::max(1.0, sampleRate);
  const double k = 2.0 * sr;

  fluxCoef = 1.0 - std::exp(-2.0 * kPi * kFluxHz / sr);

  attack = std::exp(-1.0 / (kCompAttack * sr));
  fastRelease = std::exp(-1.0 / (kCompFastRelease * sr));
  slowRelease = std::exp(-1.0 / (kCompSlowRelease * sr));
  slowCharge = std::exp(-1.0 / (kCompSlowCharge * sr));

  tapeLimit = std::pow(10.0, kTapeLimitDb / 20.0);

  const double shelfW = warped(kShelfHz, sr);
  const double shelfG = std::pow(10.0, kShelfDb / 20.0);
  const double lowW = warped(kTapeLowHz, sr);
  const double lowG = std::pow(10.0, kTapeLowDb / 20.0);
  const double bumpW = warped(kBumpHz, sr);
  const double bumpG = std::pow(10.0, kBumpDb / 20.0);
  const double couplingW = warped(kCouplingHz, sr);

  // The sidechain's high-pass, a Butterworth pair.
  Biquad hp;
  filters::highPass(hp, kCompSidechainHz, std::sqrt(0.5), sr);

  for (auto &c : channels) {
    // A high shelf: one in the lows, shelfG in the highs.
    c.shelf.design(shelfG / shelfW, 1.0, 1.0 / shelfW, 1.0, k);
    // Low shelves: lowG and bumpG in the lows, one in the highs.
    c.tapeLow.design(1.0, lowW * lowG, 1.0, lowW, k);
    c.bump.design(1.0, bumpW * bumpG, 1.0, bumpW, k);
    // The coupling capacitors' high-pass.
    c.coupling.design(1.0, 0.0, 1.0, couplingW, k);
    c.sidechain = hp;
  }

  // Unity at a quiet 1 kHz tone: the linear filters on the signal's own path,
  // read at 1 kHz and divided back out.
  const auto at = [sr](const FirstOrder &f) {
    return filters::magnitudeAt(f, 1000.0, sr);
  };

  const auto &c = channels[0];
  trim = 1.0 / (at(c.shelf) * at(c.bump) * at(c.coupling));

  reset();
}

void Console::reset() noexcept {
  for (auto &c : channels) {
    c.flux = 0.0;
    c.shelf.clear();
    c.tapeLow.clear();
    c.bump.clear();
    c.coupling.clear();
    c.sidechain.clear();
    c.lastTape = 0.0;
  }

  fast = slow = 0.0;
}

double Console::transformer(Channel &c, double x) const noexcept {
  using namespace console;

  c.flux += fluxCoef * (x - c.flux);

  // What the core can carry of the lows, and the rest lost. The flux is slow
  // enough that the knee's harmonics stay low, so it needs no averaging.
  const double p = c.flux;
  const double carried =
      p / std::pow(1.0 + std::pow(std::abs(p / kFluxLimit), kFluxKnee),
                   1.0 / kFluxKnee);

  return c.shelf.run(x - (p - carried));
}

double Console::tape(Channel &c, double x) const noexcept {
  using namespace console;

  const double q = c.tapeLow.run(x);

  // The part past the limit, averaged over the step from the last sample by
  // its antiderivative, so the hard corner does not alias.
  const auto past = [this](double v) {
    const double a = std::abs(v) - tapeLimit;
    return a > 0.0 ? std::copysign(a, v) : 0.0;
  };

  const auto pastIntegral = [this](double v) {
    const double a = std::abs(v) - tapeLimit;
    return a > 0.0 ? 0.5 * a * a : 0.0;
  };

  const double step = q - c.lastTape;
  const double clipped =
      std::abs(step) < 1.0e-9
          ? past(0.5 * (q + c.lastTape))
          : (pastIntegral(q) - pastIntegral(c.lastTape)) / step;

  c.lastTape = q;

  return c.coupling.run(c.bump.run(x - kTapeShare * clipped));
}

void Console::process(float &left, float &right) noexcept {
  const double l = transformer(channels[0], (double)left);
  const double r = transformer(channels[1], (double)right);

  // Linked: one reduction from whichever side is louder in the sidechain.
  const double sideL = channels[0].sidechain.run(l);
  const double sideR = channels[1].sidechain.run(r);
  const double peak = std::max(std::abs(sideL), std::abs(sideR));
  const double target = staticReduction(20.0 * std::log10(peak + 1.0e-9));

  fast = target > fast ? target + attack * (fast - target)
                       : target + fastRelease * (fast - target);
  slow = fast > slow ? fast + slowCharge * (slow - fast)
                     : fast + slowRelease * (slow - fast);

  const double gain = std::pow(10.0, -std::max(fast, slow) / 20.0);

  left = (float)(trim * tape(channels[0], l * gain));
  right = (float)(trim * tape(channels[1], r * gain));
}

} // namespace ovt
