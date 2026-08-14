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
constexpr int kGroupMinWidth[] = {164, 202, 262, 338, 82};
constexpr int kOutputGroupIndex = 3;
constexpr int kGroupCount = 5;

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

LabelledKnob::LabelledKnob(juce::String captionText, juce::Colour fill)
    : caption(std::move(captionText)) {
  slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
  addAndMakeVisible(slider);
}

void LabelledKnob::paint(juce::Graphics &g) {
  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));
  g.drawText(caption, getLocalBounds().removeFromBottom(11),
             juce::Justification::centred, false);
}

void LabelledKnob::resized() {
  // A little vertical inset, or the tick ring sits right on the group border.
  slider.setBounds(getLocalBounds().withTrimmedBottom(12).reduced(0, 2));
}

// =============================================================================

TopBar::TopBar(juce::AudioProcessorValueTreeState &state,
               juce::Component &popupParent)
    : apvts(state) {
  LabelledKnob *knobs[] = {&master, &spread, &bend};

  for (auto *k : knobs) {
    k->slider.setPopupDisplayEnabled(true, true, &popupParent);
    addAndMakeVisible(*k);
  }

  master.slider.setTooltip("Output level");
  spread.slider.setTooltip("Fans the partials across the stereo field");
  bend.slider.setTooltip("Pitch bend range in semitones");

  masterAttachment = std::make_unique<SliderAttachment>(
      apvts, params::masterGainId, master.slider);
  spreadAttachment = std::make_unique<SliderAttachment>(apvts, params::spreadId,
                                                        spread.slider);
  bendAttachment = std::make_unique<SliderAttachment>(
      apvts, params::bendRangeId, bend.slider);

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

  // ---- polyphony ------------------------------------------------------------
  for (size_t i = 0; i < params::kPolyphonyChoices.size(); ++i)
    polyBox.addItem(juce::String(params::kPolyphonyChoices[i]), (int)i + 1);

  polyAttachment =
      std::make_unique<ComboBoxAttachment>(apvts, params::polyphonyId, polyBox);
  polyBox.setTooltip(
      "Maximum simultaneous notes. Each note runs 32 sine partials.");
  addAndMakeVisible(polyBox);

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
  for (int i = 0; i < (int)LinkScope::NumScopes; ++i)
    scopeBox.addItem(linkScopeName((LinkScope)i), i + 1);

  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    curveBox.addItem(linkCurveName((LinkCurve)i), i + 1);

  scopeBox.setSelectedId(1, juce::dontSendNotification);
  curveBox.setSelectedId(1, juce::dontSendNotification);

  scopeBox.setTooltip("Which channels a LINK drag reaches. Same interval "
                      "follows only the strips sharing the one you grab, so "
                      "you can move just the fifths or just the octaves.");
  curveBox.setTooltip(
      "How the drag is shared out. Tilt up moves the higher "
      "partials more, tilt down the lower ones. Spread scatters "
      "them as you push up and gathers them as you pull down.");

  for (auto *box : {&scopeBox, &curveBox}) {
    box->onChange = [this] {
      if (onLinkSettingsChanged)
        onLinkSettingsChanged();
    };
    addAndMakeVisible(*box);
  }

  updateLinkEnablement();

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
      {&polyCaption, "POLY", juce::Justification::centredLeft},
      {&zoomCaption, "ZOOM", juce::Justification::centredLeft},
      {&scopeCaption, "LINK SCOPE", juce::Justification::centredLeft},
      {&curveCaption, "LINK CURVE", juce::Justification::centredLeft},
  };

  for (auto &c : captions) {
    c.label->setText(c.text, juce::dontSendNotification);
    c.label->setFont(makeFont(9.0f, true));
    c.label->setColour(juce::Label::textColourId, colours::textDim);
    c.label->setJustificationType(c.just);
    c.label->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*c.label);
  }

  voicesLabel.setFont(makeFont(10.0f));
  voicesLabel.setColour(juce::Label::textColourId, colours::textDim);
  voicesLabel.setJustificationType(juce::Justification::centredRight);
  voicesLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(voicesLabel);

  setVoiceCount(0, 8);
}

void TopBar::styleToggle(juce::TextButton &b, const juce::String &text,
                         const juce::String &tooltip) {
  b.setButtonText(text);
  b.setClickingTogglesState(true);
  b.setTooltip(tooltip);
  addAndMakeVisible(b);
}

LinkScope TopBar::getLinkScope() const {
  return (LinkScope)juce::jlimit(0, (int)LinkScope::NumScopes - 1,
                                 scopeBox.getSelectedId() - 1);
}

LinkCurve TopBar::getLinkCurve() const {
  return (LinkCurve)juce::jlimit(0, (int)LinkCurve::NumCurves - 1,
                                 curveBox.getSelectedId() - 1);
}

void TopBar::setLinkScope(LinkScope s) {
  scopeBox.setSelectedId((int)s + 1, juce::dontSendNotification);
}

void TopBar::setLinkCurve(LinkCurve c) {
  curveBox.setSelectedId((int)c + 1, juce::dontSendNotification);
}

void TopBar::updateLinkEnablement() {
  const auto on = linkButton.getToggleState();

  scopeBox.setEnabled(on);
  curveBox.setEnabled(on);
  scopeCaption.setAlpha(on ? 1.0f : 0.45f);
  curveCaption.setAlpha(on ? 1.0f : 0.45f);
}

