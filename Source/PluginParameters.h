#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Params.h"

namespace ovt::params {

// ---- global parameter IDs ---------------------------------------------------
inline constexpr const char *masterGainId = "masterGain";
inline constexpr const char *polyphonyId = "polyphony";
inline constexpr const char *oneVoicePerKeyId = "oneVoicePerKey";
inline constexpr const char *bendRangeId = "bendRange";
inline constexpr const char *phaseResetId = "phaseReset";
inline constexpr const char *stretchId = "stretch";
inline constexpr const char *atSourceId = "atSource";
inline constexpr const char *slideDestId = "slideDest";
inline constexpr const char *trackId = "track";
inline constexpr const char *wobbleId = "wobble";
inline constexpr const char *temperamentId = "temperament";
inline constexpr const char *tuningRootId = "tuningRoot";
inline constexpr const char *referenceHzId = "referenceHz";
inline constexpr const char *safetyClipId = "safetyClip";
inline constexpr const char *mpeId = "mpe";
inline constexpr const char *lofiRateId = "lofiRate";
inline constexpr const char *lofiBitsId = "lofiBits";

// ---- master effects ---------------------------------------------------------
inline constexpr const char *echoOnId = "echoOn";
inline constexpr const char *echoMixId = "echoMix";
inline constexpr const char *echoTimeId = "echoTime";
inline constexpr const char *echoFeedbackId = "echoFeedback";
inline constexpr const char *echoAgeId = "echoAge";

inline constexpr const char *reverbOnId = "reverbOn";
inline constexpr const char *reverbMixId = "reverbMix";
inline constexpr const char *reverbDecayId = "reverbDecay";
inline constexpr const char *reverbDampId = "reverbDamp";
inline constexpr const char *reverbPreDelayId = "reverbPreDelay";

// ---- per-partial parameter IDs ----------------------------------------------
// Suffixes are appended to a stable "h01".."h32" prefix.
inline constexpr const char *tuneSuffix = "tune";
inline constexpr const char *pmRateSuffix = "pmRate";
inline constexpr const char *pmDepthSuffix = "pmDepth";
inline constexpr const char *phaseSuffix = "phase";
inline constexpr const char *driftSuffix = "drift";
inline constexpr const char *delaySuffix = "delay";
inline constexpr const char *attackSuffix = "attack";
inline constexpr const char *decaySuffix = "decay";
inline constexpr const char *sustainSuffix = "sustain";
inline constexpr const char *swellSuffix = "swell";
inline constexpr const char *offLevelSuffix = "offLevel";
inline constexpr const char *releaseSuffix = "release";
inline constexpr const char *amRateSuffix = "amRate";
inline constexpr const char *amDepthSuffix = "amDepth";
inline constexpr const char *liftSuffix = "lift";
inline constexpr const char *velSuffix = "vel";
inline constexpr const char *atSuffix = "aftertouch";
inline constexpr const char *muteSuffix = "mute";
inline constexpr const char *soloSuffix = "solo";
inline constexpr const char *volumeSuffix = "volume";
inline constexpr const char *panSuffix = "pan";

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
    std::atomic<float> *phase = nullptr;
    std::atomic<float> *drift = nullptr;
    std::atomic<float> *delay = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *swell = nullptr;
    std::atomic<float> *offLevel = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *lift = nullptr;
    std::atomic<float> *vel = nullptr;
    std::atomic<float> *at = nullptr;
    std::atomic<float> *mute = nullptr;
    std::atomic<float> *solo = nullptr;
    std::atomic<float> *volume = nullptr;
    std::atomic<float> *pan = nullptr;
  };

  std::array<Osc, kNumHarmonics> osc{};

  struct NoiseChannel {
    std::atomic<float> *colour = nullptr;
    std::atomic<float> *delay = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *swell = nullptr;
    std::atomic<float> *offLevel = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *lift = nullptr;
    std::atomic<float> *vel = nullptr;
    std::atomic<float> *at = nullptr;
    std::atomic<float> *mute = nullptr;
    std::atomic<float> *solo = nullptr;
    std::atomic<float> *volume = nullptr;
    std::atomic<float> *pan = nullptr;
  };

  NoiseChannel noise{};

  std::atomic<float> *masterGain = nullptr;
  std::atomic<float> *polyphony = nullptr;
  std::atomic<float> *oneVoicePerKey = nullptr;
  std::atomic<float> *bendRange = nullptr;
  std::atomic<float> *phaseReset = nullptr;
  std::atomic<float> *stretch = nullptr;
  std::atomic<float> *atSource = nullptr;
  std::atomic<float> *slideDest = nullptr;
  std::atomic<float> *track = nullptr;
  std::atomic<float> *wobble = nullptr;
  std::atomic<float> *temperament = nullptr;
  std::atomic<float> *tuningRoot = nullptr;
  std::atomic<float> *referenceHz = nullptr;
  std::atomic<float> *safetyClip = nullptr;
  std::atomic<float> *mpe = nullptr;
  std::atomic<float> *lofiRate = nullptr;
  std::atomic<float> *lofiBits = nullptr;

  struct Echo {
    std::atomic<float> *on = nullptr;
    std::atomic<float> *mix = nullptr;
    std::atomic<float> *time = nullptr;
    std::atomic<float> *feedback = nullptr;
    std::atomic<float> *age = nullptr;
  };

  struct Reverb {
    std::atomic<float> *on = nullptr;
    std::atomic<float> *mix = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *damp = nullptr;
    std::atomic<float> *preDelay = nullptr;
  };

  Echo echo{};
  Reverb reverb{};

  void connect(juce::AudioProcessorValueTreeState &apvts);

  /// Builds an audio-thread snapshot. @param bendNormalised is -1..1 from the
  /// MIDI wheel.
  void snapshot(SynthParams &out, float bendNormalised) const;

  int polyphonyValue() const;
};

