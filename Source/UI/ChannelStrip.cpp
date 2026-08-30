#include "ChannelStrip.h"

#include <cmath>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }
} // namespace

RelativeKnob::RelativeKnob() {
  setSliderStyle(juce::Slider::RotaryVerticalDrag);
  setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

  // Full travel in either direction covers the whole parameter range, so even
  // the extremes ("everything to just", "everything to equal") stay one drag
  // away despite the control being relative.
  setRange(-1.0, 1.0, 0.0);
  setValue(0.0, juce::dontSendNotification);
  setDoubleClickReturnValue(false, 0.0);

  // A relative gesture needs a start and an end to bracket it. The wheel has
  // neither, so it would move the knob without moving anything else.
  setScrollWheelEnabled(false);

  // Tells the look and feel to draw the arc outwards from twelve o'clock.
  getProperties().set("bipolar", true);

  onValueChange = [this] {
    if (onRelativeDelta)
      onRelativeDelta((float)getValue());
  };
}

void RelativeKnob::startedDragging() {
  if (onRelativeStart)
    onRelativeStart();
}

void RelativeKnob::stoppedDragging() {
  if (onRelativeEnd)
    onRelativeEnd();

  // Spring back so the next drag starts from wherever the strips now sit.
  setValue(0.0, juce::dontSendNotification);
  repaint();
}

// =============================================================================

LabelledKnob::LabelledKnob(juce::String captionText, juce::Colour fill)
    : caption(std::move(captionText)) {
  // The caption is the visible name, so it is the spoken one too. addKnob
  // overrides it where a caption repeats between groups: three of them say
  // MIX, and which mix is exactly what a screen reader cannot see.
  //
  // The member, not the parameter. The initialiser above moved out of
  // captionText, so reading it here gives an empty string and a nameless knob.
  slider.setTitle(caption);

  slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
  addAndMakeVisible(slider);
}

namespace {
// Top to bottom through a labelled knob: room above the dial, the dial, the
// gap, the caption, room under it.
//
// The gap used to take three of these pixels and the two margins one between
// them, which read as the caption belonging to whatever stood underneath it
// rather than to the knob above. Moving two pixels out of the gap and into the
// margins ties the pair together and gives the group some air. The four still
// add to what they always did, so the dial keeps its diameter.
constexpr int kAboveDial = 3;
constexpr int kCaptionGap = 1;
constexpr int kCaptionHeight = 11;
constexpr int kBelowCaption = 1;
} // namespace

void LabelledKnob::paint(juce::Graphics &g) {
  auto area = getLocalBounds();
  area.removeFromBottom(kBelowCaption);

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));
  g.drawText(caption, area.removeFromBottom(kCaptionHeight),
             juce::Justification::centred, false);
}

juce::Rectangle<int> LabelledKnob::dialBounds(juce::Rectangle<int> bounds) {
  bounds.removeFromBottom(kBelowCaption + kCaptionHeight + kCaptionGap);
  bounds.removeFromTop(kAboveDial);
  return bounds;
}

void LabelledKnob::resized() { slider.setBounds(dialBounds(getLocalBounds())); }

// =============================================================================

namespace {
/// How many lamps fit. Aiming at sixteen, but a short window gets fewer rather
/// than a column of slivers, and a tall one gets more rather than bars.
int segmentsFor(int height) { return juce::jlimit(6, 24, height / 15); }
} // namespace

// =============================================================================

namespace {
/// Which of the seven bars are lit for each character we can draw.
///
///      aaa
///     f   b
///      ggg
///     e   c
///      ddd
constexpr uint8_t kSegA = 1, kSegB = 2, kSegC = 4, kSegD = 8, kSegE = 16,
                  kSegF = 32, kSegG = 64;

uint8_t segmentsFor(char c) {
  switch (c) {
  case '0':
    return kSegA | kSegB | kSegC | kSegD | kSegE | kSegF;
  case '1':
    return kSegB | kSegC;
  case '2':
    return kSegA | kSegB | kSegG | kSegE | kSegD;
  case '3':
    return kSegA | kSegB | kSegG | kSegC | kSegD;
  case '4':
    return kSegF | kSegG | kSegB | kSegC;
  case '5':
    return kSegA | kSegF | kSegG | kSegC | kSegD;
  case '6':
    return kSegA | kSegF | kSegG | kSegE | kSegC | kSegD;
  case '7':
    return kSegA | kSegB | kSegC;
  case '8':
    return kSegA | kSegB | kSegC | kSegD | kSegE | kSegF | kSegG;
  case '9':
    return kSegA | kSegB | kSegC | kSegD | kSegF | kSegG;

  // The few letters worth having. Enough for ET and for -inF, which are the
  // two things these displays have to say that are not numbers. Lower case
  // where the upper case letter has no seven-bar form, which is what a real
  // display does rather than leaving the cell blank.
  case 'E':
    return kSegA | kSegD | kSegE | kSegF | kSegG;
  case 'F':
    return kSegA | kSegE | kSegF | kSegG;
  case 't':
    return kSegD | kSegE | kSegF | kSegG;
  case 'n':
    return kSegC | kSegE | kSegG;
  case 'i':
    return kSegC;

  default:
    return 0;
  }
}

/// Whether a character rides in a narrow cell of its own rather than taking a
/// whole digit's width.
///
/// The point and the minus both do, the way they do on a real display: a sign
/// has a position rather than a digit, so -13.7 spends no more of the display
/// on its sign than -2.0 does.
///
/// There is no plus. Seven bars cannot make one worth reading, so nothing
/// asks for one, and a character with no form here comes out blank rather
/// than as the speck a plus would be.
bool isNarrow(char c) { return c == '.' || c == '-'; }
} // namespace