void TopBar::setVoiceCount(int active, int limit) {
  voicesLabel.setText(juce::String(active) + " / " + juce::String(limit) +
                          " voices",
                      juce::dontSendNotification);
}

void TopBar::setZoomChoice(float zoom) {
  for (int i = 0; i < (int)std::size(kZoomChoices); ++i) {
    if (std::abs(kZoomChoices[i] - zoom) < 0.01f) {
      zoomBox.setSelectedId(i + 1, juce::dontSendNotification);
      return;
    }
  }
}

int TopBar::heightForWidth(int width) {
  const auto content = width - 2 * kBarMargin - kTitleWidth - kTitleLead;
  const auto onOneRow =
      content >= totalGroupWidth(0, kGroupCount) - kOutputMaxSqueeze;

  const auto rows = onOneRow ? 1 : 2;

  return rows * kRowHeight + (rows - 1) * kRowGap + 2 * kBarPadY;
}

int TopBar::chooseSplit(int firstRowWidth, int secondRowWidth) {
  if (firstRowWidth >= totalGroupWidth(0, kGroupCount) - kOutputMaxSqueeze)
    return kGroupCount;

  // Fill the first row as far as it will go, which keeps the bar compact and
  // anchored to the title. The exception is a second row that would come out
  // nearly empty: one small group stranded on a row of its own looks like a
  // mistake, so in that case a group is pulled down to join it.
  int fallback = 0;

  for (int split = kGroupCount - 1; split >= 1; --split) {
    const auto first = totalGroupWidth(0, split);
    const auto second = totalGroupWidth(split, kGroupCount);

    if (first > firstRowWidth || second > secondRowWidth)
      continue;

    if (fallback == 0)
      fallback = split;

    if (second * 100 >= secondRowWidth * 30)
      return split;
  }

  return fallback > 0 ? fallback : 1;
}

int TopBar::minimumWidth() {
  // The narrowest window at which some split fits across two rows. Only the
  // first row loses width to the title, which is why the two rows are measured
  // against different budgets. Greedy filling from the left reaches the same
  // split, so this is a real floor rather than an optimistic one.
  int best = std::numeric_limits<int>::max();

  for (int split = 1; split < kGroupCount; ++split) {
    const auto first = totalGroupWidth(0, split) + kTitleWidth + kTitleLead;
    const auto second = totalGroupWidth(split, kGroupCount);

    best = juce::jmin(best, juce::jmax(first, second) + 2 * kBarMargin);
  }

  return best;
}

void TopBar::parkControls() {
  juce::Component *all[] = {
      &master,      &spread,        &bend,         &meter,       &meterCaption,
      &presetBox,   &presetCaption, &polyBox,      &polyCaption, &zoomBox,
      &zoomCaption, &scopeBox,      &scopeCaption, &curveBox,    &curveCaption,
      &linkButton,  &phaseButton,   &clipButton,   &voicesLabel};

  for (auto *c : all)
    c->setBounds({});

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

  switch (group) {
  case PresetGroup:
    captioned(presetBox, presetCaption, r);
    break;

  case VoiceGroup:
    captioned(polyBox, polyCaption, r.removeFromLeft(56));
    r.removeFromLeft(6);
    bend.setBounds(r.removeFromLeft(52));
    r.removeFromLeft(6);
    voicesLabel.setBounds(r);
    break;

  case LinkGroup:
    button(linkButton, r.removeFromLeft(48));
    r.removeFromLeft(6);
    captioned(scopeBox, scopeCaption, r.removeFromLeft(94));
    r.removeFromLeft(6);
    captioned(curveBox, curveCaption, r.removeFromLeft(94));
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

  for (int g = firstGroup; g < lastGroup; ++g) {
    if (g > firstGroup)
      row.removeFromLeft(kGroupGap);

    auto width = kGroupMinWidth[g];
    if (g == kOutputGroupIndex)
      width += outputAdjust;

    // Never hand out more than is left, so a very narrow window crops the last
    // group rather than letting groups overlap each other.
    width = juce::jmin(width, row.getWidth());
    if (width < 40)
      break;

    placeGroup(g, row.removeFromLeft(width));
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

void TopBar::resized() {
  parkControls();

  auto area = getLocalBounds().reduced(kBarMargin, kBarPadY);
  const auto fullWidth = area;

  // Only the first row has to make room for the title.
  auto firstRow = area.removeFromTop(kRowHeight);
  firstRow.removeFromLeft(kTitleWidth + kTitleLead);

  if (firstRow.getWidth() < 60)
    return;

  const auto wrapAt =
      fullWidth.getHeight() >= 2 * kRowHeight
          ? chooseSplit(firstRow.getWidth(), fullWidth.getWidth())
          : kGroupCount;

  layoutRow(firstRow, 0, wrapAt);

  if (wrapAt < kGroupCount) {
    area.removeFromTop(kRowGap);

    // The second row starts at the margin, since the title is above it rather
    // than beside it.
    layoutRow(area.removeFromTop(kRowHeight)
                  .withX(fullWidth.getX())
                  .withWidth(fullWidth.getWidth()),
              wrapAt, kGroupCount);
  }
}

} // namespace ovt::ui
