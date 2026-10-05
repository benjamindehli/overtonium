#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MuteSoloButton.h"
#include "ShapeButton.h"
#include "Theme.h"

namespace ovt::ui {

/// A Slider that knows whether the human is currently holding it.
///
/// That distinction is what makes the LINK switch safe: a value change caused
/// by a drag gets broadcast to the other 31 strips, while one caused by
/// automation, a preset load or the broadcast itself does not, so there is no
/// feedback loop.
class LinkableSlider : public juce::Slider {
public:
  /// A right-click is the strip's, not the slider's: it opens the LINK menu.
  /// Left to itself the slider would open a drag gesture it never closes,
  /// since the mouse-up goes to the menu rather than back here.
  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isPopupMenu())
      return;

    // A drag that is going to be a drawn one never becomes a slider drag at
    // all. Letting the slider take it and handing the pointer on as well would
    // move this fader twice, once from its own drag and once from being the
    // first column the drawing crosses.
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

private:
  bool dragging = false;
  bool drawing = false;
};

/// An endless, relative control.
///
/// It has no meaningful absolute position. It reports how far it has been
/// turned since the drag began and springs back to centre on release, so the
/// 32 strips it drives keep whatever spread you dialled into them instead of
/// all jumping to one value.
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
/// The strips get their captions from the gutter, so this is for the controls
/// that stand on their own: the master section and the effects.
class LabelledKnob : public juce::Component {
public:
  explicit LabelledKnob(juce::String captionText,
                        juce::Colour fill = colours::accent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Where the dial sits inside bounds of this size.
  ///
  /// Public because a panel that mixes knobs with buttons has to line the
  /// buttons up with the dials rather than with the middle of the row: the
  /// caption underneath means the two are not the same place.
  static juce::Rectangle<int> dialBounds(juce::Rectangle<int>);

  /// And where the caption under it sits, for the same reason turned around.
  /// The bar names a group in the band its knobs put their captions in, and
  /// reading that band off the same function is what keeps the two on one
  /// line however the band is sized.
  static juce::Rectangle<int> captionBounds(juce::Rectangle<int>);

  LinkableSlider slider;

private:
  juce::String caption;
};

/// A small seven-segment readout, the way a piece of hardware labels a value.
///
/// Used for the converter settings on the bar and for the two figures under
/// each channel, the cents a partial is tuned by and the level its fader is
/// at. Digits, a decimal point, a sign, and the few letters needed to say the
/// things that are not numbers: ET for a partial left in equal temperament,
/// and -inF for a fader all the way down.
///
/// Dimming means the value is a statement of fact rather than something being
/// changed: a converter left at the host's own rate still says what that rate
/// is, and a partial with no tuning to do still says so.
///
/// Give it an onClick and it becomes a control, opening whatever menu the
/// value came from, and picks up a hover outline to say so. Without one it is
/// a readout and stays quiet under the pointer.
class SegmentDisplay : public juce::Component,
                       public juce::SettableTooltipClient {
public:
  /// How many bars a cell has, which decides what it can say.
  ///
  /// Seven manages the digits and about five letters, which is everything a
  /// readout on this panel has to report. Fourteen carries the alphabet, for
  /// the one display that has to spell: a preset is called Glockenspiel.
  enum class Bars { Seven, Fourteen };

  /// @param unit   drawn small beside the digits, or empty for none.
  /// @param cells  how many characters the display has, or 0 to size itself
  ///               to whatever it is given. A fixed count keeps the cells the
  ///               same width whatever the reading is, and fills the ones a
  ///               short reading does not reach, which is what a display with
  ///               a real number of digits in it does.
  explicit SegmentDisplay(juce::String unit, Bars bars = Bars::Seven,
                          int cells = 0);

  /// @param digits  0 to 9, a decimal point, a leading + or -, and the
  ///                 letters in segmentsFor. A fourteen-bar display takes the
  ///                 whole alphabet as well, upper-casing as it goes, which is
  ///                 what a display with no lower case does. Anything else is
  ///                 drawn blank.
  /// @param active  false dims it, meaning nothing is being changed.
  void setReading(const juce::String &digits, bool active);

  /// How many bars its cells have, which says what it is for: a seven-bar one
  /// reports a number and a fourteen-bar one spells a name. Read back by the
  /// tests, which hold each display's reading against what its own cells can
  /// draw rather than against a single alphabet.
  Bars howManyBars() const noexcept { return bars; }

  /// Fits a name to a fixed number of cells.
  ///
  /// Upper case, then the spaces go from the right, then the vowels, then
  /// what is left is cut. Never the first character, whatever it is: a name
  /// that starts with a vowel still has to start with it.
  ///
  /// Spaces before vowels, because a space is a cell saying nothing and a
  /// vowel is a cell saying something. It runs the words together, which is
  /// the price: Tape Choir keeps every letter as TAPECHOIR where cutting
  /// would have given TAPE CHOI. Y is not a vowel here, which is what keeps
  /// SYNTH and NYLON and STYLOPOLY readable.
  ///
  /// Shorter than @p cells comes back padded, so the caller always has
  /// exactly that many characters.
  static juce::String squeeze(const juce::String &, int cells);

  /// How many of the reading's characters the last paint had room for.
  ///
  /// A name wider than the display is cut rather than squeezed, and a cell
  /// count that is one short of the characters is what a dropped letter looks
  /// like from here. Nothing else can see it: the cells are drawn, so the
  /// only other way to ask is to count marks in a render, and a K has a wider
  /// gap down its own middle than there is between one cell and the next.
  int cellsDrawn() const noexcept { return drawn; }

  /// What it is showing, and whether it is showing it lit. Read back by the
  /// tests, which is the only way to check a component that is otherwise
  /// nothing but paint.
  const juce::String &getReading() const noexcept { return reading; }
  bool isActive() const noexcept { return active; }

  /// Whether a display this wide has room to name its unit beside the digits.
  ///
  /// Public because it is a layout rule rather than a painting detail: what
  /// the bar hands its converter readouts decides whether they can say what
  /// their numbers mean, and a test holds the bar to it.
  static bool hasRoomForUnit(int width);

  /// Whether this character has a form to draw, either as a glyph or as one
  /// of the narrow cells. Anything else comes out as an unlit digit, so the
  /// tests hold every reading the panel can produce against this.
  static bool canDraw(char, Bars = Bars::Seven);

  std::function<void()> onClick;

  void paint(juce::Graphics &) override;
  void mouseUp(const juce::MouseEvent &) override;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

  /// Announces one that opens a menu as the button it is.
  ///
  /// A plain component has no role and no actions, so the two converter
  /// settings were the one part of the panel a screen reader could neither
  /// name nor reach, and their digits are drawn rather than written so there
  /// was nothing to fall back on either. One that is only a readout is left
  /// as it is, which is what it is.
  std::unique_ptr<juce::AccessibilityHandler>
  createAccessibilityHandler() override;

private:
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

  juce::String reading, unitText;
  bool active = false;
  bool hovered = false;
};

/// A slim output meter for one partial.
///
/// Segmented rather than a continuous bar, which is the old spectrum-analyser
/// look and is also what makes it cheap. A smooth bar has to be redrawn on
/// every frame in which the level moves at all, which for a decaying note is
/// every frame. A segmented one only changes when the level crosses a segment
/// boundary, so a slow decay redraws a handful of times a second instead of
/// thirty, and the frames in between cost nothing at all.
///
/// Unlit segments keep the channel colour at low alpha rather than going dark,
/// so the meter reads as a column of lamps that are off rather than as a bar
/// that has gone.
class LevelMeter : public juce::Component {
public:
  explicit LevelMeter(juce::Colour barColour) : colour(barColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// The channel background behind this meter, at its top and bottom edges.
  ///
  /// Given these, the meter paints its own backdrop and can declare itself
  /// opaque, which stops the strip underneath being redrawn every time a lamp
  /// changes. The colours have to come from the strip because the background
  /// is a gradient down the whole channel, and the meter only covers a slice
  /// of it.
  void setBackdrop(juce::Colour top, juce::Colour bottom);

  /// @param level  linear amplitude from the audio thread, 0 to 1.
  /// @returns the region it asked to have repainted, empty when nothing moved.
  ///
  /// Handing the region back rather than exposing the maths behind it keeps
  /// the test honest: it checks the rectangle the component actually used, in
  /// the units the component actually used, and cannot drift from it.
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
/// It says how hard the group below it is working on this partial right now:
/// the envelope's position, the key-off stage that takes over from it, how far
/// the tremolo has pulled the level down. The rule is already there, so the
/// lamp costs no height at all and reads as something fitted to the divider
/// rather than as another row of controls.
///
/// Brightness is quantised, for the same reason the level meter is segmented.
/// A lamp that follows a value exactly is a lamp that repaints on every frame
/// in which the value moves at all, which for anything modulated is every
/// frame. In steps it repaints only when it has something new to show, and a
/// slow envelope goes whole seconds without costing a frame.
class ActivityLamp : public juce::Component {
public:
  explicit ActivityLamp(juce::Colour litColour) : colour(litColour) {
    setInterceptsMouseClicks(false, false);
  }

  /// The channel background behind the lamp, so it can paint its own backdrop
  /// and declare itself opaque. See LevelMeter::setBackdrop, which is the same
  /// bargain: an opaque child is one the strip underneath need not redraw.
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

/// A needle on a rule, showing where pitch modulation has this partial.
///
/// Reads like a tuner because that is the thing it is: centre is the note as
/// written, right is sharp, left is flat, against a fixed scale that is the
/// same on every strip. Full deflection is the widest displacement the two
/// controls can produce together, so how far the needle swings says how deep
/// the modulation is set and two channels can be compared by eye.
class ActivityNeedle : public juce::Component {
public:
  explicit ActivityNeedle(juce::Colour needleColour) : colour(needleColour) {
    setInterceptsMouseClicks(false, false);
  }

  void setBackdrop(juce::Colour);

  /// The margin the slot leaves itself inside the component, each side.
  ///
  /// The lip of a recess is drawn around the opening rather than inside it,
  /// so an opening taken out to the component's own edge has nowhere to put
  /// its side walls and loses its rounded ends with them. One pixel is what
  /// the lip needs and is all this has to give. The travel is measured off
  /// the same number, so the needle still reaches both ends of the slot and
  /// no further.
  static constexpr int kSlotInset = 1;

  /// @param position  -1 to 1, flat to sharp, already scaled by the caller.
  /// @returns true when the needle moved a pixel and needs repainting.
  bool push(float position);

  void paint(juce::Graphics &) override;

private:
  /// Where the needle sits, in pixels from the left edge, or -1 for parked.
  /// Quantised to the pixel because a needle that has not moved a whole pixel
  /// has not moved.
  int column = -1;

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
  /// The row wash can sit behind the children because the things it crosses
  /// are knobs, which have transparent corners for it to show through. A
  /// column crosses the meter and the lamps, which paint their own backgrounds
  /// so the strip underneath does not have to, and behind those it would
  /// simply disappear, leaving the channel marked everywhere except the parts
  /// with something in them.
  void paintOverChildren(juce::Graphics &) override;

  void resized() override;

  /// Which groups of rows are folded away. Set by the editor on every strip at
  /// once, since the rows are shared across the whole mixer.
  void setCollapsedSections(SectionMask);

  /// How far the parameters are scrolled, shared by every column.
  ///
  /// The gutter, the 32 strips and the noise channel are handed the same
  /// number by the editor, which is what keeps a caption pointing at the knob
  /// beside it. Snapped to a row boundary inside layoutRows, so a row is never
  /// half over the header.
  void setScroll(int);

  /// Asked for when a click lands on one of the rules between sections, which
  /// line up with the gutter's headings and do the same thing.
  std::function<void(Section)> onSectionToggled;

  /// Sets this channel's fader from a height, for a drag that is drawing
  /// across the mixer rather than moving one fader.
  ///
  /// The height is in this strip's own coordinates. The strip does the reading
  /// rather than the editor because the value is the slider's business: its
  /// range is skewed to match the parameter's, and a proportion of the track
  /// is the only honest way in.
  void drawFaderAt(int y);

  /// The row the pointer is on, with the rules between sections counted as
  /// rows of their own. See reportHover.
  static Row rowUnder(const RowBounds &, juce::Point<int>);

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;

  /// Keeps a wheel that landed on a control from scrolling the mixer as well.
  ///
  /// The strip listens to everything inside it, so that a pointer resting on a
  /// knob is reported by the strip rather than swallowed by the control. JUCE
  /// hands that listener every event, wheels included, and Component's own
  /// handler passes whatever it is given up to the parent. So a scroll the
  /// knob had already taken went on to the viewport and dragged the series
  /// sideways under the hand that was turning the knob, whenever the window
  /// was narrow enough for there to be anything to scroll.
  ///
  /// Only a wheel that actually landed on the strip is passed on, which leaves
  /// the background scrolling the series and a control keeping its own.
  /// Asks the editor to scroll, and says whether it had anywhere to go.
  bool scrollParametersBy(const juce::MouseWheelDetails &);

  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  /// Greys the strip out when another strip's solo is silencing it.
  void setSilencedByOthers(bool shouldDim);

  /// @returns the region of this strip that needs redrawing, empty if the
  /// meter did not move. The editor collects these and invalidates once, so
  /// the window sees one dirty rectangle a frame rather than thirty-three.
  juce::Rectangle<int> setMeterLevel(float level) {
    const auto band = meter.push(level);
    return band.isEmpty() ? band : band.translated(meter.getX(), meter.getY());
  }

  /// What the lamps on the section rules show.
  ///
  /// @param envelope  signed: positive while the key is down, negative once
  ///                  the key-off stage has it. The two lamps split on that
  ///                  sign, so one hands over to the other rather than both
  ///                  being lit at once.
  /// @param tremolo   how far the tremolo has pulled the level down, 0 to 1.
  /// @param pitch     displacement in cents, or the parked value when the
  ///                  partial is silent.
  ///
  /// Appends the bounds of every lamp that moved, in this strip's own
  /// coordinates, and leaves the frame with nothing appended when nothing
  /// moved.
  ///
  /// They go back to the editor rather than each invalidating itself, so they
  /// are merged along with the meter bands into the handful of rectangles the
  /// window is given. A hundred and twenty-nine separate small invalidations
  /// is the case the merging exists to avoid: past a handful the window gives
  /// up and redraws their bounding box, which here is the whole mixer.
  ///
  /// They merge well, too. Every strip's lamps sit at the same height, so the
  /// union of a row of them is a thin wide band with no wasted area in it,
  /// which is the opposite of what the meter bands do.
  void setActivity(float envelope, float tremolo, float pitch, float velGain,
                   float pressure, juce::Array<juce::Rectangle<int>> &into);

  /// Where a displacement sits on the needle's travel, -1 to 1.
  ///
  /// Fixed scale, not the strip's own: full deflection is always
  /// params::kPitchNeedleFullScaleCents, so a shallow setting stays near the
  /// middle instead of using the whole width like a deep one.
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

  /// Which macro drives this row, as its colour, or transparent for none.
  ///
  /// Carried beside the control's own colour rather than replacing it. The
  /// pointer on a knob stays the channel's colour, which is what says which
  /// partial you are looking at, and the ring beyond it goes to the macro's,
  /// which is what says where the macro has taken the value.
  ///
  /// @param result  where the row ends up once the macro has had its say, as
  ///                a proportion of the control's travel. The same as the
  ///                control's own position when no macro is driving it, which
  ///                is what makes a macro at rest look like no macro at all.
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

  LinkableSlider tune, phase, pmRate, pmDepth, drift, strike, delay, attack,
      decay, sustain, swell, offLevel, release, amRate, amDepth, velocity,
      aftertouch, pan, volume;
  /// What each modulator traces. A glyph rather than a knob, since eight
  /// named shapes are a list and not a range.
  ShapeButton pmShape, amShape;

  MuteSoloButton muteButton, soloButton;
  /// No unit on either: the gutter caption beside them already says cents and
  /// dB, and twenty-one pixels of "ct" would leave the digits nothing.
  SegmentDisplay tuneReadout{{}}, levelReadout{{}};
  LevelMeter meter;

  ActivityNeedle pitchLamp;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp, velocityLamp, pressureLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;

  Row highlighted = kNoRow;

  /// Folded groups. Nothing here decides it, the editor does, but every
  /// layout and hit test in this strip has to agree with it.
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

  /// Whether the glow is lit in the accent rather than in the channel's own
  /// colour. LINK's preview is per channel, since it is saying how much each
  /// one would take; the drawing's is one colour across the mixer, since every
  /// fader is equally drawable and the band is one surface.
  bool glowAccent = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
