#include "OptionBars.h"

#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

namespace {
/// The same margins the top bar keeps, so the controls in a bar line up under
/// the ones above them.
constexpr int kBarMargin = 12;
constexpr int kBarPadY = 6;

/// Room for the bar's name, the way the wordmark has room in the top bar.
constexpr int kNameWidth = 62;
} // namespace

OptionBar::OptionBar(juce::String nameToShow) : name(std::move(nameToShow)) {
  // Shouted on the panel like everything else, and said normally to a screen
  // reader.
  setTitle(name.substring(0, 1) + name.substring(1).toLowerCase());
}

juce::Rectangle<int> OptionBar::controlArea() const {
  return getLocalBounds()
      .reduced(kBarMargin, kBarPadY)
      .withTrimmedLeft(kNameWidth);
}

void OptionBar::paint(juce::Graphics &g) {
  // A shade under the top bar, so the two read as one panel in two layers
  // rather than as one bar that has grown.
  g.setColour(colours::panel.darker(0.12f));
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  g.setColour(colours::textDim);
  g.setFont(makeFont(10.0f, true));
  g.drawText(
      name,
      getLocalBounds().reduced(kBarMargin, kBarPadY).withWidth(kNameWidth),
      juce::Justification::centredLeft, false);
}

PlayingBar::PlayingBar() : OptionBar("PLAYING") {}

TuningBar::TuningBar() : OptionBar("TUNING") {}

} // namespace ovt::ui
