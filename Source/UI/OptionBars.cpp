#include "OptionBars.h"

#include <cmath>
#include <iterator>
#include <vector>

#include "../PluginParameters.h"
#include "../dsp/Temperament.h"
#include "LookAndFeel.h"
#include "SettingsMenus.h"

namespace ovt::ui {

namespace {
/// The same margins and spacing the top bar keeps, so the groups in a bar
/// read as more of it.
constexpr int kBarMargin = 12;
constexpr int kBarPadY = 6;

/// A row and the gap between rows, the top bar's own.
constexpr int kRow = 54;
constexpr int kRowGap = 4;
constexpr int kGroupGap = 6;
constexpr int kGroupPad = 6;
constexpr int kControlGap = 6;
constexpr int kControlHeight = 24;

/// How far a screen that fills its group stands in from the group's edge.
constexpr int kReadoutInset = 5;

/// How close to a whole number of cents an interval has to be to count as
/// pure. A tempered fifth is a quarter of a cent narrow, which is pure to any
/// ear and has to read as pure here.
constexpr double kPureWithin = 1.0;

/// Short enough for the cells the displays have.
constexpr const char *kAftertouchWords[] = {"PRESSURE", "WHEEL", "EITHER"};
constexpr const char *kSlideWords[] = {"OFF", "BRIGHT", "TUNING"};
} // namespace

OptionBar::OptionBar(juce::String nameToShow) : name(std::move(nameToShow)) {
  // Shouted on the panel like everything else, and said normally to a screen
  // reader.
  setTitle(name.substring(0, 1) + name.substring(1).toLowerCase());
}

int OptionBar::addGroup(std::vector<Item> items) {
  for (auto &item : items)
    addAndMakeVisible(item.control);

  groups.push_back({std::move(items), true});
  return (int)groups.size() - 1;
}

void OptionBar::setGroupShown(int group, bool shown) {
  if (group < 0 || group >= (int)groups.size() ||
      groups[(size_t)group].shown == shown)
    return;

  groups[(size_t)group].shown = shown;
  resized();
  repaint();

  if (onHeightMayChange)
    onHeightMayChange();
}

void OptionBar::setItemShown(juce::Component &control, bool shown) {
  for (auto &group : groups)
    for (auto &item : group.items)
      if (item.control == &control && item.shown != shown) {
        item.shown = shown;
        resized();
        repaint();

        if (onHeightMayChange)
          onHeightMayChange();
        return;
      }
}

int OptionBar::widthOf(const Group &group) {
  if (!group.shown)
    return 0;

  int width = 0, shownItems = 0;
  for (const auto &item : group.items)
    if (item.shown) {
      width += item.width + (shownItems > 0 ? kControlGap : 0);
      ++shownItems;
    }

  return shownItems > 0 ? width + 2 * kGroupPad : 0;
}

int OptionBar::perLine(const Group &group, int room) {
  int shown = 0, widest = 1;
  for (const auto &item : group.items)
    if (item.shown) {
      ++shown;
      widest = juce::jmax(widest, item.width);
    }

  if (shown == 0 || widthOf(group) - 2 * kGroupPad <= room)
    return juce::jmax(1, shown);

  // As many as fit across at the widest control's width, then shared out.
  const auto fit = juce::jmax(1, (room + kControlGap) / (widest + kControlGap));
  const auto lines = (shown + fit - 1) / fit;
  return (shown + lines - 1) / lines;
}

std::vector<juce::Rectangle<int>>
OptionBar::placeGroups(juce::Rectangle<int> area, int *rows) const {
  std::vector<juce::Rectangle<int>> placed;
  placed.reserve(groups.size());

  // Left to right, and onto a new row when the next group does not fit in
  // what is left of this one, the way the top bar packs its groups. A group
  // wider than the whole bar, which the top bar never has, wraps its own
  // controls onto as many lines as it needs inside its frame, one row each,
  // and nothing stands beside it.
  int row = 0, x = area.getX(), used = 0;

  for (const auto &group : groups) {
    auto width = widthOf(group);

    if (width == 0) {
      placed.push_back({});
      continue;
    }

    int lines = 1;

    if (width > area.getWidth()) {
      // Shared out the way resized lays them out, so the two agree.
      const auto each = perLine(group, area.getWidth() - 2 * kGroupPad);
      int shown = 0, line = 0, widest = 0;

      for (const auto &item : group.items) {
        if (!item.shown)
          continue;

        if (shown > 0 && shown % each == 0) {
          widest = juce::jmax(widest, line);
          line = 0;
          ++lines;
        }

        line += item.width + (line > 0 ? kControlGap : 0);
        ++shown;
      }

      width = juce::jmax(widest, line) + 2 * kGroupPad;
    }

    if (used > 0 && (lines > 1 || x + width > area.getRight())) {
      ++row;
      x = area.getX();
      used = 0;
    }

    placed.push_back({x, area.getY() + row * (kRow + kRowGap), width,
                      lines * kRow + (lines - 1) * kRowGap});

    if (lines > 1) {
      row += lines;
      x = area.getX();
      used = 0;
      continue;
    }

    x += width + kGroupGap;
    ++used;
  }

  if (rows != nullptr)
    *rows = juce::jmax(1, used > 0 ? row + 1 : row);

  return placed;
}

int OptionBar::heightForWidth(int width) const {
  int rows = 1;
  placeGroups(
      {kBarMargin, kBarPadY, juce::jmax(1, width - 2 * kBarMargin), kRow},
      &rows);
  return rows * kRow + (rows - 1) * kRowGap + 2 * kBarPadY;
}

void OptionBar::resized() {
  groupBounds =
      placeGroups(getLocalBounds().reduced(kBarMargin, kBarPadY), nullptr);

  for (size_t g = 0; g < groups.size(); ++g) {
    const auto &bounds = groupBounds[g];

    // The line the controls go along, one row tall, moving down a row when the
    // next control does not fit in what is left of it, which only a group
    // wider than the bar ever needs.
    auto line = bounds.withHeight(kRow);
    auto inner = line.reduced(kGroupPad, 0);
    bool first = true;
    int placedHere = 0;
    const auto each = perLine(
        groups[g], getLocalBounds().reduced(kBarMargin, kBarPadY).getWidth() -
                       2 * kGroupPad);

    for (auto &item : groups[g].items) {
      const bool shown = !bounds.isEmpty() && item.shown;
      item.control->setVisible(shown);

      if (!shown)
        continue;

      if (placedHere > 0 && placedHere % each == 0) {
        line = line.translated(0, kRow + kRowGap);
        inner = line.reduced(kGroupPad, 0);
        first = true;
      }

      if (!first)
        inner.removeFromLeft(kControlGap);
      first = false;
      ++placedHere;
      item.line = line;

      // Centred on the line the top bar's dials stand on, with the caption in
      // the band its knobs put theirs in.
      const auto column = inner.removeFromLeft(item.width);
      const auto dial = LabelledKnob::dialBounds(column);

      // A knob takes the whole column, as the top bar's do, since it draws its
      // own caption under its dial.
      if (dynamic_cast<LabelledKnob *>(item.control) != nullptr)
        item.control->setBounds(column);
      else if (item.fills)
        item.control->setBounds(
            column.withY(line.getY() + kReadoutInset)
                .withHeight(line.getHeight() - 2 * kReadoutInset));
      else
        item.control->setBounds(column.getX(),
                                dial.getCentreY() - kControlHeight / 2,
                                column.getWidth(), kControlHeight);
    }
  }
}

void OptionBar::paint(juce::Graphics &g) {
  // A shade under the top bar, so the two read as one panel in two layers
  // rather than as one bar that has grown.
  g.setColour(colours::panel.darker(0.12f));
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  g.setFont(makeFont(9.0f, true));

  for (size_t i = 0; i < groups.size() && i < groupBounds.size(); ++i) {
    const auto &bounds = groupBounds[i];

    if (bounds.isEmpty())
      continue;

    paintGroupPocket(g, bounds);

    // After the pocket, which leaves its own outline colour behind.
    g.setColour(colours::textDim);

    // Centred under each control, a little wider than it so a caption longer
    // than a narrow display still fits.
    for (const auto &item : groups[i].items)
      if (item.caption.isNotEmpty() && item.shown)
        g.drawText(item.caption,
                   LabelledKnob::captionBounds(item.line)
                       .withX(item.control->getX() - kControlGap)
                       .withWidth(item.control->getWidth() + 2 * kControlGap),
                   juce::Justification::centred, false);
  }
}

void OptionBar::showList(juce::Component &anchor, const juce::PopupMenu &list,
                         juce::AudioProcessorValueTreeState &apvts) {
  auto menu = list;
  menu.setLookAndFeel(&getLookAndFeel());

  juce::Component::SafePointer<OptionBar> self(this);

  menu.showMenuAsync(juce::PopupMenu::Options()
                         .withTargetComponent(&anchor)
                         .withStandardItemHeight(22),
                     [self, &apvts](int result) {
                       if (result != 0 && settings::apply(apvts, result) &&
                           self != nullptr)
                         self->refresh();
                     });
}

void OptionBar::styleSwitch(GlowButton &button, const juce::String &word,
                            const juce::String &title,
                            const juce::String &tooltip) {
  button.setButtonText(word);
  button.setTitle(title);
  button.setTooltip(tooltip);
  button.setColour(juce::TextButton::textColourOnId, colours::accent);
}

// ---- Playing ----------------------------------------------------------------

PlayingBar::PlayingBar(juce::AudioProcessorValueTreeState &state)
    : OptionBar("PLAYING"), apvts(state) {
  styleSwitch(legato, "LEGATO", "Legato",
              "A key going down while another is held carries the note on "
              "rather than starting a new one");
  styleSwitch(onePerKey, "1 PER KEY", "One voice per key",
              "A key played again takes over its own voice rather than "
              "starting another");
  styleSwitch(mpe, "MPE", "MPE",
              "Each note bent, pressed and slid on a channel of its own");

  const auto flip = [this](settings::Id id) {
    settings::apply(apvts, id);
    refresh();
  };

  legato.onClick = [flip] { flip(settings::kLegato); };
  onePerKey.onClick = [flip] { flip(settings::kOneVoicePerKey); };
  mpe.onClick = [flip] { flip(settings::kMpe); };

  voices.setTooltip("How many notes can sound at once");
  bend.setTooltip("How far the pitch wheel bends, in semitones");
  aftertouch.setTooltip("Which control the AT row on every channel follows");
  slide.setTooltip("What sliding a finger along an MPE key moves");

  voices.onClick = [this] { showList(voices, settings::voices(apvts), apvts); };
  bend.onClick = [this] { showList(bend, settings::bendRange(apvts), apvts); };
  aftertouch.onClick = [this] {
    showList(aftertouch, settings::aftertouchFrom(apvts), apvts);
  };
  slide.onClick = [this] { showList(slide, settings::slideTo(apvts), apvts); };

  // Every display as wide as its cells need, so a figure here is the same
  // width as one anywhere else at this height.
  const auto natural = [](const SegmentDisplay &d) {
    return d.naturalWidth(kControlHeight);
  };

  addGroup({{&voices, juce::jmax(40, natural(voices)), "VOICES"},
            {&legato, 62, {}},
            {&onePerKey, 74, {}}});
  addGroup({{&bend, juce::jmax(40, natural(bend)), "BEND"}});
  addGroup({{&mpe, 44, {}},
            {&aftertouch, natural(aftertouch), "AFTERTOUCH"},
            {&slide, natural(slide), "SLIDE"}});

  refresh();
}

void PlayingBar::refresh() {
  const auto count = settings::voiceCount(apvts);
  voices.setReading(juce::String(count), true);
  voices.setTitle("Voices: " + juce::String(count));

  const auto range = settings::choiceIndex(apvts, params::bendRangeId);
  bend.setReading(juce::String(range), true);
  bend.setTitle("Pitch bend range: " + juce::String(range) + " semitones");

  const auto source =
      juce::jlimit(0, 2, settings::choiceIndex(apvts, params::atSourceId));
  aftertouch.setReading(kAftertouchWords[source], true);
  aftertouch.setTitle(juce::String("Aftertouch from: ") +
                      params::kAftertouchSourceNames[(size_t)source]);

  const auto target =
      juce::jlimit(0, 2, settings::choiceIndex(apvts, params::slideDestId));
  slide.setReading(kSlideWords[target], true);
  slide.setTitle("Slide to: " + params::slideDestChoices[target]);

  legato.setToggleState(settings::legatoOn(apvts), juce::dontSendNotification);
  onePerKey.setToggleState(settings::isOn(apvts, params::oneVoicePerKeyId),
                           juce::dontSendNotification);
  mpe.setToggleState(settings::isOn(apvts, params::mpeId),
                     juce::dontSendNotification);
}

// ---- the Adaptive readouts
// -----------------------------------------------------

namespace {
juce::String pitchName(int key) {
  return params::kPitchClassNames[(size_t)(((key % 12) + 12) % 12)];
}

double centsBetween(double low, double high) {
  return 1200.0 * std::log2(high / low);
}

/// Cents as a display shows them: one decimal, a minus below zero, and a
/// plain zero for anything that rounds to it.
juce::String centsReading(double cents) {
  // No plus, which seven bars cannot draw. A sharp note is the one with no
  // sign, as on a channel's own readout and on a tuner.
  if (std::abs(cents) < 0.05)
    return "0.0";

  return juce::String(cents, 1);
}

/// Pure sizes for the intervals a chord reaches for, in cents within an
/// octave, by how many semitones the keys spell. A seventh is pure at 9:5,
/// 16:9 or 7:4, all three of which Adaptive tunes. Seconds and the tritone
/// have none.
std::vector<double> pureSizes(int semitones) {
  const auto c = [](double r) { return 1200.0 * std::log2(r); };

  switch (semitones) {
  case 0:
    return {0.0};
  case 3:
    return {c(6.0 / 5.0)};
  case 4:
    return {c(5.0 / 4.0)};
  case 5:
    return {c(4.0 / 3.0)};
  case 7:
    return {c(3.0 / 2.0)};
  case 8:
    return {c(8.0 / 5.0)};
  case 9:
    return {c(5.0 / 3.0)};
  case 10:
    return {c(9.0 / 5.0), c(16.0 / 9.0), c(7.0 / 4.0)};
  case 11:
    return {c(15.0 / 8.0)};
  default:
    return {};
  }
}
} // namespace

AdaptiveReading AdaptiveReading::of(const AdaptiveSnapshot &snapshot) {
  AdaptiveReading r;
  const auto count =
      juce::jlimit(0, AdaptiveSnapshot::kMaxNotes, snapshot.count);

  if (count == 0) {
    r.verdict = "NO KEYS HELD";
    return r;
  }

  const auto &low = snapshot.notes[0];

  // ---- how far it has drifted
  // -------------------------------------------------
  int versions = 0, onChain = 0;

  for (int i = 0; i < count; ++i) {
    const auto &n = snapshot.notes[(size_t)i];

    if (!n.harmonic) {
      versions += n.version;
      ++onChain;
    }
  }

  r.drift = onChain > 0 ? juce::roundToInt((double)versions / onChain) : 0;

  if (snapshot.harmonicRoot >= 0)
    r.seventhOn = pitchName(snapshot.harmonicRoot);

  if (count < 2) {
    r.verdict = "ONE KEY";
    return r;
  }

  // ---- the chord as one harmonic series
  // --------------------------------------
  //
  // The lowest note as a harmonic of something below it, the lowest such
  // something that every other note is also a harmonic of. A chord that is
  // one series has every partial of every note landing on another, so
  // nothing in it beats.
  for (int below = 1; below <= 16 && r.harmonics.isEmpty(); ++below) {
    const auto fundamental = low.hz / below;
    juce::StringArray numbers;

    for (int i = 0; i < count; ++i) {
      const auto n = snapshot.notes[(size_t)i].hz / fundamental;
      const auto whole = std::round(n);

      if (whole > 64.0 ||
          std::abs(1200.0 * std::log2(n / whole)) >= kPureWithin)
        break;

      numbers.add(juce::String((int)whole));
    }

    if (numbers.size() == count)
      r.harmonics = numbers.joinIntoString(":");
  }

  // ---- what was given up --------------------------------------------------
  double worst = 0.0, worstSigned = 0.0;
  int worstLow = -1, worstHigh = -1;

  for (int a = 0; a < count; ++a)
    for (int b = a + 1; b < count; ++b) {
      const auto &lo = snapshot.notes[(size_t)a];
      const auto &hi = snapshot.notes[(size_t)b];
      if (lo.harmonic || hi.harmonic)
        continue;

      const auto sizes = pureSizes((hi.key - lo.key) % 12);
      if (sizes.empty())
        continue;

      const auto span = centsBetween(lo.hz, hi.hz);
      const auto within = span - 1200.0 * std::floor((hi.key - lo.key) / 12);

      double best = 1.0e9;
      for (const auto pure : sizes)
        if (std::abs(within - pure) < std::abs(best))
          best = within - pure;

      if (std::abs(best) > worst) {
        worst = std::abs(best);
        worstSigned = best;
        worstLow = lo.key;
        worstHigh = hi.key;
      }
    }

  if (worst >= kPureWithin)
    r.givenUp = pitchName(worstLow) + "-" + pitchName(worstHigh) + " " +
                juce::String(juce::roundToInt(worst)) +
                juce::String::fromUTF8("\xc2\xa2 ") +
                (worstSigned > 0.0 ? "WIDE" : "NARROW");

  r.verdict = r.harmonics.isNotEmpty() ? "NOTHING BEATS"
              : r.givenUp.isNotEmpty() ? r.givenUp
                                       : "ALL PURE";
  return r;
}

juce::String AdaptiveReading::spoken() const {
  if (verdict == "NO KEYS HELD")
    return "Adaptive: no keys held";

  juce::String s = "Adaptive: ";
  if (harmonics.isNotEmpty())
    s << "the chord is harmonics " << harmonics << ", ";
  s << verdict.toLowerCase();
  if (seventhOn.isNotEmpty())
    s << ", harmonic seventh on " << seventhOn;
  s << ", drift " << drift << (std::abs(drift) == 1 ? " comma" : " commas");
  return s;
}

// ---- Tuning -----------------------------------------------------------------

namespace {
/// In Temperament's order. Nine cells hold every one whole but Pythagorean
/// and Werckmeister, which are cut where a reader still knows them.
constexpr const char *kTemperamentWords[] = {
    "EQUAL", "JUST", "PYTHAG", "MEANTONE", "WERCK III", "YOUNG", "ADAPTIVE"};
static_assert(std::size(kTemperamentWords) ==
                  (size_t)Temperament::NumTemperaments,
              "a word for every temperament");
} // namespace

const char *TuningBar::temperamentWord(int index) {
  return kTemperamentWords[juce::jlimit(
      0, (int)std::size(kTemperamentWords) - 1, index)];
}

TuningBar::TuningBar(juce::AudioProcessorValueTreeState &state,
                     juce::Component &popupParent)
    : OptionBar("TUNING"), apvts(state) {
  temperament.setTooltip("How the twelve keys of the octave are tuned");
  root.setTooltip("The key the temperament is built on");
  root.showSharpSign(true);
  reference.setTooltip("The pitch A is tuned to");
  retune.slider.setTooltip("How long Adaptive takes to slide a held note to "
                           "where the chord around it needs it. At zero it "
                           "jumps.");
  retune.slider.setTitle("Adaptive retune time");
  retune.slider.setPopupDisplayEnabled(true, true, &popupParent);
  retune.slider.setDoubleClickReturnValue(
      true, (double)params::kAdaptiveRetuneSeconds[0]);
  retuneAttachment =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          apvts, params::adaptiveRetuneId, retune.slider);

