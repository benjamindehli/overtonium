#pragma once

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../MidiLearn.h"
#include "ChannelStrip.h"
#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

/// The stereo output meter.
///
/// Horizontal and split, because the two channels only differ once the panning
/// is dialled in and a summed meter would hide exactly that. Carries a peak
/// hold, since the thing you most want from an output meter is what it hit a
/// moment ago rather than what it is doing right now.
///
/// Segmented, like the channel meters, and for the same two reasons: it reads
/// at a glance, and it only asks to be redrawn when a lamp changes rather than
/// on every frame in which the level moves at all.
class StereoOutputMeter : public juce::Component {
public:
  StereoOutputMeter() { setInterceptsMouseClicks(false, false); }

  /// The strip along the bottom that carries the decibel marks rather than
  /// any lamps.
  static constexpr int kScaleHeight = 4;

  /// Where the two bars actually are, which is not the whole of this: the
  /// scale runs underneath them. The fader that lies over the meter reads it
  /// from here rather than measuring the same thing again, since a cap four
  /// pixels below the lamps it is supposed to be standing on reads as a
  /// control that has come loose.
  juce::Rectangle<int> barBounds() const {
    return getLocalBounds().withTrimmedBottom(kScaleHeight);
  }

  /// @param l,r  linear peaks from the audio thread.
  void push(float l, float r);

  void paint(juce::Graphics &) override;

private:
  /// How many lamps fit across the meter.
  int segments() const;

  struct Bar {
    float displayed = 0.0f;
    float peak = 0.0f;
    int hold = 0;

    /// Lamps alight, and the one holding the peak, or -1 when there is none.
    /// Kept rather than derived so they can hold still while the level wobbles
    /// across a boundary.
    int lit = 0;
    int peakLamp = -1;

    /// @returns true when a lamp changed, which is the only time redrawing
    /// would show anything.
    bool advance(float level, int count);
  };

  void paintBar(juce::Graphics &, juce::Rectangle<float>, const Bar &,
                int count) const;

  Bar left, right;
};

