#include "LookAndFeel.h"

#include <array>
#include <cmath>

#include <BinaryData.h>

namespace ovt::ui {

juce::Font makeFont(float height, bool bold) {
  auto options = juce::FontOptions(height);

  if (bold)
    options = options.withStyle("Bold");

  return juce::Font(options);
}

namespace {
/// How much of the grain shows. Small enough that it reads as surface rather
/// than as texture: at anything higher the panel starts to look like sandpaper
/// and the thin lit edges lose against it.
constexpr float kGrainStrength = 0.020f;

/// Square, and a power of two, so the tiling has no visible period at the
/// sizes the window is ever drawn at.
constexpr int kGrainSize = 128;
} // namespace

namespace {
/// Counted rather than inferred, so a test can see the tile go.
int grainBuilds = 0;
} // namespace

int grainTileBuildCount() { return grainBuilds; }

GrainTile::GrainTile() {
  // Built on first use and shared by every window, which is the whole reason
  // this is not per-component. What it is not is permanent: see GrainTile.
  ++grainBuilds;

  image = [] {
    juce::Image img(juce::Image::ARGB, kGrainSize, kGrainSize, true);
    juce::Image::BitmapData data(img, juce::Image::BitmapData::writeOnly);

    // Fixed seed. A texture that differed between runs would make every
    // screenshot comparison useless and would serve no purpose at all.
    juce::Random rng(0x5EED0001);

    for (int y = 0; y < kGrainSize; ++y) {
      for (int x = 0; x < kGrainSize; ++x) {
        // Signed noise, so the grain lifts and lowers the surface rather than
        // only darkening it, which is what stops it reading as dirt.
        //
        // Cubed, which is what makes it a surface rather than static. Straight
        // noise puts as many loud pixels as quiet ones on the panel and reads
        // as television snow. Cubing leaves most of the tile nearly clear and
        // lets the few strong grains carry it.
        const auto n = rng.nextFloat() - 0.5f;
        const auto shaped = n * n * n * 8.0f;
        const auto level =
            (juce::uint8)juce::jlimit(0, 255, 128 + (int)(shaped * 255.0f));
        data.setPixelColour(
            x, y,
            juce::Colour(level, level, level).withAlpha(std::abs(shaped)));
      }
    }

    return img;
  }();
}

juce::Image grainTile() {
  // The temporary keeps the shared tile alive for exactly as long as it takes
  // to copy the handle out of it. While any look and feel exists the count
  // never reaches zero here, so this costs a reference and not a rebuild.
  const juce::SharedResourcePointer<GrainTile> held;
  return held->image;
}

void paintGrain(juce::Graphics &g, juce::Rectangle<int> area) {
  if (area.isEmpty())
    return;

  g.setTiledImageFill(grainTile(), 0, 0, kGrainStrength);
  g.fillRect(area);
}

void paintChannelBackground(juce::Graphics &g, juce::Rectangle<int> bounds,
                            juce::Colour base) {
  const auto r = bounds.toFloat();

  g.setGradientFill(juce::ColourGradient(base.brighter(0.10f), r.getX(),
                                         r.getY(), base.darker(0.06f), r.getX(),
                                         r.getBottom(), false));
  g.fillRect(r);

  // The header casts onto the top of every channel.
  const auto shade = juce::jmin(10.0f, r.getHeight() * 0.05f);
  g.setGradientFill(juce::ColourGradient(
      juce::Colours::black.withAlpha(0.28f), r.getX(), r.getY(),
      juce::Colours::black.withAlpha(0.0f), r.getX(), r.getY() + shade, false));
  g.fillRect(r.withHeight(shade));

  paintGrain(g, bounds);

  // Lit edge on the left, shadowed on the right. After the grain, so the two
  // one-pixel borders are not eaten by it.
  g.setColour(juce::Colours::white.withAlpha(0.05f));
  g.fillRect(r.getX(), r.getY(), 1.0f, r.getHeight());

  g.setColour(juce::Colours::black.withAlpha(0.35f));
  g.fillRect(r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
}

juce::Image logoWordmark() {
  return juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                         BinaryData::logo_pngSize);
}

std::unique_ptr<juce::Drawable> logoMakersMark() {
  return juce::Drawable::createFromImageData(BinaryData::dehlimusikk_svg,
                                             BinaryData::dehlimusikk_svgSize);
}

namespace {
/// How hard the hover marks sit on the panel.
///
/// Named once and shared by the row and the column, because the two are meant
/// to be one idea seen twice and would be worth nothing as a pair if they
/// could drift apart. Light: neither has to carry the job alone, since the
/// gutter caption and the channel number both go accent to say which is which,
/// and the wash is only there to join the lit label to the rest of its band.
///
/// A shade under what the accent used to take, because the light the marks are
/// now drawn in is brighter than the cyan was and would otherwise land harder
/// at the same figures. Measured against the channel grey, the band lifts it by
/// about as much as it always did, and lifts every channel by the same amount
/// rather than by three different ones.
constexpr float kHoverWash = 0.030f;
constexpr float kHoverEdge = 0.10f;

/// How lit a display's ground is before anything is done to it.
///
/// A shade above the hover wash, because it has to carry the look on its own
/// rather than joining a band that is already marked. Hovering adds its own on
/// top of this, and the strip's column wash adds a third, so the pointer still
/// has somewhere to go from here.
constexpr float kDisplayWash = 0.055f;
constexpr float kDisplayWashHover = 0.10f;
} // namespace

void paintDisplayGround(juce::Graphics &g, juce::Rectangle<float> area,
                        float corner, bool hovered) {
  g.setColour(colours::groove);
  g.fillRoundedRectangle(area, corner);

  g.setColour(
      colours::accent.withAlpha(hovered ? kDisplayWashHover : kDisplayWash));
  g.fillRoundedRectangle(area, corner);
}

void paintRecess(juce::Graphics &g, juce::Rectangle<float> opening,
                 float corner, float depth) {
  // The lit walls first, one depth out all the way round, and then the
  // shadowed ones over them: the opening itself moved up and to the left by
  // that same depth, which covers the top and the left and leaves the light
  // showing along the bottom and the right. That is the pair a recess lit from
  // the top left shows. The two walls facing the light are the far ones, where
  // a raised object shows the two nearest, which is why a channel is lit on
  // its left edge and dark on its right and a hole in one is the other way
  // about.
  //
  // The shadow is the size of the opening rather than the size of the lip, so
  // that the lip comes out one pixel on every side. Offsetting the larger of
  // the two shapes instead puts two pixels of shadow above and to the left
  // against one of light below and to the right, which reads as every element
  // sitting off centre in its own hole, and the wider shadow also eats a pixel
  // its neighbour was using.
  g.setColour(juce::Colours::white.withAlpha(0.11f));
  g.fillRoundedRectangle(opening.expanded(depth), corner + depth);

  g.setColour(juce::Colours::black.withAlpha(0.6f));
  g.fillRoundedRectangle(opening.translated(-depth, -depth), corner);
}

void strokeGlowing(juce::Graphics &g, const juce::Path &path,
                   juce::Colour colour, float thickness) {
  struct Pass {
    float width;
    float alpha;
  };

  // Widest and faintest first, so each pass lands on the one before it.
  const Pass passes[] = {
      {thickness * 3.2f, 0.10f}, {thickness * 1.9f, 0.18f}, {thickness, 1.0f}};

  for (const auto &pass : passes) {
    g.setColour(colour.withMultipliedAlpha(pass.alpha));
    g.strokePath(path,
                 juce::PathStrokeType(pass.width, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
  }
}

void paintRowHighlight(juce::Graphics &g, juce::Rectangle<int> row) {
  const auto r = row.toFloat();

  g.setColour(colours::text.withAlpha(kHoverWash));
  g.fillRect(r);

  g.setColour(colours::text.withAlpha(kHoverEdge));
  g.fillRect(r.getX(), r.getY(), r.getWidth(), 1.0f);
  g.fillRect(r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
}

void paintColumnHighlight(juce::Graphics &g, juce::Rectangle<int> strip) {
  const auto r = strip.toFloat();

  g.setColour(colours::text.withAlpha(kHoverWash));
  g.fillRect(r);

  g.setColour(colours::text.withAlpha(kHoverEdge));
  g.fillRect(r.getX(), r.getY(), 1.0f, r.getHeight());
  g.fillRect(r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
}

juce::Image linkCursorImage(LinkCurve curve, float scale) {
  constexpr int size = 30;

  juce::Image image(juce::Image::ARGB, (int)((float)size * scale),
                    (int)((float)size * scale), true);

  {
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));

    juce::Path arrow;
    arrow.startNewSubPath(1.0f, 1.0f);
    arrow.lineTo(1.0f, 14.5f);
    arrow.lineTo(4.6f, 11.0f);
    arrow.lineTo(7.0f, 16.5f);
    arrow.lineTo(9.4f, 15.4f);
    arrow.lineTo(7.0f, 10.1f);
    arrow.lineTo(12.0f, 10.1f);
    arrow.closeSubPath();

    // Outlined in black first, so the pointer reads against a light background
    // as well as against the panel.
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.strokePath(arrow, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::white);
    g.fillPath(arrow);

    // The five bars, as a share of full height.
    std::array<float, 5> heights{};

    switch (curve) {
    case LinkCurve::Taper:
      // Peaked in the middle and falling away on both sides, which is the
      // shape of the drag when the grab is somewhere in the middle of the
      // mixer. Grab an end and the same curve shows as a plain tilt.
      heights = {0.28f, 0.62f, 0.96f, 0.62f, 0.28f};
      break;
    case LinkCurve::Spread:
      heights = {0.9f, 0.3f, 0.75f, 0.35f, 0.95f};
      break;

    case LinkCurve::Uniform:
    case LinkCurve::NumCurves:
    default:
      heights = {0.6f, 0.6f, 0.6f, 0.6f, 0.6f};
      break;
    }

    const auto base = 27.0f;
    const auto span = 13.0f;
    auto x = 13.0f;

    for (auto h : heights) {
      const juce::Rectangle<float> bar(x, base - span * h, 2.4f, span * h);

      g.setColour(juce::Colours::black.withAlpha(0.9f));
      g.fillRect(bar.expanded(1.0f, 1.0f));
      g.setColour(colours::accent.brighter(0.5f));
      g.fillRect(bar);

      x += 3.4f;
    }
  }

  return image;
}

juce::Image drawCursorImage(float scale) {
  constexpr int size = 30;

  juce::Image image(juce::Image::ARGB, (int)((float)size * scale),
                    (int)((float)size * scale), true);

  {
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));

    // A pencil held the way a pointer is: the point at the hotspot in the top
    // left and the body running back to the lower right, so the hand it
    // suggests is the one actually on the mouse.
    //
    // Laid out along its own axis rather than by eye. The first attempt put
    // the eraser beside the body instead of on the end of it and drew the body
    // too thin, which came out looking like a sword.
    const auto tip = juce::Point<float>(1.0f, 1.0f);

    // Down and to the right at forty-five degrees, and across it.
    const auto along = 0.70710678f;
    const auto at = [&](float distance, float across) {
      return juce::Point<float>(tip.x + along * (distance + across),
                                tip.y + along * (distance - across));
    };

    constexpr float halfWidth = 3.4f;
    constexpr float shoulder = 5.0f; // where the sharpening stops
    constexpr float ferrule = 15.0f; // where the wood stops
    constexpr float end = 20.0f;

    juce::Path wood;
    wood.startNewSubPath(tip);
    wood.lineTo(at(shoulder, halfWidth));
    wood.lineTo(at(ferrule, halfWidth));
    wood.lineTo(at(ferrule, -halfWidth));
    wood.lineTo(at(shoulder, -halfWidth));
    wood.closeSubPath();

    juce::Path rubber;
    rubber.startNewSubPath(at(ferrule, halfWidth));
    rubber.lineTo(at(end, halfWidth));
    rubber.lineTo(at(end, -halfWidth));
    rubber.lineTo(at(ferrule, -halfWidth));
    rubber.closeSubPath();

    // The graphite, which is the part that says pencil rather than crayon.
    juce::Path lead;
    lead.startNewSubPath(tip);
    lead.lineTo(at(2.6f, 1.8f));
    lead.lineTo(at(2.6f, -1.8f));
    lead.closeSubPath();

    // Outlined in black first, so it reads against a light background as well
    // as against the panel. Same bargain as the LINK pointer.
    juce::Path whole(wood);
    whole.addPath(rubber);

    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.strokePath(whole, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    g.setColour(colours::accent.brighter(0.5f));
    g.fillPath(wood);

    g.setColour(colours::text);
    g.fillPath(rubber);

    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.fillPath(lead);
  }

  return image;
}

juce::MouseCursor drawCursor() {
  constexpr float scale = 2.0f;

  return juce::MouseCursor(juce::ScaledImage(drawCursorImage(scale), scale),
                           {1, 1});
}

juce::MouseCursor linkCursor(LinkCurve curve) {
  // Drawn at twice the nominal size and handed over with a scale, so it stays
  // sharp on a high-density display.
  constexpr float scale = 2.0f;

  return juce::MouseCursor(
      juce::ScaledImage(linkCursorImage(curve, scale), scale), {1, 1});
}

OvertoniumLookAndFeel::OvertoniumLookAndFeel() {
  setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
  setColour(juce::Slider::rotarySliderOutlineColourId, colours::groove);
  setColour(juce::Slider::trackColourId, colours::accent);
  setColour(juce::Slider::backgroundColourId, colours::groove);
  setColour(juce::Slider::thumbColourId, colours::text);
  setColour(juce::Slider::textBoxTextColourId, colours::text);

  setColour(juce::Label::textColourId, colours::text);

  setColour(juce::TextButton::buttonColourId, colours::panelAlt);
  setColour(juce::TextButton::buttonOnColourId, colours::accent);
  setColour(juce::TextButton::textColourOffId, colours::textDim);
  setColour(juce::TextButton::textColourOnId, colours::background);

  setColour(juce::ComboBox::backgroundColourId, colours::panelAlt);
  setColour(juce::ComboBox::textColourId, colours::text);
  setColour(juce::ComboBox::outlineColourId, colours::outline);
  setColour(juce::ComboBox::arrowColourId, colours::textDim);

  setColour(juce::PopupMenu::backgroundColourId, colours::panel);
  setColour(juce::PopupMenu::textColourId, colours::text);
  setColour(juce::PopupMenu::highlightedBackgroundColourId,
            colours::accent.withAlpha(0.28f));
  setColour(juce::PopupMenu::highlightedTextColourId, colours::text);

  setColour(juce::TooltipWindow::backgroundColourId, colours::panel);
  setColour(juce::TooltipWindow::textColourId, colours::text);
  setColour(juce::TooltipWindow::outlineColourId, colours::outline);

  setColour(juce::BubbleComponent::backgroundColourId, colours::panel);
  setColour(juce::BubbleComponent::outlineColourId, colours::outline);

  setColour(juce::ScrollBar::thumbColourId, colours::outline.brighter(0.35f));
  setColour(juce::ScrollBar::trackColourId, colours::background);
}

juce::Font OvertoniumLookAndFeel::getComboBoxFont(juce::ComboBox &) {
  return makeFont(12.0f);
}
juce::Font OvertoniumLookAndFeel::getPopupMenuFont() { return makeFont(14.0f); }
juce::Font OvertoniumLookAndFeel::getSliderPopupFont(juce::Slider &) {
  return makeFont(13.0f, true);
}

void OvertoniumLookAndFeel::drawRotarySlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider &slider) {
  const auto bounds =
      juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
  const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

  if (radius < 4.0f)
    return;

  const auto centre = bounds.getCentre();
  const auto dim = slider.isEnabled() ? 1.0f : 0.4f;
  const auto angle =
      rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // Relative controls read as an offset either side of twelve o'clock rather
  // than as a level filling up from the left.
  const bool bipolar =
      (bool)slider.getProperties().getWithDefault("bipolar", false);
  const auto anchor =
      bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
              : rotaryStartAngle;

  // A knob belonging to an effect that is switched off keeps its position,
  // since a setting is dialled in before the thing is turned on, but nothing
  // on it is lit. A ring glowing on a stage that is not in the signal says the
  // opposite of the truth, and now that the group is named underneath it there
  // is no longer any need for the ring to be what says the group is there.
  //
  // The whole colour is swapped rather than the ring alone. The pointer on the
  // cap is drawn from the same one, and a lit pointer standing in a dark ring
  // reads as one lamp that failed rather than as a stage that is off.
  const bool live =
      !(bool)slider.getProperties().getWithDefault("unlit", false);

  const auto fill =
      live ? slider.findColour(juce::Slider::rotarySliderFillColourId)
           : colours::textDim;

  // How much of a LINK drag this knob is about to take, or is taking. Zero for
  // a knob the drag does not reach.
  const auto glow =
      (float)(double)slider.getProperties().getWithDefault("linkGlow", 0.0);

  // ---- the armed halo -------------------------------------------------------
  // Sits under the ticks and behind the cap, so what shows is a ring of colour
  // in the gap between the two. Bright in proportion to how far the curve is
  // about to move this particular knob.
  if (glow > 0.0f) {
    g.setColour(fill.withAlpha(0.16f * glow * dim));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f));

