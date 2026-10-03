#include "PluginParameters.h"

#include "dsp/BusDrive.h"

#include "dsp/Harmonics.h"
#include "dsp/TapeEcho.h"
#include "dsp/Velocity.h"

namespace ovt::params {

namespace {
/// A range where equal turns of the knob are equal ratios.
///
/// The natural shape for a time control, because a step from 1 ms to 2 ms
/// matters as much as one from 100 ms to 200 ms, and this gives them the same
/// amount of travel.
///
/// setSkewForCentre cannot. Fitting a power curve through a midpoint across
/// four and a half decades needs an exponent of about 7.4, and the bottom
/// eighth of that curve is flat enough to be dead: nudging the knob there
/// moves the value by nothing at all.
///
/// Needs a low end above zero, since nothing has a ratio to zero.
///
/// The two conversions work in double and narrow only on the way out. In float
/// throughout, a value converted to the knob's position and straight back does
/// not always land on the value it started from: the decay and release
/// defaults were two such, and auval reports every one of them as a parameter
/// that would not hold its default. Round-tripping a float through a logarithm
/// cannot be made exact, since the position is a float too, but double takes
/// the positions that fail from more than four in ten to about one in twenty,
/// and every default this plugin ships lands on one that holds. None of this
/// is on the audio thread: the engine reads cached atomics rather than going
/// through a range.
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

/// A range that has to include zero, curved so the bottom of it is usable.
///
/// A time that can be switched off cannot be logarithmic, since nothing has a
/// ratio to zero. This grows exponentially from the low end instead, which
/// gives the same thing that matters: a slope at the bottom that is small but
/// never zero, so nudging the knob there always moves the value.
///
/// @param centre  the value at half travel, which fixes the curvature. It has
///                to sit below the midpoint of the range, which for every
///                control here it does by a wide margin.
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
/// Piano stretch lives in the first hundred cents or so and a bell wants a
/// thousand, so a linear control would spend most of its travel somewhere
/// nobody goes. Squaring either side of centre puts a quarter of the range in
/// half the travel and keeps the symmetry a bipolar control needs.
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

/// Where in its own cycle a partial starts, as degrees rather than turns,
/// which is the unit anybody discussing phase already uses.
juce::String phaseText(float turns, int) {
  return juce::String(juce::roundToInt(turns * 360.0f)) +
         juce::String::charToString(0xb0);
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

juce::String percentText(float v, int) {
  return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

/// The echo age has a floor under it that the player has no reason to know
/// about. It exists so the two tape paths never wander by exactly the same
/// amount, which is what would collapse the repeat to mono, and it is a fact
/// about the machine rather than a setting. So the knob reads across its whole
/// travel and the floor is folded in on the way through, in both directions,
/// which keeps a typed value and a shown one agreeing.
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

/// Pan reads as a side and a distance, the way a desk marks it, rather than as
/// a signed number nobody converts in their head.
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

/// The fader reaches a shade below the quietest level a readout can name.
///
/// Without the margin, -99.9 dB lands on the very bottom of the travel, and
/// the very bottom is silence, so the floor could be read but never set. The
/// margin puts it about a hundredth of the way up, leaving a couple of pixels
/// below it that are too quiet to name and then silence itself.
constexpr float kFaderFloorDb = kQuietestLevelDb - 2.1f;

/// How a level fader's travel maps to gain.
///
/// A square law in decibels, where a plain gain fader is a square law in gain.
/// The difference is what happens at the quiet end. Spacing the travel by gain
/// spends almost all of it on the loudest few decibels and leaves everything
/// below -66 dB inside the last five pixels of a 223 pixel fader, which is why
/// that end could be read but never set. Spacing it by decibels instead puts
/// -66 dB about a fifth of the way up, with -99.9 dB at the bottom of the
/// travel and silence in the last pixel.
///
/// Squared rather than straight: a fader linear in decibels would give the top
/// twelve decibels a tenth of its travel, a quarter of what a gain law gives
/// them, and that is where most of the mixing happens.
///
/// The stored value is still a gain, so presets and saved state mean what they
/// always did. What changes is where a given level sits along the fader, and
/// with it any host automation written against the old shape.
juce::NormalisableRange<float> levelRange() {
  return {0.0f, 1.0f,

          // Travel into gain.
          [](float, float, float norm) {
            // The bottom of the travel is silence rather than the quietest
            // readable level, so a fader pulled all the way down is off rather
            // than very nearly off.
            if (norm <= 0.0f)
              return 0.0f;

            // The floor has to be handed over rather than left to its default,
            // which is -100 dB. This fader reaches below that, and a default
            // that rounds everything past it to silence would make the bottom
            // hundredth of the travel dead.
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

juce::String macroAmountId(int macro) {
  return "macro" + juce::String(macro + 1) + "_amount";
}

juce::String macroRowId(int macro) {
  return "macro" + juce::String(macro + 1) + "_row";
}

juce::String macroScopeId(int macro) {
  return "macro" + juce::String(macro + 1) + "_scope";
}

juce::String macroCurveId(int macro) {
  return "macro" + juce::String(macro + 1) + "_curve";
}

juce::String macroColourId(int macro) {
  return "macro" + juce::String(macro + 1) + "_colour";
}

const char *macroScopeName(int scope) {
  switch ((MacroScope)scope) {
  case MacroScope::All:
    return "All";
  case MacroScope::Odd:
    return "Odd";
  case MacroScope::Even:
    return "Even";

  case MacroScope::Interval:
    break;
  }

  // Everything past the three is an interval. Taken from the one table that
  // names them rather than written again here, and capitalised to sit beside
  // All, Odd and Even without looking like a different kind of thing. Built
  // once and kept, since this hands back a pointer that has to outlive it.
  if (scope >= (int)MacroScope::Interval && scope < kNumMacroScopes) {
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

/// Whether a macro reaches this channel.
bool macroReaches(int scope, int index0) {
  switch ((MacroScope)scope) {
  case MacroScope::Odd:
    return ((index0 + 1) % 2) == 1;
  case MacroScope::Even:
    return ((index0 + 1) % 2) == 0;

  case MacroScope::All:
    return true;

  case MacroScope::Interval:
    break;
  }

  // Everything past the three names an interval, and reaches every channel
  // standing at it: the fifth is harmonics 3, 6, 12 and 24.
  return harmonicTable()[(size_t)index0].pitchClass ==
         scope - (int)MacroScope::Interval;
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

/// What a row is called in a macro's own menu.
///
/// Spelt here rather than taken from the panel's row labels, which repeat: two
/// rows are both called "rate" and three are called "depth", which reads fine
/// under a heading and not at all in a flat list of nineteen.
const char *macroRowName(int row) {
  // None first, which is a macro nobody has made yet, so this is one longer
  // than the list of rows.
  static const char *const names[] = {"None",
                                      "Tune",
                                      "Phase",
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
                                      "Level"};

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
/// Every channel that carries a per-channel switch, in mixer order, with the
/// noise channel last the way the strip sits.
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

void clearChannelSwitch(juce::AudioProcessorValueTreeState &apvts,
                        const char *suffix) {
  for (const auto &id : channelIds(suffix)) {
    auto *param = apvts.getParameter(id);

    // Only what is actually on. Writing zero over a zero would report a
    // gesture on all thirty-three whatever the mixer looked like, and would
    // put a step in the undo history that changed nothing.
    if (param == nullptr || param->getValue() <= 0.5f)
      continue;

    param->beginChangeGesture();
    param->setValueNotifyingHost(0.0f);
    param->endChangeGesture();
  }
}

float defaultVolumeFor(int index0) {
  // 1/n over the first eight partials, silence above: a soft sawtooth / drawbar
  // blend that is immediately playable without being a wall of sound.
  if (index0 >= 8)
    return 0.0f;

  return 1.0f / (float)(index0 + 1);
}

namespace {
/// One name per entry of the list beside it, in the same order.
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

LfoShape pitchShapeAt(int index) {
  return kPitchShapes[(size_t)juce::jlimit(0, (int)kPitchShapes.size() - 1,
                                           index)];
}

LfoShape ampShapeAt(int index) {
  return kAmpShapes[(size_t)juce::jlimit(0, (int)kAmpShapes.size() - 1, index)];
}

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

  // On, because an acoustic instrument works this way and because a key with
  // a long release that is tapped repeatedly otherwise stacks every tap. Off
  // is for anyone who wants the tails to sum, which is a sound rather than a
  // fault.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{oneVoicePerKeyId, 1},
                                     "One Voice Per Key", true));

  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{bendRangeId, 1}, "Pitch Bend Range", 0, 24, 2));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{phaseResetId, 1},
                                     "Phase Reset", true));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{safetyClipId, 1},
                                     "Safety Clip", true));

  // On, because the limiter without it is a rougher limiter rather than a
  // broken one and the two milliseconds are worth that to most people. See
  // params::lookaheadId for why it is a session parameter and not a patch's.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{lookaheadId, 1},
                                     "Lookahead", true));

