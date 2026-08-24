#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
//
// The two text rows are the height they are because of what stands in them.
// Seven-segment digits are as wide as they are tall, so a row three pixels
// shorter caps the cell width and a reading like -13.7 runs its figures into
// each other. Three pixels on each of the two buys legible digits.
constexpr int kRowHeights[kNumRows] = {
    26, // Header
    38, // TuneKnob
    16, // TuneText
    30, // Phase
    15, // PitchModHeading
    30, // PmRate
    30, // PmDepth
    30, // Drift
    15, // EnvHeading
    30, // Delay
    30, // Attack
    30, // Decay
    30, // Sustain
    15, // KeyOffHeading
    30, // Swell
    30, // OffLevel
    30, // Release
    30, // Lift
    15, // AmpModHeading
    30, // AmRate
    30, // AmDepth
    15, // OutputHeading
    30, // Velocity
    30, // Aftertouch
    30, // Pan
    20, // MuteSolo
    -1, // Fader
    16  // FaderText
};

constexpr int kMinFaderHeight = 60;
constexpr int kIdealFaderHeight = 92;

int fixedHeight(SectionMask collapsed) {
  int total = 0;

  for (int i = 0; i < kNumRows; ++i)
    if (kRowHeights[i] > 0 && !rowIsCollapsed((Row)i, collapsed))
      total += kRowHeights[i];

  return total;
}
} // namespace

Row sectionHeading(Section s) {
  switch (s) {
  case Section::PitchMod:
    return Row::PitchModHeading;
  case Section::Envelope:
    return Row::EnvHeading;
  case Section::KeyOff:
    return Row::KeyOffHeading;
  case Section::AmpMod:
    return Row::AmpModHeading;
  case Section::Output:
    return Row::OutputHeading;
  case Section::NumSections:
    break;
  }

  return kNoRow;
}

Section sectionOf(Row r) {
  switch (r) {
  case Row::PitchModHeading:
  case Row::PmRate:
  case Row::PmDepth:
  case Row::Drift:
    return Section::PitchMod;

  case Row::EnvHeading:
  case Row::Delay:
  case Row::Attack:
  case Row::Decay:
  case Row::Sustain:
    return Section::Envelope;

  case Row::KeyOffHeading:
  case Row::Swell:
  case Row::OffLevel:
  case Row::Release:
  case Row::Lift:
    return Section::KeyOff;

  case Row::AmpModHeading:
  case Row::AmRate:
  case Row::AmDepth:
    return Section::AmpMod;

  case Row::OutputHeading:
  case Row::Velocity:
  case Row::Aftertouch:
  case Row::Pan:
    return Section::Output;

  // Always on screen. Spelt out rather than left to a default so a new row
  // has to be put in a section, or deliberately kept out of one, before it
  // will build.
  case Row::Header:
  case Row::TuneKnob:
  case Row::TuneText:
  case Row::Phase:
  case Row::MuteSolo:
  case Row::Fader:
  case Row::FaderText:
  case Row::NumRows:
    break;
  }

  return Section::NumSections;
}

bool rowIsCollapsed(Row r, SectionMask collapsed) {
  const auto s = sectionOf(r);

  // The heading is what is left to click on, so it never folds with the rest.
  return s != Section::NumSections && sectionHeading(s) != r &&
         isCollapsed(collapsed, s);
}

Section headingSectionAt(const RowBounds &rows, juce::Point<int> p) {
  for (int i = 0; i < kNumSections; ++i) {
    const auto s = (Section)i;

    if (rows[(size_t)sectionHeading(s)].contains(p))
      return s;
  }

  return Section::NumSections;
}

int collapsedRowsHeight(SectionMask collapsed) {
  int total = 0;

  for (int i = 0; i < kNumRows; ++i)
    if (kRowHeights[i] > 0 && rowIsCollapsed((Row)i, collapsed))
      total += kRowHeights[i];

  return total;
}

int preferredStripHeight(SectionMask collapsed) {
  return fixedHeight(collapsed) + kIdealFaderHeight;
}

int minimumStripHeight(SectionMask collapsed) {
  return fixedHeight(collapsed) + kMinFaderHeight;
}

RowBounds layoutRows(juce::Rectangle<int> area, SectionMask collapsed) {
  const int flexible =
      juce::jmax(kMinFaderHeight, area.getHeight() - fixedHeight(collapsed));

  RowBounds out;
  auto remaining = area;

  for (int i = 0; i < kNumRows; ++i) {
    // A folded row keeps its place in the array and takes no height, so
    // everything that reads RowBounds carries on working and simply lays out
    // an empty rectangle. The window shrinks by the same amount, so the fader
    // keeps the height it had rather than stretching into the gap.
    const int h = rowIsCollapsed((Row)i, collapsed) ? 0
                  : kRowHeights[i] > 0              ? kRowHeights[i]
                                                    : flexible;

    out[(size_t)i] = remaining.removeFromTop(h);
  }

  return out;
}

