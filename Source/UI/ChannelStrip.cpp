#include "ChannelStrip.h"
#include "LearnMenu.h"

#include <cmath>

#include "../PluginParameters.h"
#include "../dsp/Velocity.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
/// A wheel notch in pixels, by the same arithmetic a Viewport uses, so
/// scrolling the parameters and the mixer move by the same amount.
int wheelDistance(const juce::MouseWheelDetails &wheel) {
  constexpr float kViewportSingleStep = 16.0f;

  // deltaY as the platform gives it, ignoring isReversed. The platform has
  // already applied natural scrolling, so undoing it would scroll backwards.
  return -juce::roundToInt(wheel.deltaY * 14.0f * kViewportSingleStep);
}
} // namespace

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }
} // namespace

void LinkableSlider::paint(juce::Graphics &g) {
  // Everything the look and feel reads to draw this control: its value,
  // whether it is enabled, its colours, and every property, so a property
  // added later cannot leave a stale picture behind.
  LookKey key;
  key.add(getValue()).add(isEnabled()).add(getProperties());

  for (const auto id :
       {juce::Slider::rotarySliderFillColourId,
        juce::Slider::rotarySliderOutlineColourId, juce::Slider::trackColourId,
        juce::Slider::thumbColourId, juce::Slider::backgroundColourId})
    key.add(findColour(id));

  looks.paint(g, *this, key,
              [this](juce::Graphics &into) { juce::Slider::paint(into); });
}

RelativeKnob::RelativeKnob() {
  setSliderStyle(juce::Slider::RotaryVerticalDrag);
  setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

  // Full travel in either direction covers the whole parameter range.
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
  // The caption is the spoken name too, unless addKnob overrides it. Read
  // from the member, since captionText has been moved from.
  slider.setTitle(caption);

  slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
  addAndMakeVisible(slider);
}

namespace {
// Top to bottom through a labelled knob: room above the dial, the dial, the
// gap, the caption, room under it.
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

void LabelledKnob::setCaption(const juce::String &text) {
  if (text == caption)
    return;

  caption = text;
  repaint(captionBounds(getLocalBounds()));
}

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

  // A minus is the middle bar. It takes a cell like any digit.
  case '-':
    return kSegG;

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
  // A minus and a one share a cell, for a three-digit reading that can be
  // negative. Only the macro panel's amount uses these.
  case '~':
    return kSegG;
  case '!':
    return kSegG | kSegB | kSegC;

  default:
    return 0;
  }
}

/// Whether a character is the decimal point, which every cell has beside it
/// rather than taking a cell of its own.
bool isNarrow(char c) { return c == '.'; }

/// One segment, as an elongated hexagon with mitred ends.
///
/// The ends slope so that neighbouring segments point at each other across
/// the gap between them.
juce::Path segmentShape(juce::Rectangle<float> r, float noseScale = 1.0f) {
  const bool flat = r.getWidth() >= r.getHeight();
  const auto thick = flat ? r.getHeight() : r.getWidth();
  const auto along = flat ? r.getWidth() : r.getHeight();

  // Never more than the bar can give, or a short bar becomes two triangles.
  const auto nose = juce::jmin(thick * 0.5f, along * 0.42f) * noseScale;

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
// For the displays that spell names, which seven bars cannot do.
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

  // The sharp, for a root such as C# in the Tuning bar: the middle crossed by
  // the centre uprights, with the right uprights and the bottom bar.
  case '#':
    return kStB | kStC | kStD | kStG | kStI | kStL;

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

  // The punctuation worth having in a preset name.
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

  // A space is a cell with nothing lit.
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
    // From the right, and never index zero, so the name keeps its first
    // character.
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
  // Upper case and fitted to the cells here, so getReading returns what the
  // cells show. A sharp goes to the sign rather than a cell, so the name is
  // fitted without it.
  const auto sharp = sharpSign && digits.containsChar('#');
  const auto name = sharpSign ? digits.removeCharacters("#") : digits;
  const auto shown =
      (bars == Bars::Fourteen ? squeeze(name, fixedCells) : name) +
      (sharp ? "#" : "");

  if (shown == reading && isActive == active)
    return;

  reading = shown;
  active = isActive;
  repaint();
}

