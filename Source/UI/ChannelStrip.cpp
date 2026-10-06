#include "ChannelStrip.h"
#include "LearnMenu.h"

#include <cmath>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
/// A wheel notch in pixels, by the same arithmetic a Viewport uses on itself.
///
/// Taken from JUCE rather than chosen so that scrolling the parameters and
/// scrolling the mixer sideways move by the same amount for the same gesture,
/// which they would not if this were a number somebody liked the feel of.
int wheelDistance(const juce::MouseWheelDetails &wheel) {
  constexpr float kViewportSingleStep = 16.0f;

  // deltaY as the platform gives it, with no account taken of isReversed. The
  // platform has already turned the wheel the way the person asked for, so
  // natural scrolling arrives here pointing the right way and undoing it is
  // what made it scroll backwards on a Mac set up that way. A Viewport reads
  // it the same, which is why the sideways scroll was always right.
  return -juce::roundToInt(wheel.deltaY * 14.0f * kViewportSingleStep);
}
} // namespace

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
//
// Three above and one below rather than anything nearer the middle. A dial is
// a heavy round object with its tick marks above it and the caption is a light
// line of small text below, so a block centred by arithmetic reads as sitting
// low. Only the top bar uses these: the mixer's own knobs carry no caption,
// since the gutter names their rows once for all 33 channels.
constexpr int kAboveDial = 3;
constexpr int kCaptionGap = 1;
constexpr int kCaptionHeight = 11;
constexpr int kBelowCaption = 1;
} // namespace

void LabelledKnob::paint(juce::Graphics &g) {
  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));
  g.drawText(caption, captionBounds(getLocalBounds()),
             juce::Justification::centred, false);
}

juce::Rectangle<int> LabelledKnob::captionBounds(juce::Rectangle<int> bounds) {
  bounds.removeFromBottom(kBelowCaption);
  return bounds.removeFromBottom(kCaptionHeight);
}

juce::Rectangle<int> LabelledKnob::dialBounds(juce::Rectangle<int> bounds) {
  bounds.removeFromBottom(kBelowCaption + kCaptionHeight + kCaptionGap);
  bounds.removeFromTop(kAboveDial);
  return bounds;
}

void LabelledKnob::resized() { slider.setBounds(dialBounds(getLocalBounds())); }

// =============================================================================

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

  // ---- a sign sharing the hundreds digit's cell ---------------------------
  //
  // A reading that runs to three digits and can be negative has a hundreds
  // place that is only ever a one or nothing, and a sign that is only ever a
  // minus or nothing. The bar a minus needs and the two uprights a one needs
  // are different segments, so one cell carries both and the reading saves
  // the whole width of a separate sign.
  //
  // Which is worth having twice over: the digits get the room, and the sign
  // stops being a narrow cell that the digits have to be padded around to
  // keep them from shifting when a value crosses zero.
  //
  // Only the macro panel's amount asks for these. Nothing on a channel
  // changes sign.
  // The sign is the middle bar, lit like any other segment, and the one
  // beside it is the pair of uprights. Nothing is drawn specially for either:
  // a cell shows the bars it has and these are two of them.
  case '~':
    return kSegG;
  case '!':
    return kSegG | kSegB | kSegC;

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

/// One segment, as an elongated hexagon with mitred ends.
///
/// This is the shape a real one is, and it is most of what makes a drawn
/// display look drawn: a rounded rectangle has the same footprint but reads
/// as a lozenge laid on a panel, where a mitred bar reads as one of a set cut
/// from a single mask. The ends slope so that neighbouring segments point at
/// each other across the small gap between them, which is what turns seven
/// separate bars into a figure.
juce::Path segmentShape(juce::Rectangle<float> r) {
  const bool flat = r.getWidth() >= r.getHeight();
  const auto thick = flat ? r.getHeight() : r.getWidth();
  const auto along = flat ? r.getWidth() : r.getHeight();

  // Never more than the bar can give. A half-middle on a narrow cell is
  // barely longer than it is thick, and a full mitre on that leaves two
  // triangles meeting at a point.
  const auto nose = juce::jmin(thick * 0.5f, along * 0.42f);

  juce::Path p;

  if (flat) {
    p.startNewSubPath(r.getX(), r.getCentreY());
    p.lineTo(r.getX() + nose, r.getY());
    p.lineTo(r.getRight() - nose, r.getY());
    p.lineTo(r.getRight(), r.getCentreY());
    p.lineTo(r.getRight() - nose, r.getBottom());
    p.lineTo(r.getX() + nose, r.getBottom());
  } else {
    p.startNewSubPath(r.getCentreX(), r.getY());
    p.lineTo(r.getRight(), r.getY() + nose);
    p.lineTo(r.getRight(), r.getBottom() - nose);
    p.lineTo(r.getCentreX(), r.getBottom());
    p.lineTo(r.getX(), r.getBottom() - nose);
    p.lineTo(r.getX(), r.getY() + nose);
  }

  p.closeSubPath();
  return p;
}

// ---- fourteen bars, for the displays that have to spell ---------------------
//
//      aaaaaaa
//     f  h i j  b
//     f   hij   b
//      ggg   GGG
//     e   mlk   c
//     e  m l k  c
//      ddddddd
//
// Seven bars cannot write a name. They manage digits and about five letters,
// which is all the readouts on a channel ever have to say, but a preset is
// called Glockenspiel or Wurli and there is no seven-bar W at all. Fourteen
// is what the hardware that had to show words used, and it carries the whole
// alphabet: the middle bar splits in two and four diagonals and two uprights
// fill the cell.
constexpr uint16_t kStA = 1 << 0, kStB = 1 << 1, kStC = 1 << 2, kStD = 1 << 3,
                   kStE = 1 << 4, kStF = 1 << 5, kStG1 = 1 << 6, kStG2 = 1 << 7,
                   kStH = 1 << 8, kStI = 1 << 9, kStJ = 1 << 10, kStK = 1 << 11,
                   kStL = 1 << 12, kStM = 1 << 13;

constexpr uint16_t kStG = kStG1 | kStG2;

