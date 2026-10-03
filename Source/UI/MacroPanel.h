#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginParameters.h"
#include "Theme.h"

namespace ovt::ui {

/// Where macros are made, over the mixer rather than squeezed beside it.
///
/// A macro needs five things said about it and there are eight of them, which
/// is more than any edge of this window has room for. So this covers the
/// mixer while it is up and goes away when you click off it, which is a
/// dialog in all but name: the one place this instrument has one, because the
/// alternative was a band of permanent chrome over the thing being modulated.
///
/// The eight are a pool rather than a count. See params::kNumMacros for why a
/// plugin cannot simply grow one when asked.
class MacroPanel final : public juce::Component {
public:
  explicit MacroPanel(juce::AudioProcessorValueTreeState &state);

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;

  /// Reads the parameters again and shows a strip for every macro that has
  /// been made. Called when the panel opens and on the editor's timer, so a
  /// host moving a macro's row is followed rather than ignored.
  void refresh();

  /// Asked to close, either by the button or by a click on the dim.
  std::function<void()> onDismiss;

  /// A right-click on a macro's amount, carrying that parameter's id. As
  /// TopBar::onLearnRequested: what the menu offers is the editor's to say.
  std::function<void(const juce::String &)> onLearnRequested;

  /// How many macros are made, for the button that opens this to say so.
  static int madeCount(const juce::AudioProcessorValueTreeState &);

private:
  /// A fader that hands its right-click on rather than dragging.
  ///
  /// The same reasoning as ui::LinkableSlider: left to itself the slider
  /// opens a drag gesture it never closes, because the mouse-up goes to the
  /// menu instead of back here.
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

    /// What the amount comes to in the row's own units, which is the only
    /// form of it anybody can act on: an amount of 0.25 means nothing until
    /// it says what 0.25 of that row is.
    juce::Label reading;
    AmountSlider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attachment;
  };

  void buildStrip(int macro);
  void choose(int macro, const juce::String &parameterId, const char *title,
              int count, const std::function<juce::String(int)> &nameOf,
              juce::Component *under, int firstValue = 0);

  /// Puts the amount into the row's own units under the fader, and borrows
  /// the row's own feel for the fader while it is there.
  void showReading(int macro);

  int chosen(const juce::String &parameterId) const;
  void write(const juce::String &parameterId, int value);

  /// The first macro nobody has made, or -1 when all eight are in use.
  int firstFree() const;

  juce::AudioProcessorValueTreeState &apvts;

  std::array<Strip, (size_t)params::kNumMacros> strips;
  juce::TextButton addButton, closeButton;

  /// Which macros are showing, in the order they are drawn.
  std::vector<int> showing;

  /// The card the strips sit on, which is what a click has to land inside to
  /// count as a click on the panel rather than on the dim behind it.
  juce::Rectangle<int> card;

  /// Where the column names go, worked out with the columns themselves so
  /// the two cannot drift apart. Row, scope, curve and amount.
  std::array<juce::Rectangle<int>, 4> columnLabel{};

  /// The band the line about what a macro is sits in, which is the space a
  /// first macro will take once there is one.
  juce::Rectangle<int> emptyMessage;
};

} // namespace ovt::ui
