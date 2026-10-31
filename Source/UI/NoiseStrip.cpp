#include "NoiseStrip.h"
#include "LearnMenu.h"

#include <cmath>

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
    : apvts(state), hover(hoverTarget), popupHost(popupParent),
      colour(kNoiseColour),
      amShape(state, params::noiseParamId(params::amShapeSuffix),
              params::amShapeSuffix, params::kAmpShapes,
              params::ampShapeNames()),
      muteButton(state, "M"), soloButton(state, "S"), meter(kNoiseColour),
      envLamp(kNoiseColour), keyOffLamp(kNoiseColour),
      tremoloLamp(kNoiseColour), velocityLamp(kNoiseColour),
      pressureLamp(kNoiseColour) {
  for (auto *lamp :
       {&envLamp, &keyOffLamp, &tremoloLamp, &velocityLamp, &pressureLamp})
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

  strike.getProperties().set("bipolar", true);
  velocity.getProperties().set("bipolar", true);
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
  muteButton.setTooltip("Mute the noise channel");
  soloButton.setTooltip("Solo the noise channel");
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

  setTooltip("Noise channel. Same envelope, tremolo, velocity and aftertouch "
             "as a partial, and a string it can strike, tuned to the note.");
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

  headerCap.onPaint = [this](juce::Graphics &g) { paintHeaderBand(g); };
  addAndMakeVisible(headerCap);

  const auto id = params::noiseParamId(suffix);
  sliderAttachments.push_back(std::make_unique<SliderAttachment>(apvts, id, s));
  learn::tag(s, id);

  if (auto *p = apvts.getParameter(id))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));
}

void NoiseStrip::setCollapsedSections(SectionMask mask) {
  if (mask == collapsed)
    return;

  collapsed = mask;

  if (rowIsCollapsed(highlighted, collapsed))
    highlighted = kNoRow;

  resized();
  repaint();
}

void NoiseStrip::setScroll(int s) {
  if (s == scroll)
    return;

  scroll = s;
  resized();
  repaint();
}

void NoiseStrip::setSilencedByOthers(bool shouldDim) {
  if (silenced == shouldDim)
    return;

  silenced = shouldDim;

  // Greys the controls rather than washing the whole strip, so the mute and
  // solo stay readable.
  std::function<void(juce::Component &)> drain = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // Not the mute and solo pair.
      if (dynamic_cast<MuteSoloButton *>(child) != nullptr)
        continue;

      // Sliders and the waveform displays, which carry colour of their own.
      if (dynamic_cast<juce::Slider *>(child) != nullptr ||
          dynamic_cast<ShapeButton *>(child) != nullptr) {
        child->getProperties().set("unlit", silenced);
        child->repaint();
      }

      drain(*child);
    }
  };

  drain(*this);

  repaint();
}

void NoiseStrip::setDrawGlow(bool on) {
  if (on == drawGlow)
    return;

  drawGlow = on;

  volume.getProperties().set("linkGlow", on ? 1.0 : 0.0);
  volume.getProperties().set("glowAccent", on);
  volume.repaint();
}

void NoiseStrip::drawFaderAt(int y) {
  const auto track = volume.getBounds();

  if (track.getHeight() <= 1)
    return;

  const auto fromTop = juce::jlimit(
      0.0, 1.0, (double)(y - track.getY()) / (double)track.getHeight());

  volume.setValue(volume.proportionOfLengthToValue(1.0 - fromTop),
                  juce::sendNotificationSync);
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

void NoiseStrip::clearHover() {
  // Held until the pointer moves again, since the exit a menu sends still
  // carries a position inside this strip.
  hoverSuppressed = true;

  hover.hoverChanged(-1, kNoRow);

  if (hovered) {
    hovered = false;
    repaint();
  }
}

// Only a real move or entry ends a suppression. See clearHover.
void NoiseStrip::mouseEnter(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);
}

void NoiseStrip::mouseMove(const juce::MouseEvent &e) {
  hoverSuppressed = false;
  reportHover(e);

  // The pointing hand over a rule that folds, as on the gutter's headings.
  if (e.originalComponent == this) {
    const auto rows = layoutRows(
        getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed, scroll);

    setMouseCursor(headingSectionAt(rows, e.getPosition()) !=
                           Section::NumSections
                       ? juce::MouseCursor::PointingHandCursor
                       : juce::MouseCursor::ParentCursor);
  }
}
void NoiseStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

