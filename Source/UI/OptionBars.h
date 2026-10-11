#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelStrip.h"
#include "Theme.h"

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
  void resized() override;

  /// Reads every value off the parameters again. The editor calls it on its
  /// housekeeping tick while the bar is open, since a host, a preset or the
  /// Settings menu can move any of them.
  virtual void refresh() = 0;

protected:
  /// One control in a group, how wide it stands, and the caption under it,
  /// which may be empty.
  struct Item {
    juce::Component *control;
    int width;
    juce::String caption;
  };

  /// Adds a framed group, laid out left to right after the ones before it,
  /// and returns its index for setGroupShown.
  int addGroup(std::vector<Item>);

  /// Shows or hides a group, closing the gap it leaves.
  void setGroupShown(int group, bool shown);

  /// Opens one of the shared Settings lists under a control and carries out
  /// what is chosen.
  void showList(juce::Component &anchor, const juce::PopupMenu &list,
                juce::AudioProcessorValueTreeState &);

  /// Readies a switch: a word on a cap that lights while it is on.
  static void styleSwitch(GlowButton &, const juce::String &word,
                          const juce::String &title,
                          const juce::String &tooltip);

private:
  juce::String name;

  struct Group {
    std::vector<Item> items;
    bool shown = true;
  };

  std::vector<Group> groups;
  std::vector<juce::Rectangle<int>> groupBounds;
};

/// Voices, legato, one voice per key, pitch bend range and expression.
class PlayingBar : public OptionBar {
public:
  explicit PlayingBar(juce::AudioProcessorValueTreeState &);

  void refresh() override;

private:
  juce::AudioProcessorValueTreeState &apvts;

  SegmentDisplay voices{{}}, bend{"st"};
  SegmentDisplay aftertouch{{}, SegmentDisplay::Bars::Fourteen, 8};
  SegmentDisplay slide{{}, SegmentDisplay::Bars::Fourteen, 6};
  GlowButton legato, onePerKey, mpe;
};

/// Temperament, root and reference pitch, and under Adaptive how fast it
/// retunes and what it is doing to the chord being held.
class TuningBar : public OptionBar {
public:
  explicit TuningBar(juce::AudioProcessorValueTreeState &);

  void refresh() override;

  /// The word the temperament display shows for each temperament, in their
  /// order, sized for its nine cells.
  static const char *temperamentWord(int index);

private:
  juce::AudioProcessorValueTreeState &apvts;

  SegmentDisplay temperament{{}, SegmentDisplay::Bars::Fourteen, 9};
  SegmentDisplay root{{}, SegmentDisplay::Bars::Fourteen, 2};
  SegmentDisplay reference{"Hz"};
  SegmentDisplay retune{{}, SegmentDisplay::Bars::Fourteen, 6};

  int retuneGroup = -1;
};

} // namespace ovt::ui