    // A second, tighter pass around the cap, which is where the eye lands.
    g.setColour(fill.withAlpha(0.12f * glow * dim));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f));
  }

  // ---- waiting for a controller ---------------------------------------------
  // A drawn ring rather than a filled halo, so it reads as a different kind of
  // statement from the LINK glow underneath it and the two can be true at
  // once. See colours::learning.
  if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
    g.setColour(colours::learning.withAlpha(0.9f * dim));
    g.drawEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f),
                  1.4f);
  }

  // ---- the tick ring --------------------------------------------------------
  // Discrete ticks rather than a continuous arc. It reads as a measurement
  // instrument, which is what this thing is, and it echoes the 32 discrete
  // partials the whole synth is built from.
  //
  // When a macro drives this control the ring lights to where the macro has
  // taken the value, in the macro's colour, while the pointer on the cap
  // stays at the value the patch holds, in the channel's. The gap between
  // them is the modulation, and it has to be visible as a gap: a macro
  // offsets what the patch says on its way to the engine and never writes
  // it, so a knob showing only one of the two would be hiding the other.
  const auto drivenBy =
      slider.getProperties().getWithDefault("macroColour", {});

  const bool driven = !drivenBy.isVoid();

  // A macro's colour is a light like any other, so it goes out with the rest
  // when the channel is silenced. The ring still stands where the macro took
  // the value, which is the same bargain the knobs of a switched-off effect
  // make: the setting is kept and the light is not.
  const auto ringColour =
      driven && live ? juce::Colour((juce::uint32)(int)drivenBy) : fill;

  const auto ringAngle =
      driven ? rotaryStartAngle +
                   (float)(double)slider.getProperties().getWithDefault(
                       "macroResult", (double)sliderPos) *
                       (rotaryEndAngle - rotaryStartAngle)
             : angle;

  const int ticks = juce::jlimit(9, 25, juce::roundToInt(radius * 1.15f));
  const auto lo = juce::jmin(anchor, ringAngle);
  const auto hi = juce::jmax(anchor, ringAngle);

  for (int i = 0; i < ticks; ++i) {
    const auto t = (float)i / (float)(ticks - 1);
    const auto a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
    const bool lit = a >= lo - 1.0e-4f && a <= hi + 1.0e-4f;

    const auto inner = radius * (lit ? 0.76f : 0.82f);
    const auto outer = radius * (lit ? 1.00f : 0.96f);
    const auto sinA = std::sin(a);
    const auto cosA = std::cos(a);

    // The unlit ticks warm towards the knob's own colour while it is armed,
    // which lights the whole ring rather than only the part showing the value.
    const auto unlit =
        colours::groove.brighter(0.22f).interpolatedWith(fill, 0.65f * glow);

    g.setColour(lit ? ringColour.withMultipliedAlpha(dim)
                    : unlit.withMultipliedAlpha(dim));
    g.drawLine({centre.x + inner * sinA, centre.y - inner * cosA,
                centre.x + outer * sinA, centre.y - outer * cosA},
               lit ? juce::jmax(1.6f, radius * 0.11f)
                   : juce::jmax(1.0f, radius * 0.07f));
  }

  // ---- the cap --------------------------------------------------------------
  // A skirted disc of the kind a large-format desk has: a flat dark face with
  // a fluted collar poking out from under it, and one wide stripe running
  // from the middle of the face out across the collar.
  //
  // Flat, not domed. A strong radial gradient with a specular bloom reads as
  // a ball bearing, which is not what the top of a control looks like. What
  // little roundness there is lives in the rim and in the shadow the cap
  // throws onto the panel.
  const auto bodyR = radius * 0.68f;
  const juce::Rectangle<float> body(centre.x - bodyR, centre.y - bodyR,
                                    bodyR * 2.0f, bodyR * 2.0f);

  // Cast shadow, down and to the right of the light. Built from a few
  // overlapping ellipses rather than a real blur, which would be far too
  // expensive to run on several hundred controls.
  const auto cast = body.translated(bodyR * 0.10f, bodyR * 0.18f);
  for (int i = 3; i >= 1; --i) {
    g.setColour(juce::Colours::black.withAlpha(0.09f * dim));
    g.fillEllipse(cast.expanded((float)i * bodyR * 0.07f));
  }

  // ---- the collar -----------------------------------------------------------
  // A continuous ring with narrow slots cut into it, which is what the
  // reference has: wide flutes with thin gaps between them, not teeth with
  // daylight between them. Drawn as the ring first and the slots over it, so
  // that the flutes are what is left rather than what is added and the ring
  // can take one gradient for the whole of its lighting.
  const auto rimOut = bodyR;
  const auto rimIn = rimOut * 0.82f;

  // The wash runs from the upper left to the lower right, which is where the
  // light on this panel comes from. A linear gradient across a band two
  // pixels deep is indistinguishable from shading it by angle, and it is one
  // fill rather than one per segment on six hundred knobs.
  // Lighter than the face it rings, or the band and the face are one tone
  // and the collar is two pixels of nothing. What tells them apart is that
  // the collar is a turned edge catching the light and the face is flat.
  const auto litSide = colours::panelAlt.brighter(0.78f);
  const auto darkSide = colours::panelAlt.darker(0.30f);

  g.setGradientFill(juce::ColourGradient(
      litSide.withMultipliedAlpha(dim), centre.x - rimOut * 0.7f,
      centre.y - rimOut * 0.7f, darkSide.withMultipliedAlpha(dim),
      centre.x + rimOut * 0.7f, centre.y + rimOut * 0.7f, false));
  g.fillEllipse(body);

  // Eight, as the reference has, whatever size the knob is drawn at. Sized
  // off the radius this ran to sixteen on a bar knob, and at that pitch the
  // slots are a pixel apart, they alias into a shimmer as they turn, and no
  // single one of them can be followed round, which is exactly what the eye
  // needs in order to see the knob turning at all. Eight is also wide enough
  // apart that the flute between two of them is a face rather than a tooth.
  constexpr int flutes = 8;

  for (int i = 0; i < flutes; ++i) {
    // Turned with the knob, which is the whole of what makes it look like a
    // knob turning rather than a stripe sliding round a disc that never
    // moves. The slots sat at fixed angles and only the stripe travelled, so
    // the cap read as a dial face with a hand on it.
    //
    // What sells it is that the lighting does not turn: the wash above stays
    // put while the slots move through it, so each flute darkens and lightens
    // as it comes round. A texture moving against a fixed light is what a
    // turning surface looks like.
    //
    // The first slot is at the stripe's own angle, so the stripe crosses the
    // collar in the gap between two flutes rather than over the top of one.
    const auto a =
        angle + juce::MathConstants<float>::twoPi * (float)i / (float)flutes;

    const auto sinF = std::sin(a);
    const auto cosF = std::cos(a);

    g.setColour(juce::Colours::black.withAlpha(0.8f * dim));
    // Shallower than the collar is deep, so a ring of it survives unbroken
    // between the slots and the face plate. Cut all the way in and the
    // flutes are separate teeth with the face showing between them, which is
    // a gear rather than a knurled edge.
    const auto slotIn = rimIn + (rimOut - rimIn) * 0.38f;

    g.drawLine({centre.x + slotIn * sinF, centre.y - slotIn * cosF,
                centre.x + (rimOut + 0.3f) * sinF,
                centre.y - (rimOut + 0.3f) * cosF},
               juce::jmax(1.0f, rimOut * 0.13f));
  }

  // ---- the face -------------------------------------------------------------
  // Most of the knob. The collar is a band around the edge rather than a
  // third of the cap, which is the proportion the reference has and what
  // makes the thing read as a disc with a grip round it instead of as a gear.
  const juce::Rectangle<float> face =
      body.withSizeKeepingCentre(rimIn * 2.0f, rimIn * 2.0f);

  g.setGradientFill(juce::ColourGradient(
      colours::panelAlt.brighter(0.10f), centre.x, face.getY(),
      colours::panelAlt.darker(0.30f), centre.x, face.getBottom(), false));
  g.fillEllipse(face);

  // What separates the face from the collar around it. A line along the
  // underside rather than a ring all the way round, because a full ring on a
  // flat face reads as an outline drawn on it.
  //
  // On the lower right, which is the mirror of where the reference has it.
  // That photograph is lit from the upper right and this panel is lit from
  // the upper left, and every shadow on it already falls the other way: the
  // cast shadow under this very cap is offset down and to the right. One rim
  // catching the light from the other side would be the only thing in the
  // window disagreeing about where the light is.
  //
  // Measured clockwise from twelve, so this runs from three o'clock round
  // past six and a little beyond.
  juce::Path underside;
  underside.addCentredArc(centre.x, centre.y, rimIn - 0.4f, rimIn - 0.4f, 0.0f,
                          1.1f, 3.5f, true);

  g.setColour(juce::Colours::black.withAlpha(0.45f * dim));
  g.strokePath(underside, juce::PathStrokeType(1.1f));

  juce::Path topside;
  topside.addCentredArc(centre.x, centre.y, rimIn - 0.4f, rimIn - 0.4f, 0.0f,
                        -2.1f, 0.6f, true);

  g.setColour(juce::Colours::white.withAlpha(0.14f * dim));
  g.strokePath(topside, juce::PathStrokeType(1.0f));

  // ---- the stripe -----------------------------------------------------------
  // One wide mark from the middle of the face out across the collar, in the
  // control's own colour rather than white, so it belongs to the knob and
  // lines up with the lit ticks beyond it instead of sitting on the cap like
  // something stuck there.
  //
  // A rectangle rather than a stroked line, so both ends are square. A line
  // with round caps puts a dome at the centre of the face, which is where the
  // eye is least willing to forgive one.
  const auto weight = juce::jmax(2.0f, radius * 0.15f);

  const auto mark = [&](float from, float to, juce::Colour colour) {
    juce::Path bar;
    bar.addRectangle(-weight * 0.5f, -to, weight, to - from);
    bar.applyTransform(
        juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

    g.setColour(colour);
    g.fillPath(bar);
  };

  const auto head = fill.brighter(0.25f).withMultipliedAlpha(dim);

  mark(0.0f, rimIn, head);

  // It stops in the slot rather than running out to the edge of the collar,
  // so the two flutes either side of it stand a little proud of its tip. That
  // is what the reference does, and it is the difference between a mark let
  // into the knob and one laid across the top of it.

  // The part crossing the collar is a step down from the face, so it takes
  // the collar's light rather than the face's: bright where the stripe is
  // pointing into the light and dark where it is pointing away from it. That
  // is the one place on the knob where the stripe and the lighting have to
  // agree, and it is what stops the mark reading as painted across the top of
  // the flutes rather than let into the gap between two of them.
  const auto into =
      0.5f - 0.5f * (std::cos(angle) * 0.72f - std::sin(angle) * 0.69f);

  mark(rimIn, rimIn + (rimOut - rimIn) * 0.55f,
       head.interpolatedWith(head.darker(0.38f), into));
}

void OvertoniumLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
    juce::Slider &slider) {
  const bool metered =
      (bool)slider.getProperties().getWithDefault("meteredGroove", false);

  // The macro panel's amount, which is a channel fader laid on its side with
  // its own segments rather than a meter's underneath it. There is no meter
  // to put there and nothing to meter: an amount is a setting, not a level.
  //
  // Lit from the middle out rather than from one end. The amount is signed
  // and what it says is a distance, so a run growing either way from nothing
  // is the shape of it, the same way the relative knobs draw their arc from
  // twelve o'clock.
  if (style == juce::Slider::LinearHorizontal &&
      (bool)slider.getProperties().getWithDefault("segmentedTrack", false)) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto dim = slider.isEnabled() ? 1.0f : 0.4f;
    const auto colour = slider.findColour(juce::Slider::trackColourId);

    const auto trackH = juce::jmax(6.0f, bounds.getHeight() * 0.5f);
    const auto track = bounds.withSizeKeepingCentre(bounds.getWidth(), trackH);

    g.setColour(colours::groove);
    g.fillRoundedRectangle(track, trackH * 0.35f);

    const int count =
        juce::jlimit(8, 48, juce::roundToInt(track.getWidth() / 9.0f));

    const auto step = track.getWidth() / (float)count;
    const auto gap = juce::jlimit(1.0f, 3.0f, step * 0.18f);

    // The same pair the meter uses: lit, and the same colour held down low so
    // an amount of nothing still says which macro the row belongs to.
    const auto on = colour.withAlpha(0.92f * dim);
    const auto off = colour.withAlpha(0.13f * dim);

    const auto at = juce::jlimit(track.getX(), track.getRight(), sliderPos);
    const auto mid = track.getCentreX();
    const auto lo = juce::jmin(mid, at);
    const auto hi = juce::jmax(mid, at);

    for (int i = 0; i < count; ++i) {
      const auto cell = juce::Rectangle<float>(track.getX() + (float)i * step,
                                               track.getY(), step, trackH)
                            .reduced(gap * 0.5f, 1.0f);

      const bool on_ = cell.getCentreX() >= lo && cell.getCentreX() <= hi;

      g.setColour(on_ ? on : off);
      g.fillRoundedRectangle(cell, juce::jmin(2.0f, cell.getWidth() * 0.4f));
    }

    // The same glass cap the master fader wears, for the same reason: it is
    // the thing you grab and it has to be unmistakable against a track that
    // is itself lit.
    const auto capW = juce::jmax(6.0f, bounds.getHeight() * 0.3f);
    const juce::Rectangle<float> cap(at - capW * 0.5f, bounds.getY() + 0.5f,
                                     capW, bounds.getHeight() - 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.34f * dim));
    g.fillRoundedRectangle(cap.translated(1.5f, 0.5f), 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.13f * dim));
    g.fillRoundedRectangle(cap, 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.46f * dim));
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.26f * dim));
    g.fillRect(cap.getX() + 1.5f, cap.getY() + 2.5f, 1.0f,
               cap.getHeight() - 5.0f);

    if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
      g.setColour(colours::learning);
      g.drawRoundedRectangle(cap.expanded(1.5f), 2.5f, 1.6f);
    }

    return;
  }

  // The master fader, which is the same idea as a channel's laid on its side:
  // a meter under the whole control and a glass cap over it saying where the
  // level is set. It has no groove of its own for the same reason a channel's
  // has none, and no ticks, because the bar is one row tall and there is
  // nowhere to put them.
  if (style == juce::Slider::LinearHorizontal && metered) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto dim = slider.isEnabled() ? 1.0f : 0.4f;
    const auto at = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPos);

    const auto capW = juce::jmax(6.0f, bounds.getHeight() * 0.30f);
    const juce::Rectangle<float> cap(at - capW * 0.5f, bounds.getY() + 0.5f,
                                     capW, bounds.getHeight() - 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.34f * dim));
    g.fillRoundedRectangle(cap.translated(1.5f, 0.5f), 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.13f * dim));
    g.fillRoundedRectangle(cap, 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.46f * dim));
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

    // The lip runs down the cap rather than across it, which is the same
    // light off the same glass turned a quarter.
    g.setColour(juce::Colours::white.withAlpha(0.26f * dim));
    g.fillRect(cap.getX() + 1.5f, cap.getY() + 2.5f, 1.0f,
               cap.getHeight() - 5.0f);

    // Waiting for a controller. Around the cap rather than around the whole
    // control, since this fader lies across the meter that reads it and a
    // ring at its bounds would enclose the meter and read as marking that.
    if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
      g.setColour(colours::learning);
      g.drawRoundedRectangle(cap.expanded(1.5f), 2.5f, 1.6f);
    }

    return;
  }

  if (style != juce::Slider::LinearVertical) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);

    // The waiting marker, for the plain faders JUCE draws for us. The macro
    // amounts are these, and a control that can be learned has to be able to
    // say it is listening wherever it lives.
    if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
      g.setColour(colours::learning);
      g.drawRoundedRectangle(
          juce::Rectangle<int>(x, y, width, height).toFloat().reduced(0.5f),
          3.0f, 1.4f);
    }

    return;
  }

  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

  // A fader on a channel nobody can hear, which is told the same way a knob
  // is. It had no answer to this at all: a channel's fader is metered, so it
  // draws no track fill to take the colour out of, and the glass cap and its
  // macro bar were staying lit on a silenced strip.
  const bool live =
      !(bool)slider.getProperties().getWithDefault("unlit", false);
  const auto dim = slider.isEnabled() && live ? 1.0f : 0.4f;

  // When a meter sits behind the fader it owns the groove, so the track fill
  // that would otherwise show the set level is dropped. The cap alone says
  // where the fader is, which leaves the whole track free to show output.
  const auto fillTop =
      juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPos);

  if (!metered) {
    const auto centreX = bounds.getCentreX();
    const auto grooveW = juce::jmax(4.0f, bounds.getWidth() * 0.22f);
    const juce::Rectangle<float> groove(centreX - grooveW * 0.5f, bounds.getY(),
                                        grooveW, bounds.getHeight());

    g.setColour(colours::groove);
    g.fillRoundedRectangle(groove, grooveW * 0.5f);

    // A relative fader shows its offset either side of the middle, the same way
    // the relative knobs draw their arc from twelve o'clock.
    const bool bipolar =
        (bool)slider.getProperties().getWithDefault("bipolar", false);
    const auto anchor = bipolar ? bounds.getCentreY() : bounds.getBottom();

    g.setColour(slider.findColour(juce::Slider::trackColourId)
                    .withMultipliedAlpha(0.9f * dim));
    g.fillRoundedRectangle(groove.withTop(juce::jmin(anchor, fillTop))
                               .withBottom(juce::jmax(anchor, fillTop)),
                           grooveW * 0.5f);
  }

  // Armed by LINK, the same as the halo on the knobs. A fader has no ring to
  // warm, so the outline of its track carries it instead.
  const auto glow =
      (float)(double)slider.getProperties().getWithDefault("linkGlow", 0.0);

  if (glow > 0.0f) {
    // The channel's own colour for LINK, which is saying how much this one
    // would take, and the accent for the drawing, which is saying that the
    // whole band is one surface to sweep across. The accent is also what the
    // switch that armed it is lit in, so the lit band and the lit switch read
    // as one statement.
    const auto lit =
        (bool)slider.getProperties().getWithDefault("glowAccent", false)
            ? colours::accent
            : slider.findColour(juce::Slider::trackColourId);

    g.setColour(lit.withAlpha(0.10f * glow * dim));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(lit.withAlpha(0.55f * glow * dim));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.2f);
  }

  // The same statement a knob's ring makes, on the one shape a fader has.
  if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
    g.setColour(colours::learning.withAlpha(0.9f * dim));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.4f);
  }

  const auto macro = slider.getProperties().getWithDefault("macroColour", {});
  const bool driven = !macro.isVoid();

  const auto tint = live && driven ? juce::Colour((juce::uint32)(int)macro)
                                   : colours::textDim;

  // How far up the macro has taken this fader, as a fraction of the track.
  // Negative when nothing is driving it, so no segment counts as lit.
  const auto reached =
      driven ? (float)(double)slider.getProperties().getWithDefault(
                   "macroResult", -1.0)
             : -1.0f;

  // ---- the macro's own scale ------------------------------------------------
  // Two columns of segments either side of the track, lit from its foot up to
  // the level the macro has taken this fader to, exactly as a knob's ring
  // lights from its anchor.
  //
  // The same segments the meter is made of, at the same pitch and in the same
  // places, only narrower: the fader and the meter are given one rectangle
  // between them, so the gutters can line up with the track rather than being
  // a second scale beside it. They share the arithmetic as well as the
  // rectangle, or a pixel of drift puts them out of step.
  //
  // Only when a macro has the fader. There is nothing for an undriven one to
  // say here, and the meter, the cap and the figure under it already say
  // where the level is three times over.
  if (driven && bounds.getWidth() >= 22.0f) {
    // The slider's own rectangle, not the one this was handed. JUCE insets
    // what it passes a look and feel by the thumb's radius, so the track it
    // gives is a few pixels shorter than the component, and the meter lays
    // its segments out across the whole of its own. Both read the same
    // arithmetic from meterSegments and still came out at different heights:
    // the same count of shorter segments, covering less of the track.
    const auto whole = slider.getLocalBounds().toFloat();

    const auto trackW = juce::jmax(6.0f, whole.getWidth() * 0.62f);
    const auto track = whole.withSizeKeepingCentre(trackW, whole.getHeight());

    const int count = meterSegments((int)whole.getHeight());
    const auto step = track.getHeight() / (float)count;
    const auto gap = juce::jlimit(1.0f, 3.0f, step * 0.18f);

    // What is left either side of the track, less a pixel of air against the
    // track and another against the edge of the strip.
    const auto lane = juce::jmax(1.5f, (track.getX() - whole.getX()) - 2.0f);

    const auto onNow =
        juce::jlimit(0, count, juce::roundToInt(reached * (float)count));

    // Unlit in the macro's own colour held down low, the way the meter's
    // unlit segments are the channel's. A column that goes grey where it ends
    // reads as two scales rather than as one that is partly on.
    const auto on = tint.withMultipliedAlpha(0.8f * dim);
    const auto off = tint.withMultipliedAlpha(0.14f * dim);

    for (int i = 0; i < count; ++i) {
      const auto cell =
          juce::Rectangle<float>(
              0.0f, track.getBottom() - (float)(i + 1) * step, lane, step)
              .reduced(0.0f, gap * 0.5f);

      const auto corner = juce::jmin(2.0f, cell.getHeight() * 0.4f);

      g.setColour(i < onNow ? on : off);
      g.fillRoundedRectangle(cell.withX(whole.getX() + 1.0f), corner);
      g.fillRoundedRectangle(cell.withX(whole.getRight() - 1.0f - lane),
                             corner);
    }
  }

  // Glass cap: a translucent window on the meter, edged so it reads as a cap
  // rather than as a gap, and lit along the top the way the buttons and the
  // knob bodies are.
  //
  // It used to carry a bright line across its middle as well, for reading the
  // exact position off the track. Between that line and an edge of nearly the
  // same weight the cap came out as a pill with a slot cut in it, three light
  // lines inside ten pixels, and it was the whitest thing on a strip that has
  // since gone darker and more colourful around it. The line is also no longer
  // needed: the exact position is printed in dB under the fader.
  const auto capH = juce::jmax(6.0f, bounds.getWidth() * 0.30f);
  const juce::Rectangle<float> cap(bounds.getX() + 0.5f, fillTop - capH * 0.5f,
                                   bounds.getWidth() - 1.0f, capH);

  g.setColour(juce::Colours::black.withAlpha(0.34f * dim));
  g.fillRoundedRectangle(cap.translated(0.5f, 1.5f), 2.0f);

  // Light enough that the lit segments behind it stay legible through the
  // glass, which is the whole reason the meter runs under the fader.
  //
  // Tinted rather than outlined when a macro has this fader. The glass is
  // the one part of a fader that can take a colour without becoming a line:
  // an edge in the macro's colour sat right against the cap's own white one,
  // two hard rings a pixel apart, and read as a sticker put on the cap
  // rather than as the cap being driven.
  g.setColour(driven ? tint.withMultipliedAlpha(0.3f * dim)
                     : juce::Colours::white.withAlpha(0.13f * dim));
  g.fillRoundedRectangle(cap, 2.0f);

  // The cap's own edge, which is white whatever is driving it. What says a
  // macro has this fader is the tint in the glass and the lit run up the
  // scale, neither of which is a line laid over something else.
  g.setColour(juce::Colours::white.withAlpha(0.46f * dim));
  g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

  // The lip catches the light off centre, so it says glass rather than
  // dividing the cap in half.
  g.setColour(juce::Colours::white.withAlpha(0.26f * dim));
  g.fillRect(cap.getX() + 2.5f, cap.getY() + 1.5f, cap.getWidth() - 5.0f, 1.0f);
}