uint16_t starburstFor(char c) {
  switch (c) {
  case '0':
    return kStA | kStB | kStC | kStD | kStE | kStF | kStJ | kStM;
  case '1':
    return kStB | kStC | kStJ;
  case '2':
    return kStA | kStB | kStG | kStE | kStD;
  case '3':
    return kStA | kStB | kStC | kStD | kStG2;
  case '4':
    return kStF | kStG | kStB | kStC;
  case '5':
    return kStA | kStF | kStG | kStC | kStD;
  case '6':
    return kStA | kStF | kStE | kStD | kStC | kStG;
  case '7':
    return kStA | kStB | kStC;
  case '8':
    return kStA | kStB | kStC | kStD | kStE | kStF | kStG;
  case '9':
    return kStA | kStB | kStC | kStD | kStF | kStG;

  case 'A':
    return kStA | kStB | kStC | kStE | kStF | kStG;
  case 'B':
    return kStA | kStB | kStC | kStD | kStG2 | kStI | kStL;
  case 'C':
    return kStA | kStD | kStE | kStF;
  case 'D':
    return kStA | kStB | kStC | kStD | kStI | kStL;
  case 'E':
    return kStA | kStD | kStE | kStF | kStG1;
  case 'F':
    return kStA | kStE | kStF | kStG1;
  case 'G':
    return kStA | kStC | kStD | kStE | kStF | kStG2;
  case 'H':
    return kStB | kStC | kStE | kStF | kStG;
  case 'I':
    return kStA | kStD | kStI | kStL;
  case 'J':
    return kStB | kStC | kStD | kStE;
  case 'K':
    return kStE | kStF | kStG1 | kStJ | kStK;
  case 'L':
    return kStD | kStE | kStF;
  case 'M':
    return kStB | kStC | kStE | kStF | kStH | kStJ;
  case 'N':
    return kStB | kStC | kStE | kStF | kStH | kStK;
  case 'O':
    return kStA | kStB | kStC | kStD | kStE | kStF;
  case 'P':
    return kStA | kStB | kStE | kStF | kStG;
  case 'Q':
    return kStA | kStB | kStC | kStD | kStE | kStF | kStK;
  case 'R':
    return kStA | kStB | kStE | kStF | kStG | kStK;
  case 'S':
    return kStA | kStC | kStD | kStF | kStG;
  case 'T':
    return kStA | kStI | kStL;
  case 'U':
    return kStB | kStC | kStD | kStE | kStF;
  case 'V':
    return kStE | kStF | kStJ | kStM;
  case 'W':
    return kStB | kStC | kStE | kStF | kStK | kStM;
  case 'X':
    return kStH | kStJ | kStK | kStM;
  case 'Y':
    return kStH | kStJ | kStL;
  case 'Z':
    return kStA | kStD | kStJ | kStM;

  // The punctuation a preset name can carry. A saved name keeps everything a
  // filename can hold, which is nearly everything, so these are the ones
  // worth having rather than all of them: the hyphen alone is in three of the
  // factory names and without it Lo-fi reads as LO FI.
  case '-':
    return kStG;
  case '_':
    return kStD;
  case '=':
    return kStG | kStD;
  case '+':
    return kStG | kStI | kStL;
  case '\'':
    return kStI;
  case '(':
    return kStJ | kStK;
  case ')':
    return kStH | kStM;
  case '!':
    return kStI | kStL;

  // A space is a cell with nothing lit, which is a word break rather than a
  // character that failed to draw. Both come out the same and that is right:
  // a cell on a real display either has bars on or it does not, and a name
  // with something unspellable in it shows a gap where a real one would.
  case ' ':
    return 0;

  default:
    return 0;
  }
}
} // namespace

bool SegmentDisplay::canDraw(char c, Bars bars) {
  if (bars == Bars::Fourteen)
    // A space has no bars lit and is still a character it can show, so the
    // table's zero cannot be the answer on its own here.
    return isNarrow(c) || c == ' ' ||
           starburstFor((char)juce::CharacterFunctions::toUpperCase(c)) != 0;

  return isNarrow(c) || segmentsFor(c) != 0;
}

SegmentDisplay::SegmentDisplay(juce::String unit, Bars howMany, int cells)
    : bars(howMany), fixedCells(cells), unitText(std::move(unit)) {}

juce::String SegmentDisplay::squeeze(const juce::String &name, int cells) {
  const auto upper = name.toUpperCase();

  if (cells <= 0)
    return upper;

  if (upper.length() <= cells)
    return upper.paddedRight(' ', cells);

  std::vector<juce::juce_wchar> kept;
  kept.reserve((size_t)upper.length());

  for (int i = 0; i < upper.length(); ++i)
    kept.push_back(upper[i]);

  const auto drop = [&kept, cells](bool (*wanted)(juce::juce_wchar)) {
    // From the right, so what goes is the end of the name rather than the
    // start of it, which is the half you recognise.
    //
    // Stopping at index one is belt and braces rather than a rule doing any
    // work. Reaching index zero would mean everything after it had already
    // gone and one character were still too many, and there is no cell count
    // below one, so the loop stops on its own first. It is written down
    // because a name has to keep its first character whatever else changes
    // here, and nothing else in this function would say so.
    for (auto i = (int)kept.size() - 1; i > 0 && (int)kept.size() > cells; --i)
      if (wanted(kept[(size_t)i]))
        kept.erase(kept.begin() + i);
  };

  drop([](juce::juce_wchar c) { return c == ' '; });
  drop([](juce::juce_wchar c) {
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
  });

  juce::String out;

  for (auto c : kept)
    out += juce::String::charToString(c);

  return out.substring(0, cells).paddedRight(' ', cells);
}

