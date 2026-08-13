#include "TopBar.h"

#include "../PluginParameters.h"
#include "../Presets.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
constexpr float kZoomChoices[] = {0.75f, 1.0f, 1.25f, 1.5f};

} // namespace

// =============================================================================

namespace {
/// Shared with the channel meters so the two read on the same scale.
constexpr float kMeterFloorDb = -48.0f;

float toNormalised(float level) {
  if (level <= 1.0e-5f)
    return 0.0f;

  return juce::jlimit(0.0f, 1.0f,
                      (juce::Decibels::gainToDecibels(level) - kMeterFloorDb) /
                          -kMeterFloorDb);
}
} // namespace

bool StereoOutputMeter::Bar::advance(float level) {
  const auto norm = toNormalised(level);
  const auto next =
      norm > displayed ? norm : juce::jmax(norm, displayed - 0.05f);

  bool moved = std::abs(next - displayed) > 0.003f;
  displayed = next;

  // Hold the peak for about a second, then let it fall with the bar.
  if (next >= peak) {
    peak = next;
    hold = 30;
    moved = true;
  } else if (hold > 0) {
    --hold;
  } else if (peak > 0.0f) {
    peak = juce::jmax(next, peak - 0.02f);
    moved = true;
  }

  return moved;
}

void StereoOutputMeter::push(float l, float r) {
  const bool a = left.advance(l);
  const bool b = right.advance(r);

  if (a || b)
    repaint();
}

void StereoOutputMeter::paintBar(juce::Graphics &g, juce::Rectangle<float> r,
                                 const Bar &bar) const {
  g.setColour(colours::groove);
  g.fillRoundedRectangle(r, 1.5f);

  // The gradient spans the whole bar, so the colours stay anchored to their
  // decibel positions instead of stretching with the level.
  juce::ColourGradient grade(colours::accent, r.getX(), r.getY(),
                             colours::muteOn, r.getRight(), r.getY(), false);
  grade.addColour(0.80, colours::accent);
  grade.addColour(0.94, colours::soloOn);

  if (bar.displayed > 0.001f) {
    g.setGradientFill(grade);
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * bar.displayed), 1.5f);
  }

  if (bar.peak > 0.001f) {
    const auto x = r.getX() + r.getWidth() * bar.peak;
    g.setColour(bar.peak > 0.94f ? colours::muteOn : colours::text);
    g.fillRect(juce::jmin(x, r.getRight() - 1.5f), r.getY(), 1.5f,
               r.getHeight());
  }
}

void StereoOutputMeter::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat();

  // Scale marks at the decibel values worth aiming at.
  const auto scale = area.removeFromBottom(5.0f);
  for (const float db : {-48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f}) {
    const auto t = (db - kMeterFloorDb) / -kMeterFloorDb;
    const auto x = scale.getX() + t * (scale.getWidth() - 1.5f);

    g.setColour(colours::textDim.withAlpha(db >= -6.0f ? 0.55f : 0.30f));
    g.fillRect(x, scale.getY(), 1.0f, 3.0f);
  }

  const auto gap = 2.0f;
  const auto barH = (area.getHeight() - gap) * 0.5f;

  paintBar(g, area.removeFromTop(barH), left);
  area.removeFromTop(gap);
  paintBar(g, area.removeFromTop(barH), right);
}

// =============================================================================

LabelledKnob::LabelledKnob(juce::String captionText, juce::Colour fill)
    : caption(std::move(captionText)) {
  slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
  addAndMakeVisible(slider);
}

void LabelledKnob::paint(juce::Graphics &g) {
  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));
  g.drawText(caption, getLocalBounds().removeFromBottom(11),
             juce::Justification::centred, false);
}

void LabelledKnob::resized() {
  slider.setBounds(getLocalBounds().withTrimmedBottom(12));
}

// =============================================================================

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  LabelledKnob *knobs[] = {&master, &spread, &bend};

  for (auto *k : knobs) {
    k->slider.setPopupDisplayEnabled(true, true, &popupParent);
    addAndMakeVisible(*k);
  }

  master.slider.setTooltip("Output level");
  spread.slider.setTooltip("Fans the partials across the stereo field");
  bend.slider.setTooltip("Pitch bend range in semitones");

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);
  spreadAttachment = std::make_unique<SliderAttachment>(apvts, params::spreadId,
                                                        spread.slider);
  bendAttachment = std::make_unique<SliderAttachment>(
      apvts, params::bendRangeId, bend.slider);

  meterCaption.setText("OUT  L / R", juce::dontSendNotification);
  meterCaption.setFont(makeFont(9.0f, true));
  meterCaption.setColour(juce::Label::textColourId, colours::textDim);
  meterCaption.setJustificationType(juce::Justification::centredLeft);
  meterCaption.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(meterCaption);
  addAndMakeVisible(meter);

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
              "Gang the strips, so dragging one channel's knob moves that "
              "knob on all 32. The master channel does the same thing.");
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

  LabelledKnob *knobs[] = {&master, &spread, &bend};

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

  if (b.getWidth() > 110) {
    placeCaptioned(presetBox, presetCaption,
                   b.removeFromRight(juce::jmin(170, b.getWidth())));
    b.removeFromRight(14);
  }

  // Whatever is left in the middle goes to the output meter, so it grows with
  // the window rather than staying a token strip.
  if (b.getWidth() > 90) {
    auto column = b.withSizeKeepingCentre(juce::jmin(300, b.getWidth()), 37);
    meterCaption.setBounds(column.removeFromTop(12));
    column.removeFromTop(1);
    meter.setBounds(column);
  } else {
    meter.setBounds({});
    meterCaption.setBounds({});
  }
}

} // namespace ovt::ui
