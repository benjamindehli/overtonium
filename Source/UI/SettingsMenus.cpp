#include "SettingsMenus.h"

#include <cmath>
#include <iterator>

#include "../PluginParameters.h"
#include "../dsp/Params.h"
#include "../dsp/Temperament.h"

namespace ovt::ui::settings {

namespace {
/// The semitone counts anyone actually uses, rather than all 25 of them.
constexpr int kBendChoices[] = {0, 1, 2, 3, 4, 5, 7, 12, 24};

/// The order the menu lists the temperaments in: equal and just first, the
/// historical ones by age, and Adaptive last since it is the one that is not
/// fixed. The parameter keeps the order they were added in, so a host's lanes
/// and saved sessions do not move.
constexpr Temperament kTemperamentOrder[] = {
    Temperament::Equal,         Temperament::Just,
    Temperament::Pythagorean,   Temperament::QuarterComma,
    Temperament::Werckmeister3, Temperament::Vallotti,
    Temperament::KirnbergerIII, Temperament::Young,
    Temperament::Adaptive};
static_assert(std::size(kTemperamentOrder) ==
                  (size_t)Temperament::NumTemperaments,
              "every temperament in the menu");

void choose(juce::AudioProcessorValueTreeState &apvts, const char *id,
            int index) {
  if (auto *p = apvts.getParameter(id))
    p->setValueNotifyingHost(p->convertTo0to1((float)index));
}

void flip(juce::AudioProcessorValueTreeState &apvts, const char *id) {
  if (auto *p = apvts.getParameter(id))
    p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
}
} // namespace

bool isOn(juce::AudioProcessorValueTreeState &apvts, const char *id) {
  auto *p = apvts.getParameter(id);
  return p != nullptr && p->getValue() > 0.5f;
}

int choiceIndex(juce::AudioProcessorValueTreeState &apvts, const char *id) {
  auto *p = apvts.getParameter(id);
  return p != nullptr ? juce::roundToInt(p->convertFrom0to1(p->getValue())) : 0;
}

bool rootInUse(juce::AudioProcessorValueTreeState &apvts) {
  return choiceIndex(apvts, params::temperamentId) != (int)Temperament::Equal ||
         choiceIndex(apvts, params::resonatorModeId) ==
             (int)ResonatorMode::Series;
}

int voiceCount(juce::AudioProcessorValueTreeState &apvts) {
  const auto index = juce::jlimit(0, (int)params::kPolyphonyChoices.size() - 1,
                                  choiceIndex(apvts, params::polyphonyId));
  return params::kPolyphonyChoices[(size_t)index];
}

bool legatoOn(juce::AudioProcessorValueTreeState &apvts) {
  return choiceIndex(apvts, params::polyphonyId) == params::kLegatoIndex ||
         isOn(apvts, params::legatoId);
}

juce::PopupMenu voices(juce::AudioProcessorValueTreeState &apvts) {
  // The voice counts, from one up. The list's first entry, monophonic legato,
  // is the Legato switch with one voice now, so a session that stored it reads
  // as that: one voice, and Legato ticked.
  const auto index = choiceIndex(apvts, params::polyphonyId);
  const auto oldLegato = index == params::kLegatoIndex;

  juce::PopupMenu m;

  for (int i = 1; i < (int)params::kPolyphonyChoices.size(); ++i)
    m.addItem(kVoices + i, params::polyphonyName(i), true,
              i == index || (oldLegato && i == 1));

  return m;
}

juce::PopupMenu bendRange(juce::AudioProcessorValueTreeState &apvts) {
  auto *range = apvts.getParameter(params::bendRangeId);
  const auto current =
      range != nullptr
          ? juce::roundToInt(range->convertFrom0to1(range->getValue()))
          : 2;

  juce::PopupMenu m;

  for (auto semitones : kBendChoices)
    m.addItem(kBendRange + semitones,
              juce::String(semitones) +
                  (semitones == 1 ? " semitone" : " semitones"),
              true, semitones == current);

  return m;
}

juce::PopupMenu aftertouchFrom(juce::AudioProcessorValueTreeState &apvts) {
  // Which channel-wide control the AT row on every strip listens to. Most
  // keyboards have no aftertouch, so the wheel stands in for it, and by
  // default either will do.
  const auto current = choiceIndex(apvts, params::atSourceId);

  juce::PopupMenu m;

  for (int i = 0; i < (int)params::kAftertouchSourceNames.size(); ++i)
    m.addItem(kAftertouchFrom + i, params::kAftertouchSourceNames[(size_t)i],
              true, i == current);

  return m;
}

juce::PopupMenu slideTo(juce::AudioProcessorValueTreeState &apvts) {
  const auto current = choiceIndex(apvts, params::slideDestId);

  juce::PopupMenu m;

  for (int i = 0; i < params::slideDestChoices.size(); ++i)
    m.addItem(kSlideTo + i, params::slideDestChoices[i], true, i == current);

  return m;
}

juce::PopupMenu temperament(juce::AudioProcessorValueTreeState &apvts) {
  const auto current = choiceIndex(apvts, params::temperamentId);

  juce::PopupMenu m;

  for (const auto t : kTemperamentOrder)
    m.addItem(kTemperament + (int)t, temperamentName(t), true,
              (int)t == current);

  return m;
}

juce::PopupMenu root(juce::AudioProcessorValueTreeState &apvts) {
  const auto current = choiceIndex(apvts, params::tuningRootId);

  juce::PopupMenu m;

  for (int i = 0; i < (int)params::kPitchClassNames.size(); ++i)
    m.addItem(kRoot + i, params::kPitchClassNames[(size_t)i], true,
              i == current);

  return m;
}

juce::PopupMenu referencePitch(juce::AudioProcessorValueTreeState &apvts) {
  const auto current = choiceIndex(apvts, params::referenceHzId);

  juce::PopupMenu m;

  for (int i = 0; i < (int)params::kReferenceHzChoices.size(); ++i)
    m.addItem(kReferencePitch + i,
              juce::String(params::kReferenceHzChoices[(size_t)i]) + " Hz",
              true, i == current);

  return m;
}

juce::PopupMenu retune(juce::AudioProcessorValueTreeState &apvts) {
  // Two quick settings of what is a time, ticked when the knob sits on one,
  // within a millisecond, and neither ticked anywhere else.
  auto *p = apvts.getParameter(params::adaptiveRetuneId);
  const auto seconds =
      p != nullptr ? (double)p->convertFrom0to1(p->getValue()) : 0.0;

  juce::PopupMenu m;

  for (int i = 0; i < (int)params::kAdaptiveRetuneNames.size(); ++i) {
    const auto at = params::kAdaptiveRetuneSeconds[(size_t)i];
    m.addItem(kRetune + i,
              juce::String(params::kAdaptiveRetuneNames[(size_t)i]) + ", " +
                  juce::String(juce::roundToInt(at * 1000.0)) + " ms",
              true, std::abs(seconds - at) < 0.001);
  }

  return m;
}

bool apply(juce::AudioProcessorValueTreeState &apvts, int id) {
  switch (id) {
  case kPhaseReset:
    flip(apvts, params::phaseResetId);
    return true;
  case kSafetyClip:
    flip(apvts, params::safetyClipId);
    return true;
  case kMpe:
    flip(apvts, params::mpeId);
    return true;
  case kOneVoicePerKey:
    flip(apvts, params::oneVoicePerKeyId);
    return true;
  case kLookahead:
    flip(apvts, params::lookaheadId);
    return true;
  case kHarmonicSevenths:
    flip(apvts, params::adaptiveSeventhId);
    return true;
  case kAdaptiveHome:
    flip(apvts, params::adaptiveHomeId);
    return true;

  case kLegato: {
    // From the list's old legato entry, unticking moves to one voice with the
    // switch off, and ticking is the switch. Either way the count stops
    // pointing at an entry the menu no longer offers.
    auto *count = apvts.getParameter(params::polyphonyId);
    auto *legatoSwitch = apvts.getParameter(params::legatoId);

    const bool fromOld =
        count != nullptr &&
        choiceIndex(apvts, params::polyphonyId) == params::kLegatoIndex;
    const bool on = legatoOn(apvts);

    if (fromOld)
      count->setValueNotifyingHost(count->convertTo0to1(1.0f));

    if (legatoSwitch != nullptr)
      legatoSwitch->setValueNotifyingHost(on ? 0.0f : 1.0f);

    return true;
  }

  default:
    break;
  }

  if (id > kVoices && id < kVoices + (int)params::kPolyphonyChoices.size()) {
    // A voice count chosen from the old legato entry keeps legato, since that
    // entry was legato as well as one voice.
    if (choiceIndex(apvts, params::polyphonyId) == params::kLegatoIndex)
      if (auto *legatoSwitch = apvts.getParameter(params::legatoId))
        legatoSwitch->setValueNotifyingHost(1.0f);

    choose(apvts, params::polyphonyId, id - kVoices);
    return true;
  }

  if (id >= kBendRange && id <= kBendRange + 24) {
    choose(apvts, params::bendRangeId, id - kBendRange);
    return true;
  }

  // Each list carries an index into its choices rather than a value, since
  // the lists are not contiguous runs of numbers.
  const struct {
    int base, count;
    const char *param;
  } lists[] = {
      {kAftertouchFrom, (int)params::kAftertouchSourceNames.size(),
       params::atSourceId},
      {kTemperament, (int)Temperament::NumTemperaments, params::temperamentId},
      {kRoot, (int)params::kPitchClassNames.size(), params::tuningRootId},
      {kReferencePitch, (int)params::kReferenceHzChoices.size(),
       params::referenceHzId},
      {kSlideTo, params::slideDestChoices.size(), params::slideDestId},
  };

  for (const auto &list : lists)
    if (id >= list.base && id < list.base + list.count) {
      choose(apvts, list.param, id - list.base);
      return true;
    }

  // A time rather than a choice, so the quick settings set it to theirs.
  if (id >= kRetune &&
      id < kRetune + (int)params::kAdaptiveRetuneSeconds.size()) {
    if (auto *p = apvts.getParameter(params::adaptiveRetuneId))
      p->setValueNotifyingHost(p->convertTo0to1(
          (float)params::kAdaptiveRetuneSeconds[(size_t)(id - kRetune)]));
    return true;
  }

  return false;
}

} // namespace ovt::ui::settings
