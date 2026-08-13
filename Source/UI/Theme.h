#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/Harmonics.h"

namespace ovt::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff0b0d10 };
    inline const juce::Colour panel      { 0xff14181d };
    inline const juce::Colour panelAlt   { 0xff181d23 };
    inline const juce::Colour groove     { 0xff090b0e };
    inline const juce::Colour outline    { 0xff272e37 };
    inline const juce::Colour text       { 0xffd9dfe7 };
    inline const juce::Colour textDim    { 0xff6f7a86 };
    inline const juce::Colour accent     { 0xff5fd0c4 };
    inline const juce::Colour muteOn     { 0xffe0733d };
    inline const juce::Colour soloOn     { 0xffe8c34a };
}

/** Hue per interval class, so the eye can group octaves, fifths and thirds at a glance
    while scanning 32 strips. */
juce::Colour intervalColour (int pitchClass);

/** Vertical slots in a channel strip. The gutter on the left lays out the same list so
    the row labels always line up with the controls. */
enum class Row
{
    Header = 0,
    TuneKnob,
    TuneText,
    PitchModHeading,
    PmRate,
    PmDepth,
    EnvHeading,
    Attack,
    Decay,
    Sustain,
    Release,
    AmpModHeading,
    AmRate,
    AmDepth,
    MuteSolo,
    Fader,
    FaderText,
    NumRows
};

inline constexpr int kNumRows    = (int) Row::NumRows;
inline constexpr int kStripWidth = 38;
inline constexpr int kGutterWidth = 78;

using RowBounds = std::array<juce::Rectangle<int>, kNumRows>;

RowBounds layoutRows (juce::Rectangle<int> area);

/** Total height needed before the fader starts being squeezed. */
int preferredStripHeight();

/** Height below which the fader would be unusably short. */
int minimumStripHeight();

/** Left-gutter caption for a row, or nullptr for rows that need no caption. */
const char* rowLabel (Row r);

/** The per-strip controls that the LINK switch ganged across all 32 channels. */
enum class Role
{
    Tune = 0, PmRate, PmDepth, Attack, Decay, Sustain, Release, AmRate, AmDepth, Volume, NumRoles
};

inline constexpr int kNumRoles = (int) Role::NumRoles;

/** Maps a role onto the matching parameter-ID suffix from ovt::params. */
const char* roleSuffix (Role r);

/** Implemented by the editor; lets a strip broadcast a drag to its 31 siblings. */
struct LinkTarget
{
    virtual ~LinkTarget() = default;

    virtual bool isLinkEnabled() const = 0;
    virtual void linkDragStarted (Role role, int sourceIndex) = 0;
    /** @param plainValue  the un-normalised value; the editor converts it per destination. */
    virtual void linkValueChanged (Role role, int sourceIndex, float plainValue) = 0;
    virtual void linkDragEnded (Role role, int sourceIndex) = 0;
};

} // namespace ovt::ui
