#include "Cassette.h"

#include <algorithm>
#include <complex>

namespace ovt {

namespace {
constexpr double kPi = 3.14159265358979323846;

/// A corner frequency in radians per second, prewarped so the digital
/// filter's corner lands where the analogue one's was.
double warped(double hz, double sampleRate) noexcept {
  const double nyquistSafe = std::min(hz, 0.45 * sampleRate);
  return 2.0 * sampleRate * std::tan(kPi * nyquistSafe / sampleRate);
}

double db(double x) noexcept { return std::pow(10.0, x / 20.0); }

/// A peaking section, the analogue prototype's response at w0 by the bilinear
/// transform, as the cookbook gives it.
void peaking(double hz, double gainDb, double q, double sampleRate, double &b0,
             double &b1, double &b2, double &a1, double &a2) noexcept {
  const double w = 2.0 * kPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
  const double a = std::pow(10.0, gainDb / 40.0);
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha / a;

  b0 = (1.0 + alpha * a) / a0;
  b1 = -2.0 * cosw / a0;
  b2 = (1.0 - alpha * a) / a0;
  a1 = -2.0 * cosw / a0;
  a2 = (1.0 - alpha / a) / a0;
}

void lowPass(double hz, double q, double sampleRate, double &b0, double &b1,
             double &b2, double &a1, double &a2) noexcept {
  const double w = 2.0 * kPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;

  b0 = (1.0 - cosw) / 2.0 / a0;
  b1 = (1.0 - cosw) / a0;
  b2 = (1.0 - cosw) / 2.0 / a0;
  a1 = -2.0 * cosw / a0;
  a2 = (1.0 - alpha) / a0;
}

/// A band pass whose peak is one, and its noise bandwidth in hertz, which is
/// what sizes the white noise through it to a wanted rms.
double bandPass(double lowHz, double highHz, double sampleRate, double &b0,
                double &b1, double &b2, double &a1, double &a2) noexcept {
  const double centre = std::sqrt(lowHz * highHz);
  const double q = centre / (highHz - lowHz);
  const double w = 2.0 * kPi * centre / sampleRate;
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;

  b0 = alpha / a0;
  b1 = 0.0;
  b2 = -alpha / a0;
  a1 = -2.0 * cosw / a0;
  a2 = (1.0 - alpha) / a0;

  return 0.5 * kPi * centre / q;
}

template <typename Section>
double magnitudeAt(const Section &s, double hz, double sampleRate) noexcept {
  const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / sampleRate);

  return std::abs((s.b0 + s.b1 * z1 + s.b2 * z1 * z1) /
                  (1.0 + s.a1 * z1 + s.a2 * z1 * z1));
}
} // namespace

void Cassette::FirstOrder::design(double c1, double c0, double d1, double d0,
                                  double k) noexcept {
  const double norm = d1 * k + d0;

  b0 = (c1 * k + c0) / norm;
  b1 = (c0 - c1 * k) / norm;
  a1 = (d0 - d1 * k) / norm;
}

double Cassette::FirstOrder::run(double x) noexcept {
  const double y = b0 * x + b1 * x1 - a1 * y1;

  x1 = x;
  y1 = y;

  return y;
}

double Cassette::Biquad::run(double x) noexcept {
  const double y = b0 * x + z1;

  z1 = b1 * x - a1 * y + z2;
  z2 = b2 * x - a2 * y;

  return y;
}

double Cassette::Follower::run(double x, double attackCoef, double fastCoef,
                               double slowCoef) noexcept {
  // The average of the rectified signal, scaled by pi / 2 so a sine reads its
  // amplitude, which is the level the shelf was fitted against. Not a peak
  // follower: the tape's own hiss has peaks four times its average, and a
  // side chain riding them let a third of the hiss Dolby removes back in.
  const double level = 1.5707963267948966 * std::abs(x);

  fast = level + (level > fast ? attackCoef : fastCoef) * (fast - level);
  slow = level + (level > slow ? attackCoef : slowCoef) * (slow - level);

  return value();
}

double Cassette::Follower::value() const noexcept {
  return std::max(fast, cassette::kDolbySlowShare * slow);
}

