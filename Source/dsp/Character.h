#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Harmonics.h"
#include "SineTable.h"

namespace ovt {

/// Which oscillator each partial is, as a circuit rather than as a waveform.
///
/// Every one of them is a sine oscillator. What differs is how it fails to be
/// one, which is the only thing that ever told two analogue oscillators apart:
/// a circuit that produces a mathematically perfect sine does not exist, and
/// the ways each design misses are what people mean when they call one warm
/// and another sterile.
///
/// The order is the order they are saved in, so a new one goes on the end.
///
/// Declared here rather than beside the parameter it comes from, because the
/// voice branches on it and the DSP core is not allowed to include JUCE.
enum class Character {
  Pure = 0, ///< the table as sampled. No circuit misses this cleanly.
  Bulb,     ///< a Wien bridge held steady by a lamp, which lags
  Rail,     ///< a phase-shift oscillator grown into its own supply rail
  Diode,    ///< a triangle bent into a sine by two diodes that do not match
  Valve,    ///< a triode leaning over on one side before the other
  Opamp,    ///< an amplifier that cannot move as fast as the note asks
  NumCharacters
};

/// What each one is called on the panel.
///
/// Named for the part that does it. Not for the circuit it sits in, which
/// nobody reaches for by name while they are playing, and not for what it does
/// to the wave, which is where three of these started: a lamp, a rail, a pair
/// of diodes, a triode and an amplifier are five things you could hold, and
/// the list reads as a parts bin rather than as a list of adjectives.
///
/// Pure is the one exception and has to be. There is no part that makes a
/// mathematically perfect sine, which is the whole point of it.
inline const char *characterName(Character c) {
  switch (c) {
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
/// Thirty-two units built to one spec do not distort by the same amount any
/// more than they sit at the same pitch, and this is that half of the rack. It
/// is a short list rather than a figure per channel on purpose: the drive is
/// baked into a table, so a value per channel would mean thirty-two tables per
/// character and a note touching thirty-two of them per sample, where the
/// whole reason a character costs nothing is that 512 oscillators read one
/// 16 kB table that stays in cache. Three keeps the idea and keeps the tables
/// countable.
///
/// The middle one is the nominal drive, so a third of the partials sound
/// exactly as they did before there were variants.
inline constexpr std::array<double, 3> kDriveVariants{0.85, 1.0, 1.15};

/// Which of those a unit with no opinion uses.
inline constexpr int kNominalDrive = 1;

/// How much of that spread each circuit takes.
///
/// One for the three that are a fixed waveshape, where the harmonics move
/// about in step with the drive. The rate limit is the exception and needs a
/// third of it: its harmonics do not grow steadily, they appear all at once as
/// the wave starts to be limited, so the same 15% either side runs from -41.7
/// dB to -20.5 on the third harmonic, which is not one circuit built twice. At
/// a third it spreads about as far as the others do.
inline double driveDepthFor(Character c) noexcept {
  return c == Character::Opamp ? 0.33 : 1.0;
}

/// What a unit's drive comes to for this circuit, as a multiple of nominal.
inline double driveFor(Character c, int variant) noexcept {
  const auto which = std::clamp(variant, 0, (int)kDriveVariants.size() - 1);

  return 1.0 + (kDriveVariants[(size_t)which] - 1.0) * driveDepthFor(c);
}

/// How far apart thirty-two units built to the same spec end up.
///
/// Not DRIFT, which wanders. This is the spread a rack has the moment it is
/// switched on and has again the next time, and it is what makes a rack sound
/// like a rack rather than like one oscillator copied thirty-two times: the
/// partials sit a little off the ratios they were asked for, so they beat
/// against each other and against the harmonics the character itself adds,
/// which a patch with every TUNE at zero otherwise has no reason to do.
///
/// The scale is what a tuned rack holds, not what the parts are made to. Raw
/// component tolerance is a percent or more, which is eighty cents, and a rack
/// eighty cents wide is a rack nobody has tuned. What is left after tuning is a
/// couple of cents, and the order between the characters below is which part of
/// each circuit sets its frequency and which sets its level.
///
/// The pitch figures are half what they first shipped at. Played rather than
/// measured: at three to six cents several patches read as detuned rather than
/// as a rack of units, which is a rack that wants tuning again. The level
/// spread was right at the first figure and has not moved.
struct UnitTolerance {
  float cents = 0.0f;    ///< the most one unit is out by, after tuning
  float decibels = 0.0f; ///< and the most its level is out by
};

/// What each circuit holds to.
inline UnitTolerance unitToleranceFor(Character c) noexcept {
  switch (c) {
  // The lamp holds the level and nothing else here does, which makes this the
  // steadiest of them by a long way in amplitude. What the lamp is, though, is
  // a filament inside the bridge it regulates, and a warm resistor is not the
  // resistor the frequency was set with.
  case Character::Bulb:
    return {1.5f, 0.1f};

  // Three RC stages set the frequency and their errors stack. The level is
  // wherever the amplifier runs out of rail, which is a different place in
  // every unit.
  case Character::Rail:
    return {2.5f, 0.5f};

  // The frequency comes from an integrator, which is the most accurate way to
  // set one here. The level comes from how well two diodes match, which is the
  // least accurate thing in any of these circuits.
  case Character::Diode:
    return {2.0f, 0.4f};

  // A heater in every unit and nothing regulating either end of it.
  case Character::Valve:
    return {3.0f, 0.35f};

  // An ordinary amplifier around an ordinary core.
  case Character::Opamp:
    return {2.0f, 0.3f};

  // Pure is the one that is not a circuit, so a rack of them has no spread at
  // all. That is also what keeps every patch written before any of this
  // existed sounding exactly as it did.
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return {};
}

/// One rack of thirty-two units per character, built once and then only read.
///
/// Drawn rather than measured, since there is no rack to measure, but drawn
/// the same way every time: a preset, a session and a bounce all get the same
/// rack, and two machines rendering the same project get the same one too.
class UnitSpread {
public:
  /// What thirty-two units of one character came out at.
  struct Rack {
    std::array<float, kNumHarmonics> cents{}; ///< added to the partial's pitch
    std::array<float, kNumHarmonics> gain{};  ///< linear, 1 being on spec
    /// Which of kDriveVariants this unit's circuit was built to, so no two
    /// neighbours distort by quite the same amount.
    std::array<int, kNumHarmonics> drive{};
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

      // The first unit is the one the rest were tuned against, so it is
      // exactly on spec in all three. Otherwise a note would land a few cents
      // off the key that asked for it and a patch would change level when the
      // character changed, neither of which is unit tolerance: they are the
      // whole rack being out, which is what tuning it is for.
      Xorshift rng((uint32_t)c + 1u);

      // A stream of its own rather than more of that one, so that which drive
      // a unit got cannot move where it sits. Sharing the stream would shift
      // every draw after the first and re-roll the pitch and level of all
      // thirty-two, which is a different rack rather than the same rack with
      // its circuits built to three drives.
      Xorshift driveRng((uint32_t)c + 0x9e3779b9u);

      for (int i = 1; i < kNumHarmonics; ++i) {
        rack.cents[(size_t)i] = rng.bipolar() * tolerance.cents;

        rack.gain[(size_t)i] =
            std::pow(10.0f, rng.bipolar() * tolerance.decibels / 20.0f);

        // Drawn flat across the three rather than clustered on the nominal,
        // since the point of them is that a run of channels is not one
        // circuit repeated.
        rack.drive[(size_t)i] =
            std::min((int)(driveRng.unipolar() * (float)kDriveVariants.size()),
                     (int)kDriveVariants.size() - 1);
      }
    }
  }

  std::array<Rack, (size_t)Character::NumCharacters> racks{};
};

/// The harmonics a character adds are real harmonics and alias like any other,
/// so a table is built several times over, each one stopping at a different
/// harmonic, and the voice picks the highest one that fits under Nyquist at
/// the pitch the partial is currently at.
///
/// Five is where the recipes below stop being worth carrying: past it the
/// levels are under -50 dB, which is below what the interpolation error of the
/// table itself amounts to.
inline constexpr int kMaxCharacterHarmonic = 5;

/// Tables per shape, one for each count of harmonics that fits.
inline constexpr int kCharacterBands = kMaxCharacterHarmonic - 1;

/// Where a full-scale sine first asks the amplifier to move faster than it
/// can, in Hz.
///
/// Slewing is the one imperfection here that is not a fixed waveshape. A rate
/// limit does nothing at all to a slow wave and turns a fast one into a
/// triangle, so the shape depends on the frequency, and a character built this
/// way needs a table per band of it rather than one table.
///
/// A kilohertz, which is the second number here scaled to the instrument
/// rather than taken from the part. A 741 slews at 0.5 V per microsecond, so
/// at ten volts peak it stops keeping up somewhere around 8 kHz, and a
/// partial up there has no room left under Nyquist for the odd harmonics
/// slewing makes: the character would be real, correct and completely
/// inaudible. A kilohertz is where it can be heard, and it is where the
/// keyboard tracking rolloff sits for a related reason, being about C6 and so
/// in the middle of where anyone plays.
///
/// Below it the partials are untouched. Above it they harden as they climb,
/// which is what playing a slew-limited oscillator up the keyboard does.
inline constexpr double kSlewCornerHz = 1000.0;

/// How far past the corner each slew table stands, as a multiple of it.
///
/// It stops at a little over twice the corner because that is where the wave
/// has become a triangle and stays one. Measured, the third harmonic reaches
/// -19.1 dB there and does not move again however much harder the limit
/// bites: a triangle at a given fundamental is a triangle. Anything above the
/// last ratio reads the last table.
inline constexpr std::array<double, 3> kSlewRatios{1.25, 1.6, 2.2};

/// One character, as the harmonic series its circuit produces.
///
/// Held as amplitudes and phases rather than as a shaping function, because a
/// waveshaper cannot be run on the audio thread at this scale and because
/// harmonics are what has to be counted to keep the thing under Nyquist. The
/// circuit is run once, at startup, and this is what it leaves behind.
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
/// About four hundred kilobytes of them, which sounds like a lot next to one
/// 16 kB sine until you count what it replaces: the alternative is a waveshaper
/// in the inner loop and four times oversampling to keep it from aliasing,
/// which is the whole engine again three times over.
class CharacterTables {
public:
  static const CharacterTables &instance() noexcept {
    static const CharacterTables t;
    return t;
  }

  /// The table to read for this character, at this pitch.
  ///
  /// @param freq  the partial's frequency right now, which decides how hard a
  /// slew limit bites. Every other character reads the same table whatever it
  /// is playing.
  /// @param highestHarmonic  the highest multiple of that frequency which
  /// still fits below Nyquist. One means there is only room for the
  /// fundamental, which is a plain sine whatever the character says.
  /// @param drive  which of kDriveVariants this unit was built to. See
  /// UnitSpread, which is where a partial's own belongs.
  const Wave &table(Character c, double freq, int highestHarmonic,
                    int drive = kNominalDrive) const noexcept {
    const auto which = (size_t)c;

    // A character with nothing above its fundamental, and a partial with no
    // room for one, are the same oscillator: the plain sine every other
    // partial is already reading, which is one table staying warm rather than
    // several taking turns.
    if (which >= (size_t)Character::NumCharacters || highestHarmonic < 2 ||
        !shaped[which])
      return SineTable::instance();

    const int shape = c == Character::Opamp ? slewBandFor(freq) : 0;

    if (shape < 0)
      return SineTable::instance();

    const auto band = std::min(highestHarmonic, kMaxCharacterHarmonic) - 2;

    return tables[(size_t)(base[which] + at(c, drive, shape) + band)];
  }

  /// What a character does to the harmonic series, for the tests to read back
  /// and for anyone wondering where the numbers came from.
  ///
  /// @param shapeBand  which of a character's shapes to report, for the one
  /// that has more than a single shape.
  /// @param drive  which of kDriveVariants, the nominal one by default, which
  /// is what every figure written down about these characters describes.
  const std::array<Harmonic, kMaxCharacterHarmonic> &
  harmonics(Character c, int shapeBand = 0,
            int drive = kNominalDrive) const noexcept {
    const auto shape = std::clamp(shapeBand, 0, shapeCount(c) - 1);

    return recipes[(size_t)(recipeBase[(size_t)c] +
                            at(c, drive, shape) / kCharacterBands)];
  }

  /// How many shapes a character has. One for everything that is a fixed
  /// waveform, and a band per slew ratio for the one that is not.
  static int shapeCount(Character c) noexcept {
    return c == Character::Opamp ? (int)kSlewRatios.size() : 1;
  }

private:
  /// Where one drive's shapes start, counted in tables from the character's
  /// own base. Drives outermost, then shapes, then the Nyquist bands, so a
  /// partial that crosses a band moves by one and the rest stays put.
  static int at(Character c, int drive, int shape) noexcept {
    const auto which = std::clamp(drive, 0, (int)kDriveVariants.size() - 1);

    return (which * shapeCount(c) + shape) * kCharacterBands;
  }

  CharacterTables() {
    int offset = 0;

    for (int c = 0; c < (int)Character::NumCharacters; ++c) {
      base[(size_t)c] = offset;
      recipeBase[(size_t)c] = (int)recipes.size();

      bool anything = false;

      // Drives outermost, so one drive's shapes and bands sit together and a
      // partial only ever moves between the bands of the drive it was built
      // to. Bulb and Pure make no harmonics at any drive, and fall out below
      // without a table to their name.
      for (int drive = 0; drive < (int)kDriveVariants.size(); ++drive)
        for (int shape = 0; shape < shapeCount((Character)c); ++shape) {
          const auto recipe = analyse(
              renderCycle((Character)c, shape, driveFor((Character)c, drive)));
          recipes.push_back(recipe);

          // Below this a harmonic is quieter than the table's own interpolation
          // error, so building a table for it would cost cache and change
          // nothing. Decided from the recipe rather than from a list of which
          // characters are supposed to be clean, so adding one cannot get the
          // answer wrong.
          constexpr float kAudible = 1.0e-4f;

          bool worthIt = false;

          for (int n = 2; n <= kMaxCharacterHarmonic; ++n)
            worthIt |= std::hypot(recipe[(size_t)(n - 1)].sine,
                                  recipe[(size_t)(n - 1)].cosine) > kAudible;

          anything |= worthIt;

          // Built for every shape once any of them is worth building, so the
          // bands stay at a fixed stride and a quiet one in the middle cannot
          // shift the rest out from under the index.
          build(recipe);
          offset += kCharacterBands;
        }

      shaped[(size_t)c] = anything;

      // Nothing was kept, so nothing was spent.
      if (!anything) {
        tables.resize((size_t)base[(size_t)c]);
        offset = base[(size_t)c];
      }
    }
  }

  /// One turn of what a circuit makes of a sine.
  ///
  /// A whole cycle rather than a point at a time, because a slew limit has a
  /// memory: what it does at one instant depends on where it had got to at the
  /// last one.
  static std::vector<double> renderCycle(Character c, int shape, double drive) {
    constexpr double kTwoPi = 6.283185307179586476;
    std::vector<double> out((size_t)kPoints, 0.0);

    const auto sine = [](int i) {
      return std::sin(kTwoPi * (double)i / (double)kPoints);
    };

    switch (c) {
    case Character::Rail: {
      // A phase-shift oscillator has no amplitude control in it. It grows
      // until the amplifier runs out of rail and the rail is what sets the
      // level, so what comes out is a sine leaning on a soft limit. The drive
      // is chosen for a third harmonic at -23 dB, which is audible as a
      // hardening of the tone rather than as distortion.
      //
      // How far into the rail this unit runs is the drive: one that clips a
      // little sooner hardens a little more.
      const double kDrive = 1.0 * drive;

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * sine(i)) / std::tanh(kDrive);

      return out;
    }

    case Character::Diode: {
      // A triangle bent into a sine by a pair of diodes. The shaper itself is
      // exact, and the imperfection is that the two halves are not: one diode
      // conducts a little sooner than the other, so the wave is not the same
      // shape above the axis as below it. That asymmetry is where the even
      // harmonics come from, and it is why this sounds different from the
      // rail even at a similar total distortion.
      // How badly the two are matched is this unit's drive. Nominal leaves
      // the quarter of a turn's difference the character was built around.
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
      // A triode. Its curve is not symmetrical about anything, so a wave
      // sitting on it leans over on one side before the other, and what that
      // asymmetry makes is a second harmonic. That is the whole of the sound
      // people call valve warmth: an octave above every partial, stronger
      // than anything else the circuit adds.
      //
      // The bias is where on the curve the wave sits. Further along it and the
      // second harmonic grows while the third falls away, so it sets which of
      // the two the character is about.
      const double kDrive = 1.1 * drive;
      constexpr double kBias = 0.25;

      const double rest = std::tanh(kDrive * kBias);

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * (sine(i) + kBias)) - rest;

      return out;
    }

    case Character::Opamp: {
      // An amplifier that cannot move faster than its slew rate. Below the
      // corner it is never asked to, and past it the wave loses first its
      // corners and then everything but its slopes, which is a triangle.
      //
      // Simulated rather than shaped, since a rate limit is the one
      // imperfection here with a memory. Several turns of it, so what comes
      // out is the steady state the circuit settles into rather than the first
      // cycle after it was switched on.
      // The drive here is how far past its own corner the amplifier is being
      // asked to go, which is the same thing as a unit that slews a little
      // slower than its neighbour.
      const double ratio = kSlewRatios[(size_t)std::clamp(
                               shape, 0, (int)kSlewRatios.size() - 1)] *
                           drive;

      // The most it can move between two points of the table. A full-scale
      // sine's steepest part climbs 2 pi in a turn, so at a ratio of one this
      // is exactly what the wave asks for and nothing is limited.
      const double step = kTwoPi / ((double)kPoints * ratio);
      double y = 0.0;

      for (int pass = 0; pass < 8; ++pass)
        for (int i = 0; i < kPoints; ++i) {
          y += std::clamp(sine(i) - y, -step, step);
          out[(size_t)i] = y;
        }

      return out;
    }

    case Character::Pure:
    case Character::Bulb:
    case Character::NumCharacters:
      break;
    }

    (void)drive;

    // Bulb is deliberately here. A Wien bridge is the cleanest sine any of
    // these circuits makes, because holding the amplitude steady is the whole
    // job of the lamp in it, and what the lamp costs is not harmonics but
    // time: it takes a moment to settle whenever anything changes. That is a
    // behaviour rather than a waveform, so it lives in the voice. See
    // Voice::render.
    for (int i = 0; i < kPoints; ++i)
      out[(size_t)i] = sine(i);

    return out;
  }

  /// Reads the harmonics off a cycle a circuit has been run over.
  ///
  /// The fundamental is normalised to the amplitude a plain sine would have,
  /// so a character changes what a partial sounds like and not how loud it is:
  /// the fader still means the same thing, and what the character adds sits on
  /// top of it. DC comes out in the analysis and is simply not built back in,
  /// which matters more here than it looks: 512 oscillators each carrying a
  /// small offset is headroom quietly disappearing.
  static std::array<Harmonic, kMaxCharacterHarmonic>
  analyse(const std::vector<double> &cycle) noexcept {
    constexpr double kTwoPi = 6.283185307179586476;

    std::array<Harmonic, kMaxCharacterHarmonic> out{};

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

  /// Builds every band of one shape and appends them in order.
  ///
  /// One harmonic at a time into a running sum, taking a copy after each,
  /// because each band is the band below it plus one more harmonic. Building
  /// them separately would be the same work four times over, and this runs at
  /// startup where something is waiting for it.
  void build(const std::array<Harmonic, kMaxCharacterHarmonic> &recipe) {
    constexpr double kTwoPi = 6.283185307179586476;

    std::vector<double> sum((size_t)kPoints, 0.0);

    for (int n = 1; n <= kMaxCharacterHarmonic; ++n) {
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

  std::vector<Wave> tables;
  std::vector<std::array<Harmonic, kMaxCharacterHarmonic>> recipes;

  /// Where each character's tables and recipes start, and whether it has any
  /// tables at all.
  std::array<int, (size_t)Character::NumCharacters> base{};
  std::array<int, (size_t)Character::NumCharacters> recipeBase{};
  std::array<bool, (size_t)Character::NumCharacters> shaped{};
};

/// The highest multiple of this frequency that still fits below Nyquist.
///
/// Worked out from the pitch the partial is at rather than from the one it was
/// tuned to, so a partial bent upwards drops its top harmonic on the way up
/// rather than folding it back down.
inline int highestHarmonicUnder(double freq, double sampleRate) noexcept {
  if (freq <= 0.0 || sampleRate <= 0.0)
    return kMaxCharacterHarmonic;

  // Against the same 0.49 the fundamental's own fade ends at rather than
  // against Nyquist itself, so a harmonic that lands exactly on it is dropped
  // rather than sampled twice a cycle and heard as whatever that comes to.
  return (int)(0.49 * sampleRate / freq);
}

} // namespace ovt
