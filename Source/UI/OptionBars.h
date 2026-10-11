#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/AdaptiveSnapshot.h"
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
    /// Fills its group's height rather than standing on the dial line, for
    /// something with more to say than one row of segments.
    bool fills = false;
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

  SegmentDisplay voices{{}, SegmentDisplay::Bars::Seven, 2};
  SegmentDisplay bend{"st", SegmentDisplay::Bars::Seven, 2};
  SegmentDisplay aftertouch{{}, SegmentDisplay::Bars::Fourteen, 8};
  SegmentDisplay slide{{}, SegmentDisplay::Bars::Fourteen, 6};
  GlowButton legato, onePerKey, mpe;
};

/// What Adaptive did to the held chord, worked out from what the engine
/// published and laid out as the Tuning bar shows it.
struct AdaptiveReading {
  /// The chord as harmonics of one note, such as 4:5:6:7, or empty when it is
  /// not one.
  juce::String harmonics;

  /// Nothing held, nothing beats, all pure, or the interval given up.
  juce::String verdict;

  /// The worst interval given up, such as G-D 21c WIDE, or empty.
  juce::String givenUp;

  /// The harmonic seventh's root, such as C, or empty for none.
  juce::String seventhOn;

  /// The chord's average version, in commas from the middle ones.
  int drift = 0;

  /// What a screen reader is told.
  juce::String spoken() const;

  static AdaptiveReading of(const AdaptiveSnapshot &);
};

/// The readouts, in a screen.
class AdaptiveReadout : public juce::Component {
public:
  void show(const AdaptiveReading &);
  const AdaptiveReading &shown() const noexcept { return reading; }

  void paint(juce::Graphics &) override;

private:
  AdaptiveReading reading = AdaptiveReading::of({});
};

/// Temperament, root and reference pitch, and under Adaptive how fast it
/// retunes and what it is doing to the chord being held.
class TuningBar : public OptionBar {
public:
  /// @param popupParent  where the Retune knob shows its value while it is
  ///                     turned, as the top bar's knobs do.
  TuningBar(juce::AudioProcessorValueTreeState &, juce::Component &popupParent);

  void refresh() override;

  /// The held chord as the engine last settled it. Kept while the bar is
  /// closed too, so it opens on the chord being held.
  void showChord(const AdaptiveSnapshot &);

  /// The cents a pitch class sits from equal temperament, as its display
  /// shows them, and whether it is lit. For the tests.
  const SegmentDisplay &scaleDisplay(int pitchClass) const {
    return *scale[(size_t)pitchClass];
  }

  /// The drift display, for the tests.
  const SegmentDisplay &driftDisplay() const { return drift; }

  /// The word the temperament display shows for each temperament, in their
  /// order, sized for its nine cells.
  static const char *temperamentWord(int index);

private:
  juce::AudioProcessorValueTreeState &apvts;

  SegmentDisplay temperament{{}, SegmentDisplay::Bars::Fourteen, 9};
  SegmentDisplay root{{}, SegmentDisplay::Bars::Fourteen, 2};
  SegmentDisplay reference{"Hz", SegmentDisplay::Bars::Seven, 3};
  LabelledKnob retune{"RETUNE"};

  /// One display a pitch class, C to B, saying how far it sits from equal
  /// temperament. Lit for every one under a fixed temperament, and under
  /// Adaptive for the ones held, the rest showing where they would land.
  std::array<std::unique_ptr<SegmentDisplay>, 12> scale;

  /// What the engine last settled, which Adaptive's displays read.
  AdaptiveSnapshot chord;

  void showScale();
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      retuneAttachment;
  AdaptiveReadout readout;

  /// How far the held chord sits from home, in whole commas.
  SegmentDisplay drift{{}, SegmentDisplay::Bars::Seven, 2};

  int retuneGroup = -1, readoutGroup = -1;
};

} // namespace ovt::ui
