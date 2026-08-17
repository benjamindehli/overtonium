#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/Harmonics.h"

namespace ovt::ui {

namespace colours {
inline const juce::Colour background{0xff0b0d10};
inline const juce::Colour panel{0xff14181d};
inline const juce::Colour panelAlt{0xff181d23};
inline const juce::Colour groove{0xff090b0e};
inline const juce::Colour outline{0xff272e37};
inline const juce::Colour text{0xffd9dfe7};
inline const juce::Colour textDim{0xff6f7a86};
/// Chrome, not content. Sits in the cyan the channel ramp never reaches, so
/// the global controls never read as one of the channels.
inline const juce::Colour accent{0xff62bbd9};
inline const juce::Colour muteOn{0xffe0733d};
inline const juce::Colour soloOn{0xffe8c34a};
} // namespace colours

/// Colour for an interval class, so the eye can group octaves, fifths and
/// thirds while scanning 32 strips.
///
/// The twelve classes are spread in chromatic order across a narrow band that
/// runs from blue, through magenta and red, to yellow. Narrow enough that the
/// mixer reads as one family rather than a rainbow, and placed clear of the
/// green and cyan the global accent uses.
juce::Colour intervalColour(int pitchClass);

/// Vertical slots in a channel strip. The gutter on the left lays out the same
/// list so the row labels always line up with the controls.
enum class Row {
  Header = 0,
  TuneKnob,
  TuneText,
  Phase,
  PitchModHeading,
  PmRate,
  PmDepth,
  Drift,
  EnvHeading,
  Delay,
  Attack,
  Decay,
  Sustain,
  KeyOffHeading,
  Swell,
  OffLevel,
  Release,
  AmpModHeading,
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

/// Stands for "no row", which is what a hover over a heading or a gap reports.
inline constexpr Row kNoRow = Row::NumRows;

using RowBounds = std::array<juce::Rectangle<int>, kNumRows>;

RowBounds layoutRows(juce::Rectangle<int> area);

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

/// Total height needed before the fader starts being squeezed.
int preferredStripHeight();

/// Height below which the fader would be unusably short.
int minimumStripHeight();

/// Left-gutter caption for a row, or nullptr for rows that need no caption.
const char *rowLabel(Row r);

/// The per-strip controls that the LINK switch ganged across all 32 channels.
enum class Role {
  Tune = 0,
  Phase,
  PmRate,
  PmDepth,
  Drift,
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
  TiltUp,      ///< higher partials move more
  TiltDown,    ///< lower partials move more
  Spread,      ///< pushing up scatters them, pulling down gathers them
  NumCurves
};

const char *linkScopeName(LinkScope);
const char *linkCurveName(LinkCurve);

/// Everything the LINK switch is currently set to.
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
/// every strip around it. Tilting is therefore relative to where you grabbed,
/// and partials further up or down the series move progressively more or less
/// than the one in your hand.
float linkCurveWeight(LinkCurve, int index0, int sourceIndex);

/// Where one linked strip lands partway through a LINK drag.
///
/// Pure arithmetic, kept out of the editor so it can be tested without a
/// window. A zero delta must return the baseline exactly for every curve,
/// which is what lets a drag be undone by returning the knob.
///
/// @param delta     how far the dragged knob has moved, in normalised units
/// @param weight    this strip's share, from linkCurveWeight
/// @param jitter    a fixed direction in [-1, 1], only used by Spread
/// @param target    the dragged strip's live value, gathered towards by Spread
float linkedValue(LinkCurve, float baseline, float delta, float weight,
                  float jitter, float target);

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
  virtual void showLinkMenu() = 0;
};

} // namespace ovt::ui