void SegmentDisplay::setReading(const juce::String &digits, bool isActive) {
  // Upper case, and fitted to the cells there are. Done here rather than at
  // every caller so that what getReading hands back is what the cells are
  // actually showing.
  const auto shown =
      bars == Bars::Fourteen ? squeeze(digits, fixedCells) : digits;

  if (shown == reading && isActive == active)
    return;

  reading = shown;
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

  // What separates one segment from the next. Tight, because the gap is what
  // the eye reads as the join between two bars and a wide one reads as seven
  // marks that happen to be near each other. It cannot go to nothing: the
  // mitred ends point at each other, so the gap at a corner is already about
  // seven tenths of this, and below about a fifth of a bar's thickness the
  // corners close up and a figure becomes a blob.
  const auto gap = t * 0.24f;
  const auto w = area.getWidth();
  const auto h = area.getHeight();
  const auto mid = (h - t) * 0.5f;

  struct Bar {
    uint8_t flag;
    juce::Rectangle<float> r;
  };

  const Bar straight[] = {
      {kSegA, {t * 0.5f + gap, 0.0f, w - t - gap * 2.0f, t}},
      {kSegB, {w - t, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegC, {w - t, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegD, {t * 0.5f + gap, h - t, w - t - gap * 2.0f, t}},
      {kSegE, {0.0f, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegF, {0.0f, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegG, {t * 0.5f + gap, mid, w - t - gap * 2.0f, t}},
  };

  for (const auto &bar : straight) {
    const bool isLit = (lit & bar.flag) != 0;
    const auto shape = segmentShape(bar.r.translated(area.getX(), area.getY()));

    // No bloom around a lit segment, though a real one has it. A bar here is
    // about three pixels thick, so any spill worth seeing is wider than the
    // bar it comes from: what it reads as is blur, and a blurred figure is
    // further from a real display than a crisp one is.
    g.setColour(isLit ? on : off);
    g.fillPath(shape);
  }
}

void SegmentDisplay::paintStarburst(juce::Graphics &g,
                                    juce::Rectangle<float> area, char c,
                                    juce::Colour on, juce::Colour off) const {
  const auto lit = starburstFor(c);

  // Thinner than a seven-bar cell's. Fourteen bars in the same box means four
  // diagonals crossing the middle, and at the seven-bar weight they meet in a
  // blot with no cell showing through.
  const auto t = juce::jmax(1.0f, area.getHeight() * 0.095f);
  // Tighter than it was, for the reason the seven-bar one is. A little wider
  // than that one in proportion, because fourteen bars in the same box means
  // four diagonals crossing the middle as well, and those have ends of their
  // own to stay clear of.
  const auto gap = t * 0.32f;
  const auto w = area.getWidth();
  const auto h = area.getHeight();
  const auto mid = (h - t) * 0.5f;
  const auto half = (w - t) * 0.5f;

  // The uprights and the two halves of the middle, drawn as rectangles the
  // way the seven-bar ones are.
  struct Bar {
    uint16_t flag;
    juce::Rectangle<float> r;
  };

  const Bar straight[] = {
      {kStA, {t * 0.5f + gap, 0.0f, w - t - gap * 2.0f, t}},
      {kStB, {w - t, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kStC, {w - t, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kStD, {t * 0.5f + gap, h - t, w - t - gap * 2.0f, t}},
      {kStE, {0.0f, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kStF, {0.0f, t * 0.5f + gap, t, mid - gap * 1.5f}},
      // The two halves of the middle, each running from its own end of the
      // cell to the upright that stands in the centre of it.
      //
      // Mirror images, which they were not: the right one used to start half
      // a bar past the centre instead of a whole one past it, so it reached
      // a bar's width too far left and the middle of every cell sat off to
      // that side.
      //
      // And longer than they were. A half-middle is squeezed between the
      // outer upright and the centre one, and at the inset the other bars
      // use it came out three pixels: a dot rather than a bar, so a hyphen
      // read as two specks.
      {kStG1, {t * 0.5f, mid, half - t * 0.5f - gap, t}},
      {kStG2, {half + t + gap, mid, w - t * 1.5f - half - gap, t}},
      {kStI, {half, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kStL, {half, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
  };

  for (const auto &bar : straight) {
    const bool isLit = (lit & bar.flag) != 0;
    const auto shape = segmentShape(bar.r.translated(area.getX(), area.getY()));

    // No bloom around a lit segment, though a real one has it. A bar here is
    // about three pixels thick, so any spill worth seeing is wider than the
    // bar it comes from: what it reads as is blur, and a blurred figure is
    // further from a real display than a crisp one is.
    g.setColour(isLit ? on : off);
    g.fillPath(shape);
  }

  // The four diagonals, which are strokes rather than rectangles. Each runs
  // from a corner of the cell to the middle of it.
  //
  // Trimmed along its own direction rather than by the same amount in x and
  // in y. A cell is half as wide as it is tall, so these are nowhere near
  // forty-five degrees, and pulling both ends in by an equal step on each
  // axis stopped them a third of the way short: X came out as four marks
  // around a hole and V and W did not close at the bottom.
  const auto cx = w * 0.5f;
  const auto cy = h * 0.5f;
  const auto corner = t * 0.9f;

  const auto spoke = [&](juce::Point<float> from) {
    const auto to = juce::Point<float>(cx, cy);
    const auto along = to - from;
    const auto len = std::sqrt(along.x * along.x + along.y * along.y);

    if (len < 1.0e-3f)
      return juce::Line<float>(from, to);

    const auto step = along / len;

    // Clear of the frame at the outer end and of the centre bars at the
    // inner one, both measured in bar widths so they hold at any size.
    return juce::Line<float>(from + step * (t * 0.3f), to - step * (t * 0.85f));
  };

  struct Slash {
    uint16_t flag;
    juce::Line<float> line;
  };

  const Slash slashes[] = {
      {kStH, spoke({corner, corner})},
      {kStJ, spoke({w - corner, corner})},
      {kStK, spoke({w - corner, h - corner})},
      {kStM, spoke({corner, h - corner})},
  };

  for (const auto &slash : slashes) {
    const bool isLit = (lit & slash.flag) != 0;
    const juce::Line<float> at{slash.line.getStart() + area.getPosition(),
                               slash.line.getEnd() + area.getPosition()};

    g.setColour(isLit ? on : off);
    g.drawLine(at, t * 0.9f);
  }
}

/// What the unit takes beside the digits, and what has to be left for them.
///
/// Measured in the width the component is given rather than in what is left
/// after its own insets, since the caller sizing it has only the former.
static constexpr float kUnitWidth = 21.0f;
static constexpr int kUnitInsets = 8;
static constexpr int kDigitsNeed = 44;

bool SegmentDisplay::hasRoomForUnit(int width) {
  return width - kUnitInsets > kDigitsNeed;
}

std::unique_ptr<juce::AccessibilityHandler>
SegmentDisplay::createAccessibilityHandler() {
  // Asked for lazily, the first time something goes looking for the panel's
  // controls, which is long after the bar has wired its readouts up. So
  // whether this one opens a menu is settled by the time it is asked.
  if (onClick == nullptr)
    return juce::Component::createAccessibilityHandler();

  return std::make_unique<juce::AccessibilityHandler>(
      *this, juce::AccessibilityRole::button,
      juce::AccessibilityActions().addAction(
          juce::AccessibilityActionType::press, [this] {
            if (onClick != nullptr)
              onClick();
          }));
}

void SegmentDisplay::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat().reduced(1.0f);

  const auto on = active ? colours::accent : colours::textDim;

  // Fainter where there are fourteen bars to a cell. Twice the bars means
  // twice the unlit ones, and at the seven-bar weight the dead segments add
  // up to a grid the lit ones have to be picked out of.
  const auto rest = bars == Bars::Fourteen ? 0.55f : 1.0f;
  const auto off = on.withAlpha((hovered ? 0.20f : 0.12f) * rest);
  const auto lit = on.withAlpha(active ? 0.95f : 0.75f);

  // Set into the panel rather than laid on it. The margin this uses is the one
  // pixel the ground was already leaving, so the digits keep every pixel they
  // had.
  paintRecess(g, area, 2.5f);

  // A screen rather than a recess. The unlit bars still have something dark to
  // be dark against, but the ground itself is lit, which is what the strip's
  // hover used to be the only thing providing.
  paintDisplayGround(g, area, 2.5f, hovered);

  if (hovered) {
    g.setColour(colours::outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 2.5f, 1.0f);
  }

  area = area.reduced(3.0f, 2.0f);

  drawn = 0;

  if (reading.isEmpty())
    return;

  // The point and the sign ride on narrow stripes of their own rather than
  // taking a whole cell, the way they do on a real display.
  int cells = 0, points = 0;

  for (auto c : reading)
    (isNarrow((char)c) ? points : cells) += 1;

  // Every character takes a whole cell where there are fourteen bars. A sign
  // riding on a narrow stripe is a thing a number does, and this one spells,
  // so a hyphen is a letter's worth of room like anything else. Counting it
  // as a stripe left the cells one short of the characters and dropped the
  // last one: Lo-fi came out as LO-F.
  if (bars == Bars::Fourteen) {
    cells += points;
    points = 0;
  }

  // A name longer than the display can spell legibly is cut rather than
  // squeezed. Below about five pixels a fourteen-bar cell is four diagonals
  // in a smudge, and a reading nobody can make out is worse than a short one.
  if (bars == Bars::Fourteen) {
    constexpr float kNarrowestCell = 5.0f;

    const auto room = (int)(area.getWidth() / kNarrowestCell);

    if (cells > room) {
      cells = juce::jmax(1, room);
      points = 0;
    }
  }

  if (cells < 1)
    return;

  // The unit only earns its place once the digits have what they need, and it
  // takes the room the words actually need rather than the budget set aside
  // for them.
  //
  // kUnitWidth is a reserve wide enough for the longest of them. Centring the
  // block on the reserve rather than on the text puts whatever is left over
  // inside the block, so a short unit pushes the digits left and the reading
  // sits against the inside of the screen looking cropped. "kHz" is three
  // characters in a reserve sized for more.
  const auto unitFont = makeFont(7.5f, true);

  auto unitW = 0.0f;

  if (unitText.isNotEmpty() && hasRoomForUnit(getWidth())) {
    juce::GlyphArrangement measure;
    measure.addLineOfText(unitFont, unitText, 0.0f, 0.0f);

    unitW = juce::jmin(kUnitWidth,
                       measure.getBoundingBox(0, -1, true).getWidth() + 2.0f);
  }

  // A point costs about a third of a digit, which is what the extra term in
  // the denominator is buying.
  // Wider than a seven-bar cell for its height, because there is more to fit
  // across it: the two middle uprights and the four diagonals all live in the
  // width, and at the narrower ratio they met in the middle as a blot.
  const auto widest =
      area.getHeight() * (bars == Bars::Fourteen ? 0.74f : 0.72f);

  // No margin is taken out of this.
  //
  // Two pixels were, to stop the leading segment landing against the inside
  // of the screen. It was not landing there, and taking them changed what the
  // line below chooses: with less room, a reading that had been sized off its
  // own height came to be sized off the width instead, so the figures grew
  // and shrank with how many characters were in them. A readout whose digits
  // change size as the number changes is worse than one sitting a pixel
  // nearer its edge than it might.
  const auto cellW = juce::jmin((area.getWidth() - unitW) /
                                    ((float)cells + 0.32f * (float)points),
                                widest);

  const auto glyphW = cellW * 0.82f;
  const auto pointW = cellW * 0.32f;
  const auto runW = cellW * (float)cells + pointW * (float)points;

  // Centred as one block, so a three-character reading does not sit off to one
  // side of a display sized for four.
  auto x = area.getCentreX() - (runW + unitW) * 0.5f;

  const auto digitArea = area.withX(x).withWidth(runW);

  if (unitW > 0.0f) {
    g.setColour(colours::textDim.withAlpha(0.8f));
    g.setFont(unitFont);
    g.drawText(unitText,
               area.withX(digitArea.getRight() + 2.0f).withWidth(unitW - 2.0f),
               juce::Justification::centredLeft, false);
  }

  drawn = 0;

  for (int i = 0; i < reading.length(); ++i) {
    const auto c = (char)reading[i];

    if (bars == Bars::Fourteen && drawn >= cells)
      break;

    if (bars == Bars::Fourteen) {
      // Everything takes a whole cell here. A sign riding on a narrow stripe
      // is a thing a number does, and this one is spelling.
      paintStarburst(g, {x, digitArea.getY(), glyphW, digitArea.getHeight()}, c,
                     lit, off);
      x += cellW;
      ++drawn;
      continue;
    }

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
      ++drawn;
      continue;
    }

    paintGlyph(g, {x, digitArea.getY(), glyphW, digitArea.getHeight()}, c, lit,
               off);
    x += cellW;
    ++drawn;
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

  // The hole it is mounted in, which is why the rule stops two pixels short
  // either side: that gap is the lip, and it was there before there was
  // anything to put in it.
  paintRecess(g, lamp, diameter * 0.5f);

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

  // Blended against the backdrop rather than laid over it at that alpha, which
  // comes to the same colour and covers the floor of the hole while it is at
  // it. A see-through face would show the shadow under it and read as a lamp
  // somebody had smudged.
  g.setColour(isOpaque()
                  ? backdrop.interpolatedWith(colour, 0.16f + 0.84f * lit)
                  : colour.withAlpha(0.16f + 0.84f * lit));
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

  const auto usable = juce::jmax(1, getWidth() - 2 * kSlotInset - 3);
  const auto at =
      kSlotInset + 1 +
      (int)std::lround(0.5 * (1.0 + juce::jlimit(-1.0f, 1.0f, position)) *
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
  const auto track = juce::Rectangle<float>(
                         bounds.getWidth() - 2.0f * (float)kSlotInset, height)
                         .withCentre({bounds.getCentreX(), midY + 0.5f});

  // A slot milled across the strip rather than a line drawn on it, and the
  // inset above is what lets it read as one. Run out to both edges the slot
  // loses its rounded ends and the two side walls of the cut along with
  // them, because the lip is drawn around the opening and the component stops
  // where the opening does. What is left is a bar that looks sheared off
  // rather than milled, which is visible the moment anybody looks closely.
  paintRecess(g, track, height * 0.35f);

  // Blended against the backdrop rather than washed over it, for the same
  // reason the lamps are: the same colour, and it covers the shadow that runs
  // under the slot as well as around it.
  g.setColour(isOpaque() ? backdrop.interpolatedWith(colour, 0.16f)
                         : colour.withAlpha(0.16f));
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

int LevelMeter::segments() const { return meterSegments(getHeight()); }

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
    const auto run = juce::Rectangle<float>(
        track.getX(), litTop, track.getWidth(), track.getBottom() - litTop);

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
      pmShape(state, params::oscParamId(params::pmShapeSuffix, index0),
              params::pmShapeSuffix, params::kPitchShapes,
              params::pitchShapeNames()),
      amShape(state, params::oscParamId(params::amShapeSuffix, index0),
              params::amShapeSuffix, params::kAmpShapes,
              params::ampShapeNames()),
      muteButton(state, "M"), soloButton(state, "S"), meter(colour),
      pitchLamp(colour), envLamp(colour), keyOffLamp(colour),
      tremoloLamp(colour), velocityLamp(colour), pressureLamp(colour) {
  // Deep listener, so a pointer resting on a knob is reported by the strip that
  // owns it rather than being swallowed by the control.
  addMouseListener(this, true);

  for (juce::Component *lamp :
       {(juce::Component *)&pitchLamp, (juce::Component *)&envLamp,
        (juce::Component *)&keyOffLamp, (juce::Component *)&tremoloLamp,
        (juce::Component *)&velocityLamp, (juce::Component *)&pressureLamp})
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
  for (auto *shape : {&pmShape, &amShape})
    addAndMakeVisible(*shape);

  pmShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " pitch mod shape");
  amShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " amp mod shape");

  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, colour);
  setUpKnob(pmDepth, Role::PmDepth, colour);
  setUpKnob(phase, Role::Phase, colour);
  setUpKnob(drift, Role::Drift, colour);
  setUpKnob(strike, Role::Strike, colour);

  // The one knob whose own value says nothing useful. Turning it reads as the
  // two times it produces on this strip rather than as a percentage, which is
  // the question anybody dialling it in is actually asking. Read at the moment
  // the popup opens, so it follows the ATTACK and DELAY rows underneath it.
  strike.textFromValueFunction = [this](double amount) {
    return params::strikeRangeText((float)amount, (float)delay.getValue(),
                                   (float)attack.getValue());
  };

  setUpKnob(delay, Role::Delay, colour);
  setUpKnob(attack, Role::Attack, colour);
  setUpKnob(decay, Role::Decay, colour);
  setUpKnob(sustain, Role::Sustain, colour);
  setUpKnob(swell, Role::Swell, colour);
  setUpKnob(offLevel, Role::OffLevel, colour);
  setUpKnob(release, Role::Release, colour);
  setUpKnob(amRate, Role::AmRate, colour);
  setUpKnob(amDepth, Role::AmDepth, colour);
  setUpKnob(velocity, Role::Velocity, colour);
  setUpKnob(aftertouch, Role::Aftertouch, colour);

  setUpKnob(pan, Role::Pan, colour);

  // These all run either side of zero, so their arcs read out from twelve
  // o'clock rather than filling from the left.
  strike.getProperties().set("bipolar", true);
  velocity.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);
  setUpFader(volume, Role::Volume, colour);

  // The letter is what lights, not the face it stands on. See GlowButton.
  muteButton.setColour(juce::TextButton::textColourOnId, colours::muteOn);
  soloButton.setColour(juce::TextButton::textColourOnId, colours::soloOn);
  muteButton.setTooltip("Mute harmonic " + juce::String(info.harmonic));
  soloButton.setTooltip("Solo harmonic " + juce::String(info.harmonic));
  muteButton.setTitle("Harmonic " + juce::String(info.harmonic) + " mute");
  soloButton.setTitle("Harmonic " + juce::String(info.harmonic) + " solo");
  // One moulding around the pair, each half lighting on its own.
  OvertoniumLookAndFeel::gangLamps(muteButton, soloButton);

  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::muteSuffix, index), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::soloSuffix, index), soloButton);

  learn::tag(muteButton, params::oscParamId(params::muteSuffix, index));
  learn::tag(soloButton, params::oscParamId(params::soloSuffix, index));

  // Readouts rather than controls, so they take no clicks of their own and
  // the strip underneath goes on reporting which row the pointer is over.
  for (auto *d : {&tuneReadout, &levelReadout}) {
    d->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*d);
  }

  addAndMakeVisible(meter);

  headerCap.onPaint = [this](juce::Graphics &g) { paintHeaderBand(g); };
  addAndMakeVisible(headerCap);
  meter.toBack(); // the fader cap has to draw over it

  updateTuneReadout();
  updateLevelReadout();

  // An octave is 1200 cents in equal temperament and in just intonation
  // alike, so on six of the thirty-two channels the blend has nowhere to move
  // the partial and TUNE does nothing to the sound. Said outright rather than
  // left as a +0.0 for the reader to draw the conclusion from, because a knob
  // that does nothing reads as a broken one.
  //
  // The knob is still live on those channels and still carries its value, and
  // it has to be: nought means the tempered position, which for an octave is
  // the exact ratio, and Equal Saw sits at nought on all six. A knob greyed
  // out there would also break a LINK drag down the row, which is how a patch
  // like that gets dialled in.
  const auto cents = juce::String(info.jiCents, 1);

  const auto tuning =
      exactly(info.jiCents, 0.0)
          ? juce::String("An octave is the same interval in both, so TUNE has "
                         "nothing to move here. STRETCH is what moves an "
                         "octave partial.")
          : juce::String("Just intonation is ") +
                (info.jiCents >= 0.0 ? "+" : "") + cents + " cents from that";

  setTooltip("Harmonic " + juce::String(info.harmonic) + "  -  " +
             intervalName(info.pitchClass) + "\n" +
             juce::String(info.etSemitones) +
             " semitones above the played note\n" + tuning);
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
  baseColour[(size_t)role] = fill;
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
  baseColour[(size_t)role] = fill;
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

  learn::tag(s, paramId);

  // Double-click restores the parameter's own default rather than the range
  // minimum.
  if (auto *p = apvts.getParameter(paramId))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  s.onUserDragStart = [this, role] { link.linkDragStarted(role, index); };
  s.onUserDragEnd = [this, role] { link.linkDragEnded(role, index); };

  // Only the faders. A fader's value is where it stands, which is what makes a
  // pointer's height mean something; a knob has no such reading, so there is
  // nothing for a drawn drag to say to one.
  if (role == Role::Volume) {
    s.onDrawStart = [this](const juce::MouseEvent &e) {
      return link.drawStarted(e.getScreenPosition());
    };

    s.onDrawMove = [this](const juce::MouseEvent &e) {
      link.drawMovedTo(e.getScreenPosition());
    };

    s.onDrawEnd = [this] { link.drawEnded(); };
  }

  s.onValueChange = [this, &s, role] {
    if (role == Role::Tune)
      updateTuneReadout();
    else if (role == Role::Volume)
      updateLevelReadout();

    if (s.isUserDragging())
      link.linkValueChanged(role, index, (float)s.getValue());
  };
}

void ChannelStrip::drawFaderAt(int y) {
  const auto track = volume.getBounds();

  if (track.getHeight() <= 1)
    return;

  // Top of the track is full, bottom is nothing, and anything past either end
  // is that end: a drawn line that strays above the mixer should leave the
  // faders it passes at the top rather than wrapping or stopping.
  const auto fromTop = juce::jlimit(
      0.0, 1.0, (double)(y - track.getY()) / (double)track.getHeight());

  // Through the slider rather than straight at the parameter, since the
  // slider's range carries the same skew the parameter's does and a
  // proportion of the track is only a value once that has been applied.
  volume.setValue(volume.proportionOfLengthToValue(1.0 - fromTop),
                  juce::sendNotificationSync);
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
    text =
        std::abs(cents) < 0.05 ? juce::String("0.0") : juce::String(cents, 1);
  }

  tuneReadout.setReading(text, active && !silenced);
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
      (db >= 0.0f ? "" : "-") + juce::String(std::abs(db), 1), !silenced);
}

void ChannelStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;

  // The light out of the controls rather than a wash over the whole strip.
  // The wash dimmed the mute along with everything else, and on a silenced
  // channel the mute is the one thing that has to stay readable, since it is
  // usually what silenced it. Greying the controls instead is what the echo
  // and reverb knobs already do when their machine is off, and what the
  // converter readouts do when they are following the host: the value stays
  // where it was set and the light comes out of it.
  std::function<void(juce::Component &)> drain = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // Not the pair. They are the answer to the question the dimming asks.
      if (dynamic_cast<MuteSoloButton *>(child) != nullptr)
        continue;

      // Sliders and the waveform displays, which are the two things on a
      // strip that carry colour of their own. The meters and the activity
      // lamps need no telling: a silenced channel gives them nothing to show.
      if (dynamic_cast<juce::Slider *>(child) != nullptr ||
          dynamic_cast<ShapeButton *>(child) != nullptr) {
        child->getProperties().set("unlit", silenced);
        child->repaint();
      }

      drain(*child);
    }
  };

  drain(*this);

  updateTuneReadout();
  updateLevelReadout();
  repaint();
}

