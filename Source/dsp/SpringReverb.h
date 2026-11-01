#pragma once

#include <array>
#include <vector>

#include "Params.h"

namespace ovt {

/// A spring reverb.
///
/// What makes it a spring is dispersion: a wave travelling down a helix
/// carries its top end faster than its bottom, so a hit comes back as a
/// descending chirp, and each bounce adds another.
///
/// Each of three springs is a delay of its own transit time with a chain of
/// first-order allpasses inside the loop, which delays each frequency by a
/// different amount. The springs are different lengths so their chirps never
/// line up, and the two channels are different weightings of the three.
///
/// The band is limited to roughly 80 Hz to 5 kHz by a pickup outside the loop.
/// DECAY is the loop gain, DAMP the spring's own top-end loss inside the loop,
/// and PRE-DELAY sits ahead of everything.
class SpringReverb {
public:
  static constexpr float kMaxPreDelaySeconds = 0.25f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Does not allocate.
  void process(float *outL, float *outR, int numSamples,
               const ReverbParams &p) noexcept;

  /// How long the tail rings after the input stops, zero when the spring is
  /// off.
  float tailSeconds(const ReverbParams &) const noexcept;

private:
  /// A first-order allpass: unity at every frequency, and a delay that is not.
  /// A negative coefficient holds the bottom back, so the sweep descends as a
  /// spring's does.
  struct Allpass {
    float a = -0.78f;
    float state = 0.0f;

    float process(float x) noexcept {
      const auto v = x - a * state;
      const auto y = a * v + state;

      state = v;

      return y;
    }
  };

  /// One spring: a loop of its own length, dispersing on every pass.
  struct Spring {
    std::vector<float> line;
    int write = 0;

    std::vector<Allpass> dispersion;

    /// What the spring itself loses on each pass: its top end, which is what
    /// DAMP sets, and its bottom, which a helix has none of either way.
    float dark = 0.0f, rumble = 0.0f;

    /// What came back last sample, which is what goes round again.
    float tail = 0.0f;

    void resize(int transitSamples, int sections, float lowest, float highest);
    void clear() noexcept;
  };

  double sampleRate = 44100.0;

  std::array<Spring, 3> springs;

  /// The pickup at the end of the tray, one per channel, two poles, met once
  /// outside the loop. Bounding the band here lets the loop's own damping
  /// corner sit higher. See kOpenHz.
  struct Pickup {
    float first = 0.0f, second = 0.0f;

    float process(float x, float coef) noexcept {
      first = x + (first - x) * coef;
      second = first + (second - first) * coef;

      return second;
    }

    void clear() noexcept { first = second = 0.0f; }
  };

  Pickup pickupL, pickupR;

  std::vector<float> preDelay;
  int preWrite = 0;

  bool wasEnabled = false;
};

} // namespace ovt