void OvertoniumLookAndFeel::drawButtonBackground(
    juce::Graphics &g, juce::Button &button,
    const juce::Colour &backgroundColour, bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown) {
  drawButtonFace(g, button, button.getToggleState(), backgroundColour,
                 shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

OvertoniumLookAndFeel::LampGang
OvertoniumLookAndFeel::lampGangOf(const juce::Component &c) {
  const auto side = c.getProperties().getWithDefault("lampGang", {}).toString();

  if (side == "left")
    return LampGang::Left;

  if (side == "right")
    return LampGang::Right;

  return LampGang::Alone;
}

void OvertoniumLookAndFeel::gangLamps(juce::Component &left,
                                      juce::Component &right) {
  left.getProperties().set("lampGang", "left");
  right.getProperties().set("lampGang", "right");
}

juce::Rectangle<float>
OvertoniumLookAndFeel::lampCapBounds(juce::Rectangle<float> bounds,
                                     LampGang gang) {
  const auto wall = juce::jlimit(2.0f, 4.5f, bounds.getHeight() * 0.15f);
  auto cap = bounds.reduced(wall);

  // Half a wall on the joined side, the neighbour giving the other half.
  if (gang == LampGang::Left)
    cap.setRight(bounds.getRight() - wall * 0.5f);
  else if (gang == LampGang::Right)
    cap.setLeft(bounds.getX() + wall * 0.5f);

  return cap;
}

juce::Colour OvertoniumLookAndFeel::lampFace(juce::Colour lamp, bool on) {
  if (!on)
    // White plastic in an unlit room, which is a grey rather than a white.
    // Light enough to read a printed legend off it, dark enough that every
    // lamp is brighter than it when one comes on, and the second condition is
    // the binding one: the reds are the darkest lamps here and they are what
    // this has to stay under. Raising it is the lever if the caps want to be
    // whiter, and the margin on the mute is what it costs.
    return juce::Colour(0xff6f757c);

  // The lamp seen through that plastic. Bright, because the light is behind
  // the whole face rather than painted onto part of it, and milky, because it
  // has come through a diffuser. The mix towards white is both what a white
  // cap with a lamp in it looks like and what lifts the reds clear of the
  // unlit grey.
  //
  // Saturation goes up rather than down with the light. These lamps are
  // chrome colours chosen to be read as small text on a dark panel, so they
  // are already pale, and raising the value without the saturation walks them
  // towards white before the diffuser gets to.
  return lamp.withMultipliedSaturation(1.25f)
      .withBrightness(juce::jmax(lamp.getBrightness(), 0.92f))
      .interpolatedWith(juce::Colours::white, 0.14f);
}

juce::Colour OvertoniumLookAndFeel::lampLegend() {
  // Printed on the plastic, so it is the same whatever is behind it, and a
  // button never changes the colour of its own word by lighting. Not quite
  // black, which against a lit gold reads as a hole in the cap rather than
  // as ink on it.
  return juce::Colour(0xff0e1116);
}

void OvertoniumLookAndFeel::drawLampCap(juce::Graphics &g,
                                        juce::Rectangle<float> bounds,
                                        juce::Colour lamp, bool on,
                                        bool highlighted, bool down,
                                        LampGang gang) {
  // How much of the button the bezel takes. A fixed inset rather than a
  // share of the size, because the bezel is a moulding: it is the same few
  // millimetres whether the button is a wide one or a small one, and scaling
  // it would turn the small ones into a frame with a dot in the middle.
  const auto cap = lampCapBounds(bounds, gang);
  const auto wall = cap.getY() - bounds.getY();
  // Barely rounded. These are moulded square caps, and the radius that read
  // as a chamfer on a wide button turned the small ones into lozenges: a
  // 14 px mute has only 7 px of half-height for a corner to eat into.
  const auto outerCorner = juce::jmin(2.6f, bounds.getHeight() * 0.18f);

  // The moulding both halves of a gang stand in, which each of them draws in
  // full and shows its own half of, the rest falling outside the component
  // and being clipped away. Drawing half a well each would mean mitring the
  // joint and suppressing two edge strokes along it, for the same picture.
  auto well = bounds;

  if (gang == LampGang::Left) {
    well.setWidth(bounds.getWidth() * 2.0f);
  } else if (gang == LampGang::Right) {
    // setX moves the rectangle rather than growing it, so the width has to
    // follow or the far edge comes back with it.
    well.setX(bounds.getX() - bounds.getWidth());
    well.setWidth(bounds.getWidth() * 2.0f);
  }

  // ---- the well -----------------------------------------------------------
  // Darker than the panel it sits in, so the cap is in a hole rather than on
  // a plinth, and lit along its lower inside edge where a carved well catches
  // the light that misses its top wall.
  //
  // Dark against this panel rather than dark absolutely. The reference
  // photographs are of a machine whose panel is near black, so their bezels
  // can be too, and copying the value put a pit in a panel that is (20, 24,
  // 29). This is a step below what it is cut into, which is what reads as a
  // moulding here.
  g.setColour(juce::Colour(0xff0d1015));
  g.fillRoundedRectangle(well, outerCorner);

  // Two edges rather than one. The dark outside is the moulding standing
  // above the panel and the light inside is the floor of the well catching
  // what misses its top wall, and together they are what says "carved" at a
  // size too small to model properly.
  g.setColour(juce::Colours::black.withAlpha(0.45f));
  g.drawRoundedRectangle(well.reduced(0.5f), outerCorner, 1.0f);

  g.setColour(juce::Colours::white.withAlpha(0.09f));
  g.drawRoundedRectangle(well.reduced(1.2f).translated(0.0f, 0.8f),
                         outerCorner * 0.9f, 1.0f);

  const auto corner = juce::jmax(1.0f, outerCorner - wall * 0.5f);

  // ---- what the lamp is doing ---------------------------------------------
  // Lit, the plastic is the colour itself. Unlit, it is the same hue with the
  // light taken out of it rather than a grey: an unlit amber reads as brown
  // and an unlit green as olive, which is what says the button could light.
  // The plastic at the middle of the cap, where the lamp is behind it. Every
  // shade below is this darkened: the hotspot is the base rather than
  // something added, so the whole face is lit and only the edges fall away.
  auto face = lampFace(lamp, on);

  if (down)
    face = face.brighter(0.12f);
  else if (highlighted)
    face = face.brighter(on ? 0.06f : 0.18f);

  // ---- the cast ------------------------------------------------------------
  // A lit cap puts a trace of its colour on the moulding around it, and that
  // is all it does: an even wash over the whole bezel rather than a halo
  // hugging the cap.
  //
  // The halo was the better physics and the worse picture. Light fading out
  // from the cap's edge is light escaping around the cap, which only happens
  // if the cap has sunk below the moulding, so a lit button read as pressed
  // in no matter how faint the glow was. The shape was the problem rather
  // than the strength. There is also nowhere to put a real bloom: the
  // component ends at the bezel's outside edge, perhaps three pixels out,
  // which is too little to fall off in and is exactly the ring that reads as
  // a gap.
  if (on) {
    g.setColour(lamp.withAlpha(0.07f));
    g.fillRoundedRectangle(well, outerCorner);
  }

  if (!down) {
    // Proud: the cap stands above the well and drops a shadow onto its floor.
    //
    // Whether it is lit or not. These are indicator lamps rather than latching
    // switches, and a cap that sinks when its thing is on says that pressing
    // it is what turns the thing off, which is the wrong way round for ECHO or
    // for a mute. Only a finger on it puts it down, and only while it is
    // there: see the branch below.
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), corner);
  }

  // ---- the cap ------------------------------------------------------------
  // One shading for both states, because the cap does not move when the lamp
  // comes on. It is lit from above like any raised surface, brightest just
  // below its top edge and falling away to the bottom.
  //
  // Shallow. The plastic is flat and what says it stands proud is the shadow
  // it throws into the well and the lip along its top rather than a steep
  // gradient down its face, which is what the photographs show: an unlit cap
  // there reads (59, 42, 18) at the top and (59, 42, 17) at the bottom.
  {
    juce::ColourGradient down_(face.brighter(0.10f), cap.getCentreX(),
                               cap.getY(), face.darker(0.22f), cap.getCentreX(),
                               cap.getBottom(), false);

    // The lamp sits behind the middle of the cap, so a lit one holds its
    // brightness further down the face before it falls away. Unlit there is
    // nothing behind it and the fall is even.
    if (on)
      down_.addColour(0.52, face);

    g.setGradientFill(down_);
  }

  g.fillRoundedRectangle(cap, corner);

  // The lamp is a point behind the middle of the cap, so the light falls off
  // towards the ends as well as towards the top and bottom. Without this the
  // gradient above was the only shading and a wide cap was a uniform bar of
  // colour from end to end, which is what a painted face looks like rather
  // than a lit one. A horizontal wash rather than a radial gradient, because
  // the caps run from a 14 px square mute to a 62 px CHARACTER and a circular
  // hotspot would be a disc on the wide ones.
  {
    const auto edge = juce::Colours::black.withAlpha(on ? 0.18f : 0.16f);

    juce::ColourGradient sides(edge, cap.getX(), cap.getCentreY(), edge,
                               cap.getRight(), cap.getCentreY(), false);

    // Flat across the middle third, so the word sits on an even face and only
    // the ends darken.
    sides.addColour(0.28, juce::Colours::transparentBlack);
    sides.addColour(0.72, juce::Colours::transparentBlack);

    g.setGradientFill(sides);
    g.fillRoundedRectangle(cap, corner);
  }

  if (down) {
    // The one time a cap sits in its well, and only while a finger is on it.
    // The wall above then throws a shadow across its top, which is the step
    // that says it has moved.
    const auto deep = juce::jmax(2.0f, cap.getHeight() * 0.3f);

    g.setGradientFill(juce::ColourGradient(
        juce::Colours::black.withAlpha(0.34f), cap.getCentreX(), cap.getY(),
        juce::Colours::transparentBlack, cap.getCentreX(), cap.getY() + deep,
        false));
    g.fillRoundedRectangle(cap, corner);
  } else {
    // A lip along the top, which is the light the raised edge catches. The
    // whole of what it has to beat is the shadow under the cap, so it is here
    // whether the lamp is on or off.
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.fillRoundedRectangle(cap.getX() + 1.2f, cap.getY() + 0.6f,
                           cap.getWidth() - 2.4f, 1.0f, 0.5f);
  }

  // ---- the sheen ----------------------------------------------------------
  // A white wash down from the top, which is light falling on a raised
  // surface. Faint, because white over a colour takes the colour out of it,
  // and the whole of what makes the plastic read as plastic is that it keeps
  // its hue. Fainter still on a lit cap, which has its own light to show and
  // less of anything else to lose.
  if (!down) {
    const auto sheen =
        cap.withHeight(cap.getHeight() * 0.52f).reduced(1.0f, 0.0f);

    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(on ? 0.04f : 0.06f), sheen.getCentreX(),
        sheen.getY(), juce::Colours::transparentWhite, sheen.getCentreX(),
        sheen.getBottom(), false));
    g.fillRoundedRectangle(sheen, corner * 0.8f);
  }

  // ---- the cap's edge -----------------------------------------------------
  // What separates the plastic from the well it stands in. Lighter on a lit
  // cap, where a hard black line around a bright face reads as a border drawn
  // on rather than as the side of something.
  g.setColour(juce::Colours::black.withAlpha(on ? 0.30f : 0.45f));
  g.drawRoundedRectangle(cap.reduced(0.5f), corner, 1.0f);
}

