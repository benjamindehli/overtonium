#pragma once

#include <vector>

#include "Drift.h"

namespace ovt {

/// A warped record under the whole instrument.
///
/// Pitch is bent by reading the output back through a delay line whose length
/// keeps moving. Three things move it at once: a slow warp, the
/// once-round-the-record wander, a faster wobble on top, and now and then a
/// random slip that bends hard and settles back. Slips grow in rate and size
/// faster than the wander as the amount comes up.
///
/// Both channels share the same modulation, so a centred source stays centred.
///
/// The delay grows with the amount, so at zero there is no delay and turning
/// the control up cannot step. At zero it is bypassed and the line cleared.
class Wobble {
public:
  /// How far the read point can be pushed either side of centre.
  static constexpr float kMaxDepthSeconds = 0.030f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Does not allocate.
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
