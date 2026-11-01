#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MuteSoloButton.h"
#include "PaintCache.h"
#include "ShapeButton.h"
#include "Theme.h"

namespace ovt::ui {

/// A Slider that knows whether the user is holding it.
///
/// A change from a drag is broadcast to the other strips by LINK, while one
/// from automation, a preset load or the broadcast itself is not, so there
/// is no feedback loop.
class LinkableSlider : public juce::Slider {
public:
  /// A right-click is the strip's, for the LINK menu. The slider would start a
  /// drag it never ends, since the mouse-up goes to the menu.
  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isPopupMenu())
      return;

    // A drawn drag never becomes a slider drag, or this fader would move twice.
    if (onDrawStart != nullptr && onDrawStart(e)) {
      drawing = true;
      return;
    }

    juce::Slider::mouseDown(e);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.mods.isPopupMenu())
      return;

    if (drawing) {
      if (onDrawMove != nullptr)
        onDrawMove(e);

      return;
    }

    juce::Slider::mouseDrag(e);
  }

  void mouseUp(const juce::MouseEvent &e) override {
    if (drawing) {
      drawing = false;

      if (onDrawEnd != nullptr)
        onDrawEnd();

      return;
    }

    juce::Slider::mouseUp(e);
  }

  /// Offered every drag before the slider takes it. Returning true means the
  /// gesture belongs to something else, which then gets the pointer until the
  /// button comes up. Only the faders are given these.
  std::function<bool(const juce::MouseEvent &)> onDrawStart;
  std::function<void(const juce::MouseEvent &)> onDrawMove;
  std::function<void()> onDrawEnd;

  void startedDragging() override {
    dragging = true;
    if (onUserDragStart != nullptr)
      onUserDragStart();
  }

  void stoppedDragging() override {
    dragging = false;
    if (onUserDragEnd != nullptr)
      onUserDragEnd();
  }

  bool isUserDragging() const noexcept { return dragging; }

  /// Whether this drag was handed to the drawing rather than moving the
  /// slider. See onDrawStart.
  bool isDrawing() const noexcept { return drawing; }

  std::function<void()> onUserDragStart, onUserDragEnd;

  /// Drawn from a cached picture when nothing that decides its look has
  /// changed. See paint for what counts as a change.
  void paint(juce::Graphics &) override;

private:
  bool dragging = false;
  bool drawing = false;

  PaintCache looks;
};

/// An endless, relative control.
///
/// It reports how far it has been turned since the drag began and springs
/// back to centre on release, so the strips it drives keep their spread.
class RelativeKnob : public juce::Slider {
public:
  RelativeKnob();

  void startedDragging() override;
  void stoppedDragging() override;

  /// Offset since the drag began, in normalised parameter units.
  std::function<void(float delta)> onRelativeDelta;
  std::function<void()> onRelativeStart, onRelativeEnd;
};

