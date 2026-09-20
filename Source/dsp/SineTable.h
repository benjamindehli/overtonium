#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace ovt {

/// One turn of a waveform, sampled and read back with linear interpolation.
///
/// A 4096-point table with linear interpolation keeps harmonic distortion
/// around -95 dBFS, which is inaudible here and roughly an order of magnitude
/// cheaper than calling std::sin once per sample. With 32 partials x 16 voices
/// that difference is the whole ball game.
///
/// Everything the oscillator can be reads through this one class, at this one
/// size, so a partial with character on it costs exactly what a plain sine
/// does: the same two loads and the same interpolation, from a different
/// table. See Character.h.
class Wave {
public:
  static constexpr int kBits = 12;
  static constexpr int kSize = 1 << kBits; // 4096
  static constexpr int kMask = kSize - 1;

  /// @param phase01  phase in turns. Values outside [0, 1) are wrapped, but the
  /// caller is expected to keep it in range for accuracy.
  inline float at(double phase01) const noexcept {
    const double x = phase01 * (double)kSize;
    const auto i = (int64_t)x; // truncation; x >= 0 in normal use
    const auto f = (float)(x - (double)i);
    const auto i0 = (size_t)(i & kMask); // wrap keeps us in bounds regardless

    const float a = tbl[i0];
    const float b = tbl[i0 + 1]; // safe: tbl has kSize + 1 entries
    return a + f * (b - a);
  }

  inline float operator()(double phase01) const noexcept { return at(phase01); }

  /// A quarter turn along, which for a sine is its cosine.
  inline float quarterTurnOn(double phase01) const noexcept {
    return at(phase01 + 0.25);
  }

  /// Fills the table from a function of phase in turns, plus the guard point
  /// that makes interpolation branch-free.
  template <typename Fn> void fill(Fn &&f) noexcept {
    for (int i = 0; i <= kSize; ++i)
      tbl[(size_t)i] = (float)f((double)(i & kMask) / (double)kSize);

    tbl[(size_t)kSize] = tbl[0];
  }

  const float *data() const noexcept { return tbl.data(); }

private:
  std::array<float, kSize + 1> tbl{};
};

/// The plain sine every partial is unless a character says otherwise.
class SineTable : public Wave {
public:
  static const SineTable &instance() noexcept {
    static const SineTable t;
    return t;
  }

  /// cos(2*pi*phase01), i.e. the table shifted by a quarter turn.
  inline float cosine(double phase01) const noexcept {
    return quarterTurnOn(phase01);
  }

private:
  SineTable() noexcept {
    fill([](double turns) { return std::sin(6.283185307179586476 * turns); });
  }
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
