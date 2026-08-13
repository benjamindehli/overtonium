#include "LookAndFeel.h"

namespace ovt::ui {

juce::Font makeFont(float height, bool bold) {
  auto options = juce::FontOptions(height);

  if (bold)
    options = options.withStyle("Bold");

  return juce::Font(options);
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
      juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
  const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

  if (radius < 4.0f)
    return;

  const auto centre = bounds.getCentre();
  const auto lineW = juce::jmax(2.0f, radius * 0.24f);
  const auto arcR = radius - lineW * 0.5f;
  const auto angle =
      rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  const juce::PathStrokeType stroke(lineW, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);

  juce::Path track;
  track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle,
                      rotaryEndAngle, true);
  g.setColour(colours::groove.brighter(0.16f));
  g.strokePath(track, stroke);

  // Relative controls read as an offset either side of twelve o'clock rather
  // than as a level filling up from the left.
  const bool bipolar =
      (bool)slider.getProperties().getWithDefault("bipolar", false);
  const auto arcFrom =
      bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
              : rotaryStartAngle;

  if (std::abs(angle - arcFrom) > 0.001f) {
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f,
                        juce::jmin(arcFrom, angle), juce::jmax(arcFrom, angle),
                        true);

    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                    .withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.4f));
    g.strokePath(value, stroke);
  }

  // Pointer, drawn from the centre outwards so tiny knobs stay readable. It is
  // glass: a translucent body with bright edges, so the value arc reads through
  // it rather than being masked by it.
  juce::Path pointer;
  const auto pointerLength = arcR * 0.8f;
  const auto pointerWidth = juce::jmax(2.0f, lineW * 0.5f);
  pointer.addRoundedRectangle(-pointerWidth * 0.5f, -pointerLength,
                              pointerWidth, pointerLength * 0.72f,
                              pointerWidth * 0.5f);
  pointer.applyTransform(
      juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

  const auto dim = slider.isEnabled() ? 1.0f : 0.4f;

  g.setColour(juce::Colours::white.withAlpha(0.30f * dim));
  g.fillPath(pointer);
  g.setColour(juce::Colours::white.withAlpha(0.85f * dim));
  g.strokePath(pointer, juce::PathStrokeType(0.9f));
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

  // Glass cap: translucent body, bright edges and a bright centre line for
  // reading the exact position. The meter behind shows through it.
  const auto capH = juce::jmax(6.0f, bounds.getWidth() * 0.30f);
  const juce::Rectangle<float> cap(bounds.getX() + 0.5f, fillTop - capH * 0.5f,
                                   bounds.getWidth() - 1.0f, capH);

  g.setColour(juce::Colours::white.withAlpha(0.22f * dim));
  g.fillRoundedRectangle(cap, 2.0f);

  g.setColour(juce::Colours::white.withAlpha(0.80f * dim));
  g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);

  g.setColour(juce::Colours::white.withAlpha(0.55f * dim));
  g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 4.0f,
             1.0f);
}

void OvertoniumLookAndFeel::drawButtonBackground(
    juce::Graphics &g, juce::Button &button,
    const juce::Colour &backgroundColour, bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown) {
  const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
  const auto corner = juce::jmin(4.0f, bounds.getHeight() * 0.3f);

  auto fill = button.getToggleState() ? backgroundColour : colours::panelAlt;

  if (shouldDrawButtonAsDown)
    fill = fill.brighter(0.15f);
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.brighter(0.08f);

  g.setColour(fill);
  g.fillRoundedRectangle(bounds, corner);

  g.setColour(button.getToggleState() ? fill.brighter(0.25f)
                                      : colours::outline);
  g.drawRoundedRectangle(bounds, corner, 1.0f);
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

  g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
  g.fillRoundedRectangle(bounds, 4.0f);

  g.setColour(box.findColour(juce::ComboBox::outlineColourId));
  g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

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
