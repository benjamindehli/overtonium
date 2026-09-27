#include "BucketEcho.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
constexpr double kTwoPi = 6.283185307179586;

/// The two clocks.
///
/// Slow enough to be a drift rather than a vibrato, close enough to each other
/// that neither sounds like the odd one out, and sharing no common factor, so
/// the pair never falls into step and never repeats a relationship.
constexpr double kSweepL = 0.31, kSweepR = 0.43;

/// And the one they share, slower than either, which is the clock itself
/// drifting rather than the two sides disagreeing about where it is.
constexpr double kDriftRate = 0.19;

/// The movement, split in two because the two halves of it cost different
/// things.
///
/// What the sides do differently is what gives the repeats width, and it is
/// also the only part that cancels when somebody sums to mono: two delays a
/// little apart comb when they are added. Measured on a 400 Hz burst at a
/// quarter of a second, as the correlation between the sides and what summing
/// them costs against one side alone:
///
///   differential       correlation   mono sum
///   0.0016 / 0.0021        -0.60      -6.97 dB
///   0.0008 / 0.0011        -0.23      -4.15 dB
///   0.0005 / 0.0007        +0.35      -1.72 dB
///
/// So that half stays small. What both sides do together costs nothing in
/// mono, because a delay that moves the same way on both is a vibrato rather
/// than a comb, and a bucket brigade's clock really does drift as a whole.
/// That is where the depth goes: four times the differential, slower than
/// either side's own, and audible as the pitch of the repeats breathing.
constexpr float kSweepDepthL = 0.0005f, kSweepDepthR = 0.0007f;
constexpr float kDriftDepth = 0.0020f;

/// Where the reconstruction filter sits at a short setting, and how far the
/// clock drags it down at a long one.
///
/// A line of buckets holds what it holds, so the only way to a longer delay is
/// a slower clock, and the filter has to follow it down. Fifty milliseconds is
/// about where these pedals are clean and two seconds is where they are mud,
/// which is the whole range of the part.
constexpr float kClockToneHz = 9000.0f;
constexpr float kClockRefSeconds = 0.05f;

inline float onePole(float hz, double sr) noexcept {
  return (float)std::exp(-kTwoPi * (double)hz / sr);
}

/// The line clipping, which it does early and softly.
inline float squeeze(float x, float drive) noexcept {
  return std::tanh(x * drive) / drive;
}
} // namespace

void BucketEcho::Line::resize(int length) {
  buffer.assign((size_t)std::max(1, length), 0.0f);
  write = 0;
}

void BucketEcho::Line::clear() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  write = 0;
  damp = 0.0f;
  dc = 0.0f;
  phase = 0.0;
}

float BucketEcho::Line::read(float delaySamples) const noexcept {
  const auto length = (int)buffer.size();

  if (length <= 1)
    return 0.0f;

  const auto clamped = std::clamp(delaySamples, 1.0f, (float)(length - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  auto index = write - whole;
  while (index < 0)
    index += length;

  auto previous = index - 1;
  if (previous < 0)
    previous += length;

  const auto a = buffer[(size_t)index];
  const auto b = buffer[(size_t)previous];

  return a + (b - a) * frac;
}

void BucketEcho::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);

  // Room for the longest setting and for the clock to wander past it.
  bufferLength = (int)(kMaxTimeSeconds * 1.05 * sampleRate) + 4;

  left.resize(bufferLength);
  right.resize(bufferLength);

  left.rateHz = kSweepL;
  right.rateHz = kSweepR;

  left.rng = Xorshift{0x1f123bb5u};
  right.rng = Xorshift{0x27d4eb2du};

  reset();
}

void BucketEcho::reset() noexcept {
  left.clear();
  right.clear();

  // Not the same place, or the two clocks would start in step and the first
  // second of a patch would be in mono.
  right.phase = 0.37;

  drift = 0.0;
  smoothedDelay = -1.0f;
  envelope = 0.0f;
  wasEnabled = false;
}

float BucketEcho::tailSeconds(const EchoParams &p) const noexcept {
  if (!p.enabled || p.type != EchoType::Bucket || p.mix <= 0.0f)
    return 0.0f;

  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);

  if (feedback <= 0.0f)
    return p.timeSeconds;

  const auto passes = std::log(0.001f) / std::log(feedback);

  return std::min(30.0f, p.timeSeconds * passes);
}