void ChannelStrip::mouseDown(const juce::MouseEvent &e) {
  // The strip listens to every one of its children, so a click that lands on
  // the strip's own background arrives here twice: once because the strip is
  // the component under the pointer, and once more through that listener. A
  // click on a child arrives once, through the listener alone.
  //
  // Nothing on the two events tells them apart. JUCE hands the listener an
  // event rebuilt from the same click, so the component, the position and the
  // modifiers all match. What they cannot differ in is when they happened, so
  // that is what separates them, which is how JUCE itself throws away the
  // duplicate wheel events it sometimes gets. See Slider::mouseWheelMove.
  //
  // Both things this handler does are switches, so both need it. Twice was two
  // LINK menus stacked on each other, and would be a section folded and
  // unfolded again in the same click.
  const bool echoOfTheSameClick = e.eventTime == lastClick;
  lastClick = e.eventTime;

  if (!e.mods.isPopupMenu()) {
    foldSectionUnder(e, echoOfTheSameClick);
    return;
  }

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

  // Two menus opened stacked on each other before the echo was noticed.
  // Choosing an item on the front one left the one behind it standing with its
  // ticks unmoved, so the setting looked to have been refused while the
  // instrument had already taken it.
  if (echoOfTheSameClick)
    return;

  link.showLinkMenu(learn::parameterIdAt(e.originalComponent));
}

