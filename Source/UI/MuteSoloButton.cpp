#include "MuteSoloButton.h"

#include <cstring>

#include "../PluginParameters.h"

namespace ovt::ui {

namespace {
constexpr int kClearMutes = 1;
constexpr int kClearSolos = 2;
constexpr int kOnlyThis = 3;

/// "Clear all solos (2)", or just the words when there are none to clear.
juce::String entry(const juce::String &text, int count) {
  return count > 0 ? text + " (" + juce::String(count) + ")" : text;
}
} // namespace

MuteSoloButton::MuteSoloButton(juce::AudioProcessorValueTreeState &state,
                               const juce::String &label,
                               const juce::String &parameterId,
                               const char *suffix)
    : apvts(state), id(parameterId), switchSuffix(suffix) {
  setButtonText(label);
  setClickingTogglesState(true);
}

bool MuteSoloButton::isSolo() const {
  // Compared by content, not by address, so a copy of the string still works.
  return std::strcmp(switchSuffix, params::soloSuffix) == 0;
}

juce::String MuteSoloButton::onlyThisHint() const {
#if JUCE_MAC
  const juce::String key = "Option";
#else
  const juce::String key = "Alt";
#endif

  return key + "-click to " + (isSolo() ? "solo" : "mute") +
         " only this channel.";
}

juce::PopupMenu MuteSoloButton::buildMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  const auto muted = params::channelsSwitchedOn(apvts, params::muteSuffix);
  const auto soloed = params::channelsSwitchedOn(apvts, params::soloSuffix);

  // Greyed out when this channel is already the only one, since there is then
  // nothing for it to do.
  auto *own = apvts.getParameter(id);
  const auto ownOn = own != nullptr && own->getValue() > 0.5f;
  const auto alreadyOnly = ownOn && (isSolo() ? soloed : muted) == 1;

  m.addItem(kOnlyThis,
            isSolo() ? "Solo only this channel" : "Mute only this channel",
            own != nullptr && !alreadyOnly);
  m.addSeparator();

  // Greyed out rather than absent when there is nothing to clear, so the menu
  // keeps its shape.
  m.addItem(kClearMutes, entry("Clear all mutes", muted), muted > 0);
  m.addItem(kClearSolos, entry("Clear all solos", soloed), soloed > 0);

  return m;
}

void MuteSoloButton::chooseFromMenu(int result) {
  if (result == kOnlyThis)
    params::switchOnlyChannel(apvts, switchSuffix, id);
  else if (result == kClearMutes)
    params::clearChannelSwitch(apvts, params::muteSuffix);
  else if (result == kClearSolos)
    params::clearChannelSwitch(apvts, params::soloSuffix);
}

void MuteSoloButton::switchOnlyThis() {
  auto *own = apvts.getParameter(id);

  if (own == nullptr)
    return;

  // Option-click on the one channel that is already alone undoes it, so the
  // same click goes in and out of hearing one channel.
  const auto alone = own->getValue() > 0.5f &&
                     params::channelsSwitchedOn(apvts, switchSuffix) == 1;

  if (alone)
    params::clearChannelSwitch(apvts, switchSuffix);
  else
    params::switchOnlyChannel(apvts, switchSuffix, id);
}

void MuteSoloButton::mouseDown(const juce::MouseEvent &e) {
  onlyThisPress = false;

  if (e.mods.isPopupMenu()) {
    auto menu = buildMenu();

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withStandardItemHeight(22),
                       [this](int result) { chooseFromMenu(result); });
    return;
  }

  // Not passed on, so the button is never pressed and letting go does not
  // toggle it back.
  if (e.mods.isAltDown()) {
    onlyThisPress = true;
    switchOnlyThis();
    return;
  }

  GlowButton::mouseDown(e);
}

void MuteSoloButton::mouseDrag(const juce::MouseEvent &e) {
  if (!onlyThisPress)
    GlowButton::mouseDrag(e);
}

void MuteSoloButton::mouseUp(const juce::MouseEvent &e) {
  if (onlyThisPress) {
    onlyThisPress = false;
    return;
  }

  GlowButton::mouseUp(e);
}

} // namespace ovt::ui
