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
constexpr int kGroupMinWidth[] = {144, 90, 60, 168, 222, 260, 186};
constexpr int kOutputGroupIndex = 6;
constexpr int kGroupCount = 7;

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

namespace {
/// Which of the seven bars are lit for each character we can draw.
///
///      aaa
///     f   b
///      ggg
///     e   c
///      ddd
constexpr uint8_t kSegA = 1, kSegB = 2, kSegC = 4, kSegD = 8, kSegE = 16,
                  kSegF = 32, kSegG = 64;

uint8_t segmentsFor(char c) {
  switch (c) {
  case '0':
    return kSegA | kSegB | kSegC | kSegD | kSegE | kSegF;
  case '1':
    return kSegB | kSegC;
  case '2':
    return kSegA | kSegB | kSegG | kSegE | kSegD;
  case '3':
    return kSegA | kSegB | kSegG | kSegC | kSegD;
  case '4':
    return kSegF | kSegG | kSegB | kSegC;
  case '5':
    return kSegA | kSegF | kSegG | kSegC | kSegD;
  case '6':
    return kSegA | kSegF | kSegG | kSegE | kSegC | kSegD;
  case '7':
    return kSegA | kSegB | kSegC;
  case '8':
    return kSegA | kSegB | kSegC | kSegD | kSegE | kSegF | kSegG;
  case '9':
    return kSegA | kSegB | kSegC | kSegD | kSegF | kSegG;
  default:
    return 0;
  }
}
} // namespace

SegmentDisplay::SegmentDisplay(juce::String unit) : unitText(std::move(unit)) {}

void SegmentDisplay::setReading(const juce::String &digits, bool isActive) {
  if (digits == reading && isActive == active)
    return;

  reading = digits;
  active = isActive;
  repaint();
}

void SegmentDisplay::mouseUp(const juce::MouseEvent &e) {
  if (onClick && contains(e.getPosition()))
    onClick();
}

void SegmentDisplay::mouseEnter(const juce::MouseEvent &) {
  hovered = true;
  repaint();
}

void SegmentDisplay::mouseExit(const juce::MouseEvent &) {
  hovered = false;
  repaint();
}

