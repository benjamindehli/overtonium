#include "Theme.h"

#include <cmath>

#include "../PluginParameters.h"

namespace ovt::ui {

juce::Colour intervalColour(int pitchClass) {
  const auto pc = ((pitchClass % 12) + 12) % 12;
  const auto t = (float)pc / 11.0f;

  // The band is the middle of a blue to yellow sweep, cropped at both ends.
  // The full sweep put pure blue and pure yellow at the extremes, which was
  // louder than a control surface wants. The hue passes through 360, so the
  // wrap has to happen after interpolating rather than before.
  const auto hue = 238.1f + t * (411.5f - 238.1f);

  // Saturation and value fall towards the warm end because yellow reads far
  // brighter than blue at equal nominal value. Without this the sevenths would
  // be several times the luminance of the octaves and would visually swamp
  // them.
  const auto sat = 0.730f + t * (0.585f - 0.730f);
  const auto val = 0.954f + t * (0.863f - 0.954f);

  return juce::Colour::fromHSV(std::fmod(hue, 360.0f) / 360.0f, sat, val, 1.0f);
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
    30, // Drift
    15, // EnvHeading
    28, // Delay
    28, // Attack
    28, // Decay
    28, // Sustain
    28, // Release
    15, // AmpModHeading
    30, // AmRate
    30, // AmDepth
    15, // OutputHeading
    28, // Velocity
    28, // Aftertouch
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
  case Row::Drift:
    return "drift";
  case Row::EnvHeading:
    return "ENVELOPE";
  case Row::Delay:
    return "delay";
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
  case Row::OutputHeading:
    return "OUTPUT";
  case Row::Velocity:
    return "velocity";
  case Row::Aftertouch:
    return "aftertouch";
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
  case Role::Drift:
    return params::driftSuffix;
  case Role::Delay:
    return params::delaySuffix;
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
  case Role::Velocity:
    return params::velSuffix;
  case Role::Aftertouch:
    return params::atSuffix;
  case Role::Volume:
    return params::volumeSuffix;

  case Role::NumRoles:
  default:
    jassertfalse;
    return params::tuneSuffix;
  }
}

} // namespace ovt::ui
