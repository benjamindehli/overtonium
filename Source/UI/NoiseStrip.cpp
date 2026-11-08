#include "NoiseStrip.h"
#include "LearnMenu.h"

#include <cmath>
#include <iterator>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
/// The height and font size of each string knob's caption, under the knob.
constexpr int kStringCaptionHeight = 9;
constexpr float kStringCaptionFont = 9.0f;

inline size_t rowIndex(Row r) { return (size_t)r; }

/// The rows that hold the noise channel's own controls rather than what the
/// gutter names: colour on the tuning rows and the string on the pitch
/// modulation rows. They take no part in the row highlight.
bool ownRow(Row r) {
  return r == Row::TuneKnob || r == Row::TuneText ||
         r == Row::PitchModHeading || r == Row::PmShape || r == Row::PmRate ||
         r == Row::PmDepth || r == Row::Drift || r == Row::Glide;
}

/// A neutral grey, outside the interval palette, since noise is not part of
/// the harmonic series.
const juce::Colour kNoiseColour{0xff9aa4b0};
} // namespace

NoiseStrip::NoiseStrip(juce::AudioProcessorValueTreeState &state,
                       HoverTarget &hoverTarget, juce::Component &popupParent)
    : StripColumn(hoverTarget), apvts(state), popupHost(popupParent),
      colour(kNoiseColour),
      amShape(state, params::noiseParamId(params::amShapeSuffix),
              params::amShapeSuffix, params::kAmpShapes,
              params::ampShapeNames()),
      muteButton(state, "M", params::noiseParamId(params::muteSuffix),
                 params::muteSuffix),
      soloButton(state, "S", params::noiseParamId(params::soloSuffix),
                 params::soloSuffix),
      meter(kNoiseColour), envLamp(kNoiseColour), keyOffLamp(kNoiseColour),
      tremoloLamp(kNoiseColour), outputLamp(kNoiseColour) {
  addAndMakeVisible(outputLamp);

  for (auto *lamp : {&envLamp, &keyOffLamp, &tremoloLamp})
    addAndMakeVisible(*lamp);

  addMouseListener(this, true);

  addAndMakeVisible(amShape);
  amShape.setTitle("Noise amp mod shape");

  setUpKnob(colourKnob, params::colourSuffix, colour,
            "Tilts the noise from dark rumble through flat to bright hiss");
  setUpKnob(strike, params::strikeSuffix, colour,
            "How much key velocity brings in the delay and shortens the "
            "attack. Negative inverts it.");
  // The popup shows the times it produces rather than a percentage. See
  // ChannelStrip.
  strike.textFromValueFunction = [this](double amount) {
    return params::strikeRangeText((float)amount, (float)delay.getValue(),
                                   (float)attack.getValue());
  };

  setUpKnob(delay, params::delaySuffix, colour, "Delay before the attack");
  setUpKnob(attack, params::attackSuffix, colour, "Attack");
  setUpKnob(decay, params::decaySuffix, colour, "Decay");
  setUpKnob(sustain, params::sustainSuffix, colour, "Sustain");
  setUpKnob(swell, params::swellSuffix, colour,
            "How long the key-off stage takes to reach its level");
  setUpKnob(offLevel, params::offLevelSuffix, colour,
            "Where the envelope goes when the key is let go. Zero skips the "
            "stage and releases from wherever it was.");
  setUpKnob(release, params::releaseSuffix, colour, "Release");
  setUpKnob(amRate, params::amRateSuffix, colour, "Tremolo rate");
  setUpKnob(amDepth, params::amDepthSuffix, colour, "Tremolo depth");
  setUpKnob(velocity, params::velSuffix, colour,
            "How much key velocity scales the noise. Negative inverts it.");
  setUpKnob(keyboard, params::keySuffix, colour,
            "How the noise follows the key played. Up to 100% it tilts around "
            "middle C, up to 24 dB per octave. Past 100% the turn moves away "
            "from middle C, an octave for every 20%.");
  setUpKnob(aftertouch, params::atSuffix, colour,
            "How much key pressure moves the noise. Negative fades it out.");

  setUpKnob(pan, params::panSuffix, colour,
            "Where the noise sits in the stereo field");

  setUpKnob(stringAmount, params::stringSuffix, colour,
            "From the noise alone to the noise through a string tuned to the "
            "note. A short burst plucks it, held noise blows into it.",
            "Noise string");
  setUpKnob(stringDecay, params::stringDecaySuffix, colour,
            "How long the string rings while the key is down. After the key is "
            "released, the release damps it.",
            "Noise string decay");
  setUpKnob(stringDamp, params::stringDampSuffix, colour,
            "How quickly the string loses its top, bright to dark",
            "Noise string damp");
  setUpKnob(stringPoint, params::stringPointSuffix, colour,
            "The point where the noise strikes the string, from near the end, "
            "which keeps every harmonic, to the middle, which loses the even "
            "ones.",
            "Noise string point");

  // Over the knobs, as resized keeps it.
  headerCap.onPaint = [this](juce::Graphics &g) { paintHeaderBand(g); };
  addAndMakeVisible(headerCap);

  strike.getProperties().set("bipolar", true);
  velocity.getProperties().set("bipolar", true);
  keyboard.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);

  volume.setSliderStyle(juce::Slider::LinearVertical);
  volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volume.setColour(juce::Slider::trackColourId, colour);
  volume.getProperties().set("meteredGroove", true);
  volume.setPopupDisplayEnabled(true, true, &popupHost);
  volume.setTooltip("Noise level");
  volume.setTitle("Noise level");
  addAndMakeVisible(volume);
  sliderAttachments.push_back(std::make_unique<SliderAttachment>(
      apvts, params::noiseParamId(params::volumeSuffix), volume));
  learn::tag(volume, params::noiseParamId(params::volumeSuffix));

  addAndMakeVisible(meter);
  meter.toBack(); // the fader cap has to draw over it

  // The lamp colours. See GlowButton.
  muteButton.setColour(juce::TextButton::textColourOnId, colours::muteOn);
  soloButton.setColour(juce::TextButton::textColourOnId, colours::soloOn);
  muteButton.setTooltip("Mute the noise channel. " + muteButton.onlyThisHint());
  soloButton.setTooltip("Solo the noise channel. " + soloButton.onlyThisHint());
  muteButton.setTitle("Noise mute");
  soloButton.setTitle("Noise solo");
  // One moulding around the pair, each half lighting on its own.
  OvertoniumLookAndFeel::gangLamps(muteButton, soloButton);

  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::noiseParamId(params::muteSuffix), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::noiseParamId(params::soloSuffix), soloButton);

  learn::tag(muteButton, params::noiseParamId(params::muteSuffix));
  learn::tag(soloButton, params::noiseParamId(params::soloSuffix));

  colourReadout.setJustificationType(juce::Justification::centred);
  colourReadout.setFont(makeFont(10.0f));
  colourReadout.setColour(juce::Label::textColourId, colours::textDim);
  colourReadout.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(colourReadout);
  colourReadout.setText("colour", juce::dontSendNotification);

  levelReadout.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(levelReadout);

  volume.onValueChange = [this] { updateLevelReadout(); };
  updateLevelReadout();

  setTooltip("Noise channel. Same envelope, tremolo, velocity, keyboard and "
             "aftertouch as a partial, and a string it can strike, tuned to "
             "the note.");
}