void SegmentDisplay::showSharpSign(bool shouldShow) {
  sharpSign = shouldShow;
  repaint();
}

void SegmentDisplay::mouseUp(const juce::MouseEvent &e) {
  if (onClick && contains(e.getPosition()))
    onClick();
}

void SegmentDisplay::mouseEnter(const juce::MouseEvent &) {
  // Only a display with an onClick answers the pointer.
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

  // Every bar is drawn whether it is lit or not.
  const auto t = juce::jmax(1.0f, area.getHeight() * 0.16f);

  // The gap between segments. Below about a fifth of a bar's thickness the
  // mitred corners close up.
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

    // No bloom, which at this size reads as blur.
    g.setColour(isLit ? on : off);
    g.fillPath(shape);
  }
}

void SegmentDisplay::paintStarburst(juce::Graphics &g,
                                    juce::Rectangle<float> area, char c,
                                    juce::Colour on, juce::Colour off) const {
  const auto lit = starburstFor(c);

  // Thinner than a seven-bar cell's, so the four diagonals do not meet in a
  // blot.
  const auto t = juce::jmax(1.0f, area.getHeight() * 0.095f);
  // A little wider than the seven-bar gap, to keep clear of the diagonals.
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
      // The two halves of the middle, each running from its own end of the cell
      // to its centre.
      {kStG1,
       {t * 0.5f + gap * 0.5f, mid, w * 0.5f - t * 0.5f - gap * 0.8f, t}},
      {kStG2,
       {w * 0.5f + gap * 0.3f, mid, w * 0.5f - t * 0.5f - gap * 0.8f, t}},
      {kStI, {half, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kStL, {half, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
  };

  // The bars inside the frame, the centre uprights and the diagonals, are
  // fainter unlit than the frame is.
  const auto inner = off.withMultipliedAlpha(0.55f);

  for (const auto &bar : straight) {
    const bool isLit = (lit & bar.flag) != 0;
    const bool inside = bar.flag == kStI || bar.flag == kStL;
    // The two halves of the middle are short enough that a full mitre at
    // each end leaves them a point, so they take a short bevel instead.
    const bool middle = bar.flag == kStG1 || bar.flag == kStG2;
    const auto shape = segmentShape(bar.r.translated(area.getX(), area.getY()),
                                    middle ? 0.35f : 1.0f);

    // No bloom, which at this size reads as blur.
    g.setColour(isLit ? on : inside ? inner : off);
    g.fillPath(shape);
  }

  // The four diagonals, from each corner of the cell to its middle. Trimmed
  // along their own direction, since a cell is not square.
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

    g.setColour(isLit ? on : inner);
    g.drawLine(at, t * 0.9f);
  }
}

/// The width the unit takes beside the digits, measured in the width the
/// component is given.
static constexpr float kUnitWidth = 21.0f;
static constexpr int kUnitInsets = 8;
static constexpr int kDigitsNeed = 44;

int SegmentDisplay::naturalWidth(int height) const {
  // The same insets paint takes: a pixel for the recess, then three across
  // and two down for the screen's margin.
  const auto inner = (float)height - 2.0f - 4.0f;
  const auto cellW = inner * (bars == Bars::Fourteen ? 0.74f : 0.72f);
  const auto sharpW = sharpSign ? inner * 0.5f : 0.0f;
  const auto unitW = unitText.isNotEmpty() ? kUnitWidth : 0.0f;

  return (int)std::ceil(cellW * (float)juce::jlimit(1, kMaxCells, fixedCells) +
                        sharpW + unitW + 2.0f + 6.0f);
}

bool SegmentDisplay::hasRoomForUnit(int width) {
  return width - kUnitInsets > kDigitsNeed;
}

std::unique_ptr<juce::AccessibilityHandler>
SegmentDisplay::createAccessibilityHandler() {
  // Asked for lazily, after the bar has set onClick.
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
  LookKey key;
  key.add(reading).add(active).add(hovered).add(sharpSign).add(unitText);

  // How many cells the reading fitted is worked out while drawing, so it is
  // kept beside each picture for cellsDrawn to go on reporting.
  bool drewIt = false;
  const auto used = looks.paint(g, *this, key, [&](juce::Graphics &into) {
    paintReading(into);
    drewIt = true;
  });

  if (drewIt)
    drawnFor[used] = drawn;
  else
    drawn = drawnFor[used];
}

void SegmentDisplay::paintReading(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat().reduced(1.0f);

  // Unlit bars are fainter where there are fourteen to a cell.
  const auto rest = bars == Bars::Fourteen ? 0.55f : 1.0f;
  const auto off = colours::accent.withAlpha((hovered ? 0.20f : 0.12f) * rest);

  // Inactive shows only unlit bars. A segment is lit or it is not.
  const auto lit = active ? colours::accent.withAlpha(0.95f) : off;

  // Set into the panel, using the pixel of margin around the ground.
  paintRecess(g, area, 2.5f);

  // The lit screen ground.
  paintDisplayGround(g, area, 2.5f, hovered);

  if (hovered) {
    g.setColour(colours::outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 2.5f, 1.0f);
  }

  area = area.reduced(3.0f, 2.0f);

  // ---- the cells ----
  //
  // A fixed number of them, all drawn whatever the reading is and laid out
  // from the display's size alone.
  const auto cells = juce::jlimit(1, kMaxCells, fixedCells);

  // The unit is printed beside the cells and takes only the room its words
  // need.
  const auto unitFont = makeFont(7.5f, true);

  auto unitW = 0.0f;

  if (unitText.isNotEmpty() && hasRoomForUnit(getWidth())) {
    juce::GlyphArrangement measure;
    measure.addLineOfText(unitFont, unitText, 0.0f, 0.0f);

    unitW = juce::jmin(kUnitWidth,
                       measure.getBoundingBox(0, -1, true).getWidth() + 2.0f);
  }

  // Wider for fourteen bars, which have more to fit across the cell.
  const auto widest =
      area.getHeight() * (bars == Bars::Fourteen ? 0.74f : 0.72f);
  // The sharp sign stands after the cells, narrower than one.
  const auto sharpW = sharpSign ? area.getHeight() * 0.5f : 0.0f;

  const auto cellW =
      juce::jmin((area.getWidth() - unitW - sharpW) / (float)cells, widest);

  // The figure, and the point in the room to its right.
  const auto glyphW = cellW * 0.78f;
  const auto runW = cellW * (float)cells;
  const auto left = area.getCentreX() - (runW + sharpW + unitW) * 0.5f;

  if (unitW > 0.0f) {
    g.setColour(colours::textDim.withAlpha(0.8f));
    g.setFont(unitFont);
    g.drawText(unitText,
               area.withX(left + runW + sharpW + 2.0f).withWidth(unitW - 2.0f),
               juce::Justification::centredLeft, false);
  }

  if (sharpSign) {
    // Two uprights crossed by two bars that rise to the right, the shape a
    // sharp is printed in, at the weight of a fourteen-bar cell's own bars.
    const juce::Rectangle<float> slot{left + runW, area.getY(), sharpW,
                                      area.getHeight()};
    const auto w = slot.getWidth(), h = slot.getHeight();
    const auto weight = juce::jmax(1.0f, h * 0.095f);

    juce::Path sign;
    for (const auto x : {0.36f, 0.64f})
      sign.addLineSegment({slot.getX() + w * x, slot.getY() + h * 0.12f,
                           slot.getX() + w * x, slot.getY() + h * 0.88f},
                          weight);
    for (const auto y : {0.40f, 0.68f})
      sign.addLineSegment({slot.getX() + w * 0.1f, slot.getY() + h * y,
                           slot.getX() + w * 0.9f,
                           slot.getY() + h * (y - 0.1f)},
                          weight * 1.3f);

    g.setColour(active && reading.containsChar('#') ? lit : off);
    g.fillPath(sign);
  }

  // What each cell shows. A point lights the point of the cell before it,
  // and only starts a cell of its own at the front of a reading.
  struct Cell {
    char c = ' ';
    bool point = false;
  };

  std::array<Cell, kMaxCells> laid{};
  int used = 0;
  bool over = false;

  // Off draws as if there were no reading at all, so nothing it would have
  // lit shows in any shade.
  for (auto ch : active ? reading : juce::String()) {
    const auto c = (char)ch;

    if (sharpSign && c == '#')
      continue;

    if (c == '.' && used > 0 && !laid[(size_t)(used - 1)].point) {
      laid[(size_t)(used - 1)].point = true;
      continue;
    }

    if (used == cells) {
      over = true;
      break;
    }

    laid[(size_t)used++] = c == '.' ? Cell{' ', true} : Cell{c, false};
  }

  // More than the cells can hold shows as over range, a minus in every cell.
  if (over) {
    for (int i = 0; i < cells; ++i)
      laid[(size_t)i] = {'-', false};
    used = cells;
  }

  drawn = over ? 0 : reading.length();

  // A number stands to the right, the way a meter's does, so its units digit
  // keeps its place as it grows. A name starts at the left.
  const auto from = bars == Bars::Seven ? cells - used : 0;

  const auto t = bars == Bars::Fourteen
                     ? juce::jmax(1.0f, area.getHeight() * 0.095f)
                     : juce::jmax(1.0f, area.getHeight() * 0.16f);
  const auto dot = juce::jmin(t * (bars == Bars::Fourteen ? 1.3f : 1.0f),
                              (cellW - glyphW) * 0.9f);

  for (int i = 0; i < cells; ++i) {
    const auto at = i - from;
    const auto cell =
        at >= 0 && at < used ? laid[(size_t)at] : Cell{' ', false};
    const auto x = left + cellW * (float)i;
    const juce::Rectangle<float> glyph{x, area.getY(), glyphW,
                                       area.getHeight()};

    if (bars == Bars::Fourteen)
      paintStarburst(g, glyph, cell.c, lit, off);
    else
      paintGlyph(g, glyph, cell.c, lit, off);

    g.setColour(cell.point ? lit : off);
    g.fillRoundedRectangle(x + glyphW + (cellW - glyphW - dot) * 0.5f,
                           area.getBottom() - dot, dot, dot, dot * 0.35f);
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

  // The rule the lamp is mounted on, drawn either side of it.
  const auto midY = std::floor(bounds.getCentreY());
  const auto diameter = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);
  const auto lamp = juce::Rectangle<float>(diameter, diameter)
                        .withCentre({bounds.getCentreX(), midY + 0.5f});

  const auto leftRun = lamp.getX() - bounds.getX() - 2.0f;
  const auto rightRun = bounds.getRight() - lamp.getRight() - 2.0f;

  g.setColour(colours::outline.withAlpha(0.7f));
  g.fillRect(bounds.getX(), midY, leftRun, 1.0f);
  g.fillRect(lamp.getRight() + 2.0f, midY, rightRun, 1.0f);

  // A lit lip under the rule, so it reads as a groove. Inside the lamp's own
  // bounds, so its invalidated band covers it.
  g.setColour(juce::Colours::white.withAlpha(0.055f));
  g.fillRect(bounds.getX(), midY + 1.0f, leftRun, 1.0f);
  g.fillRect(lamp.getRight() + 2.0f, midY + 1.0f, rightRun, 1.0f);

  // The hole it is mounted in. The rule stops two pixels short either side
  // for its lip.
  paintRecess(g, lamp, diameter * 0.5f);

  // Unlit is the channel colour at low alpha rather than nothing.
  const auto lit = (float)step / (float)kSteps;

  // The same bloom the meters get, only when lit and inside the lamp's own
  // row, so the invalidated band is unchanged.
  if (lit > 0.02f) {
    g.setColour(colour.withAlpha(0.10f * lit));
    g.fillEllipse(lamp.expanded(diameter * 0.42f));

    g.setColour(colour.withAlpha(0.12f * lit));
    g.fillEllipse(lamp.expanded(diameter * 0.18f));
  }

  // Blended against the backdrop rather than drawn at that alpha, so the face
  // is opaque and covers the recess shadow under it.
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
  // Parked rather than centred when there is nothing to show, so an idle
  // partial does not look in tune.
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

void ActivityNeedle::showCentre(bool show) {
  if (show == centreMark)
    return;

  centreMark = show;
  repaint();
}

void ActivityNeedle::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  if (isOpaque()) {
    g.setColour(backdrop);
    g.fillRect(bounds);
  }

  const auto midY = std::floor(bounds.getCentreY());
  const auto height = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);

  // The travel it reads against, in the channel colour held right down. It
  // doubles as the rule dividing the groups.
  const auto track = juce::Rectangle<float>(
                         bounds.getWidth() - 2.0f * (float)kSlotInset, height)
                         .withCentre({bounds.getCentreX(), midY + 0.5f});

  // A slot milled across the strip. The inset leaves room for its lip.
  paintRecess(g, track, height * 0.35f);

  // Blended against the backdrop, so it covers the shadow under the slot.
  g.setColour(isOpaque() ? backdrop.interpolatedWith(colour, 0.16f)
                         : colour.withAlpha(0.16f));
  g.fillRoundedRectangle(track, height * 0.35f);

  // Centre, so sharp and flat mean something when the needle is near it.
  if (centreMark) {
    g.setColour(colour.withAlpha(0.34f));
    g.fillRect(bounds.getCentreX() - 0.5f, track.getY() + 1.0f, 1.0f,
               track.getHeight() - 2.0f);
  }

  if (column < 0)
    return;

  g.setColour(colour);
  g.fillRect((float)column - 1.0f, track.getY(), 2.0f, track.getHeight());
}

