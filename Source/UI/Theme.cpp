#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
/// The macro colours: none first, then eight hues spread around the wheel
/// at one lightness, told apart on a knob's ring.
const juce::Colour kMacroColours[] = {
    juce::Colour(0x00000000), juce::Colour(0xffe0584a),
    juce::Colour(0xffe08a3c), juce::Colour(0xffd8c24a),
    juce::Colour(0xff64c07a), juce::Colour(0xff52c0c0),
    juce::Colour(0xff5a8fe0), juce::Colour(0xffb07bd4),
    juce::Colour(0xffd665b0)};
} // namespace

void drawGearIcon(juce::Graphics &g, juce::Rectangle<float> area,
                  juce::Colour colour) {
  const auto centre = area.getCentre();
  // The teeth are the outermost thing drawn, so this radius sets the margin.
  const auto outer = juce::jmin(area.getWidth(), area.getHeight()) * 0.38f;
  const auto root = outer * 0.74f;

  // One outline stepping between the two radii, with the bore punched by an
  // even-odd fill, since the colour behind the button is not known here.
  constexpr int teeth = 8;
  constexpr float tooth = 0.46f; // of each tooth's share of the circle

  juce::Path path;

  for (int i = 0; i < teeth; ++i) {
    const auto step = juce::MathConstants<float>::twoPi / (float)teeth;
    const auto from = (float)i * step;

    const auto at = [&](float angle, float radius) {
      return juce::Point<float>(centre.x + radius * std::sin(angle),
                                centre.y - radius * std::cos(angle));
    };

    if (i == 0)
      path.startNewSubPath(at(from, root));
    else
      path.lineTo(at(from, root));

    path.lineTo(at(from + step * (0.5f - tooth * 0.5f), outer));
    path.lineTo(at(from + step * (0.5f + tooth * 0.5f), outer));
    path.lineTo(at(from + step, root));
  }

  path.closeSubPath();
  path.addEllipse(centre.x - outer * 0.34f, centre.y - outer * 0.34f,
                  outer * 0.68f, outer * 0.68f);
  path.setUsingNonZeroWinding(false);

  g.setColour(colour);
  g.fillPath(path);
}

