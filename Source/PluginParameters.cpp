#include "PluginParameters.h"

#include "dsp/Harmonics.h"

namespace ovt::params {

namespace {
juce::NormalisableRange<float> rangeWithCentre(float lo, float hi,
                                               float centre) {
  juce::NormalisableRange<float> r(lo, hi);
  r.setSkewForCentre(centre);
  return r;
}

juce::String timeText(float seconds, int) {
  if (seconds < 1.0f)
    return juce::String(seconds * 1000.0f, seconds < 0.1f ? 1 : 0) + " ms";

  return juce::String(seconds, 2) + " s";
}

juce::String percentText(float v, int) {
  return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

/// Bipolar controls keep their sign, so the inverted half is unmistakable.
juce::String signedPercentText(float v, int) {
  const auto pc = juce::roundToInt(v * 100.0f);
  return (pc > 0 ? "+" : "") + juce::String(pc) + " %";
}

juce::String gainText(float v, int) {
  if (v <= 0.0001f)
    return "-inf dB";

  return juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
}

juce::String blendText(float v, int) {
  if (v <= 0.0005f)
    return "Equal temp.";
  if (v >= 0.9995f)
    return "Just";

  return juce::String(juce::roundToInt(v * 100.0f)) + "% just";
}

/// "H7 " — short enough to keep host parameter lists readable.
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

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{spreadId, 1}, "Stereo Spread",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{bendRangeId, 1}, "Pitch Bend Range", 0, 24, 2));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{phaseResetId, 1},
                                     "Phase Reset", true));

  layout.add(std::make_unique<BoolP>(juce::ParameterID{safetyClipId, 1},
                                     "Safety Clip", true));

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
        rangeWithCentre(0.01f, 30.0f, 2.0f), 4.0f, FAttr().withLabel("Hz")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(pmDepthSuffix, i), 1},
        p + "Pitch Mod Depth", rangeWithCentre(0.0f, 200.0f, 25.0f), 0.0f,
        FAttr().withLabel("ct")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(driftSuffix, i), 1}, p + "Drift",
        rangeWithCentre(0.0f, 25.0f, 6.0f), 0.0f, FAttr().withLabel("ct")));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(delaySuffix, i), 1}, p + "Delay",
        rangeWithCentre(0.0f, 5.0f, 0.2f), 0.0f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(attackSuffix, i), 1}, p + "Attack",
        rangeWithCentre(0.0005f, 5.0f, 0.03f), 0.005f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(decaySuffix, i), 1}, p + "Decay",
        rangeWithCentre(0.001f, 20.0f, 0.5f), 0.6f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(sustainSuffix, i), 1}, p + "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        FAttr().withStringFromValueFunction(percentText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(releaseSuffix, i), 1}, p + "Release",
        rangeWithCentre(0.001f, 20.0f, 0.5f), 0.4f,
        FAttr().withStringFromValueFunction(timeText)));

    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(amRateSuffix, i), 1}, p + "Amp Mod Rate",
        rangeWithCentre(0.01f, 30.0f, 4.0f), 4.0f, FAttr().withLabel("Hz")));

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

    // Square-law fader: half travel lands at a quarter of full amplitude, which
    // is roughly where the ear expects "half as loud".
    layout.add(std::make_unique<FloatP>(
        juce::ParameterID{oscParamId(volumeSuffix, i), 1}, p + "Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f),
        defaultVolumeFor(i), FAttr().withStringFromValueFunction(gainText)));
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
      rangeWithCentre(0.0f, 5.0f, 0.2f), 0.0f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(attackSuffix), 1}, "Noise Attack",
      rangeWithCentre(0.0005f, 5.0f, 0.03f), 0.005f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(decaySuffix), 1}, "Noise Decay",
      rangeWithCentre(0.001f, 20.0f, 0.5f), 0.6f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(sustainSuffix), 1}, "Noise Sustain",
      juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f,
      FAttr().withStringFromValueFunction(percentText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(releaseSuffix), 1}, "Noise Release",
      rangeWithCentre(0.001f, 20.0f, 0.5f), 0.4f,
      FAttr().withStringFromValueFunction(timeText)));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(amRateSuffix), 1}, "Noise Amp Mod Rate",
      rangeWithCentre(0.01f, 30.0f, 4.0f), 4.0f, FAttr().withLabel("Hz")));

  layout.add(std::make_unique<FloatP>(
      juce::ParameterID{noiseParamId(amDepthSuffix), 1}, "Noise Amp Mod Depth",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
      FAttr().withStringFromValueFunction(percentText)));

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
      juce::ParameterID{noiseParamId(volumeSuffix), 1}, "Noise Level",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f), 0.0f,
      FAttr().withStringFromValueFunction(gainText)));

  return layout;
}