// =============================================================================

void LevelMeter::setBackdrop(juce::Colour top, juce::Colour bottom) {
  backdropTop = top;
  backdropBottom = bottom;

  // An opaque child is left out of what its parent has to paint.
  setOpaque(true);
  repaint();
}

int LevelMeter::segments() const { return meterSegments(getHeight()); }

namespace {
/// How far the bloom around a lit meter run reaches beyond it, in pixels.
///
/// The paint and the dirty band push() returns must agree on it, or the old
/// level leaves a smear.
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
  // has fallen clear of it, so a level on a boundary does not flicker.
  constexpr float kClearOf = 0.3f;

  while (after < count && exact >= (float)after + 1.0f)
    ++after;

  while (after > 0 && exact < (float)after - kClearOf)
    --after;

  lit = after;

  // Most frames change no lamp and cost nothing.
  if (before == after)
    return {};

  // Only the lamps that lit or went out. Not repainted here: the editor
  // merges these from every meter and invalidates once.
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
  // The slice of channel background this meter stands on, painted here so
  // the component can be opaque.
  if (isOpaque()) {
    g.setGradientFill(juce::ColourGradient(backdropTop, 0.0f, 0.0f,
                                           backdropBottom, 0.0f,
                                           (float)getHeight(), false));
    g.fillRect(getLocalBounds());
  }

