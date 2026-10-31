#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/Character.h"
#include "../dsp/Harmonics.h"
#include "../dsp/OutputStage.h"
#include "../dsp/Params.h"
#include "PaintCache.h"

namespace ovt::ui {

/// How many segments a meter of this height divides into, about sixteen.
///
/// Shared because the meter and the fader drawn over it both paint segments and
/// must agree to the pixel.
int meterSegments(int height);

namespace colours {
inline const juce::Colour background{0xff0b0d10};
inline const juce::Colour panel{0xff14181d};
inline const juce::Colour panelAlt{0xff15191e};

/// What every channel stands on, the noise channel included.
inline const juce::Colour channel{0xff0f1115};
inline const juce::Colour groove{0xff090b0e};
inline const juce::Colour outline{0xff222830};
inline const juce::Colour text{0xffd9dfe7};
inline const juce::Colour textDim{0xff77828d};
/// The global controls, in a cyan the channel colours never reach.
inline const juce::Colour accent{0xff62bbd9};
/// The lamp behind a lit mute button.
inline const juce::Colour muteOn{0xffee563f};
/// The lamp behind a lit solo button.
inline const juce::Colour soloOn{0xffe8c34a};

/// A control waiting for MIDI Learn, in a violet nothing else uses.
inline const juce::Colour learning{0xffb07bd4};
} // namespace colours

/// A tile of fine monochrome noise laid over the panels, built once and shared.
/// Deterministic, so screenshots are repeatable.
///
/// Held through a SharedResourcePointer, never a static: releasing a Direct2D
/// backed image under the Windows loader lock at unload hangs the host. The
/// last OvertoniumLookAndFeel holds it, so it dies with the editor.
struct GrainTile {
  GrainTile();
  juce::Image image;
};

/// How many times the tile has been built, so a test can check it is released
/// with the editor rather than kept for the life of the process.
int grainTileBuildCount();

/// The grain tile. Returned by value, since a reference would dangle when no
/// look and feel is alive to hold the shared tile.
juce::Image grainTile();

/// Lays the grain over an area at the weight the panels use.
void paintGrain(juce::Graphics &, juce::Rectangle<int> area);

/// Colour for an interval class, so the eye can group octaves, fifths and
/// thirds across 32 strips. The twelve classes run in chromatic order along a
/// band from blue through magenta and red to yellow.
juce::Colour intervalColour(int pitchClass);

/// One colour from that same band, from zero at the blue end to one at the
/// yellow end.
juce::Colour bandColour(float t);

/// The colour each oscillator character lights in. Pure does not light.
juce::Colour characterColour(Character);

/// The colour each machine of the resonator, the echo, the reverb and the
/// output stage lights in, spread as far apart in hue as each set allows.
juce::Colour resonatorColour(ResonatorMode);
juce::Colour echoColour(EchoType);
juce::Colour reverbColour(ReverbType);
juce::Colour clipColour(ClipType);

/// A switch drawn as a white plastic cap with a lamp behind it.
///
/// textColourOnId is the lamp colour, shown only when the button is on.
/// textColourOffId is not used. See OvertoniumLookAndFeel::lampFace.
class GlowButton : public juce::TextButton {
public:
  /// Drawn in place of the word, in the colour the word would have had. The
  /// word is still what a screen reader and the tooltip get.
  std::function<void(juce::Graphics &, juce::Rectangle<float>, juce::Colour)>
      onIcon;

  /// Says that what onIcon draws has changed, and repaints. Needed because the
  /// paint cache cannot compare functions and would show the old icon.
  void iconChanged() {
    ++iconRevision;
    repaint();
  }

  void paintButton(juce::Graphics &, bool highlighted, bool down) override;

private:
  /// Everything paintButton draws, before it is kept as a picture.
  void paintCap(juce::Graphics &, bool highlighted, bool down);

  /// See iconChanged.
  int iconRevision = 0;

