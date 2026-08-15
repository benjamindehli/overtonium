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
         r == Row::KeyOffHeading || r == Row::AmpModHeading ||
         r == Row::OutputHeading;
}
} // namespace

// =============================================================================

void RowGutter::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows = layoutRows(getLocalBounds().reduced(0, 4));

  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

void RowGutter::paint(juce::Graphics &g) {
  paintChannelBackground(g, getLocalBounds(), colours::panel.darker(0.25f));

  const auto rows = layoutRows(getLocalBounds().reduced(0, 4));

  if (rowShowsHighlight(highlighted))
    paintRowHighlight(g, rows[(size_t)highlighted]);

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
    const bool lit = row == highlighted && rowShowsHighlight(row);

    g.setFont(makeFont(heading ? 10.0f : 9.5f, heading || lit));
    g.setColour(lit ? colours::accent
                    : (heading ? colours::text : colours::textDim));
    g.drawText(text, area, juce::Justification::centredRight, false);
  }

  g.setColour(colours::outline);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());

  // ---- the maker's badge ----------------------------------------------------
  // The fader row is the one place in the window with room going spare: it
  // stretches with the height, its caption sits at the top, and the rest is
  // panel. A badge at the foot of it is where a console puts one.
  if (makersMark == nullptr)
    return;

  auto foot = rows[(size_t)Row::Fader];
  foot.removeFromTop(20); // clear of the LEVEL caption

  const auto art = makersMark->getDrawableBounds();
  const auto width = juce::jmin(getWidth() - 20, 58);
  const auto height = juce::roundToInt((float)width * art.getHeight() /
                                       juce::jmax(1.0f, art.getWidth()));

  // On a short window there is no room, and a squashed badge is worse than
  // none, so it simply is not drawn.
  if (foot.getHeight() < height + 12)
    return;

  const juce::Rectangle<int> badge(
      getWidth() - width - 10, foot.getBottom() - height - 6, width, height);

  makersMark->drawWithin(g, badge.toFloat(), juce::RectanglePlacement::centred,
                         0.5f);
}

// =============================================================================

OvertoniumEditor::OvertoniumEditor(OvertoniumProcessor &p)
    : juce::AudioProcessorEditor(&p), processor(p), topBar(p.apvts, *this),
      noiseStrip(p.apvts, *this, *this) {
  setLookAndFeel(&lookAndFeel);

  // The background is filled edge to edge, so say so: an opaque top-level
  // component saves the window manager blending it against whatever is behind.
  setOpaque(true);

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
        std::make_unique<ChannelStrip>(processor.apvts, *this, *this, *this, i);
    stripsHolder.addAndMakeVisible(*strip);
    strips.push_back(std::move(strip));
  }

  topBar.onPresetChosen = [this](int index) {
    applyPreset(index);
    topBar.setPresetName(presets::names()[index]);
  };

  topBar.onUserPresetChosen = [this](juce::File file) {
    juce::String error;

    if (presets::load(processor.apvts, file, error))
      topBar.setPresetName(file.getFileNameWithoutExtension());
    else
      complain("Could not load that preset", error);
  };

  topBar.onSaveUserPreset = [this](juce::String name) {
    juce::String error;

    if (presets::save(processor.apvts, name, error))
      topBar.setPresetName(presets::sanitiseName(name));
    else
      complain("Could not save that preset", error);
  };

  topBar.onCopyFactoryCode = [this] {
    const auto name = presets::sanitiseName(topBar.getPresetName());

    juce::SystemClipboard::copyTextToClipboard(presets::factoryCode(
        processor.apvts, name.isEmpty() ? "Untitled" : name));

    complain("Copied", "The C++ for this patch is on the clipboard. Paste it "
                       "into Presets.cpp as a new case and add its name to "
                       "kNames.");
  };

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

    // Switching LINK on, or changing what it reaches, changes the answer to
    // "what would this knob take with it", so the preview follows immediately
    // rather than waiting for the pointer to move.
    updateLinkGlow();
    updateLinkCursor();
  };

  updateLinkCursor();

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

