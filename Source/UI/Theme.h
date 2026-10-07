#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/Character.h"
#include "../dsp/Harmonics.h"

namespace ovt::ui {

/// How many segments a meter of this height divides into.
///
/// Here rather than on the meter because two drawings have to agree about it:
/// the meter paints the segments and the fader laid over the same rectangle
/// paints its own in the gutters either side, and sharing the rectangle is not
/// enough. They have to share the arithmetic, or the gutters drift out of step
/// with the track by a pixel and read as a second scale beside it.
///
/// Aiming at sixteen, but a short window gets fewer rather than a column of
/// slivers, and a tall one gets more rather than bars.
int meterSegments(int height);

namespace colours {
inline const juce::Colour background{0xff0b0d10};
inline const juce::Colour panel{0xff14181d};
inline const juce::Colour panelAlt{0xff181d23};

/// What every channel stands on, the noise channel included.
///
/// One shade rather than two. The strips used to alternate between panel and
/// panelAlt so you could tell one from the next, which was worth having when
/// a channel was a column of grey knobs. It is not any more: every knob
/// carries its interval colour, the meters and the lamps are lit, and a
/// stripe running behind all of that is one more thing competing with it. The
/// strips are still told apart by their own lit and shadowed edges, which is
/// how a console does it.
inline const juce::Colour channel{0xff12151a};
inline const juce::Colour groove{0xff090b0e};
inline const juce::Colour outline{0xff272e37};
inline const juce::Colour text{0xffd9dfe7};
inline const juce::Colour textDim{0xff6f7a86};
/// Chrome, not content. Sits in the cyan the channel ramp never reaches, so
/// the global controls never read as one of the channels.
inline const juce::Colour accent{0xff62bbd9};
/// Red rather than orange, because red is what a cut channel means. It is
/// the lamp behind the cap rather than the plastic, which is white: an unlit
/// mute shows none of this and a lit one shows all of it.
inline const juce::Colour muteOn{0xffe04831};
inline const juce::Colour soloOn{0xffe8c34a};

/// A control waiting to be told which controller moves it.
///
/// Its own colour rather than one borrowed, because all three of the others
/// are already saying something that could be true of the same control at the
/// same moment: the accent is what LINK warms and what the draw tool lights,
/// red is a cut channel and amber a soloed one. A violet is in none of those
/// conversations, and nothing else in the window uses it.
inline const juce::Colour learning{0xffb07bd4};
} // namespace colours

/// A tile of fine monochrome noise, built once and shared.
///
/// Real panels are not perfectly smooth, and a flat fill is the main thing
/// that reads as a drawing rather than an object. Deterministic, so two
/// windows of the same size are identical and a screenshot test can rely on
/// it.
///
/// Held through a SharedResourcePointer rather than in a static, and the
/// difference is not tidiness. A juce::Image on JUCE 9 under Windows is backed
/// by Direct2D, so releasing one gives GPU resources back. A static is
/// released when the host unloads the binary, which on Windows runs under the
/// loader lock, and the driver threads that teardown has to reach are
/// themselves parked waiting for that lock. It never completes. That is a hang
/// on removing the plugin in any host that unloads it, which is what was
/// reported in FL Studio and in Reaper while MuLab, which keeps the binary
/// loaded, was fine.
///
/// So the lifetime ends with the last OvertoniumLookAndFeel instead, which
/// holds one of these and dies with its editor, while the message thread is
/// running and the binary is still loaded. See [[grainHolder]] on that class.
struct GrainTile {
  GrainTile();
  juce::Image image;
};

/// How many times the tile has been built.
///
/// For a test, and the only way to ask the question from outside: that the
/// tile is released and built again rather than living for the life of the
/// process is the whole point of the change above, and it is invisible in
/// every other respect.
int grainTileBuildCount();

/// Returned by value, cheaply, because a juce::Image is a counted handle on
/// its pixels. A reference would dangle in the one case that matters: called
/// with no look and feel alive, the shared tile would be built, referenced and
/// destroyed before the caller saw it.
juce::Image grainTile();

/// Lays the grain over an area at the weight the panels use.
void paintGrain(juce::Graphics &, juce::Rectangle<int> area);

/// Colour for an interval class, so the eye can group octaves, fifths and
/// thirds while scanning 32 strips.
///
/// The twelve classes are spread in chromatic order across a narrow band that
/// runs from blue, through magenta and red, to yellow. Narrow enough that the
/// mixer reads as one family rather than a rainbow, and placed clear of the
/// green and cyan the global accent uses.
juce::Colour intervalColour(int pitchClass);

/// One colour from that same band, by where along it you want to stand.
///
/// Zero is the blue end and one the yellow, which is the interval colours'
/// own scale with the twelve steps taken off it. Anything that wants to
/// borrow the mixer's palette without being a pitch class asks here, so there
/// is one band rather than two that drift apart.
juce::Colour bandColour(float t);

/// Where each oscillator character stands on it.
///
/// Yellow for the gentlest and red for the hardest, running down the warm end
/// of the band and finishing exactly on the fifth, which is the colour of
/// channel 3. Pure is not on the band at all: it is the absence of a
/// character rather than one of them, and it does not light.
juce::Colour characterColour(Character);

/// A lamp behind a square plastic cap, the way a tape machine's are.
///
/// Every switch on the panel is one of these: the two effect toggles, the
/// character button, the two on the bar that open something, the tool, and
/// the M and S on all thirty-three channels. The cap sits in a moulded well
/// and the whole of it lights, so what the eye finds scanning 33 channels is
/// a lit square rather than a letter it would have to read.
///
/// The plastic is white and the same on every one of them. What a button
/// names in textColourOnId is the lamp behind it, which is what it shows
/// when it is on and nothing at all when it is off, so a button that never
/// lights need not name one. The word is printed on the cap in the same ink
/// whatever the lamp is doing, textColourOffId reaching nothing here. See
/// OvertoniumLookAndFeel::lampFace and lampLegend.
class GlowButton : public juce::TextButton {
public:
  /// Drawn in place of the word, in the colour the word would have had.
  ///
  /// For the buttons whose names are longer than the bar can spare: a gear
  /// and a rack of faders say settings and macros in a third of the width
  /// the words want, and the words are still what a screen reader is given
  /// and what the tooltip says.
  std::function<void(juce::Graphics &, juce::Rectangle<float>, juce::Colour)>
      onIcon;

