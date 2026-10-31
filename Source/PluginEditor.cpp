#include "PluginEditor.h"
#include "UI/LearnMenu.h"

#include <cmath>

#include "Presets.h"
#include "UI/Theme.h"
#include "UpdateCheck.h"

using namespace ovt;
using namespace ovt::ui;

namespace {
using ovt::ui::kScrollBarThickness;
/// Breathing room between the noise channel and the scrolling series.
constexpr int kMasterGap = 8;

/// The two bars under the top bar, as bits of OvertoniumEditor::openBars.
constexpr int kPlayingBar = 1;
constexpr int kTuningBar = 2;

/// The narrowest window, in logical pixels: a usable stretch of mixer, and
/// never narrower than the top bar can lay itself out in.
int minimumLogicalWidth() {
  return juce::jmax(kGutterWidth + kStripWidth + kMasterGap + 6 * kStripWidth +
                        kScrollBarThickness,
                    ovt::ui::TopBar::minimumWidth());
}

/// State keys stored alongside the parameters so window size survives a reopen.
const juce::Identifier kEditorWidth{"editorWidth"};
const juce::Identifier kEditorHeight{"editorHeight"};
const juce::Identifier kEditorZoom{"editorZoom"};
const juce::Identifier kLinkScope{"linkScope"};

/// The channels the Selected scope reaches, as comma-separated harmonic
/// numbers.
const juce::Identifier kLinkSelection{"linkSelection"};

/// Whether LINK is on. Saved beside the scope and the curve, since it is
/// part of the same setting.
const juce::Identifier kLinkOn{"linkOn"};

/// Whether the fader drawing tool is latched on. Saved with the session, as
/// LINK is.
const juce::Identifier kDrawLatched{"drawLatched"};

/// The curve, by name. See linkCurveFromState.
const juce::Identifier kLinkCurveId{"linkCurveId"};

/// The old index-based curve key. Read when kLinkCurveId is absent, and
/// removed on the next write.
const juce::Identifier kLinkCurve{"linkCurve"};
const juce::Identifier kCollapsedSections{"collapsedSections"};

/// Which bars are open under the top bar, saved with the session.
const juce::Identifier kOpenBars{"openBars"};

/// How far the parameters are scrolled, saved with the session. Clamped on
/// the way in, since the window may be a different height.
const juce::Identifier kScroll{"parameterScroll"};

} // namespace

// =============================================================================

RowGutter::RowGutter() {
  // One button that always opens the tool menu. It shows the current tool's
  // pointer as its icon. See ui::PointerTool.
  toolButton.setTooltip(
      "What dragging in the mixer does. Pointer moves one control, Link moves "
      "its whole row, and Draw paints values across the channels.");
  toolButton.setTitle("Pointer tool");
  toolButton.setColour(juce::TextButton::textColourOnId, colours::accent);

  // Always lit, since the plain pointer is a tool too and a dark button
  // would read as disabled. Set here because syncLinkUi returns early before
  // the first paint.
  toolButton.setToggleState(true, juce::dontSendNotification);

  toolButton.onClick = [this] {
    if (onLinkClicked != nullptr)
      onLinkClicked(&toolButton);
  };

  toolButton.onIcon = [this](juce::Graphics &g, juce::Rectangle<float> area,
                             juce::Colour colour) {
    ovt::ui::drawToolIcon(g, area, colour, tool);
  };

  addAndMakeVisible(toolButton);

  // L and T, like a channel's M and S, with the full name in the tooltip and
  // the accessible title.
  glideLegato.setButtonText("L");
  glideLegato.setTooltip(
      "Legato glide: only glide from a key that is still held. When off, every "
      "note glides from the last one, even after its key is released.");
  glideLegato.setTitle("Legato glide");
  glideLegato.setComponentID("glideLegato");
  glideLegato.onClick = [this] {
    if (onGlideLegatoClicked != nullptr)
      onGlideLegatoClicked();
  };

  glideFixedTime.setButtonText("T");
  glideFixedTime.setTooltip(
      "Fixed time: every glide takes the knob's time, however far it goes. "
      "When off, the knob is the time per octave, so wide leaps take longer.");
  glideFixedTime.setTitle("Fixed glide time");
  glideFixedTime.setComponentID("glideFixedTime");
  glideFixedTime.onClick = [this] {
    if (onGlideFixedTimeClicked != nullptr)
      onGlideFixedTimeClicked();
  };

  for (auto *b : {&glideLegato, &glideFixedTime})
    addAndMakeVisible(*b);

  // The shared-modulator switch on each shape row, the same switch as the
  // last item in every shape button's menu.
  const auto setUpInPhase = [this](ovt::ui::ScreenSwitch &b, const char *which,
                                   const char *id,
                                   std::function<void()> RowGutter::*callback) {
    b.onIcon = ovt::ui::drawSharedIcon;
    b.setTooltip(juce::String(which) +
                 " shared by all notes: every note follows the same modulator "
                 "on each channel. When off, each note starts its own.");
    b.setTitle(juce::String(which) + " shared by all notes");
    b.setComponentID(id);
    b.onClick = [this, callback] {
      if (this->*callback != nullptr)
        (this->*callback)();
    };
    addAndMakeVisible(b);
  };

  setUpInPhase(pitchInPhase, "Pitch modulation", "pitchInPhase",
               &RowGutter::onPitchInPhaseClicked);
  setUpInPhase(ampInPhase, "Amp modulation", "ampInPhase",
               &RowGutter::onAmpInPhaseClicked);

  headerCap.onPaint = [this](juce::Graphics &g) { paintHeaderBand(g); };
  addAndMakeVisible(headerCap);
}

void RowGutter::setHighlightedRow(Row row) {
  if (row == highlighted)
    return;

  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);

  repaintRowHighlight(*this, rows, highlighted);
  highlighted = row;
  repaintRowHighlight(*this, rows, highlighted);
}

void RowGutter::resized() {
  // The same rows the captions use, so the button stands in the band where
  // the strips put their channel numbers.
  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);

  // The cap first, then the button, so the button is in front of it. The
  // cap reaches the foot of the header row, which already includes the
  // column's top padding.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[(size_t)Row::Header].getBottom()));

  // The switches beside their captions, hidden when the section is folded.
  // Laid out before the cap is brought forward, so a row scrolled under the
  // header slides under it.
  const auto headerBottom = headerCap.getBottom();
  placeCaps(Row::PmShape, {&pitchInPhase}, rows, headerBottom);
  placeCaps(Row::Glide, {&glideLegato, &glideFixedTime}, rows, headerBottom);
  placeCaps(Row::AmShape, {&ampInPhase}, rows, headerBottom);

  headerCap.toFront(false);

  toolButton.setBounds(rows[(size_t)Row::Header].reduced(7, 1));
  toolButton.toFront(false);
}

void RowGutter::placeCaps(Row row,
                          std::initializer_list<juce::Component *> caps,
                          const ovt::ui::RowBounds &rows, int headerBottom) {
  // The size of a channel's M and S.
  constexpr int kCapSize = 18;
  constexpr int kGap = 4;

  const auto area = rows[(size_t)row];
  const bool shown = area.getHeight() > 0 && !rowIsCollapsed(row, collapsed) &&
                     area.getBottom() > headerBottom;

  // Right up against the caption, which is right-aligned eight pixels in and
  // measured in the unlit weight.
  const auto caption =
      juce::GlyphArrangement::getStringWidthInt(makeFont(9.5f), rowLabel(row));

  auto x = area.getRight() - 8 - caption - kGap - (int)caps.size() * kCapSize;
  const auto y = area.getCentreY() - kCapSize / 2;

  for (auto *cap : caps) {
    if (shown)
      cap->setBounds(x, y, kCapSize, kCapSize);

    cap->setVisible(shown);
    x += kCapSize;
  }
}

