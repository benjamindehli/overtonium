#include "LookAndFeel.h"

#include <array>

#include <BinaryData.h>

namespace ovt::ui {

juce::Font makeFont(float height, bool bold) {
  auto options = juce::FontOptions(height);

  if (bold)
    options = options.withStyle("Bold");

  return juce::Font(options);
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

  // Lit edge on the left, shadowed on the right.
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
constexpr float kHoverWash = 0.035f;
constexpr float kHoverEdge = 0.13f;
} // namespace

void paintRowHighlight(juce::Graphics &g, juce::Rectangle<int> row) {
  const auto r = row.toFloat();

  g.setColour(colours::accent.withAlpha(kHoverWash));
  g.fillRect(r);

  g.setColour(colours::accent.withAlpha(kHoverEdge));
  g.fillRect(r.getX(), r.getY(), r.getWidth(), 1.0f);
  g.fillRect(r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
}

void paintColumnHighlight(juce::Graphics &g, juce::Rectangle<int> strip) {
  const auto r = strip.toFloat();

  g.setColour(colours::accent.withAlpha(kHoverWash));
  g.fillRect(r);

  g.setColour(colours::accent.withAlpha(kHoverEdge));
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
    case LinkCurve::TiltUp:
      heights = {0.25f, 0.42f, 0.6f, 0.78f, 0.96f};
      break;
    case LinkCurve::TiltDown:
      heights = {0.96f, 0.78f, 0.6f, 0.42f, 0.25f};
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

  const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);

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

  // ---- the tick ring --------------------------------------------------------
  // Discrete ticks rather than a continuous arc. It reads as a measurement
  // instrument, which is what this thing is, and it echoes the 32 discrete
  // partials the whole synth is built from.
  const int ticks = juce::jlimit(9, 25, juce::roundToInt(radius * 1.15f));
  const auto lo = juce::jmin(anchor, angle);
  const auto hi = juce::jmax(anchor, angle);

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

    g.setColour(lit ? fill.withMultipliedAlpha(dim)
                    : unlit.withMultipliedAlpha(dim));
    g.drawLine({centre.x + inner * sinA, centre.y - inner * cosA,
                centre.x + outer * sinA, centre.y - outer * cosA},
               lit ? juce::jmax(1.6f, radius * 0.11f)
                   : juce::jmax(1.0f, radius * 0.07f));
  }

  // ---- the cap --------------------------------------------------------------
  // A machined disc, not a dome. A strong radial gradient with a specular bloom
  // reads as a ball bearing, which is not what the top of a control looks like.
  // The face is nearly flat and the roundness lives entirely in the rim, which
  // catches the light along its upper edge and falls into shadow underneath.
  const auto bodyR = radius * 0.60f;
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

  g.setGradientFill(juce::ColourGradient(
      colours::panelAlt.brighter(0.16f), centre.x, body.getY(),
      colours::panelAlt.darker(0.34f), centre.x, body.getBottom(), false));
  g.fillEllipse(body);

  // The rim is the only part that is meant to look curved.
  g.setGradientFill(juce::ColourGradient(
      juce::Colours::white.withAlpha(0.28f * dim), centre.x, body.getY(),
      juce::Colours::black.withAlpha(0.45f * dim), centre.x, body.getBottom(),
      false));
  g.drawEllipse(body.reduced(0.6f), 1.2f);

  // ---- the pointer ----------------------------------------------------------
  // In the control's own colour rather than white, so it belongs to the knob
  // and lines up with the lit ticks beyond the rim instead of sitting on the
  // cap like something stuck there. Seated in a dark groove so it reads as
  // inlaid into the face.
  const auto sinA = std::sin(angle);
  const auto cosA = std::cos(angle);
  const auto inner = bodyR * 0.26f;
  const auto outer = bodyR * 0.84f;
  const auto weight = juce::jmax(1.8f, radius * 0.12f);

  const auto x1 = centre.x + inner * sinA;
  const auto y1 = centre.y - inner * cosA;
  const auto x2 = centre.x + outer * sinA;
  const auto y2 = centre.y - outer * cosA;

  g.setColour(juce::Colours::black.withAlpha(0.55f * dim));
  g.drawLine(x1, y1 + 0.9f, x2, y2 + 0.9f, weight);

  g.setColour(fill.brighter(0.25f).withMultipliedAlpha(dim));
  g.drawLine(x1, y1, x2, y2, weight);
}

void OvertoniumLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
    juce::Slider &slider) {
  if (style != juce::Slider::LinearVertical) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);
    return;
  }

  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const auto dim = slider.isEnabled() ? 1.0f : 0.4f;

  // When a meter sits behind the fader it owns the groove, so the track fill
  // that would otherwise show the set level is dropped. The cap alone says
  // where the fader is, which leaves the whole track free to show output.
  const bool meteredGroove =
      (bool)slider.getProperties().getWithDefault("meteredGroove", false);

  const auto fillTop =
      juce::jlimit(bounds.getY(), bounds.getBottom(), sliderPos);

  if (!meteredGroove) {
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
    const auto lit = slider.findColour(juce::Slider::trackColourId);

    g.setColour(lit.withAlpha(0.10f * glow * dim));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(lit.withAlpha(0.55f * glow * dim));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.2f);
  }

  // Scale ticks either side of the track, in the same language as the tick
  // ring on the knobs. They give the meter something to be read against.
  if (bounds.getWidth() >= 22.0f) {
    constexpr int steps = 9;
    const auto inset = 1.0f;
    const auto len = 3.0f;

    for (int i = 0; i < steps; ++i) {
      const auto t = (float)i / (float)(steps - 1);
      const auto ty = bounds.getY() + t * bounds.getHeight();
      const bool major = (i % 4) == 0;

      g.setColour(colours::textDim.withAlpha((major ? 0.45f : 0.22f) * dim));
      g.fillRect(bounds.getX() + inset, ty - 0.5f, major ? len + 1.0f : len,
                 1.0f);
      g.fillRect(bounds.getRight() - inset - (major ? len + 1.0f : len),
                 ty - 0.5f, major ? len + 1.0f : len, 1.0f);
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
  g.setColour(juce::Colours::white.withAlpha(0.13f * dim));
  g.fillRoundedRectangle(cap, 2.0f);

  g.setColour(juce::Colours::white.withAlpha(0.46f * dim));
  g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

  // The lip catches the light off centre, so it says glass rather than
  // dividing the cap in half.
  g.setColour(juce::Colours::white.withAlpha(0.26f * dim));
  g.fillRect(cap.getX() + 2.5f, cap.getY() + 1.5f, cap.getWidth() - 5.0f,
             1.0f);
}

void OvertoniumLookAndFeel::drawButtonBackground(
    juce::Graphics &g, juce::Button &button,
    const juce::Colour &backgroundColour, bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown) {
  const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
  const auto corner = juce::jmin(4.0f, bounds.getHeight() * 0.3f);

  const bool on = button.getToggleState();
  auto fill = on ? backgroundColour : colours::panelAlt;

  if (shouldDrawButtonAsDown)
    fill = fill.brighter(0.15f);
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.brighter(0.08f);

  // Raised buttons cast, engaged ones sit down into the panel and do not.
  if (!on && !shouldDrawButtonAsDown) {
    g.setColour(juce::Colours::black.withAlpha(0.30f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), corner);
  }

  // Lit from above when on and merely raised when off, so the state reads at a
  // glance across 33 channels rather than needing a colour comparison.
  g.setGradientFill(juce::ColourGradient(
      fill.brighter(on ? 0.30f : 0.12f), bounds.getCentreX(), bounds.getY(),
      fill.darker(on ? 0.10f : 0.05f), bounds.getCentreX(), bounds.getBottom(),
      false));
  g.fillRoundedRectangle(bounds, corner);

  // A bright lip along the top edge, matching the glass on the indicators.
  g.setColour(juce::Colours::white.withAlpha(on ? 0.35f : 0.10f));
  g.fillRoundedRectangle(bounds.getX() + 1.5f, bounds.getY() + 1.0f,
                         bounds.getWidth() - 3.0f, 1.0f, 0.5f);

  g.setColour(on ? fill.brighter(0.45f) : colours::outline);
  g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
}

void OvertoniumLookAndFeel::drawButtonText(juce::Graphics &g,
                                           juce::TextButton &button, bool,
                                           bool) {
  const auto h = (float)button.getHeight();
  g.setFont(makeFont(juce::jlimit(8.0f, 13.0f, h * 0.58f), true));

  g.setColour(button.findColour(button.getToggleState()
                                    ? juce::TextButton::textColourOnId
                                    : juce::TextButton::textColourOffId));

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

} // namespace ovt::ui
