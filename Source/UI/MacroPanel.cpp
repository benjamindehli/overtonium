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

/// Between the last macro and the two buttons, and under them. The buttons
/// belong to the list rather than to the card's bottom edge, so they sit just
/// below the rows with the breathing room underneath instead of above.
constexpr int kButtonsGap = 12;
constexpr int kButtonHeight = 26;
constexpr int kBottomPad = 18;
constexpr int kMaxCardWidth = 760;

/// What each control on a strip is given, left to right. The amount takes
/// whatever is left, since it is the one being drawn on and the rest are
/// menus that only have to hold a word.
constexpr int kColourWidth = 30;
constexpr int kRowWidth = 124;
constexpr int kScopeWidth = 104;
constexpr int kCurveWidth = 74;
constexpr int kAnchorWidth = 40;
/// Wide enough for a sign, three digits, a point and the unit beside them.
/// At 74 the digits had 53 px between five cells and came out touching: see
/// SegmentDisplay::hasRoomForUnit, which is the floor rather than the aim.
/// The amount gives the difference up and still has the widest column.
constexpr int kReadingWidth = 92;
/// Square, like the colour swatch at the other end of the row. Both are a
/// mark rather than a word and the row is thirty tall, so anything narrower
/// read as a button that had been squeezed.
constexpr int kRemoveWidth = 30;
constexpr int kGap = 6;

} // namespace