class TopBar : public juce::Component {
public:
  TopBar(juce::AudioProcessorValueTreeState &apvts,
         juce::Component &popupParent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// A right-click on one of the bar's own controls, carrying the parameter
  /// it moves. The bar puts up no menu itself: what it offers is the editor's
  /// to decide, the same way the gutter hands back the LINK button's anchor.
  ///
  /// Learn items only, with none of the LINK settings the mixer's menu
  /// carries, since what LINK does is gang channel strips and none of these
  /// is one.
  std::function<void(const juce::String &)> onLearnRequested;

  /// Fired when MACROS is clicked, which puts the macro panel up over the
  /// mixer. On the bar rather than in the gutter because it opens something,
  /// which is what the two buttons beside it do. See ui::MacroPanel.
  std::function<void()> onMacrosClicked;

  /// Fired when one of the three tools is chosen from the tool menu.
  std::function<void(PointerTool)> onToolChosen;

  /// Lights MACROS while its panel is up, and says how many are made.
  void setMacrosOn(bool);
  void setMacroCount(int);

  void mouseDown(const juce::MouseEvent &) override;

  /// Puts a newer release in the credit line under the wordmark, where the
  /// tagline usually sits, and makes it clickable. Called with an empty
  /// version to say nothing, which is the state it starts in and stays in for
  /// anyone who has not turned the check on.
  void setUpdateAvailable(const juce::String &version, const juce::String &url);

  /// Offers the update check in the credit line, once, instead of putting a
  /// dialog in front of someone who has not asked for one. A plugin cannot
  /// tell whether it is being opened by a person or by a host scanning its
  /// folder, and a modal in the second case stops the scan.
  void setUpdateOffer(bool);

  std::function<void()> onUpdateOfferAccepted;

  void mouseUp(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;

  // ---- callbacks the editor fills in ----
  std::function<void(int)> onPresetChosen;
  std::function<void(juce::File)> onUserPresetChosen;
  std::function<void(juce::String)> onSaveUserPreset;
  std::function<void()> onCopyFactoryCode;
  std::function<void(float)> onZoomChanged;

  /// Sets the window back to the size that shows the whole mixer.
  ///
  /// The window remembers what it was left at, which is what a window should
  /// do and is also a one-way trip: drag it narrow, close it, and every
  /// session after that opens narrow. This is the way back.
  std::function<void()> onFitAllChannels;

  /// History. It lives at the head of the Settings menu because that is the
  /// only menu the window has, and because a keyboard shortcut cannot be
  /// relied on: most hosts keep Cmd-Z for themselves.
  std::function<void()> onUndo, onRedo;

  /// The update-check setting, read when the menu opens and written when it is
  /// toggled. The editor owns the value, since it is the editor that saves it
  /// with the session and starts the check.
  std::function<bool()> isUpdateCheckAllowed;
  std::function<void(bool)> onUpdateCheckToggled;
  std::function<bool()> canUndo, canRedo;

  /// Shown on the preset button, so the bar says what is loaded.
  void setPresetName(const juce::String &);
  juce::String getPresetName() const;

  /// What the character button is showing. Written from the parameter by
  /// updatePanelReadouts and read back here, which is the only way to check a
  /// button that cannot be clicked without a window to put its menu in.
  juce::String getCharacterName() const {
    return characterButton.getButtonText();
  }

  /// Whether the character button is lit, and in what. Read back by the tests
  /// for the same reason the name is: a button is otherwise nothing but paint.
  bool isCharacterLit() const { return characterButton.getToggleState(); }

  juce::Colour getCharacterColour() const {
    return characterButton.findColour(juce::TextButton::textColourOnId);
  }

  bool isLinkEnabled() const { return linkOn; }

  /// Latched by the editor at the start of each LINK drag.
  LinkScope getLinkScope() const { return scope; }
  LinkCurve getLinkCurve() const { return curve; }

  void setLinkEnabled(bool);
  void setLinkScope(LinkScope);
  void setLinkCurve(LinkCurve);

  std::function<void()> onLinkSettingsChanged;

  /// The switch, the scope and the curve, in one menu.
  ///
  /// Shared by the button in the bar and by a right-click anywhere on a strip,
  /// so there is one list rather than two that can drift apart.
  ///
  /// @param anchor       what to hang the menu off, or nullptr to put it
  ///                      under the pointer.
  /// @param parameterId   what a right-click landed on, empty when the menu
  ///                      came from the LINK button instead.
  /// @param map           where a learned binding goes. Passed in rather than
  ///                      reached for, since the bar is given its state tree
  ///                      and not the processor behind it.
  /// No default for the tool. It had one, and both callers left it out, so
  /// the menu built itself as though the plain pointer were always chosen:
  /// a permanent tick on Pointer and LINK's two lists greyed out for good.
  void showLinkMenu(juce::Component *anchor, const juce::String &parameterId,
                    MidiLearn *map, PointerTool tool);

  /// The bar reflows onto further rows when the groups no longer fit across
  /// one, so nothing has to be dropped on a narrow window. Static because the
  /// editor has to know the height before it can hand the bar its bounds.
  static int heightForWidth(int width);

  /// Narrowest window the bar can lay out without hiding a group. The editor
  /// takes this as a floor, since refusing to shrink further is better than
  /// quietly dropping controls.
  static int minimumWidth();

  /// Refreshes what the bar reads back from the parameters rather than from a
  /// slider: the two converter readouts and the character the oscillators are
  /// set to. The host rate has to be passed in because "leave it alone" is a
  /// setting whose value only the processor knows.
  void updatePanelReadouts(double hostSampleRate);
  void setOutputLevels(float l, float r) { meter.push(l, r); }
  void setZoomChoice(float zoom);

private:
  /// The release the credit line is currently advertising, and where it sends
  /// someone who clicks. Empty when there is nothing to say.
  juce::String updateVersion, updateUrl;
  bool offeringUpdateCheck = false;

  /// Where that text landed, so a click can be tested against it. Filled in
  /// while painting, since that is where the block is worked out.
  juce::Rectangle<int> updateBounds;

  void paintCreditLine(juce::Graphics &, juce::Rectangle<int>);
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  void styleToggle(GlowButton &, const juce::String &text,
                   const juce::String &tooltip);

  /// One effect knob, wired to its parameter.
  struct Control {
    std::unique_ptr<LabelledKnob> knob;
    std::unique_ptr<SliderAttachment> attachment;
  };

  void addKnob(std::vector<Control> &into, const juce::String &group,
               const juce::String &caption, const juce::String &paramId,
               const juce::String &tooltip, juce::Component &popupParent);

  /// Polyphony, bend range and the two output switches. All of them are set
  /// once and left, which is a menu rather than a panel.
  void showSettingsMenu();

public:
  /// The same menu as data, built rather than shown.
  ///
  /// Kept apart from showing it for the reason the LINK menu is: a menu that
  /// can only be reached by clicking is a menu that never gets tested, and
  /// showing one needs a real window. This can be walked without either.
  juce::PopupMenu buildSettingsMenu();

  /// The echo's four positions: off, and one per machine.
  ///
  /// A menu rather than a switch, for the same reason the character is one.
  /// Behind it are two parameters rather than one: the switch that turns the
  /// echo on is older than the choice of machine, and every saved patch stores
  /// it and every automation lane points at it, so it stayed where it was and
  /// the type arrived beside it. Off writes the switch, the other three write
  /// the switch and a type.
  ///
  /// Built as data for the same reason the settings menu is: a menu that can
  /// only be reached by clicking is a menu that never gets tested.
  juce::PopupMenu buildClipMenu();
  void chooseClip(int id);

  juce::PopupMenu buildEchoMenu();

  /// The same, for the reverb, and for the same two reasons: its on switch
  /// predates the choice of machine, and a menu built as data is a menu that
  /// can be tested without being clicked.
  juce::PopupMenu buildReverbMenu();

  /// How wide the two buttons that say a value have to be.
  ///
  /// Each is sized for the longest word it can show, in the capitals the bar
  /// shouts everything in, plus the air either side that every other button
  /// here has. A word that does not fit is drawn with its middle taken out and
  /// nothing says so, which is why a test measures these rather than trusting
  /// them. See testBarButtonsFitTheirWords.
  /// Both land on the same figure, from different directions: OP-AMP is 51 px
  /// and DIGITAL is 50, and the air either side is what every other button on
  /// the bar has.
  static constexpr int kCharacterWidth = 62;
  static constexpr int kEchoWidth = 62;
  static constexpr int kReverbWidth = 58;

  /// The word painted under each of those, saying what the button chooses.
  ///
  /// In the same order as the three widths above, and public for the same
  /// reason they are: a word wider than the button it sits under is drawn
  /// with its middle taken out and nothing says so. See
  /// testBarButtonsFitTheirWords.
  static constexpr const char *kGroupNames[] = {"CHARACTER", "ECHO", "REVERB"};

  /// The clipper's switch, under the meter beside the converter readouts.
  ///
  /// Sized for the one word it ever says, at the font a button this short
  /// picks for itself, which is the same 9 px the captions around it use.
  /// Wide enough for the longest shape's name rather than for the word CLIP,
  /// since the button says which machine is running the way the echo's and
  /// the reverb's do.
  /// 50 rather than the 62 it had, which was sized for the word LIMITER
  /// before the button started shortening to LIMIT and ASYM. The widest
  /// label is now 33 px of text, and the twelve given back go to the
  /// converter readouts beside it, which need 53 each to name their units.
  static constexpr int kClipWidth = 50;

  /// The one word it ever says. Shared with the tests, which have to pick this
  /// button out of the bar's children: it is the only one that stands in the
  /// caption band rather than on the line of controls.
  static constexpr const char *kClipName = "CLIP";

  /// What the Settings menu says this build is, as it says it.
  ///
  /// Built here rather than written out at the point it is drawn, so the test
  /// that checks the menu names the version can ask the same question the menu
  /// answers instead of assembling the string a second time and agreeing with
  /// itself.
  static juce::String versionLine();

private:
  /// Whether an effect's knobs show a lit ring. Off means the stage is not in
  /// the signal, and a knob that is not doing anything should not look as
  /// though it is.
  void setRingsLive(std::vector<Control> &controls, bool live);

  /// Applies what buildEchoMenu came back with. Zero means dismissed.
  void chooseEcho(int id);

  /// The same for the reverb.
  void chooseReverb(int id);

public:
private:
  /// The factory list, then whatever has been saved, then what can be done
  /// with them.
  void showPresetMenu();

public:
  /// The same menu as data, built rather than shown.
  ///
  /// Two submenus and then the actions, rather than one flat list. Twenty-six
  /// factory presets and however many of your own runs off the bottom of a
  /// short screen, and a menu that scrolls is a menu you cannot see the shape
  /// of. Folders inside the preset directory become groups under Saved, nested
  /// as deeply as they are on disk.
  ///
  /// Split from showing it for the reason the settings menu is: showing one
  /// needs a real window, and a menu that can only be reached by clicking is a
  /// menu no test ever walks.
  juce::PopupMenu buildPresetMenu();

private:
  /// The list behind a choice shown on the panel: the two converter settings
  /// and the oscillator character. Built here rather than inline so whatever
  /// opens one shares the list with whatever else reads it.
  void showChoiceMenu(const char *paramId, const juce::StringArray &choices,
                      juce::Component *anchor);

  /// Asks for a name and hands it back. Its own window, since a menu cannot
  /// take typing.
  void askForPresetName();

  /// Related controls sit together in a bordered group.
  enum Group {
    PresetGroup = 0,
    VoiceGroup,
    SeriesGroup,
    EchoGroup,
    ReverbGroup,
    OutputGroup,
    NumGroups
  };

  /// Where the rows break. Entry r is the first group on row r, and the last
  /// entry is NumGroups, so row r holds [start[r], start[r + 1]).
  struct RowPlan {
    int rows = 1;
    std::array<int, NumGroups + 1> start{};
  };

  /// Packs the groups into as many rows as it takes.
  static RowPlan planRows(int firstRowWidth, int fullWidth, int maxRows);

  /// Clears every control's bounds before a fresh pass. Without this a control
  /// that does not get placed keeps whatever position it had when the window
  /// was wider, which is what put the preset and poly captions on top of one
  /// another.
  void parkControls();

  void layoutRow(juce::Rectangle<int> row, int firstGroup, int lastGroup);
  void placeGroup(int group, juce::Rectangle<int> bounds);

  std::array<juce::Rectangle<int>, NumGroups> groupBounds{};

  juce::AudioProcessorValueTreeState &apvts;

  // Anything that exists on all 32 strips now lives on the master channel.
  // What is left here is the handful of genuinely single global values, which
  // have nothing to stay relative to and so are ordinary absolute knobs.
  /// The output level, laid over the meter rather than beside it.
  ///
  /// Every one of the thirty-three channels sets its level with a fader whose
  /// meter runs behind it, so the master reads as the odd one out when it is a
  /// knob. Laid on its side over the output meter it matches them, and the
  /// forty-eight pixels it used to take plus its gap go to the meter instead,
  /// which is the one thing on this bar worth more room.
  juce::Slider masterFader{juce::Slider::LinearHorizontal,
                           juce::Slider::NoTextBox};

  /// What the series does, as opposed to what is done to it afterwards. Both
  /// are properties of the instrument, so they stand between the tools and the
  /// two effect boxes rather than among either.
  LabelledKnob stretch{"STRETCH"}, track{"TRACK"}, wobble{"WOBBLE"};

  StereoOutputMeter meter;

  /// Under the meter, at the end of the chain, which is where a converter
  /// sits. On the panel rather than in a menu because they are part of a
  /// preset: loading one can change them, so they have to be visible.
  SegmentDisplay rateDisplay{"kHz"}, bitsDisplay{"bit"};

  /// Which oscillator the partials are, at the head of the group that says
  /// what the series is. A word rather than a readout, since none of these is
  /// a number, and on the panel rather than in a menu for the same reason the
  /// converter is: a preset decides it, so it has to be visible.
  ///
  /// It lights in the character's own colour, and does not light at all on
  /// Pure, which is the one that adds nothing.
  GlowButton characterButton;

  /// The logo, rescaled once to the size it is drawn at. Scaling a 2464 px
  /// image down to 150 on every repaint would be both slow and soft.
  juce::Image logo, logoScaled;

  /// Fourteen bars, because it has to spell: Glockenspiel and Wurli are not
  /// things seven can say. The one display on the panel that carries a name
  /// rather than a number.
  SegmentDisplay presetDisplay{{}, SegmentDisplay::Bars::Fourteen};

  /// The name as it was given, which the display cannot hand back: its cells
  /// have no lower case and it upper-cases what it is told. Saving a preset
  /// reads this, and so does the check that decides whether the bar has
  /// drifted from what is loaded.
  juce::String presetName;

  /// Icons rather than words, for the reason kGroupMinWidth gives.
  GlowButton settingsButton, macroButton;
  int macrosMade = 0;

  /// Whether LINK is on. The switch itself is a button in the gutter, since
  /// that is the column the tool belongs to, but what it switches lives here
  /// with the scope and the curve it goes with.
  bool linkOn = false;

  /// Held between opening the menu and acting on it, so the ids the menu hands
  /// back mean something.
  juce::Array<juce::File> userPresetFiles;

  std::unique_ptr<juce::AlertWindow> nameWindow;
  GlowButton echoButton, reverbButton, clipButton;

  /// The clipper is a plain switch, so it takes a plain attachment. The two
  /// effects cannot: their buttons open a menu over two parameters.
  ///
  /// Declared after the button rather than beside the alias it is built from,
  /// and that is the whole of why it is here. Members are destroyed in reverse
  /// order, so an attachment declared first outlives its button, and an
  /// attachment's destructor asks the button to stop listening to it. The
  /// sanitizers catch that as a call on an object that is no longer a Button,
  /// and nothing else does: the memory is still there and still looks right.

  /// Likewise. Zoom is set once to suit the screen and then left, and giving
  /// its box back to the bar is what lets the output group keep its readouts
  /// legible at the width the window opens at.
  float zoom = 1.0f;

  std::vector<Control> echoControls, reverbControls;

  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;

  std::unique_ptr<SliderAttachment> masterAttachment, stretchAttachment,
      trackAttachment, wobbleAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
