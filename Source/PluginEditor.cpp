#include "PluginEditor.h"

#include "Presets.h"
#include "UI/Theme.h"

using namespace ovt;
using namespace ovt::ui;

namespace {
constexpr int kTopBarHeight = 66;
constexpr int kEdge = 4;
constexpr int kScrollBarThickness = 10;

int chromeHeight() { return kTopBarHeight + 2 * kEdge + kScrollBarThickness; }

/// State keys stored alongside the parameters so window size survives a reopen.
const juce::Identifier kEditorWidth{"editorWidth"};
const juce::Identifier kEditorHeight{"editorHeight"};
const juce::Identifier kEditorZoom{"editorZoom"};

bool isHeadingRow(Row r) {
  return r == Row::PitchModHeading || r == Row::EnvHeading ||
         r == Row::AmpModHeading || r == Row::OutputHeading;
}
} // namespace

// =============================================================================

void RowGutter::paint(juce::Graphics &g) {
  g.setColour(colours::panel);
  g.fillRect(getLocalBounds());

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
    : juce::AudioProcessorEditor(&p), processor(p), topBar(p.apvts, *this) {
  setLookAndFeel(&lookAndFeel);

  addAndMakeVisible(content);
  content.addAndMakeVisible(topBar);
  content.addAndMakeVisible(gutter);
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
  topBar.onMacroStart = [this](Role role) { macroDragStarted(role); };
  topBar.onMacroDelta = [this](Role role, float d) { macroMoved(role, d); };
  topBar.onMacroEnd = [this](Role role) { macroDragEnded(role); };
  topBar.onZoomChanged = [this](float z) { setZoom(z); };

  // ---- restore the last window size -----------------------------------------
  const auto &state = processor.apvts.state;

  zoom = (float)(double)state.getProperty(kEditorZoom, 1.0);
  zoom = juce::jlimit(0.5f, 2.0f, zoom);
  topBar.setZoomChoice(zoom);

  // Default size shows all 32 strips at once, which is the whole point of the
  // layout.
  const int defaultWidth =
      kGutterWidth + kNumHarmonics * kStripWidth + 2 * kEdge;
  const int defaultHeight = chromeHeight() + preferredStripHeight();

  const int savedWidth = (int)state.getProperty(kEditorWidth, defaultWidth);
  const int savedHeight = (int)state.getProperty(kEditorHeight, defaultHeight);

  setResizable(true, true);
  applyResizeLimits();

  setSize(juce::roundToInt(savedWidth * zoom),
          juce::roundToInt(savedHeight * zoom));

  startTimerHz(15);
}

OvertoniumEditor::~OvertoniumEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void OvertoniumEditor::paint(juce::Graphics &g) {
  g.fillAll(colours::background);
}

void OvertoniumEditor::resized() {
  const int logicalWidth = juce::roundToInt((float)getWidth() / zoom);
  const int logicalHeight = juce::roundToInt((float)getHeight() / zoom);

  content.setTransform(juce::AffineTransform::scale(zoom));
  content.setBounds(0, 0, logicalWidth, logicalHeight);

  auto area = content.getLocalBounds();
  topBar.setBounds(area.removeFromTop(kTopBarHeight));

  area.reduce(kEdge, kEdge);

  const auto gutterArea = area.removeFromLeft(kGutterWidth);
  viewport.setBounds(area);

  const int stripHeight = viewport.getMaximumVisibleHeight();

  gutter.setBounds(gutterArea.withHeight(stripHeight));
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
  const int minWidth = kGutterWidth + 6 * kStripWidth + 2 * kEdge;
  const int minHeight = chromeHeight() + minimumStripHeight();

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

void OvertoniumEditor::captureBaseline(Role role) {
  for (int i = 0; i < kNumHarmonics; ++i) {
    auto *param = oscParameter(role, i);
    baseline[(size_t)i] = param != nullptr ? param->getValue() : 0.0f;
  }
}

void OvertoniumEditor::applyOffsetFromBaseline(Role role, float delta,
                                               int skipIndex) {
  const juce::ScopedValueSetter<bool> guard(propagatingLink, true);

  for (int i = 0; i < kNumHarmonics; ++i) {
    if (i == skipIndex)
      continue;

    if (auto *param = oscParameter(role, i))
      param->setValueNotifyingHost(
          juce::jlimit(0.0f, 1.0f, baseline[(size_t)i] + delta));
  }
}

void OvertoniumEditor::linkDragStarted(Role role, int sourceIndex) {
  // Latch the switch state at drag start: toggling LINK mid-drag must not leave
  // the host holding gestures that never get closed.
  if (!isLinkEnabled())
    return;

  linkGestureActive = true;
  captureBaseline(role);

  for (int i = 0; i < kNumHarmonics; ++i)
    if (i != sourceIndex)
      if (auto *param = oscParameter(role, i))
        param->beginChangeGesture();
}

void OvertoniumEditor::linkValueChanged(Role role, int sourceIndex,
                                        float plainValue) {
  if (!linkGestureActive || propagatingLink)
    return;

  auto *source = oscParameter(role, sourceIndex);
  if (source == nullptr)
    return;

  // Ganging is relative: the siblings move by however far the dragged knob has
  // travelled, so their differences survive. Because the offset is always
  // measured from the values captured at drag start, dragging back to where you
  // began restores them exactly, even if some hit an end stop on the way.
  const float delta =
      source->convertTo0to1(plainValue) - baseline[(size_t)sourceIndex];

  applyOffsetFromBaseline(role, delta, sourceIndex);
}

void OvertoniumEditor::linkDragEnded(Role role, int sourceIndex) {
  if (!linkGestureActive)
    return;

  linkGestureActive = false;

  for (int i = 0; i < kNumHarmonics; ++i)
    if (i != sourceIndex)
      if (auto *param = oscParameter(role, i))
        param->endChangeGesture();
}

// ---- endless macros ---------------------------------------------------------

void OvertoniumEditor::macroDragStarted(Role role) {
  macroRole = role;
  macroGestureActive = true;
  captureBaseline(role);

  for (int i = 0; i < kNumHarmonics; ++i)
    if (auto *param = oscParameter(role, i))
      param->beginChangeGesture();
}

void OvertoniumEditor::macroMoved(Role role, float delta) {
  if (!macroGestureActive || macroRole != role)
    return;

  applyOffsetFromBaseline(role, delta, -1);
}

void OvertoniumEditor::macroDragEnded(Role role) {
  if (!macroGestureActive || macroRole != role)
    return;

  macroGestureActive = false;

  for (int i = 0; i < kNumHarmonics; ++i)
    if (auto *param = oscParameter(role, i))
      param->endChangeGesture();
}

// ---- polling ----------------------------------------------------------------

void OvertoniumEditor::timerCallback() {
  auto &apvts = processor.apvts;

  // Voice readout
  const auto polyIndex = juce::jlimit(
      0, (int)params::kPolyphonyChoices.size() - 1,
      (int)apvts.getRawParameterValue(params::polyphonyId)->load());

  topBar.setVoiceCount(processor.getActiveVoiceCount(),
                       params::kPolyphonyChoices[(size_t)polyIndex]);

  // Dim whatever a solo elsewhere is silencing, so the mixer shows what you can
  // hear.
  bool anySolo = false;
  for (int i = 0; i < kNumHarmonics; ++i) {
    if (apvts.getRawParameterValue(params::oscParamId(params::soloSuffix, i))
            ->load() > 0.5f) {
      anySolo = true;
      break;
    }
  }

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
}

// =============================================================================

juce::AudioProcessorEditor *OvertoniumProcessor::createEditor() {
  return new OvertoniumEditor(*this);
}