  // ---- the macros ---------------------------------------------------------
  //
  // Four parameters each, of which one is the point: the amount is what a
  // host draws, and the other three say what it reaches. Those three are
  // parameters rather than state so that they travel with a patch by the
  // rule everything else follows, and so a preset can define what its macros
  // do rather than inheriting whatever the last one set.
  for (int m = 0; m < kNumMacros; ++m) {
    const auto name = "Macro " + juce::String(m + 1);

    // Bipolar and centred, because a macro that could only push one way would
    // need the patch dialled at an extreme to be useful in the other.
    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{macroAmountId(m), 1}, name,
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f), 0.0f));

    // None first, which is what an unmade macro points at. Every one of the
    // eight starts there, so a fresh instrument has a pool and no macros.
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

    // Visual only, and a parameter anyway, because a preset file carries
    // parameters and nothing else: a patch that makes four macros has to be
    // able to say what colour they are. See presets::capture.
    juce::StringArray colours;
    for (int i = 0; i < kNumMacroColours; ++i)
      colours.add(macroColourName(i));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{macroColourId(m), 1}, name + " Colour", colours,
        // Each macro starts wearing a different one, so making four in a row
        // gives four colours without anyone choosing them.
        1 + m % (kNumMacroColours - 1)));
  }

  // Off by default, because it changes what an incoming channel number means
  // and most keyboards are not saying anything by it. See
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

  // Never quite new. See TapeEcho::kMinAge: at zero the two tape paths wander
  // by the same nothing and the repeat comes back in mono. See ageText for why
  // the knob still reads from nothing to everything.
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
        logRange(0.01f, 30.0f), 4.0f, FAttr().withLabel("Hz")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(pmDepthSuffix, i), 1},
        p + "Pitch Mod Depth", expRange(0.0f, kMaxPitchModCents, 25.0f), 0.0f,
        FAttr().withLabel("ct")));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{oscParamId(pmShapeSuffix, i), 1},
        p + "Pitch Mod Shape", pitchShapeNames(), 0));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(phaseSuffix, i), 1}, p + "Start Phase",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(phaseText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(driftSuffix, i), 1}, p + "Drift",
        expRange(0.0f, kMaxDriftCents, 6.0f), 0.0f, FAttr().withLabel("ct")));

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
        logRange(0.0002f, kMaxAttackSeconds), 0.005f,
        FAttr().withStringFromValueFunction(timeText)));

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
        logRange(0.01f, 30.0f), 4.0f, FAttr().withLabel("Hz")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(amDepthSuffix, i), 1}, p + "Amp Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(percentText)));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{oscParamId(amShapeSuffix, i), 1}, p + "Amp Mod Shape",
        ampShapeNames(), 0));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(velSuffix, i), 1}, p + "Velocity",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.7f,
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
  // Same controls as a strip, minus everything to do with pitch, plus a colour
  // control in its place.
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
      logRange(0.01f, 30.0f), 4.0f, FAttr().withLabel("Hz")));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(amDepthSuffix), 1}, "Noise Amp Mod Depth",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{noiseParamId(amShapeSuffix), 1}, "Noise Amp Mod Shape",
      ampShapeNames(), 0));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(velSuffix), 1}, "Noise Velocity",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.7f,
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
  // Whether a channel's modulator is one circuit the whole keyboard hears or
  // one per note. They belong with the modulators they govern rather than down
  // here, and they are here anyway: everything above them shipped, and a
  // parameter added at the end is one that cannot move anything a lane was
  // written against on a host that goes by position.
  //
  // Named "in phase" rather than "sync", which in this corner of the world
  // means locked to the host's tempo. Nothing here is.
  layout.add(std::make_unique<BoolP>(juce::ParameterID{pmInPhaseId, 1},
                                     "Pitch Mod In Phase", false));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{amInPhaseId, 1},
                                     "Amp Mod In Phase", false));

  // On the end, like everything that arrives after a release, so nothing
  // already automated moves. Every type it will ever have is listed now
  // rather than added as each one is written, because the length of the list
  // is part of what a stored choice means: a host keeps a fraction of the
  // range, so a list that grows moves every lane ever written against it.
  juce::StringArray echoTypeChoices;
  for (int i = 0; i < (int)EchoType::NumTypes; ++i)
    echoTypeChoices.add(echoTypeName((EchoType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{echoTypeId, 1}, "Echo Type", echoTypeChoices,
      (int)EchoType::Tape));

  juce::StringArray clipTypeChoices;
  for (int i = 0; i < (int)ClipType::NumTypes; ++i)
    clipTypeChoices.add(clipTypeName((ClipType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{clipTypeId, 1}, "Clip Type", clipTypeChoices,
      (int)ClipType::Soft));

  juce::StringArray reverbTypeChoices;
  for (int i = 0; i < (int)ReverbType::NumTypes; ++i)
    reverbTypeChoices.add(reverbTypeName((ReverbType)i));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{reverbTypeId, 1}, "Reverb Type", reverbTypeChoices,
      (int)ReverbType::Room));

  return layout;
}

