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
/// How much of the grain shows. Higher and the thin lit edges lose against it.
constexpr float kGrainStrength = 0.020f;

/// The grain tile's side in pixels.
constexpr int kGrainSize = 128;
} // namespace

namespace {
/// Counted rather than inferred, so a test can see the tile go.
int grainBuilds = 0;
} // namespace

int grainTileBuildCount() { return grainBuilds; }

GrainTile::GrainTile() {
  // Built on first use and shared by every window. See GrainTile for its
  // lifetime.
  ++grainBuilds;

  image = [] {
    juce::Image img(juce::Image::ARGB, kGrainSize, kGrainSize, true);
    juce::Image::BitmapData data(img, juce::Image::BitmapData::writeOnly);

    // Fixed seed, so screenshots can be compared between runs.
    juce::Random rng(0x5EED0001);

    for (int y = 0; y < kGrainSize; ++y) {
      for (int x = 0; x < kGrainSize; ++x) {
        // Signed noise, so the grain both lifts and lowers the surface, and
        // cubed, so most of the tile is nearly clear.
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
  // While any look and feel holds the tile, this costs a reference and not a
  // rebuild.
  const juce::SharedResourcePointer<GrainTile> held;
  return held->image;
}

void paintGrain(juce::Graphics &g, juce::Rectangle<int> area) {
  if (area.isEmpty())
    return;

  g.setTiledImageFill(grainTile(), 0, 0, kGrainStrength);
  g.fillRect(area);
}

void paintChannelBackground(juce::Component &c, juce::Graphics &g,
                            juce::Rectangle<int> bounds, juce::Colour base) {
  if (auto *lnf = dynamic_cast<OvertoniumLookAndFeel *>(&c.getLookAndFeel()))
    lnf->drawChannelBackground(g, bounds, base);
  else
    paintChannelBackground(g, bounds, base);
}

void OvertoniumLookAndFeel::drawChannelBackground(juce::Graphics &g,
                                                  juce::Rectangle<int> bounds,
                                                  juce::Colour base) {
  if (bounds.isEmpty())
    return;

  const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();

  for (const auto &b : backgrounds)
    if (b.width == bounds.getWidth() && b.height == bounds.getHeight() &&
        std::abs(b.scale - scale) < 1.0e-4f && b.base == base.getARGB()) {
      g.drawImage(b.picture, bounds.toFloat());
      return;
    }

  // Drawn at the display's own resolution, so it is copied pixel for pixel.
  const auto w =
      juce::jmax(1, juce::roundToInt((float)bounds.getWidth() * scale));
  const auto h =
      juce::jmax(1, juce::roundToInt((float)bounds.getHeight() * scale));
  juce::Image picture(juce::Image::ARGB, w, h, true);
  {
    juce::Graphics into(picture);
    into.addTransform(
        juce::AffineTransform::scale((float)w / (float)bounds.getWidth(),
                                     (float)h / (float)bounds.getHeight()));
    paintChannelBackground(into, bounds.withZeroOrigin(), base);
  }

  if (backgrounds.size() >= 6)
    backgrounds.erase(backgrounds.begin());

  backgrounds.push_back(
      {bounds.getWidth(), bounds.getHeight(), scale, base.getARGB(), picture});
  g.drawImage(picture, bounds.toFloat());
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
/// How hard the hover marks sit on the panel, shared by the row and the column
/// so the two always match.
constexpr float kHoverWash = 0.030f;
constexpr float kHoverEdge = 0.10f;

/// How lit a display's ground is, at rest and hovered.
constexpr float kDisplayWash = 0.07f;
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

void paintGroupPocketShade(juce::Graphics &g, juce::Rectangle<int> r) {
  const auto f = r.toFloat();

  // Shadow inside the top edge, light inside the bottom one. The whole
  // outline is stroked each time and clipped to its half, which keeps both
  // marks on the box's own rounded edge.
  const auto inner = f.reduced(0.5f);
  const auto middle = f.getCentreY();

  {
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(f.withBottom(middle).getSmallestIntegerContainer());
    g.setColour(juce::Colours::black.withAlpha(0.34f));
    g.drawRoundedRectangle(inner.translated(0.0f, 1.0f), 4.0f, 1.3f);
  }

  {
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(f.withTop(middle).getSmallestIntegerContainer());
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(inner.translated(0.0f, -1.0f), 4.0f, 1.0f);
  }

  g.setColour(colours::outline.withAlpha(0.9f));
  g.drawRoundedRectangle(f.reduced(0.5f), 4.0f, 1.0f);
}

void paintGroupPocketFill(juce::Graphics &g, juce::Rectangle<int> r) {
  g.setColour(colours::panelAlt.withAlpha(0.75f));
  g.fillRoundedRectangle(r.toFloat(), 4.0f);
}

void paintGroupPocket(juce::Graphics &g, juce::Rectangle<int> r) {
  paintGroupPocketFill(g, r);
  paintGroupPocketShade(g, r);
}

void paintRecess(juce::Graphics &g, juce::Rectangle<float> opening,
                 float corner, float depth) {
  // The lit lip first, then the shadow over it: the opening moved up and to
  // the left by the depth, which leaves light along the bottom and the right.
  // The shadow is the size of the opening, not the lip, so the lip comes out
  // even on every side. It is clipped to the lip so it cannot land on bare
  // panel at the top left corner.
  const auto lip = opening.expanded(depth);

  g.setColour(juce::Colours::white.withAlpha(0.11f));
  g.fillRoundedRectangle(lip, corner + depth);

  juce::Path rim;
  rim.addRoundedRectangle(lip, corner + depth);

  juce::Graphics::ScopedSaveState keep(g);
  g.reduceClipRegion(rim);
  g.setColour(juce::Colours::black.withAlpha(0.6f));
  g.fillRoundedRectangle(opening.translated(-depth, -depth), corner + depth);
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

    // A pencil with its point at the hotspot in the top left and its body
    // running to the lower right, laid out along its own axis.
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

    // The graphite.
    juce::Path lead;
    lead.startNewSubPath(tip);
    lead.lineTo(at(2.6f, 1.8f));
    lead.lineTo(at(2.6f, -1.8f));
    lead.closeSubPath();

    // Outlined in black first, so it reads against a light background as well
    // as against the panel.
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

  // Relative controls read as an offset either side of twelve o'clock rather
  // than as a level filling up from the left.
  const bool bipolar =
      (bool)slider.getProperties().getWithDefault("bipolar", false);
  const auto anchor =
      bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
              : rotaryStartAngle;

  // A knob standing for something that does not exist yet, such as an unmade
  // macro: ring unlit, no pointer, and the cap at rest, since the collar turns
  // with the knob and would give a setting away.
  const bool blank =
      (bool)slider.getProperties().getWithDefault("blank", false);

  const auto angle =
      blank
          ? anchor
          : rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // A knob on an effect that is off or a channel nobody can hear keeps its
  // position but its ring goes dark. The pointer keeps the plain text grey so
  // it still says where the knob stands.
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
  // Under the ticks and behind the cap, bright in proportion to how far the
  // curve is about to move this knob.
  if (glow > 0.0f) {
    g.setColour(fill.withAlpha(0.16f * glow * dim));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f));

    // A second, tighter pass around the cap, which is where the eye lands.
    g.setColour(fill.withAlpha(0.12f * glow * dim));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f));
  }

  // ---- waiting for a controller ---------------------------------------------
  // A drawn ring rather than a filled halo, so it can show at the same time as
  // the LINK glow. See colours::learning.
  if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
    g.setColour(colours::learning.withAlpha(0.9f * dim));
    g.drawEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f),
                  1.4f);
  }

  // ---- the tick ring --------------------------------------------------------
  // When a macro drives this control the ring lights to the macro's result in
  // the macro's colour, while the pointer stays at the value the patch holds.
  // The gap between them is the modulation.
  const auto drivenBy =
      slider.getProperties().getWithDefault("macroColour", {});

  const bool driven = !drivenBy.isVoid();

  // A macro's colour goes out with the rest of the ring when the channel is
  // silenced.
  const auto ringColour =
      driven ? juce::Colour((juce::uint32)(int)drivenBy) : fill;

  const auto ringAngle =
      driven ? rotaryStartAngle +
                   (float)(double)slider.getProperties().getWithDefault(
                       "macroResult", (double)sliderPos) *
                       (rotaryEndAngle - rotaryStartAngle)
             : angle;

  const int ticks = juce::jlimit(9, 25, juce::roundToInt(radius * 1.15f));
  const auto lo = juce::jmin(anchor, ringAngle);
  const auto hi = juce::jmax(anchor, ringAngle);

  // Where the travel changes kind, as a share of it: an attack below its
  // shortest time sets the start phase instead. The ticks below it are drawn
  // as dots. Nought on every knob that has no such stretch.
  const auto split =
      (float)(double)slider.getProperties().getWithDefault("phaseSplit", 0.0);

  for (int i = 0; i < ticks; ++i) {
    const auto t = (float)i / (float)(ticks - 1);
    const auto a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
    const bool lit = live && !blank && a >= lo - 1.0e-4f && a <= hi + 1.0e-4f;

    const auto inner = radius * (lit ? 0.76f : 0.82f);
    const auto outer = radius * (lit ? 1.00f : 0.96f);
    const auto sinA = std::sin(a);
    const auto cosA = std::cos(a);

    // The unlit ticks warm towards the knob's own colour while LINK arms it.
    const auto unlit =
        colours::groove.brighter(0.22f).interpolatedWith(fill, 0.65f * glow);

    g.setColour(lit ? ringColour.withMultipliedAlpha(dim)
                    : unlit.withMultipliedAlpha(dim));

    if (t < split - 1.0e-4f) {
      // A dot as wide as the tick is thick, at the middle of its length.
      const auto mid = (inner + outer) * 0.5f;
      const auto d = lit ? juce::jmax(2.4f, radius * 0.17f)
                         : juce::jmax(1.6f, radius * 0.11f);

      g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(
          {centre.x + mid * sinA, centre.y - mid * cosA}));
      continue;
    }

    g.drawLine({centre.x + inner * sinA, centre.y - inner * cosA,
                centre.x + outer * sinA, centre.y - outer * cosA},
               lit ? juce::jmax(1.6f, radius * 0.11f)
                   : juce::jmax(1.0f, radius * 0.07f));
  }

  // ---- the cap --------------------------------------------------------------
  // A flat dark face with a fluted collar round it and one wide stripe from
  // the middle out across the collar.
  const auto bodyR = radius * 0.68f;
  const juce::Rectangle<float> body(centre.x - bodyR, centre.y - bodyR,
                                    bodyR * 2.0f, bodyR * 2.0f);

  // Cast shadow, down and to the right, from a few overlapping ellipses
  // rather than a blur, which would be too expensive on hundreds of controls.
  const auto cast = body.translated(bodyR * 0.10f, bodyR * 0.18f);
  for (int i = 3; i >= 1; --i) {
    g.setColour(juce::Colours::black.withAlpha(0.09f * dim));
    g.fillEllipse(cast.expanded((float)i * bodyR * 0.07f));
  }

  // ---- the collar -----------------------------------------------------------
  // A ring with narrow notches cut through it, drawn as one path so it takes
  // one gradient for its lighting.
  const auto rimOut = bodyR;
  const auto rimIn = rimOut * 0.855f;

  // How far a notch bites into the collar band, as a share of it.
  constexpr float kNotchDepth = 0.28f;

  // The collar wall's tone, with only a slight turn across it, since it is a
  // wall and not a bevel.
  const auto wallLit = colours::panelAlt.brighter(0.46f);
  const auto wallShaded = colours::panelAlt.brighter(0.30f);

  // Eight whatever the knob's size. More alias into a shimmer as they turn
  // on a small knob.
  constexpr int flutes = 8;

  // The notches are cut out of the collar, so what shows in them is the panel
  // and the cap's shadow rather than a guessed dark tone.
  const auto pitch = juce::MathConstants<float>::twoPi / (float)flutes;
  const auto notchW = pitch * 0.32f;
  const auto notchR = rimOut - (rimOut - rimIn) * kNotchDepth;

  juce::Path collar;

  for (int i = 0; i < flutes; ++i) {
    // The flutes turn with the knob while the lighting stays put. The first
    // notch is at the stripe's angle, so the stripe runs into a gap.
    const auto fluteStart = angle + (float)i * pitch + notchW * 0.5f;
    const auto fluteEnd = angle + (float)(i + 1) * pitch - notchW * 0.5f;

    collar.addCentredArc(centre.x, centre.y, rimOut, rimOut, 0.0f, fluteStart,
                         fluteEnd, i == 0);
    collar.addCentredArc(centre.x, centre.y, notchR, notchR, 0.0f, fluteEnd,
                         fluteEnd + notchW, false);
  }

  collar.closeSubPath();

  // The collar, lit from the top left.
  g.setGradientFill(juce::ColourGradient(
      wallLit.withMultipliedAlpha(dim), centre.x - rimOut * 0.7f,
      centre.y - rimOut * 0.7f, wallShaded.withMultipliedAlpha(dim),
      centre.x + rimOut * 0.7f, centre.y + rimOut * 0.7f, false));
  g.fillPath(collar);

  // A lit edge stroked along the collar's outline, notches and all, fading
  // towards the lower right. On a dark knob this is what shows the notches,
  // so it fades to a little rather than to nothing.
  g.setGradientFill(juce::ColourGradient(
      juce::Colours::white.withAlpha(0.16f * dim), centre.x - rimOut * 0.80f,
      centre.y - rimOut * 0.80f, juce::Colours::white.withAlpha(0.04f * dim),
      centre.x + rimOut * 0.80f, centre.y + rimOut * 0.80f, false));
  g.strokePath(collar, juce::PathStrokeType(juce::jmax(0.7f, radius * 0.045f)));

  // ---- the face -------------------------------------------------------------
  // Most of the knob, inside the collar.
  const juce::Rectangle<float> face =
      body.withSizeKeepingCentre(rimIn * 2.0f, rimIn * 2.0f);

  // A little lighter than the collar, with a shallow top-to-bottom wash.
  g.setGradientFill(juce::ColourGradient(
      colours::panelAlt.brighter(0.13f), centre.x, face.getY(),
      colours::panelAlt.darker(0.04f), centre.x, face.getBottom(), false));
  g.fillEllipse(face);

  // Nothing is drawn between the cap and the collar, which are one piece.

  // ---- the stripe -----------------------------------------------------------
  // One wide mark from the middle of the face out across the collar, in the
  // control's own colour. A filled rectangle, so both ends are square.
  const auto weight = juce::jmax(2.0f, radius * 0.15f);

  const auto mark = [&](float from, float to, juce::Colour colour) {
    juce::Path bar;
    bar.addRectangle(-weight * 0.5f, -to, weight, to - from);
    bar.applyTransform(
        juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

    g.setColour(colour);
    g.fillPath(bar);
  };

  if (blank)
    return;

  const auto head = fill.brighter(0.25f).withMultipliedAlpha(dim);

  mark(0.0f, rimIn, head);

  // The part crossing the wall runs to its outer edge and takes the wall's
  // light: bright where it points into the light and darker where it points
  // away.
  const auto into =
      0.5f - 0.5f * (std::cos(angle) * 0.72f - std::sin(angle) * 0.69f);

  mark(rimIn, rimOut, head.interpolatedWith(head.darker(0.38f), into));
}

void OvertoniumLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
    juce::Slider &slider) {
  const bool metered =
      (bool)slider.getProperties().getWithDefault("meteredGroove", false);

  // The macro panel's amount: segments lit from the middle out, since the
  // amount is signed.
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

    // Lit, and the same colour held low so an amount of nothing still shows
    // which macro the row belongs to.
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

    // The same glass cap the master fader wears.
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

  // The master fader: a meter under the whole control and a glass cap over it
  // saying where the level is set. No groove and no ticks.
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

    // The lip runs down the cap.
    g.setColour(juce::Colours::white.withAlpha(0.26f * dim));
    g.fillRect(cap.getX() + 1.5f, cap.getY() + 2.5f, 1.0f,
               cap.getHeight() - 5.0f);

    // Waiting for a controller. Around the cap only, so it does not enclose the
    // meter.
    if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
      g.setColour(colours::learning);
      g.drawRoundedRectangle(cap.expanded(1.5f), 2.5f, 1.6f);
    }

    return;
  }

  if (style != juce::Slider::LinearVertical) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);

    // The waiting marker, for the plain faders JUCE draws.
    if ((bool)slider.getProperties().getWithDefault("learnArmed", false)) {
      g.setColour(colours::learning);
      g.drawRoundedRectangle(
          juce::Rectangle<int>(x, y, width, height).toFloat().reduced(0.5f),
          3.0f, 1.4f);
    }

    return;
  }

  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

  // A fader on a channel nobody can hear is dimmed, the same as a knob.
  const bool live =
      !(bool)slider.getProperties().getWithDefault("unlit", false);
  const auto dim = slider.isEnabled() && live ? 1.0f : 0.4f;

  // When a meter sits behind the fader it owns the groove, so no track fill is
  // drawn and the cap alone shows the setting.
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

  // Armed by LINK: the outline of the track lights, as the halo does on a knob.
  const auto glow =
      (float)(double)slider.getProperties().getWithDefault("linkGlow", 0.0);

  if (glow > 0.0f) {
    // The channel's own colour for LINK, the accent for drawing, which matches
    // the switch that armed it.
    const auto lit =
        (bool)slider.getProperties().getWithDefault("glowAccent", false)
            ? colours::accent
            : slider.findColour(juce::Slider::trackColourId);

    g.setColour(lit.withAlpha(0.10f * glow * dim));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(lit.withAlpha(0.55f * glow * dim));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.2f);
  }

  // Waiting for a controller.
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
  // the macro's result. Laid out with the meter's own arithmetic so the two
  // line up. Only drawn when a macro has the fader.
  if (driven && bounds.getWidth() >= 22.0f) {
    // The slider's own rectangle, not the one this was handed, which JUCE
    // insets by the thumb's radius. The meter uses the whole component.
    const auto whole = slider.getLocalBounds().toFloat();

    const auto trackW = juce::jmax(6.0f, whole.getWidth() * 0.62f);
    const auto track = whole.withSizeKeepingCentre(trackW, whole.getHeight());

    const int count = meterSegments((int)whole.getHeight());
    const auto step = track.getHeight() / (float)count;
    const auto gap = juce::jlimit(1.0f, 3.0f, step * 0.18f);

    // What is left either side of the track, less a pixel of air against the
    // track and another against the edge of the strip.
    const auto lane = juce::jmax(1.5f, (track.getX() - whole.getX()) - 2.0f);

    // None lit on a channel nobody can hear, the same as a knob's ring.
    const auto onNow =
        live ? juce::jlimit(0, count, juce::roundToInt(reached * (float)count))
             : 0;

    // Unlit in the macro's own colour held down low.
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

  // Glass cap: a translucent window on the meter, edged and lit along the
  // top.
  const auto capH = juce::jmax(6.0f, bounds.getWidth() * 0.30f);
  const juce::Rectangle<float> cap(bounds.getX() + 0.5f, fillTop - capH * 0.5f,
                                   bounds.getWidth() - 1.0f, capH);

  g.setColour(juce::Colours::black.withAlpha(0.34f * dim));
  g.fillRoundedRectangle(cap.translated(0.5f, 1.5f), 2.0f);

  // Light enough that the lit segments behind it stay legible. Tinted in the
  // macro's colour when a macro has this fader.
  g.setColour(driven ? tint.withMultipliedAlpha(0.3f * dim)
                     : juce::Colours::white.withAlpha(0.13f * dim));
  g.fillRoundedRectangle(cap, 2.0f);

  // The cap's own edge, white whatever is driving it.
  g.setColour(juce::Colours::white.withAlpha(0.46f * dim));
  g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

  // The lip, off centre so it does not divide the cap in half.
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
  // A whole number of pixels, so the cap's edges land on the grid and get no
  // row of partial coverage round them.
  const auto wall = (float)juce::jmax(
      2,
      juce::roundToInt(juce::jlimit(2.0f, 4.0f, bounds.getHeight() * 0.14f)));

  auto cap = snapToPixels(bounds.reduced(wall));

  // Half a wall on the joined side, the neighbour giving the other half. Not
  // snapped, so the two halves add up to one wall.
  if (gang == LampGang::Left)
    cap.setRight(bounds.getRight() - wall * 0.5f);
  else if (gang == LampGang::Right)
    cap.setLeft(bounds.getX() + wall * 0.5f);

  return cap;
}

