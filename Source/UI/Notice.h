#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// A question put to whoever is at the window, as a card over the dimmed
/// mixer the way MACROS is, with two answers.
///
/// Drawn in the window rather than opened as a window of its own. A plugin
/// cannot run a modal loop, and a host scanning its plugin folder opens an
/// editor with nobody there to answer, so a dialog that waited would stop the
/// scan. This waits for nothing: until it is answered it sits over the mixer
/// and the rest of the instrument carries on.
///
/// Answered only by one of its buttons. A click on the dim beside the card
/// does nothing, since a question put away by accident would count as one
/// somebody never meant to answer.
class Notice final : public juce::Component {
public:
  Notice();

  /// Puts the question up, replacing any already showing.
  void show(const juce::String &title, const juce::String &message,
            const juce::String &primary, const juce::String &secondary,
            std::function<void()> onPrimary, std::function<void()> onSecondary);

  bool isShowing() const noexcept { return isVisible(); }

  const juce::String &getMessage() const noexcept { return message; }

  /// The two answers, for the tests, which press them.
  juce::TextButton &primaryButton() noexcept { return primary; }
  juce::TextButton &secondaryButton() noexcept { return secondary; }

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  juce::String heading, message;
  juce::TextButton primary, secondary;
  std::function<void()> primaryAction, secondaryAction;
  juce::Rectangle<int> card;

  void answer(const std::function<void()> &action);
};

} // namespace ovt::ui