void Cache::connect(juce::AudioProcessorValueTreeState &apvts) {
  masterGain = apvts.getRawParameterValue(masterGainId);
  polyphony = apvts.getRawParameterValue(polyphonyId);
  oneVoicePerKey = apvts.getRawParameterValue(oneVoicePerKeyId);
  pmInPhase = apvts.getRawParameterValue(pmInPhaseId);
  amInPhase = apvts.getRawParameterValue(amInPhaseId);
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
  }

  // Read once here rather than per block. A row's range is a property of the
  // layout and cannot move while the plugin is running.
  for (int r = 0; r < kNumMacroRows; ++r) {
    auto &range = rowRange[(size_t)r];
    range = {0.0f, 1.0f};

    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            apvts.getParameter(oscParamId(kMacroRows[(size_t)r], 0))))
      range = {p->getNormalisableRange().start, p->getNormalisableRange().end};
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

  jassert(echo.on != nullptr && reverb.on != nullptr);

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &o = osc[(size_t)i];

    o.tune = apvts.getRawParameterValue(oscParamId(tuneSuffix, i));
    o.phase = apvts.getRawParameterValue(oscParamId(phaseSuffix, i));
    o.pmRate = apvts.getRawParameterValue(oscParamId(pmRateSuffix, i));
    o.pmDepth = apvts.getRawParameterValue(oscParamId(pmDepthSuffix, i));
    o.pmShape = apvts.getRawParameterValue(oscParamId(pmShapeSuffix, i));
    o.drift = apvts.getRawParameterValue(oscParamId(driftSuffix, i));
    o.strike = apvts.getRawParameterValue(oscParamId(strikeSuffix, i));
    o.delay = apvts.getRawParameterValue(oscParamId(delaySuffix, i));
    o.attack = apvts.getRawParameterValue(oscParamId(attackSuffix, i));
    o.decay = apvts.getRawParameterValue(oscParamId(decaySuffix, i));
    o.sustain = apvts.getRawParameterValue(oscParamId(sustainSuffix, i));
    o.swell = apvts.getRawParameterValue(oscParamId(swellSuffix, i));
    o.offLevel = apvts.getRawParameterValue(oscParamId(offLevelSuffix, i));
    o.release = apvts.getRawParameterValue(oscParamId(releaseSuffix, i));
    o.amRate = apvts.getRawParameterValue(oscParamId(amRateSuffix, i));
    o.amDepth = apvts.getRawParameterValue(oscParamId(amDepthSuffix, i));
    o.amShape = apvts.getRawParameterValue(oscParamId(amShapeSuffix, i));
    o.vel = apvts.getRawParameterValue(oscParamId(velSuffix, i));
    o.at = apvts.getRawParameterValue(oscParamId(atSuffix, i));
    o.mute = apvts.getRawParameterValue(oscParamId(muteSuffix, i));
    o.solo = apvts.getRawParameterValue(oscParamId(soloSuffix, i));
    o.volume = apvts.getRawParameterValue(oscParamId(volumeSuffix, i));
    o.pan = apvts.getRawParameterValue(oscParamId(panSuffix, i));

    jassert(o.tune != nullptr && o.volume != nullptr);
  }

  noise.colour = apvts.getRawParameterValue(noiseParamId(colourSuffix));
  noise.strike = apvts.getRawParameterValue(noiseParamId(strikeSuffix));
  noise.delay = apvts.getRawParameterValue(noiseParamId(delaySuffix));
  noise.attack = apvts.getRawParameterValue(noiseParamId(attackSuffix));
  noise.decay = apvts.getRawParameterValue(noiseParamId(decaySuffix));
  noise.sustain = apvts.getRawParameterValue(noiseParamId(sustainSuffix));
  noise.swell = apvts.getRawParameterValue(noiseParamId(swellSuffix));
  noise.offLevel = apvts.getRawParameterValue(noiseParamId(offLevelSuffix));
  noise.release = apvts.getRawParameterValue(noiseParamId(releaseSuffix));
  noise.amRate = apvts.getRawParameterValue(noiseParamId(amRateSuffix));
  noise.amDepth = apvts.getRawParameterValue(noiseParamId(amDepthSuffix));
  noise.amShape = apvts.getRawParameterValue(noiseParamId(amShapeSuffix));
  noise.vel = apvts.getRawParameterValue(noiseParamId(velSuffix));
  noise.at = apvts.getRawParameterValue(noiseParamId(atSuffix));
  noise.mute = apvts.getRawParameterValue(noiseParamId(muteSuffix));
  noise.solo = apvts.getRawParameterValue(noiseParamId(soloSuffix));
  noise.volume = apvts.getRawParameterValue(noiseParamId(volumeSuffix));
  noise.pan = apvts.getRawParameterValue(noiseParamId(panSuffix));

  jassert(noise.colour != nullptr && noise.volume != nullptr);
}