bool SegmentDisplay::canDraw(char c) {
  return isNarrow(c) || segmentsFor(c) != 0;
}

SegmentDisplay::SegmentDisplay(juce::String unit) : unitText(std::move(unit)) {}

void SegmentDisplay::setReading(const juce::String &digits, bool isActive) {
  if (digits == reading && isActive == active)
    return;

  reading = digits;
  active = isActive;
  repaint();
}

void SegmentDisplay::mouseUp(const juce::MouseEvent &e) {
  if (onClick && contains(e.getPosition()))
    onClick();
}

void SegmentDisplay::mouseEnter(const juce::MouseEvent &) {
  // Only a display that does something answers the pointer. On a channel
  // strip these are readouts, and thirty-three of them lighting up as the
  // pointer crossed the mixer would be a lot of movement saying nothing.
  if (onClick == nullptr)
    return;

  hovered = true;
  repaint();
}

void SegmentDisplay::mouseExit(const juce::MouseEvent &) {
  hovered = false;
  repaint();
}

void SegmentDisplay::paintGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                                char c, juce::Colour on,
                                juce::Colour off) const {
  const auto lit = segmentsFor(c);

  // Every bar is drawn whether it is on or not, which is what makes it read as
  // a display with something switched off rather than as floating shapes.
  const auto t = juce::jmax(1.0f, area.getHeight() * 0.16f);
  const auto gap = t * 0.35f;
  const auto w = area.getWidth();
  const auto h = area.getHeight();
  const auto mid = (h - t) * 0.5f;

  struct Bar {
    uint8_t flag;
    juce::Rectangle<float> r;
  };

  const Bar bars[] = {
      {kSegA, {t * 0.5f + gap, 0.0f, w - t - gap * 2.0f, t}},
      {kSegB, {w - t, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegC, {w - t, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegD, {t * 0.5f + gap, h - t, w - t - gap * 2.0f, t}},
      {kSegE, {0.0f, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegF, {0.0f, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegG, {t * 0.5f + gap, mid, w - t - gap * 2.0f, t}},
  };

  for (const auto &bar : bars) {
    g.setColour((lit & bar.flag) != 0 ? on : off);
    g.fillRoundedRectangle(bar.r.translated(area.getX(), area.getY()),
                           t * 0.35f);
  }
}

void SegmentDisplay::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat().reduced(1.0f);

  const auto on = active ? colours::accent : colours::textDim;
  const auto off = on.withAlpha(hovered ? 0.20f : 0.12f);
  const auto lit = on.withAlpha(active ? 0.95f : 0.75f);

  // A recess, so the readout sits in the panel rather than on it, and so the
  // unlit bars have something to be dark against.
  g.setColour(colours::groove);
  g.fillRoundedRectangle(area, 2.5f);

  if (hovered) {
    g.setColour(colours::outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 2.5f, 1.0f);
  }

  area = area.reduced(3.0f, 2.0f);

  if (reading.isEmpty())
    return;

  // The point and the sign ride on narrow stripes of their own rather than
  // taking a whole cell, the way they do on a real display.
  int cells = 0, points = 0;

  for (auto c : reading)
    (isNarrow((char)c) ? points : cells) += 1;

  if (cells < 1)
    return;

  // The unit only earns its place once the digits have what they need.
  const auto unitW =
      unitText.isNotEmpty() && area.getWidth() > 44.0f ? 21.0f : 0.0f;

  // A point costs about a third of a digit, which is what the extra term in
  // the denominator is buying.
  const auto cellW =
      juce::jmin((area.getWidth() - unitW) /
                     ((float)cells + 0.32f * (float)points),
                 area.getHeight() * 0.72f);

  const auto glyphW = cellW * 0.82f;
  const auto pointW = cellW * 0.32f;
  const auto runW = cellW * (float)cells + pointW * (float)points;

  // Centred as one block, so a three-character reading does not sit off to one
  // side of a display sized for four.
  auto x = area.getCentreX() - (runW + unitW) * 0.5f;

  const auto digitArea = area.withX(x).withWidth(runW);

  if (unitW > 0.0f) {
    g.setColour(colours::textDim.withAlpha(0.8f));
    g.setFont(makeFont(7.5f, true));
    g.drawText(unitText,
               area.withX(digitArea.getRight() + 2.0f).withWidth(unitW - 2.0f),
               juce::Justification::centredLeft, false);
  }

  for (int i = 0; i < reading.length(); ++i) {
    const auto c = (char)reading[i];

    if (isNarrow(c)) {
      // Same weight as a segment. The point sits on the baseline the segments
      // end on, the sign across the middle where the g bar runs.
      const auto t = juce::jmax(1.0f, digitArea.getHeight() * 0.16f);
      const auto midY = digitArea.getCentreY() - t * 0.5f;

      g.setColour(lit);

      if (c == '.')
        g.fillRoundedRectangle(x + (pointW - t) * 0.5f,
                               digitArea.getBottom() - t, t, t, t * 0.35f);
      else
        g.fillRoundedRectangle(x, midY, pointW, t, t * 0.35f);

      x += pointW;
      continue;
    }

    paintGlyph(g, {x, digitArea.getY(), glyphW, digitArea.getHeight()}, c, lit,
               off);
    x += cellW;
  }
}

// =============================================================================

void ActivityLamp::setBackdrop(juce::Colour behind) {
  backdrop = behind;
  setOpaque(true);
  repaint();
}

bool ActivityLamp::push(float brightness) {
  const auto exact = juce::jlimit(0.0f, 1.0f, brightness) * (float)kSteps;
  const auto next = juce::jlimit(0, kSteps, (int)std::lround(exact));

  if (next == step)
    return false;

  step = next;
  return true;
}

void ActivityLamp::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  if (isOpaque()) {
    g.setColour(backdrop);
    g.fillRect(bounds);
  }

  // The rule the lamp is mounted on, drawn either side of it so the divider
  // still reads as a continuous line across the strip.
  const auto midY = std::floor(bounds.getCentreY());
  const auto diameter = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);
  const auto lamp = juce::Rectangle<float>(diameter, diameter)
                        .withCentre({bounds.getCentreX(), midY + 0.5f});

  const auto leftRun = lamp.getX() - bounds.getX() - 2.0f;
  const auto rightRun = bounds.getRight() - lamp.getRight() - 2.0f;

  g.setColour(colours::outline.withAlpha(0.7f));
  g.fillRect(bounds.getX(), midY, leftRun, 1.0f);
  g.fillRect(lamp.getRight() + 2.0f, midY, rightRun, 1.0f);

  // A lit lip immediately under the score, which is what turns a drawn line
  // into a groove cut into the panel: the far wall of the cut catches the
  // light coming from above. One pixel, inside the lamp's own bounds, so the
  // band it already invalidates covers it and nothing else has to repaint.
  g.setColour(juce::Colours::white.withAlpha(0.055f));
  g.fillRect(bounds.getX(), midY + 1.0f, leftRun, 1.0f);
  g.fillRect(lamp.getRight() + 2.0f, midY + 1.0f, rightRun, 1.0f);

  // Unlit is the channel colour at low alpha rather than nothing at all, so
  // the divider reads as a lamp that is off rather than as a gap in the rule.
  const auto lit = (float)step / (float)kSteps;

  // The same bloom the meters get, and for the same reason. Two fills, only
  // when the lamp is actually lit, and both inside the row the lamp already
  // owns, so the band that gets invalidated is unchanged.
  if (lit > 0.02f) {
    g.setColour(colour.withAlpha(0.10f * lit));
    g.fillEllipse(lamp.expanded(diameter * 0.42f));

    g.setColour(colour.withAlpha(0.12f * lit));
    g.fillEllipse(lamp.expanded(diameter * 0.18f));
  }

  g.setColour(colour.withAlpha(0.16f + 0.84f * lit));
  g.fillEllipse(lamp);
}

// =============================================================================

void ActivityNeedle::setBackdrop(juce::Colour behind) {
  backdrop = behind;
  setOpaque(true);
  repaint();
}

bool ActivityNeedle::push(float position) {
  // Parked rather than centred when there is nothing to show, so a partial
  // with no pitch modulation on it does not sit there implying it is in tune
  // rather than idle.
  if (position <= -2.0f) {
    if (column < 0)
      return false;

    column = -1;
    return true;
  }

  const auto usable = juce::jmax(1, getWidth() - 3);
  const auto at =
      1 + (int)std::lround(0.5 * (1.0 + juce::jlimit(-1.0f, 1.0f, position)) *
                           (double)usable);

  if (at == column)
    return false;

  column = at;
  return true;
}

void ActivityNeedle::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  if (isOpaque()) {
    g.setColour(backdrop);
    g.fillRect(bounds);
  }

  const auto midY = std::floor(bounds.getCentreY());
  const auto height = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);

  // The travel it reads against, in the channel colour held right down. The
  // same bargain the unlit lamps make: a track that is dark rather than absent
  // says the needle has somewhere to go, and doubles as the rule dividing the
  // groups, so no separate line is drawn here.
  const auto track = juce::Rectangle<float>(bounds.getWidth(), height)
                         .withCentre({bounds.getCentreX(), midY + 0.5f});

  g.setColour(colour.withAlpha(0.16f));
  g.fillRoundedRectangle(track, height * 0.35f);

  // Centre, so sharp and flat mean something when the needle is near it.
  g.setColour(colour.withAlpha(0.34f));
  g.fillRect(bounds.getCentreX() - 0.5f, track.getY() + 1.0f, 1.0f,
             track.getHeight() - 2.0f);

  if (column < 0)
    return;

  g.setColour(colour);
  g.fillRect((float)column - 1.0f, track.getY(), 2.0f, track.getHeight());
}

