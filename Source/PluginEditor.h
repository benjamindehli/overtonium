#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/ChannelStrip.h"
#include "UI/LookAndFeel.h"
#include "UI/MasterStrip.h"
#include "UI/TopBar.h"

/// Left-hand caption column. Lays out the same rows as a channel strip so every
/// knob in the mixer has a name without repeating it 32 times.
class RowGutter : public juce::Component {
public:
  void paint(juce::Graphics &) override;
};

class OvertoniumEditor : public juce::AudioProcessorEditor,
                         public ovt::ui::LinkTarget,
                         public ovt::ui::MacroTarget,
                         private juce::Timer {
public:
  explicit OvertoniumEditor(OvertoniumProcessor &);
  ~OvertoniumEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  // ---- ovt::ui::LinkTarget ----
  bool isLinkEnabled() const override;
  void linkDragStarted(ovt::ui::Role, int sourceIndex) override;
  void linkValueChanged(ovt::ui::Role, int sourceIndex,
                        float plainValue) override;
  void linkDragEnded(ovt::ui::Role, int sourceIndex) override;

  // ---- ovt::ui::MacroTarget ----
  void macroStarted(ovt::ui::Role) override;
  void macroMoved(ovt::ui::Role, float delta) override;
  void macroEnded(ovt::ui::Role) override;
  void setAllMutes(bool muted) override;
  void clearAllSolos() override;
  bool anySoloActive() const override;

private:
  void timerCallback() override;

  void setZoom(float newZoom);
  void applyResizeLimits();
  void applyPreset(int index);

  /// Snapshots every strip's normalised value for a role, so a relative gesture
  /// can be expressed as an offset from where things stood when it began.
  void captureBaseline(ovt::ui::Role);
  void applyOffsetFromBaseline(ovt::ui::Role, float delta, int skipIndex);

  juce::RangedAudioParameter *oscParameter(ovt::ui::Role, int index) const;
  juce::RangedAudioParameter *boolParameter(const char *suffix,
                                            int index) const;

  OvertoniumProcessor &processor;

  // Declared first so it outlives every component that borrows it.
  ovt::ui::OvertoniumLookAndFeel lookAndFeel;

  juce::TooltipWindow tooltips{this, 600};

  /// Single child holding the whole UI, so zoom is one AffineTransform.
  juce::Component content;
  ovt::ui::TopBar topBar;
  RowGutter gutter;
  ovt::ui::MasterStrip masterStrip;

  // stripsHolder is declared before the viewport that displays it, so on
  // teardown the viewport is destroyed first and never sees a dangling viewed
  // component.
  juce::Component stripsHolder;
  juce::Viewport viewport;

  std::vector<std::unique_ptr<ovt::ui::ChannelStrip>> strips;

  float zoom = 1.0f;

  std::array<float, ovt::kNumHarmonics> baseline{};

  bool propagatingLink = false;
  bool linkGestureActive = false;
  bool macroGestureActive = false;
  ovt::ui::Role macroRole = ovt::ui::Role::Tune;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumEditor)
};