  /// Its last two looks, kept as pictures.
  PaintCache looks;
};

/// A switch shown as a letter in a screen, for the gutter. On, the letter
/// lights in its lamp colour, and off it is a faint print on the dark glass.
class ScreenSwitch : public juce::Button {
public:
  ScreenSwitch() : juce::Button({}) {}

  /// What the letter lights in. Accent unless told otherwise.
  juce::Colour lamp = colours::accent;

  /// Drawn in place of the letter, in the colour the letter would have had.
  std::function<void(juce::Graphics &, juce::Rectangle<float>, juce::Colour)>
      onIcon;

  void paintButton(juce::Graphics &, bool highlighted, bool down) override;
};

enum class PointerTool : int;

/// The icons the bar and the gutter wear, drawn rather than loaded so
/// they take the colour they are given and stay sharp at any zoom.
void drawGearIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
void drawMacroIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
/// A few keys of a keyboard and a tuning fork, for the two buttons that open
/// the Playing and Tuning bars.
void drawKeyboardIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
void drawTuningForkIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
/// Three strokes merging into one: several notes hearing one modulator. For
/// the gutter switch that shares a modulator between all notes.
void drawSharedIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
/// The arrow, alone for the plain pointer and with a mark beside it for the
/// other two. Drawn rather than taken from the cursor images, whose offsets
/// from their hotspots differ.
void drawToolIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour,
                  PointerTool);

/// Vertical slots in a channel strip. The gutter on the left lays out the same
/// list so the row labels always line up with the controls.
enum class Row {
  Header = 0,
  TuneKnob,
  TuneText,
  PitchModHeading,
  PmShape,
  PmRate,
  PmDepth,
  Drift,
  Glide,
  EnvHeading,
  Strike,
  Delay,
  Attack,
  Decay,
  Sustain,
  KeyOffHeading,
  Swell,
  OffLevel,
  Release,
  AmpModHeading,
  AmShape,
  AmRate,
  AmDepth,
  OutputHeading,
  Velocity,
  Aftertouch,
  Pan,
  MuteSolo,
  Fader,
  FaderText,
  NumRows
};

inline constexpr int kNumRows = (int)Row::NumRows;
inline constexpr int kStripWidth = 38;
inline constexpr int kGutterWidth = 78;

/// The scrollbar beyond the noise channel, whose width the window reserves
/// whether or not it is showing. The window's opening width is built from it.
inline constexpr int kScrollBarThickness = 10;

/// The inset every column lays its rows inside. The gutter and every strip
/// must agree on it to the pixel so the captions line up. The vertical figure
/// is also the mixer's top and bottom margin.
inline constexpr int kStripPadX = 2;
inline constexpr int kStripPadY = 9;

/// Stands for "no row", which is what a hover over a heading or a gap reports.
inline constexpr Row kNoRow = Row::NumRows;

using RowBounds = std::array<juce::Rectangle<int>, kNumRows>;

/// The groups of rows that can be folded away to get the mixer's height down.
///
/// Each is named by its heading row, which stays shown when folded so it can
/// be clicked to unfold. The tuning rows and the fader rows are never folded.
enum class Section {
  PitchMod = 0,
  Envelope,
  KeyOff,
  AmpMod,
  Output,
  NumSections
};

inline constexpr int kNumSections = (int)Section::NumSections;

/// Which sections are folded, one bit each. One mask for the whole mixer, so
/// every strip keeps in line with the gutter captions.
using SectionMask = unsigned int;

inline constexpr SectionMask sectionBit(Section s) {
  return (SectionMask)1 << (int)s;
}

inline constexpr bool isCollapsed(SectionMask mask, Section s) {
  return (mask & sectionBit(s)) != 0;
}

/// Whether this row is one of the five that name a section.
bool isHeadingRow(Row);

/// The heading row that names a section, and the reverse.
Row sectionHeading(Section);

/// The section a row is folded away with, or NumSections for a row that always
/// shows. A heading belongs to its own section but never hides with it.
Section sectionOf(Row);

