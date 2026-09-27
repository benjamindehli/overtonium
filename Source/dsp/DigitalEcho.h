#pragma once

#include <vector>

#include "Params.h"

namespace ovt {

/// A digital delay across the master output.
///
/// The one that does not pretend to be anything. No tape to wear, no line of
/// buckets to clock, no motor and no lamp: what goes in comes back out, later
/// and quieter, and the only thing it loses on each pass is level. That is
/// worth having beside the other two precisely because it is the plain one.
///
/// **Its stereo is ping-pong** rather than two paths run slightly differently.
/// The input arrives on one side and every repeat crosses to the other, so a
/// single note becomes a line of repeats alternating left and right at the
/// interval TIME names. That is a different machine from the tape and the
/// bucket brigade rather than a setting of one: on those two, nothing crosses
/// between the sides at any point and the width comes from the two paths
/// disagreeing. Here the two sides are one path, and the width is the whole
/// point of the topology.
///
/// **What AGE does here** is the one thing none of the other types can do. At
/// nothing it is exactly a copy, which is what a digital delay is for. Turned
/// up, the repeats are handed back through fewer bits and at a lower rate, so
/// a tail starts as the signal and ends as a memory of it: the same machine
/// the converter on the bar is, pointed at the loop instead of at the output.
/// It is inside the feedback path as well as on the way out, so each pass is
/// rougher than the last rather than all of them being equally rough.
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
  /// One side of the ping-pong. No tone and no motor: a buffer and an index.
  struct Side {
    std::vector<float> buffer;
    int write = 0;

    /// What the rate reduction is holding, and how much longer it holds it.
    float held = 0.0f;
    int remaining = 0;

    void resize(int length);
    void clear() noexcept;

    /// Reads back a whole number of samples, since nothing here moves between
    /// them: a digital delay that interpolated would be a worse digital delay.
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

  /// Smoothed, like the others, so moving TIME winds to the new spacing rather
  /// than cutting to it. A digital delay would be within its rights to jump,
  /// but a knob that jumps is a knob nobody turns while anything is playing.
  float smoothedDelay = -1.0f;

  bool wasEnabled = false;
};

} // namespace ovt
