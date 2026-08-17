#include "NoiseStrip.h"

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }

/// A neutral grey, deliberately outside the interval palette. Noise is not a
/// member of the harmonic series and should not look like one.
const juce::Colour kNoiseColour{0xff9aa4b0};
} // namespace

NoiseStrip::NoiseStrip(juce::AudioProcessorValueTreeState &state,
                       HoverTarget &hoverTarget, juce::Component &popupParent)
    : apvts(state), hover(hoverTarget), popupHost(popupParent),
      colour(kNoiseColour), meter(kNoiseColour) {
  addMouseListener(this, true);

  const auto secondary = colour.withSaturation(0.10f).withBrightness(0.68f);

  setUpKnob(colourKnob, params::colourSuffix, colour,
            "Tilts the noise from dark rumble through flat to bright hiss");
  setUpKnob(delay, params::delaySuffix, secondary, "Delay before the attack");
  setUpKnob(attack, params::attackSuffix, secondary, "Attack");
  setUpKnob(decay, params::decaySuffix, secondary, "Decay");
  setUpKnob(sustain, params::sustainSuffix, secondary, "Sustain");
  setUpKnob(swell, params::swellSuffix, secondary,
            "How long the key-off stage takes to reach its level");
  setUpKnob(offLevel, params::offLevelSuffix, secondary,
            "Where the envelope goes when the key is let go. Zero skips the "
            "stage and releases from wherever it was.");
  setUpKnob(release, params::releaseSuffix, secondary, "Release");
  setUpKnob(amRate, params::amRateSuffix, secondary, "Tremolo rate");
  setUpKnob(amDepth, params::amDepthSuffix, secondary, "Tremolo depth");
  setUpKnob(velocity, params::velSuffix, secondary,
            "How much key velocity scales the noise. Negative inverts it.");
  setUpKnob(aftertouch, params::atSuffix, secondary,
            "How much key pressure moves the noise. Negative fades it out.");

  setUpKnob(pan, params::panSuffix, secondary,
            "Where the noise sits in the stereo field");

  velocity.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);

  volume.setSliderStyle(juce::Slider::LinearVertical);
  volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volume.setColour(juce::Slider::trackColourId, colour);
  volume.getProperties().set("meteredGroove", true);
  volume.setPopupDisplayEnabled(true, true, &popupHost);
  volume.setTooltip("Noise level");
  addAndMakeVisible(volume);
  sliderAttachments.push_back(std::make_unique<SliderAttachment>(
      apvts, params::noiseParamId(params::volumeSuffix), volume));

  addAndMakeVisible(meter);
  meter.toBack(); // the fader cap has to draw over it

  muteButton.setClickingTogglesState(true);
  soloButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::buttonOnColourId, colours::muteOn);
  soloButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  muteButton.setTooltip("Mute the noise channel");
  soloButton.setTooltip("Solo the noise channel");
  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::noiseParamId(params::muteSuffix), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::noiseParamId(params::soloSuffix), soloButton);

  for (auto *l : {&colourReadout, &levelReadout}) {
    l->setJustificationType(juce::Justification::centred);
    l->setFont(makeFont(10.0f));
    l->setColour(juce::Label::textColourId, colours::textDim);
    l->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*l);
  }

  colourReadout.setText("colour", juce::dontSendNotification);

  setTooltip("Noise channel. It has the same envelope, tremolo, velocity and "
             "aftertouch as a partial, but no pitch, so no tuning, pitch "
             "modulation or drift.");
}

void NoiseStrip::setUpKnob(juce::Slider &s, const char *suffix,
                           juce::Colour fill, const juce::String &tooltip) {
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  s.setTooltip(tooltip);
  addAndMakeVisible(s);

  const auto id = params::noiseParamId(suffix);
  sliderAttachments.push_back(std::make_unique<SliderAttachment>(apvts, id, s));

  if (auto *p = apvts.getParameter(id))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));
}

void NoiseStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;
  setAlpha(silenced ? 0.4f : 1.0f);
}

void NoiseStrip::mouseEnter(const juce::MouseEvent &e) { reportHover(e); }
void NoiseStrip::mouseMove(const juce::MouseEvent &e) { reportHover(e); }
void NoiseStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

void NoiseStrip::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  // -1 says the pointer is off the harmonic series, which is what stops a
  // hover here from arming a LINK preview.
  hover.hoverChanged(-1, getLocalBounds().contains(p) ? controlRowAt(rows, p)
                                                      : kNoRow);
}

void NoiseStrip::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

void NoiseStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  paintChannelBackground(g, bounds, colours::panel.brighter(0.03f));

  const auto rows = layoutRows(bounds.reduced(2, 4));
  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colour);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  g.setColour(colours::text);
  g.setFont(makeFont(11.0f, true));
  g.drawText("NZ", header.removeFromTop(14), juce::Justification::centred,
             false);

  g.setColour(colour.withAlpha(0.85f));
  g.setFont(makeFont(9.0f));
  g.drawText("noise", header, juce::Justification::centred, false);

  g.setColour(colours::outline.withAlpha(0.7f));
  for (auto r : {Row::PitchModHeading, Row::EnvHeading, Row::KeyOffHeading,
                 Row::AmpModHeading, Row::OutputHeading}) {
    const auto row = rows[rowIndex(r)];
    g.fillRect(row.getX(), row.getY() + row.getHeight() / 2, row.getWidth(), 1);
  }

  // Start phase and the whole pitch modulation block have nothing to show, so
  // say so once rather than leaving a stretch of blank panel that looks like a
  // drawing bug. Noise has no phase to start at any more than it has a pitch.
  auto absent =
      rows[rowIndex(Row::Phase)].getUnion(rows[rowIndex(Row::Drift)]);

  g.setColour(colours::textDim.withAlpha(0.5f));
  g.setFont(makeFont(9.0f));
  g.drawText("no pitch", absent, juce::Justification::centred, false);
}

void NoiseStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  // Colour takes the tuning row, which is the one thing noise has that a
  // partial does not.
  colourKnob.setBounds(rows[rowIndex(Row::TuneKnob)]);
  colourReadout.setBounds(rows[rowIndex(Row::TuneText)]);

  delay.setBounds(rows[rowIndex(Row::Delay)].reduced(1));
  attack.setBounds(rows[rowIndex(Row::Attack)].reduced(1));
  decay.setBounds(rows[rowIndex(Row::Decay)].reduced(1));
  sustain.setBounds(rows[rowIndex(Row::Sustain)].reduced(1));
  swell.setBounds(rows[rowIndex(Row::Swell)].reduced(1));
  offLevel.setBounds(rows[rowIndex(Row::OffLevel)].reduced(1));
  release.setBounds(rows[rowIndex(Row::Release)].reduced(1));
  amRate.setBounds(rows[rowIndex(Row::AmRate)].reduced(1));
  amDepth.setBounds(rows[rowIndex(Row::AmDepth)].reduced(1));
  velocity.setBounds(rows[rowIndex(Row::Velocity)].reduced(1));
  aftertouch.setBounds(rows[rowIndex(Row::Aftertouch)].reduced(1));
  pan.setBounds(rows[rowIndex(Row::Pan)].reduced(1));

  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));

  {
    const auto base = colours::panel.brighter(0.03f);
    const auto top = base.brighter(0.10f);
    const auto bottom = base.darker(0.06f);

    const auto at = [&](int y) {
      return top.interpolatedWith(
          bottom, juce::jlimit(0.0f, 1.0f,
                               (float)y / (float)juce::jmax(1, getHeight())));
    };

    meter.setBackdrop(at(meter.getY()), at(meter.getBottom()));
  }
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  auto ms = rows[rowIndex(Row::MuteSolo)];
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2).reduced(1));
  soloButton.setBounds(ms.reduced(1));
}

} // namespace ovt::ui
