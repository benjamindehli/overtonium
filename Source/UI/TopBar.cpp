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
constexpr int kGroupGap = 6;
constexpr int kTitleWidth = 150;
constexpr int kGroupPad = 6;

/// Minimum width of each group, in the order they are laid out. Only the
/// output group grows, because the meter is the one thing worth more room.
constexpr int kGroupMinWidth[] = {144, 90, 60, 222, 260, 186, 82};
constexpr int kOutputGroupIndex = 5;
constexpr int kGroupCount = 7;

constexpr int kKnobWidth = 38;
constexpr int kFxToggleWidth = 52;
constexpr int kFxToggleGap = 6;

/// How many rows of bar are worth having above a mixer.
constexpr int kMaxComfortableRows = 3;

/// The meter is the only thing worth extra room, but not unlimited extra room.
/// Uncapped it swallows a whole row and reads as a progress bar.
constexpr int kOutputMaxExtra = 120;

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

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  master.slider.setPopupDisplayEnabled(true, true, &popupParent);
  master.slider.setTooltip("Output level");
  addAndMakeVisible(master);

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);

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

  // ---- settings -------------------------------------------------------------
  // Everything that is set once and then left: polyphony, bend range, and the
  // two switches that used to sit on the panel taking up room they had not
  // earned.
  settingsButton.setButtonText("SETTINGS");
  settingsButton.setTooltip("Polyphony, pitch bend range, phase reset and the "
                            "safety clipper");
  settingsButton.onClick = [this] { showSettingsMenu(); };
  addAndMakeVisible(settingsButton);

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
  // One button rather than a switch and a chevron beside it. It always opens
  // the menu, and it lights when the switch inside is on, so the state is
  // visible without the state being what the click does.
  linkButton.setButtonText("LINK");
  linkButton.setTooltip(
      "Gang the strips, so dragging one channel's knob moves the same knob on "
      "the others. The menu picks which channels it reaches and how the "
      "movement is shared out. The same menu is on a right-click in the "
      "mixer.");
  linkButton.setColour(juce::TextButton::buttonOnColourId, colours::soloOn);
  linkButton.onClick = [this] { showLinkMenu(&linkButton); };
  addAndMakeVisible(linkButton);

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
  addKnob(echoControls, "AGE", params::echoAgeId,
          "How worn the machine is. Turning it up takes the top off every "
          "repeat, sets the motor wandering and lets the tape lean over when "
          "it is driven. New is clean and bright, old is dark and unsteady.",
          popupParent);

  addKnob(reverbControls, "MIX", params::reverbMixId,
          "How much of the output is reverb", popupParent);
  addKnob(reverbControls, "DECAY", params::reverbDecayId,
          "How long the tail takes to fall away. The room is sized to match, "
          "since a long tail in a small room is a spring rather than a place.",
          popupParent);
  addKnob(reverbControls, "DAMP", params::reverbDampId,
          "How quickly the top end dies out of the tail", popupParent);
  addKnob(reverbControls, "PRE", params::reverbPreDelayId,
          "Silence between the note and its reverb. A little of it keeps the "
          "attack clear of the wash.",
          popupParent);
  addKnob(reverbControls, "WIDTH", params::reverbWidthId,
          "Mono at zero, fully spread at the top", popupParent);

  // ---- captions -------------------------------------------------------------
  struct {
    juce::Label *label;
    const char *text;
    juce::Justification just;
  } captions[] = {
      {&presetCaption, "PRESET", juce::Justification::centredLeft},
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

void TopBar::updateLinkEnablement() { linkButton.repaint(); }

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

void TopBar::showSettingsMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  auto *polyphony = apvts.getParameter(params::polyphonyId);
  auto *bendRange = apvts.getParameter(params::bendRangeId);
  auto *phase = apvts.getParameter(params::phaseResetId);
  auto *clip = apvts.getParameter(params::safetyClipId);

  const auto polyIndex =
      polyphony != nullptr
          ? juce::roundToInt(polyphony->convertFrom0to1(polyphony->getValue()))
          : 0;

  // The count of what is actually sounding goes in the header rather than on
  // the panel, where it was a readout nobody was watching.
  m.addSectionHeader("Polyphony (" + juce::String(activeVoices) + " sounding)");

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

  m.addSeparator();
  m.addSectionHeader("Output");
  m.addItem(300, "Phase reset", true,
            phase != nullptr && phase->getValue() > 0.5f);
  m.addItem(301, "Safety clip", true,
            clip != nullptr && clip->getValue() > 0.5f);

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(&settingsButton)
                      .withStandardItemHeight(22),
                  [this](int result) {
                    if (result == 0)
                      return;

                    const auto flip = [this](const char *id) {
                      if (auto *p = apvts.getParameter(id))
                        p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f
                                                                      : 1.0f);
                    };

                    if (result == 300)
                      return flip(params::phaseResetId);

                    if (result == 301)
                      return flip(params::safetyClipId);

                    const auto id = result >= 200 ? params::bendRangeId
                                                  : params::polyphonyId;
                    const auto plain = result >= 200 ? (float)(result - 200)
                                                     : (float)(result - 100);

                    if (auto *p = apvts.getParameter(id))
                      p->setValueNotifyingHost(p->convertTo0to1(plain));
                  });
}

void TopBar::setVoiceCount(int active, int) { activeVoices = active; }

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
  juce::Component *all[] = {&master,      &meter,          &meterCaption,
                            &presetBox,   &presetCaption,  &zoomBox,
                            &zoomCaption, &settingsButton, &linkButton,
                            &echoButton,  &reverbButton};

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
    button(settingsButton, r);
    break;

  case LinkGroup:
    button(linkButton, r);
    break;

  case EchoGroup:
    effect(echoButton, echoControls, r);
    break;

  case ReverbGroup:
    effect(reverbButton, reverbControls, r);
    break;

  case OutputGroup: {
    master.setBounds(r.removeFromLeft(48));
    r.removeFromLeft(6);

    // The meter takes whatever the group was given beyond its minimum.
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
