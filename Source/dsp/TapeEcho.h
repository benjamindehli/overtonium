#pragma once

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
/// Stereo is a crossfeed rather than a second time. Two different delay times
/// comb against each other as soon as the source has any width, while feeding
/// each head back into the other keeps one time and lets the repeats walk
/// across the image.
class TapeEcho {
public:
  /// Longest head distance the buffer has to allow for, plus room for the
  /// motor to wander past it.
  static constexpr float kMaxTimeSeconds = 2.0f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Runs even with no input, since a tail is still a tail.
  void process(float *outL, float *outR, int numSamples,
               const EchoParams &p) noexcept;

  /// Longest tail the current settings can produce, for the host's benefit.
  float tailSeconds(const EchoParams &) const noexcept;

private:
  struct Head {
    std::vector<float> buffer;
    int write = 0;

    float damp = 0.0f; ///< one-pole lowpass state, the tape losing its top
    float dc = 0.0f;   ///< one-pole highpass state, the tape losing its bottom

    void resize(int length);
    void clear() noexcept;

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

  Xorshift rng{0x51ed270bu};
  SmoothRandom wow;          ///< the slow wander
  float flutterPhase = 0.0f; ///< the fast one, which is periodic on a real deck
};

} // namespace ovt
