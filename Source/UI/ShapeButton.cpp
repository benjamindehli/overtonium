#include "ShapeButton.h"
#include "SettingsMenus.h"

#include <cstring>
#include <iterator>
#include <vector>

#include "LookAndFeel.h"
#include "Theme.h"

namespace ovt::ui {

namespace {
/// How many points one turn is drawn with. The square shapes are drawn from
/// their corners rather than sampled.
constexpr int kCurvePoints = 48;

/// A fixed sequence drawn for the random shapes, so the glyph stays the same
/// on every repaint.
constexpr float kRandomPoints[] = {-0.6f, 0.35f, -0.15f, 0.8f, -0.45f};

/// Menu ids well past any shape's index, so they cannot clash.
constexpr int kEveryChannel = 1000;
constexpr int kInPhase = 1001;

/// The first id of the "Set every channel to" submenu, one per shape after it.
constexpr int kEveryChannelTo = 2000;

/// The first id of the "Partial" submenu, one per partial after it.
constexpr int kPartialFrom = 3000;

/// Following its own partial, and setting every channel to follow its own.
constexpr int kFeedback = 1002;
constexpr int kEveryChannelFeedback = 1003;
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
    // The same points the stepped one uses, joined rather than held.
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

  strokeGlowing(g, path, colour, 1.3f);
}

ShapeButton::ShapeButton(juce::AudioProcessorValueTreeState &state,
                         const juce::String &parameterId, const char *suffix,
                         juce::Span<const LfoShape> shapes,
                         juce::StringArray shapeNames, int ownPartial,
                         bool offersFeedback)
    : apvts(state), id(parameterId), sharedSuffix(suffix),
      partialOf(ownPartial), feedbackOffered(offersFeedback && ownPartial >= 0),
      offered(shapes), names(std::move(shapeNames)) {
  setWantsKeyboardFocus(false);

  if (auto *param = apvts.getParameter(id))
    attachment =
        std::make_unique<juce::ParameterAttachment>(*param, [this](float) {
          repaint();

          if (onChange != nullptr)
            onChange();
        });
}

int ShapeButton::selectedIndex() const {
  return juce::jlimit(0, (int)offered.size() - 1,
                      settings::choiceIndex(apvts, id));
}

LfoShape ShapeButton::selectedShape() const {
  return offered[(size_t)selectedIndex()];
}

int ShapeButton::selectedPartial() const {
  if (partialOf < 0)
    return -1;

  // The partials follow the shapes in the parameter's list.
  auto partial = settings::choiceIndex(apvts, id) - (int)offered.size();

  if (partial < 0 || partial >= kNumHarmonics)
    partial = -1;

  // Following itself is feedback where that is offered. Elsewhere it turns
  // the modulation off, which the menu cannot ask for, so it reads as the
  // shape underneath.
  return partial == partialOf && !feedbackOffered ? -1 : partial;
}

bool ShapeButton::feedsBack() const {
  return feedbackOffered && selectedPartial() == partialOf;
}

juce::String ShapeButton::currentName() const {
  if (feedsBack())
    return "Feedback";

  const auto partial = selectedPartial();

  return partial >= 0 ? "Partial " + juce::String(partial + 1)
                      : names[selectedIndex()];
}

void ShapeButton::paint(juce::Graphics &g) {
  LookKey key;
  key.add(selectedIndex())
      .add(selectedPartial())
      .add(hovered)
      .add(getProperties());

  looks.paint(g, *this, key,
              [this](juce::Graphics &into) { paintShape(into); });
}

void ShapeButton::paintShape(juce::Graphics &g) {
  const auto area = getLocalBounds().toFloat().reduced(3.0f, 2.0f);

  paintRecess(g, area, 2.0f);
  paintDisplayGround(g, area, 2.0f, hovered);

  // On a channel nobody can hear the screen is dark, like the readouts beside
  // it.
  if ((bool)getProperties().getWithDefault("unlit", false))
    return;

  const auto ink = colours::accent.withAlpha(hovered ? 1.0f : 0.88f);

  // Following a partial, which has no shape to draw: a times sign and the
  // partial's number, since its wave multiplies this one. Following its own,
  // FB for feedback.
  if (const auto partial = selectedPartial(); partial >= 0) {
    g.setColour(ink);
    g.setFont(makeFont(juce::jmin(11.0f, area.getHeight() * 0.8f), true));
    g.drawText(feedsBack() ? juce::String("FB")
                           : juce::String::fromUTF8("\xc3\x97") +
                                 juce::String(partial + 1),
               area, juce::Justification::centred, false);
    return;
  }

  // The same accent the tuning digits light in.
  drawShape(g, area.reduced(3.0f, 2.5f), selectedShape(), ink);
}

juce::PopupMenu ShapeButton::buildMenu() const {
  juce::PopupMenu m;

  const auto partial = selectedPartial();
  const auto feedback = feedsBack();
  const auto current = partial >= 0 ? -1 : selectedIndex();

  for (int i = 0; i < (int)offered.size(); ++i)
    m.addItem(i + 1, names[i], true, i == current);

  // Another partial's wave, on a channel that can follow one. Every partial
  // but this one.
  if (partialOf >= 0) {
    juce::PopupMenu partials;

    for (int j = 0; j < kNumHarmonics; ++j)
      if (j != partialOf)
        partials.addItem(kPartialFrom + j, "Partial " + juce::String(j + 1),
                         true, j == partial);

    m.addSubMenu("Partial", partials, true, nullptr, partial >= 0 && !feedback);
  }

  // Its own wave, where that is offered.
  if (feedbackOffered)
    m.addItem(kFeedback, "Feedback", true, feedback);

  // LINK cannot reach this control, so the menu sets every channel instead,
  // either to this channel's shape or to any shape without choosing it here
  // first. Not to another partial, which no channel can follow as itself,
  // but to feedback, which each channel takes from its own.
  m.addSeparator();
  m.addItem(kEveryChannel, "Set every channel to this",
            partial < 0 || feedback);

  juce::PopupMenu every;

  for (int i = 0; i < (int)offered.size(); ++i)
    every.addItem(kEveryChannelTo + i, names[i]);

  if (feedbackOffered)
    every.addItem(kEveryChannelFeedback, "Feedback");

  m.addSubMenu("Set every channel to", every);

  // Whether this modulator is one circuit the whole keyboard hears or one per
  // note.
  m.addSeparator();
  m.addItem(kInPhase, "Shared by all notes", true, inPhase());

  return m;
}

const char *ShapeButton::inPhaseId() const {
  // Compared by content, not by address, so a copy of the string still works.
  const auto pitch = std::strcmp(sharedSuffix, params::pmShapeSuffix) == 0;

  return pitch ? params::pmInPhaseId : params::amInPhaseId;
}

bool ShapeButton::inPhase() const {
  auto *param = apvts.getParameter(inPhaseId());

  return param != nullptr && param->getValue() > 0.5f;
}

void ShapeButton::setInPhase(bool shouldShare) {
  params::setAsOneStep(apvts.getParameter(inPhaseId()),
                       shouldShare ? 1.0f : 0.0f);
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
  std::vector<juce::RangedAudioParameter *> every;

  for (int i = 0; i < kNumHarmonics; ++i)
    every.push_back(apvts.getParameter(params::oscParamId(sharedSuffix, i)));

  // The noise channel has no pitch modulator, so it is left out there.
  every.push_back(apvts.getParameter(params::noiseParamId(sharedSuffix)));

  std::vector<params::Move> moves;

  for (auto *param : every)
    if (param != nullptr)
      moves.emplace_back(param, param->convertTo0to1((float)index));

  params::moveAsOneStep(moves);
}

void ShapeButton::applyFeedbackToEveryChannel() {
  std::vector<params::Move> moves;

  // The noise channel has no partial of its own to follow, so it is left out.
  for (int i = 0; i < kNumHarmonics; ++i)
    if (auto *param = apvts.getParameter(params::oscParamId(sharedSuffix, i)))
      moves.emplace_back(
          param, param->convertTo0to1((float)((int)offered.size() + i)));

  params::moveAsOneStep(moves);
}

void ShapeButton::mouseDown(const juce::MouseEvent &e) {
  // Right-click is left to reach the strip underneath, which opens its menu.
  if (e.mods.isPopupMenu())
    return;

  if (apvts.getParameter(id) == nullptr)
    return;

  auto m = buildMenu();
  m.setLookAndFeel(&getLookAndFeel());

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(this)
                      .withStandardItemHeight(22),
                  [this](int result) { chooseFromMenu(result); });
}

