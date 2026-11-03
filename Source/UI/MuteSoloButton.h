#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

/// The M or S on a channel strip and on the noise channel.
///
/// A left-click toggles through the parameter attachment. Option-click, or
/// Alt-click off the Mac, mutes or solos only this channel. A right-click
/// offers the same, and to clear every mute or every solo across the mixer,
/// and does not reach the strip's LINK menu.
class MuteSoloButton : public GlowButton {
public:
  /// @param parameterId  this channel's own switch
  /// @param suffix       params::muteSuffix or params::soloSuffix, which also
  ///                     names every other channel's switch
  MuteSoloButton(juce::AudioProcessorValueTreeState &state,
                 const juce::String &label, const juce::String &parameterId,
                 const char *suffix);

  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

  /// What the right-click opens, as data for the tests.
  juce::PopupMenu buildMenu();

  /// Does what the menu entry with this id says, as the menu does when one is
  /// picked. Zero, for a menu put away, does nothing. Public so a test can pick
  /// entries without opening the menu, which needs a window.
  void chooseFromMenu(int result);

  /// What Option-click does: this channel on and every other off, or every one
  /// off if this is already the only one on. One step in the undo history.
  void switchOnlyThis();

  /// The sentence a tooltip adds to say so, with the platform's name for the
  /// key.
  juce::String onlyThisHint() const;

private:
  /// Whether this is a solo button rather than a mute.
  bool isSolo() const;

  juce::AudioProcessorValueTreeState &apvts;
  juce::String id;
  const char *switchSuffix;

  /// Set while an Option-click is held, so the press that switched only this
  /// channel does not also toggle it when the button is let go.
  bool onlyThisPress = false;
};

} // namespace ovt::ui