namespace {
/// Rows that hold something you can point at.
bool rowHasControl(Row r) {
  switch (r) {
  case Row::TuneKnob:
  case Row::Phase:
  case Row::PmRate:
  case Row::PmDepth:
  case Row::Drift:
  case Row::Delay:
  case Row::Attack:
  case Row::Decay:
  case Row::Sustain:
  case Row::Swell:
  case Row::OffLevel:
  case Row::Release:
  case Row::Lift:
  case Row::AmRate:
  case Row::AmDepth:
  case Row::Velocity:
  case Row::Aftertouch:
  case Row::Pan:
  case Row::MuteSolo:
  case Row::Fader:
    return true;

  // The rest carry no control: the header, the rules between the groups, and
  // the two readouts, which answer with the control above them instead. Spelt
  // out rather than left to a default so a new row has to be placed on one
  // side or the other before it will build.
  case Row::Header:
  case Row::TuneText:
  case Row::PitchModHeading:
  case Row::EnvHeading:
  case Row::KeyOffHeading:
  case Row::AmpModHeading:
  case Row::OutputHeading:
  case Row::FaderText:
  case Row::NumRows:
    return false;
  }

  return false;
}
} // namespace

Row controlRowAt(const RowBounds &rows, juce::Point<int> p) {
  for (int i = 0; i < kNumRows; ++i) {
    const auto &row = rows[(size_t)i];

    // Only the vertical span is tested. The strips carry a couple of pixels of
    // margin either side, and losing the highlight in that margin would make
    // the band flicker as the pointer crosses from one channel to the next.
    if (p.y < row.getY() || p.y >= row.getBottom())
      continue;

    // Two rows answer with the control above them, and everything else
    // answers for itself. Two exceptions read better as exceptions than as a
    // switch over twenty-eight rows to say something about two of them.
    const auto here = (Row)i;

    if (here == Row::TuneText)
      return Row::TuneKnob;

    if (here == Row::FaderText)
      return Row::Fader;

    return rowHasControl(here) ? here : kNoRow;
  }

  return kNoRow;
}

bool rowShowsHighlight(Row row) {
  return row != kNoRow && row != Row::Fader && row != Row::MuteSolo;
}

void repaintRowHighlight(juce::Component &c, const RowBounds &rows, Row row) {
  if (!rowShowsHighlight(row))
    return;

  c.repaint(rows[(size_t)row]);
}

void mergeIntoRows(juce::Array<juce::Rectangle<int>> &regions) {
  for (int i = 0; i < regions.size(); ++i) {
    // Copies, not references into the array. Array::remove shrinks the
    // storage once the capacity is more than twice the size, and a reference
    // held across that is pointing at freed memory. Whether it survives comes
    // down to the allocator: shrinking in place, as glibc does, hides it, and
    // moving the block, as macOS does, gives garbage.
    auto band = regions.getUnchecked(i);

    for (int j = regions.size(); --j > i;) {
      const auto other = regions.getUnchecked(j);

      if (other.getY() != band.getY() ||
          other.getHeight() != band.getHeight())
        continue;

      band = band.getUnion(other);
      regions.remove(j);
    }

    regions.set(i, band);
  }
}

