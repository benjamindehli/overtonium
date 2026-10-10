#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

int meterSegments(int height) { return juce::jlimit(6, 24, height / 15); }

namespace {
/// What each macro colour actually is.
///
/// Chosen to be told apart at the size of a knob's ring rather than to be
/// pretty in a row: eight hues spread around the wheel, all at a lightness
/// that reads against the panel without competing with a lit mute or solo.
/// The first is no colour at all, for anyone who would rather the mixer
/// stayed the colour the series makes it.
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
  // 0.38 of the shorter side rather than 0.46, which left a pixel of margin
  // in a 24 px button and read as a cog jammed into its face. The teeth are
  // the outermost thing drawn, so the radius is the whole of the margin.
  const auto outer = juce::jmin(area.getWidth(), area.getHeight()) * 0.38f;
  const auto root = outer * 0.74f;

  // A filled cog rather than spokes on a ring, which is what it was and what
  // made it a ship's wheel: a wheel is a rim with spokes reaching in, and a
  // cog is a solid body with teeth standing out of it. Drawn as one outline
  // that steps between the two radii, with the bore punched by an even-odd
  // fill rather than painted over, since what is behind the button is not a
  // colour this knows.
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
  // single line: every note feeding one modulator. Fractions of the side it is
  // given, so it scales with the switch.
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
  // Three faders with their caps at different places, which is what the
  // macro panel looks like and what a macro does: several controls moved
  // from one place.
  //
  // Everything here is a fraction of the height it is given, because the
  // area is the lamp cap rather than the button, and the cap is some 6 px
  // shorter: the fixed spacing this had before ran the outer two rows
  // under the bezel.
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

    // The rules carry as much of this icon as the caps do, and at a quarter
    // of the ink they were a hint rather than a line. They are a step back
    // from the caps rather than a whisper.
    g.setColour(colour.withMultipliedAlpha(0.62f));
    g.fillRoundedRectangle(left, y - track * 0.5f, w, track, track * 0.5f);

    g.setColour(colour);
    g.fillRoundedRectangle(left + w * at[i] - capW * 0.5f, y - capH * 0.5f,
                           capW, capH, capW * 0.36f);
  }
}