void SegmentDisplay::paintGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                                char c, juce::Colour on,
                                juce::Colour off) const {
  const auto lit = segmentsFor(c);

  // Every bar is drawn whether it is on or not, which is what makes it read as
  // a display with something switched off rather than as floating shapes.
  const auto t = juce::jmax(1.0f, area.getHeight() * 0.16f);
  const auto gap = t * 0.35f;
  const auto w = area.getWidth();
  const auto h = area.getHeight();
  const auto mid = (h - t) * 0.5f;

  struct Bar {
    uint8_t flag;
    juce::Rectangle<float> r;
  };

  const Bar bars[] = {
      {kSegA, {t * 0.5f + gap, 0.0f, w - t - gap * 2.0f, t}},
      {kSegB, {w - t, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegC, {w - t, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegD, {t * 0.5f + gap, h - t, w - t - gap * 2.0f, t}},
      {kSegE, {0.0f, mid + t * 0.5f + gap * 0.5f, t, mid - gap * 1.5f}},
      {kSegF, {0.0f, t * 0.5f + gap, t, mid - gap * 1.5f}},
      {kSegG, {t * 0.5f + gap, mid, w - t - gap * 2.0f, t}},
  };

  for (const auto &bar : bars) {
    g.setColour((lit & bar.flag) != 0 ? on : off);
    g.fillRoundedRectangle(bar.r.translated(area.getX(), area.getY()),
                           t * 0.35f);
  }
}

void SegmentDisplay::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat().reduced(1.0f);

  const auto on = active ? colours::accent : colours::textDim;
  const auto off = on.withAlpha(hovered ? 0.20f : 0.12f);
  const auto lit = on.withAlpha(active ? 0.95f : 0.75f);

  // A recess, so the readout sits in the panel rather than on it, and so the
  // unlit bars have something to be dark against.
  g.setColour(colours::groove);
  g.fillRoundedRectangle(area, 2.5f);

  if (hovered) {
    g.setColour(colours::outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 2.5f, 1.0f);
  }

  area = area.reduced(3.0f, 2.0f);

  if (reading.isEmpty())
    return;

  // The point rides between digits on a narrow stripe of its own rather than
  // taking a whole cell, the way it does on a real display.
  int cells = 0, points = 0;

  for (auto c : reading)
    (c == '.' ? points : cells) += 1;

  if (cells < 1)
    return;

  // The unit only earns its place once the digits have what they need.
  const auto unitW =
      unitText.isNotEmpty() && area.getWidth() > 44.0f ? 21.0f : 0.0f;

  // A point costs about a third of a digit, which is what the extra term in
  // the denominator is buying.
  const auto cellW =
      juce::jmin((area.getWidth() - unitW) /
                     ((float)cells + 0.32f * (float)points),
                 area.getHeight() * 0.72f);

  const auto glyphW = cellW * 0.82f;
  const auto pointW = cellW * 0.32f;
  const auto runW = cellW * (float)cells + pointW * (float)points;

  // Centred as one block, so a three-character reading does not sit off to one
  // side of a display sized for four.
  auto x = area.getCentreX() - (runW + unitW) * 0.5f;

  const auto digitArea = area.withX(x).withWidth(runW);

  if (unitW > 0.0f) {
    g.setColour(colours::textDim.withAlpha(0.8f));
    g.setFont(makeFont(7.5f, true));
    g.drawText(unitText,
               area.withX(digitArea.getRight() + 2.0f).withWidth(unitW - 2.0f),
               juce::Justification::centredLeft, false);
  }

  for (int i = 0; i < reading.length(); ++i) {
    const auto c = (char)reading[i];

    if (c == '.') {
      // Same weight as a segment, sitting on the baseline the segments end on.
      const auto t = juce::jmax(1.0f, digitArea.getHeight() * 0.16f);

      g.setColour(lit);
      g.fillRoundedRectangle(x + (pointW - t) * 0.5f, digitArea.getBottom() - t,
                             t, t, t * 0.35f);

      x += pointW;
      continue;
    }

    paintGlyph(g, {x, digitArea.getY(), glyphW, digitArea.getHeight()}, c, lit,
               off);
    x += cellW;
  }
}

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

  track.slider.setPopupDisplayEnabled(true, true, &popupParent);
  track.slider.setTooltip(
      "Thins the series as you play up the keyboard, the way a real body does: "
      "the partials climb through a rolloff that stays put. Measured against "
      "the fundamental, so high notes get duller rather than quieter.");
  track.slider.setDoubleClickReturnValue(true, 0.0);
  addAndMakeVisible(track);

  trackAttachment = std::make_unique<SliderAttachment>(
      apvts, params::trackId, track.slider);

  wobble.slider.setPopupDisplayEnabled(true, true, &popupParent);
  wobble.slider.setTooltip(
      "A warped record under the whole instrument, ahead of the echo. A slow "
      "wander with the odd sharp slip on top, both of which get worse as you "
      "turn it up. Both channels bend together, since it is one platter.");
  wobble.slider.setDoubleClickReturnValue(true, 0.0);
  addAndMakeVisible(wobble);

  wobbleAttachment = std::make_unique<SliderAttachment>(
      apvts, params::wobbleId, wobble.slider);

  // Two bars beside the master fader, at the end of the signal path, need no
  // caption to say what they are.
  addAndMakeVisible(meter);

  rateDisplay.setTooltip(
      "The rate the instrument renders at. Turning it down is a real cut "
      "rather than a filter over the top: it does less work, and the partials "
      "above the new ceiling fold back down instead of disappearing.");

  bitsDisplay.setTooltip("The depth the instrument quantises to, ahead of the "
                         "echo, the reverb and the fader.");

  rateDisplay.onClick = [this] {
    juce::StringArray choices;
    for (auto hz : params::kLofiRateChoices)
      choices.add(params::lofiRateName(hz));

    showConverterMenu(params::lofiRateId, choices, &rateDisplay);
  };

  bitsDisplay.onClick = [this] {
    juce::StringArray choices;
    for (auto bits : params::kLofiBitChoices)
      choices.add(params::lofiBitName(bits));

    showConverterMenu(params::lofiBitsId, choices, &bitsDisplay);
  };

  addAndMakeVisible(rateDisplay);
  addAndMakeVisible(bitsDisplay);

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

void TopBar::showConverterMenu(const char *paramId,
                               const juce::StringArray &choices,
                               juce::Component *anchor) {
  auto *param = apvts.getParameter(paramId);
  if (param == nullptr)
    return;

  const auto current =
      juce::roundToInt(param->convertFrom0to1(param->getValue()));

  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  for (int i = 0; i < choices.size(); ++i)
    m.addItem(i + 1, choices[i], true, i == current);

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(anchor)
                      .withStandardItemHeight(22),
                  [param](int result) {
                    if (result > 0)
                      param->setValueNotifyingHost(
                          param->convertTo0to1((float)(result - 1)));
                  });
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

  m.addItem(700, "Undo", canUndo && canUndo());
  m.addItem(701, "Redo", canRedo && canRedo());
  m.addSeparator();

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

  // Which channel-wide control the AT row on every strip listens to. Most
  // keyboards have no aftertouch, so the wheel stands in for it, and by
  // default either will do.
  const auto sourceIndex = [this] {
    auto *p = apvts.getParameter(params::atSourceId);
    return p != nullptr ? juce::roundToInt(p->convertFrom0to1(p->getValue()))
                        : 0;
  }();

  juce::PopupMenu sources;

  for (int i = 0; i < (int)params::kAftertouchSourceNames.size(); ++i)
    sources.addItem(600 + i, params::kAftertouchSourceNames[(size_t)i], true,
                    i == sourceIndex);

  // Tuning of the keyboard itself, which is a decision about the instrument
  // and gets set once, so it belongs here rather than on the panel.
  const auto pickedIndex = [this](const char *id) {
    auto *p = apvts.getParameter(id);
    return p != nullptr ? juce::roundToInt(p->convertFrom0to1(p->getValue()))
                        : 0;
  };

  juce::PopupMenu temperaments, roots, references;

  for (int i = 0; i < (int)Temperament::NumTemperaments; ++i)
    temperaments.addItem(900 + i, temperamentName((Temperament)i), true,
                         i == pickedIndex(params::temperamentId));

  for (int i = 0; i < (int)params::kPitchClassNames.size(); ++i)
    roots.addItem(1000 + i, params::kPitchClassNames[(size_t)i], true,
                  i == pickedIndex(params::tuningRootId));

  for (int i = 0; i < (int)params::kReferenceHzChoices.size(); ++i)
    references.addItem(
        1100 + i,
        juce::String(params::kReferenceHzChoices[(size_t)i]) + " Hz", true,
        i == pickedIndex(params::referenceHzId));

  juce::PopupMenu zooms;

  for (int i = 0; i < (int)std::size(kZoomChoices); ++i) {
    const auto percent = juce::roundToInt(kZoomChoices[i] * 100.0f);

    zooms.addItem(800 + i, juce::String(percent) + "%", true,
                  std::abs(kZoomChoices[i] - zoom) < 0.01f);
  }

  m.addSeparator();
  m.addSectionHeader("Tuning");
  m.addSubMenu("Temperament", temperaments);

  // A temperament has to be built on something, and equal temperament is the
  // one that does not care.
  m.addSubMenu("Root", roots,
               pickedIndex(params::temperamentId) != (int)Temperament::Equal);

  m.addSubMenu("Reference pitch", references);

  m.addSeparator();
  m.addSubMenu("Aftertouch from", sources);
  m.addSubMenu("Zoom", zooms);

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

                    // The converter entries carry an index into their choice
                    // list rather than a value, since the lists are not
                    // contiguous runs of numbers.
                    const auto choose = [this](const char *id, int index) {
                      auto *p = apvts.getParameter(id);

                      if (p != nullptr)
                        p->setValueNotifyingHost(
                            p->convertTo0to1((float)index));
                    };

                    if (result >= 1100)
                      return choose(params::referenceHzId, result - 1100);

                    if (result >= 1000)
                      return choose(params::tuningRootId, result - 1000);

                    if (result >= 900)
                      return choose(params::temperamentId, result - 900);

                    if (result >= 800) {
                      const auto index = result - 800;

                      if (index < (int)std::size(kZoomChoices) && onZoomChanged)
                        onZoomChanged(kZoomChoices[index]);

                      return;
                    }

                    if (result == 700)
                      return onUndo ? onUndo() : void();

                    if (result == 701)
                      return onRedo ? onRedo() : void();

                    if (result >= 600)
                      return choose(params::atSourceId, result - 600);

                    const auto id = result >= 200 ? params::bendRangeId
                                                  : params::polyphonyId;
                    const auto plain = result >= 200 ? (float)(result - 200)
                                                     : (float)(result - 100);

                    if (auto *p = apvts.getParameter(id))
                      p->setValueNotifyingHost(p->convertTo0to1(plain));
                  });
}

