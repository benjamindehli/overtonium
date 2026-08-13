#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Params.h"

namespace ovt::params {

// ---- global parameter IDs ---------------------------------------------------
inline constexpr const char *masterGainId = "masterGain";
inline constexpr const char *polyphonyId = "polyphony";
inline constexpr const char *spreadId = "stereoSpread";
inline constexpr const char *bendRangeId = "bendRange";
inline constexpr const char *phaseResetId = "phaseReset";
inline constexpr const char *safetyClipId = "safetyClip";

// ---- per-partial parameter IDs ----------------------------------------------
// Suffixes are appended to a stable "h01".."h32" prefix.
inline constexpr const char *tuneSuffix = "tune";
inline constexpr const char *pmRateSuffix = "pmRate";
inline constexpr const char *pmDepthSuffix = "pmDepth";
inline constexpr const char *driftSuffix = "drift";
inline constexpr const char *delaySuffix = "delay";
inline constexpr const char *attackSuffix = "attack";
inline constexpr const char *decaySuffix = "decay";
inline constexpr const char *sustainSuffix = "sustain";
inline constexpr const char *releaseSuffix = "release";
inline constexpr const char *amRateSuffix = "amRate";
inline constexpr const char *amDepthSuffix = "amDepth";
inline constexpr const char *velSuffix = "vel";
inline constexpr const char *atSuffix = "aftertouch";
inline constexpr const char *muteSuffix = "mute";
inline constexpr const char *soloSuffix = "solo";
inline constexpr const char *volumeSuffix = "volume";

/// "h07_tune" for index0 == 6. Zero-padded so the IDs sort naturally.
juce::String oscParamId(const char *suffix, int index0);

/// "noise_attack". The noise channel reuses the strip suffixes where they
/// apply, and adds one of its own.
juce::String noiseParamId(const char *suffix);

inline constexpr const char *colourSuffix = "colour";

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Default fader level for a partial: a 1/n roll-off over the first octave-and-
/// a-bit, which lands somewhere between a drawbar organ and a soft sawtooth.
float defaultVolumeFor(int index0);

/// Cached atomic pointers, resolved once so the audio thread never does a
/// string lookup.
struct Cache {
  struct Osc {
    std::atomic<float> *tune = nullptr;
    std::atomic<float> *pmRate = nullptr;
    std::atomic<float> *pmDepth = nullptr;
    std::atomic<float> *drift = nullptr;
    std::atomic<float> *delay = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *vel = nullptr;
    std::atomic<float> *at = nullptr;
    std::atomic<float> *mute = nullptr;
    std::atomic<float> *solo = nullptr;
    std::atomic<float> *volume = nullptr;
  };

  std::array<Osc, kNumHarmonics> osc{};

  struct NoiseChannel {
    std::atomic<float> *colour = nullptr;
    std::atomic<float> *delay = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *vel = nullptr;
    std::atomic<float> *at = nullptr;
    std::atomic<float> *mute = nullptr;
    std::atomic<float> *solo = nullptr;
    std::atomic<float> *volume = nullptr;
  };

  NoiseChannel noise{};

  std::atomic<float> *masterGain = nullptr;
  std::atomic<float> *polyphony = nullptr;
  std::atomic<float> *spread = nullptr;
  std::atomic<float> *bendRange = nullptr;
  std::atomic<float> *phaseReset = nullptr;
  std::atomic<float> *safetyClip = nullptr;

  void connect(juce::AudioProcessorValueTreeState &apvts);

  /// Builds an audio-thread snapshot. @param bendNormalised is -1..1 from the
  /// MIDI wheel.
  void snapshot(SynthParams &out, float bendNormalised) const;

  int polyphonyValue() const;
};

/// The polyphony choices, in parameter order.
inline const std::array<int, 7> kPolyphonyChoices{1, 2, 4, 6, 8, 12, 16};

} // namespace ovt::params
