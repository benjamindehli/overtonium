#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// A linear-phase half-band low pass, for running one stage at twice the rate
/// the rest of the instrument does.
///
/// Half-band because the job is always the same job: let everything below the
/// old Nyquist through and stop everything above it. A filter cut at exactly a
/// quarter of the doubled rate has every second coefficient equal to zero,
/// which is half the arithmetic for free and is why this shape is the one
/// everybody uses for doubling.
///
/// Twenty-five taps, from measuring rather than from taste. A rate limit is
/// the harshest thing this instrument does to a waveform, so it is what the
/// length was chosen against: a 3.1 kHz partial at 80% of full scale, limited
/// hard, leaving products where nothing legitimate can land.
///
///   taps  latency  worst folded product
///      9        4        -44 dB
///     17        8        -73 dB
///     25       12        -91 dB
///     33       16        -91 dB
///     49       24        -91 dB
///
/// Nine taps is not worth doing. Seventeen is already inaudible and would cost
/// four samples less. Twenty-five is where the curve flattens, so it is where
/// the length stops being a trade and starts being waste, and 0.25 ms of
/// latency is a quarter of one buffer at any rate anybody runs.
///
/// Kaiser windowed at beta 8, which puts the stopband past where the taps run
/// out, so the measurement above is the length talking rather than the window.
class Halfband {
public:
  /// Taps either side of the middle one. The group delay is this many samples
  /// at the rate the filter runs at, so a signal that goes up through one of
  /// these and back down through another comes out this many samples late at
  /// the rate it arrived at.
  static constexpr int kHalf = 12;
  static constexpr int kTaps = 2 * kHalf + 1;

  /// Built once, read for the life of the process.
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

    // Normalised in two halves rather than as a whole, because the middle tap
    // is not an ordinary one. It is the only even tap that is not zero, so it
    // alone carries the samples that were already there: fixing it at exactly
    // a half makes doubling hand those back untouched, and the interpolated
    // ones are the only ones the filter has to invent. Normalising the sum
    // instead leaves the middle tap near a half rather than at it, and that
    // difference is a quiet copy of the input running through everything.
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