  // The meter sits behind the fader and owns its whole track.
  const auto full = getLocalBounds().toFloat();
  const auto trackW = juce::jmax(6.0f, full.getWidth() * 0.62f);
  const auto track = full.withSizeKeepingCentre(trackW, full.getHeight());

  g.setColour(colours::groove);
  g.fillRoundedRectangle(track, trackW * 0.35f);

  // Recessed into the channel.
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

  // Bloom around the lit run, drawn once for the whole run and kept inside the
  // component's bounds so the invalidated band need not grow.
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
  // The background fills the whole strip, so nothing behind it needs
  // painting.
  setOpaque(true);

  // Deep listener, so a pointer resting on a knob is reported by the strip that
  // owns it rather than being swallowed by the control.
  addMouseListener(this, true);

  for (juce::Component *lamp :
       {(juce::Component *)&pitchLamp, (juce::Component *)&envLamp,
        (juce::Component *)&keyOffLamp, (juce::Component *)&tremoloLamp,
        (juce::Component *)&velocityLamp, (juce::Component *)&pressureLamp})
    addAndMakeVisible(*lamp);

  // The strip decides the pointer for everything on it, which is how the LINK
  // tool shows itself. The mute and solo buttons keep their own.
  setMouseCursor(juce::MouseCursor::ParentCursor);

