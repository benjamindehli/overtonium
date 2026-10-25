#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "Character.h"
#include "Halfband.h"

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
/// double here, and the antiderivative is written in a form without
/// cancellation where the plain one would lose its digits.
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
  /// How far behind the input the output runs, in samples at the rate the
  /// host is using. The stage runs at twice that rate, so a signal passes
  /// through two half-band filters on the way there and back, and this is
  /// what they cost between them. It does not depend on the character or the
  /// amount: a bypassed stage delays by the same, through a plain delay
  /// rather than through the filters, so switching one on moves nothing.
  static constexpr int kLatency = Halfband::kHalf;

  void reset() noexcept {
    heat = 0.0;
    left = Channel{};
    right = Channel{};
  }

  void prepare(double newSampleRate) noexcept {
    sampleRate = std::max(1.0, newSampleRate);
    reset();
  }

  /// How hard each circuit is run, which is a property of the part rather
  /// than a control: there is no knob for this and there was only ever a
  /// temporary one, for finding these. The table and what each was measured
  /// against are in BusDrive.cpp.
  static double amountFor(Character c) noexcept;

  /// @param amount  normally amountFor(c). Taken rather than looked up so
  /// that a test can sweep it, which is how the two bugs recorded below were
  /// found: nothing in the instrument passes anything else.
  void process(float *left, float *right, int numSamples, Character c,
               float amount) noexcept;