  void paintButton(juce::Graphics &, bool highlighted, bool down) override;
};

enum class PointerTool : int;

/// The three icons the bar and the gutter wear, drawn rather than loaded so
/// they take the colour they are given and stay sharp at any zoom.
void drawGearIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
void drawMacroIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
/// The arrow, alone for the plain pointer and with a mark beside it for the
/// other two.
///
/// Drawn here rather than taken from the cursors themselves, which is what
/// it did first and what did not work: a cursor's artwork hangs down and to
/// the right of its hotspot, so each one landed at a different size and a
/// different place inside the button. One arrow at one size, with the mark
/// standing to its right and centred on it, gives the three the same height
/// and the same footing.
void drawToolIcon(juce::Graphics &, juce::Rectangle<float>, juce::Colour,
                  PointerTool);

/// Vertical slots in a channel strip. The gutter on the left lays out the same
/// list so the row labels always line up with the controls.
enum class Row {
  Header = 0,
  TuneKnob,
  TuneText,
  Phase,
  PitchModHeading,
  PmShape,
  PmRate,
  PmDepth,
  Drift,
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
/// whether or not it is showing. Here rather than in PluginEditor.cpp
/// because the width the window opens at is built from it, and a test that
/// worked that width out without it was ten pixels adrift.
inline constexpr int kScrollBarThickness = 10;

/// The inset every column lays its rows inside.
///
/// Named because the gutter, the 32 strips and the noise strip all have to
/// agree about it to the pixel: they each call layoutRows with their own
/// rectangle, and a caption that disagrees with the knob beside it by one
/// pixel is a caption pointing at the wrong row further down.
///
/// The vertical figure is also the mixer's top and bottom margin. The area
/// below the bar has no border of its own, so the breathing room lives inside
/// the channels instead of around them.
inline constexpr int kStripPadX = 2;
inline constexpr int kStripPadY = 9;

/// Stands for "no row", which is what a hover over a heading or a gap reports.
inline constexpr Row kNoRow = Row::NumRows;

using RowBounds = std::array<juce::Rectangle<int>, kNumRows>;

/// The groups of rows that can be folded away to get the mixer's height down.
///
/// Each is named by its heading row, which stays put when the group is folded
/// so there is something left to click to bring it back. The rows above the
/// first heading, and the fader and its neighbours at the foot, are not in a
/// section: the tuning is what the instrument is for and the fader is what you
/// mix with, so neither is ever the thing in the way.
enum class Section {
  PitchMod = 0,
  Envelope,
  KeyOff,
  AmpMod,
  Output,
  NumSections
};

inline constexpr int kNumSections = (int)Section::NumSections;

/// Which sections are folded, one bit each.
///
/// A whole-mixer property rather than a per-strip one, because the strips are
/// columns sharing one set of rows. Folding a section on one channel and not
/// the next would put every row below it out of step with the gutter captions,
/// which are the only thing naming the knobs.
using SectionMask = unsigned int;

inline constexpr SectionMask sectionBit(Section s) {
  return (SectionMask)1 << (int)s;
}

inline constexpr bool isCollapsed(SectionMask mask, Section s) {
  return (mask & sectionBit(s)) != 0;
}

/// Whether this row is one of the five that name a section.
///
/// Shared rather than answered again in each file that asks, which is now the
/// gutter and both kinds of strip: a heading is drawn differently, highlighted
/// differently and clicked for a different reason, so three places have to
/// agree about which rows they are.
bool isHeadingRow(Row);

/// The heading row that names a section, and the reverse.
Row sectionHeading(Section);

/// The section a row is folded away with, or NumSections for a row that always
/// shows. A heading belongs to its own section but never hides with it.
Section sectionOf(Row);

/// Whether this row is hidden under the given mask.
bool rowIsCollapsed(Row, SectionMask);

/// The section whose heading row is under this point, or NumSections.
Section headingSectionAt(const RowBounds &, juce::Point<int>);

/// How a column's height divides when the parameters can scroll.
///
/// Two bands. The channel header is pinned at the top and everything below it
/// scrolls as one list, the mixer included. The header stays because it is
/// what tells you which channel you are looking at, and a column of knobs with
/// no number on it is thirty-two identical columns.
///
/// It exists because the parameters are small on a 1080p screen and zooming in
/// to read them made the window taller than such a screen has room for.
/// Scrolling is what lets the window stay the height of the screen while the
/// rows inside it get bigger.
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
/// At or above the preferred height this is exactly the old layout: every row
/// is on screen and the fader takes the slack, so a window nobody has shrunk
/// looks as it always did. Below it the fader stops growing and keeps the
/// height it wants while the rows scroll past it instead.
Bands layoutBands(juce::Rectangle<int> area, SectionMask collapsed = 0);

/// Every offset the band can rest at: the top of each scrolling row.
///
/// Scrolling snaps to these so nothing is ever half over the pinned header.
/// The foot of the band is free to cut a row off, which is what shows there is
/// more below it.
std::vector<int> scrollStopsFor(juce::Rectangle<int> area,
                                SectionMask collapsed);

RowBounds layoutRows(juce::Rectangle<int> area, SectionMask collapsed = 0,
                     int scroll = 0);

/// The row a point in a strip belongs to, or kNoRow for the header and the
/// section rules, which have nothing to point at.
///
/// The two readouts answer with the control above them, so drifting off the
/// tuning knob onto its cents figure does not put the highlight out.
Row controlRowAt(const RowBounds &, juce::Point<int>);

/// Whether a row takes the pointer highlight.
///
/// The faders and the mute and solo buttons do not. They are the two rows
/// nobody has to hunt for, and a wash the height of a whole fader was a lot of
/// paint to say something that obvious. The fader still reports itself, so LINK
/// can still show what a drag on it would reach.
bool rowShowsHighlight(Row);

/// Repaints the band a row highlights, if it highlights one at all.
void repaintRowHighlight(juce::Component &, const RowBounds &, Row);

/// Merges rectangles that share a top and a bottom edge into one band each.
///
/// For the lamps, which sit at the same four heights on every strip. Their
/// natural grouping is by row rather than by neighbour: thirty-three lamps
/// across one rule union into a band that is thin, wide and entirely full,
/// where merging one of them with the meter band lower down the same strip
/// gives a tall rectangle that is mostly nothing.
///
/// That difference is worth the separate pass. Measured on a mixer with all
/// 32 channels modulating, putting the lamps through the general merge below
/// costs 762,000 pixels a frame against the meters' 22,000, because six
/// rectangles is not enough to keep the rows apart. By row it is 81,000, and
/// most of that is only reached when every channel is moving at once.
void mergeIntoRows(juce::Array<juce::Rectangle<int>> &regions);

/// Reduces a set of dirty rectangles to at most `limit` of them, merging the
/// pairs that add the least area.
///
/// Thirty-three channel meters produce thirty-three small scattered
/// rectangles a frame. Handing all of them to the window manager is no good,
/// since past a handful it gives up and redraws their bounding box, which
/// here reaches from the top bar to the faders. Handing it one merged
/// rectangle is no good either: the bands sit at different heights, so their
/// union is nearly as tall as the mixer while the changes inside it are not.
/// A few rectangles is the setting that survives coalescing and still says
/// something specific.
///
/// The result always covers every rectangle that went in.
void coalesceRegions(juce::Array<juce::Rectangle<int>> &regions, int limit);

/// The height the folded rows are no longer taking.
///
/// What the window shrinks by when a section is folded. Without this the fader
/// simply absorbs the slack and the mixer is exactly as tall as it was, which
/// is not what folding a section is for.
int collapsedRowsHeight(SectionMask collapsed);

/// Total height needed before the fader starts being squeezed.
int preferredStripHeight(SectionMask collapsed = 0);

/// Height below which the fader would be unusably short.
int minimumStripHeight(SectionMask collapsed = 0);

/// Left-gutter caption for a row, or nullptr for rows that need no caption.
const char *rowLabel(Row r);

/// The per-strip controls that the LINK switch ganged across all 32 channels.
enum class Role {
  Tune = 0,
  Phase,
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

/// What a control is called out loud.
///
/// Distinct from rowLabel, which names a row for the gutter and can repeat
/// itself: two rows both read "rate" there, and the column they sit in says
/// which is which. A screen reader has no column, so these spell it out.
const char *roleLabel(Role r);

/// The role a parameter suffix belongs to, or NumRoles if none does.
///
/// The noise channel is built from suffixes rather than roles, but its
/// controls are the same controls, so this lets both take their spoken names
/// from one table instead of two that can drift.
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

/// The curve as a saved state writes it, and reads it back.
///
/// A name rather than the enum's index, because the list has already changed
/// once: an index written by an older version does not mean the same curve now.
/// A name cannot drift that way.
const char *linkCurveId(LinkCurve);

/// @param id      what the current property held, empty if it is absent
/// @param legacy  what the older integer property held, negative if absent.
///                That list ran Uniform, Tilt up, Tilt down, Spread, and Taper
///                is what replaced the two tilts, so both of them land on it.
LinkCurve linkCurveFromState(const juce::String &id, int legacy);

/// Everything the LINK switch is currently set to.
/// What a drag in the mixer does.
///
/// One choice rather than two switches, which is what it always was: a drag
/// cannot be a link and a drawing at once, and the two toggles this replaces
/// had to work around that by making LINK read as off while drawing was
/// armed, lighting a switch for a gesture that had been taken away from it.
enum class PointerTool : int { Pointer = 0, Link, Draw, NumTools };

const char *pointerToolName(PointerTool);

/// The cursor that tool gives, which is what its button wears instead of a
/// word: the button shows the pointer you are about to be holding.
juce::Image pointerToolImage(PointerTool, LinkCurve, float scale);

struct LinkSettings {
  bool enabled = false;
  LinkScope scope = LinkScope::All;
  LinkCurve curve = LinkCurve::Uniform;
};

/// The LINK menu, built as data rather than assembled at the click.
///
/// One list serves the button in the bar and the right-click in the mixer, and
/// keeping it out here means it can be checked without a window. A menu that
/// can only be reached by clicking is a menu that never gets tested.
juce::PopupMenu buildLinkMenu(const LinkSettings &);

/// The tool menu: which of the three, and under a separator the settings
/// belonging to whichever is chosen. One place to go rather than two.
juce::PopupMenu buildToolMenu(PointerTool, const LinkSettings &);

/// @return true when the id was a tool rather than one of LINK's settings,
///         in which case `tool` has been moved.
bool applyToolMenuChoice(int id, PointerTool &tool);

/// Applies what the menu came back with.
///
/// @returns false when the id was not one of ours, which includes the 0 that
/// means the menu was dismissed.
bool applyLinkMenuChoice(int id, LinkSettings &);

/// Weight applied to a strip's share of a LINK drag.
///
/// Anchored on the strip being dragged, which always comes out at exactly 1.
/// That matters: the knob under the mouse has to follow the mouse, so if the
/// curve gave it anything other than its full share it would disagree with
/// every strip around it.
///
/// Taper is therefore measured from where you grabbed: a strip's share falls
/// off with how far along the series it sits from the one in your hand, down to
/// nothing at thirty-one channels away. Grab the first channel and the mixer
/// tilts down across its whole width, grab the last and it tilts up, and grab
/// anywhere between and the curve peaks under your hand and falls away on both
/// sides.
float linkCurveWeight(LinkCurve, int index0, int sourceIndex);

/// Where one linked strip lands partway through a LINK drag.
///
/// Pure arithmetic, kept out of the editor so it can be tested without a
/// window. A zero delta must return the baseline exactly for every curve,
/// which is what lets a drag be undone by returning the knob.
///
/// @param delta     how far the dragged knob has moved, in the space below
/// @param weight    this strip's share, from linkCurveWeight
/// @param jitter    a fixed direction in [-1, 1], only used by Spread
/// @param target    the dragged strip's live value, gathered towards by Spread
/// @param low,high  the ends of the space this is working in
///
/// The space is normally the knob's own travel, which runs nought to one. The
/// level row is the exception and works in decibels, because its travel is
/// shaped to feel right under a finger rather than to be even, so moving every
/// fader the same distance moves the quiet ones many times further in level
/// than the loud ones. See linkIsDecibels.
float linkedValue(LinkCurve, float baseline, float delta, float weight,
                  float jitter, float target, float low = 0.0f,
                  float high = 1.0f);

/// Whether a drag on this row should be shared out in decibels rather than
/// across the knob's travel.
///
/// True of the faders and nothing else. Every other row's travel is already
/// even in whatever it is measuring, or logarithmic and therefore even in
/// ratio, which is what "the same amount" means for a rate or a time. A level
/// fader is neither: its travel is square-law into gain, so a drag that moved
/// every fader the same distance moved a quiet channel thirty decibels while
/// the one in your hand moved seven.
///
/// It applies to the curves that share out an amount, which is Uniform and
/// Taper. Spread is not one of those: it scatters the series across its range
/// and gathers it back onto the strip in your hand, which is a gesture about
/// where things sit rather than about how much louder they are, and it stays
/// in the travel where it has always been.
inline bool linkIsDecibels(Role r, LinkCurve c) {
  return r == Role::Volume && c != LinkCurve::Spread;
}

/// Implemented by the editor; lets a strip say where the pointer is.
///
/// A knob thirty channels along is a long way from the caption that names it,
/// so the whole mixer picks out the row under the pointer and the gutter
/// brightens the one label that belongs to it.
struct HoverTarget {
  virtual ~HoverTarget() = default;

