#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// A question with two answers, as a card over the dimmed mixer.
///
/// Drawn in the window rather than as a modal dialog, which a plugin cannot
/// run and which would stall a host scanning plugins. Answered only by its
/// buttons, not by a click beside the card.
class Notice final : public juce::Component {
public:
  Notice();

  /// Puts the question up, replacing any already showing.
  void show(const juce::String &title, const juce::String &message,
            const juce::String &primary, const juce::String &secondary,
            std::function<void()> onPrimary, std::function<void()> onSecondary);

  bool isShowing() const noexcept { return isVisible(); }

  /// The message showing, for the tests.
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

  /// Puts the card away and runs the chosen answer.
  void answer(const std::function<void()> &action);
};

} // namespace ovt::ui
