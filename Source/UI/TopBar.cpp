#include "TopBar.h"
#include "GlideMenu.h"
#include "LearnMenu.h"
#include "SettingsMenus.h"

#include "../PluginParameters.h"
#include "../Presets.h"
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "LookAndFeel.h"

namespace ovt::ui {

namespace {
constexpr float kZoomChoices[] = {0.75f, 1.0f, 1.25f, 1.5f};

/// The space round the edges of the bar: the margin on the left and the
/// right, which is also the space between the four buttons, and the padding
/// above and below the row. The bottom padding is one more than the top
/// because the bar's bottom edge is a 1 px line.
constexpr int kBarMargin = 7;
constexpr int kBarPadTop = 5;
constexpr int kBarPadBottom = 6;
constexpr int kRowHeight = 55;
constexpr int kRowGap = 4;
constexpr int kGroupGap = 6;

/// Inside a framed group, between its frame and its controls.
constexpr int kGroupPad = 6;

/// Between the groups that have no frame: the four buttons, the wordmark
/// with the preset screen, and the first frame after them.
constexpr int kUnframedGap = 19;

/// Between the four buttons, across and down.
constexpr int kButtonGap = 7;

/// Below the controls in a frame, so its captions sit 2 px above the frame's
/// bottom edge.
constexpr int kBelowControls = 1;

/// Between the wordmark and the preset screen under it.
constexpr int kWordmarkGap = 2;

/// Minimum width of each group, in the order they are laid out. Only the
/// output group grows.
///
/// The first holds the four buttons, two by two, which wear icons so they fit
/// 73 px. Every pixel more would come off the converter readouts, which stop
/// naming their units below 53 px. The first two groups have no frame, so
/// their widths are their contents.
constexpr int kGroupMinWidth[] = {73, 132, 218, 194, 232, 228, 172};
constexpr int kPresetGroupIndex = 1;
constexpr int kSeriesGroupIndex = 2;
constexpr int kOutputGroupIndex = 6;
constexpr int kGroupCount = 7;

/// The room left before a group that follows another on its row, more round
/// the two groups that have no frame.
constexpr int gapBefore(int group) {
  if (group == kPresetGroupIndex || group == kSeriesGroupIndex)
    return kUnframedGap;

  return kGroupGap;
}

/// Buttons, lists and the output meter all stand this tall, centred on the
/// dials beside them, so a row reads as one line of controls.
constexpr int kControlHeight = 24;

constexpr int kKnobWidth = 38;

constexpr int kFxToggleGap = 6;

/// How far the master fader's cap stands past the lamps it lies on, at each
/// end, as a channel's cap does against its meter.
constexpr int kFaderOverhang = 2;

/// How many rows of bar are worth having above a mixer.
constexpr int kMaxComfortableRows = 3;

/// The most extra room the meter takes.
constexpr int kOutputMaxExtra = 120;

/// How far the meter narrows when a row is slightly too tight, rather than
/// wrap a group onto the next row.
constexpr int kOutputMaxSqueeze = 40;

/// The SETTINGS tooltip when there is no new release to mention.
constexpr const char *kSettingsTooltip =
    "Polyphony, pitch bend range, expression, tuning, output and zoom";

/// What the preset button says when nothing has been loaded yet.
constexpr const char *kNoPreset = "Select...";

int totalGroupWidth(int first, int last) {
  int total = 0;
  for (int g = first; g < last; ++g)
    total += kGroupMinWidth[g] + (g > first ? gapBefore(g) : 0);

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

/// The colour of the lamp standing at this fraction of full scale. Fixed per
/// lamp, whatever the level.
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

  // The same hysteresis as the channel meters, so a level sitting on a
  // boundary does not flicker.
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
    const auto lamp = juce::Rectangle<float>(r.getX() + (float)i * step,
                                             r.getY(), step, r.getHeight())
                          .reduced(gap * 0.5f, 1.0f);

    const auto colour = lampColour(((float)i + 0.5f) / (float)count);

    // The peak stays alight above the bar it came from.
    const bool on = i < bar.lit || i == bar.peakLamp;

    g.setColour(colour.withAlpha(on ? 0.92f : 0.13f));
    g.fillRoundedRectangle(lamp, juce::jmin(1.5f, lamp.getWidth() * 0.4f));
  }
}

void StereoOutputMeter::paint(juce::Graphics &g) {
  auto area = getLocalBounds().toFloat();

  // Scale marks at the decibel values worth aiming at.
  const auto scale = area.removeFromBottom((float)kScaleHeight);
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

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  logo = logoWordmark();

  // So a right-click on a knob reaches mouseDown, which offers MIDI Learn.
  addMouseListener(this, true);

  masterFader.setPopupDisplayEnabled(true, true, &popupParent);
  masterFader.setTooltip("Output level");

  // Named for a screen reader, since it has no caption.
  masterFader.setTitle("Master level");

  // The meter owns the groove. See drawLinearSlider.
  masterFader.getProperties().set("meteredGroove", true);

  addAndMakeVisible(masterFader);

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, masterFader);
  learn::tag(masterFader, params::masterGainId);

  stretch.slider.setPopupDisplayEnabled(true, true, &popupParent);
  stretch.slider.setTooltip(
      "Pulls the partials sharp of the harmonic series, as a stiff string "
      "does. A little sounds like a piano, a lot like a bell. Adds to each "
      "channel's TUNE.");
  stretch.slider.setDoubleClickReturnValue(true, 0.0);
  stretch.slider.getProperties().set("bipolar", true);
  addAndMakeVisible(stretch);

  stretchAttachment = std::make_unique<SliderAttachment>(
      apvts, params::stretchId, stretch.slider);
  learn::tag(stretch, params::stretchId);

  track.slider.setPopupDisplayEnabled(true, true, &popupParent);
  track.slider.setTooltip("Makes high notes duller by rolling off the partials "
                          "above a fixed frequency, as an acoustic instrument "
                          "does. The fundamental keeps its level.");
  track.slider.setDoubleClickReturnValue(true, 0.0);
  addAndMakeVisible(track);

  trackAttachment =
      std::make_unique<SliderAttachment>(apvts, params::trackId, track.slider);
  learn::tag(track, params::trackId);

  wobble.slider.setPopupDisplayEnabled(true, true, &popupParent);
  wobble.slider.setTooltip(
      "Pitch wobble like a warped record on the whole instrument, before the "
      "echo: a slow wander with occasional sharp slips. Both increase as you "
      "turn it up.");
  wobble.slider.setDoubleClickReturnValue(true, 0.0);
  addAndMakeVisible(wobble);

