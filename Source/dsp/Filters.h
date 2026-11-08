#pragma once

#include <cmath>

namespace ovt {

/// The coefficient of a one-pole low pass with its corner at `hz`: the share
/// of the previous output each sample keeps. The callers that take a cutoff
/// from a knob clamp it to the rate first.
inline float onePole(double hz, double sampleRate) noexcept {
  return (float)std::exp(-6.283185307179586 * hz / sampleRate);
}

} // namespace ovt
