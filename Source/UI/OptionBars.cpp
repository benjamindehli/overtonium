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
constexpr int kGroupGap = 6;
constexpr int kGroupPad = 6;
constexpr int kControlGap = 6;
constexpr int kControlHeight = 24;

/// Room for the bar's name, the way the wordmark has room in the top bar.
constexpr int kNameWidth = 62;

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
}

void OptionBar::resized() {
  groupBounds.clear();

  auto area = getLocalBounds().reduced(kBarMargin, kBarPadY);
  area.removeFromLeft(kNameWidth);

  for (auto &entry : groups) {
    auto &group = entry.items;

    int width = 2 * kGroupPad;
    for (size_t i = 0; i < group.size(); ++i)
      width += group[i].width + (i > 0 ? kControlGap : 0);

    // A group that does not fit is left out whole rather than cut, and so is
    // everything after it. The Settings menu still has all of it.
    const bool fits = entry.shown && width <= area.getWidth();

    if (!entry.shown) {
      groupBounds.push_back({});
      for (auto &item : group)
        item.control->setVisible(false);
      continue;
    }

    auto bounds = fits ? area.removeFromLeft(width) : juce::Rectangle<int>();
    groupBounds.push_back(bounds);
    area.removeFromLeft(kGroupGap);

    auto inner = bounds.reduced(kGroupPad, 0);

    for (size_t i = 0; i < group.size(); ++i) {
      auto &item = group[i];
      item.control->setVisible(fits);

      if (!fits)
        continue;

      if (i > 0)
        inner.removeFromLeft(kControlGap);

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
            column.withY(bounds.getY() + kReadoutInset)
                .withHeight(bounds.getHeight() - 2 * kReadoutInset));
      else
        item.control->setBounds(column.getX(),
                                dial.getCentreY() - kControlHeight / 2,
                                column.getWidth(), kControlHeight);
    }

    if (!fits)
      area = {};
  }
}

void OptionBar::paint(juce::Graphics &g) {
  // A shade under the top bar, so the two read as one panel in two layers
  // rather than as one bar that has grown.
  g.setColour(colours::panel.darker(0.12f));
  g.fillRect(getLocalBounds());

  g.setColour(colours::outline);
  g.fillRect(0, getHeight() - 1, getWidth(), 1);

  g.setColour(colours::textDim);
  g.setFont(makeFont(10.0f, true));
  g.drawText(
      name,
      getLocalBounds().reduced(kBarMargin, kBarPadY).withWidth(kNameWidth),
      juce::Justification::centredLeft, false);

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
      if (item.caption.isNotEmpty())
        g.drawText(item.caption,
                   LabelledKnob::captionBounds(bounds)
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

  addGroup({{&voices, 40, "VOICES"}, {&legato, 62, {}}, {&onePerKey, 74, {}}});
  addGroup({{&bend, 40, "BEND"}});
  addGroup(
      {{&mpe, 44, {}}, {&aftertouch, 96, "AFTERTOUCH"}, {&slide, 84, "SLIDE"}});

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

/// Cents as a display shows them: one decimal, a sign either side of zero,
/// and a plain zero for anything that rounds to it.
juce::String centsReading(double cents) {
  if (std::abs(cents) < 0.05)
    return "0.0";

  return (cents > 0.0 ? "+" : "") + juce::String(cents, 1);
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

void AdaptiveReadout::show(const AdaptiveReading &next) {
  reading = next;
  setTitle(reading.spoken());
  repaint();
}

void AdaptiveReadout::paint(juce::Graphics &g) {
  const auto r = getLocalBounds().toFloat();
  paintRecess(g, r, 3.0f);
  paintDisplayGround(g, r, 3.0f, false);

  auto area = getLocalBounds().reduced(10, 4);
  const auto lit = colours::accent;
  const auto dim = lit.withAlpha(0.6f);

  // DRIFT at the far end, and the chord as harmonics, or what it gave up,
  // with a line under it, in the rest.
  auto tail = area.removeFromRight(86);
  auto head = area.withTrimmedRight(8);
  g.setColour(lit);
  g.setFont(makeFont(19.0f, true));
  const auto big = reading.harmonics.isNotEmpty() ? reading.harmonics
                   : reading.givenUp.isNotEmpty() ? reading.givenUp
                                                  : juce::String("-");
  g.drawFittedText(big, head.removeFromTop(26),
                   juce::Justification::centredLeft, 1, 0.6f);

  g.setColour(dim);
  g.setFont(makeFont(10.0f, true));
  const auto line = reading.seventhOn.isNotEmpty()
                        ? "7:4 ON " + reading.seventhOn + ", " + reading.verdict
                        : reading.verdict;
  g.drawFittedText(line, head, juce::Justification::topLeft, 2, 0.7f);

  g.setColour(lit.withAlpha(0.25f));
  g.fillRect(tail.getX(), tail.getY() + 3, 1, tail.getHeight() - 6);
  tail.removeFromLeft(10);

  g.setColour(dim);
  g.setFont(makeFont(10.0f, true));
  g.drawText("DRIFT", tail.removeFromTop(14), juce::Justification::centredLeft,
             false);
  g.setColour(lit);
  g.setFont(makeFont(13.0f, true));
  const auto d = reading.drift;
  g.drawText((d > 0 ? "+" : "") + juce::String(d) +
                 (std::abs(d) == 1 ? " COMMA" : " COMMAS"),
             tail.removeFromTop(16), juce::Justification::centredLeft, false);
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

  addGroup({{&temperament, 108, "TEMPERAMENT"},
            {&root, 30, "ROOT"},
            {&reference, 54, "REFERENCE"}});
  retuneGroup = addGroup({{&retune, 38, {}}});

  std::vector<Item> scaleItems;
  for (int pc = 0; pc < 12; ++pc) {
    scale[(size_t)pc] = std::make_unique<SegmentDisplay>(juce::String());
    scale[(size_t)pc]->setTooltip(
        "How far this note sits from equal temperament, in cents, with A on "
        "the reference pitch");
    scaleItems.push_back(
        {scale[(size_t)pc].get(), 46, params::kPitchClassNames[(size_t)pc]});
  }
  addGroup(std::move(scaleItems));

  readoutGroup = addGroup({{&readout, 240, {}, true}});

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
  setGroupShown(readoutGroup, adaptive);

  showScale();
}

void TuningBar::showChord(const AdaptiveSnapshot &snapshot) {
  chord = snapshot;
  readout.show(AdaptiveReading::of(snapshot));
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
