#pragma once

#include <algorithm>
#include <array>
#include <cmath>

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
/// Declared here rather than beside the parameter it comes from, because the
/// voice branches on it and the DSP core is not allowed to include JUCE.
enum class Character {
  Pure = 0, ///< the table as sampled. No circuit misses this cleanly.
  Bulb,     ///< a Wien bridge held steady by a lamp, which lags
  Squashed, ///< a phase-shift oscillator driven into its own rails
  Folded,   ///< a triangle bent into a sine by two mismatched diodes
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

/// One character, as the harmonic series its circuit produces.
///
/// Held as amplitudes and phases rather than as a shaping function, because a
/// waveshaper cannot be run on the audio thread at this scale and because
/// harmonics are what has to be counted to keep the thing under Nyquist. The
/// shaper is run once, at startup, and this is what it leaves behind.
struct Harmonic {
  float sine = 0.0f;   ///< amplitude of sin(2 pi n x)
  float cosine = 0.0f; ///< and of cos(2 pi n x), which carries the phase
};

/// Every table an oscillator can read, built once and then only read.
///
/// Roughly 200 kB of tables, which sounds like a lot next to one 16 kB sine
/// until you count what it replaces: the alternative is a waveshaper in the
/// inner loop and four times oversampling to keep it from aliasing, which is
/// the whole engine again three times over.
class CharacterTables {
public:
  static const CharacterTables &instance() noexcept {
    static const CharacterTables t;
    return t;
  }

  /// The table to read for this character at this pitch.
  ///
  /// @param highestHarmonic  the highest multiple of the partial's own
  /// frequency that still fits below Nyquist. One means there is only room for
  /// the fundamental, which is a plain sine whatever the character says.
  const Wave &table(Character c, int highestHarmonic) const noexcept {
    const auto which = (size_t)c;

    // A character with nothing above its fundamental, and a partial with no
    // room for one, are the same oscillator: the plain sine every other
    // partial is already reading, which is one table staying warm rather than
    // several taking turns.
    if (which >= (size_t)Character::NumCharacters || highestHarmonic < 2 ||
        !shaped[which])
      return SineTable::instance();

    const auto band =
        (size_t)(std::min(highestHarmonic, kMaxCharacterHarmonic) - 2);

    return tables[which][band];
  }

  /// What a character does to the harmonic series, for the tests to read back
  /// and for anyone wondering where the numbers came from.
  const std::array<Harmonic, kMaxCharacterHarmonic> &
  harmonics(Character c) const noexcept {
    return recipes[(size_t)c];
  }

private:
  CharacterTables() noexcept {
    for (int c = 0; c < (int)Character::NumCharacters; ++c) {
      const auto &recipe = recipes[(size_t)c] = analyse((Character)c);

      // Below this a harmonic is quieter than the table's own interpolation
      // error, so building a table for it would cost cache and change
      // nothing. Decided from the recipe rather than from a list of which
      // characters are supposed to be clean, so adding one cannot get the
      // answer wrong.
      constexpr float kAudible = 1.0e-4f;

      for (int n = 2; n <= kMaxCharacterHarmonic; ++n)
        shaped[(size_t)c] |=
            std::hypot(recipe[(size_t)(n - 1)].sine,
                       recipe[(size_t)(n - 1)].cosine) > kAudible;

      if (!shaped[(size_t)c])
        continue;

      for (int highest = 2; highest <= kMaxCharacterHarmonic; ++highest)
        build(tables[(size_t)c][(size_t)(highest - 2)], recipe, highest);
    }
  }

  /// What each circuit does to a sine, applied to one turn of one.
  ///
  /// @param turns  where in the cycle we are, 0 to 1.
  static double shape(Character c, double turns) noexcept {
    constexpr double kTwoPi = 6.283185307179586476;
    const double s = std::sin(kTwoPi * turns);

    switch (c) {
    case Character::Squashed: {
      // A phase-shift oscillator has no amplitude control in it. It grows
      // until the amplifier runs out of rail and the rail is what sets the
      // level, so what comes out is a sine leaning on a soft limit. The drive
      // is chosen for a third harmonic at -23 dB, which is audible as a
      // hardening of the tone rather than as distortion.
      constexpr double kDrive = 1.0;
      return std::tanh(kDrive * s) / std::tanh(kDrive);
    }

    case Character::Folded: {
      // A triangle bent into a sine by a pair of diodes. The shaper itself is
      // exact, and the imperfection is that the two halves are not: one diode
      // conducts a little sooner than the other, so the wave is not the same
      // shape above the axis as below it. That asymmetry is where the even
      // harmonics come from, and it is why this sounds different from
      // Squashed even at a similar total distortion.
      const double tri = 1.0 - 4.0 * std::abs(turns - 0.5);
      constexpr double kMismatch = 0.75;
      const double gain = tri >= 0.0 ? 1.0 : kMismatch;

      return std::sin(1.5707963267948966 * gain * tri);
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
    return s;
  }

  /// Runs a circuit over one turn of a sine and reads off the harmonics it
  /// left behind.
  ///
  /// The fundamental is normalised to the amplitude a plain sine would have,
  /// so a character changes what a partial sounds like and not how loud it is:
  /// the fader still means the same thing, and what the character adds sits on
  /// top of it. DC comes out in the analysis and is simply not built back in,
  /// which matters more here than it looks: 512 oscillators each carrying a
  /// small offset is headroom quietly disappearing.
  static std::array<Harmonic, kMaxCharacterHarmonic>
  analyse(Character c) noexcept {
    constexpr double kTwoPi = 6.283185307179586476;
    constexpr int kPoints = 4096;

    std::array<Harmonic, kMaxCharacterHarmonic> out{};

    for (int n = 1; n <= kMaxCharacterHarmonic; ++n) {
      double sn = 0.0, cs = 0.0;

      for (int i = 0; i < kPoints; ++i) {
        const double turns = (double)i / (double)kPoints;
        const double v = shape(c, turns);

        sn += v * std::sin(kTwoPi * (double)n * turns);
        cs += v * std::cos(kTwoPi * (double)n * turns);
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

  static void build(Wave &wave,
                    const std::array<Harmonic, kMaxCharacterHarmonic> &recipe,
                    int highest) noexcept {
    wave.fill([&recipe, highest](double turns) {
      constexpr double kTwoPi = 6.283185307179586476;
      double v = 0.0;

      for (int n = 1; n <= highest; ++n) {
        const auto &h = recipe[(size_t)(n - 1)];
        v += (double)h.sine * std::sin(kTwoPi * (double)n * turns) +
             (double)h.cosine * std::cos(kTwoPi * (double)n * turns);
      }

      return v;
    });
  }

  std::array<std::array<Wave, kMaxCharacterHarmonic - 1>,
             (size_t)Character::NumCharacters>
      tables{};

  std::array<std::array<Harmonic, kMaxCharacterHarmonic>,
             (size_t)Character::NumCharacters>
      recipes{};

  /// Whether a character has anything above its fundamental worth a table.
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