  temperament.onClick = [this] {
    showList(temperament, settings::temperament(apvts), apvts);
  };

  // Equal temperament is the one that does not care, so the root says nothing
  // under it and opens nothing, as its Settings entry is greyed out.
  root.onClick = [this] {
    if (settings::choiceIndex(apvts, params::temperamentId) !=
        (int)Temperament::Equal)
      showList(root, settings::root(apvts), apvts);
  };

  reference.onClick = [this] {
    showList(reference, settings::referencePitch(apvts), apvts);
  };

  // Every display as wide as its cells need. See PlayingBar.
  const auto natural = [](const SegmentDisplay &d) {
    return d.naturalWidth(kControlHeight);
  };

  addGroup({{&temperament, natural(temperament), "TEMPERAMENT"},
            {&root, juce::jmax(30, natural(root)), "ROOT"},
            {&reference, natural(reference), "REFERENCE"}});
  retuneGroup = addGroup({{&retune, 38, {}}});

  std::vector<Item> scaleItems;
  for (int pc = 0; pc < 12; ++pc) {
    scale[(size_t)pc] = std::make_unique<SegmentDisplay>(juce::String());
    scale[(size_t)pc]->setTooltip(
        "How far this note sits from equal temperament, in cents, with A on "
        "the reference pitch");
    scaleItems.push_back({scale[(size_t)pc].get(), natural(*scale[(size_t)pc]),
                          params::kPitchClassNames[(size_t)pc]});
  }
  addGroup(std::move(scaleItems));