void RowGutter::setTool(ovt::ui::PointerTool which, ovt::ui::LinkCurve curve) {
  if (which == tool &&
      toolIcon.isValid() == (which != ovt::ui::PointerTool::Pointer))
    return;

  tool = which;
  toolIcon = ovt::ui::pointerToolImage(tool, curve, 1.0f);

  // Only the icon changes, and the button must be told or it repaints its
  // cached image. See GlowButton::iconChanged.
  toolButton.setTitle(juce::String("Tool: ") + ovt::ui::pointerToolName(tool));
  toolButton.iconChanged();
  repaint();
}

void RowGutter::setCollapsedSections(SectionMask mask) {
  if (mask == collapsed)
    return;

  collapsed = mask;
  resized();
  repaint();
}

void RowGutter::setScroll(int s) {
  if (s == scroll)
    return;

  scroll = s;
  resized();
  repaint();
}

void RowGutter::setGlideSwitches(bool legato, bool fixedTime) {
  glideLegato.setToggleState(legato, juce::dontSendNotification);
  glideFixedTime.setToggleState(fixedTime, juce::dontSendNotification);
}

void RowGutter::setSharedModulators(bool pitch, bool amp) {
  pitchInPhase.setToggleState(pitch, juce::dontSendNotification);
  ampInPhase.setToggleState(amp, juce::dontSendNotification);
}

void RowGutter::mouseDown(const juce::MouseEvent &e) {
  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());

  if (section != Section::NumSections && onSectionToggled != nullptr)
    onSectionToggled(section);
}

void RowGutter::mouseMove(const juce::MouseEvent &e) {
  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);
  const auto section = headingSectionAt(rows, e.getPosition());
  const bool onHeading = section != Section::NumSections;

  setMouseCursor(onHeading ? juce::MouseCursor::PointingHandCursor
                           : juce::MouseCursor::NormalCursor);

  // Only the headings, since the other captions name controls elsewhere.
  if (onHoverChanged)
    onHoverChanged(onHeading ? sectionHeading(section) : kNoRow);
}

void RowGutter::mouseExit(const juce::MouseEvent &) {
  if (onHoverChanged)
    onHoverChanged(kNoRow);
}

void RowGutter::paintHeaderBand(juce::Graphics &g) {
  // The header holds the tool button, which sits in front of this cap. See
  // resized.
  paintChannelBackground(g, getLocalBounds(), colours::panel.darker(0.25f));

  // The right-edge divider too, so the cap does not cover the top of it.
  g.setColour(colours::outline);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void RowGutter::paint(juce::Graphics &g) {
  paintChannelBackground(g, getLocalBounds(), colours::panel.darker(0.25f));

  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);

  if (rowShowsHighlight(highlighted))
    paintRowHighlight(g, rows[(size_t)highlighted]);

  for (int i = 0; i < kNumRows; ++i) {
    const auto row = (Row)i;
    const auto *text = rowLabel(row);

    if (text == nullptr)
      continue;

    auto area = rows[(size_t)i].reduced(8, 0);

    // "LEVEL" sits at the top of the tall fader row.
    if (row == Row::Fader)
      area = area.removeFromTop(16);

    // A folded row has no height, so its caption would overlap the heading.
    if (rowIsCollapsed(row, collapsed))
      continue;

    const bool heading = isHeadingRow(row);
    const bool lit = row == highlighted && rowShowsHighlight(row);

    g.setFont(makeFont(heading ? 10.0f : 9.5f, heading || lit));
    g.setColour(lit ? colours::accent
                    : (heading ? colours::text : colours::textDim));
    g.drawText(text, area, juce::Justification::centredRight, false);

    // The disclosure mark at the far left of the heading, pointing down when
    // open and right when folded.
    if (heading) {
      const auto section = sectionOf(row);
      const bool folded = isCollapsed(collapsed, section);
      // One triangle turned a quarter for both orientations. The centre clears
      // PITCH MOD, the longest caption.
      constexpr float kAlong = 7.0f;
      constexpr float kAcross = 4.0f;

      const float cx = 9.5f;
      const float cy = (float)rows[(size_t)i].getCentreY();

      juce::Path mark;

      if (folded)
        mark.addTriangle(cx - kAcross * 0.5f, cy - kAlong * 0.5f,
                         cx - kAcross * 0.5f, cy + kAlong * 0.5f,
                         cx + kAcross * 0.5f, cy);
      else
        mark.addTriangle(cx - kAlong * 0.5f, cy - kAcross * 0.5f,
                         cx + kAlong * 0.5f, cy - kAcross * 0.5f, cx,
                         cy + kAcross * 0.5f);

      // Lit with its own caption, since the two are one control.
      g.setColour(lit ? colours::accent : colours::textDim);
      g.fillPath(mark);
    }
  }

  g.setColour(colours::outline);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());

  // ---- the maker's badge ----------------------------------------------------
  // A badge at the foot of the fader row, the one place with spare room.
  if (makersMark == nullptr)
    return;

  auto foot = rows[(size_t)Row::Fader];
  foot.removeFromTop(20); // clear of the LEVEL caption

  const auto art = makersMark->getDrawableBounds();
  const auto width = juce::jmin(getWidth() - 20, 58);
  const auto height = juce::roundToInt((float)width * art.getHeight() /
                                       juce::jmax(1.0f, art.getWidth()));

  // Not drawn at all when there is no room.
  if (foot.getHeight() < height + 12)
    return;

  const juce::Rectangle<int> badge(
      getWidth() - width - 10, foot.getBottom() - height - 6, width, height);

  makersMark->drawWithin(g, badge.toFloat(), juce::RectanglePlacement::centred,
                         0.5f);
}

// =============================================================================

