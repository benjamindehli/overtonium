#pragma once

#include <array>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A plate reverb.
///
/// A plate has no geometry to speak of, so a hit spreads across the sheet
/// almost at once and the tail is dense from its first instant, with no early
/// reflections. The room machine is the opposite: a feedback delay network
/// whose lines arrive one after another.
///
/// The shape is Dattorro's. Four allpasses scatter the input, then a tank of
/// two crossed branches passes it round in a figure of eight, each branch an
/// allpass that is slowly modulated, a delay, a damping filter, a second
/// allpass and a second delay. Each output sums taps across both branches, so
/// the two channels differ without one being a delayed copy of the other.
///
/// The tank allpasses wander about a millisecond, more than Dattorro's figure,
/// which keeps a long tail from being heard as the same arrivals repeating.
///
/// DECAY sets the tank's gain, DAMP the filter in each branch, and PRE-DELAY
/// sits ahead of everything.
class PlateReverb {
public:
  static constexpr float kMaxPreDelaySeconds = 0.25f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Does not allocate.
  void process(float *outL, float *outR, int numSamples,
               const ReverbParams &p) noexcept;

  /// How long the tail rings after the input stops, zero when the plate is off.
  float tailSeconds(const ReverbParams &) const noexcept;

private:
  /// A delay of a fixed length, read at whatever taps are wanted.
  struct Line {
    std::vector<float> buffer;
    int write = 0;

    void resize(int length);
    void clear() noexcept;

    void push(float x) noexcept {
      buffer[(size_t)write] = x;

      if (++write >= (int)buffer.size())
        write = 0;
    }

    /// Whole samples back from the write head.
    float at(int back) const noexcept;

    /// Fractionally, for the modulated stages, which have to move between
    /// samples or they step rather than glide.
    float atFraction(float back) const noexcept;
  };

  /// A Schroeder allpass: a delay with a feedforward and a feedback of the
  /// same gain, which scatters without colouring.
  struct Allpass {
    Line line;
    float gain = 0.5f;

    float process(float x) noexcept {
      const auto delayed = line.at((int)line.buffer.size() - 1);
      const auto v = x + delayed * gain;

      line.push(v);

      return delayed - v * gain;
    }
  };

  /// The same, with its length wandering, which keeps a held chord from
  /// finding a resonance and sitting on it.
  struct ModulatedAllpass {
    Line line;
    float gain = 0.5f;
    float nominal = 0.0f;
    float depth = 0.0f;

    double phase = 0.0;
    double rateHz = 0.7;

    float process(float x, double sampleRate) noexcept;
  };

  double sampleRate = 44100.0;

  std::vector<float> preDelay;
  int preWrite = 0;

  std::array<Allpass, 4> diffusers;

  /// One half of the figure of eight. Two of them, crossed.
  struct Branch {
    ModulatedAllpass first;
    Line firstDelay;
    Allpass second;
    Line secondDelay;

    float damp = 0.0f;
  };

  std::array<Branch, 2> tank;

  /// The taps, resolved to this sample rate and clamped to the lines they
  /// read, worked out once in prepare rather than seven times a sample.
  std::array<int, 7> tapAtL{}, tapAtR{};

  /// Which line a tap reads, given the branch it names.
  const Line &lineFor(const Branch &b, int which) const noexcept;

  /// What the tank handed back last time round, which is what the other
  /// branch is fed.
  std::array<float, 2> crossed{};

  bool wasEnabled = false;
};

} // namespace ovt