juce::String strikeRangeText(float amount, float delay, float attack) {
  const auto shown = signedPercentText(amount, 0);

  // The same threshold the percentage rounds by, so the two halves of the
  // reading cannot disagree about whether the knob is doing anything.
  if (std::abs(amount) < 0.005f)
    return shown;

  const auto range = strikeRange(amount, delay, attack);

  return shown + "  " + timeText(range.quickest, 0) + " to " +
         timeText(range.slowest, 0);
}

juce::String lofiRateName(int hz) {
  if (hz <= 0)
    return "Host";

  // Rates that are not a whole number of kHz are the ones people know by their
  // exact figure, so they keep it.
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
  return (int)polyphony->load() == kLegatoIndex;
}

namespace {

/// The field a row writes into, so a macro can offset any of them without a
/// switch at every call site.
///
/// Every row a macro can drive is a plain float with a range, which is the
/// same restriction LINK works under and for the same reason: there is
/// nothing to offset on a row of shapes or of mute buttons.
float *macroField(OscParams &o, int row) {
  // Shifted by the None the menu opens with, so case 1 is the first
  // real row. A macro still pointing at None never reaches here.
  switch (row) {
  case 1:
    return &o.tuneBlend;
  case 2:
    return &o.startPhase;
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

  default:
    return nullptr;
  }
}

static_assert(kNumMacroRows == 19,
              "macroField has a case per row and has to grow with the list");

/// This channel's share, from the fundamental outwards.
///
/// Taper is measured against the full width of the mixer rather than against
/// whichever end is nearer, which is what ui::linkCurveWeight does from the
/// strip under the mouse. A test holds the two together.
float macroWeight(MacroCurve curve, int index0) {
  if (curve != MacroCurve::Taper)
    return 1.0f;

  const auto distance = (float)juce::jlimit(0, kNumHarmonics - 1, index0) /
                        (float)(kNumHarmonics - 1);

  return 1.0f - distance;
}

} // namespace

