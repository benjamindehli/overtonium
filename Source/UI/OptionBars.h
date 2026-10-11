#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ovt::ui {

/// A bar that opens between the top bar and the mixer.
///
/// For the settings a player changes while playing and wants to see at a
/// glance, which in a menu are hidden until it is opened and take two or
/// three clicks to change. One row high and drawn the way the top bar is, so
/// it reads as more of the same panel rather than as a window laid over it.
/// It takes its height from the mixer, which scrolls, so opening one never
/// costs a channel.
class OptionBar : public juce::Component {
public:
  /// One row of the top bar with the padding above and below it.
  static constexpr int kHeight = 66;

  explicit OptionBar(juce::String name);

  void paint(juce::Graphics &) override;

protected:
  /// Where the controls go, right of the name.
  juce::Rectangle<int> controlArea() const;

private:
  juce::String name;
};

/// Voices, legato, one voice per key, pitch bend range and expression.
class PlayingBar : public OptionBar {
public:
  PlayingBar();
};

/// Temperament, root and reference pitch, and under Adaptive what it is doing
/// to the chord being held.
class TuningBar : public OptionBar {
public:
  TuningBar();
};

} // namespace ovt::ui
