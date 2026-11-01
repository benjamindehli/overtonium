#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

/// The M or S on a channel strip and on the noise channel.
///
/// A left-click toggles through the parameter attachment. A right-click offers
/// to clear every mute or every solo across the mixer, and does not reach the
/// strip's LINK menu.
class MuteSoloButton : public GlowButton {
public:
  MuteSoloButton(juce::AudioProcessorValueTreeState &state,
                 const juce::String &label);

  void mouseDown(const juce::MouseEvent &) override;

  /// What the right-click opens, as data for the tests.
  juce::PopupMenu buildMenu();

private:
  juce::AudioProcessorValueTreeState &apvts;
};

} // namespace ovt::ui