// =============================================================================

void LevelMeter::setBackdrop(juce::Colour top, juce::Colour bottom) {
  backdropTop = top;
  backdropBottom = bottom;

  // Every pixel is now this component's responsibility, which is the whole
  // point: an opaque child is subtracted from what its parent has to paint.
  setOpaque(true);
  repaint();
}

int LevelMeter::segments() const { return segmentsFor(getHeight()); }

namespace {
/// How far the bloom around a lit meter run reaches beyond it, in pixels.
///
/// Named once because two places have to agree about it: the paint that draws
/// it, and the dirty band push() hands back. A band that does not cover the
/// bloom leaves a smear of the previous level behind, which is what the
/// repaint test checks for.
constexpr float kMeterGlowSpread = 3.0f;
} // namespace

juce::Rectangle<int> LevelMeter::push(float level) {
  // A decibel scale with a floor at -48 dB. On a linear amplitude scale
  // everything above the fundamental sits squashed against the bottom.
  const float norm =
      level <= 1.0e-5f
          ? 0.0f
          : juce::jlimit(0.0f, 1.0f,
                         (juce::Decibels::gainToDecibels(level) + 48.0f) /
                             48.0f);

  // Instant rise and a gentle fall, so a partial decaying under its own
  // envelope reads as a decay rather than as flicker.
  const float next =
      norm > displayed ? norm : juce::jmax(norm, displayed - 0.06f);

  displayed = next;

  // Where the level sits, measured in lamps.
  const auto count = segments();
  const auto exact = juce::jlimit(0.0f, 1.0f, displayed) * (float)count;

  const auto before = juce::jlimit(0, count, lit);
  auto after = before;

  // A lamp lights when the level reaches it, and goes out only once the level
  // has fallen clear of it. Without that margin a tremolo sitting on a
  // boundary makes the lamp flicker at the frame rate, and every one of those
  // frames costs a redraw of the window whether the change is one lamp or a
  // hundred.
  constexpr float kClearOf = 0.3f;

  while (after < count && exact >= (float)after + 1.0f)
    ++after;

  while (after > 0 && exact < (float)after - kClearOf)
    --after;

  lit = after;

  // The level moves continuously but the display does not, so most frames have
  // nothing to say. That is where the saving is: not in drawing less, but in
  // not being drawn at all.
  if (before == after)
    return {};

  // Only the lamps that lit or went out. Deliberately not repainted here: the
  // editor collects these from all 33 meters and invalidates once, since a
  // fistful of scattered rectangles gets coalesced into its bounding box, and
  // that bounding box is the whole window.
  const auto step = (float)getHeight() / (float)count;

  const auto top = (float)getHeight() - (float)juce::jmax(before, after) * step;
  const auto bottom =
      (float)getHeight() - (float)juce::jmin(before, after) * step;

  // The margin covers the bloom, which reaches kMeterGlowSpread beyond the
  // run, plus a pixel for the rounding above.
  const auto margin = (int)kMeterGlowSpread + 1;

  return juce::Rectangle<int>(0, juce::roundToInt(top) - margin, getWidth(),
                              juce::roundToInt(bottom - top) + 2 * margin)
      .getIntersection(getLocalBounds());
}

