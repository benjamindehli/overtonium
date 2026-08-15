#pragma once

#include <algorithm>
#include <cmath>

namespace ovt {

/// Per-partial envelope: delay plus ADSR on the way in, and a two-stage key-off
/// on the way out.
///
/// Linear attack, exponential decay and release. The exponential segments are
/// what make additive tones read as "plucked" or "struck" rather than
/// synthetic, and they let each partial die away at its own rate, which is the
/// whole point of a 32-channel envelope section.
///
/// The delay stage holds before the attack begins, so partials can be made to
/// arrive at different times and the spectrum unfolds instead of appearing all
/// at once.
///
/// Letting go of the key does not simply fade from wherever the level sat. It
/// swells to a key-off level of its own first, and only then releases. Above
/// the sustain that is a release click or a bloom, the sound a damper or a
/// hammer return makes. Below it, it is the fast initial drop into a long tail
/// that a piano or a struck bell actually has. A key-off level of zero skips
/// the stage entirely, which is the default and is exactly what the envelope
/// did before it had one.
///
/// Times are "to within 1%": a decay time of 0.5 s means the level has covered
/// 99% of the distance to the sustain level after 0.5 s. The swell is the
/// exception and is exact, because the release has to start on schedule rather
/// than whenever an exponential happens to arrive.
class Envelope {
public:
  enum class Stage { Idle, Delay, Attack, Decay, Sustain, Swell, Release };

  void setSampleRate(double sr) noexcept {
    sampleRate = std::max(1.0, sr);
    dirty = true;
  }

  /// Cheap to call every block: coefficients are only recomputed when something
  /// moves.
  void configure(float delay, float a, float d, float s, float swell,
                 float offLvl, float r) noexcept {
    if (!dirty && delay == delayTime && a == attackTime && d == decayTime &&
        s == sustainLevel && swell == swellTime && offLvl == offLevel &&
        r == releaseTime)
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

    // Latched in samples at note-on, so turning the knob afterwards cannot
    // retime a note that is already waiting.
    delayRemaining = (int)(std::max(0.0f, delayTime) * sampleRate);
    stage = delayRemaining > 0 ? Stage::Delay : Stage::Attack;
  }

  void noteOff() noexcept {
    if (stage == Stage::Idle)
      return;

    if (offLevel <= kEpsilon) {
      stage = Stage::Release;
      return;
    }

    // Latched in samples, so the release starts when the knob says it does.
    // The exponential itself only covers 99% of the distance in that time, and
    // waiting for the rest of it would push the release out by more than twice
    // the swell.
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

  void reset() noexcept {
    stage = Stage::Idle;
    level = 0.0f;
    delayRemaining = 0;
    swellRemaining = 0;
    forcedRelease = false;
  }

  bool isActive() const noexcept { return stage != Stage::Idle; }
  Stage getStage() const noexcept { return stage; }
  float getLevel() const noexcept { return level; }

  /// True when the envelope is holding at silence and nothing but a key-off
  /// can change that.
  ///
  /// A partial with no sustain but a key-off level sits here for as long as
  /// the key is down, which on a held chord is most of the time. Saying so
  /// lets the caller skip the oscillator without dropping the note, which is
  /// the work the old early exit to Idle used to save by mistake.
  bool isSilentlyHolding() const noexcept {
    return stage == Stage::Sustain && level < kEpsilon;
  }

  inline float tick() noexcept {
    switch (stage) {
    case Stage::Idle:
      return 0.0f;

    case Stage::Delay:
      // Holds wherever the level already sat, which keeps a legato retrigger
      // from dropping out during the wait.
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

        // A decay that lands on a sustain of zero used to mean the note was
        // finished. That stopped being true when the key-off stage arrived: a
        // key-off level above zero still has something to say when the key is
        // let go, and going idle here threw the note away before it could.
        // Which is exactly the sound a music box or a thumb piano makes, so it
        // is the case that matters most.
        //
        // So it holds at silence and waits instead. Only when both levels are
        // zero is there genuinely nothing left to come.
        stage = (sustainLevel < kEpsilon && offLevel <= kEpsilon)
                    ? Stage::Idle
                    : Stage::Sustain;
      }
      break;

    case Stage::Sustain:
      level = sustainLevel;
      break;

    case Stage::Swell:
      level = offLevel + (level - offLevel) * swellCoef;

      if (--swellRemaining <= 0) {
        level = offLevel;
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
  float attackInc = 0.0f, decayCoef = 0.0f, swellCoef = 0.0f,
        releaseCoef = 0.0f;

  bool forcedRelease = false;
  float forcedReleaseCoef = 0.0f;
  bool dirty = true;
};

} // namespace ovt