OvertoniumEditor::OvertoniumEditor(OvertoniumProcessor &p)
    : juce::AudioProcessorEditor(&p), topBar(p.apvts, *this),
      playingBar(p.apvts, *this), tuningBar(p.apvts, *this),
      noiseStrip(p.apvts, *this, *this), macroPanel(p.apvts) {
  setLookAndFeel(&lookAndFeel);

  // The background is filled edge to edge.
  setOpaque(true);

  addAndMakeVisible(content);
  content.addAndMakeVisible(topBar);
  content.addChildComponent(playingBar);
  content.addChildComponent(tuningBar);
  content.addAndMakeVisible(gutter);
  content.addAndMakeVisible(noiseStrip);
  content.addAndMakeVisible(viewport);

  viewport.setViewedComponent(&stripsHolder, false);
  viewport.setScrollBarsShown(false,
                              true); // horizontal only, so rows stay aligned
  viewport.setScrollBarThickness(kScrollBarThickness);

  strips.reserve(kNumHarmonics);
  for (int i = 0; i < kNumHarmonics; ++i) {
    auto strip =
        std::make_unique<ChannelStrip>(plugin().apvts, *this, *this, *this, i);
    strip->onSectionToggled = [this](Section s) { toggleSection(s); };
    stripsHolder.addAndMakeVisible(*strip);
    strips.push_back(std::move(strip));
  }

  topBar.isUpdateCheckAllowed = [] { return ovt::updateCheckAllowed(); };

  topBar.onUpdateCheckToggled = [this](bool allowed) {
    ovt::setUpdateCheckAllowed(allowed);

    // Answered in the menu, so the question counts as asked.
    ovt::markUpdateCheckOffered();

    if (allowed)
      maybeCheckForUpdates();
    else
      topBar.setUpdateAvailable({}, {});
  };

  // applyPreset goes through the processor, which records the name, so this
  // only shows it.
  topBar.onPresetChosen = [this](int index) {
    applyPreset(index);
    topBar.setPresetName(plugin().presetName());
  };

  topBar.onUserPresetChosen = [this](juce::File file) {
    juce::String error;
    bool loaded = false;

    plugin().recordEdit("Load preset", [this, &file, &error, &loaded] {
      loaded =
          presets::load(plugin().apvts, file, error, &plugin().macroChannels());
    });

    if (loaded)
      setPresetName(file.getFileNameWithoutExtension());
    else
      complain("Could not load that preset", error);
  };

  topBar.onSaveUserPreset = [this](juce::String name) {
    juce::String error;

    if (presets::save(plugin().apvts, name, error, &plugin().macroChannels()))
      setPresetName(presets::sanitiseName(name));
    else
      complain("Could not save that preset", error);
  };

  topBar.onCopyFactoryCode = [this] {
    const auto name = presets::sanitiseName(topBar.getPresetName());

    juce::SystemClipboard::copyTextToClipboard(presets::factoryCode(
        plugin().apvts, name.isEmpty() ? "Untitled" : name));

    complain("Copied", "The C++ for this patch is on the clipboard. Paste it "
                       "into Presets.cpp as a new case and add its name to "
                       "kNames.");
  };

  topBar.onZoomChanged = [this](float z) { setZoom(z); };
  topBar.onFitAllChannels = [this] { fitAllChannels(); };

  topBar.onUndo = [this] { stepHistory(false); };
  topBar.onRedo = [this] { stepHistory(true); };
  topBar.canUndo = [this] { return plugin().undo().canUndo(); };
  topBar.canRedo = [this] { return plugin().undo().canRedo(); };

  // Needed to see key presses at all. Many hosts keep Cmd-Z for themselves,
  // so the menu has undo and redo too.
  setWantsKeyboardFocus(true);

  // ---- restore the last window size -----------------------------------------
  //
  // A copy, taken once under the lock, since a host may be in
  // setStateInformation on another thread. See OvertoniumProcessor::stateLock.
  const auto state = [this] {
    const juce::ScopedLock sl(plugin().stateLock());
    return plugin().apvts.state.createCopy();
  }();

  zoom = (float)(double)state.getProperty(kEditorZoom, 1.0);
  zoom = juce::jlimit(0.5f, 2.0f, zoom);
  topBar.setZoomChoice(zoom);

  drawLatched = state.getProperty(kDrawLatched, false);
  drawArmed = drawLatched;

  gutter.onDrawClicked = [this] { toggleDrawLatch(); };

  topBar.setLinkEnabled(state.getProperty(kLinkOn, false));
  topBar.setLinkScope((LinkScope)juce::jlimit(
      0, (int)LinkScope::NumScopes - 1, (int)state.getProperty(kLinkScope, 0)));
  topBar.setLinkCurve(
      linkCurveFromState(state.getProperty(kLinkCurveId).toString(),
                         (int)state.getProperty(kLinkCurve, -1)));

  for (const auto &number : juce::StringArray::fromTokens(
           state.getProperty(kLinkSelection).toString(), ",", "")) {
    const auto i = number.trim().getIntValue() - 1;

    if (i >= 0 && i < kNumHarmonics)
      selection[(size_t)i] = true;
  }

  topBar.onSelectionCleared = [this] { clearSelection(); };
  selectionChanged(false);

  topBar.onLinkSettingsChanged = [this] {
    rememberTool();

    {
      // Every direct write to this tree holds the lock. See
      // OvertoniumProcessor::stateLock.
      const juce::ScopedLock sl(plugin().stateLock());
      auto &tree = plugin().apvts.state;

      tree.setProperty(kLinkScope, (int)topBar.getLinkScope(), nullptr);
      tree.setProperty(kLinkCurveId, linkCurveId(topBar.getLinkCurve()),
                       nullptr);
      tree.removeProperty(kLinkCurve, nullptr);
    }

    syncLinkUi();
  };

  // A function shared with the callback above, so opening a window and a
  // menu choice update the same things.
  syncLinkUi();

  // The menu belongs to the bar, which holds what it changes. The gutter
  // holds the button.
  gutter.onLinkClicked = [this](juce::Component *anchor) {
    topBar.showLinkMenu(anchor, {}, &plugin().midiLearn, currentTool());
  };

  topBar.onToolChosen = [this](ovt::ui::PointerTool which) {
    chooseTool(which);
  };

  topBar.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  // Over everything including the bar, and hidden until asked for.
  addChildComponent(macroPanel);

  // Above even MACROS, so a waiting question is never hidden.
  addChildComponent(notice);

  macroPanel.channels = &plugin().macroChannels();
  macroPanel.recordEdit = [this](const juce::String &name,
                                 const std::function<void()> &change) {
    plugin().recordEdit(name, change);
  };
  macroPanel.currentSelection = [this] {
    std::uint32_t mask = 0;

    for (int i = 0; i < kNumHarmonics; ++i)
      if (selection[(size_t)i])
        mask |= 1u << i;

    return mask;
  };

  macroPanel.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  macroPanel.onDismiss = [this] {
    macroPanel.setVisible(false);
    topBar.setMacrosOn(false);
  };

  topBar.onMacrosClicked = [this] { showMacroPanel(!macroPanel.isVisible()); };

  for (auto *bar : {static_cast<ovt::ui::OptionBar *>(&playingBar),
                    static_cast<ovt::ui::OptionBar *>(&tuningBar)})
    bar->onLearnRequested = [this](const juce::String &id) {
      showLearnMenu(id);
    };

  playingBar.onMacroMenuRequested = [this](int macro) { showMacroMenu(macro); };

  noiseStrip.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  // Filled now, since housekeeping only runs at 4 Hz.
  topBar.updatePanelReadouts(plugin().getSampleRate());

  // Read before the heights below, which depend on the fold state.
  collapsedSections =
      (SectionMask)(int)state.getProperty(kCollapsedSections, 0) &
      ((1u << kNumSections) - 1u);
  publishCollapsedSections();

  // Also before the heights, since an open bar takes height.
  openBars = (int)state.getProperty(kOpenBars, 0) & (kPlayingBar | kTuningBar);
  playingBar.setVisible((openBars & kPlayingBar) != 0);
  tuningBar.setVisible((openBars & kTuningBar) != 0);
  topBar.setBarsOpen(playingBar.isVisible(), tuningBar.isVisible());
  topBar.onPlayingClicked = [this] { toggleBar(kPlayingBar); };

  // A bar that gains or loses a row takes the mixer's height with it.
  for (auto *bar : {static_cast<ovt::ui::OptionBar *>(&playingBar),
                    static_cast<ovt::ui::OptionBar *>(&tuningBar)})
    bar->onHeightMayChange = [this] {
      if (getWidth() > 0) {
        applyResizeLimits();
        resized();
      }
    };
  topBar.onTuningClicked = [this] { toggleBar(kTuningBar); };

  // Clamped later by resized(), once the scroll range is known.
  scrollY = juce::jmax(0, (int)state.getProperty(kScroll, 0));

  // Hidden until there is something to scroll.
  content.addChildComponent(parameterBar);
  parameterBar.addListener(this);
  parameterBar.setAutoHide(false);

  gutter.onSectionToggled = [this](Section s) { toggleSection(s); };

  // A switch click flips the parameter in one gesture. The switch lights
  // from the parameter, so presets and menus that move it show too.
  const auto flip = [this](const char *id) {
    if (auto *param = plugin().apvts.getParameter(id)) {
      param->beginChangeGesture();
      param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
      param->endChangeGesture();
    }

    syncSharedModulators();
  };

  gutter.onGlideLegatoClicked = [flip] { flip(ovt::params::glideTriggerId); };
  gutter.onGlideFixedTimeClicked = [flip] { flip(ovt::params::glideModeId); };
  gutter.onPitchInPhaseClicked = [flip] { flip(ovt::params::pmInPhaseId); };
  gutter.onAmpInPhaseClicked = [flip] { flip(ovt::params::amInPhaseId); };

  // Index -1, as for the noise channel, so a caption cannot arm a LINK
  // preview on a channel.
  gutter.onHoverChanged = [this](Row row) { hoverChanged(-1, row); };
  noiseStrip.onSectionToggled = [this](Section s) { toggleSection(s); };

  const auto standard = standardSize();

  const int savedWidth =
      (int)state.getProperty(kEditorWidth, standard.getWidth());
  const int savedHeight =
      (int)state.getProperty(kEditorHeight, standard.getHeight());

  setResizable(true, true);
  applyResizeLimits(savedWidth);

  // Explicit casts, to keep clang's precision warning quiet.
  setSize(juce::roundToInt((float)savedWidth * zoom),
          juce::roundToInt((float)savedHeight * zoom));

  // Before the first paint, rather than on the first housekeeping tick.
  syncSharedModulators();

  startTimerHz(30);

  // Whatever was loaded before this window opened. Empty shows the
  // placeholder.
  topBar.setPresetName(plugin().presetName());

  // Last, so a prompt cannot appear over a half-built window.
  offerUpdateCheck();
}

