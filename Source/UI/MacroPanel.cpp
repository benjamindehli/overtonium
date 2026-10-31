#include "MacroPanel.h"

#include "LearnMenu.h"
#include "LookAndFeel.h"

namespace ovt::ui {

namespace {

/// The card's share of the window, and what it will not go past.
constexpr int kPadding = 18;
constexpr int kRowHeight = 30;
constexpr int kRowGap = 6;
constexpr int kHeaderHeight = 34;

/// The strip of column names between the title and the first macro.
constexpr int kColumnsHeight = 16;

/// Between the last macro and the two buttons, and under them.
constexpr int kButtonsGap = 12;
constexpr int kButtonHeight = 26;
constexpr int kBottomPad = 18;
constexpr int kMaxCardWidth = 760;

/// What each control on a strip is given, left to right. The amount takes
/// whatever is left.
constexpr int kColourWidth = 30;
constexpr int kRowWidth = 124;
constexpr int kScopeWidth = 104;
constexpr int kCurveWidth = 74;
constexpr int kAnchorWidth = 40;
/// Wide enough for a sign, three digits and a point without the cells
/// touching. See SegmentDisplay::hasRoomForUnit.
constexpr int kReadingWidth = 92;
/// Square, like the colour swatch at the other end of the row.
constexpr int kRemoveWidth = 30;
constexpr int kGap = 6;

} // namespace

MacroPanel::MacroPanel(juce::AudioProcessorValueTreeState &state)
    : apvts(state) {
  // Takes clicks on the dim too, so clicking away dismisses rather than
  // reaching the mixer.
  setInterceptsMouseClicks(true, true);

  for (int m = 0; m < params::kNumMacros; ++m)
    buildStrip(m);

  addButton.setButtonText("Add a macro");
  addButton.setTooltip("Assigns one of the eight macros to a row.");
  addButton.onClick = [this] {
    const auto free = firstFree();

    if (free < 0)
      return;

    make(free);
  };

  addAndMakeVisible(addButton);

  closeButton.setButtonText("Done");
  closeButton.onClick = [this] {
    if (onDismiss != nullptr)
      onDismiss();
  };

  addAndMakeVisible(closeButton);
}

void MacroPanel::buildStrip(int macro) {
  auto &strip = strips[(size_t)macro];

  // Distinct names for a screen reader, which the tests check.
  const auto which = "Macro " + juce::String(macro + 1) + " ";

  strip.amount.setTitle(which + "amount");
  strip.colour.setTitle(which + "colour");
  strip.row.setTitle(which + "row");
  strip.scope.setTitle(which + "scope");
  strip.curve.setTitle(which + "curve");
  strip.anchor.setTitle(which + "taper anchor");
  strip.remove.setTitle("Remove macro " + juce::String(macro + 1));

  // A swatch rather than a word.
  strip.colour.setTooltip("The colour this macro gives the controls it "
                          "moves.");
  strip.colour.onClick = [this, macro] {
    choose(
        macro, params::macroColourId(macro), "Colour", params::kNumMacroColours,
        [](int i) { return juce::String(params::macroColourName(i)); },
        &strips[(size_t)macro].colour);
  };

  strip.row.setTooltip("Which row of the mixer this macro moves.");
  strip.row.onClick = [this, macro] {
    // One longer than the rows, for None at the end, which removes the macro.
    choose(
        macro, params::macroRowId(macro), "Row", params::kNumMacroRows + 1,
        [](int i) { return juce::String(params::macroRowName(i)); },
        &strips[(size_t)macro].row);
  };

  strip.scope.onClick = [this, macro] { chooseScope(macro); };

  strip.curve.setTooltip("How the amount is spread. Uniform moves every "
                         "channel the same, and Taper "
                         "is strongest at one channel and fades away from it.");
  strip.curve.onClick = [this, macro] {
    choose(
        macro, params::macroCurveId(macro), "Curve",
        (int)params::MacroCurve::NumCurves,
        [](int i) {
          return juce::String(params::macroCurveName((params::MacroCurve)i));
        },
        &strips[(size_t)macro].curve);
  };

  strip.anchor.setTooltip(
      "The channel the taper is strongest at. Shown when the curve is Taper.");
  strip.anchor.onClick = [this, macro] {
    choose(
        macro, params::macroAnchorId(macro), "Anchor", kNumHarmonics,
        [](int i) { return juce::String(i + 1); },
        &strips[(size_t)macro].anchor, 1);
  };

  // A readout rather than a control, so it ignores the pointer.
  strip.reading.setInterceptsMouseClicks(false, false);

  strip.amount.onValueChange = [this, macro] { showReading(macro); };

  // Only the amount can be learned.
  learn::tag(strip.amount, params::macroAmountId(macro));

  strip.amount.onPopup = [this, macro] {
    if (onLearnRequested != nullptr)
      onLearnRequested(params::macroAmountId(macro));
  };

  strip.remove.setButtonText("x");
  strip.remove.setTooltip(
      "Removes this macro. The controls it moved go back to the patch values.");
  strip.remove.onClick = [this, macro] {
    write(params::macroRowId(macro), 0);
    refresh();
  };

  // Drawn as a channel fader laid on its side.
  strip.amount.getProperties().set("segmentedTrack", true);
  strip.amount.getProperties().set("bipolar", true);
  strip.amount.setTooltip(
      "How far it moves the row. This is the parameter a host automates.");
  strip.amount.setDoubleClickReturnValue(true, 0.0);

  strip.attachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          apvts, params::macroAmountId(macro), strip.amount);

