#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// How the keyboard itself is tuned, as opposed to how each partial is (TUNE).
///
/// The index is stored by the host, so entries are only ever appended.
enum class Temperament {
  Equal = 0,
  Just,
  Pythagorean,
  QuarterComma,
  Werckmeister3,
  Young,
  /// Retuned as it is played: every pitch class has five versions a comma
  /// apart and each key takes the purest against what is sounding. The table
  /// below holds the middle versions. See SynthEngine::frequencyFor.
  Adaptive,
  /// After Adaptive to keep stored indices. The menus show them beside the
  /// other fixed temperaments.
  KirnbergerIII,
  Vallotti,
  NumTemperaments
};

/// The name the menus show.
inline const char *temperamentName(Temperament t) {
  switch (t) {
  case Temperament::Just:
    return "Just (major)";
  case Temperament::Pythagorean:
    return "Pythagorean";
  case Temperament::QuarterComma:
    return "Quarter-comma meantone";
  case Temperament::Werckmeister3:
    return "Werckmeister III";
  case Temperament::Young:
    return "Young";
  case Temperament::Adaptive:
    return "Adaptive";
  case Temperament::KirnbergerIII:
    return "Kirnberger III";
  case Temperament::Vallotti:
    return "Vallotti";

  // Listed rather than left to a default, so adding a temperament is a
  // compiler error here until it has been given a name.
  case Temperament::Equal:
  case Temperament::NumTemperaments:
    break;
  }

  return "Equal";
}

namespace tuning {

/// Cents in a pure fifth, 3:2.
inline constexpr double kPureFifth = 701.9550008653874;

/// The Pythagorean comma: what twelve pure fifths overshoot seven octaves by.
inline constexpr double kPythagoreanComma = 12.0 * kPureFifth - 8400.0;

/// Cents in a pure major third, 5:4.
inline constexpr double kPureMajorThird = 386.3137138648348;

/// The syntonic comma, between four pure fifths and a pure major third.
inline constexpr double kSyntonicComma =
    4.0 * kPureFifth - 2400.0 - kPureMajorThird;

/// The schisma: what eight pure fifths and a pure major third miss five octaves
/// by, about two cents.
inline constexpr double kSchisma = 8.0 * kPureFifth + kPureMajorThird - 6000.0;

/// Groven's fifth: narrowed by an eighth of a schisma, so eight of them and a
/// pure third close five octaves exactly. About a quarter of a cent from pure.
inline constexpr double kGrovenFifth = kPureFifth - kSchisma / 8.0;

/// The step between the Adaptive versions of a pitch class: four Groven
/// fifths up and a pure third down, about 20.5 cents.
inline constexpr double kAdaptiveComma =
    4.0 * kGrovenFifth - 2400.0 - kPureMajorThird;

/// The order pitch classes are reached by stacking fifths from the root.
inline constexpr int kFifthOrder[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};

/// Builds a temperament from the twelve fifths that close its circle.
///
/// The twelve must add up to seven octaves for the circle to close.
///
/// @param below  how many fifths below the root the chain starts, so step 0
///               of `fifths` is the one from that note. Decides where the wolf
///               falls in an uneven circle.
inline std::array<double, 12> fromFifths(const std::array<double, 12> &fifths,
                                         int below = 0) {
  std::array<double, 12> offsets{};

  // Start where equal temperament puts the first note, so every offset is a
  // small deviation and the fold below never splits the chain.
  double running = -700.0 * (double)below;

  for (int step = 0; step < 12; ++step) {
    const auto pitchClass = kFifthOrder[(step - below + 12) % 12];

    // Fold into one octave, then measure against where equal temperament
    // would have put it.
    auto cents = std::fmod(running, 1200.0);
    if (cents < 0.0)
      cents += 1200.0;

    offsets[(size_t)pitchClass] = cents - 100.0 * (double)pitchClass;

    // A deviation is the short way round, never the long way.
    if (offsets[(size_t)pitchClass] > 600.0)
      offsets[(size_t)pitchClass] -= 1200.0;
    if (offsets[(size_t)pitchClass] < -600.0)
      offsets[(size_t)pitchClass] += 1200.0;

    running += fifths[(size_t)step];
  }

  // Measured from the root, as every table is.
  const auto root = offsets[0];
  for (auto &cents : offsets)
    cents -= root;

  return offsets;
}

/// Eleven fifths of one size and a twelfth that mops up whatever is left, which
/// is the wolf.
inline std::array<double, 12> withWolf(double fifth) {
  std::array<double, 12> fifths{};
  fifths.fill(fifth);
  fifths[11] = 8400.0 - 11.0 * fifth;

  return fifths;
}

/// Where a temperament with a wolf starts its chain: three fifths below the
/// root, so on C the wolf is G#-Eb, the usual place for it.
inline constexpr int kWolfBelow = 3;

} // namespace tuning