juce::Colour OvertoniumLookAndFeel::lampFace(juce::Colour lamp, bool on) {
  if (!on)
    // Unlit grey. It has to stay darker than every lit lamp, the reds being
    // the darkest.
    return juce::Colour(0xff818079);

  // The lamp seen through the plastic: saturation raised, brightness raised
  // and a little white mixed in.
  return lamp.withMultipliedSaturation(1.25f)
      .withBrightness(juce::jmax(lamp.getBrightness(), 0.92f))
      .interpolatedWith(juce::Colours::white, 0.14f);
}

juce::ColourGradient
OvertoniumLookAndFeel::legendInk(juce::Colour ink, juce::Rectangle<float> cap,
                                 bool on) {
  // How much of the lamp a printed legend lets past, at the middle and at
  // the ends.
  const auto middle = on ? 0.74f : 0.94f;
  const auto ends = on ? 0.92f : 1.0f;

  juce::ColourGradient ramp(ink.withAlpha(ends), cap.getX(), cap.getCentreY(),
                            ink.withAlpha(ends), cap.getRight(),
                            cap.getCentreY(), false);

  ramp.addColour(0.5, ink.withAlpha(middle));
  return ramp;
}

juce::Rectangle<float>
OvertoniumLookAndFeel::snapToPixels(juce::Rectangle<float> r) {
  const auto l = (float)juce::roundToInt(r.getX());
  const auto t = (float)juce::roundToInt(r.getY());

  return {l, t, (float)juce::roundToInt(r.getRight()) - l,
          (float)juce::roundToInt(r.getBottom()) - t};
}

