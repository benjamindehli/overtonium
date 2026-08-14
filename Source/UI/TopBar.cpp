#include "TopBar.h"

#include "../PluginParameters.h"
#include "../Presets.h"
#include <limits>

#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
constexpr float kZoomChoices[] = {0.75f, 1.0f, 1.25f, 1.5f};

constexpr int kBarMargin = 12;
constexpr int kBarPadY = 6;
constexpr int kRowHeight = 54;
constexpr int kRowGap = 4;
constexpr int kGroupGap = 8;
constexpr int kTitleWidth = 178;
constexpr int kGroupPad = 7;

/// Minimum width of each group, in the order they are laid out. Only the
/// output group grows, because the meter is the one thing worth more room.
constexpr int kGroupMinWidth[] = {164, 124, 96, 324, 366, 338, 82};
constexpr int kOutputGroupIndex = 5;
constexpr int kGroupCount = 7;

constexpr int kKnobWidth = 42;
constexpr int kFxToggleWidth = 52;
constexpr int kFxToggleGap = 6;

constexpr int kEchoKnobs = 6;
constexpr int kReverbKnobs = 7;

/// How many rows of bar are worth having above a mixer.
constexpr int kMaxComfortableRows = 3;

/// The meter is the only thing worth extra room, but not unlimited extra room.
/// Uncapped it swallows a whole second row and reads as a progress bar.
constexpr int kOutputMaxExtra = 190;

/// ...and the one that can give ground when a row is slightly too tight. A
/// window a few pixels short of fitting one row should narrow the meter rather
/// than wrap, which is what used to strand the view group alone on row two.
constexpr int kOutputMaxSqueeze = 40;

/// The title only occupies the first row, so the second gets the full width.
constexpr int kTitleLead = 10;

int totalGroupWidth(int first, int last) {
  int total = 0;
  for (int g = first; g < last; ++g)
    total += kGroupMinWidth[g] + (g > first ? kGroupGap : 0);

  return total;
}
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

// =============================================================================

