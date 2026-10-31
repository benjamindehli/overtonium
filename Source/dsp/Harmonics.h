#pragma once

#include <array>
#include <cmath>

#include "Exact.h"

namespace ovt {

/// Number of sine partials per voice.
inline constexpr int kNumHarmonics = 32;

/// Tuning data for one partial of the harmonic series.
///
/// Derived from the harmonic number n, so blend == 1 gives exactly n *
/// fundamental. Rounded to whole cents:
///
///         n   semis  cents   interval        n   semis  cents   interval
///          1     0      0    prime/octave   17    49     +5     minor second
///          2    12      0    prime/octave   18    50     +4     major second
///          3    19     +2    fifth          19    51     -2     minor third
///          4    24      0    prime/octave   20    52    -14     major third
///          5    28    -14    major third    21    53    -29     fourth
///          6    31     +2    fifth          22    54    -49     tritone
///          7    34    -31    minor seventh  23    54    +28     tritone
///          8    36      0    prime/octave   24    55     +2     fifth
///          9    38     +4    major second   25    56    -27     minor sixth
///         10    40    -14    major third    26    56    +41     minor sixth
///         11    42    -49    tritone        27    57     +6     major sixth
///         12    43     +2    fifth          28    58    -31     minor seventh
///         13    44    +41    minor sixth    29    58    +30     minor seventh
///         14    46    -31    minor seventh  30    59    -12     major seventh
///         15    47    -12    major seventh  31    59    +45     major seventh
///         16    48      0    prime/octave   32    60      0     prime/octave
struct HarmonicInfo {
  int harmonic = 1; ///< n, 1-based
  /// 1200 * log2(n): the true just interval above the fundamental.
  double exactCents = 0.0;
  int etSemitones = 0; ///< nearest 12-TET semitone
  /// Signed cent deviation of the just interval from etSemitones.
  double jiCents = 0.0;
  int pitchClass = 0; ///< etSemitones mod 12, used for the interval name/colour
};

/// The 32 partials, built once on first use.
inline const std::array<HarmonicInfo, kNumHarmonics> &harmonicTable() noexcept {
  static const std::array<HarmonicInfo, kNumHarmonics> table = [] {
    std::array<HarmonicInfo, kNumHarmonics> t{};

    for (int i = 0; i < kNumHarmonics; ++i) {
      const int n = i + 1;

      t[(size_t)i].harmonic = n;
      t[(size_t)i].exactCents = 1200.0 * std::log2((double)n);
      t[(size_t)i].etSemitones =
          (int)std::lround(t[(size_t)i].exactCents / 100.0);
      t[(size_t)i].jiCents =
          t[(size_t)i].exactCents - 100.0 * t[(size_t)i].etSemitones;
      t[(size_t)i].pitchClass = ((t[(size_t)i].etSemitones % 12) + 12) % 12;
    }

    return t;
  }();

  return table;
}

/// @param index0  zero-based partial index (0 == fundamental, 31 == 32nd
/// harmonic).
inline const HarmonicInfo &harmonic(int index0) noexcept {
  return harmonicTable()[(size_t)index0];
}

/// How far partial n sits above its own harmonic, in cents, following the
/// stiff-string law n * f0 * sqrt(1 + B * n^2). Applied on top of TUNE.
///
/// @param stretchCents  how far the top partial is displaced, used in place of
///                      B. Zero is the plain harmonic series, negative
///                      compresses it.
inline double inharmonicCents(int index0, double stretchCents) noexcept {
  if (exactly(stretchCents, 0.0))
    return 0.0;

  constexpr double top = (double)kNumHarmonics;

  // B is chosen so the top partial lands exactly on stretchCents.
  const double b = (std::exp2(stretchCents / 600.0) - 1.0) / (top * top);
  const double n = (double)(index0 + 1);

  return 600.0 * std::log2(std::max(1.0e-6, 1.0 + b * n * n));
}

/// Semitone offset above the played note.
///
/// @param blend  0 == equal temperament (snapped to the nearest semitone), 1 ==
/// just intonation (an exact n:1 ratio with the fundamental).
/// @param stretchCents  inharmonicity, applied on top of the blend. Zero is
/// the plain harmonic series.
inline double semitoneOffset(int index0, double blend,
                             double stretchCents = 0.0) noexcept {
  const auto &h = harmonic(index0);

  return (double)h.etSemitones + blend * h.jiCents * 0.01 +
         inharmonicCents(index0, stretchCents) * 0.01;
}

/// The interval name for a pitch class, as shown in the table above.
inline const char *intervalName(int pitchClass) noexcept {
  static const char *names[12] = {
      "prime/octave", "minor second", "major second",  "minor third",
      "major third",  "fourth",       "tritone",       "fifth",
      "minor sixth",  "major sixth",  "minor seventh", "major seventh"};
  return names[(size_t)(((pitchClass % 12) + 12) % 12)];
}

} // namespace ovt
