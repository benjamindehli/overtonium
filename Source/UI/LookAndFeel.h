#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

class OvertoniumLookAndFeel : public juce::LookAndFeel_V4 {
public:
  OvertoniumLookAndFeel();

  void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPosProportional, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &) override;

  void drawLinearSlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle, juce::Slider &) override;

  void drawButtonBackground(juce::Graphics &, juce::Button &,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawButtonText(juce::Graphics &, juce::TextButton &,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

  void drawComboBox(juce::Graphics &, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &) override;

  juce::Font getComboBoxFont(juce::ComboBox &) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getSliderPopupFont(juce::Slider &) override;
};

/// Small helper so the codebase has one place that knows how to make a font.
juce::Font makeFont(float height, bool bold = false);

/// Fills a channel background under the panel's light source, which comes from
/// the top left throughout. A brighter left edge and a darker right one make a
/// run of strips read as raised columns rather than a flat sheet, and the band
/// of shade along the top is the shadow the header casts onto them.
void paintChannelBackground(juce::Graphics &, juce::Rectangle<int> bounds,
                            juce::Colour base);

/// Picks out the row under the pointer.
///
/// Drawn as a ruled band, so that it reads as a single line running the width
/// of the mixer, from the caption in the gutter to the channel your hand is on.
///
/// In plain light rather than in a colour. It belongs to the pointer rather
/// than to any one partial, and a band crossing thirty-three channels lands on
/// thirty-three different hues at once: a tint of its own agrees with a few of
/// them and argues with the rest, where clear light lifts all of them by the
/// same amount and leaves the colour underneath saying what it was already
/// saying. Naming which row and which channel is left to the gutter caption and
/// the channel number, which both go accent.
void paintRowHighlight(juce::Graphics &, juce::Rectangle<int> row);

/// The same wash turned on its side, marking the channel under the pointer.
///
/// The row highlight answers which control you are on, out of twenty-two. This
/// answers which channel, out of thirty-three, and the two crossing is what
/// tells you at a glance that you are about to drag the decay of harmonic 19.
void paintColumnHighlight(juce::Graphics &, juce::Rectangle<int> strip);

/// The ground a lit readout sits on: the tuning digits and the shape glyph.
///
/// Not the plain groove the other recesses use. These two are screens rather
/// than holes in the panel, and a screen is never quite off. The wash of
/// accent under them is the tint the strip's hover used to be the only source
/// of, which is where it came from: it looked right there, so it belongs there
/// all the time. Hovering still lifts it further, so the pointer has somewhere
/// to go.
void paintDisplayGround(juce::Graphics &, juce::Rectangle<float> area,
                        float corner, bool hovered);

/// Strokes a path the way a phosphor screen shows one.
///
/// Three passes: a wide faint bloom, a narrower brighter one, then the trace
/// itself. A line drawn once at full strength reads as ink on paper, and these
/// are meant to read as something lit from behind.
void strokeGlowing(juce::Graphics &, const juce::Path &, juce::Colour,
                   float thickness);

/// The product wordmark, for the corner of the bar.
juce::Image logoWordmark();

/// The maker's mark, for the foot of the gutter.
///
/// Vector rather than pixels, since it is drawn small and its size depends on
/// how tall the window is. Returns null if the drawing cannot be parsed, and
/// every caller has to cope with that rather than assume.
std::unique_ptr<juce::Drawable> logoMakersMark();

/// A pointer that says what a LINK drag would do to the channels it reaches.
///
/// Five bars beside the arrow, in the shape of the curve: level for uniform,
/// rising or falling for the tilts, scattered for spread. The tool is a mode,
/// and a mode you cannot see is a mode you forget you are in.
juce::MouseCursor linkCursor(LinkCurve);

/// The artwork behind linkCursor, exposed so it can be looked at without a
/// pointer to hang it on.
juce::Image linkCursorImage(LinkCurve, float scale);

} // namespace ovt::ui
