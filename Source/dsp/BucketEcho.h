#pragma once

#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A bucket brigade delay across the master output.
///
/// The line has a fixed number of buckets, so a longer delay means a slower
/// clock and a reconstruction filter that comes down with it. TIME therefore
/// also darkens the repeats on this type.
///
/// Stereo is two clocks, each modulated by a slow sine of its own at rates that
/// share no factor, plus a slower drift both follow.
///
/// AGE darkens the repeats further, drives the line harder so the tail grows
/// dirtier with each pass, and raises a hiss gated by a slow compander
/// envelope, so it swells behind a chord and a silent patch stays silent.
class BucketEcho {
public:
  static constexpr float kMaxTimeSeconds = 2.0f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Runs even with no input, since a tail is still a tail.
  void process(float *outL, float *outR, int numSamples,
               const EchoParams &p) noexcept;

  /// Longest tail the current settings can produce, for the host's benefit.
  float tailSeconds(const EchoParams &) const noexcept;

private:
  /// One line of buckets: its charge, its tone and its own clock.
  struct Line {
    std::vector<float> buffer;
    int write = 0;

    float damp = 0.0f; ///< the reconstruction filter, which is most of this
    float dc = 0.0f;   ///< and the coupling that keeps the charge centred

    double phase = 0.0; ///< where its clock has wandered to
    double rateHz = 0.31;

    Xorshift rng{1u}; ///< the hiss, which is its own per side

    void resize(int length);
    void clear() noexcept;

    /// Reads `delaySamples` back, interpolated, since a clock that wanders
    /// does not land on samples.
    float read(float delaySamples) const noexcept;
  };

  double sampleRate = 44100.0;
  int bufferLength = 0;

  Line left, right;

  /// Where the shared drift has got to. One phase for both sides, since this
  /// is the clock itself moving rather than the sides disagreeing.
  double drift = 0.0;

  /// The clock is wound to rather than set, like the tape's head distance: a
  /// line of buckets cannot change its rate instantly either.
  float smoothedDelay = -1.0f;

  /// What the expander is working from, which is the loudest thing it has
  /// heard lately. Shared between the sides, since one compander sees both.
  float envelope = 0.0f;

  bool wasEnabled = false;
};

} // namespace ovt
