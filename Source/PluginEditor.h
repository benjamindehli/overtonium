#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/ChannelStrip.h"
#include "UI/LookAndFeel.h"
#include "UI/MacroPanel.h"
#include "UI/NoiseStrip.h"
#include "UI/Notice.h"
#include "UI/OptionBars.h"
#include "UI/TopBar.h"
#include "dsp/Drift.h"

/// Left-hand caption column. Lays out the same rows as a channel strip so
/// every knob in the mixer has a name without repeating it 32 times.
class RowGutter : public juce::Component {
public:
  RowGutter();

  void paint(juce::Graphics &) override;

  /// Brightens the caption for the row the pointer is on.
  void setHighlightedRow(ovt::ui::Row);

  /// Which sections are folded, so folded rows are not drawn and the headings
  /// show which way they point.
  void setCollapsedSections(ovt::ui::SectionMask);

  /// How far the parameters are scrolled. The editor hands the gutter, every
  /// strip and the noise channel the same number so captions stay beside their
  /// knobs. Snapped to a row boundary inside layoutRows.
  void setScroll(int);

  /// Which of the two modulators are shared by all notes. Lights the switch
  /// beside each modulator's shape caption, since a preset can turn it on.
  void setSharedModulators(bool pitch, bool amp);

  /// Fired when one of the shared switches is clicked. The editor owns the
  /// parameter.
  std::function<void()> onPitchInPhaseClicked;
  std::function<void()> onAmpInPhaseClicked;

  /// The two glide switches beside the GLIDE caption: whether a note glides
  /// only legato, and whether every glide takes a fixed time rather than moving
  /// at a fixed rate. Both are part of the patch.
  void setGlideSwitches(bool legato, bool fixedTime);

  /// Fired when one of those switches is clicked. The editor owns the
  /// parameter.
  std::function<void()> onGlideLegatoClicked;
  std::function<void()> onGlideFixedTimeClicked;

  /// Fired when a heading is clicked. The editor owns the fold state, since the
  /// strips are told too.
  std::function<void(ovt::ui::Section)> onSectionToggled;

  /// Fired as the pointer moves over the gutter's own headings, so the editor
  /// can light the matching row on the strips.
  std::function<void(ovt::ui::Row)> onHoverChanged;

  /// Fired when the tool button is clicked, with the button to hang the menu
  /// off.
  std::function<void(juce::Component *)> onLinkClicked;

  /// Latches the draw tool on or off.
  std::function<void()> onDrawClicked;

  /// Which tool the button shows, and the cursor for it.
  void setTool(ovt::ui::PointerTool, ovt::ui::LinkCurve);

  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

private:
  ovt::ui::Row highlighted = ovt::ui::kNoRow;
  ovt::ui::SectionMask collapsed = 0;
  int scroll = 0;

  /// Hides a row that has scrolled under the pinned header. See HeaderCap.
  ovt::ui::HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);

  /// The tool button, in the empty band above the captions where the strips
  /// carry their channel numbers. One button for the three tools, showing the
  /// cursor. See ui::PointerTool.
  ovt::ui::GlowButton toolButton;

  /// The glide switches, a pair just left of the GLIDE caption. Lit is on.
  ovt::ui::ScreenSwitch glideLegato, glideFixedTime;

  /// The in-phase switches, one just left of each shape caption. Lit is one
  /// modulator shared by the whole keyboard.
  ovt::ui::ScreenSwitch pitchInPhase, ampInPhase;

  /// Puts a run of switches right up against a row's caption, or hides them
  /// with it.
  void placeCaps(ovt::ui::Row, std::initializer_list<juce::Component *>,
                 const ovt::ui::RowBounds &, int headerBottom);

  ovt::ui::PointerTool tool = ovt::ui::PointerTool::Pointer;
  juce::Image toolIcon;

  /// The maker's badge, in the empty foot of the gutter.
  std::unique_ptr<juce::Drawable> makersMark{ovt::ui::logoMakersMark()};
};

