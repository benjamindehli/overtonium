#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// Copy and paste for one partial, at the top of the menu a right-click on a
/// channel's number opens. See ovt::channelcopy for what is copied.
namespace channelmenu {

/// Item ids, clear of the LINK menu, the learn items at 9001 and the glide
/// items at 9101.
enum MenuIds { kCopy = 9201, kPaste = 9202, kPasteSelected = 9203 };

/// What the menu offers for the channel that was clicked, and where the
/// choice goes.
struct Offer {
  /// 0 to kNumHarmonics - 1.
  int channel = 0;

  /// Whether the clipboard holds a channel.
  bool canPaste = false;

  /// Whether any channel is selected, for "Paste to selected channels".
  bool hasSelection = false;

  /// Called with kCopy, kPaste or kPasteSelected.
  std::function<void(int)> choose;
};

/// Adds the items and a separator under them.
inline void appendItems(juce::PopupMenu &menu, const Offer &offer) {
  const auto number = juce::String(offer.channel + 1);

  menu.addSectionHeader("Channel " + number);
  menu.addItem(kCopy, "Copy channel " + number);
  menu.addItem(kPaste, "Paste to channel " + number, offer.canPaste);
  menu.addItem(kPasteSelected, "Paste to selected channels",
               offer.canPaste && offer.hasSelection);
  menu.addSeparator();
}

/// Hands the choice on, if it was one of these.
///
/// @return true when the id belonged here, so the caller stops looking.
inline bool applyChoice(int result, const Offer &offer) {
  if (result != kCopy && result != kPaste && result != kPasteSelected)
    return false;

  if (offer.choose != nullptr)
    offer.choose(result);

  return true;
}

} // namespace channelmenu
} // namespace ovt::ui