void Cache::snapshot(SynthParams &out, float bendNormalised) const {
  // Solo spans the noise channel too, so soloing a partial silences the noise
  // and soloing the noise silences the series.
  bool anySolo = noise.solo->load() > 0.5f;
  for (const auto &o : osc) {
    if (anySolo)
      break;

    anySolo = o.solo->load() > 0.5f;
  }

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto &c = osc[(size_t)i];
    auto &o = out.osc[(size_t)i];

    o.tuneBlend = c.tune->load();
    o.pmRateHz = c.pmRate->load();
    o.pmShape = pitchShapeAt((int)c.pmShape->load());
    o.pmDepthCents = c.pmDepth->load();
    o.startPhase = c.phase->load();
    o.driftCents = c.drift->load();
    o.strikeAmount = c.strike->load();
    o.delay = c.delay->load();
    o.attack = c.attack->load();
    o.decay = c.decay->load();
    o.sustain = c.sustain->load();
    o.swell = c.swell->load();
    o.offLevel = c.offLevel->load();
    o.release = c.release->load();
    o.amRateHz = c.amRate->load();
    o.amShape = ampShapeAt((int)c.amShape->load());
    o.amDepth = c.amDepth->load();
    o.velAmount = c.vel->load();
    o.atAmount = c.at->load();
    o.volume = c.volume->load();
    o.pan = c.pan->load();

    const bool muted = c.mute->load() > 0.5f;
    const bool soloed = c.solo->load() > 0.5f;

    // Mixer convention: solo isolates, but an explicit mute still wins.
    o.audible = muted ? false : (anySolo ? soloed : true);
  }

  // ---- the macros ---------------------------------------------------------
  //
  // Offsets laid over what the patch says rather than writes into it, which
  // is the whole point of them: a host draws one lane, 32 channels move, and
  // nothing in the patch changes. Writing instead would mean 32 parameter
  // changes per move for the host to catch, which is the problem issue #24
  // reported rather than a way out of it, and it would overwrite the sound
  // the macro was supposed to be shaping.
  //
  // It also means the knobs do not move when a macro does. That is what a VCA
  // does, and it is the one thing about these worth knowing in advance.
  for (int m = 0; m < kNumMacros; ++m) {
    const auto &cached = macro[(size_t)m];

    if (cached.amount == nullptr)
      continue;

    const auto amount = cached.amount->load();

    // The common case by far, and the one that has to be free.
    if (std::abs(amount) < 1.0e-6f)
      continue;

    const int row =
        juce::jlimit(0, kNumMacroRows, (int)std::lround(cached.row->load()));

    // Pointing at None, which is what an unmade macro does. Its amount may
    // well be somewhere other than zero: taking the row away is how a macro
    // is removed, and the fader it was on keeps wherever it was left.
    if (row == 0)
      continue;

    const auto scope = juce::jlimit(0, kNumMacroScopes - 1,
                                    (int)std::lround(cached.scope->load()));
    const auto curve =
        (MacroCurve)juce::jlimit(0, (int)MacroCurve::NumCurves - 1,
                                 (int)std::lround(cached.curve->load()));

    const auto range = rowRange[(size_t)(row - 1)];
    const auto span = range.second - range.first;

    for (int i = 0; i < kNumHarmonics; ++i) {
      if (!macroReaches(scope, i))
        continue;

      auto *field = macroField(out.osc[(size_t)i], row);

      if (field == nullptr)
        continue;

      // Clamped to the row's own range, so a macro can take a control to its
      // end and no further. Two macros on one row add up and the clamp still
      // holds.
      *field = juce::jlimit(range.first, range.second,
                            *field + amount * span * macroWeight(curve, i));
    }
  }

  {
    auto &n = out.noise;

    n.colour = noise.colour->load();
    n.strikeAmount = noise.strike->load();
    n.delay = noise.delay->load();
    n.attack = noise.attack->load();
    n.decay = noise.decay->load();
    n.sustain = noise.sustain->load();
    n.swell = noise.swell->load();
    n.offLevel = noise.offLevel->load();
    n.release = noise.release->load();
    n.amRateHz = noise.amRate->load();
    n.amShape = ampShapeAt((int)noise.amShape->load());
    n.amDepth = noise.amDepth->load();
    n.velAmount = noise.vel->load();
    n.atAmount = noise.at->load();
    n.volume = noise.volume->load();
    n.pan = noise.pan->load();

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
      return p == nullptr
                 ? 0
                 : juce::jlimit(0, count - 1, (int)std::lround(p->load()));
    };

    out.global.temperament =
        (Temperament)pick(temperament, (int)Temperament::NumTemperaments);

    out.global.tuningRoot = pick(tuningRoot, (int)kPitchClassNames.size());

    out.global.referenceHz = (double)kReferenceHzChoices[(size_t)pick(
        referenceHz, (int)kReferenceHzChoices.size())];

    out.global.character =
        (Character)pick(character, (int)Character::NumCharacters);
  }

  // From the character rather than from anything anyone can turn, and after
  // it for that reason. See BusDrive::amountFor.
  out.global.busDrive = (float)ovt::BusDrive::amountFor(out.global.character);
  out.global.safetyClip = safetyClip->load() > 0.5f;

  // Older states have no such parameter, so a null pointer means a build
  // that predates the switch, which had the lookahead and no way to refuse
  // it.
  out.global.lookahead = lookahead == nullptr || lookahead->load() > 0.5f;

  // A patch saved before there were types says nothing about one and reads
  // back as zero, which is the soft clipper it was made on.
  out.global.clipType =
      clipType == nullptr
          ? ClipType::Soft
          : (ClipType)juce::jlimit(0, (int)ClipType::NumTypes - 1,
                                   (int)std::lround(clipType->load()));
  out.global.oneVoicePerKey = oneVoicePerKey->load() > 0.5f;
  out.global.pitchModInPhase = pmInPhase->load() > 0.5f;
  out.global.ampModInPhase = amInPhase->load() > 0.5f;

  {
    const auto r = juce::jlimit(0, (int)kLofiRateChoices.size() - 1,
                                (int)std::lround(lofiRate->load()));
    const auto b = juce::jlimit(0, (int)kLofiBitChoices.size() - 1,
                                (int)std::lround(lofiBits->load()));

    out.lofi.rateHz = (double)kLofiRateChoices[(size_t)r];
    out.lofi.bits = kLofiBitChoices[(size_t)b];
  }

  out.echo.enabled = echo.on->load() > 0.5f;

  // A patch saved before there were types says nothing about one and reads
  // back as zero, which is the tape it was made on.
  out.echo.type =
      echo.type == nullptr
          ? EchoType::Tape
          : (EchoType)juce::jlimit(0, (int)EchoType::NumTypes - 1,
                                   (int)std::lround(echo.type->load()));
  out.echo.mix = echo.mix->load();
  out.echo.timeSeconds = echo.time->load();
  out.echo.feedback = echo.feedback->load();
  out.echo.age = echo.age->load();

  out.reverb.enabled = reverb.on->load() > 0.5f;

  out.reverb.type =
      reverb.type == nullptr
          ? ReverbType::Room
          : (ReverbType)juce::jlimit(0, (int)ReverbType::NumTypes - 1,
                                     (int)std::lround(reverb.type->load()));
  out.reverb.mix = reverb.mix->load();
  out.reverb.decaySeconds = reverb.decay->load();
  out.reverb.damping = reverb.damp->load();
  out.reverb.preDelaySeconds = reverb.preDelay->load();
}

} // namespace ovt::params
