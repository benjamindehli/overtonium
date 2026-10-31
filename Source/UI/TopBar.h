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

/// The stereo output meter: two horizontal segmented bars with a peak hold.
/// It only repaints when a lamp changes.
class StereoOutputMeter : public juce::Component {
public:
  StereoOutputMeter() { setInterceptsMouseClicks(false, false); }

  /// The strip along the bottom that carries the decibel marks rather than
  /// any lamps.
  static constexpr int kScaleHeight = 4;

  /// Where the two bars are, above the scale. The master fader lies over this.
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
    /// Kept rather than derived so they hold still at a boundary.
    int lit = 0;
    int peakLamp = -1;

    /// @returns true when a lamp changed and a repaint is needed.
    bool advance(float level, int count);
  };

  void paintBar(juce::Graphics &, juce::Rectangle<float>, const Bar &,
                int count) const;

  Bar left, right;
};

/// The bar along the top of the window: presets, the series knobs, the
/// character, the resonator, the echo, the reverb, the output meter and the
/// buttons that open the menus, the macro panel and the option bars.
class TopBar : public juce::Component {
public:
  TopBar(juce::AudioProcessorValueTreeState &apvts,
         juce::Component &popupParent);

  void paint(juce::Graphics &) override;
  void resized() override;

  /// A right-click on one of the bar's own controls, carrying the parameter
  /// it moves. The editor puts up the MIDI Learn menu.
  std::function<void(const juce::String &)> onLearnRequested;

  /// Fired when MACROS is clicked, which puts the macro panel up over the
  /// mixer. See ui::MacroPanel.
  std::function<void()> onMacrosClicked;

  /// Fired when one of the three tools is chosen from the tool menu.
  std::function<void(PointerTool)> onToolChosen;

  /// Fired when PLAYING or TUNING is clicked. The editor answers through
  /// setBarsOpen, so a lamp follows its bar rather than the click.
  std::function<void()> onPlayingClicked, onTuningClicked;

  /// Lights PLAYING and TUNING while their bars are open.
  void setBarsOpen(bool playing, bool tuning);

  /// Lights MACROS while its panel is up, and says how many are made.
  void setMacrosOn(bool);
  void setMacroCount(int);

  void mouseDown(const juce::MouseEvent &) override;

  /// Heard from every control in the bar too, since the bar listens deeply.
  /// Only a wheel that landed on the bar itself is passed up, so turning a
  /// knob does not also scroll the mixer.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  /// Says a newer release is out: SETTINGS lights, and the release is the
  /// first item in its menu. An empty version says nothing.
  void setUpdateAvailable(const juce::String &version, const juce::String &url);

  /// The release SETTINGS is saying is out, or empty. Read back by the tests.
  const juce::String &availableUpdate() const noexcept { return updateVersion; }
  bool isSettingsLit() const { return settingsButton.getToggleState(); }

  // ---- callbacks the editor fills in ----
  std::function<void(int)> onPresetChosen;
  std::function<void(juce::File)> onUserPresetChosen;
  std::function<void(juce::String)> onSaveUserPreset;
  std::function<void()> onCopyFactoryCode;
  std::function<void(float)> onZoomChanged;

  /// Sets the window back to the size that shows the whole mixer.
  std::function<void()> onFitAllChannels;

  /// Undo and redo, at the head of the Settings menu, since most hosts keep
  /// Cmd-Z for themselves.
  std::function<void()> onUndo, onRedo;

  /// The update-check setting, read when the menu opens and written when it is
  /// toggled. The editor owns the value.
  std::function<bool()> isUpdateCheckAllowed;
  std::function<void(bool)> onUpdateCheckToggled;
  std::function<bool()> canUndo, canRedo;

  /// Shown on the preset button, so the bar says what is loaded.
  void setPresetName(const juce::String &);
  juce::String getPresetName() const;

  /// What the character button is showing. Read back by the tests.
  juce::String getCharacterName() const {
    return characterButton.getButtonText();
  }

  /// Whether the character button is lit, and in what. Read back by the tests.
  bool isCharacterLit() const { return characterButton.getToggleState(); }