void drawSharedIcon(juce::Graphics &g, juce::Rectangle<float> area,
                    juce::Colour colour) {
  // Three short strokes from the left that meet at one point and run on as a
  // single line. Fractions of the side it is given, so it scales.
  const auto s = juce::jmin(area.getWidth(), area.getHeight());
  const auto c = area.getCentre();
  const auto left = c.x - s * 0.40f;
  const auto join = c.x + s * 0.05f;
  const auto right = c.x + s * 0.40f;
  const auto spread = s * 0.30f;

  juce::Path p;

  for (int i = -1; i <= 1; ++i) {
    p.startNewSubPath(left, c.y + (float)i * spread);
    p.lineTo(join, c.y);
  }

  p.startNewSubPath(join, c.y);
  p.lineTo(right, c.y);

  g.setColour(colour);
  g.strokePath(p, juce::PathStrokeType(juce::jmax(1.2f, s * 0.11f),
                                       juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
}

void drawMacroIcon(juce::Graphics &g, juce::Rectangle<float> area,
                   juce::Colour colour) {
  // Three faders with their caps at different places. Fractions of the height,
  // since the area is the lamp cap, which is smaller than the button.
  const auto h = area.getHeight();
  const auto w = juce::jmin(area.getWidth() * 0.78f, h * 1.5f);
  const auto centre = area.getCentre();
  const auto left = centre.x - w * 0.5f;

  const auto step = h * 0.26f;
  const auto capH = h * 0.30f;
  const auto capW = juce::jmax(2.4f, w * 0.17f);
  const auto track = juce::jmax(1.0f, h * 0.1f);

  const float at[] = {0.72f, 0.38f, 0.58f};

  for (int i = 0; i < 3; ++i) {
    const auto y = centre.y + ((float)i - 1.0f) * step;

    // The rules a step back from the caps.
    g.setColour(colour.withMultipliedAlpha(0.62f));
    g.fillRoundedRectangle(left, y - track * 0.5f, w, track, track * 0.5f);

    g.setColour(colour);
    g.fillRoundedRectangle(left + w * at[i] - capW * 0.5f, y - capH * 0.5f,
                           capW, capH, capW * 0.36f);
  }
}

void drawKeyboardIcon(juce::Graphics &g, juce::Rectangle<float> area,
                      juce::Colour colour) {
  // Three white keys and the two black ones between them, in fractions of the
  // cap's height so the icons in the block match at any zoom.
  const auto h = area.getHeight();
  const auto outline = juce::jmax(1.0f, h * 0.07f);
  const auto keys = area.withSizeKeepingCentre(h * 0.88f, h * 0.65f);
  const auto white = keys.getWidth() / 3.0f;
  const auto black = juce::jmax(2.0f, h * 0.24f);

  g.setColour(colour);
  g.drawRoundedRectangle(keys, outline, outline);

  for (int i = 1; i < 3; ++i) {
    const auto x = keys.getX() + white * (float)i;

    g.fillRect(x - outline * 0.4f, keys.getY(), outline * 0.8f,
               keys.getHeight());
    g.fillRect(x - black * 0.5f, keys.getY(), black, keys.getHeight() * 0.58f);
  }
}

void drawTuningForkIcon(juce::Graphics &g, juce::Rectangle<float> area,
                        juce::Colour colour) {
  // Two tines on a short stem, as tall as the keyboard icon.
  const auto h = area.getHeight();
  const auto c = area.getCentre();
  const auto top = c.y - h * 0.325f;
  const auto bottom = c.y + h * 0.325f;
  const auto half = h * 0.21f;
  const auto bend = c.y + h * 0.12f;
  const auto shoulder = bend - h * 0.15f;

  juce::Path fork;
  fork.startNewSubPath(c.x - half, top);
  fork.lineTo(c.x - half, shoulder);
  fork.quadraticTo(c.x - half, bend, c.x, bend);
  fork.quadraticTo(c.x + half, bend, c.x + half, shoulder);
  fork.lineTo(c.x + half, top);
  fork.startNewSubPath(c.x, bend);
  fork.lineTo(c.x, bottom);

  g.setColour(colour);
  g.strokePath(fork, juce::PathStrokeType(juce::jmax(1.0f, h * 0.082f)));
}

void drawToolIcon(juce::Graphics &g, juce::Rectangle<float> area,
                  juce::Colour colour, PointerTool tool) {
  // The arrow sits left of centre when a mark stands beside it, and in the
  // middle when nothing does.
  const auto marked = tool != PointerTool::Pointer;
  const auto h = juce::jmin(area.getHeight() * 0.62f, area.getWidth() * 0.44f);

  auto arrowAt = area.getCentre();

  if (marked)
    arrowAt.x -= h * 0.52f;

  const auto top = arrowAt.translated(-h * 0.22f, -h * 0.5f);

  juce::Path arrow;
  arrow.startNewSubPath(top);
  arrow.lineTo(top.translated(0.0f, h));
  arrow.lineTo(top.translated(h * 0.24f, h * 0.73f));
  arrow.lineTo(top.translated(h * 0.42f, h * 1.02f));
  arrow.lineTo(top.translated(h * 0.60f, h * 0.90f));
  arrow.lineTo(top.translated(h * 0.42f, h * 0.62f));
  arrow.lineTo(top.translated(h * 0.68f, h * 0.60f));
  arrow.closeSubPath();

  g.setColour(colour);
  g.fillPath(arrow);

  if (!marked)
    return;

  // Centred on the arrow rather than on the button.
  const auto markAt =
      juce::Point<float>(arrowAt.x + h * 0.88f, area.getCentreY());

  if (tool == PointerTool::Link) {
    // Two overlapping rings, a chain.
    const auto r = h * 0.26f;

    g.drawEllipse(markAt.x - r * 1.7f, markAt.y - r, r * 2.0f, r * 2.0f, 1.3f);
    g.drawEllipse(markAt.x - r * 0.3f, markAt.y - r, r * 2.0f, r * 2.0f, 1.3f);
    return;
  }

  // A drawn contour, for sweeping a shape across the series.
  const auto w = h * 0.86f;
  const auto half = w * 0.5f;

  juce::Path contour;
  contour.startNewSubPath(markAt.x - half, markAt.y + h * 0.26f);
  contour.lineTo(markAt.x - half * 0.33f, markAt.y - h * 0.30f);
  contour.lineTo(markAt.x + half * 0.33f, markAt.y + h * 0.12f);
  contour.lineTo(markAt.x + half, markAt.y - h * 0.34f);

  g.strokePath(contour, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

juce::Colour bandColour(float t) {
  t = std::clamp(t, 0.0f, 1.0f);

  // The middle of a blue to yellow sweep. The hue passes through 360, so it
  // wraps after interpolating rather than before.
  const auto hue = 238.1f + t * (411.5f - 238.1f);

  // Saturation and value fall towards the warm end, since yellow reads far
  // brighter than blue at equal value.
  const auto sat = 0.730f + t * (0.585f - 0.730f);
  const auto val = 0.954f + t * (0.863f - 0.954f);

  return juce::Colour::fromHSV(std::fmod(hue, 360.0f) / 360.0f, sat, val, 1.0f);
}

juce::Colour intervalColour(int pitchClass) {
  const auto pc = ((pitchClass % 12) + 12) % 12;

  return bandColour((float)pc / 11.0f);
}

juce::Colour characterColour(Character c) {
  // 6581, PD and FM in a colour of the instrument each was measured from. The
  // rest at one OKLCH lightness and saturation, spread in hue to be told
  // apart. Pure does not light.
  switch (c) {
  case Character::Sid:
    return juce::Colour(0xff867ade);
  case Character::Pd:
    return juce::Colour(0xffd5df7c);
  case Character::Fm:
    return juce::Colour(0xff00825f);
  case Character::Opamp:
    return juce::Colour(0xfff47a7c);
  case Character::Valve:
    return juce::Colour(0xffed8642);
  case Character::Bulb:
    return juce::Colour(0xffd29a00);
  case Character::Wheel:
    return juce::Colour(0xff00beac);
  case Character::Vco:
    return juce::Colour(0xff00b5e7);
  case Character::Combo:
    return juce::Colour(0xff65a6ff);
  case Character::Diode:
    return juce::Colour(0xffcb84e1);
  case Character::Rail:
    return juce::Colour(0xffe87ab3);
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return colours::textDim;
}

// The machines light at the characters' lightness and saturation, each set
// spread evenly round the hue wheel, in the order of its type.

juce::Colour resonatorColour(ResonatorMode m) {
  // Piano, Open, Octave, Series, Fifths and Chord, a sixth of the wheel apart.
  constexpr std::array<juce::uint32, 6> kLit{
      0xffce83de, 0xfff47a7c, 0xffd49800, 0xff6dbb5e, 0xff00bbc3, 0xff6ea4ff};
  static_assert(kLit.size() + 1 == (size_t)ResonatorMode::NumModes);

  const auto at = (int)m - 1;

  return at < 0 || at >= (int)kLit.size() ? colours::textDim
                                          : juce::Colour(kLit[(size_t)at]);
}

juce::Colour echoColour(EchoType t) {
  // Tape, Bucket brigade and Digital, a third of the wheel apart.
  constexpr std::array<juce::uint32, 3> kLit{0xff6ebb5d, 0xff6da4ff,
                                             0xfff47a7d};
  static_assert(kLit.size() == (size_t)EchoType::NumTypes);

  return juce::Colour(kLit[(size_t)std::clamp((int)t, 0, 2)]);
}

juce::Colour reverbColour(ReverbType t) {
  // Room, Modulated Plate and Spring, between the echo's three.
  constexpr std::array<juce::uint32, 3> kLit{0xffd59800, 0xff00bbc3,
                                             0xffce83de};
  static_assert(kLit.size() == (size_t)ReverbType::NumTypes);

  return juce::Colour(kLit[(size_t)std::clamp((int)t, 0, 2)]);
}

juce::Colour clipColour(dm::ClipType t) {
  // Soft, Hard, Asymmetric, Limiter, Fold, Console, Cassette and Print, an
  // eighth apart.
  constexpr std::array<juce::uint32, 8> kLit{0xff00bbc3, 0xff43acfb, 0xffa492fb,
                                             0xffdd7eca, 0xfff47a7d, 0xffe48e26,
                                             0xffabac1d, 0xff43c07a};
  static_assert(kLit.size() == (size_t)dm::ClipType::NumTypes);

  return juce::Colour(kLit[(size_t)std::clamp((int)t, 0, 7)]);
}

namespace {
// -1 marks the one flexible row (the fader), which absorbs any leftover height.
// The two text rows are tall enough for seven-segment digits, which are as wide
// as they are tall, to stay legible.
constexpr int kRowHeights[kNumRows] = {
    16, // Header
    38, // TuneKnob
    16, // TuneText
    15, // PitchModHeading
    22, // PmShape
    30, // PmRate
    30, // PmDepth
    30, // Drift
    30, // Glide
    15, // EnvHeading
    30, // Strike
    30, // Delay
    30, // Attack
    30, // Decay
    30, // Sustain
    15, // KeyOffHeading
    30, // Swell
    30, // OffLevel
    30, // Release
    15, // AmpModHeading
    22, // AmShape
    30, // AmRate
    30, // AmDepth
    15, // OutputHeading
    30, // Velocity
    30, // Keyboard
    30, // Aftertouch
    30, // Pan
    20, // MuteSolo
    -1, // Fader
    16  // FaderText
};

} // namespace

int rowHeight(Row r) {
  const auto i = (int)r;
  return i >= 0 && i < kNumRows ? kRowHeights[i] : 0;
}

namespace {

/// The height the fader wants, and the least it is ever given.
constexpr int kIdealFaderHeight = 92;

/// The least the scrolling band is ever reduced to: the fader, its readout,
/// the mute and solo pair and a knob row or two. This sets the shortest window
/// the plugin allows.
constexpr int kMinMiddleHeight = 240;

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
  case Row::PmShape:
  case Row::PmDepth:
  case Row::Drift:
  case Row::Glide:
    return Section::PitchMod;

  case Row::EnvHeading:
  case Row::Strike:
  case Row::Delay:
  case Row::Attack:
  case Row::Decay:
  case Row::Sustain:
    return Section::Envelope;

  case Row::KeyOffHeading:
  case Row::Swell:
  case Row::OffLevel:
  case Row::Release:
    return Section::KeyOff;

  case Row::AmpModHeading:
  case Row::AmRate:
  case Row::AmShape:
  case Row::AmDepth:
    return Section::AmpMod;

  case Row::OutputHeading:
  case Row::Velocity:
  case Row::Keyboard:
  case Row::Aftertouch:
  case Row::Pan:
    return Section::Output;

  // Always on screen. Listed rather than defaulted, so a new row has to be
  // put in a section or kept out of one before it will build.
  case Row::Header:
  case Row::TuneKnob:
  case Row::TuneText:
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

bool isHeadingRow(Row r) {
  return r == Row::PitchModHeading || r == Row::EnvHeading ||
         r == Row::KeyOffHeading || r == Row::AmpModHeading ||
         r == Row::OutputHeading;
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
  // The rows are laid out inside the strip's padding, top and bottom.
  return fixedHeight(collapsed) + kIdealFaderHeight + 2 * kStripPadY;
}

int minimumStripHeight(SectionMask) {
  // The same whatever is folded, since what does not fit scrolls.
  return kRowHeights[(size_t)Row::Header] + kMinMiddleHeight;
}

namespace {
/// The header is the only row that does not scroll.
bool rowIsPinned(Row r) { return r == Row::Header; }

/// Every scrolling row but the fader, whose height is not fixed.
int rowsAroundTheFader(SectionMask collapsed) {
  int total = 0;

  for (int i = 0; i < kNumRows; ++i) {
    const auto row = (Row)i;

    if (rowIsPinned(row) || row == Row::Fader)
      continue;

    if (!rowIsCollapsed(row, collapsed))
      total += kRowHeights[i];
  }

  return total;
}
} // namespace

Bands layoutBands(juce::Rectangle<int> area, SectionMask collapsed) {
  Bands out;

  auto remaining = area;
  out.header = remaining.removeFromTop(kRowHeights[(size_t)Row::Header]);
  out.middle = remaining;

  const int around = rowsAroundTheFader(collapsed);

  // The fader takes what is left over, and never less than its ideal.
  out.faderHeight =
      juce::jmax(kIdealFaderHeight, out.middle.getHeight() - around);

  out.contentHeight = around + out.faderHeight;

  out.maxScroll = juce::jmax(0, out.contentHeight - out.middle.getHeight());

  return out;
}

RowBounds layoutRows(juce::Rectangle<int> area, SectionMask collapsed,
                     int scroll) {
  const auto bands = layoutBands(area, collapsed);
  const int clamped = juce::jlimit(0, bands.maxScroll, scroll);

  RowBounds out;
  out[(size_t)Row::Header] = bands.header;

  // The scrolling rows, a pixel at a time. A row partly under the header is
  // covered by the column's HeaderCap. A row wholly outside the band comes
  // back empty, as a folded row does.
  int y = bands.middle.getY() - clamped;

  for (int i = 0; i < kNumRows; ++i) {
    const auto row = (Row)i;

    if (rowIsPinned(row))
      continue;

    const int h = rowIsCollapsed(row, collapsed) ? 0
                  : row == Row::Fader            ? bands.faderHeight
                                                 : kRowHeights[i];

    const juce::Rectangle<int> at(bands.middle.getX(), y,
                                  bands.middle.getWidth(), h);

    // Advanced for every row whether it is placed or not, so the rows keep
    // their order.
    y += h;

    const bool gone = h == 0 || at.getBottom() <= bands.middle.getY() ||
                      at.getY() >= bands.middle.getBottom();

    out[(size_t)i] =
        gone ? juce::Rectangle<int>(at.getX(), at.getY(), at.getWidth(), 0)
             : at;
  }

  return out;
}

namespace {
/// Rows that hold something you can point at.
bool rowHasControl(Row r) {
  switch (r) {
  case Row::TuneKnob:
  case Row::PmRate:
  case Row::PmShape:
  case Row::PmDepth:
  case Row::Drift:
  case Row::Glide:
  case Row::Strike:
  case Row::Delay:
  case Row::Attack:
  case Row::Decay:
  case Row::Sustain:
  case Row::Swell:
  case Row::OffLevel:
  case Row::Release:
  case Row::AmRate:
  case Row::AmShape:
  case Row::AmDepth:
  case Row::Velocity:
  case Row::Keyboard:
  case Row::Aftertouch:
  case Row::Pan:
  case Row::MuteSolo:
  case Row::Fader:
    return true;

  // The rest carry no control. Listed rather than defaulted, so a new row has
  // to be placed on one side before it will build.
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

    // Only the vertical span is tested, so the highlight does not flicker in
    // the margin between strips.
    if (p.y < row.getY() || p.y >= row.getBottom())
      continue;

    // The two readouts answer with the control above them.
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
    // Copies, not references: Array::remove can shrink the storage and leave
    // a reference pointing at freed memory.
    auto band = regions.getUnchecked(i);

    for (int j = regions.size(); --j > i;) {
      const auto other = regions.getUnchecked(j);

      if (other.getY() != band.getY() || other.getHeight() != band.getHeight())
        continue;

      band = band.getUnion(other);
      regions.remove(j);
    }

    regions.set(i, band);
  }
}

void coalesceRegions(juce::Array<juce::Rectangle<int>> &regions, int limit) {
  limit = juce::jmax(1, limit);

  // Left to right, so the pairs considered for merging are neighbours.
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
  case Row::PmShape:
    return "shape";
  case Row::PmRate:
    return "rate";
  case Row::PmDepth:
    return "depth";
  case Row::Drift:
    return "drift";
  case Row::Glide:
    return "glide";
  case Row::EnvHeading:
    return "ENVELOPE";
  case Row::Strike:
    return "strike";
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
  case Row::AmpModHeading:
    return "AMP MOD";
  case Row::OutputHeading:
    return "OUTPUT";
  case Row::Velocity:
    return "velocity";
  case Row::Keyboard:
    return "keyboard";
  case Row::Aftertouch:
    return "aftertouch";
  case Row::Pan:
    return "pan";
  case Row::AmShape:
    return "shape";
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

juce::String rowHelp(Row r) {
  // A heading folds when clicked, which nothing else on it says.
  const juce::String fold = " Click the heading to fold the section away or "
                            "open it again.";

  switch (r) {
  case Row::TuneKnob:
    return "Where each partial sits, from its equal-tempered position to the "
           "exact ratio of the harmonic series.";
  case Row::TuneText:
    return "How far each partial is from equal temperament, in cents.";
  case Row::PitchModHeading:
    return "Vibrato for each partial, with its own shape, rate and depth." +
           fold;
  case Row::PmShape:
    return "The shape the vibrato traces. Click the drawing on a channel to "
           "choose one, or choose another partial, whose wave then moves this "
           "one's phase, as an FM synthesizer does.";
  case Row::PmRate:
    return "How fast the vibrato moves, from 0.01 to 30 Hz. Not used while a "
           "partial is the source.";
  case Row::PmDepth:
    return "How far the vibrato moves the partial, up to 1200 cents. With a "
           "partial as the source, the modulation index, from 0 to 12.";
  case Row::Drift:
    return "A slow random wander in pitch, different for every partial, up to "
           "25 cents.";
  case Row::Glide:
    return "How long each partial takes to reach a new note, so the partials "
           "can arrive one at a time. L glides only legato, and T makes every "
           "glide take the same time.";
  case Row::EnvHeading:
    return "How each partial rises and falls while the key is down." + fold;
  case Row::Strike:
    return "How much velocity changes the delay and attack below. Positive "
           "makes a hard note start sooner and rise faster, and a soft one "
           "wait and rise slowly.";
  case Row::Delay:
    return "How long the partial stays silent before its attack begins.";
  case Row::Attack:
    return "How long the partial takes to rise. Below 0.2 ms it sets where in "
           "its cycle the partial starts instead.";
  case Row::Decay:
    return "How long the partial takes to fall from its peak to the sustain "
           "level.";
  case Row::Sustain:
    return "The level the partial holds while the key is down.";
  case Row::KeyOffHeading:
    return "A second envelope that runs when the key is released." + fold;
  case Row::Swell:
    return "How long the partial takes to move to the key-off level once the "
           "key is released.";
  case Row::OffLevel:
    return "The level the partial moves to when the key is released. Above "
           "the sustain it swells, below it drops. Zero skips the stage.";
  case Row::Release:
    return "How long the partial takes to fade to silence after the key-off "
           "stage.";
  case Row::AmpModHeading:
    return "Tremolo for each partial, with its own shape, rate and depth." +
           fold;
  case Row::AmShape:
    return "The shape the tremolo traces. Click the drawing on a channel to "
           "choose one, or choose another partial, whose wave then multiplies "
           "this one.";
  case Row::AmRate:
    return "How fast the tremolo moves, from 0.01 to 30 Hz. Not used while a "
           "partial is the source.";
  case Row::AmDepth:
    return "How far the tremolo lowers the level, up to 100%. With a partial "
           "as the source, how much of the product is heard, up to ring "
           "modulation.";
  case Row::OutputHeading:
    return "How each partial answers the keys, where it sits and how loud it "
           "is." +
           fold;
  case Row::Velocity:
    return "How much key velocity scales the level. Positive makes soft notes "
           "quieter, negative makes hard notes quieter. The dB switch beside "
           "it spreads the change evenly in decibels.";
  case Row::Keyboard:
    return "How the level follows the key played. Up to 100% it tilts around "
           "middle C, which never changes: positive turns down the keys below "
           "it, negative the keys above it, up to 24 dB per octave. Past 100% "
           "the slope stays at 24 and the turn moves away from middle C, an "
           "octave for every 20%, until only the top or the bottom of the "
           "keyboard is left.";
  case Row::Aftertouch:
    return "How much key pressure moves the level. Negative fades the partial "
           "out as you press.";
  case Row::Pan:
    return "Where the partial sits between left and right. The level stays the "
           "same as it moves.";
  case Row::MuteSolo: {
#if JUCE_MAC
    const juce::String key = "Option";
#else
    const juce::String key = "Alt";
#endif
    return "Mute or solo each partial. Mute overrides solo. " + key +
           "-click to mute or solo only one, and right-click to clear them "
           "all.";
  }
  case Row::Fader:
    return "Each partial's level, in decibels, with its meter inside the "
           "track.";
  case Row::FaderText:
    return "The level each fader is set to, in decibels.";

  case Row::Header:
  case Row::NumRows:
  default:
    return {};
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
  case LinkScope::Selected:
    return "Selected";

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
  case LinkCurve::Taper:
    return "Taper from the grab";
  case LinkCurve::Spread:
    return "Spread / gather";

  case LinkCurve::NumCurves:
  default:
    jassertfalse;
    return "Uniform";
  }
}

const char *linkCurveId(LinkCurve c) {
  switch (c) {
  case LinkCurve::Uniform:
    return "uniform";
  case LinkCurve::Taper:
    return "taper";
  case LinkCurve::Spread:
    return "spread";

  case LinkCurve::NumCurves:
  default:
    jassertfalse;
    return "uniform";
  }
}

LinkCurve linkCurveFromState(const juce::String &id, int legacy) {
  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    if (id == linkCurveId((LinkCurve)i))
      return (LinkCurve)i;

  // The old index order was Uniform, Tilt up, Tilt down, Spread.
  switch (legacy) {
  case 1:
  case 2:
    return LinkCurve::Taper;
  case 3:
    return LinkCurve::Spread;
  default:
    return LinkCurve::Uniform;
  }
}

namespace {
/// Menu ids, in separate blocks so they cannot clash.
constexpr int kClearSelectionId = 2;
constexpr int kScopeBaseId = 100;
constexpr int kCurveBaseId = 200;
} // namespace

bool isClearSelectionChoice(int id) { return id == kClearSelectionId; }

const char *pointerToolName(PointerTool t) {
  switch (t) {
  case PointerTool::Pointer:
    return "Pointer";
  case PointerTool::Link:
    return "Link";
  case PointerTool::Draw:
    return "Draw";

  case PointerTool::NumTools:
    break;
  }

  return "Pointer";
}

juce::Image pointerToolImage(PointerTool t, LinkCurve curve, float scale) {
  switch (t) {
  case PointerTool::Link:
    return linkCursorImage(curve, scale);
  case PointerTool::Draw:
    return drawCursorImage(scale);

  case PointerTool::Pointer:
  case PointerTool::NumTools:
    break;
  }

  return {};
}

/// Ids for the three, kept clear of the scope and curve blocks below.
constexpr int kToolBaseId = 40;

juce::PopupMenu buildToolMenu(PointerTool tool, const LinkSettings &settings) {
  juce::PopupMenu m;

  m.addSectionHeader("Tool");

  for (int i = 0; i < (int)PointerTool::NumTools; ++i)
    m.addItem(kToolBaseId + i, pointerToolName((PointerTool)i), true,
              i == (int)tool);

  // Only Link has settings. They grey out under another tool rather than
  // disappear, so the items do not move.
  const auto linking = tool == PointerTool::Link;

  m.addSeparator();
  m.addSectionHeader("Scope");
  for (int i = 0; i < (int)LinkScope::NumScopes; ++i)
    m.addItem(kScopeBaseId + i, linkScopeName((LinkScope)i), linking,
              i == (int)settings.scope);

  // Offered whichever tool is chosen, since a selection outlasts the tool.
  m.addItem(kClearSelectionId, "Clear selection", settings.hasSelection);

  m.addSeparator();
  m.addSectionHeader("Curve");
  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    m.addItem(kCurveBaseId + i, linkCurveName((LinkCurve)i), linking,
              i == (int)settings.curve);

  return m;
}

bool applyToolMenuChoice(int id, PointerTool &tool) {
  if (id < kToolBaseId || id >= kToolBaseId + (int)PointerTool::NumTools)
    return false;

  tool = (PointerTool)(id - kToolBaseId);
  return true;
}

bool applyLinkMenuChoice(int id, LinkSettings &settings) {
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
  // How far along the series this strip sits from the one being dragged, as 0
  // to 1, where 1 is the full width of the mixer.
  const auto distance =
      (float)std::abs(juce::jlimit(0, kNumHarmonics - 1, index0) -
                      juce::jlimit(0, kNumHarmonics - 1, sourceIndex)) /
      (float)(kNumHarmonics - 1);

  switch (c) {
  case LinkCurve::Taper:
    // Against the full width, so the reach is the same wherever you grab.
    return 1.0f - distance;

  case LinkCurve::Uniform:
  case LinkCurve::Spread:
  case LinkCurve::NumCurves:
  default:
    return 1.0f;
  }
}

float linkedValue(LinkCurve curve, float baseline, float delta, float weight,
                  float jitter, float target, float low, float high) {
  float value = baseline;

  if (curve == LinkCurve::Spread) {
    // Pushing up scatters each strip along its jitter. Pulling down gathers
    // them onto the dragged strip, arriving at half a drag.
    const auto gather = juce::jlimit(0.0f, 1.0f, -delta * 2.0f);

    value = delta >= 0.0f ? baseline + delta * jitter
                          : baseline + (target - baseline) * gather;
  } else {
    value = baseline + delta * weight;
  }

  return juce::jlimit(low, high, value);
}

const char *roleLabel(Role r) {
  switch (r) {
  case Role::Tune:
    return "tuning";
  case Role::Glide:
    return "glide";
  case Role::PmRate:
    return "pitch modulation rate";
  case Role::PmDepth:
    return "pitch modulation depth";
  case Role::Drift:
    return "drift";
  case Role::Strike:
    return "strike velocity";
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
  case Role::Keyboard:
    return "keyboard amount";

  // Listed rather than defaulted, so a new role has to be given a name before
  // it will build.
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

int wheelPixels(const juce::MouseWheelDetails &wheel) {
  constexpr float kViewportSingleStep = 16.0f;

  // deltaY as the platform gives it, ignoring isReversed. The platform has
  // already applied natural scrolling, so undoing it would scroll backwards.
  return -juce::roundToInt(wheel.deltaY * 14.0f * kViewportSingleStep);
}

const char *roleSuffix(Role r) {
  switch (r) {
  case Role::Tune:
    return params::tuneSuffix;
  case Role::PmRate:
    return params::pmRateSuffix;
  case Role::PmDepth:
    return params::pmDepthSuffix;
  case Role::Glide:
    return params::glideSuffix;
  case Role::Drift:
    return params::driftSuffix;
  case Role::Strike:
    return params::strikeSuffix;
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
  case Role::Keyboard:
    return params::keySuffix;

  case Role::NumRoles:
  default:
    jassertfalse;
    return params::tuneSuffix;
  }
}

bool roleForRow(Row r, Role &out) {
  switch (r) {
  // The shape rows have none, since a list of shapes cannot be weighted. The
  // shape menu sets them all at once instead.
  case Row::PmShape:
  case Row::AmShape:
    return false;

  case Row::TuneKnob:
    out = Role::Tune;
    return true;
  case Row::PmRate:
    out = Role::PmRate;
    return true;
  case Row::PmDepth:
    out = Role::PmDepth;
    return true;
  case Row::Drift:
    out = Role::Drift;
    return true;
  case Row::Glide:
    out = Role::Glide;
    return true;
  case Row::Strike:
    out = Role::Strike;
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
  case Row::AmRate:
    out = Role::AmRate;
    return true;
  case Row::AmDepth:
    out = Role::AmDepth;
    return true;
  case Row::Velocity:
    out = Role::Velocity;
    return true;
  case Row::Keyboard:
    out = Role::Keyboard;
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

  // The rows LINK has nothing to reach.
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

namespace ovt::params {

juce::Colour macroColour(int colour) {
  return ovt::ui::kMacroColours[(size_t)juce::jlimit(0, kNumMacroColours - 1,
                                                     colour)];
}

} // namespace ovt::params