void OvertoniumEditor::parentHierarchyChanged() {
  // Deferred, since this runs while the window is taking the editor as its
  // content and is not yet its final size. Restyling now would lay the editor
  // out at the window's initial size. Safe if the editor closes first.
  juce::MessageManager::callAsync([safe = SafePointer<OvertoniumEditor>(this)] {
    if (safe != nullptr)
      safe->dressStandaloneWindow();
  });
}

void OvertoniumEditor::dressStandaloneWindow() {
  if (plugin().wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    return;

  // Not in the constructor, since the standalone makes the editor its
  // content only afterwards.
  if (auto *window =
          dynamic_cast<juce::DocumentWindow *>(getTopLevelComponent()))
    dressWindow(*window);
}

void OvertoniumEditor::componentMovedOrResized(juce::Component &component, bool,
                                               bool wasResized) {
  if (!wasResized)
    return;

  if (auto *window = dynamic_cast<juce::DocumentWindow *>(&component))
    centreWindowOptionsButton(*window);
}

void OvertoniumEditor::centreWindowOptionsButton(juce::DocumentWindow &window) {
  const auto bar = window.getTitleBarArea();

  // By name, since the button is JUCE's. Only the standalone's window has
  // one.
  for (auto *child : window.getChildren()) {
    auto *button = dynamic_cast<juce::TextButton *>(child);

    if (button == nullptr || button->getName() != "Options")
      continue;

    button->setBounds(button->getBounds().withY(
        bar.getY() + (bar.getHeight() - button->getHeight()) / 2));
    return;
  }
}

void OvertoniumEditor::dressWindow(juce::DocumentWindow &window) {
  // Only once, since this runs on every hierarchy change and restyling sends
  // a look and feel change through the whole window.
  if (&window.getLookAndFeel() == &lookAndFeel)
    return;

  // Recreates the title bar buttons in our style. Safe although the window
  // outlives the editor, since a component holds its look and feel weakly. A
  // test pins that.
  window.setLookAndFeel(&lookAndFeel);
  window.setBackgroundColour(colours::background);

  // Re-centres the title bar contents after every layout. Only one window is
  // listened to at a time.
  if (auto *previous = standaloneWindow.getComponent())
    previous->removeComponentListener(this);

  window.addComponentListener(this);
  standaloneWindow = &window;

  centreWindowOptionsButton(window);
}

void OvertoniumEditor::setPresetName(const juce::String &name) {
  plugin().setLoadedPresetName(name);
  topBar.setPresetName(name);
}

OvertoniumEditor::~OvertoniumEditor() {
  stopTimer();

  if (auto *window = standaloneWindow.getComponent())
    window->removeComponentListener(this);

  // Asked to stop, never waited for: waiting on the message thread could
  // hang the window.
  plugin().updates().cancel();
  plugin().updates().setListener(nullptr);

  setLookAndFeel(nullptr);
}

void OvertoniumEditor::paint(juce::Graphics &g) {
  const auto r = getLocalBounds().toFloat();

  g.setGradientFill(juce::ColourGradient(
      colours::background.brighter(0.16f), r.getX(), r.getY(),
      colours::background.darker(0.35f), r.getRight(), r.getBottom(), false));
  g.fillRect(r);

  // No grain or vignette, since the opaque strips cover almost the whole
  // window and a vignette would need paintOverChildren, which runs for every
  // meter repaint.
}

void OvertoniumEditor::resized() {
  const int logicalWidth = juce::roundToInt((float)getWidth() / zoom);
  const int logicalHeight = juce::roundToInt((float)getHeight() / zoom);

  // The minimum height changes when the top bar reflows, so the limits are
  // reapplied on a change. Only on a change, since applying them resizes and
  // comes back here.
  if (chromeHeight(juce::jmax(minimumLogicalWidth(), logicalWidth)) !=
      limitsChromeHeight) {
    const auto before = getBounds();
    applyResizeLimits(logicalWidth);

    // If raising the floor grew the window, that pass has already laid
    // everything out at the new size.
    if (getBounds() != before)
      return;
  }

  // Over the whole window and unzoomed, since the macro panel and notices
  // are not part of the instrument's scale.
  macroPanel.setBounds(getLocalBounds());
  notice.setBounds(getLocalBounds());

  content.setTransform(juce::AffineTransform::scale(zoom));
  content.setBounds(0, 0, logicalWidth, logicalHeight);

  auto area = content.getLocalBounds();
  topBar.setBounds(
      area.removeFromTop(ovt::ui::TopBar::heightForWidth(logicalWidth)));

  // Playing above Tuning, in the order their buttons stand in.
  for (auto *bar : {static_cast<juce::Component *>(&playingBar),
                    static_cast<juce::Component *>(&tuningBar)})
    if (bar->isVisible())
      bar->setBounds(area.removeFromTop(
          static_cast<ovt::ui::OptionBar *>(bar)->heightForWidth(
              logicalWidth)));

  // No inset. The vertical breathing room is kStripPadY inside each column.
  const auto gutterArea = area.removeFromLeft(kGutterWidth);

  // The scroll bar width is always reserved, so the channels do not shift
  // sideways when it appears.
  const auto parameterBarArea = area.removeFromRight(kScrollBarThickness);

  // Noise is pinned on the far right, outside the scrolling series.
  const auto noiseArea = area.removeFromRight(kStripWidth);
  area.removeFromRight(kMasterGap);

  viewport.setBounds(area);

  const int stripHeight = viewport.getMaximumVisibleHeight();

  gutter.setBounds(gutterArea.withHeight(stripHeight));
  noiseStrip.setBounds(noiseArea.withHeight(stripHeight));
  stripsHolder.setSize(kNumHarmonics * kStripWidth, stripHeight);

  for (int i = 0; i < (int)strips.size(); ++i)
    strips[(size_t)i]->setBounds(i * kStripWidth, 0, kStripWidth, stripHeight);

  // Every column gets the same scroll, so captions stay beside their knobs.
  // Clamped here, since the range depends on the height and the fold state.
  const auto bands =
      layoutBands(juce::Rectangle<int>(0, 0, kStripWidth, stripHeight)
                      .reduced(kStripPadX, kStripPadY),
                  collapsedSections);

  stripLayoutArea = juce::Rectangle<int>(0, 0, kStripWidth, stripHeight)
                        .reduced(kStripPadX, kStripPadY);
  scrollRange = bands.maxScroll;
  scrollBandHeight = bands.middle.getHeight();

  scrollY = juce::jlimit(0, scrollRange, scrollY);

  // The full height of the mixer, not just the band it scrolls.
  parameterBar.setVisible(scrollRange > 0);
  parameterBar.setBounds(parameterBarArea);

  parameterBar.setRangeLimits(0.0, (double)bands.contentHeight,
                              juce::dontSendNotification);
  syncScrollBar();

  gutter.setScroll(scrollY);
  noiseStrip.setScroll(scrollY);

  for (auto &strip : strips)
    strip->setScroll(scrollY);

  // Only write what changed, since a live resize fires this constantly. The
  // lock covers the reads as well as the writes. See
  // OvertoniumProcessor::stateLock.
  const juce::ScopedLock sl(plugin().stateLock());
  auto &state = plugin().apvts.state;

  if ((int)state.getProperty(kEditorWidth, -1) != logicalWidth)
    state.setProperty(kEditorWidth, logicalWidth, nullptr);

  if ((int)state.getProperty(kEditorHeight, -1) != logicalHeight)
    state.setProperty(kEditorHeight, logicalHeight, nullptr);

  if (std::abs((double)state.getProperty(kEditorZoom, -1.0) - (double)zoom) >
      1.0e-6)
    state.setProperty(kEditorZoom, (double)zoom, nullptr);
}

juce::Rectangle<int> OvertoniumEditor::standardSize() const {
  // Wide enough for all 32 strips, and tall enough for whatever is not
  // folded away.
  const int width = kGutterWidth + kStripWidth + kMasterGap +
                    kNumHarmonics * kStripWidth + kScrollBarThickness;

  // At this width there is no sideways scroll bar, so its room goes to the
  // strips.
  return {width, chromeHeight(width) - kScrollBarThickness +
                     preferredStripHeight(collapsedSections)};
}

void OvertoniumEditor::fitAllChannels() {
  const auto standard = standardSize();

  // The limits first, for the width about to be set, since setSize does not
  // consult them.
  applyResizeLimits(standard.getWidth());

  setSize(juce::roundToInt((float)standard.getWidth() * zoom),
          juce::roundToInt((float)standard.getHeight() * zoom));
}

void OvertoniumEditor::applyResizeLimits() {
  applyResizeLimits(juce::roundToInt((float)getWidth() / zoom));
}

void OvertoniumEditor::applyResizeLimits(int forLogicalWidth) {
  // Limits are in logical pixels, so they scale with the zoom.
  const int minWidth = minimumLogicalWidth();

  // The height floor follows the width, since the top bar takes more rows
  // as the window narrows. Never computed below minWidth, which covers an
  // unset size during construction.
  const int width = juce::jmax(minWidth, forLogicalWidth);
  const int chrome = chromeHeight(width);
  const int minHeight = chrome + minimumStripHeight(collapsedSections);

  limitsChromeHeight = chrome;

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

  // The bar keeps its own copy for the tick in the Zoom submenu.
  topBar.setZoomChoice(zoom);

  applyResizeLimits(juce::roundToInt(logicalWidth));

  setSize(juce::roundToInt(logicalWidth * zoom),
          juce::roundToInt(logicalHeight * zoom));
}

void OvertoniumEditor::publishCollapsedSections() {
  gutter.setCollapsedSections(collapsedSections);
  noiseStrip.setCollapsedSections(collapsedSections);

  for (auto &strip : strips)
    strip->setCollapsedSections(collapsedSections);
}

void OvertoniumEditor::scrollBarMoved(juce::ScrollBar *bar, double newStart) {
  if (bar != &parameterBar)
    return;

  // Through scrollParameters, as the wheel is. Guarded so the bar is not
  // moved back under the pointer while it is being dragged.
  const juce::ScopedValueSetter<bool> dragging(barIsDriving, true);

  scrollParameters(juce::roundToInt(newStart) - scrollY);
}

bool OvertoniumEditor::scrollParameters(int delta) {
  if (scrollRange <= 0)
    return false;

  // Straight to the pixel asked for. The header cap hides a part row.
  const int wanted = juce::jlimit(0, scrollRange, scrollY + delta);

  // Consumed even at either end, so the wheel never falls through and
  // scrolls the mixer sideways.
  if (wanted == scrollY)
    return true;

  scrollY = wanted;

  gutter.setScroll(scrollY);
  noiseStrip.setScroll(scrollY);

  for (auto &strip : strips)
    strip->setScroll(scrollY);

  if (!barIsDriving)
    syncScrollBar();

  {
    // See OvertoniumProcessor::stateLock.
    const juce::ScopedLock sl(plugin().stateLock());
    plugin().apvts.state.setProperty(kScroll, scrollY, nullptr);
  }

  return true;
}

void OvertoniumEditor::syncScrollBar() {
  // The wheel moves the rows without resized(), so the bar is told here.
  // Silently, so it does not call back.
  parameterBar.setCurrentRange(
      juce::Range<double>((double)scrollY,
                          (double)(scrollY + scrollBandHeight)),
      juce::dontSendNotification);
}

void OvertoniumEditor::mouseWheelMove(const juce::MouseEvent &e,
                                      const juce::MouseWheelDetails &wheel) {
  // Sideways scrolling goes straight to the mixer's viewport, since the
  // gutter and noise channel are its siblings and a bubbled event would stop
  // here.
  const bool sideways = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);

  if (sideways || e.mods.isShiftDown()) {
    if (viewport.useMouseWheelMoveIfNeeded(e, wheel))
      return;
  } else if (scrollParameters(
                 -juce::roundToInt(wheel.deltaY * 14.0f * 16.0f))) {
    return;
  }

  juce::Component::mouseWheelMove(e, wheel);
}

