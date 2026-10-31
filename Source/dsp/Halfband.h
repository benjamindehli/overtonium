#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// A linear-phase half-band low pass, for running one stage at twice the rate
/// the rest of the instrument does.
///
/// The cut sits at a quarter of the doubled rate, which makes every second
/// coefficient zero. 25 taps, Kaiser windowed at beta 8, is where the folded
/// products of a hard rate limit stop falling (about -91 dB) for 12 samples of
/// latency.
class Halfband {
public:
  /// Taps either side of the middle one, which is also the group delay in
  /// samples at the rate the filter runs at. A round trip up and back down
  /// comes out this many samples late at the original rate.
  static constexpr int kHalf = 12;
  static constexpr int kTaps = 2 * kHalf + 1;

  /// The coefficients, built once and read for the life of the process.
  static const std::array<double, kTaps> &kernel() noexcept {
    static const auto built = build();
    return built;
  }

private:
  static std::array<double, kTaps> build() noexcept {
    std::array<double, kTaps> h{};

    constexpr double kPi = 3.141592653589793;
    constexpr double kBeta = 8.0;

    const auto denom = bessel0(kBeta);

    for (int i = -kHalf; i <= kHalf; ++i) {
      // sinc at half the sample spacing, which is the cut at a quarter of the
      // doubled rate and is what makes every second coefficient vanish.
      const auto t = 0.5 * (double)i;
      const auto sinc = i == 0 ? 0.5 : std::sin(kPi * t) / (kPi * (double)i);

      const auto r = (double)i / (double)kHalf;
      const auto w =
          bessel0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / denom;

      h[(size_t)(i + kHalf)] = sinc * w;
    }

    // The odd taps are normalised on their own and the middle tap is set to
    // exactly a half, so doubling passes the existing samples through
    // untouched. Normalising the whole sum would leave a faint copy of the
    // input in everything.
    double odd = 0.0;

    for (int i = -kHalf; i <= kHalf; ++i)
      if (i % 2 != 0)
        odd += h[(size_t)(i + kHalf)];

    for (int i = -kHalf; i <= kHalf; ++i)
      if (i % 2 != 0)
        h[(size_t)(i + kHalf)] *= 0.5 / odd;

    h[(size_t)kHalf] = 0.5;

    return h;
  }

  static double bessel0(double x) noexcept {
    double sum = 1.0, term = 1.0;

    for (int i = 1; i < 40; ++i) {
      term *= (x / 2.0) * (x / 2.0) / ((double)i * (double)i);
      sum += term;

      if (term < 1.0e-16 * sum)
        break;
    }

    return sum;
  }
};

} // namespace ovt
