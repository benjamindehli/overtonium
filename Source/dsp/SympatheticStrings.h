#pragma once

#include <array>

#include "KarplusString.h"
#include "Params.h"

namespace ovt {

/// Strings nobody plays, ringing along with what is, the way the strings of a
/// piano with its dampers up do, or the understrings of a Hardanger fiddle.
///
/// One string for each key of a piano, A0 to C8, each a KarplusString sized
/// for its own note. The engine decides which are tuned to what and whose
/// damper is up for each block, since only it knows which keys are held and
/// where Adaptive has put them. This feeds every string the summed channels,
/// before the bus drive, and mixes what rings back in with them, so the strings
/// are part of the instrument rather than a room behind it.
///
/// A string whose damper is down rings out over a short damper time and is
/// then left alone, so with the pedal up only the held keys' strings cost
/// anything.
class SympatheticStrings {
public:
  static constexpr int kStrings = 88;
  static constexpr int kLowestKey = 21; ///< A0

  /// Which strings sound and how, for one block. A string with no pitch is
  /// not in use, and is damped like one whose damper is down.
  struct Plan {
    std::array<double, kStrings> hz{};
    std::array<bool, kStrings> free{};

    /// Free only because the pedal is down, rather than because its key is
    /// held. Fed less, so the pedal blooms under a chord rather than turning
    /// it up. See kPedalSend.
    std::array<bool, kStrings> pedalled{};
  };

  /// The key a string belongs to, and back.
  static constexpr int keyOf(int string) noexcept {
    return string + kLowestKey;
  }
  static constexpr int stringOf(int key) noexcept { return key - kLowestKey; }

  /// Sizes every string for the host's rate. The only place that allocates.
  void prepare(double sampleRate) noexcept;

  /// Silences them all.
  void reset() noexcept;

  /// Whether anything is still ringing, so a bank switched off can be left to
  /// ring out before it is skipped.
  bool isRinging() const noexcept;

  /// Feeds the strings what is in `left` and `right`, and replaces it with
  /// them as far as MIX says.
  void process(float *left, float *right, int numSamples, const StringsParams &,
               const Plan &) noexcept;

  /// How long they ring on once nothing drives them, nought when they are off.
  static float tailSeconds(const StringsParams &p) noexcept {
    return p.mode == StringsMode::Off ? 0.0f : p.decaySeconds;
  }

  /// How long a string rings once its damper comes down: longest in the
  /// bass, where a felt on a heavy string cannot stop it at once, and
  /// shortest at the top, falling evenly in pitch between the two.
  static double damperSeconds(int string) noexcept;
  static constexpr double kDamperBassSeconds = 0.8;
  static constexpr double kDamperTrebleSeconds = 0.12;

  /// How much of the summed channels each string is fed, once its own
  /// resonance has been taken back off: a partial held at a string's pitch
  /// drives it to this share of the partial's own level, however long DECAY
  /// is, which is what keeps every string free under the pedal from running
  /// away. DECAY decides how long they take to build up and to ring out.
  static constexpr float kSend = 0.5f;

  /// What a string freed by the pedal is fed, as a share of what a held
  /// key's string is.
  static constexpr float kPedalSend = 0.25f;

  /// What the strings are turned up by on the way out, so MIX at 100 % lands
  /// near the level of the instrument it replaces. Chosen by measuring a
  /// chord held under the pedal on five presets at three decays: Piano comes
  /// out within a few decibels of the instrument, Series a few under it, and
  /// Octave, whose twelve tempered strings most partials miss, 4 to 16 under.
  static constexpr float kWetScale = 1.0f;

private:
  std::array<KarplusString, kStrings> strings;

  /// Whether each string is still sounding, so a damped, silent one can be
  /// skipped altogether.
  std::array<bool, kStrings> ringing{};
};

} // namespace ovt