int OvertoniumEditor::chromeHeight(int logicalWidth) const {
  // The top bar and each open bar reflow when narrow, so all of it depends
  // on the width.
  auto height =
      ovt::ui::TopBar::heightForWidth(logicalWidth) + kScrollBarThickness;

  if ((openBars & kPlayingBar) != 0)
    height += playingBar.heightForWidth(logicalWidth);
  if ((openBars & kTuningBar) != 0)
    height += tuningBar.heightForWidth(logicalWidth);

  return height;
}

void OvertoniumEditor::toggleBar(int bar) {
  openBars ^= bar;

  // Refreshed before it shows, so it never opens on stale values.
  playingBar.refresh();
  tuningBar.refresh();

  playingBar.setVisible((openBars & kPlayingBar) != 0);
  tuningBar.setVisible((openBars & kTuningBar) != 0);
  topBar.setBarsOpen(playingBar.isVisible(), tuningBar.isVisible());

  {
    // See OvertoniumProcessor::stateLock.
    const juce::ScopedLock sl(plugin().stateLock());
    plugin().apvts.state.setProperty(kOpenBars, openBars, nullptr);
  }

  // The floor moves by a row. A window at the old floor grows, any other
  // keeps its size and the mixer gives up the height.
  applyResizeLimits();
  resized();
}

void OvertoniumEditor::toggleSection(Section section) {
  if (section == Section::NumSections)
    return;

  collapsedSections ^= sectionBit(section);

  publishCollapsedSections();
  {
    // See OvertoniumProcessor::stateLock.
    const juce::ScopedLock sl(plugin().stateLock());
    plugin().apvts.state.setProperty(kCollapsedSections, (int)collapsedSections,
                                     nullptr);
  }

  // The window keeps its size. The rows scroll, so folding only shows more
  // of them.
  resized();
}

void OvertoniumEditor::maybeCheckForUpdates() {
  if (!ovt::updateCheckAllowed())
    return;

  // A SafePointer, since the fetch outlives the window. The answer arrives
  // on the message thread, where the editor is also destroyed, so the pointer
  // is either valid or null.
  plugin().updates().setListener(
      [safe = juce::Component::SafePointer<OvertoniumEditor>(this)] {
        if (auto *editor = safe.getComponent())
          if (const auto release = editor->plugin().updates().newerRelease())
            editor->announceRelease(release->version, release->url);
      });

  plugin().updates().start(OVERTONIUM_VERSION);
}

void OvertoniumEditor::offerUpdateCheck() {
  if (ovt::updateCheckAllowed()) {
    maybeCheckForUpdates();
    return;
  }

  if (ovt::updateCheckOffered())
    return;

  // Shown inside the window, so a host scanning plugins is not held up, and
  // marked only when answered, so an unattended editor does not use up the
  // one time it is asked.
  notice.show(
      "UPDATES",
      "Overtonium can check for a new version when its window opens. "
      "It reads one small file from the project's website and sends "
      "nothing about you. You can change this any time under "
      "SETTINGS.",
      "Check for updates", "Not now",
      [this] {
        ovt::markUpdateCheckOffered();
        ovt::setUpdateCheckAllowed(true);
        maybeCheckForUpdates();
      },
      [] { ovt::markUpdateCheckOffered(); });
}

void OvertoniumEditor::announceRelease(const juce::String &version,
                                       const juce::String &url) {
  // SETTINGS shows it for as long as it is true.
  topBar.setUpdateAvailable(version, url);

  // And a dialog once per release, marked when answered.
  if (ovt::announcedVersion() == version)
    return;

  notice.show(
      "UPDATE",
      "Overtonium " + version +
          " is available. The release notes list what has changed. "
          "SETTINGS stays lit until you install it.",
      "See what's new", "Later",
      [version, url] {
        ovt::markVersionAnnounced(version);
        juce::URL(url).launchInDefaultBrowser();
      },
      [version] { ovt::markVersionAnnounced(version); });
}

