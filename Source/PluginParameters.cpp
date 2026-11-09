#include "PluginParameters.h"

#include "dsp/BusDrive.h"

#include "dsp/Harmonics.h"
#include "dsp/Keyboard.h"
#include "dsp/TapeEcho.h"
#include "dsp/Velocity.h"

#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace ovt::params {

namespace {
/// A range where equal turns of the knob are equal ratios, the natural
/// shape for a time control. setSkewForCentre leaves the bottom of such a wide
/// range dead. Needs a low end above zero.
///
/// The conversions work in double so every default survives the round trip
/// to a knob position, which auval checks.
juce::NormalisableRange<float> logRange(float lo, float hi) {
  jassert(lo > 0.0f);

  return {
      lo, hi,
      [](float a, float b, float t) {
        return (float)((double)a * std::pow((double)b / (double)a, (double)t));
      },
      [](float a, float b, float v) {
        const auto t =
            std::log((double)v / (double)a) / std::log((double)b / (double)a);

        return (float)juce::jlimit(0.0, 1.0, t);
      },
      [](float a, float b, float v) { return juce::jlimit(a, b, v); }};
}

/// A range that includes zero, growing exponentially from the low end so
/// the slope at the bottom is small but never zero.
///
/// @param centre  the value at half travel, which fixes the curvature. Must
///                sit below the midpoint of the range.
juce::NormalisableRange<float> expRange(float lo, float hi, float centre) {
  const auto frac = juce::jlimit(0.001f, 0.45f, (centre - lo) / (hi - lo));

  // Half travel lands on `centre` when exp(k/2) = 1/frac - 1.
  const auto k = 2.0f * std::log(1.0f / frac - 1.0f);
  const auto denom = std::exp(k) - 1.0f;

  return {lo, hi,
          [k, denom](float a, float b, float t) {
            return a + (b - a) * (std::exp(k * t) - 1.0f) / denom;
          },
          [k, denom](float a, float b, float v) {
            const auto y = (v - a) / (b - a) * denom + 1.0f;

            return juce::jlimit(0.0f, 1.0f, std::log(std::max(1.0e-9f, y)) / k);
          },
          [](float a, float b, float v) { return juce::jlimit(a, b, v); }};
}

/// A bipolar range with its fine end in the middle.
///
/// Squaring either side of centre puts a quarter of the range in half the
/// travel, for piano stretch near zero and bells far out.
juce::NormalisableRange<float> squaredBipolarRange(float extent) {
  return {-extent, extent,
          [](float lo, float hi, float t) {
            const auto x = 2.0f * t - 1.0f;
            return juce::jmap(x * std::abs(x), -1.0f, 1.0f, lo, hi);
          },
          [](float lo, float hi, float v) {
            const auto x = juce::jmap(v, lo, hi, -1.0f, 1.0f);
            return 0.5f * (std::copysign(std::sqrt(std::abs(x)), x) + 1.0f);
          },
          [](float lo, float hi, float v) { return juce::jlimit(lo, hi, v); }};
}

juce::String stretchText(float cents, int) {
  if (std::abs(cents) < 0.5f)
    return "Harmonic";

  return (cents > 0.0f ? "+" : "") + juce::String(juce::roundToInt(cents)) +
         " ct";
}

/// A glide time, where zero reads as off.
juce::String glideText(float seconds, int) {
  if (seconds < 0.0005f)
    return "Off";

  return seconds < 1.0f ? juce::String(seconds * 1000.0f, 0) + " ms"
                        : juce::String(seconds, 2) + " s";
}

juce::String trackText(float dbPerOctave, int) {
  if (dbPerOctave < 0.05f)
    return "Off";

  return juce::String(dbPerOctave, 1) + " dB/oct";
}

juce::String timeText(float seconds, int) {
  if (seconds < 1.0f)
    return juce::String(seconds * 1000.0f, seconds < 0.1f ? 1 : 0) + " ms";

  return juce::String(seconds, 2) + " s";
}

/// A partial's ATTACK: where in its cycle it starts below the shortest time,
/// in degrees, ninety being its peak, and the time above that.
juce::String attackText(float value, int) {
  if (value < 0.0f)
    return juce::String(juce::roundToInt(-value * 90.0f)) +
           juce::String::charToString(0xb0);

  return timeText(value, 0);
}

/// The other way, for a host that lets you type a value: degrees for a
/// start in the cycle, otherwise milliseconds unless it says seconds.
float attackValue(const juce::String &text) {
  const auto n = text.getFloatValue();

  if (text.containsChar(0xb0))
    return -juce::jlimit(0.0f, 1.0f, n / 90.0f);

  const auto t = text.trim().toLowerCase();
  const bool seconds = t.endsWith("s") && !t.endsWith("ms");

  return juce::jlimit(kShortestAttack, kMaxAttackSeconds,
                      seconds ? n : n / 1000.0f);
}

/// The ATTACK row's range, linear in octaves of onset with the phase region
/// at the bottom, the same scale STRIKE offsets along. See onsetOctaves.
juce::NormalisableRange<float> attackRange() {
  const double span = kOnsetPhaseOctaves;
  const double times = std::log2((double)kMaxAttackSeconds / kShortestAttack);

  return {-1.0f, kMaxAttackSeconds,
          [span, times](float, float, float n) {
            const double octaves = (double)n * (span + times) - span;
            return octaves < 0.0
                       ? (float)(octaves / span)
                       : (float)((double)kShortestAttack * std::exp2(octaves));
          },
          [span, times](float, float, float v) {
            return (float)((onsetOctaves(v) + span) / (span + times));
          },
          [](float lo, float hi, float v) { return juce::jlimit(lo, hi, v); }};
}

/// A modulator's rate: hundredths below 10 Hz and tenths above.
juce::String rateText(float hz, int) {
  return juce::String(hz, hz < 10.0f ? 2 : 1) + " Hz";
}

/// A depth or a drift in cents, with tenths below 10 cents.
juce::String centsText(float cents, int) {
  if (cents < 0.05f)
    return "0 ct";

  return juce::String(cents, cents < 10.0f ? 1 : 0) + " ct";
}

/// A macro's amount as a signed percentage to a tenth. Rounded before the
/// sign is chosen, so a value a hair either side of zero reads 0.0 %.
juce::String macroAmountText(float v, int) {
  const auto tenths = juce::roundToInt(v * 1000.0f);

  if (tenths == 0)
    return "0.0 %";

  const auto size = std::abs(tenths);
  return (tenths > 0 ? "+" : "-") + juce::String(size / 10) + "." +
         juce::String(size % 10) + " %";
}

/// The other way, so a typed "50" means half way.
float macroAmountValue(const juce::String &text) {
  return juce::jlimit(-1.0f, 1.0f, text.getFloatValue() / 100.0f);
}

juce::String percentText(float v, int) {
  return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

/// The echo age has a floor that keeps the two tape paths from wandering
/// identically, which would collapse the repeat to mono. The text hides it in
/// both directions, so the knob reads 0 to 100 %.
juce::String ageText(float v, int) {
  const auto span = 1.0f - TapeEcho::kMinAge;
  const auto shown = (v - TapeEcho::kMinAge) / span;

  return juce::String(juce::roundToInt(shown * 100.0f)) + " %";
}

float ageValue(const juce::String &text) {
  const auto span = 1.0f - TapeEcho::kMinAge;

  return TapeEcho::kMinAge +
         juce::jlimit(0.0f, 1.0f, text.getFloatValue() / 100.0f) * span;
}

/// Bipolar controls keep their sign, so the inverted half is unmistakable.
juce::String signedPercentText(float v, int) {
  const auto pc = juce::roundToInt(v * 100.0f);
  return (pc > 0 ? "+" : "") + juce::String(pc) + " %";
}

/// Pan reads as a side and a distance, the way a desk marks it.
juce::String panText(float v, int) {
  const auto amount = juce::roundToInt(std::abs(v) * 100.0f);

  if (amount == 0)
    return "Centre";

  return (v < 0.0f ? "L" : "R") + juce::String(amount);
}

juce::String gainText(float v, int) {
  if (isSilentGain(v))
    return "-inf dB";

  return juce::String(levelDecibels(v), 1) + " dB";
}

constexpr float kLevelShape = 2.0f;

/// The fader reaches a little below the quietest level a readout can name,
/// so that level can be set and not only read.
constexpr float kFaderFloorDb = kQuietestLevelDb - 2.1f;

/// How a level fader's travel maps to gain: a square law in decibels, so the
/// quiet end is reachable and the top still has most of the travel. The
/// stored value is a gain.
juce::NormalisableRange<float> levelRange() {
  return {0.0f, 1.0f,

          // Travel into gain.
          [](float, float, float norm) {
            // The bottom of the travel is silence.
            if (norm <= 0.0f)
              return 0.0f;

            // The floor is passed explicitly since the default of -100 dB is
            // above it and would make the bottom of the travel dead.
            return juce::Decibels::decibelsToGain(
                kFaderFloorDb * std::pow(1.0f - norm, kLevelShape),
                kFaderFloorDb);
          },

          // Gain back into travel.
          [](float, float, float gain) {
            if (gain <= 0.0f)
              return 0.0f;

            const auto db = juce::Decibels::gainToDecibels(gain, kFaderFloorDb);

            return juce::jlimit(
                0.0f, 1.0f,
                1.0f - std::pow(db / kFaderFloorDb, 1.0f / kLevelShape));
          }};
}

juce::String blendText(float v, int) {
  if (v <= 0.0005f)
    return "Equal temp.";
  if (v >= 0.9995f)
    return "Just";

  return juce::String(juce::roundToInt(v * 100.0f)) + "% just";
}

/// "H7 ", short enough to keep host parameter lists readable.
juce::String prefixFor(int index0) {
  return "H" + juce::String(index0 + 1) + " ";
}
} // namespace

juce::String oscParamId(const char *suffix, int index0) {
  return "h" + juce::String(index0 + 1).paddedLeft('0', 2) + "_" + suffix;
}

juce::String macroParamId(int macro, const char *suffix) {
  return "macro" + juce::String(macro + 1) + "_" + suffix;
}

juce::String macroAmountId(int macro) { return macroParamId(macro, "amount"); }

juce::String macroRowId(int macro) { return macroParamId(macro, "row"); }

juce::String macroScopeId(int macro) { return macroParamId(macro, "scope"); }

juce::String macroCurveId(int macro) { return macroParamId(macro, "curve"); }

juce::String macroColourId(int macro) { return macroParamId(macro, "colour"); }

juce::String macroAnchorId(int macro) { return macroParamId(macro, "anchor"); }

const char *macroScopeName(int scope) {
  switch ((MacroScope)scope) {
  case MacroScope::All:
    return "All";
  case MacroScope::Odd:
    return "Odd";
  case MacroScope::Even:
    return "Even";
  case MacroScope::Selected:
    return "Selected";

  case MacroScope::Interval:
    break;
  }

  // Everything past the three is an interval, named from intervalName and
  // capitalised. Built once, since this returns a pointer into it.
  if (scope >= (int)MacroScope::Interval && scope < (int)MacroScope::Selected) {
    static const auto capitalised = [] {
      std::array<juce::String, 12> out;

      for (int i = 0; i < 12; ++i) {
        out[(size_t)i] = juce::String(intervalName(i));
        out[(size_t)i] = out[(size_t)i].substring(0, 1).toUpperCase() +
                         out[(size_t)i].substring(1);
      }

      return out;
    }();

    return capitalised[(size_t)(scope - (int)MacroScope::Interval)].toRawUTF8();
  }

  return "All";
}

juce::String channelListText(std::uint32_t mask) {
  juce::StringArray numbers;

  for (int i = 0; i < kNumHarmonics; ++i)
    if (((mask >> i) & 1u) != 0)
      numbers.add(juce::String(i + 1));

  return numbers.joinIntoString(",");
}

std::uint32_t channelListMask(const juce::String &text) {
  std::uint32_t mask = 0;

  // Anything that is not a channel number is skipped.
  for (const auto &number : juce::StringArray::fromTokens(text, ",", "")) {
    const auto i = number.trim().getIntValue() - 1;

    if (i >= 0 && i < kNumHarmonics)
      mask |= 1u << i;
  }

  return mask;
}

/// Whether a macro reaches this channel.
bool macroReaches(int scope, int index0, std::uint32_t selected) {
  switch ((MacroScope)scope) {
  case MacroScope::Odd:
    return ((index0 + 1) % 2) == 1;
  case MacroScope::Even:
    return ((index0 + 1) % 2) == 0;

  case MacroScope::All:
    return true;

  case MacroScope::Selected:
    static_assert(kNumHarmonics <= 32, "a mask has a bit per channel");
    return index0 >= 0 && index0 < kNumHarmonics &&
           ((selected >> index0) & 1u) != 0;

  case MacroScope::Interval:
    break;
  }

  // Everything past the three is an interval, reaching every channel at
  // that pitch class.
  return harmonicTable()[(size_t)index0].pitchClass ==
         scope - (int)MacroScope::Interval;
}

/// This channel's share, from the fundamental outwards. A test holds Taper
/// together with ui::linkCurveWeight.
float macroWeight(MacroCurve curve, int index0, int anchor) {
  if (curve != MacroCurve::Taper)
    return 1.0f;

  // Measured against the full width of the mixer from the anchor channel,
  // since a macro has no strip under the mouse.
  const auto distance =
      (float)std::abs(juce::jlimit(0, kNumHarmonics - 1, index0) -
                      juce::jlimit(0, kNumHarmonics - 1, anchor)) /
      (float)(kNumHarmonics - 1);

  return 1.0f - distance;
}

juce::NormalisableRange<float>
macroRowRange(const juce::AudioProcessorValueTreeState &state, int row) {
  if (row >= 1 && row <= kNumMacroRows)
    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            state.getParameter(oscParamId(kMacroRows[(size_t)(row - 1)], 0))))
      return p->getNormalisableRange();

  return juce::NormalisableRange<float>(0.0f, 1.0f);
}

