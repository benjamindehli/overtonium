#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "Theme.h"

namespace ovt::ui
{

/** A knob with a caption underneath. */
class LabelledKnob : public juce::Component
{
public:
    explicit LabelledKnob (juce::String captionText, juce::Colour fill = colours::accent);

    void paint (juce::Graphics&) override;
    void resized() override;

    LinkableSlider slider;

private:
    juce::String caption;
};

class TopBar : public juce::Component
{
public:
    TopBar (juce::AudioProcessorValueTreeState& apvts, juce::Component& popupParent);

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- callbacks the editor fills in ----
    std::function<void (int)>   onPresetChosen;
    std::function<void (float)> onTuneAllChanged;
    std::function<void()>       onTuneAllDragStart, onTuneAllDragEnd;
    std::function<void (float)> onZoomChanged;

    bool isLinkEnabled() const { return linkButton.getToggleState(); }

    void setVoiceCount (int active, int limit);

    /** Syncs the macro knob when every strip happens to share the same tuning. */
    void setTuneAllDisplay (float blend);

    void setZoomChoice (float zoom);

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void styleToggle (juce::TextButton&, const juce::String& text, const juce::String& tooltip);

    juce::AudioProcessorValueTreeState& apvts;

    LabelledKnob tuneAll { "TUNE ALL", colours::accent };
    LabelledKnob master  { "MASTER" };
    LabelledKnob spread  { "SPREAD" };
    LabelledKnob velocity{ "VEL" };
    LabelledKnob bend    { "BEND" };

    juce::ComboBox presetBox, polyBox, zoomBox;
    juce::Label    presetCaption, polyCaption, zoomCaption, voicesLabel;

    juce::TextButton linkButton, phaseButton, clipButton;

    std::unique_ptr<SliderAttachment>   masterAttachment, spreadAttachment, velAttachment, bendAttachment;
    std::unique_ptr<ComboBoxAttachment> polyAttachment;
    std::unique_ptr<ButtonAttachment>   phaseAttachment, clipAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace ovt::ui
