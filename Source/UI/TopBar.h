#pragma once

#include <array>
#include <functional>
#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

/// The stereo output meter.
///
/// Horizontal and split, because the two channels only differ once stereo
/// spread is dialled in and a summed meter would hide exactly that. Carries a
/// peak hold, since the thing you most want from an output meter is what it hit
/// a moment ago rather than what it is doing right now.
class StereoOutputMeter : public juce::Component {
public:
  StereoOutputMeter() { setInterceptsMouseClicks(false, false); }

  /// @param l,r  linear peaks from the audio thread.
  void push(float l, float r);

  void paint(juce::Graphics &) override;

private:
  struct Bar {
    float displayed = 0.0f;
    float peak = 0.0f;
    int hold = 0;

    /// @returns true when something moved by enough to be worth redrawing.
    bool advance(float level);
  };

  void paintBar(juce::Graphics &, juce::Rectangle<float>, const Bar &) const;

  Bar left, right;
};

/// A knob with a caption underneath.
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

class TopBar : public juce::Component {
public:
  TopBar(juce::AudioProcessorValueTreeState &apvts,
         juce::Component &popupParent);

  void paint(juce::Graphics &) override;
  void resized() override;

  // ---- callbacks the editor fills in ----
  std::function<void(int)> onPresetChosen;
  std::function<void(float)> onZoomChanged;

  bool isLinkEnabled() const { return linkButton.getToggleState(); }

  /// Latched by the editor at the start of each LINK drag.
  LinkScope getLinkScope() const;
  LinkCurve getLinkCurve() const;

  void setLinkScope(LinkScope);
  void setLinkCurve(LinkCurve);

  std::function<void()> onLinkSettingsChanged;

  /// The bar reflows onto a second row when the groups no longer fit across
  /// one, so nothing has to be dropped on a narrow window. Static because the
  /// editor has to know the height before it can hand the bar its bounds.
  static int heightForWidth(int width);

  /// Narrowest window the bar can lay out without hiding a group. The editor
  /// takes this as a floor, since refusing to shrink further is better than
  /// quietly dropping controls.
  static int minimumWidth();

  void setVoiceCount(int active, int limit);
  void setOutputLevels(float l, float r) { meter.push(l, r); }
  void setZoomChoice(float zoom);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboBoxAttachment =
      juce::AudioProcessorValueTreeState::ComboBoxAttachment;

  void styleToggle(juce::TextButton &, const juce::String &text,
                   const juce::String &tooltip);

  /// The two selectors only mean anything while LINK is engaged.
  void updateLinkEnablement();

  /// Related controls sit together in a bordered group.
  enum Group {
    PresetGroup = 0,
    VoiceGroup,
    LinkGroup,
    OutputGroup,
    ViewGroup,
    NumGroups
  };

  /// Clears every control's bounds before a fresh pass. Without this a control
  /// that does not get placed keeps whatever position it had when the window
  /// was wider, which is what put the preset and poly captions on top of one
  /// another.
  void parkControls();

  void layoutRow(juce::Rectangle<int> row, int firstGroup, int lastGroup);
  void placeGroup(int group, juce::Rectangle<int> bounds);

  std::array<juce::Rectangle<int>, NumGroups> groupBounds{};

  juce::AudioProcessorValueTreeState &apvts;

  // Anything that exists on all 32 strips now lives on the master channel.
  // What is left here is the handful of genuinely single global values, which
  // have nothing to stay relative to and so are ordinary absolute knobs.
  LabelledKnob master{"MASTER"};
  LabelledKnob spread{"SPREAD"};
  LabelledKnob bend{"BEND"};

  StereoOutputMeter meter;
  juce::Label meterCaption;

  juce::ComboBox presetBox, polyBox, zoomBox, scopeBox, curveBox;
  juce::Label presetCaption, polyCaption, zoomCaption, scopeCaption,
      curveCaption, voicesLabel;

  juce::TextButton linkButton, phaseButton, clipButton;

  std::unique_ptr<SliderAttachment> masterAttachment, spreadAttachment,
      bendAttachment;
  std::unique_ptr<ComboBoxAttachment> polyAttachment;
  std::unique_ptr<ButtonAttachment> phaseAttachment, clipAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
