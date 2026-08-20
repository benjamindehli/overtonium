#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// How the keyboard itself is tuned, as opposed to how each partial is.
///
/// TUNE decides where a partial sits above the note you played. This decides
/// where that note sits, which until now was always twelve-tone equal
/// temperament. The two are worth having together: a stack of just-intonation
/// partials sounds different again over a keyboard that is not equal, and the
/// beating between them is most of what a temperament is for.
enum class Temperament {
  Equal = 0,
  Just,
  Pythagorean,
  QuarterComma,
  Werckmeister3,
  Young,
  NumTemperaments
};

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

/// The order pitch classes are reached by stacking fifths from the root.
inline constexpr int kFifthOrder[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};

/// Builds a temperament from the twelve fifths that close its circle.
///
/// Nearly every historical temperament is described this way: take the circle
/// of fifths and say how much each one is narrowed. Deriving the pitch classes
/// from that rather than copying a table of cents means the defining property
/// stays visible in the code, and it is checkable, since the twelve have to add
/// up to seven octaves exactly or the circle does not close.
inline std::array<double, 12> fromFifths(const std::array<double, 12> &fifths) {
  std::array<double, 12> offsets{};
  double running = 0.0;

  for (int step = 0; step < 12; ++step) {
    const auto pitchClass = kFifthOrder[step];

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

} // namespace tuning

/// Cent deviations from equal temperament, indexed by pitch class above the
/// temperament's own root.
inline const std::array<double, 12> &
temperamentOffsets(Temperament t) noexcept {
  using namespace tuning;

  // Built once. None of this depends on anything but the constants above.
  static const std::array<std::array<double, 12>, 6> tables = [] {
    std::array<std::array<double, 12>, 6> out{};

    // Equal: the thing everything else is measured against.
    out[0].fill(0.0);

    // Just major, as exact ratios rather than as a circle of fifths, because
    // it is not one: it is chosen interval by interval to be pure.
    const double ratios[12] = {1.0,      16.0 / 15.0, 9.0 / 8.0,  6.0 / 5.0,
                               5.0 / 4.0, 4.0 / 3.0,  45.0 / 32.0, 3.0 / 2.0,
                               8.0 / 5.0, 5.0 / 3.0,  9.0 / 5.0,  15.0 / 8.0};

    for (int i = 0; i < 12; ++i)
      out[1][(size_t)i] = 1200.0 * std::log2(ratios[i]) - 100.0 * (double)i;

    // Pythagorean: every fifth pure, so the twelfth takes the whole comma.
    out[2] = fromFifths(withWolf(kPureFifth));

    // Quarter-comma meantone: every fifth narrowed by a quarter of a syntonic
    // comma, which is exactly what makes the major thirds pure.
    out[3] = fromFifths(withWolf(kPureFifth - kSyntonicComma / 4.0));

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

    return out;
  }();

  const auto index = (int)t;
  return tables[(size_t)(index >= 0 && index < 6 ? index : 0)];
}

/// Frequency of a MIDI note.
///
/// @param root         pitch class the temperament is built on, 0 == C.
/// @param referenceHz  where A sits. It stays there in every temperament: the
///                     offsets are taken relative to A's own, which is what a
///                     tuner does when they tune A first and work outwards.
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
