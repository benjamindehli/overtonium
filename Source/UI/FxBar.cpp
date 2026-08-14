#include "FxBar.h"

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
constexpr int kBarMargin = 12;
constexpr int kBarPadY = 6;
constexpr int kRowHeight = 54;
constexpr int kGroupPad = 7;
constexpr int kGroupGap = 8;
constexpr int kKnobWidth = 42;
constexpr int kToggleWidth = 52;
constexpr int kToggleHeight = 22;
constexpr int kToggleGap = 6;

constexpr int kEchoKnobs = 6;
constexpr int kReverbKnobs = 7;

int groupWidth(int knobs) {
  return 2 * kGroupPad + kToggleWidth + kToggleGap + knobs * kKnobWidth;
}
} // namespace

int FxBar::height() { return kRowHeight + 2 * kBarPadY; }

int FxBar::minimumWidth() {
  return 2 * kBarMargin + groupWidth(kEchoKnobs) + kGroupGap +
         groupWidth(kReverbKnobs);
}

FxBar::FxBar(juce::AudioProcessorValueTreeState &state,
             juce::Component &popupParent)
    : apvts(state), popupHost(popupParent) {
  styleToggle(echoButton, "ECHO",
              "Tape echo across the whole instrument, before the master fader");
  styleToggle(reverbButton, "REVERB",
              "Reverb across the whole instrument, after the echo");

  echoAttachment =
      std::make_unique<ButtonAttachment>(apvts, params::echoOnId, echoButton);
  reverbAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::reverbOnId, reverbButton);

  addKnob(echoControls, "MIX", params::echoMixId,
          "How much of the output is repeats");
  addKnob(echoControls, "TIME", params::echoTimeId,
          "Distance between the heads. Moving it winds the tape rather than "
          "cutting to the new time, so the repeats slide in pitch on the way.");
  addKnob(echoControls, "FDBK", params::echoFeedbackId,
          "How much of each repeat goes round again");
  addKnob(echoControls, "TONE", params::echoToneId,
          "Dark leaves every repeat duller than the last, bright keeps them "
          "close to the original");
  addKnob(echoControls, "WOW", params::echoWobbleId,
          "Wow and flutter. The motor is never quite steady, so the pitch of "
          "the repeats wanders.");
  addKnob(echoControls, "SPREAD", params::echoSpreadId,
          "Crossfeed between the heads. At zero the repeats stay where they "
          "were played, at full they alternate sides.");

  addKnob(reverbControls, "MIX", params::reverbMixId,
          "How much of the output is reverb");
  addKnob(reverbControls, "SIZE", params::reverbSizeId,
          "Room dimensions, from a booth to a hall");
  addKnob(reverbControls, "DECAY", params::reverbDecayId,
          "How long the tail takes to fall away");
  addKnob(reverbControls, "DAMP", params::reverbDampId,
          "How quickly the top end dies out of the tail");
  addKnob(reverbControls, "LO CUT", params::reverbLowCutId,
          "Keeps the fundamental out of the tail, which matters on an "
          "instrument built from 32 partials");
  addKnob(reverbControls, "PRE", params::reverbPreDelayId,
          "Silence between the note and its reverb. A little of it keeps the "
          "attack clear of the wash.");
  addKnob(reverbControls, "WIDTH", params::reverbWidthId,
          "Mono at zero, fully spread at the top");
}

void FxBar::styleToggle(juce::TextButton &b, const juce::String &text,
                        const juce::String &tooltip) {
  b.setButtonText(text);
  b.setClickingTogglesState(true);
  b.setTooltip(tooltip);
  b.setColour(juce::TextButton::buttonOnColourId, colours::accent);
  addAndMakeVisible(b);
}

FxBar::Control &FxBar::addKnob(std::vector<Control> &into,
                               const juce::String &caption,
                               const juce::String &paramId,
                               const juce::String &tooltip) {
  Control c;
  c.knob = std::make_unique<LabelledKnob>(caption);
  c.knob->slider.setTooltip(tooltip);
  c.knob->slider.setPopupDisplayEnabled(true, true, &popupHost);

  if (auto *p = apvts.getParameter(paramId))
    c.knob->slider.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  addAndMakeVisible(*c.knob);

  c.attachment =
      std::make_unique<SliderAttachment>(apvts, paramId, c.knob->slider);

  into.push_back(std::move(c));
  return into.back();
}

void FxBar::layoutGroup(juce::Rectangle<int> area, juce::TextButton &toggle,
                        std::vector<Control> &controls) {
  auto inner = area.reduced(kGroupPad, 0);

  // The switch names the group, so it sits at its head rather than needing a
  // caption of its own.
  auto toggleArea = inner.removeFromLeft(kToggleWidth);
  toggle.setBounds(
      toggleArea.withSizeKeepingCentre(kToggleWidth, kToggleHeight));

  inner.removeFromLeft(kToggleGap);

  for (auto &c : controls)
    c.knob->setBounds(inner.removeFromLeft(kKnobWidth));
}

void FxBar::resized() {
  auto area = getLocalBounds().reduced(kBarMargin, kBarPadY);
  auto row = area.removeFromTop(kRowHeight);

  groupBounds[0] = row.removeFromLeft(groupWidth(kEchoKnobs));
  row.removeFromLeft(kGroupGap);
  groupBounds[1] = row.removeFromLeft(groupWidth(kReverbKnobs));

  layoutGroup(groupBounds[0], echoButton, echoControls);
  layoutGroup(groupBounds[1], reverbButton, reverbControls);
}

void FxBar::paint(juce::Graphics &g) {
  g.setColour(colours::panel);
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, 0, getWidth(), 1);

  for (const auto &r : groupBounds) {
    if (r.isEmpty())
      continue;

    const auto f = r.toFloat();

    g.setColour(colours::panelAlt.withAlpha(0.75f));
    g.fillRoundedRectangle(f, 4.0f);
    g.setColour(colours::outline.withAlpha(0.9f));
    g.drawRoundedRectangle(f.reduced(0.5f), 4.0f, 1.0f);
  }
}

} // namespace ovt::ui
