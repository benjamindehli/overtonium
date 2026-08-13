#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/ChannelStrip.h"
#include "UI/LookAndFeel.h"
#include "UI/TopBar.h"

/** Left-hand caption column. Lays out the same rows as a channel strip so every knob in
    the mixer has a name without repeating it 32 times. */
class RowGutter : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

class OvertoniumEditor : public juce::AudioProcessorEditor,
                         public ovt::ui::LinkTarget,
                         private juce::Timer
{
public:
    explicit OvertoniumEditor (OvertoniumProcessor&);
    ~OvertoniumEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- ovt::ui::LinkTarget ----
    bool isLinkEnabled() const override;
    void linkDragStarted  (ovt::ui::Role, int sourceIndex) override;
    void linkValueChanged (ovt::ui::Role, int sourceIndex, float plainValue) override;
    void linkDragEnded    (ovt::ui::Role, int sourceIndex) override;

private:
    void timerCallback() override;

    void setZoom (float newZoom);
    void applyResizeLimits();
    void applyPreset (int index);

    void beginTuneAllGesture();
    void setAllTuning (float blend);
    void endTuneAllGesture();

    juce::RangedAudioParameter* oscParameter (ovt::ui::Role, int index) const;

    OvertoniumProcessor& processor;

    // Declared first so it outlives every component that borrows it.
    ovt::ui::OvertoniumLookAndFeel lookAndFeel;

    juce::TooltipWindow tooltips { this, 600 };

    /** Single child holding the whole UI, so zoom is one AffineTransform. */
    juce::Component content;
    ovt::ui::TopBar topBar;
    RowGutter       gutter;

    // stripsHolder is declared before the viewport that displays it, so on teardown the
    // viewport is destroyed first and never sees a dangling viewed component.
    juce::Component stripsHolder;
    juce::Viewport  viewport;

    std::vector<std::unique_ptr<ovt::ui::ChannelStrip>> strips;

    float zoom = 1.0f;

    bool propagatingLink   = false;
    bool linkGestureActive = false;
    bool tuneAllGesture    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertoniumEditor)
};