void Cassette::prepare(double rate) {
  using namespace cassette;

  sampleRate = std::max(1.0, rate);
  const double sr = sampleRate;
  const double k = 2.0 * sr;

  attack = std::exp(-1.0 / (kDolbyAttack * sr));
  fastRelease = std::exp(-1.0 / (kDolbyFastRelease * sr));
  slowRelease = std::exp(-1.0 / (kDolbySlowRelease * sr));
  decodeHears = db(kDolbyDecodeHearsDb);
  maxDepth = db(kDolbyMaxDb) - 1.0;

  // The depth falls as maxDepth / (1 + (level / threshold)^(20 / spread)),
  // so the level is scaled once and raised to a power, rather than taken to
  // decibels and back on every sample.
  thresholdScale = 1.0 / db(kDolbyThresholdDb);
  spreadPower = 20.0 / kDolbySpreadDb;

  gateAttack = std::exp(-1.0 / (kHissAttack * sr));
  gateRelease = std::exp(-1.0 / (kHissRelease * sr));
  gateOpen = 1.0 / db(kHissOpenDb);

  // White noise of unit variance carries half the rate's worth of hertz, so
  // a density in dB per hertz against a full-scale sine, whose power is a
  // half, is this much rms.
  hissScale = std::sqrt(0.5 * std::pow(10.0, kHissDbPerHz / 10.0) * sr / 2.0);

  // The same against a tone of amplitude one in the band, whose power is a
  // half, for the noise the tape adds under a signal.
  riseScale = std::sqrt(0.5 * std::pow(10.0, kRiseDbPerHz / 10.0) * sr / 2.0);
  riseRelease = std::exp(-1.0 / (kRiseRelease * sr));

  const double encodeW = warped(kDolbyEncodeHz, sr);
  const double decodeW = warped(kDolbyDecodeHz, sr);
  const double senseW = warped(kDolbySenseHz, sr);
  const double lowW = warped(kPreLowHz, sr);
  const double highW = warped(kPreHighHz, sr);
  const double lowG = db(kPreLowDb);
  const double highG = db(kPreHighDb);
  const double topW = warped(kTopHz, sr);
  const double riseW = warped(kRiseHz, sr);
  const double hissLowW = warped(kHissLowHz, sr);
  const double hissShelfW = warped(kHissShelfHz, sr);

  std::uint32_t seed = 0x9e3779b9u;

  for (auto &c : channels) {
    // Each a high pass, the band the side chain reads and the shelf lifts.
    c.encodeBand.design(1.0, 0.0, 1.0, encodeW, k);
    c.decodeBand.design(1.0, 0.0, 1.0, decodeW, k);
    c.encodeSense.design(1.0, 0.0, 1.0, senseW, k);
    c.decodeSense.design(1.0, 0.0, 1.0, senseW, k);

    // The record equaliser: lowG in the lows, highG in the highs, and the
    // exact inverse of both after the limit.
    c.preLow.design(1.0, lowW * lowG, 1.0, lowW, k);
    c.postLow.design(1.0, lowW, 1.0, lowW * lowG, k);
    c.preHigh.design(highG / highW, 1.0, 1.0 / highW, 1.0, k);
    c.postHigh.design(1.0 / highW, 1.0, highG / highW, 1.0, k);

    peaking(kBumpHz, kBumpDb, kBumpQ, sr, c.bump.b0, c.bump.b1, c.bump.b2,
            c.bump.a1, c.bump.a2);
    lowPass(kGapHz, kGapQ, sr, c.gap.b0, c.gap.b1, c.gap.b2, c.gap.a1,
            c.gap.a2);
    c.top.design(0.0, topW, 1.0, topW, k);
    c.riseBand.design(1.0, 0.0, 1.0, riseW, k);
    c.riseTop.design(0.0, topW, 1.0, topW, k);

    // One in the highs, kHissShelfHz / kHissLowHz below.
    c.hissShelf.design(1.0, hissShelfW, 1.0, hissLowW, k);

    // The two tracks hiss independently, so each has its own stream.
    c.rng = seed;
    seed = seed * 1664525u + 1013904223u;

    lineLength = (int)std::ceil((kFlutterCentre + kFlutterSwing) * sr) + 4;
    c.line.assign((size_t)lineLength, 0.0f);
  }

  transportRng = seed;

  flutterStep = kFlutterHz / sr;
  secondStep = kFlutterSecondHz / sr;

  for (size_t b = 0; b < kFlutterBands.size(); ++b) {
    auto &f = flutterBands[b];
    const double bandwidth = bandPass(
        kFlutterBands[b].lowHz, std::min(kFlutterBands[b].highHz, 0.45 * sr),
        sr, f.b0, f.b1, f.b2, f.a1, f.a2);

    flutterBandScale[b] =
        kFlutterBands[b].rms * std::sqrt(3.0 * (sr / 2.0) / bandwidth);
  }

  {
    const double bandwidth =
        bandPass(kWobbleHz / 1.6, kWobbleHz * 1.6, sr, wobble.b0, wobble.b1,
                 wobble.b2, wobble.a1, wobble.a2);
    wobbleScale = kWobbleDb * std::sqrt(3.0 * (sr / 2.0) / bandwidth);
  }

  centreSamples = kFlutterCentre * sr;
  swingSamples = kFlutterSwing * sr;

  // Slow enough to leave every band of the speed alone, which is what keeps
  // the delay centred rather than walking off with the lowest of them.
  leak = 1.0 - std::exp(-2.0 * kPi * 0.05 / sr);

  // The antiderivative of what the limit takes away, by the trapezium rule
  // from zero. Of what it takes away rather than of the curve, because that is
  // near nothing for a quiet signal, and so is the error between two entries:
  // tabulating the whole curve and subtracting the straight line afterwards
  // left an error the size of a -40 dBFS signal. Even, so only the positive
  // half is kept.
  clipTable.assign((size_t)kClipTableSize + 1, 0.0);
  clipStep = kClipTableSpan * kClipLimit / kClipTableSize;

  for (int i = 1; i <= kClipTableSize; ++i) {
    const double u0 = (i - 1) * clipStep, u1 = i * clipStep;
    const double a = clipCurve(u0) - u0;
    const double b = clipCurve(u1) - u1;
    clipTable[(size_t)i] = clipTable[(size_t)i - 1] + 0.5 * (a + b) * clipStep;
  }

  // Unity at a quiet 1 kHz tone: the tape's linear response there, divided
  // back out. The record equaliser and its inverse cancel, and Dolby at a
  // quiet level is left as it is, since the way it mistracks is the sound.
  const auto &c = channels[0];
  const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * 1000.0 / sr);
  const double top =
      std::abs((c.top.b0 + c.top.b1 * z1) / (1.0 + c.top.a1 * z1));

  trim = 1.0 / (magnitudeAt(c.bump, 1000.0, sr) *
                magnitudeAt(c.gap, 1000.0, sr) * top);

  reset();
}

