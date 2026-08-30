#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// The M or S on a channel strip, and on the noise channel beside it.
///
/// A left-click is an ordinary toggle, which the parameter attachment handles.
/// A right-click offers to clear either switch across the whole mixer, because
/// thirty-three strips is a great many places for a solo to be left on and
/// finding it by eye means reading thirty-three pairs of small buttons.
///
/// The buttons rather than the strip, since a strip already answers a
/// right-click with the LINK menu and these swallow that click on their way
/// past. Right-clicking a switch acts on switches, right-clicking anywhere
/// else in the mixer still reaches LINK.
class MuteSoloButton : public juce::TextButton {
public:
  MuteSoloButton(juce::AudioProcessorValueTreeState &state,
                 const juce::String &label);

  void mouseDown(const juce::MouseEvent &) override;

  /// What the right-click opens. Exposed so a test can read the entries
  /// without a window to put them in.
  juce::PopupMenu buildMenu();

private:
  juce::AudioProcessorValueTreeState &apvts;
};

} // namespace ovt::ui
