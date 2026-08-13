#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "Theme.h"

namespace ovt::ui {

/// The master channel.
///
/// It carries the same controls in the same rows as the 32 partial strips, but
/// every one of them is relative: moving a knob here offsets that control on
/// all 32 channels at once and leaves whatever spread you shaped intact. It
/// sits between the row labels and the scrolling mixer, so it stays in view no
/// matter how far along the series you have scrolled.
class MasterStrip : public juce::Component, public juce::SettableTooltipClient {
public:
  MasterStrip(MacroTarget &target, juce::Component &popupParent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Lights the solo button while any partial is soloed, since clicking it is
  /// how you clear them.
  void setSoloActive(bool active);

private:
  void setUp(RelativeKnob &, Role);

  MacroTarget &macros;
  juce::Component &popupHost;

  std::array<RelativeKnob, kNumRoles> knobs;
  juce::TextButton muteButton{"M"}, soloButton{"S"};

  bool soloActive = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStrip)
};

} // namespace ovt::ui
