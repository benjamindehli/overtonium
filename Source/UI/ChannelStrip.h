#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MuteSoloButton.h"
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
    if (!e.mods.isPopupMenu())
      juce::Slider::mouseDown(e);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (!e.mods.isPopupMenu())
      juce::Slider::mouseDrag(e);
  }

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

  std::function<void()> onUserDragStart, onUserDragEnd;

private:
  bool dragging = false;
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
  /// @param unit  drawn small beside the digits, or empty for none.
  explicit SegmentDisplay(juce::String unit);

  /// @param digits  0 to 9, a decimal point, a leading + or -, and the
  ///                 letters in segmentsFor. Anything else is drawn blank.
  /// @param active  false dims it, meaning nothing is being changed.
  void setReading(const juce::String &digits, bool active);

  /// What it is showing, and whether it is showing it lit. Read back by the
  /// tests, which is the only way to check a component that is otherwise
  /// nothing but paint.
  const juce::String &getReading() const noexcept { return reading; }
  bool isActive() const noexcept { return active; }

  /// Whether this character has a form to draw, either as a glyph or as one
  /// of the narrow cells. Anything else comes out as an unlit digit, so the
  /// tests hold every reading the panel can produce against this.
  static bool canDraw(char);

  std::function<void()> onClick;

  void paint(juce::Graphics &) override;
  void mouseUp(const juce::MouseEvent &) override;
  void mouseEnter(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

private:
  /// Draws one character in the classic seven-bar arrangement.
  void paintGlyph(juce::Graphics &, juce::Rectangle<float>, char,
                  juce::Colour on, juce::Colour off) const;

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

  void mouseEnter(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;

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
  void setActivity(float envelope, float tremolo, float pitch,
                   juce::Array<juce::Rectangle<int>> &into);

  /// Where a displacement sits on the needle's travel, -1 to 1.
  ///
  /// Fixed scale, not the strip's own: full deflection is always
  /// params::kMaxPitchDisplacementCents, so a shallow setting stays near the
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
  void setLinkGlow(Role, float amount);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(LinkableSlider &, Role, juce::Colour fill);
  void setUpFader(LinkableSlider &, Role, juce::Colour fill);
  void wireUp(LinkableSlider &, Role);

  /// What this channel stands on: the shared grey, or a shade up for an
  /// octave. See NoiseStrip, which stands at the same shade for the same
  /// reason, that it is worth telling apart from the run of the series.
  juce::Colour backdropBase() const;

  void updateTuneReadout();
  void updateLevelReadout();

  /// Tells the editor which row the pointer is on. Called for the strip's own
  /// mouse events and for those of every control on it.
  void reportHover(const juce::MouseEvent &);

  /// Lets go of the hover without a mouse event to say so, for when a modal
  /// menu is about to take the pointer away.
  void clearHover();

  LinkableSlider *sliderForRole(Role);

  juce::AudioProcessorValueTreeState &apvts;
  LinkTarget &link;
  HoverTarget &hover;
  juce::Component &popupHost;

  const int index;
  const HarmonicInfo info;
  const juce::Colour colour;

  LinkableSlider tune, phase, pmRate, pmDepth, drift, delay, attack, decay,
      sustain, swell, offLevel, release, lift, amRate, amDepth, velocity,
      aftertouch, pan,
      volume;
  MuteSoloButton muteButton, soloButton;
  /// No unit on either: the gutter caption beside them already says cents and
  /// dB, and twenty-one pixels of "ct" would leave the digits nothing.
  SegmentDisplay tuneReadout{{}}, levelReadout{{}};
  LevelMeter meter;

  ActivityNeedle pitchLamp;
  ActivityLamp envLamp, keyOffLamp, tremoloLamp;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;

  Row highlighted = kNoRow;

  /// Folded groups. Nothing here decides it, the editor does, but every
  /// layout and hit test in this strip has to agree with it.
  SectionMask collapsed = 0;
  bool hovered = false;

  /// Set when a menu takes the pointer away, and cleared when the pointer
  /// moves under its own steam again. See clearHover.
  bool hoverSuppressed = false;
  Role glowRole = Role::Tune;
  float glowAmount = 0.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