void ShapeButton::chooseFromMenu(int result) {
  if (result <= 0)
    return;

  if (result == kEveryChannel) {
    if (feedsBack())
      applyFeedbackToEveryChannel();
    else if (selectedPartial() < 0)
      applyToEveryChannel(selectedIndex());

    return;
  }

  if (result == kEveryChannelFeedback) {
    if (feedbackOffered)
      applyFeedbackToEveryChannel();

    return;
  }

  if (result == kFeedback) {
    if (feedbackOffered && attachment != nullptr)
      attachment->setValueAsCompleteGesture(
          (float)((int)offered.size() + partialOf));

    return;
  }

  if (result == kInPhase)
    return setInPhase(!inPhase());

  if (result >= kEveryChannelTo &&
      result < kEveryChannelTo + (int)offered.size())
    return applyToEveryChannel(result - kEveryChannelTo);

  if (result >= kPartialFrom && result < kPartialFrom + kNumHarmonics) {
    if (partialOf >= 0 && attachment != nullptr)
      attachment->setValueAsCompleteGesture(
          (float)((int)offered.size() + result - kPartialFrom));

    return;
  }

  if (result - 1 >= (int)offered.size())
    return;

  if (attachment != nullptr)
    attachment->setValueAsCompleteGesture((float)(result - 1));
}

} // namespace ovt::ui