const char *macroColourName(int colour) {
  static const char *const names[] = {"None",   "Red",    "Orange",
                                      "Yellow", "Green",  "Cyan",
                                      "Blue",   "Violet", "Magenta"};

  static_assert((int)(sizeof(names) / sizeof(names[0])) == kNumMacroColours,
                "every colour a macro can wear has to have a name");

  return names[(size_t)juce::jlimit(0, kNumMacroColours - 1, colour)];
}

const char *macroCurveName(MacroCurve c) {
  switch (c) {
  case MacroCurve::Uniform:
    return "Uniform";
  case MacroCurve::Taper:
    return "Taper";

  case MacroCurve::NumCurves:
    break;
  }

  return "Uniform";
}

/// What a row is called in a macro's own menu. Spelt out here since the
/// panel's row labels repeat ("rate", "depth") once out of their headings.
const char *macroRowName(int row) {
  // None first, so this is one longer than the list of rows.
  static const char *const names[] = {"None",
                                      "Tune",
                                      "Glide",
                                      "Pitch mod rate",
                                      "Pitch mod depth",
                                      "Drift",
                                      "Strike",
                                      "Delay",
                                      "Attack",
                                      "Decay",
                                      "Sustain",
                                      "Key-off swell",
                                      "Key-off level",
                                      "Release",
                                      "Amp mod rate",
                                      "Amp mod depth",
                                      "Velocity",
                                      "Aftertouch",
                                      "Pan",
                                      "Level",
                                      "Keyboard"};

  static_assert((int)(sizeof(names) / sizeof(names[0])) == kNumMacroRows + 1,
                "every row a macro can drive has to have a name, and None too");

  return names[(size_t)juce::jlimit(0, kNumMacroRows, row)];
}

