#pragma once

#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A tape echo across the master output.
///
/// The head distance is reached by winding rather than jumping, every pass
/// loses top end and a little bottom, the motor wanders, and the tape
/// saturates when driven.
///
/// Stereo is two tape paths side by side. Each takes its own channel, feeds
/// only itself and comes back on the side it went out on, so a partial's
/// repeats stay where the mixer put it. The two motors run at slightly
/// different speeds and depths from different random streams, so the paths
/// never fall into step. How far they disagree follows AGE.
class TapeEcho {
public:
  /// Longest head distance the buffer has to allow for, plus room for the
  /// motor to wander past it.
  static constexpr float kMaxTimeSeconds = 2.0f;

  /// The least worn the machine is allowed to be. At zero both paths wander by
  /// nothing and the repeat collapses to mono, so a floor keeps some width.
  static constexpr float kMinAge = 0.08f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Runs even with no input, since a tail is still a tail.
  void process(float *outL, float *outR, int numSamples,
               const EchoParams &p) noexcept;

  /// Longest tail the current settings can produce, for the host's benefit.
  float tailSeconds(const EchoParams &) const noexcept;

private:
  /// One tape path: its loop, its tone, and its own motor.
  struct Head {
    std::vector<float> buffer;
    int write = 0;

    float damp = 0.0f; ///< one-pole lowpass state, the tape losing its top
    float dc = 0.0f;   ///< one-pole highpass state, the tape losing its bottom

    // The motor. Its own random stream as well as its own rate, since two
    // decks running at the same nominal speed still wander independently, and
    // sharing a stream would have them wander in step.
    Xorshift rng{1u};
    SmoothRandom wow;
    float flutterPhase = 0.0f;

    double wowRateHz = 0.7;
    float flutterRateHz = 6.3f;
    float wowDepth = 0.006f;
    float flutterDepth = 0.0012f;

    void resize(int length);
    void clear() noexcept;

    /// Restarts the motor from a known place.
    void restartMotor(double sr, uint32_t seed, float startPhase) noexcept;

    /// How far off the nominal head distance the motor has wandered.
    float wander(float delaySamples, float age, double sr) noexcept;

    /// Reads `delaySamples` back from the write head, interpolated so the
    /// motor can wander between samples.
    float read(float delaySamples) const noexcept;
  };

  double sampleRate = 44100.0;
  int bufferLength = 0;

  Head left, right;

  /// The head distance is smoothed rather than set, so turning the knob winds
  /// the tape to its new speed instead of cutting to it.
  float smoothedDelay = -1.0f;

  bool wasEnabled = false;
};

} // namespace ovt