void OvertoniumEditor::applyPreset(int index) {
  // Through the processor so the host's program follows. Recorded for undo,
  // since a person chose it. A program change from MIDI is not recorded.
  plugin().recordEdit("Load preset",
                      [this, index] { plugin().applyFactoryPreset(index); });
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
  return plugin().apvts.getParameter(
      params::oscParamId(roleSuffix(role), index));
}

bool OvertoniumEditor::drawStarted(juce::Point<int> onScreen) {
  if (!drawArmed)
    return false;

  // One gesture for the whole drawn line, opened on every fader it could
  // reach. Faders that never move add nothing to the undo step.
  for (auto *param : faderParameters())
    if (param != nullptr)
      param->beginChangeGesture();

  drawingNow = true;
  lastDrawn = onScreen;
  hoverLocked = true;

  applyDrawAt(onScreen);
  return true;
}

void OvertoniumEditor::drawMovedTo(juce::Point<int> onScreen) {
  if (!drawingNow)
    return;

  // Every column between the last point and this one, so a fast drag leaves
  // no gaps.
  const auto from = lastDrawn;
  const auto steps = juce::jmax(1, std::abs(onScreen.x - from.x));

  for (int i = 1; i <= steps; ++i) {
    const auto t = (double)i / (double)steps;

    applyDrawAt({from.x + juce::roundToInt(t * (onScreen.x - from.x)),
                 from.y + juce::roundToInt(t * (onScreen.y - from.y))});
  }

  lastDrawn = onScreen;
}

void OvertoniumEditor::drawEnded() {
  if (!drawingNow)
    return;

  drawingNow = false;
  hoverLocked = false;

  for (auto *param : faderParameters())
    if (param != nullptr)
      param->endChangeGesture();
}

void OvertoniumEditor::selectionStarted(int index0, juce::Point<int> onScreen) {
  if (index0 < 0 || index0 >= kNumHarmonics)
    return;

  // The first channel decides the whole drag: starting on an unselected
  // channel adds, starting on a selected one removes.
  selectingAdds = !selection[(size_t)index0];
  selection[(size_t)index0] = selectingAdds;
  selectingNow = true;
  lastSelected = onScreen;

  // Adding to a selection switches LINK on with the Selected scope, as if
  // chosen from the menu. Removing leaves the tool alone.
  if (selectingAdds && (!topBar.isLinkEnabled() ||
                        topBar.getLinkScope() != LinkScope::Selected)) {
    chooseTool(ovt::ui::PointerTool::Link);
    topBar.setLinkScope(LinkScope::Selected);

    if (topBar.onLinkSettingsChanged)
      topBar.onLinkSettingsChanged();
  }

  selectionChanged(false);
}

void OvertoniumEditor::selectionMovedTo(juce::Point<int> onScreen) {
  if (!selectingNow)
    return;

  // Every column between the last point and this one, as for drawing.
  const auto from = lastSelected;
  const auto steps = juce::jmax(1, std::abs(onScreen.x - from.x));

  for (int i = 1; i <= steps; ++i)
    applySelectionAt({from.x + juce::roundToInt((double)i / (double)steps *
                                                (onScreen.x - from.x)),
                      from.y});

  lastSelected = onScreen;
  selectionChanged(false);
}

void OvertoniumEditor::selectionEnded() {
  if (!selectingNow)
    return;

  selectingNow = false;
  selectionChanged(true);
}

void OvertoniumEditor::applySelectionAt(juce::Point<int> onScreen) {
  // By column alone, at any height.
  for (size_t i = 0; i < strips.size(); ++i) {
    const auto local = strips[i]->getLocalPoint(nullptr, onScreen);

    if (local.x >= 0 && local.x < strips[i]->getWidth())
      selection[i] = selectingAdds;
  }
}

void OvertoniumEditor::clearSelection() {
  selection.fill(false);
  selectionChanged(true);
}

void OvertoniumEditor::selectionChanged(bool save) {
  bool any = false;

  for (size_t i = 0; i < strips.size(); ++i) {
    strips[i]->setSelected(selection[i]);
    any = any || selection[i];
  }

  topBar.setHasSelection(any);

  if (save) {
    juce::StringArray numbers;

    for (int i = 0; i < kNumHarmonics; ++i)
      if (selection[(size_t)i])
        numbers.add(juce::String(i + 1));

    // See OvertoniumProcessor::stateLock.
    const juce::ScopedLock sl(plugin().stateLock());
    plugin().apvts.state.setProperty(kLinkSelection,
                                     numbers.joinIntoString(","), nullptr);
  }

  updateLinkGlow();
}

/// Every fader a drawn line can reach, the noise channel included.
std::vector<juce::RangedAudioParameter *>
OvertoniumEditor::faderParameters() const {
  std::vector<juce::RangedAudioParameter *> out;
  out.reserve(kNumHarmonics + 1);

  for (int i = 0; i < kNumHarmonics; ++i)
    out.push_back(oscParameter(Role::Volume, i));

  out.push_back(
      plugin().apvts.getParameter(params::noiseParamId(params::volumeSuffix)));

  return out;
}

/// Sets whichever fader is under this point, if one is.
void OvertoniumEditor::applyDrawAt(juce::Point<int> onScreen) {
  for (auto &strip : strips) {
    const auto local = strip->getLocalPoint(nullptr, onScreen);

    if (local.x >= 0 && local.x < strip->getWidth()) {
      strip->drawFaderAt(local.y);
      return;
    }
  }

  const auto local = noiseStrip.getLocalPoint(nullptr, onScreen);

  if (local.x >= 0 && local.x < noiseStrip.getWidth())
    noiseStrip.drawFaderAt(local.y);
}

void OvertoniumEditor::pollDrawModifier() {
  // Polled, since a Slider under the pointer swallows modifier changes and
  // the editor would never hear them.
  const auto held = juce::ModifierKeys::getCurrentModifiers().isShiftDown();

  if (held == shiftHeld)
    return;

  shiftHeld = held;
  refreshDrawArmed();
}

void OvertoniumEditor::refreshDrawArmed() {
  // Held or latched, both arm it and both light the button.
  const auto armed = shiftHeld || drawLatched;

  if (armed == drawArmed)
    return;

  drawArmed = armed;
  syncLinkUi();
}

void OvertoniumEditor::rememberTool() {
  // See OvertoniumProcessor::stateLock.
  const juce::ScopedLock sl(plugin().stateLock());
  auto &tree = plugin().apvts.state;

  tree.setProperty(kLinkOn, topBar.isLinkEnabled(), nullptr);
  tree.setProperty(kDrawLatched, drawLatched, nullptr);
}

void OvertoniumEditor::toggleDrawLatch() {
  drawLatched = !drawLatched;

  rememberTool();
  refreshDrawArmed();
}

void OvertoniumEditor::syncLinkUi() {
  // The button is in the gutter and the settings are on the bar, so the
  // button is told. It shows one tool, with drawing winning while armed, so
  // the pencil shows while the modifier is held.
  gutter.setTool(currentTool(), topBar.getLinkCurve());

  // The preview follows at once, without waiting for the pointer to move.
  updateLinkGlow();
  updateLinkCursor();
}

bool OvertoniumEditor::isLinkEnabled() const { return topBar.isLinkEnabled(); }

ovt::ui::PointerTool OvertoniumEditor::currentTool() const {
  if (drawArmed)
    return ovt::ui::PointerTool::Draw;

  return topBar.isLinkEnabled() ? ovt::ui::PointerTool::Link
                                : ovt::ui::PointerTool::Pointer;
}

