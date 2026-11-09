#include "LookAndFeel.h"

#include <array>
#include <cmath>

#include <BinaryData.h>

namespace ovt::ui {

juce::Image logoWordmark() {
  return juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                         BinaryData::logo_pngSize);
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

} // namespace ovt::ui
