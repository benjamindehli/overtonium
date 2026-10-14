#include "SympatheticStrings.h"

#include <algorithm>
#include <cmath>

namespace ovt {

namespace {
/// Worked through in pieces this long, so the input and the sum have
/// somewhere to live without allocating.
constexpr int kChunk = 256;

/// Below this a string with its damper down has nothing left to say.
constexpr float kQuiet = 1.0e-6f;

double equalHz(int key) { return 440.0 * std::exp2((double)(key - 69) / 12.0); }
} // namespace

void SympatheticStrings::prepare(double sampleRate) noexcept {
  // Each long enough for its own note pulled down as far as any temperament,
  // or Adaptive two commas down, can take it, with room to spare.
  for (int i = 0; i < kStrings; ++i)
    strings[(size_t)i].prepare(sampleRate, 0.85 * equalHz(keyOf(i)));

  ringing.fill(false);
}

void SympatheticStrings::reset() noexcept {
  for (auto &s : strings)
    s.reset();

  ringing.fill(false);
}

double SympatheticStrings::damperSeconds(int string) noexcept {
  const auto t = std::clamp((double)string / (double)(kStrings - 1), 0.0, 1.0);
  return kDamperBassSeconds *
         std::pow(kDamperTrebleSeconds / kDamperBassSeconds, t);
}

bool SympatheticStrings::isRinging() const noexcept {
  return std::any_of(ringing.begin(), ringing.end(), [](bool r) { return r; });
}

void SympatheticStrings::process(float *left, float *right, int numSamples,
                                 const StringsParams &params,
                                 const Plan &plan) noexcept {
  const float mix = std::clamp(params.mix, 0.0f, 1.0f);
  const float damp = std::clamp(params.damping, 0.0f, 1.0f);
  const double decay = std::max(0.05, (double)params.decaySeconds);

  for (int done = 0; done < numSamples; done += kChunk) {
    const int len = std::min(kChunk, numSamples - done);

    float in[kChunk];
    float sum[kChunk];

    for (int n = 0; n < len; ++n) {
      in[n] = 0.5f * (left[done + n] + right[done + n]) * kSend;
      sum[n] = 0.0f;
    }

    for (int i = 0; i < kStrings; ++i) {
      const auto s = (size_t)i;
      const bool inUse = plan.hz[s] > 0.0;
      const bool free = inUse && plan.free[s];

      // Damped and silent: nothing to do until its damper comes up.
      if (!free && !ringing[s])
        continue;

      auto &string = strings[s];

      // A string out of use keeps the pitch it last had while it rings out.
      const auto hz = inUse ? plan.hz[s] : equalHz(keyOf(i));

      // A damper never lets a string ring longer than DECAY would.
      string.setBlock(hz, free ? decay : std::min(decay, damperSeconds(i)),
                      damp, -1.0f, len);

      float peak = 0.0f;

      // The same rate whatever DECAY is. A string fed at its own pitch builds
      // up to coupling / (1 - g), so a longer DECAY rings louder as well as
      // longer, and at kCouplingSeconds that is kSend. Feeding each string in
      // proportion to its own loss would hold the level steady, but a string
      // with a long DECAY would then take in almost nothing during a short
      // note.
      const auto coupling =
          1.0f - (float)std::pow(10.0, -3.0 / (hz * kCouplingSeconds));
      const auto drive =
          free ? coupling * (plan.pedalled[s] ? kPedalSend : 1.0f) : 0.0f;

      for (int n = 0; n < len; ++n) {
        // Only an undamped string takes anything in. A damper presses on a
        // string, and what it does not stop it soon will.
        const auto y = string.tick(in[n] * drive) * string.audibility();
        peak = std::max(peak, std::abs(y));
        sum[n] += y;
      }

      ringing[s] = free || peak >= kQuiet;
    }

    // MIX goes from the instrument alone to the strings alone, as the echo's
    // and the reverb's do.
    for (int n = 0; n < len; ++n) {
      const auto wet = sum[n] * kWetScale;
      left[done + n] += (wet - left[done + n]) * mix;
      right[done + n] += (wet - right[done + n]) * mix;
    }
  }
}

} // namespace ovt