  drift.setTooltip("How far the held chord sits from where just intonation on "
                   "the root would put it, in commas. It moves only as far as "
                   "the music takes it, and never more than two either way.");
  // Lit by the chord, never by a click.
  pure.setInterceptsMouseClicks(false, false);
  pure.setWantsKeyboardFocus(false);
  pure.setButtonText("PURE");
  pure.setTooltip("Lit while every interval in the chord you hold is pure. "
                  "Dark when the chord has had to give one up, which some "
                  "chords cannot avoid.");

  styleSwitch(sevenths, "7:4", "Harmonic sevenths",
              "Tunes a minor seventh 7:4 above its root, the harmonic "
              "seventh, wherever nothing else in the chord would beat "
              "against it. Off, every seventh takes the size the chain of "
              "fifths gives it.");
  sevenths.onClick = [this] {
    settings::apply(apvts, settings::kHarmonicSevenths);
    refresh();
  };

  styleSwitch(home, "HOME", "Stay home",
              "Keeps the chord you hold as near home as Adaptive can, so the "
              "keyboard does not drift. When a progression would carry it "
              "off, a held note slides a comma instead. Off, the keyboard "
              "goes where the music takes it, up to two commas either way.");
  home.onClick = [this] {
    settings::apply(apvts, settings::kAdaptiveHome);
    refresh();
  };