/// Whether this row is hidden under the given mask.
bool rowIsCollapsed(Row, SectionMask);

/// How tall a row stands when it is shown. For a control placed off the rows
/// that still wants to be the size the rows make everything else.
int rowHeight(Row);

/// The section whose heading row is under this point, or NumSections.
Section headingSectionAt(const RowBounds &, juce::Point<int>);

/// How a column's height divides when the parameters can scroll: the channel
/// header pinned at the top, and everything below it scrolling as one list.
struct Bands {
  juce::Rectangle<int> header; ///< Row::Header, pinned
  juce::Rectangle<int> middle; ///< the opening the rest is seen through

  /// What the rows need, with the fader at whatever height it has here.
  int contentHeight = 0;

  /// How far the middle can be scrolled, which is zero when it all fits.
  int maxScroll = 0;

  /// The fader's height, which is its ideal until everything fits and the
  /// slack has nowhere else to go.
  int faderHeight = 0;
};

/// What the scrolling band holds: every row but the header, with the fader at
/// its ideal height.
int middleContentHeight(SectionMask collapsed = 0);

/// Divides a column into its two bands.
///
/// At or above the preferred height every row is shown and the fader takes the
/// slack. Below it the fader keeps its ideal height and the rows scroll.
Bands layoutBands(juce::Rectangle<int> area, SectionMask collapsed = 0);

/// Every offset the band can rest at: the top of each scrolling row, so
/// nothing is left half under the pinned header.
std::vector<int> scrollStopsFor(juce::Rectangle<int> area,
                                SectionMask collapsed);

/// The bounds of every row in a column, scrolled by `scroll` pixels.
RowBounds layoutRows(juce::Rectangle<int> area, SectionMask collapsed = 0,
                     int scroll = 0);

/// The row a point in a strip belongs to, or kNoRow for the header and the
/// section rules. The two readouts answer with the control above them.
Row controlRowAt(const RowBounds &, juce::Point<int>);

/// Whether a row takes the pointer highlight. The faders and the mute and
/// solo buttons do not, though the fader still reports its hover to LINK.
bool rowShowsHighlight(Row);

/// Repaints the band a row highlights, if it highlights one at all.
void repaintRowHighlight(juce::Component &, const RowBounds &, Row);

/// Merges rectangles that share a top and a bottom edge into one band each.
///
/// For the lamps, which sit at the same heights on every strip. Merging them
/// by row repaints far fewer pixels than coalesceRegions would.
void mergeIntoRows(juce::Array<juce::Rectangle<int>> &regions);

/// Reduces a set of dirty rectangles to at most `limit` of them, merging the
/// pairs that add the least area. The result covers every rectangle that went
/// in.
///
/// A few rectangles, because the window manager repaints the bounding box of
/// many, and one merged rectangle is nearly the whole mixer.
void coalesceRegions(juce::Array<juce::Rectangle<int>> &regions, int limit);

/// The height the folded rows are no longer taking, which is what the window
/// shrinks by when a section is folded.
int collapsedRowsHeight(SectionMask collapsed);

/// Total height a strip needs before the fader starts being squeezed,
/// padding included.
int preferredStripHeight(SectionMask collapsed = 0);

/// Height below which the fader would be unusably short.
int minimumStripHeight(SectionMask collapsed = 0);

/// Left-gutter caption for a row, or nullptr for rows that need no caption.
const char *rowLabel(Row r);

/// The per-strip controls that the LINK switch ganged across all 32 channels.
enum class Role {
  Tune = 0,
  Glide,
  PmRate,
  PmDepth,
  Drift,
  Strike,
  Delay,
  Attack,
  Decay,
  Sustain,
  Swell,
  OffLevel,
  Release,
  AmRate,
  AmDepth,
  Velocity,
  Aftertouch,
  Pan,
  Volume,
  NumRoles
};

inline constexpr int kNumRoles = (int)Role::NumRoles;

/// Maps a role onto the matching parameter-ID suffix from ovt::params.
const char *roleSuffix(Role r);