void MenuButton::paintButton(juce::Graphics &g, bool highlighted, bool down) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

  auto fill = colours::panelAlt;
  if (down)
    fill = fill.brighter(0.15f);
  else if (highlighted)
    fill = fill.brighter(0.08f);

  g.setGradientFill(juce::ColourGradient(
      fill.brighter(0.12f), bounds.getCentreX(), bounds.getY(),
      fill.darker(0.05f), bounds.getCentreX(), bounds.getBottom(), false));
  g.fillRoundedRectangle(bounds, 4.0f);

  g.setColour(colours::outline);
  g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

  juce::Path chevron;
  const auto cx = bounds.getCentreX();
  const auto cy = bounds.getCentreY();
  chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
  chevron.lineTo(cx, cy + 2.5f);
  chevron.lineTo(cx + 4.0f, cy - 2.0f);

  g.setColour(isEnabled() ? colours::text : colours::textDim);
  g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// =============================================================================

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  LabelledKnob *knobs[] = {&master, &spread};

  for (auto *k : knobs) {
    k->slider.setPopupDisplayEnabled(true, true, &popupParent);
    addAndMakeVisible(*k);
  }

  master.slider.setTooltip("Output level");
  spread.slider.setTooltip("Fans the partials across the stereo field");

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);
  spreadAttachment = std::make_unique<SliderAttachment>(apvts, params::spreadId,
                                                        spread.slider);

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

  // ---- voices ---------------------------------------------------------------
  // Polyphony and bend range are both a short list of whole numbers, so they
  // read better as a menu than as a box and a knob holding open a group.
  voicesButton.setTooltip("How many notes at once, and how far the wheel "
                          "bends. Each note runs 32 sine partials.");
  voicesButton.onClick = [this] { showVoiceMenu(); };
  addAndMakeVisible(voicesButton);

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
              "Gang the strips, so dragging one channel's knob moves the same "
              "knob on the others. Scope picks which ones, curve picks how "
              "the movement is shared out.");
  styleToggle(phaseButton, "PHASE",
              "Reset partial phase on each note for a coherent attack");
  styleToggle(clipButton, "CLIP",
              "Soft-clip the output. Worth leaving on with 32 faders.");

  linkButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  linkButton.onClick = [this] {
    updateLinkEnablement();
    if (onLinkSettingsChanged)
      onLinkSettingsChanged();
  };

  // ---- what LINK reaches, and how it is shared out
  // ---------------------------
  linkMenuButton.setTooltip(
      "Which channels a LINK drag reaches, and how the movement is shared out "
      "between them. The same menu is on a right-click anywhere in the mixer.");
  linkMenuButton.onClick = [this] { showLinkMenu(&linkMenuButton); };
  addAndMakeVisible(linkMenuButton);

  // ---- the master effects
  // ----------------------------------------------------
  styleToggle(echoButton, "ECHO",
              "Tape echo across the whole instrument, before the master fader");
  styleToggle(reverbButton, "REVERB",
              "Reverb across the whole instrument, after the echo");

  echoButton.setColour(juce::TextButton::buttonOnColourId, colours::accent);
  reverbButton.setColour(juce::TextButton::buttonOnColourId, colours::accent);

  echoAttachment =
      std::make_unique<ButtonAttachment>(apvts, params::echoOnId, echoButton);
  reverbAttachment = std::make_unique<ButtonAttachment>(
      apvts, params::reverbOnId, reverbButton);

  addKnob(echoControls, "MIX", params::echoMixId,
          "How much of the output is repeats", popupParent);
  addKnob(echoControls, "TIME", params::echoTimeId,
          "Distance between the heads. Moving it winds the tape rather than "
          "cutting to the new time, so the repeats slide in pitch on the way.",
          popupParent);
  addKnob(echoControls, "FDBK", params::echoFeedbackId,
          "How much of each repeat goes round again", popupParent);
  addKnob(echoControls, "TONE", params::echoToneId,
          "Dark leaves every repeat duller than the last, bright keeps them "
          "close to the original",
          popupParent);
  addKnob(echoControls, "WOW", params::echoWobbleId,
          "Wow and flutter. The motor is never quite steady, so the pitch of "
          "the repeats wanders.",
          popupParent);
  addKnob(echoControls, "SPREAD", params::echoSpreadId,
          "Sends the repeats in on one side and crosses them over on every "
          "pass, so they alternate between the speakers",
          popupParent);

  addKnob(reverbControls, "MIX", params::reverbMixId,
          "How much of the output is reverb", popupParent);
  addKnob(reverbControls, "SIZE", params::reverbSizeId,
          "Room dimensions, from a booth to a hall", popupParent);
  addKnob(reverbControls, "DECAY", params::reverbDecayId,
          "How long the tail takes to fall away", popupParent);
  addKnob(reverbControls, "DAMP", params::reverbDampId,
          "How quickly the top end dies out of the tail", popupParent);
  addKnob(reverbControls, "LO CUT", params::reverbLowCutId,
          "Keeps the fundamental out of the tail, which matters on an "
          "instrument built from 32 partials",
          popupParent);
  addKnob(reverbControls, "PRE", params::reverbPreDelayId,
          "Silence between the note and its reverb. A little of it keeps the "
          "attack clear of the wash.",
          popupParent);
  addKnob(reverbControls, "WIDTH", params::reverbWidthId,
          "Mono at zero, fully spread at the top", popupParent);

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
      {&voicesCaption, "VOICES", juce::Justification::centredLeft},
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

  setVoiceCount(0, 8);
  updateLinkEnablement();
}

void TopBar::styleToggle(juce::TextButton &b, const juce::String &text,
                         const juce::String &tooltip) {
  b.setButtonText(text);
  b.setClickingTogglesState(true);
  b.setTooltip(tooltip);
  addAndMakeVisible(b);
}

void TopBar::addKnob(std::vector<Control> &into, const juce::String &caption,
                     const juce::String &paramId, const juce::String &tooltip,
                     juce::Component &popupParent) {
  Control c;
  c.knob = std::make_unique<LabelledKnob>(caption);
  c.knob->slider.setTooltip(tooltip);
  c.knob->slider.setPopupDisplayEnabled(true, true, &popupParent);

  if (auto *p = apvts.getParameter(paramId))
    c.knob->slider.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  addAndMakeVisible(*c.knob);

  c.attachment =
      std::make_unique<SliderAttachment>(apvts, paramId, c.knob->slider);

  into.push_back(std::move(c));
}