void OvertoniumLookAndFeel::drawButtonFace(juce::Graphics &g,
                                           juce::Button &button, bool on,
                                           const juce::Colour &backgroundColour,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
  const auto gang = lampGangOf(button);

  auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

  // The half pixel goes back on the joined side. It is there so a stroked
  // edge lands on the pixel rather than across two of it, and there is no
  // edge to land on where the two halves meet: leaving it took a pixel out of
  // the moulding down the middle of every gang.
  if (gang == LampGang::Left)
    bounds.setRight(bounds.getRight() + 0.5f);
  else if (gang == LampGang::Right)
    bounds.setLeft(bounds.getX() - 0.5f);

  // The colour behind the plastic, which every button already declares as
  // the colour its word lights in. A button that names none is chrome rather
  // than a lamp, and wears the accent.
  auto lamp = button.findColour(juce::TextButton::textColourOnId);

  if (lamp.isTransparent())
    lamp = colours::accent;

  // backgroundColour still decides for anything that asked for a particular
  // face, which is how the macro panel's colour swatches are drawn.
  if (!backgroundColour.isTransparent() &&
      backgroundColour != colours::panelAlt)
    lamp = backgroundColour;

  drawLampCap(g, bounds, lamp, on, shouldDrawButtonAsHighlighted,
              shouldDrawButtonAsDown, gang);

  // Waiting for a controller, in place of the cap's own edge rather than
  // beside it: a mute button is too small to carry a second ring outside its
  // own.
  if ((bool)button.getProperties().getWithDefault("learnArmed", false)) {
    const auto corner = juce::jmin(4.5f, bounds.getHeight() * 0.28f);

    g.setColour(colours::learning);
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.4f);
  }
}