void NoiseStrip::setUpKnob(juce::Slider &s, const char *suffix,
                           juce::Colour fill, const juce::String &tooltip,
                           const char *title) {
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  s.setPopupDisplayEnabled(true, true, &popupHost);
  s.setTooltip(tooltip);

  // See ChannelStrip::setUpKnob. The name comes from the strips' table, and
  // the tooltip becomes the description.
  const auto role = roleForSuffix(suffix);
  s.setTitle(title != nullptr         ? juce::String(title)
             : role != Role::NumRoles ? "Noise " + juce::String(roleLabel(role))
                                      : "Noise colour");
  s.setDescription(tooltip);
  addAndMakeVisible(s);

  const auto id = params::noiseParamId(suffix);
  sliderAttachments.push_back(std::make_unique<SliderAttachment>(apvts, id, s));
  learn::tag(s, id);

  if (auto *p = apvts.getParameter(id))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));
}

void NoiseStrip::setDrawGlow(bool on) {
  if (on == drawGlow)
    return;

  drawGlow = on;

  volume.getProperties().set("linkGlow", on ? 1.0 : 0.0);
  volume.getProperties().set("glowAccent", on);
  volume.repaint();
}

void NoiseStrip::mouseDown(const juce::MouseEvent &e) {
  // A menu takes the pointer away, so let go of the hover first. See
  // ChannelStrip::mouseDown.
  if (e.mods.isPopupMenu()) {
    clearHover();

    // The learn menu, without the LINK settings.
    const auto id = learn::parameterIdAt(e.originalComponent);

    if (id.isNotEmpty() && onLearnRequested != nullptr)
      onLearnRequested(id);

    return;
  }

  // The rules between sections fold them here too. The same click arrives
  // twice, since the strip listens deeply, and only its time tells them apart.
  // See ChannelStrip::mouseDown.
  const bool echoOfTheSameClick = e.eventTime == lastClick;
  lastClick = e.eventTime;

  if (e.originalComponent != this || echoOfTheSameClick ||
      onSectionToggled == nullptr)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());

  if (section != Section::NumSections)
    onSectionToggled(section);
}

