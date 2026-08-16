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
constexpr int kGroupMinWidth[] = {144, 90, 60, 64, 222, 260, 186, 82};
constexpr int kOutputGroupIndex = 6;
constexpr int kGroupCount = 8;

/// Buttons, lists and the output meter all stand this tall, centred on the
/// dials beside them, so a row reads as one line of controls.
constexpr int kControlHeight = 24;

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

/// What it is, under the wordmark. It measures 128 px here against the 150 px
/// the title block gives it, which leaves room for a wider font elsewhere.
constexpr const char *kCredit = "32-partial overtone synthesiser";

/// What the preset button says when nothing has been loaded yet.
constexpr const char *kNoPreset = "Select...";

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

/// The colour of the lamp standing at this fraction of full scale.
///
/// Anchored to decibel positions rather than to the level, so a lamp keeps its
/// colour whatever the bar is doing and the top of the scale always reads as
/// the top of the scale.
juce::Colour lampColour(float t) {
  if (t < 0.80f)
    return colours::accent;

  if (t < 0.94f)
    return colours::accent.interpolatedWith(colours::soloOn,
                                            (t - 0.80f) / 0.14f);

  return colours::soloOn.interpolatedWith(colours::muteOn, (t - 0.94f) / 0.06f);
}
} // namespace

int StereoOutputMeter::segments() const {
  // A lamp every eight pixels or so, within reason.
  return juce::jlimit(12, 48, getWidth() / 8);
}

bool StereoOutputMeter::Bar::advance(float level, int count) {
  const auto norm = toNormalised(level);
  const auto next =
      norm > displayed ? norm : juce::jmax(norm, displayed - 0.05f);

  displayed = next;

  // Hold the peak for a couple of seconds, then let it fall with the bar.
  if (next >= peak) {
    peak = next;
    hold = 30;
  } else if (hold > 0) {
    --hold;
  } else if (peak > 0.0f) {
    peak = juce::jmax(next, peak - 0.02f);
  }

  const auto wasLit = lit;
  const auto wasPeak = peakLamp;

  // The same hysteresis as the channel meters: a lamp lights when the level
  // reaches it and goes out only once the level has fallen clear of it, so a
  // note sitting on a boundary cannot flicker at the frame rate.
  constexpr float kClearOf = 0.3f;

  const auto exact = juce::jlimit(0.0f, 1.0f, displayed) * (float)count;
  lit = juce::jlimit(0, count, lit);

  while (lit < count && exact >= (float)lit + 1.0f)
    ++lit;

  while (lit > 0 && exact < (float)lit - kClearOf)
    --lit;

  peakLamp = peak > 0.001f
                 ? juce::jlimit(0, count - 1,
                                (int)(juce::jmin(peak, 1.0f) * (float)count))
                 : -1;

  return lit != wasLit || peakLamp != wasPeak;
}

void StereoOutputMeter::push(float l, float r) {
  const auto count = segments();
  if (count < 1)
    return;

  const bool a = left.advance(l, count);
  const bool b = right.advance(r, count);

  if (a || b)
    repaint();
}

void StereoOutputMeter::paintBar(juce::Graphics &g, juce::Rectangle<float> r,
                                 const Bar &bar, int count) const {
  g.setColour(colours::groove);
  g.fillRoundedRectangle(r, 1.5f);

  const auto step = r.getWidth() / (float)count;
  const auto gap = juce::jlimit(1.0f, 3.0f, step * 0.18f);

  for (int i = 0; i < count; ++i) {
    const auto lamp =
        juce::Rectangle<float>(r.getX() + (float)i * step, r.getY(), step,
                               r.getHeight())
            .reduced(gap * 0.5f, 1.0f);

    const auto colour = lampColour(((float)i + 0.5f) / (float)count);

    // The peak stays alight above the bar it came from, which is the whole
    // point of holding it.
    const bool on = i < bar.lit || i == bar.peakLamp;

    g.setColour(colour.withAlpha(on ? 0.92f : 0.13f));
    g.fillRoundedRectangle(lamp, juce::jmin(1.5f, lamp.getWidth() * 0.4f));
  }
}

void StereoOutputMeter::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat();

  // Scale marks at the decibel values worth aiming at.
  const auto scale = area.removeFromBottom(4.0f);
  for (const float db : {-48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f}) {
    const auto t = (db - kMeterFloorDb) / -kMeterFloorDb;
    const auto x = scale.getX() + t * (scale.getWidth() - 1.5f);

    g.setColour(colours::textDim.withAlpha(db >= -6.0f ? 0.55f : 0.30f));
    g.fillRect(x, scale.getY(), 1.0f, 3.0f);
  }

  const auto count = segments();
  const auto gap = 2.0f;
  const auto barH = (area.getHeight() - gap) * 0.5f;

  paintBar(g, area.removeFromTop(barH), left, count);
  area.removeFromTop(gap);
  paintBar(g, area.removeFromTop(barH), right, count);
}

