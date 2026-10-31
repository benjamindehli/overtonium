#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

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
inline constexpr const char *characterId = "character";
inline constexpr const char *wobbleId = "wobble";

inline constexpr const char *temperamentId = "temperament";
inline constexpr const char *tuningRootId = "tuningRoot";
inline constexpr const char *referenceHzId = "referenceHz";
inline constexpr const char *safetyClipId = "safetyClip";

/// Which of the eight machines the output stage uses, when it is on. Kept
/// beside the safetyClip switch, as echoTypeId is beside echoOn.
inline constexpr const char *clipTypeId = "clipType";

/// Whether the output stage is allowed its lookahead.
///
/// A session parameter in kSessionParamIds, so no preset can change the
/// latency. Off by default, since only the Limiter, Console, Cassette and
/// Print use the lookahead.
inline constexpr const char *lookaheadId = "lookahead";
inline constexpr const char *mpeId = "mpe";
inline constexpr const char *lofiRateId = "lofiRate";
inline constexpr const char *lofiBitsId = "lofiBits";
inline constexpr const char *pmInPhaseId = "pmInPhase";
inline constexpr const char *amInPhaseId = "amInPhase";
inline constexpr const char *glideTriggerId = "glideTrigger";
inline constexpr const char *glideModeId = "glideMode";

/// Legato, beside the voice count. With one voice it is monophonic legato,
/// with more the top line, where the highest held note carries on into a new
/// key above the rest. See SynthEngine::setLegato.
inline constexpr const char *legatoId = "legato";

/// How long a held note takes to slide to its new tuning when the adaptive
/// temperament retunes it, from at once to a second. A session setting. See
/// SynthEngine::settleAdaptive.
inline constexpr const char *adaptiveRetuneId = "adaptiveRetune";

/// Whether Adaptive tunes a minor seventh on the harmonic seventh, 7:4 above
/// its root. On by default. A session setting.
inline constexpr const char *adaptiveSeventhId = "adaptiveSeventh";

/// Whether Adaptive keeps the held chord near home, moving a held note a
/// comma when a progression would drift. Off by default.
inline constexpr const char *adaptiveHomeId = "adaptiveHome";

/// The longest a retune can take, in seconds.
inline constexpr float kMaxAdaptiveRetuneSeconds = 1.0f;

/// Two named retune times the Settings menu offers. Fast is the default.
inline constexpr std::array<double, 2> kAdaptiveRetuneSeconds{0.03, 0.3};
inline constexpr std::array<const char *, 2> kAdaptiveRetuneNames{"Fast",
                                                                  "Smooth"};

// ---- master effects ---------------------------------------------------------
inline constexpr const char *echoOnId = "echoOn";

/// Which machine the repeats come from.
///
/// Separate from echoOn rather than replacing it, so saved patches and
/// automation lanes on echoOn keep working. The panel shows one four-position
/// control that writes both. A patch without a type gets the tape echo.
inline constexpr const char *echoTypeId = "echoType";
inline constexpr const char *echoMixId = "echoMix";
inline constexpr const char *echoTimeId = "echoTime";
inline constexpr const char *echoFeedbackId = "echoFeedback";
inline constexpr const char *echoAgeId = "echoAge";

inline constexpr const char *reverbOnId = "reverbOn";

/// Which machine the tail comes from. Beside reverbOn, as echoTypeId is
/// beside echoOn.
inline constexpr const char *reverbTypeId = "reverbType";
inline constexpr const char *reverbMixId = "reverbMix";
inline constexpr const char *reverbDecayId = "reverbDecay";
inline constexpr const char *reverbDampId = "reverbDamp";
inline constexpr const char *reverbPreDelayId = "reverbPreDelay";

/// The resonator between the summed channels and the bus drive, a bank of
/// sympathetic strings. See SympatheticStrings.
inline constexpr const char *resonatorModeId = "resonatorMode";
inline constexpr const char *resonatorMixId = "resonatorMix";
inline constexpr const char *resonatorDecayId = "resonatorDecay";
inline constexpr const char *resonatorDampId = "resonatorDamp";

/// How long an undamped sympathetic string rings at the longest, and its
/// default.
inline constexpr float kMaxResonatorDecaySeconds = 10.0f;
inline constexpr float kResonatorDecayDefault = 4.0f;