void OvertoniumEditor::chooseTool(ovt::ui::PointerTool which) {
  // LINK's scope and curve are kept, so going back to it finds them as they
  // were. The modifier can still arm drawing.
  drawLatched = which == ovt::ui::PointerTool::Draw;
  topBar.setLinkEnabled(which == ovt::ui::PointerTool::Link);

  // Saved so the next window opens on this tool.
  rememberTool();

  // refreshDrawArmed, since the modifier itself has not changed.
  refreshDrawArmed();
  syncLinkUi();
}

void OvertoniumEditor::followMacroTints() {
  const auto readInt = [this](const juce::String &id) {
    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            plugin().apvts.getParameter(id)))
      return (int)std::lround(p->convertFrom0to1(p->getValue()));

    return 0;
  };

  // The eight macros read once rather than per control. Not skipped when no
  // macro moved, since the result also depends on the knob, and the strips
  // only repaint on a change.
  struct Reach {
    int row = 0;
    int scope = 0;
    std::uint32_t selected = 0;
    ovt::params::MacroCurve curve = ovt::params::MacroCurve::Uniform;
    int anchor = 0;
    juce::Colour colour;
    float amount = 0.0f;
    juce::NormalisableRange<float> range{0.0f, 1.0f};
  };

  std::array<Reach, (size_t)ovt::params::kNumMacros> macros;

  for (int m = 0; m < ovt::params::kNumMacros; ++m) {
    auto &reach = macros[(size_t)m];

    reach.row = readInt(ovt::params::macroRowId(m));

    if (reach.row == 0)
      continue;

    reach.scope = readInt(ovt::params::macroScopeId(m));
    reach.selected = plugin().macroChannels()[(size_t)m].load();
    reach.curve =
        (ovt::params::MacroCurve)readInt(ovt::params::macroCurveId(m));
    reach.colour =
        ovt::params::macroColour(readInt(ovt::params::macroColourId(m)));
    reach.range = ovt::params::macroRowRange(plugin().apvts, reach.row);
    reach.anchor = readInt(ovt::params::macroAnchorId(m)) - 1;

    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            plugin().apvts.getParameter(ovt::params::macroAmountId(m))))
      reach.amount = p->convertFrom0to1(p->getValue());
  }

  // The bar's macro count, the only sign of macros while the panel is shut.
  // Updated on this timer so it follows the macros.
  int made = 0;
  for (const auto &reach : macros)
    made += reach.row != 0 ? 1 : 0;

  topBar.setMacroCount(made);

  for (int i = 0; i < kNumHarmonics; ++i) {
    for (int r = 0; r < kNumRoles; ++r) {
      auto *q = oscParameter((Role)r, i);

      if (q == nullptr)
        continue;

      juce::Colour wearing;
      auto result = q->getValue();

      // The lowest-numbered macro reaching this control colours it.
      for (const auto &reach : macros) {
        // None, or a different row. Row 1 is the first real one, so a role is
        // row + 1.
        if (reach.row == 0 || reach.row - 1 != r)
          continue;

        if (!ovt::params::macroReaches(reach.scope, i, reach.selected))
          continue;

        // A macro wearing None drives the control without colouring it.
        wearing = reach.colour;

        // Where the engine will put it, by the snapshot's arithmetic. See
        // Cache::snapshot.
        result = juce::jlimit(
            0.0f, 1.0f,
            q->getValue() + reach.amount * ovt::params::macroWeight(
                                               reach.curve, i, reach.anchor));
        break;
      }

      strips[(size_t)i]->setMacroTint((Role)r, wearing, result);
    }
  }
}

void OvertoniumEditor::followArmedControl() {
  auto *waiting = plugin().midiLearn.armed();
  const juce::String wanted =
      waiting != nullptr ? waiting->paramID : juce::String();

  if (wanted == armedParameter)
    return;

  // Off the old control first, since a second can be armed while the first
  // waits.
  if (auto *was = ovt::ui::learn::controlFor(*this, armedParameter))
    ovt::ui::learn::markArmed(*was, false);

  armedParameter = wanted;

  if (auto *now = ovt::ui::learn::controlFor(*this, armedParameter))
    ovt::ui::learn::markArmed(*now, true);
}

void OvertoniumEditor::showLearnMenu(const juce::String &parameterId) {
  auto *parameter = dynamic_cast<juce::RangedAudioParameter *>(
      plugin().apvts.getParameter(parameterId));

  if (parameter == nullptr)
    return;

  juce::PopupMenu m;
  m.setLookAndFeel(&getLookAndFeel());

  // A header first, since appendItems opens with a separator.
  m.addSectionHeader(parameter->getName(40));
  ovt::ui::learn::appendItems(m, plugin().midiLearn, parameter);

  const auto p = juce::Desktop::getInstance()
                     .getMainMouseSource()
                     .getScreenPosition()
                     .roundToInt();

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withStandardItemHeight(22)
                      .withTargetScreenArea({p.x, p.y, 1, 1}),
                  [this, parameter](int result) {
                    ovt::ui::learn::applyChoice(result, plugin().midiLearn,
                                                parameter);
                  });
}

void OvertoniumEditor::showMacroPanel(bool show) {
  if (show)
    macroPanel.refresh();

  macroPanel.setVisible(show);
  macroPanel.toFront(false);
  topBar.setMacrosOn(show);

  // A question still waiting stays in front of it.
  if (notice.isShowing())
    notice.toFront(false);
}

void OvertoniumEditor::showMacroMenu(int macro) {
  using ovt::ui::PlayingBar;

  auto *amount = dynamic_cast<juce::RangedAudioParameter *>(
      plugin().apvts.getParameter(ovt::params::macroAmountId(macro)));
  auto *row = dynamic_cast<juce::RangedAudioParameter *>(
      plugin().apvts.getParameter(ovt::params::macroRowId(macro)));

  if (amount == nullptr || row == nullptr)
    return;

  const bool made = std::lround(row->convertFrom0to1(row->getValue())) != 0;

  auto m = PlayingBar::macroMenu(macro, made, plugin().midiLearn, amount);
  m.setLookAndFeel(&getLookAndFeel());

  const auto p = juce::Desktop::getInstance()
                     .getMainMouseSource()
                     .getScreenPosition()
                     .roundToInt();

  juce::Component::SafePointer<OvertoniumEditor> self(this);

  m.showMenuAsync(juce::PopupMenu::Options()
                      .withStandardItemHeight(22)
                      .withTargetScreenArea({p.x, p.y, 1, 1}),
                  [self, macro, amount](int result) {
                    if (self == nullptr)
                      return;

                    if (result == PlayingBar::kAssignMacro) {
                      self->macroPanel.make(macro);
                      self->showMacroPanel(true);
                      self->playingBar.refresh();
                    } else if (result == PlayingBar::kEditMacro) {
                      self->showMacroPanel(true);
                    } else {
                      ovt::ui::learn::applyChoice(
                          result, self->plugin().midiLearn, amount);
                    }
                  });
}

void OvertoniumEditor::showLinkMenu(const juce::String &parameterId) {
  topBar.showLinkMenu(nullptr, parameterId, &plugin().midiLearn, currentTool());
}

void OvertoniumEditor::updateLinkCursor() {
  // Set on the holder, since the strips and knobs take their parent's
  // pointer. A pencil while drawing is armed, the LINK cursor while LINK is
  // on. The noise channel is outside the holder, as LINK never reaches it.
  stripsHolder.setMouseCursor(drawArmed ? drawCursor()
                              : topBar.isLinkEnabled()
                                  ? linkCursor(topBar.getLinkCurve())
                                  : juce::MouseCursor());

  // The pointer may be still over a strip, so it is updated now.
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
    case LinkScope::Selected:
      selected = selection[(size_t)i];
      break;
    case LinkScope::All:
    case LinkScope::NumScopes:
    default:
      break;
    }

    // The strip under the mouse is always part of its own drag.
    selected = selected || i == sourceIndex;

    out[(size_t)i] =
        selected ? juce::jmax(0.001f, linkCurveWeight(curve, i, sourceIndex))
                 : 0.0f;
  }
}