void OvertoniumEditor::complain(const juce::String &title,
                                const juce::String &detail) {
  juce::NativeMessageBox::showAsync(
      juce::MessageBoxOptions()
          .withIconType(juce::MessageBoxIconType::NoIcon)
          .withTitle(title)
          .withMessage(detail)
          .withButton("OK")
          .withAssociatedComponent(this),
      nullptr);
}

// ---- LINK -------------------------------------------------------------------

juce::RangedAudioParameter *OvertoniumEditor::oscParameter(Role role,
                                                           int index) const {
  return processor.apvts.getParameter(
      params::oscParamId(roleSuffix(role), index));
}

bool OvertoniumEditor::isLinkEnabled() const { return topBar.isLinkEnabled(); }

void OvertoniumEditor::showLinkMenu() { topBar.showLinkMenu(nullptr); }

void OvertoniumEditor::updateLinkCursor() {
  // Set on the holder rather than on each control: the strips and their knobs
  // all take the parent's pointer, so this one assignment reaches every one of
  // them. The noise channel is outside the holder, which is right, since LINK
  // never reaches it either.
  stripsHolder.setMouseCursor(topBar.isLinkEnabled()
                                  ? linkCursor(topBar.getLinkCurve())
                                  : juce::MouseCursor());

  // The pointer may be sitting still over a strip, in which case nothing else
  // would ask for it again.
  stripsHolder.updateMouseCursor();
}

void OvertoniumEditor::gatherLinkWeights(
    int sourceIndex, LinkScope scope, LinkCurve curve,
    std::array<float, kNumHarmonics> &out) const {
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

    out[(size_t)i] =
        selected ? juce::jmax(0.001f, linkCurveWeight(curve, i, sourceIndex))
                 : 0.0f;
  }
}

void OvertoniumEditor::linkDragStarted(Role role, int sourceIndex) {
  hoverLocked = true;

  // Latch the switch state at drag start: toggling LINK mid-drag must not leave
  // the host holding gestures that never get closed.
  if (!isLinkEnabled())
    return;

  auto &gesture = linkGesture;
  gesture.active = true;
  gesture.role = role;
  gesture.curve = topBar.getLinkCurve();
  gesture.source = sourceIndex;

  gatherLinkWeights(sourceIndex, topBar.getLinkScope(), gesture.curve,
                    gesture.weight);

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto *param = oscParameter(role, i);
    gesture.baseline[(size_t)i] = param != nullptr ? param->getValue() : 0.0f;
    gesture.jitter[(size_t)i] = spreadRandom.bipolar();

    if (gesture.includes(i) && i != sourceIndex && param != nullptr)
      param->beginChangeGesture();
  }

  updateLinkGlow();
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
  hoverLocked = false;

  if (!linkGesture.active)
    return;

  linkGesture.active = false;

  for (int i = 0; i < kNumHarmonics; ++i)
    if (i != sourceIndex && linkGesture.includes(i))
      if (auto *param = oscParameter(role, i))
        param->endChangeGesture();

  // Back to a preview: the pointer is still on the knob that was just let go.
  updateLinkGlow();
}

// ---- hover ------------------------------------------------------------------

void OvertoniumEditor::hoverChanged(int stripIndex, Row row) {
  if (hoverLocked || (stripIndex == hoverStrip && row == hoverRow))
    return;

  const bool rowMoved = row != hoverRow;

  hoverStrip = stripIndex;
  hoverRow = row;

  if (rowMoved) {
    for (auto &strip : strips)
      strip->setHighlightedRow(row);

    noiseStrip.setHighlightedRow(row);
    gutter.setHighlightedRow(row);
  }

  updateLinkGlow();
}

