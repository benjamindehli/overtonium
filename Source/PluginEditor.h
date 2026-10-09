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
#include "UI/TopBar.h"
#include "dsp/Drift.h"

/// Left-hand caption column. Lays out the same rows as a channel strip so every
/// knob in the mixer has a name without repeating it 32 times.
class RowGutter : public juce::Component {
public:
  RowGutter();

  void paint(juce::Graphics &) override;

  /// Brightens the caption for the row the pointer is on, which is the point of
  /// the whole highlight: the name of the knob you are holding, thirty channels
  /// away, is the one thing that lights up.
  void setHighlightedRow(ovt::ui::Row);

  /// Which sections are folded, so the captions of folded rows are not drawn
  /// and the headings can show which way they point.
  void setCollapsedSections(ovt::ui::SectionMask);

  /// How far the parameters are scrolled, shared by every column.
  ///
  /// The gutter, the 32 strips and the noise channel are handed the same
  /// number by the editor, which is what keeps a caption pointing at the knob
  /// beside it. Snapped to a row boundary inside layoutRows, so a row is never
  /// half over the header.
  void setScroll(int);

  /// Which of the two modulators are one circuit the whole keyboard hears.
  ///
  /// Lights that group's heading. The switch is part of the patch and lives in
  /// a menu, so without this a preset could arrive with a shared tremolo and
  /// nothing on the panel would say so: a mode you cannot see is a mode you
  /// forget you are in. One mark per modulator rather than one on each of the
  /// thirty-three shape buttons, since the state is the same on all of them.
  ///
  /// Also lights the P cap beside each modulator's shape caption, which is the
  /// same switch. The heading keeps its light for when the section is folded
  /// and the cap is folded away with it.
  void setSharedModulators(bool pitch, bool amp);

  /// Fired when one of the P caps is clicked. The editor owns the change, since
  /// it is a parameter.
  std::function<void()> onPitchInPhaseClicked;
  std::function<void()> onAmpInPhaseClicked;

  /// The two glide switches, beside the GLIDE caption: whether a note glides
  /// only legato, and whether every glide takes a fixed time rather than
  /// moving at a fixed rate. Both are part of the patch and live in the glide
  /// knob's menu too, so a preset can throw them, and the caps say so.
  void setGlideSwitches(bool legato, bool fixedTime);

  /// Fired when one of those caps is clicked. The editor owns the change,
  /// since it is a parameter.
  std::function<void()> onGlideLegatoClicked;
  std::function<void()> onGlideFixedTimeClicked;

  /// Fired when a heading is clicked. The editor owns the decision, since the
  /// strips have to be told about it too.
  std::function<void(ovt::ui::Section)> onSectionToggled;

  /// Fired as the pointer moves over the gutter's own headings.
  ///
  /// The gutter has always been a passive display, lit by whichever strip the
  /// pointer was on. Its headings are clickable, though, so pointing at one
  /// has to light it in the same way pointing at its rule on a strip does, and
  /// the only thing that can light the strips as well is the editor.
  std::function<void(ovt::ui::Row)> onHoverChanged;

  /// Fired when the LINK button is clicked, with the button to hang the menu
  /// off. The menu itself belongs to the bar, which owns the settings it
  /// changes.
  std::function<void(juce::Component *)> onLinkClicked;

  /// Lights the button while LINK is on.

  /// Fired when the DRAW button is clicked, which latches the tool on rather
  /// than needing the modifier held.
  std::function<void()> onDrawClicked;

  /// Lights the button while a drag across the faders would draw them, whether
  /// that is because the modifier is held or because it is latched.
  /// Which tool the button wears, and the cursor to wear for it.
  void setTool(ovt::ui::PointerTool, ovt::ui::LinkCurve);

  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;

private:
  ovt::ui::Row highlighted = ovt::ui::kNoRow;
  ovt::ui::SectionMask collapsed = 0;
  int scroll = 0;

  /// Hides a row that has scrolled under the pinned header. See HeaderCap. The
  /// LINK button lives in the header too and is kept in front of it.
  ovt::ui::HeaderCap headerCap;

  void paintHeaderBand(juce::Graphics &);
  bool sharedPitchMod = false, sharedAmpMod = false;