  chordGroup = addGroup({{&pure, 44, {}},
                         {&sevenths, 40, {}},
                         {&home, 52, {}},
                         {&drift, juce::jmax(40, natural(drift)), "DRIFT"}});

  showChord(chord);
  refresh();
}

void TuningBar::refresh() {
  const auto picked = settings::choiceIndex(apvts, params::temperamentId);
  const auto equal = picked == (int)Temperament::Equal;
  const auto adaptive = picked == (int)Temperament::Adaptive;

  temperament.setReading(temperamentWord(picked), true);
  temperament.setTitle(juce::String("Temperament: ") +
                       temperamentName((Temperament)picked));

  const auto key =
      juce::jlimit(0, (int)params::kPitchClassNames.size() - 1,
                   settings::choiceIndex(apvts, params::tuningRootId));
  root.setReading(params::kPitchClassNames[(size_t)key], !equal);
  root.setTitle(juce::String("Root: ") + params::kPitchClassNames[(size_t)key] +
                (equal ? ", not used by equal temperament" : ""));

  const auto hz = params::kReferenceHzChoices[(size_t)juce::jlimit(
      0, (int)params::kReferenceHzChoices.size() - 1,
      settings::choiceIndex(apvts, params::referenceHzId))];
  reference.setReading(juce::String(hz), true);
  reference.setTitle("Reference pitch: " + juce::String(hz) + " Hz");

  // Retune means nothing under any other temperament, and there is nothing to
  // read out, so both are only there under Adaptive.
  setGroupShown(retuneGroup, adaptive);
  sevenths.setToggleState(settings::isOn(apvts, params::adaptiveSeventhId),
                          juce::dontSendNotification);
  home.setToggleState(settings::isOn(apvts, params::adaptiveHomeId),
                      juce::dontSendNotification);
  setGroupShown(chordGroup, adaptive);

  showScale();
}