/// What a control is called for a screen reader. Unlike rowLabel, which can
/// repeat itself, every name here is distinct.
const char *roleLabel(Role r);

/// The role a parameter suffix belongs to, or NumRoles if none does. Lets the
/// noise channel take its spoken names from the same table.
Role roleForSuffix(const char *suffix);

/// The role a row carries, if it carries one at all.
///
/// @returns false for the mute and solo row, which holds buttons rather than a
/// value LINK could gang, and for every row with no control on it.
bool roleForRow(Row, Role &out);

/// Which channels a LINK drag reaches.
enum class LinkScope {
  All = 0,
  SameInterval, ///< only strips sharing the dragged one's interval class
  Odd,          ///< odd harmonic numbers, the hollow half of the series
  Even,
  Selected, ///< the channels picked by their numbers, whichever they are
  NumScopes
};

/// How a LINK drag is distributed across the strips it reaches.
enum class LinkCurve {
  Uniform = 0, ///< every strip moves by the same amount
  Taper,       ///< strips move less the further they sit from the one grabbed
  Spread,      ///< pushing up scatters them, pulling down gathers them
  NumCurves
};

const char *linkScopeName(LinkScope);
const char *linkCurveName(LinkCurve);

/// The curve as a saved state writes it. A name rather than an index, so the
/// list can change without breaking saved states.
const char *linkCurveId(LinkCurve);

/// Reads the curve back from a saved state.
///
/// @param id      what the current property held, empty if it is absent
/// @param legacy  what the older integer property held, negative if absent.
///                Its order was Uniform, Tilt up, Tilt down, Spread, and both
///                tilts read as Taper.
LinkCurve linkCurveFromState(const juce::String &id, int legacy);

/// What a drag in the mixer does.
enum class PointerTool : int { Pointer = 0, Link, Draw, NumTools };

const char *pointerToolName(PointerTool);

/// The cursor that tool gives.
juce::Image pointerToolImage(PointerTool, LinkCurve, float scale);

/// Everything the LINK switch is currently set to.
struct LinkSettings {
  bool enabled = false;
  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;

  /// Whether any channel is selected, which is all the menu needs to know to
  /// offer clearing the selection.
  bool hasSelection = false;
};

/// The LINK menu, shared by the bar and the mixer's right-click, and built
/// here so tests can check it without a window.
juce::PopupMenu buildLinkMenu(const LinkSettings &);

/// The tool menu: which of the three, and under a separator the settings
/// belonging to whichever is chosen.
juce::PopupMenu buildToolMenu(PointerTool, const LinkSettings &);

/// @return true when the id was a tool rather than one of LINK's settings,
///         in which case `tool` has been moved.
bool applyToolMenuChoice(int id, PointerTool &tool);

/// Applies what the menu came back with.
///
/// @returns false when the id was not one of ours, which includes the 0 that
/// means the menu was dismissed.
bool applyLinkMenuChoice(int id, LinkSettings &);

/// Whether the menu came back asking for the selection to be cleared. That is
/// not one of LINK's settings but an action on the channels, so it is asked
/// about separately rather than folded into applyLinkMenuChoice.
bool isClearSelectionChoice(int id);

/// Weight applied to a strip's share of a LINK drag.
///
/// The dragged strip always gets exactly 1, so the knob under the mouse
/// follows the mouse. Taper falls off with distance from the dragged strip,
/// down to nothing thirty-one channels away.
float linkCurveWeight(LinkCurve, int index0, int sourceIndex);

/// Where one linked strip lands partway through a LINK drag.
///
/// A zero delta must return the baseline exactly for every curve, so a drag
/// can be undone by returning the knob.
///
/// @param delta     how far the dragged knob has moved, in the space below
/// @param weight    this strip's share, from linkCurveWeight
/// @param jitter    a fixed direction in [-1, 1], only used by Spread
/// @param target    the dragged strip's live value, gathered towards by Spread
/// @param low,high  the ends of the space this is working in
///
/// The space is normally the knob's travel, nought to one. See linkIsDecibels
/// for the exception.
float linkedValue(LinkCurve, float baseline, float delta, float weight,
                  float jitter, float target, float low = 0.0f,
                  float high = 1.0f);

