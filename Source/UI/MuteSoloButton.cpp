#include "MuteSoloButton.h"

#include "../PluginParameters.h"

namespace ovt::ui {

namespace {
constexpr int kClearMutes = 1;
constexpr int kClearSolos = 2;

/// "Clear all solos (2)", or just the words when there are none to clear.
juce::String entry(const juce::String &text, int count) {
  return count > 0 ? text + " (" + juce::String(count) + ")" : text;
}
} // namespace

MuteSoloButton::MuteSoloButton(juce::AudioProcessorValueTreeState &state,
                              const juce::String &label)
    : juce::TextButton(label), apvts(state) {
  setClickingTogglesState(true);
}

juce::PopupMenu MuteSoloButton::buildMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  const auto muted = params::channelsSwitchedOn(apvts, params::muteSuffix);
  const auto soloed = params::channelsSwitchedOn(apvts, params::soloSuffix);

  // Greyed out rather than absent when there is nothing to clear, so the menu
  // is the same shape every time and also answers the question the player
  // opened it to ask: is anything soloed at all.
  m.addItem(kClearMutes, entry("Clear all mutes", muted), muted > 0);
  m.addItem(kClearSolos, entry("Clear all solos", soloed), soloed > 0);

  return m;
}

void MuteSoloButton::mouseDown(const juce::MouseEvent &e) {
  if (!e.mods.isPopupMenu()) {
    juce::TextButton::mouseDown(e);
    return;
  }

  auto menu = buildMenu();

  menu.showMenuAsync(
      juce::PopupMenu::Options()
          .withTargetComponent(this)
          .withStandardItemHeight(22),
      [this](int result) {
        if (result == kClearMutes)
          params::clearChannelSwitch(apvts, params::muteSuffix);
        else if (result == kClearSolos)
          params::clearChannelSwitch(apvts, params::soloSuffix);
      });
}

} // namespace ovt::ui
