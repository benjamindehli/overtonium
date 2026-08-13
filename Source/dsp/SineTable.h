#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace ovt {

/// Linearly interpolated sine lookup table.
///
/// A 4096-point table with linear interpolation keeps harmonic distortion
/// around -95 dBFS, which is inaudible here and roughly an order of magnitude
/// cheaper than calling std::sin once per sample. With 32 partials x 16 voices
/// that difference is the whole ball game.
class SineTable {
public:
  static constexpr int kBits = 12;
  static constexpr int kSize = 1 << kBits; // 4096
  static constexpr int kMask = kSize - 1;

  static const SineTable &instance() noexcept {
    static const SineTable t;
    return t;
  }

  /// @param phase01  phase in turns. Values outside [0, 1) are wrapped, but the
  /// caller is expected to keep it in range for accuracy.
  inline float operator()(double phase01) const noexcept {
    const double x = phase01 * (double)kSize;
    const auto i = (int64_t)x; // truncation; x >= 0 in normal use
    const auto f = (float)(x - (double)i);
    const auto i0 = (size_t)(i & kMask); // wrap keeps us in bounds regardless

    const float a = tbl[i0];
    const float b = tbl[i0 + 1]; // safe: tbl has kSize + 1 entries
    return a + f * (b - a);
  }

  /// cos(2*pi*phase01), i.e. the table shifted by a quarter turn.
  inline float cosine(double phase01) const noexcept {
    return (*this)(phase01 + 0.25);
  }

private:
  SineTable() noexcept {
    for (int i = 0; i <= kSize; ++i)
      tbl[(size_t)i] =
          (float)std::sin(6.283185307179586476 * (double)i / (double)kSize);
  }

  std::array<float, kSize + 1>
      tbl{}; // guard point at the end makes interpolation branch-free
};

/// Wraps a phase accumulator back into [0, 1). Handles increments > 1 turn.
inline double wrapPhase(double p) noexcept {
  if (p >= 1.0) {
    p -= std::floor(p);
  } else if (p < 0.0) {
    p -= std::floor(p);
    if (p >= 1.0) // guard against floor() rounding at the boundary
      p = 0.0;
  }

  return p;
}

} // namespace ovt