void TopBar::updateConverterReadouts(double hostSampleRate) {
  const auto chosen = [this](const char *id, int count) {
    auto *p = apvts.getParameter(id);

    return p == nullptr
               ? 0
               : juce::jlimit(
                     0, count - 1,
                     juce::roundToInt(p->convertFrom0to1(p->getValue())));
  };

  const auto rate = params::kLofiRateChoices[(size_t)chosen(
      params::lofiRateId, (int)params::kLofiRateChoices.size())];

  // Asking for more than the host has is not something anyone can be given, so
  // the readout says what is actually happening rather than what was picked.
  // Before the first prepareToPlay there is no host rate to compare against,
  // and a deliberate choice should still show rather than waiting for one.
  const bool known = hostSampleRate > 0.0;
  const bool cutting = rate > 0 && (!known || (double)rate < hostSampleRate);
  const auto shown = cutting ? (double)rate : hostSampleRate;

  rateDisplay.setReading(
      shown > 0.0 ? juce::String(shown / 1000.0, 1) : juce::String(), cutting);

  const auto bits = params::kLofiBitChoices[(size_t)chosen(
      params::lofiBitsId, (int)params::kLofiBitChoices.size())];

  // Nothing being quantised means the 32-bit float everything else runs in.
  bitsDisplay.setReading(juce::String(bits > 0 ? bits : 32), bits > 0);
}