void Cassette::reset() noexcept {
  for (auto &c : channels) {
    c.encodeBand.clear();
    c.decodeBand.clear();
    c.encodeSense.clear();
    c.decodeSense.clear();
    c.encodeLevel.clear();
    c.decodeLevel.clear();
    c.decodeIn = c.decodeBandOut = 0.0;
    c.encodeDepth = c.decodeDepth = maxDepth;
    c.preLow.clear();
    c.preHigh.clear();
    c.postLow.clear();
    c.postHigh.clear();
    c.lastClip = 0.0;
    c.bump.clear();
    c.gap.clear();
    c.top.clear();
    c.riseBand.clear();
    c.riseTop.clear();
    c.riseLevel.clear();
    c.hissShelf.clear();
    std::fill(c.line.begin(), c.line.end(), 0.0f);
  }

  for (auto &f : flutterBands)
    f.clear();

  wobble.clear();
  gate.clear();
  flutterPhase = secondPhase = 0.0;
  wobbleGain = 1.0;
  controlCount = 0;
  refresh = true;
  delaySamples = centreSamples;
  writeIndex = 0;
}

double Cassette::uniform(std::uint32_t &state) noexcept {
  // xorshift32, between -1 and 1.
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;

  return (double)state * (2.0 / 4294967295.0) - 1.0;
}

double Cassette::shelfDepth(double level) const noexcept {
  return maxDepth / (1.0 + std::pow(level * thresholdScale, spreadPower));
}

double Cassette::clipCurve(double u) const noexcept {
  using namespace cassette;

  return u / std::pow(1.0 + std::pow(std::abs(u / kClipLimit), kClipKnee),
                      1.0 / kClipKnee);
}

double Cassette::lostIntegral(double u) const noexcept {
  const double a = std::abs(u);
  const double at = a / clipStep;

  // Past the table the curve has all but reached its limit, so what it takes
  // away is the rest of the straight line above it.
  if (at >= kClipTableSize) {
    const double end = kClipTableSpan * cassette::kClipLimit;
    return clipTable.back() + cassette::kClipLimit * (a - end) -
           0.5 * (a * a - end * end);
  }

  const auto i = (size_t)at;
  const double frac = at - (double)i;

  return clipTable[i] + frac * (clipTable[i + 1] - clipTable[i]);
}

double Cassette::clip(Channel &c, double u) const noexcept {
  // Only what the limit takes away is averaged. Averaging the whole would be
  // the mean of two neighbouring samples, which is a low pass of its own, 2 dB
  // down at 10 kHz at 48 kHz, on a signal that is mostly straight through.
  const auto lost = [this](double v) { return clipCurve(v) - v; };

  const double step = u - c.lastClip;
  const double taken =
      std::abs(step) < 1.0e-7
          ? lost(0.5 * (u + c.lastClip))
          : (lostIntegral(u) - lostIntegral(c.lastClip)) / step;

  c.lastClip = u;

  return u + taken;
}