void Cache::connect(juce::AudioProcessorValueTreeState &apvts) {
  masterGain = apvts.getRawParameterValue(masterGainId);
  polyphony = apvts.getRawParameterValue(polyphonyId);
  spread = apvts.getRawParameterValue(spreadId);
  bendRange = apvts.getRawParameterValue(bendRangeId);
  phaseReset = apvts.getRawParameterValue(phaseResetId);
  safetyClip = apvts.getRawParameterValue(safetyClipId);

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &o = osc[(size_t)i];

    o.tune = apvts.getRawParameterValue(oscParamId(tuneSuffix, i));
    o.pmRate = apvts.getRawParameterValue(oscParamId(pmRateSuffix, i));
    o.pmDepth = apvts.getRawParameterValue(oscParamId(pmDepthSuffix, i));
    o.drift = apvts.getRawParameterValue(oscParamId(driftSuffix, i));
    o.delay = apvts.getRawParameterValue(oscParamId(delaySuffix, i));
    o.attack = apvts.getRawParameterValue(oscParamId(attackSuffix, i));
    o.decay = apvts.getRawParameterValue(oscParamId(decaySuffix, i));
    o.sustain = apvts.getRawParameterValue(oscParamId(sustainSuffix, i));
    o.release = apvts.getRawParameterValue(oscParamId(releaseSuffix, i));
    o.amRate = apvts.getRawParameterValue(oscParamId(amRateSuffix, i));
    o.amDepth = apvts.getRawParameterValue(oscParamId(amDepthSuffix, i));
    o.vel = apvts.getRawParameterValue(oscParamId(velSuffix, i));
    o.at = apvts.getRawParameterValue(oscParamId(atSuffix, i));
    o.mute = apvts.getRawParameterValue(oscParamId(muteSuffix, i));
    o.solo = apvts.getRawParameterValue(oscParamId(soloSuffix, i));
    o.volume = apvts.getRawParameterValue(oscParamId(volumeSuffix, i));

    jassert(o.tune != nullptr && o.volume != nullptr);
  }

  noise.colour = apvts.getRawParameterValue(noiseParamId(colourSuffix));
  noise.delay = apvts.getRawParameterValue(noiseParamId(delaySuffix));
  noise.attack = apvts.getRawParameterValue(noiseParamId(attackSuffix));
  noise.decay = apvts.getRawParameterValue(noiseParamId(decaySuffix));
  noise.sustain = apvts.getRawParameterValue(noiseParamId(sustainSuffix));
  noise.release = apvts.getRawParameterValue(noiseParamId(releaseSuffix));
  noise.amRate = apvts.getRawParameterValue(noiseParamId(amRateSuffix));
  noise.amDepth = apvts.getRawParameterValue(noiseParamId(amDepthSuffix));
  noise.vel = apvts.getRawParameterValue(noiseParamId(velSuffix));
  noise.at = apvts.getRawParameterValue(noiseParamId(atSuffix));
  noise.mute = apvts.getRawParameterValue(noiseParamId(muteSuffix));
  noise.solo = apvts.getRawParameterValue(noiseParamId(soloSuffix));
  noise.volume = apvts.getRawParameterValue(noiseParamId(volumeSuffix));

  jassert(noise.colour != nullptr && noise.volume != nullptr);
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
    o.driftCents = c.drift->load();
    o.delay = c.delay->load();
    o.attack = c.attack->load();
    o.decay = c.decay->load();
    o.sustain = c.sustain->load();
    o.release = c.release->load();
    o.amRateHz = c.amRate->load();
    o.amDepth = c.amDepth->load();
    o.velAmount = c.vel->load();
    o.atAmount = c.at->load();
    o.volume = c.volume->load();

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
    n.release = noise.release->load();
    n.amRateHz = noise.amRate->load();
    n.amDepth = noise.amDepth->load();
    n.velAmount = noise.vel->load();
    n.atAmount = noise.at->load();
    n.volume = noise.volume->load();

    const bool muted = noise.mute->load() > 0.5f;
    const bool soloed = noise.solo->load() > 0.5f;
    n.audible = muted ? false : (anySolo ? soloed : true);
  }

  out.global.masterGain =
      juce::Decibels::decibelsToGain(masterGain->load(), -60.0f);
  out.global.stereoSpread = spread->load();
  out.global.bendSemitones = bendNormalised * bendRange->load();
  out.global.phaseReset = phaseReset->load() > 0.5f;
  out.global.safetyClip = safetyClip->load() > 0.5f;
}

} // namespace ovt::params