void LevelMeter::paint(juce::Graphics &g) {
  // The slice of channel background this meter stands on. Painting it here
  // rather than letting the strip show through is what lets the component be
  // opaque, and an opaque component is one the strip underneath does not have
  // to redraw behind.
  if (isOpaque()) {
    g.setGradientFill(juce::ColourGradient(backdropTop, 0.0f, 0.0f,
                                           backdropBottom, 0.0f,
                                           (float)getHeight(), false));
    g.fillRect(getLocalBounds());
  }

  // The meter sits behind the fader and owns its whole track, so it is drawn
  // wide enough to read at a glance rather than as a hairline beside it.
  const auto full = getLocalBounds().toFloat();
  const auto trackW = juce::jmax(6.0f, full.getWidth() * 0.62f);
  const auto track = full.withSizeKeepingCentre(trackW, full.getHeight());

  g.setColour(colours::groove);
  g.fillRoundedRectangle(track, trackW * 0.35f);

  // Recessed, so the light that reaches the panel does not reach the bottom of
  // the channel it is cut into.
  const auto lip = juce::jmin(6.0f, track.getHeight() * 0.06f);
  g.setGradientFill(
      juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), track.getX(),
                           track.getY(), juce::Colours::black.withAlpha(0.0f),
                           track.getX(), track.getY() + lip, false));
  g.fillRect(track.withHeight(lip));

  const auto count = segments();
  const auto onNow = juce::jlimit(0, count, lit);
  const auto step = track.getHeight() / (float)count;
  const auto gap = juce::jlimit(1.0f, 3.0f, step * 0.18f);

  // Off is the channel's own colour held down low, so an idle meter still says
  // which channel it belongs to.
  const auto on = colour.withAlpha(0.92f);
  const auto off = colour.withAlpha(0.13f);

  // Bloom around the lit run, so the meter reads as something emitting light
  // into the panel rather than as coloured rectangles.
  //
  // Once for the whole run rather than once per segment: twenty fills a frame
  // times thirty-three meters is a real cost, and at this size the eye cannot
  // tell the two apart anyway. Kept inside the component's own bounds, which
  // is what stops the invalidated band having to grow and the strip behind
  // having to repaint.
  if (onNow > 0) {
    const auto litTop = track.getBottom() - (float)onNow * step;
    const auto run = juce::Rectangle<float>(track.getX(), litTop,
                                            track.getWidth(),
                                            track.getBottom() - litTop);

    const auto room = juce::jmax(0.0f, (full.getWidth() - trackW) * 0.5f);
    const auto spread = juce::jmin(kMeterGlowSpread, room);

    g.setColour(colour.withAlpha(0.09f));
    g.fillRoundedRectangle(
        run.expanded(spread, juce::jmin(kMeterGlowSpread, spread)), 4.0f);

    g.setColour(colour.withAlpha(0.10f));
    g.fillRoundedRectangle(run.expanded(spread * 0.45f, spread * 0.45f), 3.0f);
  }

  for (int i = 0; i < count; ++i) {
    const auto lamp =
        juce::Rectangle<float>(track.getX() + 1.0f,
                               track.getBottom() - (float)(i + 1) * step,
                               track.getWidth() - 2.0f, step)
            .reduced(0.0f, gap * 0.5f);

    g.setColour(i < onNow ? on : off);
    g.fillRoundedRectangle(lamp, juce::jmin(2.0f, lamp.getHeight() * 0.4f));
  }
}