double Cassette::tape(Channel &c, double x) noexcept {
  // Onto the tape through Dolby's encoder, its depth from the input's own
  // high band.
  const double band = c.encodeBand.run(x);
  c.encodeLevel.run(c.encodeSense.run(band), attack, fastRelease, slowRelease);

  if (refresh)
    c.encodeDepth = shelfDepth(c.encodeLevel.value());

  const double encoded = x + c.encodeDepth * band;

  // The record side, lows first.
  const double recorded = c.postHigh.run(
      c.postLow.run(clip(c, c.preHigh.run(c.preLow.run(encoded)))));

  // The tape's response.
  return c.top.run(c.gap.run(c.bump.run(recorded)));
}

double Cassette::rise(Channel &c, double x) noexcept {
  const double level =
      c.riseLevel.run(c.riseBand.run(x), gateAttack, riseRelease, riseRelease);

  return c.riseTop.run((uniform(c.rng) + uniform(c.rng)) * std::sqrt(1.5)) *
         riseScale * level;
}

double Cassette::decode(Channel &c, double x) noexcept {
  // A feedback decoder: the shelf lowers what it hears by a depth read from
  // its own output, so out + depth * band(out) = in. The band is one first
  // order section, so that is solved for this sample directly.
  //
  // The depth is read from the level a step ago, which a side chain that
  // takes a millisecond to rise cannot tell from now.
  const auto &f = c.decodeBand;

  if (refresh)
    c.decodeDepth = shelfDepth(c.decodeLevel.value());

  const double depth = c.decodeDepth;

  const double rest = f.b1 * c.decodeIn - f.a1 * c.decodeBandOut;
  const double out = (x - depth * rest) / (1.0 + depth * f.b0);
  const double band = f.b0 * out + rest;

  c.decodeIn = out;
  c.decodeBandOut = band;
  c.decodeLevel.run(c.decodeSense.run(band) * decodeHears, attack, fastRelease,
                    slowRelease);

  return out;
}

void Cassette::process(float &left, float &right) noexcept {
  using namespace cassette;

  // ---- the transport, shared by both tracks -------------------------------
  double speed =
      kFlutterRms * std::sqrt(2.0) * std::sin(2.0 * kPi * flutterPhase) +
      kFlutterSecondRms * std::sqrt(2.0) * std::sin(2.0 * kPi * secondPhase);

  flutterPhase += flutterStep;
  flutterPhase -= std::floor(flutterPhase);
  secondPhase += secondStep;
  secondPhase -= std::floor(secondPhase);

  for (size_t b = 0; b < flutterBands.size(); ++b)
    speed += flutterBands[b].run(uniform(transportRng)) * flutterBandScale[b];

  // Faster tape reaches the head sooner, so the delay shortens.
  delaySamples += -speed + leak * (centreSamples - delaySamples);
  delaySamples = std::clamp(delaySamples, centreSamples - swingSamples,
                            centreSamples + swingSamples);

  // The depths and the wobble are worked out every kControlStep samples. Each
  // is a power, five of them a sample were most of what this cost, and none
  // of them moves in less than a millisecond.
  refresh = controlCount == 0;
  controlCount = (controlCount + 1) % kControlStep;

  const double wobbled = wobble.run(uniform(transportRng)) * wobbleScale;

  if (refresh)
    wobbleGain = db(wobbled);

  const double level = wobbleGain;

  // ---- whether there is anything for the hiss to follow -------------------
  const double heard =
      gate.run(std::max(std::abs((double)left), std::abs((double)right)),
               gateAttack, gateRelease, gateRelease);
  const double open = std::min(1.0, heard * gateOpen);

  const auto one = [&](Channel &c, float in) {
    c.line[(size_t)writeIndex] = (float)tape(c, (double)in);

    // Read back through a four-point cubic, which keeps the top far better
    // than a straight line would as the delay moves.
    const double at = (double)writeIndex - delaySamples;
    const double wrapped = at < 0.0 ? at + lineLength : at;
    const int i = (int)wrapped;
    const double t = wrapped - i;

    const auto tap = [&](int offset) {
      int j = i + offset;
      j = j < 0 ? j + lineLength : (j >= lineLength ? j - lineLength : j);
      return (double)c.line[(size_t)j];
    };

    const double xm1 = tap(-1), x0 = tap(0), x1 = tap(1), x2 = tap(2);
    const double read = x0 + 0.5 * t *
                                 (x1 - xm1 +
                                  t * (2.0 * xm1 - 5.0 * x0 + 4.0 * x1 - x2 +
                                       t * (3.0 * (x0 - x1) + x2 - xm1)));

    const double hiss = c.hissShelf.run(uniform(c.rng) + uniform(c.rng)) *
                            hissScale * std::sqrt(1.5) * open +
                        rise(c, (double)in);

    return (float)(trim * decode(c, read * level + hiss));
  };

  left = one(channels[0], left);
  right = one(channels[1], right);

  writeIndex = (writeIndex + 1) % lineLength;
}

} // namespace ovt