// =============================================================================

// =============================================================================

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  logo = logoWordmark();

  master.slider.setPopupDisplayEnabled(true, true, &popupParent);
  master.slider.setTooltip("Output level");
  addAndMakeVisible(master);

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);

  stretch.slider.setPopupDisplayEnabled(true, true, &popupParent);
  stretch.slider.setTooltip(
      "Pulls the partials off the harmonic series, the way a stiff string or a "
      "struck bar does. A little of it is piano stretch. A lot of it is a "
      "bell. It stacks with each channel's TUNE rather than replacing it.");
  stretch.slider.setDoubleClickReturnValue(true, 0.0);
  stretch.slider.getProperties().set("bipolar", true);
  addAndMakeVisible(stretch);

  stretchAttachment = std::make_unique<SliderAttachment>(
      apvts, params::stretchId, stretch.slider);

  // Two bars beside the master fader, at the end of the signal path, need no
  // caption to say what they are.
  addAndMakeVisible(meter);

  // ---- presets --------------------------------------------------------------
  presetButton.setButtonText(kNoPreset);
  presetButton.setTooltip("Factory and saved presets, and somewhere to put "
                          "the one you are working on");
  presetButton.onClick = [this] { showPresetMenu(); };
  addAndMakeVisible(presetButton);

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

void TopBar::setPresetName(const juce::String &name) {
  presetButton.setButtonText(name.isEmpty() ? kNoPreset : name);
}

juce::String TopBar::getPresetName() const {
  const auto shown = presetButton.getButtonText();
  return shown == kNoPreset ? juce::String() : shown;
}

void TopBar::showPresetMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  const auto factory = presets::names();

  m.addSectionHeader("Factory");
  for (int i = 0; i < factory.size(); ++i)
    m.addItem(100 + i, factory[i]);

  // Read fresh every time it opens, so a preset saved a moment ago is there
  // and one deleted in Finder is not.
  userPresetFiles = presets::userPresets();

  if (!userPresetFiles.isEmpty()) {
    m.addSeparator();
    m.addSectionHeader("Saved");

    for (int i = 0; i < userPresetFiles.size(); ++i)
      m.addItem(1000 + i,
                userPresetFiles.getReference(i).getFileNameWithoutExtension());
  }

  m.addSeparator();
  m.addItem(1, "Save preset...");
  m.addItem(2, "Open the preset folder");
  m.addSeparator();
  m.addItem(3, "Copy as factory preset code");

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(&presetButton)
                      .withStandardItemHeight(22),
                  [this](int result) {
                    if (result == 0)
                      return;

                    if (result == 1)
                      return askForPresetName();

                    if (result == 2)
                      return (void)presets::userDirectory().revealToUser();

                    if (result == 3) {
                      if (onCopyFactoryCode)
                        onCopyFactoryCode();

                      return;
                    }

                    if (result >= 1000) {
                      const auto index = result - 1000;

                      if (index < userPresetFiles.size() && onUserPresetChosen)
                        onUserPresetChosen(userPresetFiles.getReference(index));

                      return;
                    }

                    if (onPresetChosen)
                      onPresetChosen(result - 100);
                  });
}

