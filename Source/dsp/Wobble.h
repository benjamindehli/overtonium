#pragma once

#include <vector>

#include "Drift.h"

namespace ovt {

/// A warped record under the whole instrument.
///
/// Pitch is bent by reading the output back through a delay line whose length
/// keeps moving, which is the same thing that happens when a platter runs
/// eccentric or a capstan slips: the medium arrives early or late and the
/// pitch goes with it. Three things move it at once.
///
/// A slow warp, the once-round-the-record wander that a dished or off-centre
/// disc gives. A faster wobble on top, too quick to follow and too slow to be
/// vibrato. And now and then a nudge, a sharp slip that bends hard and settles
/// back, which is the glitch: it arrives at random and its rate and size climb
/// faster than the wander does, so a low setting is a tired turntable and a
/// high one is a broken transport.
///
/// Both channels are driven from the same modulation. It is one platter, so a
/// centred source stays centred, which is also what keeps this distinct from
/// the echo further down the chain, where the whole point is that the two
/// sides disagree.
///
/// The delay it introduces grows with the amount rather than being switched
/// in, so at zero there is no delay at all and turning the control up cannot
/// step. Bypassed entirely at zero, with the line cleared on the way out.
class Wobble {
public:
  /// How far the read point can be pushed either side of centre. Full travel
  /// at the slow rate works out at a couple of semitones, which is a record
  /// nobody would keep.
  static constexpr float kMaxDepthSeconds = 0.030f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place.
  ///
  /// @param amount  0 to 1. Zero passes the signal through untouched.
  void process(float *left, float *right, int numSamples,
               float amount) noexcept;

private:
  float read(const std::vector<float> &line, int write,
             float delaySamples) const noexcept;

  double sampleRate = 44100.0;
  int length = 0;

  std::vector<float> lineL, lineR;
  int write = 0;

  /// Where the read point sits now, smoothed so a change of amount slides
  /// rather than jumps.
  float smoothedOffset = 0.0f;

  Xorshift rng{0x2545f491u};
  SmoothRandom warp;   ///< once round the record
  SmoothRandom wobble; ///< faster, shallower

  /// The slip. A target that is knocked to a random value and decays back,
  /// chased by a value that cannot move instantly, so the pitch bends hard
  /// without the read point ever jumping.
  float nudgeTarget = 0.0f;
  float nudge = 0.0f;

  float nudgeDecay = 0.0f;
  float nudgeChase = 0.0f;

  bool wasRunning = false;
};

} // namespace ovt