/// The polyphony choices, in parameter order.
inline const std::array<int, 7> kPolyphonyChoices{1, 2, 4, 6, 8, 12, 16};

/// What a preset leaves alone.
///
/// How you play the instrument, how loud it is, and what it is tuned to. A
/// patch describes a sound, and none of these are part of one: a temperament
/// and a reference pitch belong to the music you are playing, polyphony and
/// bend range to the keyboard you are playing it on, and the master fader and
/// the clipper to the desk. Loading a sound should move none of them.
///
/// Named here rather than in Presets.cpp so the code that honours the rule and
/// the test that checks it cannot come to disagree about what the rule is.
inline const std::array<const char *, 11> kSessionParamIds{
    masterGainId,  polyphonyId,   bendRangeId,  atSourceId,
    safetyClipId,  referenceHzId, temperamentId, tuningRootId,
    mpeId,         slideDestId,   oneVoicePerKeyId};

/// How far the two pitch wanderers can each take a partial, in cents.
///
/// Named because the needle on the PITCH MOD rule reads full scale at the sum
/// of them, and a scale that quietly disagreed with the knobs feeding it would
/// be worse than no scale. A test holds these against the ranges themselves.
inline constexpr float kMaxPitchModCents = 200.0f;
inline constexpr float kMaxDriftCents = 25.0f;

/// The widest displacement the two together can produce.
inline constexpr float kMaxPitchDisplacementCents =
    kMaxPitchModCents + kMaxDriftCents;

/// The pitch classes a temperament can be built on, in parameter order.
inline const std::array<const char *, 12> kPitchClassNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// Reference pitches worth offering, in parameter order. Baroque at one end,
/// old concert pitch at the other, and the handful of values orchestras
/// actually use in between.
inline const std::array<int, 11> kReferenceHzChoices{415, 430, 432, 435, 438,
                                                     440, 442, 443, 444, 446,
                                                     466};

/// What drives the per-channel AT amount, in parameter order.
///
/// Aftertouch is the natural source and the one the row is named for, but most
/// keyboards do not have it, and the wheel is the control everybody's hand
/// already goes to. So the wheel can stand in for it, and by default does, at
/// no cost to a controller that sends pressure: a wheel left alone reads zero
/// and changes nothing.
///
/// Polyphonic aftertouch is not on this list. It is per note rather than per
/// channel, there is nothing ambiguous about where it should go, and it stays
/// routed whatever this says.
enum class AftertouchSource { ChannelPressure = 0, ModWheel, Either };


inline const juce::StringArray slideDestChoices{"Off", "Brightness", "Tuning"};

inline const std::array<const char *, 3> kAftertouchSourceNames{
    "Channel pressure", "Mod wheel", "Either"};

/// Render rates, in parameter order. Zero means the host's own, which is the
/// default and the only entry that is not a reduction.
inline const std::array<int, 8> kLofiRateChoices{0,     32000, 22050, 16000,
                                                 11025, 8000,  6000,  4000};

/// Quantiser depths, in parameter order. Zero means none, which is the default
/// and is what the rest of the instrument runs at anyway.
inline const std::array<int, 9> kLofiBitChoices{0, 16, 12, 10, 8, 6, 4, 3, 2};

/// What the menu calls each of them.
juce::String lofiRateName(int hz);
juce::String lofiBitName(int bits);

// ---- reading a level back -----------------------------------------------
//
// Three places turn a level into text: the channel readout, the noise readout
// and the string the host is given. They have to agree, so the floor and the
// test for silence live here rather than three times over.

/// The quietest level a readout counts down to, in decibels.
///
/// Set by the width of a seven-segment readout, which holds a sign and four
/// digits: -99.9 is the quietest figure that fits and -100.0 is the first that
/// does not. Nothing about the fader stops below it, and the range from here to
/// silence is a hair of travel, so this is a limit on what can be said rather
/// than on what can be set.
inline constexpr float kQuietestLevelDb = -99.9f;

/// Whether a gain is quieter than any reading could describe.
inline bool isSilentGain(float gain) noexcept {
  // Half a display step below the floor, so a level that would round up to the
  // floor is still given its number instead of being called silence.
  return gain <= juce::Decibels::decibelsToGain(kQuietestLevelDb - 0.05f);
}

/// The decibel figure a readout shows for a gain.
///
/// Floored, so it can never ask for a character the display has no cell for.
inline float levelDecibels(float gain) noexcept {
  return juce::Decibels::gainToDecibels(gain, kQuietestLevelDb);
}

// The shape of a level fader's travel is levelRange in PluginParameters.cpp,
// which nothing outside it needs to name.

} // namespace ovt::params