  juce::Colour getCharacterColour() const {
    return characterButton.findColour(juce::TextButton::textColourOnId);
  }

  /// The colour the rings in the character's frame light in.
  juce::Colour getCharacterRingColour() const {
    return stretch.slider.findColour(juce::Slider::rotarySliderFillColourId);
  }

  bool isLinkEnabled() const { return linkOn; }

  /// Latched by the editor at the start of each LINK drag.
  LinkScope getLinkScope() const { return scope; }
  LinkCurve getLinkCurve() const { return curve; }

  void setLinkEnabled(bool);
  void setLinkScope(LinkScope);
  void setLinkCurve(LinkCurve);

  std::function<void()> onLinkSettingsChanged;

  /// Whether any channel is selected, so the menu can offer to clear it. The
  /// selection itself is the editor's. See LinkScope::Selected.
  void setHasSelection(bool any) { hasSelection = any; }

  /// Asked for when "Clear selection" is chosen from the menu.
  std::function<void()> onSelectionCleared;

  /// The LINK menu, for the button in the bar and a right-click on a strip.
  ///
  /// @param anchor       what to hang the menu off, or nullptr to put it
  ///                      under the pointer.
  /// @param parameterId   what a right-click landed on, empty when the menu
  ///                      came from the LINK button instead.
  /// @param map           where a learned binding goes
  /// @param tool          the tool now chosen. No default, so callers cannot
  ///                      leave it out.
  void showLinkMenu(juce::Component *anchor, const juce::String &parameterId,
                    MidiLearn *map, PointerTool tool);

  /// The bar's height at this width, since it reflows onto further rows when
  /// the groups do not fit across one. Static because the editor needs it
  /// before it sets the bar's bounds.
  static int heightForWidth(int width);

  /// Narrowest window the bar can lay out without hiding a group. The editor
  /// takes this as a floor.
  static int minimumWidth();

  /// Refreshes what the bar reads from the parameters rather than from a
  /// slider, such as the converter readouts and the character. The host rate is
  /// passed in for the setting that follows the host.
  void updatePanelReadouts(double hostSampleRate);
  void setOutputLevels(float l, float r) { meter.push(l, r); }
  void setZoomChoice(float zoom);

private:
  /// The release SETTINGS is advertising and its link, empty when none.
  juce::String updateVersion, updateUrl;

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

  /// Puts up the Settings menu.
  void showSettingsMenu();

public:
  /// The Settings menu as data, so tests can walk it without a window.
  juce::PopupMenu buildSettingsMenu();

  /// The clipper's machines, and applying a choice from that menu.
  juce::PopupMenu buildClipMenu();
  void chooseClip(int id);

  /// The echo's four positions: off, and one per machine. Off writes the
  /// echo's on switch, the others write the switch and a type.
  juce::PopupMenu buildEchoMenu();

  /// The same, for the reverb.
  juce::PopupMenu buildReverbMenu();

  /// What the resonator is tuned to, or Off, in one parameter.
  juce::PopupMenu buildResonatorMenu();

  /// How wide the buttons that name a machine are, each sized for the longest
  /// word it can show. A word that does not fit is silently cut, so a test
  /// measures these. See testBarButtonsFitTheirWords.
  static constexpr int kCharacterWidth = 62;
  static constexpr int kResonatorWidth = 62;
  static constexpr int kEchoWidth = 62;
  static constexpr int kReverbWidth = 58;

  /// The caption under each of those buttons, in the same order as the
  /// widths above, and checked by the same test.
  static constexpr const char *kGroupNames[] = {"CHARACTER", "RESONATOR",
                                                "ECHO", "REVERB"};

  /// The clipper's button, under the meter beside the converter readouts,
  /// wide enough for the longest machine's short name.
  static constexpr int kClipWidth = 50;

  /// The clipper's caption. Shared with the tests, which find the button by it.
  static constexpr const char *kClipName = "CLIP";

  /// The version line the Settings menu shows. Public for the tests.
  static juce::String versionLine();

private:
  /// Whether an effect's knobs show a lit ring, which they do only while the
  /// stage is in the signal.
  void setRingsLive(std::vector<Control> &controls, bool live);