juce::String polyphonyName(int index) {
  if (index == kLegatoIndex)
    return "Legato";

  const auto at = juce::jlimit(0, (int)kPolyphonyChoices.size() - 1, index);
  const auto voices = kPolyphonyChoices[(size_t)at];

  return juce::String(voices) + (voices == 1 ? " voice" : " voices");
}

juce::String noiseParamId(const char *suffix) {
  return juce::String("noise_") + suffix;
}

namespace {
/// Every channel that carries a per-channel switch, in mixer order, noise
/// last.
std::array<juce::String, kNumHarmonics + 1> channelIds(const char *suffix) {
  std::array<juce::String, kNumHarmonics + 1> ids;

  for (int i = 0; i < kNumHarmonics; ++i)
    ids[(size_t)i] = oscParamId(suffix, i);

  ids[kNumHarmonics] = noiseParamId(suffix);
  return ids;
}
} // namespace

int channelsSwitchedOn(juce::AudioProcessorValueTreeState &apvts,
                       const char *suffix) {
  int on = 0;

  for (const auto &id : channelIds(suffix))
    if (auto *value = apvts.getRawParameterValue(id))
      on += value->load() > 0.5f ? 1 : 0;

  return on;
}

namespace {
/// Sets the switch on every channel, on for `onlyId` and off for the rest, or
/// off everywhere when it is empty.
void setChannelSwitches(juce::AudioProcessorValueTreeState &apvts,
                        const char *suffix, const juce::String &onlyId) {
  std::vector<std::pair<juce::RangedAudioParameter *, float>> moving;

  for (const auto &id : channelIds(suffix)) {
    auto *param = apvts.getParameter(id);

    if (param == nullptr)
      continue;

    const auto on = id == onlyId;

    // Only what changes, so no gesture or empty undo step is made for the rest.
    if ((param->getValue() > 0.5f) != on)
      moving.emplace_back(param, on ? 1.0f : 0.0f);
  }

  moveAsOneStep(moving);
}
} // namespace

void moveAsOneStep(const std::vector<Move> &moves) {
  for (const auto &move : moves)
    move.first->beginChangeGesture();

  for (const auto &move : moves)
    move.first->setValueNotifyingHost(move.second);

  for (const auto &move : moves)
    move.first->endChangeGesture();
}

void setAsOneStep(juce::RangedAudioParameter *param, float normalised) {
  if (param != nullptr)
    moveAsOneStep({{param, normalised}});
}

void clearChannelSwitch(juce::AudioProcessorValueTreeState &apvts,
                        const char *suffix) {
  setChannelSwitches(apvts, suffix, {});
}

void switchVelocityCurve(juce::AudioProcessorValueTreeState &apvts) {
  auto *curve = apvts.getParameter(velocityCurveId);

  if (curve == nullptr)
    return;

  const auto toDecibels = curve->getValue() < 0.5f;

  // Normalised values, so each parameter is set as it is stored.
  std::vector<std::pair<juce::RangedAudioParameter *, float>> moving;

  for (const auto &id : channelIds(velSuffix)) {
    auto *param = apvts.getParameter(id);

    if (param == nullptr)
      continue;

    const auto amount = param->convertFrom0to1(param->getValue());
    const auto converted =
        toDecibels ? decibelsAmountFor(amount) : classicAmountFor(amount);
    const auto stored = param->convertTo0to1(converted);

    // Only what changes, so a channel at zero gets no gesture.
    if (std::abs(stored - param->getValue()) > 1.0e-7f)
      moving.emplace_back(param, stored);
  }

  moving.emplace_back(curve, toDecibels ? 1.0f : 0.0f);

  // The switch and the amounts together, as one step.
  moveAsOneStep(moving);
}

void switchOnlyChannel(juce::AudioProcessorValueTreeState &apvts,
                       const char *suffix, const juce::String &onlyId) {
  setChannelSwitches(apvts, suffix, onlyId);
}

float defaultVolumeFor(int index0) {
  // 1/n over the first eight partials, silence above.
  if (index0 >= 8)
    return 0.0f;

  return 1.0f / (float)(index0 + 1);
}

namespace {
/// One name per LfoShape, in the same order.
const char *const kShapeNames[kNumLfoShapes] = {
    "Sine",   "Triangle",    "Sawtooth",      "Reverse Sawtooth",
    "Square", "Square (up)", "Sample & Hold", "Random"};

template <size_t N>
juce::StringArray namesOf(const std::array<LfoShape, N> &shapes) {
  juce::StringArray out;

  for (auto shape : shapes)
    out.add(kShapeNames[(int)shape]);

  return out;
}
} // namespace

juce::StringArray pitchShapeNames() { return namesOf(kPitchShapes); }
juce::StringArray ampShapeNames() { return namesOf(kAmpShapes); }