/// Folds the section whose rule the click landed on, if it landed on one.
///
/// The rules across a strip line up with the headings in the gutter because
/// both are laid out by the same call, so the same hit test answers for both
/// and a strip needs no geometry of its own. See RowGutter::mouseDown.
void ChannelStrip::foldSectionUnder(const juce::MouseEvent &e, bool echo) {
  // Only a click on the strip's own background. Everything inside a strip that
  // can be clicked has its own job, and the rules are the one part of it with
  // nothing standing on them: the lamps that four of them carry let clicks
  // through precisely so the rule underneath is still a rule.
  if (e.originalComponent != this || echo || onSectionToggled == nullptr)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());

  if (section != Section::NumSections)
    onSectionToggled(section);
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

  // The same hand the gutter's headings show, so a rule that folds looks like
  // one. Only for the strip's own events: a knob sets its own cursor and this
  // handler sees the knob's moves too.
  //
  // Back to the parent's cursor rather than to a plain arrow, which is the
  // part that is easy to get wrong. A strip asks for its parent's cursor so
  // that LINK and the drawing tool can set one across the whole mixer at once,
  // and an arrow set here would mask it for everything below this strip.
  if (e.originalComponent == this) {
    const auto rows = layoutRows(
        getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed, scroll);

    setMouseCursor(headingSectionAt(rows, e.getPosition()) !=
                           Section::NumSections
                       ? juce::MouseCursor::PointingHandCursor
                       : juce::MouseCursor::ParentCursor);
  }
}
void ChannelStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

