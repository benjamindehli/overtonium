#pragma once

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

  bool isLinkEnabled() const { return linkButton.getToggleState(); }

  /// Latched by the editor at the start of each LINK drag.
  LinkScope getLinkScope() const { return scope; }
  LinkCurve getLinkCurve() const { return curve; }

  void setLinkScope(LinkScope);
  void setLinkCurve(LinkCurve);

  std::function<void()> onLinkSettingsChanged;

  /// The switch, the scope and the curve, in one menu.
  ///
  /// Shared by the button in the bar and by a right-click anywhere on a strip,
  /// so there is one list rather than two that can drift apart.
  ///
  /// @param anchor  what to hang the menu off, or nullptr to put it under the
  ///                pointer.
  void showLinkMenu(juce::Component *anchor);

  /// The bar reflows onto further rows when the groups no longer fit across
  /// one, so nothing has to be dropped on a narrow window. Static because the
  /// editor has to know the height before it can hand the bar its bounds.
  static int heightForWidth(int width);

  /// Narrowest window the bar can lay out without hiding a group. The editor
  /// takes this as a floor, since refusing to shrink further is better than
  /// quietly dropping controls.
  static int minimumWidth();

  /// Refreshes the two converter readouts. The host rate has to be passed in
  /// because "leave it alone" is a setting whose value only the processor
  /// knows.
  void updateConverterReadouts(double hostSampleRate);
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

  void styleToggle(juce::TextButton &, const juce::String &text,
                   const juce::String &tooltip);

  /// The switch lives in the menu now, so the button has to redraw when it
  /// moves rather than when it is clicked.
  void updateLinkEnablement();

  /// One effect knob, wired to its parameter.
  struct Control {
    std::unique_ptr<LabelledKnob> knob;
    std::unique_ptr<SliderAttachment> attachment;
  };

  void addKnob(std::vector<Control> &into, const juce::String &group,
               const juce::String &caption,
               const juce::String &paramId, const juce::String &tooltip,
               juce::Component &popupParent);

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

  /// The converter lists. Hung off the readout that opens them, and built here
  /// rather than inline so the readout and anything else share one list.
  void showConverterMenu(const char *paramId,
                         const juce::StringArray &choices,
                         juce::Component *anchor);

  /// Asks for a name and hands it back. Its own window, since a menu cannot
  /// take typing.
  void askForPresetName();

  /// Related controls sit together in a bordered group.
  enum Group {
    PresetGroup = 0,
    VoiceGroup,
    LinkGroup,
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
  LabelledKnob master{"MASTER"};

  /// What the series does, as opposed to what is done to it afterwards. Both
  /// are properties of the instrument, so they stand between the tools and the
  /// two effect boxes rather than among either.
  LabelledKnob stretch{"STRETCH"}, track{"TRACK"}, wobble{"WOBBLE"};

  StereoOutputMeter meter;

  /// Under the meter, at the end of the chain, which is where a converter
  /// sits. On the panel rather than in a menu because they are part of a
  /// preset: loading one can change them, so they have to be visible.
  SegmentDisplay rateDisplay{"kHz"}, bitsDisplay{"bit"};

  /// The logo, rescaled once to the size it is drawn at. Scaling a 2464 px
  /// image down to 150 on every repaint would be both slow and soft.
  juce::Image logo, logoScaled;

  juce::TextButton presetButton, settingsButton, linkButton;

  /// Held between opening the menu and acting on it, so the ids the menu hands
  /// back mean something.
  juce::Array<juce::File> userPresetFiles;

  std::unique_ptr<juce::AlertWindow> nameWindow;
  juce::TextButton echoButton, reverbButton;

  /// Likewise. Zoom is set once to suit the screen and then left, and giving
  /// its box back to the bar is what lets the output group keep its readouts
  /// legible at the width the window opens at.
  float zoom = 1.0f;

  std::vector<Control> echoControls, reverbControls;

  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;

  std::unique_ptr<SliderAttachment> masterAttachment, stretchAttachment,
      trackAttachment, wobbleAttachment;
  std::unique_ptr<ButtonAttachment> echoAttachment, reverbAttachment;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

} // namespace ovt::ui