  const std::array<juce::Component *, 8> parts{
      &strip.colour, &strip.row,    &strip.scope,  &strip.curve,
      &strip.anchor, &strip.remove, &strip.amount, &strip.reading};

  for (auto *c : parts)
    addChildComponent(c);
}

int MacroPanel::chosen(const juce::String &parameterId) const {
  if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
          apvts.getParameter(parameterId)))
    return (int)std::lround(p->convertFrom0to1(p->getValue()));

  return 0;
}

void MacroPanel::write(const juce::String &parameterId, int value) {
  if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
          apvts.getParameter(parameterId)))
    p->setValueNotifyingHost(p->convertTo0to1((float)value));
}

void MacroPanel::make(int macro) {
  if (macro < 0 || macro >= params::kNumMacros)
    return;

  // Pointed at the first real row, TUNE.
  if (chosen(params::macroRowId(macro)) == 0)
    write(params::macroRowId(macro), 1);

  refresh();
}

int MacroPanel::firstFree() const {
  for (int m = 0; m < params::kNumMacros; ++m)
    if (chosen(params::macroRowId(m)) == 0)
      return m;

  return -1;
}

int MacroPanel::madeCount(const juce::AudioProcessorValueTreeState &state) {
  int made = 0;

  for (int m = 0; m < params::kNumMacros; ++m)
    if (auto *p = dynamic_cast<juce::RangedAudioParameter *>(
            state.getParameter(params::macroRowId(m))))
      made += (int)std::lround(p->convertFrom0to1(p->getValue())) != 0 ? 1 : 0;

  return made;
}

void MacroPanel::choose(int macro, const juce::String &parameterId,
                        const char *title, int count,
                        const std::function<juce::String(int)> &nameOf,
                        juce::Component *under, int firstValue) {
  juce::PopupMenu menu;
  menu.setLookAndFeel(&getLookAndFeel());
  menu.addSectionHeader("Macro " + juce::String(macro + 1) + ": " + title);

  const auto at = chosen(parameterId);

  for (int i = 0; i < count; ++i)
    menu.addItem(i + 1, nameOf(i), true, i + firstValue == at);

  menu.showMenuAsync(
      juce::PopupMenu::Options().withStandardItemHeight(22).withTargetComponent(
          under),
      [this, parameterId, firstValue](int result) {
        if (result <= 0)
          return;

        write(parameterId, result - 1 + firstValue);
        refresh();
      });
}

void MacroPanel::chooseScope(int macro) {
  juce::PopupMenu menu;
  menu.setLookAndFeel(&getLookAndFeel());
  menu.addSectionHeader("Macro " + juce::String(macro + 1) + ": Scope");

  const auto at = chosen(params::macroScopeId(macro));
  const auto selected = (int)params::MacroScope::Selected;

  for (int i = 0; i < selected; ++i)
    menu.addItem(i + 1, params::macroScopeName(i), true, i == at);

  // Selected needs channels, either picked now or kept from before.
  const auto picked = currentSelection != nullptr ? currentSelection() : 0u;
  const auto kept =
      channels != nullptr ? (*channels)[(size_t)macro].load() : 0u;

  menu.addSeparator();
  menu.addItem(
      selected + 1,
      picked != 0 ? juce::String("Selected channels (") +
                        params::channelListText(picked).replace(",", ", ") + ")"
                  : juce::String("Selected channels"),
      channels != nullptr && (picked != 0 || kept != 0), at == selected);

  menu.showMenuAsync(
      juce::PopupMenu::Options().withStandardItemHeight(22).withTargetComponent(
          &strips[(size_t)macro].scope),
      [this, macro, picked](int result) {
        if (result <= 0)
          return;

        const auto change = [this, macro, picked, result] {
          // The enum rather than a local constant, which MSVC needs captured
          // and AppleClang warns about capturing.
          if (result - 1 == (int)params::MacroScope::Selected && picked != 0 &&
              channels != nullptr)
            (*channels)[(size_t)macro].store(picked);

          write(params::macroScopeId(macro), result - 1);
        };

        if (recordEdit != nullptr)
          recordEdit("Choose a macro's channels", change);
        else
          change();

        refresh();
      });
}