// Only a real move or entry ends a suppression. See clearHover.
void NoiseStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

void NoiseStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  // The same ground every other channel stands on.
  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  // The same row highlight the numbered channels draw.
  if (rowShowsHighlight(highlighted) && !isHeadingRow(highlighted) &&
      !ownRow(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

  // No rule across the pitch modulation block, which holds the string. The
  // string's knobs carry their own names.
  static const char *const names[] = {"string", "decay", "damp", "point"};

  g.setColour(colours::textDim);
  g.setFont(makeFont(kStringCaptionFont));

  for (size_t i = 0; i < stringCaptions.size(); ++i)
    if (!stringCaptions[i].isEmpty())
      g.drawText(names[i], stringCaptions[i], juce::Justification::centred,
                 false);
}

void NoiseStrip::updateLevelReadout() {
  // Dark while another channel's solo silences this one, as a channel's is.
  levelReadout.setReading(levelReading((float)volume.getValue()), !silenced);
}

void NoiseStrip::silenceChanged() { updateLevelReadout(); }

bool NoiseStrip::isOwnRow(Row r) const { return ownRow(r); }

void NoiseStrip::setActivity(float envelope, float tremolo, float velGain,
                             float keyGain, float pressure,
                             juce::Array<juce::Rectangle<int>> &into) {
  const auto refresh = [&into](juce::Component &lamp, bool moved) {
    if (moved)
      into.add(lamp.getBounds());
  };

  const auto level = std::abs(envelope);
  const auto afterKeyOff = envelope < 0.0f;

  refresh(envLamp, envLamp.push(afterKeyOff ? 0.0f : level));
  refresh(keyOffLamp, keyOffLamp.push(afterKeyOff ? level : 0.0f));
  refresh(tremoloLamp, tremoloLamp.push(level > 0.0f ? tremolo : 0.0f));

  // One for each row, as on a numbered channel. See ChannelStrip::setActivity.
  const auto on = level > 0.0f;

  refresh(outputLamp, outputLamp.push({on ? velGain : 0.0f, on ? keyGain : 0.0f,
                                       on ? pressure : 0.0f}));
}

void NoiseStrip::paintHeaderBand(juce::Graphics &g) {
  // See ChannelStrip::paintHeaderBand. The background is drawn for the whole
  // column and clipped to the cap, so the two line up.
  const auto bounds = getLocalBounds();
  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  const auto header = rows[rowIndex(Row::Header)];

  // A noise trace where a numbered channel has its number, as tall and as wide
  // as a two-digit number in the same font and in the same place, so it lines
  // up with the numbers across the strips.
  juce::GlyphArrangement digits;
  digits.addFittedText(makeFont(14.0f, true), "88", (float)header.getX(),
                       (float)header.getY(), (float)header.getWidth(),
                       (float)header.getHeight(), juce::Justification::centred,
                       1);
  // The outline's bounds rather than the glyphs', which are the font's full
  // line height. Inset by half the stroke, so the line stays within the ink.
  juce::Path outline;
  digits.createPath(outline);

  constexpr float stroke = 1.6f;
  const auto area = outline.getBounds().reduced(0.0f, stroke * 0.5f);

  // A fixed sequence, so the trace stays the same on every repaint.
  static constexpr float kTrace[] = {0.1f,  -0.5f, 0.75f,  -0.2f, 0.4f,
                                     -1.0f, 0.9f,  -0.35f, 0.2f,  -0.7f,
                                     1.0f,  -0.4f, 0.55f,  -0.1f};
  constexpr int steps = (int)std::size(kTrace) - 1;

  juce::Path trace;

  for (int i = 0; i <= steps; ++i) {
    const juce::Point<float> p(
        area.getX() + area.getWidth() * (float)i / (float)steps,
        area.getCentreY() - kTrace[i] * area.getHeight() * 0.5f);

    if (i == 0)
      trace.startNewSubPath(p);
    else
      trace.lineTo(p);
  }

  // Accent when hovered, the same as a numbered channel. See ChannelStrip.
  g.setColour(hovered ? colours::accent : colours::text);
  g.strokePath(trace, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void NoiseStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

  // Places a control in its row, shrunk by `shrink` pixels, or hides it.
  const auto placeRow = [&](juce::Component &c, Row r, int shrink) {
    // Hidden rather than left at zero height, which would still take the
    // mouse. A row is empty when folded or scrolled out of the band.
    if (rows[rowIndex(r)].isEmpty()) {
      c.setVisible(false);
      return;
    }

    c.setVisible(true);
    c.setBounds(rows[rowIndex(r)].reduced(shrink));
  };

  colourKnob.setBounds(rows[rowIndex(Row::TuneKnob)]);
  colourReadout.setBounds(rows[rowIndex(Row::TuneText)]);

  placeRow(strike, Row::Strike, 1);
  placeRow(delay, Row::Delay, 1);
  placeRow(attack, Row::Attack, 1);
  placeRow(decay, Row::Decay, 1);
  placeRow(sustain, Row::Sustain, 1);
  placeRow(swell, Row::Swell, 1);
  placeRow(offLevel, Row::OffLevel, 1);
  placeRow(release, Row::Release, 1);
  placeRow(amRate, Row::AmRate, 1);
  placeRow(amShape, Row::AmShape, 0);
  placeRow(amDepth, Row::AmDepth, 1);
  placeRow(velocity, Row::Velocity, 1);
  placeRow(keyboard, Row::Keyboard, 1);
  placeRow(aftertouch, Row::Aftertouch, 1);
  placeRow(pan, Row::Pan, 1);

  // The string's four knobs share the pitch modulation block evenly, each with
  // its name under it, rather than lining up with the partials' rows.
  {
    juce::Slider *knobs[] = {&stringAmount, &stringDecay, &stringDamp,
                             &stringPoint};

    const auto top = rows[rowIndex(Row::PitchModHeading)].getY();
    const auto bottom = rows[rowIndex(Row::EnvHeading)].getY();
    const auto bandTop = rows[rowIndex(Row::Header)].getBottom();
    const bool folded = rowIsCollapsed(Row::PmRate, collapsed);
    const auto cell = (bottom - top) / 4;

    for (size_t i = 0; i < 4; ++i) {
      auto r = juce::Rectangle<int>(
          rows[rowIndex(Row::PmRate)].getX(), top + (int)i * cell,
          rows[rowIndex(Row::PmRate)].getWidth(), cell);

      if (folded || r.getBottom() <= bandTop || r.getY() >= getHeight()) {
        knobs[i]->setVisible(false);
        stringCaptions[i] = {};
        continue;
      }

      // The size a knob in a row is, centred in its share of the block.
      stringCaptions[i] = r.removeFromBottom(kStringCaptionHeight);
      knobs[i]->setVisible(true);
      knobs[i]->setBounds(r.withSizeKeepingCentre(r.getWidth() - 2,
                                                  rowHeight(Row::PmRate) - 2));
    }
  }

  // Down to the foot of the header, whose rectangle already includes the top
  // padding.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[rowIndex(Row::Header)].getBottom()));
  headerCap.toFront(false);

  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));

  {
    meter.setBackdrop(backdropAt(meter.getY()), backdropAt(meter.getBottom()));

    const auto place = [&](ActivityLamp &lamp, Row row) {
      const auto r = rows[rowIndex(row)];
      lamp.setBounds(r);
      lamp.setBackdrop(backdropAt(r.getCentreY()));
    };

    place(envLamp, Row::EnvHeading);
    place(keyOffLamp, Row::KeyOffHeading);
    place(tremoloLamp, Row::AmpModHeading);
    // One slot for the three rows under it, as on a numbered channel.
    outputLamp.setBounds(rows[rowIndex(Row::OutputHeading)]);
    outputLamp.setBackdrop(backdropAt(outputLamp.getBounds().getCentreY()));
  }
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  // Touching, as one moulded block. See ChannelStrip.
  auto ms = rows[rowIndex(Row::MuteSolo)].reduced(1);
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2));
  soloButton.setBounds(ms);
}

void NoiseStrip::mouseWheelMove(const juce::MouseEvent &e,
                                const juce::MouseWheelDetails &wheel) {
  if (e.originalComponent == this)
    juce::Component::mouseWheelMove(e, wheel);
}

} // namespace ovt::ui