/// The row the pointer is on, counting the rules between sections as rows.
Row ChannelStrip::rowUnder(const RowBounds &rows, juce::Point<int> p) {
  const auto section = headingSectionAt(rows, p);

  return section != Section::NumSections ? sectionHeading(section)
                                         : controlRowAt(rows, p);
}

void ChannelStrip::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto inside = getLocalBounds().contains(p) && !hoverSuppressed;

  // Leaving one knob for the next fires the exit before the enter, so the
  // answer is always worked out from where the pointer now is rather than from
  // which callback arrived.
  //
  // A rule between two sections answers with its own heading, which no control
  // row does: controlRowAt only speaks for rows that carry something. It is
  // worth the exception because the rule is clickable, and the caption in the
  // gutter lighting is how this panel says what is under the pointer. Without
  // it the one part of a strip you can click was the one part that said
  // nothing.
  hover.hoverChanged(index, inside ? rowUnder(rows, p) : kNoRow);

  // The channel highlight is the strip's own business rather than the
  // editor's, for the same reason: worked out from the pointer, it cannot be
  // left lit by an exit and an enter arriving in the wrong order.
  if (inside != hovered) {
    hovered = inside;
    repaint();
  }
}

void ChannelStrip::paintOverChildren(juce::Graphics &g) {
  // A heading's wash goes over the children rather than behind them, which is
  // the opposite of every other row's. Four of the five carry an activity lamp
  // that fills the whole row and paints an opaque backdrop, so a wash drawn
  // underneath is covered by it and only the output heading, which has no
  // lamp, appeared to highlight at all.
  if (rowShowsHighlight(highlighted) && isHeadingRow(highlighted)) {
    const auto rows = layoutRows(
        getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed, scroll);

    paintRowHighlight(g, rows[rowIndex(highlighted)]);
  }

  if (hovered)
    paintColumnHighlight(g, getLocalBounds());
}