  // Every knob carries the strip's colour at full strength.
  for (auto *shape : {&pmShape, &amShape})
    addAndMakeVisible(*shape);

  pmShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " pitch mod shape");
  amShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " amp mod shape");

  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, colour);
  setUpKnob(pmDepth, Role::PmDepth, colour);
  setUpKnob(drift, Role::Drift, colour);
  setUpKnob(glide, Role::Glide, colour);
  setUpKnob(strike, Role::Strike, colour);

  // STRIKE's popup shows the two times it produces on this strip rather than
  // a percentage. Read when the popup opens, so it follows ATTACK and DELAY.
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

  // The colour of the lamp behind the cap.
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

  // An octave is the same in equal temperament and just intonation, so on six
  // channels TUNE does nothing to the sound, and the readout says so. The knob
  // stays live, since nought is still a value and LINK drags cross it.
  const auto cents = juce::String(info.jiCents, 1);

  const auto tuning =
      exactly(info.jiCents, 0.0)
          ? juce::String("An octave is the same in both, so TUNE has nothing "
                         "to move here. STRETCH moves it.")
          : juce::String("Just intonation is ") +
                (info.jiCents >= 0.0 ? "+" : "") + cents + " cents from that";

  setTooltip("Harmonic " + juce::String(info.harmonic) + ", " +
             intervalName(info.pitchClass) + "\n" +
             juce::String(info.etSemitones) +
             " semitones above the played note\n" + tuning);
}

