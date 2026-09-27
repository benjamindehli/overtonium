#pragma once

#include <array>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A plate reverb.
///
/// A sheet of steel under tension with a driver at one corner and pickups at
/// two others. What makes it not a room is that it has no geometry to speak
/// of: a hit spreads across the whole sheet almost at once, so there are no
/// early reflections to count and no build-up to hear. The tail is simply
/// there, dense from its first instant, which is why plates were the studio
/// reverb of choice for thirty years and why a voice sits in one so easily.
///
/// The room next door is a feedback delay network, and it is the opposite
/// thing: eight lines whose lengths are a geometry, arriving one after
/// another. Given the same decay time the two sound nothing alike, and this
/// one is the one to reach for when a sound has to be wet without the wet
/// being a place.
///
/// **How it is built.** Four allpasses scatter the input before it reaches
/// anything, then a tank of two branches passes the signal round in a figure
/// of eight, each branch made of an allpass that is slowly modulated, a delay,
/// a damping filter, a second allpass and a second delay. Each output is taken
/// from several taps across the other branch rather than from the end of its
/// own, which is what makes the two channels differ without either being a
/// delayed copy of the other. The shape is Dattorro's, which is the one
/// everybody uses because it is the one that works.
///
/// **It is a modulated plate**, and the menu says so rather than pretending
/// otherwise. The two allpasses in the tank wander about a millisecond, which
/// is far more than a sheet of steel under tension does and more than the
/// shape was originally given. The reason is the circuit: it is three quarters
/// of a second long, so at a long decay the same fixed set of arrivals comes
/// back round again and again and is heard as a pattern rather than a wash.
/// The wander breaks that up. What it costs is that a held chord moves in a
/// way a real plate does not, and that turned out to be worth having.
///
/// **What the panel means here.** DECAY is the tank's own gain rather than a
/// room size, since a plate has no size to set. DAMP is the filter in each
/// branch, which on a real plate is the sheet losing its top end to the air
/// around it. PRE-DELAY is ahead of everything, as it is on the room.
class PlateReverb {
public:
  static constexpr float kMaxPreDelaySeconds = 0.25f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place.
  void process(float *outL, float *outR, int numSamples,
               const ReverbParams &p) noexcept;

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