private:
  /// How a circuit gives way.
  ///
  /// A triode and a pair of diodes bend gradually and never quite arrive
  /// anywhere, which is what a tanh does. An op-amp is a feedback amplifier:
  /// it holds a straight line until it meets the rail and then stops, so its
  /// knee is a corner rather than a bend. Both are odd and both have unit
  /// slope at rest, so everything either side of them is the same.
  ///
  /// The third is not a circuit giving way at all. It is the slight bow in a
  /// converter or an output stage, measured on one of the synthesizers, which
  /// leans one half of the wave out and the other in by the same small amount
  /// and so makes only a second harmonic, and sum and difference tones between
  /// notes. See kPdSummingH2Db.
  enum class Shape { Tanh, Cubic, Quadratic };
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
    Shape shape = Shape::Tanh;
  };

  /// Not every circuit bends a waveform.
  ///
  /// Four of them do, and are described by a curve. A lamp does not: it is a
  /// resistance that rises as it warms, so what it changes is how loud the mix
  /// is rather than what shape it has, and it takes about half a second to
  /// change its mind. That adds no overtones at all, which is why it is the
  /// one machine here that needs none of the antialiasing the others are
  /// built around and can run at the rate it arrives at.
  enum class Machine { None, Curve, Lamp };

  /// What one character does at one amount.
  struct Recipe {
    Machine machine = Machine::None;
    Curve curve;

    /// How far the lamp pulls the mix back once it is thoroughly warm.
    double depth = 0.0;

    /// How fast it warms and cools, per sample at the rate the host is using.
    double warming = 0.0;
  };

  static Recipe recipeFor(Character c, float amount,
                          double sampleRate) noexcept;

  /// Which half of the wave a sample is on, counted from where the bias put
  /// the rest position rather than from zero.
  static const Side &sideFor(const Curve &k, double u) noexcept {
    return u >= 0.0 ? k.up : k.down;
  }

  /// The bend itself, of unit slope at rest so that whatever is normalised
  /// around it stays an overtone control rather than a second volume.
  static double bend(Shape shape, double u) noexcept {
    if (shape == Shape::Tanh)
      return std::tanh(u);

    // Of unit slope at rest like the others, and leaning the positive half in.
    // The bow was measured on a mix of at most three voices, a small part of
    // the way to u = 1, and a parabola left to itself past there does what no
    // output stage does: the positive half turns back and gets quieter as it
    // is pushed harder, and the negative half bends further without limit. So
    // past u = 1 each half runs out of room instead. The positive half holds
    // the top it has reached, where its slope is already nothing. The negative
    // half bends over from u = -1, its slope falling from two to nothing over
    // one more unit, and holds there. Those edges are chosen, not measured,
    // and only a dense chord played hard reaches them on the 6581. PD's drive
    // puts them about seventy times full scale out.
    if (shape == Shape::Quadratic) {
      if (u >= 1.0)
        return 0.5;

      if (u > -1.0)
        return u - 0.5 * u * u;

      const auto v = std::min(-1.0 - u, 1.0);

      return -1.5 - 2.0 * v + v * v;
    }

    // Straight until it meets the rail and flat past it, joined so the slope
    // arrives at nothing rather than at a corner. What an amplifier with
    // feedback around it does: linear, linear, and then no more.
    const auto a = std::abs(u);

    return a >= 1.0 ? std::copysign(2.0 / 3.0, u) : u - u * u * u / 3.0;
  }

  static double shape(const Curve &k, double x) noexcept {
    const auto u = x + k.bias;
    const auto &s = sideFor(k, u);

    return (bend(k.shape, s.drive * u) - k.offset) * s.scale;
  }

  /// log(cosh(u)), which is the antiderivative of tanh and the whole of the
  /// antialiasing.
  ///
  /// Two forms, because neither is good everywhere. Past the crossover, cosh
  /// overflows anything it is asked for directly, so it is written out of
  /// log1p. Below it, that form is the difference of two numbers near log 2
  /// that cancel down to almost nothing, so cosh(a) - 1 is written as
  /// 2 sinh(a / 2)^2 instead, which has no cancellation in it.
  ///
  /// Both forms are exact, so they agree at the crossover to the last bit.
  /// They have to. The difference quotient divides the change in this function
  /// by the change in the input, and near the top of a wave that change is
  /// tiny. A jump of 1e-5 where the two forms meet becomes an error of a tenth
  /// of full scale on the one sample that crosses it, on the way up and again
  /// on the way down, which is heard as a buzz on every note loud enough to
  /// reach it. A truncated series has exactly that jump.
  static double logCosh(double u) noexcept {
    const auto a = std::abs(u);

    if (a < 0.5) {
      const auto half = std::sinh(0.5 * a);
      return std::log1p(2.0 * half * half);
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
  /// The antiderivative of bend, even where bend is odd, and nothing at
  /// rest so that the two halves of a curve meet where they should.
  static double bendIntegral(Shape shape, double u) noexcept {
    if (shape == Shape::Tanh)
      return logCosh(u);

    // Not even, because the quadratic is not odd. Each piece starts from
    // where the one before it ends, so the pieces agree where they meet.
    if (shape == Shape::Quadratic) {
      if (u >= 1.0)
        return 1.0 / 3.0 + 0.5 * (u - 1.0);

      if (u > -1.0)
        return u * u * (0.5 - u / 6.0);

      const auto v = -1.0 - u;

      if (v < 1.0)
        return 2.0 / 3.0 + v * (1.5 + v - v * v / 3.0);

      return 17.0 / 6.0 + 2.5 * (v - 1.0);
    }

    const auto a = std::abs(u);

    if (a >= 1.0)
      return 5.0 / 12.0 + (2.0 / 3.0) * (a - 1.0);

    const auto s = a * a;

    return s * 0.5 - s * s / 12.0;
  }

  static double integral(const Curve &k, double x) noexcept {
    const auto u = x + k.bias;
    const auto &s = sideFor(k, u);

    return (bendIntegral(k.shape, s.drive * u) / s.drive - k.offset * u) *
           s.scale;
  }

  /// Below this the divisor of the difference quotient is doing more harm than
  /// the antialiasing does good, so the curve is read at the midpoint instead.
  static constexpr double kFlat = 1.0e-9;

  static double processSample(const Curve &k, double x, double &last) noexcept {
    const auto dx = x - last;

    const auto y = std::abs(dx) < kFlat
                       ? shape(k, 0.5 * (x + last))
                       : (integral(k, x) - integral(k, last)) / dx;

    last = x;

    return y;
  }

  /// How much the filter that brings the curve back down lifts the top, to
  /// undo what the antialiasing takes off it.
  ///
  /// Reading the curve as its average over the step from the last sample is
  /// what keeps it from aliasing, and on anything quiet enough to stay on the
  /// straight part of a curve that average is just the mean of two
  /// neighbouring samples at the doubled rate. That is a low pass of its own:
  /// cos(w / 2), which at 48 kHz takes 0.4 dB off 9 kHz, 1.1 dB off 15 kHz and
  /// 2 dB off 20 kHz, on every character with a bus stage.
  ///
  /// [-k, 1 + 2k, -k] at the doubled rate lifts the top by close to the
  /// inverse. At this k the two together stay within 0.2 dB from 20 Hz to
  /// 20 kHz at 44.1, 48 and 96 kHz.
  static constexpr double kLift = 0.17;

  /// The filter that brings the curve back down: the half-band with the lift
  /// folded into it, two taps longer and still symmetric. Its middle is one
  /// tap further in, and the window is taken one sample earlier to match, so
  /// it lands on the same sample as the half-band alone and the stage's
  /// latency does not move.
  static const std::array<double, Halfband::kTaps + 2> &downKernel() noexcept {
    static const auto built = [] {
      const auto &h = Halfband::kernel();
      const double lift[3] = {-kLift, 1.0 + 2.0 * kLift, -kLift};
      std::array<double, Halfband::kTaps + 2> d{};

      for (size_t m = 0; m < h.size(); ++m)
        for (size_t j = 0; j < 3; ++j)
          d[m + j] += h[m] * lift[j];

      return d;
    }();

    return built;
  }

  /// Everything one side of the stereo field remembers.
  struct Channel {
    /// The samples as they arrived, newest first. Both the doubling and the
    /// bypass read out of this, which is what keeps the two the same length.
    std::array<double, Halfband::kHalf + 1> in{};

    /// What came back from the circuit at the doubled rate, newest first, as
    /// long as the filter that brings it back down. See downKernel.
    std::array<double, Halfband::kTaps + 2> up{};

    double last = 0.0; ///< the curve's previous input

    /// A curve that leans to one side rectifies as well as distorting, and
    /// the offset that leaves is inaudible, eats headroom and thumps when a
    /// chord lands. Every valve stage worth the name is AC-coupled for the
    /// same reason, at about the same corner.
    double dc = 0.0, dcIn = 0.0;
  };

  /// One sample in, one sample out, with the circuit run twice in between.
  double run(Channel &c, const Curve &k, double x, double dcCoef) noexcept;

  /// The samples that arrived, handed back the same but late. What a stage
  /// with nothing to do returns, and what the lamp works on, since it changes
  /// a level rather than a shape and has no use for the doubled rate.
  double delayed(Channel &c, double x) noexcept;

  double sampleRate = 44100.0;

  /// How warm the lamp is, which is the mean power it has been seeing. One
  /// between the two channels rather than one each: a bus has one amplifier
  /// on it, and a lamp that leaned back on whichever side happened to be
  /// louder would move the image rather than the level.
  double heat = 0.0;

  Channel left, right;
};

} // namespace ovt