void OvertoniumLookAndFeel::drawButtonText(juce::Graphics &g,
                                           juce::TextButton &button, bool,
                                           bool) {
  const auto h = (float)button.getHeight();
  g.setFont(makeFont(juce::jlimit(8.0f, 13.0f, h * 0.58f), true));

  // The same ink whatever the lamp is doing: the word is printed on the cap
  // rather than being part of what lights. See lampLegend.
  g.setColour(lampLegend());

  g.drawText(button.getButtonText(), button.getLocalBounds(),
             juce::Justification::centred, false);
}

void OvertoniumLookAndFeel::drawComboBox(juce::Graphics &g, int width,
                                         int height, bool, int, int, int, int,
                                         juce::ComboBox &box) {
  const auto bounds =
      juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);

  const auto fill = box.findColour(juce::ComboBox::backgroundColourId);

  g.setGradientFill(juce::ColourGradient(
      fill.brighter(0.12f), bounds.getCentreX(), bounds.getY(),
      fill.darker(0.05f), bounds.getCentreX(), bounds.getBottom(), false));
  g.fillRoundedRectangle(bounds, 4.0f);

  g.setColour(juce::Colours::white.withAlpha(0.08f));
  g.fillRoundedRectangle(bounds.getX() + 2.0f, bounds.getY() + 1.0f,
                         bounds.getWidth() - 4.0f, 1.0f, 0.5f);

  g.setColour(box.findColour(juce::ComboBox::outlineColourId));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

  juce::Path chevron;
  const auto cx = bounds.getRight() - 12.0f;
  const auto cy = bounds.getCentreY();
  chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
  chevron.lineTo(cx, cy + 2.5f);
  chevron.lineTo(cx + 4.0f, cy - 2.0f);

  g.setColour(box.findColour(juce::ComboBox::arrowColourId));
  g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// =============================================================================