// ---- per-partial parameter IDs ----------------------------------------------
// Suffixes are appended to a stable "h01".."h32" prefix.
inline constexpr const char *tuneSuffix = "tune";
inline constexpr const char *pmRateSuffix = "pmRate";
inline constexpr const char *pmDepthSuffix = "pmDepth";
inline constexpr const char *pmShapeSuffix = "pmShape";
inline constexpr const char *glideSuffix = "glide";
inline constexpr const char *driftSuffix = "drift";
inline constexpr const char *delaySuffix = "delay";
inline constexpr const char *attackSuffix = "attack";
inline constexpr const char *strikeSuffix = "strike";
inline constexpr const char *decaySuffix = "decay";
inline constexpr const char *sustainSuffix = "sustain";
inline constexpr const char *swellSuffix = "swell";
inline constexpr const char *offLevelSuffix = "offLevel";
inline constexpr const char *releaseSuffix = "release";
inline constexpr const char *amRateSuffix = "amRate";
inline constexpr const char *amDepthSuffix = "amDepth";
inline constexpr const char *amShapeSuffix = "amShape";
inline constexpr const char *velSuffix = "vel";
inline constexpr const char *atSuffix = "aftertouch";
inline constexpr const char *muteSuffix = "mute";
inline constexpr const char *soloSuffix = "solo";
inline constexpr const char *volumeSuffix = "volume";
inline constexpr const char *panSuffix = "pan";

// ---- macros -----------------------------------------------------------------

/// How many macros there are, so a row can be automated as one.
///
/// A fixed pool, since a plugin cannot add parameters later. The panel shows
/// the ones in use and the host sees all of them. Raising this is safe, since
/// parameters are appended. Lowering it would strand automation.
inline constexpr int kNumMacros = 8;

/// The rows a macro can drive, in the order the panel reads them. The same
/// rows LINK can gang. The UI names these again as ui::Role, and a test keeps
/// the two in step.
inline constexpr const char *kMacroRows[] = {
    tuneSuffix,   glideSuffix,    pmRateSuffix,  pmDepthSuffix, driftSuffix,
    strikeSuffix, delaySuffix,    attackSuffix,  decaySuffix,   sustainSuffix,
    swellSuffix,  offLevelSuffix, releaseSuffix, amRateSuffix,  amDepthSuffix,
    velSuffix,    atSuffix,       panSuffix,     volumeSuffix};

inline constexpr int kNumMacroRows =
    (int)(sizeof(kMacroRows) / sizeof(kMacroRows[0]));

/// Which channels a macro reaches.
///
/// All, Odd, Even, then one entry per pitch class from Interval onwards, so
/// Interval + 7 is every channel a fifth above the fundamental. Selected is
/// last, so saved sessions keep their scope, and reaches the channels the
/// macro was given from LINK's selection when it was set. See MacroChannels.
enum class MacroScope {
  All = 0,
  Odd,
  Even,
  Interval,
  Selected = Interval + 12
};

inline constexpr int kNumMacroScopes = (int)MacroScope::Selected + 1;

/// The channels each macro on Selected reaches, a bit per channel from the
/// fundamental. Not parameters, since a channel list is not something to
/// automate. Saved in the session and in preset files separately.
using MacroChannels = std::array<std::atomic<std::uint32_t>, kNumMacros>;

/// A channel mask as the harmonic numbers it holds, "3,5,9,12", as the
/// session and preset files write it, and back.
juce::String channelListText(std::uint32_t mask);
std::uint32_t channelListMask(const juce::String &text);

/// How a macro is shared out across the channels it reaches. Taper is
/// measured from the anchor channel.
enum class MacroCurve { Uniform = 0, Taper, NumCurves };

/// The colours a macro can wear. The first is no colour.
inline constexpr int kNumMacroColours = 9;

const char *macroScopeName(int scope);

/// Whether a macro with this scope reaches this channel. Shared by the
/// snapshot and the window so the tint always matches what is driven.
///
/// @param selected  the macro's own channels, which only Selected reads.
/// @param selected  the macro's own channels, which only Selected reads.
bool macroReaches(int scope, int index0, std::uint32_t selected = 0);

/// This channel's share of a macro. Shared by the snapshot and the window so
/// the knob ring shows what the engine plays.
/// @param anchor  the channel a taper leans on hardest, 0-based. Uniform
///                ignores it.
float macroWeight(MacroCurve curve, int index0, int anchor);