/// Cent deviations from equal temperament, indexed by pitch class above the
/// temperament's own root.
inline const std::array<double, 12> &
temperamentOffsets(Temperament t) noexcept {
  using namespace tuning;

  // Built once. None of this depends on anything but the constants above.
  using Tables =
      std::array<std::array<double, 12>, (size_t)Temperament::NumTemperaments>;

  static const Tables tables = [] {
    Tables out{};

    // Equal: the thing everything else is measured against.
    out[0].fill(0.0);

    // Just major, as exact ratios rather than as a circle of fifths, because
    // it is not one: it is chosen interval by interval to be pure.
    const double ratios[12] = {1.0,       16.0 / 15.0, 9.0 / 8.0,   6.0 / 5.0,
                               5.0 / 4.0, 4.0 / 3.0,   45.0 / 32.0, 3.0 / 2.0,
                               8.0 / 5.0, 5.0 / 3.0,   9.0 / 5.0,   15.0 / 8.0};

    for (int i = 0; i < 12; ++i)
      out[1][(size_t)i] = 1200.0 * std::log2(ratios[i]) - 100.0 * (double)i;

    // Pythagorean: every fifth pure, so the twelfth takes the whole comma.
    out[2] = fromFifths(withWolf(kPureFifth), kWolfBelow);

    // Quarter-comma meantone: every fifth narrowed by a quarter of a syntonic
    // comma, which is exactly what makes the major thirds pure.
    out[3] =
        fromFifths(withWolf(kPureFifth - kSyntonicComma / 4.0), kWolfBelow);

    // Werckmeister III: four fifths narrowed by a quarter of the Pythagorean
    // comma, the rest pure, so the circle closes with no wolf. C-G, G-D, D-A
    // and B-F# are the tempered ones, which are steps 0, 1, 2 and 5.
    {
      std::array<double, 12> fifths{};
      fifths.fill(kPureFifth);

      for (int step : {0, 1, 2, 5})
        fifths[(size_t)step] = kPureFifth - kPythagoreanComma / 4.0;

      out[4] = fromFifths(fifths);
    }

    // Young: six consecutive fifths narrowed by a sixth of the comma each,
    // six pure. Gentler than Werckmeister and usable in every key.
    {
      std::array<double, 12> fifths{};
      fifths.fill(kPureFifth);

      for (int step = 0; step < 6; ++step)
        fifths[(size_t)step] = kPureFifth - kPythagoreanComma / 6.0;

      out[5] = fromFifths(fifths);
    }

    // Adaptive: just major built from Groven's fifths and pure thirds. With
    // the schisma tempered out, spellings that differ by it land on the same
    // pitch.
    {
      const int fifthsAndThirds[12][2] = {{0, 0},  {-1, -1}, {2, 0},  {1, -1},
                                          {0, 1},  {-1, 0},  {2, 1},  {1, 0},
                                          {0, -1}, {-1, 1},  {2, -1}, {1, 1}};

      for (int i = 0; i < 12; ++i) {
        const auto raw = fifthsAndThirds[i][0] * kGrovenFifth +
                         fifthsAndThirds[i][1] * kPureMajorThird;
        const auto folded =
            raw - 1200.0 * std::round((raw - 100.0 * (double)i) / 1200.0);
        out[6][(size_t)i] = folded - 100.0 * (double)i;
      }
    }

    // Kirnberger III: C-G, G-D, D-A and A-E narrowed by a quarter of the
    // syntonic comma, so C-E is pure as in meantone, and F#-C# narrowed by the
    // schisma, which is what is left of the Pythagorean comma. Steps 0 to 3
    // and 6. The rest are pure.
    {
      std::array<double, 12> fifths{};
      fifths.fill(kPureFifth);

      for (int step : {0, 1, 2, 3})
        fifths[(size_t)step] = kPureFifth - kSyntonicComma / 4.0;
      fifths[6] = kPureFifth - kSchisma;

      out[7] = fromFifths(fifths);
    }

    // Vallotti: Young's six narrowed fifths, starting a fifth lower, F-C
    // through E-B. That is step 11 and steps 0 to 4, so Vallotti on any root
    // is Young on the root a fifth below.
    {
      std::array<double, 12> fifths{};
      fifths.fill(kPureFifth);

      for (int step : {11, 0, 1, 2, 3, 4})
        fifths[(size_t)step] = kPureFifth - kPythagoreanComma / 6.0;

      out[8] = fromFifths(fifths);
    }

    return out;
  }();

  const auto index = (int)t;
  return tables[(size_t)(index >= 0 && index < (int)tables.size() ? index : 0)];
}

/// Frequency of a MIDI note.
///
/// @param root         pitch class the temperament is built on, 0 == C.
/// @param referenceHz  where A sits, in every temperament.
inline double noteFrequency(int midiNote, Temperament t, int root,
                            double referenceHz) noexcept {
  double cents = 0.0;

  if (t != Temperament::Equal) {
    const auto &offsets = temperamentOffsets(t);

    const auto wrap = [](int pc) { return ((pc % 12) + 12) % 12; };

    // A is MIDI 69, pitch class 9.
    cents = offsets[(size_t)wrap(midiNote - root)] -
            offsets[(size_t)wrap(9 - root)];
  }

  return referenceHz *
         std::exp2(((double)(midiNote - 69) + cents / 100.0) / 12.0);
}

} // namespace ovt