// The standalone's title bar.

namespace {
/// The minimise or the close in it.
///
/// A shape and nothing else. JUCE's own are coloured discs, which is a
/// convention from another desktop and reads as a traffic light sitting on the
/// panel. These are drawn the way every other small mark in the instrument is:
/// dim until the pointer is on them, and then lit.
class TitleBarButton final : public juce::Button {
public:
  TitleBarButton(const juce::String &name, juce::Path glyph, juce::Colour lit)
      : juce::Button(name), shape(std::move(glyph)), hovered(lit) {}

  void paintButton(juce::Graphics &g, bool highlighted, bool down) override {
    auto area = getLocalBounds().toFloat().reduced((float)getHeight() * 0.32f);

    g.setColour(highlighted || down ? hovered : colours::textDim);
    g.fillPath(shape, shape.getTransformToScaleToFit(area, true));
  }

private:
  juce::Path shape;
  juce::Colour hovered;
};
} // namespace

juce::Button *OvertoniumLookAndFeel::createDocumentWindowButton(int type) {
  constexpr float kThickness = 0.14f;

  juce::Path shape;

  if (type == juce::DocumentWindow::closeButton) {
    shape.addLineSegment({0.0f, 0.0f, 1.0f, 1.0f}, kThickness);
    shape.addLineSegment({1.0f, 0.0f, 0.0f, 1.0f}, kThickness);

    // The one button worth being able to hit by accident, so it is the one
    // that says so before you do.
    return new TitleBarButton("close", std::move(shape), colours::muteOn);
  }

  if (type == juce::DocumentWindow::minimiseButton) {
    shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, kThickness);
    return new TitleBarButton("minimise", std::move(shape), colours::text);
  }

  shape.addLineSegment({0.5f, 0.0f, 0.5f, 1.0f}, kThickness);
  shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, kThickness);

  return new TitleBarButton("maximise", std::move(shape), colours::text);
}

