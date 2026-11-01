#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginParameters.h"
#include "ChannelStrip.h"
#include "Theme.h"

namespace ovt::ui {

/// Where macros are made, laid over the mixer and closed by a click outside
/// it. The eight macros are a fixed pool. See params::kNumMacros.
class MacroPanel final : public juce::Component {
public:
  explicit MacroPanel(juce::AudioProcessorValueTreeState &state);

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;

  /// Reads the parameters again and shows a strip for every macro that has
  /// been made. Called when the panel opens and on the editor's timer.
  void refresh();

  /// Makes this macro if nobody has, pointed at the first row as Add a macro
  /// does, and shows it. For the Playing bar's knob of a macro not yet made.
  void make(int macro);

  /// Asked to close, either by the button or by a click on the dim.
  std::function<void()> onDismiss;

  /// A right-click on a macro's amount, carrying that parameter's id. As
  /// TopBar::onLearnRequested.
  std::function<void(const juce::String &)> onLearnRequested;

  /// Where each macro on Selected keeps its channels, and the channels picked
  /// for LINK right now, which choosing Selected takes. Both set by the editor.
  params::MacroChannels *channels = nullptr;
  std::function<std::uint32_t()> currentSelection;

  /// Runs a change as one step in the undo history. Taking new channels with
  /// the scope already on Selected moves no parameter, so nothing else would
  /// record it.
  std::function<void(const juce::String &, const std::function<void()> &)>
      recordEdit;

  /// How many macros are made, for the button that opens this to say so.
  static int madeCount(const juce::AudioProcessorValueTreeState &);

private:
  /// A fader that hands its right-click on rather than dragging, since the
  /// slider would open a drag gesture whose mouse-up goes to the menu. As
  /// ui::LinkableSlider.
  class AmountSlider final : public juce::Slider {
  public:
    AmountSlider()
        : juce::Slider(juce::Slider::LinearHorizontal,
                       juce::Slider::NoTextBox) {}

    std::function<void()> onPopup;

    void mouseDown(const juce::MouseEvent &e) override {
      if (e.mods.isPopupMenu()) {
        if (onPopup != nullptr)
          onPopup();

        return;
      }

      juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent &e) override {
      if (!e.mods.isPopupMenu())
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent &e) override {
      if (!e.mods.isPopupMenu())
        juce::Slider::mouseUp(e);
    }
  };

  /// One macro's controls. The row button doubles as what says the macro
  /// exists at all: a macro pointing at None is one nobody has made.
  struct Strip {
    juce::TextButton colour, row, scope, curve, anchor, remove;

    /// The amount in per cent, under a column headed PERCENT. A positive
    /// amount shows no sign, since seven bars cannot draw a plus.
    SegmentDisplay reading{{}};
    AmountSlider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attachment;
  };

  /// Makes one macro's controls and wires them to its parameters.
  void buildStrip(int macro);

  /// The scope's menu. Choosing Selected also takes the channels picked now.
  void chooseScope(int macro);

  /// Puts up a menu of a choice parameter's values under a control and writes
  /// what is chosen.
  void choose(int macro, const juce::String &parameterId, const char *title,
              int count, const std::function<juce::String(int)> &nameOf,
              juce::Component *under, int firstValue = 0);

  /// Shows the amount as a percentage beside the fader.
  void showReading(int macro);

  /// A choice parameter's index, and writing one.
  int chosen(const juce::String &parameterId) const;
  void write(const juce::String &parameterId, int value);

  /// The first macro nobody has made, or -1 when all eight are in use.
  int firstFree() const;

  juce::AudioProcessorValueTreeState &apvts;

  std::array<Strip, (size_t)params::kNumMacros> strips;
  juce::TextButton addButton, closeButton;

  /// Which macros are showing, in the order they are drawn.
  std::vector<int> showing;

  /// The card the strips sit on. A click outside it dismisses the panel.
  juce::Rectangle<int> card;

  /// Where the column names go, worked out with the columns themselves. Row,
  /// scope, curve, amount and percent.
  std::array<juce::Rectangle<int>, 5> columnLabel{};

  /// Where the line explaining macros goes while none is made.
  juce::Rectangle<int> emptyMessage;
};

} // namespace ovt::ui
