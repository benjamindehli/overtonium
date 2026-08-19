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
/// Drawn in the chrome colour rather than a channel colour, since it belongs to
/// the pointer rather than to any one partial, and drawn as a ruled band so
/// that it reads as a single line running the width of the mixer, from the
/// caption in the gutter to the channel your hand is on.
void paintRowHighlight(juce::Graphics &, juce::Rectangle<int> row);

/// The same wash turned on its side, marking the channel under the pointer.
///
/// The row highlight answers which control you are on, out of twenty-one. This
/// answers which channel, out of thirty-three, and the two crossing is what
/// tells you at a glance that you are about to drag the decay of harmonic 19.
void paintColumnHighlight(juce::Graphics &, juce::Rectangle<int> strip);

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