void OvertoniumLookAndFeel::drawDocumentWindowTitleBar(
    juce::DocumentWindow &window, juce::Graphics &g, int w, int h,
    int titleSpaceX, int titleSpaceW, const juce::Image *, bool) {
  if (w * h == 0)
    return;

  const auto bounds = juce::Rectangle<int>(0, 0, w, h);

  // Lit from above and grained, like every other surface here, so the bar is
  // the top of the instrument rather than a lid on it.
  g.setGradientFill(juce::ColourGradient(colours::panel.brighter(0.10f), 0.0f,
                                         0.0f, colours::panel.darker(0.25f),
                                         0.0f, (float)h, false));
  g.fillRect(bounds);

  paintGrain(g, bounds);

  g.setColour(colours::outline);
  g.fillRect(0, h - 1, w, 1);

  // The name, dimmed when the window is not the one being worked in, which is
  // the only thing the title bar has to say.
  g.setFont(makeFont((float)h * 0.54f, true));
  g.setColour(window.isActiveWindow() ? colours::text : colours::textDim);

  // Centred on the window rather than on the space between its buttons.
  //
  // The space is what JUCE offers, and it is the wrong middle: the buttons
  // are all at one end, and the standalone puts its own Options button at the
  // other without telling the title bar about it, so a name centred in what
  // is left sits left of the window's middle. Centred on the whole width and
  // then held inside the space, which only bites on a window too narrow to
  // hold the name in the middle anyway.
  const auto text = window.getName();
  const auto width = juce::jmin(
      titleSpaceW,
      juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 8);

  const auto x = juce::jlimit(titleSpaceX, titleSpaceX + titleSpaceW - width,
                              (w - width) / 2);

  g.drawText(text, juce::Rectangle<int>(x, 0, width, h),
             juce::Justification::centred, true);
}

} // namespace ovt::ui
