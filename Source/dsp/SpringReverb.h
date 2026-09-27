#pragma once

#include <array>
#include <vector>

#include "Params.h"

namespace ovt {

/// A spring reverb.
///
/// Three helical springs in a tray, a transducer shaking one end of each and a
/// pickup listening at the other. It is the cheapest reverb ever built and the
/// least like a room of any of them, which is exactly why it has its own sound
/// rather than being a worse plate.
///
/// **What makes it a spring** is dispersion. A wave travelling down a helix
/// does not carry all its frequencies at the same speed: the top of the band
/// arrives first and the bottom drags behind it, so a hit comes back not as a
/// hit but as a descending chirp. That is the boing, and it is not an artefact
/// of a bad reverb but the whole physics of the thing. Every bounce between
/// the ends adds another chirp on top of the last, so a spring gets less like
/// its input as it decays rather than merely quieter, which neither the room
/// nor the plate does.
///
/// **How it is built.** Each spring is a delay of its own transit time with a
/// long chain of first-order allpasses inside the loop. An allpass passes
/// every frequency at full level and delays each by a different amount, which
/// is dispersion written down: sixty of them in a row turn one sample into a
/// sweep of six and a half milliseconds from the top of the band to the
/// bottom, and the loop puts the signal through them again on every bounce.
/// The three springs are different lengths so their chirps never line up, and
/// the two channels are different weightings of the three rather than two
/// tanks.
///
/// The loop is also band-limited, because a transducer and a pickup are: there
/// is nothing below eighty cycles and little above four and a half thousand,
/// however the panel is set. A spring that went to twenty kilohertz would not
/// be a spring.
///
/// **What the panel means here.** DECAY is the loop gain, as on the plate.
/// DAMP closes the pickup's top end down towards the middle of the band, which
/// is what a long tank sounds like from the far end. PRE-DELAY is ahead of
/// everything, as it is on the other two.
class SpringReverb {
public:
  static constexpr float kMaxPreDelaySeconds = 0.25f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place.
  void process(float *outL, float *outR, int numSamples,
               const ReverbParams &p) noexcept;

  float tailSeconds(const ReverbParams &) const noexcept;

private:
  /// A first-order allpass: unity at every frequency, and a delay that is not.
  ///
  /// With the coefficient negative it holds the bottom of the band back and
  /// lets the top through almost at once, which is the way round a spring
  /// disperses. Positive would give the same sweep rising, which is the sound
  /// of a plate being played backwards and not of anything.
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

    /// The two ends of the band the transducer works over. Two poles at the
    /// top, one at the bottom, because it is the top that a spring has none
    /// of: a single pole leaves twelve kilohertz only five decibels down on a
    /// thousand, which is a narrow plate rather than a spring.
    float dark = 0.0f, darker = 0.0f, rumble = 0.0f;

    /// What came back last sample, which is what goes round again.
    float tail = 0.0f;

    void resize(int transitSamples, int sections, float lowest, float highest);
    void clear() noexcept;
  };

  double sampleRate = 44100.0;

  std::array<Spring, 3> springs;

  std::vector<float> preDelay;
  int preWrite = 0;

  bool wasEnabled = false;
};

} // namespace ovt