namespace {
/// The shape a stored index means, clamped so an index from a later build
/// stays in range.
LfoShape pitchShapeAt(int index) {
  return kPitchShapes[(size_t)juce::jlimit(0, (int)kPitchShapes.size() - 1,
                                           index)];
}

LfoShape ampShapeAt(int index) {
  return kAmpShapes[(size_t)juce::jlimit(0, (int)kAmpShapes.size() - 1, index)];
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
  using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
  using FloatP = juce::AudioParameterFloat;
  using BoolP = juce::AudioParameterBool;
  using FAttr = juce::AudioParameterFloatAttributes;

  Layout layout;

  // ---- global ---------------------------------------------------------------
  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{masterGainId, 1}, "Master",
      juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), -12.0f,
      FAttr().withLabel("dB")));

  juce::StringArray polyChoices;
  for (int i = 0; i < (int)kPolyphonyChoices.size(); ++i)
    polyChoices.add(polyphonyName(i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{polyphonyId, 1}, "Polyphony", polyChoices,
      5)); // 8 voices

  // On by default, so a repeatedly tapped key with a long release does not
  // stack every tap.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{oneVoicePerKeyId, 1},
                                     "No Stack", true));

  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{bendRangeId, 1}, "Pitch Bend Range", 0, 24, 2));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{phaseResetId, 1},
                                     "Phase Reset", true));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{safetyClipId, 1},
                                     "Safety Clip", true));

  // Off by default. See params::lookaheadId.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{lookaheadId, 1},
                                     "Lookahead", false));

  // ---- the macros ---------------------------------------------------------
  //
  // The amount is what a host draws. Row, scope, curve, anchor and colour are
  // parameters too, so a preset can define what its macros do.
  for (int m = 0; m < kNumMacros; ++m) {
    const auto name = "Macro " + juce::String(m + 1);

    // Bipolar and centred, so a macro can push either way.
    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{macroAmountId(m), 1}, name,
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f), 0.0f,
        FAttr()
            .withStringFromValueFunction(macroAmountText)
            .withValueFromStringFunction(macroAmountValue)));

    // None first, so a fresh instrument has no macros made.
    juce::StringArray rows;
    for (int r = 0; r <= kNumMacroRows; ++r)
      rows.add(macroRowName(r));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{macroRowId(m), 1}, name + " Row", rows, 0));

    juce::StringArray scopes;
    for (int i = 0; i < kNumMacroScopes; ++i)
      scopes.add(macroScopeName(i));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{macroScopeId(m), 1}, name + " Scope", scopes, 0));

    juce::StringArray curves;
    for (int i = 0; i < (int)MacroCurve::NumCurves; ++i)
      curves.add(macroCurveName((MacroCurve)i));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{macroCurveId(m), 1}, name + " Curve", curves, 0));

    // Visual only, but a parameter so a preset carries it.
    juce::StringArray colours;
    for (int i = 0; i < kNumMacroColours; ++i)
      colours.add(macroColourName(i));

    // Which channel a taper leans on hardest, numbered from 1 as the mixer
    // shows it.
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{macroAnchorId(m), 1}, name + " Anchor", 1,
        kNumHarmonics, 1));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{macroColourId(m), 1}, name + " Colour", colours,
        // Each macro starts with a different colour.
        1 + m % (kNumMacroColours - 1)));
  }

  // Off by default, since it changes what a MIDI channel means. See
  // OvertoniumProcessor::handleMidiMessage.
  layout.add(
      std::make_unique<BoolP>(juce::ParameterID{mpeId, 1}, "MPE", false));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{stretchId, 1}, "Stretch", squaredBipolarRange(1200.0f),
      0.0f, FAttr().withStringFromValueFunction(stretchText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{trackId, 1}, "Tracking",
      juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 0.0f,
      FAttr().withStringFromValueFunction(trackText)));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{characterId, 1}, "Character", characterChoices(),
      (int)Character::Pure));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{wobbleId, 1}, "Wobble",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  juce::StringArray temperamentChoices;
  for (int i = 0; i < (int)Temperament::NumTemperaments; ++i)
    temperamentChoices.add(temperamentName((Temperament)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{temperamentId, 1}, "Temperament", temperamentChoices,
      (int)Temperament::Equal));

  juce::StringArray rootChoices;
  for (auto *name : kPitchClassNames)
    rootChoices.add(name);

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{tuningRootId, 1}, "Tuning Root", rootChoices, 0));

  juce::StringArray referenceChoices;
  for (auto hz : kReferenceHzChoices)
    referenceChoices.add("A = " + juce::String(hz) + " Hz");

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{referenceHzId, 1}, "Reference Pitch", referenceChoices,
      5)); // 440

  juce::StringArray atSourceChoices;
  for (auto *name : kAftertouchSourceNames)
    atSourceChoices.add(name);

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{atSourceId, 1}, "Aftertouch From", atSourceChoices,
      (int)AftertouchSource::Either));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{slideDestId, 1}, "Slide To", slideDestChoices,
      (int)SlideDestination::Brightness));

  juce::StringArray rateChoices;
  for (auto hz : kLofiRateChoices)
    rateChoices.add(lofiRateName(hz));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{lofiRateId, 1}, "Sample Rate", rateChoices, 0));

  juce::StringArray bitChoices;
  for (auto bits : kLofiBitChoices)
    bitChoices.add(lofiBitName(bits));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{lofiBitsId, 1}, "Bit Depth", bitChoices, 0));

  // ---- master effects -------------------------------------------------------
  layout.add(
      std::make_unique<BoolP>(juce::ParameterID{echoOnId, 1}, "Echo", false));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{echoMixId, 1}, "Echo Mix",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{echoTimeId, 1}, "Echo Time", logRange(0.02f, 2.0f),
      0.35f, FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{echoFeedbackId, 1}, "Echo Feedback",
      juce::NormalisableRange<float>(0.0f, 0.95f), 0.35f,
      FAttr().withStringFromValueFunction(percentText)));

  // Never quite new: see TapeEcho::kMinAge, and ageText for how the knob
  // reads.
  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{echoAgeId, 1}, "Echo Age",
      juce::NormalisableRange<float>(TapeEcho::kMinAge, 1.0f), 0.35f,
      FAttr().withStringFromValueFunction(ageText).withValueFromStringFunction(
          ageValue)));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{reverbOnId, 1}, "Reverb",
                                     false));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{reverbMixId, 1}, "Reverb Mix",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(
      std::make_unique<FloatP>(juce::ParameterID{reverbDecayId, 1},
                               "Reverb Decay", logRange(0.2f, 20.0f), 2.0f,
                               FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{reverbDampId, 1}, "Reverb Damping",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{reverbPreDelayId, 1}, "Reverb Pre-delay",
      expRange(0.0f, 0.25f, 0.05f), 0.0f,
      FAttr().withStringFromValueFunction(timeText)));

  // ---- per partial ----------------------------------------------------------
  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto p = prefixFor(i);

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(tuneSuffix, i), 1}, p + "Tune",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        1.0f, // the pure harmonic series is the natural home position
        FAttr().withStringFromValueFunction(blendText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(pmRateSuffix, i), 1}, p + "Pitch Mod Rate",
        logRange(0.01f, 30.0f), 4.0f,
        FAttr().withStringFromValueFunction(rateText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(pmDepthSuffix, i), 1},
        p + "Pitch Mod Depth", expRange(0.0f, kMaxPitchModCents, 25.0f), 0.0f,
        FAttr().withStringFromValueFunction(centsText)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{oscParamId(pmShapeSuffix, i), 1},
        p + "Pitch Mod Shape", pitchShapeNames(), 0));

    // How long this partial takes to reach a new note, or how long an octave
    // takes under Rate. Off by default. Declared where the removed start phase
    // was, so every other parameter keeps its index.
    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(glideSuffix, i), 1}, p + "Glide",
        expRange(0.0f, kMaxGlideSeconds, 0.4f), 0.0f,
        FAttr().withStringFromValueFunction(glideText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(driftSuffix, i), 1}, p + "Drift",
        expRange(0.0f, kMaxDriftCents, 6.0f), 0.0f,
        FAttr().withStringFromValueFunction(centsText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(strikeSuffix, i), 1},
        p + "Strike Velocity", juce::NormalisableRange<float>(-1.0f, 1.0f),
        0.0f, FAttr().withStringFromValueFunction(signedPercentText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(delaySuffix, i), 1}, p + "Delay",
        expRange(0.0f, 5.0f, 0.2f), 0.0f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(attackSuffix, i), 1}, p + "Attack",
        attackRange(), 0.005f,
        FAttr()
            .withStringFromValueFunction(attackText)
            .withValueFromStringFunction(attackValue)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(decaySuffix, i), 1}, p + "Decay",
        logRange(0.001f, 20.0f), 0.6f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(sustainSuffix, i), 1}, p + "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        FAttr().withStringFromValueFunction(percentText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(swellSuffix, i), 1}, p + "Key-off Swell",
        expRange(0.0f, 5.0f, 0.1f), 0.005f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(offLevelSuffix, i), 1},
        p + "Key-off Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(percentText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(releaseSuffix, i), 1}, p + "Release",
        logRange(0.001f, 20.0f), 0.4f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(amRateSuffix, i), 1}, p + "Amp Mod Rate",
        logRange(0.01f, 30.0f), 4.0f,
        FAttr().withStringFromValueFunction(rateText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(amDepthSuffix, i), 1}, p + "Amp Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(percentText)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{oscParamId(amShapeSuffix, i), 1}, p + "Amp Mod Shape",
        ampShapeNames(), 0));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(velSuffix, i), 1}, p + "Velocity",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(signedPercentText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(atSuffix, i), 1}, p + "Aftertouch",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(signedPercentText)));

    layout.add(std::make_unique<BoolP>(
        juce::ParameterID{oscParamId(muteSuffix, i), 1}, p + "Mute", false));

    layout.add(std::make_unique<BoolP>(
        juce::ParameterID{oscParamId(soloSuffix, i), 1}, p + "Solo", false));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(panSuffix, i), 1}, p + "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(panText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(volumeSuffix, i), 1}, p + "Level",
        levelRange(), defaultVolumeFor(i),
        FAttr().withStringFromValueFunction(gainText)));
  }

  // ---- noise channel --------------------------------------------------------
  // Same controls as a strip, minus pitch, plus colour.
  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(colourSuffix), 1}, "Noise Colour",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(strikeSuffix), 1}, "Noise Strike Velocity",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(signedPercentText)));

  layout.add(
      std::make_unique<FloatP>(juce::ParameterID{noiseParamId(delaySuffix), 1},
                               "Noise Delay", expRange(0.0f, 5.0f, 0.2f), 0.0f,
                               FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(attackSuffix), 1}, "Noise Attack",
      logRange(0.0002f, kMaxAttackSeconds), 0.005f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(
      std::make_unique<FloatP>(juce::ParameterID{noiseParamId(decaySuffix), 1},
                               "Noise Decay", logRange(0.001f, 20.0f), 0.6f,
                               FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(sustainSuffix), 1}, "Noise Sustain",
      juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(swellSuffix), 1}, "Noise Key-off Swell",
      expRange(0.0f, 5.0f, 0.1f), 0.005f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(offLevelSuffix), 1}, "Noise Key-off Level",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(releaseSuffix), 1}, "Noise Release",
      logRange(0.001f, 20.0f), 0.4f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(amRateSuffix), 1}, "Noise Amp Mod Rate",
      logRange(0.01f, 30.0f), 4.0f,
      FAttr().withStringFromValueFunction(rateText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(amDepthSuffix), 1}, "Noise Amp Mod Depth",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{noiseParamId(amShapeSuffix), 1}, "Noise Amp Mod Shape",
      ampShapeNames(), 0));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(velSuffix), 1}, "Noise Velocity",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(signedPercentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(atSuffix), 1}, "Noise Aftertouch",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(signedPercentText)));

  layout.add(std::make_unique<BoolP>(
      juce::ParameterID{noiseParamId(muteSuffix), 1}, "Noise Mute", false));

  layout.add(std::make_unique<BoolP>(
      juce::ParameterID{noiseParamId(soloSuffix), 1}, "Noise Solo", false));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(panSuffix), 1}, "Noise Pan",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(panText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(volumeSuffix), 1}, "Noise Level",
      levelRange(), 0.0f, FAttr().withStringFromValueFunction(gainText)));

  // ---- two switches over all of the above -----------------------------------
  //
  // Whether a channel's modulator is shared by the whole keyboard or runs per
  // note. From here on every parameter is appended at the end, so automation
  // on hosts that go by position keeps pointing at the same parameters. The
  // ids keep the old "in phase" wording, since hosts and sessions find a
  // parameter by id.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{pmInPhaseId, 1},
                                     "Pitch Mod Shared", false));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{amInPhaseId, 1},
                                     "Amp Mod Shared", false));

  // On the end. Every type is listed now, since a host stores a choice as a
  // fraction of the list, so a list that grew would move every lane.
  juce::StringArray echoTypeChoices;
  for (int i = 0; i < (int)EchoType::NumTypes; ++i)
    echoTypeChoices.add(echoTypeName((EchoType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{echoTypeId, 1}, "Echo Type", echoTypeChoices,
      (int)EchoType::Tape));

  juce::StringArray clipTypeChoices;
  for (int i = 0; i < (int)dm::ClipType::NumTypes; ++i)
    clipTypeChoices.add(dm::clipTypeName((dm::ClipType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{clipTypeId, 1}, "Clip Type", clipTypeChoices,
      (int)dm::ClipType::Soft));

  juce::StringArray reverbTypeChoices;
  for (int i = 0; i < (int)ReverbType::NumTypes; ++i)
    reverbTypeChoices.add(reverbTypeName((ReverbType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{reverbTypeId, 1}, "Reverb Type", reverbTypeChoices,
      (int)ReverbType::Room));

  // When a note glides and what the glide times mean. On the end. Part of
  // the patch. Two entries each, fixed, since a choice's length is part of
  // what a stored value means.
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{glideTriggerId, 1}, "Glide Trigger",
      juce::StringArray{glideTriggerName(GlideTrigger::Always),
                        glideTriggerName(GlideTrigger::Legato)},
      (int)GlideTrigger::Always));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{glideModeId, 1}, "Glide Mode",
      juce::StringArray{glideModeName(GlideMode::Rate),
                        glideModeName(GlideMode::Time)},
      (int)GlideMode::Rate));

  // Legato as a switch beside the voice count, on the end. Off by default.
  // A session setting.
  layout.add(
      std::make_unique<BoolP>(juce::ParameterID{legatoId, 1}, "Legato", false));

  // How long the adaptive temperament takes to move a held note, on the end.
  // A session setting. Curved like the glide, from zero, where a note jumps.
  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{adaptiveRetuneId, 1}, "Adaptive Retune",
      expRange(0.0f, kMaxAdaptiveRetuneSeconds, 0.12f),
      (float)kAdaptiveRetuneSeconds[0],
      FAttr().withStringFromValueFunction(timeText)));

  // Whether Adaptive uses the harmonic seventh, on by default, on the end.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{adaptiveSeventhId, 1},
                                     "Adaptive Harmonic Sevenths", true));

  // Whether Adaptive stays home rather than drifting, off by default.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{adaptiveHomeId, 1},
                                     "Adaptive No Drift", false));

  // The string the noise channel strikes, on the end. STRING at zero is the
  // channel without one. Part of the patch.
  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(stringSuffix), 1}, "Noise String",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(stringDecaySuffix), 1},
      "Noise String Decay", logRange(0.05f, kMaxStringDecaySeconds),
      kStringDecayDefault, FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(stringDampSuffix), 1}, "Noise String Damp",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(stringPointSuffix), 1},
      "Noise String Point", juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f,
      FAttr().withStringFromValueFunction(percentText)));

  // The resonator, on the end. Off by default. Part of the patch, with its
  // mode list declared in full as the reverb's is.
  juce::StringArray resonatorModeChoices;
  for (int i = 0; i < (int)ResonatorMode::NumModes; ++i)
    resonatorModeChoices.add(resonatorModeName((ResonatorMode)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{resonatorModeId, 1}, "Resonator Mode",
      resonatorModeChoices, (int)ResonatorMode::Off));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{resonatorMixId, 1}, "Resonator Mix",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{resonatorDecayId, 1}, "Resonator Decay",
      logRange(0.2f, kMaxResonatorDecaySeconds), kResonatorDecayDefault,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{resonatorDampId, 1}, "Resonator Damp",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f,
      FAttr().withStringFromValueFunction(percentText)));

  // The velocity curve, on the end. Part of the patch. dB for anything new,
  // and Classic for whatever was saved before it existed, see
  // valueInOlderFiles. Two entries, fixed, since a choice's length is part of
  // what a stored value means.
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{velocityCurveId, 1}, "Velocity Curve",
      juce::StringArray{"Classic", "dB"}, (int)VelocityCurve::Decibels));

  // The KEYBOARD rows, on the end so existing automation keeps its place.
  // Linear in the amount, since the slope's own curve is in keyboardTilt, and
  // the readout names the slope and the centre.
  const auto keyboardAttributes =
      FAttr()
          .withStringFromValueFunction(keyboardText)
          .withValueFromStringFunction(keyboardValue);

  for (int i = 0; i < kNumHarmonics; ++i)
    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(keySuffix, i), 1},
        prefixFor(i) + "Keyboard",
        juce::NormalisableRange<float>(-kKeyboardMaxAmount, kKeyboardMaxAmount),
        0.0f, keyboardAttributes));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(keySuffix), 1}, "Noise Keyboard",
      juce::NormalisableRange<float>(-kKeyboardMaxAmount, kKeyboardMaxAmount),
      0.0f, keyboardAttributes));

  return layout;
}