void NoiseStrip::reportHover(const juce::MouseEvent &e) {
  const auto p = e.getEventRelativeTo(this).getPosition();
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto inside = getLocalBounds().contains(p) && !hoverSuppressed;

  // -1 says the pointer is off the harmonic series, so no LINK preview.
  const auto row = inside ? ChannelStrip::rowUnder(rows, p) : kNoRow;
  hover.hoverChanged(-1, ownRow(row) ? kNoRow : row);

  if (inside != hovered) {
    hovered = inside;
    repaint();
  }
}

void NoiseStrip::paintOverChildren(juce::Graphics &g) {
  // A heading's wash goes over the children, since the activity lamps fill
  // their rows with an opaque backdrop.
  if (rowShowsHighlight(highlighted) && isHeadingRow(highlighted) &&
      !ownRow(highlighted)) {
    const auto rows = layoutRows(
        getLocalBounds().reduced(kStripPadX, kStripPadY), collapsed, scroll);

    paintRowHighlight(g, rows[rowIndex(highlighted)]);
  }

  if (hovered)
    paintColumnHighlight(g, getLocalBounds());
}

void NoiseStrip::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

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
  const auto v = (float)volume.getValue();

  // See ChannelStrip::updateLevelReadout.
  if (params::isSilentGain(v))
    return levelReadout.setReading("-inF", true);

  const auto db = params::levelDecibels(v);

  levelReadout.setReading(
      (db >= 0.0f ? "" : "-") + juce::String(std::abs(db), 1), true);
}

void NoiseStrip::setActivity(float envelope, float tremolo, float velGain,
                             float pressure,
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
  refresh(velocityLamp, velocityLamp.push(level > 0.0f ? velGain : 0.0f));
  refresh(pressureLamp, pressureLamp.push(level > 0.0f ? pressure : 0.0f));
}

void NoiseStrip::paintHeaderBand(juce::Graphics &g) {
  // See ChannelStrip::paintHeaderBand. The background is drawn for the whole
  // column and clipped to the cap, so the two line up.
  const auto bounds = getLocalBounds();
  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  auto header = rows[rowIndex(Row::Header)];

  g.setColour(colour);
  g.fillRect(header.removeFromTop(3).reduced(1, 0));

  header.removeFromTop(1);

  // Accent when hovered, the same as a numbered channel. See ChannelStrip.
  g.setColour(hovered ? colours::accent : colours::text);
  g.setFont(makeFont(11.0f, true));
  g.drawText("NZ", header.removeFromTop(14), juce::Justification::centred,
             false);

  g.setColour(colour.withAlpha(0.85f));
  g.setFont(makeFont(9.0f));
  g.drawText("noise", header, juce::Justification::centred, false);
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
    const auto base = colours::channel;
    const auto top = base.brighter(0.10f);
    const auto bottom = base.darker(0.06f);

    const auto at = [&](int y) {
      return top.interpolatedWith(
          bottom, juce::jlimit(0.0f, 1.0f,
                               (float)y / (float)juce::jmax(1, getHeight())));
    };

    meter.setBackdrop(at(meter.getY()), at(meter.getBottom()));

    const auto place = [&](ActivityLamp &lamp, Row row) {
      const auto r = rows[rowIndex(row)];
      lamp.setBounds(r);
      lamp.setBackdrop(at(r.getCentreY()));
    };

    place(envLamp, Row::EnvHeading);
    place(keyOffLamp, Row::KeyOffHeading);
    place(tremoloLamp, Row::AmpModHeading);
    // Two on the one rule, as on a numbered channel. See ChannelStrip.
    {
      auto row = rows[rowIndex(Row::OutputHeading)];
      const auto half = row.removeFromLeft(row.getWidth() / 2);

      velocityLamp.setBounds(half);
      velocityLamp.setBackdrop(at(half.getCentreY()));

      pressureLamp.setBounds(row);
      pressureLamp.setBackdrop(at(row.getCentreY()));
    }
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