  wobbleAttachment = std::make_unique<SliderAttachment>(apvts, params::wobbleId,
                                                        wobble.slider);
  learn::tag(wobble, params::wobbleId);

  addAndMakeVisible(meter);

  // Over the meter, which was added later and would otherwise paint on top.
  masterFader.toFront(false);

  rateDisplay.setTooltip(
      "The rate the instrument renders at. Lower rates save CPU, and partials "
      "above the new limit fold back down as aliasing.");

  bitsDisplay.setTooltip("The bit depth the instrument is reduced to, before "
                         "the echo, the reverb and the fader.");

  rateDisplay.onClick = [this] {
    juce::StringArray choices;
    for (auto hz : params::kLofiRateChoices)
      choices.add(params::lofiRateName(hz));

    showChoiceMenu(params::lofiRateId, choices, &rateDisplay);
  };

  bitsDisplay.onClick = [this] {
    juce::StringArray choices;
    for (auto bits : params::kLofiBitChoices)
      choices.add(params::lofiBitName(bits));

    showChoiceMenu(params::lofiBitsId, choices, &bitsDisplay);
  };

  addAndMakeVisible(rateDisplay);
  addAndMakeVisible(bitsDisplay);

  // ---- which oscillator -----------------------------------------------------
  characterButton.setTooltip(
      "The circuit every partial's oscillator is built on. Pure is an exact "
      "sine. Bulb's level lags behind pitch changes, Rail and Diode add "
      "harmonics, Valve adds a warm octave, and Op-amp hardens high notes.");

  characterButton.onClick = [this] {
    showChoiceMenu(params::characterId, params::characterChoices(),
                   &characterButton);
  };

  addAndMakeVisible(characterButton);

  // ---- the resonator
  // ---------------------------------------------------------
  styleToggle(
      resonatorButton, "RESONATOR",
      "88 sympathetic strings that resonate with what you play, before the bus "
      "drive. Piano rings the held keys' strings, and all 88 while the "
      "sustain pedal is down. Open rings all 88 as if the pedal were always "
      "down. Octave rings twelve strings from C3, and Series the tuning root's "
      "harmonics.");

  // The click opens a menu rather than toggling, and the light says what came
  // back.
  resonatorButton.setClickingTogglesState(false);

  resonatorButton.onClick = [this] {
    auto m = buildResonatorMenu();
    m.setLookAndFeel(&getLookAndFeel());

    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&resonatorButton)
                        .withStandardItemHeight(22),
                    [this](int result) { chooseResonator(result); });
  };

  // ---- presets --------------------------------------------------------------
  presetDisplay.setReading(kNoPreset, true);
  presetDisplay.setTooltip("Factory and saved presets, and saving your own");
  presetDisplay.onClick = [this] { showPresetMenu(); };
  addAndMakeVisible(presetDisplay);

  // ---- settings -------------------------------------------------------------
  macroButton.setButtonText("MACROS");
  macroButton.setTooltip("Single host parameters that each move a whole row of "
                         "knobs, so a row can be automated as one lane.");
  macroButton.setTitle("Macros");
  macroButton.setColour(juce::TextButton::textColourOnId, colours::accent);
  macroButton.onClick = [this] {
    if (onMacrosClicked != nullptr)
      onMacrosClicked();
  };
  macroButton.onIcon = [](juce::Graphics &g, juce::Rectangle<float> area,
                          juce::Colour colour) {
    drawMacroIcon(g, area, colour);
  };

  addAndMakeVisible(macroButton);

  settingsButton.setButtonText("SETTINGS");

  // The cap takes its colour from the lamp, so this one names the accent to
  // match MACROS even though it never lights for itself.
  settingsButton.setColour(juce::TextButton::textColourOnId, colours::accent);

  // A gear rather than the word, to save width. See kGroupMinWidth.
  settingsButton.onIcon = [](juce::Graphics &g, juce::Rectangle<float> area,
                             juce::Colour colour) {
    drawGearIcon(g, area, colour);
  };
  // The sections it opens, in the order it opens them.
  settingsButton.setTooltip(kSettingsTooltip);
  settingsButton.onClick = [this] { showSettingsMenu(); };
  addAndMakeVisible(settingsButton);

  // Lit by the editor while their bars are open rather than by their own
  // clicks. See setBarsOpen.
  const struct {
    GlowButton &button;
    const char *word, *title, *tooltip;
    void (*icon)(juce::Graphics &, juce::Rectangle<float>, juce::Colour);
    std::function<void()> TopBar::*callback;
  } bars[] = {
      {playingButton, "PLAYING", "Playing",
       "Opens a bar with voices, legato, pitch bend range and expression",
       drawKeyboardIcon, &TopBar::onPlayingClicked},
      {tuningButton, "TUNING", "Tuning",
       "Opens a bar with the temperament, root, reference pitch and what "
       "Adaptive is doing",
       drawTuningForkIcon, &TopBar::onTuningClicked},
  };

  for (const auto &b : bars) {
    b.button.setButtonText(b.word);
    b.button.setTitle(b.title);
    b.button.setTooltip(b.tooltip);
    b.button.setColour(juce::TextButton::textColourOnId, colours::accent);
    b.button.onIcon = b.icon;

    const auto callback = b.callback;
    b.button.onClick = [this, callback] {
      if (this->*callback)
        (this->*callback)();
    };

    addAndMakeVisible(b.button);
  }

  // ---- the master effects
  // ----------------------------------------------------
  styleToggle(echoButton, "ECHO",
              "A delay on the instrument, before the master fader: tape, "
              "bucket brigade, or a ping-pong digital delay. AGE works "
              "differently on each.");

  echoButton.onClick = [this] {
    auto m = buildEchoMenu();
    m.setLookAndFeel(&getLookAndFeel());

    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&echoButton)
                        .withStandardItemHeight(22),
                    [this](int result) { chooseEcho(result); });
  };
  styleToggle(reverbButton, "REVERB",
              "A reverb on the instrument, after the echo: a room, a "
              "modulated plate, or a spring tank that turns hits into a "
              "falling chirp.");

  reverbButton.onClick = [this] {
    auto m = buildReverbMenu();
    m.setLookAndFeel(&getLookAndFeel());

    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&reverbButton)
                        .withStandardItemHeight(22),
                    [this](int result) { chooseReverb(result); });
  };

  // The output stage, with the converter at the end of the chain. A machine
  // button like the echo's and the reverb's.
  styleToggle(clipButton, kClipName,
              "Shapes the output after the master fader. Soft rounds the "
              "peaks, Hard cuts them flat, Asymmetric bends one half of the "
              "wave first, Limiter turns the level down, Fold folds the "
              "wave back, Console is a mixing console's bus and Cassette a "
              "cassette recorder. Off lets the output go past full scale.");

  clipButton.onClick = [this] {
    auto m = buildClipMenu();
    m.setLookAndFeel(&getLookAndFeel());

    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&clipButton)
                        .withStandardItemHeight(22),
                    [this](int result) { chooseClip(result); });
  };

  addKnob(resonatorControls, "Resonator", "MIX", params::resonatorMixId,
          "From the instrument alone to the resonator alone", popupParent);
  addKnob(resonatorControls, "Resonator", "DECAY", params::resonatorDecayId,
          "How long a free string rings", popupParent);
  addKnob(resonatorControls, "Resonator", "DAMP", params::resonatorDampId,
          "How quickly the resonator loses its high end", popupParent);

  addKnob(echoControls, "Echo", "MIX", params::echoMixId,
          "How much of the output is repeats", popupParent);
  addKnob(echoControls, "Echo", "TIME", params::echoTimeId,
          "Time between repeats. Moving it winds the tape rather than cutting "
          "to the new time, so the repeats slide in pitch.",
          popupParent);
  addKnob(echoControls, "Echo", "FDBK", params::echoFeedbackId,
          "How much of each repeat goes round again", popupParent);
  addKnob(echoControls, "Echo", "AGE", params::echoAgeId,
          "How worn the machine is. Higher makes the repeats darker and less "
          "steady, through the tape, the bucket brigade's clock or the "
          "digital's bits.",
          popupParent);

  addKnob(reverbControls, "Reverb", "MIX", params::reverbMixId,
          "How much of the output is reverb", popupParent);
  addKnob(reverbControls, "Reverb", "DECAY", params::reverbDecayId,
          "How long the tail takes to die away. The room size follows it.",
          popupParent);
  addKnob(reverbControls, "Reverb", "DAMP", params::reverbDampId,
          "How quickly the tail loses its high end", popupParent);
  addKnob(reverbControls, "Reverb", "PRE", params::reverbPreDelayId,
          "Silence before the reverb starts. A little keeps the attack clear.",
          popupParent);
}

