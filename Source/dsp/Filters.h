#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace ovt {

/// The coefficient of a one-pole low pass with its corner at `hz`: the share
/// of the previous output each sample keeps. The callers that take a cutoff
/// from a knob clamp it to the rate first.
inline float onePole(double hz, double sampleRate) noexcept {
  return (float)std::exp(-6.283185307179586 * hz / sampleRate);
}

/// The sections the console and the cassette machines are built from, and
/// the designs they use.
namespace filters {

inline constexpr double kPi = 3.14159265358979323846;

/// A corner frequency in radians per second, prewarped so the digital
/// filter's corner lands where the analogue one's was.
inline double warped(double hz, double sampleRate) noexcept {
  const double nyquistSafe = std::min(hz, 0.45 * sampleRate);
  return 2.0 * sampleRate * std::tan(kPi * nyquistSafe / sampleRate);
}

/// One first-order section, from an analogue prototype by the bilinear
/// transform with its corner prewarped.
struct FirstOrder {
  double b0 = 1.0, b1 = 0.0, a1 = 0.0;
  double x1 = 0.0, y1 = 0.0;

  /// (c1 s + c0) / (d1 s + d0), with s in radians per second.
  void design(double c1, double c0, double d1, double d0, double k) noexcept {
    const double norm = d1 * k + d0;

    b0 = (c1 * k + c0) / norm;
    b1 = (c0 - c1 * k) / norm;
    a1 = (d0 - d1 * k) / norm;
  }

  double run(double x) noexcept {
    const double y = b0 * x + b1 * x1 - a1 * y1;

    x1 = x;
    y1 = y;

    return y;
  }

  void clear() noexcept { x1 = y1 = 0.0; }
};

/// One second-order section.
struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
  double z1 = 0.0, z2 = 0.0;

  double run(double x) noexcept {
    const double y = b0 * x + z1;

    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;

    return y;
  }

  void clear() noexcept { z1 = z2 = 0.0; }
};

/// A peaking section, the analogue prototype's response at w0 by the bilinear
/// transform, as the cookbook gives it.
inline void peaking(Biquad &s, double hz, double gainDb, double q,
                    double sampleRate) noexcept {
  const double w = 2.0 * kPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
  const double a = std::pow(10.0, gainDb / 40.0);
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha / a;

  s.b0 = (1.0 + alpha * a) / a0;
  s.b1 = -2.0 * cosw / a0;
  s.b2 = (1.0 - alpha * a) / a0;
  s.a1 = -2.0 * cosw / a0;
  s.a2 = (1.0 - alpha / a) / a0;
}

/// A second order low pass, as the cookbook gives it.
inline void lowPass(Biquad &s, double hz, double q,
                    double sampleRate) noexcept {
  const double w = 2.0 * kPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;

  s.b0 = (1.0 - cosw) / 2.0 / a0;
  s.b1 = (1.0 - cosw) / a0;
  s.b2 = (1.0 - cosw) / 2.0 / a0;
  s.a1 = -2.0 * cosw / a0;
  s.a2 = (1.0 - alpha) / a0;
}

/// A second order high pass, as the cookbook gives it.
inline void highPass(Biquad &s, double hz, double q,
                     double sampleRate) noexcept {
  const double w = 2.0 * kPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;

  s.b0 = (1.0 + cosw) / 2.0 / a0;
  s.b1 = -(1.0 + cosw) / a0;
  s.b2 = (1.0 + cosw) / 2.0 / a0;
  s.a1 = -2.0 * cosw / a0;
  s.a2 = (1.0 - alpha) / a0;
}

/// A band pass whose peak is one.
///
/// @return its noise bandwidth in hertz, which is what sizes the white noise
///         through it to a wanted rms.
inline double bandPass(Biquad &s, double lowHz, double highHz,
                       double sampleRate) noexcept {
  const double centre = std::sqrt(lowHz * highHz);
  const double q = centre / (highHz - lowHz);
  const double w = 2.0 * kPi * centre / sampleRate;
  const double alpha = std::sin(w) / (2.0 * q);
  const double cosw = std::cos(w);
  const double a0 = 1.0 + alpha;

  s.b0 = alpha / a0;
  s.b1 = 0.0;
  s.b2 = -alpha / a0;
  s.a1 = -2.0 * cosw / a0;
  s.a2 = (1.0 - alpha) / a0;

  return 0.5 * kPi * centre / q;
}

/// A section's gain at a frequency.
inline double magnitudeAt(const FirstOrder &s, double hz,
                          double sampleRate) noexcept {
  const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / sampleRate);

  return std::abs((s.b0 + s.b1 * z1) / (1.0 + s.a1 * z1));
}

inline double magnitudeAt(const Biquad &s, double hz,
                          double sampleRate) noexcept {
  const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / sampleRate);

  return std::abs((s.b0 + s.b1 * z1 + s.b2 * z1 * z1) /
                  (1.0 + s.a1 * z1 + s.a2 * z1 * z1));
}

} // namespace filters
} // namespace ovt