/// A knob with a caption underneath.
///
/// For the controls that stand on their own, such as the master section and
/// the effects. The strips get their captions from the gutter.
class LabelledKnob : public juce::Component {
public:
  explicit LabelledKnob(juce::String captionText,
                        juce::Colour fill = colours::accent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Where the dial sits inside bounds of this size.
  ///
  /// Public so a panel can line buttons up with the dials rather than with the
  /// middle of the row.
  static juce::Rectangle<int> dialBounds(juce::Rectangle<int>);

  /// Where the caption under the dial sits, so a group name can share its line.
  static juce::Rectangle<int> captionBounds(juce::Rectangle<int>);

  /// Changes the caption only, leaving the accessible name to the owner.
  void setCaption(const juce::String &);
  const juce::String &getCaption() const noexcept { return caption; }

  LinkableSlider slider;

private:
  juce::String caption;
};

/// A small segment readout with a fixed number of cells.
///
/// Used for the converter settings on the bar and for the cents and level
/// figures under each channel. With an onClick it becomes a control that
/// opens a menu and shows a hover outline.
class SegmentDisplay : public juce::Component,
                       public juce::SettableTooltipClient {
public:
  /// How many bars a cell has: seven for digits and a few letters, fourteen
  /// for the whole alphabet.
  enum class Bars { Seven, Fourteen };

  /// The most cells a display can have.
  static constexpr int kMaxCells = 16;

  /// @param unit   drawn small beside the digits, or empty for none.
  /// @param cells  how many cells the display has. Fixed, so the figures are
  ///               the same size whatever they say.
  explicit SegmentDisplay(juce::String unit, Bars bars = Bars::Seven,
                          int cells = 4);

  /// @param digits  0 to 9, a minus, which takes a cell, a decimal point,
  ///                 which lights the point of the cell before it, and the
  ///                 letters in segmentsFor. A fourteen-bar display also takes
  ///                 the whole alphabet, upper-cased. Anything else is blank.
  /// @param active  false leaves every bar unlit.
  void setReading(const juce::String &digits, bool active);

  /// Gives the display a fixed sharp sign beside its cells, lit when the
  /// reading has a # in it.
  void showSharpSign(bool shouldShow);

  /// How wide this display has to be at @p height for every cell to stand at
  /// full width, with room for the sharp sign and the unit if it has them.
  int naturalWidth(int height) const;

  /// How many bars its cells have. Read back by the tests.
  Bars howManyBars() const noexcept { return bars; }

  /// Fits a name to a fixed number of cells.
  ///
  /// Upper case, then spaces go from the right, then vowels (not Y), then the
  /// rest is cut. The first character is always kept. Shorter names come back
  /// padded to exactly @p cells characters.
  static juce::String squeeze(const juce::String &, int cells);

  /// How many of the reading's characters the last paint had room for, so
  /// the tests can see a dropped letter.
  int cellsDrawn() const noexcept { return drawn; }

  /// What it is showing, and whether it is lit. Read back by the tests.
  const juce::String &getReading() const noexcept { return reading; }
  bool isActive() const noexcept { return active; }

  /// Whether a display this wide has room to name its unit beside the digits.
  ///
  static bool hasRoomForUnit(int width);

  /// Whether this character has a form to draw. Anything else comes out as an
  /// unlit digit.
  static bool canDraw(char, Bars = Bars::Seven);

  std::function<void()> onClick;

  void paint(juce::Graphics &) override;
  void mouseUp(const juce::MouseEvent &) override;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

  /// Announces a display with an onClick as a button, so a screen reader can
  /// name and press it. A plain readout gets no handler of its own.
  std::unique_ptr<juce::AccessibilityHandler>
  createAccessibilityHandler() override;

private:
  /// Everything paint draws, before it is kept as a picture.
  void paintReading(juce::Graphics &);

  /// Draws one character in the classic seven-bar arrangement.
  void paintGlyph(juce::Graphics &, juce::Rectangle<float>, char,
                  juce::Colour on, juce::Colour off) const;

  /// The same, in fourteen.
  void paintStarburst(juce::Graphics &, juce::Rectangle<float>, char,
                      juce::Colour on, juce::Colour off) const;

  const Bars bars;
  const int fixedCells;

  /// Set by paint, read by cellsDrawn.
  mutable int drawn = 0;

  /// Its last two looks, and what cellsDrawn was for each.
  PaintCache looks;
  std::array<int, 2> drawnFor{};

  juce::String reading, unitText;
  bool active = false;
  bool hovered = false;
  bool sharpSign = false;
};

/// A slim output meter for one partial.
///
/// Segmented, so it only repaints when the level crosses a segment boundary.
/// Unlit segments keep the channel colour at low alpha.
class LevelMeter : public juce::Component {
public:
  explicit LevelMeter(juce::Colour barColour) : colour(barColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// The channel background behind this meter, at its top and bottom edges.
  ///
  /// Given these, the meter paints its own backdrop and declares itself
  /// opaque, so the strip underneath is not redrawn when a lamp changes.
  void setBackdrop(juce::Colour top, juce::Colour bottom);

  /// @param level  linear amplitude from the audio thread, 0 to 1.
  /// @returns the region it asked to have repainted, empty when nothing moved.
  ///
  juce::Rectangle<int> push(float level);

  void paint(juce::Graphics &) override;

private:
  int segments() const;

  juce::Colour colour;
  juce::Colour backdropTop, backdropBottom;
  float displayed = 0.0f;

  /// How many lamps are lit. Kept rather than derived so it can hold still
  /// while the level wobbles across a boundary.
  int lit = 0;
};

/// A lamp mounted on one of the rules that divide a strip into groups.
///
/// It shows how hard the group below is working on this partial, such as the
/// envelope's position or how far the tremolo has pulled the level down.
/// Brightness is quantised so it only repaints when it moves a step.
class ActivityLamp : public juce::Component {
public:
  explicit ActivityLamp(juce::Colour litColour) : colour(litColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// The channel background behind the lamp, so it can paint its own backdrop
  /// and declare itself opaque. See LevelMeter::setBackdrop.
  void setBackdrop(juce::Colour);

  /// @param brightness  0 to 1, how hard the group is working.
  /// @returns true when the lamp moved a step and needs repainting.
  bool push(float brightness);

  void paint(juce::Graphics &) override;

  /// How many steps the brightness is rounded to. Enough to read as
  /// continuous, few enough that a slow move is mostly free.
  static constexpr int kSteps = 12;

private:
  juce::Colour colour, backdrop;
  int step = 0;
};

/// A needle on a rule, showing where pitch modulation, drift and a glide still
/// on its way have this partial.
///
/// Centre is the note as written, right is sharp, left is flat, on a fixed
/// scale that is the same on every strip.
class ActivityNeedle : public juce::Component {
public:
  explicit ActivityNeedle(juce::Colour needleColour) : colour(needleColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// See LevelMeter::setBackdrop.
  void setBackdrop(juce::Colour);

  /// The margin the slot leaves itself inside the component, each side.
  ///
  /// One pixel for the recess lip, which is drawn outside the opening. The
  /// travel is measured off the same number.
  static constexpr int kSlotInset = 1;

  /// @param position  -1 to 1, flat to sharp, already scaled by the caller.
  /// @returns true when the needle moved a pixel and needs repainting.
  bool push(float position);

  /// Whether the travel is marked at its middle. Off for a reading that rests
  /// at the left end.
  void showCentre(bool);

  /// Where the needle sits, in pixels from the left edge, or -1 for parked.
  /// Read back by the tests.
  int getColumn() const noexcept { return column; }

  void paint(juce::Graphics &) override;

private:
  /// Where the needle sits, in pixels from the left edge, or -1 for parked.
  /// Quantised to the pixel, so it only repaints when it moves a pixel.
  int column = -1;
  bool centreMark = true;

  juce::Colour colour, backdrop;
};

/// One vertical channel: everything that belongs to a single partial.
class ChannelStrip : public juce::Component,
                     public juce::SettableTooltipClient {
public:
  ChannelStrip(juce::AudioProcessorValueTreeState &state,
               LinkTarget &linkTarget, HoverTarget &hoverTarget,
               juce::Component &popupParent, int index0);

  void paint(juce::Graphics &) override;

  /// The channel highlight, drawn over everything rather than behind it.
  ///
  /// Over the children because the meter and the lamps paint opaque
  /// backgrounds that would hide it.
  void paintOverChildren(juce::Graphics &) override;

  void resized() override;

  /// Which groups of rows are folded away. Set by the editor on every strip at
  /// once, since the rows are shared across the whole mixer.
  void setCollapsedSections(SectionMask);

  /// How far the parameters are scrolled, shared by every column.
  ///
  /// The gutter and every strip get the same number, so captions line up with
  /// their knobs. Snapped to a row boundary inside layoutRows.
  void setScroll(int);

  /// Asked for when a click lands on one of the rules between sections, which
  /// line up with the gutter's headings and do the same thing.
  std::function<void(Section)> onSectionToggled;

  /// Sets this channel's fader from a height, for a drag that is drawing
  /// across the mixer rather than moving one fader.
  ///
  /// @p y is in this strip's own coordinates. Read through the slider, whose
  /// range is skewed to match the parameter's.
  void drawFaderAt(int y);

  /// The row the pointer is on, with the rules between sections counted as
  /// rows of their own. See reportHover.
  static Row rowUnder(const RowBounds &, juce::Point<int>);

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

  /// Asks the editor to scroll, and says whether it had anywhere to go.
  bool scrollParametersBy(const juce::MouseWheelDetails &);

  /// Passes on only a wheel that landed on the strip itself, so a wheel a
  /// control has taken does not scroll the mixer as well.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  /// Turns the strip's lights and screens off when another strip's solo is
  /// silencing it. Its mute and solo stay lit.
  void setSilencedByOthers(bool shouldDim);

  /// @returns the region of this strip that needs redrawing, empty if the
  /// meter did not move. The editor merges these and invalidates once.
  juce::Rectangle<int> setMeterLevel(float level) {
    const auto band = meter.push(level);
    return band.isEmpty() ? band : band.translated(meter.getX(), meter.getY());
  }

  /// What the lamps on the section rules show.
  ///
  /// @param envelope  signed: positive while the key is down, negative once
  ///                  the key-off stage has it.
  /// @param tremolo   how far the tremolo has pulled the level down, 0 to 1.
  /// @param pitch     displacement in cents, or the parked value when the
  ///                  partial is silent.
  ///
  /// Appends the bounds of every lamp that moved, in this strip's own
  /// coordinates, for the editor to merge with the meter bands. Too many
  /// separate invalidations make the window redraw their whole bounding box.
  void setActivity(float envelope, float tremolo, float pitch, float velGain,
                   float pressure, juce::Array<juce::Rectangle<int>> &into);

  /// Where a displacement sits on the needle's travel, -1 to 1.
  ///
  /// Full deflection is always params::kPitchNeedleFullScaleCents.
  static float needlePosition(float cents);

  /// Whether the pointer is on this channel, which is what lights the column.
  bool isHovered() const noexcept { return hovered; }

  /// Picks out one row, or kNoRow to clear. Every strip is told the same row,
  /// so the highlight runs the width of the mixer.
  void setHighlightedRow(Row);

  /// Arms the control LINK would move on this strip.
  ///
  /// @param amount  0 for a strip the drag does not reach, otherwise how much
  ///                of the drag it takes relative to the strip that takes most.
  void setLinkGlow(Role, float amount, bool accent = false);

  /// How much of a LINK drag this channel would take, 0 to 1, for the number
  /// at the head of the strip.
  ///
  void setLinkReach(float amount);

  /// Whether this channel is in the selection the Selected scope reaches.
  /// Marked under the number, apart from the LINK reach, which lights the
  /// number itself.
  void setSelected(bool);
  bool isSelected() const { return selected; }

  /// Which macro drives this row, as its colour, or transparent for none.
  ///
  /// The ring goes to the macro's colour while the pointer keeps the
  /// channel's.
  ///
  /// @param result  where the row ends up once the macro has had its say, as
  ///                a proportion of the control's travel.
  void setMacroTint(Role, juce::Colour, float result);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(LinkableSlider &, Role, juce::Colour fill);
  void setUpFader(LinkableSlider &, Role, juce::Colour fill);
  void wireUp(LinkableSlider &, Role);

  void updateTuneReadout();
  void updateLevelReadout();

  /// Tells the editor which row the pointer is on. Called for the strip's own
  /// mouse events and for those of every control on it.
  void reportHover(const juce::MouseEvent &);

  /// Lets go of the hover without a mouse event to say so, for when a modal
  /// menu is about to take the pointer away.
  void clearHover();

  LinkableSlider *sliderForRole(Role);

  /// The colour each control wears when no macro has it, kept so that one
  /// letting go puts the channel's own back rather than an approximation.
  std::array<juce::Colour, (size_t)kNumRoles> baseColour{};

  /// Which macro colour each row is wearing now, so a repaint only happens
  /// when one actually changes.
  std::array<juce::Colour, (size_t)kNumRoles> macroTint{};

  /// And where the macro has taken that row, for the same reason.
  std::array<float, (size_t)kNumRoles> macroResult{};

  juce::AudioProcessorValueTreeState &apvts;
  LinkTarget &link;
  HoverTarget &hover;
  juce::Component &popupHost;

  const int index;
  const HarmonicInfo info;
  const juce::Colour colour;

  LinkableSlider tune, glide, pmRate, pmDepth, drift, strike, delay, attack,
      decay, sustain, swell, offLevel, release, amRate, amDepth, velocity,
      aftertouch, pan, volume;
  /// What each modulator traces.
  ShapeButton pmShape, amShape;

  MuteSoloButton muteButton, soloButton;
  /// No unit on either, since the gutter caption beside them says it.
  SegmentDisplay tuneReadout{{}}, levelReadout{{}};
  LevelMeter meter;

  ActivityNeedle pitchLamp;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp, velocityLamp, pressureLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;

  Row highlighted = kNoRow;

  /// Folded groups, set by the editor. Every layout and hit test here follows
  /// it.
  SectionMask collapsed = 0;
  int scroll = 0;

  /// What hides a row that has scrolled under the pinned header. See HeaderCap.
  HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);
  bool hovered = false;

  /// Set when a menu takes the pointer away, and cleared when the pointer
  /// moves under its own steam again. See clearHover.
  bool hoverSuppressed = false;

  /// When the last click on this strip happened, so the same click arriving a
  /// second time cannot act twice. See mouseDown.
  juce::Time lastClick;

  void foldSectionUnder(const juce::MouseEvent &, bool echo);
  Role glowRole = Role::Tune;
  float glowAmount = 0.0f;

  /// See setLinkReach. Quantised before it is acted on, so a curve sliding
  /// under the pointer does not repaint thirty-two headers every frame.
  float linkReach = 0.0f;

  /// See setSelected, and whether a selection drag began on this strip.
  bool selected = false;
  bool selecting = false;

  /// Whether the glow is lit in the accent, for drawing, rather than in the
  /// channel's own colour, for LINK.
  bool glowAccent = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
