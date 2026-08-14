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
  // A little vertical inset, or the tick ring sits right on the group border.
  slider.setBounds(getLocalBounds().withTrimmedBottom(12).reduced(0, 2));
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
  // The meter sits behind the fader and owns its whole track, so it is drawn
  // wide enough to read at a glance rather than as a hairline beside it.
  const auto full = getLocalBounds().toFloat();
  const auto trackW = juce::jmax(6.0f, full.getWidth() * 0.62f);
  const auto track = full.withSizeKeepingCentre(trackW, full.getHeight());
  const auto corner = trackW * 0.35f;

  g.setColour(colours::groove);
  g.fillRoundedRectangle(track, corner);

  // Recessed, so the light that reaches the panel does not reach the bottom of
  // the channel it is cut into.
  const auto lip = juce::jmin(6.0f, track.getHeight() * 0.06f);
  g.setGradientFill(
      juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), track.getX(),
                           track.getY(), juce::Colours::black.withAlpha(0.0f),
                           track.getX(), track.getY() + lip, false));
  g.fillRect(track.withHeight(lip));

  if (displayed <= 0.001f)
    return;

  const auto height = track.getHeight() * displayed;

  g.setColour(colour.withAlpha(0.92f));
  g.fillRoundedRectangle(track.withTop(track.getBottom() - height), corner);
}

// =============================================================================

ChannelStrip::ChannelStrip(juce::AudioProcessorValueTreeState &state,
                           LinkTarget &linkTarget, HoverTarget &hoverTarget,
                           juce::Component &popupParent, int index0)
    : apvts(state), link(linkTarget), hover(hoverTarget),
      popupHost(popupParent), index(index0), info(harmonic(index0)),
      colour(intervalColour(harmonic(index0).pitchClass)), meter(colour) {
  // Deep listener, so a pointer resting on a knob is reported by the strip that
  // owns it rather than being swallowed by the control.
  addMouseListener(this, true);

  // The strip decides the pointer for everything on it, which is how the LINK
  // tool shows itself. Children keep their own only where that would be wrong,
  // as on the mute and solo buttons.
  setMouseCursor(juce::MouseCursor::ParentCursor);

  // Tune and Level carry the strip's identity colour; the modulation and
  // envelope knobs are desaturated so they read as secondary at a glance across
  // 32 channels.
  const auto secondary = colour.withSaturation(0.28f).withBrightness(0.72f);

  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, secondary);
  setUpKnob(pmDepth, Role::PmDepth, secondary);
  setUpKnob(drift, Role::Drift, secondary);
  setUpKnob(delay, Role::Delay, secondary);
  setUpKnob(attack, Role::Attack, secondary);
  setUpKnob(decay, Role::Decay, secondary);
  setUpKnob(sustain, Role::Sustain, secondary);
  setUpKnob(release, Role::Release, secondary);
  setUpKnob(amRate, Role::AmRate, secondary);
  setUpKnob(amDepth, Role::AmDepth, secondary);
  setUpKnob(velocity, Role::Velocity, secondary);
  setUpKnob(aftertouch, Role::Aftertouch, secondary);

  setUpKnob(pan, Role::Pan, secondary);

  // These all run either side of zero, so their arcs read out from twelve
  // o'clock rather than filling from the left.
  velocity.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);
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
  meter.toBack(); // the fader cap has to draw over it

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
  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::setUpFader(LinkableSlider &s, Role role, juce::Colour fill) {
  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::LinearVertical);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::trackColourId, fill);
  s.getProperties().set("meteredGroove", true);
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

void ChannelStrip::mouseDown(const juce::MouseEvent &e) {
  if (e.mods.isPopupMenu())
    link.showLinkMenu();
}

void ChannelStrip::mouseEnter(const juce::MouseEvent &e) { reportHover(e); }
void ChannelStrip::mouseMove(const juce::MouseEvent &e) { reportHover(e); }
void ChannelStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

void ChannelStrip::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  // Leaving one knob for the next fires the exit before the enter, so the
  // answer is always worked out from where the pointer now is rather than from
  // which callback arrived.
  hover.hoverChanged(index, getLocalBounds().contains(p) ? controlRowAt(rows, p)
                                                         : kNoRow);
}

