#pragma once

#include <algorithm>
#include <cmath>

#include "Exact.h"

namespace ovt {

/// Per-partial envelope: delay plus ADSR on the way in, and a two-stage key-off
/// on the way out.
///
/// Linear attack, exponential decay and release. The delay stage holds before
/// the attack begins.
///
/// On key-off the level first swells to a key-off level of its own, then
/// releases. A key-off level of zero skips the swell.
///
/// Times are "to within 1%": a decay of 0.5 s covers 99% of the distance to
/// the sustain level in 0.5 s. The swell is exact, so the release starts on
/// schedule.
class Envelope {
public:
  enum class Stage { Idle, Delay, Attack, Decay, Sustain, Swell, Release };

  void setSampleRate(double sr) noexcept {
    sampleRate = std::max(1.0, sr);
    dirty = true;
  }

  /// Cheap to call every block: coefficients are only recomputed on a change.
  void configure(float delay, float a, float d, float s, float swell,
                 float offLvl, float r) noexcept {
    if (!dirty && exactly(delay, delayTime) && exactly(a, attackTime) &&
        exactly(d, decayTime) && exactly(s, sustainLevel) &&
        exactly(swell, swellTime) && exactly(offLvl, offLevel) &&
        exactly(r, releaseTime))
      return;

    delayTime = delay;
    attackTime = a;
    decayTime = d;
    sustainLevel = std::clamp(s, 0.0f, 1.0f);
    swellTime = swell;
    offLevel = std::clamp(offLvl, 0.0f, 1.0f);
    releaseTime = r;
    dirty = false;

    attackInc =
        (float)(1.0 / (std::max(1.0e-4, (double)attackTime) * sampleRate));
    decayCoef = coefFor(decayTime);
    swellCoef = coefFor(swellTime);
    releaseCoef = coefFor(releaseTime);
  }

  /// @param resetLevel  true restarts from silence (percussive retrigger),
  /// false ramps from wherever the envelope currently sits (legato).
  void noteOn(bool resetLevel) noexcept {
    if (resetLevel)
      level = 0.0f;

    forcedRelease = false;

    // Latched at note-on, so a knob change cannot retime a waiting note.
    delayRemaining = (int)(std::max(0.0f, delayTime) * sampleRate);
    stage = delayRemaining > 0 ? Stage::Delay : Stage::Attack;
  }

  /// @param lift  scales the level the tail starts from, from the release
  /// velocity. One leaves the patch as written. See liftFromVelocity.
  ///
  /// It scales the starting level rather than the key-off level, since a
  /// key-off level of zero means "release from the current level". The result
  /// is clamped to one.
  void noteOff(float lift = 1.0f) noexcept {
    if (stage == Stage::Idle)
      return;

    const bool scaled = !exactly(lift, 1.0f);

    if (offLevel <= kEpsilon && !scaled) {
      stage = Stage::Release;
      return;
    }

    // Latched, so a knob change cannot move a key-off already under way.
    swellTarget = std::clamp((offLevel <= kEpsilon ? level : offLevel) * lift,
                             0.0f, 1.0f);

    // Counted in samples, so the release starts exactly when the swell time
    // says, not when the exponential arrives.
    swellRemaining = (int)(std::max(0.0f, swellTime) * sampleRate);
    stage = Stage::Swell;
  }

  /// Steals the voice with a short fade instead of a click.
  void forceRelease(float seconds) noexcept {
    if (stage == Stage::Idle)
      return;

    forcedReleaseCoef = coefFor(seconds);
    forcedRelease = true;
    stage = Stage::Release;
  }

  /// Silences at once, for a voice being cleared.
  void reset() noexcept {
    stage = Stage::Idle;
    level = 0.0f;
    delayRemaining = 0;
    swellRemaining = 0;
    swellTarget = 0.0f;
    forcedRelease = false;
  }

  bool isActive() const noexcept { return stage != Stage::Idle; }
  Stage getStage() const noexcept { return stage; }
  float getLevel() const noexcept { return level; }

  /// True when the envelope is holding at silence until a key-off, so the
  /// caller can skip the oscillator without dropping the note.
  bool isSilentlyHolding() const noexcept {
    return stage == Stage::Sustain && level < kEpsilon;
  }

  /// Advances one sample and returns the level, 0 to 1.
  inline float tick() noexcept {
    switch (stage) {
    case Stage::Idle:
      return 0.0f;

    case Stage::Delay:
      // Holds the current level, so a legato retrigger does not drop out.
      if (--delayRemaining <= 0)
        stage = Stage::Attack;
      break;

    case Stage::Attack:
      level += attackInc;
      if (level >= 1.0f) {
        level = 1.0f;
        stage =
            (sustainLevel >= 1.0f - kEpsilon) ? Stage::Sustain : Stage::Decay;
      }
      break;

    case Stage::Decay:
      level = sustainLevel + (level - sustainLevel) * decayCoef;
      if (level - sustainLevel < kEpsilon) {
        level = sustainLevel;

        // A zero sustain still waits for key-off if there is a key-off
        // level to swell to. Only both at zero ends the note here.
        stage = (sustainLevel < kEpsilon && offLevel <= kEpsilon)
                    ? Stage::Idle
                    : Stage::Sustain;
      }
      break;

    case Stage::Sustain:
      level = sustainLevel;
      break;

    case Stage::Swell:
      level = swellTarget + (level - swellTarget) * swellCoef;

      if (--swellRemaining <= 0) {
        level = swellTarget;
        stage = Stage::Release;
      }
      break;

    case Stage::Release:
      level *= forcedRelease ? forcedReleaseCoef : releaseCoef;
      if (level < kEpsilon) {
        level = 0.0f;
        stage = Stage::Idle;
      }
      break;
    }

    return level;
  }

private:
  static constexpr float kEpsilon = 1.0e-5f;

  /// Exponential coefficient that covers 99% of the remaining distance in
  /// `seconds`.
  float coefFor(float seconds) const noexcept {
    return (float)std::exp(-4.60517018598809 /
                           (std::max(1.0e-4, (double)seconds) * sampleRate));
  }

  double sampleRate = 44100.0;
  Stage stage = Stage::Idle;
  float level = 0.0f;

  float delayTime = -1.0f, attackTime = -1.0f, decayTime = -1.0f,
        sustainLevel = -1.0f, swellTime = -1.0f, offLevel = -1.0f,
        releaseTime = -1.0f;
  int delayRemaining = 0, swellRemaining = 0;

  /// Where the current key-off is heading. Separate from offLevel, which is
  /// rewritten from the knob every block.
  float swellTarget = 0.0f;

  float attackInc = 0.0f, decayCoef = 0.0f, swellCoef = 0.0f,
        releaseCoef = 0.0f;

  bool forcedRelease = false;
  float forcedReleaseCoef = 0.0f;
  bool dirty = true;
};

} // namespace ovt