/// The range of a row a macro can drive: what an amount of 1 spans, and the
/// skew the macro's fader borrows. Row 0 is None.
juce::NormalisableRange<float>
macroRowRange(const juce::AudioProcessorValueTreeState &, int row);
const char *macroCurveName(MacroCurve);
const char *macroRowName(int row);
const char *macroColourName(int colour);

/// A macro's tint, or a fully transparent colour for the first entry.
/// Defined in Theme.cpp.
juce::Colour macroColour(int colour);

/// A macro's parameter ids. The amount is the one a host draws, and row 0
/// (None) means the macro is not in use.
juce::String macroAmountId(int macro);
juce::String macroRowId(int macro);
juce::String macroScopeId(int macro);
juce::String macroCurveId(int macro);
juce::String macroColourId(int macro);
juce::String macroAnchorId(int macro);

/// What each modulation destination offers, in the order its parameter stores.
/// Amplitude has no unipolar square, since it only comes down from the fader.
/// Entries are appended only, since the index is saved in presets and
/// sessions.
inline const std::array<LfoShape, 8> kPitchShapes{
    LfoShape::Sine,          LfoShape::Triangle,
    LfoShape::Sawtooth,      LfoShape::ReverseSawtooth,
    LfoShape::BipolarSquare, LfoShape::UnipolarSquare,
    LfoShape::SampleAndHold, LfoShape::Random};

inline const std::array<LfoShape, 7> kAmpShapes{
    LfoShape::Sine,          LfoShape::Triangle,
    LfoShape::Sawtooth,      LfoShape::ReverseSawtooth,
    LfoShape::BipolarSquare, LfoShape::SampleAndHold,
    LfoShape::Random};

/// What the menus call them. Indexed the same as the lists above.
juce::StringArray pitchShapeNames();
juce::StringArray ampShapeNames();

/// The shape a stored index means, clamped so an index from a later build
/// stays in range.
LfoShape pitchShapeAt(int index);
LfoShape ampShapeAt(int index);

/// "h07_tune" for index0 == 6. Zero-padded so the IDs sort naturally.
juce::String oscParamId(const char *suffix, int index0);

/// "noise_attack". The noise channel reuses the strip suffixes where they
/// apply.
juce::String noiseParamId(const char *suffix);

// ---- a switch across the whole mixer ----------------------------------------
//
// For mute and solo, which every channel carries.

/// How many channels have this switch on, the noise channel among them.
int channelsSwitchedOn(juce::AudioProcessorValueTreeState &,
                       const char *suffix);

/// Turns it off wherever it is on, so a host sees a gesture only on the
/// channels that moved.
void clearChannelSwitch(juce::AudioProcessorValueTreeState &,
                        const char *suffix);

inline constexpr const char *colourSuffix = "colour";

/// The string the noise strikes, the noise channel's own. See KarplusString.
inline constexpr const char *stringSuffix = "string";
inline constexpr const char *stringDecaySuffix = "stringDecay";
inline constexpr const char *stringDampSuffix = "stringDamp";
inline constexpr const char *stringPointSuffix = "stringPoint";

/// How long the noise channel's string rings at the longest, and its default.
inline constexpr float kMaxStringDecaySeconds = 10.0f;
inline constexpr float kStringDecayDefault = 1.5f;

/// Every parameter the plugin declares, in host order.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Default fader level for a partial, a 1/n roll-off over the first octave
/// and a bit.
float defaultVolumeFor(int index0);

/// What a strike amount comes to on this strip, as the popup under the knob
/// shows it: "+81 %  2.0 ms to 512 ms". Not given to the host, since it
/// depends on two other controls.
///
/// @param delay,attack  this strip's own two rows, in seconds. At an amount of
///                      zero only the percentage comes back.
juce::String strikeRangeText(float amount, float delay, float attack);

/// Cached atomic pointers, resolved once so the audio thread never does a
/// string lookup.
struct Cache {
  struct Osc {
    std::atomic<float> *tune = nullptr;
    std::atomic<float> *pmRate = nullptr;
    std::atomic<float> *pmDepth = nullptr;
    std::atomic<float> *pmShape = nullptr;
    std::atomic<float> *glide = nullptr;
    std::atomic<float> *drift = nullptr;
    std::atomic<float> *delay = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *strike = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *swell = nullptr;
    std::atomic<float> *offLevel = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *amShape = nullptr;
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
    std::atomic<float> *strike = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *swell = nullptr;
    std::atomic<float> *offLevel = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *amRate = nullptr;
    std::atomic<float> *amDepth = nullptr;
    std::atomic<float> *amShape = nullptr;
    std::atomic<float> *vel = nullptr;
    std::atomic<float> *at = nullptr;
    std::atomic<float> *mute = nullptr;
    std::atomic<float> *solo = nullptr;
    std::atomic<float> *volume = nullptr;
    std::atomic<float> *pan = nullptr;
    std::atomic<float> *string = nullptr;
    std::atomic<float> *stringDecay = nullptr;
    std::atomic<float> *stringDamp = nullptr;
    std::atomic<float> *stringPoint = nullptr;
  };

