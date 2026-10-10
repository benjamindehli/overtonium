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

  /// Where a cap sits in a block of two sharing one moulding.
  ///
  /// The mute and the solo are a two-gang block, the way a console's are. At
  /// the size they are drawn, two bezels facing each other across a two pixel
  /// gap spend most of the pair's width on moulding, and the wall is already
  /// at its floor, so the only pixels to recover are those two. They stay two
  /// components, each lighting on its own, and the well is drawn as the whole
  /// block by both of them: what each one shows of it is its own half,
  /// because a component clips to itself.
  enum class LampGang { Alone, Left, Right };

  /// Which half of a gang a button is, from the "lampGang" property it
  /// carries. Read here rather than passed, so that a button's bounds and its
  /// moulding cannot be told different things.
  static LampGang lampGangOf(const juce::Component &);

  /// Puts a pair into one moulding. Order is left to right.
  static void gangLamps(juce::Component &left, juce::Component &right);

  /// The cap's own rectangle inside a button's bounds, which is what anything
  /// drawn on the button has to fit: the bezel is part of the component, so a
  /// word or an icon given the whole bounds runs under the moulding.
  ///
  /// A ganged cap gives up only half a wall on the side it joins, the other
  /// half coming from its neighbour, so the divider between two caps is the
  /// same width as the moulding around them.
  static juce::Rectangle<float> lampCapBounds(juce::Rectangle<float> bounds,
                                              LampGang gang);

  /// The plastic itself, at the middle of the cap where the lamp is behind
  /// it.
  ///
  /// The cap is a white translucent, the same one for every lamp, and what
  /// a lamp does is shine through it. Unlit, every cap on the panel is
  /// therefore the same grey, that being what white plastic looks like in an
  /// unlit room, and the only colour anywhere is what is switched on.
  static juce::Colour lampFace(juce::Colour lamp, bool on);

  /// What a word or an icon on a cap is drawn in, lit or not.
  ///
  /// Always the same near-black, so a legend never changes colour under its
  /// own lamp. It is printed on the plastic rather than being part of what
  /// lights, which is what the machines do and what lets the eye read the
  /// state off the colour of the cap alone.
  /// A rectangle on whole pixels.
  ///
  /// A filled shape wants its edges on the grid: landed between two pixels it
  /// gets a row of partial coverage all the way round, which comes out darker
  /// than its face by whatever is behind it and reads as a border nobody drew.
  /// A stroked edge wants the opposite, which is why the bounds these come
  /// from are inset by half a pixel in the first place.
  static juce::Rectangle<float> snapToPixels(juce::Rectangle<float>);

  static juce::Colour lampLegend();

  /// The legend as it actually prints on a lit cap: thinnest over the middle,
  /// where the lamp is and where most light gets through it, and closing up
  /// towards the ends. Unlit it is very nearly ink, there being nothing
  /// behind it to pass.
  static juce::ColourGradient legendInk(juce::Colour ink,
                                        juce::Rectangle<float> cap, bool on);

  /// A square plastic cap of the kind a tape machine or a desk has, standing
  /// in a moulded well and lit from behind when its thing is on.
  ///
  /// The cap does not move when the lamp comes on. These are indicator lamps
  /// rather than latching switches, and a cap that sinks while its thing is
  /// on says that pressing it is what turns the thing off, which is the wrong
  /// way round for ECHO or for a mute. @p down is a finger on it and nothing
  /// else, so the press lasts as long as the press does.
  ///
  /// The cap keeps its colour lit or not and only its brightness moves, which
  /// is what the real ones do: the lamp is behind a coloured plastic, so an
  /// unlit amber button is brown and an unlit green one is a dark olive
  /// rather than grey. That is also what makes the off state readable as a
  /// lamp that could light rather than as a hole.
  ///
  /// @param lamp  the colour behind the plastic, lit or not.
  static void drawLampCap(juce::Graphics &, juce::Rectangle<float> bounds,
                          juce::Colour lamp, bool on, bool highlighted,
                          bool down, LampGang gang);

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

  /// The standalone's own title bar, which is JUCE's rather than the
  /// platform's and would otherwise be the grey-green every unstyled JUCE app
  /// wears, with a red cross and a yellow dash on it. Dressed to match the
  /// panel underneath, so the window reads as one object.
  ///
  /// Only the standalone has one of these. In a host the window belongs to the
  /// host and none of this is reached. See OvertoniumEditor.
  void drawDocumentWindowTitleBar(juce::DocumentWindow &, juce::Graphics &,
                                  int w, int h, int titleSpaceX,
                                  int titleSpaceW, const juce::Image *icon,
                                  bool drawTitleTextOnLeft) override;

  juce::Button *createDocumentWindowButton(int buttonType) override;

  juce::Font getComboBoxFont(juce::ComboBox &) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getSliderPopupFont(juce::Slider &) override;

private:
  /// What keeps the grain tile alive, and the reason it is a member here
  /// rather than a static beside the function that builds it.
  ///
  /// One look and feel exists per editor, so the tile is built when the first
  /// window opens and released when the last one closes, on the message thread
  /// with the binary still loaded. Held in a static instead, it would be
  /// released when the host unloads the plugin, and a juce::Image under
  /// Windows is backed by Direct2D, so that teardown deadlocks against the
  /// loader lock. See GrainTile in Theme.h for the whole of it.
  ///
  /// Never read. Its existence is the point.
  juce::SharedResourcePointer<GrainTile> grainHolder;
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

/// The lip of the opening a screen or a lamp is set into.
///
/// Around the thing rather than inside it. Every one of them is a few pixels
/// across, so a border taken out of the picture would leave no picture, and
/// each already stands in a margin of its own that nothing else is using.
///
/// Two fills and a clip, no stroking, which is what keeps it cheap enough for
/// the lamps. The clip keeps the shadow inside the lip. The light comes from
/// the top left, as it does everywhere else here, so the walls facing it are
/// the bottom and the right, and those are the two that come up lit. Call it
/// before drawing the face, which then covers everything but the lip.
///
/// @param opening  the face that will be drawn over it.
/// @param corner  that face's own corner radius. Half the height gives a
/// round hole, which is what a lamp sits in.
/// @param depth  how far the lip stands out, in pixels. One is enough to read
/// and is all the tighter margins have.
///
/// Both shapes run under the opening as well as around it, so the face has to
/// cover its own ground. One drawn at low alpha would show the shadow through
/// itself: see the lamps, which blend against their backdrop rather than
/// asking this for a floor to sit on.
void paintRecess(juce::Graphics &, juce::Rectangle<float> opening, float corner,
                 float depth = 1.0f);

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

/// The pointer while a drag across the faders would draw them rather than move
/// one. A pencil, since that is what the gesture is.
juce::MouseCursor drawCursor();

/// The artwork behind it, exposed for the same reason linkCursorImage is.
juce::Image drawCursorImage(float scale);

/// The artwork behind linkCursor, exposed so it can be looked at without a
/// pointer to hang it on.
juce::Image linkCursorImage(LinkCurve, float scale);

} // namespace ovt::ui