void TopBar::setLinkScope(LinkScope s) {
  scope = (LinkScope)juce::jlimit(0, (int)LinkScope::NumScopes - 1, (int)s);
}

void TopBar::setLinkCurve(LinkCurve c) {
  curve = (LinkCurve)juce::jlimit(0, (int)LinkCurve::NumCurves - 1, (int)c);
}

void TopBar::updateLinkEnablement() {
  linkMenuButton.setEnabled(linkButton.getToggleState());
}

void TopBar::showLinkMenu(juce::Component *anchor) {
  const LinkSettings settings{linkButton.getToggleState(), scope, curve};

  auto m = buildLinkMenu(settings);
  m.setLookAndFeel(&getLookAndFeel());

  auto options = juce::PopupMenu::Options().withStandardItemHeight(22);

  if (anchor != nullptr) {
    options = options.withTargetComponent(anchor);
  } else {
    // Under the pointer, which is where a right-click expects to find it.
    const auto p = juce::Desktop::getInstance()
                       .getMainMouseSource()
                       .getScreenPosition()
                       .roundToInt();

    options = options.withTargetScreenArea({p.x, p.y, 1, 1});
  }

  m.showMenuAsync(options, [this, settings](int result) {
    auto chosen = settings;

    if (!applyLinkMenuChoice(result, chosen))
      return;

    linkButton.setToggleState(chosen.enabled, juce::dontSendNotification);
    scope = chosen.scope;
    curve = chosen.curve;

    updateLinkEnablement();

    if (onLinkSettingsChanged)
      onLinkSettingsChanged();
  });
}

void TopBar::showVoiceMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  auto *polyphony = apvts.getParameter(params::polyphonyId);
  auto *bendRange = apvts.getParameter(params::bendRangeId);

  const auto polyIndex =
      polyphony != nullptr
          ? juce::roundToInt(polyphony->convertFrom0to1(polyphony->getValue()))
          : 0;

  m.addSectionHeader("Polyphony");
  for (int i = 0; i < (int)params::kPolyphonyChoices.size(); ++i)
    m.addItem(100 + i,
              juce::String(params::kPolyphonyChoices[(size_t)i]) + " voices",
              true, i == polyIndex);

  const auto currentBend =
      bendRange != nullptr
          ? juce::roundToInt(bendRange->convertFrom0to1(bendRange->getValue()))
          : 2;

  // The semitone counts anyone actually uses, rather than all 25 of them.
  static const int kBendChoices[] = {0, 1, 2, 3, 4, 5, 7, 12, 24};

  m.addSeparator();
  m.addSectionHeader("Pitch bend range");
  for (auto semitones : kBendChoices)
    m.addItem(200 + semitones,
              juce::String(semitones) +
                  (semitones == 1 ? " semitone" : " semitones"),
              true, semitones == currentBend);

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(&voicesButton)
                      .withStandardItemHeight(22),
                  [this](int result) {
                    if (result == 0)
                      return;

                    const auto id = result >= 200 ? params::bendRangeId
                                                  : params::polyphonyId;
                    const auto plain = result >= 200 ? (float)(result - 200)
                                                     : (float)(result - 100);

                    if (auto *p = apvts.getParameter(id))
                      p->setValueNotifyingHost(p->convertTo0to1(plain));
                  });
}

void TopBar::setVoiceCount(int active, int limit) {
  const auto text = juce::String(active) + " / " + juce::String(limit);

  if (voicesButton.getButtonText() != text)
    voicesButton.setButtonText(text);
}

void TopBar::setZoomChoice(float zoom) {
  for (int i = 0; i < (int)std::size(kZoomChoices); ++i) {
    if (std::abs(kZoomChoices[i] - zoom) < 0.01f) {
      zoomBox.setSelectedId(i + 1, juce::dontSendNotification);
      return;
    }
  }
}