  NoiseChannel noise{};

  /// A macro's parameters.
  struct Macro {
    std::atomic<float> *amount = nullptr;
    std::atomic<float> *row = nullptr;
    std::atomic<float> *scope = nullptr;
    std::atomic<float> *curve = nullptr;
    std::atomic<float> *anchor = nullptr;
  };

  std::array<Macro, (size_t)kNumMacros> macro{};

  /// What each macro on Selected reaches. Owned here rather than pointed at,
  /// since nothing in the parameter tree holds it.
  MacroChannels macroChannels{};

  /// The range of every row a macro can drive, read once. Every channel shares
  /// one range per row.
  std::array<juce::NormalisableRange<float>, (size_t)kNumMacroRows> rowRange{};

  std::atomic<float> *masterGain = nullptr;
  std::atomic<float> *polyphony = nullptr;
  std::atomic<float> *oneVoicePerKey = nullptr;
  std::atomic<float> *bendRange = nullptr;
  std::atomic<float> *phaseReset = nullptr;
  std::atomic<float> *stretch = nullptr;
  std::atomic<float> *atSource = nullptr;
  std::atomic<float> *slideDest = nullptr;
  std::atomic<float> *track = nullptr;
  std::atomic<float> *character = nullptr;
  std::atomic<float> *wobble = nullptr;
  std::atomic<float> *temperament = nullptr;
  std::atomic<float> *tuningRoot = nullptr;
  std::atomic<float> *referenceHz = nullptr;
  std::atomic<float> *safetyClip = nullptr;
  std::atomic<float> *lookahead = nullptr;
  std::atomic<float> *clipType = nullptr;
  std::atomic<float> *mpe = nullptr;
  std::atomic<float> *lofiRate = nullptr;
  std::atomic<float> *lofiBits = nullptr;
  std::atomic<float> *pmInPhase = nullptr;
  std::atomic<float> *amInPhase = nullptr;
  std::atomic<float> *glideTrigger = nullptr;
  std::atomic<float> *legato = nullptr;
  std::atomic<float> *adaptiveRetune = nullptr;
  std::atomic<float> *adaptiveSeventh = nullptr;
  std::atomic<float> *adaptiveHome = nullptr;
  std::atomic<float> *glideMode = nullptr;

  struct Echo {
    std::atomic<float> *on = nullptr;
    std::atomic<float> *type = nullptr;
    std::atomic<float> *mix = nullptr;
    std::atomic<float> *time = nullptr;
    std::atomic<float> *feedback = nullptr;
    std::atomic<float> *age = nullptr;
  };

  struct Reverb {
    std::atomic<float> *on = nullptr;
    std::atomic<float> *type = nullptr;
    std::atomic<float> *mix = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *damp = nullptr;
    std::atomic<float> *preDelay = nullptr;
  };

  struct Resonator {
    std::atomic<float> *mode = nullptr;
    std::atomic<float> *mix = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *damp = nullptr;
  };

  Echo echo{};
  Reverb reverb{};
  Resonator resonator{};

  /// Resolves every pointer. Call once, after the APVTS is built.
  void connect(juce::AudioProcessorValueTreeState &apvts);

  /// Builds an audio-thread snapshot. @param bendNormalised is -1..1 from the
  /// MIDI wheel.
  void snapshot(SynthParams &out, float bendNormalised) const;

  /// The voice count the polyphony choice allows.
  int polyphonyValue() const;

  /// Whether legato is on, from its switch or the old Legato choice.
  bool legatoValue() const;
};

/// The voice count each polyphony choice allows, in parameter order. The
/// first two are both one voice. See kLegatoIndex.
inline const std::array<int, 8> kPolyphonyChoices{1, 1, 2, 4, 6, 8, 12, 16};

