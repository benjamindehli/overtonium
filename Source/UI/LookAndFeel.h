#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ovt::ui {

/// The plugin's look and feel: knobs, faders, lamp caps, combo boxes, the
/// standalone title bar and the cached channel background.
class OvertoniumLookAndFeel : public juce::LookAndFeel_V4 {
public:
  OvertoniumLookAndFeel();

  void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPosProportional, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &) override;

  void drawLinearSlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle, juce::Slider &) override;

  /// Where a cap sits in a block of two sharing one moulding.
  ///
  /// Each of the two components draws the well of the whole block, and its
  /// own clip shows only its half.
  enum class LampGang { Alone, Left, Right };

  /// Which half of a gang a button is, from its "lampGang" property, so that
  /// its bounds and its moulding always agree.
  static LampGang lampGangOf(const juce::Component &);

  /// Puts a pair into one moulding. Order is left to right.
  static void gangLamps(juce::Component &left, juce::Component &right);

  /// The cap's own rectangle inside a button's bounds, which is what a word
  /// or an icon drawn on the button has to fit. A ganged cap gives up only
  /// half a wall on the side it joins.
  static juce::Rectangle<float> lampCapBounds(juce::Rectangle<float> bounds,
                                              LampGang gang);

  /// The colour of the cap's face at its middle, over the lamp. Unlit, every
  /// cap is the same grey.
  static juce::Colour lampFace(juce::Colour lamp, bool on);

  /// A rectangle on whole pixels, so a filled shape gets no row of partial
  /// coverage round its edge.
  static juce::Rectangle<float> snapToPixels(juce::Rectangle<float>);

  /// What a word or an icon on a cap is drawn in: the same near-black, lit or
  /// not.
  static juce::Colour lampLegend();

  /// The legend ink on a cap, lightest over the middle of a lit cap and very
  /// nearly @p ink when unlit.
  static juce::ColourGradient legendInk(juce::Colour ink,
                                        juce::Rectangle<float> cap, bool on);

  /// A square plastic cap standing in a moulded well, lit from behind when
  /// @p on.
  ///
  /// The cap only sinks while @p down, which is a finger on it. Being on does
  /// not sink it.
  ///
  /// @param lamp  the colour behind the plastic, lit or not.
  static void drawLampCap(juce::Graphics &, juce::Rectangle<float> bounds,
                          juce::Colour lamp, bool on, bool highlighted,
                          bool down, LampGang gang);

  /// Draws a button as a lamp cap in @p fill, taking its gang from the button.
  void drawButtonFace(juce::Graphics &, juce::Button &, bool engaged,
                      const juce::Colour &fill, bool highlighted, bool down);

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

  /// The standalone's title bar, drawn to match the panel. Not reached in a
  /// host, which owns the window.
  void drawDocumentWindowTitleBar(juce::DocumentWindow &, juce::Graphics &,
                                  int w, int h, int titleSpaceX,
                                  int titleSpaceW, const juce::Image *icon,
                                  bool drawTitleTextOnLeft) override;

  juce::Button *createDocumentWindowButton(int buttonType) override;

  juce::Font getComboBoxFont(juce::ComboBox &) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getSliderPopupFont(juce::Slider &) override;

  /// paintChannelBackground, drawn from a cached picture keyed by size, scale
  /// and colour. The grain starts at each strip's own corner, so one picture
  /// serves every strip of the same size.
  void drawChannelBackground(juce::Graphics &, juce::Rectangle<int> bounds,
                             juce::Colour base);

private:
  struct Background {
    int width = 0, height = 0;
    float scale = 0.0f;
    juce::uint32 base = 0;
    juce::Image picture;
  };

  /// The few sizes in use at once. Kept short, since a resize makes the old
  /// sizes useless.
  std::vector<Background> backgrounds;

  /// Keeps the grain tile alive while any editor is open, so it is released
  /// on the message thread rather than at unload. A static here would
  /// deadlock against the loader lock on Windows. See GrainTile in Theme.h.
  /// Never read.
  juce::SharedResourcePointer<GrainTile> grainHolder;
};

/// The one place that makes a font.
juce::Font makeFont(float height, bool bold = false);

/// Fills a channel background: base colour, grain, a lighter left edge, a
/// darker right one and a band of shade along the top.
void paintChannelBackground(juce::Graphics &, juce::Rectangle<int> bounds,
                            juce::Colour base);

/// The same, through the component's look and feel when it is this
/// plugin's, so the picture it keeps is used.
void paintChannelBackground(juce::Component &, juce::Graphics &,
                            juce::Rectangle<int> bounds, juce::Colour base);

/// Picks out the row under the pointer with a band of plain light, so it
/// lifts every channel's colour by the same amount.
void paintRowHighlight(juce::Graphics &, juce::Rectangle<int> row);

/// The same wash turned on its side, marking the channel under the pointer.
void paintColumnHighlight(juce::Graphics &, juce::Rectangle<int> strip);

/// The ground a lit readout sits on: the tuning digits and the shape glyph.
/// A faint accent wash, lifted further when @p hovered.
void paintDisplayGround(juce::Graphics &, juce::Rectangle<float> area,
                        float corner, bool hovered);

/// The pocket milled into the panel behind a group of controls on the top bar
/// and the bars under it.
void paintGroupPocket(juce::Graphics &, juce::Rectangle<int> group);

/// The floor of a pocket alone, for a bar that lays something in it before
/// the shading goes on.
void paintGroupPocketFill(juce::Graphics &, juce::Rectangle<int> group);

/// The shading round the inside of a pocket on its own, for drawing again
/// over something laid in the pocket.
void paintGroupPocketShade(juce::Graphics &, juce::Rectangle<int> group);

/// The lip around the opening a screen or a lamp is set into, drawn outside
/// @p opening.
///
/// Call it before drawing the face. Both shapes also run under the opening, so
/// the face has to be opaque or the shadow shows through it.
///
/// @param opening  the face that will be drawn over it.
/// @param corner  that face's own corner radius. Half the height gives a
/// round hole.
/// @param depth  how far the lip stands out, in pixels.
void paintRecess(juce::Graphics &, juce::Rectangle<float> opening, float corner,
                 float depth = 1.0f);

/// Strokes a path the way a phosphor screen shows one, in three passes: a
/// wide faint bloom, a narrower brighter one, then the trace itself.
void strokeGlowing(juce::Graphics &, const juce::Path &, juce::Colour,
                   float thickness);

/// The product wordmark, for the corner of the bar.
juce::Image logoWordmark();

/// The maker's mark, for the foot of the gutter. Returns null if the drawing
/// cannot be parsed.
std::unique_ptr<juce::Drawable> logoMakersMark();

/// A pointer that shows what a LINK drag would do: five bars beside the arrow
/// in the shape of the curve.
juce::MouseCursor linkCursor(LinkCurve);

/// The pencil pointer shown while a drag across the faders would draw them
/// rather than move one.
juce::MouseCursor drawCursor();

/// The artwork behind drawCursor, exposed so it can be looked at on its own.
juce::Image drawCursorImage(float scale);

/// The artwork behind linkCursor, exposed so it can be looked at on its own.
juce::Image linkCursorImage(LinkCurve, float scale);

} // namespace ovt::ui
