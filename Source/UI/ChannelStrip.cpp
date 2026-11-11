#include "ChannelStrip.h"
#include "LearnMenu.h"

#include <cmath>

#include "../PluginParameters.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
inline size_t rowIndex(Row r) { return (size_t)r; }
} // namespace

// =============================================================================

void SplitLamp::setBackdrop(juce::Colour behind) {
  backdrop = behind;
  setOpaque(true);
  repaint();
}

bool SplitLamp::push(const std::array<float, kParts> &brightness) {
  bool moved = false;

  for (size_t i = 0; i < steps.size(); ++i) {
    const auto exact =
        juce::jlimit(0.0f, 1.0f, brightness[i]) * (float)ActivityLamp::kSteps;
    const auto next =
        juce::jlimit(0, ActivityLamp::kSteps, (int)std::lround(exact));

    moved |= next != steps[i];
    steps[i] = next;
  }

  return moved;
}

void SplitLamp::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  if (isOpaque()) {
    g.setColour(backdrop);
    g.fillRect(bounds);
  }

  // The same slot as the needle's, the same height as a round lamp, and
  // standing in for the rule in the same way.
  const auto midY = std::floor(bounds.getCentreY());
  const auto height = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);
  const auto corner = height * 0.35f;
  const auto slot =
      juce::Rectangle<float>(bounds.getWidth() - 2.0f * kSlotInset, height)
          .withCentre({bounds.getCentreX(), midY + 0.5f});

  // Where each part starts and ends, whole pixels so the gaps stay sharp. A
  // pixel between parts, and the odd pixels to the middle one.
  constexpr float kGap = 1.0f;
  const auto inner = slot.getWidth() - kGap * (float)(kParts - 1);
  const auto side = std::floor(inner / (float)kParts);
  const float widths[kParts] = {side, inner - 2.0f * side, side};

  std::array<juce::Rectangle<float>, kParts> parts;
  {
    auto x = slot.getX();
    for (size_t i = 0; i < parts.size(); ++i) {
      parts[i] = {x, slot.getY(), widths[i], slot.getHeight()};
      x += widths[i] + kGap;
    }
  }

  // The glow under every lit part, all of it inside this component, so it is
  // never cut off where one lamp's bounds would end.
  for (size_t i = 0; i < parts.size(); ++i) {
    const auto lit = (float)steps[i] / (float)ActivityLamp::kSteps;

    if (lit <= 0.02f)
      continue;

    g.setColour(colour.withAlpha(0.10f * lit));
    g.fillRoundedRectangle(parts[i].expanded(height * 0.30f), corner * 2.0f);

    g.setColour(colour.withAlpha(0.12f * lit));
    g.fillRoundedRectangle(parts[i].expanded(height * 0.14f), corner * 1.5f);
  }

  paintRecess(g, slot, corner);

  // The parts, clipped to the slot so the outer two take its rounded ends.
  juce::Path opening;
  opening.addRoundedRectangle(slot, corner);

  juce::Graphics::ScopedSaveState keep(g);
  g.reduceClipRegion(opening);

  std::array<juce::Colour, kParts> fills;

  for (size_t i = 0; i < parts.size(); ++i) {
    const auto lit = (float)steps[i] / (float)ActivityLamp::kSteps;

    fills[i] = isOpaque()
                   ? backdrop.interpolatedWith(colour, 0.16f + 0.84f * lit)
                   : colour.withAlpha(0.16f + 0.84f * lit);
    g.setColour(fills[i]);
    g.fillRect(parts[i]);
  }

  // Each gap is the two parts beside it mixed and taken a little darker, so
  // it reads as a seam in one lamp rather than a black bar, between two lit
  // parts as well as two dark ones.
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    g.setColour(fills[i].interpolatedWith(fills[i + 1], 0.5f).darker(0.6f));
    g.fillRect(parts[i].getRight(), slot.getY(), kGap, slot.getHeight());
  }
}

void ActivityNeedle::setBackdrop(juce::Colour behind) {
  backdrop = behind;
  setOpaque(true);
  repaint();
}