void coalesceRegions(juce::Array<juce::Rectangle<int>> &regions, int limit) {
  limit = juce::jmax(1, limit);

  // Left to right, so the pairs considered for merging are neighbours in the
  // mixer rather than opposite ends of it.
  std::sort(regions.begin(), regions.end(),
            [](const juce::Rectangle<int> &a, const juce::Rectangle<int> &b) {
              return a.getX() < b.getX();
            });

  while (regions.size() > limit) {
    auto bestCost = std::numeric_limits<int64_t>::max();
    int bestAt = 0;

    // What merging two neighbours would add: the area of the rectangle that
    // holds both, less the two areas it replaces.
    for (int i = 0; i + 1 < regions.size(); ++i) {
      const auto &a = regions.getReference(i);
      const auto &b = regions.getReference(i + 1);
      const auto merged = a.getUnion(b);

      const auto cost = (int64_t)merged.getWidth() * merged.getHeight() -
                        (int64_t)a.getWidth() * a.getHeight() -
                        (int64_t)b.getWidth() * b.getHeight();

      if (cost < bestCost) {
        bestCost = cost;
        bestAt = i;
      }
    }

    regions.setUnchecked(bestAt, regions.getReference(bestAt).getUnion(
                                     regions.getReference(bestAt + 1)));
    regions.remove(bestAt + 1);
  }
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
  case Row::Phase:
    return "phase";
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
  case Row::KeyOffHeading:
    return "KEY OFF";
  case Row::Swell:
    return "swell";
  case Row::OffLevel:
    return "level";
  case Row::Release:
    return "release";
  case Row::Lift:
    return "lift";
  case Row::AmpModHeading:
    return "AMP MOD";
  case Row::OutputHeading:
    return "OUTPUT";
  case Row::Velocity:
    return "velocity";
  case Row::Aftertouch:
    return "aftertouch";
  case Row::Pan:
    return "pan";
  case Row::AmRate:
    return "rate";
  case Row::AmDepth:
    return "depth";
  case Row::MuteSolo:
    return "MUTE / SOLO";
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

const char *linkScopeName(LinkScope s) {
  switch (s) {
  case LinkScope::All:
    return "All";
  case LinkScope::SameInterval:
    return "Same interval";
  case LinkScope::Odd:
    return "Odd harmonics";
  case LinkScope::Even:
    return "Even harmonics";

  case LinkScope::NumScopes:
  default:
    jassertfalse;
    return "All";
  }
}

const char *linkCurveName(LinkCurve c) {
  switch (c) {
  case LinkCurve::Uniform:
    return "Uniform";
  case LinkCurve::TiltUp:
    return "Tilt up";
  case LinkCurve::TiltDown:
    return "Tilt down";
  case LinkCurve::Spread:
    return "Spread / gather";

  case LinkCurve::NumCurves:
  default:
    jassertfalse;
    return "Uniform";
  }
}

namespace {
/// Menu ids. Scopes and curves are offset so the two lists cannot be confused
/// for one another, and neither can be confused for the switch.
constexpr int kLinkToggleId = 1;
constexpr int kScopeBaseId = 100;
constexpr int kCurveBaseId = 200;
} // namespace

juce::PopupMenu buildLinkMenu(const LinkSettings &settings) {
  juce::PopupMenu m;

  m.addItem(kLinkToggleId, "LINK", true, settings.enabled);
  m.addSeparator();

  // The two lists are only worth reading while the switch is on, so they grey
  // out with it rather than disappearing, which would move everything else.
  m.addSectionHeader("Scope");
  for (int i = 0; i < (int)LinkScope::NumScopes; ++i)
    m.addItem(kScopeBaseId + i, linkScopeName((LinkScope)i), settings.enabled,
              i == (int)settings.scope);

  m.addSeparator();
  m.addSectionHeader("Curve");
  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    m.addItem(kCurveBaseId + i, linkCurveName((LinkCurve)i), settings.enabled,
              i == (int)settings.curve);

  return m;
}

bool applyLinkMenuChoice(int id, LinkSettings &settings) {
  if (id == kLinkToggleId) {
    settings.enabled = !settings.enabled;
    return true;
  }

  if (id >= kCurveBaseId && id < kCurveBaseId + (int)LinkCurve::NumCurves) {
    settings.curve = (LinkCurve)(id - kCurveBaseId);
    return true;
  }

  if (id >= kScopeBaseId && id < kScopeBaseId + (int)LinkScope::NumScopes) {
    settings.scope = (LinkScope)(id - kScopeBaseId);
    return true;
  }

  return false;
}

float linkCurveWeight(LinkCurve c, int index0, int sourceIndex) {
  // How far up or down the series this strip sits from the one being dragged,
  // as -1 to +1.
  const auto distance =
      (float)(juce::jlimit(0, kNumHarmonics - 1, index0) - sourceIndex) /
      (float)(kNumHarmonics - 1);

  // Geometric rather than linear, so the far ends stay in proportion and the
  // grabbed strip lands on exactly 1 whichever direction the tilt runs.
  constexpr float ratio = 3.0f;

  switch (c) {
  case LinkCurve::TiltUp:
    return std::pow(ratio, distance);
  case LinkCurve::TiltDown:
    return std::pow(ratio, -distance);

  case LinkCurve::Uniform:
  case LinkCurve::Spread:
  case LinkCurve::NumCurves:
  default:
    return 1.0f;
  }
}

float linkedValue(LinkCurve curve, float baseline, float delta, float weight,
                  float jitter, float target) {
  float value = baseline;

  if (curve == LinkCurve::Spread) {
    // Pushing up scatters each strip along the direction it was given. Pulling
    // down gathers them onto the strip being dragged rather than onto a fixed
    // average, which keeps the knob in your hand as the thing everything
    // collapses towards instead of leaving it stranded off to one side. Half a
    // drag is enough to arrive, so the gesture completes in one movement.
    const auto gather = juce::jlimit(0.0f, 1.0f, -delta * 2.0f);

    value = delta >= 0.0f ? baseline + delta * jitter
                          : baseline + (target - baseline) * gather;
  } else {
    value = baseline + delta * weight;
  }

  return juce::jlimit(0.0f, 1.0f, value);
}

const char *roleLabel(Role r) {
  switch (r) {
  case Role::Tune:
    return "tuning";
  case Role::Phase:
    return "start phase";
  case Role::PmRate:
    return "pitch modulation rate";
  case Role::PmDepth:
    return "pitch modulation depth";
  case Role::Drift:
    return "drift";
  case Role::Delay:
    return "envelope delay";
  case Role::Attack:
    return "attack";
  case Role::Decay:
    return "decay";
  case Role::Sustain:
    return "sustain";
  case Role::Swell:
    return "key off swell";
  case Role::OffLevel:
    return "key off level";
  case Role::Release:
    return "release";
  case Role::Lift:
    return "lift";
  case Role::AmRate:
    return "tremolo rate";
  case Role::AmDepth:
    return "tremolo depth";
  case Role::Velocity:
    return "velocity amount";
  case Role::Aftertouch:
    return "pressure amount";
  case Role::Pan:
    return "pan";
  case Role::Volume:
    return "level";

  // Listed rather than defaulted, so a new role has to be given a name before
  // it will build. An unnamed control is invisible to a screen reader.
  case Role::NumRoles:
    break;
  }

  return "";
}

Role roleForSuffix(const char *suffix) {
  if (suffix == nullptr)
    return Role::NumRoles;

  for (int i = 0; i < (int)Role::NumRoles; ++i)
    if (juce::String(roleSuffix((Role)i)) == suffix)
      return (Role)i;

  return Role::NumRoles;
}

const char *roleSuffix(Role r) {
  switch (r) {
  case Role::Tune:
    return params::tuneSuffix;
  case Role::PmRate:
    return params::pmRateSuffix;
  case Role::PmDepth:
    return params::pmDepthSuffix;
  case Role::Phase:
    return params::phaseSuffix;
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
  case Role::Swell:
    return params::swellSuffix;
  case Role::OffLevel:
    return params::offLevelSuffix;
  case Role::Release:
    return params::releaseSuffix;
  case Role::Lift:
    return params::liftSuffix;
  case Role::AmRate:
    return params::amRateSuffix;
  case Role::AmDepth:
    return params::amDepthSuffix;
  case Role::Velocity:
    return params::velSuffix;
  case Role::Aftertouch:
    return params::atSuffix;
  case Role::Pan:
    return params::panSuffix;
  case Role::Volume:
    return params::volumeSuffix;

  case Role::NumRoles:
  default:
    jassertfalse;
    return params::tuneSuffix;
  }
}

bool roleForRow(Row r, Role &out) {
  switch (r) {
  case Row::TuneKnob:
    out = Role::Tune;
    return true;
  case Row::PmRate:
    out = Role::PmRate;
    return true;
  case Row::PmDepth:
    out = Role::PmDepth;
    return true;
  case Row::Phase:
    out = Role::Phase;
    return true;
  case Row::Drift:
    out = Role::Drift;
    return true;
  case Row::Delay:
    out = Role::Delay;
    return true;
  case Row::Attack:
    out = Role::Attack;
    return true;
  case Row::Decay:
    out = Role::Decay;
    return true;
  case Row::Sustain:
    out = Role::Sustain;
    return true;
  case Row::Swell:
    out = Role::Swell;
    return true;
  case Row::OffLevel:
    out = Role::OffLevel;
    return true;
  case Row::Release:
    out = Role::Release;
    return true;
  case Row::Lift:
    out = Role::Lift;
    return true;
  case Row::AmRate:
    out = Role::AmRate;
    return true;
  case Row::AmDepth:
    out = Role::AmDepth;
    return true;
  case Row::Velocity:
    out = Role::Velocity;
    return true;
  case Row::Aftertouch:
    out = Role::Aftertouch;
    return true;
  case Row::Pan:
    out = Role::Pan;
    return true;
  case Row::Fader:
    out = Role::Volume;
    return true;

  // The rows LINK has nothing to reach: the header, the rules, the two
  // readouts, and mute and solo, which are switches rather than values.
  case Row::Header:
  case Row::TuneText:
  case Row::PitchModHeading:
  case Row::EnvHeading:
  case Row::KeyOffHeading:
  case Row::AmpModHeading:
  case Row::OutputHeading:
  case Row::MuteSolo:
  case Row::FaderText:
  case Row::NumRows:
    return false;
  }

  return false;
}

} // namespace ovt::ui
