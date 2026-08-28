#include "PluginParameters.h"

#include "dsp/Harmonics.h"
#include "dsp/TapeEcho.h"

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

  return {lo, hi,
          [](float a, float b, float t) {
            return (float)((double)a *
                           std::pow((double)b / (double)a, (double)t));
          },
          [](float a, float b, float v) {
            const auto t = std::log((double)v / (double)a) /
                           std::log((double)b / (double)a);

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

            return juce::jlimit(0.0f, 1.0f,
                                std::log(std::max(1.0e-9f, y)) / k);
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
  return {
      0.0f, 1.0f,

      // Travel into gain.
      [](float, float, float norm) {
        // The bottom of the travel is silence rather than the quietest
        // readable level, so a fader pulled all the way down is off rather
        // than very nearly off.
        if (norm <= 0.0f)
          return 0.0f;

        // The floor has to be handed over rather than left to its default,
        // which is -100 dB. This fader reaches below that, and a default that
        // rounds everything past it to silence would make the bottom hundredth
        // of the travel dead.
        return juce::Decibels::decibelsToGain(
            kFaderFloorDb * std::pow(1.0f - norm, kLevelShape), kFaderFloorDb);
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

juce::String noiseParamId(const char *suffix) {
  return juce::String("noise_") + suffix;
}

float defaultVolumeFor(int index0) {
  // 1/n over the first eight partials, silence above: a soft sawtooth / drawbar
  // blend that is immediately playable without being a wall of sound.
  if (index0 >= 8)
    return 0.0f;

  return 1.0f / (float)(index0 + 1);
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
  for (auto n : kPolyphonyChoices)
    polyChoices.add(juce::String(n));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{polyphonyId, 1}, "Polyphony", polyChoices,
      4)); // 8 voices

  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{bendRangeId, 1}, "Pitch Bend Range", 0, 24, 2));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{phaseResetId, 1},
                                     "Phase Reset", true));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{safetyClipId, 1},
                                     "Safety Clip", true));

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

  layout.add(
      std::make_unique<FloatP>(juce::ParameterID{echoTimeId, 1}, "Echo Time",
                               logRange(0.02f, 2.0f), 0.35f,
                               FAttr().withStringFromValueFunction(timeText)));

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
      FAttr()
          .withStringFromValueFunction(ageText)
          .withValueFromStringFunction(ageValue)));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{reverbOnId, 1}, "Reverb",
                                     false));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{reverbMixId, 1}, "Reverb Mix",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{reverbDecayId, 1}, "Reverb Decay",
      logRange(0.2f, 20.0f), 2.0f,
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
        p + "Pitch Mod Depth", expRange(0.0f, kMaxPitchModCents, 25.0f),
        0.0f,
        FAttr().withLabel("ct")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(phaseSuffix, i), 1}, p + "Start Phase",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(phaseText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(driftSuffix, i), 1}, p + "Drift",
        expRange(0.0f, kMaxDriftCents, 6.0f), 0.0f,
        FAttr().withLabel("ct")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(delaySuffix, i), 1}, p + "Delay",
        expRange(0.0f, 5.0f, 0.2f), 0.0f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(attackSuffix, i), 1}, p + "Attack",
        logRange(0.0002f, 5.0f), 0.005f,
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
        juce::ParameterID{oscParamId(liftSuffix, i), 1}, p + "Release Velocity",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        FAttr().withStringFromValueFunction(signedPercentText)));

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
      juce::ParameterID{noiseParamId(delaySuffix), 1}, "Noise Delay",
      expRange(0.0f, 5.0f, 0.2f), 0.0f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(attackSuffix), 1}, "Noise Attack",
      logRange(0.0002f, 5.0f), 0.005f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(decaySuffix), 1}, "Noise Decay",
      logRange(0.001f, 20.0f), 0.6f,
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

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(liftSuffix), 1}, "Noise Release Velocity",
      juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(signedPercentText)));

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

  return layout;
}