/// Whether a drag on this row should be shared out in decibels rather than
/// across the knob's travel.
///
/// True of the faders under Uniform and Taper, because the fader's travel is
/// square-law into gain. Spread stays in the travel.
inline bool linkIsDecibels(Role r, LinkCurve c) {
  return r == Role::Volume && c != LinkCurve::Spread;
}

/// Implemented by the editor, lets a strip say where the pointer is so the
/// mixer and the gutter can highlight that row.
struct HoverTarget {
  virtual ~HoverTarget() = default;

  /// @param stripIndex  the partial under the pointer, or -1 for the noise
  ///                    channel, which sits outside the series.
  virtual void hoverChanged(int stripIndex, Row row) = 0;
};

/// An opaque lid over a column's pinned header, so a control scrolled under
/// it is neither seen nor clickable.
class HeaderCap final : public juce::Component {
public:
  /// What to draw. Given the column's own coordinates, since the cap sits at
  /// the column's top left and shares them.
  std::function<void(juce::Graphics &)> onPaint;

  void paint(juce::Graphics &g) override {
    if (onPaint)
      onPaint(g);
  }
};

/// Implemented by the editor, lets a strip broadcast a drag to its 31 siblings
/// and reach the gestures that cross strips.
struct LinkTarget {
  virtual ~LinkTarget() = default;

  virtual bool isLinkEnabled() const = 0;
  virtual void linkDragStarted(Role role, int sourceIndex) = 0;
  /// @param plainValue  the un-normalised value. The editor converts it per
  /// destination.
  virtual void linkValueChanged(Role role, int sourceIndex,
                                float plainValue) = 0;
  virtual void linkDragEnded(Role role, int sourceIndex) = 0;

  /// Pops the LINK menu under the pointer, for a right-click in the mixer.
  ///
  /// @param parameterId  what the click landed on, empty if it landed on
  ///                     nothing a controller could move. The menu grows its
  ///                     MIDI Learn items from this. See ui::learn.
  virtual void showLinkMenu(const juce::String &parameterId) = 0;

  /// A wheel over a column, which scrolls the parameters. Through the editor
  /// because every column and the gutter must scroll together.
  ///
  /// @param delta  pixels to move by, positive downward. Snapped to a row
  ///               boundary by the layout, so the caller need not.
  /// @returns whether it was taken. False means there is nothing to scroll,
  ///          and the wheel falls through to the viewport to move sideways.
  virtual bool scrollParameters(int delta) = 0;

  /// A drag held with a modifier, which draws the faders it passes over
  /// instead of moving one. Through the editor because the pointer stays with
  /// the strip the drag began on.
  ///
  /// @param onScreen  where the pointer is, in screen coordinates
  /// @returns whether the drag was taken. False leaves the fader to move
  ///          itself, which is what happens when the modifier is not held.
  virtual bool drawStarted(juce::Point<int> onScreen) = 0;
  virtual void drawMovedTo(juce::Point<int> onScreen) = 0;
  virtual void drawEnded() = 0;

  /// A command-click on a channel's number, which toggles it in the
  /// selection, and a drag from there across the numbers, which toggles every
  /// channel it crosses. Through the editor for the same reason as drawing.
  ///
  /// @param index0    the channel whose number was clicked, from zero
  /// @param onScreen  where the pointer is, in screen coordinates
  virtual void selectionStarted(int index0, juce::Point<int> onScreen) {
    juce::ignoreUnused(index0, onScreen);
  }
  virtual void selectionMovedTo(juce::Point<int> onScreen) {
    juce::ignoreUnused(onScreen);
  }
  virtual void selectionEnded() {}
};

} // namespace ovt::ui