bool ActivityNeedle::push(float position) {
  // Parked rather than centred when there is nothing to show, so an idle
  // partial does not look in tune.
  if (position <= -2.0f) {
    if (column < 0)
      return false;

    column = -1;
    return true;
  }

  const auto usable = juce::jmax(1, getWidth() - 2 * kSlotInset - 3);
  const auto at =
      kSlotInset + 1 +
      (int)std::lround(0.5 * (1.0 + juce::jlimit(-1.0f, 1.0f, position)) *
                       (double)usable);

  if (at == column)
    return false;

  column = at;
  return true;
}

void ActivityNeedle::showCentre(bool show) {
  if (show == centreMark)
    return;

  centreMark = show;
  repaint();
}

void ActivityNeedle::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  if (isOpaque()) {
    g.setColour(backdrop);
    g.fillRect(bounds);
  }

  const auto midY = std::floor(bounds.getCentreY());
  const auto height = juce::jmin(bounds.getHeight() - 2.0f, 7.0f);

  // The travel it reads against, in the channel colour held right down. It
  // doubles as the rule dividing the groups.
  const auto track = juce::Rectangle<float>(
                         bounds.getWidth() - 2.0f * (float)kSlotInset, height)
                         .withCentre({bounds.getCentreX(), midY + 0.5f});

  // A slot milled across the strip. The inset leaves room for its lip.
  paintRecess(g, track, height * 0.35f);

  // Blended against the backdrop, so it covers the shadow under the slot.
  g.setColour(isOpaque() ? backdrop.interpolatedWith(colour, 0.16f)
                         : colour.withAlpha(0.16f));
  g.fillRoundedRectangle(track, height * 0.35f);

  // Centre, so sharp and flat mean something when the needle is near it.
  if (centreMark) {
    g.setColour(colour.withAlpha(0.34f));
    g.fillRect(bounds.getCentreX() - 0.5f, track.getY() + 1.0f, 1.0f,
               track.getHeight() - 2.0f);
  }

  if (column < 0)
    return;

  g.setColour(colour);
  g.fillRect((float)column - 1.0f, track.getY(), 2.0f, track.getHeight());
}

// =============================================================================

