#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginParameters.h"
#include "../dsp/AdaptiveSnapshot.h"
#include "ChannelStrip.h"
#include "LearnMenu.h"
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
  /// One row of the top bar with the padding above and below it, which is how
  /// tall a bar is when everything in it fits across.
  static constexpr int kHeight = 66;

  /// How tall the bar is at a width. Its groups wrap onto further rows when
  /// they do not fit across, as the top bar's do, each row as tall as one of
  /// the top bar's.
  int heightForWidth(int width) const;

  /// Told when something shown or hidden may have changed how many rows the
  /// bar needs, so whatever lays it out can ask again.
  std::function<void()> onHeightMayChange;

  /// A right-click on a knob in the bar, carrying its parameter's id. As
  /// TopBar::onLearnRequested: what the menu offers is the editor's to say.
  std::function<void(const juce::String &)> onLearnRequested;

  explicit OptionBar(juce::String name);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// Heard from every control in the bar as well as from the bar itself,
  /// since a right-click on a knob goes to the knob and nowhere else.
  void mouseDown(const juce::MouseEvent &) override;

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
    /// Whether it is there at all. A group of nothing shown takes no room.
    bool shown = true;
    /// The line of its group it stands on, where its caption goes. Set by
    /// resized.
    juce::Rectangle<int> line{};
  };

  /// Adds a framed group, laid out left to right after the ones before it,
  /// and returns its index for setGroupShown.
  int addGroup(std::vector<Item>);

  /// Shows or hides a group, closing the gap it leaves.
  void setGroupShown(int group, bool shown);

  /// Shows or hides one control within its group, the group closing up round
  /// it.
  void setItemShown(juce::Component &control, bool shown);

  /// Paints whatever stands behind a group's controls and captions, over its
  /// pocket. Nothing, unless a bar has something to put there.
  virtual void paintGroupGround(juce::Graphics &, int /*group*/,
                                juce::Rectangle<int> /*bounds*/) const {}

  /// What a control's caption is drawn in.
  virtual juce::Colour captionColour(const Item &) const;

  /// A group's controls, for a bar painting behind them.
  const std::vector<Item> &itemsOf(int group) const {
    return groups[(size_t)group].items;
  }

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

  /// How wide a group stands, counting only what is shown, or zero if
  /// nothing in it is.
  static int widthOf(const Group &);

  /// How many of a group's shown controls stand on each line within a width:
  /// all of them when they fit, and otherwise as few lines as they need with
  /// the controls shared out evenly between them, so twelve in two lines are
  /// six and six rather than eleven and one.
  static int perLine(const Group &, int room);

  /// Where each group goes at a width, row by row, wrapping as the top bar
  /// does. The same answer for heightForWidth and resized, so the two cannot
  /// disagree about how many rows there are.
  std::vector<juce::Rectangle<int>> placeGroups(juce::Rectangle<int> area,
                                                int *rows) const;
};

/// What the Playing bar shows of how the instrument is being played, read off
/// the processor by the editor's timer.
struct PlayingActivity {
  /// Voices sounding, release tails included.
  int voicesInUse = 0;
  bool sustain = false;

  /// -1 to 1 against the BEND range.
  float bend = 0.0f;

  /// 0 to 1.
  float wheel = 0.0f;
  float pressure = 0.0f;
};

/// Voices, legato, one voice per key, pitch bend range and expression, how
/// the instrument is being played, and the macros.
class PlayingBar : public OptionBar {
public:
  /// @param popupParent  where a macro knob shows its value while it is
  ///                     turned.
  PlayingBar(juce::AudioProcessorValueTreeState &,
             juce::Component &popupParent);

  void refresh() override;

  /// Repaints only what changed, so it can be handed every tick.
  void showActivity(const PlayingActivity &);

  void resized() override;

  const SegmentDisplay &inUseDisplay() const { return inUse; }
  const ScreenSwitch &sustainLamp() const { return sustain; }
  const ActivityNeedle &bendMeter() const { return bendNeedle; }
  const ActivityNeedle &wheelMeter() const { return wheelNeedle; }
  const ActivityNeedle &pressureMeter() const { return pressureNeedle; }
  LabelledKnob &macroKnob(int macro) { return *macros[(size_t)macro]; }

  /// A right-click on a macro knob, carrying which macro. The editor shows
  /// macroMenu for it.
  std::function<void(int macro)> onMacroMenuRequested;

  void mouseDown(const juce::MouseEvent &) override;