void ChannelStrip::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

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

void ChannelStrip::setScroll(int s) {
  if (s == scroll)
    return;

  scroll = s;
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
  case Role::Strike:
    return &strike;
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

void ChannelStrip::setMacroTint(Role role, juce::Colour tint, float result) {
  const auto at = (size_t)role;

  if (at >= macroTint.size())
    return;

  auto *slider = sliderForRole(role);

  if (slider == nullptr)
    return;

  const bool sameColour = macroTint[at] == tint;
  const bool sameResult = std::abs(macroResult[at] - result) < 1.0e-4f;

  if (sameColour && sameResult)
    return;

  macroTint[at] = tint;
  macroResult[at] = result;

  // Both read by the look and feel, which keeps the control's own colour for
  // the pointer and uses these for the ring. See drawRotarySlider.
  if (tint.isTransparent()) {
    slider->getProperties().remove("macroColour");
    slider->getProperties().remove("macroResult");
  } else {
    slider->getProperties().set("macroColour", (int)tint.getARGB());
    slider->getProperties().set("macroResult", (double)result);
  }

  slider->repaint();
}

void ChannelStrip::setLinkGlow(Role role, float amount, bool accent) {
  amount = juce::jlimit(0.0f, 1.0f, amount);

  if (role == glowRole && accent == glowAccent &&
      std::abs(amount - glowAmount) < 0.004f)
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
  glowAccent = accent;

  if (auto *s = sliderForRole(glowRole)) {
    s->getProperties().set("linkGlow", (double)glowAmount);
    s->getProperties().set("glowAccent", glowAccent);
    s->repaint();
  }
}

void ChannelStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  paintChannelBackground(g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  if (rowShowsHighlight(highlighted) && !isHeadingRow(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

  // Section rules, aligned with the gutter headings. All five carry a lamp
  // now, and a lamp draws the rule either side of itself, so the strip has
  // none of them left to draw. See ActivityLamp::paint.
}

void ChannelStrip::paintHeaderBand(juce::Graphics &g) {
  // The column's own background first, so the cap is opaque and the gradient
  // under the header is the same one the rest of the strip has. Drawn for the
  // whole strip and clipped to the cap, rather than worked out again for a
  // smaller rectangle, so the two cannot come adrift.
  const auto bounds = getLocalBounds();
  paintChannelBackground(g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

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
}

void ChannelStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

  // Hidden rather than left at zero height. A knob with no height still takes
  // the mouse and still answers a hover, so a folded section would go on
  // lighting gutter captions and opening LINK menus for knobs nobody can see.
  const auto placeRow = [&](juce::Component &c, Row r, int shrink) {
    // Hidden rather than left at zero height, and asked of the rectangle
    // rather than of the fold mask, because a row now comes back empty for
    // two reasons: folded away, or scrolled out of the band. Both want the
    // same answer, and a knob with no height still takes the mouse and still
    // answers a hover.
    if (rows[rowIndex(r)].isEmpty()) {
      c.setVisible(false);
      return;
    }

    c.setVisible(true);
    c.setBounds(rows[rowIndex(r)].reduced(shrink));
  };

  placeRow(tune, Row::TuneKnob, 0);
  placeRow(tuneReadout, Row::TuneText, 0);
  placeRow(pmRate, Row::PmRate, 1);
  placeRow(pmShape, Row::PmShape, 0);
  placeRow(pmDepth, Row::PmDepth, 1);
  placeRow(phase, Row::Phase, 1);
  placeRow(drift, Row::Drift, 1);
  placeRow(strike, Row::Strike, 1);
  placeRow(delay, Row::Delay, 1);
  placeRow(attack, Row::Attack, 1);
  placeRow(decay, Row::Decay, 1);
  placeRow(sustain, Row::Sustain, 1);
  placeRow(swell, Row::Swell, 1);
  placeRow(offLevel, Row::OffLevel, 1);
  placeRow(release, Row::Release, 1);
  placeRow(amRate, Row::AmRate, 1);
  placeRow(amShape, Row::AmShape, 0);
  placeRow(amDepth, Row::AmDepth, 1);
  placeRow(velocity, Row::Velocity, 1);
  placeRow(aftertouch, Row::Aftertouch, 1);
  placeRow(pan, Row::Pan, 1);
  // Meter and fader share the same rectangle. The meter draws the track and
  // the fader draws only its cap on top, so the output fills the fader itself.
  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));

  // The strip's background is a gradient down the whole channel, so the meter
  // is told the two colours at its own edges.
  {
    const auto base = colours::channel;
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

  // Over the header and over anything that has scrolled under it. Brought to
  // the front because controls are added after it and would otherwise be in
  // front of the thing meant to hide them.
  // Down to the foot of the header and no further. The row's own rectangle
  // already carries the column's top padding, so adding it again put the cap
  // nine pixels into the band and clipped the tops of the tuning knobs.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[rowIndex(Row::Header)].getBottom()));
  headerCap.toFront(false);

  // Touching, because the two are one moulded block and the moulding between
  // them is drawn by the pair rather than left as a gap. Inset once around
  // the outside instead of once around each.
  auto ms = rows[rowIndex(Row::MuteSolo)].reduced(1);
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2));
  soloButton.setBounds(ms);

  // The lamps stand on the rules that divide the strip into groups, each one
  // at the head of the group it reports on. No row grew to make space for
  // them: the rule was already occupying that height to draw a single line.
  {
    const auto base = colours::channel;
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
    // Two lamps on the one rule, one for each of the rows beneath it. Each
    // takes half the row and draws the rule either side of itself, so the
    // divider still runs the width of the strip with two lamps mounted on it.
    {
      auto row = rows[rowIndex(Row::OutputHeading)];
      const auto half = row.removeFromLeft(row.getWidth() / 2);

      velocityLamp.setBounds(half);
      velocityLamp.setBackdrop(at(half.getCentreY()));

      pressureLamp.setBounds(row);
      pressureLamp.setBackdrop(at(row.getCentreY()));
    }
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
  const auto span = juce::jmax(1.0f, params::kPitchNeedleFullScaleCents);
  const auto reach = juce::jlimit(-1.0f, 1.0f, cents / span);

  return reach < 0.0f ? -std::sqrt(-reach) : std::sqrt(reach);
}

void ChannelStrip::setActivity(float envelope, float tremolo, float pitch,
                               float velGain, float pressure,
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

  refresh(pitchLamp,
          pitchLamp.push(level <= 0.0f ? kParked : needlePosition(pitch)));

  // One lamp for each of the two rows under this heading, each reading its own
  // row the way that row's knob reads.
  //
  // The velocity lamp shows the level the row has left, so it is full when the
  // blow has cost the partial nothing and dark when the row has taken all of
  // it. Full at rest is the right way round for a row that can only subtract:
  // the knob at its centre leaves every note alone, and a lamp that lit as the
  // row did more would be showing the gap rather than the thing.
  //
  // The pressure lamp is the other way because its row is: aftertouch adds
  // nothing until the key is leaned on, so it rests dark and comes up with the
  // hand.
  //
  // Both gated on the envelope like the tremolo, so neither sits lit over a
  // partial that has finished sounding.
  refresh(velocityLamp, velocityLamp.push(level > 0.0f ? velGain : 0.0f));
  refresh(pressureLamp, pressureLamp.push(level > 0.0f ? pressure : 0.0f));
}

void ChannelStrip::mouseWheelMove(const juce::MouseEvent &e,
                                  const juce::MouseWheelDetails &wheel) {
  // Only a wheel that began on the strip itself. One over a knob belongs to
  // the knob, and this is a deep listener so it hears both.
  if (e.originalComponent != this)
    return;

  // A sideways gesture belongs to the mixer rather than to the parameters. A
  // trackpad sends one as deltaX with deltaY near zero, and the vertical
  // distance worked out from deltaY alone came to nothing while the handler
  // still reported the wheel as taken, so the event was swallowed and a
  // two-finger swipe moved nothing at all.
  //
  // Shift goes the same way, which is how a mouse with one wheel asks for it.
  // Both fall through to the viewport, which reads deltaX and shift as a
  // sideways scroll of its own accord.
  const bool sideways = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);

  if (!sideways && !e.mods.isShiftDown() && scrollParametersBy(wheel))
    return;

  juce::Component::mouseWheelMove(e, wheel);
}

bool ChannelStrip::scrollParametersBy(const juce::MouseWheelDetails &wheel) {
  return link.scrollParameters(wheelDistance(wheel));
}

} // namespace ovt::ui