void OvertoniumEditor::updateLinkGlow() {
  auto role = Role::Tune;
  std::array<float, kNumHarmonics> weight{};

  if (linkGesture.active) {
    role = linkGesture.role;
    weight = linkGesture.weight;
  } else if (isLinkEnabled() && hoverStrip >= 0 && roleForRow(hoverRow, role)) {
    // Nothing has been grabbed yet, so this is a preview of what the knob under
    // the pointer would take with it.
    gatherLinkWeights(hoverStrip, topBar.getLinkScope(), topBar.getLinkCurve(),
                      weight);
  }

  auto strongest = 0.0f;
  for (auto w : weight)
    strongest = juce::jmax(strongest, w);

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto w = weight[(size_t)i];

    // Measured against the strip that moves most, so a tilted curve shows
    // itself: the end of the series that takes the biggest share is the end
    // that lights up brightest. The floor keeps a strip that is in the drag but
    // barely moving from looking like one that is out of it.
    const auto glow =
        w > 0.0f && strongest > 0.0f ? 0.4f + 0.6f * (w / strongest) : 0.0f;

    strips[(size_t)i]->setLinkGlow(role, glow);
  }
}

// ---- polling ----------------------------------------------------------------

void OvertoniumEditor::timerCallback() {
  ++tick;

  // Two things about a frame cost the window manager: that it happened at all,
  // and how much of the window the dirty rectangles enclose. It enlarges them
  // to their bounding box, so the mixer is invalidated as a handful of
  // rectangles rather than thirty-three scattered ones: merging them all into
  // one is nearly as bad as leaving them scattered, since the bands sit at
  // different heights and their union is most of the mixer.
  //
  // The frames themselves are the larger cost, so the meters are read on every
  // other tick, fifteen times a second, which is more than a segmented meter
  // can show anyway. Splitting the mixer into halves that take alternate turns
  // was measured too: it halves the area of a frame but doubles how many
  // frames there are, which is the wrong way round.
  if ((tick % 2) != 0)
    return;

  dirtyRegions.clearQuick();

  const auto add = [this](juce::Component &from, juce::Rectangle<int> band) {
    if (!band.isEmpty())
      dirtyRegions.add(content.getLocalArea(&from, band));
  };

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &strip = *strips[(size_t)i];
    add(strip, strip.setMeterLevel(processor.getPartialLevel(i)));
  }

  add(noiseStrip, noiseStrip.setMeterLevel(processor.getNoiseLevel()));

  coalesceRegions(dirtyRegions, kMaxDirtyRegions);

  for (const auto &region : dirtyRegions)
    content.repaint(region);

  // Its own region, at the other end of the window from the mixer.
  topBar.setOutputLevels(processor.getOutputLevelLeft(),
                         processor.getOutputLevelRight());

  // The rest is housekeeping that nobody can see at 30 Hz, so it runs at a
  // fraction of the rate.
  if ((tick % 8) != 0)
    return;

  // Read through the cached atomics rather than the parameter map: the map
  // wants a string per lookup, and this runs several times a second.
  const auto &cache = processor.parameters();

  const auto on = [](const std::atomic<float> *p) {
    return p != nullptr && p->load() > 0.5f;
  };

  topBar.setVoiceCount(processor.getActiveVoiceCount(), cache.polyphonyValue());

  // Dim whatever a solo elsewhere is silencing, so the mixer shows what you can
  // hear. Solo spans the noise channel too, so it takes part in the dimming.
  bool anySolo = on(cache.noise.solo);

  for (int i = 0; i < kNumHarmonics && !anySolo; ++i)
    anySolo = on(cache.osc[(size_t)i].solo);

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto &osc = cache.osc[(size_t)i];
    const bool audible = on(osc.mute) ? false : (anySolo ? on(osc.solo) : true);

    strips[(size_t)i]->setSilencedByOthers(!audible);
  }

  noiseStrip.setSilencedByOthers(
      on(cache.noise.mute) ? true : (anySolo && !on(cache.noise.solo)));
}

// =============================================================================

juce::AudioProcessorEditor *OvertoniumProcessor::createEditor() {
  return new OvertoniumEditor(*this);
}