  /// What a right-click on a macro knob offers: for a macro that is made, MIDI
  /// Learn for its amount and a way into MACROS to change it, and for one that
  /// is not, only a way to make it, since a controller bound to a macro that
  /// drives nothing would do nothing.
  enum MacroMenuIds { kAssignMacro = 9101, kEditMacro = 9102 };
  static juce::PopupMenu macroMenu(int macro, bool made, const MidiLearn &,
                                   juce::RangedAudioParameter *amount);

  /// What a macro knob is captioned when it is aimed at this row: the row's
  /// name, shortened in the mixer's own words where it would not fit under
  /// the knob. Empty for no row.
  static juce::String macroCaption(int row);

  /// How wide each macro knob stands, room for an eight letter caption.
  static constexpr int kMacroWidth = 50;

private:
  juce::AudioProcessorValueTreeState &apvts;

  SegmentDisplay voices{{}, SegmentDisplay::Bars::Seven, 2};
  SegmentDisplay bend{"st", SegmentDisplay::Bars::Seven, 2};
  SegmentDisplay aftertouch{{}, SegmentDisplay::Bars::Fourteen, 8};
  SegmentDisplay slide{{}, SegmentDisplay::Bars::Fourteen, 6};
  GlowButton legato, noStack, mpe;

  SegmentDisplay inUse{{}, SegmentDisplay::Bars::Seven, 2};
  ScreenSwitch sustain;
  ActivityNeedle bendNeedle{colours::accent}, wheelNeedle{colours::accent},
      pressureNeedle{colours::accent};

  /// What it was last handed, so a needle can be put back where it belongs
  /// once the layout has given it a width to measure its travel on.
  PlayingActivity shown;

  void pushNeedles();

  /// Each macro's amount, the same parameter the slider in the MACROS panel
  /// drives, in the macro's colour.
  std::array<std::unique_ptr<LabelledKnob>, params::kNumMacros> macros;
  std::array<
      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
      params::kNumMacros>
      macroAttachments;

  /// Where a made macro's knob shows its amount while it is turned. An
  /// unmade one shows nothing, since its amount does nothing.
  juce::Component &popupParent;
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

  /// The drift display and the two lamps, for the tests.
  const SegmentDisplay &driftDisplay() const { return drift; }
  const ScreenSwitch &pureLamp() const { return pure; }
  GlowButton &seventhsSwitch() { return sevenths; }
  GlowButton &homeSwitch() { return home; }

  /// The word the temperament display shows for each temperament, in their
  /// order, sized for its eight cells.
  static const char *temperamentWord(int index);

private:
  juce::AudioProcessorValueTreeState &apvts;

  SegmentDisplay temperament{{}, SegmentDisplay::Bars::Fourteen, 8};
  SegmentDisplay root{{}, SegmentDisplay::Bars::Fourteen, 1};
  SegmentDisplay reference{"Hz", SegmentDisplay::Bars::Seven, 3};
  LabelledKnob retune{"RETUNE"};

  /// One display a pitch class, C to B, saying how far it sits from equal
  /// temperament. Lit for every one under a fixed temperament, and under
  /// Adaptive for the ones held, the rest showing where they would land.
  std::array<std::unique_ptr<SegmentDisplay>, 12> scale;

  /// What the engine last settled, which Adaptive's displays read.
  AdaptiveSnapshot chord;

  void showScale();

  /// A light ground behind each white key's display and its name, like the
  /// keys themselves, so which note is which reads at a glance.
  void paintGroupGround(juce::Graphics &, int group,
                        juce::Rectangle<int> bounds) const override;
  juce::Colour captionColour(const Item &) const override;

  /// Whether a control is one of the seven white keys' displays.
  bool isWhiteKey(const juce::Component *) const;

  int scaleGroup = -1;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      retuneAttachment;
  /// What Adaptive did with the held chord: PURE, lit in a screen like the
  /// gutter's switches while every interval in it is pure. An indicator, not
  /// a switch.
  ScreenSwitch pure;

  /// Whether Adaptive puts a seventh on 7:4 at all, a cap that lights while
  /// it does, like LEGATO and MPE.
  GlowButton sevenths;

  /// Whether Adaptive stays home rather than drifting, a cap captioned NO
  /// DRIFT.
  GlowButton home;

  /// How far the held chord sits from home, in whole commas.
  SegmentDisplay drift{{}, SegmentDisplay::Bars::Seven, 2};

  int adaptiveGroup = -1;
};

} // namespace ovt::ui
