#include "MasterStrip.h"

#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }
} // namespace

MasterStrip::MasterStrip(MacroTarget &target, juce::Component &popupParent)
    : macros(target), popupHost(popupParent) {
  for (int r = 0; r < kNumRoles; ++r)
    setUp(knobs[(size_t)r], (Role)r);

  // The level row is a fader on the partial strips, so it is a fader here too.
  // Rows have to line up for the gutter labels to mean anything.
  auto &level = knobs[(size_t)Role::Volume];
  level.setSliderStyle(juce::Slider::LinearVertical);
  level.setColour(juce::Slider::trackColourId, colours::accent);
  level.getProperties().set("meteredGroove", true);
  level.setTooltip("Moves the level of all 32 channels, keeping their spread. "
                   "The track shows the finished output.");

  addAndMakeVisible(meter);
  meter.toBack(); // the fader cap has to draw over it

  muteButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::buttonOnColourId, colours::muteOn);
  muteButton.setTooltip("Mute or unmute all 32 channels");
  muteButton.onClick = [this] {
    macros.setAllMutes(muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  // Soloing everything would be the same as soloing nothing, so the master S
  // is the useful half of the pair: it clears whatever is soloed.
  soloButton.setClickingTogglesState(false);
  soloButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  soloButton.setTooltip("Clear every solo");
  soloButton.onClick = [this] { macros.clearAllSolos(); };
  addAndMakeVisible(soloButton);

  setTooltip(
      "Master channel. Every control here is relative: it offsets all 32 "
      "channels and keeps the shape you dialled in.");
}

void MasterStrip::setUp(RelativeKnob &k, Role role) {
  k.setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
  k.setPopupDisplayEnabled(true, true, &popupHost);
  k.setTooltip(juce::String("Moves ") + roleName(role) +
               " on all 32 channels, keeping their spread");

  k.onRelativeStart = [this, role] { macros.macroStarted(role); };
  k.onRelativeDelta = [this, role](float d) { macros.macroMoved(role, d); };
  k.onRelativeEnd = [this, role] { macros.macroEnded(role); };

  addAndMakeVisible(k);
}

void MasterStrip::setSoloActive(bool active) {
  if (soloActive == active)
    return;

  soloActive = active;
  soloButton.setToggleState(active, juce::dontSendNotification);
}

void MasterStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  g.setColour(colours::panel.brighter(0.06f));
  g.fillRect(bounds);

  const auto rows = layoutRows(bounds.reduced(2, 4));
  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colours::accent);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  g.setColour(colours::text);
  g.setFont(makeFont(12.0f, true));
  g.drawText("ALL", header.removeFromTop(14), juce::Justification::centred,
             false);

  g.setColour(colours::accent.withAlpha(0.85f));
  g.setFont(makeFont(9.0f));
  g.drawText("master", header, juce::Justification::centred, false);

  g.setColour(colours::outline.withAlpha(0.7f));
  for (auto r : {Row::PitchModHeading, Row::EnvHeading, Row::AmpModHeading,
                 Row::OutputHeading}) {
    const auto row = rows[rowIndex(r)];
    g.fillRect(row.getX(), row.getY() + row.getHeight() / 2, row.getWidth(), 1);
  }

  // A firmer edge than the hairlines between partial strips, so the master
  // reads as separate from the series rather than as harmonic zero.
  g.setColour(colours::outline);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void MasterStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  for (int r = 0; r < kNumRoles; ++r) {
    const auto row = rowForRole((Role)r);
    auto area = rows[rowIndex(row)];

    knobs[(size_t)r].setBounds(row == Row::Fader ? area.reduced(2, 1)
                                                 : area.reduced(1));
  }

  // Shares its rectangle with the level control, as on the partial strips.
  meter.setBounds(rows[rowIndex(Row::Fader)].reduced(2, 1));

  auto ms = rows[rowIndex(Row::MuteSolo)];
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2).reduced(1));
  soloButton.setBounds(ms.reduced(1));
}

} // namespace ovt::ui
