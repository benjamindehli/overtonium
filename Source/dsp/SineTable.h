#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ovt {

/// One turn of a waveform, sampled and read back with linear interpolation.
///
/// 4096 points keep the distortion around -95 dBFS at a fraction of the cost
/// of std::sin. Every oscillator waveform reads through this class at this
/// size, so a partial with a character costs the same as a plain sine. See
/// Character.h.
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

  /// The same read from a fixed-point phase, a 64-bit fraction of a turn: the
  /// top kBits are the entry and the rest the fraction towards the next.
  inline float at(std::uint64_t phase) const noexcept {
    constexpr int kFractionBits = 64 - kBits;
    constexpr float kScale = 1.0f / 4503599627370496.0f; // 2^52

    const auto i0 = (size_t)(phase >> kFractionBits);
    const auto f =
        (float)(phase & ((std::uint64_t{1} << kFractionBits) - 1u)) * kScale;

    const float a = tbl[i0];
    const float b = tbl[i0 + 1];
    return a + f * (b - a);
  }

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

/// Converts a phase in turns, or a step in turns per sample, to the 64-bit
/// fraction of a turn the oscillators run on, where adding wraps by itself.
///
/// 64 bits rather than 32 because 32 rounds a step coarsely enough for a tone
/// to drift audibly off pitch.
inline std::uint64_t toFixedPhase(double turns) noexcept {
  // A phase just under one turn can round up to 2^64, which does not fit, so
  // it is clamped to the largest double below that.
  return (std::uint64_t)std::min(wrapPhase(turns) * 18446744073709551616.0,
                                 18446744073709549568.0);
}

inline std::uint64_t toFixedStep(double turnsPerSample) noexcept {
  return toFixedPhase(turnsPerSample);
}

/// A 64-bit fixed-point phase back to turns.
inline double fromFixedPhase(std::uint64_t phase) noexcept {
  return (double)phase * (1.0 / 18446744073709551616.0);
}

} // namespace ovt