void ChannelStrip::setUpKnob(LinkableSlider &s, Role role, juce::Colour fill) {
  // The name a screen reader announces.
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

  // Where the attack stops being a time and becomes a start phase, as a share
  // of the knob's travel, read off the parameter's own range. See
  // kShortestAttack.
  if (role == Role::Attack)
    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            apvts.getParameter(paramId)))
      s.getProperties().set(
          "phaseSplit",
          (double)p->getNormalisableRange().convertTo0to1(kShortestAttack));

  s.onUserDragStart = [this, role] { link.linkDragStarted(role, index); };
  s.onUserDragEnd = [this, role] { link.linkDragEnded(role, index); };

  // Only the faders take a drawn drag.
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
  // is that end.
  const auto fromTop = juce::jlimit(
      0.0, 1.0, (double)(y - track.getY()) / (double)track.getHeight());

  // Through the slider, whose range carries the parameter's skew.
  volume.setValue(volume.proportionOfLengthToValue(1.0 - fromTop),
                  juce::sendNotificationSync);
}

void ChannelStrip::updateTuneReadout() {
  const auto blend = tune.getValue();
  const auto cents = blend * info.jiCents;

  juce::String text;

  if (std::abs(info.jiCents) < 0.05) {
    // The octaves land in the same place either way.
    text = "0.0";
  } else if (blend <= 0.0005) {
    text = "Et";
  } else {
    // Only the minus is shown, since seven bars cannot draw a readable plus. A
    // sharp partial is the one with no sign.
    text =
        std::abs(cents) < 0.05 ? juce::String("0.0") : juce::String(cents, 1);
  }

  // A channel silenced by another's solo has nothing sounding to report, so
  // its cells go dark.
  tuneReadout.setReading(text, !silenced);
}

void ChannelStrip::updateLevelReadout() {
  const auto v = (float)volume.getValue();

  // A fader all the way down reads -inF. Everything above counts down to
  // params::kQuietestLevelDb, which is as far as the cells reach.
  if (params::isSilentGain(v))
    return levelReadout.setReading("-inF", !silenced);

  const auto db = params::levelDecibels(v);

  levelReadout.setReading(
      (db >= 0.0f ? "" : "-") + juce::String(std::abs(db), 1), !silenced);
}

void ChannelStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;

  // Turns the controls' lights off rather than dimming the whole strip, so
  // the mute stays readable.
  std::function<void(juce::Component &)> drain = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // Not the mute and solo pair.
      if (dynamic_cast<MuteSoloButton *>(child) != nullptr)
        continue;

      // Sliders and the waveform displays carry colour of their own. The meters
      // and the lamps get nothing to show on a silenced channel.
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
  // The strip listens to its children, so a click on its own background
  // arrives twice, with everything the same but nothing to tell them apart
  // except the event time. Both actions below are toggles, so the echo is
  // dropped.
  const bool echoOfTheSameClick = e.eventTime == lastClick;
  lastClick = e.eventTime;

  // The command key on the channel's number picks the channel, for the
  // Selected scope. Only on the number, since on a knob that key asks for fine
  // movement.
  if (!e.mods.isPopupMenu() && e.mods.isCommandDown() &&
      headerCap.getBounds().contains(
          e.getEventRelativeTo(this).getPosition())) {
    if (!echoOfTheSameClick) {
      selecting = true;
      link.selectionStarted(index, e.getScreenPosition());
    }

    return;
  }

  if (!e.mods.isPopupMenu()) {
    foldSectionUnder(e, echoOfTheSameClick);
    return;
  }

  // The menu about to open is modal and this strip is never told the pointer
  // left, so the hover is let go now.
  clearHover();

  // A right-click on a mute or solo button opens that button's own menu
  // only. originalComponent still names where the click landed after any
  // retargeting.
  if (dynamic_cast<const MuteSoloButton *>(e.originalComponent) != nullptr)
    return;

  // One menu per click, not two stacked.
  if (echoOfTheSameClick)
    return;

  link.showLinkMenu(learn::parameterIdAt(e.originalComponent));
}

/// Folds the section whose rule the click landed on, if it landed on one.
///
/// Uses the same hit test as the gutter headings. See RowGutter::mouseDown.
void ChannelStrip::foldSectionUnder(const juce::MouseEvent &e, bool echo) {
  // Only a click on the strip's own background. The lamps on the rules let
  // clicks through.
  if (e.originalComponent != this || echo || onSectionToggled == nullptr)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());

  if (section != Section::NumSections)
    onSectionToggled(section);
}

void ChannelStrip::clearHover() {
  // Held until the pointer moves again. The exit that comes as a menu opens
  // carries a position inside this strip and would light it back up.
  hoverSuppressed = true;

  hover.hoverChanged(index, kNoRow);

  if (hovered) {
    hovered = false;
    repaint();
  }
}

// Only a pointer move ends a suppression, not an exit. See clearHover.
void ChannelStrip::mouseEnter(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);
}

void ChannelStrip::mouseMove(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);

  // The same hand the gutter's headings show, for the strip's own events
  // only. Back to the parent's cursor rather than an arrow, so LINK and the
  // drawing tool can still set one across the whole mixer.
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

  // Worked out from where the pointer is now, since leaving one knob for the
  // next fires the exit before the enter. A rule between sections answers
  // with its own heading, since it is clickable.
  hover.hoverChanged(index, inside ? rowUnder(rows, p) : kNoRow);

  // The channel highlight, worked out from the pointer for the same reason.
  if (inside != hovered) {
    hovered = inside;
    repaint();
  }
}

