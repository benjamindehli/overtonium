#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "Character.h"
#include "Halfband.h"

namespace ovt {

/// The summing amplifier the whole series runs into.
///
/// Every partial is otherwise treated on its own, so this is the one place
/// they meet. A gentle nonlinearity on the mix gives sum and difference
/// tones between partials, so a loud chord fuses while a quiet one is left
/// alone. It sits after the voices and before the wobble, the echo, the
/// reverb and the master fader, as a summing amplifier does on a desk.
///
/// The curve runs at twice the host rate with first-order antiderivative
/// antialiasing, since a curve on the mix cannot be band-limited the way
/// the characters are.
///
/// Two things the arithmetic must keep:
///
/// It is all done in double, and the antiderivative is written without
/// cancellation. Normalising by the drive is the difference of two nearly
/// equal numbers over a small one, and single precision loses the gain at
/// low drive.
///
/// Nothing is carried over a block boundary but the previous input sample.
/// What the curve made of it is worked out again under the current curve,
/// since mixing two curves' antiderivatives while the drive moves gives a
/// spike once per block.
class BusDrive {
public:
  /// How far behind the input the output runs, in samples at the host rate:
  /// the cost of the two half-band filters. It does not depend on the
  /// character or the amount, since a bypassed stage delays by the same, so
  /// the latency the host sees never changes with the patch.
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

  /// How hard each circuit is run, a property of the part rather than a
  /// control. The table is in BusDrive.cpp.
  static double amountFor(Character c) noexcept;

  /// @param amount  normally amountFor(c). Taken as a parameter so that a test
  /// can sweep it.
  void process(float *left, float *right, int numSamples, Character c,
               float amount) noexcept;

private:
  /// How a circuit gives way.
  ///
  /// Tanh bends gradually, like a triode or a pair of diodes. Cubic holds a
  /// straight line until it meets the rail, like an op-amp. Both are odd with
  /// unit slope at rest. Quadratic is the slight bow of a converter or output
  /// stage, which makes only a second harmonic and sum and difference tones.
  /// See kPdSummingH2Db.
  enum class Shape { Tanh, Cubic, Quadratic };
  /// One side of the wave. Two of them, because a circuit need not treat the
  /// two halves alike.
  ///
  /// The scale is the reciprocal of the drive, which gives every curve a
  /// small-signal gain of one, so the amount adds overtones rather than level.
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

  /// What kind of stage a character has. Curve bends the waveform. Lamp is a
  /// resistance that rises as it warms over about half a second, so it changes
  /// the level and not the shape, adds no overtones and runs at the host rate
  /// without antialiasing.
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

  /// The bend itself, of unit slope at rest so that the amount controls
  /// overtones and not level.
  static double bend(Shape shape, double u) noexcept {
    if (shape == Shape::Tanh)
      return std::tanh(u);

    // Of unit slope at rest, leaning the positive half in. Past u = 1 a plain
    // parabola would turn back, so each half runs out of room instead: the
    // positive half holds its top, and the negative half bends over from
    // u = -1 to flat over one more unit. These edges are chosen, not measured.
    if (shape == Shape::Quadratic) {
      if (u >= 1.0)
        return 0.5;

      if (u > -1.0)
        return u - 0.5 * u * u;

      const auto v = std::min(-1.0 - u, 1.0);

      return -1.5 - 2.0 * v + v * v;
    }

    // Straight until it meets the rail and flat past it, joined so the slope
    // reaches zero without a corner.
    const auto a = std::abs(u);

    return a >= 1.0 ? std::copysign(2.0 / 3.0, u) : u - u * u * u / 3.0;
  }

  static double shape(const Curve &k, double x) noexcept {
    const auto u = x + k.bias;
    const auto &s = sideFor(k, u);

    return (bend(k.shape, s.drive * u) - k.offset) * s.scale;
  }

  /// log(cosh(u)), the antiderivative of tanh.
  ///
  /// Two forms, because neither is good everywhere. Past the crossover it is
  /// written with log1p so cosh does not overflow. Below it, cosh(a) - 1 is
  /// written as 2 sinh(a / 2)^2 to avoid cancellation.
  ///
  /// Both forms are exact so they agree at the crossover to the last bit. The
  /// difference quotient divides by a tiny step near the top of a wave, so any
  /// jump there would be heard as a buzz.
  static double logCosh(double u) noexcept {
    const auto a = std::abs(u);

    if (a < 0.5) {
      const auto half = std::sinh(0.5 * a);
      return std::log1p(2.0 * half * half);
    }

    return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
  }

  /// The antiderivative of bend, zero at rest so that the two halves of a
  /// curve meet where they should. Written in u rather than in x so a step
  /// that straddles the crossing is averaged correctly.
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
  /// The antialiased curve averages two neighbouring samples on its straight
  /// part, a low pass of cos(w / 2). [-k, 1 + 2k, -k] at the doubled rate
  /// lifts the top by close to the inverse, keeping the pair within 0.2 dB
  /// from 20 Hz to 20 kHz.
  static constexpr double kLift = 0.17;

  /// The filter that brings the curve back down: the half-band with the lift
  /// folded into it, two taps longer and still symmetric. The window is taken
  /// one sample earlier to match, so the stage's latency does not move.
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
    /// bypass read out of this, which keeps their delays the same.
    std::array<double, Halfband::kHalf + 1> in{};

    /// What came back from the circuit at the doubled rate, newest first, as
    /// long as the filter that brings it back down. See downKernel.
    std::array<double, Halfband::kTaps + 2> up{};

    double last = 0.0; ///< the curve's previous input

    /// A DC blocker. A curve that leans to one side leaves an offset that eats
    /// headroom and thumps when a chord lands.
    double dc = 0.0, dcIn = 0.0;
  };

  /// One sample in, one sample out, with the circuit run twice in between.
  double run(Channel &c, const Curve &k, double x, double dcCoef) noexcept;

  /// The samples that arrived, handed back the same but late. Used by a stage
  /// with nothing to do and by the lamp, which has no use for the doubled
  /// rate.
  double delayed(Channel &c, double x) noexcept;

  double sampleRate = 44100.0;

  /// How warm the lamp is, the mean power it has been seeing. Shared between
  /// the two channels so the lamp moves the level and not the stereo image.
  double heat = 0.0;

  Channel left, right;
};

} // namespace ovt
