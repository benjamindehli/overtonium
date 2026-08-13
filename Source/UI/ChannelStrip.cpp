#include "ChannelStrip.h"

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }
} // namespace

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

void LevelMeter::push(float level) {
  // A decibel scale with a floor at -48 dB. On a linear amplitude scale
  // everything above the fundamental sits squashed against the bottom.
  const float norm =
      level <= 1.0e-5f
          ? 0.0f
          : juce::jlimit(0.0f, 1.0f,
                         (juce::Decibels::gainToDecibels(level) + 48.0f) /
                             48.0f);

  // Instant rise and a gentle fall, so a partial decaying under its own
  // envelope reads as a decay rather than as flicker.
  const float next =
      norm > displayed ? norm : juce::jmax(norm, displayed - 0.06f);

  // Below a pixel at any sensible size, so not worth waking the painter for.
  if (std::abs(next - displayed) < 0.004f)
    return;

  displayed = next;
  repaint();
}

void LevelMeter::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  g.setColour(colours::groove);
  g.fillRoundedRectangle(bounds, 1.5f);

  if (displayed <= 0.001f)
    return;

  const auto height = bounds.getHeight() * displayed;

  g.setColour(colour.withAlpha(0.9f));
  g.fillRoundedRectangle(bounds.withTop(bounds.getBottom() - height), 1.5f);
}

// =============================================================================

ChannelStrip::ChannelStrip(juce::AudioProcessorValueTreeState &state,
                           LinkTarget &linkTarget, juce::Component &popupParent,
                           int index0)
    : apvts(state), link(linkTarget), popupHost(popupParent), index(index0),
      info(harmonic(index0)),
      colour(intervalColour(harmonic(index0).pitchClass)), meter(colour) {
  // Tune and Level carry the strip's identity colour; the modulation and
  // envelope knobs are desaturated so they read as secondary at a glance across
  // 32 channels.
  const auto secondary = colour.withSaturation(0.28f).withBrightness(0.72f);

  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, secondary);
  setUpKnob(pmDepth, Role::PmDepth, secondary);
  setUpKnob(drift, Role::Drift, secondary);
  setUpKnob(attack, Role::Attack, secondary);
  setUpKnob(decay, Role::Decay, secondary);
  setUpKnob(sustain, Role::Sustain, secondary);
  setUpKnob(release, Role::Release, secondary);
  setUpKnob(amRate, Role::AmRate, secondary);
  setUpKnob(amDepth, Role::AmDepth, secondary);
  setUpKnob(velocity, Role::Velocity, secondary);
  setUpKnob(aftertouch, Role::Aftertouch, secondary);
  setUpFader(volume, Role::Volume, colour);

  muteButton.setClickingTogglesState(true);
  soloButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::buttonOnColourId, colours::muteOn);
  soloButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  muteButton.setTooltip("Mute harmonic " + juce::String(info.harmonic));
  soloButton.setTooltip("Solo harmonic " + juce::String(info.harmonic));
  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::muteSuffix, index), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::soloSuffix, index), soloButton);

  for (auto *l : {&tuneReadout, &levelReadout}) {
    l->setJustificationType(juce::Justification::centred);
    l->setFont(makeFont(10.0f));
    l->setColour(juce::Label::textColourId, colours::textDim);
    l->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*l);
  }

  addAndMakeVisible(meter);

  updateTuneReadout();
  updateLevelReadout();

  const auto cents = juce::String(info.jiCents, 1);
  setTooltip("Harmonic " + juce::String(info.harmonic) + "  -  " +
             intervalName(info.pitchClass) + "\n" +
             juce::String(info.etSemitones) +
             " semitones above the played note" + "\nJust intonation is " +
             (info.jiCents >= 0.0 ? "+" : "") + cents + " cents from that");
}