TopBar::RowPlan TopBar::planRows(int firstRowWidth, int fullWidth,
                                 int maxRows) {
  RowPlan plan;
  plan.start.fill(kGroupCount);
  plan.start[0] = 0;

  int group = 0;
  int row = 0;

  // Greedy, filling each row as far as it will go. The first row is short by
  // the width of the title, the rest have the whole bar.
  while (group < kGroupCount && row < maxRows) {
    const auto available = row == 0 ? firstRowWidth : fullWidth;

    int used = 0;
    int onThisRow = 0;

    while (group < kGroupCount) {
      auto width = kGroupMinWidth[group] + (onThisRow > 0 ? kGroupGap : 0);

      // The meter can give up a little rather than send its group to the next
      // row, so a window a few pixels short does not wrap.
      if (group == kOutputGroupIndex)
        width -= kOutputMaxSqueeze;

      if (onThisRow > 0 && used + width > available)
        break;

      used += width;
      ++onThisRow;
      ++group;
    }

    ++row;
    plan.start[(size_t)row] = group;
  }

  plan.rows = row;

  // A last row holding one small group looks like a mistake rather than a
  // layout. If there is a fuller row above it, pull a group down to join it.
  if (plan.rows >= 2) {
    const auto last = plan.rows - 1;
    const auto lastWidth =
        totalGroupWidth(plan.start[(size_t)last], kGroupCount);
    const auto onRowAbove =
        plan.start[(size_t)last] - plan.start[(size_t)last - 1];

    if (lastWidth * 100 < fullWidth * 30 && onRowAbove >= 2)
      plan.start[(size_t)last] -= 1;
  }

  return plan;
}

int TopBar::heightForWidth(int width) {
  const auto full = width - 2 * kBarMargin;
  const auto first = full - kTitleWidth - kTitleLead;

  const auto rows = planRows(first, full, kGroupCount).rows;

  return rows * kRowHeight + (rows - 1) * kRowGap + 2 * kBarPadY;
}

int TopBar::minimumWidth() {
  // The bar keeps working however narrow the window gets, stacking one group
  // per row if it has to, but at some point the chrome is taller than the
  // mixer underneath it. This is the narrowest window where it still fits in
  // kMaxComfortableRows, and the editor takes it as the floor.
  for (int width = 2 * kBarMargin + kTitleWidth; width < 4000; ++width) {
    const auto full = width - 2 * kBarMargin;
    const auto first = full - kTitleWidth - kTitleLead;

    if (planRows(first, full, kGroupCount).rows <= kMaxComfortableRows)
      return width;
  }

  jassertfalse;
  return 1024;
}

void TopBar::parkControls() {
  juce::Component *all[] = {
      &master,       &spread,        &meter,      &meterCaption,
      &presetBox,    &presetCaption, &zoomBox,    &zoomCaption,
      &voicesButton, &voicesCaption, &linkButton, &linkMenuButton,
      &phaseButton,  &clipButton,    &echoButton, &reverbButton};

  for (auto *c : all)
    c->setBounds({});

  for (auto *set : {&echoControls, &reverbControls})
    for (auto &c : *set)
      c.knob->setBounds({});

  groupBounds.fill({});
}

void TopBar::placeGroup(int group, juce::Rectangle<int> bounds) {
  groupBounds[(size_t)group] = bounds;

  auto r = bounds.reduced(kGroupPad, 0);

  // Captioned controls carry their label above. Knobs carry theirs below, so
  // both come out the same overall height and can share a row.
  const auto captioned = [](juce::Component &c, juce::Label &cap,
                            juce::Rectangle<int> column) {
    auto area = column.withSizeKeepingCentre(column.getWidth(), 37);
    cap.setBounds(area.removeFromTop(12));
    area.removeFromTop(1);
    c.setBounds(area);
  };

  const auto button = [](juce::Button &b, juce::Rectangle<int> column) {
    b.setBounds(column.withSizeKeepingCentre(column.getWidth(), 24));
  };

  /// The switch names the effect, so it stands at the head of its group and
  /// the knobs follow.
  const auto effect = [&](juce::Button &toggle, std::vector<Control> &controls,
                          juce::Rectangle<int> area) {
    button(toggle, area.removeFromLeft(kFxToggleWidth));
    area.removeFromLeft(kFxToggleGap);

    for (auto &c : controls)
      c.knob->setBounds(area.removeFromLeft(kKnobWidth));
  };

  switch (group) {
  case PresetGroup:
    captioned(presetBox, presetCaption, r);
    break;

  case VoiceGroup:
    captioned(voicesButton, voicesCaption, r);
    break;

  case LinkGroup:
    button(linkButton, r.removeFromLeft(48));
    r.removeFromLeft(6);
    button(linkMenuButton, r.removeFromLeft(28));
    break;

  case EchoGroup:
    effect(echoButton, echoControls, r);
    break;

  case ReverbGroup:
    effect(reverbButton, reverbControls, r);
    break;

  case OutputGroup: {
    spread.setBounds(r.removeFromLeft(52));
    master.setBounds(r.removeFromLeft(52));
    r.removeFromLeft(6);

    // The meter takes whatever the group was given beyond its minimum.
    button(clipButton, r.removeFromRight(44));
    r.removeFromRight(4);
    button(phaseButton, r.removeFromRight(50));
    r.removeFromRight(6);

    captioned(meter, meterCaption, r);
    break;
  }

  case ViewGroup:
    captioned(zoomBox, zoomCaption, r);
    break;

  default:
    jassertfalse;
    break;
  }
}

