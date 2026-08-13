#include "Theme.h"

#include "../PluginParameters.h"

namespace ovt::ui {

juce::Colour intervalColour(int pitchClass) {
  // Hues chosen so neighbouring interval classes stay distinguishable at 38 px
  // wide.
  static const float hues[12] = {
      45.0f,  // prime/octave  gold
      10.0f,  // minor second  red
      75.0f,  // major second  chartreuse
      150.0f, // minor third   spring green
      115.0f, // major third   green
      175.0f, // fourth        teal
      320.0f, // tritone       magenta
      195.0f, // fifth         cyan
      265.0f, // minor sixth   indigo
      285.0f, // major sixth   violet
      30.0f,  // minor seventh orange
      220.0f  // major seventh blue
  };

  const auto pc = (size_t)(((pitchClass % 12) + 12) % 12);
  return juce::Colour::fromHSV(hues[pc] / 360.0f, 0.55f, 0.88f, 1.0f);
}

namespace {
// -1 marks the one flexible row (the fader), which absorbs any leftover height.
constexpr int kRowHeights[kNumRows] = {
    26, // Header
    38, // TuneKnob
    13, // TuneText
    15, // PitchModHeading
    30, // PmRate
    30, // PmDepth
    15, // EnvHeading
    28, // Attack
    28, // Decay
    28, // Sustain
    28, // Release
    15, // AmpModHeading
    30, // AmRate
    30, // AmDepth
    20, // MuteSolo
    -1, // Fader
    13  // FaderText
};

constexpr int kMinFaderHeight = 60;
constexpr int kIdealFaderHeight = 92;

constexpr int fixedHeight() {
  int total = 0;
  for (auto h : kRowHeights)
    if (h > 0)
      total += h;

  return total;
}
} // namespace

int preferredStripHeight() { return fixedHeight() + kIdealFaderHeight; }

int minimumStripHeight() { return fixedHeight() + kMinFaderHeight; }

RowBounds layoutRows(juce::Rectangle<int> area) {
  const int flexible =
      juce::jmax(kMinFaderHeight, area.getHeight() - fixedHeight());

  RowBounds out;
  auto remaining = area;

  for (int i = 0; i < kNumRows; ++i) {
    const int h = kRowHeights[i] > 0 ? kRowHeights[i] : flexible;
    out[(size_t)i] = remaining.removeFromTop(h);
  }

  return out;
}

const char *rowLabel(Row r) {
  switch (r) {
  case Row::TuneKnob:
    return "TUNE";
  case Row::PitchModHeading:
    return "PITCH MOD";
  case Row::PmRate:
    return "rate";
  case Row::PmDepth:
    return "depth";
  case Row::EnvHeading:
    return "ENVELOPE";
  case Row::Attack:
    return "attack";
  case Row::Decay:
    return "decay";
  case Row::Sustain:
    return "sustain";
  case Row::Release:
    return "release";
  case Row::AmpModHeading:
    return "AMP MOD";
  case Row::AmRate:
    return "rate";
  case Row::AmDepth:
    return "depth";
  case Row::MuteSolo:
    return "M / S";
  case Row::Fader:
    return "LEVEL";

  case Row::TuneText:
    return "cents";
  case Row::FaderText:
    return "dB";

  case Row::Header:
  case Row::NumRows:
  default:
    return nullptr;
  }
}

const char *roleSuffix(Role r) {
  switch (r) {
  case Role::Tune:
    return params::tuneSuffix;
  case Role::PmRate:
    return params::pmRateSuffix;
  case Role::PmDepth:
    return params::pmDepthSuffix;
  case Role::Attack:
    return params::attackSuffix;
  case Role::Decay:
    return params::decaySuffix;
  case Role::Sustain:
    return params::sustainSuffix;
  case Role::Release:
    return params::releaseSuffix;
  case Role::AmRate:
    return params::amRateSuffix;
  case Role::AmDepth:
    return params::amDepthSuffix;
  case Role::Volume:
    return params::volumeSuffix;

  case Role::NumRoles:
  default:
    jassertfalse;
    return params::tuneSuffix;
  }
}

} // namespace ovt::ui