// =============================================================================

ChannelStrip::ChannelStrip(juce::AudioProcessorValueTreeState &state,
                           LinkTarget &linkTarget, HoverTarget &hoverTarget,
                           juce::Component &popupParent, int index0)
    : apvts(state), link(linkTarget), hover(hoverTarget),
      popupHost(popupParent), index(index0), info(harmonic(index0)),
      colour(intervalColour(harmonic(index0).pitchClass)),
      muteButton(state, "M"), soloButton(state, "S"), meter(colour),
      pitchLamp(colour), envLamp(colour), keyOffLamp(colour),
      tremoloLamp(colour) {
  // Deep listener, so a pointer resting on a knob is reported by the strip that
  // owns it rather than being swallowed by the control.
  addMouseListener(this, true);

  for (juce::Component *lamp : {(juce::Component *)&pitchLamp,
                                (juce::Component *)&envLamp,
                                (juce::Component *)&keyOffLamp,
                                (juce::Component *)&tremoloLamp})
    addAndMakeVisible(*lamp);

  // The strip decides the pointer for everything on it, which is how the LINK
  // tool shows itself. Children keep their own only where that would be wrong,
  // as on the mute and solo buttons.
  setMouseCursor(juce::MouseCursor::ParentCursor);

  // Every knob carries the strip's colour at full strength. They used to be
  // desaturated below the tuning knob so the head of the strip read as the
  // important one, which was the right call against a background that
  // alternated: the eye needed something to hold on to. With the mixer on one
  // flat grey the colour is the only thing separating a channel from its
  // neighbours, and holding it back on nineteen knobs out of twenty was
  // spending the one thing that was working.
  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, colour);
  setUpKnob(pmDepth, Role::PmDepth, colour);
  setUpKnob(phase, Role::Phase, colour);
  setUpKnob(drift, Role::Drift, colour);
  setUpKnob(delay, Role::Delay, colour);
  setUpKnob(attack, Role::Attack, colour);
  setUpKnob(decay, Role::Decay, colour);
  setUpKnob(sustain, Role::Sustain, colour);
  setUpKnob(swell, Role::Swell, colour);
  setUpKnob(offLevel, Role::OffLevel, colour);
  setUpKnob(release, Role::Release, colour);
  setUpKnob(lift, Role::Lift, colour);
  setUpKnob(amRate, Role::AmRate, colour);
  setUpKnob(amDepth, Role::AmDepth, colour);
  setUpKnob(velocity, Role::Velocity, colour);
  setUpKnob(aftertouch, Role::Aftertouch, colour);

  setUpKnob(pan, Role::Pan, colour);

  // These all run either side of zero, so their arcs read out from twelve
  // o'clock rather than filling from the left.
  lift.getProperties().set("bipolar", true);
  velocity.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);
  setUpFader(volume, Role::Volume, colour);

  muteButton.setColour(juce::TextButton::buttonOnColourId, colours::muteOn);
  soloButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  muteButton.setTooltip("Mute harmonic " + juce::String(info.harmonic));
  soloButton.setTooltip("Solo harmonic " + juce::String(info.harmonic));
  muteButton.setTitle("Harmonic " + juce::String(info.harmonic) + " mute");
  soloButton.setTitle("Harmonic " + juce::String(info.harmonic) + " solo");
  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::muteSuffix, index), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::soloSuffix, index), soloButton);

  // Readouts rather than controls, so they take no clicks of their own and
  // the strip underneath goes on reporting which row the pointer is over.
  for (auto *d : {&tuneReadout, &levelReadout}) {
    d->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*d);
  }

  addAndMakeVisible(meter);
  meter.toBack(); // the fader cap has to draw over it

  updateTuneReadout();
  updateLevelReadout();

  const auto cents = juce::String(info.jiCents, 1);
  setTooltip("Harmonic " + juce::String(info.harmonic) + "  -  " +
             intervalName(info.pitchClass) + "\n" +
             juce::String(info.etSemitones) +
             " semitones above the played note" + "\nJust intonation is " +
             (info.jiCents >= 0.0 ? "+" : "") + cents + " cents from that");
}