void TopBar::styleToggle(GlowButton &b, const juce::String &text,
                         const juce::String &tooltip) {
  b.setButtonText(text);
  b.setClickingTogglesState(true);
  b.setTooltip(tooltip);
  addAndMakeVisible(b);
}

void TopBar::addKnob(std::vector<Control> &into, const juce::String &group,
                     const juce::String &caption, const juce::String &paramId,
                     const juce::String &tooltip,
                     juce::Component &popupParent) {
  Control c;
  c.knob = std::make_unique<LabelledKnob>(caption);
  c.knob->slider.setTooltip(tooltip);

  // Prefixed with the group, since MIX appears in more than one. The tooltip
  // goes in the description, since a name is read out every time.
  c.knob->slider.setTitle(group + " " + caption.toLowerCase());
  c.knob->slider.setDescription(tooltip);
  c.knob->slider.setPopupDisplayEnabled(true, true, &popupParent);

  if (auto *p = apvts.getParameter(paramId))
    c.knob->slider.setDoubleClickReturnValue(
        true, (double)p->convertFrom0to1(p->getDefaultValue()));

  addAndMakeVisible(*c.knob);

  c.attachment =
      std::make_unique<SliderAttachment>(apvts, paramId, c.knob->slider);
  learn::tag(*c.knob, paramId);

  into.push_back(std::move(c));
}

void TopBar::setLinkEnabled(bool on) { linkOn = on; }

void TopBar::setLinkScope(LinkScope s) {
  scope = (LinkScope)juce::jlimit(0, (int)LinkScope::NumScopes - 1, (int)s);
}

void TopBar::setLinkCurve(LinkCurve c) {
  curve = (LinkCurve)juce::jlimit(0, (int)LinkCurve::NumCurves - 1, (int)c);
}

void TopBar::mouseDown(const juce::MouseEvent &e) {
  if (!e.mods.isPopupMenu() || onLearnRequested == nullptr)
    return;

  const auto id = learn::parameterIdAt(e.originalComponent);

  if (id.isNotEmpty())
    onLearnRequested(id);
}

void TopBar::mouseWheelMove(const juce::MouseEvent &e,
                            const juce::MouseWheelDetails &wheel) {
  if (e.originalComponent == this)
    juce::Component::mouseWheelMove(e, wheel);
}

void TopBar::setBarsOpen(bool playing, bool tuning) {
  playingButton.setToggleState(playing, juce::dontSendNotification);
  tuningButton.setToggleState(tuning, juce::dontSendNotification);
}

void TopBar::setMacrosOn(bool on) {
  // The lamp says whether any macros are made, not whether the panel is up.
  juce::ignoreUnused(on);
}

void TopBar::setMacroCount(int made) {
  if (made == macrosMade)
    return;

  macrosMade = made;

  // Lit when any are made. The count goes to the screen reader name rather
  // than the button, which has no width for it. See kGroupMinWidth.
  macroButton.setToggleState(made > 0, juce::dontSendNotification);
  macroButton.setTitle(made > 0 ? "Macros, " + juce::String(made) + " made"
                                : juce::String("Macros"));
}

void TopBar::showLinkMenu(juce::Component *anchor,
                          const juce::String &parameterId, MidiLearn *map,
                          PointerTool tool) {
  const LinkSettings settings{linkOn, scope, curve, hasSelection};

  auto m = buildToolMenu(tool, settings);

  // MIDI Learn items are added to the end of the LINK menu.
  auto *parameter = dynamic_cast<juce::RangedAudioParameter *>(
      parameterId.isEmpty() ? nullptr : apvts.getParameter(parameterId));

  // The glide switches first, on a glide knob only.
  glide::appendItems(m, apvts, parameterId);

  if (map != nullptr)
    learn::appendItems(m, *map, parameter);
  m.setLookAndFeel(&getLookAndFeel());

  auto options = juce::PopupMenu::Options().withStandardItemHeight(22);

  if (anchor != nullptr) {
    options = options.withTargetComponent(anchor);
  } else {
    // Under the pointer.
    const auto p = juce::Desktop::getInstance()
                       .getMainMouseSource()
                       .getScreenPosition()
                       .roundToInt();

    options = options.withTargetScreenArea({p.x, p.y, 1, 1});
  }

  m.showMenuAsync(options, [this, settings, map, parameter](int result) {
    if (map != nullptr && learn::applyChoice(result, *map, parameter))
      return;

    if (glide::applyChoice(result, apvts))
      return;

    if (isClearSelectionChoice(result)) {
      if (onSelectionCleared != nullptr)
        onSelectionCleared();

      return;
    }

    auto picked = PointerTool::Pointer;

    if (applyToolMenuChoice(result, picked)) {
      if (onToolChosen != nullptr)
        onToolChosen(picked);

      return;
    }

    auto chosen = settings;

    if (!applyLinkMenuChoice(result, chosen))
      return;

    linkOn = chosen.enabled;
    scope = chosen.scope;
    curve = chosen.curve;

    if (onLinkSettingsChanged)
      onLinkSettingsChanged();
  });
}