std::optional<float> valueInOlderFiles(juce::StringRef id) {
  if (id == juce::StringRef(velocityCurveId))
    return (float)VelocityCurve::Classic;

  if (juce::String(id).endsWith(juce::String("_") + keySuffix))
    return 0.0f;

  return std::nullopt;
}

namespace {
/// Every parameter on a channel strip: its id's suffix, where the cache keeps
/// it, and the field of the snapshot it goes to as a plain number. The shapes,
/// MUTE and SOLO have no such field, since snapshot turns them into something
/// else, and are read by name.
struct OscRow {
  const char *suffix;
  std::atomic<float> *Cache::Osc::*cached;
  float OscParams::*field;
};

constexpr OscRow kOscRows[] = {
    {tuneSuffix, &Cache::Osc::tune, &OscParams::tuneBlend},
    {glideSuffix, &Cache::Osc::glide, &OscParams::glideSeconds},
    {pmRateSuffix, &Cache::Osc::pmRate, &OscParams::pmRateHz},
    {pmDepthSuffix, &Cache::Osc::pmDepth, &OscParams::pmDepthCents},
    {pmShapeSuffix, &Cache::Osc::pmShape, nullptr},
    {driftSuffix, &Cache::Osc::drift, &OscParams::driftCents},
    {strikeSuffix, &Cache::Osc::strike, &OscParams::strikeAmount},
    {delaySuffix, &Cache::Osc::delay, &OscParams::delay},
    {attackSuffix, &Cache::Osc::attack, &OscParams::attack},
    {decaySuffix, &Cache::Osc::decay, &OscParams::decay},
    {sustainSuffix, &Cache::Osc::sustain, &OscParams::sustain},
    {swellSuffix, &Cache::Osc::swell, &OscParams::swell},
    {offLevelSuffix, &Cache::Osc::offLevel, &OscParams::offLevel},
    {releaseSuffix, &Cache::Osc::release, &OscParams::release},
    {amRateSuffix, &Cache::Osc::amRate, &OscParams::amRateHz},
    {amDepthSuffix, &Cache::Osc::amDepth, &OscParams::amDepth},
    {amShapeSuffix, &Cache::Osc::amShape, nullptr},
    {velSuffix, &Cache::Osc::vel, &OscParams::velAmount},
    {atSuffix, &Cache::Osc::at, &OscParams::atAmount},
    {keySuffix, &Cache::Osc::key, &OscParams::keyAmount},
    {muteSuffix, &Cache::Osc::mute, nullptr},
    {soloSuffix, &Cache::Osc::solo, nullptr},
    {volumeSuffix, &Cache::Osc::volume, &OscParams::volume},
    {panSuffix, &Cache::Osc::pan, &OscParams::pan},
};

/// The same for the noise channel.
struct NoiseRow {
  const char *suffix;
  std::atomic<float> *Cache::NoiseChannel::*cached;
  float NoiseParams::*field;
};

constexpr NoiseRow kNoiseRows[] = {
    {colourSuffix, &Cache::NoiseChannel::colour, &NoiseParams::colour},
    {strikeSuffix, &Cache::NoiseChannel::strike, &NoiseParams::strikeAmount},
    {delaySuffix, &Cache::NoiseChannel::delay, &NoiseParams::delay},
    {attackSuffix, &Cache::NoiseChannel::attack, &NoiseParams::attack},
    {decaySuffix, &Cache::NoiseChannel::decay, &NoiseParams::decay},
    {sustainSuffix, &Cache::NoiseChannel::sustain, &NoiseParams::sustain},
    {swellSuffix, &Cache::NoiseChannel::swell, &NoiseParams::swell},
    {offLevelSuffix, &Cache::NoiseChannel::offLevel, &NoiseParams::offLevel},
    {releaseSuffix, &Cache::NoiseChannel::release, &NoiseParams::release},
    {amRateSuffix, &Cache::NoiseChannel::amRate, &NoiseParams::amRateHz},
    {amDepthSuffix, &Cache::NoiseChannel::amDepth, &NoiseParams::amDepth},
    {amShapeSuffix, &Cache::NoiseChannel::amShape, nullptr},
    {velSuffix, &Cache::NoiseChannel::vel, &NoiseParams::velAmount},
    {atSuffix, &Cache::NoiseChannel::at, &NoiseParams::atAmount},
    {keySuffix, &Cache::NoiseChannel::key, &NoiseParams::keyAmount},
    {muteSuffix, &Cache::NoiseChannel::mute, nullptr},
    {soloSuffix, &Cache::NoiseChannel::solo, nullptr},
    {volumeSuffix, &Cache::NoiseChannel::volume, &NoiseParams::volume},
    {panSuffix, &Cache::NoiseChannel::pan, &NoiseParams::pan},
    {stringSuffix, &Cache::NoiseChannel::string, &NoiseParams::stringAmount},
    {stringDecaySuffix, &Cache::NoiseChannel::stringDecay,
     &NoiseParams::stringDecay},
    {stringDampSuffix, &Cache::NoiseChannel::stringDamp,
     &NoiseParams::stringDamp},
    {stringPointSuffix, &Cache::NoiseChannel::stringPoint,
     &NoiseParams::stringPoint},
};

// Every pointer the cache keeps for a channel is in its table.
static_assert(std::size(kOscRows) ==
                  sizeof(Cache::Osc) / sizeof(std::atomic<float> *),
              "a channel parameter is missing from kOscRows");
static_assert(std::size(kNoiseRows) ==
                  sizeof(Cache::NoiseChannel) / sizeof(std::atomic<float> *),
              "a noise parameter is missing from kNoiseRows");
} // namespace