void TopBar::layoutRow(juce::Rectangle<int> row, int firstGroup,
                       int lastGroup) {
  if (firstGroup >= lastGroup)
    return;

  const auto needed = totalGroupWidth(firstGroup, lastGroup);
  const auto slack = row.getWidth() - needed;

  // Spare room goes to the meter, and a small shortfall comes out of it too.
  const auto outputAdjust = slack >= 0 ? juce::jmin(slack, kOutputMaxExtra)
                                       : juce::jmax(slack, -kOutputMaxSqueeze);

  const bool hasOutput =
      kOutputGroupIndex >= firstGroup && kOutputGroupIndex < lastGroup;

  for (int g = firstGroup; g < lastGroup; ++g) {
    if (g > firstGroup)
      row.removeFromLeft(kGroupGap);

    auto width = kGroupMinWidth[g];
    if (g == kOutputGroupIndex && hasOutput)
      width += outputAdjust;

    // Never hand out more than is left, so a very narrow window crops the last
    // group rather than letting groups overlap each other.
    width = juce::jmin(width, row.getWidth());
    if (width < 40)
      break;

    placeGroup(g, row.removeFromLeft(width));
  }
}

void TopBar::resized() {
  parkControls();

  auto area = getLocalBounds().reduced(kBarMargin, kBarPadY);
  const auto fullWidth = area;

  const auto affordable =
      juce::jmax(1, (area.getHeight() + kRowGap) / (kRowHeight + kRowGap));

  const auto plan = planRows(fullWidth.getWidth() - kTitleWidth - kTitleLead,
                             fullWidth.getWidth(), affordable);

  for (int row = 0; row < plan.rows; ++row) {
    if (row > 0)
      area.removeFromTop(kRowGap);

    auto line = area.removeFromTop(kRowHeight);

    // Only the first row has to make room for the title beside it.
    if (row == 0)
      line.removeFromLeft(kTitleWidth + kTitleLead);

    if (line.getWidth() < 60)
      break;

    layoutRow(line, plan.start[(size_t)row], plan.start[(size_t)row + 1]);
  }
}

void TopBar::paint(juce::Graphics &g) {
  g.setColour(colours::panel);
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  // A panel behind each group, which is what carries the "these belong
  // together" reading. The controls name themselves, so the boxes carry no
  // titles of their own and cost no extra height.
  for (const auto &r : groupBounds) {
    if (r.isEmpty())
      continue;

    const auto f = r.toFloat();

    g.setColour(colours::panelAlt.withAlpha(0.75f));
    g.fillRoundedRectangle(f, 4.0f);
    g.setColour(colours::outline.withAlpha(0.9f));
    g.drawRoundedRectangle(f.reduced(0.5f), 4.0f, 1.0f);
  }

  auto title = getLocalBounds().reduced(kBarMargin, kBarPadY);
  title = title.removeFromLeft(kTitleWidth).withHeight(kRowHeight);

  g.setColour(colours::text);
  g.setFont(makeFont(21.0f, true));
  g.drawText("OVERTONIUM", title.removeFromTop(24),
             juce::Justification::centredLeft, false);

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.5f));
  g.drawText("32-partial overtone synthesiser", title.removeFromTop(13),
             juce::Justification::centredLeft, false);
}

} // namespace ovt::ui