void TopBar::setPresetName(const juce::String &name) {
  presetName = name;

  // Lit either way, the placeholder included.
  presetDisplay.setReading(name.isEmpty() ? kNoPreset : name, true);

  // The title names the reading for a screen reader, since the cells are
  // drawn rather than written.
  presetDisplay.setTitle(name.isEmpty() ? "Preset: none loaded"
                                        : "Preset: " + name);
}

juce::String TopBar::getPresetName() const { return presetName; }

juce::PopupMenu TopBar::buildClipMenu() {
  juce::PopupMenu m;

  auto *on = apvts.getParameter(params::safetyClipId);
  auto *type = apvts.getParameter(params::clipTypeId);

  const bool running = on != nullptr && on->getValue() > 0.5f;
  const auto which =
      type == nullptr
          ? 0
          : juce::roundToInt(type->convertFrom0to1(type->getValue()));

  m.addItem(1, "Off", true, !running);
  m.addSeparator();

  for (int i = 0; i < (int)ClipType::NumTypes; ++i)
    m.addItem(i + 2, clipTypeName((ClipType)i), true, running && i == which);

  return m;
}

void TopBar::chooseClip(int id) {
  if (id <= 0)
    return;

  auto *on = apvts.getParameter(params::safetyClipId);
  auto *type = apvts.getParameter(params::clipTypeId);

  if (on == nullptr)
    return;

  // Off writes the switch alone, so coming back on returns to the shape that
  // was chosen.
  if (id == 1) {
    on->setValueNotifyingHost(0.0f);
    return;
  }

  if (type != nullptr)
    type->setValueNotifyingHost(type->convertTo0to1((float)(id - 2)));

  on->setValueNotifyingHost(1.0f);
}

juce::PopupMenu TopBar::buildEchoMenu() {
  juce::PopupMenu m;

  auto *on = apvts.getParameter(params::echoOnId);
  auto *type = apvts.getParameter(params::echoTypeId);

  const bool running = on != nullptr && on->getValue() > 0.5f;
  const auto which =
      type == nullptr
          ? 0
          : juce::roundToInt(type->convertFrom0to1(type->getValue()));

  m.addItem(1, "Off", true, !running);
  m.addSeparator();

  for (int i = 0; i < (int)EchoType::NumTypes; ++i)
    m.addItem(i + 2, echoTypeName((EchoType)i), true, running && i == which);

  return m;
}

void TopBar::chooseEcho(int id) {
  if (id <= 0)
    return;

  auto *on = apvts.getParameter(params::echoOnId);
  auto *type = apvts.getParameter(params::echoTypeId);

  if (on == nullptr)
    return;

  // Off writes the switch alone, so coming back on returns to the machine that
  // was chosen.
  if (id == 1) {
    on->setValueNotifyingHost(0.0f);
    return;
  }

  if (type != nullptr)
    type->setValueNotifyingHost(type->convertTo0to1((float)(id - 2)));

  on->setValueNotifyingHost(1.0f);
}

juce::PopupMenu TopBar::buildResonatorMenu() {
  juce::PopupMenu m;

  auto *mode = apvts.getParameter(params::resonatorModeId);
  const auto which =
      mode == nullptr
          ? 0
          : juce::roundToInt(mode->convertFrom0to1(mode->getValue()));

  // Off apart from the rest.
  for (int i = 0; i < (int)ResonatorMode::NumModes; ++i) {
    m.addItem(i + 1, resonatorModeName((ResonatorMode)i), true, i == which);

    if (i == (int)ResonatorMode::Off)
      m.addSeparator();
  }

  return m;
}

void TopBar::chooseResonator(int id) {
  if (id <= 0 || id > (int)ResonatorMode::NumModes)
    return;

  if (auto *mode = apvts.getParameter(params::resonatorModeId))
    mode->setValueNotifyingHost(mode->convertTo0to1((float)(id - 1)));
}

juce::PopupMenu TopBar::buildReverbMenu() {
  juce::PopupMenu m;

  auto *on = apvts.getParameter(params::reverbOnId);
  auto *type = apvts.getParameter(params::reverbTypeId);

  const bool running = on != nullptr && on->getValue() > 0.5f;
  const auto which =
      type == nullptr
          ? 0
          : juce::roundToInt(type->convertFrom0to1(type->getValue()));

  m.addItem(1, "Off", true, !running);
  m.addSeparator();

  for (int i = 0; i < (int)ReverbType::NumTypes; ++i)
    m.addItem(i + 2, reverbTypeName((ReverbType)i), true,
              running && i == which);

  return m;
}

void TopBar::chooseReverb(int id) {
  if (id <= 0)
    return;

  auto *on = apvts.getParameter(params::reverbOnId);
  auto *type = apvts.getParameter(params::reverbTypeId);

  if (on == nullptr)
    return;

  // Off leaves the type alone, as the echo's does, so coming back on returns
  // to the machine that was chosen.
  if (id == 1) {
    on->setValueNotifyingHost(0.0f);
    return;
  }

  if (type != nullptr)
    type->setValueNotifyingHost(type->convertTo0to1((float)(id - 2)));

  on->setValueNotifyingHost(1.0f);
}

