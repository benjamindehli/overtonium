#include "Cassette.h"
#include "Filters.h"

#include <algorithm>

namespace ovt {

namespace {
using filters::kPi;
using filters::warped;

/// Decibels to a gain.
double db(double x) noexcept { return std::pow(10.0, x / 20.0); }

} // namespace

double Cassette::Follower::run(double x, double attackCoef, double fastCoef,
                               double slowCoef) noexcept {
  // The average of the rectified signal, scaled by pi / 2 so a sine reads its
  // amplitude. Not a peak follower, since riding the hiss's peaks would let
  // much of the hiss back in.
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

    filters::peaking(c.bump, kBumpHz, kBumpDb, kBumpQ, sr);
    filters::lowPass(c.gap, kGapHz, kGapQ, sr);
    c.top.design(0.0, topW, 1.0, topW, k);
    c.riseBand.design(1.0, 0.0, 1.0, riseW, k);
    c.riseTop.design(0.0, topW, 1.0, topW, k);

    // One in the highs, kHissShelfHz / kHissLowHz below.
    c.hissShelf.design(1.0, hissShelfW, 1.0, hissLowW, k);

    lineLength = (int)std::ceil((kFlutterCentre + kFlutterSwing) * sr) + 4;
    c.line.assign((size_t)lineLength, 0.0f);
  }

  flutterStep = kFlutterHz / sr;
  secondStep = kFlutterSecondHz / sr;

  for (size_t b = 0; b < kFlutterBands.size(); ++b) {
    auto &f = flutterBands[b];
    const double bandwidth =
        filters::bandPass(f, kFlutterBands[b].lowHz,
                          std::min(kFlutterBands[b].highHz, 0.45 * sr), sr);

    flutterBandScale[b] =
        kFlutterBands[b].rms * std::sqrt(3.0 * (sr / 2.0) / bandwidth);
  }

  {
    const double bandwidth =
        filters::bandPass(wobble, kWobbleHz / 1.6, kWobbleHz * 1.6, sr);
    wobbleScale = kWobbleDb * std::sqrt(3.0 * (sr / 2.0) / bandwidth);
  }

  centreSamples = kFlutterCentre * sr;
  swingSamples = kFlutterSwing * sr;

  // Pulls the delay back to its centre, slower than any band of the speed.
  leak = 1.0 - std::exp(-2.0 * kPi * 0.05 / sr);

  // The antiderivative of what the limit takes away, by the trapezium rule
  // from zero. Tabulating only what is taken away, rather than the whole
  // curve, keeps the table's error near nothing for a quiet signal. Even, so
  // only the positive half is kept.
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

  trim = 1.0 / (filters::magnitudeAt(c.bump, 1000.0, sr) *
                filters::magnitudeAt(c.gap, 1000.0, sr) *
                filters::magnitudeAt(c.top, 1000.0, sr));

  reset();
}

void Cassette::reset() noexcept {
  // Seeded here rather than in prepare, so the hiss and the flutter start the
  // same way after every reset and two renders of the same thing match.
  std::uint32_t seed = 0x9e3779b9u;

  for (auto &c : channels) {
    // The two tracks hiss independently, so each has its own stream.
    c.rng = seed;
    seed = seed * 1664525u + 1013904223u;

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
  transportRng = seed;
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
  // away is the rest of the straight line above it. Written as "not inside"
  // so a NaN takes this branch instead of becoming an index.
  if (!(at < kClipTableSize)) {
    const double end = kClipTableSpan * cassette::kClipLimit;
    return clipTable.back() + cassette::kClipLimit * (a - end) -
           0.5 * (a * a - end * end);
  }

  const auto i = (size_t)at;
  const double frac = at - (double)i;

  return clipTable[i] + frac * (clipTable[i + 1] - clipTable[i]);
}

double Cassette::clip(Channel &c, double u) const noexcept {
  // Only what the limit takes away is averaged, since averaging the whole
  // signal would act as a low pass on it.
  const auto lost = [this](double v) { return clipCurve(v) - v; };

  const double taken =
      antialiased(u, c.lastClip, 1.0e-7, lost,
                  [this](double v) { return lostIntegral(v); });

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
  // A feedback decoder: out + depth * band(out) = in, solved for this sample
  // directly since the band is one first order section. The depth comes from
  // the level a step ago.
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

  // The depths and the wobble are each a power and move slowly, so they are
  // worked out every kControlStep samples to save CPU.
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
                            hissScale * hissTrim * std::sqrt(1.5) * open +
                        rise(c, (double)in);

    return (float)(trim * decode(c, read * level + hiss));
  };

  left = one(channels[0], left);
  right = one(channels[1], right);

  writeIndex = (writeIndex + 1) % lineLength;
}

} // namespace ovt
