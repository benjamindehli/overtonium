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

/// Everything above and below the strips. The top bar reflows onto a second
/// row when narrow, so its height depends on the width it is given.
int chromeHeight(int logicalWidth) {
  return ovt::ui::TopBar::heightForWidth(logicalWidth) + kScrollBarThickness;
}

/// The narrowest window, in logical pixels.
///
/// Wide enough for a usable stretch of mixer, and never narrower than the top
/// bar can lay itself out in without dropping a group. Every width the floor
/// is worked out for is clamped up to this, so that a window with no size yet
/// gets the narrow window's answer rather than one for a width nothing can be.
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

/// Whether LINK is on. Kept beside the scope and the curve because it is the
/// same setting: those two say what a drag would reach and this says whether
/// it reaches anything. Saving two of the three and not the third meant a
/// window reopened remembering how LINK was set up and having switched it off.
const juce::Identifier kLinkOn{"linkOn"};

/// Whether the fader drawing tool is latched on. Remembered with the session
/// for the reason LINK is: it is a tool rather than part of the sound, and one
/// you left on should still be on when you come back to it. A mode nobody can
/// see is a mode nobody remembers, which is why the button it sets is lit the
/// whole time it is on.
const juce::Identifier kDrawLatched{"drawLatched"};

/// The curve, by name. See linkCurveFromState.
const juce::Identifier kLinkCurveId{"linkCurveId"};

/// What held the curve before the names, as an index into a list that has since
/// changed. Read when kLinkCurveId is absent, and dropped on the next write so
/// a state cannot carry two answers.
const juce::Identifier kLinkCurve{"linkCurve"};
const juce::Identifier kCollapsedSections{"collapsedSections"};

/// How far the parameters are scrolled, remembered with the session.
///
/// Kept for the reason the fold mask is: it is where you left the window
/// rather than anything about the sound, and coming back to a mixer scrolled
/// somewhere else is the same small annoyance as coming back to one folded
/// differently. Clamped on the way in, since the window it is restored into
/// may be a different height from the one it was saved from.
const juce::Identifier kScroll{"parameterScroll"};

/// What the APVTS calls each parameter's node in the state tree. Its own
/// constant is private, but the name is part of the format: it is what the
/// saved state and every preset file are written in.
} // namespace

// =============================================================================