ChannelStrip::ChannelStrip(juce::AudioProcessorValueTreeState &state,
                           LinkTarget &linkTarget, HoverTarget &hoverTarget,
                           juce::Component &popupParent, int index0)
    : StripColumn(hoverTarget), apvts(state), link(linkTarget),
      popupHost(popupParent), index(index0), info(harmonic(index0)),
      colour(intervalColour(harmonic(index0).pitchClass)),
      pmShape(state, params::oscParamId(params::pmShapeSuffix, index0),
              params::pmShapeSuffix, params::kPitchShapes,
              params::pitchShapeNames(), index0, true),
      amShape(state, params::oscParamId(params::amShapeSuffix, index0),
              params::amShapeSuffix, params::kAmpShapes,
              params::ampShapeNames(), index0),
      muteButton(state, "M", params::oscParamId(params::muteSuffix, index0),
                 params::muteSuffix),
      soloButton(state, "S", params::oscParamId(params::soloSuffix, index0),
                 params::soloSuffix),
      meter(colour), pitchLamp(colour), envLamp(colour), keyOffLamp(colour),
      tremoloLamp(colour), outputLamp(colour) {
  // The background fills the whole strip, so nothing behind it needs
  // painting.
  setOpaque(true);

  // Deep listener, so a pointer resting on a knob is reported by the strip that
  // owns it rather than being swallowed by the control.
  addMouseListener(this, true);

  for (juce::Component *lamp :
       {(juce::Component *)&pitchLamp, (juce::Component *)&envLamp,
        (juce::Component *)&keyOffLamp, (juce::Component *)&tremoloLamp,
        (juce::Component *)&outputLamp})
    addAndMakeVisible(*lamp);

  // The strip decides the pointer for everything on it, which is how the LINK
  // tool shows itself. The mute and solo buttons keep their own.
  setMouseCursor(juce::MouseCursor::ParentCursor);

  // Every knob carries the strip's colour at full strength.
  for (auto *shape : {&pmShape, &amShape})
    addAndMakeVisible(*shape);

  pmShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " pitch mod shape");
  amShape.setTitle("Harmonic " + juce::String(info.harmonic) +
                   " amp mod shape");

  setUpKnob(tune, Role::Tune, colour);
  setUpKnob(pmRate, Role::PmRate, colour);
  setUpKnob(pmDepth, Role::PmDepth, colour);
  setUpKnob(drift, Role::Drift, colour);
  setUpKnob(glide, Role::Glide, colour);
  setUpKnob(strike, Role::Strike, colour);

  // STRIKE's popup shows the two times it produces on this strip rather than
  // a percentage. Read when the popup opens, so it follows ATTACK and DELAY.
  strike.textFromValueFunction = [this](double amount) {
    return params::strikeRangeText((float)amount, (float)delay.getValue(),
                                   (float)attack.getValue());
  };

  setUpKnob(delay, Role::Delay, colour);
  setUpKnob(attack, Role::Attack, colour);
  setUpKnob(decay, Role::Decay, colour);
  setUpKnob(sustain, Role::Sustain, colour);
  setUpKnob(swell, Role::Swell, colour);
  setUpKnob(offLevel, Role::OffLevel, colour);
  setUpKnob(release, Role::Release, colour);
  setUpKnob(amRate, Role::AmRate, colour);
  setUpKnob(amDepth, Role::AmDepth, colour);

  // A partial as the source sets its own rate, so the knob greys out.
  amShape.onChange = [this] {
    amRate.setEnabled(amShape.selectedPartial() < 0);
  };
  amShape.onChange();

  pmShape.onChange = [this] {
    pmRate.setEnabled(pmShape.selectedPartial() < 0);
  };
  pmShape.onChange();

  // With a partial moving the phase, DEPTH is the modulation index, which is
  // what its popup shows. The host still shows the cents it stores.
  pmDepth.textFromValueFunction =
      [this, cents = pmDepth.textFromValueFunction](double value) {
        if (pmShape.selectedPartial() >= 0)
          return "Index " + juce::String(value / (double)kCentsPerIndex, 2);

        return cents != nullptr ? cents(value) : juce::String(value);
      };
  setUpKnob(velocity, Role::Velocity, colour);
  setUpKnob(keyboard, Role::Keyboard, colour);
  setUpKnob(aftertouch, Role::Aftertouch, colour);

  setUpKnob(pan, Role::Pan, colour);

  // These all run either side of zero, so their arcs read out from twelve
  // o'clock rather than filling from the left.
  strike.getProperties().set("bipolar", true);
  velocity.getProperties().set("bipolar", true);
  keyboard.getProperties().set("bipolar", true);
  aftertouch.getProperties().set("bipolar", true);
  pan.getProperties().set("bipolar", true);
  setUpFader(volume, Role::Volume, colour);

  // The colour of the lamp behind the cap.
  muteButton.setColour(juce::TextButton::textColourOnId, colours::muteOn);
  soloButton.setColour(juce::TextButton::textColourOnId, colours::soloOn);
  muteButton.setTooltip("Mute harmonic " + juce::String(info.harmonic) + ". " +
                        muteButton.onlyThisHint());
  soloButton.setTooltip("Solo harmonic " + juce::String(info.harmonic) + ". " +
                        soloButton.onlyThisHint());
  muteButton.setTitle("Harmonic " + juce::String(info.harmonic) + " mute");
  soloButton.setTitle("Harmonic " + juce::String(info.harmonic) + " solo");
  // One moulding around the pair, each half lighting on its own.
  OvertoniumLookAndFeel::gangLamps(muteButton, soloButton);

  addAndMakeVisible(muteButton);
  addAndMakeVisible(soloButton);

  muteAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::muteSuffix, index), muteButton);
  soloAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::oscParamId(params::soloSuffix, index), soloButton);

  learn::tag(muteButton, params::oscParamId(params::muteSuffix, index));
  learn::tag(soloButton, params::oscParamId(params::soloSuffix, index));

  // Readouts rather than controls, so they take no clicks of their own and
  // the strip underneath goes on reporting which row the pointer is over.
  for (auto *d : {&tuneReadout, &levelReadout}) {
    d->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*d);
  }

  addAndMakeVisible(meter);

  headerCap.onPaint = [this](juce::Graphics &g) { paintHeaderBand(g); };
  addAndMakeVisible(headerCap);
  meter.toBack(); // the fader cap has to draw over it

  updateTuneReadout();
  updateLevelReadout();

  // An octave is the same in equal temperament and just intonation, so on six
  // channels TUNE does nothing to the sound, and the readout says so. The knob
  // stays live, since nought is still a value and LINK drags cross it.
  const auto cents = juce::String(info.jiCents, 1);

  const auto tuning =
      exactly(info.jiCents, 0.0)
          ? juce::String("An octave is the same in both, so TUNE has nothing "
                         "to move here. STRETCH moves it.")
          : juce::String("Just intonation is ") +
                (info.jiCents >= 0.0 ? "+" : "") + cents + " cents from that";

  setTooltip("Harmonic " + juce::String(info.harmonic) + ", " +
             intervalName(info.pitchClass) + "\n" +
             juce::String(info.etSemitones) +
             " semitones above the played note\n" + tuning);
}

