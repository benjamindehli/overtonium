#pragma once

#include <cmath>

#include "Character.h"

namespace ovt {

/// The summing amplifier the whole series runs into.
///
/// Everything else in this instrument treats a partial on its own. Each one
/// has its own oscillator, its own envelope and, since the characters arrived,
/// its own circuit and its own place in a rack of thirty-two. Two partials
/// sounding together therefore produce exactly the sum of what each produces
/// alone, which is true of no real instrument and of no real desk.
///
/// This is the one place they meet. A nonlinearity on the mix produces sum and
/// difference tones between every pair of partials, so a loud chord fuses
/// rather than stacking, and a quiet one is left alone because a curve this
/// gentle is a straight line until something is big enough to bend it. That is
/// also why nothing measures the level here: a saturator is level-dependent by
/// construction, and an envelope follower would only be a slower way of
/// finding out what the sample in hand already says.
///
/// It sits after the voices and before the wobble, the echo and the reverb,
/// which is where a summing amplifier sits on a desk: the effects hear the
/// saturated bus, rather than the bus saturating their tails. It is ahead of
/// the master fader for the same reason a gain stage is ahead of a volume
/// control, so turning down cleans up nothing.
///
/// **The one thing it costs.** Every character is a band-limited table
/// precisely so that nothing folds back down the series. A curve on the mix
/// cannot make that promise: the second harmonic of an 18 kHz partial has
/// nowhere to be at 48 kHz but 12 kHz. What is here instead is first-order
/// antiderivative antialiasing, which buries the folded products rather than
/// preventing them, costs a handful of operations on two channels rather than
/// on the 512 oscillator reads a note already is, and adds half a sample of
/// delay rather than the several hundred an oversampler would report to the
/// host.
///
/// It buys about ten decibels, which is worth having and is not a solution.
/// An 18 kHz partial at 80% of full scale folds back at -26 dB wide open,
/// against -16 dB reading the curve directly, and at -44 against -35 a quarter
/// of the way up. A partial that loud that high is a test case rather than a
/// patch, since the series is quietest where this is worst. If the stage
/// stays, the next thing it wants is to run at twice the rate.
class BusDrive {
public:
  void reset() noexcept {
    lastL = lastR = 0.0f;
    lastFL = lastFR = 0.0f;
    dcL = dcR = 0.0f;
    dcInL = dcInR = 0.0f;
    primed = false;
  }

  void prepare(double newSampleRate) noexcept {
    sampleRate = std::max(1.0, newSampleRate);
    reset();
  }

  /// @param amount  how hard the series runs into it, 0 to 1. Zero is not a
  /// gentle setting but a bypass: the samples are left exactly as they came.
  void process(float *left, float *right, int numSamples, Character c,
               float amount) noexcept;

private:
  /// Small-signal gain of one, whatever the drive, so turning this up adds
  /// overtones rather than level and A against B stays a fair comparison.
  struct Curve {
    float drive = 0.0f;
    float bias = 0.0f;
    float offset = 0.0f; ///< what the bias does at zero, subtracted back off
    float scale = 1.0f;  ///< 1 / the slope at zero
  };

  static Curve curveFor(Character c, float amount) noexcept;

  static float shape(const Curve &k, float x) noexcept {
    return (std::tanh(k.drive * (x + k.bias)) - k.offset) * k.scale;
  }

  /// Antiderivative of shape, for the pair of samples either side of a step to
  /// be averaged over rather than sampled at.
  static float integral(const Curve &k, float x) noexcept {
    // log(cosh(u)) written so it cannot overflow: cosh of anything past about
    // 89 is infinity in single precision, and a loud sample at a high drive
    // gets there easily.
    const auto u = k.drive * (x + k.bias);
    const auto a = std::abs(u);
    const auto logCosh = a + std::log1p(std::exp(-2.0f * a)) - kLog2;

    return (logCosh / k.drive - k.offset * x) * k.scale;
  }

  static constexpr float kLog2 = 0.6931472f;

  /// Below this the divisor of the difference quotient is doing more harm than
  /// the antialiasing does good, so the curve is read at the midpoint instead.
  static constexpr float kFlat = 1.0e-5f;

  float processSample(const Curve &k, float x, float &last,
                      float &lastF) noexcept {
    const auto f = integral(k, x);
    const auto dx = x - last;

    const auto y =
        std::abs(dx) < kFlat ? shape(k, 0.5f * (x + last)) : (f - lastF) / dx;

    last = x;
    lastF = f;

    return y;
  }

  double sampleRate = 44100.0;

  float lastL = 0.0f, lastR = 0.0f;
  float lastFL = 0.0f, lastFR = 0.0f;

  /// A curve that leans to one side rectifies as well as distorting, and the
  /// offset that leaves is inaudible, eats headroom and thumps when a chord
  /// lands. Every valve stage worth the name is AC-coupled for the same
  /// reason, at about the same corner.
  float dcL = 0.0f, dcR = 0.0f;
  float dcInL = 0.0f, dcInR = 0.0f;

  /// The first sample after a reset has nothing behind it to average against,
  /// so it reads the curve directly rather than a difference quotient taken
  /// against a zero that was never really there.
  bool primed = false;
};

} // namespace ovt
