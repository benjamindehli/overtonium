#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/ChannelStrip.h"
#include "UI/LookAndFeel.h"
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

  /// Fired when a heading is clicked. The editor owns the decision, since the
  /// strips have to be told about it too.
  std::function<void(ovt::ui::Section)> onSectionToggled;

  /// Fired when the LINK button is clicked, with the button to hang the menu
  /// off. The menu itself belongs to the bar, which owns the settings it
  /// changes.
  std::function<void(juce::Component *)> onLinkClicked;

  /// Lights the button while LINK is on.
  void setLinkOn(bool);

  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;

private:
  ovt::ui::Row highlighted = ovt::ui::kNoRow;
  ovt::ui::SectionMask collapsed = 0;

  /// LINK stands in the empty band above the captions, where the strips beside
  /// it carry their channel numbers.
  ///
  /// Here rather than in the bar because this is the column the tool belongs
  /// to: it gangs the rows the captions name. What it leaves behind on the bar
  /// is the room the converter readouts needed to say what their numbers mean.
  juce::TextButton linkButton;

  /// The maker's badge, in the empty foot of the gutter.
  std::unique_ptr<juce::Drawable> makersMark{ovt::ui::logoMakersMark()};
};

class OvertoniumEditor : public juce::AudioProcessorEditor,
                         public ovt::ui::LinkTarget,
                         public ovt::ui::HoverTarget,
                         private juce::Timer,
                         private juce::ValueTree::Listener {
public:
  explicit OvertoniumEditor(OvertoniumProcessor &);
  ~OvertoniumEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  bool keyPressed(const juce::KeyPress &) override;

  // ---- ovt::ui::LinkTarget ----
  bool isLinkEnabled() const override;
  void linkDragStarted(ovt::ui::Role, int sourceIndex) override;
  void linkValueChanged(ovt::ui::Role, int sourceIndex,
                        float plainValue) override;
  void linkDragEnded(ovt::ui::Role, int sourceIndex) override;

  void showLinkMenu() override;

  // ---- ovt::ui::HoverTarget ----
  void hoverChanged(int stripIndex, ovt::ui::Row) override;

  /// Closes off an undo transaction once the parameters have stopped moving.
  ///
  /// The alternative is hooking every parameter's gesture callbacks, which a
  /// host may call from the audio thread, and opening a transaction allocates.
  /// Watching for stillness instead needs no hooks and gives the same answer:
  /// a gesture is one step however many values it moved and however long it
  /// goes on, and letting go for a moment starts the next one.
  ///
  /// Public for the same reason the processor's pending program change is: it
  /// is driven by a timer, and a test has no message loop to run one.
  void closeUndoTransactionWhenIdle();

private:
  void timerCallback() override;

  void setZoom(float newZoom);
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

  /// Single child holding the whole UI, so zoom is one AffineTransform.
  juce::Component content;
  ovt::ui::TopBar topBar;
  RowGutter gutter;
  ovt::ui::NoiseStrip noiseStrip;

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

  /// Counts timer callbacks, so the meters and the housekeeping can each run
  /// at their own fraction of it.
  int tick = 0;

  /// Closes the open transaction, then steps back or forward.
  void stepHistory(bool redo);

  // ---- juce::ValueTree::Listener ----
  void valueTreePropertyChanged(juce::ValueTree &,
                                const juce::Identifier &) override;

  /// When a parameter last reached the state tree, by the millisecond counter.
  /// See closeUndoTransactionWhenIdle.
  juce::uint32 lastParameterMove = 0;

  /// How still the panel has to be before a gesture is closed off as one step.
  ///
  /// Long enough to sit between the notches of a scroll wheel turned at a
  /// deliberate pace, since that is how most of this instrument gets adjusted,
  /// and short enough that two edits you meant as two do not become one.
  static constexpr juce::uint32 kUndoIdleMs = 500;

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