juce::Colour OvertoniumLookAndFeel::lampLegend() {
  // Not quite black, which against a lit gold reads as a hole in the cap.
  return juce::Colour(0xff161718);
}

void OvertoniumLookAndFeel::drawLampCap(juce::Graphics &g,
                                        juce::Rectangle<float> bounds,
                                        juce::Colour lamp, bool on,
                                        bool highlighted, bool down,
                                        LampGang gang) {
  const auto cap = lampCapBounds(bounds, gang);
  const auto wall = cap.getY() - bounds.getY();
  // Square, with only a little taken off the corners.
  const auto outerCorner = juce::jmin(1.7f, bounds.getHeight() * 0.11f);

  // The moulding of the whole gang. Each half draws it in full and its own
  // clip shows only its half.
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
  // A step darker than the panel it sits in.
  g.setColour(juce::Colour(0xff1a1f26));
  g.fillRoundedRectangle(well, outerCorner);

  // The outside edge of the moulding, lit at the top left and shadowed at the
  // bottom right.
  g.setGradientFill(
      juce::ColourGradient(juce::Colours::white.withAlpha(0.12f), well.getX(),
                           well.getY(), juce::Colours::black.withAlpha(0.40f),
                           well.getRight(), well.getBottom(), false));
  g.drawRoundedRectangle(well.reduced(0.5f), outerCorner, 1.0f);

  // ---- the bevel -----------------------------------------------------------
  // Four facets mitred at the corners, sloping in to the opening. Top and left
  // are lit, bottom and right in shadow. The opening is the same rectangle as
  // the cap, so the facets meet it exactly.
  const auto opening = snapToPixels(well.reduced(wall));

  const juce::Point<float> oTL(well.getX(), well.getY());
  const juce::Point<float> oTR(well.getRight(), well.getY());
  const juce::Point<float> oBR(well.getRight(), well.getBottom());
  const juce::Point<float> oBL(well.getX(), well.getBottom());
  const juce::Point<float> iTL(opening.getX(), opening.getY());
  const juce::Point<float> iTR(opening.getRight(), opening.getY());
  const juce::Point<float> iBR(opening.getRight(), opening.getBottom());
  const juce::Point<float> iBL(opening.getX(), opening.getBottom());

  const auto facet = [&g](juce::Point<float> a, juce::Point<float> b,
                          juce::Point<float> c, juce::Point<float> d,
                          juce::Colour colour) {
    juce::Path quad;
    quad.startNewSubPath(a);
    quad.lineTo(b);
    quad.lineTo(c);
    quad.lineTo(d);
    quad.closeSubPath();

    g.setColour(colour);
    g.fillPath(quad);
  };

  // Top, left, right and bottom facets, in that order.
  facet(oTL, oTR, iTR, iTL, juce::Colours::white.withAlpha(0.055f));
  facet(oBL, oTL, iTL, iBL, juce::Colours::white.withAlpha(0.012f));
  facet(oTR, oBR, iBR, iTR, juce::Colours::black.withAlpha(0.21f));
  facet(oBR, oBL, iBL, iBR, juce::Colours::black.withAlpha(0.34f));

  // The dark seam between the frame's inner edge and the cap.
  g.setColour(juce::Colours::black.withAlpha(0.55f));
  g.drawRect(opening.expanded(0.5f), 1.0f);

  const auto corner = juce::jmax(1.0f, outerCorner - wall * 0.5f);

  // ---- what the lamp is doing ---------------------------------------------
  // The plastic at the middle of the cap. Every shade below is this darkened.
  auto face = lampFace(lamp, on);

  if (down)
    face = face.brighter(0.12f);
  else if (highlighted)
    face = face.brighter(on ? 0.06f : 0.18f);

  // ---- the cast ------------------------------------------------------------
  // A lit cap puts an even trace of its colour over the whole moulding. A
  // halo round the cap would make it read as pressed in.
  if (on) {
    g.setColour(lamp.withAlpha(0.07f));
    g.fillRoundedRectangle(well, outerCorner);
  }

  // ---- the cap ------------------------------------------------------------
  // A shallow wash from top to bottom.
  {
    juce::ColourGradient down_(face.darker(0.10f), cap.getCentreX(), cap.getY(),
                               face.darker(0.16f), cap.getCentreX(),
                               cap.getBottom(), false);

    // Brightest a touch above the middle, where the lamp is.
    down_.addColour(0.46, face.brighter(on ? 0.085f : 0.065f));

    g.setGradientFill(down_);
  }

  g.fillRoundedRectangle(cap, corner);

  // The light falls off towards the ends as well. Horizontal rather than
  // radial, since a round hotspot would be a disc on the wide caps.
  {
    const auto edge = juce::Colours::black.withAlpha(on ? 0.13f : 0.07f);

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
    // Pressed: the wall above throws a shadow across the top of the cap.
    const auto deep = juce::jmax(2.0f, cap.getHeight() * 0.3f);

    g.setGradientFill(juce::ColourGradient(
        juce::Colours::black.withAlpha(0.34f), cap.getCentreX(), cap.getY(),
        juce::Colours::transparentBlack, cap.getCentreX(), cap.getY() + deep,
        false));
    g.fillRoundedRectangle(cap, corner);
  }

  // ---- the cap's edge -----------------------------------------------------
  // Lighter on a lit cap.
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

  // The half pixel goes back on the joined side, where there is no stroked
  // edge to land on the pixel.
  if (gang == LampGang::Left)
    bounds.setRight(bounds.getRight() + 0.5f);
  else if (gang == LampGang::Right)
    bounds.setLeft(bounds.getX() - 0.5f);

  // The lamp colour is the button's lit text colour, or the accent if it
  // names none.
  auto lamp = button.findColour(juce::TextButton::textColourOnId);

  if (lamp.isTransparent())
    lamp = colours::accent;

  // backgroundColour still decides for anything that asked for a particular
  // face, such as the macro panel's colour swatches.
  if (!backgroundColour.isTransparent() &&
      backgroundColour != colours::panelAlt)
    lamp = backgroundColour;

  drawLampCap(g, bounds, lamp, on, shouldDrawButtonAsHighlighted,
              shouldDrawButtonAsDown, gang);

  // Waiting for a controller, drawn on the cap's own edge, since a mute
  // button is too small for a second ring outside it.
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

  // The same ink whatever the lamp is doing. See lampLegend and legendInk.
  const auto cap =
      lampCapBounds(button.getLocalBounds().toFloat(), lampGangOf(button));

  g.setGradientFill(legendInk(lampLegend(), cap, button.getToggleState()));

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
/// A title bar button: a plain shape, dim until the pointer is on it.
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

    // Lit red under the pointer.
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

  // Lit from above and grained, like the panel.
  g.setGradientFill(juce::ColourGradient(colours::panel.brighter(0.10f), 0.0f,
                                         0.0f, colours::panel.darker(0.25f),
                                         0.0f, (float)h, false));
  g.fillRect(bounds);

  paintGrain(g, bounds);

  g.setColour(colours::outline);
  g.fillRect(0, h - 1, w, 1);

  // The name, dimmed when the window is not active.
  g.setFont(makeFont((float)h * 0.54f, true));
  g.setColour(window.isActiveWindow() ? colours::text : colours::textDim);

  // Centred on the whole window, then held inside the title space. The
  // standalone's Options button is not counted in that space, so centring
  // in it alone would sit left of the middle.
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