  /// LINK stands in the empty band above the captions, where the strips beside
  /// it carry their channel numbers.
  ///
  /// Here rather than in the bar because this is the column the tool belongs
  /// to: it gangs the rows the captions name. What it leaves behind on the bar
  /// is the room the converter readouts needed to say what their numbers mean.
  /// One button for the three tools, wearing the cursor rather than a word.
  /// See ui::PointerTool.
  ovt::ui::GlowButton toolButton;

  /// The glide switches, a pair standing just left of the GLIDE caption. Lit
  /// is on.
  ovt::ui::ScreenSwitch glideLegato, glideFixedTime;

  /// The in-phase switches, one just left of each shape caption. Lit is one
  /// modulator the whole keyboard hears.
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
  /// gutter's switch, the glow that previews a drag, and the pointer.
  void syncLinkUi();

  /// Whether a drag across the faders would draw them. Not whether one is
  /// under way: this is what the pointer, the two gutter switches and the LINK
  /// preview answer to before anything is grabbed.
  ///
  /// Either the modifier is held or the tool is latched on, and the panel does
  /// not distinguish: both arm it and both light the same button.
  bool drawArmed = false;
  bool shiftHeld = false;
  bool drawLatched = false;

  /// Read on the housekeeping tick rather than waited for. See the body.
  void pollDrawModifier();
  void refreshDrawArmed();
  void toggleDrawLatch();

  /// Writes which tool is selected into the session.
  ///
  /// Both halves of it, because the tool is derived from two flags and
  /// remembering it means remembering both. Everything that moves either of
  /// them goes through here rather than writing its own, which is what the
  /// fault was: one path wrote one flag and the others wrote neither.
  void rememberTool();

  /// Whether a drawn drag is under way, as opposed to merely possible.
  bool drawingNow = false;

  /// Where the drawing last reached, so the columns in between can be filled
  /// in. See drawMovedTo.
  juce::Point<int> lastDrawn;

  bool drawStarted(juce::Point<int>) override;
  void drawMovedTo(juce::Point<int>) override;
  void drawEnded() override;

  std::vector<juce::RangedAudioParameter *> faderParameters() const;
  void applyDrawAt(juce::Point<int>);
  void linkDragStarted(ovt::ui::Role, int sourceIndex) override;
  void linkValueChanged(ovt::ui::Role, int sourceIndex,
                        float plainValue) override;
  void linkDragEnded(ovt::ui::Role, int sourceIndex) override;

  void showLinkMenu(const juce::String &parameterId) override;

  /// The learn items on their own, for the controls that are not part of the
  /// series LINK gangs: everything on the bar, and the noise channel.
  void showLearnMenu(const juce::String &parameterId);

  /// Keeps the waiting marker on whichever control the map is listening for.
  ///
  /// Polled rather than pushed, because the thing that ends the wait is a
  /// controller message arriving on the audio thread, which is no place to be
  /// repainting from. See ovt::MidiLearn.
  void followArmedControl();

  /// Lends every control a macro drives that macro's colour.
  ///
  /// Worked out here rather than by the strips, because which macro owns a
  /// control is a fact about all eight of them and no strip can see more than
  /// its own column. Pushed the way the LINK glow already is.
  void followMacroTints();

  /// What carries the marker now, so it can be taken off again without
  /// searching the window for anything that might have one.
  juce::String armedParameter;
  bool scrollParameters(int delta) override;

  /// A wheel that reached the editor, which is one over the gutter or the
  /// noise channel. Those two sit outside the mixer's viewport, so their
  /// wheels arrive here by bubbling rather than being routed, and they have to
  /// scroll the parameters like everything else.
  void mouseWheelMove(const juce::MouseEvent &,
                      const juce::MouseWheelDetails &) override;

  void scrollBarMoved(juce::ScrollBar *, double newStart) override;

  // ---- ovt::ui::HoverTarget ----
  void hoverChanged(int stripIndex, ovt::ui::Row) override;

  /// Sets the window back to the size that shows all 32 channels.
  ///
  /// A window remembers what it was left at, which is what a window should do
  /// and is also a one-way trip: drag it narrow, close it, and every session
  /// after that opens narrow. This is the way back, and it is in the Settings
  /// menu beside the zoom because both are about the window rather than about
  /// the instrument.
  ///
  /// Public so a test can ask for it. The menu that offers it needs a window
  /// to open in, which a test has no way of giving it.
  void fitAllChannels();

