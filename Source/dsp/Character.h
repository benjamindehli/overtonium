#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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
  Squashed, ///< a phase-shift oscillator driven into its own rails
  Folded,   ///< a triangle bent into a sine by two mismatched diodes
  Valve,    ///< a triode leaning over on one side before the other
  Slewed,   ///< an amplifier that cannot move as fast as the note asks
  NumCharacters
};

/// What each one is called on the panel.
///
/// Named for what the circuit does to the wave rather than for the circuit,
/// since nobody reaches for a phase-shift oscillator by name while they are
/// playing.
inline const char *characterName(Character c) {
  switch (c) {
  case Character::Bulb:
    return "Bulb";
  case Character::Squashed:
    return "Squashed";
  case Character::Folded:
    return "Folded";
  case Character::Valve:
    return "Valve";
  case Character::Slewed:
    return "Slewed";

  // Listed rather than left to a default, so adding a character is a compiler
  // error here until it has been given a name.
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return "Pure";
}

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
  const Wave &table(Character c, double freq,
                    int highestHarmonic) const noexcept {
    const auto which = (size_t)c;

    // A character with nothing above its fundamental, and a partial with no
    // room for one, are the same oscillator: the plain sine every other
    // partial is already reading, which is one table staying warm rather than
    // several taking turns.
    if (which >= (size_t)Character::NumCharacters || highestHarmonic < 2 ||
        !shaped[which])
      return SineTable::instance();

    const int shape = c == Character::Slewed ? slewBandFor(freq) : 0;

    if (shape < 0)
      return SineTable::instance();

    const auto band = std::min(highestHarmonic, kMaxCharacterHarmonic) - 2 +
                      shape * kCharacterBands;

    return tables[(size_t)(base[which] + band)];
  }

  /// What a character does to the harmonic series, for the tests to read back
  /// and for anyone wondering where the numbers came from.
  ///
  /// @param shapeBand  which of a character's shapes to report, for the one
  /// that has more than a single shape.
  const std::array<Harmonic, kMaxCharacterHarmonic> &
  harmonics(Character c, int shapeBand = 0) const noexcept {
    return recipes[(size_t)(recipeBase[(size_t)c] +
                            std::clamp(shapeBand, 0, shapeCount(c) - 1))];
  }

  /// How many shapes a character has. One for everything that is a fixed
  /// waveform, and a band per slew ratio for the one that is not.
  static int shapeCount(Character c) noexcept {
    return c == Character::Slewed ? (int)kSlewRatios.size() : 1;
  }

private:
  CharacterTables() {
    int offset = 0;

    for (int c = 0; c < (int)Character::NumCharacters; ++c) {
      base[(size_t)c] = offset;
      recipeBase[(size_t)c] = (int)recipes.size();

      bool anything = false;

      for (int shape = 0; shape < shapeCount((Character)c); ++shape) {
        const auto recipe = analyse(renderCycle((Character)c, shape));
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
  static std::vector<double> renderCycle(Character c, int shape) {
    constexpr double kTwoPi = 6.283185307179586476;
    std::vector<double> out((size_t)kPoints, 0.0);

    const auto sine = [](int i) {
      return std::sin(kTwoPi * (double)i / (double)kPoints);
    };

    switch (c) {
    case Character::Squashed: {
      // A phase-shift oscillator has no amplitude control in it. It grows
      // until the amplifier runs out of rail and the rail is what sets the
      // level, so what comes out is a sine leaning on a soft limit. The drive
      // is chosen for a third harmonic at -23 dB, which is audible as a
      // hardening of the tone rather than as distortion.
      constexpr double kDrive = 1.0;

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * sine(i)) / std::tanh(kDrive);

      return out;
    }

    case Character::Folded: {
      // A triangle bent into a sine by a pair of diodes. The shaper itself is
      // exact, and the imperfection is that the two halves are not: one diode
      // conducts a little sooner than the other, so the wave is not the same
      // shape above the axis as below it. That asymmetry is where the even
      // harmonics come from, and it is why this sounds different from
      // Squashed even at a similar total distortion.
      constexpr double kMismatch = 0.75;

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
      constexpr double kDrive = 1.1;
      constexpr double kBias = 0.25;

      const double rest = std::tanh(kDrive * kBias);

      for (int i = 0; i < kPoints; ++i)
        out[(size_t)i] = std::tanh(kDrive * (sine(i) + kBias)) - rest;

      return out;
    }

    case Character::Slewed: {
      // An amplifier that cannot move faster than its slew rate. Below the
      // corner it is never asked to, and past it the wave loses first its
      // corners and then everything but its slopes, which is a triangle.
      //
      // Simulated rather than shaped, since a rate limit is the one
      // imperfection here with a memory. Several turns of it, so what comes
      // out is the steady state the circuit settles into rather than the first
      // cycle after it was switched on.
      const double ratio = kSlewRatios[(size_t)std::clamp(
          shape, 0, (int)kSlewRatios.size() - 1)];

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
