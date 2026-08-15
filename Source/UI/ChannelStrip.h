#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

  LinkableSlider slider;

private:
  juce::String caption;
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

/// One vertical channel: everything that belongs to a single partial.
class ChannelStrip : public juce::Component,
                     public juce::SettableTooltipClient {
public:
  ChannelStrip(juce::AudioProcessorValueTreeState &state,
               LinkTarget &linkTarget, HoverTarget &hoverTarget,
               juce::Component &popupParent, int index0);

  void paint(juce::Graphics &) override;
  void resized() override;

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

  void updateTuneReadout();
  void updateLevelReadout();

  /// Tells the editor which row the pointer is on. Called for the strip's own
  /// mouse events and for those of every control on it.
  void reportHover(const juce::MouseEvent &);

  LinkableSlider *sliderForRole(Role);

  juce::AudioProcessorValueTreeState &apvts;
  LinkTarget &link;
  HoverTarget &hover;
  juce::Component &popupHost;

  const int index;
  const HarmonicInfo info;
  const juce::Colour colour;

  LinkableSlider tune, pmRate, pmDepth, drift, delay, attack, decay, sustain,
      swell, offLevel, release, amRate, amDepth, velocity, aftertouch, pan,
      volume;
  juce::TextButton muteButton{"M"}, soloButton{"S"};
  juce::Label tuneReadout, levelReadout;
  LevelMeter meter;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;

  Row highlighted = kNoRow;
  Role glowRole = Role::Tune;
  float glowAmount = 0.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
