#pragma once

#include <algorithm>
#include <vector>

namespace ovt {

/// Reads a ring buffer `delaySamples` back from `write`, blending the two
/// samples either side of the fractional position.
///
/// The delay is clamped between `shortest` and two short of the length.
/// Zero reads back the sample just written, which suits a line whose length
/// starts at nothing. A line too short to blend in reads silence.
inline float readBack(const std::vector<float> &line, int write,
                      float delaySamples, float shortest) noexcept {
  const auto length = (int)line.size();

  if (length <= 2)
    return 0.0f;

  const auto clamped = std::clamp(delaySamples, shortest, (float)(length - 2));
  const auto whole = (int)clamped;
  const auto frac = clamped - (float)whole;

  auto index = write - whole;
  while (index < 0)
    index += length;

  auto previous = index - 1;
  if (previous < 0)
    previous += length;

  const auto a = line[(size_t)index];
  const auto b = line[(size_t)previous];

  return a + (b - a) * frac;
}

} // namespace ovt
