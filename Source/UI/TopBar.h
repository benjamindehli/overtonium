#pragma once

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

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
  LinkScope getLinkScope() const { return scope; }
  LinkCurve getLinkCurve() const { return curve; }

  void setLinkScope(LinkScope);
  void setLinkCurve(LinkCurve);

  std::function<void()> onLinkSettingsChanged;

  /// The switch, the scope and the curve, in one menu.
  ///
  /// Shared by the button in the bar and by a right-click anywhere on a strip,
  /// so there is one list rather than two that can drift apart.
  ///
  /// @param anchor  what to hang the menu off, or nullptr to put it under the
  ///                pointer.
  void showLinkMenu(juce::Component *anchor);

  /// The bar reflows onto further rows when the groups no longer fit across
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

  void styleToggle(juce::TextButton &, const juce::String &text,
                   const juce::String &tooltip);

  /// The switch lives in the menu now, so the button has to redraw when it
  /// moves rather than when it is clicked.
  void updateLinkEnablement();

  /// One effect knob, wired to its parameter.
  struct Control {
    std::unique_ptr<LabelledKnob> knob;
    std::unique_ptr<SliderAttachment> attachment;
  };

  void addKnob(std::vector<Control> &into, const juce::String &caption,
               const juce::String &paramId, const juce::String &tooltip,
               juce::Component &popupParent);

  /// Polyphony, bend range and the two output switches. All of them are set
  /// once and left, which is a menu rather than a panel.
  void showSettingsMenu();

  /// Related controls sit together in a bordered group.
  enum Group {
    PresetGroup = 0,
    VoiceGroup,
    LinkGroup,
    EchoGroup,
    ReverbGroup,
    OutputGroup,
    ViewGroup,
    NumGroups
  };

  /// Where the rows break. Entry r is the first group on row r, and the last
  /// entry is NumGroups, so row r holds [start[r], start[r + 1]).
  struct RowPlan {
    int rows = 1;
    std::array<int, NumGroups + 1> start{};
  };

  /// Packs the groups into as many rows as it takes.
  static RowPlan planRows(int firstRowWidth, int fullWidth, int maxRows);

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

  StereoOutputMeter meter;
  juce::Label meterCaption;

  juce::ComboBox presetBox, zoomBox;
  juce::Label presetCaption, zoomCaption;

  juce::TextButton settingsButton, linkButton;
  juce::TextButton echoButton, reverbButton;

  /// Shown in the settings menu rather than on the panel.
  int activeVoices = 0;

  std::vector<Control> echoControls, reverbControls;

  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;

  std::unique_ptr<SliderAttachment> masterAttachment;
  std::unique_ptr<ButtonAttachment> echoAttachment, reverbAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
