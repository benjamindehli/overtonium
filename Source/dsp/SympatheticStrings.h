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

  /// Whether anything is still ringing.
  bool isRinging() const noexcept;

  /// Whether it still has anything to do once switched off: strings ringing,
  /// the instrument's level still on its way back up to where it is with
  /// the resonator gone, or levels still to set after a reset.
  bool isBusy() const noexcept;

  /// Feeds the strings what is in `left` and `right`, and replaces it with
  /// them as far as MIX says.
  void process(float *left, float *right, int numSamples,
               const ResonatorParams &, const Plan &) noexcept;

  /// How long they ring on once nothing drives them, nought when they are off.
  static float tailSeconds(const ResonatorParams &p) noexcept {
    return p.mode == ResonatorMode::Off ? 0.0f : p.decaySeconds;
  }

  /// How long a string rings once its damper comes down: longest in the
  /// bass, where a felt on a heavy string cannot stop it at once, and
  /// shortest at the top, falling evenly in pitch between the two.
  static double damperSeconds(int string) noexcept;
  static constexpr double kDamperBassSeconds = 0.8;
  static constexpr double kDamperTrebleSeconds = 0.25;

  /// How loud a string gets against a partial held at its pitch, when DECAY
  /// is kCouplingSeconds.
  static constexpr float kSend = 0.5f;

  /// What a string freed by the pedal is fed, as a share of what a held
  /// key's string is. Small, so pressing the pedal changes a held chord by
  /// about 1.3 dB at any DECAY.
  static constexpr float kPedalSend = 0.08f;

  /// The DECAY at which a held partial drives its string to kSend of its own
  /// level. The feed follows the string's loss from there. See
  /// kLossExponent.
  static constexpr double kCouplingSeconds = 4.0;

  /// How closely a string's feed follows what it loses each pass. At 1 a
  /// held note would be as loud at any DECAY and a short note would get
  /// quieter as DECAY grows. At 0 the reverse. See process.
  static constexpr double kLossExponent = 0.75;

  /// How much the resonator is turned up on the way out, in decibels, so that
  /// on a held chord at the default DECAY it is about as loud as the
  /// instrument. Set per mode, since they ring very differently.
  static float makeUpDecibels(ResonatorMode) noexcept;

  /// How quickly the instrument's level and the resonator's follow MIX, the
  /// mode and switching it off, as a time constant. A level stepped at the
  /// start of a block is a click.
  static constexpr double kGainSeconds = 0.02;

private:
  std::array<KarplusString, kStrings> strings;

  /// Whether each string is still sounding, so a damped, silent one can be
  /// skipped altogether.
  std::array<bool, kStrings> ringing{};

  /// What each string was fed at the end of the last block, so a change in
  /// what it is fed can be moved across the next one.
  std::array<float, kStrings> feeding{};

  /// The level the instrument and the resonator are let through at, as they
  /// move towards what MIX and the mode ask for.
  float dryNow = 1.0f, wetNow = 0.0f;

  /// Set when everything has just been silenced, so the levels can go
  /// straight to where they are headed: with nothing sounding there is no
  /// step to hear. Not set when it stops itself after being switched off, so
  /// switching it on again still slides in.
  bool fresh = true;

  /// Stops every string, without touching the levels.
  void silence() noexcept;

  double rate = 48000.0;
};

} // namespace ovt