void Cache::connect(juce::AudioProcessorValueTreeState &apvts) {
  masterGain = apvts.getRawParameterValue(masterGainId);
  polyphony = apvts.getRawParameterValue(polyphonyId);
  bendRange = apvts.getRawParameterValue(bendRangeId);
  phaseReset = apvts.getRawParameterValue(phaseResetId);
  stretch = apvts.getRawParameterValue(stretchId);
  atSource = apvts.getRawParameterValue(atSourceId);
  slideDest = apvts.getRawParameterValue(slideDestId);
  track = apvts.getRawParameterValue(trackId);
  wobble = apvts.getRawParameterValue(wobbleId);
  temperament = apvts.getRawParameterValue(temperamentId);
  tuningRoot = apvts.getRawParameterValue(tuningRootId);
  referenceHz = apvts.getRawParameterValue(referenceHzId);
  safetyClip = apvts.getRawParameterValue(safetyClipId);
  mpe = apvts.getRawParameterValue(mpeId);
  lofiRate = apvts.getRawParameterValue(lofiRateId);
  lofiBits = apvts.getRawParameterValue(lofiBitsId);

  echo.on = apvts.getRawParameterValue(echoOnId);
  echo.mix = apvts.getRawParameterValue(echoMixId);
  echo.time = apvts.getRawParameterValue(echoTimeId);
  echo.feedback = apvts.getRawParameterValue(echoFeedbackId);
  echo.age = apvts.getRawParameterValue(echoAgeId);

  reverb.on = apvts.getRawParameterValue(reverbOnId);
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
    o.drift = apvts.getRawParameterValue(oscParamId(driftSuffix, i));
    o.delay = apvts.getRawParameterValue(oscParamId(delaySuffix, i));
    o.attack = apvts.getRawParameterValue(oscParamId(attackSuffix, i));
    o.decay = apvts.getRawParameterValue(oscParamId(decaySuffix, i));
    o.sustain = apvts.getRawParameterValue(oscParamId(sustainSuffix, i));
    o.swell = apvts.getRawParameterValue(oscParamId(swellSuffix, i));
    o.offLevel = apvts.getRawParameterValue(oscParamId(offLevelSuffix, i));
    o.release = apvts.getRawParameterValue(oscParamId(releaseSuffix, i));
    o.amRate = apvts.getRawParameterValue(oscParamId(amRateSuffix, i));
    o.amDepth = apvts.getRawParameterValue(oscParamId(amDepthSuffix, i));
    o.lift = apvts.getRawParameterValue(oscParamId(liftSuffix, i));
    o.vel = apvts.getRawParameterValue(oscParamId(velSuffix, i));
    o.at = apvts.getRawParameterValue(oscParamId(atSuffix, i));
    o.mute = apvts.getRawParameterValue(oscParamId(muteSuffix, i));
    o.solo = apvts.getRawParameterValue(oscParamId(soloSuffix, i));
    o.volume = apvts.getRawParameterValue(oscParamId(volumeSuffix, i));
    o.pan = apvts.getRawParameterValue(oscParamId(panSuffix, i));

    jassert(o.tune != nullptr && o.volume != nullptr);
  }

  noise.colour = apvts.getRawParameterValue(noiseParamId(colourSuffix));
  noise.delay = apvts.getRawParameterValue(noiseParamId(delaySuffix));
  noise.attack = apvts.getRawParameterValue(noiseParamId(attackSuffix));
  noise.decay = apvts.getRawParameterValue(noiseParamId(decaySuffix));
  noise.sustain = apvts.getRawParameterValue(noiseParamId(sustainSuffix));
  noise.swell = apvts.getRawParameterValue(noiseParamId(swellSuffix));
  noise.offLevel = apvts.getRawParameterValue(noiseParamId(offLevelSuffix));
  noise.release = apvts.getRawParameterValue(noiseParamId(releaseSuffix));
  noise.amRate = apvts.getRawParameterValue(noiseParamId(amRateSuffix));
  noise.amDepth = apvts.getRawParameterValue(noiseParamId(amDepthSuffix));
  noise.lift = apvts.getRawParameterValue(noiseParamId(liftSuffix));
  noise.vel = apvts.getRawParameterValue(noiseParamId(velSuffix));
  noise.at = apvts.getRawParameterValue(noiseParamId(atSuffix));
  noise.mute = apvts.getRawParameterValue(noiseParamId(muteSuffix));
  noise.solo = apvts.getRawParameterValue(noiseParamId(soloSuffix));
  noise.volume = apvts.getRawParameterValue(noiseParamId(volumeSuffix));
  noise.pan = apvts.getRawParameterValue(noiseParamId(panSuffix));

  jassert(noise.colour != nullptr && noise.volume != nullptr);
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
    o.pmDepthCents = c.pmDepth->load();
    o.startPhase = c.phase->load();
    o.driftCents = c.drift->load();
    o.delay = c.delay->load();
    o.attack = c.attack->load();
    o.decay = c.decay->load();
    o.sustain = c.sustain->load();
    o.swell = c.swell->load();
    o.offLevel = c.offLevel->load();
    o.release = c.release->load();
    o.amRateHz = c.amRate->load();
    o.amDepth = c.amDepth->load();
    o.liftAmount = c.lift->load();
    o.velAmount = c.vel->load();
    o.atAmount = c.at->load();
    o.volume = c.volume->load();
    o.pan = c.pan->load();

    const bool muted = c.mute->load() > 0.5f;
    const bool soloed = c.solo->load() > 0.5f;

    // Mixer convention: solo isolates, but an explicit mute still wins.
    o.audible = muted ? false : (anySolo ? soloed : true);
  }

  {
    auto &n = out.noise;

    n.colour = noise.colour->load();
    n.delay = noise.delay->load();
    n.attack = noise.attack->load();
    n.decay = noise.decay->load();
    n.sustain = noise.sustain->load();
    n.swell = noise.swell->load();
    n.offLevel = noise.offLevel->load();
    n.release = noise.release->load();
    n.amRateHz = noise.amRate->load();
    n.amDepth = noise.amDepth->load();
    n.liftAmount = noise.lift->load();
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
                 : juce::jlimit(0, count - 1,
                                (int)std::lround(p->load()));
    };

    out.global.temperament = (Temperament)pick(
        temperament, (int)Temperament::NumTemperaments);

    out.global.tuningRoot = pick(tuningRoot, (int)kPitchClassNames.size());

    out.global.referenceHz = (double)kReferenceHzChoices[(size_t)pick(
        referenceHz, (int)kReferenceHzChoices.size())];
  }
  out.global.safetyClip = safetyClip->load() > 0.5f;

  {
    const auto r = juce::jlimit(0, (int)kLofiRateChoices.size() - 1,
                                (int)std::lround(lofiRate->load()));
    const auto b = juce::jlimit(0, (int)kLofiBitChoices.size() - 1,
                                (int)std::lround(lofiBits->load()));

    out.lofi.rateHz = (double)kLofiRateChoices[(size_t)r];
    out.lofi.bits = kLofiBitChoices[(size_t)b];
  }

  out.echo.enabled = echo.on->load() > 0.5f;
  out.echo.mix = echo.mix->load();
  out.echo.timeSeconds = echo.time->load();
  out.echo.feedback = echo.feedback->load();
  out.echo.age = echo.age->load();

  out.reverb.enabled = reverb.on->load() > 0.5f;
  out.reverb.mix = reverb.mix->load();
  out.reverb.decaySeconds = reverb.decay->load();
  out.reverb.damping = reverb.damp->load();
  out.reverb.preDelaySeconds = reverb.preDelay->load();
}

} // namespace ovt::params
