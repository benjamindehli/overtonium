#pragma once

#include <algorithm>
#include <cmath>

#include "Params.h"

namespace ovt::echo {

/// The longest distance between the repeats any of the three allows.
inline constexpr float kMaxTimeSeconds = 2.0f;

/// The settings each echo reads the same way, clamped, and how the spacing
/// follows TIME.
struct Settings {
  float mix = 0.0f;
  float feedback = 0.0f;
  float age = 0.0f;
  float timeSeconds = 0.0f;

  /// The spacing TIME asks for, in samples.
  float targetDelay = 0.0f;

  /// How much of the old spacing each sample keeps. About 90 ms to reach a
  /// new one: fast enough to feel like a knob, slow enough that the pitch
  /// slide is the point rather than a glitch.
  float glide = 0.0f;
};

inline Settings settingsFor(const EchoParams &p, double sampleRate) noexcept {
  Settings s;
  s.mix = std::clamp(p.mix, 0.0f, 1.0f);
  s.feedback = std::clamp(p.feedback, 0.0f, 0.95f);
  s.age = std::clamp(p.age, 0.0f, 1.0f);
  s.timeSeconds = std::clamp(p.timeSeconds, 0.01f, kMaxTimeSeconds);
  s.targetDelay = (float)(s.timeSeconds * sampleRate);
  s.glide = (float)std::exp(-1.0 / (0.09 * sampleRate));
  return s;
}

/// How long the repeats of the echo of type `mine` take to fall 60 dB, which
/// is where a repeat stops being audible under anything else. Zero when that
/// echo is not the one in use.
inline float tailSeconds(const EchoParams &p, EchoType mine) noexcept {
  if (!p.enabled || p.type != mine || p.mix <= 0.0f)
    return 0.0f;

  const auto feedback = std::clamp(p.feedback, 0.0f, 0.95f);

  if (feedback < 0.01f)
    return p.timeSeconds;

  const auto passes = std::log(0.001f) / std::log(feedback);

  return std::min(30.0f, p.timeSeconds * passes);
}

} // namespace ovt::echo
