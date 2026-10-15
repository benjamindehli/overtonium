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
  feeding.fill(0.0f);
}

void SympatheticStrings::reset() noexcept {
  for (auto &s : strings)
    s.reset();

  ringing.fill(false);
  feeding.fill(0.0f);
}

double SympatheticStrings::damperSeconds(int string) noexcept {
  const auto t = std::clamp((double)string / (double)(kStrings - 1), 0.0, 1.0);
  return kDamperBassSeconds *
         std::pow(kDamperTrebleSeconds / kDamperBassSeconds, t);
}

float SympatheticStrings::makeUpDecibels(ResonatorMode mode) noexcept {
  switch (mode) {
  case ResonatorMode::Piano:
    return 8.0f;
  case ResonatorMode::Open:
    return 7.5f;
  case ResonatorMode::Series:
    return 4.5f;
  case ResonatorMode::Octave:
    return 11.0f;
  case ResonatorMode::Off:
  case ResonatorMode::NumModes:
    break;
  }

  return 0.0f;
}

bool SympatheticStrings::isRinging() const noexcept {
  return std::any_of(ringing.begin(), ringing.end(), [](bool r) { return r; });
}

void SympatheticStrings::process(float *left, float *right, int numSamples,
                                 const ResonatorParams &params,
                                 const Plan &plan) noexcept {
  // An equal-power crossfade. The resonator and the instrument do not add up
  // in phase, so a straight one would dip in the middle of the knob.
  const float mix = std::clamp(params.mix, 0.0f, 1.0f);
  const float dryGain = std::pow(std::cos(mix * 1.5707963f), 1.5f);
  const float wetGain = std::pow(std::sin(mix * 1.5707963f), 1.5f) *
                        std::pow(10.0f, makeUpDecibels(params.mode) / 20.0f);
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

      // How fast a string takes energy in. A string fed at its own pitch
      // settles at drive / (1 - g), where 1 - g is what it loses each pass,
      // so a held note's level follows DECAY unless the drive follows the
      // loss. A short note, though, takes in drive times its length,
      // whatever DECAY is. No drive keeps both even, so it follows the loss
      // to the power kLossExponent, which splits the difference: from 0.2 to
      // 10 s a held note and a short one each move by a few decibels, in
      // opposite directions. At kCouplingSeconds a held partial drives its
      // string to kSend of its own level.
      const auto lossAt = [hz](double seconds) {
        return 1.0 - std::pow(10.0, -3.0 / (hz * seconds));
      };
      const auto reference = lossAt(kCouplingSeconds);
      const auto target =
          free ? (float)(reference *
                         std::pow(lossAt(decay) / reference, kLossExponent)) *
                     (plan.pedalled[s] ? kPedalSend : 1.0f)
               : 0.0f;

      // Moved across the block rather than stepped at its start. A step in
      // what eighty-seven strings are fed, when the pedal goes down or up, is
      // eighty-seven clicks at once.
      const auto step = (target - feeding[s]) / (float)len;

      for (int n = 0; n < len; ++n) {
        feeding[s] += step;

        // Only an undamped string takes anything in. A damper presses on a
        // string, and what it does not stop it soon will.
        const auto y = string.tick(in[n] * feeding[s]) * string.audibility();
        peak = std::max(peak, std::abs(y));
        sum[n] += y;
      }

      feeding[s] = target;
      ringing[s] = free || peak >= kQuiet;
    }

    // MIX goes from the instrument alone to the resonator alone, as the echo's
    // and the reverb's do.
    for (int n = 0; n < len; ++n) {
      const auto wet = sum[n] * wetGain;
      left[done + n] = left[done + n] * dryGain + wet;
      right[done + n] = right[done + n] * dryGain + wet;
    }
  }
}

} // namespace ovt