void ChannelStrip::setUpKnob(LinkableSlider &s, Role role, juce::Colour fill) {
  // The name a screen reader announces.
  s.setTitle("Harmonic " + juce::String(info.harmonic) + " " + roleLabel(role));

  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::rotarySliderFillColourId, fill);
  baseColour[(size_t)role] = fill;
  s.setPopupDisplayEnabled(true, true, &popupHost);
  addAndMakeVisible(s);

  wireUp(s, role);
}

void ChannelStrip::setUpFader(LinkableSlider &s, Role role, juce::Colour fill) {
  s.setTitle("Harmonic " + juce::String(info.harmonic) + " " + roleLabel(role));

  s.setMouseCursor(juce::MouseCursor::ParentCursor);
  s.setSliderStyle(juce::Slider::LinearVertical);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  s.setColour(juce::Slider::trackColourId, fill);
  baseColour[(size_t)role] = fill;
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

  learn::tag(s, paramId);

  // Double-click restores the parameter's own default rather than the range
  // minimum.
  if (auto *p = apvts.getParameter(paramId))
    s.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  s.onUserDragStart = [this, role] { link.linkDragStarted(role, index); };
  s.onUserDragEnd = [this, role] { link.linkDragEnded(role, index); };

  // Only the faders take a drawn drag.
  if (role == Role::Volume) {
    s.onDrawStart = [this](const juce::MouseEvent &e) {
      return link.drawStarted(e.getScreenPosition());
    };

    s.onDrawMove = [this](const juce::MouseEvent &e) {
      link.drawMovedTo(e.getScreenPosition());
    };

    s.onDrawEnd = [this] { link.drawEnded(); };
  }

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

  if (std::abs(info.jiCents) < 0.05) {
    // The octaves land in the same place either way.
    text = "0.0";
  } else if (blend <= 0.0005) {
    text = "Et";
  } else {
    // Only the minus is shown, since seven bars cannot draw a readable plus. A
    // sharp partial is the one with no sign.
    text =
        std::abs(cents) < 0.05 ? juce::String("0.0") : juce::String(cents, 1);
  }

  // A channel silenced by another's solo has nothing sounding to report, so
  // its cells go dark.
  tuneReadout.setReading(text, !silenced);
}

void ChannelStrip::updateLevelReadout() {
  levelReadout.setReading(levelReading((float)volume.getValue()), !silenced);
}

void ChannelStrip::silenceChanged() {
  updateTuneReadout();
  updateLevelReadout();
}