void TopBar::setZoomChoice(float newZoom) { zoom = newZoom; }

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
  juce::Component *all[] = {&master,      &meter,       &presetButton,
                            &linkButton,  &settingsButton, &echoButton,
                            &reverbButton, &stretch,    &track,
                            &rateDisplay, &bitsDisplay};

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

  case SeriesGroup: {
    // Split evenly rather than at the usual knob width, since STRETCH is a
    // longer caption than anything else in the bar and would otherwise be cut.
    const auto each = r.getWidth() / 3;

    stretch.setBounds(r.removeFromLeft(each));
    track.setBounds(r.removeFromLeft(each));
    wobble.setBounds(r);
    break;
  }

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

    // The two readouts go under it, in the band the knob captions occupy, so
    // the converter reads as the last thing before the output rather than as
    // another control competing with the meter.
    auto below = r.withTop(meter.getBottom() + 3).withTrimmedBottom(1);

    const auto each = juce::jmin(66, (below.getWidth() - 6) / 2);
    auto pair = below.withSizeKeepingCentre(each * 2 + 6, below.getHeight());

    rateDisplay.setBounds(pair.removeFromLeft(each));
    pair.removeFromLeft(6);
    bitsDisplay.setBounds(pair);
    break;
  }

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

    const bool framed = group == SeriesGroup || group == EchoGroup ||
                        group == ReverbGroup || group == OutputGroup;

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
