#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/ChannelStrip.h"
#include "UI/LookAndFeel.h"
#include "UI/NoiseStrip.h"
#include "UI/TopBar.h"
#include "dsp/Drift.h"

/// Left-hand caption column. Lays out the same rows as a channel strip so every
/// knob in the mixer has a name without repeating it 32 times.
class RowGutter : public juce::Component {
public:
  void paint(juce::Graphics &) override;

  /// Brightens the caption for the row the pointer is on, which is the point of
  /// the whole highlight: the name of the knob you are holding, thirty channels
  /// away, is the one thing that lights up.
  void setHighlightedRow(ovt::ui::Row);

private:
  ovt::ui::Row highlighted = ovt::ui::kNoRow;

  /// The maker's badge, in the empty foot of the gutter.
  std::unique_ptr<juce::Drawable> makersMark{ovt::ui::logoMakersMark()};
};

class OvertoniumEditor : public juce::AudioProcessorEditor,
                         public ovt::ui::LinkTarget,
                         public ovt::ui::HoverTarget,
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

  void showLinkMenu() override;

  // ---- ovt::ui::HoverTarget ----
  void hoverChanged(int stripIndex, ovt::ui::Row) override;

private:
  void timerCallback() override;

  void setZoom(float newZoom);
  void applyResizeLimits();
  void applyPreset(int index);

  /// Says something went wrong, or that something worked, without stopping
  /// what the message thread is doing.
  void complain(const juce::String &title, const juce::String &detail);

  /// Which strips a drag from this one would reach, and by how much. Zero marks
  /// a strip the scope leaves out.
  void gatherLinkWeights(int sourceIndex, ovt::ui::LinkScope,
                         ovt::ui::LinkCurve,
                         std::array<float, ovt::kNumHarmonics> &out) const;

  /// Lights the control LINK is moving, or would move if you grabbed the one
  /// under the pointer.
  void updateLinkGlow();

  /// Hands the strips a pointer that says what a drag would do to them.
  void updateLinkCursor();

  /// Everything a LINK drag needs, latched when it begins.
  ///
  /// Latching matters: the offset is always measured from where things stood
  /// at the start, so returning the knob restores them exactly, and changing
  /// the scope or curve mid-drag cannot half-apply one rule and half another.
  struct LinkGesture {
    bool active = false;
    ovt::ui::Role role = ovt::ui::Role::Tune;
    ovt::ui::LinkCurve curve = ovt::ui::LinkCurve::Uniform;
    int source = -1;

    std::array<float, ovt::kNumHarmonics> baseline{};
    std::array<float, ovt::kNumHarmonics> weight{}; ///< zero means not selected
    std::array<float, ovt::kNumHarmonics> jitter{}; ///< fixed spread directions

    bool includes(int i) const { return weight[(size_t)i] > 0.0f; }
  };

  LinkGesture linkGesture;

  /// Deliberately never reseeded, so each spread differs from the last.
  ovt::Xorshift spreadRandom{0x9e3779b9u};

  juce::RangedAudioParameter *oscParameter(ovt::ui::Role, int index) const;

  OvertoniumProcessor &processor;

  // Declared first so it outlives every component that borrows it.
  ovt::ui::OvertoniumLookAndFeel lookAndFeel;

  juce::TooltipWindow tooltips{this, 600};

  /// Single child holding the whole UI, so zoom is one AffineTransform.
  juce::Component content;
  ovt::ui::TopBar topBar;
  RowGutter gutter;
  ovt::ui::NoiseStrip noiseStrip;

  // stripsHolder is declared before the viewport that displays it, so on
  // teardown the viewport is destroyed first and never sees a dangling viewed
  // component.
  juce::Component stripsHolder;
  juce::Viewport viewport;

  std::vector<std::unique_ptr<ovt::ui::ChannelStrip>> strips;

  float zoom = 1.0f;
  int housekeepingTick = 0;

  int hoverStrip = -1;
  ovt::ui::Row hoverRow = ovt::ui::kNoRow;

  /// A rotary drag unbinds the pointer from the screen, so the positions it
  /// reports during one mean nothing. The highlight stays where it was grabbed
  /// until the drag ends.
  bool hoverLocked = false;

  bool propagatingLink = false;
  bool macroGestureActive = false;
  ovt::ui::Role macroRole = ovt::ui::Role::Tune;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumEditor)
};