void ChannelStrip::mouseDown(const juce::MouseEvent &e) {
  // The strip listens to its children, so a click on its own background
  // arrives twice, with everything the same but nothing to tell them apart
  // except the event time. Both actions below are toggles, so the echo is
  // dropped.
  const bool echoOfTheSameClick = e.eventTime == lastClick;
  lastClick = e.eventTime;

  // The command key on the channel's number picks the channel, for the
  // Selected scope. Only on the number, since on a knob that key asks for fine
  // movement.
  if (!e.mods.isPopupMenu() && e.mods.isCommandDown() &&
      headerCap.getBounds().contains(
          e.getEventRelativeTo(this).getPosition())) {
    if (!echoOfTheSameClick) {
      selecting = true;
      link.selectionStarted(index, e.getScreenPosition());
    }

    return;
  }

  if (!e.mods.isPopupMenu()) {
    foldSectionUnder(e, echoOfTheSameClick);
    return;
  }

  // The menu about to open is modal and this strip is never told the pointer
  // left, so the hover is let go now.
  clearHover();

  // A right-click on a mute or solo button opens that button's own menu
  // only. originalComponent still names where the click landed after any
  // retargeting.
  if (dynamic_cast<const MuteSoloButton *>(e.originalComponent) != nullptr)
    return;

  // One menu per click, not two stacked.
  if (echoOfTheSameClick)
    return;

  link.showLinkMenu(learn::parameterIdAt(e.originalComponent));
}

/// Folds the section whose rule the click landed on, if it landed on one.
///
/// Uses the same hit test as the gutter headings. See RowGutter::mouseDown.
void ChannelStrip::foldSectionUnder(const juce::MouseEvent &e, bool echo) {
  // Only a click on the strip's own background. The lamps on the rules let
  // clicks through.
  if (e.originalComponent != this || echo || onSectionToggled == nullptr)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());

  if (section != Section::NumSections)
    onSectionToggled(section);
}

// Only a pointer move ends a suppression, not an exit. See clearHover.
void ChannelStrip::mouseExit(const juce::MouseEvent &e) { reportHover(e); }

/// The row the pointer is on, counting the rules between sections as rows.
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
  case Role::Glide:
    return &glide;
  case Role::Strike:
    return &strike;
  case Role::Delay:
    return &delay;
  case Role::Attack:
    return &attack;
  case Role::Decay:
    return &decay;
  case Role::Sustain:
    return &sustain;
  case Role::Swell:
    return &swell;
  case Role::OffLevel:
    return &offLevel;
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
  case Role::Keyboard:
    return &keyboard;

  case Role::NumRoles:
  default:
    jassertfalse;
    return nullptr;
  }
}

void ChannelStrip::setMacroTint(Role role, juce::Colour tint, float result) {
  const auto at = (size_t)role;

  if (at >= macroTint.size())
    return;

  auto *slider = sliderForRole(role);

  if (slider == nullptr)
    return;

  const bool sameColour = macroTint[at] == tint;
  const bool sameResult = std::abs(macroResult[at] - result) < 1.0e-4f;

  if (sameColour && sameResult)
    return;

  macroTint[at] = tint;
  macroResult[at] = result;

  // Read by the look and feel for the ring. See drawRotarySlider.
  if (tint.isTransparent()) {
    slider->getProperties().remove("macroColour");
    slider->getProperties().remove("macroResult");
  } else {
    slider->getProperties().set("macroColour", (int)tint.getARGB());
    slider->getProperties().set("macroResult", (double)result);
  }

  slider->repaint();
}

void ChannelStrip::setLinkGlow(Role role, float amount, bool accent) {
  amount = juce::jlimit(0.0f, 1.0f, amount);

  if (role == glowRole && accent == glowAccent &&
      std::abs(amount - glowAmount) < 0.004f)
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
  glowAccent = accent;

  if (auto *s = sliderForRole(glowRole)) {
    s->getProperties().set("linkGlow", (double)glowAmount);
    s->getProperties().set("glowAccent", glowAccent);
    s->repaint();
  }
}