  /// Which of the three tools a drag would use right now.
  ///
  /// Derived rather than stored, from LINK's own switch and whether drawing
  /// is armed, so there is no fourth place for the three to disagree. Drawing
  /// wins, including when it is armed by holding the modifier rather than
  /// chosen.
  ovt::ui::PointerTool currentTool() const;

  /// Moves whichever of those two a chosen tool means.
  ///
  /// Public for the same reason fitAllChannels is: what offers it is a menu,
  /// and a menu needs a window a test has no way of giving it.
  void chooseTool(ovt::ui::PointerTool);

  /// Puts this editor's look and feel and its panel colour on a window.
  ///
  /// The standalone's window is JUCE's rather than the platform's, so left
  /// alone it wears the grey-green every unstyled JUCE app does, with a red
  /// cross and a yellow dash for its buttons. This puts the instrument's own
  /// panel across the top of it instead.
  ///
  /// Public because a test cannot reach it any other way: which wrapper this
  /// is running as is fixed by JUCE at construction and cannot be pretended,
  /// so a test hands it a window directly.
  void dressWindow(juce::DocumentWindow &);

private:
  void timerCallback() override;

  /// Finds the standalone's window, if this is the standalone, and dresses it.
  ///
  /// Only ever the standalone. In a host the top level window belongs to the
  /// host, and a plugin that restyled it would be redecorating someone else's
  /// application.
  void dressStandaloneWindow();

  void parentHierarchyChanged() override;

  void setZoom(float newZoom);

  /// The size that shows the whole mixer, at the fold state it is in.
  juce::Rectangle<int> standardSize() const;

  /// Sets the resize limits for a window of the given logical width.
  ///
  /// The height floor depends on the width, because the top bar reflows onto
  /// more rows as the window narrows and every row it takes is a row the
  /// mixer cannot have. The constrainer holds one number, not a curve, so the
  /// number has to follow the width rather than be picked for the worst case.
  ///
  /// The width is passed rather than read off the window because the two
  /// callers that change it call this before the change lands: a zoom has
  /// already updated the factor but not the bounds, and the fit is about to
  /// move to a width it is not at yet.
  void applyResizeLimits(int forLogicalWidth);

  /// The same, for the width the window has now.
  void applyResizeLimits();
  void applyPreset(int index);

  /// Records the name on the processor and shows it, in that order.
  ///
  /// Both halves, because the button is what you read and the processor is
  /// what remembers. A preset of your own has no program index, so this is the
  /// only thing that knows it was loaded.
  void setPresetName(const juce::String &name);

  /// Folds or unfolds one group of rows across the whole mixer, and takes the
  /// window's height with it.
  void toggleSection(ovt::ui::Section);

  /// Hands the current fold state to the gutter and every strip, which is the
  /// only way any of them find out about it.
  void publishCollapsedSections();

  /// Lights the heading of a modulator group the whole keyboard shares. See
  /// RowGutter::setSharedModulators.
  void syncSharedModulators();

  /// Asks once, the first time an editor is opened, whether to look for new
  /// versions, and remembers the answer. Nothing leaves the machine before
  /// someone has said yes.
  void offerUpdateCheck();

  /// Starts a check if the setting allows one.
  void maybeCheckForUpdates();

  /// Says something went wrong, or that something worked, without stopping
  /// what the message thread is doing.
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
  /// Latching matters: the offset is always measured from where things stood
  /// at the start, so returning the knob restores them exactly, and changing
  /// the scope or curve mid-drag cannot half-apply one rule and half another.
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

  /// The object the base class already holds, with its real type back on.
  ///
  /// AudioProcessorEditor keeps it as an AudioProcessor&, and declaring a
  /// second reference beside it shadows that one and stores the same address
  /// twice. Casting on the way past costs nothing and does neither.
  OvertoniumProcessor &plugin() const {
    return static_cast<OvertoniumProcessor &>(processor);
  }

  // Declared first so it outlives every component that borrows it.
  ovt::ui::OvertoniumLookAndFeel lookAndFeel;

  juce::TooltipWindow tooltips{this, 600};

  /// The standalone's window, once this editor has dressed it. Held so the
  /// listener can be taken off again: a look and feel is held weakly and
  /// looks after itself, where a listener is a raw pointer and would dangle.
  juce::Component::SafePointer<juce::DocumentWindow> standaloneWindow;

