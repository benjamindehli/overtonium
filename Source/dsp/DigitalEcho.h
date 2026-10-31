#pragma once

#include <vector>

#include "Params.h"

namespace ovt {

/// A digital delay across the master output.
///
/// What goes in comes back later and quieter, losing only level on each pass.
///
/// Its stereo is ping-pong: the input enters one side and every repeat crosses
/// to the other, so repeats alternate left and right at the interval TIME
/// names.
///
/// AGE hands the repeats back through fewer bits and at a lower rate. At zero
/// the repeat is an exact copy.
class DigitalEcho {
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
  /// One side of the ping-pong: a buffer, an index and the rate reduction's
  /// held sample.
  struct Side {
    std::vector<float> buffer;
    int write = 0;

    /// What the rate reduction is holding, and how much longer it holds it.
    float held = 0.0f;
    int remaining = 0;

    void resize(int length);
    void clear() noexcept;

    /// Reads back a whole number of samples, without interpolation.
    float read(int delaySamples) const noexcept;
  };

  /// Fewer bits and fewer of them per second, which is what AGE asks for.
  ///
  /// @param hold   how many samples each one is stretched across
  /// @param levels quantisation steps either side of nothing, or zero for none
  static float roughen(Side &s, float x, int hold, float levels) noexcept;

  double sampleRate = 44100.0;
  int bufferLength = 0;

  Side left, right;

  /// Smoothed, so moving TIME winds to the new spacing rather than cutting to
  /// it.
  float smoothedDelay = -1.0f;

  bool wasEnabled = false;
};

} // namespace ovt