void ChannelStrip::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds();

  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  if (rowShowsHighlight(highlighted) && !isHeadingRow(highlighted))
    paintRowHighlight(g, rows[rowIndex(highlighted)]);

  // The section rules are drawn by the lamps on them. See ActivityLamp::paint.
}

void ChannelStrip::paintHeaderBand(juce::Graphics &g) {
  // The column's own background first, so the cap is opaque. Drawn for the
  // whole strip and clipped to the cap, so it matches the strip exactly.
  const auto bounds = getLocalBounds();
  paintChannelBackground(*this, g, bounds, colours::channel);

  const auto rows =
      layoutRows(bounds.reduced(kStripPadX, kStripPadY), collapsed, scroll);

  const auto header = rows[rowIndex(Row::Header)];

  // Accent when the pointer is on this channel, and mixed towards the accent
  // by how much of a LINK drag the channel would take.
  g.setColour(hovered
                  ? colours::accent
                  : colours::text.interpolatedWith(colours::accent, linkReach));
  g.setFont(makeFont(14.0f, true));
  g.drawText(juce::String(info.harmonic), header, juce::Justification::centred,
             false);

  // A bar under the number for a selected channel.
  if (selected) {
    g.setColour(colours::accent);
    g.fillRect(juce::Rectangle<int>(14, 2).withCentre(
        {header.getCentreX(), header.getBottom() - 2}));
  }
}

void ChannelStrip::mouseDrag(const juce::MouseEvent &e) {
  if (selecting)
    link.selectionMovedTo(e.getScreenPosition());
}

void ChannelStrip::mouseUp(const juce::MouseEvent &) {
  if (!selecting)
    return;

  selecting = false;
  link.selectionEnded();
}

void ChannelStrip::setSelected(bool on) {
  if (selected == on)
    return;

  selected = on;
  headerCap.repaint();
}

void ChannelStrip::setLinkReach(float amount) {
  // Quantised to eight steps, so a sliding curve does not repaint every
  // header every frame.
  const auto step = std::round(juce::jlimit(0.0f, 1.0f, amount) * 8.0f) / 8.0f;

  if (std::abs(step - linkReach) < 0.001f)
    return;

  linkReach = step;

  // The header only, which the cap draws.
  headerCap.repaint();
}

void ChannelStrip::resized() {
  const auto rows = layoutRows(getLocalBounds().reduced(kStripPadX, kStripPadY),
                               collapsed, scroll);

  const auto placeRow = [&](juce::Component &c, Row r, int shrink) {
    // Hidden rather than left at zero height, since a knob with no height
    // still takes the mouse. A row is empty when folded or scrolled out.
    if (rows[rowIndex(r)].isEmpty()) {
      c.setVisible(false);
      return;
    }

    c.setVisible(true);
    c.setBounds(rows[rowIndex(r)].reduced(shrink));
  };

  placeRow(tune, Row::TuneKnob, 0);
  placeRow(tuneReadout, Row::TuneText, 0);
  placeRow(pmRate, Row::PmRate, 1);
  placeRow(pmShape, Row::PmShape, 0);
  placeRow(pmDepth, Row::PmDepth, 1);
  placeRow(drift, Row::Drift, 1);
  placeRow(glide, Row::Glide, 1);
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
  // Meter and fader share the same rectangle. The meter draws the track and
  // the fader draws only its cap on top, so the output fills the fader itself.
  const auto faderRow = rows[rowIndex(Row::Fader)];
  meter.setBounds(faderRow.reduced(2, 1));

  // The strip's background is a gradient, so the meter is told the colours
  // at its own edges.
  meter.setBackdrop(backdropAt(meter.getY()), backdropAt(meter.getBottom()));
  volume.setBounds(faderRow.reduced(2, 1));
  levelReadout.setBounds(rows[rowIndex(Row::FaderText)]);

  // Over the header and anything scrolled under it, brought to the front
  // since controls are added after it. Down to the foot of the header only,
  // since the row's rectangle already carries the top padding.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[rowIndex(Row::Header)].getBottom()));
  headerCap.toFront(false);

  // Touching, as one ganged block, inset once around the outside.
  auto ms = rows[rowIndex(Row::MuteSolo)].reduced(1);
  muteButton.setBounds(ms.removeFromLeft(ms.getWidth() / 2));
  soloButton.setBounds(ms);

  // The lamps stand on the rules that divide the strip into groups, each at
  // the head of the group it reports on.
  {
    const auto place = [&](juce::Component &lamp, Row row) {
      const auto r = rows[rowIndex(row)];
      lamp.setBounds(r);
      return backdropAt(r.getCentreY());
    };

    pitchLamp.setBackdrop(place(pitchLamp, Row::PitchModHeading));
    envLamp.setBackdrop(place(envLamp, Row::EnvHeading));
    keyOffLamp.setBackdrop(place(keyOffLamp, Row::KeyOffHeading));
    tremoloLamp.setBackdrop(place(tremoloLamp, Row::AmpModHeading));
    // One slot for the three rows under it.
    outputLamp.setBackdrop(place(outputLamp, Row::OutputHeading));
  }
}

