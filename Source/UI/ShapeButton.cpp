#include "ShapeButton.h"

#include "Theme.h"

namespace ovt::ui {

namespace {
/// How many points one turn is drawn with.
///
/// Enough that a sine reads as a curve at 30 px across, and the square shapes
/// are drawn from their own corners rather than sampled, so nothing here has
/// to resolve a vertical edge.
constexpr int kCurvePoints = 48;

/// The random shapes have no single picture, since the whole point of them is
/// that the next turn is not this one. A fixed sequence is drawn instead: not
/// what will be heard, but the right shape of thing, and the same every time
/// so the glyph does not flicker as the mixer repaints.
constexpr float kRandomPoints[] = {-0.6f, 0.35f, -0.15f, 0.8f, -0.45f};

/// Well past any shape's index, so the two cannot collide as shapes are added.
constexpr int kEveryChannel = 1000;
} // namespace

void ShapeButton::drawShape(juce::Graphics &g, juce::Rectangle<float> area,
                            LfoShape shape, juce::Colour colour) {
  const auto midY = area.getCentreY();
  const auto amp = area.getHeight() * 0.5f;
  const auto x0 = area.getX();
  const auto w = area.getWidth();

  const auto at = [&](float t, float v) {
    return juce::Point<float>(x0 + t * w, midY - v * amp);
  };

  juce::Path path;

  switch (shape) {
  case LfoShape::BipolarSquare:
  case LfoShape::UnipolarSquare: {
    const float low = shape == LfoShape::BipolarSquare ? -1.0f : 0.0f;

    path.startNewSubPath(at(0.0f, 1.0f));
    path.lineTo(at(0.5f, 1.0f));
    path.lineTo(at(0.5f, low));
    path.lineTo(at(1.0f, low));
    break;
  }

  case LfoShape::Sawtooth:
    path.startNewSubPath(at(0.0f, -1.0f));
    path.lineTo(at(1.0f, 1.0f));
    break;

  case LfoShape::ReverseSawtooth:
    path.startNewSubPath(at(0.0f, 1.0f));
    path.lineTo(at(1.0f, -1.0f));
    break;

  case LfoShape::Triangle:
    path.startNewSubPath(at(0.0f, 0.0f));
    path.lineTo(at(0.25f, 1.0f));
    path.lineTo(at(0.75f, -1.0f));
    path.lineTo(at(1.0f, 0.0f));
    break;

  case LfoShape::SampleAndHold: {
    const int steps = (int)std::size(kRandomPoints);

    for (int i = 0; i < steps; ++i) {
      const auto a = (float)i / (float)steps;
      const auto b = (float)(i + 1) / (float)steps;

      if (i == 0)
        path.startNewSubPath(at(a, kRandomPoints[i]));
      else
        path.lineTo(at(a, kRandomPoints[i]));

      path.lineTo(at(b, kRandomPoints[i]));
    }
    break;
  }

  case LfoShape::Random: {
    // The same points the stepped one uses, joined rather than held, so the
    // pair read as two ways of doing one thing.
    const int steps = (int)std::size(kRandomPoints);

    path.startNewSubPath(at(0.0f, kRandomPoints[0]));

    for (int i = 1; i < steps; ++i) {
      const auto t = (float)i / (float)(steps - 1);
      const auto prev = (float)(i - 1) / (float)(steps - 1);

      path.cubicTo(at(prev + 0.5f / (float)(steps - 1), kRandomPoints[i - 1]),
                   at(t - 0.5f / (float)(steps - 1), kRandomPoints[i]),
                   at(t, kRandomPoints[i]));
    }
    break;
  }

  case LfoShape::Sine:
  case LfoShape::NumShapes:
  default:
    for (int i = 0; i <= kCurvePoints; ++i) {
      const auto t = (float)i / (float)kCurvePoints;
      const auto v = std::sin(t * 6.283185307179586f);

      if (i == 0)
        path.startNewSubPath(at(t, v));
      else
        path.lineTo(at(t, v));
    }
    break;
  }

  g.setColour(colour);
  g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
}

ShapeButton::ShapeButton(juce::AudioProcessorValueTreeState &state,
                         const juce::String &parameterId, const char *suffix,
                         juce::Span<const LfoShape> shapes,
                         juce::StringArray shapeNames)
    : apvts(state), id(parameterId), sharedSuffix(suffix), offered(shapes),
      names(std::move(shapeNames)) {
  setWantsKeyboardFocus(false);

  if (auto *param = apvts.getParameter(id))
    attachment = std::make_unique<juce::ParameterAttachment>(
        *param, [this](float) { repaint(); }, apvts.undoManager);
}

int ShapeButton::selectedIndex() const {
  auto *param = apvts.getParameter(id);
  if (param == nullptr)
    return 0;

  return juce::jlimit(0, (int)offered.size() - 1,
                      juce::roundToInt(param->convertFrom0to1(
                          param->getValue())));
}

LfoShape ShapeButton::selectedShape() const {
  return offered[(size_t)selectedIndex()];
}

juce::String ShapeButton::currentName() const {
  return names[selectedIndex()];
}

void ShapeButton::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat().reduced(3.0f, 2.0f);

  g.setColour(colours::groove.withAlpha(hovered ? 0.9f : 0.55f));
  g.fillRoundedRectangle(area, 2.0f);

  drawShape(g, area.reduced(3.0f, 2.5f), selectedShape(),
            hovered ? colours::text : colours::textDim);
}

void ShapeButton::mouseEnter(const juce::MouseEvent &) {
  hovered = true;
  repaint();
}

void ShapeButton::mouseExit(const juce::MouseEvent &) {
  hovered = false;
  repaint();
}

void ShapeButton::applyToEveryChannel(int index) {
  const auto set = [this, index](const juce::String &target) {
    auto *param = apvts.getParameter(target);

    // The noise channel has a tremolo but no pitch modulator, so one of the
    // two suffixes finds nothing there. Asking and getting nothing is the
    // check.
    if (param == nullptr)
      return;

    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float)index));
    param->endChangeGesture();
  };

  for (int i = 0; i < kNumHarmonics; ++i)
    set(params::oscParamId(sharedSuffix, i));

  set(params::noiseParamId(sharedSuffix));
}

void ShapeButton::mouseDown(const juce::MouseEvent &e) {
  // Right-click belongs to LINK, the same as every other control on a strip,
  // so it is left alone here and reaches the strip underneath.
  if (e.mods.isPopupMenu())
    return;

  if (apvts.getParameter(id) == nullptr)
    return;

  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  const auto current = selectedIndex();

  for (int i = 0; i < names.size(); ++i)
    m.addItem(i + 1, names[i], true, i == current);

  // Thirty-three channels is a lot of clicking otherwise, and this is the one
  // control on a strip LINK cannot reach.
  m.addSeparator();
  m.addItem(kEveryChannel, "Set every channel to this");

  m.showMenuAsync(
      juce::PopupMenu::Options()
          .withTargetComponent(this)
          .withStandardItemHeight(22),
      [this](int result) {
        if (result <= 0)
          return;

        if (result == kEveryChannel)
          return applyToEveryChannel(selectedIndex());

        if (attachment != nullptr)
          attachment->setValueAsCompleteGesture((float)(result - 1));
      });
}

} // namespace ovt::ui
