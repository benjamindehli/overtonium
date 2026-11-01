#pragma once

#include <algorithm>
#include <cmath>

namespace ovt {

/// Everything key velocity does to a partial: the fader through the VELOCITY
/// row, and the attack and delay through STRIKE, all from one curve.
///
/// Shared by the voice and the panel, whose popups show the resulting times
/// and must agree with the voice exactly. JUCE-free like the rest of the core.

/// How much of a bipolar velocity amount a blow of this speed earns, 0 to 1.
///
/// A positive amount lands in full on the softest notes, a negative one on the
/// hardest, and the two are exact mirrors.
inline float velocityReach(float amount, float velocity) noexcept {
  const auto a = std::clamp(amount, -1.0f, 1.0f);
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return a >= 0.0f ? a * (1.0f - v) : -a * v;
}

/// What the VELOCITY row leaves of a partial's fader, 0 to 1. Zero amount
/// leaves the fader as set.
inline float velocityGain(float amount, float velocity) noexcept {
  return 1.0f - velocityReach(amount, velocity);
}

/// The longest attack the ATTACK row can be set to, in seconds. STRIKE never
/// stretches an attack past it. See struckAttack.
inline constexpr float kMaxAttackSeconds = 5.0f;

/// The shortest attack the ATTACK row offers as a time, in seconds.
///
/// Below it the row sets where in its cycle the partial starts instead, from a
/// rising zero crossing here to the partial's peak at the bottom of the knob.
/// See onsetOctaves.
inline constexpr float kShortestAttack = 0.0002f;

/// How far below the shortest attack the row reaches, in octaves of attack
/// time, so starting on the peak is worth the same knob travel as quartering
/// an attack.
inline constexpr double kOnsetPhaseOctaves = 2.0;

/// How a partial begins: where in its cycle, in turns, and how long it then
/// takes to reach full level, in seconds. One of the two is always nought.
struct Onset {
  float seconds;
  float turns;
};

/// The ATTACK row as one scale, in octaves above the shortest attack.
///
/// Positive is a time, negative is the phase region down to
/// -kOnsetPhaseOctaves at the peak. The row stores seconds above zero and a
/// fraction of the phase region below it (-1 is the peak).
inline double onsetOctaves(float attack) noexcept {
  if (attack < 0.0f)
    return (double)std::max(attack, -1.0f) * kOnsetPhaseOctaves;

  return std::log2((double)std::max(attack, kShortestAttack) /
                   (double)kShortestAttack);
}

/// The onset at a place on that scale.
inline Onset onsetAt(double octaves) noexcept {
  if (octaves < 0.0)
    return {0.0f, (float)(0.25 * std::min(1.0, -octaves / kOnsetPhaseOctaves))};

  return {std::min((float)((double)kShortestAttack * std::exp2(octaves)),
                   kMaxAttackSeconds),
          0.0f};
}

/// How far the strike amount can move a time, in octaves (256 to 1). Wide
/// enough that a 2 ms attack can soften to half a second. The delay uses the
/// same figure in the other direction.
inline constexpr float kStrikeOctaves = 8.0f;

/// What the strike amount does to a partial's attack time, as a ratio.
///
/// Only ever lengthens the attack, so the ATTACK row stays the fastest the
/// partial gets. A positive amount lengthens soft notes, a negative one hard
/// notes. Scaled in octaves because attack time is heard in ratios. The result
/// is bounded by struckAttack.
inline float strikeAttackScale(float amount, float velocity) noexcept {
  return std::exp2(kStrikeOctaves * velocityReach(amount, velocity));
}

/// A partial's attack once the blow has been folded in, in seconds, capped at
/// the top of the ATTACK row's range.
inline float struckAttack(float attack, float scale) noexcept {
  return std::min(attack * scale, kMaxAttackSeconds);
}

/// A partial's onset once the blow has been folded in.
///
/// Moves along the same scale as the knob, so it can cross between the phase
/// region and attack times. Within the times it matches struckAttack.
inline Onset struckOnset(float attack, float scale) noexcept {
  return onsetAt(onsetOctaves(attack) +
                 std::log2((double)std::max(scale, 1.0e-6f)));
}

/// What the same amount does to the delay before that attack, as a ratio.
///
/// Only ever shortens the delay, so the DELAY row stays the latest the partial
/// arrives. A hard blow arrives sooner as well as faster. Reads the reach from
/// the opposite end of the travel to the attack.
inline float strikeDelayScale(float amount, float velocity) noexcept {
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return std::exp2(-kStrikeOctaves * velocityReach(amount, 1.0f - v));
}

/// The two ends of a partial's onset across the velocity range, smallest
/// first: delay plus attack in seconds, and the starting phase in turns for an
/// end with no wait at all.
struct StrikeRange {
  float quickest;
  float slowest;
  float quickestTurns = 0.0f;
  float slowestTurns = 0.0f;
};

/// The StrikeRange for a strip's strike amount, delay and attack.
inline StrikeRange strikeRange(float amount, float delay,
                               float attack) noexcept {
  struct End {
    float seconds, turns;
  };

  const auto end = [&](float velocity) {
    const auto onset = struckOnset(attack, strikeAttackScale(amount, velocity));
    return End{std::max(0.0f, delay) * strikeDelayScale(amount, velocity) +
                   onset.seconds,
               onset.turns};
  };

  const auto hard = end(1.0f);
  const auto soft = end(0.0f);

  // Quicker is less time, and at equal time, more of a turn.
  const bool hardFirst =
      hard.seconds < soft.seconds ||
      (!(soft.seconds < hard.seconds) && hard.turns >= soft.turns);
  const auto &q = hardFirst ? hard : soft;
  const auto &s = hardFirst ? soft : hard;

  return {q.seconds, s.seconds, q.turns, s.turns};
}

} // namespace ovt