  /// @param stripIndex  the partial under the pointer, or -1 for the noise
  ///                    channel, which sits outside the series.
  virtual void hoverChanged(int stripIndex, Row row) = 0;
};

/// Implemented by the editor; lets a strip broadcast a drag to its 31 siblings.
/// An opaque lid over a column's pinned header.
///
/// Scrolling is smooth, so a row can sit half over the top of the band. This
/// covers it, and being a component it takes the mouse as well, so a control
/// that has slid under the header is neither seen nor clickable.
///
/// It is here rather than each column moving its controls into a clipping
/// child, which is the other way to get those two things and would have taken
/// hover, LINK, folding and the draw tool through a change none of them
/// needed: they all reach the controls through the column itself.
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

struct LinkTarget {
  virtual ~LinkTarget() = default;

  virtual bool isLinkEnabled() const = 0;
  virtual void linkDragStarted(Role role, int sourceIndex) = 0;
  /// @param plainValue  the un-normalised value; the editor converts it per
  /// destination.
  virtual void linkValueChanged(Role role, int sourceIndex,
                                float plainValue) = 0;
  virtual void linkDragEnded(Role role, int sourceIndex) = 0;

  /// Pops the LINK menu under the pointer. A right-click anywhere in the mixer
  /// is the quickest way to change what the next drag will do, without going
  /// back up to the bar for it.
  ///
  /// @param parameterId  what the click landed on, empty if it landed on
  ///                     nothing a controller could move. The menu grows its
  ///                     MIDI Learn items from this. See ui::learn.
  virtual void showLinkMenu(const juce::String &parameterId) = 0;

