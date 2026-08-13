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

/// One vertical channel: everything that belongs to a single partial.
class ChannelStrip : public juce::Component,
                     public juce::SettableTooltipClient {
public:
  ChannelStrip(juce::AudioProcessorValueTreeState &state,
               LinkTarget &linkTarget, juce::Component &popupParent,
               int index0);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Greys the strip out when another strip's solo is silencing it.
  void setSilencedByOthers(bool shouldDim);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void setUpKnob(LinkableSlider &, Role, juce::Colour fill);
  void setUpFader(LinkableSlider &, Role, juce::Colour fill);
  void wireUp(LinkableSlider &, Role);

  void updateTuneReadout();
  void updateLevelReadout();

  juce::AudioProcessorValueTreeState &apvts;
  LinkTarget &link;
  juce::Component &popupHost;

  const int index;
  const HarmonicInfo info;
  const juce::Colour colour;

  LinkableSlider tune, pmRate, pmDepth, drift, attack, decay, sustain, release,
      amRate, amDepth, velocity, aftertouch, volume;
  juce::TextButton muteButton{"M"}, soloButton{"S"};
  juce::Label tuneReadout, levelReadout;

  std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
  std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment;

  bool silenced = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

} // namespace ovt::ui
