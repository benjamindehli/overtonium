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
///
/// **Two things this arithmetic will do if it is written carelessly**, both
/// found by playing rather than by reading:
///
/// The curve is normalised by dividing by the drive, so as the drive goes to
/// nothing it is the difference of two nearly equal numbers over a third small
/// number. In single precision that loses most of its significant digits: at
/// the bottom of the knob a partial came through 1.4 dB quiet, which is a gain
/// error rather than the straight line it should have been. It is all done in
/// double here, and the antiderivative uses a series where its own cancellation
/// would bite.
///
/// And the antiderivative belongs to a particular curve. Carrying the last
/// one over a block boundary while the drive moves means dividing the
/// difference of two different functions by the step between two samples,
/// which is unbounded as that step goes to nothing. One spike per block for as
/// long as the knob is moving, into an echo with feedback on it. So nothing is
/// carried: the previous input sample is state, and what the curve made of it
/// is worked out again under the curve in hand.
class BusDrive {
public:
  void reset() noexcept {
    lastL = lastR = 0.0;
    slewL = slewR = 0.0;
    dcL = dcR = 0.0;
    dcInL = dcInR = 0.0;
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
  /// Not every circuit is a curve.
  ///
  /// Three of them bend a sample according to how big it is and nothing else,
  /// which is a function and can be antialiased by integrating it. The op-amp
  /// answers to how fast the mix is moving instead, which is a different
  /// machine with a memory, and no function of one sample can describe it.
  enum class Kind { None, Curve, Slew };
  /// One side of the wave. Two of them, because a circuit is not obliged to
  /// treat the two halves alike and the ones worth modelling do not: a pair of
  /// diodes that do not match runs out of room sooner one way than the other.
  ///
  /// The scale is the reciprocal of the drive, which is what gives every curve
  /// a small-signal gain of one whatever it is set to. Turning the amount up
  /// therefore adds overtones rather than level, and A against B stays a fair
  /// comparison.
  struct Side {
    double drive = 0.0;
    double scale = 1.0;
  };

  struct Curve {
    Side up, down;
    double bias = 0.0;
    double offset = 0.0; ///< what the bias does at rest, subtracted back off
  };

  /// What one character does at one amount, at the rate being rendered.
  ///
  /// The rate is in here because a slew limit is expressed in how much a
  /// sample may differ from the one before it, which is a different number at
  /// every rate for the same amplifier.
  struct Recipe {
    Kind kind = Kind::None;
    Curve curve;
    double step = 0.0; ///< Kind::Slew: the most one sample may move
  };

  static Recipe recipeFor(Character c, float amount,
                          double sampleRate) noexcept;

  /// Which half of the wave a sample is on, counted from where the bias put
  /// the rest position rather than from zero.
  static const Side &sideFor(const Curve &k, double u) noexcept {
    return u >= 0.0 ? k.up : k.down;
  }

  static double shape(const Curve &k, double x) noexcept {
    const auto u = x + k.bias;
    const auto &s = sideFor(k, u);

    return (std::tanh(s.drive * u) - k.offset) * s.scale;
  }

  /// log(cosh(u)), which is the antiderivative of tanh and the whole of the
  /// antialiasing.
  ///
  /// Two forms, because neither is good everywhere. Past the crossover, cosh
  /// overflows anything it is asked for directly, so it is written out of
  /// log1p. Below it, the direct form is the difference of two numbers near
  /// log 2 that cancel down to almost nothing, so the series it cancels to is
  /// used instead.
  static double logCosh(double u) noexcept {
    const auto a = std::abs(u);

    if (a < 0.5) {
      const auto s = a * a;
      return s * (0.5 - s * (1.0 / 12.0 - s * (1.0 / 45.0 - s / 315.0)));
    }

    return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
  }

  /// The antiderivative of shape, for the pair of samples either side of a
  /// step to be averaged over rather than sampled at.
  ///
  /// Written in u rather than in x so that both halves agree at the crossing:
  /// each is zero there, so the pair is one continuous function and a step
  /// that straddles the crossing is averaged correctly rather than picking up
  /// the difference between two constants.
  static double integral(const Curve &k, double x) noexcept {
    const auto u = x + k.bias;
    const auto &s = sideFor(k, u);

    return (logCosh(s.drive * u) / s.drive - k.offset * u) * s.scale;
  }

  /// Below this the divisor of the difference quotient is doing more harm than
  /// the antialiasing does good, so the curve is read at the midpoint instead.
  static constexpr double kFlat = 1.0e-9;

  /// An amplifier that cannot move faster than its rate, whatever it is asked
  /// for. Unlike the curves this cannot overshoot what went in, so a moving
  /// amount cannot make it jump: the worst a change of step does is a slightly
  /// different slope on the next sample.
  static double slewSample(double step, double x, double &y) noexcept {
    y += std::clamp(x - y, -step, step);

    return y;
  }

  static double processSample(const Curve &k, double x, double &last) noexcept {
    const auto dx = x - last;

    const auto y = std::abs(dx) < kFlat
                       ? shape(k, 0.5 * (x + last))
                       : (integral(k, x) - integral(k, last)) / dx;

    last = x;

    return y;
  }

  double sampleRate = 44100.0;

  double lastL = 0.0, lastR = 0.0;

  /// Where the amplifier had got to, which for a rate limit is the state that
  /// matters: the next sample is reached from here or not at all.
  double slewL = 0.0, slewR = 0.0;

  /// A curve that leans to one side rectifies as well as distorting, and the
  /// offset that leaves is inaudible, eats headroom and thumps when a chord
  /// lands. Every valve stage worth the name is AC-coupled for the same
  /// reason, at about the same corner.
  double dcL = 0.0, dcR = 0.0;
  double dcInL = 0.0, dcInR = 0.0;
};

} // namespace ovt
