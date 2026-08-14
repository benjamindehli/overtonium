#pragma once

#include <array>
#include <vector>

#include "Params.h"

namespace ovt {

/// A feedback delay network reverb.
///
/// Eight delay lines are fed back through a Householder matrix, which is
/// orthogonal, so the network neither gains nor loses energy of its own accord
/// and the decay is entirely the doing of the per-line gains. Ahead of it sit
/// four allpass stages per channel, which scatter a single hit into a dense
/// wash before it ever reaches the network.
///
/// A comb-based reverb rings badly on this instrument in particular: 32 pure
/// sines held indefinitely will find every resonance a fixed network has. The
/// line lengths are therefore mutually prime and slowly modulated, which keeps
/// the tail moving underneath a sustained chord instead of settling on a pitch.
class Reverb {
public:
  static constexpr int kLines = 8;
  static constexpr int kAllpasses = 4;
  static constexpr float kMaxPreDelaySeconds = 0.25f;

  /// Longest the lines are ever cut, as a multiple of their nominal length.
  static constexpr float kMaxScale = 1.6f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place.
  void process(float *outL, float *outR, int numSamples,
               const ReverbParams &p) noexcept;

  float tailSeconds(const ReverbParams &) const noexcept;

private:
  /// A delay line with a damping filter on its way round the loop.
  struct Line {
    std::vector<float> buffer;
    int write = 0;
    float damp = 0.0f;
    double modPhase = 0.0;

    void resize(int maxLength);
    void clear() noexcept;

    /// Reads back from the write head, interpolated, since the length is
    /// modulated and the size control lands between samples.
    float read(float delaySamples) const noexcept;
    void push(float x) noexcept;
  };

  struct Allpass {
    std::vector<float> buffer;
    int write = 0;

    void resize(int lengthSamples);
    void clear() noexcept;
    float process(float x, float coefficient) noexcept;
  };

  double sampleRate = 44100.0;

  std::array<Line, kLines> lines{};
  /// Nominal line lengths in samples, before the size control scales them.
  std::array<float, kLines> baseSamples{};

  std::array<Allpass, kAllpasses> diffusionL{}, diffusionR{};

  std::vector<float> preDelayL, preDelayR;
  int preDelayWrite = 0;
  int preDelayLength = 0;

  float lowCutL = 0.0f, lowCutR = 0.0f;

  /// Size is smoothed rather than jumped to. Re-cutting eight delay lines
  /// under a sounding tail is audible, and gliding them is not.
  float smoothedScale = -1.0f;

  bool wasEnabled = false;
};

} // namespace ovt
