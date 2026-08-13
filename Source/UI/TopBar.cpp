#include "TopBar.h"

#include "../PluginParameters.h"
#include "../Presets.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
constexpr float kZoomChoices[] = {0.75f, 1.0f, 1.25f, 1.5f};
}

// =============================================================================

RelativeKnob::RelativeKnob() {
  setSliderStyle(juce::Slider::RotaryVerticalDrag);
  setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

  // Full travel in either direction covers the whole parameter range, so even
  // the extremes ("everything to just", "everything to equal") stay one drag
  // away despite the control being relative.
  setRange(-1.0, 1.0, 0.0);
  setValue(0.0, juce::dontSendNotification);
  setDoubleClickReturnValue(false, 0.0);

  // A relative gesture needs a start and an end to bracket it. The wheel has
  // neither, so it would move the knob without moving anything else.
  setScrollWheelEnabled(false);

  // Tells the look and feel to draw the arc outwards from twelve o'clock.
  getProperties().set("bipolar", true);

  onValueChange = [this] {
    if (onRelativeDelta)
      onRelativeDelta((float)getValue());
  };
}

void RelativeKnob::startedDragging() {
  if (onRelativeStart)
    onRelativeStart();
}

void RelativeKnob::stoppedDragging() {
  if (onRelativeEnd)
    onRelativeEnd();

  // Spring back so the next drag starts from wherever the strips now sit.
  setValue(0.0, juce::dontSendNotification);
  repaint();
}

// =============================================================================

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  KnobPanel *knobs[] = {&tuneAll,  &master,     &spread,
                        &velocity, &aftertouch, &bend};

  for (auto *k : knobs) {
    k->getSlider().setPopupDisplayEnabled(true, true, &popupParent);
    addAndMakeVisible(*k);
  }

  // ---- the endless macros are not parameters --------------------------------
  // They nudge all 32 strips by the same amount, so the shape you dialled in
  // survives the gesture.
  tuneAll.slider.setTooltip("Moves every partial towards just intonation or "
                            "equal temperament, keeping their spread");
  velocity.slider.setTooltip(
      "Moves every partial's velocity sensitivity, keeping their spread");
  aftertouch.slider.setTooltip(
      "Moves every partial's aftertouch amount, keeping their spread");

  drift.slider.setTooltip(
      "Moves every partial's pitch drift, keeping their spread");

  wireMacro(tuneAll.slider, Role::Tune);
  wireMacro(drift.slider, Role::Drift);
  wireMacro(velocity.slider, Role::Velocity);
  wireMacro(aftertouch.slider, Role::Aftertouch);

  master.slider.setTooltip("Output level");
  spread.slider.setTooltip("Fans the partials across the stereo field");
  bend.slider.setTooltip("Pitch bend range in semitones");

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);
  spreadAttachment = std::make_unique<SliderAttachment>(apvts, params::spreadId,
                                                        spread.slider);
  bendAttachment = std::make_unique<SliderAttachment>(
      apvts, params::bendRangeId, bend.slider);

  // ---- presets --------------------------------------------------------------
  presetBox.addItemList(presets::names(), 1);
  presetBox.setTextWhenNothingSelected("Select...");
  presetBox.onChange = [this] {
    const auto id = presetBox.getSelectedId();
    if (id > 0 && onPresetChosen)
      onPresetChosen(id - 1);
  };
  addAndMakeVisible(presetBox);

  // ---- polyphony ------------------------------------------------------------
  for (size_t i = 0; i < params::kPolyphonyChoices.size(); ++i)
    polyBox.addItem(juce::String(params::kPolyphonyChoices[i]), (int)i + 1);

  polyAttachment =
      std::make_unique<ComboBoxAttachment>(apvts, params::polyphonyId, polyBox);
  polyBox.setTooltip(
      "Maximum simultaneous notes. Each note runs 32 sine partials.");
  addAndMakeVisible(polyBox);

  // ---- zoom -----------------------------------------------------------------
  for (int i = 0; i < (int)std::size(kZoomChoices); ++i)
    zoomBox.addItem(
        juce::String(juce::roundToInt(kZoomChoices[i] * 100.0f)) + "%", i + 1);

  zoomBox.setSelectedId(2, juce::dontSendNotification);
  zoomBox.onChange = [this] {
    const auto id = zoomBox.getSelectedId();
    if (id > 0 && onZoomChanged)
      onZoomChanged(kZoomChoices[id - 1]);
  };
  addAndMakeVisible(zoomBox);

  // ---- toggles --------------------------------------------------------------
  styleToggle(linkButton, "LINK",
              "Gang every strip: moving one knob moves all 32");
  styleToggle(phaseButton, "PHASE",
              "Reset partial phase on each note for a coherent attack");
  styleToggle(clipButton, "CLIP",
              "Soft-clip the output. Worth leaving on with 32 faders.");

  linkButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);

  phaseAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::phaseResetId, phaseButton);
  clipAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::safetyClipId, clipButton);

  // ---- captions -------------------------------------------------------------
  struct {
    juce::Label *label;
    const char *text;
    juce::Justification just;
  } captions[] = {
      {&presetCaption, "PRESET", juce::Justification::centredLeft},
      {&polyCaption, "POLY", juce::Justification::centredLeft},
      {&zoomCaption, "ZOOM", juce::Justification::centredLeft},
  };

  for (auto &c : captions) {
    c.label->setText(c.text, juce::dontSendNotification);
    c.label->setFont(makeFont(9.0f, true));
    c.label->setColour(juce::Label::textColourId, colours::textDim);
    c.label->setJustificationType(c.just);
    c.label->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*c.label);
  }

  voicesLabel.setFont(makeFont(10.0f));
  voicesLabel.setColour(juce::Label::textColourId, colours::textDim);
  voicesLabel.setJustificationType(juce::Justification::centredRight);
  voicesLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(voicesLabel);

  setVoiceCount(0, 8);
}

