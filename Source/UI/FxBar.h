#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "Theme.h"

namespace ovt::ui {

/// The master effects, along the bottom of the window.
///
/// They sit under the mixer rather than in the top bar because that is where
/// they sit in the signal: everything above this row happens per partial, and
/// everything on it happens to the sum. The two groups carry the same bordered
/// panels as the top bar, so the whole window reads as one surface.
class FxBar : public juce::Component {
public:
  FxBar(juce::AudioProcessorValueTreeState &, juce::Component &popupParent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Fixed, unlike the top bar: the two groups fit side by side at every width
  /// the window can be dragged to, so there is nothing to reflow.
  static int height();

  /// Narrowest the bar can be laid out without controls colliding.
  static int minimumWidth();

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  /// One knob wired to one parameter, with its caption and tooltip.
  struct Control {
    std::unique_ptr<LabelledKnob> knob;
    std::unique_ptr<SliderAttachment> attachment;
  };

  Control &addKnob(std::vector<Control> &into, const juce::String &caption,
                   const juce::String &paramId, const juce::String &tooltip);

  void styleToggle(juce::TextButton &, const juce::String &text,
                   const juce::String &tooltip);

  /// Lays a group out into `area`: the switch, then its knobs.
  void layoutGroup(juce::Rectangle<int> area, juce::TextButton &toggle,
                   std::vector<Control> &controls);

  juce::AudioProcessorValueTreeState &apvts;
  juce::Component &popupHost;

  juce::TextButton echoButton, reverbButton;
  std::unique_ptr<ButtonAttachment> echoAttachment, reverbAttachment;

  std::vector<Control> echoControls, reverbControls;

  std::array<juce::Rectangle<int>, 2> groupBounds{};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBar)
};

} // namespace ovt::ui
