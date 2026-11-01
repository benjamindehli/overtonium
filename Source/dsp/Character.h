#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Harmonics.h"
#include "Measured.h"
#include "SineTable.h"

namespace ovt {

/// Which oscillator each partial is, as a circuit rather than as a waveform.
///
/// Every one is a sine oscillator, and they differ in how each falls short of
/// a perfect sine. Wheel and everything after it are measured from recordings.
/// See Measured.h and "Character" in DESIGN.md.
///
/// The index is saved, so a new one goes on the end. Declared here rather than
/// beside its parameter because the DSP core may not include JUCE.
enum class Character {
  Pure = 0, ///< the table as sampled. No circuit misses this cleanly.
  Bulb,     ///< a Wien bridge held steady by a lamp, which lags
  Rail,     ///< a phase-shift oscillator grown into its own supply rail
  Diode,    ///< a triangle bent into a sine by two diodes that do not match
  Valve,    ///< a triode leaning over on one side before the other
  Opamp,    ///< an amplifier that cannot move as fast as the note asks
  Wheel,    ///< a tonewheel organ's generator, a wheel for every pitch
  Fm,       ///< a frequency modulation synthesizer's operator, alone
  Pd,       ///< a phase distortion synthesizer's oscillator, undistorted
  Sid,      ///< a sine through a 6581's measured converter
  Vco,      ///< an analogue oscillator's sine, through a mixer that clips
  Combo,    ///< a transistor combo organ's filtered square, on its 8' lever
  NumCharacters
};

/// Whether a character comes from a recording instead of a circuit. A
/// recording is built at one drive where the circuits are built at three.
inline bool isMeasured(Character c) noexcept {
  return c == Character::Wheel || c == Character::Fm || c == Character::Pd ||
         c == Character::Sid || c == Character::Vco || c == Character::Combo;
}

/// What each one is called on the panel: named for the part that does it, or
/// for the method of synthesis for FM and PD.
inline const char *characterName(Character c) {
  switch (c) {
  case Character::Wheel:
    return "Wheel";
  case Character::Fm:
    return "FM";
  case Character::Pd:
    return "PD";
  case Character::Sid:
    return "6581";
  case Character::Vco:
    return "VCO";
  case Character::Combo:
    return "Combo";
  case Character::Bulb:
    return "Bulb";
  case Character::Rail:
    return "Rail";
  case Character::Diode:
    return "Diode";
  case Character::Valve:
    return "Valve";
  case Character::Opamp:
    return "Op-amp";

  // Listed rather than left to a default, so adding a character is a compiler
  // error here until it has been given a name.
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return "Pure";
}

/// How hard each unit's circuit is driven, as a multiple of the nominal.
///
/// Three variants rather than one per channel, since the drive is baked into a
/// table and one table shared by every oscillator is what keeps a character
/// cheap. The middle one is the nominal drive.
inline constexpr std::array<double, 3> kDriveVariants{0.85, 1.0, 1.15};

/// Which of those a unit with no opinion uses.
inline constexpr int kNominalDrive = 1;

/// How much of that spread each circuit takes. The rate limit takes a third,
/// since its harmonics appear all at once as the wave starts to be limited and
/// the full spread would sound like different circuits.
inline double driveDepthFor(Character c) noexcept {
  return c == Character::Opamp ? 0.33 : 1.0;
}

/// What a unit's drive comes to for this circuit, as a multiple of nominal.
inline double driveFor(Character c, int variant) noexcept {
  const auto which = std::clamp(variant, 0, (int)kDriveVariants.size() - 1);

  return 1.0 + (kDriveVariants[(size_t)which] - 1.0) * driveDepthFor(c);
}

/// How many of those a character is built at: one for a recording, which has
/// a single fixed level of distortion.
inline int driveCountFor(Character c) noexcept {
  return isMeasured(c) ? 1 : (int)kDriveVariants.size();
}

/// How far apart thirty-two units built to the same spec end up, after tuning.
///
/// Not DRIFT, which wanders. This is the fixed spread of a rack, which makes
/// the partials beat against each other and against the character's own
/// harmonics. See "Character" in DESIGN.md for the figures.
struct UnitTolerance {
  float cents = 0.0f;    ///< the most one unit is out by, after tuning
  float decibels = 0.0f; ///< and the most its level is out by
};

/// What each circuit holds to.
inline UnitTolerance unitToleranceFor(Character c) noexcept {
  switch (c) {
  // The lamp holds the level, and heats the bridge it regulates.
  case Character::Bulb:
    return {1.5f, 0.1f};

  // Three RC stages set the frequency and their errors stack. The rail sets
  // the level.
  case Character::Rail:
    return {2.5f, 0.5f};

  // An accurate integrator sets the frequency, two mismatched diodes the level.
  case Character::Diode:
    return {2.0f, 0.4f};

  // A heater in every unit and nothing regulating either end of it.
  case Character::Valve:
    return {3.0f, 0.35f};

  // An ordinary amplifier around an ordinary core.
  case Character::Opamp:
    return {2.0f, 0.3f};

  // Every wheel is geared to one motor, so no pitch spread. About half the
  // recorded level spread is used, since part of it is the keyboard wiring.
  case Character::Wheel:
    return {0.0f, 1.0f};

  // A clock and digital levels, so every unit is identical.
  case Character::Fm:
  case Character::Pd:
  case Character::Sid:
    return {};

  // A divider organ makes every octave of a note from one oscillator, and its
  // levels follow a smooth curve, so there is nothing to spread.
  case Character::Combo:
    return {};

  // Only one was recorded, so it takes the op-amp's spread.
  case Character::Vco:
    return {2.0f, 0.3f};

  // Not a circuit, so no spread. Also keeps older patches as they were.
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return {};
}

/// One rack of thirty-two units per character, built once and then only read.
///
/// Drawn from fixed seeds, so every preset, session, bounce and machine gets
/// the same rack.
class UnitSpread {
public:
  /// What thirty-two units of one character came out at.
  struct Rack {
    std::array<float, kNumHarmonics> cents{}; ///< added to the partial's pitch
    std::array<float, kNumHarmonics> gain{};  ///< linear, 1 being on spec
    /// Which of kDriveVariants this unit's circuit was built to, so no two
    /// neighbours distort by quite the same amount.
    std::array<int, kNumHarmonics> drive{};

    /// How much each tonewheel moves as its shaft turns (zero on every other
    /// character): pitch in cents and level as a fraction once a turn, pitch in
    /// Hz and level twice a turn, and the phase of each level change against
    /// the pitch change, in turns. See Measured.h.
    std::array<float, kNumHarmonics> shaftCents{};
    std::array<float, kNumHarmonics> shaftLevel{};
    std::array<float, kNumHarmonics> twiceHz{};
    std::array<float, kNumHarmonics> twiceLevel{};
    std::array<float, kNumHarmonics> shaftLevelTurns{};
    std::array<float, kNumHarmonics> twiceTurns{};
  };

  static const UnitSpread &instance() noexcept {
    static const UnitSpread s;
    return s;
  }

  const Rack &rack(Character c) const noexcept {
    const auto which = (size_t)c;

    return racks[which < (size_t)Character::NumCharacters ? which : 0];
  }

private:
  UnitSpread() noexcept {
    for (int c = 0; c < (int)Character::NumCharacters; ++c) {
      const auto tolerance = unitToleranceFor((Character)c);
      auto &rack = racks[(size_t)c];

      rack.gain.fill(1.0f);
      rack.drive.fill(kNominalDrive);

      // The first unit is left exactly on spec, so a note lands on its key and
      // changing character does not change the level.
      Xorshift rng((uint32_t)c + 1u);

      // Separate streams, so a unit's drive cannot move its pitch and level.
      Xorshift driveRng((uint32_t)c + 0x9e3779b9u);

      for (int i = 1; i < kNumHarmonics; ++i) {
        rack.cents[(size_t)i] = rng.bipolar() * tolerance.cents;

        rack.gain[(size_t)i] =
            std::pow(10.0f, rng.bipolar() * tolerance.decibels / 20.0f);

        // Flat across the three rather than clustered on the nominal.
        rack.drive[(size_t)i] =
            std::min((int)(driveRng.unipolar() * (float)kDriveVariants.size()),
                     (int)kDriveVariants.size() - 1);
      }

      // The wheels' wobble, on its own stream. Every unit gets it, the first
      // included, since it is how a wheel turns rather than how it was tuned.
      if ((Character)c == Character::Wheel) {
        Xorshift wobbleRng((uint32_t)c + 0x632be5abu);

        const auto between = [&wobbleRng](const std::array<double, 2> &range) {
          return range[0] *
                 std::pow(range[1] / range[0], (double)wobbleRng.unipolar());
        };

        const auto fraction = [](double db) {
          return (float)(std::pow(10.0, db / 20.0) - 1.0);
        };

        for (size_t i = 0; i < (size_t)kNumHarmonics; ++i) {
          rack.shaftCents[i] = (float)between(kWheelShaftCents);
          rack.shaftLevel[i] = fraction(between(kWheelShaftDb));
          rack.twiceHz[i] = (float)between(kWheelTwiceHz);
          rack.twiceLevel[i] = fraction(between(kWheelTwiceDb));
          rack.shaftLevelTurns[i] = wobbleRng.unipolar();
          rack.twiceTurns[i] = wobbleRng.unipolar();
        }
      }
    }
  }

  std::array<Rack, (size_t)Character::NumCharacters> racks{};
};

/// The most harmonics a circuit character carries. Each table is built once
/// per harmonic count, and the voice reads the highest that fits under Nyquist
/// at the partial's pitch. Past the fifth the circuits are below -50 dB.
inline constexpr int kMaxCharacterHarmonic = 5;

/// The most any character carries, and the size every recipe is held at. The
/// 6581 needs all of it for its odd-harmonic tail.
inline constexpr int kMostCharacterHarmonics = 32;

/// How many harmonics this character carries: five for the circuits and
/// Wheel, kMeasuredHarmonics for the other recordings, whose upper harmonics
/// fall away slowly, and all of them for the 6581.
inline int harmonicsFor(Character c) noexcept {
  if (c == Character::Sid)
    return kMostCharacterHarmonics;

  return c == Character::Fm || c == Character::Pd || c == Character::Vco ||
                 c == Character::Combo
             ? kMeasuredHarmonics
             : kMaxCharacterHarmonic;
}

/// Tables per shape, one for each count of harmonics that fits.
inline int bandsFor(Character c) noexcept { return harmonicsFor(c) - 1; }

/// Where a full-scale sine first asks the amplifier to move faster than it
/// can, in Hz.
///
/// Slewing depends on frequency, so Op-amp needs a table per band of it. Set
/// to a kilohertz rather than a real part's figure so the effect falls where
/// its harmonics still fit under Nyquist and can be heard.
inline constexpr double kSlewCornerHz = 1000.0;

/// How far past the corner each slew table stands, as a multiple of it. At the
/// last the wave has become a triangle and stays one, so anything above reads
/// the last table.
inline constexpr std::array<double, 3> kSlewRatios{1.25, 1.6, 2.2};

/// One harmonic of a character's recipe. Characters are held as harmonics
/// rather than as a shaping function so they can be kept under Nyquist and
/// cost nothing on the audio thread.
struct Harmonic {
  float sine = 0.0f;   ///< amplitude of sin(2 pi n x)
  float cosine = 0.0f; ///< and of cos(2 pi n x), which carries the phase
};

/// Which slew table a partial at this frequency reads, or -1 for none of them.
inline int slewBandFor(double freq) noexcept {
  const double ratio = freq / kSlewCornerHz;

  // Nothing is being limited yet, so it is a plain sine.
  if (ratio <= kSlewRatios.front())
    return -1;

  for (int i = 1; i < (int)kSlewRatios.size(); ++i)
    if (ratio < kSlewRatios[(size_t)i])
      return i - 1;

  return (int)kSlewRatios.size() - 1;
}

/// Every table an oscillator can read, built once and then only read.
///
/// About 1.9 MB in all, in place of a waveshaper with oversampling in the
/// inner loop. A patch only reads its own character's tables.
class CharacterTables {
public:
  static const CharacterTables &instance() noexcept {
    static const CharacterTables t;
    return t;
  }

  /// The table to read for this character, at this pitch.
  ///
  /// @param freq  the partial's current frequency, which picks the slew band
  /// for Op-amp and the band for Wheel and Combo.
  /// @param highestHarmonic  the highest multiple of that frequency which
  /// still fits below Nyquist. Below two it is a plain sine.
  /// @param drive  which of kDriveVariants this unit was built to. See
  /// UnitSpread.
  const Wave &table(Character c, double freq, int highestHarmonic,
                    int drive = kNominalDrive) const noexcept {
    const auto which = (size_t)c;

    // No harmonics, or no room for them: read the shared plain sine.
    if (which >= (size_t)Character::NumCharacters || highestHarmonic < 2 ||
        !shaped[which])
      return SineTable::instance();

    const int shape = c == Character::Opamp   ? slewBandFor(freq)
                      : c == Character::Wheel ? wheelBandFor(freq)
                      : c == Character::Combo ? comboBandFor(freq)
                                              : 0;

    if (shape < 0)
      return SineTable::instance();

    const auto band = std::min(highestHarmonic, harmonicsFor(c)) - 2;

    return tables[(size_t)(base[which] + at(c, drive, shape) + band)];
  }

  /// A character's recipe, for the tests.
  ///
  /// @param shapeBand  which of a character's shapes to report.
  /// @param drive  which of kDriveVariants, the nominal one by default.
  const std::array<Harmonic, kMostCharacterHarmonics> &
  harmonics(Character c, int shapeBand = 0,
            int drive = kNominalDrive) const noexcept {
    const auto shape = std::clamp(shapeBand, 0, shapeCount(c) - 1);

    return recipes[(size_t)(recipeBase[(size_t)c] +
                            at(c, drive, shape) / bandsFor(c))];
  }

  /// How many shapes a character has: one per slew ratio for Op-amp, one per
  /// band of recorded wheels or keys for Wheel and Combo, otherwise one.
  static int shapeCount(Character c) noexcept {
    if (c == Character::Opamp)
      return (int)kSlewRatios.size();

    if (c == Character::Wheel)
      return (int)kWheelRecipes.size();

    if (c == Character::Combo)
      return (int)kComboRecipes.size();

    return 1;
  }

private:
  /// Where one drive's shapes start, counted in tables from the character's
  /// own base. Drives outermost, then shapes, then the Nyquist bands.
  static int at(Character c, int drive, int shape) noexcept {
    const auto which = std::clamp(drive, 0, driveCountFor(c) - 1);

    return (which * shapeCount(c) + shape) * bandsFor(c);
  }

  CharacterTables() {
    int offset = 0;

    for (int c = 0; c < (int)Character::NumCharacters; ++c) {
      base[(size_t)c] = offset;
      recipeBase[(size_t)c] = (int)recipes.size();

      bool anything = false;

      // Bulb and Pure make no harmonics at any drive and end up with no table.
      for (int drive = 0; drive < driveCountFor((Character)c); ++drive)
        for (int shape = 0; shape < shapeCount((Character)c); ++shape) {
          const auto recipe =
              isMeasured((Character)c)
                  ? measured((Character)c, shape)
                  : analyse(renderCycle((Character)c, shape,
                                        driveFor((Character)c, drive)));
          recipes.push_back(recipe);

          // Below this a harmonic is quieter than the table's own
          // interpolation error, so it is not worth a table.
          constexpr float kAudible = 1.0e-4f;

          bool worthIt = false;

          for (int n = 2; n <= harmonicsFor((Character)c); ++n)
            worthIt |= std::hypot(recipe[(size_t)(n - 1)].sine,
                                  recipe[(size_t)(n - 1)].cosine) > kAudible;

          anything |= worthIt;

          // Built for every shape once any is worth it, so the bands keep a
          // fixed stride.
          build(recipe, harmonicsFor((Character)c));
          offset += bandsFor((Character)c);
        }

      shaped[(size_t)c] = anything;

      // Nothing was kept, so nothing was spent.
      if (!anything) {
        tables.resize((size_t)base[(size_t)c]);
        offset = base[(size_t)c];
      }
    }
  }

  /// The measured harmonics of a recorded character.
  static std::array<Harmonic, kMostCharacterHarmonics> measured(Character c,
                                                                int shape) {
    if (c == Character::Sid)
      return sid();

    const auto &from =
        c == Character::Wheel   ? kWheelRecipes[(size_t)std::clamp(
                                      shape, 0, (int)kWheelRecipes.size() - 1)]
        : c == Character::Combo ? kComboRecipes[(size_t)std::clamp(
                                      shape, 0, (int)kComboRecipes.size() - 1)]
        : c == Character::Fm    ? kFmRecipe
        : c == Character::Vco   ? kVcoRecipe
                                : kPdRecipe;

    // The by-ear lift on the synthesizers' harmonics. The fundamental stays at
    // one, so it changes the tone and not the level.
    const auto lift =
        c == Character::Wheel || c == Character::Vco || c == Character::Combo
            ? 1.0f
            : (float)std::pow(
                  10.0, (c == Character::Fm ? kFmByEar : kPdByEar).harmonicsDb /
                            20.0);

    std::array<Harmonic, kMostCharacterHarmonics> out{};

    for (size_t n = 0; n < from.size(); ++n) {
      const auto gain = n == 0 ? 1.0f : lift;
      out[n] = {from[n][0] * gain, from[n][1] * gain};
    }

    // PD's second harmonic comes from its mix, so the bus makes it rather
    // than the table. See kPdSummingH2Db.
    if (c == Character::Pd)
      out[1] = {};

    return out;
  }

  /// What the 6581 makes of a sine, from its measured converter.
  ///
  /// A full-scale sine is rounded to the chip's 12-bit codes, each code turned
  /// into the level its bit weights give, and the bow applied. Worked out over
  /// many more points than a table holds so the converter's steps do not alias
  /// into the kept harmonics. Fundamental normalised to one, no DC.
  static std::array<Harmonic, kMostCharacterHarmonics> sid() {
    constexpr int kFine = 1 << 16;

    std::vector<double> level(4096);

    for (int code = 0; code < 4096; ++code) {
      double sum = 0.0;

      for (int b = 0; b < 12; ++b)
        sum += ((code >> b) & 1) * kSidBitWeights[(size_t)b];

      // From the swing of 0 to 1 the weights add up to, to -1 to 1.
      level[(size_t)code] = 2.0 * sum - 1.0;
    }

    std::vector<double> turn(kFine), cycle(kFine);

    for (int i = 0; i < kFine; ++i)
      turn[(size_t)i] = std::sin(kTwoPi * (double)i / (double)kFine);

    for (int i = 0; i < kFine; ++i) {
      const auto code = (int)std::lround(2047.5 + 2047.5 * turn[(size_t)i]);
      const auto x = level[(size_t)std::clamp(code, 0, 4095)];
      // Only the part of the bow inside each voice. The part on the mix is
      // the bus's, so it reaches every note at once. See kSidMixQuadratic.
      cycle[(size_t)i] = x - (kSidQuadratic - kSidMixQuadratic) * x * x;
    }

    std::array<Harmonic, kMostCharacterHarmonics> out{};

    for (int n = 1; n <= kMostCharacterHarmonics; ++n) {
      double sn = 0.0, cs = 0.0;

      for (int i = 0; i < kFine; ++i) {
        const auto at = (size_t)(((int64_t)n * i) % kFine);
        sn += cycle[(size_t)i] * turn[at];
        cs += cycle[(size_t)i] * turn[(at + kFine / 4) % kFine];
      }

      out[(size_t)(n - 1)] = {(float)(2.0 * sn / kFine),
                              (float)(2.0 * cs / kFine)};
    }

    const double first = std::hypot((double)out[0].sine, (double)out[0].cosine);

    for (auto &h : out)
      h = {(float)((double)h.sine / first), (float)((double)h.cosine / first)};

    return out;
  }

  /// One turn of what a circuit makes of a sine. A whole cycle at once, since
  /// a slew limit has a memory.
  static std::vector<double> renderCycle(Character c, int shape, double drive) {
    std::vector<double> out((size_t)kPoints, 0.0);

    const auto sine = [](int i) {
      return std::sin(kTwoPi * (double)i / (double)kPoints);
    };

    switch (c) {
    case Character::Rail: {
      // A phase-shift oscillator grows until the amplifier runs out of rail,
      // so it is a sine on a soft limit. Tuned for a third harmonic at -23 dB.
      // The drive is how far into the rail this unit runs.
      const double kDrive = 1.0 * drive;

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * sine(i)) / std::tanh(kDrive);

      return out;
    }

    case Character::Diode: {
      // A triangle bent into a sine by a pair of diodes, one conducting sooner
      // than the other. The asymmetry makes even harmonics. The drive is how
      // badly the two are matched.
      const double kMismatch = 1.0 - 0.25 * drive;

      for (int i = 0; i < kPoints; ++i) {
        const double turns = (double)i / (double)kPoints;
        const double tri = 1.0 - 4.0 * std::abs(turns - 0.5);
        const double gain = tri >= 0.0 ? 1.0 : kMismatch;

        out[(size_t)i] = std::sin(1.5707963267948966 * gain * tri);
      }

      return out;
    }

    case Character::Valve: {
      // A triode: the asymmetric curve makes a second harmonic. The bias is
      // where on the curve the wave sits, trading the second against the
      // third.
      const double kDrive = 1.1 * drive;
      constexpr double kBias = 0.25;

      const double rest = std::tanh(kDrive * kBias);

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * (sine(i) + kBias)) - rest;

      return out;
    }

    case Character::Opamp: {
      // An amplifier with a slew rate, which past the corner turns the wave
      // towards a triangle. Simulated for several turns to reach the steady
      // state. The drive is how far past its corner this unit is asked to go.
      const double ratio = kSlewRatios[(size_t)std::clamp(
                               shape, 0, (int)kSlewRatios.size() - 1)] *
                           drive;

      // The most it can move between two table points. At a ratio of one this
      // equals a full-scale sine's steepest slope.
      const double step = kTwoPi / ((double)kPoints * ratio);
      double y = 0.0;

      for (int pass = 0; pass < 8; ++pass)
        for (int i = 0; i < kPoints; ++i) {
          y += std::clamp(sine(i) - y, -step, step);
          out[(size_t)i] = y;
        }

      return out;
    }

    // Recorded characters are not simulated. See measured().
    case Character::Wheel:
    case Character::Fm:
    case Character::Pd:
    case Character::Sid:
    case Character::Vco:
    case Character::Combo:
    case Character::Pure:
    case Character::Bulb:
    case Character::NumCharacters:
      break;
    }

    (void)drive;

    // Bulb is a clean sine here. Its lamp lag is behaviour rather than a
    // waveform and lives in the voice. See Voice::render.
    for (int i = 0; i < kPoints; ++i)
      out[(size_t)i] = sine(i);

    return out;
  }

  /// Reads the harmonics off a cycle a circuit has been run over.
  ///
  /// The fundamental is normalised to a plain sine's amplitude, so a character
  /// does not change the level. DC is dropped, since 512 oscillators each
  /// carrying an offset would eat headroom.
  static std::array<Harmonic, kMostCharacterHarmonics>
  analyse(const std::vector<double> &cycle) noexcept {
    std::array<Harmonic, kMostCharacterHarmonics> out{};

    for (int n = 1; n <= kMaxCharacterHarmonic; ++n) {
      double sn = 0.0, cs = 0.0;

      for (int i = 0; i < kPoints; ++i) {
        const double turns = (double)i / (double)kPoints;

        sn += cycle[(size_t)i] * std::sin(kTwoPi * (double)n * turns);
        cs += cycle[(size_t)i] * std::cos(kTwoPi * (double)n * turns);
      }

      out[(size_t)(n - 1)].sine = (float)(2.0 * sn / (double)kPoints);
      out[(size_t)(n - 1)].cosine = (float)(2.0 * cs / (double)kPoints);
    }

    const double first = std::hypot((double)out[0].sine, (double)out[0].cosine);
    const double scale = first > 1.0e-9 ? 1.0 / first : 1.0;

    for (auto &h : out) {
      h.sine = (float)((double)h.sine * scale);
      h.cosine = (float)((double)h.cosine * scale);
    }

    return out;
  }

  /// Builds every band of one shape and appends them in order, adding one
  /// harmonic at a time to a running sum and taking a copy after each.
  void build(const std::array<Harmonic, kMostCharacterHarmonics> &recipe,
             int harmonics) {
    std::vector<double> sum((size_t)kPoints, 0.0);

    for (int n = 1; n <= harmonics; ++n) {
      const auto &h = recipe[(size_t)(n - 1)];

      for (int i = 0; i < kPoints; ++i) {
        const double turns = (double)i / (double)kPoints;

        sum[(size_t)i] +=
            (double)h.sine * std::sin(kTwoPi * (double)n * turns) +
            (double)h.cosine * std::cos(kTwoPi * (double)n * turns);
      }

      if (n < 2)
        continue;

      tables.emplace_back();
      tables.back().fill([&sum](double turns) {
        const auto i =
            (size_t)(turns * (double)kPoints + 0.5) % (size_t)kPoints;

        return sum[i];
      });
    }
  }

  /// One turn, at the resolution the tables themselves hold.
  static constexpr int kPoints = Wave::kSize;

  /// A class member rather than a function-local constexpr, because MSVC will
  /// not read a function-local constexpr inside a lambda without capturing it.
  static constexpr double kTwoPi = 6.283185307179586476;

  std::vector<Wave> tables;
  std::vector<std::array<Harmonic, kMostCharacterHarmonics>> recipes;

  /// Where each character's tables and recipes start, and whether it has any
  /// tables at all.
  std::array<int, (size_t)Character::NumCharacters> base{};
  std::array<int, (size_t)Character::NumCharacters> recipeBase{};
  std::array<bool, (size_t)Character::NumCharacters> shaped{};
};

/// The highest multiple of this frequency that still fits below Nyquist.
/// Taken from the current pitch, so a partial bent upwards drops its top
/// harmonic rather than folding it back.
inline int highestHarmonicUnder(double freq, double sampleRate) noexcept {
  if (freq <= 0.0 || sampleRate <= 0.0)
    return kMostCharacterHarmonics;

  // 0.49 rather than 0.5, matching the fundamental's own fade, so a harmonic
  // exactly on Nyquist is dropped.
  return (int)(0.49 * sampleRate / freq);
}

} // namespace ovt