void ChannelStrip::paintOverChildren(juce::Graphics &g) {
  // A heading's wash goes over the children, since the activity lamps on
  // those rows paint opaque backdrops.
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

  // Only the two bands that changed are repainted, not the whole channel.
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
  case Role::Drift:
    return &drift;
  case Role::Glide:
    return &glide;
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

  // Read by the look and feel for the ring. See drawRotarySlider.
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

  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  if (rowShowsHighlight(highlighted) && !isHeadingRow(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

  // The section rules are drawn by the lamps on them. See ActivityLamp::paint.
}

void ChannelStrip::paintHeaderBand(juce::Graphics &g) {
  // The column's own background first, so the cap is opaque. Drawn for the
  // whole strip and clipped to the cap, so it matches the strip exactly.
  const auto bounds = getLocalBounds();
  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colour);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  // Accent when the pointer is on this channel, and mixed towards the accent
  // by how much of a LINK drag the channel would take.
  g.setColour(hovered
                  ? colours::accent
                  : colours::text.interpolatedWith(colours::accent, linkReach));
  g.setFont(makeFont(14.0f, true));
  g.drawText(juce::String(info.harmonic), header, juce::Justification::centred,
             false);

  // A bar under the number for a selected channel.
  if (selected) {
    g.setColour(colours::accent);
    g.fillRect(juce::Rectangle<int>(14, 2).withCentre(
        {header.getCentreX(), header.getBottom() - 2}));
  }
}

void ChannelStrip::mouseDrag(const juce::MouseEvent &e) {
  if (selecting)
    link.selectionMovedTo(e.getScreenPosition());
}

void ChannelStrip::mouseUp(const juce::MouseEvent &) {
  if (!selecting)
    return;

  selecting = false;
  link.selectionEnded();
}

void ChannelStrip::setSelected(bool on) {
  if (selected == on)
    return;

  selected = on;
  headerCap.repaint();
}

void ChannelStrip::setLinkReach(float amount) {
  // Quantised to eight steps, so a sliding curve does not repaint every
  // header every frame.
  const auto step = std::round(juce::jlimit(0.0f, 1.0f, amount) * 8.0f) / 8.0f;

  if (std::abs(step - linkReach) < 0.001f)
    return;

  linkReach = step;

  // The header only, which the cap draws.
  headerCap.repaint();
}

void ChannelStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

  const auto placeRow = [&](juce::Component &c, Row r, int shrink) {
    // Hidden rather than left at zero height, since a knob with no height
    // still takes the mouse. A row is empty when folded or scrolled out.
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
  placeRow(drift, Row::Drift, 1);
  placeRow(glide, Row::Glide, 1);
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

  // The strip's background is a gradient, so the meter is told the colours
  // at its own edges.
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

  // Over the header and anything scrolled under it, brought to the front
  // since controls are added after it. Down to the foot of the header only,
  // since the row's rectangle already carries the top padding.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[rowIndex(Row::Header)].getBottom()));
  headerCap.toFront(false);

  // Touching, as one ganged block, inset once around the outside.
  auto ms = rows[rowIndex(Row::MuteSolo)].reduced(1);
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2));
  soloButton.setBounds(ms);

  // The lamps stand on the rules that divide the strip into groups, each at
  // the head of the group it reports on.
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
    // Two lamps on the one rule, one for each row beneath it, each taking half.
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
  // A square root on a fixed scale, so a shallow setting of a few cents
  // still moves the needle a visible distance and two channels can be
  // compared by eye.
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

  // One lamp or the other by the sign. The other gets zero.
  const auto level = std::abs(envelope);
  const auto afterKeyOff = envelope < 0.0f;

  refresh(envLamp, envLamp.push(afterKeyOff ? 0.0f : level));
  refresh(keyOffLamp, keyOffLamp.push(afterKeyOff ? level : 0.0f));

  // The tremolo lamp is gated on the envelope, so it stops when the note does
  // rather than going on pulsing over silence.
  refresh(tremoloLamp, tremoloLamp.push(level > 0.0f ? tremolo : 0.0f));

  // Parked only when nothing is sounding. kParked is anything past the ends
  // of the travel.
  constexpr float kParked = -3.0f;

  refresh(pitchLamp,
          pitchLamp.push(level <= 0.0f ? kParked : needlePosition(pitch)));

  // One lamp for each of the two rows under this heading. The velocity lamp
  // shows the level the row leaves, so it is full at rest. The pressure lamp
  // rests dark and rises with aftertouch. Both are gated on the envelope.
  refresh(velocityLamp, velocityLamp.push(level > 0.0f ? velGain : 0.0f));
  refresh(pressureLamp, pressureLamp.push(level > 0.0f ? pressure : 0.0f));
}

void ChannelStrip::mouseWheelMove(const juce::MouseEvent &e,
                                  const juce::MouseWheelDetails &wheel) {
  // Only a wheel that began on the strip itself. One over a knob belongs to
  // the knob, and this is a deep listener so it hears both.
  if (e.originalComponent != this)
    return;

  // A sideways gesture, or shift with a wheel, falls through to the viewport
  // to scroll the mixer.
  const bool sideways = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);

  if (!sideways && !e.mods.isShiftDown() && scrollParametersBy(wheel))
    return;

  juce::Component::mouseWheelMove(e, wheel);
}

bool ChannelStrip::scrollParametersBy(const juce::MouseWheelDetails &wheel) {
  return link.scrollParameters(wheelDistance(wheel));
}

} // namespace ovt::ui