void ChannelStrip::setUpKnob(LinkableSlider &s, Role role, juce::Colour fill) {
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::setUpFader(LinkableSlider &s, Role role, juce::Colour fill) {
  s.setSliderStyle(juce::Slider::LinearVertical);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::trackColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::wireUp(LinkableSlider &s, Role role) {
  const auto paramId = params::oscParamId(roleSuffix(role), index);

  // The attachment must exist before onValueChange fires, or the first callback
  // reads a slider whose range has not been set up yet.
  sliderAttachments.push_back(
      std::make_unique<SliderAttachment>(apvts, paramId, s));

  // Double-click restores the parameter's own default rather than the range
  // minimum.
  if (auto *p = apvts.getParameter(paramId))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  s.onUserDragStart = [this, role] { link.linkDragStarted(role, index); };
  s.onUserDragEnd = [this, role] { link.linkDragEnded(role, index); };

  s.onValueChange = [this, &s, role] {
    if (role == Role::Tune)
      updateTuneReadout();
    else if (role == Role::Volume)
      updateLevelReadout();

    if (s.isUserDragging())
      link.linkValueChanged(role, index, (float)s.getValue());
  };
}

void ChannelStrip::updateTuneReadout() {
  const auto blend = tune.getValue();
  const auto cents = blend * info.jiCents;

  juce::String text;

  if (std::abs(info.jiCents) < 0.05)
    text = "0.0"; // the octaves are the same either way
  else if (blend <= 0.0005)
    text = "ET";
  else
    text = (cents >= 0.0 ? "+" : "") + juce::String(cents, 1);

  tuneReadout.setText(text, juce::dontSendNotification);
}

void ChannelStrip::updateLevelReadout() {
  const auto v = (float)volume.getValue();

  levelReadout.setText(v <= 0.0005f
                           ? juce::String("-inf")
                           : juce::String(juce::Decibels::gainToDecibels(v), 1),
                       juce::dontSendNotification);
}

void ChannelStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;
  setAlpha(silenced ? 0.4f : 1.0f);
}

void ChannelStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  g.setColour((index % 2) == 0 ? colours::panel : colours::panelAlt);
  g.fillRect(bounds);

  // A faint wash on the octave partials makes the shape of the series readable
  // even when you are scrolled halfway along the mixer.
  if (info.pitchClass == 0) {
    g.setColour(colour.withAlpha(0.055f));
    g.fillRect(bounds);
  }

  const auto rows = layoutRows(bounds.reduced(2, 4));

  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colour);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  g.setColour(colours::text);
  g.setFont(makeFont(14.0f, true));
  g.drawText(juce::String(info.harmonic), header.removeFromTop(14),
             juce::Justification::centred, false);

  g.setColour(colour.withAlpha(0.85f));
  g.setFont(makeFont(9.0f));
  g.drawText(intervalShortName(info.pitchClass), header,
             juce::Justification::centred, false);

  // Section rules, aligned with the gutter headings.
  g.setColour(colours::outline.withAlpha(0.7f));
  for (auto r : {Row::PitchModHeading, Row::EnvHeading, Row::AmpModHeading,
                 Row::OutputHeading}) {
    const auto row = rows[rowIndex(r)];
    g.fillRect(row.getX(), row.getY() + row.getHeight() / 2, row.getWidth(), 1);
  }

  g.setColour(colours::background.withAlpha(0.55f));
  g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void ChannelStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  tune.setBounds(rows[rowIndex(Row::TuneKnob)]);
  tuneReadout.setBounds(rows[rowIndex(Row::TuneText)]);
  pmRate.setBounds(rows[rowIndex(Row::PmRate)].reduced(1));
  pmDepth.setBounds(rows[rowIndex(Row::PmDepth)].reduced(1));
  drift.setBounds(rows[rowIndex(Row::Drift)].reduced(1));
  attack.setBounds(rows[rowIndex(Row::Attack)].reduced(1));
  decay.setBounds(rows[rowIndex(Row::Decay)].reduced(1));
  sustain.setBounds(rows[rowIndex(Row::Sustain)].reduced(1));
  release.setBounds(rows[rowIndex(Row::Release)].reduced(1));
  amRate.setBounds(rows[rowIndex(Row::AmRate)].reduced(1));
  amDepth.setBounds(rows[rowIndex(Row::AmDepth)].reduced(1));
  velocity.setBounds(rows[rowIndex(Row::Velocity)].reduced(1));
  aftertouch.setBounds(rows[rowIndex(Row::Aftertouch)].reduced(1));
  // Fader and meter side by side, the way a mixer channel reads: what you asked
  // for on the left, what is actually coming out on the right.
  auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.removeFromRight(9).reduced(1, 2));
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  auto ms = rows[rowIndex(Row::MuteSolo)];
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2).reduced(1));
  soloButton.setBounds(ms.reduced(1));
}

} // namespace ovt::ui