  /// A wheel over a column, which moves the parameters rather than the mixer.
  ///
  /// Through the editor because the scroll belongs to the whole mixer: every
  /// column is handed the same number, and a column that scrolled itself would
  /// leave the gutter's captions beside the wrong knobs.
  ///
  /// @param delta  pixels to move by, positive downward. Snapped to a row
  ///               boundary by the layout, so the caller need not.
  /// @returns whether it was taken. False means there is nothing to scroll,
  ///          and the wheel falls through to the viewport to move sideways,
  ///          which is what it did before there was a second axis.
  virtual bool scrollParameters(int delta) = 0;

  /// A drag held with a modifier, which draws the faders it passes over
  /// instead of moving one of them.
  ///
  /// It goes through the editor for the same reason a LINK drag does, and more
  /// so: the pointer belongs to the fader the drag began on until the button
  /// comes up, so no other strip ever hears about it. The editor is the only
  /// thing that knows where the strips are.
  ///
  /// @param onScreen  where the pointer is, in screen coordinates. A drag
  ///                  that crosses strips cannot be described in any one
  ///                  strip's, and a screen point needs no component to know
  ///                  about any other to be understood.
  /// @returns whether the drag was taken. False leaves the fader to move
  ///          itself, which is what happens when the modifier is not held.
  virtual bool drawStarted(juce::Point<int> onScreen) = 0;
  virtual void drawMovedTo(juce::Point<int> onScreen) = 0;
  virtual void drawEnded() = 0;
};

} // namespace ovt::ui