void TuningBar::showChord(const AdaptiveSnapshot &snapshot) {
  chord = snapshot;

  const auto reading = AdaptiveReading::of(snapshot);

  // Pure needs at least two keys to be anything, and nothing given up.
  const auto allPure = snapshot.count > 1 && reading.givenUp.isEmpty();
  pure.setToggleState(allPure, juce::dontSendNotification);
  pure.setTitle(allPure ? "Pure: every interval is pure"
                : reading.givenUp.isNotEmpty()
                    ? "Pure: dark, " + reading.givenUp.toLowerCase() +
                          " given up"
                    : juce::String("Pure: dark"));

  // Nothing held has nowhere to sit, so its cells stay dark rather than say
  // zero.
  const auto held = snapshot.count > 0;
  drift.setReading(held ? juce::String(reading.drift) : juce::String(), held);
  drift.setTitle(held
                     ? "Drift: " + juce::String(reading.drift) +
                           (std::abs(reading.drift) == 1 ? " comma" : " commas")
                     : juce::String("Drift: no keys held"));

  showScale();
}

void TuningBar::showScale() {
  const auto picked = settings::choiceIndex(apvts, params::temperamentId);
  const auto adaptive = picked == (int)Temperament::Adaptive;
  const auto rootClass =
      juce::jlimit(0, 11, settings::choiceIndex(apvts, params::tuningRootId));
  const auto referenceHz = (double)params::kReferenceHzChoices[(
      size_t)juce::jlimit(0, (int)params::kReferenceHzChoices.size() - 1,
                          settings::choiceIndex(apvts, params::referenceHzId))];

  const auto count = juce::jlimit(0, AdaptiveSnapshot::kMaxNotes, chord.count);

  for (int pc = 0; pc < 12; ++pc) {
    // Middle C's octave stands for every octave, since a pitch class sits the
    // same distance from equal temperament in all of them.
    const auto key = 60 + pc;
    double hz = 0.0, equal = referenceHz * std::exp2((double)(key - 69) / 12.0);
    bool sounding = !adaptive;

    if (adaptive) {
      for (int i = 0; i < count && !sounding; ++i) {
        const auto &n = chord.notes[(size_t)i];
        if (((n.key % 12) + 12) % 12 == pc) {
          sounding = true;
          hz = n.hz;
          equal = chord.referenceHz * std::exp2((double)(n.key - 69) / 12.0);
        }
      }

      // Not held, so where it would land if it were struck into silence: the
      // version its pitch class used last.
      if (!sounding) {
        const auto version =
            chord.remembered[(size_t)((pc - rootClass + 12) % 12)];
        hz = noteFrequency(key, Temperament::Adaptive, rootClass, referenceHz) *
             std::exp2((double)version * tuning::kAdaptiveComma / 1200.0);
      }
    } else {
      hz = noteFrequency(key, (Temperament)picked, rootClass, referenceHz);
    }

    const auto text = centsReading(centsBetween(equal, hz));
    auto &display = *scale[(size_t)pc];
    display.setReading(text, sounding);
    display.setTitle(juce::String(params::kPitchClassNames[(size_t)pc]) + ": " +
                     text + " cents from equal" +
                     (adaptive ? (sounding ? ", held" : ", not held") : ""));
  }
}

} // namespace ovt::ui
