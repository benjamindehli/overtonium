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
  PitchModHeading,
  PmRate,
  PmDepth,
  Drift,
  EnvHeading,
  Delay,
  Attack,
  Decay,
  Sustain,
  Release,
  AmpModHeading,
  AmRate,
  AmDepth,
  OutputHeading,
  Velocity,
  Aftertouch,
  MuteSolo,
  Fader,
  FaderText,
  NumRows
};

inline constexpr int kNumRows = (int)Row::NumRows;
inline constexpr int kStripWidth = 38;
inline constexpr int kGutterWidth = 78;

using RowBounds = std::array<juce::Rectangle<int>, kNumRows>;

RowBounds layoutRows(juce::Rectangle<int> area);

/// Total height needed before the fader starts being squeezed.
int preferredStripHeight();

/// Height below which the fader would be unusably short.
int minimumStripHeight();

/// Left-gutter caption for a row, or nullptr for rows that need no caption.
const char *rowLabel(Row r);

/// The per-strip controls that the LINK switch ganged across all 32 channels.
enum class Role {
  Tune = 0,
  PmRate,
  PmDepth,
  Drift,
  Delay,
  Attack,
  Decay,
  Sustain,
  Release,
  AmRate,
  AmDepth,
  Velocity,
  Aftertouch,
  Volume,
  NumRoles
};

inline constexpr int kNumRoles = (int)Role::NumRoles;

/// Maps a role onto the matching parameter-ID suffix from ovt::params.
const char *roleSuffix(Role r);

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

/// Weight applied to a strip's share of a LINK drag, before the curve's own
/// behaviour. Ramps across the whole series rather than across the selection,
/// so "higher partials move more" keeps meaning the same thing whichever
/// scope is chosen.
float linkCurveWeight(LinkCurve, int index0);

/// Where one linked strip lands partway through a LINK drag.
///
/// Pure arithmetic, kept out of the editor so it can be tested without a
/// window. A zero delta must return the baseline exactly for every curve,
/// which is what lets a drag be undone by returning the knob.
///
/// @param delta     how far the dragged knob has moved, in normalised units
/// @param weight    this strip's share, from linkCurveWeight
/// @param jitter    a fixed direction in [-1, 1], only used by Spread
/// @param mean      average of the selection at drag start, only used by Spread
float linkedValue(LinkCurve, float baseline, float delta, float weight,
                  float jitter, float mean);

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
};

} // namespace ovt::ui
