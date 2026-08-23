#pragma once

#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A tape echo across the master output.
///
/// Modelled on the parts of a tape loop that matter to the ear rather than on
/// the machine: the head distance is reached by winding rather than by
/// jumping, every pass round the loop loses top end and a little of its
/// bottom, the motor wanders, and the tape saturates when it is driven.
///
/// Stereo is two tape paths side by side rather than one walking across the
/// image. Each takes its own channel, feeds only itself and comes back hard on
/// the side it went out on, so nothing crosses over at any point and wherever
/// the mixer put a partial is where its repeats stay. Hard panning is safe to
/// do precisely because the two play at very nearly the same moment.
///
/// What separates them is the transport. The two motors run at slightly
/// different speeds, wander by slightly different amounts and draw from
/// different random streams, so they never fall into step. Two takes of the
/// same part never drift together and neither do these: a centred source comes
/// back as a pair that agrees about the note and disagrees about everything
/// else.
///
/// How far they disagree follows AGE, since holding speed is what a machine in
/// good order does. A new deck tracks true and its repeat comes back centred.
class TapeEcho {
public:
  /// Longest head distance the buffer has to allow for, plus room for the
  /// motor to wander past it.
  static constexpr float kMaxTimeSeconds = 2.0f;

  /// The least worn the machine is allowed to be.
  ///
  /// Zero is a transport that holds speed perfectly, which no transport does,
  /// and which collapses the two paths onto each other: they wander by the
  /// same nothing, so the repeat comes back in mono. A floor here keeps the
  /// doubling always present. Measured on a sustained tone, the two channels
  /// sit at a correlation of 0.77 at this setting, so there is a definite
  /// width without it reading as a chorus.
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
    void restartMotor(double sr, uint32_t seed,
                      float startPhase) noexcept;

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
