#pragma once

#include <algorithm>
#include <cmath>

namespace ovt {

/// Everything key velocity does to a partial.
///
/// Three destinations off one shape: the fader, through the VELOCITY row, and
/// the attack and the delay, through STRIKE. They are the same curve read in
/// different directions, which is what the panel already claims by giving both
/// rows the same bipolar knob and the same reading, and saying it once here is
/// what makes that true rather than a coincidence.
///
/// A header of its own rather than a corner of the voice that latches it,
/// because the panel needs the same arithmetic: a knob reading "+81 %" says
/// nothing a player can act on, and the popup under it turns that into the two
/// times it actually produces. Both have to agree to the millisecond, which
/// they can only do by asking the same functions. JUCE-free like the rest of
/// the core, so the UI including it costs nothing.

/// How much of a bipolar velocity amount a blow of this speed earns, 0 to 1.
///
/// The shape everything below is built on. Zero amount earns nothing whatever
/// the velocity, and the sign decides which end of the keyboard's travel the
/// full amount lands on: positive spends itself on soft notes, negative on hard
/// ones. The two halves are exact mirrors, so -50% at a given velocity earns
/// what +50% earns at the opposite velocity.
inline float velocityReach(float amount, float velocity) noexcept {
  const auto a = std::clamp(amount, -1.0f, 1.0f);
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return a >= 0.0f ? a * (1.0f - v) : -a * v;
}

/// What the VELOCITY row leaves of a partial's fader, 0 to 1.
///
/// The reach taken away rather than spent, which is the whole difference
/// between this row and STRIKE: one subtracts level as the blow softens, the
/// other adds time. At zero amount nothing is taken and every note is as loud
/// as the fader says, which is what a strip does before anybody touches the
/// row.
inline float velocityGain(float amount, float velocity) noexcept {
  return 1.0f - velocityReach(amount, velocity);
}

/// The longest attack the ATTACK row can be set to, in seconds.
///
/// Named here rather than only in the parameter layout because STRIKE stretches
/// the attack towards it and stops there. A blow can take the onset anywhere
/// the knob itself could have gone and no further, which is what keeps a very
/// short attack at a full strike amount from reaching a figure the control
/// could never have been set to. See struckAttack.
inline constexpr float kMaxAttackSeconds = 5.0f;

/// The shortest attack the ATTACK row offers as a time, in seconds.
///
/// Below it the row goes on, into where in its cycle the partial starts, with
/// no attack time at all: from a rising zero crossing at this end, the softest
/// onset there is, to the partial's own peak at the bottom of the knob, the
/// hardest. Below about 500 Hz a quarter of a period is longer than this
/// attack, so on most of the keyboard the onset is set by where the cycle
/// starts rather than by the envelope, and that is what this end of the knob
/// reaches. See onsetOctaves.
inline constexpr float kShortestAttack = 0.0002f;

/// How far below the shortest attack the row reaches, in octaves of attack
/// time. Two, so starting a partial on its peak is worth the same stretch of
/// knob, and the same stretch of STRIKE, as quartering an attack.
inline constexpr double kOnsetPhaseOctaves = 2.0;

/// How a partial begins: where in its cycle, in turns, and how long it then
/// takes to reach full level, in seconds. One of the two is always nought.
struct Onset {
  float seconds;
  float turns;
};

/// The ATTACK row as one scale, in octaves above the shortest attack.
///
/// A time is positive, and the phase region is negative down to
/// -kOnsetPhaseOctaves at a partial starting on its peak. The row's own value
/// is seconds above zero and a fraction of that region below it, -1 being the
/// peak, so a preset or a session that stored a time stores the same time.
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

/// How far the strike amount can move a time, in octaves.
///
/// Eight, which is a range of 256 to 1, and it has to be that wide because the
/// attacks worth stretching are short. A partial set to 2 ms attacks in 2 ms
/// under a hard blow and takes half a second under the softest, which is the
/// difference between a struck note and a swelled one. Four octaves reached
/// only 32 ms from the same setting, and 2 ms against 32 ms is two kinds of
/// snap rather than two kinds of note: to hear the onset genuinely soften you
/// had to set an attack long enough that the hard note was no longer punchy,
/// which is the wrong trade to have to make.
///
/// The delay takes the same figure in the other direction, so 400 ms of it
/// comes in to under two milliseconds under a hard blow, which is the same
/// thing said at the front: hit it hard and the partial is simply there.
inline constexpr float kStrikeOctaves = 8.0f;

/// What the strike amount does to a partial's attack time.
///
/// Velocity only ever lengthens the attack, never shortens it past what the
/// knob says, so the ATTACK row goes on meaning the fastest this partial gets
/// and turning the amount up cannot outrun it. A positive amount spends that
/// on the quiet end: a note at full velocity attacks exactly as set, and the
/// onset softens the lighter it is played. A negative amount is the mirror,
/// anchored at the quiet end instead.
///
/// Octaves rather than a straight multiply, because attack time is heard in
/// ratios: the step from 5 to 10 ms is the audible change that the step from
/// 2 to 2.005 s is not.
///
/// The figure this returns is a ratio and can be large. What it is allowed to
/// do to a real attack is bounded by struckAttack, which is where the two meet.
inline float strikeAttackScale(float amount, float velocity) noexcept {
  return std::exp2(kStrikeOctaves * velocityReach(amount, velocity));
}

/// A partial's attack once the blow has been folded in, in seconds.
///
/// Stopped at the top of the ATTACK row's own range rather than left to run.
/// Two hundred and fifty-six times a short attack is a long one and exactly
/// what the control is for, but the same ratio on an attack that was already
/// long is half a minute, which is not a setting anybody reached for and not
/// one the knob alone could have produced. So the blow can take the onset
/// anywhere the knob could have gone and no further.
///
/// The cost is that a full amount on an attack above about 20 ms gives the very
/// softest notes the same ceiling rather than a longer one each. That is the
/// right end to lose resolution at: they are already slower than anything the
/// patch was built around.
inline float struckAttack(float attack, float scale) noexcept {
  return std::min(attack * scale, kMaxAttackSeconds);
}

/// A partial's onset once the blow has been folded in.
///
/// STRIKE moves the onset up the same scale the knob does, so it reaches the
/// whole of the row: a partial set to start on its peak can have a light touch
/// start it nearer the zero crossing, or past it into a short attack. Within
/// the times this is struckAttack exactly, since an octave of scale is a
/// doubling of time, and it stops at the same ceiling.
inline Onset struckOnset(float attack, float scale) noexcept {
  return onsetAt(onsetOctaves(attack) +
                 std::log2((double)std::max(scale, 1.0e-6f)));
}

/// What the same amount does to the delay before that attack.
///
/// The other way round, and deliberately so. Velocity only ever pulls the
/// delay in, never pushes it out, so the DELAY row goes on meaning the latest
/// this partial ever arrives. The two then say the same thing about a hard
/// blow from both ends: it arrives sooner and it arrives faster, which is what
/// striking anything harder does, while a light touch lets the partial come in
/// late and open slowly.
///
/// Built from the same reach read from the opposite end of the travel, since a
/// positive amount has to spend itself on hard notes here where the attack
/// spends it on soft ones. It needs no ceiling of its own: shortening runs
/// towards nothing, and nothing is a delay the knob can already be set to.
inline float strikeDelayScale(float amount, float velocity) noexcept {
  const auto v = std::clamp(velocity, 0.0f, 1.0f);

  return std::exp2(-kStrikeOctaves * velocityReach(amount, 1.0f - v));
}

/// The two ends of a partial's onset across the velocity range, in seconds.
///
/// The whole wait from key-down to full level, which is the delay and the
/// attack together, worked out at both ends of the keyboard's travel and handed
/// back smallest first. One pair rather than two because that is the question
/// being asked: how soon is this partial all there, hit hard and hit softly.
///
/// Folding the delay in rather than reporting it separately also means a strip
/// with no delay, which is nearly all of them, reads as a plain attack range
/// instead of carrying a second figure that is always nought.
///
/// An end with no wait at all can still differ in where the partial starts,
/// so each end carries its phase as well, in turns, for a reading that says
/// how hard the edge is when there is no time to give.
struct StrikeRange {
  float quickest;
  float slowest;
  float quickestTurns = 0.0f;
  float slowestTurns = 0.0f;
};

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

  // Quicker is less time, and with no time at all, more of a turn: a partial
  // starting on its peak is all there sooner than one starting on a crossing.
  const bool hardFirst =
      hard.seconds < soft.seconds ||
      (!(soft.seconds < hard.seconds) && hard.turns >= soft.turns);
  const auto &q = hardFirst ? hard : soft;
  const auto &s = hardFirst ? soft : hard;

  return {q.seconds, s.seconds, q.turns, s.turns};
}

} // namespace ovt