  /// The colour a group's rings light in, which is its button's.
  static void setRingColour(juce::Slider &, juce::Colour);
  static void setRingColour(std::vector<Control> &controls, juce::Colour);

  /// Applies what buildEchoMenu came back with. Zero means dismissed.
  void chooseEcho(int id);

  /// The same for the reverb.
  void chooseReverb(int id);

  /// And for the resonator.
  void chooseResonator(int id);

public:
private:
  /// Puts up the preset menu.
  void showPresetMenu();

public:
  /// The preset menu as data, so tests can walk it without a window.
  ///
  /// A Factory submenu, a Saved submenu and then the actions. Folders inside
  /// the preset directory become nested groups under Saved.
  juce::PopupMenu buildPresetMenu();

private:
  /// Puts up the list behind a choice parameter shown on the panel, such as
  /// the converter settings and the oscillator character.
  void showChoiceMenu(const char *paramId, const juce::StringArray &choices,
                      juce::Component *anchor);

  /// Asks for a name and hands it back. Its own window, since a menu cannot
  /// take typing.
  void askForPresetName();

  /// Related controls sit together in a bordered group.
  enum Group {
    VoiceGroup = 0,
    PresetGroup,
    SeriesGroup,
    ResonatorGroup,
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
  static RowPlan planRows(int width, int maxRows);

  /// Clears every control's bounds before a fresh pass, so a control that is
  /// not placed does not keep a stale position.
  void parkControls();

  void layoutRow(juce::Rectangle<int> row, int firstGroup, int lastGroup);
  void placeGroup(int group, juce::Rectangle<int> bounds);

  std::array<juce::Rectangle<int>, NumGroups> groupBounds{};

  juce::AudioProcessorValueTreeState &apvts;

  /// The output level, a fader laid over the output meter as the channel
  /// faders are over theirs.
  juce::Slider masterFader{juce::Slider::LinearHorizontal,
                           juce::Slider::NoTextBox};

  /// The knobs that shape the harmonic series itself.
  LabelledKnob stretch{"STRETCH"}, track{"TRACK"}, wobble{"WOBBLE"};

  StereoOutputMeter meter;

  /// The converter's rate and bits, under the meter. On the panel because a
  /// preset can change them.
  SegmentDisplay rateDisplay{"kHz"},
      bitsDisplay{"bit", SegmentDisplay::Bars::Seven, 2};

  /// The oscillator character. Lights in the character's colour, and not at
  /// all on Pure.
  GlowButton characterButton;

  /// What the resonator is tuned to. Lit while it rings, and saying OFF when
  /// it does not.
  GlowButton resonatorButton;

  /// The logo, rescaled once to the size it is drawn at rather than on every
  /// repaint.
  juce::Image logo, logoScaled;

  /// The preset name, in nine fourteen-segment cells so it can spell. Always
  /// nine, so the letters keep their size. Longer names are fitted by
  /// SegmentDisplay::squeeze.
  SegmentDisplay presetDisplay{{}, SegmentDisplay::Bars::Fourteen, 9};

  /// The name as it was given, since the display upper-cases it.
  juce::String presetName;

  /// Icons rather than words, to save width. See kGroupMinWidth.
  GlowButton settingsButton, macroButton;

  /// Under those two, opening the Playing and Tuning bars.
  GlowButton playingButton, tuningButton;
  int macrosMade = 0;

  /// Whether LINK is on. The switch is in the gutter, but its state lives here
  /// with the scope and the curve.
  bool linkOn = false;

  /// Held between opening the menu and acting on it, so the ids the menu hands
  /// back mean something.
  juce::Array<juce::File> userPresetFiles;

  std::unique_ptr<juce::AlertWindow> nameWindow;
  GlowButton echoButton, reverbButton, clipButton;

  /// The zoom, set from the Settings menu.
  float zoom = 1.0f;

  std::vector<Control> resonatorControls, echoControls, reverbControls;

  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;
  bool hasSelection = false;

  std::unique_ptr<SliderAttachment> masterAttachment, stretchAttachment,
      trackAttachment, wobbleAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