void ChannelStrip::setUpKnob(LinkableSlider &s, Role role, juce::Colour fill) {
  // What a screen reader announces. JUCE gives a slider a role and reads its
  // value out already; the name is the only part it cannot work out, and
  // without one a mixer of six hundred controls is six hundred things all
  // called "slider".
  s.setTitle("Harmonic " + juce::String(info.harmonic) + " " + roleLabel(role));

  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::setUpFader(LinkableSlider &s, Role role, juce::Colour fill) {
  s.setTitle("Harmonic " + juce::String(info.harmonic) + " " + roleLabel(role));

  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::LinearVertical);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::trackColourId, fill);
  s.getProperties().set("meteredGroove", true);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::wireUp(LinkableSlider &s, Role role) {
  const auto paramId = params::oscParamId(roleSuffix(role), index);

  // The attachment must exist before onValueChange fires, or the first callback
  // reads a slider whose range has not been set up yet.
  sliderAttachments.push_back(
      std::make_unique<SliderAttachment>(apvts, paramId, s));

  // Double-click restores the parameter's own default rather than the range
  // minimum.
  if (auto *p = apvts.getParameter(paramId))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  s.onUserDragStart = [this, role] { link.linkDragStarted(role, index); };
  s.onUserDragEnd = [this, role] { link.linkDragEnded(role, index); };

  s.onValueChange = [this, &s, role] {
    if (role == Role::Tune)
      updateTuneReadout();
    else if (role == Role::Volume)
      updateLevelReadout();

    if (s.isUserDragging())
      link.linkValueChanged(role, index, (float)s.getValue());
  };
}

void ChannelStrip::updateTuneReadout() {
  const auto blend = tune.getValue();
  const auto cents = blend * info.jiCents;

  juce::String text;
  auto active = true;

  if (std::abs(info.jiCents) < 0.05) {
    // The octaves land in the same place either way, so there is nothing for
    // the knob to do and the display says so rather than implying a choice.
    text = "0.0";
    active = false;
  } else if (blend <= 0.0005) {
    text = "Et";
    active = false;
  } else {
    // Only the minus is shown. Seven bars can make a convincing minus but not
    // a plus: the upright has to be drawn into the same narrow cell, and what
    // comes out reads as a speck beside the minus rather than as its opposite.
    // A sharp partial is then the one with no sign, which is how a tuner
    // writes it too.
    //
    // Nothing shuffles as a result. A harmonic's just interval sits on one
    // side of equal temperament or the other and stays there, so a given
    // channel's reading never changes sign.
    text = std::abs(cents) < 0.05 ? juce::String("0.0")
                                  : juce::String(cents, 1);
  }

  tuneReadout.setReading(text, active);
}

void ChannelStrip::updateLevelReadout() {
  const auto v = (float)volume.getValue();

  // A fader all the way down has no decibels to report, and saying so is more
  // use than a floor figure that looks like a setting. Dimmed, since it is the
  // one reading that is not a level. Everything above that counts down to
  // params::kQuietestLevelDb, which is as far as the cells reach.
  if (params::isSilentGain(v))
    return levelReadout.setReading("-inF", false);

  const auto db = params::levelDecibels(v);

  levelReadout.setReading(
      (db >= 0.0f ? "" : "-") + juce::String(std::abs(db), 1), true);
}

void ChannelStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;
  setAlpha(silenced ? 0.4f : 1.0f);
}

void ChannelStrip::mouseDown(const juce::MouseEvent &e) {
  if (!e.mods.isPopupMenu())
    return;

  // Whatever menu is about to open is modal, and a modal menu means this strip
  // is never told the pointer has left it. Letting the hover go now is what
  // stops the column being left lit on a channel the pointer has long since
  // moved away from, which used to need hovering that channel again to clear.
  clearHover();

  // The strip listens to every one of its children, so a right-click on a mute
  // or solo button arrives here as well as at the button. Those carry a menu of
  // their own and it is the one that should open, rather than both in turn.
  //
  // originalComponent rather than eventComponent, since that is the one JUCE
  // promises still names where the click landed after any retargeting.
  if (dynamic_cast<const MuteSoloButton *>(e.originalComponent) != nullptr)
    return;

  link.showLinkMenu();
}

void ChannelStrip::clearHover() {
  // Held until the pointer moves of its own accord again. Opening a menu takes
  // the mouse away, and the exit that arrives on the way out carries a
  // position still inside this strip, which reportHover reads as "the pointer
  // is here" and would light it straight back up.
  hoverSuppressed = true;

  hover.hoverChanged(index, kNoRow);

  if (hovered) {
    hovered = false;
    repaint();
  }
}

// A pointer that moves under its own steam is the thing that ends a
// suppression, and an exit is not that: see clearHover.
void ChannelStrip::mouseEnter(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);
}

void ChannelStrip::mouseMove(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);
}
void ChannelStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

void ChannelStrip::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto rows =
      layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed);
  const auto inside = getLocalBounds().contains(p) && !hoverSuppressed;

  // Leaving one knob for the next fires the exit before the enter, so the
  // answer is always worked out from where the pointer now is rather than from
  // which callback arrived.
  hover.hoverChanged(index, inside ? controlRowAt(rows, p) : kNoRow);

  // The channel highlight is the strip's own business rather than the
  // editor's, for the same reason: worked out from the pointer, it cannot be
  // left lit by an exit and an enter arriving in the wrong order.
  if (inside != hovered) {
    hovered = inside;
    repaint();
  }
}