void BucketEcho::process(float *outL, float *outR, int numSamples,
                         const EchoParams &p) noexcept {
  if (numSamples <= 0 || bufferLength <= 0)
    return;

  if (!p.enabled || p.type != EchoType::Bucket) {
    if (wasEnabled)
      reset();

    return;
  }

  wasEnabled = true;

  const auto mix = std::clamp(p.mix, 0.0f, 1.0f);
  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);
  const auto age = std::clamp(p.age, 0.0f, 1.0f);
  const auto time = std::clamp(p.timeSeconds, 0.01f, kMaxTimeSeconds);

  const auto targetDelay = (float)(time * sampleRate);

  if (smoothedDelay < 0.0f)
    smoothedDelay = targetDelay;

  const auto glide = (float)std::exp(-1.0 / (0.09 * sampleRate));

  // The clock first, which is the setting, and then the wear on top of it.
  // Halved at the top of AGE, so an old pedal at a long setting is very dark
  // indeed and a new one set short is still recognisably the signal.
  const auto clockTone =
      std::clamp(kClockToneHz * (kClockRefSeconds / time), 420.0f, 12000.0f);
  const auto toneHz = clockTone * (1.0f - 0.5f * age);

  const auto lpCoef = onePole(toneHz, sampleRate);
  const auto hpCoef = onePole(70.0f, sampleRate);

  // How hard the line is driven, which is what makes the tail grow rather than
  // only fade. Inside the loop, so it compounds on every pass.
  const auto drive = 1.0f + age * 3.0f;

  // The compander. Fast enough to open before the repeat it belongs to and
  // slow enough to still be open under the tail of it, which is the
  // mistracking that makes the hiss swell behind a chord rather than sit
  // under it.
  const auto envAttack = (float)std::exp(-1.0 / (0.005 * sampleRate));
  const auto envRelease = (float)std::exp(-1.0 / (0.45 * sampleRate));

  // Enough to hear as the floor of an old pedal, and no more: this is the
  // thing that would be unbearable if it ran free.
  const auto hiss = age * age * 0.006f;

  for (int n = 0; n < numSamples; ++n) {
    smoothedDelay = targetDelay + (smoothedDelay - targetDelay) * glide;

    // Each clock wandering by its own slow amount around the same setting.
    // This is the whole of the stereo: nothing crosses between the sides.
    left.phase += left.rateHz / sampleRate;
    right.phase += right.rateHz / sampleRate;

    if (left.phase >= 1.0)
      left.phase -= 1.0;
    if (right.phase >= 1.0)
      right.phase -= 1.0;

    drift += kDriftRate / sampleRate;

    if (drift >= 1.0)
      drift -= 1.0;

    // The whole clock drifting, which both sides follow exactly.
    const auto together =
        smoothedDelay * kDriftDepth * (float)std::sin(kTwoPi * drift);

    const auto sweepL = together + smoothedDelay * kSweepDepthL *
                                       (float)std::sin(kTwoPi * left.phase);

    const auto sweepR = together + smoothedDelay * kSweepDepthR *
                                       (float)std::sin(kTwoPi * right.phase);

    const auto wetL = left.read(smoothedDelay + sweepL);
    const auto wetR = right.read(smoothedDelay + sweepR);

    // What the compander is working from. One envelope for both sides, since
    // one compander sees the whole signal.
    const auto loudest = std::max(std::abs(wetL), std::abs(wetR));
    const auto coef = loudest > envelope ? envAttack : envRelease;

    envelope = loudest + (envelope - loudest) * coef;

    // The expander's floor: the hiss is only let through in proportion to
    // what is there to hide it, which is why a silent patch stays silent.
    const auto opened = envelope / (envelope + 0.02f);
    const auto noiseL = (left.rng.bipolar() * hiss) * opened;
    const auto noiseR = (right.rng.bipolar() * hiss) * opened;

    // The reconstruction filter and the coupling, then the line clipping.
    left.damp = wetL + (left.damp - wetL) * lpCoef;
    right.damp = wetR + (right.damp - wetR) * lpCoef;

    left.dc = left.damp + (left.dc - left.damp) * hpCoef;
    right.dc = right.damp + (right.dc - right.damp) * hpCoef;

    const auto agedL = squeeze(left.damp - left.dc, drive);
    const auto agedR = squeeze(right.damp - right.dc, drive);

    const auto dryL = outL[n];
    const auto dryR = outR[n];

    left.buffer[(size_t)left.write] = dryL + agedL * feedback;
    right.buffer[(size_t)right.write] = dryR + agedR * feedback;

    if (++left.write >= bufferLength)
      left.write = 0;
    if (++right.write >= bufferLength)
      right.write = 0;

    // The hiss arrives with the repeats rather than in the loop, so feedback
    // cannot pile it up into something that never decays.
    outL[n] = dryL + (wetL + noiseL - dryL) * mix;
    outR[n] = dryR + (wetR + noiseR - dryR) * mix;
  }
}

} // namespace ovt