void Cache::connect(juce::AudioProcessorValueTreeState &apvts) {
  masterGain = apvts.getRawParameterValue(masterGainId);
  polyphony = apvts.getRawParameterValue(polyphonyId);
  oneVoicePerKey = apvts.getRawParameterValue(oneVoicePerKeyId);
  pmInPhase = apvts.getRawParameterValue(pmInPhaseId);
  amInPhase = apvts.getRawParameterValue(amInPhaseId);
  glideTrigger = apvts.getRawParameterValue(glideTriggerId);
  glideMode = apvts.getRawParameterValue(glideModeId);
  velocityCurve = apvts.getRawParameterValue(velocityCurveId);
  legato = apvts.getRawParameterValue(legatoId);
  adaptiveRetune = apvts.getRawParameterValue(adaptiveRetuneId);
  adaptiveSeventh = apvts.getRawParameterValue(adaptiveSeventhId);
  adaptiveHome = apvts.getRawParameterValue(adaptiveHomeId);
  bendRange = apvts.getRawParameterValue(bendRangeId);
  phaseReset = apvts.getRawParameterValue(phaseResetId);
  stretch = apvts.getRawParameterValue(stretchId);
  atSource = apvts.getRawParameterValue(atSourceId);
  slideDest = apvts.getRawParameterValue(slideDestId);
  track = apvts.getRawParameterValue(trackId);
  character = apvts.getRawParameterValue(characterId);
  wobble = apvts.getRawParameterValue(wobbleId);
  temperament = apvts.getRawParameterValue(temperamentId);
  tuningRoot = apvts.getRawParameterValue(tuningRootId);
  referenceHz = apvts.getRawParameterValue(referenceHzId);
  safetyClip = apvts.getRawParameterValue(safetyClipId);
  lookahead = apvts.getRawParameterValue(lookaheadId);

  for (int m = 0; m < kNumMacros; ++m) {
    auto &cached = macro[(size_t)m];

    cached.amount = apvts.getRawParameterValue(macroAmountId(m));
    cached.row = apvts.getRawParameterValue(macroRowId(m));
    cached.scope = apvts.getRawParameterValue(macroScopeId(m));
    cached.curve = apvts.getRawParameterValue(macroCurveId(m));
    cached.anchor = apvts.getRawParameterValue(macroAnchorId(m));

    jassert(cached.amount != nullptr && cached.row != nullptr &&
            cached.scope != nullptr && cached.curve != nullptr &&
            cached.anchor != nullptr);
  }

  // Every pointer comes from the layout, so a missing one is a typo in an id,
  // and snapshot reads them without checking.
  jassert(masterGain != nullptr && polyphony != nullptr &&
          oneVoicePerKey != nullptr && pmInPhase != nullptr &&
          amInPhase != nullptr && glideTrigger != nullptr &&
          glideMode != nullptr && velocityCurve != nullptr &&
          legato != nullptr && adaptiveRetune != nullptr &&
          adaptiveSeventh != nullptr && adaptiveHome != nullptr &&
          bendRange != nullptr && phaseReset != nullptr && stretch != nullptr &&
          atSource != nullptr && slideDest != nullptr && track != nullptr &&
          character != nullptr && wobble != nullptr && temperament != nullptr &&
          tuningRoot != nullptr && referenceHz != nullptr &&
          safetyClip != nullptr && lookahead != nullptr);

  // Read once here rather than per block, since a row's range cannot change.
  for (int r = 0; r < kNumMacroRows; ++r) {
    auto &range = rowRange[(size_t)r];
    range = juce::NormalisableRange<float>(0.0f, 1.0f);

    // The whole range, not its two ends, since logRange keeps its curve in the
    // conversion functions and a range rebuilt from its ends would be linear.
    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            apvts.getParameter(oscParamId(kMacroRows[(size_t)r], 0))))
      range = p->getNormalisableRange();
  }
  mpe = apvts.getRawParameterValue(mpeId);
  lofiRate = apvts.getRawParameterValue(lofiRateId);
  lofiBits = apvts.getRawParameterValue(lofiBitsId);

  echo.on = apvts.getRawParameterValue(echoOnId);
  clipType = apvts.getRawParameterValue(clipTypeId);
  echo.type = apvts.getRawParameterValue(echoTypeId);
  echo.mix = apvts.getRawParameterValue(echoMixId);
  echo.time = apvts.getRawParameterValue(echoTimeId);
  echo.feedback = apvts.getRawParameterValue(echoFeedbackId);
  echo.age = apvts.getRawParameterValue(echoAgeId);

  reverb.on = apvts.getRawParameterValue(reverbOnId);
  reverb.type = apvts.getRawParameterValue(reverbTypeId);
  reverb.mix = apvts.getRawParameterValue(reverbMixId);
  reverb.decay = apvts.getRawParameterValue(reverbDecayId);
  reverb.damp = apvts.getRawParameterValue(reverbDampId);
  reverb.preDelay = apvts.getRawParameterValue(reverbPreDelayId);

  resonator.mode = apvts.getRawParameterValue(resonatorModeId);
  resonator.mix = apvts.getRawParameterValue(resonatorMixId);
  resonator.decay = apvts.getRawParameterValue(resonatorDecayId);
  resonator.damp = apvts.getRawParameterValue(resonatorDampId);

  jassert(mpe != nullptr && lofiRate != nullptr && lofiBits != nullptr &&
          clipType != nullptr && echo.on != nullptr && echo.type != nullptr &&
          reverb.on != nullptr && reverb.type != nullptr &&
          resonator.mode != nullptr && resonator.damp != nullptr);

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &o = osc[(size_t)i];

    for (const auto &row : kOscRows) {
      o.*row.cached = apvts.getRawParameterValue(oscParamId(row.suffix, i));
      jassert(o.*row.cached != nullptr);
    }
  }

  for (const auto &row : kNoiseRows) {
    noise.*row.cached = apvts.getRawParameterValue(noiseParamId(row.suffix));
    jassert(noise.*row.cached != nullptr);
  }
}