void OvertoniumEditor::linkDragStarted(Role role, int sourceIndex) {
  hoverLocked = true;

  // LINK's state is latched at drag start, so toggling it mid-drag cannot
  // leave gestures open.
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

  const juce::ScopedValueSetter<bool> guard(propagatingLink, true);

  // The faders are shared out in decibels rather than across their travel,
  // since their travel is shaped for the hand. See linkIsDecibels.
  if (linkIsDecibels(role, gesture.curve)) {
    const auto decibelsAt = [](const juce::RangedAudioParameter *p,
                               float normalised) {
      return params::levelDecibels(p->convertFrom0to1(normalised));
    };

    const auto now = params::levelDecibels(plainValue);
    const auto delta =
        now - decibelsAt(source, gesture.baseline[(size_t)sourceIndex]);

    for (int i = 0; i < kNumHarmonics; ++i) {
      if (i == sourceIndex || !gesture.includes(i))
        continue;

      auto *param = oscParameter(role, i);
      if (param == nullptr)
        continue;

      const auto landed = linkedValue(
          gesture.curve, decibelsAt(param, gesture.baseline[(size_t)i]), delta,
          gesture.weight[(size_t)i], gesture.jitter[(size_t)i], now,
          params::kQuietestLevelDb, 0.0f);

      param->setValueNotifyingHost(param->convertTo0to1(
          juce::Decibels::decibelsToGain(landed, params::kQuietestLevelDb)));
    }

    return;
  }

  // How far the dragged knob has travelled, in normalised units.
  const auto delta =
      source->convertTo0to1(plainValue) - gesture.baseline[(size_t)sourceIndex];

  // The dragged strip's live value, which Gather collapses everything onto.
  const auto target =
      juce::jlimit(0.0f, 1.0f, gesture.baseline[(size_t)sourceIndex] + delta);

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
  // Armed, every fader lights and nothing else, through the same glow as
  // LINK's preview.
  if (drawArmed) {
    for (auto &strip : strips)
      strip->setLinkGlow(Role::Volume, 1.0f, true);

    // Every channel number lights too, since drawing reaches every channel.
    for (auto &strip : strips)
      strip->setLinkReach(1.0f);

    noiseStrip.setDrawGlow(true);
    return;
  }

  noiseStrip.setDrawGlow(false);

  auto role = Role::Tune;
  std::array<float, kNumHarmonics> weight{};

  if (linkGesture.active) {
    role = linkGesture.role;
    weight = linkGesture.weight;
  } else if (isLinkEnabled() && !drawArmed && hoverStrip >= 0 &&
             roleForRow(hoverRow, role)) {
    // Nothing grabbed yet, so preview what the knob under the pointer would
    // take with it.
    gatherLinkWeights(hoverStrip, topBar.getLinkScope(), topBar.getLinkCurve(),
                      weight);
  }

  auto strongest = 0.0f;
  for (auto w : weight)
    strongest = juce::jmax(strongest, w);

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto w = weight[(size_t)i];

    // Relative to the strip that moves most, so a curve shows its slope. The
    // floor keeps a barely moving strip visibly in the drag.
    const auto glow =
        w > 0.0f && strongest > 0.0f ? 0.4f + 0.6f * (w / strongest) : 0.0f;

    strips[(size_t)i]->setLinkGlow(role, glow);

    // The channel number takes the same reading on every scope, All included,
    // so an unlit mixer always means nothing is armed.
    strips[(size_t)i]->setLinkReach(glow);
  }
}

// ---- polling ----------------------------------------------------------------

void OvertoniumEditor::stepHistory(bool redo) {
  auto &undo = plugin().undo();

  if (redo)
    undo.redo();
  else
    undo.undo();
}

bool OvertoniumEditor::keyPressed(const juce::KeyPress &key) {
  if (!key.getModifiers().isCommandDown() || key.getKeyCode() != 'Z')
    return false;

  stepHistory(key.getModifiers().isShiftDown());
  return true;
}

void OvertoniumEditor::syncSharedModulators() {
  // Read back from the cached atomics every tick, since a preset or a menu
  // can change these. The gutter ignores calls that change nothing.
  const auto &cache = plugin().parameters();

  const auto on = [](const std::atomic<float> *p) {
    return p != nullptr && p->load() > 0.5f;
  };

  gutter.setSharedModulators(on(cache.pmInPhase), on(cache.amInPhase));
  gutter.setGlideSwitches(on(cache.glideTrigger), on(cache.glideMode));
}

void OvertoniumEditor::timerCallback() {
  ++tick;

  pollDrawModifier();
  followArmedControl();
  followMacroTints();

  // Frames and the area the dirty rectangles enclose both cost the window
  // manager, so the mixer is invalidated as a few merged rectangles, and the
  // meters are read every other tick, 15 times a second.
  if ((tick % 2) != 0)
    return;

  dirtyRegions.clearQuick();

  const auto add = [this](juce::Component &from, juce::Rectangle<int> band) {
    if (!band.isEmpty())
      dirtyRegions.add(content.getLocalArea(&from, band));
  };

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &strip = *strips[(size_t)i];
    add(strip, strip.setMeterLevel(plugin().getPartialLevel(i)));

    // Kept apart from the meter bands, which merge differently. See
    // mergeIntoRows.
    strip.setActivity(
        plugin().getPartialEnvelope(i), plugin().getPartialTremolo(i),
        plugin().getPartialPitch(i), plugin().getPartialVelocity(i),
        plugin().getPartialPressure(i), stripLamps);

    for (const auto &band : stripLamps)
      lampRegions.add(content.getLocalArea(&strip, band));

    stripLamps.clearQuick();
  }

  add(noiseStrip, noiseStrip.setMeterLevel(plugin().getNoiseLevel()));

  noiseStrip.setActivity(
      plugin().getNoiseEnvelope(), plugin().getNoiseTremolo(),
      plugin().getNoiseVelocity(), plugin().getNoisePressure(), stripLamps);

  for (const auto &band : stripLamps)
    lampRegions.add(content.getLocalArea(&noiseStrip, band));

  stripLamps.clearQuick();

  coalesceRegions(dirtyRegions, kMaxDirtyRegions);
  mergeIntoRows(lampRegions);

  for (const auto &region : dirtyRegions)
    content.repaint(region);

  for (const auto &region : lampRegions)
    content.repaint(region);

  lampRegions.clearQuick();

  // Its own region, at the other end of the window from the mixer.
  topBar.setOutputLevels(plugin().getOutputLevelLeft(),
                         plugin().getOutputLevelRight());

  // How the instrument is being played, at the full rate.
  playingBar.showActivity({plugin().getActiveVoiceCount(),
                           plugin().isSustainDown(), plugin().getBendPosition(),
                           plugin().getWheelPosition(),
                           plugin().getPressurePosition()});

  // What Adaptive did to the held chord, at the full rate, and read even
  // while the bar is closed so it opens on the current chord.
  {
    ovt::AdaptiveSnapshot chord;
    if (plugin().readAdaptiveSnapshot(chord))
      tuningBar.showChord(chord);
  }

  // The rest is housekeeping, at a fraction of the rate.
  if ((tick % 8) != 0)
    return;

  // Through the cached atomics, since the parameter map needs a string
  // lookup.
  const auto &cache = plugin().parameters();

  const auto on = [](const std::atomic<float> *p) {
    return p != nullptr && p->load() > 0.5f;
  };

  topBar.updatePanelReadouts(plugin().getSampleRate());

  // Only while open. A closed bar is read again as it opens.
  for (auto *bar : {static_cast<ovt::ui::OptionBar *>(&playingBar),
                    static_cast<ovt::ui::OptionBar *>(&tuningBar)})
    if (bar->isVisible())
      bar->refresh();

  syncSharedModulators();

  // A MIDI program change can load a preset without telling the window, so
  // the button is compared with what is loaded and set only on a difference.
  if (const auto loaded = plugin().presetName();
      loaded != topBar.getPresetName())
    topBar.setPresetName(loaded);

  // Dim whatever a solo elsewhere is silencing. Solo spans the noise channel
  // too.
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