void TopBar::showChoiceMenu(const char *paramId,
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

namespace {
/// One level of the saved-preset menu: the folders inside it, and the presets
/// sitting loose in it. A map, so folders are sorted by name.
struct SavedFolder {
  std::map<juce::String, SavedFolder> folders;
  std::vector<std::pair<juce::String, int>> presets;
};

/// Folders first, then the presets loose in this one, nested as deeply as the
/// folders are.
juce::PopupMenu buildSavedMenu(const SavedFolder &folder,
                               const juce::String &current) {
  juce::PopupMenu m;

  for (const auto &child : folder.folders)
    m.addSubMenu(child.first, buildSavedMenu(child.second, current));

  for (const auto &preset : folder.presets)
    m.addItem(preset.second, preset.first, true, preset.first == current);

  return m;
}
} // namespace

juce::PopupMenu TopBar::buildPresetMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  // The loaded preset is ticked in its submenu.
  const auto current = getPresetName();

  juce::PopupMenu factoryMenu;
  const auto factory = presets::names();

  for (int i = 0; i < factory.size(); ++i)
    factoryMenu.addItem(100 + i, factory[i], true, factory[i] == current);

  m.addSubMenu("Factory", factoryMenu);

  // Read fresh every time it opens, so a preset saved a moment ago is there
  // and one deleted in Finder is not.
  userPresetFiles = presets::userPresets();

  // Only when there is something in it.
  if (!userPresetFiles.isEmpty()) {
    const auto root = presets::userDirectory();
    SavedFolder tree;

    for (int i = 0; i < userPresetFiles.size(); ++i) {
      const auto &file = userPresetFiles.getReference(i);

      // The preset's folder under the preset directory is its group.
      const auto relative = file.getParentDirectory().getRelativePathFrom(root);

      auto *folder = &tree;

      for (const auto &part :
           juce::StringArray::fromTokens(relative, "/\\", ""))
        if (part.isNotEmpty() && part != ".")
          folder = &folder->folders[part];

      folder->presets.emplace_back(file.getFileNameWithoutExtension(),
                                   1000 + i);
    }

    m.addSubMenu("Saved", buildSavedMenu(tree, current));
  }

  m.addSeparator();
  m.addItem(1, "Save preset...");
  m.addItem(2, "Open the preset folder");
  // Puts C++ on the clipboard for pasting into Presets.cpp. Only in builds
  // with OVERTONIUM_PRESET_AUTHORING. See CMakeLists.
#if OVERTONIUM_PRESET_AUTHORING
  m.addSeparator();
  m.addItem(3, "Copy as factory preset code");
#endif

  return m;
}

void TopBar::showPresetMenu() {
  auto m = buildPresetMenu();

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(&presetDisplay)
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

juce::String TopBar::versionLine() {
  // OVERTONIUM_VERSION comes from project() in CMakeLists, which the release
  // workflow checks against the tag.
  return juce::String("Overtonium ") + OVERTONIUM_VERSION;
}

juce::PopupMenu TopBar::buildSettingsMenu() {
  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  auto *phase = apvts.getParameter(params::phaseResetId);
  auto *clip = apvts.getParameter(params::safetyClipId);
  auto *ahead = apvts.getParameter(params::lookaheadId);

  // A list the bars offer too, laid into this menu in place rather than a
  // level down.
  const auto addInPlace = [&m](const juce::PopupMenu &list) {
    for (juce::PopupMenu::MenuItemIterator it(list); it.next();)
      m.addItem(it.getItem());
  };

  // At the very top: a new release if there is one, then the version as a
  // header, since it is a fact rather than a choice. The bug report template
  // asks for this version.
  if (updateVersion.isNotEmpty()) {
    m.addItem(703, "Overtonium " + updateVersion + " is available...");
    m.addSeparator();
  }

  m.addSectionHeader(versionLine());

  m.addItem(702, "Check for new versions", true,
            isUpdateCheckAllowed && isUpdateCheckAllowed());
  m.addSeparator();

  m.addItem(700, "Undo", canUndo && canUndo());
  m.addItem(701, "Redo", canRedo && canRedo());
  m.addSeparator();

  m.addSectionHeader("Polyphony");

  // The voice counts, then how many voices one key may take, and whether a
  // key going down while another is held starts a note or carries one on.
  addInPlace(settings::voices(apvts));

  m.addItem(settings::kOneVoicePerKey, "No stack", true,
            settings::isOn(apvts, params::oneVoicePerKeyId));
  m.addItem(settings::kLegato, "Legato", true, settings::legatoOn(apvts));

  m.addSeparator();
  m.addSectionHeader("Pitch bend range");

  addInPlace(settings::bendRange(apvts));

  // How what the controller sends reaches the sound. Directly under the bend
  // range, since MPE changes what bend means.
  m.addSeparator();
  m.addSectionHeader("Expression");
  m.addItem(settings::kMpe, "MPE", true, settings::isOn(apvts, params::mpeId));
  m.addSubMenu("Aftertouch from", settings::aftertouchFrom(apvts));
  m.addSubMenu("Slide to", settings::slideTo(apvts));

  // The keyboard's tuning.
  const auto temperamentIndex =
      settings::choiceIndex(apvts, params::temperamentId);

  m.addSeparator();
  m.addSectionHeader("Tuning");
  m.addSubMenu("Temperament", settings::temperament(apvts));

  // Greyed out when nothing uses the root.
  m.addSubMenu("Root", settings::root(apvts), settings::rootInUse(apvts));

  m.addSubMenu("Reference pitch", settings::referencePitch(apvts));

  // How fast the adaptive temperament moves a held note. Adaptive only.
  m.addSubMenu("Retune", settings::retune(apvts),
               temperamentIndex == (int)Temperament::Adaptive);

  // Whether Adaptive puts a seventh on 7:4. Adaptive only.
  m.addItem(settings::kHarmonicSevenths, "Harmonic sevenths",
            temperamentIndex == (int)Temperament::Adaptive,
            settings::isOn(apvts, params::adaptiveSeventhId));

  // Whether Adaptive stays home rather than letting the keyboard drift.
  m.addItem(settings::kAdaptiveHome, "No drift",
            temperamentIndex == (int)Temperament::Adaptive,
            settings::isOn(apvts, params::adaptiveHomeId));

  juce::PopupMenu zooms;

  for (int i = 0; i < (int)std::size(kZoomChoices); ++i) {
    const auto percent = juce::roundToInt(kZoomChoices[i] * 100.0f);

    zooms.addItem(800 + i, juce::String(percent) + "%", true,
                  std::abs(kZoomChoices[i] - zoom) < 0.01f);
  }

  m.addSeparator();
  m.addSectionHeader("Output");
  m.addItem(300, "Phase reset", true,
            phase != nullptr && phase->getValue() > 0.5f);
  m.addItem(301, "Safety clip", true,
            clip != nullptr && clip->getValue() > 0.5f);

  // The clipper's limiter. It changes the latency reported to the host, so
  // the item says so.
  m.addItem(304, "Limiter lookahead (2 ms latency)", true,
            ahead != nullptr && ahead->getValue() > 0.5f);

  // Last, and apart, since it is about the window rather than the instrument.
  m.addSeparator();
  m.addSubMenu("Zoom", zooms);

  // Beside the zoom, since both are about the window.
  m.addItem(820, "Fit all 32 channels");

  return m;
}

void TopBar::showSettingsMenu() {
  auto m = buildSettingsMenu();

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(&settingsButton)
                      .withStandardItemHeight(22),
                  [this](int result) {
                    if (result == 0)
                      return;

                    // Everything the bars offer as well.
                    if (settings::apply(apvts, result))
                      return;

                    if (result == 820)
                      return onFitAllChannels ? onFitAllChannels() : void();

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

                    if (result == 703) {
                      juce::URL(updateUrl).launchInDefaultBrowser();
                      return;
                    }

                    if (result == 702) {
                      const bool now =
                          isUpdateCheckAllowed && isUpdateCheckAllowed();

                      if (onUpdateCheckToggled)
                        onUpdateCheckToggled(!now);

                      return;
                    }
                  });
}