/// The plugin window: the top bar, the option bars, the mixer of 32 channel
/// strips with the gutter and the noise channel, and the macro panel.
class OvertoniumEditor : public juce::AudioProcessorEditor,
                         public ovt::ui::LinkTarget,
                         public juce::ScrollBar::Listener,
                         public ovt::ui::HoverTarget,
                         private juce::Timer,
                         private juce::ComponentListener {
public:
  explicit OvertoniumEditor(OvertoniumProcessor &);
  ~OvertoniumEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  bool keyPressed(const juce::KeyPress &) override;

  // ---- ovt::ui::LinkTarget ----
  bool isLinkEnabled() const override;

  /// Brings everything that depends on the LINK settings into step: the
  /// gutter's button, the drag preview glow, and the pointer.
  void syncLinkUi();

  /// Whether a drag across the faders would draw them, either because the
  /// modifier is held or because the tool is latched on. Not whether one is
  /// under way.
  bool drawArmed = false;
  bool shiftHeld = false;
  bool drawLatched = false;

  /// Read on the housekeeping tick. See the body.
  void pollDrawModifier();
  void refreshDrawArmed();
  void toggleDrawLatch();

  /// Writes which tool is selected into the session. Both flags the tool is
  /// derived from are written here, and every path that changes either goes
  /// through this.
  void rememberTool();

  /// Whether a drawn drag is under way, as opposed to merely possible.
  bool drawingNow = false;

  /// Where the drawing last reached, so the columns in between can be filled
  /// in.
  juce::Point<int> lastDrawn;

  bool drawStarted(juce::Point<int>) override;
  void drawMovedTo(juce::Point<int>) override;
  void drawEnded() override;

  std::vector<juce::RangedAudioParameter *> faderParameters() const;
  void applyDrawAt(juce::Point<int>);

  /// The channels picked by their numbers, which the Selected scope reaches.
  /// Saved with the session beside LINK's other settings.
  std::array<bool, ovt::kNumHarmonics> selection{};

  /// A selection drag under way: whether it adds or removes, decided by the
  /// channel it began on, and where it last reached.
  bool selectingNow = false;
  bool selectingAdds = true;
  juce::Point<int> lastSelected;

  void selectionStarted(int index0, juce::Point<int>) override;
  void selectionMovedTo(juce::Point<int>) override;
  void selectionEnded() override;

  /// Sets whichever channel's column this point is in, at any height.
  void applySelectionAt(juce::Point<int>);

  /// Hands the selection to the strips, the bar and the session, and updates
  /// the LINK preview.
  void selectionChanged(bool save);
  void clearSelection();
  void linkDragStarted(ovt::ui::Role, int sourceIndex) override;
  void linkValueChanged(ovt::ui::Role, int sourceIndex,
                        float plainValue) override;
  void linkDragEnded(ovt::ui::Role, int sourceIndex) override;

  void showLinkMenu(const juce::String &parameterId) override;

  /// The MIDI Learn items alone, for controls LINK does not gang: the bars and
  /// the noise channel.
  void showLearnMenu(const juce::String &parameterId);

  /// Keeps the waiting marker on whichever control MIDI Learn is armed for.
  /// Polled, since the wait ends on the audio thread. See ovt::MidiLearn.
  void followArmedControl();

  /// Gives every control a macro drives that macro's colour. Worked out here
  /// since no strip can see all eight macros.
  void followMacroTints();

  /// The parameter that carries the marker now, so it can be taken off.
  juce::String armedParameter;
  bool scrollParameters(int delta) override;

  /// A wheel over the gutter or the noise channel, which sit outside the
  /// mixer's viewport, scrolls the parameters too.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  void scrollBarMoved(juce::ScrollBar *, double newStart) override;

  // ---- ovt::ui::HoverTarget ----
  void hoverChanged(int stripIndex, ovt::ui::Row) override;

  /// Sets the window back to the size that shows all 32 channels. In the
  /// Settings menu beside the zoom. Public so a test can call it.
  void fitAllChannels();

  /// Which of the three tools a drag would use right now, derived from LINK's
  /// switch and whether drawing is armed. Drawing wins.
  ovt::ui::PointerTool currentTool() const;

  /// Sets the flags a chosen tool means. Public so a test can call it.
  void chooseTool(ovt::ui::PointerTool);

  /// Puts this editor's look and feel and its panel colour on the standalone's
  /// window, which otherwise wears JUCE's default style. Public so a test can
  /// hand it a window.
  void dressWindow(juce::DocumentWindow &);

  /// A newer release has been found: SETTINGS lights, and the first time this
  /// release is found a dialog says so. Public for the tests.
  void announceRelease(const juce::String &version, const juce::String &url);

  /// The two questions the update check asks. Public for the tests.
  ovt::ui::Notice &updateNotice() noexcept { return notice; }

private:
  void timerCallback() override;

  /// Opens or closes MACROS, as its button in the top bar does.
  void showMacroPanel(bool show);

  /// What a right-click on a macro knob in the Playing bar offers. See
  /// PlayingBar::macroMenu.
  void showMacroMenu(int macro);

  /// Finds the standalone's window, if this is the standalone, and dresses it.
  /// Never in a host, whose window is not ours.
  void dressStandaloneWindow();

  void parentHierarchyChanged() override;

  void setZoom(float newZoom);

  /// The size that shows the whole mixer, at the fold state it is in.
  juce::Rectangle<int> standardSize() const;

  /// Sets the resize limits for a window of the given logical width.
  ///
  /// The height floor follows the width, since the top bar reflows onto more
  /// rows as the window narrows. The width is passed because both callers run
  /// before the new width lands.
  void applyResizeLimits(int forLogicalWidth);

  /// The same, for the width the window has now.
  void applyResizeLimits();
  void applyPreset(int index);

  /// Records the name on the processor and shows it on the button.
  void setPresetName(const juce::String &name);

  /// Folds or unfolds one group of rows across the whole mixer, and takes the
  /// window's height with it.
  void toggleSection(ovt::ui::Section);

  /// Opens or closes one of the two bars under the top bar. The mixer gives up
  /// the height, unless the window is already as short as it can be, in which
  /// case it grows.
  void toggleBar(int bar);

  /// Everything above and below the strips at a width: the top bar, the bars
  /// open under it, and the scroll bar.
  int chromeHeight(int logicalWidth) const;

  /// Hands the current fold state to the gutter and every strip.
  void publishCollapsedSections();

  /// Lights the switch of a modulator shared by all notes. See
  /// RowGutter::setSharedModulators.
  void syncSharedModulators();

  /// Asks once, the first time an editor opens, whether to look for new
  /// versions. Nothing leaves the machine before someone has said yes.
  void offerUpdateCheck();

  /// Starts a check if the setting allows one.
  void maybeCheckForUpdates();

  /// Shows a message without blocking the message thread.
  void complain(const juce::String &title, const juce::String &detail);

  /// Which strips a drag from this one would reach, and by how much. Zero marks
  /// a strip the scope leaves out.
  void gatherLinkWeights(int sourceIndex, ovt::ui::LinkScope,
                         ovt::ui::LinkCurve,
                         std::array<float, ovt::kNumHarmonics> &out) const;

  /// Lights the control LINK is moving, or would move if you grabbed the one
  /// under the pointer.
  void updateLinkGlow();

  /// Hands the strips a pointer that says what a drag would do to them.
  void updateLinkCursor();

  /// Everything a LINK drag needs, latched when it begins.
  ///
  /// Offsets are measured from the start, so returning the knob restores
  /// everything exactly, and a scope change mid-drag cannot mix two rules.
  struct LinkGesture {
    bool active = false;
    ovt::ui::Role role = ovt::ui::Role::Tune;
    ovt::ui::LinkCurve curve = ovt::ui::LinkCurve::Uniform;
    int source = -1;

    std::array<float, ovt::kNumHarmonics> baseline{};
    std::array<float, ovt::kNumHarmonics> weight{}; ///< zero means not selected
    std::array<float, ovt::kNumHarmonics> jitter{}; ///< fixed spread directions

    bool includes(int i) const { return weight[(size_t)i] > 0.0f; }
  };

  LinkGesture linkGesture;

  /// Deliberately never reseeded, so each spread differs from the last.
  ovt::Xorshift spreadRandom{0x9e3779b9u};

  juce::RangedAudioParameter *oscParameter(ovt::ui::Role, int index) const;

  /// The processor the base class holds, cast to its real type rather than
  /// kept in a second reference.
  OvertoniumProcessor &plugin() const {
    return static_cast<OvertoniumProcessor &>(processor);
  }

  // Declared first so it outlives every component that borrows it.
  ovt::ui::OvertoniumLookAndFeel lookAndFeel;

  juce::TooltipWindow tooltips{this, 600};

  /// The standalone's window, once dressed. Held so the component listener
  /// can be removed, since a listener is a raw pointer.
  juce::Component::SafePointer<juce::DocumentWindow> standaloneWindow;

  /// Single child holding the whole UI, so zoom is one AffineTransform.
  juce::Component content;
  ovt::ui::TopBar topBar;

  /// Between the top bar and the mixer, each opened by its button in the top
  /// bar. Both start closed.
  ovt::ui::PlayingBar playingBar;
  ovt::ui::TuningBar tuningBar;
  RowGutter gutter;
  ovt::ui::NoiseStrip noiseStrip;

  /// Over the mixer when open.
  ovt::ui::MacroPanel macroPanel;

  /// The two questions the update check asks, over everything else.
  ovt::ui::Notice notice;

  // Declared before the viewport that displays it, so the viewport is
  // destroyed first.
  juce::Component stripsHolder;
  juce::Viewport viewport;

  std::vector<std::unique_ptr<ovt::ui::ChannelStrip>> strips;

  float zoom = 1.0f;

  /// Which groups of rows are folded away. Saved in the state with the window
  /// size and zoom.
  ovt::ui::SectionMask collapsedSections = 0;

  /// Which of the two bars are open, as kPlayingBar and kTuningBar bits.
  /// Remembered with the session, like the fold mask.
  int openBars = 0;

  /// How far the parameters are scrolled, and how far they can be. One number
  /// for the whole mixer so captions stay beside their knobs. The range is
  /// zero when everything fits.
  int scrollY = 0;
  int scrollRange = 0;

  /// The rectangle a column lays its rows in and the height of the band, both
  /// recorded by resized() for scrollParameters.
  juce::Rectangle<int> stripLayoutArea;
  int scrollBandHeight = 0;

  /// Whether the scroll is being driven by a drag on the bar itself, in which
  /// case the bar is left where the pointer has it.
  bool barIsDriving = false;

  void syncScrollBar();

  /// The vertical scroll bar at the far right, shown only when there is
  /// something to scroll.
  juce::ScrollBar parameterBar{true};

  /// The chrome height the current limits were worked out for.
  /// setResizeLimits resizes too, so resized() and applyResizeLimits call
  /// each other. Reapplying only when this changes stops the loop.
  int limitsChromeHeight = -1;

  /// Counts timer callbacks, so the meters and the housekeeping can each run
  /// at their own fraction of it.
  int tick = 0;

  /// Steps back or forward through the history.
  void stepHistory(bool redo);

  // ---- juce::ComponentListener ----
  //
  // On the standalone's window, so its title bar contents can be put back
  // after it lays them out.
  void componentMovedOrResized(juce::Component &, bool moved,
                               bool resized) override;

  /// Centres the standalone's Options button in its title bar, since JUCE
  /// places it low and offers no hook. Moved back after each layout.
  void centreWindowOptionsButton(juce::DocumentWindow &);

  int hoverStrip = -1;
  ovt::ui::Row hoverRow = ovt::ui::kNoRow;

  /// A rotary drag hides the pointer, so the positions it reports mean
  /// nothing. The highlight stays where it was grabbed until the drag ends.
  bool hoverLocked = false;

  /// How many rectangles the mixer may invalidate in one frame. Few enough
  /// that a window manager keeps them rather than repainting their bounding
  /// box.
  static constexpr int kMaxDirtyRegions = 6;

  /// Reused every frame rather than reallocated, since this runs at 30 Hz.
  juce::Array<juce::Rectangle<int>> dirtyRegions;

  /// The lamp bands for a whole frame, and the scratch one strip fills.
  /// Members so a frame does no allocating.
  juce::Array<juce::Rectangle<int>> lampRegions, stripLamps;

  bool propagatingLink = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumEditor)
};