namespace {
/// A MIDI key's name, middle C (60) being C4.
juce::String keyName(int key) {
  const auto k = juce::jlimit(0, 127, key);

  return juce::String(kPitchClassNames[(size_t)(k % 12)]) +
         juce::String(k / 12 - 1);
}
} // namespace

juce::String keyboardText(float amount, int) {
  const auto tilt = keyboardTilt(amount);

  // Shown as zero by the slope's own rounding, so the knob does not claim a
  // side while it does nothing.
  if (tilt.dbPerOctave < 0.05f)
    return "0 dB/oct";

  const auto slope = tilt.dbPerOctave < 9.95f
                         ? juce::String(tilt.dbPerOctave, 1)
                         : juce::String(juce::roundToInt(tilt.dbPerOctave));

  return slope + " dB/oct " + (tilt.below ? "below " : "above ") +
         keyName(juce::roundToInt(tilt.centre));
}

namespace {
/// A key name such as "F#2" or "C-1" at the start of `text`, or -1.
int keyFromName(const juce::String &text) {
  const auto t = text.trim().toUpperCase();

  if (t.isEmpty())
    return -1;

  static const int letters[] = {9, 11, 0, 2, 4, 5, 7}; // A to G
  const auto letter = t[0];

  if (letter < 'A' || letter > 'G')
    return -1;

  int pitchClass = letters[letter - 'A'];
  auto rest = t.substring(1);

  if (rest.startsWithChar('#')) {
    ++pitchClass;
    rest = rest.substring(1);
  }

  if (!rest.containsOnly("-0123456789") || !rest.containsAnyOf("0123456789"))
    return -1;

  const auto key = (rest.getIntValue() + 1) * 12 + pitchClass;
  return key >= 0 && key <= 127 ? key : -1;
}
} // namespace

float keyboardValue(const juce::String &text) {
  const auto t = text.trim().toLowerCase();
  const auto n = t.getFloatValue();

  if (t.containsChar('%'))
    return juce::jlimit(-kKeyboardMaxAmount, kKeyboardMaxAmount, n / 100.0f);

  const bool above = t.contains("above");
  const auto sign = above ? -1.0f : 1.0f;

  // A centre away from middle C on the side the slope turns down is past full
  // amount. On the other side there is no such setting, so only the slope is
  // read.
  const auto centre = keyFromName(
      t.fromLastOccurrenceOf(above ? "above" : "below", false, false));
  const auto away = (float)centre - kKeyboardCentreKey;

  if (centre >= 0 && away * sign > 0.0f)
    return juce::jlimit(-kKeyboardMaxAmount, kKeyboardMaxAmount,
                        sign *
                            (1.0f + std::abs(away) / kKeyboardKeysPerAmount));

  const auto slope = juce::jlimit(0.0f, kKeyboardMaxSlopeDb, std::abs(n));
  return sign * std::sqrt(slope / kKeyboardMaxSlopeDb);
}

juce::String strikeRangeText(float amount, float delay, float attack) {
  const auto shown = signedPercentText(amount, 0);

  // The same threshold the percentage rounds by.
  if (std::abs(amount) < 0.005f)
    return shown;

  const auto range = strikeRange(amount, delay, attack);

  // An end with no wait shows where the partial starts instead, as the knob
  // does.
  const auto end = [](float seconds, float turns) {
    return seconds <= 0.0f && turns > 0.0f ? attackText(-turns * 4.0f, 0)
                                           : timeText(seconds, 0);
  };

  return shown + "  " + end(range.quickest, range.quickestTurns) + " to " +
         end(range.slowest, range.slowestTurns);
}

juce::String lofiRateName(int hz) {
  if (hz <= 0)
    return "Host";

  // Rates that are not a whole number of kHz keep their exact figure.
  return hz % 1000 == 0 ? juce::String(hz / 1000) + " kHz"
                        : juce::String((double)hz / 1000.0, 3) + " kHz";
}

juce::String lofiBitName(int bits) {
  return bits <= 0 ? "Host" : juce::String(bits) + " bit";
}

int Cache::polyphonyValue() const {
  const auto index = juce::jlimit(0, (int)kPolyphonyChoices.size() - 1,
                                  (int)polyphony->load());
  return kPolyphonyChoices[(size_t)index];
}

bool Cache::legatoValue() const {
  return (int)polyphony->load() == kLegatoIndex || legato->load() > 0.5f;
}