/// Lights or darkens the ring on every knob of an effect. Only repaints the
/// ones that change, since this runs on the housekeeping tick.
void TopBar::setRingsLive(std::vector<Control> &controls, bool live) {
  for (auto &c : controls) {
    auto &slider = c.knob->slider;

    const bool wasUnlit =
        (bool)slider.getProperties().getWithDefault("unlit", false);

    if (wasUnlit == !live)
      continue;

    slider.getProperties().set("unlit", !live);
    slider.repaint();
  }
}

/// Likewise only repaints a knob whose colour changes.
void TopBar::setRingColour(juce::Slider &slider, juce::Colour colour) {
  if (slider.findColour(juce::Slider::rotarySliderFillColourId) == colour)
    return;

  slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
  slider.repaint();
}

void TopBar::setRingColour(std::vector<Control> &controls,
                           juce::Colour colour) {
  for (auto &c : controls)
    setRingColour(c.knob->slider, colour);
}

void TopBar::updatePanelReadouts(double hostSampleRate) {
  const auto chosen = [this](const char *id, int count) {
    auto *p = apvts.getParameter(id);

    return p == nullptr ? 0
                        : juce::jlimit(0, count - 1,
                                       juce::roundToInt(
                                           p->convertFrom0to1(p->getValue())));
  };

  const auto rate = params::kLofiRateChoices[(size_t)chosen(
      params::lofiRateId, (int)params::kLofiRateChoices.size())];

  // The readout shows the rate in effect, not one above the host's. Before
  // the first prepareToPlay the host rate is unknown, so a choice shows as is.
  const bool known = hostSampleRate > 0.0;
  const bool cutting = rate > 0 && (!known || (double)rate < hostSampleRate);
  const auto shown = cutting ? (double)rate : hostSampleRate;

  const auto rateReading =
      shown > 0.0 ? juce::String(shown / 1000.0, 1) : juce::String();

  // Lit whether it is cutting or not.
  rateDisplay.setReading(rateReading, true);

  // The title carries the reading too, since a screen reader cannot read the
  // drawn digits.
  rateDisplay.setTitle("Sample rate: " + (rateReading.isNotEmpty()
                                              ? rateReading + " kHz"
                                              : juce::String("unknown")));

  const auto bits = params::kLofiBitChoices[(size_t)chosen(
      params::lofiBitsId, (int)params::kLofiBitChoices.size())];

  // Nothing being quantised means the 32-bit float everything else runs in.
  bitsDisplay.setReading(juce::String(bits > 0 ? bits : 32), true);
  bitsDisplay.setTitle("Bit depth: " + juce::String(bits > 0 ? bits : 32) +
                       " bit");

  // Read back from the parameter, since a preset can change it.
  const auto character =
      chosen(params::characterId, params::characterChoices().size());

  // ---- which delay, and whether there is one -------------------------------
  //
  // The machine's name when it is running and OFF when it is not.
  {
    auto *on = apvts.getParameter(params::echoOnId);
    auto *type = apvts.getParameter(params::echoTypeId);

    const bool running = on != nullptr && on->getValue() > 0.5f;
    const auto which = type == nullptr
                           ? EchoType::Tape
                           : (EchoType)juce::roundToInt(
                                 type->convertFrom0to1(type->getValue()));

    const juce::String name = echoTypeName(which);

    echoButton.setButtonText(
        running ? juce::String(echoTypeShortName(which)).toUpperCase()
                : juce::String("OFF"));

    // The title says which control it is, for a screen reader.
    echoButton.setTitle(running ? "Echo: " + name : juce::String("Echo: off"));

    // The lamp colour, one for each machine. See GlowButton.
    echoButton.setColour(juce::TextButton::textColourOnId, echoColour(which));
    echoButton.setToggleState(running, juce::dontSendNotification);

    setRingColour(echoControls, echoColour(which));
    setRingsLive(echoControls, running);
  }

  // ---- and what the resonator is tuned to, by the same rule ----------------
  {
    auto *mode = apvts.getParameter(params::resonatorModeId);

    const auto which =
        mode == nullptr
            ? ResonatorMode::Off
            : (ResonatorMode)juce::jlimit(
                  0, (int)ResonatorMode::NumModes - 1,
                  juce::roundToInt(mode->convertFrom0to1(mode->getValue())));

    const bool running = which != ResonatorMode::Off;
    const juce::String name = resonatorModeName(which);

    resonatorButton.setButtonText(name.toUpperCase());
    resonatorButton.setTitle(running ? "Resonator: " + name
                                     : juce::String("Resonator: off"));
    resonatorButton.setColour(juce::TextButton::textColourOnId,
                              resonatorColour(which));
    resonatorButton.setToggleState(running, juce::dontSendNotification);

    // Off has no colour of its own, and its rings are out anyway.
    if (running)
      setRingColour(resonatorControls, resonatorColour(which));

    setRingsLive(resonatorControls, running);
  }

  // ---- and which reverb, by the same rule ----------------------------------
  {
    auto *on = apvts.getParameter(params::reverbOnId);
    auto *type = apvts.getParameter(params::reverbTypeId);

    const bool running = on != nullptr && on->getValue() > 0.5f;
    const auto which = type == nullptr
                           ? ReverbType::Room
                           : (ReverbType)juce::roundToInt(
                                 type->convertFrom0to1(type->getValue()));

    const juce::String name = reverbTypeName(which);

    // The short name, as the echo does it: the menu has room to say
    // "Modulated Plate" and a button on the bar has not.
    reverbButton.setButtonText(
        running ? juce::String(reverbTypeShortName(which)).toUpperCase()
                : juce::String("OFF"));

    reverbButton.setTitle(running ? "Reverb: " + name
                                  : juce::String("Reverb: off"));
    reverbButton.setColour(juce::TextButton::textColourOnId,
                           reverbColour(which));
    reverbButton.setToggleState(running, juce::dontSendNotification);

    setRingColour(reverbControls, reverbColour(which));
    setRingsLive(reverbControls, running);
  }

  // ---- and which output stage, by the same rule ----------------------------
  {
    auto *on = apvts.getParameter(params::safetyClipId);
    auto *type = apvts.getParameter(params::clipTypeId);

    const bool running = on != nullptr && on->getValue() > 0.5f;
    const auto which = type == nullptr
                           ? ClipType::Soft
                           : (ClipType)juce::roundToInt(
                                 type->convertFrom0to1(type->getValue()));

    const juce::String name = clipTypeName(which);

    // The short form, to fit the button. See clipTypeShortName.
    clipButton.setButtonText(
        running ? juce::String(clipTypeShortName(which)).toUpperCase()
                : juce::String("OFF"));

    // Named after the caption on the bar.
    clipButton.setTitle(running ? "Clip: " + name : juce::String("Clip: off"));
    clipButton.setColour(juce::TextButton::textColourOnId, clipColour(which));
    clipButton.setToggleState(running, juce::dontSendNotification);
  }

  // In capitals, like every other word on the bar.
  characterButton.setButtonText(
      params::characterChoices()[character].toUpperCase());

  // The title says which control it is, for a screen reader.
  characterButton.setTitle("Character: " +
                           params::characterChoices()[character]);

  // Lit in the character's colour, and not at all on Pure.
  const auto which = (Character)character;

  characterButton.setColour(juce::TextButton::textColourOnId,
                            characterColour(which));
  characterButton.setToggleState(which != Character::Pure,
                                 juce::dontSendNotification);

  // The knobs in its frame light with it, in the accent on Pure.
  const auto rings =
      which == Character::Pure ? colours::accent : characterColour(which);

  for (auto *knob : {&stretch, &track, &wobble})
    setRingColour(knob->slider, rings);
}

