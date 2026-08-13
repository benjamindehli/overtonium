#pragma once

#include <cstdint>

namespace ovt {

/// xorshift32. Deterministic, allocation free and safe on the audio thread,
/// which is all a modulation source needs. It is not a good general purpose
/// generator and is not used as one.
class Xorshift {
public:
  explicit Xorshift(uint32_t seed = 1u) noexcept { reseed(seed); }

  /// Plain xorshift correlates noticeably between nearby seeds for the first
  /// few hundred outputs, so the seed is avalanched first and the state warmed
  /// up before anyone sees it. A zero state would lock the generator up, hence
  /// the forced low bit.
  void reseed(uint32_t seed) noexcept {
    seed ^= 0x9e3779b9u;
    seed ^= seed >> 16;
    seed *= 0x21f0aaadu;
    seed ^= seed >> 15;
    seed *= 0x735a2d97u;
    seed ^= seed >> 15;

    state = seed | 1u;

    for (int i = 0; i < 16; ++i)
      next();
  }

  uint32_t next() noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  /// Uniform in [-1, 1).
  float bipolar() noexcept {
    return (float)next() * (2.0f / 4294967296.0f) - 1.0f;
  }

  /// Uniform in [0, 1).
  float unipolar() noexcept { return (float)next() * (1.0f / 4294967296.0f); }

private:
  uint32_t state = 1u;
};

/// A smooth random contour.
///
/// Random points are drawn at a fixed rate and joined with a Catmull-Rom
/// spline. Sample and hold would step between them, and a lowpassed noise
/// source would wander in amplitude as well as value. This does neither: the
/// output is continuous, and it stays inside roughly +-1 whatever the rate.
class SmoothRandom {
public:
  /// @param rateHz          how often a new point is drawn
  /// @param stepsPerSecond  how often advance() will be called
  void restart(Xorshift &rng, double rateHz, double stepsPerSecond) noexcept {
    increment = stepsPerSecond > 0.0 ? rateHz / stepsPerSecond : 0.0;
    phase = 0.0;

    // Seeding all four points means a note begins part way into a wander
    // rather than from a shared zero, so voices never start in unison.
    for (auto &p : points)
      p = rng.bipolar();
  }

  void reset() noexcept {
    phase = 0.0;
    increment = 0.0;
    for (auto &p : points)
      p = 0.0f;
  }

  float advance(Xorshift &rng) noexcept {
    phase += increment;

    while (phase >= 1.0) {
      phase -= 1.0;
      points[0] = points[1];
      points[1] = points[2];
      points[2] = points[3];
      points[3] = rng.bipolar();
    }

    return interpolate((float)phase);
  }

  float current() const noexcept { return interpolate((float)phase); }

private:
  float interpolate(float t) const noexcept {
    const float a = points[1];
    const float b = 0.5f * (points[2] - points[0]);
    const float c =
        points[0] - 2.5f * points[1] + 2.0f * points[2] - 0.5f * points[3];
    const float d =
        0.5f * (points[3] - points[0]) + 1.5f * (points[1] - points[2]);

    return ((d * t + c) * t + b) * t + a;
  }

  float points[4]{};
  double phase = 0.0;
  double increment = 0.0;
};

} // namespace ovt