/// The list's first entry, monophonic legato. Legato is now its own switch
/// and the menu no longer offers this, but it stays so stored choices keep
/// their position. A stored 0 plays as legato with one voice.
inline constexpr int kLegatoIndex = 0;

/// "Legato", "1 voice", "2 voices" and so on.
juce::String polyphonyName(int index);

/// What a preset leaves alone: how the instrument is played and what it is
/// tuned to, which is everything the Settings menu offers. Adding a setting
/// means adding it here too. Named here so the preset code and its test
/// share one list.
inline const std::array<const char *, 15> kSessionParamIds{
    polyphonyId,      bendRangeId,       atSourceId,    referenceHzId,
    temperamentId,    tuningRootId,      mpeId,         slideDestId,
    oneVoicePerKeyId, phaseResetId,      lookaheadId,   legatoId,
    adaptiveRetuneId, adaptiveSeventhId, adaptiveHomeId};

/// Whether `id` is one of those.
inline bool isSessionParam(juce::StringRef id) {
  for (auto *session : kSessionParamIds)
    if (id == juce::StringRef(session))
      return true;

  return false;
}

/// How far the two pitch wanderers can each take a partial, in cents. An
/// octave for the modulator so stepped shapes can play real intervals, with a
/// taper that puts 25 cents at half travel.
inline constexpr float kMaxPitchModCents = 1200.0f;
inline constexpr float kMaxDriftCents = 25.0f;

/// The longest glide a channel can be given, in seconds, or seconds per
/// octave under GlideMode::Rate.
inline constexpr float kMaxGlideSeconds = 5.0f;

/// Where the needle on the PITCH MOD lamp reads full scale, in cents.
///
/// Smaller than the knob's maximum so vibrato and drift are visible. Past
/// this the needle pegs. A test keeps it above the drift range.
inline constexpr float kPitchNeedleFullScaleCents = 225.0f;

/// The pitch classes a temperament can be built on, in parameter order.
inline const std::array<const char *, 12> kPitchClassNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// Reference pitches worth offering, in parameter order, from baroque to old
/// concert pitch.
inline const std::array<int, 11> kReferenceHzChoices{
    415, 430, 432, 435, 438, 440, 442, 443, 444, 446, 466};

/// What drives the per-channel AT amount, in parameter order. Polyphonic
/// aftertouch is always routed, whatever this says.
enum class AftertouchSource { ChannelPressure = 0, ModWheel, Either };

inline const juce::StringArray slideDestChoices{"Off", "Brightness", "Tuning"};

/// What the oscillator characters are called, in parameter order, taken from
/// the DSP core's own names. See Character.h.
inline const juce::StringArray &characterChoices() {
  static const juce::StringArray names = [] {
    juce::StringArray out;

    for (int i = 0; i < (int)Character::NumCharacters; ++i)
      out.add(characterName((Character)i));

    return out;
  }();

  return names;
}

inline const std::array<const char *, 3> kAftertouchSourceNames{
    "Channel pressure", "Mod wheel", "Either"};

/// Render rates, in parameter order. Zero means the host's own rate, the
/// default.
inline const std::array<int, 8> kLofiRateChoices{0,     32000, 22050, 16000,
                                                 11025, 8000,  6000,  4000};

/// Quantiser depths, in parameter order. Zero means none, the default.
inline const std::array<int, 9> kLofiBitChoices{0, 16, 12, 10, 8, 6, 4, 3, 2};

/// What the menu calls each of them.
juce::String lofiRateName(int hz);
juce::String lofiBitName(int bits);

// ---- reading a level back -----------------------------------------------
//
// The channel readout, the noise readout and the host's text share this
// floor and test for silence.

/// The quietest level a readout shows, in decibels: -99.9 is the quietest
/// figure that fits the seven-segment display. The fader goes lower.
inline constexpr float kQuietestLevelDb = -99.9f;

/// Whether a gain is quieter than any reading could describe.
inline bool isSilentGain(float gain) noexcept {
  // Half a display step below the floor, so a level that rounds to the floor
  // still shows its number.
  return gain <= juce::Decibels::decibelsToGain(kQuietestLevelDb - 0.05f);
}

/// The decibel figure a readout shows for a gain, floored to what the
/// display can show.
inline float levelDecibels(float gain) noexcept {
  return juce::Decibels::gainToDecibels(gain, kQuietestLevelDb);
}

// The shape of a level fader's travel is levelRange in
// PluginParameters.cpp.

} // namespace ovt::params
