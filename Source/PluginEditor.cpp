#include "PluginEditor.h"

#include "Presets.h"
#include "UI/Theme.h"

using namespace ovt;
using namespace ovt::ui;

namespace {
constexpr int kEdge = 4;
constexpr int kScrollBarThickness = 10;
/// Breathing room between the noise channel and the scrolling series.
constexpr int kMasterGap = 8;

/// Everything above and below the strips. The top bar reflows onto a second
/// row when narrow, so its height depends on the width it is given.
int chromeHeight(int logicalWidth) {
  return ovt::ui::TopBar::heightForWidth(logicalWidth) + 2 * kEdge +
         kScrollBarThickness;
}

/// State keys stored alongside the parameters so window size survives a reopen.
const juce::Identifier kEditorWidth{"editorWidth"};
const juce::Identifier kEditorHeight{"editorHeight"};
const juce::Identifier kEditorZoom{"editorZoom"};
const juce::Identifier kLinkScope{"linkScope"};
const juce::Identifier kLinkCurve{"linkCurve"};

bool isHeadingRow(Row r) {
  return r == Row::PitchModHeading || r == Row::EnvHeading ||
         r == Row::AmpModHeading || r == Row::OutputHeading;
}
} // namespace

// =============================================================================

void RowGutter::paint(juce::Graphics &g) {
  paintChannelBackground(g, getLocalBounds(), colours::panel.darker(0.25f));

  const auto rows = layoutRows(getLocalBounds().reduced(0, 4));

  for (int i = 0; i < kNumRows; ++i) {
    const auto row = (Row)i;
    const auto *text = rowLabel(row);

    if (text == nullptr)
      continue;

    auto area = rows[(size_t)i].reduced(8, 0);

    // "LEVEL" sits at the top of the tall fader row rather than floating in the
    // middle.
    if (row == Row::Fader)
      area = area.removeFromTop(16);

    const bool heading = isHeadingRow(row);

    g.setFont(makeFont(heading ? 10.0f : 9.5f, heading));
    g.setColour(heading ? colours::text : colours::textDim);
    g.drawText(text, area, juce::Justification::centredRight, false);
  }

  g.setColour(colours::outline);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

// =============================================================================

OvertoniumEditor::OvertoniumEditor(OvertoniumProcessor &p)
    : juce::AudioProcessorEditor(&p), processor(p), topBar(p.apvts, *this),
      noiseStrip(p.apvts, *this) {
  setLookAndFeel(&lookAndFeel);

  addAndMakeVisible(content);
  content.addAndMakeVisible(topBar);
  content.addAndMakeVisible(gutter);
  content.addAndMakeVisible(noiseStrip);
  content.addAndMakeVisible(viewport);

  viewport.setViewedComponent(&stripsHolder, false);
  viewport.setScrollBarsShown(false,
                              true); // horizontal only; rows must stay aligned
  viewport.setScrollBarThickness(kScrollBarThickness);

  strips.reserve(kNumHarmonics);
  for (int i = 0; i < kNumHarmonics; ++i) {
    auto strip =
        std::make_unique<ChannelStrip>(processor.apvts, *this, *this, i);
    stripsHolder.addAndMakeVisible(*strip);
    strips.push_back(std::move(strip));
  }

  topBar.onPresetChosen = [this](int index) { applyPreset(index); };
  topBar.onZoomChanged = [this](float z) { setZoom(z); };

  // ---- restore the last window size -----------------------------------------
  const auto &state = processor.apvts.state;

  zoom = (float)(double)state.getProperty(kEditorZoom, 1.0);
  zoom = juce::jlimit(0.5f, 2.0f, zoom);
  topBar.setZoomChoice(zoom);

  topBar.setLinkScope((LinkScope)juce::jlimit(
      0, (int)LinkScope::NumScopes - 1, (int)state.getProperty(kLinkScope, 0)));
  topBar.setLinkCurve((LinkCurve)juce::jlimit(
      0, (int)LinkCurve::NumCurves - 1, (int)state.getProperty(kLinkCurve, 0)));

  topBar.onLinkSettingsChanged = [this] {
    auto &tree = processor.apvts.state;
    tree.setProperty(kLinkScope, (int)topBar.getLinkScope(), nullptr);
    tree.setProperty(kLinkCurve, (int)topBar.getLinkCurve(), nullptr);
  };

  // Default size shows all 32 strips at once, which is the whole point of the
  // layout.
  const int defaultWidth = kGutterWidth + kStripWidth + kMasterGap +
                           kNumHarmonics * kStripWidth + 2 * kEdge;
  const int defaultHeight = chromeHeight(defaultWidth) + preferredStripHeight();

  const int savedWidth = (int)state.getProperty(kEditorWidth, defaultWidth);
  const int savedHeight = (int)state.getProperty(kEditorHeight, defaultHeight);

  setResizable(true, true);
  applyResizeLimits();

  setSize(juce::roundToInt(savedWidth * zoom),
          juce::roundToInt(savedHeight * zoom));

  startTimerHz(30);
}

OvertoniumEditor::~OvertoniumEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void OvertoniumEditor::paint(juce::Graphics &g) {
  const auto r = getLocalBounds().toFloat();

  g.setGradientFill(juce::ColourGradient(
      colours::background.brighter(0.16f), r.getX(), r.getY(),
      colours::background.darker(0.35f), r.getRight(), r.getBottom(), false));
  g.fillRect(r);
}

void OvertoniumEditor::resized() {
  const int logicalWidth = juce::roundToInt((float)getWidth() / zoom);
  const int logicalHeight = juce::roundToInt((float)getHeight() / zoom);

  content.setTransform(juce::AffineTransform::scale(zoom));
  content.setBounds(0, 0, logicalWidth, logicalHeight);

  auto area = content.getLocalBounds();
  topBar.setBounds(
      area.removeFromTop(ovt::ui::TopBar::heightForWidth(logicalWidth)));

  area.reduce(kEdge, kEdge);

  const auto gutterArea = area.removeFromLeft(kGutterWidth);

  // Noise is pinned on the far right, after the series it does not belong to,
  // and stays in view rather than needing a scroll to reach.
  const auto noiseArea = area.removeFromRight(kStripWidth);
  area.removeFromRight(kMasterGap);

  viewport.setBounds(area);

  const int stripHeight = viewport.getMaximumVisibleHeight();

  gutter.setBounds(gutterArea.withHeight(stripHeight));
  noiseStrip.setBounds(noiseArea.withHeight(stripHeight));
  stripsHolder.setSize(kNumHarmonics * kStripWidth, stripHeight);

  for (int i = 0; i < (int)strips.size(); ++i)
    strips[(size_t)i]->setBounds(i * kStripWidth, 0, kStripWidth, stripHeight);

  // Only write when something actually moved: a live resize drag fires this
  // constantly, and every property set notifies the APVTS listener on the state
  // tree.
  auto &state = processor.apvts.state;

  if ((int)state.getProperty(kEditorWidth, -1) != logicalWidth)
    state.setProperty(kEditorWidth, logicalWidth, nullptr);

  if ((int)state.getProperty(kEditorHeight, -1) != logicalHeight)
    state.setProperty(kEditorHeight, logicalHeight, nullptr);

  if (std::abs((double)state.getProperty(kEditorZoom, -1.0) - (double)zoom) >
      1.0e-6)
    state.setProperty(kEditorZoom, (double)zoom, nullptr);
}

void OvertoniumEditor::applyResizeLimits() {
  // Limits are expressed in logical pixels, so they scale with the zoom factor.
  // Wide enough for a usable stretch of mixer, and never narrower than the top
  // bar can lay itself out without dropping a group.
  const int minWidth = juce::jmax(kGutterWidth + kStripWidth + kMasterGap +
                                      6 * kStripWidth + 2 * kEdge,
                                  ovt::ui::TopBar::minimumWidth());
  // The narrowest window is also the one where the bar takes two rows, so
  // the minimum height has to leave room for that.
  const int minHeight = chromeHeight(minWidth) + minimumStripHeight();

  setResizeLimits(juce::roundToInt((float)minWidth * zoom),
                  juce::roundToInt((float)minHeight * zoom),
                  juce::roundToInt(2600.0f * zoom),
                  juce::roundToInt(1400.0f * zoom));
}

void OvertoniumEditor::setZoom(float newZoom) {
  newZoom = juce::jlimit(0.5f, 2.0f, newZoom);

  if (std::abs(newZoom - zoom) < 0.001f)
    return;

  const auto logicalWidth = (float)getWidth() / zoom;
  const auto logicalHeight = (float)getHeight() / zoom;

  zoom = newZoom;
  applyResizeLimits();

  setSize(juce::roundToInt(logicalWidth * zoom),
          juce::roundToInt(logicalHeight * zoom));
}

void OvertoniumEditor::applyPreset(int index) {
  presets::apply(processor.apvts, index);
}

// ---- LINK -------------------------------------------------------------------

juce::RangedAudioParameter *OvertoniumEditor::oscParameter(Role role,
                                                           int index) const {
  return processor.apvts.getParameter(
      params::oscParamId(roleSuffix(role), index));
}

bool OvertoniumEditor::isLinkEnabled() const { return topBar.isLinkEnabled(); }

void OvertoniumEditor::linkDragStarted(Role role, int sourceIndex) {
  // Latch the switch state at drag start: toggling LINK mid-drag must not leave
  // the host holding gestures that never get closed.
  if (!isLinkEnabled())
    return;

  const auto scope = topBar.getLinkScope();

  auto &gesture = linkGesture;
  gesture.active = true;
  gesture.role = role;
  gesture.curve = topBar.getLinkCurve();
  gesture.source = sourceIndex;

  const auto sourceClass = harmonic(sourceIndex).pitchClass;

  for (int i = 0; i < kNumHarmonics; ++i) {
    bool selected = true;

    switch (scope) {
    case LinkScope::SameInterval:
      selected = harmonic(i).pitchClass == sourceClass;
      break;
    case LinkScope::Odd:
      selected = ((i + 1) % 2) == 1;
      break;
    case LinkScope::Even:
      selected = ((i + 1) % 2) == 0;
      break;
    case LinkScope::All:
    case LinkScope::NumScopes:
    default:
      break;
    }

    // The strip under the mouse is always part of its own drag, whatever the
    // scope would otherwise say.
    selected = selected || i == sourceIndex;

    auto *param = oscParameter(role, i);
    gesture.baseline[(size_t)i] = param != nullptr ? param->getValue() : 0.0f;
    gesture.weight[(size_t)i] =
        selected
            ? juce::jmax(0.001f, linkCurveWeight(gesture.curve, i, sourceIndex))
            : 0.0f;
    gesture.jitter[(size_t)i] = spreadRandom.bipolar();

    if (selected && i != sourceIndex && param != nullptr)
      param->beginChangeGesture();
  }
}

void OvertoniumEditor::linkValueChanged(Role role, int sourceIndex,
                                        float plainValue) {
  auto &gesture = linkGesture;

  if (!gesture.active || gesture.role != role ||
      gesture.source != sourceIndex || propagatingLink)
    return;

  auto *source = oscParameter(role, sourceIndex);
  if (source == nullptr)
    return;

  // How far the dragged knob has travelled, in normalised units.
  const auto delta =
      source->convertTo0to1(plainValue) - gesture.baseline[(size_t)sourceIndex];

  // The dragged strip's live value. Gathering collapses everything onto it, so
  // the knob in your hand is the target rather than a stranded outlier.
  const auto target =
      juce::jlimit(0.0f, 1.0f, gesture.baseline[(size_t)sourceIndex] + delta);

  const juce::ScopedValueSetter<bool> guard(propagatingLink, true);

  for (int i = 0; i < kNumHarmonics; ++i) {
    if (i == sourceIndex || !gesture.includes(i))
      continue;

    if (auto *param = oscParameter(role, i))
      param->setValueNotifyingHost(linkedValue(
          gesture.curve, gesture.baseline[(size_t)i], delta,
          gesture.weight[(size_t)i], gesture.jitter[(size_t)i], target));
  }
}

void OvertoniumEditor::linkDragEnded(Role role, int sourceIndex) {
  if (!linkGesture.active)
    return;

  linkGesture.active = false;

  for (int i = 0; i < kNumHarmonics; ++i)
    if (i != sourceIndex && linkGesture.includes(i))
      if (auto *param = oscParameter(role, i))
        param->endChangeGesture();
}

// ---- polling ----------------------------------------------------------------

void OvertoniumEditor::timerCallback() {
  // Meters run every tick. Each strip only repaints if its bar actually moved a
  // visible amount, so a still patch costs nothing.
  for (int i = 0; i < kNumHarmonics; ++i)
    strips[(size_t)i]->setMeterLevel(processor.getPartialLevel(i));

  noiseStrip.setMeterLevel(processor.getNoiseLevel());
  topBar.setOutputLevels(processor.getOutputLevelLeft(),
                         processor.getOutputLevelRight());

  // The rest is housekeeping that nobody can see at 30 Hz, and it costs 96
  // parameter lookups, so it runs at a quarter of the rate.
  if (++housekeepingTick < 4)
    return;

  housekeepingTick = 0;

  auto &apvts = processor.apvts;

  // Voice readout
  const auto polyIndex = juce::jlimit(
      0, (int)params::kPolyphonyChoices.size() - 1,
      (int)apvts.getRawParameterValue(params::polyphonyId)->load());

  topBar.setVoiceCount(processor.getActiveVoiceCount(),
                       params::kPolyphonyChoices[(size_t)polyIndex]);

  // Dim whatever a solo elsewhere is silencing, so the mixer shows what you can
  // hear.
  // Solo spans the noise channel too, so it takes part in the dimming.
  bool anySolo =
      apvts.getRawParameterValue(params::noiseParamId(params::soloSuffix))
          ->load() > 0.5f;

  for (int i = 0; i < kNumHarmonics && !anySolo; ++i)
    anySolo =
        apvts.getRawParameterValue(params::oscParamId(params::soloSuffix, i))
            ->load() > 0.5f;

  for (int i = 0; i < kNumHarmonics; ++i) {
    const bool muted =
        apvts.getRawParameterValue(params::oscParamId(params::muteSuffix, i))
            ->load() > 0.5f;
    const bool soloed =
        apvts.getRawParameterValue(params::oscParamId(params::soloSuffix, i))
            ->load() > 0.5f;
    const bool audible = muted ? false : (anySolo ? soloed : true);

    strips[(size_t)i]->setSilencedByOthers(!audible);
  }

  {
    const bool muted =
        apvts.getRawParameterValue(params::noiseParamId(params::muteSuffix))
            ->load() > 0.5f;
    const bool soloed =
        apvts.getRawParameterValue(params::noiseParamId(params::soloSuffix))
            ->load() > 0.5f;

    noiseStrip.setSilencedByOthers(muted ? true : (anySolo && !soloed));
  }
}

// =============================================================================

juce::AudioProcessorEditor *OvertoniumProcessor::createEditor() {
  return new OvertoniumEditor(*this);
}