void ChannelStrip::paintOverChildren(juce::Graphics &g) {
  if (hovered)
    paintColumnHighlight(g, getLocalBounds());
}

void ChannelStrip::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows =
      layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed);

  // Only the two bands that changed are repainted. With 33 strips answering
  // every time the pointer crosses a row, repainting whole channels would
  // redraw the window several times a second for a wash and two hairlines.
  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

void ChannelStrip::setCollapsedSections(SectionMask mask) {
  if (mask == collapsed)
    return;

  collapsed = mask;

  // A hover held on a row that just folded away would keep the gutter caption
  // lit for a knob nobody can see.
  if (rowIsCollapsed(highlighted, collapsed))
    highlighted = kNoRow;

  resized();
  repaint();
}

LinkableSlider *ChannelStrip::sliderForRole(Role role) {
  switch (role) {
  case Role::Tune:
    return &tune;
  case Role::PmRate:
    return &pmRate;
  case Role::PmDepth:
    return &pmDepth;
  case Role::Phase:
    return &phase;
  case Role::Drift:
    return &drift;
  case Role::Delay:
    return &delay;
  case Role::Attack:
    return &attack;
  case Role::Decay:
    return &decay;
  case Role::Sustain:
    return &sustain;
  case Role::Swell:
    return &swell;
  case Role::OffLevel:
    return &offLevel;
  case Role::Release:
    return &release;
  case Role::Lift:
    return &lift;
  case Role::AmRate:
    return &amRate;
  case Role::AmDepth:
    return &amDepth;
  case Role::Velocity:
    return &velocity;
  case Role::Aftertouch:
    return &aftertouch;
  case Role::Pan:
    return &pan;
  case Role::Volume:
    return &volume;

  case Role::NumRoles:
  default:
    jassertfalse;
    return nullptr;
  }
}

void ChannelStrip::setLinkGlow(Role role, float amount) {
  amount = juce::jlimit(0.0f, 1.0f, amount);

  if (role == glowRole && std::abs(amount - glowAmount) < 0.004f)
    return;

  // Moving to a different row leaves the old control lit unless it is put out
  // on the way past.
  if (role != glowRole)
    if (auto *previous = sliderForRole(glowRole)) {
      previous->getProperties().set("linkGlow", 0.0);
      previous->repaint();
    }

  glowRole = role;
  glowAmount = amount;

  if (auto *s = sliderForRole(glowRole)) {
    s->getProperties().set("linkGlow", (double)glowAmount);
    s->repaint();
  }
}

juce::Colour ChannelStrip::backdropBase() const {
  // Octaves stand a shade brighter, which is the same shade the noise channel
  // stands at. It used to be a wash of the channel's own blue, and that was
  // one more colour in a mixer that has plenty: the point of marking the
  // octaves is where they are, not what they are, and a change of level says
  // that without adding a hue.
  return info.pitchClass == 0 ? colours::channel.brighter(0.03f)
                              : colours::channel;
}

void ChannelStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  paintChannelBackground(g, bounds, backdropBase());

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed);

  if (rowShowsHighlight(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colour);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  // Accent when the pointer is on this channel, which is exactly what the
  // gutter does to the caption of the row it is on. The number is the name of
  // the channel, so lighting it is the same gesture.
  // Centred in what is left of the header rather than pinned to the top of
  // it. The number is the only thing standing here.
  g.setColour(hovered ? colours::accent : colours::text);
  g.setFont(makeFont(14.0f, true));
  g.drawText(juce::String(info.harmonic), header, juce::Justification::centred,
             false);

  // Section rules, aligned with the gutter headings. Four of the five carry a
  // lamp, and those draw their own rule around it, so only the output divider
  // is left for the strip to draw.
  for (auto r : {Row::OutputHeading}) {
    const auto row = rows[rowIndex(r)];
    const auto y = row.getY() + row.getHeight() / 2;

    // Scored, like the rules the lamps sit on. See ActivityLamp::paint.
    g.setColour(colours::outline.withAlpha(0.7f));
    g.fillRect(row.getX(), y, row.getWidth(), 1);

    g.setColour(juce::Colours::white.withAlpha(0.055f));
    g.fillRect(row.getX(), y + 1, row.getWidth(), 1);
  }
}