void TopBar::askForPresetName() {
  nameWindow = std::make_unique<juce::AlertWindow>(
      "Save preset", "What should it be called?",
      juce::MessageBoxIconType::NoIcon, this);

  nameWindow->setLookAndFeel(&getLookAndFeel());
  nameWindow->addTextEditor("name", getPresetName());
  nameWindow->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  nameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

  nameWindow->enterModalState(
      true, juce::ModalCallbackFunction::create([this](int result) {
        const auto name = nameWindow != nullptr
                              ? nameWindow->getTextEditorContents("name")
                              : juce::String();

        nameWindow.reset();

        if (result == 1 && onSaveUserPreset)
          onSaveUserPreset(name);
      }));
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

  // Converter quality, downwards. Both live here rather than on the panel
  // because they are a decision about the instrument rather than something you
  // ride, and because the default for both is "whatever the host is doing",
  // which is not a setting most people will ever touch.
  const auto chosenIndex = [this](const char *id) {
    auto *p = apvts.getParameter(id);
    return p != nullptr ? juce::roundToInt(p->convertFrom0to1(p->getValue()))
                        : 0;
  };

  const auto rateIndex = chosenIndex(params::lofiRateId);
  const auto bitIndex = chosenIndex(params::lofiBitsId);

  juce::PopupMenu rates, bits;

  for (int i = 0; i < (int)params::kLofiRateChoices.size(); ++i)
    rates.addItem(400 + i,
                  params::lofiRateName(params::kLofiRateChoices[(size_t)i]),
                  true, i == rateIndex);

  for (int i = 0; i < (int)params::kLofiBitChoices.size(); ++i)
    bits.addItem(500 + i,
                 params::lofiBitName(params::kLofiBitChoices[(size_t)i]), true,
                 i == bitIndex);

  m.addSeparator();
  m.addSectionHeader("Converter");
  m.addSubMenu("Sample rate", rates);
  m.addSubMenu("Bit depth", bits);

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

                    // The converter entries carry an index into their choice
                    // list rather than a value, since the lists are not
                    // contiguous runs of numbers.
                    const auto choose = [this](const char *id, int index) {
                      auto *p = apvts.getParameter(id);

                      if (p != nullptr)
                        p->setValueNotifyingHost(
                            p->convertTo0to1((float)index));
                    };

                    if (result >= 500)
                      return choose(params::lofiBitsId, result - 500);

                    if (result >= 400)
                      return choose(params::lofiRateId, result - 400);

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
  juce::Component *all[] = {&master,      &meter,          &presetButton,
                            &zoomBox,     &linkButton,     &settingsButton,
                            &echoButton,  &reverbButton,   &stretch};

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

  // Everything in the bar lines up with the dials rather than with the middle
  // of the row. A knob carries its caption underneath, so its dial sits above
  // the centre, and anything centred in the row instead reads as sagging next
  // to it.
  const auto alignedWithDials = [](juce::Component &c,
                                   juce::Rectangle<int> column) {
    const auto dial = LabelledKnob::dialBounds(column);

    c.setBounds(column.getX(), dial.getCentreY() - kControlHeight / 2,
                column.getWidth(), kControlHeight);
  };

  const auto button = [&](juce::Button &b, juce::Rectangle<int> column) {
    alignedWithDials(b, column);
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
    button(presetButton, r);
    break;

  case VoiceGroup:
    button(settingsButton, r);
    break;

  case LinkGroup:
    button(linkButton, r);
    break;

  case SeriesGroup:
    stretch.setBounds(r);
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
    alignedWithDials(meter, r);
    break;
  }

  case ViewGroup:
    alignedWithDials(zoomBox, r);
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
  // Rescaled at twice the size it is drawn, so it stays sharp on a display
  // that is doing the same thing again underneath us.
  if (logo.isValid()) {
    const auto height = kRowHeight * 2;
    const auto width = juce::roundToInt((float)height * (float)logo.getWidth() /
                                        (float)juce::jmax(1, logo.getHeight()));

    if (logoScaled.getHeight() != height)
      logoScaled =
          logo.rescaled(width, height, juce::Graphics::highResamplingQuality);
  }

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
  //
  // Only where there is something to group, though. A box drawn around a
  // single button says nothing that the button was not already saying, and
  // four of them in a row turn the bar into a fence.
  for (int group = 0; group < NumGroups; ++group) {
    const auto &r = groupBounds[(size_t)group];

    const bool framed = group == EchoGroup || group == ReverbGroup ||
                        group == OutputGroup;

    if (r.isEmpty() || !framed)
      continue;

    const auto f = r.toFloat();

    g.setColour(colours::panelAlt.withAlpha(0.75f));
    g.fillRoundedRectangle(f, 4.0f);
    g.setColour(colours::outline.withAlpha(0.9f));
    g.drawRoundedRectangle(f.reduced(0.5f), 4.0f, 1.0f);
  }

  auto title = getLocalBounds().reduced(kBarMargin, kBarPadY);
  title = title.removeFromLeft(kTitleWidth).withHeight(kRowHeight);

  if (logoScaled.isValid()) {
    const auto wordmarkHeight =
        juce::roundToInt((float)kTitleWidth * (float)logoScaled.getHeight() /
                         (float)juce::jmax(1, logoScaled.getWidth()));

    // The pair is centred as a block, so the wordmark does not drift when the
    // credit line under it changes size.
    auto block = title.withSizeKeepingCentre(kTitleWidth, wordmarkHeight + 15);

    g.drawImage(logoScaled, block.removeFromTop(wordmarkHeight).toFloat(),
                juce::RectanglePlacement::xLeft |
                    juce::RectanglePlacement::yMid);

    block.removeFromTop(2);

    g.setColour(colours::textDim);
    g.setFont(makeFont(9.5f));
    g.drawText(kCredit, block, juce::Justification::centredLeft, false);
    return;
  }

  // Nothing to fall back to but the name, which is better than a blank corner.
  g.setColour(colours::text);
  g.setFont(makeFont(21.0f, true));
  g.drawText("OVERTONIUM", title.removeFromTop(24),
             juce::Justification::centredLeft, false);

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.5f));
  g.drawText(kCredit, title.removeFromTop(13), juce::Justification::centredLeft,
             false);
}

} // namespace ovt::ui
