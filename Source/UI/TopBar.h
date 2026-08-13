#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

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
/// The base exists so the top bar can treat absolute and relative knobs alike
/// when laying them out, even though they are different slider types.
class KnobPanel : public juce::Component {
public:
  explicit KnobPanel(juce::String captionText)
      : caption(std::move(captionText)) {}

  virtual juce::Slider &getSlider() = 0;

  void paint(juce::Graphics &g) override {
    g.setColour(colours::textDim);
    g.setFont(makeFont(9.0f, true));
    g.drawText(caption, getLocalBounds().removeFromBottom(11),
               juce::Justification::centred, false);
  }

  void resized() override {
    getSlider().setBounds(getLocalBounds().withTrimmedBottom(12));
  }

private:
  juce::String caption;
};

template <typename SliderType> class LabelledKnob : public KnobPanel {
public:
  explicit LabelledKnob(juce::String captionText,
                        juce::Colour fill = colours::accent)
      : KnobPanel(std::move(captionText)) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
    addAndMakeVisible(slider);
  }

  juce::Slider &getSlider() override { return slider; }

  SliderType slider;
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

  /// The endless macros. Role says which per-strip control they drive.
  std::function<void(Role)> onMacroStart, onMacroEnd;
  std::function<void(Role, float delta)> onMacroDelta;

  bool isLinkEnabled() const { return linkButton.getToggleState(); }

  void setVoiceCount(int active, int limit);
  void setZoomChoice(float zoom);

private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
  using ComboBoxAttachment =
      juce::AudioProcessorValueTreeState::ComboBoxAttachment;

  void styleToggle(juce::TextButton &, const juce::String &text,
                   const juce::String &tooltip);
  void wireMacro(RelativeKnob &, Role);

  juce::AudioProcessorValueTreeState &apvts;

  // Tune, velocity and aftertouch exist on every strip, so their top-bar knobs
  // are relative macros. Master, spread and bend are single global values with
  // nothing to stay relative to, so they are ordinary absolute knobs.
  LabelledKnob<RelativeKnob> tuneAll{"TUNE ALL", colours::accent};
  LabelledKnob<LinkableSlider> master{"MASTER"};
  LabelledKnob<LinkableSlider> spread{"SPREAD"};
  LabelledKnob<RelativeKnob> velocity{"VEL"};
  LabelledKnob<RelativeKnob> aftertouch{"AT"};
  LabelledKnob<LinkableSlider> bend{"BEND"};

  juce::ComboBox presetBox, polyBox, zoomBox;
  juce::Label presetCaption, polyCaption, zoomCaption, voicesLabel;

  juce::TextButton linkButton, phaseButton, clipButton;

  std::unique_ptr<SliderAttachment> masterAttachment, spreadAttachment,
      bendAttachment;
  std::unique_ptr<ComboBoxAttachment> polyAttachment;
  std::unique_ptr<ButtonAttachment> phaseAttachment, clipAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