namespace {

/// The field a row writes into, so a macro can offset any of them.
float *macroField(OscParams &o, int row) {
  // Shifted by the None the menu opens with, so case 1 is the first real
  // row.
  switch (row) {
  case 1:
    return &o.tuneBlend;
  case 2:
    return &o.glideSeconds;
  case 3:
    return &o.pmRateHz;
  case 4:
    return &o.pmDepthCents;
  case 5:
    return &o.driftCents;
  case 6:
    return &o.strikeAmount;
  case 7:
    return &o.delay;
  case 8:
    return &o.attack;
  case 9:
    return &o.decay;
  case 10:
    return &o.sustain;
  case 11:
    return &o.swell;
  case 12:
    return &o.offLevel;
  case 13:
    return &o.release;
  case 14:
    return &o.amRateHz;
  case 15:
    return &o.amDepth;
  case 16:
    return &o.velAmount;
  case 17:
    return &o.atAmount;
  case 18:
    return &o.pan;
  case 19:
    return &o.volume;
  case 20:
    return &o.keyAmount;

  default:
    return nullptr;
  }
}

static_assert(kNumMacroRows == 20,
              "macroField has a case per row and has to grow with the list");

} // namespace

void Cache::snapshot(SynthParams &out, float bendNormalised) const {
  // Solo spans the noise channel too.
  bool anySolo = noise.solo->load() > 0.5f;
  for (const auto &o : osc) {
    if (anySolo)
      break;

    anySolo = o.solo->load() > 0.5f;
  }

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto &c = osc[(size_t)i];
    auto &o = out.osc[(size_t)i];

    for (const auto &row : kOscRows)
      if (row.field != nullptr)
        o.*row.field = (c.*row.cached)->load();

    o.pmShape = pitchShapeAt((int)c.pmShape->load());
    o.amShape = ampShapeAt((int)c.amShape->load());

    const bool muted = c.mute->load() > 0.5f;
    const bool soloed = c.solo->load() > 0.5f;

    // Mixer convention: solo isolates, but an explicit mute still wins.
    o.audible = muted ? false : (anySolo ? soloed : true);
  }

  // ---- the macros ---------------------------------------------------------
  //
  // Offsets laid over the patch rather than written into it, so one host lane
  // moves 32 channels without changing the patch. The knobs do not move when
  // a macro does.
  for (int m = 0; m < kNumMacros; ++m) {
    const auto &cached = macro[(size_t)m];
    const auto amount = cached.amount->load();

    // The common case, and it has to cost nothing.
    if (std::abs(amount) < 1.0e-6f)
      continue;

    const int row =
        juce::jlimit(0, kNumMacroRows, (int)std::lround(cached.row->load()));

    // Pointing at None, which is an unmade or removed macro. Its amount may
    // still be non-zero.
    if (row == 0)
      continue;

    const auto scope = juce::jlimit(0, kNumMacroScopes - 1,
                                    (int)std::lround(cached.scope->load()));
    const auto curve =
        (MacroCurve)juce::jlimit(0, (int)MacroCurve::NumCurves - 1,
                                 (int)std::lround(cached.curve->load()));

    const auto selected = macroChannels[(size_t)m].load();

    // Numbered from one on the panel, counted from zero here.
    const auto anchor = juce::jlimit(
        0, kNumHarmonics - 1, (int)std::lround(cached.anchor->load()) - 1);

    const auto &range = rowRange[(size_t)(row - 1)];

    for (int i = 0; i < kNumHarmonics; ++i) {
      if (!macroReaches(scope, i, selected))
        continue;

      auto *field = macroField(out.osc[(size_t)i], row);

      if (field == nullptr)
        continue;

      // Shifted across the control's own travel rather than its units, so a
      // macro does the same thing wherever the control is set, and clamped to
      // the ends. Two macros on one row add up.
      const auto moved =
          range.convertTo0to1(*field) + amount * macroWeight(curve, i, anchor);

      *field = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, moved));
    }
  }

  {
    auto &n = out.noise;

    for (const auto &row : kNoiseRows)
      if (row.field != nullptr)
        n.*row.field = (noise.*row.cached)->load();

    n.amShape = ampShapeAt((int)noise.amShape->load());

    const bool muted = noise.mute->load() > 0.5f;
    const bool soloed = noise.solo->load() > 0.5f;
    n.audible = muted ? false : (anySolo ? soloed : true);
  }

  out.global.masterGain =
      juce::Decibels::decibelsToGain(masterGain->load(), -60.0f);
  out.global.bendSemitones = bendNormalised * bendRange->load();
  out.global.phaseReset = phaseReset->load() > 0.5f;
  out.global.stretchCents = stretch->load();
  out.global.trackDbPerOctave = track->load();
  out.global.slideDest = (SlideDestination)(int)slideDest->load();
  out.global.wobbleAmount = wobble->load();

  {
    const auto pick = [](const std::atomic<float> *p, int count) {
      return juce::jlimit(0, count - 1, (int)std::lround(p->load()));
    };

    out.global.temperament =
        (Temperament)pick(temperament, (int)Temperament::NumTemperaments);

    out.global.tuningRoot = pick(tuningRoot, (int)kPitchClassNames.size());

    out.global.referenceHz = (double)kReferenceHzChoices[(size_t)pick(
        referenceHz, (int)kReferenceHzChoices.size())];

    out.global.adaptiveRetuneSeconds = (double)juce::jlimit(
        0.0f, kMaxAdaptiveRetuneSeconds, adaptiveRetune->load());
    out.global.harmonicSevenths = adaptiveSeventh->load() > 0.5f;
    out.global.adaptiveHome = adaptiveHome->load() > 0.5f;

    out.global.character =
        (Character)pick(character, (int)Character::NumCharacters);
  }

  // Set by the character. See BusDrive::amountFor.
  out.global.busDrive = (float)ovt::BusDrive::amountFor(out.global.character);
  out.global.safetyClip = safetyClip->load() > 0.5f;

  out.global.lookahead = lookahead->load() > 0.5f;
  out.global.clipType = (dm::ClipType)juce::jlimit(
      0, (int)dm::ClipType::NumTypes - 1, (int)std::lround(clipType->load()));
  out.global.oneVoicePerKey = oneVoicePerKey->load() > 0.5f;
  out.global.pitchModInPhase = pmInPhase->load() > 0.5f;
  out.global.ampModInPhase = amInPhase->load() > 0.5f;
  out.global.glideTrigger =
      glideTrigger->load() > 0.5f ? GlideTrigger::Legato : GlideTrigger::Always;
  out.global.glideMode =
      glideMode->load() > 0.5f ? GlideMode::Time : GlideMode::Rate;
  out.global.velocityCurve = velocityCurve->load() > 0.5f
                                 ? VelocityCurve::Decibels
                                 : VelocityCurve::Classic;

  {
    const auto r = juce::jlimit(0, (int)kLofiRateChoices.size() - 1,
                                (int)std::lround(lofiRate->load()));
    const auto b = juce::jlimit(0, (int)kLofiBitChoices.size() - 1,
                                (int)std::lround(lofiBits->load()));

    out.lofi.rateHz = (double)kLofiRateChoices[(size_t)r];
    out.lofi.bits = kLofiBitChoices[(size_t)b];
  }

  out.echo.enabled = echo.on->load() > 0.5f;

  out.echo.type = (EchoType)juce::jlimit(0, (int)EchoType::NumTypes - 1,
                                         (int)std::lround(echo.type->load()));
  out.echo.mix = echo.mix->load();
  out.echo.timeSeconds = echo.time->load();
  out.echo.feedback = echo.feedback->load();
  out.echo.age = echo.age->load();

  out.reverb.enabled = reverb.on->load() > 0.5f;

  out.reverb.type = (ReverbType)juce::jlimit(
      0, (int)ReverbType::NumTypes - 1, (int)std::lround(reverb.type->load()));
  out.reverb.mix = reverb.mix->load();
  out.reverb.decaySeconds = reverb.decay->load();
  out.reverb.damping = reverb.damp->load();
  out.reverb.preDelaySeconds = reverb.preDelay->load();

  out.resonator.mode =
      (ResonatorMode)juce::jlimit(0, (int)ResonatorMode::NumModes - 1,
                                  (int)std::lround(resonator.mode->load()));
  out.resonator.mix = resonator.mix->load();
  out.resonator.decaySeconds = resonator.decay->load();
  out.resonator.damping = resonator.damp->load();
}

} // namespace ovt::params