void ChannelStrip::resized() {
  const auto rows =
      layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed);

  // Hidden rather than left at zero height. A knob with no height still takes
  // the mouse and still answers a hover, so a folded section would go on
  // lighting gutter captions and opening LINK menus for knobs nobody can see.
  const auto placeRow = [&](juce::Component &c, Row r, int shrink) {
    if (rowIsCollapsed(r, collapsed)) {
      c.setVisible(false);
      return;
    }

    c.setVisible(true);
    c.setBounds(rows[rowIndex(r)].reduced(shrink));
  };

  placeRow(tune, Row::TuneKnob, 0);
  placeRow(tuneReadout, Row::TuneText, 0);
  placeRow(pmRate, Row::PmRate, 1);
  placeRow(pmDepth, Row::PmDepth, 1);
  placeRow(phase, Row::Phase, 1);
  placeRow(drift, Row::Drift, 1);
  placeRow(delay, Row::Delay, 1);
  placeRow(attack, Row::Attack, 1);
  placeRow(decay, Row::Decay, 1);
  placeRow(sustain, Row::Sustain, 1);
  placeRow(swell, Row::Swell, 1);
  placeRow(offLevel, Row::OffLevel, 1);
  placeRow(release, Row::Release, 1);
  placeRow(lift, Row::Lift, 1);
  placeRow(amRate, Row::AmRate, 1);
  placeRow(amDepth, Row::AmDepth, 1);
  placeRow(velocity, Row::Velocity, 1);
  placeRow(aftertouch, Row::Aftertouch, 1);
  placeRow(pan, Row::Pan, 1);
  // Meter and fader share the same rectangle. The meter draws the track and
  // the fader draws only its cap on top, so the output fills the fader itself.
  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));

  // The strip's background is a gradient down the whole channel, so the meter
  // is told the two colours at its own edges. Both take their base from
  // backdropBase, so an octave's shade cannot come adrift from the rest of it.
  {
    const auto base = backdropBase();
    const auto top = base.brighter(0.10f);
    const auto bottom = base.darker(0.06f);

    const auto at = [&](int y) {
      auto shade = top.interpolatedWith(
          bottom, juce::jlimit(0.0f, 1.0f,
                               (float)y / (float)juce::jmax(1, getHeight())));

      return shade;
    };

    meter.setBackdrop(at(meter.getY()), at(meter.getBottom()));
  }
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  auto ms = rows[rowIndex(Row::MuteSolo)];
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2).reduced(1));
  soloButton.setBounds(ms.reduced(1));

  // The lamps stand on the rules that divide the strip into groups, each one
  // at the head of the group it reports on. No row grew to make space for
  // them: the rule was already occupying that height to draw a single line.
  {
    const auto base = backdropBase();
    const auto top = base.brighter(0.10f);
    const auto bottom = base.darker(0.06f);

    const auto at = [&](int y) {
      auto shade = top.interpolatedWith(
          bottom, juce::jlimit(0.0f, 1.0f,
                               (float)y / (float)juce::jmax(1, getHeight())));

      return shade;
    };

    const auto place = [&](juce::Component &lamp, Row row) {
      const auto r = rows[rowIndex(row)];
      lamp.setBounds(r);
      return at(r.getCentreY());
    };

    pitchLamp.setBackdrop(place(pitchLamp, Row::PitchModHeading));
    envLamp.setBackdrop(place(envLamp, Row::EnvHeading));
    keyOffLamp.setBackdrop(place(keyOffLamp, Row::KeyOffHeading));
    tremoloLamp.setBackdrop(place(tremoloLamp, Row::AmpModHeading));
  }
}

float ChannelStrip::needlePosition(float cents) {
  // Compressed rather than linear, and that is the whole design of it.
  //
  // The travel is about fifteen pixels either side of centre. Spread linearly
  // over the 225 cents the two controls can reach together, an ordinary
  // vibrato of five cents moves the needle by a third of a pixel, so every
  // subtle setting on the instrument would look identical to no setting at
  // all. A square root keeps the ends where they belong and gives the shallow
  // half of the range somewhere to be: five cents lands two pixels out,
  // twenty-five lands five, and two hundred still nearly fills the travel.
  //
  // What it does not do is normalise per strip, which is what this used to do
  // and why every channel ran to the edges whatever its depth. Two channels
  // can now be compared by eye, which is the point of a fixed scale.
  const auto span = juce::jmax(1.0f, params::kMaxPitchDisplacementCents);
  const auto reach = juce::jlimit(-1.0f, 1.0f, cents / span);

  return reach < 0.0f ? -std::sqrt(-reach) : std::sqrt(reach);
}

void ChannelStrip::setActivity(float envelope, float tremolo, float pitch,
                               juce::Array<juce::Rectangle<int>> &into) {
  const auto refresh = [&into](juce::Component &lamp, bool moved) {
    if (moved)
      into.add(lamp.getBounds());
  };

  // One lamp or the other, never both: the sign says which half of the
  // envelope is running, and the half that is not gets zero rather than a
  // stale value it would otherwise hold on to.
  const auto level = std::abs(envelope);
  const auto afterKeyOff = envelope < 0.0f;

  refresh(envLamp, envLamp.push(afterKeyOff ? 0.0f : level));
  refresh(keyOffLamp, keyOffLamp.push(afterKeyOff ? level : 0.0f));

  // The tremolo lamp is gated on the envelope, so it stops when the note does
  // rather than going on pulsing over silence.
  refresh(tremoloLamp, tremoloLamp.push(level > 0.0f ? tremolo : 0.0f));

  // Parked only when nothing is sounding. With a fixed scale a partial that
  // has no modulation on it reads dead centre, which is the truth about it and
  // worth showing, where under the old per-strip scale centre meant nothing.
  // kParked is anything past the ends of the travel.
  constexpr float kParked = -3.0f;

  refresh(pitchLamp, pitchLamp.push(level <= 0.0f ? kParked
                                                  : needlePosition(pitch)));
}

} // namespace ovt::ui