void TopBar::styleToggle(juce::TextButton &b, const juce::String &text,
                         const juce::String &tooltip) {
  b.setButtonText(text);
  b.setClickingTogglesState(true);
  b.setTooltip(tooltip);
  addAndMakeVisible(b);
}

void TopBar::wireMacro(RelativeKnob &knob, Role role) {
  knob.onRelativeStart = [this, role] {
    if (onMacroStart)
      onMacroStart(role);
  };
  knob.onRelativeDelta = [this, role](float delta) {
    if (onMacroDelta)
      onMacroDelta(role, delta);
  };
  knob.onRelativeEnd = [this, role] {
    if (onMacroEnd)
      onMacroEnd(role);
  };
}

void TopBar::setVoiceCount(int active, int limit) {
  voicesLabel.setText(juce::String(active) + " / " + juce::String(limit) +
                          " voices",
                      juce::dontSendNotification);
}

void TopBar::setZoomChoice(float zoom) {
  for (int i = 0; i < (int)std::size(kZoomChoices); ++i) {
    if (std::abs(kZoomChoices[i] - zoom) < 0.01f) {
      zoomBox.setSelectedId(i + 1, juce::dontSendNotification);
      return;
    }
  }
}

void TopBar::paint(juce::Graphics &g) {
  g.setColour(colours::panel);
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  auto title = getLocalBounds().reduced(12, 6).removeFromLeft(168);

  g.setColour(colours::text);
  g.setFont(makeFont(21.0f, true));
  g.drawText("OVERTONIUM", title.removeFromTop(24),
             juce::Justification::centredLeft, false);

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.5f));
  g.drawText("32-partial overtone synthesiser", title.removeFromTop(13),
             juce::Justification::centredLeft, false);
}

void TopBar::resized() {
  auto b = getLocalBounds().reduced(12, 6);

  b.removeFromLeft(168); // title, painted rather than a child component
  b.removeFromLeft(10);

  KnobPanel *knobs[] = {&tuneAll,  &master,     &spread,
                        &velocity, &aftertouch, &bend};

  for (auto *k : knobs) {
    if (b.getWidth() < 60)
      break;

    k->setBounds(b.removeFromLeft(54));
    b.removeFromLeft(2);
  }

  auto placeCaptioned = [](juce::Component &c, juce::Label &cap,
                           juce::Rectangle<int> column) {
    auto area = column.withSizeKeepingCentre(column.getWidth(), 37);
    cap.setBounds(area.removeFromTop(12));
    area.removeFromTop(1);
    c.setBounds(area);
  };

  // Everything on the right is placed from the right edge inwards, so the knob
  // cluster in the middle is what gets squeezed when the window is narrow.
  voicesLabel.setBounds(
      b.removeFromRight(juce::jmin(72, juce::jmax(0, b.getWidth()))));
  b.removeFromRight(6);

  if (b.getWidth() > 90) {
    placeCaptioned(zoomBox, zoomCaption, b.removeFromRight(74));
    b.removeFromRight(8);
  }

  if (b.getWidth() > 180) {
    auto buttons = b.removeFromRight(164).withSizeKeepingCentre(164, 24);
    linkButton.setBounds(buttons.removeFromLeft(50));
    buttons.removeFromLeft(5);
    phaseButton.setBounds(buttons.removeFromLeft(52));
    buttons.removeFromLeft(5);
    clipButton.setBounds(buttons);
    b.removeFromRight(8);
  }

  if (b.getWidth() > 80) {
    placeCaptioned(polyBox, polyCaption, b.removeFromRight(60));
    b.removeFromRight(8);
  }

  if (b.getWidth() > 110)
    placeCaptioned(presetBox, presetCaption,
                   b.removeFromRight(juce::jmin(170, b.getWidth())));
}

} // namespace ovt::ui