float ChannelStrip::needlePosition(float cents) {
  // A square root on a fixed scale, so a shallow setting of a few cents
  // still moves the needle a visible distance and two channels can be
  // compared by eye.
  const auto span = juce::jmax(1.0f, params::kPitchNeedleFullScaleCents);
  const auto reach = juce::jlimit(-1.0f, 1.0f, cents / span);

  return reach < 0.0f ? -std::sqrt(-reach) : std::sqrt(reach);
}

void ChannelStrip::setActivity(float envelope, float tremolo, float pitch,
                               float velGain, float keyGain, float pressure,
                               juce::Array<juce::Rectangle<int>> &into) {
  const auto refresh = [&into](juce::Component &lamp, bool moved) {
    if (moved)
      into.add(lamp.getBounds());
  };

  // One lamp or the other by the sign. The other gets zero.
  const auto level = std::abs(envelope);
  const auto afterKeyOff = envelope < 0.0f;

  refresh(envLamp, envLamp.push(afterKeyOff ? 0.0f : level));
  refresh(keyOffLamp, keyOffLamp.push(afterKeyOff ? level : 0.0f));

  // The tremolo lamp is gated on the envelope, so it stops when the note does
  // rather than going on pulsing over silence.
  refresh(tremoloLamp, tremoloLamp.push(level > 0.0f ? tremolo : 0.0f));

  // Parked only when nothing is sounding. kParked is anything past the ends
  // of the travel.
  constexpr float kParked = -3.0f;

  refresh(pitchLamp,
          pitchLamp.push(level <= 0.0f ? kParked : needlePosition(pitch)));

  // One part of the output lamp for each of the three rows under this
  // heading. The velocity and keyboard parts show the level their rows leave,
  // so they are full at rest. The pressure part rests dark and rises with
  // aftertouch. All are gated on the envelope.
  const auto on = level > 0.0f;

  refresh(outputLamp, outputLamp.push({on ? velGain : 0.0f, on ? keyGain : 0.0f,
                                       on ? pressure : 0.0f}));
}

void ChannelStrip::mouseWheelMove(const juce::MouseEvent &e,
                                  const juce::MouseWheelDetails &wheel) {
  // Only a wheel that began on the strip itself. One over a knob belongs to
  // the knob, and this is a deep listener so it hears both.
  if (e.originalComponent != this)
    return;

  // A sideways gesture, or shift with a wheel, falls through to the viewport
  // to scroll the mixer.
  const bool sideways = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);

  if (!sideways && !e.mods.isShiftDown() && scrollParametersBy(wheel))
    return;

  juce::Component::mouseWheelMove(e, wheel);
}

bool ChannelStrip::scrollParametersBy(const juce::MouseWheelDetails &wheel) {
  return link.scrollParameters(wheelPixels(wheel));
}

} // namespace ovt::ui