void MacroPanel::showReading(int macro) {
  auto &strip = strips[(size_t)macro];

  if (chosen(params::macroRowId(macro)) == 0) {
    strip.reading.setReading({}, false);
    return;
  }

  // A share of the control's travel rather than its units, since a macro
  // shifts each channel from wherever it sits. A minus and no plus.
  const auto share = (float)strip.amount.getValue() * 100.0f;

  // Four cells and a point, each digit in a fixed cell. The first cell holds
  // the sign and the hundreds digit together. See segmentsFor.
  const auto tenths = juce::roundToInt(std::abs(share) * 10.0f);
  const bool hundred = tenths >= 1000;
  const bool minus = share < 0.0f;

  const auto digit = [](int d) {
    return juce::String::charToString((juce::juce_wchar)('0' + d));
  };

  const auto tens = (tenths / 100) % 10;

  juce::String shown;

  shown += hundred ? (minus ? "!" : "1") : (minus ? "~" : " ");

  // A leading zero is a blank cell.
  shown += hundred || tens > 0 ? digit(tens) : juce::String(" ");
  shown += digit((tenths / 10) % 10);
  shown += ".";
  shown += digit(tenths % 10);

  strip.reading.setReading(shown, true);

  // The title carries the number for a screen reader.
  strip.reading.setTitle("Amount: " + juce::String(share, 1) + " per cent");
}

void MacroPanel::refresh() {
  showing.clear();

  for (int m = 0; m < params::kNumMacros; ++m)
    if (chosen(params::macroRowId(m)) != 0)
      showing.push_back(m);

  for (int m = 0; m < params::kNumMacros; ++m) {
    auto &strip = strips[(size_t)m];
    const bool made =
        std::find(showing.begin(), showing.end(), m) != showing.end();

    const std::array<juce::Component *, 8> parts{
        &strip.colour, &strip.row,    &strip.scope,  &strip.curve,
        &strip.anchor, &strip.remove, &strip.amount, &strip.reading};

    for (auto *c : parts)
      c->setVisible(made);

    if (!made)
      continue;

    const auto row = chosen(params::macroRowId(m));
    const auto curve = (params::MacroCurve)chosen(params::macroCurveId(m));

    strip.row.setButtonText(params::macroRowName(row));
    strip.scope.setButtonText(
        params::macroScopeName(chosen(params::macroScopeId(m))));

    strip.curve.setButtonText(params::macroCurveName(curve));

    // The fader borrows the row's skew, symmetric because the amount is
    // signed.
    strip.amount.setSkewFactor((double)params::macroRowRange(apvts, row).skew,
                               true);

    // ---- what the scope is, in the colour the mixer says it in ------------
    // An interval scope lights in that interval's colour, and the others in
    // the accent.
    const auto scope = chosen(params::macroScopeId(m));
    const auto interval = scope - (int)params::MacroScope::Interval;

    //
    // Through buttonOnColourId, which drawButtonFace lets win over the lamp.
    const auto scopeTint = interval >= 0 && interval < 12
                               ? intervalColour(interval)
                               : colours::accent;

    // Which channels, since the word alone does not say.
    strip.scope.setTooltip(
        scope == (int)params::MacroScope::Selected && channels != nullptr
            ? "Reaches channels " +
                  params::channelListText((*channels)[(size_t)m].load())
                      .replace(",", ", ") +
                  ". Pick other channels and choose Selected again to "
                  "change them."
            : "Which channels it reaches: all, odd, even, one interval of "
              "the series, or the channels picked by their numbers.");

    strip.scope.setColour(juce::TextButton::buttonOnColourId, scopeTint);
    strip.scope.setToggleState(true, juce::dontSendNotification);

    // Only when the curve has somewhere to lean.
    strip.anchor.setVisible(curve == params::MacroCurve::Taper);

    const auto anchor =
        juce::jlimit(1, kNumHarmonics, chosen(params::macroAnchorId(m)));

    strip.anchor.setButtonText(juce::String(anchor));

    // And the channel it leans on, in that channel's own colour.
    strip.anchor.setColour(juce::TextButton::buttonOnColourId,
                           intervalColour(harmonic(anchor - 1).pitchClass));
    strip.anchor.setToggleState(true, juce::dontSendNotification);

    showReading(m);

    const auto tint = params::macroColour(chosen(params::macroColourId(m)));

    strip.colour.setColour(juce::TextButton::buttonColourId, tint);
    strip.colour.setColour(juce::TextButton::buttonOnColourId, tint);
    strip.colour.setToggleState(true, juce::dontSendNotification);
    strip.amount.setColour(juce::Slider::trackColourId,
                           tint.isTransparent() ? colours::accent : tint);
  }

  addButton.setVisible(firstFree() >= 0);

  resized();
  repaint();
}