void ChannelStrip::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  // Only the two bands that changed are repainted. With 33 strips answering
  // every time the pointer crosses a row, repainting whole channels would
  // redraw the window several times a second for a wash and two hairlines.
  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

LinkableSlider *ChannelStrip::sliderForRole(Role role) {
  switch (role) {
  case Role::Tune:
    return &tune;
  case Role::PmRate:
    return &pmRate;
  case Role::PmDepth:
    return &pmDepth;
  case Role::Drift:
    return &drift;
  case Role::Delay:
    return &delay;
  case Role::Attack:
    return &attack;
  case Role::Decay:
    return &decay;
  case Role::Sustain:
    return &sustain;
  case Role::Release:
    return &release;
  case Role::AmRate:
    return &amRate;
  case Role::AmDepth:
    return &amDepth;
  case Role::Velocity:
    return &velocity;
  case Role::Aftertouch:
    return &aftertouch;
  case Role::Pan:
    return &pan;
  case Role::Volume:
    return &volume;

  case Role::NumRoles:
  default:
    jassertfalse;
    return nullptr;
  }
}

void ChannelStrip::setLinkGlow(Role role, float amount) {
  amount = juce::jlimit(0.0f, 1.0f, amount);

  if (role == glowRole && std::abs(amount - glowAmount) < 0.004f)
    return;

  // Moving to a different row leaves the old control lit unless it is put out
  // on the way past.
  if (role != glowRole)
    if (auto *previous = sliderForRole(glowRole)) {
      previous->getProperties().set("linkGlow", 0.0);
      previous->repaint();
    }

  glowRole = role;
  glowAmount = amount;

  if (auto *s = sliderForRole(glowRole)) {
    s->getProperties().set("linkGlow", (double)glowAmount);
    s->repaint();
  }
}

void ChannelStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  paintChannelBackground(g, bounds,
                         (index % 2) == 0 ? colours::panel : colours::panelAlt);

  // A faint wash on the octave partials makes the shape of the series readable
  // even when you are scrolled halfway along the mixer.
  if (info.pitchClass == 0) {
    g.setColour(colour.withAlpha(0.055f));
    g.fillRect(bounds);
  }

  const auto rows = layoutRows(bounds.reduced(2, 4));

  if (rowShowsHighlight(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

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
}

void ChannelStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(2, 4));

  tune.setBounds(rows[rowIndex(Row::TuneKnob)]);
  tuneReadout.setBounds(rows[rowIndex(Row::TuneText)]);
  pmRate.setBounds(rows[rowIndex(Row::PmRate)].reduced(1));
  pmDepth.setBounds(rows[rowIndex(Row::PmDepth)].reduced(1));
  drift.setBounds(rows[rowIndex(Row::Drift)].reduced(1));
  delay.setBounds(rows[rowIndex(Row::Delay)].reduced(1));
  attack.setBounds(rows[rowIndex(Row::Attack)].reduced(1));
  decay.setBounds(rows[rowIndex(Row::Decay)].reduced(1));
  sustain.setBounds(rows[rowIndex(Row::Sustain)].reduced(1));
  release.setBounds(rows[rowIndex(Row::Release)].reduced(1));
  amRate.setBounds(rows[rowIndex(Row::AmRate)].reduced(1));
  amDepth.setBounds(rows[rowIndex(Row::AmDepth)].reduced(1));
  velocity.setBounds(rows[rowIndex(Row::Velocity)].reduced(1));
  aftertouch.setBounds(rows[rowIndex(Row::Aftertouch)].reduced(1));
  pan.setBounds(rows[rowIndex(Row::Pan)].reduced(1));
  // Meter and fader share the same rectangle. The meter draws the track and
  // the fader draws only its cap on top, so the output fills the fader itself.
  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  auto ms = rows[rowIndex(Row::MuteSolo)];
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2).reduced(1));
  soloButton.setBounds(ms.reduced(1));
}

} // namespace ovt::ui