void drawToolIcon(juce::Graphics &g, juce::Rectangle<float> area,
                  juce::Colour colour, PointerTool tool) {
  // The arrow sits left of centre when something stands beside it, and in
  // the middle when nothing does, so the pair reads as one mark rather than
  // as an arrow that has drifted.
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

  // Centred on the arrow rather than on the button, so the two sit on one
  // line whatever the button's height.
  const auto markAt =
      juce::Point<float>(arrowAt.x + h * 0.88f, area.getCentreY());

  if (tool == PointerTool::Link) {
    // Two rings side by side, overlapping, which is a chain at any size. The
    // first attempt drew them as rounded bars touching the arrow, and at
    // twenty pixels that is one blob rather than two links.
    const auto r = h * 0.26f;

    g.drawEllipse(markAt.x - r * 1.7f, markAt.y - r, r * 2.0f, r * 2.0f, 1.3f);
    g.drawEllipse(markAt.x - r * 0.3f, markAt.y - r, r * 2.0f, r * 2.0f, 1.3f);
    return;
  }

  // A drawn contour rather than a pencil.
  //
  // Two attempts at a pencil both read as a tick: at twenty pixels the body
  // and the point are three or four pixels each and the eye joins them into
  // one stroke. A contour is also the truer picture of what this tool does,
  // which is to sweep a shape across the series rather than to mark a single
  // control.
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

void ScreenSwitch::paintButton(juce::Graphics &g, bool highlighted, bool) {
  // Two pixels in from the bounds, which is where the lip of the recess goes,
  // so a run of them laid edge to edge leaves a margin of panel between each.
  const auto area = getLocalBounds().toFloat().reduced(2.0f);

  paintRecess(g, area, 2.5f);
  paintDisplayGround(g, area, 2.5f, highlighted);

  const bool on = getToggleState();
  const auto ink = on ? lamp : colours::textDim.withAlpha(0.55f);

  if (onIcon != nullptr) {
    const auto glyph = area.reduced(2.5f);

    // The same bloom the letters get, as a wider faint pass under the icon.
    if (on)
      onIcon(g, glyph.translated(0.0f, 0.5f), lamp.withAlpha(0.35f));

    onIcon(g, glyph, ink);
    return;
  }

  g.setFont(makeFont(10.0f, true));

  // A half-pixel echo under the lit letter, the bloom a lit segment has on the
  // readouts, which is most of what tells a lit letter from a bright one.
  if (on) {
    g.setColour(lamp.withAlpha(0.35f));
    g.drawText(getButtonText(), area.translated(0.0f, 0.5f),
               juce::Justification::centred, false);
  }

  // Unlit, faint enough that a lit one beside it is unmistakable, and still
  // there to be read, since the letter is what says which switch this is.
  g.setColour(ink);
  g.drawText(getButtonText(), area, juce::Justification::centred, false);
}

void GlowButton::paintButton(juce::Graphics &g, bool highlighted, bool down) {
  // What the cap is moulded in, for the few buttons that ask for a particular
  // face. Everything else leaves it and the lamp decides.
  const auto fill = findColour(juce::TextButton::buttonColourId);

  if (auto *laf = dynamic_cast<OvertoniumLookAndFeel *>(&getLookAndFeel()))
    laf->drawButtonFace(g, *this, getToggleState(), fill, highlighted, down);
  else
    getLookAndFeel().drawButtonBackground(g, *this, fill, highlighted, down);

  // The same ink whatever the lamp is doing. The word is printed on the
  // plastic rather than being part of what lights, so it does not change
  // colour under its own lamp, and the state is read off the cap alone.
  const auto colour = OvertoniumLookAndFeel::lampLegend();

  const auto h = (float)getHeight();

  // The cap rather than the whole button, or anything drawn on it runs out
  // under the bezel: the moulding is part of this component.
  const auto cap = OvertoniumLookAndFeel::lampCapBounds(
      getLocalBounds().toFloat(), OvertoniumLookAndFeel::lampGangOf(*this));

  // An icon takes a flat ink rather than the ramp a word gets, and an opaque
  // one rather than a translucent one.
  //
  // These are drawn as overlapping shapes: three rules with a handle sitting
  // on each. Handed a colour with alpha in it, every overlap composites twice
  // and the rule shows straight through the handle that is supposed to be
  // sitting on it, which is a drawing bug rather than translucency. The ink
  // is mixed against the plastic here instead and laid on solid, which is the
  // same colour on the face and has no seams in it.
  if (onIcon != nullptr) {
    const auto plastic =
        OvertoniumLookAndFeel::lampFace(fill, getToggleState());

    onIcon(g, cap,
           colour.interpolatedWith(plastic, getToggleState() ? 0.14f : 0.06f));
    return;
  }

  const auto font = makeFont(juce::jlimit(8.0f, 13.0f, h * 0.58f), true);

  // Centred on the cap, laid out across the whole width. Both halves matter.
  // The cap decides where the middle is, because a ganged one is not centred
  // in its component: it gives up a whole wall on the outside and half a wall
  // where it joins, which put the M and the S a pixel out towards the ends of
  // their block. The width is the button's because these are sized against
  // the words they carry and REVERB fills its own to within a pixel either
  // side, so fitting to the cap would take the B off it.
  const auto line = juce::Rectangle<float>((float)getWidth(), h)
                        .withCentre({cap.getCentreX(), h * 0.5f});

  juce::GlyphArrangement glyphs;
  glyphs.addFittedText(font, getButtonText(), line.getX(), line.getY(),
                       line.getWidth(), line.getHeight(),
                       juce::Justification::centred, 1, 1.0f);

  // Printed on the plastic rather than painted over it, so the lamp comes
  // through it: thinnest over the middle where the lamp is, closing up
  // towards the ends. See OvertoniumLookAndFeel::legendInk.
  g.setGradientFill(
      OvertoniumLookAndFeel::legendInk(colour, cap, getToggleState()));

  glyphs.draw(g);
}

juce::Colour bandColour(float t) {
  t = std::clamp(t, 0.0f, 1.0f);

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

juce::Colour intervalColour(int pitchClass) {
  const auto pc = ((pitchClass % 12) + 12) % 12;

  return bandColour((float)pc / 11.0f);
}

juce::Colour characterColour(Character c) {
  // The fifth is where the red the mixer already uses sits, on channel 3, and
  // the seventh is the yellow at the top of the band. Running the characters
  // down between them puts the gentlest at the yellow end and the hardest on
  // that red, and adding one subdivides the same stretch rather than walking
  // off the end of it into the blues.
  constexpr float kYellow = 11.0f / 11.0f;
  constexpr float kFifth = 7.0f / 11.0f;

  const auto first = 1; // Pure is not on the band
  const auto last = (int)Character::NumCharacters - 1;
  const auto steps = (float)std::max(1, last - first);

  const auto t = std::clamp((float)((int)c - first) / steps, 0.0f, 1.0f);

  return bandColour(kYellow + t * (kFifth - kYellow));
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
    30, // Aftertouch
    30, // Pan
    20, // MuteSolo
    -1, // Fader
    16  // FaderText
};

/// The height the fader wants, and now the least it is ever given.
///
/// There used to be a smaller floor beside this, 60, for a window too short to
/// give every row its height: the fader was squeezed so that nothing had to
/// scroll. Nothing has to fit any more, so the fader keeps the height it wants
/// and the rows scroll past it instead, and a minimum below the ideal has
/// nothing left to mean.
constexpr int kIdealFaderHeight = 92;

/// The least the scrolling band is ever reduced to.
///
/// Enough for the mixer and a little of what is above it: the fader at its
/// ideal height, its readout, the mute and solo pair, and a knob row or two
/// for context. Below that the band stops being something you scroll and
/// becomes a slot you hunt through.
///
/// This is what sets the shortest window the plugin allows, and that floor is
/// the whole reason the scrolling works. It used to be the height of every row
/// at once, 805, and at 150% zoom a 1080p screen offers about 577 for the
/// strips, so zooming in far enough to read the knobs was not refused so much
/// as impossible.
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
  case Row::Aftertouch:
  case Row::Pan:
    return Section::Output;

  // Always on screen. Spelt out rather than left to a default so a new row
  // has to be put in a section, or deliberately kept out of one, before it
  // will build.
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
  return fixedHeight(collapsed) + kIdealFaderHeight;
}

int minimumStripHeight(SectionMask) {
  // No longer anything to do with how much is folded away, because nothing has
  // to fit any more: what does not fit scrolls. The header is pinned and the
  // band has a floor, and that is the whole of it.
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

/// Every offset at which the band can rest: the top of each scrolling row.
///
/// Scrolling snaps to these at the top of the band so that nothing is ever
/// half over the pinned header. The bottom is free to cut a row off, which is
/// what tells you there is more below.
int middleContentHeight(SectionMask collapsed) {
  return rowsAroundTheFader(collapsed) + kIdealFaderHeight;
}

Bands layoutBands(juce::Rectangle<int> area, SectionMask collapsed) {
  Bands out;

  auto remaining = area;
  out.header = remaining.removeFromTop(kRowHeights[(size_t)Row::Header]);
  out.middle = remaining;

  const int around = rowsAroundTheFader(collapsed);

  // The fader takes what is left over, and never less than it wants. Which
  // means it grows in a window with room to spare, exactly as it always has,
  // and stops growing the moment there is anything left to scroll.
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

  // The scrolling rows, a pixel at a time.
  //
  // A row may sit half over the top of the band. The column covers that with
  // an opaque cap over its header, which also takes the mouse, so a row under
  // it is neither seen nor clickable. At the foot there is nothing to cover: a
  // row runs past the band and is cut off by the column's own edge, which is
  // what shows there is more underneath.
  //
  // A row wholly outside the band comes back empty, which is what a folded row
  // comes back as, so every caller already copes: the control hides itself and
  // rowUnder cannot find it.
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

    // Advanced for every row whether it is placed or not. Counting only the
    // ones that were placed is what made short rows vanish out of order.
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

  // Written before the names went in, when the list ran Uniform, Tilt up, Tilt
  // down, Spread. Both tilts become the curve that replaced them, and Spread
  // has to be moved down a place rather than left where its old index now
  // points, which is a different curve entirely.
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
/// Menu ids. Scopes and curves are offset so the two lists cannot be confused
/// for one another, and neither can be confused for the switch.
constexpr int kLinkToggleId = 1;
constexpr int kScopeBaseId = 100;
constexpr int kCurveBaseId = 200;
} // namespace

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

  // Only Link has anything to set, so the rest of the menu is its, and it
  // greys out rather than disappearing when another tool is chosen: a menu
  // that changed length as you moved through it would move the item under
  // the pointer.
  const auto linking = tool == PointerTool::Link;

  m.addSeparator();
  m.addSectionHeader("Scope");
  for (int i = 0; i < (int)LinkScope::NumScopes; ++i)
    m.addItem(kScopeBaseId + i, linkScopeName((LinkScope)i), linking,
              i == (int)settings.scope);

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
  // How far along the series this strip sits from the one being dragged, as 0
  // to 1, where 1 is the full width of the mixer.
  const auto distance =
      (float)std::abs(juce::jlimit(0, kNumHarmonics - 1, index0) -
                      juce::jlimit(0, kNumHarmonics - 1, sourceIndex)) /
      (float)(kNumHarmonics - 1);

  switch (c) {
  case LinkCurve::Taper:
    // Measured against the full width rather than against whichever end
    // happens to be nearer, so the reach is the same wherever you grab. Scaling
    // it to the nearer end would make a grab on channel 2 drop channel 1 to
    // nothing in a single step.
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

  case Role::NumRoles:
  default:
    jassertfalse;
    return params::tuneSuffix;
  }
}

bool roleForRow(Row r, Role &out) {
  switch (r) {
  // The shape rows deliberately have none. LINK drags a value across the
  // series by a weighted curve, and there is no weighting a list of named
  // shapes: half a sawtooth is not a shape. Setting them all at once is
  // offered by the shape menu itself instead.
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

namespace ovt::params {

juce::Colour macroColour(int colour) {
  return ovt::ui::kMacroColours[(size_t)juce::jlimit(0, kNumMacroColours - 1,
                                                     colour)];
}

} // namespace ovt::params
