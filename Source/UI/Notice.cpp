#include "Notice.h"

#include <cmath>

#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

namespace {
constexpr int kCardWidth = 380;
constexpr int kPadding = 16;
constexpr int kTitleHeight = 22;
constexpr int kMessageGap = 6;
constexpr int kButtonHeight = 26;
constexpr int kButtonGap = 8;
constexpr int kSecondaryWidth = 80;

/// The message laid out at the width it has, in the face and colour it is
/// drawn in. The same layout is measured for the card's height and drawn, so
/// the two cannot break the lines in different places.
juce::TextLayout layoutOf(const juce::String &text, int width) {
  juce::AttributedString s;
  s.append(text, makeFont(12.0f, false), colours::textDim);
  s.setWordWrap(juce::AttributedString::byWord);

  juce::TextLayout layout;
  layout.createLayout(s, (float)width);
  return layout;
}
} // namespace

Notice::Notice() {
  setInterceptsMouseClicks(true, true);
  setVisible(false);

  primary.onClick = [this] { answer(primaryAction); };
  secondary.onClick = [this] { answer(secondaryAction); };

  addAndMakeVisible(primary);
  addAndMakeVisible(secondary);
}

void Notice::show(const juce::String &title, const juce::String &text,
                  const juce::String &primaryText,
                  const juce::String &secondaryText,
                  std::function<void()> onPrimary,
                  std::function<void()> onSecondary) {
  heading = title;
  message = text;
  primary.setButtonText(primaryText);
  secondary.setButtonText(secondaryText);
  primaryAction = std::move(onPrimary);
  secondaryAction = std::move(onSecondary);

  // What a screen reader is told when the card comes up, since what it says is
  // painted rather than written into a label.
  setTitle(title);
  setDescription(text);

  setVisible(true);
  toFront(false);
  resized();
  repaint();
}

void Notice::answer(const std::function<void()> &action) {
  // Put away first, so an answer that opens something else is not covered by
  // the question it answered.
  setVisible(false);

  if (action != nullptr)
    action();
}

void Notice::resized() {
  const auto textWidth = kCardWidth - 2 * kPadding;
  const auto height = kPadding + kTitleHeight + kMessageGap +
                      (int)std::ceil(layoutOf(message, textWidth).getHeight()) +
                      kPadding + kButtonHeight + kPadding;

  card = getLocalBounds().withSizeKeepingCentre(kCardWidth, height);

  auto buttons = card.reduced(kPadding).removeFromBottom(kButtonHeight);
  secondary.setBounds(buttons.removeFromRight(kSecondaryWidth));
  buttons.removeFromRight(kButtonGap);

  // As wide as its words want, which differ between the two questions.
  const auto primaryWidth =
      juce::jmin(buttons.getWidth(),
                 juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                     makeFont(13.0f, true), primary.getButtonText())) +
                     2 * kPadding);
  primary.setBounds(buttons.removeFromRight(primaryWidth));
}

void Notice::paint(juce::Graphics &g) {
  // As MACROS dims it, so the mixer is still there underneath.
  g.fillAll(juce::Colours::black.withAlpha(0.62f));

  g.setColour(colours::panel);
  g.fillRoundedRectangle(card.toFloat(), 6.0f);

  g.setColour(colours::outline);
  g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 6.0f, 1.0f);

  auto body = card.reduced(kPadding);

  g.setColour(colours::text);
  g.setFont(makeFont(13.0f, true));
  g.drawText(heading, body.removeFromTop(kTitleHeight),
             juce::Justification::centredLeft, false);

  body.removeFromTop(kMessageGap);
  body.removeFromBottom(kButtonHeight + kPadding);

  layoutOf(message, body.getWidth()).draw(g, body.toFloat());
}

} // namespace ovt::ui