  /// Single child holding the whole UI, so zoom is one AffineTransform.
  juce::Component content;
  ovt::ui::TopBar topBar;
  RowGutter gutter;
  ovt::ui::NoiseStrip noiseStrip;

  /// Over the mixer when it is up, and not in the way when it is not.
  ovt::ui::MacroPanel macroPanel;

  // stripsHolder is declared before the viewport that displays it, so on
  // teardown the viewport is destroyed first and never sees a dangling viewed
  // component.
  juce::Component stripsHolder;
  juce::Viewport viewport;

  std::vector<std::unique_ptr<ovt::ui::ChannelStrip>> strips;

  float zoom = 1.0f;

  /// Which groups of rows are folded away. Restored from the saved state and
  /// written back when it changes, alongside the window size and the zoom.
  ovt::ui::SectionMask collapsedSections = 0;

  /// How far the parameters are scrolled, and how far they can be.
  ///
  /// One number for the whole mixer rather than one per column, because the
  /// gutter's captions name the knobs beside them and two columns that
  /// disagreed by a row would be captions pointing at the wrong controls. The
  /// range falls to zero in a window with room for everything, which is every
  /// window at 100% zoom, so scrolling is a thing that only appears when it is
  /// needed.
  int scrollY = 0;
  int scrollRange = 0;

  /// What scrollParameters needs and cannot work out for itself: the rectangle
  /// a column lays its rows in, and how tall the band is. Both are recorded by
  /// resized(), which is the only place the geometry is known.
  juce::Rectangle<int> stripLayoutArea;
  int scrollBandHeight = 0;

  /// Whether the scroll is being driven by a drag on the bar itself, in which
  /// case the bar is left where the pointer has it rather than moved to where
  /// the rows settled.
  bool barIsDriving = false;

  void syncScrollBar();

  /// The one thing on screen that says the parameters can move.
  ///
  /// Vertical, at the far right beyond the noise channel, which is where a
  /// scrollbar goes and cost the window ten pixels of width to put there.
  /// Shown only when there is something to scroll, so a window at 100% zoom
  /// with room for every row never sees it.
  juce::ScrollBar parameterBar{true};

  /// The bar height the limits in force were worked out for.
  ///
  /// Applying limits is itself a resize, since setResizeLimits ends by
  /// constraining the current bounds, so resized() and applyResizeLimits can
  /// call each other. This is what stops that: the limits are reapplied only
  /// when the bar has actually changed height, and once they have been, the
  /// resize that follows finds the same number and stops.
  int limitsBarHeight = -1;

  /// Counts timer callbacks, so the meters and the housekeeping can each run
  /// at their own fraction of it.
  int tick = 0;

  /// Steps back or forward through the history.
  void stepHistory(bool redo);

  // ---- juce::ComponentListener ----
  //
  // On the standalone's window, so its own title bar contents can be put back
  // where they belong after it has laid them out.
  void componentMovedOrResized(juce::Component &, bool moved,
                               bool resized) override;

  /// Centres the standalone's Options button in its title bar.
  ///
  /// The button belongs to JUCE's standalone window rather than to us, and it
  /// is placed at a fixed six pixels from the top of a bar whose height it
  /// then subtracts eight from, which leaves it sitting low whatever the bar
  /// is. There is no hook for it, so it is moved back after each layout.
  void centreWindowOptionsButton(juce::DocumentWindow &);

  int hoverStrip = -1;
  ovt::ui::Row hoverRow = ovt::ui::kNoRow;

  /// A rotary drag unbinds the pointer from the screen, so the positions it
  /// reports during one mean nothing. The highlight stays where it was grabbed
  /// until the drag ends.
  bool hoverLocked = false;

  /// How many rectangles the mixer is allowed to invalidate in one frame. Few
  /// enough that a window manager keeps them rather than falling back to their
  /// bounding box, which is the whole window.
  static constexpr int kMaxDirtyRegions = 6;

  /// Reused every frame rather than reallocated, since this runs at 30 Hz.
  juce::Array<juce::Rectangle<int>> dirtyRegions;

  /// The lamp bands for a whole frame, and the scratch one strip fills. Both
  /// members rather than locals so a frame does no allocating.
  juce::Array<juce::Rectangle<int>> lampRegions, stripLamps;

  bool propagatingLink = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OvertoniumEditor)
};