RowGutter::RowGutter() {
  // One button rather than a switch and a chevron beside it. It always opens
  // the menu, and it lights when the switch inside is on, so the state is
  // visible without the state being what the click does.
  // No word on it. The tool a drag will use is a thing you recognise by the
  // pointer it gives you, so the button wears that pointer, and the menu it
  // opens is where the names are. See ui::PointerTool.
  toolButton.setTooltip(
      "What a drag in the mixer does. Pointer moves one control, Link moves "
      "the row it belongs to, and Draw sweeps values across the series. Only "
      "one at a time, since a drag cannot be two of them at once.");
  toolButton.setTitle("Pointer tool");
  toolButton.setColour(juce::TextButton::textColourOnId, colours::accent);

  // Lit, and it stays lit. A lamp going out says a thing is off, and there is
  // no off here: the pointer is as much a choice as the other two, and a dark
  // cap read as a tool that had been disabled rather than as the plain
  // pointer being the one selected. Which tool it holds is said by the icon.
  //
  // Here rather than in syncLinkUi, which returns early when the tool has not
  // changed and so would never have reached it on the way to the first paint.
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

  // L and T, the way a channel says M and S: one character each, with the
  // whole of it in the tooltip and the name a screen reader is given.
  glideLegato.setButtonText("L");
  glideLegato.setTooltip("Legato glide: only from a key still held down. "
                         "Dark, every note glides from the last one, even "
                         "after its key is up.");
  glideLegato.setTitle("Legato glide");
  glideLegato.setComponentID("glideLegato");
  glideLegato.onClick = [this] {
    if (onGlideLegatoClicked != nullptr)
      onGlideLegatoClicked();
  };

  glideFixedTime.setButtonText("T");
  glideFixedTime.setTooltip("Fixed time: every glide takes the knob's time, "
                            "however far it goes. Dark, a fixed rate, the "
                            "time per octave, so a wide leap takes "
                            "longer than a step.");
  glideFixedTime.setTitle("Fixed glide time");
  glideFixedTime.setComponentID("glideFixedTime");
  glideFixedTime.onClick = [this] {
    if (onGlideFixedTimeClicked != nullptr)
      onGlideFixedTimeClicked();
  };

  for (auto *b : {&glideLegato, &glideFixedTime})
    addAndMakeVisible(*b);

  // P for phase, one on each shape row. The same switch as the last item in
  // every shape button's menu, here so the state is on the panel.
  const auto setUpInPhase = [this](ovt::ui::ScreenSwitch &b, const char *which,
                                   const char *id,
                                   std::function<void()> RowGutter::*callback) {
    b.setButtonText("P");
    b.setTooltip(juce::String("The ") + which +
                 " in phase across the keyboard: one modulator every note "
                 "hears together. Dark, each note starts its own.");
    b.setTitle(juce::String(which) + " in phase across the keyboard");
    b.setComponentID(id);
    b.onClick = [this, callback] {
      if (this->*callback != nullptr)
        (this->*callback)();
    };
    addAndMakeVisible(b);
  };

  setUpInPhase(pitchInPhase, "pitch modulation", "pitchInPhase",
               &RowGutter::onPitchInPhaseClicked);
  setUpInPhase(ampInPhase, "amp modulation", "ampInPhase",
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
  // The same rows the captions are laid out from, so the button stands in the
  // band the strips beside it put their channel numbers in. The header is a
  // fixed height at the top of the column and no fold can move it, which is
  // why this does not have to run again when one changes.
  const auto rows =
      layoutRows(getLocalBounds().reduced(0, kStripPadY), collapsed, scroll);

  // The cap first, then the button, so the button is in front of the thing
  // that hides everything else up here.
  // Down to the foot of the header and no further. The row's own rectangle
  // already carries the column's top padding, so adding it again put the cap
  // nine pixels into the band and clipped the tops of the tuning knobs.
  headerCap.setBounds(0, 0, getWidth(),
                      juce::jmax(0, rows[(size_t)Row::Header].getBottom()));

  // The caps, beside their captions and gone with them when the section is
  // folded. Laid out before the cap is brought forward, so a row scrolled
  // under the header slides under it rather than over it.
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
  // The size of a channel's M and S, so the letters are the size theirs are.
  constexpr int kCapSize = 18;
  constexpr int kGap = 4;

  const auto area = rows[(size_t)row];
  const bool shown = area.getHeight() > 0 && !rowIsCollapsed(row, collapsed) &&
                     area.getBottom() > headerBottom;

  // Right up against the caption, so the switch and the word read as one
  // thing. Captions are right-aligned eight pixels in, which is where paint
  // puts them, and measured in the unlit weight, the one a caption has when
  // nothing is pointing at it.
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

  // The lamp does not move with the tool: see where it is set, in the
  // constructor. Only the icon changes.
  toolButton.setButtonText(tool == ovt::ui::PointerTool::Pointer ? "" : "");
  toolButton.setTitle(juce::String("Tool: ") + ovt::ui::pointerToolName(tool));
  toolButton.repaint();
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
  if (pitch == sharedPitchMod && amp == sharedAmpMod)
    return;

  sharedPitchMod = pitch;
  sharedAmpMod = amp;
  pitchInPhase.setToggleState(pitch, juce::dontSendNotification);
  ampInPhase.setToggleState(amp, juce::dontSendNotification);
  repaint();
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

  // Only the headings. The other captions name a control that is somewhere
  // else, so lighting one from here would say the pointer was on a knob it is
  // nowhere near.
  if (onHoverChanged)
    onHoverChanged(onHeading ? sectionHeading(section) : kNoRow);
}

void RowGutter::mouseExit(const juce::MouseEvent &) {
  if (onHoverChanged)
    onHoverChanged(kNoRow);
}

void RowGutter::paintHeaderBand(juce::Graphics &g) {
  // The gutter's header holds the LINK button rather than anything painted,
  // and that button sits in front of this cap so it stays reachable. See the
  // toFront pair in resized.
  paintChannelBackground(g, getLocalBounds(), colours::panel.darker(0.25f));

  // And the divider down the right edge, which paint() draws for the whole
  // height. Without it here the cap covers the top of the line and the border
  // between the gutter and the channels changes colour at the header.
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

    // "LEVEL" sits at the top of the tall fader row rather than floating in the
    // middle.
    if (row == Row::Fader)
      area = area.removeFromTop(16);

    // A folded row has no height, so its caption would be drawn into a
    // sliver and overlap the heading above it.
    if (rowIsCollapsed(row, collapsed))
      continue;

    const bool heading = isHeadingRow(row);
    const bool lit = row == highlighted && rowShowsHighlight(row);

    // A heading whose modulator the whole keyboard shares goes accent, the
    // same light everything else that is switched on here comes up in. It
    // reads as a property of the group, which is what it is: every channel in
    // it answers to the one switch.
    const bool shared = (row == Row::PitchModHeading && sharedPitchMod) ||
                        (row == Row::AmpModHeading && sharedAmpMod);

    g.setFont(makeFont(heading ? 10.0f : 9.5f, heading || lit));
    g.setColour(lit || shared ? colours::accent
                              : (heading ? colours::text : colours::textDim));
    g.drawText(text, area, juce::Justification::centredRight, false);

    // The disclosure mark, at the far left of the heading so it clears the
    // right-aligned caption whatever the caption says. Pointing down when the
    // group is open and right when it is folded, which is the way every file
    // list has done it for thirty years.
    if (heading) {
      const auto section = sectionOf(row);
      const bool folded = isCollapsed(collapsed, section);
      // Both orientations are one triangle turned a quarter, so the folded
      // one stands tall and narrow instead of the squat, wide shape a single
      // box gives when the arrow points sideways.
      //
      // The centre is where it is because of PITCH MOD. Captions are
      // right-aligned and that is the longest the gutter carries, so it
      // reaches nearer the mark than any other and sets how much room there
      // is to sit clear of both it and the gutter's own edge.
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

      // Lit with its own caption rather than left dim beside it. The two are
      // one control, and half of it coming up reads as a rendering fault.
      g.setColour(lit ? colours::accent : colours::textDim);
      g.fillPath(mark);
    }
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
    : juce::AudioProcessorEditor(&p), topBar(p.apvts, *this),
      noiseStrip(p.apvts, *this, *this), macroPanel(p.apvts) {
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
        std::make_unique<ChannelStrip>(plugin().apvts, *this, *this, *this, i);
    strip->onSectionToggled = [this](Section s) { toggleSection(s); };
    stripsHolder.addAndMakeVisible(*strip);
    strips.push_back(std::move(strip));
  }

  topBar.isUpdateCheckAllowed = [] { return ovt::updateCheckAllowed(); };

  topBar.onUpdateCheckToggled = [this](bool allowed) {
    ovt::setUpdateCheckAllowed(allowed);
    topBar.setUpdateOffer(false);

    if (allowed)
      maybeCheckForUpdates();
    else
      topBar.setUpdateAvailable({}, {});
  };

  topBar.onUpdateOfferAccepted = [this] {
    ovt::setUpdateCheckAllowed(true);
    topBar.setUpdateOffer(false);
    maybeCheckForUpdates();
  };

  // applyPreset goes through the processor, which records the name itself, so
  // this only has to show what is now loaded.
  topBar.onPresetChosen = [this](int index) {
    applyPreset(index);
    topBar.setPresetName(plugin().presetName());
  };

  topBar.onUserPresetChosen = [this](juce::File file) {
    juce::String error;
    bool loaded = false;

    plugin().recordEdit("Load preset", [this, &file, &error, &loaded] {
      loaded = presets::load(plugin().apvts, file, error);
    });

    if (loaded)
      setPresetName(file.getFileNameWithoutExtension());
    else
      complain("Could not load that preset", error);
  };

  topBar.onSaveUserPreset = [this](juce::String name) {
    juce::String error;

    if (presets::save(plugin().apvts, name, error))
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

  // Without this the editor is never focused and never sees a key press. Note
  // that plenty of hosts keep Cmd-Z for their own history and it will not reach
  // us at all, which is why the menu carries the same two entries.
  setWantsKeyboardFocus(true);

  // ---- restore the last window size -----------------------------------------
  //
  // A copy, taken once, rather than ten reads off the live tree spread through
  // the rest of this constructor. A host may be in setStateInformation on
  // another thread while the window opens, and the tree it is replacing is the
  // one being read here. See OvertoniumProcessor::stateLock.
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

  topBar.onLinkSettingsChanged = [this] {
    rememberTool();

    {
      // Every direct write to this tree holds the lock, this one included.
      // See OvertoniumProcessor::stateLock.
      const juce::ScopedLock sl(plugin().stateLock());
      auto &tree = plugin().apvts.state;

      tree.setProperty(kLinkScope, (int)topBar.getLinkScope(), nullptr);
      tree.setProperty(kLinkCurveId, linkCurveId(topBar.getLinkCurve()),
                       nullptr);
      tree.removeProperty(kLinkCurve, nullptr);
    }

    syncLinkUi();
  };

  // What the restored settings have to reach, and the reason it is a function
  // rather than the tail of that callback: opening a window has to arrive at
  // the same place a menu choice does, and a second list of things to update
  // is a second list to forget something from.
  syncLinkUi();

  // The menu belongs to the bar, which holds what it changes. The gutter holds
  // the button that opens it, and hands back what to hang it off.
  gutter.onLinkClicked = [this](juce::Component *anchor) {
    topBar.showLinkMenu(anchor, {}, &plugin().midiLearn, currentTool());
  };

  topBar.onToolChosen = [this](ovt::ui::PointerTool which) {
    chooseTool(which);
  };

  topBar.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  // Over everything, and hidden until asked for. Added to the editor rather
  // than to the scrolling content, so it covers the bar as well: it is a
  // thing you are doing instead of playing, not a part of the mixer.
  addChildComponent(macroPanel);

  macroPanel.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  macroPanel.onDismiss = [this] {
    macroPanel.setVisible(false);
    topBar.setMacrosOn(false);
  };

  topBar.onMacrosClicked = [this] {
    const bool opening = !macroPanel.isVisible();

    if (opening)
      macroPanel.refresh();

    macroPanel.setVisible(opening);
    macroPanel.toFront(false);
    topBar.setMacrosOn(opening);
  };

  noiseStrip.onLearnRequested = [this](const juce::String &id) {
    showLearnMenu(id);
  };

  // Housekeeping runs at 4 Hz, and a readout that is blank for the first
  // quarter second of the window being open reads as broken.
  topBar.updatePanelReadouts(plugin().getSampleRate());

  // Read before the heights below, both of which depend on how much of the
  // strip is folded away.
  collapsedSections =
      (SectionMask)(int)state.getProperty(kCollapsedSections, 0) &
      ((1u << kNumSections) - 1u);
  publishCollapsedSections();

  // Restored but not trusted: resized() clamps it against a range it can only
  // know once the window has a size, and the window this opens into may be
  // shorter than the one it was saved from.
  scrollY = juce::jmax(0, (int)state.getProperty(kScroll, 0));

  // Added rather than made visible: resized() shows it only when there is
  // something to scroll.
  content.addChildComponent(parameterBar);
  parameterBar.addListener(this);
  parameterBar.setAutoHide(false);

  gutter.onSectionToggled = [this](Section s) { toggleSection(s); };

  // A cap is a switch, so a click flips the parameter, one gesture so the host
  // records it as one step. The cap lights from the parameter on the next
  // tick rather than from the click, which is what keeps it honest when a
  // preset or the glide knob's menu moves the same switch.
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

  // Off the series, like the noise channel, so pointing at a caption cannot
  // arm a LINK preview on whichever channel happened to be hovered last.
  gutter.onHoverChanged = [this](Row row) { hoverChanged(-1, row); };
  noiseStrip.onSectionToggled = [this](Section s) { toggleSection(s); };

  const auto standard = standardSize();

  const int savedWidth =
      (int)state.getProperty(kEditorWidth, standard.getWidth());
  const int savedHeight =
      (int)state.getProperty(kEditorHeight, standard.getHeight());

  setResizable(true, true);
  applyResizeLimits(savedWidth);

  // Cast rather than left to the compiler. int times float is a conversion
  // that can lose precision in principle, and newer clang says so.
  setSize(juce::roundToInt((float)savedWidth * zoom),
          juce::roundToInt((float)savedHeight * zoom));

  // Before the first paint rather than on the first housekeeping tick, so an
  // editor opened on a patch that shares a modulator says so straight away
  // instead of a quarter of a second later.
  syncSharedModulators();

  startTimerHz(30);

  // What was already loaded before this window existed, which is the usual
  // case: a window is opened and closed far more often than a preset is
  // chosen, and a session restored from disk has no window at all until
  // someone asks for one. Empty is a new instance, which shows the placeholder.
  topBar.setPresetName(plugin().presetName());

  // Last, so a prompt cannot appear over a half-built window.
  offerUpdateCheck();
}

void OvertoniumEditor::parentHierarchyChanged() {
  // Not now. This runs part way through the window taking this editor as its
  // content, and restyling a window sends a look and feel change through it,
  // which lays its content out again at whatever size the window is at that
  // moment. That is not yet the size it is about to become: a document window
  // is 128 px square until it is given the content's size, so the editor was
  // squashed into that and clamped back up to its own minimum, and the
  // standalone opened about a third as wide as it should have.
  //
  // Off the stack instead, by which time the window is the size it means to
  // be. Safe against the editor being closed in between.
  juce::MessageManager::callAsync([safe = SafePointer<OvertoniumEditor>(this)] {
    if (safe != nullptr)
      safe->dressStandaloneWindow();
  });
}

void OvertoniumEditor::dressStandaloneWindow() {
  if (plugin().wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    return;

  // Not in the constructor, because the editor is not in the window yet when
  // it runs: the standalone builds the editor first and makes it the window's
  // content afterwards. This is called again every time that changes.
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

  // By name, because it is not ours to hold a pointer to. Finding nothing is
  // a perfectly good outcome: it means this window has no such button, which
  // is every window but the standalone's own.
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
  // Nothing to do twice. Setting it again would send a look and feel change
  // through the whole window, and this runs on every change of hierarchy.
  if (&window.getLookAndFeel() == &lookAndFeel)
    return;

  // Setting a look and feel on a window recreates its title bar buttons, so
  // the crosses and dashes come back drawn by ours.
  //
  // Handing out a look and feel this editor owns is safe even though the
  // window outlives it: a component holds its look and feel by weak
  // reference, so when this one goes the window falls back to the
  // application's own rather than reading freed memory. A test pins that,
  // since it is the kind of thing that is only ever noticed by crashing.
  window.setLookAndFeel(&lookAndFeel);
  window.setBackgroundColour(colours::background);

  // And whatever the window puts in its own title bar is put where it belongs
  // now and after every layout it does.
  //
  // One window at a time. There is only ever one in practice, but leaving a
  // listener on a window this editor has stopped tracking is the kind of
  // thing that is fine until it is not.
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

  // Asked to stop, never waited for. Nothing is left to show the answer to, so
  // the work is pointless from here, but waiting for it on the message thread
  // is the thing that must not happen.
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

  // No grain and no vignette here. The strips are opaque and cover all but
  // about one and a half percent of the window, so anything painted on the
  // backdrop is painted where nothing can see it, and it measured at 75ms of a
  // full repaint for that privilege. Making a vignette visible would mean
  // paintOverChildren, which JUCE calls for every invalidated region including
  // the meter bands, and those are the one thing that must stay cheap.
}

void OvertoniumEditor::resized() {
  const int logicalWidth = juce::roundToInt((float)getWidth() / zoom);
  const int logicalHeight = juce::roundToInt((float)getHeight() / zoom);

  // The shortest legal window changes when the bar reflows, so a drag across
  // one of those widths has to be followed. Only on a change, which is what
  // keeps this from recursing: applying limits constrains the bounds and so
  // comes back here, and the second pass finds the same bar height and stops.
  if (ovt::ui::TopBar::heightForWidth(
          juce::jmax(minimumLogicalWidth(), logicalWidth)) != limitsBarHeight)
    applyResizeLimits(logicalWidth);

  // Over the whole window rather than over the content, and untransformed:
  // the dim and the card are chrome for a thing you are doing instead of
  // playing, so the zoom that sizes the instrument has nothing to say about
  // how big a menu of macros should be.
  macroPanel.setBounds(getLocalBounds());

  content.setTransform(juce::AffineTransform::scale(zoom));
  content.setBounds(0, 0, logicalWidth, logicalHeight);

  auto area = content.getLocalBounds();
  topBar.setBounds(
      area.removeFromTop(ovt::ui::TopBar::heightForWidth(logicalWidth)));

  // No inset here. The mixer runs to the window edges and the breathing room
  // it used to get from this border is now kStripPadY inside each column, so
  // the space is above and below the controls rather than around the block.
  const auto gutterArea = area.removeFromLeft(kGutterWidth);

  // Beyond everything, where a scrollbar goes. Taken off the width whether it
  // is shown or not, because a bar that appeared and disappeared would move
  // all 32 channels sideways by ten pixels as the window crossed the height
  // where the rows stop fitting.
  const auto parameterBarArea = area.removeFromRight(kScrollBarThickness);

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

  // Every column is handed the same scroll, which is what keeps a caption in
  // the gutter beside the knob it names. Clamped here rather than where it is
  // set, because the range depends on the height and on what is folded away,
  // and both move under it: a window dragged taller or a section unfolded can
  // leave it scrolled past the end.
  const auto bands =
      layoutBands(juce::Rectangle<int>(0, 0, kStripWidth, stripHeight)
                      .reduced(kStripPadX, kStripPadY),
                  collapsedSections);

  stripLayoutArea = juce::Rectangle<int>(0, 0, kStripWidth, stripHeight)
                        .reduced(kStripPadX, kStripPadY);
  scrollRange = bands.maxScroll;
  scrollBandHeight = bands.middle.getHeight();

  scrollY = juce::jlimit(0, scrollRange, scrollY);

  // The full height of the mixer, not just the band it scrolls. Lining it up
  // with the band left a gap above it where the pinned header is, which reads
  // as a scrollbar that will not reach the top of its own area.
  parameterBar.setVisible(scrollRange > 0);
  parameterBar.setBounds(parameterBarArea);

  parameterBar.setRangeLimits(0.0, (double)bands.contentHeight,
                              juce::dontSendNotification);
  syncScrollBar();

  gutter.setScroll(scrollY);
  noiseStrip.setScroll(scrollY);

  for (auto &strip : strips)
    strip->setScroll(scrollY);

  // Only write when something actually moved: a live resize drag fires this
  // constantly, and every property set notifies the APVTS listener on the state
  // tree.
  //
  // Held across the comparisons as well as the writes: reading a property off
  // a tree the host is replacing is the same race as writing one. See
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
  // Wide enough for all 32 strips at once, which is the whole point of the
  // layout, and tall enough for whatever is not folded away.
  const int width = kGutterWidth + kStripWidth + kMasterGap +
                    kNumHarmonics * kStripWidth + kScrollBarThickness;

  return {width, chromeHeight(width) + preferredStripHeight(collapsedSections)};
}

void OvertoniumEditor::fitAllChannels() {
  const auto standard = standardSize();

  // The limits first, and for the width this is about to be rather than the
  // one it is at. setSize does not consult them, so without this the window
  // lands at whatever the standard size says while the floor still belongs to
  // the width being left, and the next drag snaps it to that floor.
  applyResizeLimits(standard.getWidth());

  setSize(juce::roundToInt((float)standard.getWidth() * zoom),
          juce::roundToInt((float)standard.getHeight() * zoom));
}

void OvertoniumEditor::applyResizeLimits() {
  applyResizeLimits(juce::roundToInt((float)getWidth() / zoom));
}

void OvertoniumEditor::applyResizeLimits(int forLogicalWidth) {
  // Limits are expressed in logical pixels, so they scale with the zoom factor.
  // Wide enough for a usable stretch of mixer, and never narrower than the top
  // bar can lay itself out without dropping a group.
  const int minWidth = minimumLogicalWidth();

  // The floor follows the width, and the reason is the bar. It takes three
  // rows at the narrowest window, two from 648, and one from 1258, which is
  // 182, 124 and 66 pixels of chrome, so the same mixer needs a window 116
  // pixels taller at one end of the range than at the other.
  //
  // It used to be worked out at minWidth and used at every width, which made
  // the floor the narrow window's floor even on a wide one. The wide window
  // was then held 116 pixels taller than it needed to be, and Fit all 32
  // channels, which calls setSize and so consults no limits at all, went
  // straight past it: it landed at 913 against a floor of 997 and the first
  // drag afterwards snapped the window up by 84 pixels.
  //
  // Never below minWidth, so a window narrower than the floor allows, which
  // is what an unset size during construction looks like, still gets the
  // conservative answer rather than one for a width nothing can have.
  const int width = juce::jmax(minWidth, forLogicalWidth);
  const int bar = ovt::ui::TopBar::heightForWidth(width);
  const int minHeight =
      chromeHeight(width) + minimumStripHeight(collapsedSections);

  limitsBarHeight = bar;

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

  // The bar keeps its own copy, which is what the tick in the Zoom submenu is
  // drawn from. Without this the menu goes on saying 100% whatever the window
  // is actually at, since the only other place that sets it is the restore.
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

  // Through the same door the wheel uses, so there is one place that decides
  // what scrolling means and one place that tells the columns about it.
  //
  // Guarded, because that door ends by putting the bar where the rows ended
  // up, and the rows snap to whole rows where a drag does not. Writing back
  // mid-drag moved the bar out from under the pointer, which then chased it.
  const juce::ScopedValueSetter<bool> dragging(barIsDriving, true);

  scrollParameters(juce::roundToInt(newStart) - scrollY);
}

bool OvertoniumEditor::scrollParameters(int delta) {
  if (scrollRange <= 0)
    return false;

  // Straight to the pixel asked for. It used to snap to the top of a row so
  // that nothing could sit half over the pinned header, and the column caps
  // that now rather than the arithmetic.
  const int wanted = juce::jlimit(0, scrollRange, scrollY + delta);

  // Taken even when it changes nothing, because at either end there is still
  // somewhere to scroll and letting the wheel fall through would have the
  // mixer lurch sideways the moment the parameters hit the top.
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
  // The wheel moves the rows without going through resized(), so the bar is
  // told separately or it sits where the last drag left it. Silently, because
  // the bar telling us back is how a drag arrives and would be a loop.
  parameterBar.setCurrentRange(
      juce::Range<double>((double)scrollY,
                          (double)(scrollY + scrollBandHeight)),
      juce::dontSendNotification);
}

void OvertoniumEditor::mouseWheelMove(const juce::MouseEvent &e,
                                      const juce::MouseWheelDetails &wheel) {
  // Sideways is handed to the mixer's viewport outright rather than left to
  // bubble. The gutter and the noise channel are siblings of that viewport
  // rather than children of it, so an event let go from here goes up to the
  // editor and stops, and a swipe over either of them did nothing.
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

  // The window does not move any more.
  //
  // It used to shrink by exactly the rows being folded away, because every row
  // had to be on screen and folding was the only way to get the mixer's height
  // down. Now that the rows scroll, folding is about seeing more of them at
  // once rather than about fitting, so the window stays where it is and the
  // band simply has less to hold.
  //
  // That is also the end of a whole class of fault. The arithmetic that moved
  // the window had to read the height before applying the limits, because
  // applying them is itself a resize, and getting that order wrong was what
  // made unfolding swell the faders until they ran under the dock. There is no
  // order to get wrong now.
  resized();
}

void OvertoniumEditor::maybeCheckForUpdates() {
  if (!ovt::updateCheckAllowed())
    return;

  // A SafePointer rather than this, because the fetch outlives the window. The
  // answer arrives on the message thread, which is also where an editor is
  // destroyed, so by the time this runs the pointer is either good or null and
  // cannot be anything in between.
  plugin().updates().setListener(
      [safe = juce::Component::SafePointer<OvertoniumEditor>(this)] {
        if (auto *editor = safe.getComponent())
          if (const auto release = editor->plugin().updates().newerRelease())
            editor->topBar.setUpdateAvailable(release->version, release->url);
      });

  plugin().updates().start(OVERTONIUM_VERSION);
}

void OvertoniumEditor::offerUpdateCheck() {
  if (ovt::updateCheckAllowed()) {
    maybeCheckForUpdates();
    return;
  }

  // Offered once, in the credit line, and never again. No dialog: a plugin
  // cannot tell a person opening it from a host walking the plugin folder at
  // startup, and a modal in the second case stops the scan dead. pluginval
  // does exactly that, several editors in a row.
  //
  // The mark is written before the offer is shown rather than after it is
  // answered, because an offer nobody takes is still an offer made, and asking
  // again every time the window opens is nagging.
  if (ovt::updateCheckOffered())
    return;

  ovt::markUpdateCheckOffered();
  topBar.setUpdateOffer(true);
}

void OvertoniumEditor::applyPreset(int index) {
  // Through the processor rather than straight to presets::apply, so the host
  // keeps up: it is the processor that knows which program is current, and a
  // host showing "Big Saw" while the plugin shows "Wurli" is worse than a host
  // showing nothing.
  //
  // Recorded, because somebody picked it from a menu. The same call made by a
  // clip firing a program change is not, which is the whole reason the caller
  // is the one that decides.
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
  // reach rather than on the ones it turns out to. A parameter that never
  // moves contributes nothing to the step, and opening them as the pointer
  // arrives would leave the host holding a gesture per column.
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

  // Every column between the last point and this one, not just the one under
  // the pointer. A hand moving quickly crosses several strips between two
  // mouse events, and drawing only where the events landed leaves the shape
  // full of holes exactly where the drawing was fastest.
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

/// Every fader a drawn line can reach, the noise channel included.
///
/// It is not a harmonic, but it is a fader, and a drag that crossed it and
/// left it alone would be stranger than one that did not.
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
  // Asked for rather than waited for, and that is the whole of why this is a
  // poll. JUCE sends a modifier change to the component under the pointer, and
  // Slider handles it without passing it up, so over a fader or a knob the
  // editor never hears about it. The result was a tool that could only be
  // armed with the pointer in one of the gaps between channels, and that
  // latched on for good if the key was released anywhere else.
  //
  // Nothing can swallow this.
  const auto held = juce::ModifierKeys::getCurrentModifiers().isShiftDown();

  if (held == shiftHeld)
    return;

  shiftHeld = held;
  refreshDrawArmed();
}

void OvertoniumEditor::refreshDrawArmed() {
  // Held or latched: the modifier is the quick way and the button is the way
  // that stays. Either arms it and the button lights for both, so the panel
  // says what a drag would do however it came to be that way.
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
  // The switch is in the gutter and the settings it belongs to are on the bar,
  // so the button is told rather than asked.
  //
  // One button showing one tool, which is what it always was: a drag cannot
  // be a link and a drawing at once. The two switches this replaced had to
  // work around that by making LINK read as off while drawing was armed,
  // lighting a switch for a gesture that had been taken away from it.
  //
  // Drawing wins while it is armed, which includes being armed by holding
  // the modifier rather than by choosing it, so the button shows the pencil
  // for as long as the key is down and gives the tool back on release.
  gutter.setTool(currentTool(), topBar.getLinkCurve());

  // Switching LINK on, or changing what it reaches, changes the answer to
  // "what would this knob take with it", so the preview follows immediately
  // rather than waiting for the pointer to move.
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
  // Latching drawing off is not the same as choosing another tool: the
  // modifier can still arm it, and LINK's own switch keeps whatever it had
  // so that going back to it finds the scope and curve you left.
  drawLatched = which == ovt::ui::PointerTool::Draw;
  topBar.setLinkEnabled(which == ovt::ui::PointerTool::Link);

  // Written down, or the next window opens on whatever was last written by
  // something else. LINK's settings callback writes its switch when a scope
  // or a curve is chosen, so picking LINK once and then picking another tool
  // left a session that said LINK and a window that did not.
  rememberTool();

  // refreshDrawArmed rather than pollDrawModifier: the poll only acts when
  // the modifier itself has changed, and nothing here touched the keyboard.
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

  // The eight read once rather than per control. There was a signature here
  // that skipped the whole pass when no macro had moved, which was wrong in
  // the way that matters: the result is the patch's value plus the macro's
  // offset, so turning a knob moves it while every macro stands still, and
  // the ring sat where it had been left until a macro was touched.
  //
  // What makes skipping unnecessary is that the strips compare before they
  // repaint, so a pass that finds nothing changed costs arithmetic and no
  // frames.
  struct Reach {
    int row = 0;
    int scope = 0;
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

  // What the bar says about macros, which is the only sign of them while the
  // panel is shut. Counted here because here is where the eight are already
  // being read, and on this timer because that is what follows the macros
  // themselves: it sat in syncLinkUi, which runs when the tool changes, so
  // making a macro lit the button only once you touched the tool menu.
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

      // The lowest-numbered macro reaching this control takes it, and the
      // rest are invisible here. Two colours mixed would usually name a
      // third macro, and a control saying "more than one" says nothing about
      // which.
      for (const auto &reach : macros) {
        // None, or a different row from this one. Row 1 is the first real
        // one, so a role is row + 1.
        if (reach.row == 0 || reach.row - 1 != r)
          continue;

        if (!ovt::params::macroReaches(reach.scope, i))
          continue;

        // A macro wearing None drives the control without colouring it,
        // which is for anyone who would rather the mixer stayed the colour
        // the series makes it.
        wearing = reach.colour;

        // Where the engine will actually put it, by the arithmetic the
        // snapshot uses: a proportion of the control's own travel, shared out
        // by the curve and clamped to the ends.
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

  // Off the old one first, since arming a second control while the first is
  // waiting is a thing somebody can do by right-clicking twice.
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

  // appendItems opens with a separator, which is right where these join the
  // LINK menu and wrong at the top of a menu of their own.
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

void OvertoniumEditor::showLinkMenu(const juce::String &parameterId) {
  topBar.showLinkMenu(nullptr, parameterId, &plugin().midiLearn, currentTool());
}

void OvertoniumEditor::updateLinkCursor() {
  // Set on the holder rather than on each control: the strips and their knobs
  // all take the parent's pointer, so this one assignment reaches every one of
  // them. The noise channel is outside the holder, which is right, since LINK
  // never reaches it either.
  // A drag while drawing is armed draws rather than links, so the pointer says
  // so: a pencil over the mixer, and never the LINK cursor, which would be
  // promising a gesture that is not what would happen.
  stripsHolder.setMouseCursor(drawArmed ? drawCursor()
                              : topBar.isLinkEnabled()
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

  const juce::ScopedValueSetter<bool> guard(propagatingLink, true);

  // The faders are shared out in decibels rather than across their travel,
  // because their travel is shaped to feel right under a finger rather than
  // to be even: moving every fader the same distance moves a quiet channel
  // many times further in level than the one in your hand. See
  // linkIsDecibels, which is also why this is the only row that needs a space
  // of its own.
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

  // The dragged strip's live value. Gathering collapses everything onto it, so
  // the knob in your hand is the target rather than a stranded outlier.
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
  // Armed, every fader lights, and nothing else does. It answers the same
  // question LINK's preview answers, which is what the next drag would reach,
  // so it is the same mechanism rather than a second kind of highlight: two
  // ways of saying that would be two things to keep in step.
  if (drawArmed) {
    for (auto &strip : strips)
      strip->setLinkGlow(Role::Volume, 1.0f, true);

    // The numbers light too, and all of them, because drawing reaches every
    // channel. One rule with no exceptions: a lit number means this channel
    // is in whatever gesture is armed. Always on for this tool carries no
    // information by itself, which is the price of the rule holding.
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

    // The number at the head of the channel takes the same reading, which is
    // what makes a scope legible from across the mixer rather than only from
    // over the knob. On every scope including All: the question is which
    // channels a drag would reach, and "all of them" is an answer that should
    // look like all of them. Lit for some scopes and dark for others would
    // leave an unlit mixer meaning either that nothing is armed or that
    // everything is reached, which is the one thing an indicator must not do.
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
  // Read back rather than written when it is set, for the same reason the
  // preset name is: the switch is in a menu and a preset can throw it without
  // anyone touching that menu. Through the cached atomics rather than the
  // parameter map, which wants a string per lookup, and the gutter drops the
  // call when nothing moved, so an ordinary tick costs two atomic loads.
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
    add(strip, strip.setMeterLevel(plugin().getPartialLevel(i)));

    // Kept apart from the meter bands, because the two want different
    // merges. See mergeIntoRows.
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

  // The rest is housekeeping that nobody can see at 30 Hz, so it runs at a
  // fraction of the rate.
  if ((tick % 8) != 0)
    return;

  // Read through the cached atomics rather than the parameter map: the map
  // wants a string per lookup, and this runs several times a second.
  const auto &cache = plugin().parameters();

  const auto on = [](const std::atomic<float> *p) {
    return p != nullptr && p->load() > 0.5f;
  };

  topBar.updatePanelReadouts(plugin().getSampleRate());

  syncSharedModulators();

  // A preset can now be loaded by something other than this menu: a program
  // change arriving over MIDI. Nothing tells the window when that happens, so
  // the button is compared against what is loaded rather than written to, and
  // set only when the two have drifted apart, since setting it repaints.
  if (const auto loaded = plugin().presetName();
      loaded != topBar.getPresetName())
    topBar.setPresetName(loaded);

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