MacroPanel::MacroPanel(juce::AudioProcessorValueTreeState &state)
    : apvts(state) {
  // Takes every click that lands on it, including the ones on the dim, which
  // is what makes clicking away a dismissal rather than a click on the mixer.
  setInterceptsMouseClicks(true, true);

  for (int m = 0; m < params::kNumMacros; ++m)
    buildStrip(m);

  addButton.setButtonText("Add a macro");
  addButton.setTooltip("Takes one of the eight and points it at a row. There "
                       "are eight because a plugin declares its parameters "
                       "once and cannot grow one later.");
  addButton.onClick = [this] {
    const auto free = firstFree();

    if (free < 0)
      return;

    // Pointed at the first real row, which is TUNE, since that is the one
    // issue #24 asked for and the one a macro is most wanted on.
    write(params::macroRowId(free), 1);
    refresh();
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

  // Named for a screen reader, and distinct, which the suite insists on: a
  // window of 643 sliders is exactly where an unnamed one hides.
  const auto which = "Macro " + juce::String(macro + 1) + " ";

  strip.amount.setTitle(which + "amount");
  strip.colour.setTitle(which + "colour");
  strip.row.setTitle(which + "row");
  strip.scope.setTitle(which + "scope");
  strip.curve.setTitle(which + "curve");
  strip.anchor.setTitle(which + "taper anchor");
  strip.remove.setTitle("Remove macro " + juce::String(macro + 1));

  // A swatch rather than a word. The colour is the thing it says, and a
  // button reading "Violet" in grey would be saying it twice and worse.
  strip.colour.setTooltip("The colour this macro lends the controls it "
                          "drives.");
  strip.colour.onClick = [this, macro] {
    choose(
        macro, params::macroColourId(macro), "Colour", params::kNumMacroColours,
        [](int i) { return juce::String(params::macroColourName(i)); },
        &strips[(size_t)macro].colour);
  };

  strip.row.setTooltip("Which row of the mixer this macro moves.");
  strip.row.onClick = [this, macro] {
    // One longer than the rows, since None is on the end of the menu as the
    // way to remove a macro from inside its own list.
    choose(
        macro, params::macroRowId(macro), "Row", params::kNumMacroRows + 1,
        [](int i) { return juce::String(params::macroRowName(i)); },
        &strips[(size_t)macro].row);
  };

  strip.scope.setTooltip("Which channels it reaches. Past the first three, "
                         "every interval the series carries.");
  strip.scope.onClick = [this, macro] {
    choose(
        macro, params::macroScopeId(macro), "Scope", params::kNumMacroScopes,
        [](int i) { return juce::String(params::macroScopeName(i)); },
        &strips[(size_t)macro].scope);
  };

  strip.curve.setTooltip("How it is shared out. Taper leans on the "
                         "fundamental and fades towards the top of the "
                         "series.");
  strip.curve.onClick = [this, macro] {
    choose(
        macro, params::macroCurveId(macro), "Curve",
        (int)params::MacroCurve::NumCurves,
        [](int i) {
          return juce::String(params::macroCurveName((params::MacroCurve)i));
        },
        &strips[(size_t)macro].curve);
  };

  strip.anchor.setTooltip("The channel a taper leans on hardest. Shown only "
                          "when the curve has somewhere to lean.");
  strip.anchor.onClick = [this, macro] {
    choose(
        macro, params::macroAnchorId(macro), "Anchor", kNumHarmonics,
        [](int i) { return juce::String(i + 1); },
        &strips[(size_t)macro].anchor, 1);
  };

  // A readout rather than a control, so it stays quiet under the pointer:
  // it has no onClick, which is what a SegmentDisplay reads as a readout.
  strip.reading.setInterceptsMouseClicks(false, false);

  strip.amount.onValueChange = [this, macro] { showReading(macro); };

  // The amount is the one a host automates, so it is the one worth binding
  // to a controller: the row, the scope and the curve say what a macro is
  // rather than what it is doing.
  learn::tag(strip.amount, params::macroAmountId(macro));

  strip.amount.onPopup = [this, macro] {
    if (onLearnRequested != nullptr)
      onLearnRequested(params::macroAmountId(macro));
  };

  strip.remove.setButtonText("x");
  strip.remove.setTooltip("Puts this macro back in the pool. What it was "
                          "driving goes back to what the patch says.");
  strip.remove.onClick = [this, macro] {
    write(params::macroRowId(macro), 0);
    refresh();
  };

  // A channel fader laid on its side, with its own segments rather than a
  // meter's: the panel is over the mixer and should be made of the same
  // things it is covering.
  strip.amount.getProperties().set("segmentedTrack", true);
  strip.amount.getProperties().set("bipolar", true);
  strip.amount.setTooltip("How far it pushes the row, and the one a host "
                          "automates.");
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

void MacroPanel::showReading(int macro) {
  auto &strip = strips[(size_t)macro];

  if (chosen(params::macroRowId(macro)) == 0) {
    strip.reading.setReading({}, false);
    return;
  }

  // A share of the control's travel rather than a number of its units.
  //
  // Units were wrong twice over. A macro shifts a proportion of the travel,
  // so what it comes to in seconds or cents depends on where each of the 32
  // channels is already sitting, and there is no single number to print. And
  // on a row like decay, which runs from a thousandth of a second to twenty,
  // the same proportion is a few milliseconds at one end and seconds at the
  // other.
  //
  // A minus and no plus, which is what seven bars can draw: an upright has
  // to go into the same narrow cell as the minus and what comes out reads as
  // a speck beside it. The tuning readouts write a sharp partial the same
  // way, with no sign at all, and a tuner does too.
  //
  // Dim at nothing, the way a fader all the way down reads -inF dimmed: an
  // amount of zero is a statement that the macro is doing nothing rather
  // than a level it has been set to.
  const auto share = (float)strip.amount.getValue() * 100.0f;

  // Every element in the same place whatever the number is, which is what a
  // display with real cells in it does. Left to itself the reading walks
  // left and right as the amount passes ten and a hundred, and shuffles
  // along by half a cell the moment it goes negative.
  //
  // Four cells and a point. The amount runs to a hundred either way, so the
  // first cell is a one or nothing and the sign is a minus or nothing: both
  // live in that one cell, the sign on the middle bar and the one on the two
  // uprights beside it. See segmentsFor, where those two glyphs are.
  const auto tenths = juce::roundToInt(std::abs(share) * 10.0f);
  const bool hundred = tenths >= 1000;
  const bool minus = share < 0.0f;

  const auto digit = [](int d) {
    return juce::String::charToString((juce::juce_wchar)('0' + d));
  };

  const auto tens = (tenths / 100) % 10;

  juce::String shown;

  shown += hundred ? (minus ? "!" : "1") : (minus ? "~" : " ");

  // A leading zero is a blank cell, the way it is on anything with real
  // cells in it, unless there is a hundreds digit in front of it.
  shown += hundred || tens > 0 ? digit(tens) : juce::String(" ");
  shown += digit((tenths / 10) % 10);
  shown += ".";
  shown += digit(tenths % 10);

  strip.reading.setReading(shown, tenths > 0);

  // Said in words as well, because the cells are drawn rather than written
  // and there is no other way to read them. This was a Label before, whose
  // text is its own accessible name, so swapping it for a display quietly
  // took the number away from anyone not looking at it.
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

    // The fader borrows the row's own feel, so pushing a macro on a time
    // moves the way turning that time does rather than running away at the
    // top. Symmetric, because the amount is signed and a skew applied from
    // one end would make pulling down behave unlike pushing up.
    strip.amount.setSkewFactor((double)params::macroRowRange(apvts, row).skew,
                               true);

    // ---- what the scope is, in the colour the mixer says it in ------------
    // A scope naming an interval lights in that interval's own colour, which
    // is the colour every channel of it is drawn in across the mixer. So the
    // word and the channels it reaches are the same colour and the panel is
    // saying what the mixer is about to show. All, Odd and Even reach the
    // whole series rather than one interval, so they take the accent, which
    // is what the chrome uses for anything that is not one partial.
    const auto scope = chosen(params::macroScopeId(m));
    const auto interval = scope - (int)params::MacroScope::Interval;

    //
    // Through buttonOnColourId rather than the colour the button lights its
    // word in. JUCE hands a look and feel buttonOnColourId for a button that
    // is toggled, and drawButtonFace lets a background that has been named
    // win over the lamp, which is the door the colour swatches go through.
    // Setting only the lamp left every one of these the scheme's own blue.
    const auto scopeTint =
        interval >= 0 ? intervalColour(interval) : colours::accent;

    strip.scope.setColour(juce::TextButton::buttonOnColourId, scopeTint);
    strip.scope.setToggleState(true, juce::dontSendNotification);

    // Only when the curve has somewhere to lean.
    strip.anchor.setVisible(curve == params::MacroCurve::Taper);

    const auto anchor =
        juce::jlimit(1, kNumHarmonics, chosen(params::macroAnchorId(m)));

    strip.anchor.setButtonText(juce::String(anchor));

    // And the channel it leans on, in that channel's own colour, so the
    // number and the strip it names agree without having to be counted to.
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
  // saying what one is. Taken out of the same body, so the buttons sit under
  // it exactly as they sit under a list.
  if (showing.empty())
    emptyMessage = body.removeFromTop(kColumnsHeight + kRowHeight + kRowGap);

  // The names of the columns, kept as rectangles rather than drawn from the
  // same arithmetic twice: a heading that drifted from the thing under it
  // would be worse than no heading.
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

    // Whatever is left, which is what makes the amount the widest thing on
    // the line: it is the one being drawn on.
    strip.amount.setBounds(line);
  }

  // Straight under the last row, with the space left over below them.
  body.removeFromTop(kButtonsGap - kRowGap);

  auto buttons = body.removeFromTop(kButtonHeight);

  addButton.setBounds(buttons.removeFromLeft(110));
  closeButton.setBounds(buttons.removeFromRight(72));
}

void MacroPanel::paint(juce::Graphics &g) {
  // The mixer still readable underneath, so it is clear what the macros are
  // over rather than instead of.
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
    g.setColour(colours::textDim);
    g.setFont(makeFont(12.0f, false));
    g.drawText("One parameter that moves a whole row, and that a host can "
               "automate.",
               emptyMessage, juce::Justification::centred, true);

    return;
  }

  // What each column is. Named as the parameters are named, so the panel and
  // the host's own list say the same word about the same thing, and set the
  // way every other caption in this window is set: upper case, nine point,
  // bold, dim. A heading in a different hand from the ones over the mixer
  // reads as belonging to a different program.
  static const char *const names[] = {"ROW", "SCOPE", "CURVE", "AMOUNT",
                                      "PERCENT"};

  g.setColour(colours::textDim);
  g.setFont(makeFont(9.0f, true));

  for (size_t i = 0; i < columnLabel.size(); ++i)
    g.drawText(names[i], columnLabel[i], juce::Justification::centred, false);
}

void MacroPanel::mouseDown(const juce::MouseEvent &e) {
  // Only a click on the dim. Anything inside the card belongs to whatever is
  // standing there, and a click on the card's own background should not throw
  // away what somebody is in the middle of setting up.
  if (!card.contains(e.getPosition()) && onDismiss != nullptr)
    onDismiss();
}

} // namespace ovt::ui