void MacroPanel::resized() {
  const int wanted = juce::jmin(kMaxCardWidth, getWidth() - kPadding * 2);
  const int rows = juce::jmax(1, (int)showing.size());

  const int height = kHeaderHeight + kColumnsHeight +
                     rows * (kRowHeight + kRowGap) + kButtonsGap +
                     kButtonHeight + kBottomPad;

  card = juce::Rectangle<int>(0, 0, wanted,
                              juce::jmin(getHeight() - kPadding * 2, height))
             .withCentre(getLocalBounds().getCentre());

  auto body = card.reduced(kPadding, 0);
  body.removeFromTop(kHeaderHeight);

  // Nothing made yet, so the space a first macro would take carries the line
  // saying what one is.
  if (showing.empty())
    emptyMessage = body.removeFromTop(kColumnsHeight + kRowHeight + kRowGap);

  // The column names, kept as rectangles so paint uses the same arithmetic.
  if (!showing.empty()) {
    auto line = body.removeFromTop(kColumnsHeight);

    line.removeFromLeft(kColourWidth + kGap);
    columnLabel[0] = line.removeFromLeft(kRowWidth);
    line.removeFromLeft(kGap);
    columnLabel[1] = line.removeFromLeft(kScopeWidth);
    line.removeFromLeft(kGap);
    columnLabel[2] = line.removeFromLeft(kCurveWidth);
    line.removeFromLeft(kGap + kAnchorWidth + kGap);

    line.removeFromRight(kRemoveWidth + kGap);
    columnLabel[4] = line.removeFromRight(kReadingWidth);
    line.removeFromRight(kGap);
    columnLabel[3] = line;
  }

  for (int m : showing) {
    auto &strip = strips[(size_t)m];
    auto line = body.removeFromTop(kRowHeight);
    body.removeFromTop(kRowGap);

    strip.colour.setBounds(line.removeFromLeft(kColourWidth));
    line.removeFromLeft(kGap);
    strip.row.setBounds(line.removeFromLeft(kRowWidth));
    line.removeFromLeft(kGap);
    strip.scope.setBounds(line.removeFromLeft(kScopeWidth));
    line.removeFromLeft(kGap);
    strip.curve.setBounds(line.removeFromLeft(kCurveWidth));
    line.removeFromLeft(kGap);

    // The anchor keeps its place whether or not it is showing, so the fader
    // beside it does not change length when the curve does.
    strip.anchor.setBounds(line.removeFromLeft(kAnchorWidth));
    line.removeFromLeft(kGap);

    strip.remove.setBounds(line.removeFromRight(kRemoveWidth));
    line.removeFromRight(kGap);
    strip.reading.setBounds(line.removeFromRight(kReadingWidth));
    line.removeFromRight(kGap);

    // Whatever is left.
    strip.amount.setBounds(line);
  }

  // Straight under the last row, with the space left over below them.
  body.removeFromTop(kButtonsGap - kRowGap);

  auto buttons = body.removeFromTop(kButtonHeight);

  addButton.setBounds(buttons.removeFromLeft(110));
  closeButton.setBounds(buttons.removeFromRight(72));
}

void MacroPanel::paint(juce::Graphics &g) {
  // The mixer still readable underneath.
  g.fillAll(juce::Colours::black.withAlpha(0.62f));

  g.setColour(colours::panel);
  g.fillRoundedRectangle(card.toFloat(), 6.0f);

  g.setColour(colours::outline);
  g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 6.0f, 1.0f);

  auto header = card.reduced(kPadding, 0).removeFromTop(kHeaderHeight);

  g.setColour(colours::text);
  g.setFont(makeFont(13.0f, true));
  g.drawText("MACROS", header, juce::Justification::centredLeft, false);

  if (showing.empty()) {
    // A sentence, at about the size of the bold words on the buttons.
    g.setColour(colours::textDim);
    g.setFont(makeFont(15.0f, false));
    g.drawText("One parameter that moves a whole row, which a host can "
               "automate.",
               emptyMessage, juce::Justification::centred, true);

    return;
  }

  // Named as the parameters are, and set like every other caption.
  static const char *const names[] = {"ROW", "SCOPE", "CURVE", "AMOUNT",
                                      "PERCENT"};

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));

  for (size_t i = 0; i < columnLabel.size(); ++i)
    g.drawText(names[i], columnLabel[i], juce::Justification::centred, false);
}

void MacroPanel::mouseDown(const juce::MouseEvent &e) {
  // Only a click on the dim, not on the card's own background.
  if (!card.contains(e.getPosition()) && onDismiss != nullptr)
    onDismiss();
}

} // namespace ovt::ui