void TopBar::setZoomChoice(float newZoom) { zoom = newZoom; }

TopBar::RowPlan TopBar::planRows(int fullWidth, int maxRows) {
  RowPlan plan;
  plan.start.fill(kGroupCount);
  plan.start[0] = 0;

  int group = 0;
  int row = 0;

  // Greedy, filling each row as far as it will go.
  while (group < kGroupCount && row < maxRows) {
    const auto available = fullWidth;

    int used = 0;
    int onThisRow = 0;

    while (group < kGroupCount) {
      auto width =
          kGroupMinWidth[group] + (onThisRow > 0 ? gapBefore(group) : 0);

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

  // A last row holding one small group pulls a group down from a fuller row
  // above.
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
  const auto rows = planRows(width - 2 * kBarMargin, kGroupCount).rows;

  return rows * kRowHeight + (rows - 1) * kRowGap + kBarPadTop + kBarPadBottom;
}

int TopBar::minimumWidth() {
  // The narrowest window where the bar still fits in kMaxComfortableRows.
  for (int width = 2 * kBarMargin + 1; width < 4000; ++width) {
    if (planRows(width - 2 * kBarMargin, kGroupCount).rows <=
        kMaxComfortableRows)
      return width;
  }

  jassertfalse;
  return 1024;
}

void TopBar::parkControls() {
  juce::Component *all[] = {&masterFader,     &meter,          &presetDisplay,
                            &settingsButton,  &playingButton,  &tuningButton,
                            &echoButton,      &reverbButton,   &stretch,
                            &track,           &rateDisplay,    &bitsDisplay,
                            &characterButton, &resonatorButton};

  for (auto *c : all)
    c->setBounds({});

  for (auto *set : {&resonatorControls, &echoControls, &reverbControls})
    for (auto &c : *set)
      c.knob->setBounds({});

  groupBounds.fill({});
}

void TopBar::placeGroup(int group, juce::Rectangle<int> bounds) {
  groupBounds[(size_t)group] = bounds;

  // The groups with no frame use their whole width.
  const bool framed = group != VoiceGroup && group != PresetGroup;
  auto r = framed
               ? bounds.reduced(kGroupPad, 0).withTrimmedBottom(kBelowControls)
               : bounds;

  // Everything in the bar lines up with the dials, which sit above the middle
  // of the row because their captions are underneath.
  const auto alignedWithDials = [](juce::Component &c,
                                   juce::Rectangle<int> column) {
    const auto dial = LabelledKnob::dialBounds(column);

    c.setBounds(column.getX(), dial.getCentreY() - kControlHeight / 2,
                column.getWidth(), kControlHeight);
  };

  const auto button = [&](juce::Button &b, juce::Rectangle<int> column) {
    alignedWithDials(b, column);
  };

  // The button at the head of its group and the knobs after it.
  const auto effect = [&](juce::Button &toggle, std::vector<Control> &controls,
                          juce::Rectangle<int> area, int toggleWidth) {
    button(toggle, area.removeFromLeft(toggleWidth));
    area.removeFromLeft(kFxToggleGap);

    for (auto &c : controls)
      c.knob->setBounds(area.removeFromLeft(kKnobWidth));
  };

  switch (group) {
  case PresetGroup:
    // The preset screen stands on the row's floor, and paint puts the
    // wordmark above it.
    presetDisplay.setBounds(r.withTop(r.getBottom() - kControlHeight));
    break;

  case VoiceGroup: {
    // The four buttons, two by two, filling the row's height rather than
    // lining up with the dials.
    const auto half = (r.getWidth() - kButtonGap) / 2;
    const auto height = (r.getHeight() - kButtonGap) / 2;

    auto top = r.withHeight(height);
    auto bottom = r.withTop(r.getBottom() - height);

    settingsButton.setBounds(top.removeFromLeft(half));
    top.removeFromLeft(kButtonGap);
    macroButton.setBounds(top);

    playingButton.setBounds(bottom.removeFromLeft(half));
    bottom.removeFromLeft(kButtonGap);
    tuningButton.setBounds(bottom);
    break;
  }

  case ResonatorGroup:
    effect(resonatorButton, resonatorControls, r, kResonatorWidth);
    break;

  case SeriesGroup: {
    // The character at the head of the group.
    button(characterButton, r.removeFromLeft(kCharacterWidth));
    r.removeFromLeft(kFxToggleGap);

    // Split evenly rather than at the usual knob width, so the caption
    // STRETCH is not cut.
    const auto each = r.getWidth() / 3;

    stretch.setBounds(r.removeFromLeft(each));
    track.setBounds(r.removeFromLeft(each));
    wobble.setBounds(r);
    break;
  }

  case EchoGroup:
    effect(echoButton, echoControls, r, kEchoWidth);
    break;

  case ReverbGroup:
    effect(reverbButton, reverbControls, r, kReverbWidth);
    break;

  case OutputGroup: {
    // The meter takes the whole group, and the fader lies over it.
    alignedWithDials(meter, r);

    // Over the lamps rather than the scale, standing a little past them at
    // both ends. See StereoOutputMeter::barBounds.
    masterFader.setBounds(
        meter.getBounds()
            .withTrimmedBottom(StereoOutputMeter::kScaleHeight)
            .expanded(0, kFaderOverhang));

    // The readouts go under the meter, in the band the knob captions use,
    // hard up under the meter and held off the group's bottom border.
    auto below = r.withTop(meter.getBottom() + 1).withTrimmedBottom(3);

    // The rate, the bits and the clipper, centred as a block. The readouts
    // take what is left after the clipper, up to 66 px.
    const auto each =
        juce::jmin(66, (below.getWidth() - kFxToggleGap * 2 - kClipWidth) / 2);

    auto trio = below.withSizeKeepingCentre(
        each * 2 + kFxToggleGap * 2 + kClipWidth, below.getHeight());

    rateDisplay.setBounds(trio.removeFromLeft(each));
    trio.removeFromLeft(kFxToggleGap);
    bitsDisplay.setBounds(trio.removeFromLeft(each));
    trio.removeFromLeft(kFxToggleGap);
    // The clipper grows by its bezel, measured off the look and feel, so its
    // lit cap is as tall as the readouts beside it.
    const auto bezel = juce::roundToInt(
        (trio.toFloat().getHeight() -
         OvertoniumLookAndFeel::lampCapBounds(
             trio.toFloat(), OvertoniumLookAndFeel::LampGang::Alone)
             .getHeight()) *
        0.5f);

    // Upwards only, and no higher than the meter's bottom edge. The runtime
    // tests check all three sit under the meter and inside the bar.
    auto lit = trio;
    lit.setTop(juce::jmax(trio.getY() - bezel, meter.getBottom()));

    clipButton.setBounds(lit);
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
      row.removeFromLeft(gapBefore(g));

    auto width = kGroupMinWidth[g];
    if (g == kOutputGroupIndex && hasOutput)
      width += outputAdjust;

    // Never more than is left, so a very narrow window crops the last group
    // rather than overlapping groups.
    width = juce::jmin(width, row.getWidth());
    if (width < 40)
      break;

    placeGroup(g, row.removeFromLeft(width));
  }
}

void TopBar::resized() {
  // Rescaled at twice the size it is drawn, so it stays sharp on a high
  // density display.
  if (logo.isValid()) {
    const auto height = kRowHeight * 2;
    const auto width = juce::roundToInt((float)height * (float)logo.getWidth() /
                                        (float)juce::jmax(1, logo.getHeight()));

    if (logoScaled.getHeight() != height)
      logoScaled =
          logo.rescaled(width, height, juce::Graphics::highResamplingQuality);
  }

  parkControls();

  auto area = getLocalBounds()
                  .reduced(kBarMargin, 0)
                  .withTrimmedTop(kBarPadTop)
                  .withTrimmedBottom(kBarPadBottom);
  const auto fullWidth = area;

  const auto affordable =
      juce::jmax(1, (area.getHeight() + kRowGap) / (kRowHeight + kRowGap));

  const auto plan = planRows(fullWidth.getWidth(), affordable);

  for (int row = 0; row < plan.rows; ++row) {
    if (row > 0)
      area.removeFromTop(kRowGap);

    auto line = area.removeFromTop(kRowHeight);

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

  // A panel behind each group with more than one control, untitled.
  for (int group = 0; group < NumGroups; ++group) {
    const auto &r = groupBounds[(size_t)group];

    const bool framed = group == SeriesGroup || group == ResonatorGroup ||
                        group == EchoGroup || group == ReverbGroup ||
                        group == OutputGroup;

    if (r.isEmpty() || !framed)
      continue;

    paintGroupPocket(g, r);
  }

  // A caption under the button at the head of each group, saying what it
  // chooses, since the button shows a value. The output group gets none.
  {
    const struct {
      int group;
      const juce::Component *head;
    } named[] = {
        {SeriesGroup, &characterButton},
        {ResonatorGroup, &resonatorButton},
        {EchoGroup, &echoButton},
        {ReverbGroup, &reverbButton},
    };

    g.setColour(colours::textDim);
    g.setFont(makeFont(9.0f, true));

    for (size_t i = 0; i < std::size(named); ++i) {
      const auto &n = named[i];
      const auto &r = groupBounds[(size_t)n.group];

      if (r.isEmpty())
        continue;

      // Centred under the button, as the knob captions are.
      const auto band =
          LabelledKnob::captionBounds(r.withTrimmedBottom(kBelowControls))
              .withX(n.head->getX())
              .withWidth(n.head->getWidth());

      g.drawText(kGroupNames[i], band, juce::Justification::centred, false);
    }
  }

  // The wordmark, the width of the preset screen under it, its top level with
  // the buttons and the frames.
  const auto &preset = groupBounds[(size_t)PresetGroup];

  if (preset.isEmpty())
    return;

  const auto above = preset.withBottom(presetDisplay.getY() - kWordmarkGap);

  if (logoScaled.isValid()) {
    g.drawImage(logoScaled, above.toFloat(),
                juce::RectanglePlacement::xMid |
                    juce::RectanglePlacement::yTop |
                    juce::RectanglePlacement::onlyReduceInSize);
    return;
  }

  // Without the logo image, the name in text.
  g.setColour(colours::text);
  g.setFont(makeFont(18.0f, true));
  g.drawText("OVERTONIUM", above, juce::Justification::centred, false);
}

void TopBar::setUpdateAvailable(const juce::String &version,
                                const juce::String &url) {
  if (version == updateVersion)
    return;

  updateVersion = version;
  updateUrl = url;

  // Lit while a new release is available.
  settingsButton.setToggleState(version.isNotEmpty(),
                                juce::dontSendNotification);
  settingsButton.setTooltip(
      version.isNotEmpty()
          ? "Overtonium " + version +
                " is available. It is the first item in this menu."
          : juce::String(kSettingsTooltip));
}

} // namespace ovt::ui
