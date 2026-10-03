// Headless integration tests for the plugin layer: parameter wiring, MIDI
// handling, factory presets, undo, bus layouts and state round-tripping, plus
// the parts of the editor that can be measured rather than looked at.
//
// It does build an editor, for the layout checks, but never gives it a window,
// so it still runs on a CI box with no display. Nothing here may open a
// PopupMenu, which does need one.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "PluginEditor.h"
#include "PluginParameters.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "UI/ChannelStrip.h"
#include "UI/LearnMenu.h"
#include "UI/LookAndFeel.h"
#include "UI/NoiseStrip.h"
#include "UI/ShapeButton.h"
#include "UI/Theme.h"
#include "UI/TopBar.h"
#include "UpdateCheck.h"
#include "dsp/Exact.h"
#include "dsp/OutputStage.h"
#include "dsp/TapeEcho.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

void section(const char *name) { std::printf("\n== %s ==\n", name); }

struct Stats {
  float peak = 0.0f;
  bool finite = true;
};

Stats renderBlocks(OvertoniumProcessor &p, int blocks, int blockSize,
                   juce::MidiBuffer initialMidi = {}) {
  juce::AudioBuffer<float> buffer(2, blockSize);
  Stats s;

  for (int b = 0; b < blocks; ++b) {
    juce::MidiBuffer midi = (b == 0) ? initialMidi : juce::MidiBuffer{};
    buffer.clear();
    p.processBlock(buffer, midi);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
      const auto *d = buffer.getReadPointer(ch);
      for (int n = 0; n < blockSize; ++n) {
        s.finite &= std::isfinite(d[n]);
        s.peak = std::max(s.peak, std::abs(d[n]));
      }
    }
  }

  return s;
}

/// A factory preset by name.
///
/// The menu is alphabetical and gains entries over time, so a test that wants
/// a particular sound has to ask for it rather than for whatever is sitting at
/// a given position today.
int presetIndex(const juce::String &name) {
  const auto at = ovt::presets::names().indexOf(name);
  jassert(at >= 0);
  return juce::jmax(0, at);
}

juce::MidiBuffer noteOnAt(int note, float velocity, int sample) {
  juce::MidiBuffer m;
  m.addEvent(juce::MidiMessage::noteOn(1, note, velocity), sample);
  return m;
}

// -----------------------------------------------------------------------------
//
// The tests stay inside the anonymous namespace the helpers opened. Nothing
// here is called from another translation unit, and a file-scope function with
// no declaration is an external symbol nobody can reach.

/// Sizes an editor to something it can actually be.
///
/// Every call site used to type a pair of numbers, and they went stale the
/// moment the mixer gained a row: 1000 px was eight short of what the strips
/// then asked for, so the fader and its readout laid out past the bottom edge
/// with nothing failing to say so. A plain setSize does not consult the
/// constrainer, so nothing catches it.
///
/// Taken from the editor's own limits instead. The rows above the fader are
/// fixed heights and the fader absorbs the rest, so the smallest legal window
/// lays every row out properly and is the one size that cannot go out of date.
///
/// The width stays a number, since it decides how much mixer is on screen and
/// each caller wants its own. Only the height is derived, because the height is
/// the half that a new row moves.
///
/// @param extraHeight  room above the minimum, for a test that wants the fader
///                     at a comfortable length rather than its shortest.
void sizeEditor(juce::AudioProcessorEditor &editor, int width,
                int extraHeight = 0) {
  const auto *limits = editor.getConstrainer();

  // Every editor here sets resize limits in its constructor, so this is a
  // guard against a future one that forgets rather than a case that happens.
  const auto height =
      limits != nullptr ? limits->getMinimumHeight() + extraHeight : 1040;

  editor.setSize(width, height);
}

/// The bar out of an editor, for the tests that read what it is showing.
///
/// It is a child of the editor's content rather than of the editor, and the
/// depth it sits at is a layout decision that has moved before, so it is
/// searched for rather than reached into.
ovt::ui::TopBar *findTopBar(juce::Component &c) {
  if (auto *bar = dynamic_cast<ovt::ui::TopBar *>(&c))
    return bar;

  for (auto *child : c.getChildren())
    if (auto *found = findTopBar(*child))
      return found;

  return nullptr;
}

/// How many entries every automatable list has.
///
/// A host stores a choice parameter as a value between zero and one, and what
/// that value means depends on how long the list is: with three entries the
/// top of the range is the third, with four it is the fourth. So adding an
/// entry to a list that has already shipped silently moves every automation
/// lane ever written against it. A lane that said "Werckmeister III" comes
/// back saying something else, in somebody's finished piece.
///
/// Adding a parameter is safe and is done freely: it goes on the end, it is
/// identified by its own id, and nothing that exists moves. Adding an entry
/// to one of these is not the same thing, and this is here so that nobody
/// finds that out by shipping it.
///
/// When a list genuinely has to grow, the way out is a new parameter beside
/// the old one rather than a longer list, and a note in the release saying
/// what moved.
void testChoiceParameterCounts(OvertoniumProcessor &p) {
  section("Automatable lists");

  const auto entriesOf = [&p](const juce::String &id) {
    auto *choice =
        dynamic_cast<juce::AudioParameterChoice *>(p.apvts.getParameter(id));

    return choice != nullptr ? choice->choices.size() : -1;
  };

  struct List {
    const char *id;
    int entries;
  };

  // Every global one, by hand, because the number is the point. Reading it
  // from the same constant the parameter is built from would check nothing.
  const List globals[] = {
      {ovt::params::polyphonyId, 8},    {ovt::params::characterId, 6},
      {ovt::params::temperamentId, 6},  {ovt::params::tuningRootId, 12},
      {ovt::params::referenceHzId, 11}, {ovt::params::atSourceId, 3},
      {ovt::params::slideDestId, 3},    {ovt::params::lofiRateId, 8},
      {ovt::params::lofiBitsId, 9},     {ovt::params::echoTypeId, 3},
      {ovt::params::reverbTypeId, 3},
  };

  for (const auto &list : globals)
    check(entriesOf(list.id) == list.entries,
          juce::String(list.id).toStdString() + " offers " +
              std::to_string(list.entries) + " and has " +
              std::to_string(entriesOf(list.id)));

  // And the per-channel ones on every channel, since they are declared in a
  // loop and a loop is where one of thirty-two goes quietly different.
  int pitchShapes = 0, ampShapes = 0;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    pitchShapes +=
        entriesOf(ovt::params::oscParamId(ovt::params::pmShapeSuffix, i)) == 8;
    ampShapes +=
        entriesOf(ovt::params::oscParamId(ovt::params::amShapeSuffix, i)) == 7;
  }

  check(pitchShapes == ovt::kNumHarmonics,
        "every channel's pitch modulator offers 8 shapes (" +
            std::to_string(pitchShapes) + " of " +
            std::to_string(ovt::kNumHarmonics) + ")");

  check(ampShapes == ovt::kNumHarmonics,
        "every channel's amplitude modulator offers 7 (" +
            std::to_string(ampShapes) + " of " +
            std::to_string(ovt::kNumHarmonics) + ")");

  check(entriesOf(ovt::params::noiseParamId(ovt::params::amShapeSuffix)) == 7,
        "and so does the noise channel's");

  // The amplitude has one shape fewer on purpose, so a count that drifted
  // into agreement would be a mistake rather than a tidy-up: there is no
  // second direction for a unipolar square to go in.
  check(entriesOf(ovt::params::oscParamId(ovt::params::pmShapeSuffix, 0)) !=
            entriesOf(ovt::params::oscParamId(ovt::params::amShapeSuffix, 0)),
        "the two modulators deliberately do not offer the same list");
}

void testParameterWiring(OvertoniumProcessor &p) {
  section("Parameter wiring");

  // 23 per partial, 20 global, 18 for the noise channel, 10 for the two master
  // effects. Start phase and the whole pitch modulator are not among the noise
  // channel's, since noise has no pitch: it takes the amp mod shape and not
  // the pitch one, which is why the two counts differ by more than one.
  //
  // Two of the globals are switches over the per-channel modulators rather
  // than controls of their own: whether each of the two is one circuit the
  // keyboard shares. See GlobalParams::ampModInPhase.
  //
  // Three of the trailing four are the echo's type, the reverb's and the
  // output stage's, each of which arrived beside the switch that turns its
  // thing on rather than replacing it: a boolean every saved patch stores and
  // every lane points at cannot become a choice without taking both with it.
  // See params::echoTypeId and params::clipTypeId.
  //
  // The fourth is whether the output stage may look ahead, which is a session
  // setting rather than part of a patch and is the only parameter here that
  // changes what the plugin reports to the host. See params::lookaheadId.
  //
  // And six each for the macros, which are a pool rather than a count: an
  // amount, which is the one a host draws, the row it reaches, the channels
  // of it, how it is shared out, which channel a taper leans on, and the
  // colour it wears. A macro nobody has made points its row at None. See
  // params::kNumMacros.
  const int expected =
      ovt::kNumHarmonics * 23 + 20 + 18 + 10 + 4 + ovt::params::kNumMacros * 6;

  // The behaviour that was there before it became a choice. Asked of the
  // parameter rather than of the tree, so the answer does not depend on what
  // an earlier test left behind.
  if (auto *v = p.apvts.getParameter(ovt::params::oneVoicePerKeyId))
    check(v->getDefaultValue() > 0.5f, "one voice per key defaults to on");
  check(p.getParameters().size() == expected,
        "parameter count is " + std::to_string(p.getParameters().size()) +
            ", expected " + std::to_string(expected));

  // Every ID the audio thread caches must actually exist in the layout. A typo
  // here is a null atomic pointer and a crash on the first block.
  const char *globals[] = {ovt::params::masterGainId, ovt::params::polyphonyId,
                           ovt::params::bendRangeId, ovt::params::phaseResetId,
                           ovt::params::safetyClipId};

  const char *effects[] = {
      ovt::params::echoOnId,     ovt::params::echoMixId,
      ovt::params::echoTimeId,   ovt::params::echoFeedbackId,
      ovt::params::echoAgeId,    ovt::params::reverbOnId,
      ovt::params::reverbMixId,  ovt::params::reverbDecayId,
      ovt::params::reverbDampId, ovt::params::reverbPreDelayId};

  for (auto *id : effects)
    check(p.apvts.getRawParameterValue(id) != nullptr,
          std::string("effect param ") + id);

  for (auto *id : globals)
    check(p.apvts.getRawParameterValue(id) != nullptr,
          std::string("global param ") + id);

  const char *suffixes[] = {
      ovt::params::tuneSuffix,    ovt::params::phaseSuffix,
      ovt::params::pmRateSuffix,  ovt::params::pmDepthSuffix,
      ovt::params::driftSuffix,   ovt::params::strikeSuffix,
      ovt::params::delaySuffix,   ovt::params::attackSuffix,
      ovt::params::decaySuffix,   ovt::params::sustainSuffix,
      ovt::params::swellSuffix,   ovt::params::offLevelSuffix,
      ovt::params::releaseSuffix, ovt::params::amRateSuffix,
      ovt::params::amDepthSuffix, ovt::params::velSuffix,
      ovt::params::atSuffix,      ovt::params::muteSuffix,
      ovt::params::soloSuffix,    ovt::params::volumeSuffix,
      ovt::params::panSuffix};

  bool allPresent = true;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    for (auto *s : suffixes)
      allPresent &= p.apvts.getRawParameterValue(
                        ovt::params::oscParamId(s, i)) != nullptr;

  check(allPresent,
        "all " + std::to_string(ovt::kNumHarmonics * (int)std::size(suffixes)) +
            " per-partial parameters resolve");

  const char *noiseSuffixes[] = {
      ovt::params::colourSuffix,  ovt::params::strikeSuffix,
      ovt::params::delaySuffix,   ovt::params::attackSuffix,
      ovt::params::decaySuffix,   ovt::params::sustainSuffix,
      ovt::params::swellSuffix,   ovt::params::offLevelSuffix,
      ovt::params::releaseSuffix, ovt::params::amRateSuffix,
      ovt::params::amDepthSuffix, ovt::params::velSuffix,
      ovt::params::atSuffix,      ovt::params::muteSuffix,
      ovt::params::soloSuffix,    ovt::params::volumeSuffix,
      ovt::params::panSuffix};

  bool noisePresent = true;
  for (auto *n : noiseSuffixes)
    noisePresent &=
        p.apvts.getRawParameterValue(ovt::params::noiseParamId(n)) != nullptr;

  check(noisePresent, "the noise channel's parameters resolve");

  // The echo age reads across its whole travel even though it never gets below
  // TapeEcho::kMinAge. Worth checking both directions, since a host that shows
  // 0 % and then cannot be typed back to 0 % is worse than one that never
  // hid the floor in the first place.
  if (auto *age = dynamic_cast<juce::RangedAudioParameter *>(
          p.apvts.getParameter(ovt::params::echoAgeId))) {
    const auto textAt = [age](float normalised) {
      return age->getText(normalised, 0).trim().toStdString();
    };

    check(textAt(0.0f) == "0 %",
          "the bottom of the age knob reads 0 %, not " + textAt(0.0f));
    check(textAt(1.0f) == "100 %",
          "the top of the age knob reads 100 %, not " + textAt(1.0f));

    // Round trip. Typing back what was shown has to land where it started.
    check(std::abs(age->getValueForText("0 %") - 0.0f) < 1.0e-6f,
          "0 % typed in is the bottom of the range");
    check(std::abs(age->getValueForText("100 %") - 1.0f) < 1.0e-6f,
          "100 % typed in is the top of the range");

    // And the plain value behind it is still the one the DSP wants.
    check(std::abs(age->convertFrom0to1(0.0f) - ovt::TapeEcho::kMinAge) <
              1.0e-6f,
          "the floor is still under the bottom of the knob");
  } else {
    check(false, "the echo age parameter is ranged");
  }

  // Defaults should give an immediately playable 1/n spectrum.
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto v = p.apvts
                       .getRawParameterValue(ovt::params::oscParamId(
                           ovt::params::volumeSuffix, i))
                       ->load();
    check(std::abs(v - ovt::params::defaultVolumeFor(i)) < 1.0e-4f,
          "default level for partial " + std::to_string(i + 1));
  }
}

/// That the speed a key comes up at survives the trip from the message to the
/// tail, and that the two ways a keyboard says "no release velocity" both
/// leave the instrument exactly as it was.
///
/// The shape of what it does is in the DSP suite. What is here is the wiring
/// and the one thing that wiring can get wrong: reading a zero as the softest
/// possible lift rather than as no information.
void testReleaseVelocity(OvertoniumProcessor &p) {
  section("Release velocity");

  p.applyFactoryPreset(presetIndex("Init"));

  const auto put = [&p](const char *suffix, float value) {
    auto *param = p.apvts.getParameter(ovt::params::oscParamId(suffix, 0));

    if (param != nullptr)
      param->setValueNotifyingHost(param->convertTo0to1(value));

    return param != nullptr;
  };

  // Init is a plucky patch with an 8 ms release, so on its own terms there is
  // no tail to measure. Half a second of one, a short decay so the note is
  // sitting at its sustain rather than still on the way down when the key
  // comes up, and a sustain of 0.4, which leaves a hard lift somewhere to go:
  // the envelope stops at one, so doubling 0.6 would only reach the ceiling.
  const bool ready = put(ovt::params::releaseSuffix, 0.5f) &&
                     put(ovt::params::decaySuffix, 0.05f) &&
                     put(ovt::params::sustainSuffix, 0.4f);

  check(ready, "the first partial has a release and a sustain to set");
  if (!ready)
    return;

  const auto tailAfter = [&p](const juce::MidiMessage &release) {
    p.prepareToPlay(48000.0, 512);
    renderBlocks(p, 20, 512, noteOnAt(60, 0.9f, 0));

    juce::MidiBuffer up;
    up.addEvent(release, 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    p.processBlock(buffer, up);

    // A hundred milliseconds in, not the block the key came up in. That one is
    // still at the sustain whatever the lift was, since the swell into the
    // tail takes a few milliseconds and the block is ten.
    renderBlocks(p, 8, 512);

    juce::MidiBuffer none;
    buffer.clear();
    p.processBlock(buffer, none);

    return buffer.getMagnitude(0, 512);
  };

  const auto soft =
      tailAfter(juce::MidiMessage::noteOff(1, 60, (juce::uint8)1));
  const auto even =
      tailAfter(juce::MidiMessage::noteOff(1, 60, (juce::uint8)64));
  const auto hard =
      tailAfter(juce::MidiMessage::noteOff(1, 60, (juce::uint8)127));

  std::printf("  the first block of the tail: %.4f lifted softly, %.4f evenly, "
              "%.4f hard\n",
              soft, even, hard);

  // Half and double, give or take the few milliseconds of swell that a scaled
  // lift passes through and an even one skips. What the levels are exactly is
  // pinned in the DSP suite, where an envelope can be read directly.
  check(soft / even > 0.45f && soft / even < 0.62f,
        "a soft lift leaves about half the tail (" +
            std::to_string(soft / even) + " of it)");

  check(hard / even > 1.85f && hard / even < 2.25f,
        "and a hard one about twice (" + std::to_string(hard / even) + ")");

  // ---- and the two ways of saying nothing ---------------------------------

  const auto zero =
      tailAfter(juce::MidiMessage::noteOff(1, 60, (juce::uint8)0));

  check(ovt::exactly(zero, even),
        "a note-off carrying zero is read as no information rather than as the "
        "softest lift there is");

  const auto asNoteOn =
      tailAfter(juce::MidiMessage::noteOn(1, 60, (juce::uint8)0));

  check(ovt::exactly(asNoteOn, even),
        "and so is the note-on of velocity zero that most keyboards send "
        "instead of a note-off");

  p.applyFactoryPreset(presetIndex("Init"));
}

/// That choosing a character brings its bus stage with it, and that the
/// latency this costs is told to the host and never moves.
///
/// There is nothing to set: how hard each circuit is run is a property of the
/// part, so the only way in is the character. What each one does to a signal
/// is measured in the DSP suite, where a spectrum can be read.
void testBusStageFollowsTheCharacter(OvertoniumProcessor &p) {
  section("The bus stage");

  auto *character = p.apvts.getParameter(ovt::params::characterId);

  check(character != nullptr, "the character is there to choose");
  if (character == nullptr)
    return;

  const auto renderLoud = [&p, character](ovt::Character c) {
    character->setValueNotifyingHost(character->convertTo0to1((float)(int)c));

    p.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi = noteOnAt(48, 1.0f, 0);

    std::vector<float> out;

    for (int b = 0; b < 12; ++b) {
      buffer.clear();
      p.processBlock(buffer, midi);
      midi.clear();

      const auto *d = buffer.getReadPointer(0);
      out.insert(out.end(), d, d + 512);
    }

    return out;
  };

  const auto idle = p.getLatencySamples();

  const auto pure = renderLoud(ovt::Character::Pure);
  const auto valve = renderLoud(ovt::Character::Valve);
  const auto bulb = renderLoud(ovt::Character::Bulb);

  const auto differs = [](const std::vector<float> &a,
                          const std::vector<float> &b) {
    double worst = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
      worst = std::max(worst, std::abs((double)a[i] - (double)b[i]));

    return worst;
  };

  check(differs(pure, valve) > 1.0e-4,
        "a character brings a summing circuit with it (" +
            std::to_string(differs(pure, valve)) + ")");

  check(differs(valve, bulb) > 1.0e-4,
        "and they are not the same circuit (" +
            std::to_string(differs(valve, bulb)) + ")");

  // Every character, including the one that has none, costs the same few
  // samples, so a preset cannot make a host re-plan its graph. The output
  // stage's lookahead is in the figure too and is just as fixed: it is paid
  // on every clip type and with the clipper switched off.
  const auto expected =
      ovt::BusDrive::kLatency + ovt::OutputStage::lookaheadSamples(48000.0);

  check(idle == expected, "both stages' latency is reported (" +
                              std::to_string(idle) + " samples, expected " +
                              std::to_string(expected) + ")");

  check(p.getLatencySamples() == idle,
        "and does not move when the character does (" +
            std::to_string(p.getLatencySamples()) + ")");

  // The other half of the same rule. The lookahead belongs to one of the five
  // types and is paid by all of them, so switching the stage off entirely has
  // to leave the figure alone.
  auto *clip = p.apvts.getParameter(ovt::params::safetyClipId);
  clip->setValueNotifyingHost(0.0f);
  p.prepareToPlay(48000.0, 512);

  check(p.getLatencySamples() == idle,
        "nor when the output stage is switched off (" +
            std::to_string(p.getLatencySamples()) + ")");

  clip->setValueNotifyingHost(1.0f);
  p.prepareToPlay(48000.0, 512);

  // The one thing that is allowed to move it, and the reason it is allowed:
  // it sits in Settings, so no preset and nothing on the bar can reach it.
  // Giving the window up gives the whole of the output stage's share back.
  auto *ahead = p.apvts.getParameter(ovt::params::lookaheadId);

  check(ahead != nullptr, "the lookahead has a switch");

  if (ahead != nullptr) {
    check(ovt::params::isSessionParam(ovt::params::lookaheadId),
          "which is a session setting, so no preset can move the latency");

    ahead->setValueNotifyingHost(0.0f);
    p.prepareToPlay(48000.0, 512);

    check(p.getLatencySamples() == ovt::BusDrive::kLatency,
          "refusing it leaves only the bus stage (" +
              std::to_string(p.getLatencySamples()) + " samples)");

    ahead->setValueNotifyingHost(1.0f);
    p.prepareToPlay(48000.0, 512);

    check(p.getLatencySamples() == idle,
          "and asking for it again restores it (" +
              std::to_string(p.getLatencySamples()) + ")");
  }

  p.applyFactoryPreset(presetIndex("Init"));
}

void testRendering(OvertoniumProcessor &p) {
  section("Rendering and MIDI");

  p.prepareToPlay(48000.0, 512);

  const auto silence = renderBlocks(p, 4, 512);
  check(silence.finite, "idle output is finite");
  check(silence.peak < 1.0e-6f, "idle output is silent");

  const auto sounding = renderBlocks(p, 20, 512, noteOnAt(60, 0.9f, 0));
  check(sounding.finite, "note output is finite");
  check(sounding.peak > 0.01f,
        "note is audible (peak " + std::to_string(sounding.peak) + ")");
  check(sounding.peak <= 1.0f, "note output stays within full scale");
  check(p.getActiveVoiceCount() == 1, "one voice is sounding");

  juce::MidiBuffer off;
  off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
  juce::AudioBuffer<float> buffer(2, 512);
  p.processBlock(buffer, off);

  renderBlocks(p, 200, 512);
  check(p.getActiveVoiceCount() == 0, "voice frees after release");

  // Sample-accurate placement: a note at frame 256 must leave the first half
  // silent.
  p.prepareToPlay(48000.0, 512);
  juce::AudioBuffer<float> split(2, 512);
  split.clear();
  auto midi = noteOnAt(72, 1.0f, 256);
  p.processBlock(split, midi);

  float firstHalf = 0.0f, secondHalf = 0.0f;
  for (int n = 0; n < 256; ++n)
    firstHalf = std::max(firstHalf, std::abs(split.getSample(0, n)));
  for (int n = 256; n < 512; ++n)
    secondHalf = std::max(secondHalf, std::abs(split.getSample(0, n)));

  check(firstHalf < 1.0e-7f, "nothing sounds before the note-on timestamp");
  check(secondHalf > 0.0f, "the note starts at its timestamp");

  p.reset();
  renderBlocks(p, 400, 512);
}

/// That a controller can be bound to a control, that binding one does not
/// cost the instrument something it was already using, and that the map
/// survives the session being saved and opened again.
///
/// The map is reached directly rather than through the menus, since what a
/// menu does is call these. The menu itself is checked by the item it offers.
void testMidiLearnBindsControllers(OvertoniumProcessor &p) {
  section("MIDI learn");

  p.midiLearn.clear();
  p.applyFactoryPreset(presetIndex("Init"));
  p.prepareToPlay(48000.0, 256);

  auto *target = dynamic_cast<juce::RangedAudioParameter *>(
      p.apvts.getParameter(ovt::params::wobbleId));

  check(target != nullptr, "there is a control to bind");
  if (target == nullptr)
    return;

  const auto send = [&p](int controller, int value) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, controller, value), 0);
    renderBlocks(p, 2, 256, midi);
  };

  check(p.midiLearn.controllerFor(target) < 0, "and nothing is bound to start");

  // ---- the reserved ones are refused -------------------------------------
  //
  // Armed and then handed the slide axis, which is the one most likely to be
  // touched by accident on an MPE controller, since it moves whenever a
  // finger does.
  p.midiLearn.arm(target);
  send(74, 100);

  check(p.midiLearn.armed() == target,
        "the slide axis does not get taken by a knob that is waiting");
  check(p.midiLearn.controllerFor(target) < 0, "and binds nothing");

  send(1, 100);
  check(p.midiLearn.armed() == target, "nor does the mod wheel");

  // ---- an ordinary one is taken ------------------------------------------
  send(20, 64);

  check(p.midiLearn.armed() == nullptr, "an ordinary controller is learned");
  check(p.midiLearn.controllerFor(target) == 20,
        "and is what moves that control now (" +
            std::to_string(p.midiLearn.controllerFor(target)) + ")");

  // The message that bound it does not also move the control, which would be
  // a jump at the moment somebody is looking at it.
  const auto afterBinding = target->getValue();

  send(20, 127);
  check(target->getValue() > afterBinding + 0.1f,
        "the next message moves it (" + std::to_string(target->getValue()) +
            ")");

  send(20, 0);
  check(target->getValue() < 0.01f, "and all the way back down (" +
                                        std::to_string(target->getValue()) +
                                        ")");

  // ---- it survives the session -------------------------------------------
  juce::MemoryBlock saved;
  p.getStateInformation(saved);

  p.midiLearn.clear();
  check(p.midiLearn.controllerFor(target) < 0, "forgetting works");

  p.setStateInformation(saved.getData(), (int)saved.getSize());

  auto *afterLoad = dynamic_cast<juce::RangedAudioParameter *>(
      p.apvts.getParameter(ovt::params::wobbleId));

  check(p.midiLearn.controllerFor(afterLoad) == 20,
        "and the binding comes back with the session (" +
            std::to_string(p.midiLearn.controllerFor(afterLoad)) + ")");

  // ---- and a preset cannot touch it --------------------------------------
  //
  // The map describes the desk rather than the sound, so loading a patch over
  // it would be rearranging somebody's hardware.
  p.applyFactoryPreset(presetIndex("Big Saw"));

  check(p.midiLearn.controllerFor(afterLoad) == 20,
        "which a preset does not disturb");

  // ---- and it works where it would be easiest to break ------------------
  //
  // With MPE on, the parser is handed every message and only the mod wheel, a
  // program change and all-sound-off fall through to the ordinary handler. A
  // binding checked after that would be dead on exactly the controller this
  // instrument is usually played from. Everything above this point passes
  // whether the check runs before the parser or after it, because MPE is off
  // by default, which is what makes this case worth its own lines.
  if (auto *mpe = p.apvts.getParameter(ovt::params::mpeId)) {
    mpe->setValueNotifyingHost(1.0f);
    p.prepareToPlay(48000.0, 256);

    // Let go of the one it already has first. Two controllers may point at
    // one control, and controllerFor answers with the lowest of them, so
    // leaving CC 20 bound would have this reading 20 whatever CC 21 did.
    p.midiLearn.forget(afterLoad);
    p.midiLearn.arm(afterLoad);
    send(21, 64);

    check(p.midiLearn.controllerFor(afterLoad) == 21,
          "a controller is still learned with MPE on (" +
              std::to_string(p.midiLearn.controllerFor(afterLoad)) + ")");

    send(21, 127);
    const auto high = afterLoad->getValue();

    send(21, 0);

    check(high > afterLoad->getValue() + 0.1f,
          "and still moves the control it was bound to (" +
              std::to_string(high) + " then " +
              std::to_string(afterLoad->getValue()) + ")");

    mpe->setValueNotifyingHost(0.0f);
    p.prepareToPlay(48000.0, 256);
  }

  // ---- the menu says so, and the controls answer to it -------------------
  //
  // The map above is reached directly, which proves nothing about whether a
  // right-click can get at it. Two things have to hold: a control has to know
  // which parameter it moves, and the menu has to offer the item.
  {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

    check(editor != nullptr, "the editor opens");

    if (editor != nullptr) {
      std::function<const juce::Component *(const juce::Component &,
                                            const juce::String &)>
          findTagged =
              [&findTagged](
                  const juce::Component &c,
                  const juce::String &wanted) -> const juce::Component * {
        for (auto *child : c.getChildren()) {
          if (ovt::ui::learn::parameterIdAt(child) == wanted)
            return child;

          if (auto *found = findTagged(*child, wanted))
            return found;
        }

        return nullptr;
      };

      // A fader on the first channel, which is tagged where its attachment is
      // made, and the master fader on the bar, which is tagged somewhere else
      // entirely. One of each, so a tag added in only one of the two places
      // fails here.
      const auto fader = ovt::params::oscParamId(ovt::params::volumeSuffix, 0);

      check(findTagged(*editor, fader) != nullptr,
            "a channel fader knows which parameter it moves");
      check(findTagged(*editor, ovt::params::masterGainId) != nullptr,
            "and so does the master fader on the bar");

      // ---- and a waiting control says so -------------------------------
      //
      // Arming happens through the map, the marker is hung by the window on
      // its timer, and the look and feel is what draws it. This checks the
      // middle of those three: that the window finds the right control and
      // marks it, and takes the mark off again when the wait ends.
      auto *wobble = dynamic_cast<juce::RangedAudioParameter *>(
          p.apvts.getParameter(ovt::params::wobbleId));

      const auto markOn = [&](const juce::String &id) {
        auto *c = ovt::ui::learn::controlFor(*editor, id);
        return c != nullptr &&
               (bool)c->getProperties().getWithDefault("learnArmed", false);
      };

      check(!markOn(ovt::params::wobbleId), "nothing is marked to start");

      p.midiLearn.arm(wobble);
      editor->followArmedControl();

      check(markOn(ovt::params::wobbleId),
            "the control a controller is being waited for is marked");

      // Moving the wait to another control takes the mark with it, which is
      // what right-clicking a second control does.
      p.midiLearn.arm(dynamic_cast<juce::RangedAudioParameter *>(
          p.apvts.getParameter(ovt::params::stretchId)));
      editor->followArmedControl();

      check(!markOn(ovt::params::wobbleId) && markOn(ovt::params::stretchId),
            "and only one is ever marked at a time");

      p.midiLearn.arm(nullptr);
      editor->followArmedControl();

      check(!markOn(ovt::params::stretchId),
            "and the mark goes when the wait ends");
    }
  }

  {
    auto *target2 = dynamic_cast<juce::RangedAudioParameter *>(
        p.apvts.getParameter(ovt::params::stretchId));

    const auto itemsIn = [](juce::PopupMenu &menu) {
      std::string found;

      for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        found += it.getItem().text.toStdString() + "|";

      return found;
    };

    p.midiLearn.forget(target2);

    juce::PopupMenu fresh;
    ovt::ui::learn::appendItems(fresh, p.midiLearn, target2);

    check(itemsIn(fresh).find("MIDI Learn") != std::string::npos,
          "an unbound control is offered MIDI Learn (" + itemsIn(fresh) + ")");

    p.midiLearn.bind(22, target2);

    juce::PopupMenu bound;
    ovt::ui::learn::appendItems(bound, p.midiLearn, target2);

    check(itemsIn(bound).find("Forget CC 22") != std::string::npos,
          "a bound one is offered the way back (" + itemsIn(bound) + ")");

    p.midiLearn.arm(target2);

    juce::PopupMenu waiting;
    ovt::ui::learn::appendItems(waiting, p.midiLearn, target2);

    check(itemsIn(waiting).find("Stop waiting") != std::string::npos,
          "and one that is waiting can be told to stop (" + itemsIn(waiting) +
              ")");

    p.midiLearn.arm(nullptr);
  }

  p.midiLearn.clear();
  p.applyFactoryPreset(presetIndex("Init"));
}

/// That one automatable parameter moves a whole row, which is what issue #24
/// asked for: a LINK drag across TUNE moves 32 parameters and a host catches
/// only the last touched, so a relationship you can edit cannot be automated.
///
/// Read off the snapshot rather than off the sound, because what a macro does
/// is offset what the patch says on the way to the engine. The sound follows
/// from that and is a much blunter instrument for telling 32 channels apart.
void testMacrosMoveAWholeRow(OvertoniumProcessor &p) {
  section("Macros");

  p.applyFactoryPreset(presetIndex("Init"));
  p.prepareToPlay(48000.0, 256);

  const auto set = [&p](const juce::String &id, float plain) {
    if (auto *q = p.apvts.getParameter(id))
      q->setValueNotifyingHost(q->convertTo0to1(plain));
  };

  const auto snapshot = [&p] {
    ovt::SynthParams out;
    p.parameters().snapshot(out, 0.0f);
    return out;
  };

  // The row the issue names, and the one a macro is most wanted on. One
  // rather than zero: the list opens with None, which is an unmade macro.
  const int tuneRow = 1;

  const auto resting = snapshot();

  // What the parameters say before anything is asked of them, so that
  // "the macro wrote nothing" is measured against the patch rather than
  // against zero.
  std::vector<float> before;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    before.push_back(
        p.apvts
            .getParameter(ovt::params::oscParamId(ovt::params::tuneSuffix, i))
            ->getValue());

  set(ovt::params::macroRowId(0), (float)tuneRow);
  set(ovt::params::macroScopeId(0), 0.0f);
  set(ovt::params::macroCurveId(0), 0.0f);

  // Downward, because Init leaves TUNE at the top of its range: pushing up
  // would clamp on every channel and the test would read "nothing moved"
  // while the macro was working perfectly.
  set(ovt::params::macroAmountId(0), -0.25f);

  const auto moved = snapshot();

  int shifted = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(moved.osc[(size_t)i].tuneBlend -
                 resting.osc[(size_t)i].tuneBlend) > 1.0e-5f)
      ++shifted;

  check(shifted == ovt::kNumHarmonics, "one macro moves the whole row (" +
                                           std::to_string(shifted) +
                                           " of 32 channels)");

  // The thing that makes it a macro rather than a drag: the parameters it
  // offsets have not moved, so the patch is exactly where it was and the host
  // has one lane rather than thirty-two.
  int written = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    auto *q = p.apvts.getParameter(
        ovt::params::oscParamId(ovt::params::tuneSuffix, i));

    if (q != nullptr && std::abs(q->getValue() - before[(size_t)i]) > 1.0e-6f)
      ++written;
  }

  check(written == 0, "and writes none of the parameters it moves (" +
                          std::to_string(written) + " written)");

  // ---- the scope decides who it reaches ----------------------------------
  set(ovt::params::macroScopeId(0), (float)(int)ovt::params::MacroScope::Odd);

  const auto odd = snapshot();

  int oddMoved = 0, evenMoved = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const bool differs = std::abs(odd.osc[(size_t)i].tuneBlend -
                                  resting.osc[(size_t)i].tuneBlend) > 1.0e-5f;

    if (differs)
      (((i + 1) % 2) == 1 ? oddMoved : evenMoved)++;
  }

  check(oddMoved == 16 && evenMoved == 0,
        "the scope decides which channels it reaches (" +
            std::to_string(oddMoved) + " odd, " + std::to_string(evenMoved) +
            " even)");

  // ---- and the curve decides how much each one takes ---------------------
  set(ovt::params::macroScopeId(0), 0.0f);
  set(ovt::params::macroCurveId(0), (float)(int)ovt::params::MacroCurve::Taper);

  const auto tapered = snapshot();

  const auto shiftAt = [&](const ovt::SynthParams &s, int i) {
    return s.osc[(size_t)i].tuneBlend - resting.osc[(size_t)i].tuneBlend;
  };

  check(std::abs(shiftAt(tapered, 0)) > std::abs(shiftAt(tapered, 16)) &&
            std::abs(shiftAt(tapered, 16)) > std::abs(shiftAt(tapered, 31)),
        "a tapered macro moves the fundamental most and the top least");

  // Held to the curve LINK draws, so the two cannot drift into meaning
  // different things by the same name. A macro anchors at the fundamental,
  // which is what the 0 is.
  int disagreed = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto theirs =
        ovt::ui::linkCurveWeight(ovt::ui::LinkCurve::Taper, i, 0);
    const auto mine = shiftAt(tapered, i) / shiftAt(tapered, 0);

    if (std::abs(theirs - mine) > 1.0e-4f)
      ++disagreed;
  }

  check(disagreed == 0, "and takes the same share LINK would give it (" +
                            std::to_string(disagreed) + " channels differ)");

  // ---- a macro at rest is not in the way ---------------------------------
  set(ovt::params::macroAmountId(0), 0.0f);

  const auto back = snapshot();

  int stillMoved = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(back.osc[(size_t)i].tuneBlend -
                 resting.osc[(size_t)i].tuneBlend) > 1.0e-9f)
      ++stillMoved;

  check(stillMoved == 0, "and leaves nothing behind when it returns to zero");

  // ---- a macro nobody has made does nothing ------------------------------
  //
  // Which is how one is removed: the row goes back to None and the amount is
  // left wherever the fader was. A macro that went on working after being
  // taken away would be the worst of both.
  set(ovt::params::macroAmountId(0), -0.25f);
  set(ovt::params::macroRowId(0), 0.0f);

  const auto unmade = snapshot();

  int byUnmade = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(unmade.osc[(size_t)i].tuneBlend -
                 resting.osc[(size_t)i].tuneBlend) > 1.0e-9f)
      ++byUnmade;

  check(byUnmade == 0,
        "a macro with no row does nothing, whatever its amount says (" +
            std::to_string(byUnmade) + " channels moved)");

  // ---- and an interval scope reaches that interval -----------------------
  //
  // The fifth, which is harmonics 3, 6, 12 and 24. Counted against the table
  // rather than against a list written here, so this cannot drift from what
  // the mixer calls those channels.
  set(ovt::params::macroRowId(0), (float)tuneRow);
  set(ovt::params::macroScopeId(0),
      (float)((int)ovt::params::MacroScope::Interval + 7));

  const auto fifths = snapshot();

  int reached = 0, strayed = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const bool took = std::abs(fifths.osc[(size_t)i].tuneBlend -
                               resting.osc[(size_t)i].tuneBlend) > 1.0e-5f;
    const bool isFifth = ovt::harmonicTable()[(size_t)i].pitchClass == 7;

    if (took && isFifth)
      ++reached;
    if (took != isFifth)
      ++strayed;
  }

  check(reached == 4 && strayed == 0,
        "an interval scope reaches exactly that interval (" +
            std::to_string(reached) + " fifths, " + std::to_string(strayed) +
            " wrong)");

  // ---- and a driven control wears the macro's colour ---------------------
  //
  // Which control belongs to which macro is a fact about all eight of them,
  // so the window works it out and the strips are told. Read off the control
  // itself, since what the look and feel draws is whatever colour it has been
  // given: a macro taking a control over is the control being told it is a
  // different colour.
  {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

    check(editor != nullptr, "the editor opens");

    if (editor != nullptr) {
      // Read off the control, which is where the look and feel reads it: the
      // macro's colour is carried beside the control's own rather than
      // replacing it, so the pointer can stay the channel's.
      const auto colourOf = [&](int channel) {
        auto *c = ovt::ui::learn::controlFor(
            *editor, ovt::params::oscParamId(ovt::params::tuneSuffix, channel));

        if (c == nullptr)
          return juce::Colours::transparentBlack;

        const auto said = c->getProperties().getWithDefault("macroColour", {});

        return said.isVoid() ? juce::Colours::transparentBlack
                             : juce::Colour((juce::uint32)(int)said);
      };

      const auto own = colourOf(2);

      set(ovt::params::macroRowId(0), (float)tuneRow);
      set(ovt::params::macroScopeId(0),
          (float)((int)ovt::params::MacroScope::Interval + 7));
      set(ovt::params::macroColourId(0), 1.0f);
      editor->followMacroTints();

      const auto wanted = ovt::params::macroColour(1);

      // Harmonic 3 is a fifth and harmonic 2 is not, so one takes the colour
      // and the other keeps its own.
      check(colourOf(2) == wanted,
            "a control a macro drives carries the macro's colour");
      check(colourOf(1) != wanted, "and one it does not reach carries none");

      set(ovt::params::macroRowId(0), 0.0f);
      editor->followMacroTints();

      check(colourOf(2) == own,
            "and letting go takes the macro's colour off again");

      // ---- and the ring shows what the engine is playing ----------------
      //
      // The ring is worked out by the window and the offset by the snapshot,
      // from the same shared arithmetic. If those two ever disagreed the
      // knob would be showing a value nothing is playing, which is worse
      // than showing nothing: it would be a lie told confidently.
      set(ovt::params::macroRowId(0), (float)tuneRow);
      set(ovt::params::macroScopeId(0), 0.0f);
      set(ovt::params::macroCurveId(0),
          (float)(int)ovt::params::MacroCurve::Taper);
      set(ovt::params::macroAmountId(0), -0.4f);
      editor->followMacroTints();

      ovt::SynthParams played;
      p.parameters().snapshot(played, 0.0f);

      int mismatched = 0;
      for (int i = 0; i < ovt::kNumHarmonics; ++i) {
        auto *c = ovt::ui::learn::controlFor(
            *editor, ovt::params::oscParamId(ovt::params::tuneSuffix, i));

        if (c == nullptr)
          continue;

        const auto shown = (float)(double)c->getProperties().getWithDefault(
            "macroResult", -1.0);

        auto *q =
            dynamic_cast<juce::RangedAudioParameter *>(p.apvts.getParameter(
                ovt::params::oscParamId(ovt::params::tuneSuffix, i)));

        if (q == nullptr)
          continue;

        // Both as a proportion of the control's travel, which is what the
        // ring is drawn from.
        const auto engine = q->convertTo0to1(played.osc[(size_t)i].tuneBlend);

        if (std::abs(shown - engine) > 1.0e-3f)
          ++mismatched;
      }

      check(mismatched == 0,
            "the ring shows the value the engine is playing (" +
                std::to_string(mismatched) + " channels differ)");

      // ---- and follows the control, not only the macro ------------------
      //
      // The result is the patch's value plus the macro's offset, so turning
      // a knob moves it while every macro stands still. There was a
      // signature here that skipped the pass when no macro had moved, and
      // the ring sat where it had been left until a macro was touched.
      {
        auto *first =
            dynamic_cast<juce::RangedAudioParameter *>(p.apvts.getParameter(
                ovt::params::oscParamId(ovt::params::tuneSuffix, 0)));

        auto *control = ovt::ui::learn::controlFor(
            *editor, ovt::params::oscParamId(ovt::params::tuneSuffix, 0));

        if (first != nullptr && control != nullptr) {
          const auto ringAt = [control] {
            return (float)(double)control->getProperties().getWithDefault(
                "macroResult", -1.0);
          };

          const auto was = ringAt();

          // The control alone, with the macro left exactly where it is.
          first->setValueNotifyingHost(first->getValue() > 0.5f ? 0.1f : 0.9f);
          editor->followMacroTints();

          check(std::abs(ringAt() - was) > 0.05f,
                "the ring follows the control being turned, not only the "
                "macro (" +
                    std::to_string(was) + " to " + std::to_string(ringAt()) +
                    ")");
        }
      }

      set(ovt::params::macroAmountId(0), 0.0f);
      set(ovt::params::macroRowId(0), 0.0f);
      set(ovt::params::macroCurveId(0), 0.0f);
    }
  }

  set(ovt::params::macroRowId(0), 0.0f);
  set(ovt::params::macroAmountId(0), 0.0f);
  set(ovt::params::macroScopeId(0), 0.0f);
  set(ovt::params::macroCurveId(0), 0.0f);

  p.applyFactoryPreset(presetIndex("Init"));
}

void testPresets(OvertoniumProcessor &p) {
  section("Factory presets");

  const auto names = ovt::presets::names();

  // Counted rather than spelt, so adding one does not leave a number here
  // claiming otherwise. Every name has to have a case of its own, which is
  // what the loop below is really testing: a name with no case applies
  // nothing and the preset comes out silent.
  check(names.size() > 0,
        "there are factory presets (" + std::to_string(names.size()) + ")");

  bool unique = true;
  for (int i = 0; i < names.size(); ++i)
    for (int j = i + 1; j < names.size(); ++j)
      unique &= names[i] != names[j];

  check(unique, "and no two share a name");

  for (int i = 0; i < names.size(); ++i) {
    ovt::presets::apply(p.apvts, i);
    p.prepareToPlay(48000.0, 512);

    // Slow Pad and Shimmer have multi-second attacks, so give them time to open
    // up.
    const auto stats = renderBlocks(p, 400, 512, noteOnAt(57, 1.0f, 0));

    check(stats.finite, names[i].toStdString() + ": output is finite");
    check(stats.peak > 0.005f, names[i].toStdString() +
                                   ": produces sound (peak " +
                                   std::to_string(stats.peak) + ")");
    check(stats.peak <= 1.001f,
          names[i].toStdString() + ": stays within full scale");

    p.reset();
    renderBlocks(p, 4, 512);
  }

  ovt::presets::apply(p.apvts, presetIndex("Init")); // back to Init
}

void testAftertouchMidi(OvertoniumProcessor &p) {
  section("Aftertouch over MIDI");

  ovt::presets::apply(p.apvts, presetIndex("Init"));

  // Park every fader at silence and let pressure be the only way in.
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    p.apvts.getParameter(ovt::params::oscParamId(ovt::params::volumeSuffix, i))
        ->setValueNotifyingHost(0.0f);
    p.apvts.getParameter(ovt::params::oscParamId(ovt::params::atSuffix, i))
        ->setValueNotifyingHost(i < 4 ? 1.0f : 0.0f);
  }

  p.prepareToPlay(48000.0, 512);

  const auto silent = renderBlocks(p, 20, 512, noteOnAt(57, 1.0f, 0));
  check(silent.peak < 1.0e-4f,
        "faders at zero stay silent while the key is unpressed");

  juce::MidiBuffer press;
  press.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);
  const auto pressed = renderBlocks(p, 20, 512, press);
  check(pressed.finite, "aftertouch output is finite");
  check(pressed.peak > 0.01f, "channel pressure fades the partials in (peak " +
                                  std::to_string(pressed.peak) + ")");

  juce::MidiBuffer release;
  release.addEvent(juce::MidiMessage::channelPressureChange(1, 0), 0);
  const auto released = renderBlocks(p, 40, 512, release);
  check(released.peak < pressed.peak, "letting go fades them back out");

  // Polyphonic aftertouch has to reach the voice holding that note.
  juce::MidiBuffer poly;
  poly.addEvent(juce::MidiMessage::aftertouchChange(1, 57, 127), 0);
  const auto polyPressed = renderBlocks(p, 20, 512, poly);
  check(polyPressed.peak > 0.01f, "polyphonic aftertouch reaches the voice");

  poly.clear();
  poly.addEvent(juce::MidiMessage::aftertouchChange(1, 57, 0), 0);
  renderBlocks(p, 40, 512, poly);

  // ---- the mod wheel, which is what most keyboards actually have ----------
  auto *source = p.apvts.getParameter(ovt::params::atSourceId);

  const auto setSource = [&](ovt::params::AftertouchSource s) {
    source->setValueNotifyingHost(source->convertTo0to1((float)s));
  };

  const auto sendCC = [&](int value) {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::controllerEvent(1, 1, value), 0);
    return renderBlocks(p, 20, 512, m);
  };

  check((int)std::lround(
            p.apvts.getRawParameterValue(ovt::params::atSourceId)->load()) ==
            (int)ovt::params::AftertouchSource::Either,
        "either source is the default, so a wheel works out of the box");

  const auto wheelUp = sendCC(127);
  check(wheelUp.peak > 0.01f, "the mod wheel fades the partials in (peak " +
                                  std::to_string(wheelUp.peak) + ")");

  const auto wheelDown = sendCC(0);
  check(wheelDown.peak < wheelUp.peak, "and letting it fall takes them out");

  juce::MidiBuffer pressOn, pressOff;
  pressOn.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);
  pressOff.addEvent(juce::MidiMessage::channelPressureChange(1, 0), 0);

  // Set to pressure only, the wheel must do nothing at all.
  setSource(ovt::params::AftertouchSource::ChannelPressure);
  const auto ignored = sendCC(127);
  check(ignored.peak < 1.0e-4f,
        "set to channel pressure, the wheel is ignored (peak " +
            std::to_string(ignored.peak) + ")");

  // ...and pressure still gets through on that setting.
  check(renderBlocks(p, 20, 512, pressOn).peak > 0.01f,
        "while channel pressure still does");

  // Both back to rest before the mirror image. A wheel left up at 127 would
  // make the next pair of checks pass for entirely the wrong reason.
  sendCC(0);
  renderBlocks(p, 40, 512, pressOff);

  setSource(ovt::params::AftertouchSource::ModWheel);
  check(renderBlocks(p, 20, 512, pressOn).peak < 1.0e-4f,
        "set to the wheel, channel pressure is ignored");

  renderBlocks(p, 40, 512, pressOff);
  check(sendCC(127).peak > 0.01f, "while the wheel does the work");

  sendCC(0);
  setSource(ovt::params::AftertouchSource::Either);

  // CC1 must not have eaten the pedal or the panic messages on its way past.
  juce::MidiBuffer pedal;
  pedal.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
  renderBlocks(p, 2, 512, pedal);

  juce::MidiBuffer noteOff;
  noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
  renderBlocks(p, 20, 512, noteOff);

  check(p.getActiveVoiceCount() == 1,
        "the sustain pedal still reaches the engine past the wheel");

  juce::MidiBuffer pedalUp;
  pedalUp.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
  renderBlocks(p, 200, 512, pedalUp);

  check(p.getActiveVoiceCount() == 0, "and letting it up releases the note");

  p.reset();
  renderBlocks(p, 4, 512);
  ovt::presets::apply(p.apvts, presetIndex("Init"));
}

/// Notes arriving on a channel each, which is what MPE is.
///
/// The engine's own handling of that is covered by the DSP suite. What is
/// checked here is the routing: whether a message on channel N reaches the
/// voice it was meant for and no other, whether an ordinary keyboard still
/// plays with the setting on, and whether it plays exactly as it always did
/// with the setting off.
void testMpe(OvertoniumProcessor &p) {
  section("MPE");

  const auto setParam = [&p](const juce::String &id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  // One partial, held, with nothing downstream that would blur a pitch
  // measurement.
  ovt::presets::apply(p.apvts, presetIndex("Init"));
  setParam(ovt::params::echoOnId, 0.0f);
  setParam(ovt::params::reverbOnId, 0.0f);
  setParam(ovt::params::wobbleId, 0.0f);
  setParam(ovt::params::masterGainId, 1.0f);
  setParam(ovt::params::bendRangeId, 2.0f);

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto vol = i == 0 ? 1.0f : 0.0f;
    setParam(ovt::params::oscParamId(ovt::params::volumeSuffix, i), vol);
    setParam(ovt::params::oscParamId(ovt::params::atSuffix, i), 0.0f);
    setParam(ovt::params::oscParamId(ovt::params::sustainSuffix, i), 1.0f);
    setParam(ovt::params::oscParamId(ovt::params::decaySuffix, i), 8.0f);
    setParam(ovt::params::oscParamId(ovt::params::velSuffix, i), 0.0f);
  }

  // Collects what comes out, so a pitch can be taken off it.
  std::vector<float> rendered;

  const auto play = [&](int blocks, juce::MidiBuffer midi) {
    juce::AudioBuffer<float> buffer(2, 512);
    rendered.clear();

    for (int b = 0; b < blocks; ++b) {
      juce::MidiBuffer m = (b == 0) ? midi : juce::MidiBuffer{};
      buffer.clear();
      p.processBlock(buffer, m);

      const auto *d = buffer.getReadPointer(0);
      rendered.insert(rendered.end(), d, d + 512);
    }
  };

  const auto measureHz = [&]() {
    std::vector<double> crossings;
    for (size_t i = 1; i < rendered.size(); ++i)
      if (rendered[i - 1] <= 0.0f && rendered[i] > 0.0f) {
        const auto frac = -rendered[i - 1] / (rendered[i] - rendered[i - 1]);
        crossings.push_back(((double)(i - 1) + frac) / 48000.0);
      }

    if (crossings.size() < 2)
      return 0.0;

    return (double)(crossings.size() - 1) /
           (crossings.back() - crossings.front());
  };

  const auto peak = [&]() {
    float m = 0.0f;
    for (auto x : rendered)
      m = std::max(m, std::abs(x));
    return m;
  };

  const auto panic = [&]() {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    play(4, m);
  };

  const auto twoNotes = [](int chA, int chB, int note) {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(chA, note, 1.0f), 0);
    m.addEvent(juce::MidiMessage::noteOn(chB, note, 1.0f), 1);
    return m;
  };

  p.prepareToPlay(48000.0, 512);

  // ---- off: the channel means nothing, which is what it always meant -------
  {
    setParam(ovt::params::mpeId, 0.0f);
    panic();

    play(8, twoNotes(2, 3, 60));

    check(p.getActiveVoiceCount() == 1,
          "with MPE off the same key on two channels is one voice, as before "
          "(" +
              std::to_string(p.getActiveVoiceCount()) + ")");

    // ...and a key-up on a different channel from the key-down still stops it,
    // which is the omni behaviour a single-channel keyboard relies on.
    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::noteOff(7, 60), 0);
    play(8, up);

    check(p.getActiveVoiceCount() <= 1,
          "and a key-up on any channel releases it");

    panic();
  }

  // ---- on: the channel is part of who the note is -------------------------
  {
    setParam(ovt::params::mpeId, 1.0f);
    panic();

    play(8, twoNotes(2, 3, 60));

    check(p.getActiveVoiceCount() == 2,
          "with MPE on the same key on two channels is two voices (" +
              std::to_string(p.getActiveVoiceCount()) + ")");

    // Letting one go leaves the other holding.
    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::noteOff(2, 60), 0);
    play(8, up);

    check(p.getActiveVoiceCount() == 2,
          "one key-up does not take both (still releasing, so still counted)");

    check(peak() > 0.01f, "and something is still sounding");

    panic();
  }

  // ---- on: an ordinary keyboard still plays -------------------------------
  //
  // Notes on the master channel are notes of the master channel rather than
  // nothing at all, which is what stops this setting from silencing a keyboard
  // that knows nothing about it.
  {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
    play(16, m);

    check(p.getActiveVoiceCount() == 1,
          "a note on the master channel plays with MPE on (" +
              std::to_string(p.getActiveVoiceCount()) + ")");

    const auto plain = measureHz();
    check(std::abs(plain - 440.0) < 2.0,
          "at its own pitch (" + std::to_string(plain) + " Hz)");

    // And the wheel still moves it, across the range the panel asks for.
    juce::MidiBuffer wheel;
    wheel.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    play(16, wheel);

    const auto bent = measureHz();
    const auto wanted = 440.0 * std::exp2(2.0 / 12.0);

    std::printf("  master channel: %.1f Hz plain, %.1f Hz with the wheel up "
                "(wanted %.1f)\n",
                plain, bent, wanted);

    check(std::abs(bent - wanted) < 4.0,
          "and the wheel bends it by what the bend range says");

    panic();
  }

  // ---- on: bend belongs to the channel it arrives on -----------------------
  {
    // The wheel is left where the player put it by a panic, deliberately, and
    // the block above pushed it to the top. Master bend reaches every note, so
    // without centring it here the baseline below would already be bent, which
    // is correct behaviour and a confusing thing to measure against.
    juce::MidiBuffer centre;
    centre.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 0);
    play(4, centre);

    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(2, 69, 1.0f), 0);
    play(16, m);

    const auto plain = measureHz();

    juce::MidiBuffer elsewhere;
    elsewhere.addEvent(juce::MidiMessage::pitchWheel(4, 16383), 0);
    play(16, elsewhere);
    const auto unmoved = measureHz();

    juce::MidiBuffer own;
    own.addEvent(juce::MidiMessage::pitchWheel(2, 16383), 0);
    play(16, own);
    const auto moved = measureHz();

    std::printf("  member channel: %.1f Hz plain, %.1f after a bend on another "
                "channel, %.1f after one on its own\n",
                plain, unmoved, moved);

    check(std::abs(unmoved - plain) < 2.0,
          "a bend on a channel that is not holding the note leaves it alone");

    // 48 semitones is what the specification asks for on a member channel and
    // what the zone is set up with, so full travel is four octaves.
    check(moved > plain * 3.0,
          "and a bend on its own channel moves it, over the wide per-note "
          "range (" +
              std::to_string(moved) + " Hz from " + std::to_string(plain) +
              ")");

    panic();
  }

  // ---- on: pressure belongs to the channel it arrives on -------------------
  {
    // Partial 1 silent until pressed, so any level is the pressure arriving.
    setParam(ovt::params::oscParamId(ovt::params::volumeSuffix, 0), 0.0f);
    setParam(ovt::params::oscParamId(ovt::params::atSuffix, 0), 1.0f);

    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 0);
    play(16, m);

    check(peak() < 0.01f, "unpressed, the note is silent");

    juce::MidiBuffer elsewhere;
    elsewhere.addEvent(juce::MidiMessage::channelPressureChange(5, 127), 0);
    play(16, elsewhere);

    check(peak() < 0.01f, "pressure on another channel does not reach it (" +
                              std::to_string(peak()) + ")");

    juce::MidiBuffer own;
    own.addEvent(juce::MidiMessage::channelPressureChange(2, 127), 0);
    play(16, own);

    const auto pressed = peak();
    std::printf("  pressed on its own channel it reaches %.3f\n", pressed);

    check(pressed > 0.01f, "pressure on its own channel brings it in");

    setParam(ovt::params::oscParamId(ovt::params::volumeSuffix, 0), 1.0f);
    setParam(ovt::params::oscParamId(ovt::params::atSuffix, 0), 0.0f);
  }

  // ---- on: the pedal still holds ------------------------------------------
  //
  // The parser takes CC 64 for itself, so the pool's own pedal handling never
  // sees it in this mode. That is deliberate, and it only works if the parser
  // then defers the release, which is what this checks. Getting it wrong in
  // either direction is either a pedal that does nothing or a note released
  // twice.
  {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 0);
    play(8, m);

    juce::MidiBuffer down;
    down.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    play(4, down);

    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::noteOff(2, 60), 0);
    play(40, up);

    check(p.getActiveVoiceCount() == 1,
          "with the pedal down the note holds after the key is up (" +
              std::to_string(p.getActiveVoiceCount()) + ")");
    check(peak() > 0.01f, "and it is still sounding");

    juce::MidiBuffer lift;
    lift.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    play(120, lift);

    check(p.getActiveVoiceCount() == 0,
          "and lifting the pedal lets it go (" +
              std::to_string(p.getActiveVoiceCount()) + " left)");

    panic();
  }

  // ---- switching it off does not leave notes hanging ----------------------
  //
  // The voices sounding were started through entry points the other mode
  // cannot reach, so without a release on the way through they would hold for
  // ever with no key left to lift.
  {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 0);
    m.addEvent(juce::MidiMessage::noteOn(3, 64, 1.0f), 0);
    play(8, m);

    check(p.getActiveVoiceCount() == 2, "two per-note voices are sounding");

    setParam(ovt::params::mpeId, 0.0f);
    play(120, {}); // long enough for a release to finish

    check(p.getActiveVoiceCount() == 0,
          "turning MPE off releases what it was holding (" +
              std::to_string(p.getActiveVoiceCount()) + " left)");

    // And the same the other way.
    juce::MidiBuffer ordinary;
    ordinary.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    play(8, ordinary);
    check(p.getActiveVoiceCount() == 1, "an ordinary note is sounding");

    setParam(ovt::params::mpeId, 1.0f);
    play(120, {});

    check(p.getActiveVoiceCount() == 0,
          "turning it on releases what was held the ordinary way (" +
              std::to_string(p.getActiveVoiceCount()) + " left)");

    setParam(ovt::params::mpeId, 0.0f);
    panic();
  }
}

/// The lamps between the knob groups, and what a frame of them costs.
///
/// The user asking for these asked for them only if they were cheap, so the
/// cost is measured here rather than asserted anywhere. Two things make them
/// cheap and both are checked: the lamps hold still between steps instead of
/// following their value exactly, and their bands are merged by row rather
/// than by neighbour.
void testActivityLamps(OvertoniumProcessor &p) {
  section("Strip lamps");

  using namespace ovt::ui;

  // ---- merging by row -----------------------------------------------------
  //
  // The lamps all sit at the same four heights, so this is the merge that
  // costs nothing: the union of a row of them is exactly the pixels they
  // occupy plus the gaps between strips.
  {
    juce::Array<juce::Rectangle<int>> bands;
    for (int i = 0; i < 32; ++i) {
      bands.add({i * 38, 100, 34, 15}); // one row
      bands.add({i * 38, 300, 34, 15}); // another
    }

    mergeIntoRows(bands);

    check(bands.size() == 2, "sixty-four lamps on two rules merge to two "
                             "bands (" +
                                 std::to_string(bands.size()) + ")");

    bool thin = true;
    for (const auto &b : bands)
      thin &= b.getHeight() == 15 && b.getWidth() == 31 * 38 + 34;

    check(thin, "each band is one rule tall and spans the strips it covers");

    // Rows that do not line up are left alone, or a band would swallow a
    // meter that happened to be passing.
    juce::Array<juce::Rectangle<int>> mixed;
    mixed.add({0, 100, 34, 15});
    mixed.add({38, 101, 34, 15});
    mixed.add({76, 100, 34, 16});

    mergeIntoRows(mixed);
    check(mixed.size() == 3, "rules at different heights stay apart");
  }

  // ---- the needle's scale -------------------------------------------------
  //
  // Fixed, and fixed to what the knobs can actually reach. A scale that
  // disagreed with the controls feeding it would be worse than no scale, so
  // the constants the needle reads are held against the ranges themselves.
  {
    const auto rangeEnd = [&p](const char *suffix) {
      auto *param = p.apvts.getParameter(ovt::params::oscParamId(suffix, 0));
      return param != nullptr ? param->getNormalisableRange().end : -1.0f;
    };

    check(ovt::exactly(rangeEnd(ovt::params::pmDepthSuffix),
                       ovt::params::kMaxPitchModCents),
          "the pitch modulation maximum matches its own knob (" +
              std::to_string(rangeEnd(ovt::params::pmDepthSuffix)) + ")");

    // The needle is scaled for vibrato rather than for the octave a square can
    // jump, which is the one deliberate disagreement between a readout and the
    // control feeding it. What still has to hold is that the other wanderer
    // cannot peg it on its own, or the lamp would be at the end all the time.
    check(ovt::params::kPitchNeedleFullScaleCents > ovt::params::kMaxDriftCents,
          "drift alone cannot peg the needle");
    check(ovt::params::kPitchNeedleFullScaleCents <
              ovt::params::kMaxPitchModCents,
          "and the needle is scaled for vibrato, not for the widest jump");

    check(ovt::exactly(rangeEnd(ovt::params::driftSuffix),
                       ovt::params::kMaxDriftCents),
          "and so does the drift maximum (" +
              std::to_string(rangeEnd(ovt::params::driftSuffix)) + ")");

    check(ovt::exactly(ChannelStrip::needlePosition(0.0f), 0.0f),
          "an unmodulated partial sits dead centre");

    check(std::abs(ChannelStrip::needlePosition(
                       ovt::params::kPitchNeedleFullScaleCents) -
                   1.0f) < 1.0e-6f,
          "both wanders at once put it exactly at the end");

    check(std::abs(ChannelStrip::needlePosition(
                       -ovt::params::kPitchNeedleFullScaleCents) +
                   1.0f) < 1.0e-6f,
          "and flat is the mirror of sharp");

    // Nothing past the ends, whatever arrives.
    check(ovt::exactly(ChannelStrip::needlePosition(10000.0f), 1.0f) &&
              ovt::exactly(ChannelStrip::needlePosition(-10000.0f), -1.0f),
          "and it cannot be driven off the end");

    // A shallow setting has to stay well inside the travel, which is what a
    // scale normalised to each strip's own depth would not do.
    const auto shallow = ChannelStrip::needlePosition(5.0f);
    const auto deep = ChannelStrip::needlePosition(200.0f);

    std::printf("  needle at 5 cents %.3f, at 50 cents %.3f, at 200 cents "
                "%.3f of full travel\n",
                shallow, ChannelStrip::needlePosition(50.0f), deep);

    check(shallow < 0.25f, "a five cent wander stays near the middle (" +
                               std::to_string(shallow) + ")");

    check(deep > 0.9f, "and a deep one nearly fills the travel (" +
                           std::to_string(deep) + ")");

    // ...but not so compressed that a shallow setting is invisible. Over
    // fifteen pixels of travel, a tenth is a pixel and a half.
    check(shallow > 0.1f, "while still being far enough out to see (" +
                              std::to_string(shallow) + ")");

    bool rising = true;
    for (float c = 0.0f; c < 220.0f; c += 5.0f)
      rising &= ChannelStrip::needlePosition(c + 5.0f) >
                ChannelStrip::needlePosition(c);

    check(rising, "and further out means sharper all the way up");
  }

  // ---- holding still ------------------------------------------------------
  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  std::vector<ChannelStrip *> strips;
  std::function<void(juce::Component &)> collect = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ChannelStrip *>(child))
        strips.push_back(s);

      collect(*child);
    }
  };
  collect(*editor);

  check(!strips.empty(), "the mixer has strips in it");
  if (strips.empty())
    return;

  {
    auto &strip = *strips.front();

    juce::Array<juce::Rectangle<int>> bands;

    // First call has everything to say, since the lamps start dark.
    strip.setActivity(0.5f, 0.5f, 0.0f, bands);
    const auto first = bands.size();

    bands.clearQuick();
    strip.setActivity(0.5f, 0.5f, 0.0f, bands);

    check(first > 0, "a lamp that lights asks to be repainted");
    check(bands.isEmpty(), "and the same values again ask for nothing (" +
                               std::to_string(bands.size()) + " bands)");

    // A move too small to cross a step is a move nobody can see.
    bands.clearQuick();
    strip.setActivity(0.5f + 1.0f / (4.0f * ActivityLamp::kSteps), 0.5f, 0.0f,
                      bands);

    check(bands.isEmpty(), "nor does a change smaller than one step");

    // A move of a whole step does.
    bands.clearQuick();
    strip.setActivity(0.5f + 1.5f / (float)ActivityLamp::kSteps, 0.5f, 0.0f,
                      bands);

    check(!bands.isEmpty(), "a change of a whole step does");

    // ---- the two envelope lamps never both light --------------------------
    bands.clearQuick();
    strip.setActivity(-0.8f, 0.0f, 0.0f, bands);
    strip.setActivity(-0.8f, 0.0f, 0.0f, bands);

    // Reading the lamps back is not possible from here, so this is checked by
    // the shape of the request instead: flipping the sign has to move both
    // lamps, one out and one in, and nothing else.
    bands.clearQuick();
    strip.setActivity(0.8f, 0.0f, 0.0f, bands);

    check(bands.size() == 2,
          "flipping to the key-off half moves exactly two lamps, one out and "
          "one in (" +
              std::to_string(bands.size()) + ")");
  }
}

/// The seven-segment readouts under each channel.
///
/// They replaced two plain labels, and the risk in that is a reading the
/// display has no way to draw: a label will render anything, a segment display
/// will silently show an unlit digit. So every reading the two can produce is
/// held against what the display can draw.
/// The STRIKE popup, which is the only reading on the panel worked out from
/// more than one control.
void testStrikeReadout() {
  section("Strike readout");

  using namespace ovt;

  // Off: nothing is being done to the onset, so the reading is the value and
  // nothing else. Anything further would be two figures that never move.
  check(params::strikeRangeText(0.0f, 0.0f, 0.005f) == "0 %",
        "at nought the reading is the percentage alone (" +
            params::strikeRangeText(0.0f, 0.0f, 0.005f).toStdString() + ")");

  // The case the control exists for, and the one that was impossible to find
  // out without playing a note: a short attack at a full amount.
  const auto full = params::strikeRangeText(1.0f, 0.0f, 0.002f);
  std::printf("  2 ms attack at +100%%: %s\n", full.toRawUTF8());

  check(full.startsWith("+100 %"), "the value still leads the reading");
  check(full.contains("2.0 ms") && full.contains("512 ms"),
        "and it names both ends of the onset (" + full.toStdString() + ")");

  // The delay is folded in rather than reported separately, so a strip that
  // has one reads as the whole wait from key-down to full level.
  const auto delayed = params::strikeRangeText(1.0f, 0.4f, 0.002f);
  std::printf("  the same with a 400 ms delay: %s\n", delayed.toRawUTF8());

  // Both ends move: the slow one gains the whole delay on top of the stretched
  // attack, 400 plus 512, and the quick one gains the sliver the delay is
  // pulled in to, 1.6 plus the 2 ms attack.
  check(delayed.contains("912 ms") && delayed.contains("3.6 ms"),
        "a delay lands on both ends of the onset (" + delayed.toStdString() +
            ")");

  // The panel and the engine have to agree, or the reading is a second opinion
  // rather than a readout. Held against the arithmetic the voice itself runs.
  const auto range = strikeRange(1.0f, 0.4f, 0.002f);
  const auto quick = 0.4f * strikeDelayScale(1.0f, 1.0f) +
                     struckAttack(0.002f, strikeAttackScale(1.0f, 1.0f));

  check(std::abs(range.quickest - quick) < 1.0e-9f,
        "the reading is the engine's own figure rather than a copy of it");

  // Whatever the amount and whichever way it leans, the pair is a range.
  bool ordered = true;
  for (int i = -10; i <= 10; ++i) {
    const auto r = strikeRange((float)i / 10.0f, 0.4f, 0.02f);
    ordered &= r.quickest <= r.slowest;
  }

  check(ordered, "and it comes back smallest first at every amount");
}

void testSegmentReadouts(OvertoniumProcessor &p) {
  section("Segment readouts");

  using namespace ovt::ui;

  // ---- everything asked for can be drawn ----------------------------------
  {
    // Both readouts, over the whole of both ranges, plus the two things they
    // say that are not numbers.
    juce::StringArray readings{"Et", "-inF"};

    for (int i = 0; i <= 100; ++i) {
      const auto v = (float)i / 100.0f;

      if (v > 0.0005f)
        readings.add(
            (juce::String)(juce::Decibels::gainToDecibels(v) >= 0.0f ? ""
                                                                     : "-") +
            juce::String(std::abs(juce::Decibels::gainToDecibels(v)), 1));

      // Cents run to about fifty either way, which is as far as any harmonic's
      // just interval sits from equal temperament.
      const auto cents = -50.0f + v * 100.0f;
      readings.add(juce::String(cents, 1));
    }

    juce::String undrawable;

    for (const auto &reading : readings)
      for (int i = 0; i < reading.length(); ++i)
        if (!SegmentDisplay::canDraw((char)reading[i]))
          undrawable += reading[i];

    check(undrawable.isEmpty(),
          "every character the two readouts can produce has a form (" +
              std::to_string(readings.size()) + " readings, stuck on \"" +
              undrawable.toStdString() + "\")");
  }

  // ---- what the strips actually show --------------------------------------
  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  std::vector<SegmentDisplay *> displays;
  std::function<void(juce::Component &)> collect = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *d = dynamic_cast<SegmentDisplay *>(child))
        displays.push_back(d);

      collect(*child);
    }
  };
  collect(*editor);

  // Two on the bar and two on each of the thirty-two channels, plus the level
  // on the noise strip, which never had a reading at all before.
  check(displays.size() == 2 + 2 * ovt::kNumHarmonics + 1,
        "every readout is a segment display now (" +
            std::to_string(displays.size()) + ")");

  bool allDrawable = true;
  std::string firstBad;

  for (auto *d : displays)
    for (int i = 0; i < d->getReading().length(); ++i)
      if (!SegmentDisplay::canDraw((char)d->getReading()[i])) {
        allDrawable = false;
        if (firstBad.empty())
          firstBad = d->getReading().toStdString();
      }

  check(allDrawable, "and what they are showing right now is drawable" +
                         (firstBad.empty() ? "" : " (" + firstBad + ")"));

  // ---- the level readout, across its range --------------------------------
  const auto setVolume = [&p](int channel, float plain) {
    auto *param = p.apvts.getParameter(
        ovt::params::oscParamId(ovt::params::volumeSuffix, channel));
    if (param != nullptr)
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  std::vector<ChannelStrip *> strips;
  std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ChannelStrip *>(child))
        strips.push_back(s);

      gather(*child);
    }
  };
  gather(*editor);

  check(!strips.empty(), "the mixer has strips in it");
  if (strips.empty())
    return;

  // The readouts are the strip's own children, so they are found by walking
  // one strip rather than by being handed out.
  std::vector<SegmentDisplay *> onFirst;
  collect = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *d = dynamic_cast<SegmentDisplay *>(child))
        onFirst.push_back(d);

      collect(*child);
    }
  };
  collect(*strips.front());

  check(onFirst.size() == 2, "a channel carries two of them (" +
                                 std::to_string(onFirst.size()) + ")");
  if (onFirst.size() != 2)
    return;

  auto &level = *onFirst.back();

  setVolume(0, 1.0f);
  check(level.getReading() == "0.0" && level.isActive(),
        "a fader at unity reads 0.0, lit (" + level.getReading().toStdString() +
            ")");

  setVolume(0, 0.0f);
  check(level.getReading() == "-inF" && !level.isActive(),
        "and all the way down it reads -inF, dimmed, since that is a statement "
        "rather than a level (" +
            level.getReading().toStdString() + ")");

  setVolume(0, 0.5f);
  check(level.getReading() == "-6.0" && level.isActive(),
        "half way is -6.0 dB (" + level.getReading().toStdString() + ")");

  // The readout counts down to the quietest figure the cells can hold rather
  // than giving up partway, which it used to do at -66 dB.
  const auto atDb = [&](float db) {
    setVolume(0, juce::Decibels::decibelsToGain(db));
    return level.getReading();
  };

  bool counts = true;
  for (float db : {-40.0f, -66.0f, -70.0f, -80.0f, -95.0f, -99.9f})
    counts &= atDb(db) != "-inF";

  check(counts, "every level down to the floor reads as a number");

  check(atDb(-70.0f) == "-70.0",
        "a level far below the old cut-off reads back (" +
            atDb(-70.0f).toStdString() + ")");
  check(atDb(-99.9f) == "-99.9", "and the floor itself reads -99.9 (" +
                                     atDb(-99.9f).toStdString() + ")");

  // Below the floor there is no figure that fits, so it says so instead.
  check(atDb(-100.5f) == "-inF" && !level.isActive(),
        "quieter than the floor reads -inF, dimmed (" +
            atDb(-100.5f).toStdString() + ")");

  // Whatever it shows has to fit the cells: a sign and four digits.
  bool fits = true;
  for (int i = 0; i <= 200; ++i) {
    const auto reading = atDb(-0.5f * (float)i);
    fits &= reading.length() <= 5;

    for (auto c : reading)
      fits &= ovt::ui::SegmentDisplay::canDraw((char)c);
  }

  check(fits, "no level anywhere in the range overflows the display");

  // ---- where those levels sit on the fader --------------------------------
  //
  // Reading a level is no use if it cannot be set. The travel is spaced by
  // decibels, so the quiet end has room rather than being crushed into the
  // last few pixels.
  auto *fader = p.apvts.getParameter(
      ovt::params::oscParamId(ovt::params::volumeSuffix, 0));

  check(fader != nullptr, "the level parameter is there");
  if (fader == nullptr)
    return;

  const auto travelAt = [&](float db) {
    return fader->convertTo0to1(juce::Decibels::decibelsToGain(db));
  };

  std::printf("  fader travel: -12 dB at %.0f%%, -40 at %.0f%%, -66 at %.0f%%, "
              "-99.9 at %.1f%%\n",
              100.0 * travelAt(-12.0f), 100.0 * travelAt(-40.0f),
              100.0 * travelAt(-66.0f), 100.0 * travelAt(-99.9f));

  check(travelAt(0.0f) > 0.999f, "unity sits at the top of the travel");
  check(fader->convertTo0to1(0.0f) < 0.001f, "and silence at the bottom");

  // A gain law left everything below -66 dB inside the bottom 2% of a 223
  // pixel fader, which is five pixels for thirty-four decibels.
  check(travelAt(-66.0f) > 0.15f,
        "-66 dB is well clear of the bottom of the fader");
  check(travelAt(-99.9f) < 0.03f && travelAt(-99.9f) > 0.0f,
        "and the floor is near the bottom of it, but clear of silence");

  // Every step down has to move the fader down, or a level would be
  // unreachable between two positions.
  bool monotonic = true;
  for (int i = 1; i <= 200; ++i)
    monotonic &= travelAt(-0.5f * (float)i) < travelAt(-0.5f * (float)(i - 1));

  check(monotonic, "the travel falls with the level all the way down");

  // A level set on the fader has to come back as the level that was asked for.
  bool roundTrips = true;
  for (int i = 0; i <= 100; ++i) {
    const auto norm = (float)i / 100.0f;
    const auto gain = fader->convertFrom0to1(norm);
    roundTrips &= std::abs(fader->convertTo0to1(gain) - norm) < 1.0e-3f;
  }

  check(roundTrips, "and a position round-trips through the gain it means");

  setVolume(0, 1.0f);

  // ---- the cents readout --------------------------------------------------
  //
  // Partial 1 is the fundamental, whose just interval is the note itself, so
  // there is nothing for its knob to do and the display says so rather than
  // implying a choice.
  auto &cents = *onFirst.front();

  check(cents.getReading() == "0.0" && !cents.isActive(),
        "the fundamental has no cents to report, and is dimmed (" +
            cents.getReading().toStdString() + ")");

  // A partial that does. Harmonic 3 sits just under two cents above equal
  // temperament, and harmonic 7 a third of a semitone below it.
  const auto readingFor = [&](int channel, float blend) -> juce::String {
    auto *param = p.apvts.getParameter(
        ovt::params::oscParamId(ovt::params::tuneSuffix, channel));
    if (param != nullptr)
      param->setValueNotifyingHost(param->convertTo0to1(blend));

    std::vector<SegmentDisplay *> on;
    std::function<void(juce::Component &)> walk = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *d = dynamic_cast<SegmentDisplay *>(child))
          on.push_back(d);

        walk(*child);
      }
    };
    walk(*strips[(size_t)channel]);

    return on.empty() ? juce::String() : on.front()->getReading();
  };

  check(readingFor(2, 0.0f) == "Et",
        "a partial left in equal temperament says so (" +
            readingFor(2, 0.0f).toStdString() + ")");

  // No plus sign: seven bars cannot draw one that reads as anything but a
  // speck, so a sharp partial is the one with no sign at all.
  check(readingFor(2, 1.0f) == "2.0",
        "and tuned across to just intonation it reads its offset, unsigned "
        "when sharp (" +
            readingFor(2, 1.0f).toStdString() + ")");

  check(!readingFor(2, 1.0f).containsChar('+') &&
            !ovt::ui::SegmentDisplay::canDraw('+'),
        "and the display has no plus to draw in the first place");

  check(readingFor(6, 1.0f) == "-31.2",
        "the seventh harmonic being the one that goes the other way (" +
            readingFor(6, 1.0f).toStdString() + ")");
}

/// The channel highlight, which marks the column the pointer is on.
///
/// The row band answers which of the twenty-one controls you are on. This
/// answers which of the thirty-three channels, and the two crossing is what
/// tells you at a glance which knob a drag would actually move.
///
/// Each strip works it out from where the pointer is rather than being told by
/// the editor, so what is checked here is that the answer follows the pointer
/// and that exactly one channel ever claims it.
void testChannelHover(OvertoniumProcessor &p) {
  section("Channel hover");

  using namespace ovt::ui;

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  std::vector<ChannelStrip *> strips;
  std::vector<NoiseStrip *> noise;

  std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ChannelStrip *>(child))
        strips.push_back(s);
      if (auto *n = dynamic_cast<NoiseStrip *>(child))
        noise.push_back(n);

      gather(*child);
    }
  };
  gather(*editor);

  check(strips.size() == (size_t)ovt::kNumHarmonics && noise.size() == 1,
        "the mixer is all there");
  if (strips.empty() || noise.empty())
    return;

  // A pointer event landing on a component at a point of our choosing, which
  // is the only way to move a pointer with no pointer.
  const auto pointAt = [](juce::Component &c, juce::Point<int> local) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            local.toFloat(), juce::ModifierKeys(), 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &c, &c,
                            juce::Time::getCurrentTime(), local.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
  };

  const auto litCount = [&]() {
    int n = 0;
    for (auto *s : strips)
      n += s->isHovered() ? 1 : 0;

    return n + (noise.front()->isHovered() ? 1 : 0);
  };

  check(litCount() == 0, "nothing is lit before the pointer arrives (" +
                             std::to_string(litCount()) + ")");

  // ---- it follows the pointer ---------------------------------------------
  auto &first = *strips[3];
  const juce::Point<int> inside{first.getWidth() / 2, first.getHeight() / 2};

  first.mouseEnter(pointAt(first, inside));

  check(first.isHovered(), "the channel under the pointer lights");
  check(litCount() == 1,
        "and it is the only one (" + std::to_string(litCount()) + ")");

  // ---- crossing to the next one -------------------------------------------
  //
  // Leaving one strip for the next fires the exit before the enter, and the
  // answer has to come out the same either way round, which is why each strip
  // reads the pointer rather than trusting the order it is told things in.
  auto &second = *strips[4];

  first.mouseExit(pointAt(first, {first.getWidth() + 4, inside.y}));
  second.mouseEnter(pointAt(second, inside));

  check(!first.isHovered() && second.isHovered(),
        "moving along hands the highlight over");
  check(litCount() == 1, "still only one channel lit");

  // ...and in the other order, which is the case that catches a highlight
  // being cleared by an exit that arrives late.
  auto &third = *strips[5];

  third.mouseEnter(pointAt(third, inside));
  second.mouseExit(pointAt(second, {-4, inside.y}));

  check(!second.isHovered() && third.isHovered(),
        "and so does an exit arriving after the next enter");
  check(litCount() == 1, "with no channel left lit behind it");

  // ---- the noise channel counts as a channel ------------------------------
  auto &nz = *noise.front();
  const juce::Point<int> inNoise{nz.getWidth() / 2, nz.getHeight() / 2};

  nz.mouseEnter(pointAt(nz, inNoise));
  third.mouseExit(pointAt(third, {-4, inside.y}));

  check(nz.isHovered(), "the noise channel lights like any other");
  check(litCount() == 1,
        "and nothing else is (" + std::to_string(litCount()) + ")");

  // ---- leaving the mixer --------------------------------------------------
  nz.mouseExit(pointAt(nz, {inNoise.x, nz.getHeight() + 40}));

  check(litCount() == 0, "and taking the pointer off the mixer clears it (" +
                             std::to_string(litCount()) + ")");

  // ---- a right-click lets the hover go ------------------------------------
  //
  // Whatever menu that opens is modal, so the strip is never told the pointer
  // has left it. The highlight used to be left lit on the channel the menu was
  // opened from, and hovering that channel again was the only way to clear it.
  //
  // Driven through a mute button, which is the case that returns before any
  // menu is shown. A plain right-click on the strip takes the same path and
  // then opens the LINK menu, and a modal menu is not something this suite can
  // put on a screen it does not have.
  {
    const auto rightClickOn = [&pointAt](juce::Component &c,
                                         juce::Point<int> local,
                                         juce::Component *origin) {
      const auto e = pointAt(c, local);
      return juce::MouseEvent(
          e.source, local.toFloat(),
          juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f,
          0.0f, 0.0f, 0.0f, 0.0f, &c, origin, juce::Time::getCurrentTime(),
          local.toFloat(), juce::Time::getCurrentTime(), 1, false);
    };

    const auto muteButtonIn = [](juce::Component &c) -> juce::Component * {
      std::function<juce::Component *(juce::Component &)> find =
          [&](juce::Component &parent) -> juce::Component * {
        for (auto *child : parent.getChildren()) {
          if (dynamic_cast<MuteSoloButton *>(child) != nullptr)
            return child;

          if (auto *found = find(*child))
            return found;
        }

        return nullptr;
      };

      return find(c);
    };

    auto &lit = *strips[9];
    auto *button = muteButtonIn(lit);

    check(button != nullptr, "a channel has a mute button to right-click");

    if (button != nullptr) {
      lit.mouseEnter(pointAt(lit, inside));
      check(lit.isHovered(), "a channel is lit before the right-click");

      lit.mouseDown(rightClickOn(lit, inside, button));

      check(!lit.isHovered() && litCount() == 0,
            "and the right-click lets it go (" + std::to_string(litCount()) +
                ")");

      // The button carries its own menu, so the strip must not also open the
      // LINK menu on top of it. Both used to appear, one after the other.
      // Losing this fix crashes the suite rather than failing it, since a
      // modal menu needs a screen, but stating it says what the code is for.
      check(juce::Component::getCurrentlyModalComponent() == nullptr,
            "and the strip opens no menu of its own over the button's");

      // And it has to stay let go. Opening a menu takes the mouse away, and
      // the exit JUCE sends on the way arrives while the pointer is still
      // geometrically inside the strip, which reads as "still hovering".
      lit.mouseExit(pointAt(lit, inside));

      check(!lit.isHovered() && litCount() == 0,
            "and an exit arriving with the pointer still inside does not "
            "light it again (" +
                std::to_string(litCount()) + ")");

      // The pointer coming back is what ends it, so the channel is not left
      // dead to the mouse once a menu has been opened over it.
      lit.mouseEnter(pointAt(lit, inside));

      check(lit.isHovered() && litCount() == 1,
            "and hovering it again lights it as before");

      lit.mouseExit(pointAt(lit, {-4, inside.y}));
      check(litCount() == 0, "and leaving clears it");
    }

    // The noise channel opens no menu of its own, but its buttons do, so it
    // has to let go the same way.
    nz.mouseEnter(pointAt(nz, inNoise));
    check(nz.isHovered(), "the noise channel is lit before its right-click");

    nz.mouseDown(rightClickOn(nz, inNoise, &nz));

    check(!nz.isHovered() && litCount() == 0,
          "and it lets go too (" + std::to_string(litCount()) + ")");
  }

  // A point inside the strip but on a row that has nothing to point at, such
  // as a section rule, still belongs to that channel: the column says which
  // channel, not which control.
  auto &fourth = *strips[7];
  const auto rules =
      layoutRows(juce::Rectangle<int>(0, 0, kStripWidth, fourth.getHeight())
                     .reduced(kStripPadX, kStripPadY));
  const auto onRule = rules[(size_t)Row::EnvHeading].getCentre();

  fourth.mouseEnter(pointAt(fourth, onRule));

  check(controlRowAt(rules, onRule) == kNoRow,
        "a section rule is not a control");
  check(fourth.isHovered(),
        "but the channel it is on still lights, since the column answers a "
        "different question from the row");

  fourth.mouseExit(pointAt(fourth, {-4, onRule.y}));
  check(litCount() == 0, "and clears again");
}

/// The generator that turns a patch into a factory preset.
///
/// This is the step between dialling a sound in and it shipping, and the way
/// it goes wrong is silent: a parameter the generated code fails to mention is
/// not left as it was, it is left wherever neutralBase put it. So the test
/// runs the generated code the way the compiler would and checks the patch
/// comes back.
void testFactoryCodeGenerator(OvertoniumProcessor &p) {
  section("Factory code generator");

  const auto plainOf = [](juce::RangedAudioParameter *r) {
    return r->convertFrom0to1(r->getValue());
  };

  const auto snapshot = [&p, &plainOf]() {
    std::map<std::string, float> out;

    for (auto *raw : p.getParameters())
      if (auto *r = dynamic_cast<juce::RangedAudioParameter *>(raw))
        out[r->paramID.toStdString()] = plainOf(r);

    return out;
  };

  // Everything the generated case would do, read back out of the text it
  // wrote. Anything it did not say is a parameter left at neutralBase.
  //
  // Three forms, because the generator collapses a row the whole series shares
  // into one allOsc and a row that was dragged across it into one oscTable.
  // Reading only ap.set would see those rows as absent and call every value in
  // them a parameter that went missing.
  const auto parse = [](const juce::String &code) {
    std::map<std::string, float> sets;

    // A table runs to several lines, so gather whole statements first.
    juce::StringArray statements;
    juce::String current;

    for (const auto &line : juce::StringArray::fromLines(code)) {
      current += line.trim() + " ";

      if (line.contains(";")) {
        statements.add(current.trim());
        current.clear();
      }
    }

    // params::atSuffix is the one constant not named after the id it makes.
    const auto suffixOf = [](const juce::String &constant) {
      const auto stem = constant.upToFirstOccurrenceOf("Suffix", false, false);
      return stem == "at" ? juce::String("aftertouch") : stem;
    };

    for (const auto &statement : statements) {
      if (statement.contains("ap.set(\"")) {
        const auto id =
            statement.fromFirstOccurrenceOf("ap.set(\"", false, false)
                .upToFirstOccurrenceOf("\"", false, false);
        const auto value = statement.fromFirstOccurrenceOf(", ", false, false)
                               .upToFirstOccurrenceOf("f)", false, false);

        sets[id.toStdString()] = value.getFloatValue();
        continue;
      }

      if (statement.contains("ap.allOsc(params::")) {
        const auto suffix =
            suffixOf(statement.fromFirstOccurrenceOf("params::", false, false)
                         .upToFirstOccurrenceOf(",", false, false));
        const auto value =
            statement.fromFirstOccurrenceOf("return ", false, false)
                .upToFirstOccurrenceOf(";", false, false);

        for (int i = 0; i < ovt::kNumHarmonics; ++i)
          sets[ovt::params::oscParamId(suffix.toRawUTF8(), i).toStdString()] =
              value.getFloatValue();

        continue;
      }

      if (statement.contains("ap.oscTable(params::")) {
        const auto suffix =
            suffixOf(statement.fromFirstOccurrenceOf("params::", false, false)
                         .upToFirstOccurrenceOf(",", false, false));

        const auto body = statement.fromFirstOccurrenceOf("{", false, false)
                              .upToFirstOccurrenceOf("}", false, false);

        auto values = juce::StringArray::fromTokens(body, ",", "");
        values.trim();
        values.removeEmptyStrings();

        for (int i = 0; i < values.size() && i < ovt::kNumHarmonics; ++i)
          sets[ovt::params::oscParamId(suffix.toRawUTF8(), i).toStdString()] =
              values[i].getFloatValue();

        continue;
      }
    }

    return sets;
  };

  // Every factory preset, since between them they exercise far more of the
  // parameter space than any one patch would.
  const auto count = ovt::presets::names().size();
  int worstPreset = -1;
  float worstError = 0.0f;
  std::string worstParam;

  for (int i = 0; i < count; ++i) {
    ovt::presets::apply(p.apvts, i);

    const auto wanted = snapshot();
    const auto code =
        ovt::presets::factoryCode(p.apvts, ovt::presets::names()[i]);
    const auto sets = parse(code);

    // What the compiler would do with that case: start neutral, then apply
    // exactly the lines it wrote.
    ovt::presets::neutralBase(p.apvts);

    for (const auto &pair : sets)
      if (auto *param = p.apvts.getParameter(juce::String(pair.first)))
        param->setValueNotifyingHost(param->convertTo0to1(pair.second));

    const auto got = snapshot();

    for (const auto &pair : wanted) {
      // The session is the one thing a preset may not carry, so the generator
      // leaves it alone and it is not expected to come back.
      bool session = false;
      for (auto *id : ovt::params::kSessionParamIds)
        session |= pair.first == id;

      if (session)
        continue;

      const auto found = got.find(pair.first);
      const auto error =
          found == got.end() ? 1.0f : std::abs(found->second - pair.second);

      if (error > worstError) {
        worstError = error;
        worstParam = pair.first;
        worstPreset = i;
      }
    }
  }

  std::printf(
      "  %d presets round-tripped, worst error %.6f%s\n", count, worstError,
      worstParam.empty() ? ""
                         : (" on " + worstParam + " in " +
                            ovt::presets::names()[worstPreset].toStdString())
                               .c_str());

  // The generator writes values to four decimal places, so anything under a
  // thousandth is the printing rather than a parameter going missing.
  check(worstError < 1.0e-3f,
        "a generated factory preset reproduces the patch it came from");

  // ...and it must not carry the session across. Dialling a patch in on a
  // Werckmeister session should not ship Werckmeister with it.
  {
    const auto setSession = [&p](const char *id, float plain) {
      if (auto *param = p.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    };

    ovt::presets::apply(p.apvts, presetIndex("Drawbar Organ"));
    setSession(ovt::params::temperamentId,
               (float)(int)ovt::Temperament::Werckmeister3);
    setSession(ovt::params::polyphonyId, 4.0f);

    const auto code = ovt::presets::factoryCode(p.apvts, "Probe");

    bool carries = false;
    for (auto *id : ovt::params::kSessionParamIds)
      carries |= code.contains(juce::String("\"") + id + "\"");

    check(!carries, "and carries none of the session it was dialled in on");
  }
}

/// A preset someone saves carries exactly what a factory preset carries.
///
/// Two paths write a preset and they have to agree. A factory preset is
/// neutralBase() followed by the lines of its case, so what it decides is what
/// neutralBase() touches. A user preset is every parameter capture() walks. If
/// those sets drift apart, saving a sound and recalling it stops being the
/// same operation as picking one from the menu, and the difference shows up as
/// a control that moves when the other kind of preset loads.
///
/// The session is the other half of the same rule. Nothing in the settings
/// menu belongs to either kind, so it must be in neither set.
void testBothPresetKindsCarryTheSame() {
  section("Both kinds of preset carry the same set");

  OvertoniumProcessor p;

  const auto everyParam = [&p] {
    std::vector<juce::RangedAudioParameter *> out;

    for (auto *param : p.apvts.processor.getParameters())
      if (auto *r = dynamic_cast<juce::RangedAudioParameter *>(param))
        out.push_back(r);

    return out;
  }();

  // What neutralBase() decides, found by running it from both extremes.
  // Anything it writes lands on the same value twice. Anything it leaves alone
  // keeps whichever end it started from.
  const auto runFrom = [&](float normalised) {
    for (auto *r : everyParam)
      r->setValueNotifyingHost(normalised);

    ovt::presets::neutralBase(p.apvts);

    std::map<std::string, float> out;
    for (auto *r : everyParam)
      out[r->paramID.toStdString()] = r->getValue();

    return out;
  };

  const auto fromLow = runFrom(0.0f);
  const auto fromHigh = runFrom(1.0f);

  std::set<std::string> decidedByFactory;

  for (const auto &pair : fromLow) {
    const auto other = fromHigh.find(pair.first);

    if (other != fromHigh.end() &&
        juce::exactlyEqual(pair.second, other->second))
      decidedByFactory.insert(pair.first);
  }

  // What a user preset stores, read off the file it writes rather than off
  // capture()'s source, so the test covers what actually lands on disk.
  std::set<std::string> storedByUser;

  {
    const auto doc = ovt::presets::capture(p.apvts, "Probe");

    for (auto *entry : doc->getChildWithTagNameIterator("PARAM"))
      storedByUser.insert(entry->getStringAttribute("id").toStdString());
  }

  std::vector<std::string> onlyFactory, onlyUser, sessionLeaks;

  for (const auto &id : decidedByFactory)
    if (storedByUser.count(id) == 0)
      onlyFactory.push_back(id);

  for (const auto &id : storedByUser)
    if (decidedByFactory.count(id) == 0)
      onlyUser.push_back(id);

  for (auto *id : ovt::params::kSessionParamIds)
    if (decidedByFactory.count(id) > 0 || storedByUser.count(id) > 0)
      sessionLeaks.push_back(id);

  const auto list = [](const std::vector<std::string> &ids) {
    std::string out;

    for (const auto &id : ids)
      out += (out.empty() ? "" : ", ") + id;

    return out;
  };

  check(onlyFactory.empty(),
        "a factory preset decides nothing a user preset drops (" +
            list(onlyFactory) + ")");
  check(onlyUser.empty(),
        "and a user preset stores nothing a factory preset leaves undecided (" +
            list(onlyUser) + ")");
  check(sessionLeaks.empty(),
        "and neither kind carries a settings-menu parameter (" +
            list(sessionLeaks) + ")");

  std::printf("  %d parameters in a preset, %d in the session\n",
              (int)storedByUser.size(),
              (int)ovt::params::kSessionParamIds.size());
}

/// Blocks bigger than the host promised.
///
/// A host is allowed to do that, and growing the scratch to fit would be an
/// allocation on the audio thread. The block is cut into pieces the scratch
/// already holds instead, which is only worth doing if the seam is invisible:
/// the same notes at the same sample positions have to come out the same
/// whether they arrived in one block or several.
void testOversizedBlocks(OvertoniumProcessor &p) {
  section("Oversized blocks");

  // A sustained patch with nothing random in it. Drift is a random walk
  // redrawn once per control block, and the control blocks fall at different
  // places when a block is cut up, so a patch carrying drift cannot be
  // compared sample for sample across the two paths. That is a property of a
  // random modulator rather than of the cutting, and comparing a patch that
  // has one would be measuring the wrong thing.
  ovt::presets::apply(p.apvts, presetIndex("Equal Saw"));

  // Renders the same musical passage, telling the plugin one block size and
  // then handing it another.
  const auto render = [&p](int promised, int actual) {
    p.setRateAndBufferSizeDetails(48000.0, promised);
    p.prepareToPlay(48000.0, promised);
    p.reset();

    juce::AudioBuffer<float> buffer(2, actual);
    juce::MidiBuffer midi;

    // Spread across the block, so the pieces have to carry events at the
    // right offsets rather than all of them landing in the first one.
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), actual / 3);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.7f), actual / 2);
    midi.addEvent(juce::MidiMessage::noteOff(1, 48), (actual * 3) / 4);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 12000), actual - 2);

    buffer.clear();
    p.processBlock(buffer, midi);

    std::vector<float> out((size_t)actual * 2);
    for (int n = 0; n < actual; ++n) {
      out[(size_t)n * 2] = buffer.getSample(0, n);
      out[(size_t)n * 2 + 1] = buffer.getSample(1, n);
    }

    return out;
  };

  // The reference: the host keeps its word, so nothing is cut up.
  const auto honest = render(4096, 4096);

  // The same block, from a host that promised far less. This is the case that
  // used to allocate.
  const auto cutUp = render(256, 4096);

  check(honest.size() == cutUp.size(), "both runs produced a full block");

  double worst = 0.0;
  for (size_t i = 0; i < honest.size() && i < cutUp.size(); ++i)
    worst = std::max(worst, std::abs((double)honest[i] - cutUp[i]));

  std::printf("  4096 frames promised as 4096 against promised as 256: worst "
              "sample difference %.2e\n",
              worst);

  check(worst < 1.0e-6,
        "a block cut into pieces sounds the same as one that was not");

  // And it made sound at all, so the comparison is not two silences.
  double loudest = 0.0;
  for (auto v : honest)
    loudest = std::max(loudest, std::abs((double)v));

  check(loudest > 0.01,
        "the passage is audible (" + std::to_string(loudest) + ")");

  // A block far larger than the floor the scratch is given, so it really is
  // cut into many pieces rather than one or two.
  const auto many = render(64, 8192);
  check(many.size() == (size_t)8192 * 2 && std::isfinite(many[0]),
        "and a block many times the promised size still renders");

  p.setRateAndBufferSizeDetails(48000.0, 512);
  p.prepareToPlay(48000.0, 512);
}

void testLinkCurves() {
  section("Link scopes and curves");

  using namespace ovt::ui;

  // Every curve has to leave things exactly where they were at zero delta, or a
  // drag could not be undone by returning the knob.
  for (int c = 0; c < (int)LinkCurve::NumCurves; ++c) {
    const auto curve = (LinkCurve)c;
    bool exact = true;

    for (float base : {0.0f, 0.3f, 0.75f, 1.0f})
      exact &= std::abs(linkedValue(curve, base, 0.0f, 1.0f, 0.7f, 0.5f) -
                        base) < 1.0e-6f;

    check(exact, std::string(linkCurveName(curve)) + ": zero delta is a no-op");
  }

  // The strip being dragged must always come out at exactly its full share,
  // whatever the curve and wherever it sits. It follows the mouse, so anything
  // else would put it out of step with every strip around it.
  bool anchored = true;
  for (int c = 0; c < (int)LinkCurve::NumCurves; ++c)
    for (int src : {0, 7, 16, 31})
      anchored &=
          std::abs(linkCurveWeight((LinkCurve)c, src, src) - 1.0f) < 1.0e-6f;

  check(anchored,
        "the dragged strip's own weight is exactly 1 for every curve");

  // Taper falls away from wherever the grab is, by distance alone.
  const auto taper = [](int i, int src) {
    return linkCurveWeight(LinkCurve::Taper, i, src);
  };

  bool fallsOff = true;
  bool symmetric = true;

  for (int src : {0, 9, 15, 31}) {
    for (int i = 1; i < ovt::kNumHarmonics; ++i) {
      // Strictly further from the grab means strictly less share.
      const auto nearer = std::abs(i - 1 - src);
      const auto further = std::abs(i - src);

      if (further > nearer)
        fallsOff &= taper(i, src) < taper(i - 1, src);
      else if (further < nearer)
        fallsOff &= taper(i, src) > taper(i - 1, src);
    }

    // Equal distances either side of the grab get equal shares.
    for (int d = 1; d < ovt::kNumHarmonics; ++d)
      if (src - d >= 0 && src + d < ovt::kNumHarmonics)
        symmetric &=
            std::abs(taper(src - d, src) - taper(src + d, src)) < 1.0e-6f;
  }

  check(fallsOff, "taper gives a strip less the further it sits from the grab");
  check(symmetric, "taper depends on distance alone, not on direction");

  std::printf("  taper grabbing h1: h1 %.2f h16 %.2f h32 %.2f\n", taper(0, 0),
              taper(15, 0), taper(31, 0));
  std::printf("  taper grabbing h10: h1 %.2f h10 %.2f h32 %.2f\n", taper(0, 9),
              taper(9, 9), taper(31, 9));

  // Grabbing either end is the old pair of tilts: a straight ramp across the
  // whole mixer, one way or the other.
  check(taper(0, 0) > 0.99f && taper(31, 0) < 0.01f,
        "grabbing the first channel tilts the mixer down across its width");
  check(taper(31, 31) > 0.99f && taper(0, 31) < 0.01f,
        "grabbing the last channel tilts it up instead");

  // Grabbing anywhere between is what the tilts could not do.
  check(taper(9, 9) > taper(0, 9) && taper(9, 9) > taper(31, 9),
        "grabbing the middle peaks under the hand and falls away both ways");
  check(taper(0, 9) > taper(31, 9),
        "and leans towards whichever end the grab is nearer");

  check(std::abs(linkCurveWeight(LinkCurve::Uniform, 5, 20) - 1.0f) < 1.0e-6f,
        "uniform weights every strip equally");

  // Spread scatters upwards along each strip's own direction.
  const auto up = linkedValue(LinkCurve::Spread, 0.5f, 0.2f, 1.0f, 1.0f, 0.7f);
  const auto down =
      linkedValue(LinkCurve::Spread, 0.5f, 0.2f, 1.0f, -1.0f, 0.7f);

  check(up > 0.5f && down < 0.5f,
        "pushing up scatters strips in both directions");
  check(std::abs((up - 0.5f) + (down - 0.5f)) < 1.0e-6f,
        "opposite directions scatter by equal amounts");

  // Gathering collapses onto the dragged strip, from either side of it.
  const float target = 0.4f;
  const auto above =
      linkedValue(LinkCurve::Spread, 0.9f, -0.5f, 1.0f, 1.0f, target);
  const auto below =
      linkedValue(LinkCurve::Spread, 0.1f, -0.5f, 1.0f, 1.0f, target);

  check(std::abs(above - target) < 1.0e-5f &&
            std::abs(below - target) < 1.0e-5f,
        "half a drag down gathers everything onto the dragged strip");

  const auto partly =
      linkedValue(LinkCurve::Spread, 0.9f, -0.125f, 1.0f, 1.0f, target);
  check(partly < 0.9f && partly > target, "gathering is gradual, not a snap");

  // Nothing may leave the parameter's range.
  check(linkedValue(LinkCurve::Uniform, 0.9f, 0.5f, 1.0f, 0.0f, 0.5f) <= 1.0f &&
            linkedValue(LinkCurve::Uniform, 0.1f, -0.5f, 1.0f, 0.0f, 0.5f) >=
                0.0f,
        "results stay inside the parameter range");

  // ---- the faders, which are shared out in decibels ------------------------
  //
  // Every other row is moved across its travel, which is even in whatever it
  // measures. A level fader's travel is shaped to feel right under a finger
  // instead, so an even move across it is a wildly uneven move in level: a
  // drag that lifted the loudest channel by seven decibels lifted the quietest
  // by thirty. What a uniform drag has to mean here is the same number of
  // decibels on every channel, which is the same gain on every channel, which
  // is the only move that leaves the balance of a patch alone.
  check(linkIsDecibels(Role::Volume, LinkCurve::Uniform) &&
            linkIsDecibels(Role::Volume, LinkCurve::Taper),
        "the faders share out an amount in decibels");

  check(!linkIsDecibels(Role::Volume, LinkCurve::Spread),
        "except when scattering, which is about where things sit rather than "
        "how loud they are");

  check(!linkIsDecibels(Role::Tune, LinkCurve::Uniform) &&
            !linkIsDecibels(Role::PmRate, LinkCurve::Uniform),
        "and no other row does, since their travel already is what they "
        "measure");

  {
    // Two channels twelve decibels apart, lifted by six. Both have to arrive
    // six decibels up, which is to say twelve apart still.
    constexpr float low = -30.0f, high = -18.0f, lift = 6.0f;

    const auto landedLow =
        linkedValue(LinkCurve::Uniform, low, lift, 1.0f, 0.0f, high + lift,
                    ovt::params::kQuietestLevelDb, 0.0f);

    const auto landedHigh =
        linkedValue(LinkCurve::Uniform, high, lift, 1.0f, 0.0f, high + lift,
                    ovt::params::kQuietestLevelDb, 0.0f);

    check(std::abs(landedLow - (low + lift)) < 1.0e-4f &&
              std::abs(landedHigh - (high + lift)) < 1.0e-4f,
          "so both channels come up by the amount that was dragged");

    check(std::abs((landedHigh - landedLow) - (high - low)) < 1.0e-4f,
          "and the interval between them is exactly what it was");

    // A fader at the bottom is off, and the top of the range is unity.
    check(linkedValue(LinkCurve::Uniform, -6.0f, 40.0f, 1.0f, 0.0f, 0.0f,
                      ovt::params::kQuietestLevelDb, 0.0f) <= 0.0f,
          "nothing is dragged past unity");
  }

  // Same-interval scope has to pick out a real family. The octaves are the
  // partials at powers of two.
  int octaves = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (ovt::harmonic(i).pitchClass == ovt::harmonic(0).pitchClass)
      ++octaves;

  check(octaves == 6,
        "same interval on the fundamental selects the 6 octaves (" +
            std::to_string(octaves) + ")");

  int fifths = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (ovt::harmonic(i).pitchClass == ovt::harmonic(2).pitchClass)
      ++fifths;

  check(fifths == 4,
        "same interval on the third partial selects the 4 fifths (" +
            std::to_string(fifths) + ")");
}

void testRowHover() {
  section("Row hover");

  using namespace ovt::ui;

  // The same rectangle a strip lays its rows out in, at the height a strip
  // actually asks for. Taking that from preferredStripHeight rather than
  // writing a number here means adding a row cannot quietly push the last one
  // off the bottom of the test without the test noticing.
  const auto rows =
      layoutRows(juce::Rectangle<int>(0, 0, kStripWidth,
                                      preferredStripHeight() + 2 * kStripPadY)
                     .reduced(kStripPadX, kStripPadY));

  const auto rowAtCentre = [&rows](Row r) {
    const auto band = rows[(size_t)r];
    return controlRowAt(rows, {kStripWidth / 2, band.getCentreY()});
  };

  bool identity = true;
  for (Row r : {Row::TuneKnob,   Row::Phase,   Row::PmRate,   Row::PmDepth,
                Row::Drift,      Row::Strike,  Row::Delay,    Row::Attack,
                Row::Decay,      Row::Sustain, Row::Swell,    Row::OffLevel,
                Row::Release,    Row::AmRate,  Row::AmDepth,  Row::Velocity,
                Row::Aftertouch, Row::Pan,     Row::MuteSolo, Row::Fader})
    identity &= rowAtCentre(r) == r;

  check(identity, "every control row reports itself");

  bool quiet = true;
  for (Row r : {Row::Header, Row::PitchModHeading, Row::EnvHeading,
                Row::KeyOffHeading, Row::AmpModHeading, Row::OutputHeading})
    quiet &= rowAtCentre(r) == kNoRow;

  check(quiet, "the header and the section rules report nothing");

  check(rowAtCentre(Row::TuneText) == Row::TuneKnob &&
            rowAtCentre(Row::FaderText) == Row::Fader,
        "the readouts report the control above them");

  // Off the top and bottom of the laid-out area there is nothing to point at.
  check(controlRowAt(rows, {kStripWidth / 2, -20}) == kNoRow &&
            controlRowAt(rows, {kStripWidth / 2, 4000}) == kNoRow,
        "a point outside the rows reports nothing");

  // Anything the highlight can land on needs a caption in the gutter, or the
  // band would light up with nothing at the end of it.
  bool named = true;
  for (int y = rows[0].getY(); y < rows[kNumRows - 1].getBottom(); ++y) {
    const auto r = controlRowAt(rows, {kStripWidth / 2, y});

    if (r != kNoRow)
      named &= rowLabel(r) != nullptr;
  }

  check(named, "every row the pointer can land on has a caption");

  // LINK works in roles, the pointer works in rows, and the two have to line up
  // one for one or a preview would arm the wrong knob.
  std::array<int, (size_t)kNumRoles> found{};
  Role role{};

  for (int i = 0; i < kNumRows; ++i)
    if (roleForRow((Row)i, role))
      ++found[(size_t)role];

  bool oneEach = true;
  for (auto n : found)
    oneEach &= n == 1;

  check(oneEach, "each of the " + std::to_string((int)kNumRoles) +
                     " linkable roles sits on exactly one row");

  check(!roleForRow(Row::MuteSolo, role),
        "the mute and solo row carries no linkable role");

  // The two rows nobody has to hunt for are left alone.
  check(!rowShowsHighlight(Row::Fader) && !rowShowsHighlight(Row::MuteSolo),
        "the faders and the mute and solo buttons take no highlight");

  check(!rowShowsHighlight(kNoRow), "nothing highlights nothing");

  // Suppressing the band must not cost the fader its LINK preview.
  check(roleForRow(Row::Fader, role) && role == Role::Volume,
        "the fader row still reports the role LINK would gang");
}

void testUserPresets(OvertoniumProcessor &p) {
  section("User presets");

  using namespace ovt;

  // ---- names a filesystem can carry ----------------------------------------
  check(presets::sanitiseName("Glass Bells") == "Glass Bells",
        "an ordinary name survives");
  check(presets::sanitiseName("  padded  ") == "padded",
        "the edges are trimmed, so two presets cannot look identical");
  check(presets::sanitiseName("a/b:c*d?e") == "abcde",
        "what a path separator would break is removed");
  check(presets::sanitiseName("   ").isEmpty(), "and nothing is not a name");
  check(
      presets::sanitiseName(juce::String::repeatedString("x", 200)).length() ==
          64,
      "absurd names are cut down to something a menu can show");

  // ---- capture and restore --------------------------------------------------
  presets::apply(p.apvts, presetIndex("Init"));

  auto *tune = p.apvts.getParameter(params::oscParamId(params::tuneSuffix, 4));
  auto *level =
      p.apvts.getParameter(params::oscParamId(params::offLevelSuffix, 4));
  auto *decay = p.apvts.getParameter(params::reverbDecayId);

  tune->setValueNotifyingHost(tune->convertTo0to1(0.25f));
  level->setValueNotifyingHost(level->convertTo0to1(0.8f));
  decay->setValueNotifyingHost(decay->convertTo0to1(7.5f));

  const auto captured = presets::capture(p.apvts, "Round trip");
  check(captured != nullptr && captured->getNumChildElements() > 600,
        "every parameter is captured (" +
            std::to_string(captured != nullptr ? captured->getNumChildElements()
                                               : 0) +
            ")");

  // Move everything somewhere else, then put it back.
  presets::apply(p.apvts, presetIndex("Drawbar Organ"));

  check(std::abs(tune->convertFrom0to1(tune->getValue()) - 0.25f) > 0.1f,
        "the test actually disturbed the values it is about to restore");

  const auto applied = presets::restore(p.apvts, *captured);
  check(applied == captured->getNumChildElements(),
        "restoring recognises everything it wrote");

  check(std::abs(tune->convertFrom0to1(tune->getValue()) - 0.25f) < 1.0e-4f,
        "a per-partial value comes back");
  check(std::abs(level->convertFrom0to1(level->getValue()) - 0.8f) < 1.0e-4f,
        "including one added later");
  check(std::abs(decay->convertFrom0to1(decay->getValue()) - 7.5f) < 1.0e-3f,
        "and an effect value comes back");

  // ---- what a file from another build looks like ---------------------------
  {
    juce::XmlElement partial("OVERTONIUM_PRESET");
    auto *entry = partial.createNewChildElement("PARAM");
    entry->setAttribute("id", params::oscParamId(params::tuneSuffix, 4));
    entry->setAttribute("value", 0.5);

    auto *unknown = partial.createNewChildElement("PARAM");
    unknown->setAttribute("id", "h01_somethingWeRemoved");
    unknown->setAttribute("value", 1.0);

    const auto before =
        level->convertFrom0to1(level->getValue()); // untouched by the file

    check(presets::restore(p.apvts, partial) == 1,
          "a file from another build applies what it knows");
    check(std::abs(level->convertFrom0to1(level->getValue()) - before) <
              1.0e-6f,
          "and leaves the rest alone rather than resetting it");

    juce::XmlElement foreign("SOMETHING_ELSE");
    check(presets::restore(p.apvts, foreign) < 0,
          "a document that is not ours is refused");
  }

  // ---- through a real file --------------------------------------------------
  {
    const auto file = juce::File::createTempFile(".ovtpreset");

    const auto doc = presets::capture(p.apvts, "On disk");
    check(doc->writeTo(file), "a preset writes to disk");

    presets::apply(p.apvts, presetIndex("Struck Bell"));

    juce::String error;
    check(presets::load(p.apvts, file, error),
          "and loads back: " + error.toStdString());

    check(std::abs(tune->convertFrom0to1(tune->getValue()) - 0.5f) < 1.0e-4f,
          "with the values it was carrying");

    file.deleteFile();

    check(!presets::load(p.apvts, file, error),
          "a file that is not there fails rather than crashing");
    check(error.isNotEmpty(), "and says why");
  }

  // ---- the factory code generator ------------------------------------------
  {
    presets::apply(p.apvts, presetIndex("Init"));

    auto *drift =
        p.apvts.getParameter(params::oscParamId(params::driftSuffix, 0));
    drift->setValueNotifyingHost(drift->convertTo0to1(12.0f));

    const auto code = presets::factoryCode(p.apvts, "Test Patch");

    check(code.contains("case N: // Test Patch"), "the case is named");
    check(code.contains("ap.neutralBase();"),
          "it starts from the neutral base");
    check(code.contains(params::oscParamId(params::driftSuffix, 0)),
          "and carries the value that was changed");

    // Only the differences, or the case would be 640 lines of noise.
    check(!code.contains(params::oscParamId(params::tuneSuffix, 7)),
          "but not the ones left at their default");
  }

  presets::apply(p.apvts, presetIndex("Init"));
}

void testMasterEffects(OvertoniumProcessor &p) {
  section("Master effects");

  const auto setParam = [&p](const char *id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  /// Plays a short note, lets it go, and reports what is still coming out a
  /// second after the release has finished.
  const auto tailAfterNote = [&]() {
    p.prepareToPlay(48000.0, 512);
    p.reset();

    renderBlocks(p, 20, 512, noteOnAt(60, 0.9f, 0));

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    juce::AudioBuffer<float> buffer(2, 512);
    p.processBlock(buffer, off);

    // Long enough for the longest release in the Init patch to be over.
    renderBlocks(p, 120, 512);

    return renderBlocks(p, 40, 512).peak;
  };

  ovt::presets::apply(p.apvts, presetIndex("Init"));

  const auto dry = tailAfterNote();
  check(dry < 1.0e-5f, "with the effects off the note stops when it stops");

  setParam(ovt::params::reverbOnId, 1.0f);
  setParam(ovt::params::reverbMixId, 0.5f);
  setParam(ovt::params::reverbDecayId, 6.0f);
  setParam(ovt::params::reverbDampId, 0.2f);

  const auto wet = tailAfterNote();
  check(wet > 1.0e-4f, "the reverb goes on ringing after the note (peak " +
                           std::to_string(wet) + ")");

  check(p.getTailLengthSeconds() > 5.0,
        "the reported tail covers the reverb (" +
            std::to_string(p.getTailLengthSeconds()) + " s)");

  setParam(ovt::params::reverbOnId, 0.0f);

  const auto bypassed = tailAfterNote();
  check(bypassed < 1.0e-5f, "switching it off takes the tail with it");

  // The echo has to put something audible on the far side of the note too.
  setParam(ovt::params::echoOnId, 1.0f);
  setParam(ovt::params::echoMixId, 0.6f);
  setParam(ovt::params::echoTimeId, 0.4f);
  setParam(ovt::params::echoFeedbackId, 0.7f);

  const auto echoed = tailAfterNote();
  check(echoed > 1.0e-4f, "the echo repeats after the note (peak " +
                              std::to_string(echoed) + ")");

  check(p.getTailLengthSeconds() > 2.0,
        "the reported tail covers the repeats (" +
            std::to_string(p.getTailLengthSeconds()) + " s)");

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  p.reset();

  check(tailAfterNote() < 1.0e-5f, "Init puts both of them away again");
}

void testMeterRepaint() {
  section("Meter repaints");

  using namespace ovt::ui;

  LevelMeter meter(juce::Colour(0xff62bbd9));
  meter.setSize(30, 251);

  const auto render = [&meter] {
    juce::Image image(juce::Image::ARGB, meter.getWidth(), meter.getHeight(),
                      true);
    juce::Graphics g(image);
    meter.paintEntireComponent(g, false);

    return image;
  };

  /// The rows that actually differ between two renders, as [first, last).
  ///
  /// Kept as two plain ints: a juce::Range built backwards to mean "empty"
  /// collapses to (height, height) instead, and then every union with it comes
  /// back claiming the whole meter changed.
  const auto changedRows = [&meter](const juce::Image &a,
                                    const juce::Image &b) {
    int first = -1, last = -1;

    for (int y = 0; y < meter.getHeight(); ++y)
      for (int x = 0; x < meter.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) {
          if (first < 0)
            first = y;

          last = y;
          break;
        }

    return first < 0 ? juce::Range<int>() : juce::Range<int>(first, last + 1);
  };

  // Levels worth walking through: a note arriving, several frames of decay,
  // silence, and the ends of the range.
  const float levels[] = {0.0f, 0.9f,  0.85f, 0.6f, 0.35f,  0.2f,
                          0.1f, 0.02f, 0.0f,  1.0f, 0.999f, 0.5f};

  bool covered = true;
  int worstTouched = 0, largestChange = 0;

  for (auto level : levels) {
    const auto before = render();
    const auto band = meter.push(level);
    const auto after = render();

    const auto rows = changedRows(before, after);

    if (rows.getStart() >= rows.getEnd())
      continue; // nothing moved, nothing to cover

    const bool ok = !band.isEmpty() && band.getY() <= rows.getStart() &&
                    band.getBottom() >= rows.getEnd();

    if (!ok)
      std::printf("  rows %d..%d changed, band covers %d..%d\n",
                  rows.getStart(), rows.getEnd(), band.getY(),
                  band.getBottom());

    covered &= ok;
    worstTouched = std::max(worstTouched, band.getHeight());
    largestChange = std::max(largestChange, rows.getLength());
  }

  check(covered, "the dirty band covers every pixel that changes");

  std::printf("  worst case %d px repainted, largest real change %d px, of "
              "%d\n",
              worstTouched, largestChange, meter.getHeight());

  // The other half of the claim: a frame that reports nothing to repaint must
  // genuinely have nothing to repaint, or the meter freezes at a stale value.
  {
    bool honest = true;
    int quiet = 0, total = 0;

    meter.push(1.0f);
    meter.push(1.0f);

    // A slow decay, the way a released note actually falls.
    for (float level = 1.0f; level > 0.01f; level *= 0.97f) {
      const auto before = render();
      const auto band = meter.push(level);
      const auto after = render();

      ++total;

      if (band.isEmpty()) {
        ++quiet;
        honest &= changedRows(before, after).isEmpty();
      }
    }

    check(honest, "a frame that reports no repaint really did not change");

    std::printf("  a slow decay: %d of %d frames needed no repaint at all\n",
                quiet, total);

    check(quiet * 2 > total, "and most frames of a decay need none (" +
                                 std::to_string(quiet) + " of " +
                                 std::to_string(total) + ")");
  }

  // And when a frame does have something to say, it says it about one lamp
  // rather than about the whole column.
  {
    meter.push(1.0f);
    meter.push(1.0f);

    juce::Rectangle<int> crossing;

    for (float level = 1.0f; level > 0.01f && crossing.isEmpty();
         level *= 0.97f)
      crossing = meter.push(level);

    check(!crossing.isEmpty() && crossing.getHeight() < meter.getHeight() / 4,
          "a frame that does repaint touches one lamp (" +
              std::to_string(crossing.getHeight()) + " px of " +
              std::to_string(meter.getHeight()) + ")");
  }
}

void testLinkMenu() {
  section("Link menu");

  using namespace ovt::ui;

  LinkSettings settings;
  settings.enabled = false;
  settings.scope = LinkScope::Odd;
  settings.curve = LinkCurve::Taper;

  auto menu = buildLinkMenu(settings);

  int items = 0, headers = 0, ticked = 0, enabled = 0;
  std::string tickedNames;

  for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
    const auto &item = it.getItem();

    if (item.isSectionHeader) {
      ++headers;
      continue;
    }

    if (item.itemID == 0)
      continue; // separator

    ++items;

    if (item.isTicked) {
      ++ticked;
      tickedNames += item.text.toStdString() + " ";
    }

    if (item.isEnabled)
      ++enabled;
  }

  check(headers == 2, "the menu is in two named sections");
  check(items == 1 + (int)LinkScope::NumScopes + (int)LinkCurve::NumCurves,
        "the switch, every scope and every curve are all there (" +
            std::to_string(items) + ")");

  // Exactly the two current settings are ticked, and nothing else.
  check(ticked == 2, "two items are ticked");
  check(tickedNames == "Odd harmonics Taper from the grab ",
        "and they are the current ones: " + tickedNames);

  // With LINK off the lists are shown but greyed, so the menu does not change
  // shape as the switch moves.
  check(enabled == 1, "with LINK off only the switch itself can be picked");

  settings.enabled = true;
  int enabledOn = 0;
  auto onMenu = buildLinkMenu(settings);

  for (juce::PopupMenu::MenuItemIterator it(onMenu); it.next();)
    if (it.getItem().itemID != 0 && it.getItem().isEnabled &&
        !it.getItem().isSectionHeader)
      ++enabledOn;

  check(enabledOn == items, "with LINK on every item can be picked");

  // ---- what the menu does when something is chosen --------------------------
  LinkSettings state;

  check(!applyLinkMenuChoice(0, state), "dismissing the menu changes nothing");
  check(!applyLinkMenuChoice(9999, state), "an id from elsewhere is ignored");

  check(applyLinkMenuChoice(1, state) && state.enabled,
        "the first item is the switch");
  check(applyLinkMenuChoice(1, state) && !state.enabled,
        "and it toggles rather than setting");

  // Every scope and every curve has to come back as itself, which is what the
  // two id ranges are for.
  bool roundTrip = true;

  for (int i = 0; i < (int)LinkScope::NumScopes; ++i) {
    applyLinkMenuChoice(100 + i, state);
    roundTrip &= state.scope == (LinkScope)i;
  }

  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i) {
    applyLinkMenuChoice(200 + i, state);
    roundTrip &= state.curve == (LinkCurve)i;
  }

  check(roundTrip, "every scope and curve comes back as the one picked");

  // Picking a curve must not disturb the scope, or the two lists would fight.
  state.scope = LinkScope::SameInterval;
  check(applyLinkMenuChoice(200 + (int)LinkCurve::Spread, state),
        "the last curve's id is still one of ours");
  check(state.scope == LinkScope::SameInterval,
        "picking a curve leaves the scope alone");

  // ---- what a saved state carries -------------------------------------------
  //
  // The curve is written by name, because the list has changed once already and
  // an index written by an older version no longer means the same curve.
  bool names = true;
  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    names &= linkCurveFromState(linkCurveId((LinkCurve)i), -1) == (LinkCurve)i;

  check(names, "every curve round-trips through its saved name");

  check(linkCurveFromState("", -1) == LinkCurve::Uniform,
        "a state with no curve at all opens on Uniform");
  check(linkCurveFromState("not a curve", -1) == LinkCurve::Uniform,
        "and so does a name this version does not know");

  // The old list ran Uniform, Tilt up, Tilt down, Spread. Both tilts are what
  // Taper replaced, and Spread has to move down a place rather than stay at an
  // index that now means something else.
  check(linkCurveFromState("", 0) == LinkCurve::Uniform,
        "an old state's Uniform still opens on Uniform");
  check(linkCurveFromState("", 1) == LinkCurve::Taper,
        "an old state's Tilt up opens on Taper");
  check(linkCurveFromState("", 2) == LinkCurve::Taper,
        "an old state's Tilt down opens on Taper as well");
  check(linkCurveFromState("", 3) == LinkCurve::Spread,
        "an old state's Spread is still Spread, not the curve at index 3");

  // A state holding both answers takes the name, since that is the one this
  // version wrote.
  check(linkCurveFromState("spread", 1) == LinkCurve::Spread,
        "the name wins over a leftover index");
}

void testTopBarLayout() {
  section("Top bar layout");

  using ovt::ui::TopBar;

  const auto minimum = TopBar::minimumWidth();
  const auto tallest = TopBar::heightForWidth(minimum);

  std::printf("  minimum width %d, bar %d px there, %d px at 1400\n", minimum,
              tallest, TopBar::heightForWidth(1400));

  check(minimum > 0 && minimum < 1100,
        "the bar fits in a sensible minimum width (" + std::to_string(minimum) +
            ")");

  check(tallest <= 3 * 54 + 2 * 4 + 2 * 6,
        "and takes no more than three rows there (" + std::to_string(tallest) +
            " px)");

  // Wider windows must never need more rows than narrow ones.
  int previous = tallest;
  bool monotonic = true;

  for (int width = minimum; width <= 2400; width += 17) {
    const auto rows = TopBar::heightForWidth(width);
    monotonic &= rows <= previous;
    previous = rows;
  }

  check(monotonic, "the bar never grows taller as the window grows wider");

  check(TopBar::heightForWidth(2400) < TopBar::heightForWidth(minimum),
        "and it is shorter on a wide window than on a narrow one");
}

/// A knob carries its caption underneath, so its dial does not sit in the
/// middle of the row. Anything laid out down the middle instead reads as
/// sagging next to the knobs, which is easy to reintroduce by adding a control
/// and centring it, and hard to notice in a screenshot.
/// What the SETTINGS menu is, and the order it says it in.
///
/// The order is the whole point of the grouping, and nothing else would notice
/// if it drifted: the menu is only reachable by clicking, and a click needs a
/// window the tests do not have.
/// The preset menu is two submenus and the actions, not one long list.
///
/// Thirty factory presets and however many of your own will not fit on a
/// short screen, and a menu that scrolls hides its own shape. What is checked
/// here is the shape: that the top level stays short whatever is saved, that
/// every factory preset is reachable one level down, and that a folder in the
/// preset directory becomes a group rather than being flattened away or, worse,
/// its presets going missing.
void testPresetMenuGroups(OvertoniumProcessor &p) {
  section("Preset menu");

  using namespace ovt::ui;

  // Real files in the real preset folder, since that is what the menu reads.
  // Named so that nothing anyone has actually saved could collide with them,
  // and removed at the end whatever happens.
  const auto root = ovt::presets::userDirectory();
  const auto group = root.getChildFile("ZZ Test Group");
  const auto nested = group.getChildFile("Deeper");

  const auto loose = root.getChildFile("ZZ Test Loose.ovtpreset");
  const auto inGroup = group.getChildFile("ZZ Test Grouped.ovtpreset");
  const auto inNested = nested.getChildFile("ZZ Test Nested.ovtpreset");

  nested.createDirectory();

  const auto doc = ovt::presets::capture(p.apvts, "ZZ Test");
  for (const auto &f : {loose, inGroup, inNested})
    doc->writeTo(f);

  {
    juce::Component popupParent;
    TopBar bar(p.apvts, popupParent);

    auto menu = bar.buildPresetMenu();

    std::vector<std::string> top;
    juce::PopupMenu factory, saved;

    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
      const auto &item = it.getItem();

      if (item.itemID == 0 && item.subMenu == nullptr)
        continue;

      top.push_back(item.text.toStdString());

      if (item.text == "Factory" && item.subMenu != nullptr)
        factory = *item.subMenu;
      else if (item.text == "Saved" && item.subMenu != nullptr)
        saved = *item.subMenu;
    }

    // Factory, Saved, and the two actions. The authoring build adds a third,
    // which is why this is a ceiling rather than an equality.
    check(top.size() <= 5, "the top level stays short (" +
                               std::to_string(top.size()) + " entries)");
    check(!top.empty() && top[0] == "Factory",
          "the factory list is one entry, not twenty-six");
    check(top.size() > 1 && top[1] == "Saved",
          "and the saved presets are another");

    int factoryItems = 0;
    for (juce::PopupMenu::MenuItemIterator it(factory); it.next();)
      if (it.getItem().itemID != 0)
        ++factoryItems;

    check(factoryItems == ovt::presets::names().size(),
          "every factory preset is one level down (" +
              std::to_string(factoryItems) + ")");

    // The saved side: one loose preset and one folder, which itself holds a
    // preset and another folder.
    std::vector<std::string> savedTop;
    juce::PopupMenu groupMenu;

    for (juce::PopupMenu::MenuItemIterator it(saved); it.next();) {
      const auto &item = it.getItem();
      savedTop.push_back(item.text.toStdString());

      if (item.text == "ZZ Test Group" && item.subMenu != nullptr)
        groupMenu = *item.subMenu;
    }

    const auto has = [](const std::vector<std::string> &v,
                        const std::string &s) {
      return std::find(v.begin(), v.end(), s) != v.end();
    };

    check(has(savedTop, "ZZ Test Group"), "a folder becomes a group");
    check(has(savedTop, "ZZ Test Loose"),
          "and a preset beside it is still listed");

    // Folders come before the presets loose in the same level.
    check(!savedTop.empty() && savedTop[0] == "ZZ Test Group",
          "groups are listed before loose presets");

    std::vector<std::string> inside;
    for (juce::PopupMenu::MenuItemIterator it(groupMenu); it.next();)
      inside.push_back(it.getItem().text.toStdString());

    check(has(inside, "ZZ Test Grouped"), "the group holds its own preset");
    check(has(inside, "Deeper"), "and a folder inside it nests again");
  }

  loose.deleteFile();
  group.deleteRecursively();

  check(!loose.existsAsFile() && !group.exists(),
        "and the test leaves nothing behind in the preset folder");
}

/// A new instance is on no preset, whatever number it has to report.
///
/// getCurrentProgram has to answer from the moment the plugin exists and zero
/// is the only honest number, but the sound is the parameter defaults rather
/// than the first preset. A host draws its menu from that number, so the first
/// entry looks selected on an instance that has never loaded it, and picking it
/// used to be swallowed by the guard that protects a restored session.
void testFirstProgramIsReachable() {
  section("The first preset on a new instance");

  OvertoniumProcessor fresh;

  auto *volume = fresh.apvts.getParameter(
      ovt::params::oscParamId(ovt::params::volumeSuffix, 0));

  const auto before = volume->getValue();

  check(fresh.getCurrentProgram() == 0,
        "a new instance reports the first program, since it must report one");
  check(fresh.presetName().isEmpty(),
        "but nothing is loaded, which is what the button shows");

  // Exactly what a host does when someone picks the first entry.
  fresh.setCurrentProgram(0);

  check(std::abs(volume->getValue() - before) > 1.0e-4f,
        "picking it from the host loads it rather than doing nothing");
  check(fresh.presetName() == ovt::presets::names()[0],
        "and the name follows (" + fresh.presetName().toStdString() + ")");

  // And the guard it must not have broken: asking again for the one already
  // loaded still does nothing, which is what keeps a restored session intact.
  const auto loaded = volume->getValue();
  volume->setValueNotifyingHost(loaded > 0.5f ? 0.1f : 0.9f);
  const auto editedTo = volume->getValue();

  fresh.setCurrentProgram(0);

  check(std::abs(volume->getValue() - editedTo) < 0.005f,
        "and asking for it a second time leaves an edit alone");
}

/// The preset button survives the window being shut.
///
/// A window is opened and closed far more often than a preset is chosen, and a
/// session restored from disk has no window at all until someone asks for one.
/// The name lives on the processor for that reason: the editor cannot work it
/// out, since a preset of your own has no program index to be found by.
void testPresetNameOutlivesTheWindow() {
  section("The preset name outlives the window");

  const auto shownBy = [](OvertoniumProcessor &proc) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    sizeEditor(*ed, 1340);

    auto *bar = findTopBar(*ed);
    return bar != nullptr ? bar->getPresetName() : juce::String("(no bar)");
  };

  OvertoniumProcessor proc;

  check(shownBy(proc).isEmpty(),
        "a window on a new instance shows nothing loaded");

  const auto wanted = ovt::presets::names()[13];
  proc.applyFactoryPreset(13);

  check(shownBy(proc) == wanted,
        "a window opened after a preset was loaded shows it (" +
            shownBy(proc).toStdString() + ")");

  // A preset of your own, which has no program index at all.
  proc.setLoadedPresetName("Something Of My Own");
  check(shownBy(proc) == "Something Of My Own",
        "and so does one named by a user preset");

  // And it travels, so a session reopened a week later still says so.
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);

  OvertoniumProcessor restored;
  restored.setStateInformation(saved.getData(), (int)saved.getSize());

  check(restored.presetName() == "Something Of My Own",
        "the name survives a state round trip (" +
            restored.presetName().toStdString() + ")");
  check(shownBy(restored) == "Something Of My Own",
        "and a window opened on the restored session shows it");
}

/// The glyph is drawn from the parameter, never from a remembered choice.
///
/// A preset always did reset the parameter. What it did not reset was the
/// picture, because nothing told the button to paint again when something
/// other than a click moved the value. The repaint itself is scheduled through
/// a ParameterAttachment and is not observable from here: there is no peer to
/// collect a dirty region and no message loop to deliver the callback.
///
/// What is observable, and what this holds, is that the button never caches
/// the shape. Anyone later storing it in a member set only from the menu would
/// reintroduce exactly the bug this fixed, and would fail here.
void testShapeButtonFollowsTheParameter(OvertoniumProcessor &p) {
  section("The shape glyph follows the parameter");

  using namespace ovt::ui;

  std::function<std::vector<ShapeButton *>(juce::Component &)> gather =
      [&gather](juce::Component &c) {
        std::vector<ShapeButton *> found;

        if (auto *b = dynamic_cast<ShapeButton *>(&c))
          found.push_back(b);

        for (auto *child : c.getChildren())
          for (auto *b : gather(*child))
            found.push_back(b);

        return found;
      };

  std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
  sizeEditor(*ed, 1340);

  const auto buttons = gather(*ed);

  // Two per strip across the series, plus the noise channel's tremolo.
  check(buttons.size() == (size_t)(ovt::kNumHarmonics * 2 + 1),
        "every channel has its shape controls (" +
            std::to_string(buttons.size()) + ")");

  const auto pmId = ovt::params::oscParamId(ovt::params::pmShapeSuffix, 0);
  auto *param = p.apvts.getParameter(pmId);

  const auto shown = [&buttons]() {
    for (auto *b : buttons)
      if (b->getTitle().contains("Harmonic 1 pitch"))
        return b->currentName();

    return juce::String("(not found)");
  };

  check(param != nullptr, "the first partial has a pitch shape");

  // Moved the way a host or an automation lane would, not through the button.
  param->setValueNotifyingHost(param->convertTo0to1(4.0f));

  check(shown() != "Sine",
        "a change from outside is what the button reports (" +
            shown().toStdString() + ")");

  // And a factory preset, which is how this was noticed. Every preset that
  // ships leaves both modulators on sine, so this is also the check that they
  // sound as they did before shapes existed.
  p.applyFactoryPreset(ovt::presets::names().indexOf("Drawbar Organ"));

  check(shown() == "Sine", "and loading a preset puts it back to sine (" +
                               shown().toStdString() + ")");
}

/// A preset tells the host what changed, and nothing else.
///
/// This instrument has 782 parameters, which is far outside what a host
/// expects, and a preset is written as a neutral base plus the rows that
/// differ. Sent as they were decided that is over a thousand parameter
/// changes for one preset, most of them either immediately overwritten or not
/// changes at all. A host keeps per-parameter bookkeeping for automation and
/// undo and is entitled to believe every one of them.
///
/// So the applier collects and sends once, skipping anything already at the
/// value asked for. What is held here is the property rather than a number:
/// no parameter is written twice, and nothing is reported that did not move.
void testPresetsTellTheHostOnlyWhatChanged(OvertoniumProcessor &p) {
  section("A preset reports only what it changed");

  struct Counter : juce::AudioProcessorListener {
    int changes = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor *, int,
                                        float) override {
      ++changes;
    }
    void audioProcessorChanged(juce::AudioProcessor *,
                               const ChangeDetails &) override {}
  };

  const auto snapshot = [&p] {
    std::map<juce::String, float> out;

    for (auto *raw : p.getParameters())
      if (auto *r = dynamic_cast<juce::RangedAudioParameter *>(raw))
        out[r->paramID] = r->getValue();

    return out;
  };

  Counter counter;
  p.addListener(&counter);

  const auto names = ovt::presets::names();
  int worstExtra = 0;
  std::string worstName;

  for (const char *name :
       {"Cathedral", "Wurli", "Big Saw", "Init", "Shimmer"}) {
    const auto before = snapshot();
    counter.changes = 0;
    p.applyFactoryPreset(names.indexOf(name));
    const auto after = snapshot();

    int moved = 0;
    for (const auto &entry : after)
      if (std::abs(entry.second - before.at(entry.first)) > 1.0e-7f)
        ++moved;

    if (counter.changes - moved > worstExtra) {
      worstExtra = counter.changes - moved;
      worstName = name;
    }
  }

  check(worstExtra == 0,
        "no preset reports more changes than it made (worst was " +
            std::to_string(worstExtra) + " extra, on " + worstName + ")");

  // The case that used to be worst: the preset already loaded.
  p.applyFactoryPreset(names.indexOf("Wurli"));
  counter.changes = 0;
  p.applyFactoryPreset(names.indexOf("Wurli"));

  check(counter.changes == 0,
        "and loading the preset already loaded says nothing at all (" +
            std::to_string(counter.changes) + ")");

  p.removeListener(&counter);
}

/// Six of the thirty-two channels have nothing for TUNE to move.
///
/// An octave is 1200 cents in equal temperament and in just intonation alike,
/// so the blend does nothing to the sound on partials 1, 2, 4, 8, 16 and 32.
/// The strip says so on those and gives the cent figure on the rest, which is
/// the difference between a knob that looks broken and one that is explained.
void testOctaveChannelsSayTuneDoesNothing(OvertoniumProcessor &p) {
  section("The channels TUNE cannot move");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  std::vector<ovt::ui::ChannelStrip *> strips;

  std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(child))
        strips.push_back(s);

      gather(*child);
    }
  };
  gather(*editor);

  check(strips.size() == (size_t)ovt::kNumHarmonics, "the mixer is all there");
  if (strips.size() != (size_t)ovt::kNumHarmonics)
    return;

  // Asked of the tuning table rather than listed here, so a change to what
  // counts as an octave cannot leave the two disagreeing.
  int explained = 0, measured = 0, wrong = 0;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    const auto tip = strips[(size_t)i]->getTooltip();
    const auto says = tip.contains("TUNE has nothing to move here");
    const auto octave =
        std::abs(ovt::harmonicTable()[(size_t)i].jiCents) < 1.0e-9;

    if (octave != says)
      ++wrong;
    else if (octave)
      ++explained;
    else
      measured += tip.contains("Just intonation is") ? 1 : 0;
  }

  check(wrong == 0, "every channel says which of the two it is (" +
                        std::to_string(wrong) + " disagree)");

  check(explained == 6, "the six octave channels say the knob has nothing to "
                        "move (" +
                            std::to_string(explained) + ")");

  check(measured == ovt::kNumHarmonics - 6,
        "and the other twenty-six give the cents instead (" +
            std::to_string(measured) + ")");

  // Named, because the six being those six is the whole claim.
  for (int harmonic : {1, 2, 4, 8, 16, 32})
    check(strips[(size_t)(harmonic - 1)]->getTooltip().contains(
              "TUNE has nothing to move here"),
          "harmonic " + std::to_string(harmonic) + " is one of them");
}

/// Whether each modulator is one circuit the whole keyboard hears.
///
/// A global switch reached from a per-channel menu, so the thing to check is
/// that the pitch button and the amplitude button reach different switches.
/// Getting that wrong is invisible: both menus would tick and untick, and one
/// of the two modulators would quietly never share anything.
void testModulatorsInPhase(OvertoniumProcessor &p) {
  section("Modulators in phase across the keyboard");

  using namespace ovt::ui;

  const auto value = [&p](const char *id) {
    auto *param = p.apvts.getParameter(id);
    return param != nullptr && param->getValue() > 0.5f;
  };

  const auto put = [&p](const char *id, bool on) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(on ? 1.0f : 0.0f);
  };

  for (auto *id : {ovt::params::pmInPhaseId, ovt::params::amInPhaseId}) {
    auto *param = p.apvts.getParameter(id);

    check(param != nullptr, std::string(id) + " exists");

    if (param == nullptr)
      return;

    check(param->getDefaultValue() < 0.5f,
          std::string(id) + " starts off, so nothing written before it moves");

    check(!ovt::params::isSessionParam(id),
          std::string(id) +
              " travels with the patch, since two presets ask for it");
  }

  // The two buttons a strip carries, built the way a strip builds them.
  ShapeButton pitch(p.apvts,
                    ovt::params::oscParamId(ovt::params::pmShapeSuffix, 0),
                    ovt::params::pmShapeSuffix, ovt::params::kPitchShapes,
                    ovt::params::pitchShapeNames());

  ShapeButton amp(p.apvts,
                  ovt::params::oscParamId(ovt::params::amShapeSuffix, 0),
                  ovt::params::amShapeSuffix, ovt::params::kAmpShapes,
                  ovt::params::ampShapeNames());

  put(ovt::params::pmInPhaseId, true);
  put(ovt::params::amInPhaseId, false);

  const auto reads = [](const ShapeButton &button) {
    auto menu = button.buildMenu();

    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
      if (it.getItem().text == "In phase across the keyboard")
        return it.getItem().isTicked ? 1 : 0;

    return -1;
  };

  check(reads(pitch) == 1,
        "the pitch modulator's menu offers the switch and shows it on");

  check(reads(amp) == 0, "and the amplitude's shows its own, which is off");

  put(ovt::params::pmInPhaseId, false);
  put(ovt::params::amInPhaseId, true);

  check(reads(pitch) == 0 && reads(amp) == 1,
        "and they swap over when the two parameters do");

  put(ovt::params::pmInPhaseId, false);
  put(ovt::params::amInPhaseId, false);

  check(!value(ovt::params::pmInPhaseId) && !value(ovt::params::amInPhaseId),
        "and both go back off for whatever runs next");
}

void testSettingsMenu(OvertoniumProcessor &p) {
  section("Settings menu");

  using namespace ovt::ui;

  juce::Component popupParent;
  TopBar bar(p.apvts, popupParent);

  std::vector<std::string> headers, entries;

  // Held in a named menu rather than iterated straight off the call: the
  // iterator keeps a reference, and a temporary would be gone before the first
  // step of the loop.
  auto menu = bar.buildSettingsMenu();

  for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
    const auto &item = it.getItem();

    if (item.isSectionHeader)
      headers.push_back(item.text.toStdString());
    else if (item.itemID != 0 || item.subMenu != nullptr)
      entries.push_back(item.text.toStdString());
  }

  const auto joined = [](const std::vector<std::string> &v) {
    std::string s;
    for (const auto &x : v)
      s += x + " / ";
    return s;
  };

  // The version stands at the head of the list as a heading of its own, and
  // is asked for by the same call the menu builds it from rather than written
  // out here, which would have to be edited on every release.
  const auto wanted = TopBar::versionLine().toStdString() +
                      " / Polyphony / Pitch bend range / Expression / "
                      "Tuning / Output / ";

  check(joined(headers) == wanted,
        "the sections read in order (" + joined(headers) + ")");

  const auto at = [&entries](const std::string &name) {
    const auto it = std::find(entries.begin(), entries.end(), name);
    return it == entries.end() ? -1 : (int)(it - entries.begin());
  };

  // The three that answer "what does the controller in front of you send".
  check(at("MPE") >= 0 && at("Aftertouch from") == at("MPE") + 1 &&
            at("Slide to") == at("MPE") + 2,
        "MPE, the aftertouch source and the slide destination sit together");

  check(!entries.empty() && entries.back() == "Fit all 32 channels",
        "the two that are about the window are last (" +
            (entries.empty() ? std::string("nothing") : entries.back()) + ")");

  check(at("Zoom") == at("Fit all 32 channels") - 1,
        "with the zoom beside the one that undoes a narrowed window");

  // A window size is not a property of the instrument, so nothing else may
  // follow it into the same group.
  check(at("Zoom") > at("Safety clip"),
        "below everything that is about the instrument itself");

  // It answers the other half of the polyphony question, so it sits with the
  // voice counts rather than off among the output switches. Bounded by
  // entries rather than by the headers around them, since the headers are
  // collected separately above.
  check(at("One voice per key") > at("16 voices") &&
            at("One voice per key") < at("0 semitones"),
        "one voice per key sits with the voice counts (" +
            std::to_string(at("One voice per key")) + ")");
}

/// The way back from a window that was left narrow.
void testFitAllChannels(OvertoniumProcessor &p) {
  section("Fitting the mixer back in");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // The 8 is the gap before the noise channel and the 10 is the parameter
  // scrollbar beyond it, both private to the editor, both written here the
  // same way the gap already was.
  const auto wanted = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 + 10 +
                      ovt::kNumHarmonics * ovt::ui::kStripWidth;

  // The size to come back to, taken from the editor rather than worked out
  // here: the height follows whatever is folded away, so there is no number to
  // write down. An editor opens at whatever size was left in the state, which
  // earlier tests have been dragging around, so it is asked once first.
  editor->fitAllChannels();

  const auto fitted = editor->getBounds();

  check(fitted.getWidth() == wanted,
        "the window fits all 32 channels across (" +
            std::to_string(fitted.getWidth()) + ")");

  check(fitted.getHeight() > 600, "and the strips are their full height (" +
                                      std::to_string(fitted.getHeight()) + ")");

  // Dragged narrow and short, the way somebody would, and narrow enough that
  // the bar has to reflow onto two rows: the height has to come back as well
  // as the width.
  editor->setSize(700, fitted.getHeight() - 120);

  check(editor->getWidth() == 700, "a window can be dragged narrow (" +
                                       std::to_string(editor->getWidth()) +
                                       ")");

  // Short, and now allowed to be: the rows scroll, so the floor is far below
  // this and the height asked for is simply taken. It used to come to rest on
  // the floor here, because the floor was the height of every row at once and
  // the bar's second row had pushed it above what was asked for.
  check(editor->getHeight() == fitted.getHeight() - 120,
        "and as short as it was asked for (" +
            std::to_string(editor->getHeight()) + ")");

  // Not checked here that a shorter request stops at the floor, because
  // setSize does not consult the constrainer at all, which is the whole reason
  // Fit all 32 channels could land under it. The floor itself is asserted in
  // testFittingLandsWhereADragCanReturn, through the constrainer.

  editor->fitAllChannels();

  check(editor->getBounds() == fitted,
        "and comes back to exactly the size it was (" +
            std::to_string(editor->getWidth()) + " x " +
            std::to_string(editor->getHeight()) + ")");

  editor->fitAllChannels();

  check(editor->getBounds() == fitted,
        "asking twice changes nothing the second time");
}

/// The two machine menus say what is running.
///
/// Both the echo and the reverb hang four choices off one button, and behind
/// each button are two parameters rather than one: an on switch that predates
/// the choice of machine, and a type beside it. A tick that reads only one of
/// them would say Off while a plate was audible, or name a machine that was
/// switched off, and nothing in the audio path would be wrong. So the pairing
/// is checked here, from the parameters the host writes to the menu the player
/// reads.
void testMachineMenusFollowTheirParameters(OvertoniumProcessor &p) {
  section("The machine menus follow their parameters");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  auto *bar = findTopBar(*editor);

  check(bar != nullptr, "and carries a bar to read the menus off");
  if (bar == nullptr)
    return;

  /// Whatever is ticked, joined, so two ticks fail as loudly as none.
  const auto tickedIn = [](juce::PopupMenu &menu) {
    std::string found;

    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
      if (it.getItem().isTicked)
        found += it.getItem().text.toStdString();

    return found;
  };

  const auto write = [&p](const char *id, float value) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(value));
  };

  // ---- the echo ------------------------------------------------------------
  {
    write(ovt::params::echoOnId, 0.0f);

    auto off = bar->buildEchoMenu();
    check(tickedIn(off) == "Off",
          "the echo says Off when its switch is off (" + tickedIn(off) + ")");

    write(ovt::params::echoOnId, 1.0f);

    for (int i = 0; i < (int)ovt::EchoType::NumTypes; ++i) {
      write(ovt::params::echoTypeId, (float)i);

      auto menu = bar->buildEchoMenu();
      const std::string wanted = ovt::echoTypeName((ovt::EchoType)i);

      check(tickedIn(menu) == wanted,
            "and names " + wanted + " when that is the machine running (" +
                tickedIn(menu) + ")");
    }

    // Switched off with a type still chosen, which is the state that catches a
    // tick reading one parameter and not the other.
    write(ovt::params::echoOnId, 0.0f);

    auto stillDigital = bar->buildEchoMenu();
    check(tickedIn(stillDigital) == "Off",
          "and goes back to Off without forgetting which machine it was (" +
              tickedIn(stillDigital) + ")");
  }

  // ---- and the output stage, which became one of these ---------------------
  {
    write(ovt::params::safetyClipId, 0.0f);

    auto off = bar->buildClipMenu();
    check(tickedIn(off) == "Off",
          "the clipper says Off when its switch is off (" + tickedIn(off) +
              ")");

    write(ovt::params::safetyClipId, 1.0f);

    for (int i = 0; i < (int)ovt::ClipType::NumTypes; ++i) {
      write(ovt::params::clipTypeId, (float)i);

      auto menu = bar->buildClipMenu();
      const std::string wanted = ovt::clipTypeName((ovt::ClipType)i);

      check(tickedIn(menu) == wanted, "and names " + wanted +
                                          " when that is the shape running (" +
                                          tickedIn(menu) + ")");
    }

    write(ovt::params::safetyClipId, 0.0f);

    auto stillFold = bar->buildClipMenu();
    check(tickedIn(stillFold) == "Off",
          "and goes back to Off without forgetting which shape it was (" +
              tickedIn(stillFold) + ")");
  }

  // ---- and the button they came from can say them ------------------------
  //
  // The menu above has room for whatever a shape is called. The button does
  // not: it is 62 px with a label area of 46, and the ten pixels it would
  // take to fit ASYMMETRIC belong to the converter's readouts beside it,
  // which are already at the width they need to name their units. So the
  // button carries short forms, and this is what says they are short enough.
  //
  // Measured rather than eyed, because a label that overflows is not a
  // failure a rendered window announces: JUCE shaves the ends and the word
  // goes on looking like a word.
  {
    std::function<juce::TextButton *(juce::Component &)> findClip =
        [&findClip](juce::Component &c) -> juce::TextButton * {
      for (auto *child : c.getChildren()) {
        if (auto *b = dynamic_cast<juce::TextButton *>(child))
          if (b->getTitle().startsWith("Clip"))
            return b;

        if (auto *found = findClip(*child))
          return found;
      }

      return nullptr;
    };

    editor->setSize(editor->getWidth(), editor->getHeight());

    auto *clip = findClip(*editor);

    check(clip != nullptr, "the bar carries a clip button");

    if (clip != nullptr && clip->getWidth() > 0) {
      const auto font =
          clip->getLookAndFeel().getTextButtonFont(*clip, clip->getHeight());

      // The margin JUCE's own text button keeps either side of a label.
      const int margin = juce::jmin(clip->getWidth() / 4, 8);
      const auto room = (float)(clip->getWidth() - margin * 2);

      int tooWide = 0;
      for (int i = 0; i < (int)ovt::ClipType::NumTypes; ++i) {
        const juce::String label =
            juce::String(ovt::clipTypeShortName((ovt::ClipType)i))
                .toUpperCase();

        const auto width = juce::GlyphArrangement::getStringWidth(font, label);

        if (width > room) {
          ++tooWide;
          std::printf("  %s is %.1f px against %.1f\n", label.toRawUTF8(),
                      (double)width, (double)room);
        }
      }

      check(tooWide == 0, "and every shape's name fits across it (" +
                              std::to_string(tooWide) + " do not)");
    }
  }

  // ---- and the reverb, which works the same way ----------------------------
  {
    write(ovt::params::reverbOnId, 0.0f);

    auto off = bar->buildReverbMenu();
    check(tickedIn(off) == "Off",
          "the reverb says Off when its switch is off (" + tickedIn(off) + ")");

    write(ovt::params::reverbOnId, 1.0f);

    for (int i = 0; i < (int)ovt::ReverbType::NumTypes; ++i) {
      write(ovt::params::reverbTypeId, (float)i);

      auto menu = bar->buildReverbMenu();
      const std::string wanted = ovt::reverbTypeName((ovt::ReverbType)i);

      check(tickedIn(menu) == wanted,
            "and names " + wanted + " when that is the machine running (" +
                tickedIn(menu) + ")");
    }

    write(ovt::params::reverbOnId, 0.0f);

    auto stillSpring = bar->buildReverbMenu();
    check(tickedIn(stillSpring) == "Off",
          "and goes back to Off without forgetting which machine it was (" +
              tickedIn(stillSpring) + ")");
  }

  // Left as the patch found them, for whatever runs next.
  write(ovt::params::echoTypeId, 0.0f);
  write(ovt::params::reverbTypeId, 0.0f);
}

/// The Settings menu says which build this is.
///
/// The bug report template will not take a report without a version and tells
/// people to read it off this menu. It was not on it, and not anywhere else
/// either, so the one question every report has to answer was the one thing
/// the instrument would not say. This holds the menu to saying it, and to
/// saying the version this was actually built as rather than a number typed
/// out beside it.
void testSettingsNamesTheVersion(OvertoniumProcessor &p) {
  section("The Settings menu names the version");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  auto *bar = findTopBar(*editor);

  check(bar != nullptr, "and carries a bar to read the menu off");
  if (bar == nullptr)
    return;

  const auto line = ovt::ui::TopBar::versionLine();

  // Built from the same define the plugin reports to a host, so this cannot
  // pass against a version that is merely well formed.
  check(line == juce::String("Overtonium ") + OVERTONIUM_VERSION,
        "the line names this build (" + line.toStdString() + ")");

  // A number rather than a word, and one with parts to it.
  check(line.contains(".") && line.containsAnyOf("0123456789"),
        "and reads as a version rather than a name");

  auto menu = bar->buildSettingsMenu();

  // Separators are dropped, since where they fall is a matter of taste and
  // this is about the order of the things that carry words.
  std::vector<juce::PopupMenu::Item> items;

  for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    if (!it.getItem().isSeparator)
      items.push_back(it.getItem());

  check(items.size() > 4, "and the Settings menu has items in it");
  if (items.size() <= 4)
    return;

  check(items[0].text == line, "and the menu opens with it");

  // A fact rather than a choice. An ordinary item, enabled or not, reads as
  // something to press.
  check(items[0].isSectionHeader,
        "as a heading rather than as something to click");

  // Directly under it, because the two are one thought: this is the build you
  // have, and here is whether to look for a newer one.
  check(items[1].text == "Check for new versions",
        "with the update check directly beneath it");

  // Below both, rather than above them as they were. The version is the one
  // thing in this menu that does not change while you use the plugin, so it
  // reads first and the undo pair follows.
  check(items[2].text == "Undo" && items[3].text == "Redo",
        "and undo and redo below, not above");
}

/// A wheel over a knob must not also scroll the mixer sideways.
///
/// When the window is narrow enough for the series to need scrolling, a scroll
/// over a knob was reported as doing both: moving the knob and dragging the
/// view along under it. The wheel is how most of this instrument gets
/// adjusted, so a knob that also shoves the panel sideways is the difference
/// between a control and a fight.
void testWheelOverAKnobStaysOnTheKnob(OvertoniumProcessor &p) {
  section("A wheel over a knob does not scroll the mixer");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Narrow enough that the series cannot all be shown at once, which is the
  // only time there is anything to scroll.
  editor->setSize(900, editor->getHeight());

  juce::Viewport *viewport = nullptr;
  std::function<void(juce::Component &)> findViewport =
      [&](juce::Component &c) {
        if (auto *v = dynamic_cast<juce::Viewport *>(&c))
          viewport = v;
        for (auto *child : c.getChildren())
          findViewport(*child);
      };
  findViewport(*editor);

  check(viewport != nullptr, "and the series sits in a viewport");
  if (viewport == nullptr)
    return;

  // Half way along, so a scroll in either direction has somewhere to go and a
  // move cannot be hidden by hitting a limit.
  const auto span =
      viewport->getViewedComponent()->getWidth() - viewport->getViewWidth();

  check(span > 0, "which has more in it than it can show (" +
                      std::to_string(span) + " px)");

  viewport->setViewPosition(span / 2, 0);
  const auto before = viewport->getViewPositionX();

  // A knob inside it, found the way the pointer finds one.
  juce::Slider *knob = nullptr;
  std::function<void(juce::Component &)> findKnob = [&](juce::Component &c) {
    if (knob != nullptr)
      return;
    if (auto *s = dynamic_cast<juce::Slider *>(&c))
      if (s->getSliderStyle() == juce::Slider::RotaryVerticalDrag)
        knob = s;
    for (auto *child : c.getChildren())
      findKnob(*child);
  };
  findKnob(*viewport->getViewedComponent());

  check(knob != nullptr, "and carries knobs to scroll over");
  if (knob == nullptr)
    return;

  const auto valueBefore = knob->getValue();

  // What JUCE hands the component under the pointer: the event names that
  // component, which is what decides whether anything above it acts on it too.
  juce::MouseWheelDetails wheel{};
  wheel.deltaX = 0.0f;
  // Downwards, since the first knob found is at the top of its range and a
  // push upwards would move nothing and prove nothing.
  wheel.deltaY = -0.4f;
  wheel.isReversed = false;
  wheel.isSmooth = false;
  wheel.isInertial = false;

  const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                           knob->getLocalBounds().getCentre().toFloat(),
                           juce::ModifierKeys(),
                           juce::MouseInputSource::defaultPressure,
                           juce::MouseInputSource::defaultOrientation,
                           juce::MouseInputSource::defaultRotation,
                           juce::MouseInputSource::defaultTiltX,
                           juce::MouseInputSource::defaultTiltY, knob, knob,
                           juce::Time::getCurrentTime(),
                           knob->getLocalBounds().getCentre().toFloat(),
                           juce::Time::getCurrentTime(), 1, false);

  knob->mouseWheelMove(e, wheel);

  // And then what JUCE does next, which is the half that bit. Every ancestor
  // holding a deep mouse listener is handed the same wheel after the target
  // has had it, whether the target took it or not. The strip holds one so that
  // a pointer resting on a knob is reported by the strip rather than swallowed
  // by the control. See MouseListenerList::sendMouseEvent.
  ovt::ui::ChannelStrip *strip = nullptr;
  for (auto *c = knob->getParentComponent(); c != nullptr;
       c = c->getParentComponent())
    if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(c)) {
      strip = s;
      break;
    }

  check(strip != nullptr, "and the knob sits in a strip that listens deeply");

  if (strip != nullptr) {
    const juce::MouseEvent relayed(
        juce::Desktop::getInstance().getMainMouseSource(),
        strip->getLocalPoint(knob, knob->getLocalBounds().getCentre())
            .toFloat(),
        juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, strip, knob,
        juce::Time::getCurrentTime(),
        strip->getLocalPoint(knob, knob->getLocalBounds().getCentre())
            .toFloat(),
        juce::Time::getCurrentTime(), 1, false);

    strip->mouseWheelMove(relayed, wheel);
  }

  std::printf("  the knob went from %.4f to %.4f, and the view from %d to %d\n",
              valueBefore, knob->getValue(), before,
              viewport->getViewPositionX());

  check(std::abs(knob->getValue() - valueBefore) > 1.0e-9,
        "the wheel moves the knob it is over");

  check(viewport->getViewPositionX() == before,
        "and leaves the mixer where it was (" + std::to_string(before) +
            " to " + std::to_string(viewport->getViewPositionX()) + ")");

  // ---- and shift on the strip still scrolls it sideways -------------------
  //
  // The other half of what was asked for, and the thing a careless fix would
  // break: swallowing every wheel the strip is handed would stop the series
  // scrolling at all, which is worse than what was reported.
  //
  // It is the shifted wheel now rather than the plain one. The plain one moves
  // the parameters, and shift is free because a wheel over a fader belongs to
  // the fader and never reaches here.
  if (strip != nullptr) {
    const auto held = viewport->getViewPositionX();

    const juce::MouseEvent onStrip(
        juce::Desktop::getInstance().getMainMouseSource(),
        strip->getLocalBounds().getCentre().toFloat(),
        juce::ModifierKeys(juce::ModifierKeys::shiftModifier),
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, strip, strip,
        juce::Time::getCurrentTime(),
        strip->getLocalBounds().getCentre().toFloat(),
        juce::Time::getCurrentTime(), 1, false);

    strip->mouseWheelMove(onStrip, wheel);

    std::printf(
        "  and a wheel on the strip itself took the view from %d to %d\n", held,
        viewport->getViewPositionX());

    check(viewport->getViewPositionX() != held,
          "a shifted wheel on the strip's background still scrolls the "
          "series");
  }
}

/// One right-click opens one LINK menu.
///
/// Right-clicking the gap between two sections opened two menus stacked on
/// each other. Picking an item on the front one left the back one standing and
/// its ticks unmoved, so the setting appeared not to have taken while the
/// instrument had in fact changed.
///
/// The cause is the strip listening to everything inside it, which is what
/// lets a right-click on a knob open the LINK menu rather than being swallowed
/// by the control. JUCE hands a listener registered that way the component's
/// own events too, so a click on the strip's own background arrives twice:
/// once because the strip is what the pointer is over, and once more through
/// that listener. The gap between sections is the strip's own background,
/// which is why only that strip of pixels did it.
///
/// Counted through a stub rather than by opening anything: a real PopupMenu
/// wants a window and there is none here.
void testOneRightClickOpensOneMenu(OvertoniumProcessor &p) {
  section("One right-click, one LINK menu");

  struct CountingLink final : ovt::ui::LinkTarget {
    int opened = 0;

    bool isLinkEnabled() const override { return true; }
    void linkDragStarted(ovt::ui::Role, int) override {}
    void linkValueChanged(ovt::ui::Role, int, float) override {}
    void linkDragEnded(ovt::ui::Role, int) override {}
    void showLinkMenu(const juce::String &) override { ++opened; }
    // Nowhere to scroll, so the wheel would fall through as it used to.
    bool scrollParameters(int) override { return false; }

    bool drawStarted(juce::Point<int>) override { return false; }
    void drawMovedTo(juce::Point<int>) override {}
    void drawEnded() override {}
  };

  struct SilentHover final : ovt::ui::HoverTarget {
    void hoverChanged(int, ovt::ui::Row) override {}
  };

  CountingLink link;
  SilentHover hover;
  juce::Component popupParent;

  ovt::ui::ChannelStrip strip(p.apvts, link, hover, popupParent, 0);
  strip.setSize(40, 900);

  const auto rightClickOn = [](juce::Component *landedOn,
                               juce::Component *deliveredTo, juce::Time when) {
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(),
        deliveredTo->getLocalBounds().getCentre().toFloat(),
        juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, deliveredTo, landedOn, when,
        deliveredTo->getLocalBounds().getCentre().toFloat(), when, 1, false);
  };

  // ---- on the strip's own background --------------------------------------
  //
  // Delivered the way JUCE delivers it: to the component under the pointer,
  // and then again to every listener registered on it. Both carry the same
  // timestamp, because they are the same click.
  {
    const auto when = juce::Time::getCurrentTime();
    const auto e = rightClickOn(&strip, &strip, when);

    strip.mouseDown(e);
    strip.mouseDown(e);

    std::printf("  a right-click on the gap opened %d menu(s)\n", link.opened);

    check(link.opened == 1, "the gap between sections opens one menu (" +
                                std::to_string(link.opened) + ")");
  }

  // ---- and on a knob, which only ever arrives through the listener ---------
  {
    link.opened = 0;

    juce::Slider *knob = nullptr;
    std::function<void(juce::Component &)> find = [&](juce::Component &c) {
      if (knob == nullptr)
        if (auto *s = dynamic_cast<juce::Slider *>(&c))
          knob = s;
      for (auto *child : c.getChildren())
        find(*child);
    };
    find(strip);

    check(knob != nullptr, "the strip has a knob to right-click");

    if (knob != nullptr) {
      const auto when = juce::Time::getCurrentTime() + juce::RelativeTime(1.0);
      strip.mouseDown(rightClickOn(knob, &strip, when));

      check(link.opened == 1, "and a right-click on a knob still opens it (" +
                                  std::to_string(link.opened) + ")");
    }
  }
}

/// A click on the rule between two sections folds that section.
///
/// The gutter's headings have always done this. Reaching them means leaving
/// the partial you are working on, crossing the mixer and coming back, and by
/// then you have lost which column you were in. The rules across a strip line
/// up with those headings, so the same click works where your hand already is.
void testTheRulesFoldTheirSections(OvertoniumProcessor &p) {
  section("The rules between sections fold them");

  struct SilentLink final : ovt::ui::LinkTarget {
    int menus = 0;

    bool isLinkEnabled() const override { return true; }
    void linkDragStarted(ovt::ui::Role, int) override {}
    void linkValueChanged(ovt::ui::Role, int, float) override {}
    void linkDragEnded(ovt::ui::Role, int) override {}
    void showLinkMenu(const juce::String &) override { ++menus; }
    bool scrollParameters(int) override { return false; }

    bool drawStarted(juce::Point<int>) override { return false; }
    void drawMovedTo(juce::Point<int>) override {}
    void drawEnded() override {}
  };

  struct WatchingHover final : ovt::ui::HoverTarget {
    ovt::ui::Row last = ovt::ui::kNoRow;

    void hoverChanged(int, ovt::ui::Row row) override { last = row; }
  };

  SilentLink link;
  WatchingHover hover;
  juce::Component popupParent;

  ovt::ui::ChannelStrip strip(p.apvts, link, hover, popupParent, 0);
  strip.setSize(40, 900);

  std::vector<ovt::ui::Section> folded;
  strip.onSectionToggled = [&folded](ovt::ui::Section s) {
    folded.push_back(s);
  };

  const auto rows = ovt::ui::layoutRows(
      strip.getLocalBounds().reduced(ovt::ui::kStripPadX, ovt::ui::kStripPadY),
      0);

  const auto clickAt = [&strip](juce::Point<int> where, bool rightButton,
                                juce::Time when) {
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), where.toFloat(),
        juce::ModifierKeys(rightButton
                               ? juce::ModifierKeys::rightButtonModifier
                               : juce::ModifierKeys::leftButtonModifier),
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, &strip, &strip, when,
        where.toFloat(), when, 1, false);
  };

  // ---- every rule folds the section it belongs to --------------------------
  for (int i = 0; i < ovt::ui::kNumSections; ++i) {
    const auto want = (ovt::ui::Section)i;
    const auto rule = rows[(size_t)ovt::ui::sectionHeading(want)];

    folded.clear();

    // Delivered the way JUCE delivers a click on a strip's own background:
    // once because the strip is under the pointer, and once more through the
    // listener it keeps on itself.
    const auto when = juce::Time::getCurrentTime() + juce::RelativeTime(i);
    const auto e = clickAt(rule.getCentre(), false, when);

    strip.mouseDown(e);
    strip.mouseDown(e);

    check(folded.size() == 1 && folded.front() == want,
          "the rule over section " + std::to_string(i) +
              " folds that section, once (" + std::to_string(folded.size()) +
              ")");
  }

  // ---- and the pointer on a rule says which row it is on -------------------
  //
  // The caption in the gutter lighting is how this panel says what is under
  // the pointer, and the rules are the one part of a strip that can be clicked
  // without carrying a control. Left out of the answer, the only clickable
  // part of a strip was the only part that said nothing.
  {
    for (int i = 0; i < ovt::ui::kNumSections; ++i) {
      const auto want = ovt::ui::sectionHeading((ovt::ui::Section)i);
      const auto rule = rows[(size_t)want];

      hover.last = ovt::ui::kNoRow;

      const auto when =
          juce::Time::getCurrentTime() + juce::RelativeTime(200 + i);
      strip.mouseMove(clickAt(rule.getCentre(), false, when));

      check(hover.last == want, "the pointer on a rule reports that heading (" +
                                    std::to_string((int)hover.last) +
                                    " wanted " + std::to_string((int)want) +
                                    ")");
    }

    // And a knob still reports its own row rather than the rule above it.
    const auto knob = rows[(size_t)ovt::ui::Row::PmRate];
    const auto when = juce::Time::getCurrentTime() + juce::RelativeTime(260.0);

    hover.last = ovt::ui::kNoRow;
    strip.mouseMove(clickAt(knob.getCentre(), false, when));

    check(hover.last == ovt::ui::Row::PmRate,
          "and a control still reports its own row");
  }

  // ---- and a click that is not on one does nothing -------------------------
  {
    folded.clear();

    const auto knobRow = rows[(size_t)ovt::ui::Row::PmRate];
    const auto when = juce::Time::getCurrentTime() + juce::RelativeTime(60.0);

    strip.mouseDown(clickAt(knobRow.getCentre(), false, when));

    check(folded.empty(), "a click on a row of controls folds nothing (" +
                              std::to_string(folded.size()) + ")");
  }

  // ---- a right-click on a rule is still the LINK menu ----------------------
  //
  // The rules are the only part of a strip with nothing standing on them,
  // which is what makes them clickable at all, and is also where a right-click
  // reaches the strip. One button each.
  {
    folded.clear();
    link.menus = 0;

    const auto rule =
        rows[(size_t)ovt::ui::sectionHeading(ovt::ui::Section::Envelope)];
    const auto when = juce::Time::getCurrentTime() + juce::RelativeTime(120.0);

    strip.mouseDown(clickAt(rule.getCentre(), true, when));

    check(folded.empty() && link.menus == 1,
          "a right-click on a rule opens the menu and folds nothing (" +
              std::to_string(folded.size()) + " folds, " +
              std::to_string(link.menus) + " menus)");
  }
}

/// Folding and unfolding a section puts the window back where it was.
///
/// Reported from a Mac: shrink the window so the faders are squeezed, fold a
/// section, and the faders keep their height, which is right. Unfold it again
/// and they grow far taller than they were, filling the screen and running
/// under the dock.
///
/// The window's height is the sum of the rows plus whatever is left for the
/// fader, so a fader that grew means the window grew by more than the rows it
/// got back.
/// Energy at one frequency, by Goertzel, normalised by the sample count.
///
/// Enough to ask "is the note where it should be", which is the question a
/// sample rate bug answers wrongly while leaving every level and every
/// finiteness check happy.
double toneAt(const std::vector<float> &x, double freq, double sampleRate) {
  if (x.empty() || freq <= 0.0 || freq >= sampleRate * 0.5)
    return 0.0;

  const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
  const double coeff = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;

  for (float v : x) {
    const double s0 = (double)v + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }

  return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) /
         (double)x.size();
}

/// The left channel of a render, after letting the attack settle.
std::vector<float> renderTone(OvertoniumProcessor &p, double sampleRate,
                              int block, int note, double seconds) {
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

  juce::AudioBuffer<float> buf(2, block);
  std::vector<float> out;
  const int blocks = (int)(seconds * sampleRate / block) + 1;
  const int settle = blocks / 3;

  for (int b = 0; b < blocks; ++b) {
    juce::MidiBuffer m = (b == 0) ? midi : juce::MidiBuffer{};
    buf.clear();
    p.processBlock(buf, m);

    if (b >= settle)
      out.insert(out.end(), buf.getReadPointer(0),
                 buf.getReadPointer(0) + block);
  }

  return out;
}

/// A wheel over a strip moves the parameters, and shift still moves the mixer.
///
/// The end of the chain the bands are for: a window too short to show every
/// row has to be scrollable, and scrolling has to move every column together
/// or the gutter's captions end up beside the wrong knobs.
void testTheWheelScrollsTheParameters(OvertoniumProcessor &p) {
  section("The wheel scrolls the parameters");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Short enough that not every row fits, which is the whole case, and well
  // above the floor so nothing is being tested at a limit. Set outright rather
  // than through sizeEditor, whose second argument is height above the
  // minimum: that minimum is now low enough that the usual slack put this
  // window taller than the mixer needs, where nothing scrolls at all.
  editor->setSize(1348, 700);

  ovt::ui::ChannelStrip *strip = nullptr;
  std::function<void(juce::Component &)> findStrip = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(child))
        strip = (strip == nullptr) ? s : strip;
      findStrip(*child);
    }
  };
  findStrip(*editor);

  check(strip != nullptr, "and carries strips to scroll");
  if (strip == nullptr)
    return;

  // One named control as the marker, rather than whichever happens to be at
  // the top. The top of the band is always at the same height, so "the first
  // visible slider" never moves however far it is scrolled: what changes is
  // which slider that is, which is how the first version of this test passed
  // nothing while appearing to measure something.
  juce::Slider *marker = nullptr;
  for (auto *child : strip->getChildren())
    if (auto *s = dynamic_cast<juce::Slider *>(child))
      if (marker == nullptr)
        marker = s;

  check(marker != nullptr, "with a control to follow");
  if (marker == nullptr)
    return;

  const int before = marker->getY();
  check(marker->isVisible(), "visible before anything is scrolled");

  const juce::MouseEvent onStrip(
      juce::Desktop::getInstance().getMainMouseSource(),
      strip->getLocalBounds().getCentre().toFloat(), juce::ModifierKeys(),
      juce::MouseInputSource::defaultPressure,
      juce::MouseInputSource::defaultOrientation,
      juce::MouseInputSource::defaultRotation,
      juce::MouseInputSource::defaultTiltX,
      juce::MouseInputSource::defaultTiltY, strip, strip,
      juce::Time::getCurrentTime(),
      strip->getLocalBounds().getCentre().toFloat(),
      juce::Time::getCurrentTime(), 1, false);

  juce::MouseWheelDetails down{};
  down.deltaY = -1.0f;

  strip->mouseWheelMove(onStrip, down);

  std::printf("  the marker went from y %d visible, to y %d %s\n", before,
              marker->getY(), marker->isVisible() ? "visible" : "hidden");

  // It is one of the topmost rows, so scrolling down takes it off the top.
  // Either answer counts as movement: gone, or still there and higher up.
  check(!marker->isVisible() || marker->getY() < before,
        "a wheel on a strip moves the parameters");

  // And every column with it, or a caption names the wrong knob. The gutter
  // and the noise channel lay out the same rows from the same scroll, so the
  // check is that they agree about where a row starts.
  ovt::ui::NoiseStrip *noise = nullptr;
  std::function<void(juce::Component &)> findNoise = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *n = dynamic_cast<ovt::ui::NoiseStrip *>(child))
        noise = n;
      findNoise(*child);
    }
  };
  findNoise(*editor);

  check(noise != nullptr, "the noise channel is there");

  if (noise != nullptr) {
    // The two lay out the same rows from the same scroll, so the topmost
    // visible control in each has to sit at the same height. That is the
    // property the gutter's captions depend on.
    const auto topOf = [](juce::Component &c) {
      int y = -1;
      for (auto *child : c.getChildren())
        if (auto *s = dynamic_cast<juce::Slider *>(child))
          if (s->isVisible() && !s->getBounds().isEmpty() && y < 0)
            y = s->getY();
      return y;
    };

    check(topOf(*noise) == topOf(*strip),
          "and the noise channel is scrolled to the same place (" +
              std::to_string(topOf(*strip)) + " against " +
              std::to_string(topOf(*noise)) + ")");
  }

  // Back up again, and the top row returns. Scrolling past either end is the
  // clamp's job and has to be a no-op rather than a drift.
  juce::MouseWheelDetails up{};
  up.deltaY = 1.0f;

  for (int i = 0; i < 20; ++i)
    strip->mouseWheelMove(onStrip, up);

  check(marker->isVisible() && marker->getY() == before,
        "and winds back to where it started (" +
            std::to_string(marker->getY()) + ")");
}

/// A sideways gesture moves the mixer, not the parameters.
///
/// A trackpad sends one as deltaX with deltaY near zero. The vertical handler
/// worked its distance out from deltaY alone, came to nothing, and still
/// reported the wheel as taken, so the event was swallowed and a two-finger
/// swipe moved nothing at all. On a screen too narrow for 32 channels that is
/// the only way across.
void testASidewaysWheelMovesTheMixer(OvertoniumProcessor &p) {
  section("A sideways wheel moves the mixer");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Narrow enough to have somewhere to go sideways, short enough to have
  // somewhere to go down, so neither axis is tested where it cannot move.
  editor->setSize(900, 700);

  juce::Viewport *viewport = nullptr;
  std::function<void(juce::Component &)> findViewport =
      [&](juce::Component &c) {
        for (auto *child : c.getChildren()) {
          if (auto *v = dynamic_cast<juce::Viewport *>(child))
            viewport = v;
          findViewport(*child);
        }
      };
  findViewport(*editor);

  ovt::ui::ChannelStrip *strip = nullptr;
  std::function<void(juce::Component &)> findStrip = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(child))
        if (strip == nullptr)
          strip = s;
      findStrip(*child);
    }
  };
  findStrip(*editor);

  check(viewport != nullptr && strip != nullptr,
        "with a viewport and strips in it");
  if (viewport == nullptr || strip == nullptr)
    return;

  viewport->setViewPosition(120, 0);

  const auto centre = strip->getLocalBounds().getCentre().toFloat();
  const juce::MouseEvent onStrip(
      juce::Desktop::getInstance().getMainMouseSource(), centre,
      juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
      juce::MouseInputSource::defaultOrientation,
      juce::MouseInputSource::defaultRotation,
      juce::MouseInputSource::defaultTiltX,
      juce::MouseInputSource::defaultTiltY, strip, strip,
      juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1,
      false);

  // What a trackpad sends for a swipe: sideways only.
  juce::MouseWheelDetails swipe{};
  swipe.deltaX = 0.6f;
  swipe.deltaY = 0.0f;

  const int before = viewport->getViewPositionX();
  strip->mouseWheelMove(onStrip, swipe);

  check(viewport->getViewPositionX() != before,
        "a sideways swipe over a strip moves the mixer (" +
            std::to_string(before) + " to " +
            std::to_string(viewport->getViewPositionX()) + ")");

  // And it must not have scrolled the parameters while it was at it.
  juce::Slider *marker = nullptr;
  for (auto *child : strip->getChildren())
    if (auto *s = dynamic_cast<juce::Slider *>(child))
      if (marker == nullptr)
        marker = s;

  if (marker != nullptr) {
    const int y = marker->getY();
    strip->mouseWheelMove(onStrip, swipe);

    check(marker->getY() == y, "and leaves the parameters where they were");
  }
}

/// The border between the gutter and the channels is one line, all the way.
///
/// Each column covers its pinned header with an opaque cap so a row can scroll
/// under it. A cap repeats what the column paints, and the gutter paints one
/// thing its cap did not: a divider down its right edge, drawn for the full
/// height after the background. The cap covered the top of it and the border
/// changed colour at the header.
///
/// Read off a render rather than argued about, because the fault is a colour
/// in one band of pixels and nothing short of looking at them would see it.
void testTheGutterBorderIsOneLine(OvertoniumProcessor &p) {
  section("The gutter border is one line");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Short enough that the parameters scroll, which is when the caps matter.
  editor->setSize(1350, 700);

  const auto shot = editor->createComponentSnapshot(editor->getLocalBounds());
  const int x = ovt::ui::kGutterWidth - 1;

  // Below the top bar, down to the foot of the window.
  const int from = ovt::ui::TopBar::heightForWidth(1350) + 4;
  const int to = shot.getHeight() - 4;

  check(to > from + 200, "and is tall enough to read a border down");

  const auto first = shot.getPixelAt(x, from);
  int different = 0;

  for (int y = from; y < to; ++y)
    if (shot.getPixelAt(x, y) != first)
      ++different;

  check(different == 0,
        "the gutter's right edge is one colour for its whole height (" +
            std::to_string(different) + " of " + std::to_string(to - from) +
            " rows differ)");
}

/// The three bands have to lay a column out exactly as one column used to.
///
/// Scrolling the parameters is a change to what a short window does, and must
/// be no change at all to a window with room in it. At or above the preferred
/// height everything fits, so nothing scrolls, the rows run from the top of
/// the strip to the bottom without a gap or an overlap, and the fader takes
/// the slack the way it always has. That is the whole of the old behaviour,
/// asserted here rather than left to be noticed.
///
/// Below the preferred height the two part company on purpose: the old layout
/// squeezed the fader to its floor to keep every row on screen, and this keeps
/// the fader and scrolls the rows, because a fader pushed off the bottom is
/// what the request was about.
void testTheBandsLayOutAColumn() {
  section("The bands lay out a column");

  using namespace ovt::ui;

  // Every fold state, since the bands are worked out from the fold mask and a
  // section folded away is the case where the arithmetic is easiest to get
  // wrong.
  int states = 0, scrolling = 0;

  for (SectionMask mask = 0; mask < (1 << kNumSections); ++mask) {
    const int preferred = preferredStripHeight(mask);

    for (int height : {preferred, preferred + 1, preferred + 200}) {
      const juce::Rectangle<int> area(0, 0, kStripWidth, height);
      const auto bands = layoutBands(area, mask);
      const auto rows = layoutRows(area, mask, 0);

      check(bands.maxScroll == 0,
            "at or above the preferred height nothing scrolls");

      // Contiguous from the top of the area to the bottom, which is what
      // makes a caption in the gutter point at the knob beside it.
      int y = area.getY();
      bool contiguous = true;

      for (int i = 0; i < kNumRows; ++i) {
        const auto &r = rows[(size_t)i];
        if (r.getHeight() == 0)
          continue;
        contiguous &= (r.getY() == y);
        y = r.getBottom();
      }

      contiguous &= (y == area.getBottom());

      if (!contiguous) {
        check(false, "mask " + std::to_string(mask) + " at " +
                         std::to_string(height) +
                         " lays out without a gap or an overlap");
        return;
      }

      ++states;
    }

    // One pixel under, and the rows have to start scrolling rather than the
    // fader giving way.
    const juce::Rectangle<int> tight(0, 0, kStripWidth, preferred - 1);
    const auto bands = layoutBands(tight, mask);

    if (bands.maxScroll > 0)
      ++scrolling;
  }

  check(states == (1 << kNumSections) * 3,
        "every fold state lays out at three heights (" +
            std::to_string(states) + ")");
  check(scrolling == (1 << kNumSections),
        "and every one of them scrolls a pixel under its preferred height (" +
            std::to_string(scrolling) + ")");

  // The shortest window the bands allow, which is what the resize limits will
  // be built from. Far shorter than the old floor, which is the point: the old
  // one had to fit every row at once.
  const int shortest = minimumStripHeight(0);
  const auto squeezed = layoutBands({0, 0, kStripWidth, shortest}, 0);

  check(squeezed.middle.getHeight() >= 120,
        "the band keeps its four rows at the shortest window (" +
            std::to_string(squeezed.middle.getHeight()) + ")");
  check(squeezed.maxScroll > 0, "and scrolls there");

  // ---- what scrolling looks like from the outside -------------------------
  //
  // Two properties that were both wrong at first and that no height or total
  // would have caught.
  {
    const juce::Rectangle<int> area(0, 0, kStripWidth, 600);
    const auto bands = layoutBands(area, 0);

    check(bands.maxScroll > 0, "a 600px column has somewhere to scroll");

    int runs = 0, gaps = 0, overhang = 0;

    for (int scroll = 0; scroll <= bands.maxScroll; scroll += 17) {
      const auto rows = layoutRows(area, 0, scroll);

      // The rows on screen have to be one unbroken run in layout order. They
      // were not: a row too tall to fit was skipped and a shorter one behind
      // it took the space, so controls came and went out of order.
      bool started = false, ended = false, broken = false;
      int lastBottom = -1;

      for (int i = 0; i < kNumRows; ++i) {
        const auto &r = rows[(size_t)i];

        if ((Row)i == Row::Header || rowIsCollapsed((Row)i, 0))
          continue;

        if (r.getHeight() > 0) {
          if (ended)
            broken = true;
          started = true;
          lastBottom = r.getBottom();
        } else if (started) {
          ended = true;
        }
      }

      if (broken)
        ++runs;

      // And the run has to reach the foot of the band. It used to stop short
      // whenever the next row was too tall for what was left, leaving a strip
      // of nothing that read as the end of the list.
      if (lastBottom < bands.middle.getBottom())
        ++gaps;

      if (lastBottom > bands.middle.getBottom())
        ++overhang;
    }

    check(runs == 0, "the visible rows are one unbroken run at every scroll (" +
                         std::to_string(runs) + " broken)");
    check(gaps == 0, "and always reach the foot of the band (" +
                         std::to_string(gaps) + " short)");
    // Dragging the bar is now a plain pixel offset, so there is no snapping
    // left to be non-monotonic. What has to hold instead is that a row can sit
    // part way over the top of the band, since that is what smooth means and
    // what the column's header cap exists to cover.
    int clippedAtTop = 0;

    for (int scroll = 1; scroll <= bands.maxScroll; scroll += 13) {
      const auto rows = layoutRows(area, 0, scroll);

      for (int i = 0; i < kNumRows; ++i) {
        const auto &r = rows[(size_t)i];

        if (r.getHeight() > 0 && r.getY() < bands.middle.getY())
          ++clippedAtTop;
      }
    }

    check(clippedAtTop > 0,
          "a row can sit part way over the top of the band (" +
              std::to_string(clippedAtTop) + " of them)");

    check(overhang > 0,
          "with the last one cut off by it, which is what shows there is more "
          "below (" +
              std::to_string(overhang) + " of them)");
  }
}

/// The grain tile must not outlive the windows that use it.
///
/// It used to be a static, built once and kept for the life of the process,
/// which reads as an obvious saving and was a hang. A juce::Image on JUCE 9
/// under Windows is backed by Direct2D, so releasing one hands GPU resources
/// back, and a static is released when the host unloads the binary. On Windows
/// that runs under the loader lock, and the driver threads the teardown has to
/// reach are parked waiting for the same lock, so it never finishes. Hosts that
/// unload the binary hung on removing the plugin and hosts that keep it loaded
/// did not, which is exactly the split that was reported.
///
/// Nothing about that is visible from the outside except this: the tile is
/// built again after the last look and feel goes, rather than surviving it.
void testTheGrainTileDoesNotOutliveTheWindows() {
  section("The grain tile does not outlive the windows");

  const int before = ovt::ui::grainTileBuildCount();

  {
    ovt::ui::OvertoniumLookAndFeel laf;
    const auto tile = ovt::ui::grainTile();
    check(tile.isValid(), "the tile builds");
    check(tile.getWidth() == 128 && tile.getHeight() == 128,
          "at the size the panels tile at (" + std::to_string(tile.getWidth()) +
              " x " + std::to_string(tile.getHeight()) + ")");
  }

  const int afterFirst = ovt::ui::grainTileBuildCount();
  check(afterFirst == before + 1, "and is built once for a window, not twice");

  {
    ovt::ui::OvertoniumLookAndFeel laf;
    (void)ovt::ui::grainTile();
  }

  // The whole claim. If this reads equal, the tile survived the look and feel
  // that was holding it, which is the state that hangs a host on unload.
  check(ovt::ui::grainTileBuildCount() == afterFirst + 1,
        "and built afresh for the next one, having been released with the "
        "last (" +
            std::to_string(ovt::ui::grainTileBuildCount()) + " builds)");

  // Two at once share one, which is the saving the static was there for and
  // which the fix has to keep.
  const int beforeShared = ovt::ui::grainTileBuildCount();
  {
    ovt::ui::OvertoniumLookAndFeel one;
    ovt::ui::OvertoniumLookAndFeel two;
    (void)ovt::ui::grainTile();
    (void)ovt::ui::grainTile();
  }
  check(ovt::ui::grainTileBuildCount() == beforeShared + 1,
        "and two windows still share one tile between them");
}

/// A session file the plugin did not write must not be able to poison it.
///
/// setStateInformation already refuses anything that is not our XML, but a
/// file that is our XML with nonsense in the values gets through to the
/// parameters, which is what a corrupt or truncated session looks like. Of
/// everything such a file can say, NaN was the only thing that survived:
/// every clamp in JUCE's range handling is a pair of comparisons and both are
/// false against NaN, so jlimit hands it straight back. It reached the
/// oscillators and the plugin output NaN for the rest of the session, which in
/// a host is a silent master bus until someone reloads it.
void testACorruptStateCannotPoisonTheOutput() {
  section("A corrupt session cannot poison the output");

  const char *poisons[] = {"nan",   "-nan",   "inf",     "-inf",  "1e30",
                           "-1e30", "999999", "-999999", "hello", ""};

  int checked = 0;

  for (const char *poison : poisons) {
    OvertoniumProcessor victim;
    victim.setRateAndBufferSizeDetails(48000.0, 256);
    victim.prepareToPlay(48000.0, 256);
    victim.applyFactoryPreset(presetIndex("Big Saw"));

    auto state = victim.apvts.copyState();
    auto xml = state.createXml();

    int poisoned = 0;
    for (auto *child : xml->getChildIterator())
      if (child->hasAttribute("value")) {
        child->setAttribute("value", poison);
        ++poisoned;
      }

    // If this ever reads zero the test is passing on an empty state rather
    // than a poisoned one, which is the way a check like this goes quietly
    // wrong.
    if (poisoned == 0) {
      check(false, std::string("the state carried values to poison with \"") +
                       poison + "\"");
      continue;
    }

    juce::MemoryBlock block;
    victim.copyXmlToBinary(*xml, block);
    victim.setStateInformation(block.getData(), (int)block.getSize());

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    const auto stats = renderBlocks(victim, 200, 256, midi);

    check(stats.finite, std::string("a state of \"") + poison +
                            "\" in every value still renders a number");
    ++checked;
  }

  check(checked == (int)std::size(poisons), "every poison was tried");
}

/// The instrument has to be an instrument at every rate a host can ask for.
///
/// The suite otherwise lives at 48 kHz with a few excursions, and 88.2, 176.4
/// and 192 kHz were never rendered at all. Every delay line in the effects
/// sizes itself from the sample rate, which is exactly the kind of arithmetic
/// that is right at one rate and wrong at four times it.
///
/// Both halves matter. Finite output from a plugin that rendered silence
/// proves nothing, so this insists the preset actually sounds at every rate,
/// and that the level is the same one, since a filter cutoff worked out in the
/// wrong units would still be finite and would not be Big Saw.
void testEveryHostRateStaysFinite() {
  section("Every host rate renders");

  const double rates[] = {8000.0,  22050.0, 44100.0,  48000.0,
                          88200.0, 96000.0, 176400.0, 192000.0};

  double lowest = 1.0e9, highest = 0.0;

  for (double rate : rates) {
    for (int block : {64, 512}) {
      OvertoniumProcessor fresh;
      fresh.setRateAndBufferSizeDetails(rate, block);
      fresh.prepareToPlay(rate, block);
      fresh.applyFactoryPreset(presetIndex("Big Saw"));

      juce::MidiBuffer midi;
      midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
      midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

      const auto stats =
          renderBlocks(fresh, (int)(1.0 * rate / block) + 1, block, midi);

      check(stats.finite, "at " + std::to_string((int)rate) +
                              " Hz in blocks "
                              "of " +
                              std::to_string(block) +
                              " every sample is a number");
      check(stats.peak > 1.0e-3, "and the instrument actually sounds (" +
                                     std::to_string(stats.peak) + ")");

      // And in tune, which is the half that finiteness and loudness cannot
      // see. An engine that believes every host runs at 48 kHz renders a
      // perfectly good note two octaves down at 192 kHz: finite, audible and
      // exactly as loud. Only the pitch says otherwise, so the pitch is what
      // is asked.
      if (block == 512) {
        OvertoniumProcessor tone;
        tone.setRateAndBufferSizeDetails(rate, block);
        tone.prepareToPlay(rate, block);
        tone.applyFactoryPreset(presetIndex("Big Saw"));

        const auto wave = renderTone(tone, rate, block, 60, 0.6);
        const double f0 = 261.6255653; // middle C
        const double at = toneAt(wave, f0, rate);
        const double below = toneAt(wave, f0 / 4.0, rate);
        const double above = toneAt(wave, f0 * 4.0, rate);

        // Measured across every rate here, the fundamental runs 150 to 400
        // times the subharmonic and 12 to 17 times the fourth harmonic, so
        // these leave a factor of three either way rather than sitting on
        // the number.
        check(at > 20.0 * below,
              "and middle C at " + std::to_string((int)rate) +
                  " Hz is not two octaves flat (" + std::to_string(at) +
                  " against " + std::to_string(below) + ")");
        check(at > 4.0 * above,
              "nor two octaves sharp (" + std::to_string(above) + ")");
      }

      // 8 kHz is left out of the level comparison on purpose: most of the
      // series is above its Nyquist and is correctly not there, which is a
      // quieter patch rather than a broken one.
      if (rate > 20000.0) {
        lowest = std::min(lowest, (double)stats.peak);
        highest = std::max(highest, (double)stats.peak);
      }
    }
  }

  // Measured at 0.457 to 0.462 across 22 kHz to 192 kHz, so a tenth is slack
  // rather than a target. A rate-dependent coefficient shows up here long
  // before it is audible.
  check(highest < lowest * 1.1,
        "and the same patch is the same loudness at every rate above 20 kHz (" +
            std::to_string(lowest) + " to " + std::to_string(highest) + ")");
}

/// The safety clip's whole job is a bound, so the bound is checked.
///
/// It was not. One test switches the clipper off to measure headroom and
/// nothing anywhere switched it on to see whether it does what its name says.
/// The clipper is threshold + (1 - threshold) * tanh, which cannot exceed one
/// by construction, and this is what holds that construction in place.
void testTheSafetyClipHoldsUnity() {
  section("The safety clip holds unity");

  int sounded = 0;
  float worst = 0.0f;

  for (double rate : {44100.0, 48000.0, 96000.0}) {
    OvertoniumProcessor fresh;
    fresh.setRateAndBufferSizeDetails(rate, 256);
    fresh.prepareToPlay(rate, 256);
    fresh.applyFactoryPreset(presetIndex("Big Saw"));

    const auto set = [&](const char *id, float v) {
      if (auto *q = fresh.apvts.getParameter(id))
        q->setValueNotifyingHost(v);
    };

    // Driving this by setting every parameter to its maximum does not work:
    // that includes the envelope's delay and attack, and nothing sounds at all
    // inside the render. The master opened to +12 dB over a patch that already
    // sounds, with a fistful of notes on it, is what makes it loud.
    set(ovt::params::masterGainId, 1.0f);
    set(ovt::params::safetyClipId, 1.0f);

    // Every one of the five, because the bound is the stage's promise rather
    // than a property of whichever shape happens to be selected. The limiter
    // is the one that does not keep it by its shape: it keeps it by seeing
    // the peak coming, with a hard clip behind it for whatever its window is
    // too short to have seen, and this is what says so.
    for (int type = 0; type < (int)ovt::ClipType::NumTypes; ++type) {
      set(ovt::params::clipTypeId,
          (float)type / (float)((int)ovt::ClipType::NumTypes - 1));

      juce::MidiBuffer chord;
      for (int note : {36, 43, 48, 52, 55, 59, 60, 64, 67, 72})
        chord.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

      const auto each =
          renderBlocks(fresh, (int)(1.0 * rate / 256.0) + 1, 256, chord);

      check(each.finite && each.peak <= 1.0f,
            std::string(ovt::clipTypeName((ovt::ClipType)type)) +
                " stays inside unity at " + std::to_string((int)rate) +
                " Hz (" + std::to_string(each.peak) + ")");
    }

    set(ovt::params::clipTypeId, 0.0f);

    juce::MidiBuffer midi;
    for (int note : {36, 43, 48, 52, 55, 59, 60, 64, 67, 72})
      midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

    const auto stats =
        renderBlocks(fresh, (int)(1.5 * rate / 256.0) + 1, 256, midi);

    check(stats.finite, "the clipped output at " + std::to_string((int)rate) +
                            " Hz is a number");
    check(stats.peak <= 1.0f,
          "and never leaves unity (" + std::to_string(stats.peak) + ")");

    worst = std::max(worst, stats.peak);
    if (stats.peak > 1.0e-3)
      ++sounded;
  }

  // Without this the two checks above pass on silence, which is how the first
  // attempt at this test passed while rendering nothing.
  check(sounded == 3, "and it was driven hard enough to mean it (peak " +
                          std::to_string(worst) + ")");
}

/// Fit all 32 channels must land somewhere a drag can get back to.
///
/// setSize consults no limits, so the fit could put the window below the
/// shortest one the constrainer allows. It did: the floor was worked out at
/// the narrowest width, where the top bar takes three rows, and used at every
/// width, so a fitted window sat 84 pixels under a floor of 997 and the first
/// drag afterwards snapped it up. The floor now follows the width, which fixes
/// that and also lets a wide window be dragged shorter than the fit leaves it,
/// since the bar is two rows shorter there.
void testFittingLandsWhereADragCanReturn(OvertoniumProcessor &p) {
  section("Fitting lands where a drag can return");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  const auto *limits = editor->getConstrainer();

  check(limits != nullptr, "and has limits on it");
  if (limits == nullptr)
    return;

  editor->fitAllChannels();

  const int fitted = editor->getHeight();
  const int wideFloor = limits->getMinimumHeight();

  check(fitted >= wideFloor,
        "the fitted window is at or above its own floor (" +
            std::to_string(fitted) + " against " + std::to_string(wideFloor) +
            ")");

  // Which is the half that would have passed on its own if the floor were
  // simply lowered everywhere. It must still be a floor: narrow the window to
  // where the bar needs another row and it has to rise.
  const int fittedWidth = editor->getWidth();

  editor->setSize(700, editor->getHeight());

  const int narrowFloor = limits->getMinimumHeight();

  check(
      narrowFloor > wideFloor,
      "and narrowing it, where the bar takes another row, raises the floor (" +
          std::to_string(wideFloor) + " to " + std::to_string(narrowFloor) +
          ")");

  // There used to be a third check here, that the fitted window came out
  // shorter than a narrow one's floor. It caught the wide window being held to
  // the narrow one's chrome, and it worked because the floor was the height of
  // every row at once and left no slack. Now that the rows scroll, the floor
  // is far below both and the comparison cannot fail whether the fault is
  // there or not. The check above it still catches the fault, by asking
  // whether the floor moves with the width at all.

  // The window has to have been taken with it, or the bar gains a row into
  // space the mixer is still using and the strips run off the bottom.
  check(editor->getHeight() >= narrowFloor,
        "and takes the window up with it rather than leaving it short");

  // Back out again, and the fit's height stops being the shortest thing
  // available: the whole point of the floor following the width.
  editor->setSize(fittedWidth, editor->getHeight());

  check(limits->getMinimumHeight() == wideFloor,
        "widening it puts the floor back");
  check(limits->getMinimumHeight() < fitted,
        "so a wide window can be dragged shorter than fitting leaves it");
}

void testFoldingAndUnfoldingIsSymmetric(OvertoniumProcessor &p) {
  section("Folding and unfolding leaves the window where it was");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  ovt::ui::ChannelStrip *strip = nullptr;
  std::function<void(juce::Component &)> find = [&](juce::Component &c) {
    if (strip == nullptr)
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(&c))
        strip = s;
    for (auto *child : c.getChildren())
      find(*child);
  };
  find(*editor);

  check(strip != nullptr, "and carries a strip whose rules fold sections");
  if (strip == nullptr)
    return;

  // Squeezed: down to the shortest the window will go, which is where the
  // fader has given up everything it can and the fault shows. Asked of the
  // constrainer rather than set to some small number, since setSize on its own
  // does not go through it and a host's drag does.
  auto *limits = editor->getConstrainer();

  check(limits != nullptr, "and has resize limits to squeeze it against");
  if (limits == nullptr)
    return;

  editor->setSize(editor->getWidth(), limits->getMinimumHeight());

  const auto squeezed = editor->getHeight();

  // A second apart, because a strip throws away a click bearing the same time
  // as the one before it: that is how the echo of its own listener is told
  // from a real click. Two folds a microsecond apart are one fold.
  int clicks = 0;

  const auto foldOnce = [&](ovt::ui::Section s) {
    const auto rows =
        ovt::ui::layoutRows(strip->getLocalBounds().reduced(
                                ovt::ui::kStripPadX, ovt::ui::kStripPadY),
                            0);

    const auto rule = rows[(size_t)ovt::ui::sectionHeading(s)];
    const auto when =
        juce::Time::getCurrentTime() + juce::RelativeTime(++clicks);

    strip->mouseDown(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(),
        rule.getCentre().toFloat(),
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, strip, strip, when,
        rule.getCentre().toFloat(), when, 1, false));
  };

  foldOnce(ovt::ui::Section::Envelope);
  const auto folded = editor->getHeight();

  foldOnce(ovt::ui::Section::Envelope);
  const auto back = editor->getHeight();

  std::printf("  squeezed to %d, folded to %d, unfolded back to %d\n", squeezed,
              folded, back);

  // Folding used to take the window down by exactly the rows it hid, because
  // every row had to be on screen. Now they scroll, so folding is about seeing
  // more at once and the window is left alone, which is the half of issue #5
  // about collapsing a section shifting the mixer out of view.
  check(folded == squeezed, "folding leaves the window where it is (" +
                                std::to_string(squeezed) + " to " +
                                std::to_string(folded) + ")");

  check(back == squeezed, "and unfolding puts it back, not past it (" +
                              std::to_string(squeezed) + " to " +
                              std::to_string(back) + ")");

  // ---- and from a window with room to spare ------------------------------
  //
  // This used to be the case the ordering in toggleSection was written for,
  // back when folding moved the window and the order of the arithmetic against
  // the limits decided whether it moved by the right amount. There is no
  // arithmetic left to get wrong: the window holds still at both heights.
  {
    editor->setSize(editor->getWidth(), limits->getMinimumHeight() + 240);

    const auto roomy = editor->getHeight();

    foldOnce(ovt::ui::Section::KeyOff);
    const auto shorter = editor->getHeight();

    foldOnce(ovt::ui::Section::KeyOff);

    std::printf("  with room to spare: %d, folded to %d, back to %d\n", roomy,
                shorter, editor->getHeight());

    check(shorter == roomy && editor->getHeight() == roomy,
          "a window with room holds still through a fold and an unfold (" +
              std::to_string(roomy) + " to " +
              std::to_string(editor->getHeight()) + ")");
  }
}

/// LINK comes back the way it was left.
///
/// Reported as LINK not being retained when the plugin is closed and opened.
/// Two thirds of it were: the scope and the curve have always been written to
/// the editor's state, and the switch that decides whether either of them
/// applies was not. So a window reopened remembering exactly how a drag would
/// be shared out, with the drag switched off.
///
/// All three are checked rather than the one that was missing, so the next
/// setting added here cannot be the one forgotten.
void testLinkSurvivesAReopen(OvertoniumProcessor &p) {
  section("LINK survives a reopen");

  const auto reopen = [&p]() {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());
    auto *bar = editor != nullptr ? findTopBar(*editor) : nullptr;

    return std::pair<std::unique_ptr<juce::AudioProcessorEditor>,
                     ovt::ui::TopBar *>(std::move(base), bar);
  };

  // ---- left on, and set to something other than the defaults --------------
  {
    auto [window, bar] = reopen();

    check(bar != nullptr, "the editor opens with a bar");
    if (bar == nullptr)
      return;

    bar->setLinkEnabled(true);
    bar->setLinkScope(ovt::ui::LinkScope::Odd);
    bar->setLinkCurve(ovt::ui::LinkCurve::Spread);

    check(bar->onLinkSettingsChanged != nullptr,
          "and the bar reports its settings to the editor");

    if (bar->onLinkSettingsChanged)
      bar->onLinkSettingsChanged();
  }

  {
    auto [window, bar] = reopen();

    check(bar != nullptr, "it opens again");
    if (bar == nullptr)
      return;

    std::printf("  reopened with LINK %s, scope %s, curve %s\n",
                bar->isLinkEnabled() ? "on" : "off",
                ovt::ui::linkScopeName(bar->getLinkScope()),
                ovt::ui::linkCurveName(bar->getLinkCurve()));

    check(bar->isLinkEnabled(), "with LINK still on");
    check(bar->getLinkScope() == ovt::ui::LinkScope::Odd,
          "and the scope it was left on");
    check(bar->getLinkCurve() == ovt::ui::LinkCurve::Spread,
          "and the curve it was left on");
  }

  // ---- and switched off again, which has to stick as well ------------------
  //
  // A setting that is only written when it is true reads as working until
  // somebody turns it off.
  {
    auto [window, bar] = reopen();

    if (bar == nullptr)
      return;

    bar->setLinkEnabled(false);

    if (bar->onLinkSettingsChanged)
      bar->onLinkSettingsChanged();
  }

  {
    auto [window, bar] = reopen();

    if (bar == nullptr)
      return;

    check(!bar->isLinkEnabled(), "switching it off sticks too");
    check(bar->getLinkScope() == ovt::ui::LinkScope::Odd,
          "and the scope is still where it was");
  }
}

/// Holding the modifier draws the faders a drag passes over.
///
/// Setting neighbouring partials one fader at a time is the tedious way to
/// shape a spectrum, which is most of what this instrument is for. Held, a
/// drag across the fader area sets each column it crosses from the pointer's
/// height instead of moving one of them.
///
/// It is not LINK by another name. LINK shares one relative move out across a
/// scope by a rule; this sets absolute values freehand, and neither can do the
/// other's job.
void testDrawingAcrossTheFaders(OvertoniumProcessor &p) {
  section("Drawing the faders");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  editor->setSize(editor->getWidth(), editor->getHeight());

  auto *target = dynamic_cast<ovt::ui::LinkTarget *>(editor);

  check(target != nullptr, "and takes drawn drags");
  if (target == nullptr)
    return;

  const auto levelOf = [&p](int channel) {
    auto *param = p.apvts.getParameter(
        ovt::params::oscParamId(ovt::params::volumeSuffix, channel));
    return param != nullptr ? param->getValue() : -1.0f;
  };

  // Where each strip's fader stands on screen, so a line can be drawn across
  // them the way a hand would.
  std::vector<ovt::ui::ChannelStrip *> strips;
  std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
    if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(&c))
      strips.push_back(s);
    for (auto *child : c.getChildren())
      gather(*child);
  };
  gather(*editor);

  check(strips.size() == (size_t)ovt::kNumHarmonics,
        "and shows all its channels (" + std::to_string(strips.size()) + ")");

  if (strips.size() < 4)
    return;

  // The two switches in the caption gutter, found by the words on them. DRAW
  // latches the tool on without the modifier, which is what a test can reach:
  // holding a key is not something a headless run can do.
  const auto switchNamed = [&](const juce::String &text) -> juce::TextButton * {
    juce::TextButton *found = nullptr;

    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      if (auto *b = dynamic_cast<juce::TextButton *>(&c))
        if (b->getButtonText() == text)
          found = b;
      for (auto *child : c.getChildren())
        look(*child);
    };
    look(*editor);

    return found;
  };

  auto *drawSwitch = switchNamed("DRAW");
  auto *linkSwitch = switchNamed("LINK");

  check(drawSwitch != nullptr && linkSwitch != nullptr,
        "the gutter carries a DRAW switch beside LINK");

  if (drawSwitch == nullptr || linkSwitch == nullptr)
    return;

  // ---- without the modifier, nothing is taken -----------------------------
  //
  // The fader moves itself, as it always has, and the drawing never hears
  // about the drag.
  {
    const auto here =
        strips[0]->localPointToGlobal(strips[0]->getLocalBounds().getCentre());

    check(!target->drawStarted(here),
          "a drag with nothing held is left to the fader");
  }

  // ---- held, a line across four columns sets all four ----------------------
  {
    if (drawSwitch->onClick)
      drawSwitch->onClick();

    check(drawSwitch->getToggleState(), "the switch lights when it is latched");

    // ---- and the faders light to say where the drawing reaches ------------
    //
    // The same preview LINK uses to say what the next drag would touch, in the
    // accent rather than each channel's own colour, since every fader is
    // equally drawable and the band is one surface rather than 33 answers.
    {
      const auto faderOf = [](juce::Component &strip) -> juce::Slider * {
        juce::Slider *found = nullptr;

        std::function<void(juce::Component &)> look = [&](juce::Component &c) {
          if (auto *s = dynamic_cast<juce::Slider *>(&c))
            if (s->getSliderStyle() == juce::Slider::LinearVertical)
              found = s;
          for (auto *child : c.getChildren())
            look(*child);
        };
        look(strip);

        return found;
      };

      auto *fader = faderOf(*strips[0]);

      check(fader != nullptr, "a strip has a fader to light");

      if (fader != nullptr) {
        const auto glow =
            (double)fader->getProperties().getWithDefault("linkGlow", 0.0);
        const auto accent =
            (bool)fader->getProperties().getWithDefault("glowAccent", false);

        std::printf("  armed, a fader glows at %.2f, in the accent: %s\n", glow,
                    accent ? "yes" : "no");

        check(glow > 0.9, "the faders light while the tool is armed (" +
                              std::to_string(glow) + ")");
        check(accent, "and in the accent rather than the channel's colour");
      }
    }

    const auto before = levelOf(1);

    // Along the tops of the strips, which is full level, from channel 1 to 4.
    const auto top = [&](int i) {
      const auto bounds = strips[(size_t)i]->getLocalBounds();
      return strips[(size_t)i]->localPointToGlobal(
          juce::Point<int>(bounds.getCentreX(), bounds.getY()));
    };

    check(target->drawStarted(top(0)), "a drag with it held is taken");

    target->drawMovedTo(top(3));
    target->drawEnded();

    std::printf("  channels 1 to 4 drawn to %.3f, %.3f, %.3f, %.3f, from "
                "%.3f\n",
                levelOf(0), levelOf(1), levelOf(2), levelOf(3), before);

    bool allUp = true;
    for (int i = 0; i < 4; ++i)
      allUp &= levelOf(i) > 0.9f;

    check(allUp, "every channel the line crossed went with it");

    // ---- and the columns between are not skipped -------------------------
    //
    // Two events can be several strips apart, and drawing only where they
    // landed leaves holes exactly where the hand moved fastest.
    check(levelOf(1) > 0.9f && levelOf(2) > 0.9f,
          "including the ones no event landed on");
  }

  // ---- and a channel the line never reached is untouched ------------------
  {
    check(levelOf(20) < 0.9f, "a channel away from the line is left alone (" +
                                  std::to_string(levelOf(20)) + ")");
  }

  // ---- and LINK reads as off while it is armed ----------------------------
  //
  // Without being off: the setting is untouched and comes back the moment the
  // tool is let go. A switch left lit for a gesture that has been taken away
  // from it is a lie the mouse-up would expose.
  {
    check(!linkSwitch->getToggleState(),
          "LINK reads as off while drawing has the drag");

    if (drawSwitch->onClick)
      drawSwitch->onClick();

    check(!drawSwitch->getToggleState(), "and the switch goes out again");

    // Nothing is left lit once the tool is let go, which is the half a
    // highlight most easily gets wrong.
    juce::Slider *fader = nullptr;
    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      if (auto *s = dynamic_cast<juce::Slider *>(&c))
        if (s->getSliderStyle() == juce::Slider::LinearVertical)
          fader = s;
      for (auto *child : c.getChildren())
        look(*child);
    };
    look(*strips[0]);

    if (fader != nullptr)
      check(ovt::exactly(
                (double)fader->getProperties().getWithDefault("linkGlow", 1.0),
                0.0),
            "and the faders go dark with it");
  }
}

/// The tick in the Zoom submenu, which is the only thing on the panel that
/// says which zoom you are at.
///
/// The editor holds the zoom and the bar holds a copy of it to draw the tick
/// from, and the restore was the only thing that ever wrote the copy. So the
/// window scaled correctly and the menu went on ticking 100%. Driven through
/// the callback the menu item calls, since opening the menu needs a window.
void testZoomTickFollowsTheZoom(OvertoniumProcessor &p) {
  section("The zoom tick follows the zoom");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  auto *bar = findTopBar(*editor);

  check(bar != nullptr, "and carries a bar to read the menu off");
  if (bar == nullptr)
    return;

  const auto ticked = [bar]() {
    // Named rather than iterated off the call, since the iterator keeps a
    // reference to it. Whatever is ticked, so two ticks fail as loudly as none.
    auto menu = bar->buildSettingsMenu();
    std::string found;

    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
      const auto &item = it.getItem();

      if (item.text != "Zoom" || item.subMenu == nullptr)
        continue;

      for (juce::PopupMenu::MenuItemIterator sub(*item.subMenu); sub.next();)
        if (sub.getItem().isTicked)
          found += sub.getItem().text.toStdString();
    }

    return found;
  };

  // Every zoom the menu offers, asked for in turn rather than compared against
  // where the state happened to leave this editor, and ending at 100% for
  // whatever runs next.
  for (const auto factor : {1.25f, 0.75f, 1.5f, 1.0f}) {
    const auto reads = std::to_string(juce::roundToInt(factor * 100.0f)) + "%";

    bar->onZoomChanged(factor);

    check(ticked() == reads,
          "the window at " + reads + " ticks " + reads + " (" + ticked() + ")");
  }
}

/// That every word the bar has to say fits in the button that says it.
///
/// The bar shouts in capitals at a font it picks from its own height, so the
/// widest name a control can show decides how wide that control has to be.
/// Measured rather than eyeballed, because the failure is silent: a name that
/// does not fit is drawn with the middle taken out of it and nothing says so.
void testBarButtonsFitTheirWords(OvertoniumProcessor &p) {
  section("Words on the bar");

  using namespace ovt::ui;

  juce::Component popupParent;
  TopBar bar(p.apvts, popupParent);

  // The height the bar lays its controls out at, which is what the font comes
  // from. See kControlHeight.
  const auto font = makeFont(13.0f, true);

  const auto widest = [&font](const juce::StringArray &words) {
    int most = 0;
    for (const auto &w : words)
      most =
          std::max(most, (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                             font, w.toUpperCase())));

    return most;
  };

  // The short names, which is what the button shows, plus the word it shows
  // instead when the effect is off. The menu says what each machine is and has
  // the room to; the caption under the button says which effect it belongs to.
  juce::StringArray echoWords{"Off"};
  for (int i = 0; i < (int)ovt::EchoType::NumTypes; ++i)
    echoWords.add(ovt::echoTypeShortName((ovt::EchoType)i));

  // The short names again, which is what the button shows.
  juce::StringArray reverbWords{"Off"};
  for (int i = 0; i < (int)ovt::ReverbType::NumTypes; ++i)
    reverbWords.add(ovt::reverbTypeShortName((ovt::ReverbType)i));

  const auto reverbText = widest(reverbWords);
  const auto echoText = widest(echoWords);
  const auto characterText = widest(ovt::params::characterChoices());

  std::printf("  the echo's longest word is %d px and the character's %d, in "
              "buttons of %d and %d\n",
              echoText, characterText, TopBar::kEchoWidth,
              TopBar::kCharacterWidth);

  // Air either side, which every other button on the bar has.
  check(echoText + 8 <= TopBar::kEchoWidth,
        "every machine the echo can be fits in its button (" +
            std::to_string(echoText) + " px in " +
            std::to_string(TopBar::kEchoWidth) + ")");

  check(characterText + 8 <= TopBar::kCharacterWidth,
        "and every character fits in its own (" +
            std::to_string(characterText) + " px in " +
            std::to_string(TopBar::kCharacterWidth) + ")");

  std::printf("  the reverb's longest word is %d px, in a button of %d\n",
              reverbText, TopBar::kReverbWidth);

  check(reverbText + 8 <= TopBar::kReverbWidth,
        "and every reverb fits in its own (" + std::to_string(reverbText) +
            " px in " + std::to_string(TopBar::kReverbWidth) + ")");

  // ---- the clipper's switch, which is the shortest button on the bar -------
  //
  // At 16 px tall it picks a 9 px font for itself rather than the 13 the taller
  // buttons get, so it is measured at that rather than at theirs.
  {
    const auto shortFont =
        makeFont(juce::jlimit(8.0f, 13.0f, 16.0f * 0.58f), true);
    const auto clipText = (int)std::ceil(juce::GlyphArrangement::getStringWidth(
        shortFont, juce::String(TopBar::kClipName)));

    std::printf("  %s measures %d px in a button of %d\n", TopBar::kClipName,
                clipText, TopBar::kClipWidth);

    check(clipText + 8 <= TopBar::kClipWidth,
          "the clipper's switch fits its word (" + std::to_string(clipText) +
              " px in " + std::to_string(TopBar::kClipWidth) + ")");
  }

  // ---- and the word under each of them -------------------------------------
  //
  // The button shows a value, so the group is named underneath it in the band
  // the knob captions occupy, at the font those captions use. A word wider
  // than the button it sits under is drawn with its middle taken out, and the
  // knob caption six pixels to its right would have it running into that.
  {
    const auto captionFont = makeFont(9.0f, true);

    const int widths[] = {TopBar::kCharacterWidth, TopBar::kEchoWidth,
                          TopBar::kReverbWidth};

    for (size_t i = 0; i < std::size(TopBar::kGroupNames); ++i) {
      const juce::String word(TopBar::kGroupNames[i]);
      const auto measured = (int)std::ceil(
          juce::GlyphArrangement::getStringWidth(captionFont, word));

      std::printf("  %-9s under a button of %d px measures %d\n",
                  word.toRawUTF8(), widths[i], measured);

      check(measured + 4 <= widths[i],
            "the word " + word.toStdString() +
                " fits under the button it names (" + std::to_string(measured) +
                " px in " + std::to_string(widths[i]) + ")");
    }
  }
}

/// The width the bar comes onto one row at, which the design notes quote and
/// which every control added to it moves.
void testBarComesOntoOneRow(OvertoniumProcessor &) {
  section("One row");

  using namespace ovt::ui;

  const auto tall = TopBar::heightForWidth(900);
  int onOneRow = 0;

  for (int w = 900; w <= 1600; ++w)
    if (TopBar::heightForWidth(w) < tall) {
      onOneRow = w;
      break;
    }

  std::printf("  the bar comes onto one row at %d px, and the window opens at "
              "1340, so there are %d px of slack\n",
              onOneRow, 1340 - onOneRow);

  check(onOneRow > 0 && onOneRow <= 1340,
        "the bar is on one row at the width the window opens at (" +
            std::to_string(onOneRow) + ")");

  // The figure the design notes quote. It moves whenever a control on the bar
  // changes width, and when it moves the notes move with it.
  check(onOneRow == 1258, "and comes onto it at the width written down (" +
                              std::to_string(onOneRow) + ")");
}

void testTopBarAlignment(OvertoniumProcessor &p) {
  section("Top bar alignment");

  using namespace ovt::ui;

  juce::Component popupParent;
  TopBar bar(p.apvts, popupParent);

  const auto centresAt = [&bar](int width) {
    bar.setSize(width, TopBar::heightForWidth(width));

    std::vector<int> centres;

    for (auto *child : bar.getChildren()) {
      // A group that did not fit was parked rather than placed.
      if (child->getBounds().isEmpty())
        continue;

      // The converter readouts belong in the caption band under the meter
      // rather than on the line with the controls, which is checked separately
      // below.
      if (dynamic_cast<SegmentDisplay *>(child) != nullptr)
        continue;

      // Nor does the master fader, which is aligned to the lamps it lies over
      // rather than to the row. The meter carrying those lamps is on the row
      // and is checked like everything else, and the fader is checked against
      // the meter below, which is the alignment that can actually be seen.
      if (auto *slider = dynamic_cast<juce::Slider *>(child))
        if (slider->getSliderStyle() == juce::Slider::LinearHorizontal)
          continue;

      // Nor the clipper's switch, which stands in the caption band beside the
      // readouts rather than on the line with the other buttons. Checked with
      // them below, for the same reason they are.
      if (auto *button = dynamic_cast<juce::TextButton *>(child))
        if (button->getButtonText() == TopBar::kClipName)
          continue;

      // A knob's line is its dial, not the control, which reaches further down
      // to hold the caption.
      if (auto *knob = dynamic_cast<LabelledKnob *>(child))
        centres.push_back(knob->getY() + knob->slider.getBounds().getCentreY());
      else
        centres.push_back(child->getBounds().getCentreY());
    }

    return centres;
  };

  // The fader has one alignment to keep and it is not the row's: it has to
  // stand on the lamps. A cap floating below them reads as a control that has
  // come loose, and it did, because the meter keeps its decibel marks in a
  // strip under the bars and the fader was given the whole meter to lie on.
  {
    bar.setSize(1412, TopBar::heightForWidth(1412));

    const StereoOutputMeter *meter = nullptr;
    const juce::Slider *fader = nullptr;

    for (auto *child : bar.getChildren()) {
      if (auto *m = dynamic_cast<StereoOutputMeter *>(child))
        meter = m;

      if (auto *s = dynamic_cast<juce::Slider *>(child))
        if (s->getSliderStyle() == juce::Slider::LinearHorizontal)
          fader = s;
    }

    check(meter != nullptr && fader != nullptr,
          "the output group has a meter and a fader over it");

    if (meter != nullptr && fader != nullptr) {
      const auto lamps = meter->barBounds() + meter->getPosition();

      check(fader->getBounds().getCentreY() == lamps.getCentreY(),
            "and the fader is centred on the lamps rather than on the scale "
            "marks under them");

      // Standing proud of them at both ends, like a channel's cap against its
      // own meter, and by the same amount at each end so it stays centred.
      const auto proud = lamps.getY() - fader->getBounds().getY();

      check(proud > 0 && proud <= 4,
            "and stands a little proud of them at both ends (" +
                std::to_string(proud) + " px)");
    }
  }

  // One row, two rows and three, since each row lays itself out afresh.
  for (int width : {1412, 1100, 900, TopBar::minimumWidth()}) {
    const auto centres = centresAt(width);
    const auto at = " (" + std::to_string(width) + " px)";

    check(centres.size() > 10, "the bar places its controls" + at);

    // Rows are more than 30 px apart, so two controls are either on the same
    // line, which has to be exactly the same line, or on different rows. A gap
    // in between is a control that missed.
    bool aligned = true;
    for (size_t i = 0; i < centres.size(); ++i)
      for (size_t j = i + 1; j < centres.size(); ++j) {
        const auto apart = std::abs(centres[i] - centres[j]);
        aligned &= apart == 0 || apart > 30;
      }

    check(aligned, "every knob, button, list and meter sharing a row stands on "
                   "one line" +
                       at);
  }

  // And the three things under the meter sit under it, inside the row, at
  // every width the bar can be given. Excluding them from the rule above would
  // otherwise be a hole rather than a decision.
  for (int width : {1412, 1100, 900, TopBar::minimumWidth()}) {
    bar.setSize(width, TopBar::heightForWidth(width));

    juce::Rectangle<int> meterBounds;
    juce::Array<juce::Rectangle<int>> under;

    for (auto *child : bar.getChildren()) {
      if (dynamic_cast<StereoOutputMeter *>(child) != nullptr)
        meterBounds = child->getBounds();

      if (dynamic_cast<SegmentDisplay *>(child) != nullptr)
        under.add(child->getBounds());

      if (auto *button = dynamic_cast<juce::TextButton *>(child))
        if (button->getButtonText() == TopBar::kClipName)
          under.add(button->getBounds());
    }

    const auto at = " (" + std::to_string(width) + " px)";

    check(under.size() == 3,
          "both converter readouts and the clipper's switch are placed" + at);

    bool below = !meterBounds.isEmpty();
    for (const auto &r : under)
      below &= !r.isEmpty() && r.getY() >= meterBounds.getBottom() &&
               r.getBottom() <= bar.getHeight();

    check(below,
          "and all three sit under the meter without leaving the bar" + at);

    // Side by side rather than stacked or overlapping.
    bool apart = true;
    for (int i = 0; i < under.size(); ++i)
      for (int j = i + 1; j < under.size(); ++j)
        apart &= !under[i].intersects(under[j]);

    check(apart, "and none of the three overlaps another" + at);
  }

  // A number with no unit beside it is a number nobody can read. At the width
  // the window opens at, both readouts have to be wide enough to name what
  // they are counting.
  //
  // This is what moving LINK off the bar was for. With it there, the output
  // group was squeezed forty pixels at this width and both readouts came out
  // at 38, which draws the figure and nothing else.
  {
    const int width = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 +
                      ovt::kNumHarmonics * ovt::ui::kStripWidth;

    bar.setSize(width, TopBar::heightForWidth(width));

    // A window this wide can hold the whole bar on one line. Anything wider
    // cannot need fewer rows than this, so it is the comparison to make.
    check(TopBar::heightForWidth(width) == TopBar::heightForWidth(2400),
          "the bar lays out in one row at the width the window opens at (" +
              std::to_string(width) + " px)");

    int named = 0;

    for (auto *child : bar.getChildren())
      if (dynamic_cast<SegmentDisplay *>(child) != nullptr)
        named += SegmentDisplay::hasRoomForUnit(child->getWidth()) ? 1 : 0;

    check(named == 2, "and both converter readouts can name their unit there");
  }
}

/// Undo, which is only worth having if a LINK drag across 32 channels comes
/// back in one step. The round trip runs parameter -> value tree -> undo
/// manager and back again.
/// A factory preset has to give the same instrument whatever was loaded
/// before it.
///
/// Written against every parameter rather than against a list, so it catches
/// the next global somebody adds and forgets to reset, which is exactly how
/// STRETCH, TRACK and the converter each got missed.
void testPresetsAreReproducible(OvertoniumProcessor &p) {
  section("Presets start from a known state");

  // What a preset deliberately leaves alone: how you play it, as opposed to
  // what it sounds like, which takes in how loud it is. Asked of the one list
  // rather than copied into a second one here, which is the whole reason that
  // list is shared. The copy this replaced had already fallen three settings
  // behind it.

  const auto snapshot = [&p] {
    std::vector<std::pair<juce::String, float>> out;

    for (auto *raw : p.getParameters())
      if (auto *r = dynamic_cast<juce::RangedAudioParameter *>(raw))
        out.emplace_back(r->paramID, r->convertFrom0to1(r->getValue()));

    return out;
  };

  // Something has to be left in a state no preset would produce, or the test
  // proves nothing.
  const auto makeAMess = [&p] {
    const auto put = [&p](const juce::String &id, float plain) {
      if (auto *param = p.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    };

    put(ovt::params::stretchId, 700.0f);
    put(ovt::params::trackId, 9.0f);

    // The character has to be in here for the same reason everything else is.
    // A preset that says nothing about it is asking for Pure rather than for
    // whatever the last patch was, and a preset that asks for one has to get
    // that one whatever was showing before it.
    put(ovt::params::characterId, (float)(int)ovt::Character::Opamp);

    put(ovt::params::lofiRateId, 5.0f); // 8 kHz
    put(ovt::params::lofiBitsId, 4.0f); // 8 bit
    put(ovt::params::phaseResetId, 0.0f);
    put(ovt::params::temperamentId, 3.0f); // quarter-comma meantone
    put(ovt::params::tuningRootId, 5.0f);  // on F
    put(ovt::params::echoOnId, 1.0f);
    put(ovt::params::reverbOnId, 1.0f);
    put(ovt::params::reverbDecayId, 17.0f);

    for (int i = 0; i < ovt::kNumHarmonics; ++i) {
      put(ovt::params::oscParamId(ovt::params::volumeSuffix, i), 0.9f);
      put(ovt::params::oscParamId(ovt::params::driftSuffix, i), 20.0f);
      put(ovt::params::oscParamId(ovt::params::phaseSuffix, i), 0.25f);
      put(ovt::params::oscParamId(ovt::params::panSuffix, i), -0.8f);
    }
  };

  const auto names = ovt::presets::names();
  int reproducible = 0;
  juce::StringArray drifted;

  for (int i = 0; i < names.size(); ++i) {
    ovt::presets::apply(p.apvts, i);
    const auto clean = snapshot();

    makeAMess();
    ovt::presets::apply(p.apvts, i);
    const auto afterMess = snapshot();

    bool same = true;
    for (size_t k = 0; k < clean.size(); ++k) {
      if (ovt::params::isSessionParam(clean[k].first))
        continue;

      if (std::abs(clean[k].second - afterMess[k].second) > 1.0e-4f) {
        same = false;

        if (!drifted.contains(clean[k].first))
          drifted.add(clean[k].first);
      }
    }

    if (same)
      ++reproducible;
  }

  if (!drifted.isEmpty())
    std::printf("  parameters left over from the previous patch: %s\n",
                drifted.joinIntoString(", ").toRawUTF8());

  // The other half of the rule: what a preset is not allowed to touch has to
  // still be there afterwards. Skipping these in the comparison above says
  // nothing about that either way.
  const auto set = [&p](const char *id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  const auto read = [&p](const char *id) {
    auto *v = p.apvts.getRawParameterValue(id);
    return v != nullptr ? (int)std::lround(v->load()) : -1;
  };

  set(ovt::params::temperamentId, (float)ovt::Temperament::Werckmeister3);
  set(ovt::params::tuningRootId, 5.0f);  // F
  set(ovt::params::referenceHzId, 0.0f); // A = 415
  set(ovt::params::polyphonyId, 6.0f);   // 16 voices

  // Away from its default, so that a preset quietly restoring the default
  // would read as a change rather than as agreement.
  set(ovt::params::oneVoicePerKeyId, 0.0f);

  for (int i = 0; i < names.size(); ++i)
    ovt::presets::apply(p.apvts, i);

  check(read(ovt::params::temperamentId) ==
                (int)ovt::Temperament::Werckmeister3 &&
            read(ovt::params::tuningRootId) == 5,
        "loading every preset in turn leaves the temperament alone");

  check(read(ovt::params::referenceHzId) == 0 &&
            read(ovt::params::polyphonyId) == 6,
        "and the reference pitch and polyphony with it");

  check(read(ovt::params::oneVoicePerKeyId) == 0,
        "and how many voices one key may take");

  set(ovt::params::oneVoicePerKeyId, 1.0f);

  check(reproducible == names.size(),
        "all " + std::to_string(names.size()) +
            " factory presets load the same from a dirty state (" +
            std::to_string(reproducible) + ")");
}

/// Knobs come out whatever size the row they land in happens to be, so a row
/// height typed a couple of pixels off is a knob a couple of pixels off, and
/// nothing complains. The sizes that differ should differ on purpose.
void testKnobSizes(OvertoniumProcessor &p) {
  section("Knob sizes");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Tall enough that nothing scrolls, because this is about the sizes a knob
  // is drawn at and a knob scrolled out of the band is not drawn at all. It
  // used to pass at 160, back when every row had to be on screen whatever the
  // height and the fader simply gave way.
  sizeEditor(*editor, 1348, 1010);

  // The diameter the look and feel will draw, which is what the eye sees,
  // rather than the bounds, which nobody sees.
  const auto dialOf = [](const juce::Slider &s) {
    const auto b = s.getBounds().reduced(1);
    return juce::jmin(b.getWidth(), b.getHeight());
  };

  std::function<void(juce::Component &, std::set<int> &)> collect =
      [&](juce::Component &c, std::set<int> &into) {
        for (auto *child : c.getChildren()) {
          if (auto *s = dynamic_cast<juce::Slider *>(child))
            if (s->getSliderStyle() == juce::Slider::RotaryVerticalDrag &&
                !s->getBounds().isEmpty())
              into.insert(dialOf(*s));

          collect(*child, into);
        }
      };

  std::set<int> channel, noise, bar;

  std::function<void(juce::Component &)> scan = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (dynamic_cast<ovt::ui::ChannelStrip *>(child))
        collect(*child, channel);
      else if (dynamic_cast<ovt::ui::NoiseStrip *>(child))
        collect(*child, noise);
      else if (dynamic_cast<ovt::ui::TopBar *>(child))
        collect(*child, bar);
      else
        scan(*child);
    }
  };
  scan(*editor);

  const auto list = [](const std::set<int> &v) {
    std::string out;
    for (auto d : v)
      out += (out.empty() ? "" : ", ") + std::to_string(d);
    return out;
  };

  std::printf("  channel strip %s   noise strip %s   top bar %s\n",
              list(channel).c_str(), list(noise).c_str(), list(bar).c_str());

  check(bar.size() == 1,
        "every knob in the top bar is one size (" + list(bar) + ")");

  // Two on a strip: the headline tuning knob, and everything below it.
  check(channel.size() == 2,
        "a channel strip uses two sizes, the headline row and the rest (" +
            list(channel) + ")");

  check(noise == channel,
        "and the noise strip uses exactly the same two (" + list(noise) + ")");
}

/// A knob whose bottom does nothing is a knob with less of itself.
///
/// That is what fitting a power curve through a midpoint across several
/// decades produces: the curve leaves its low end with a slope of exactly
/// zero, so the first stretch of travel moves the value by nothing at all. It
/// is easy to reintroduce by reaching for setSkewForCentre on the next wide
/// range somebody adds, so this checks every parameter rather than the handful
/// known to have had it.
void testNoDeadTravel(OvertoniumProcessor &p) {
  section("Knob travel");

  const auto valueAt = [](const juce::RangedAudioParameter &param, float t) {
    return (double)param.getNormalisableRange().convertFrom0to1(t);
  };

  // How the knob's resolution at the bottom compares with its resolution at
  // the top. A power curve through a distant midpoint gives exactly zero here,
  // whatever its exponent. Anything else gives a small number, never nought.
  const auto slopeRatio = [&](const juce::RangedAudioParameter &param) {
    const auto step = 0.005f;

    const auto bottom = std::abs(valueAt(param, step) - valueAt(param, 0.0f));
    const auto top =
        std::abs(valueAt(param, 1.0f) - valueAt(param, 1.0f - step));

    return top > 0.0 ? bottom / top : 1.0;
  };

  // For a range that starts above zero the intuitive measure is a ratio: how
  // far you have to turn before the value has grown by one percent of itself.
  const auto deadTravel = [&](const juce::RangedAudioParameter &param) {
    const auto base = valueAt(param, 0.0f);

    for (float t = 0.0f; t <= 1.0f; t += 0.0005f)
      if (valueAt(param, t) > base * 1.01)
        return 100.0f * t;

    return 100.0f;
  };

  double worstSlope = 1.0;
  float worstDead = 0.0f;
  juce::String slopeId, deadId;
  int checked = 0, byRatio = 0;

  for (auto *raw : p.getParameters()) {
    auto *param = dynamic_cast<juce::RangedAudioParameter *>(raw);

    // Choices and switches step, so travel means nothing for them.
    if (param == nullptr || param->getNumSteps() < 100)
      continue;

    ++checked;

    const auto ratio = slopeRatio(*param);
    if (ratio < worstSlope) {
      worstSlope = ratio;
      slopeId = param->paramID;
    }

    if (valueAt(*param, 0.0f) > 0.0) {
      ++byRatio;
      const auto dead = deadTravel(*param);

      if (dead > worstDead) {
        worstDead = dead;
        deadId = param->paramID;
      }
    }
  }

  std::printf("  %d continuous parameters. Thinnest slope at the bottom "
              "1/%.0f of the top, on %s\n",
              checked, 1.0 / std::max(1.0e-12, worstSlope),
              slopeId.toRawUTF8());

  std::printf("  %d of them start above zero. Worst dead travel %.1f%% on "
              "%s\n",
              byRatio, worstDead, deadId.toRawUTF8());

  check(checked > 600, "there are continuous parameters to check");

  // The defect being guarded against is a slope of nought, so the bound only
  // has to be above nought. A thousandth of a percent is far below anything
  // the curves here produce and far above what a power curve does.
  check(worstSlope > 1.0e-5,
        "no knob goes flat at the bottom (" + slopeId.toStdString() +
            " is at " + std::to_string(worstSlope) + " of its top slope)");

  // Where a ratio means something, the bottom of the knob should be usable
  // rather than merely non-flat.
  check(worstDead < 2.0f,
        "and a knob that starts above zero moves as soon as you turn it (" +
            deadId.toStdString() + " at " + std::to_string(worstDead) + "%)");

  // ---- every default survives being set --------------------------------
  //
  // A host sets a parameter to its default and reads it straight back. If the
  // value it gets is not the one it wrote, auval reports the parameter as one
  // that would not hold its default, which it did for all 66 decay and release
  // knobs while the logarithmic range converted in float.
  //
  // Exact equality on purpose. A tolerance here would pass the thing being
  // guarded against, since the discrepancy was a single bit.
  int inexact = 0;
  juce::String worstDefault;

  for (auto *param : p.getParameters()) {
    auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(param);
    if (ranged == nullptr)
      continue;

    const auto def = ranged->getDefaultValue();

    // juce::exactlyEqual rather than ==, which is the same comparison with the
    // float-equality warning suppressed, and says that the exactness is meant.
    if (!juce::exactlyEqual(ranged->convertTo0to1(ranged->convertFrom0to1(def)),
                            def)) {
      ++inexact;
      worstDefault = ranged->paramID;
    }
  }

  check(inexact == 0,
        "every parameter holds the default it is given (" +
            std::to_string(inexact) + " do not" +
            (inexact ? ", such as " + worstDefault.toStdString() : "") + ")");

  // And the shortest attack really is 0.2 ms, not a number that rounds to it.
  const auto attackId = ovt::params::oscParamId(ovt::params::attackSuffix, 0);

  if (auto *param = p.apvts.getParameter(attackId))
    check(std::abs(param->getNormalisableRange().start - 0.0002f) < 1.0e-7f,
          "the shortest attack is 0.2 ms");
}

/// The undo history, which holds what a person did and nothing else.
///
/// A control opens a gesture around whatever it writes and closes it
/// afterwards, which is what the processor listens for. Automation writes the
/// same parameters with no gesture around them, and that is the whole of the
/// difference. So these drive the parameters the way a control does rather
/// than the way a host does, and there is one below that does the opposite on
/// purpose.
void testUndo(OvertoniumProcessor &p) {
  section("Undo");

  auto &undo = p.undo();

  const auto tuneOf = [&p](int i) {
    return p.apvts
        .getRawParameterValue(
            ovt::params::oscParamId(ovt::params::tuneSuffix, i))
        ->load();
  };

  const auto tuneParam = [&p](int i) {
    return p.apvts.getParameter(
        ovt::params::oscParamId(ovt::params::tuneSuffix, i));
  };

  // What a knob does: one gesture around however many writes it makes.
  const auto userTurns = [&tuneParam](const std::vector<int> &channels,
                                      float to) {
    for (int i : channels)
      tuneParam(i)->beginChangeGesture();

    for (int i : channels)
      tuneParam(i)->setValueNotifyingHost(to);

    for (int i : channels)
      tuneParam(i)->endChangeGesture();
  };

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  undo.clearUndoHistory();

  const auto before = tuneOf(0);
  const auto to = before > 0.5f ? 0.1f : 0.9f;

  userTurns({0}, to);

  check(std::abs(tuneOf(0) - to) < 1.0e-4f,
        "the parameter moved to begin with");
  check(undo.canUndo(), "and the move is on the undo stack");

  check(undo.undo(), "undo reports that it did something");
  check(std::abs(tuneOf(0) - before) < 1.0e-4f,
        "and puts the parameter back (" + std::to_string(tuneOf(0)) +
            " against " + std::to_string(before) + ")");

  check(undo.redo(), "redo reports that it did something");
  check(std::abs(tuneOf(0) - to) < 1.0e-4f, "and moves it forward again");

  // The one that matters: a gesture that moves every channel has to come back
  // as a single step, not as 32.
  undo.clearUndoHistory();

  std::array<float, ovt::kNumHarmonics> baseline{};
  std::vector<int> all;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    baseline[(size_t)i] = tuneOf(i);
    all.push_back(i);
  }

  userTurns(all, baseline[0] > 0.5f ? 0.2f : 0.8f);

  int moved = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(tuneOf(i) - baseline[(size_t)i]) > 0.1f)
      ++moved;

  check(moved == ovt::kNumHarmonics, "a ganged move reaches all 32 channels");

  check(undo.undo(), "and one undo takes it back");

  int restored = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(tuneOf(i) - baseline[(size_t)i]) < 1.0e-4f)
      ++restored;

  check(restored == ovt::kNumHarmonics,
        "all 32 of them (" + std::to_string(restored) + ")");

  check(!undo.canUndo(), "which was the whole history, not the first of 32");

  undo.clearUndoHistory();
}

/// What a host does, which is the same writes with no gesture around them.
///
/// A lane being played back moves parameters continuously for as long as the
/// piece lasts. None of it is somebody editing, and a history filling up with
/// a fader that was automated three minutes ago is a history of nothing.
void testAutomationLeavesNoHistory(OvertoniumProcessor &p) {
  section("Automation is not an edit");

  auto &undo = p.undo();

  auto *param =
      p.apvts.getParameter(ovt::params::oscParamId(ovt::params::tuneSuffix, 3));

  check(param != nullptr, "the parameter exists");
  if (param == nullptr)
    return;

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  undo.clearUndoHistory();

  // A lane sweeping the control, which is what a host does: set the value and
  // say nothing about gestures.
  for (int step = 0; step <= 40; ++step)
    param->setValueNotifyingHost((float)step / 40.0f);

  check(std::abs(param->getValue() - 1.0f) < 1.0e-4f,
        "the automation moved the parameter");

  // Parameter values reach the value tree on a timer, and it is the tree that
  // used to record them. Flushed by hand here, so that this cannot pass by
  // asking before anything has been written rather than because nothing is
  // written.
  p.apvts.copyState();

  check(!undo.canUndo(),
        "and left nothing in the history (" +
            std::to_string(undo.getNumActionsInCurrentTransaction()) +
            " actions)");

  // Nor does a preset arriving over MIDI, which is the player playing rather
  // than the player editing.
  juce::AudioBuffer<float> buffer(2, 64);
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::programChange(1, presetIndex("Wurli")), 0);
  buffer.clear();
  p.processBlock(buffer, midi);
  p.applyPendingProgramChange();

  check(p.getCurrentProgram() == presetIndex("Wurli"),
        "the program change loaded its preset");

  p.apvts.copyState();

  check(!undo.canUndo(), "and left the history alone as well");

  // Where the same load asked for from the menu is a step, because the editor
  // asks for it through recordEdit and a clip does not.
  p.recordEdit("Load preset",
               [&p] { p.applyFactoryPreset(presetIndex("Cathedral")); });

  check(undo.canUndo(), "picking one from the menu is a step");

  check(undo.undo() &&
            p.apvts.getParameter(ovt::params::characterId) != nullptr,
        "which undoes");

  check(!undo.canUndo(), "and was one step, not one per parameter");

  undo.clearUndoHistory();
  ovt::presets::apply(p.apvts, presetIndex("Init"));
}

/// Where one gesture ends and the next begins.
void testUndoGrouping(OvertoniumProcessor &p) {
  section("One gesture, one undo step");

  auto &undo = p.undo();

  auto *knob =
      p.apvts.getParameter(ovt::params::oscParamId(ovt::params::tuneSuffix, 0));

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  undo.clearUndoHistory();

  const auto start = knob->getValue();
  const float notch = start > 0.5f ? -0.05f : 0.05f;

  // A wheel turned notch by notch. Ten writes, one gesture, because a wheel
  // opens one when it starts moving and closes it when it stops.
  knob->beginChangeGesture();

  for (int i = 1; i <= 10; ++i)
    knob->setValueNotifyingHost(start + (float)i * notch);

  knob->endChangeGesture();

  check(std::abs(knob->getValue() - start) > 0.4f,
        "the gesture moved the parameter to begin with");

  check(undo.undo(), "undo reports that it did something");

  check(std::abs(knob->getValue() - start) < 1.0e-4f,
        "and one undo is the whole gesture, not the last notch of it (" +
            std::to_string(knob->getValue()) + " against " +
            std::to_string(start) + ")");

  check(!undo.canUndo(), "which leaves nothing else of it on the stack");

  // And the next gesture is the next step.
  knob->beginChangeGesture();
  knob->setValueNotifyingHost(start + 2.0f * notch);
  knob->endChangeGesture();

  knob->beginChangeGesture();
  knob->setValueNotifyingHost(start + 4.0f * notch);
  knob->endChangeGesture();

  check(undo.undo() &&
            std::abs(knob->getValue() - (start + 2.0f * notch)) < 1.0e-4f,
        "letting go and starting again is a second step");

  check(undo.undo() && std::abs(knob->getValue() - start) < 1.0e-4f,
        "and the first one is still behind it");

  // A gesture that ends where it began is not an edit at all.
  knob->beginChangeGesture();
  knob->setValueNotifyingHost(start + 6.0f * notch);
  knob->setValueNotifyingHost(start);
  knob->endChangeGesture();

  check(!undo.canUndo(),
        "a gesture that came back to where it started is not a step");

  undo.clearUndoHistory();
}

void testBusLayouts(OvertoniumProcessor &p) {
  section("Bus layouts");

  const auto layout = [](juce::AudioChannelSet main) {
    juce::AudioProcessor::BusesLayout out;
    out.outputBuses.add(main);
    return out;
  };

  check(p.getBusCount(false) == 1, "one output bus, the mix (" +
                                       std::to_string(p.getBusCount(false)) +
                                       ")");
  check(p.getBusCount(true) == 0, "and no inputs");

  check(p.checkBusesLayoutSupported(layout(juce::AudioChannelSet::stereo())),
        "stereo out is supported");
  check(p.checkBusesLayoutSupported(layout(juce::AudioChannelSet::mono())),
        "so is mono, for a host that only has one channel to give");

  check(!p.checkBusesLayoutSupported(
            layout(juce::AudioChannelSet::create5point1())),
        "5.1 is not");

  check(p.acceptsMidi(), "accepts MIDI");
  check(!p.producesMidi(), "produces no MIDI");
}

/// A buffer narrower than the layout claims.
///
/// A host is supposed to hand over a channel for every enabled bus, and not
/// all of them do. pluginval once processed a two-channel buffer against a
/// wider layout and segfaulted on the Linux runner, because the write went by
/// what the layout said rather than by what the buffer held. Nothing here may
/// reach outside the buffer it was given, whatever the layout says.
void testUndersizedBuffer() {
  section("Undersized buffers");

  OvertoniumProcessor p;
  p.setRateAndBufferSizeDetails(48000.0, 512);
  p.prepareToPlay(48000.0, 512);

  for (int channels : {1, 2, 3}) {
    juce::AudioBuffer<float> buffer(channels, 512);
    buffer.clear();

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    for (int b = 0; b < 3; ++b) {
      p.processBlock(buffer, midi);
      midi.clear();
    }

    check(true, "a " + std::to_string(channels) +
                    " channel buffer against a stereo layout survives");
  }

  // And the ordinary case still works, so the guard did not simply silence it.
  juce::AudioBuffer<float> full(2, 512);
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.9f), 0);

  float loudest = 0.0f;
  for (int b = 0; b < 20; ++b) {
    full.clear();
    p.processBlock(full, midi);
    midi.clear();

    if (b >= 10)
      for (int ch = 0; ch < full.getNumChannels(); ++ch)
        loudest = std::max(loudest, full.getMagnitude(ch, 0, 512));
  }

  check(loudest > 1.0e-4f, "and a full-width buffer still gets audio");
}

/// A host that took the mono layout gets the mix folded down, not half of it.
///
/// The stereo path copies two channels straight across, so nothing about it
/// would notice the fold being wrong. Mono is the only output that runs a sum,
/// and it is the layout a host with one channel to give will ask for, so it
/// wants exercising rather than merely being declared supported.
void testMonoOutput() {
  section("Mono output");

  const auto renderPeak = [](juce::AudioChannelSet main) {
    OvertoniumProcessor p;

    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(main);

    if (!p.setBusesLayout(layout))
      return -1.0f;

    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buffer(main.size(), 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.9f), 0);

    float peak = 0.0f;

    for (int b = 0; b < 20; ++b) {
      buffer.clear();
      p.processBlock(buffer, midi);
      midi.clear();

      // The first blocks are the attack climbing, so the level is taken once
      // the note is up rather than from the ramp.
      if (b >= 10)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
          peak = std::max(peak, buffer.getMagnitude(ch, 0, 512));
    }

    return peak;
  };

  const auto mono = renderPeak(juce::AudioChannelSet::mono());
  const auto stereo = renderPeak(juce::AudioChannelSet::stereo());

  check(mono > 1.0e-4f, "a mono layout is given audio");
  check(stereo > 1.0e-4f, "and so is a stereo one");

  // The default patch is centred, so both channels carry the same thing and
  // averaging them gives back what either one held. A fold that dropped a
  // channel or halved the sum of two identical ones would land at half this,
  // which is well outside the tolerance.
  check(mono > stereo * 0.8f && mono < stereo * 1.25f,
        "and the fold keeps the level of a centred patch rather than halving "
        "it");
}

void testStateRoundTrip(OvertoniumProcessor &p) {
  section("State round trip");

  auto *master = p.apvts.getParameter(ovt::params::masterGainId);
  auto *tune7 =
      p.apvts.getParameter(ovt::params::oscParamId(ovt::params::tuneSuffix, 6));

  master->setValueNotifyingHost(master->convertTo0to1(-3.0f));
  tune7->setValueNotifyingHost(tune7->convertTo0to1(0.25f));

  juce::MemoryBlock saved;
  p.getStateInformation(saved);

  master->setValueNotifyingHost(master->convertTo0to1(-40.0f));
  tune7->setValueNotifyingHost(tune7->convertTo0to1(1.0f));

  p.setStateInformation(saved.getData(), (int)saved.getSize());

  check(
      std::abs(p.apvts.getRawParameterValue(ovt::params::masterGainId)->load() +
               3.0f) < 0.05f,
      "master survives a state round trip");
  check(std::abs(p.apvts
                     .getRawParameterValue(
                         ovt::params::oscParamId(ovt::params::tuneSuffix, 6))
                     ->load() -
                 0.25f) < 0.005f,
        "per-partial tuning survives a state round trip");

  // Garbage in must not crash or wipe the state.
  const char junk[] = "not a valid chunk";
  p.setStateInformation(junk, (int)sizeof(junk));
  check(
      std::abs(p.apvts.getRawParameterValue(ovt::params::masterGainId)->load() +
               3.0f) < 0.05f,
      "invalid state is ignored");
}

/// The factory presets as the host sees them.
///
/// Exposing programs is what puts the presets in Logic's own menu, and it also
/// hands the host a lever it pulls without being asked. A VST3 session restore
/// sets every parameter, and JUCE makes the program one of them, so the
/// dangerous case is not choosing a preset: it is reopening a session built on
/// one. The plugin has to come back with the edits, not with the preset.
void testPrograms(OvertoniumProcessor &p) {
  section("Programs");

  const auto names = ovt::presets::names();

  // Programs are the Audio Unit's alone. Everything else reports one, so the
  // VST3 wrapper does not publish a Program parameter that would rewrite every
  // other parameter when a host moves it. This harness is a console app, so
  // the wrapper type is undefined and the count is the collapsed one.
  check(p.wrapperType != juce::AudioProcessor::wrapperType_AudioUnit,
        "the test harness is not the Audio Unit");
  check(p.getNumPrograms() == 1,
        "formats other than the Audio Unit report a single program (" +
            std::to_string(p.getNumPrograms()) + ")");

  check(p.getProgramName(0) == names[0],
        "program 0 is named " + names[0].toStdString());
  check(p.getProgramName(names.size() - 1) == names[names.size() - 1],
        "names are readable past the program count, which is what a host "
        "asking about a cached index does");

  // Out of range on both sides, since hosts ask about cached indices.
  check(p.getProgramName(-1).isEmpty() &&
            p.getProgramName(names.size()).isEmpty(),
        "an index that does not exist has no name rather than a crash");

  const int chosen = names.indexOf("Wurli");
  check(chosen > 0, "the preset the rest of this test uses exists");

  // The plugin's own menu reaches every preset whatever the program count
  // says. Guarding this path with getNumPrograms would leave a VST3 able to
  // load nothing but the first one.
  p.applyFactoryPreset(chosen);
  check(p.getCurrentProgram() == chosen,
        "the plugin's own menu reaches a preset past the program count");

  p.applyFactoryPreset(names.size() - 1);
  check(p.getCurrentProgram() == names.size() - 1, "including the last one");

  p.applyFactoryPreset(names.size());
  check(p.getCurrentProgram() == names.size() - 1,
        "and an index past the end changes nothing");

  p.applyFactoryPreset(chosen);

  // A parameter the presets actually set, so the value read below is one the
  // preset put there rather than whatever happened to be lying about. Anything
  // a preset left alone would survive the reload and make every check pass
  // without proving a thing.
  auto *volume = p.apvts.getParameter(
      ovt::params::oscParamId(ovt::params::volumeSuffix, 0));

  const auto edited = [volume] { return volume->getValue(); };

  // What a player does next: load a preset, then change something.
  const float fromPreset = edited();
  const float editedTo = fromPreset > 0.5f ? 0.1f : 0.9f;
  volume->setValueNotifyingHost(editedTo);

  check(std::abs(edited() - fromPreset) > 0.05f,
        "the edit moved the parameter away from what the preset set");

  juce::MemoryBlock saved;
  p.getStateInformation(saved);

  // Somewhere else entirely, so a restore that quietly does nothing fails
  // rather than passes.
  p.applyFactoryPreset(names.indexOf("Cathedral"));

  p.setStateInformation(saved.getData(), (int)saved.getSize());

  check(p.getCurrentProgram() == chosen,
        "the current program survives a state round trip");
  check(std::abs(edited() - editedTo) < 0.005f,
        "an edit made on top of a preset survives a state round trip");

  // The host now does what a host does after restoring: tells the plugin which
  // program the session was on. It is the one already loaded, so this has to
  // be a no-op. Were it not, the edit above would be replaced by the pristine
  // preset and the session would open sounding wrong.
  p.setCurrentProgram(chosen);

  check(std::abs(edited() - editedTo) < 0.005f,
        "the host re-selecting the current program does not discard the edit");

  // The plugin's own menu is the other way round. Picking the preset you are
  // already on is how you get back to it.
  p.applyFactoryPreset(chosen);

  check(std::abs(edited() - fromPreset) < 0.005f,
        "choosing the same preset in the plugin's menu does reload it");

  // ---- what a renumbering does to a session already saved -----------------
  //
  // Factory presets sort alphabetically, so adding one moves every preset
  // after it and an index written last year now names a different sound. The
  // saved name is the one thing that does not move, so it decides, and the
  // index is only the fallback. Simulated here by writing a wrong index into
  // a state that still carries the right name, which is exactly the state an
  // older session becomes.
  const int wasOn = names.indexOf("Wurli");
  p.applyFactoryPreset(wasOn);

  juce::MemoryBlock session;
  p.getStateInformation(session);

  if (auto xml = juce::AudioProcessor::getXmlFromBinary(
          session.getData(), (int)session.getSize())) {
    check(xml->getStringAttribute("overtoniumPresetName") == names[wasOn],
          "the saved state carries the preset's name as well as its index");

    // Somewhere else in the list, which is what a shifted index looks like.
    xml->setAttribute("overtoniumProgram", names.indexOf("Cathedral"));

    juce::MemoryBlock shifted;
    juce::AudioProcessor::copyXmlToBinary(*xml, shifted);

    p.applyFactoryPreset(names.indexOf("Big Saw"));
    p.setStateInformation(shifted.getData(), (int)shifted.getSize());

    check(p.getCurrentProgram() == wasOn,
          "a session whose stored index has since moved comes back on the "
          "preset it was actually saved on");
  } else {
    check(false, "the session state could be reread");
  }

  // A name that is not a factory preset is one of the user's own, or a patch
  // edited past recognising, and neither has a program to be. The index is all
  // there is to go on there.
  if (auto xml = juce::AudioProcessor::getXmlFromBinary(
          session.getData(), (int)session.getSize())) {
    const int elsewhere = names.indexOf("Cathedral");
    xml->setAttribute("overtoniumPresetName", "Something Of My Own");
    xml->setAttribute("overtoniumProgram", elsewhere);

    juce::MemoryBlock mine;
    juce::AudioProcessor::copyXmlToBinary(*xml, mine);
    p.setStateInformation(mine.getData(), (int)mine.getSize());

    check(p.getCurrentProgram() == elsewhere,
          "a name that is nobody's factory preset leaves the index deciding");
  } else {
    check(false, "the user-preset state could be reread");
  }

  // A state from before either property existed has neither to go on.
  const int before = p.getCurrentProgram();
  p.applyFactoryPreset(names.indexOf("Big Saw"));
  juce::MemoryBlock legacy;
  p.getStateInformation(legacy);

  const auto size = (int)legacy.getSize();

  auto xml = juce::AudioProcessor::getXmlFromBinary(legacy.getData(), size);

  if (xml != nullptr) {
    xml->removeAttribute("overtoniumProgram");
    xml->removeAttribute("overtoniumPresetName");
    juce::MemoryBlock stripped;
    juce::AudioProcessor::copyXmlToBinary(*xml, stripped);

    p.setCurrentProgram(before == 0 ? 1 : 0);
    p.setStateInformation(stripped.getData(), (int)stripped.getSize());

    check(p.getCurrentProgram() == 0,
          "a state saved without either lands on the first one");
  } else {
    check(false, "the legacy state could be reread");
  }
}

/// Presets chosen over MIDI.
///
/// The audio thread only writes the number down, and the timer that reads it
/// needs a message loop this harness does not run, so the message-thread half
/// is called by hand. That is the same arrangement the docs renderer uses for
/// the editor's timer.
void testProgramChangeMidi(OvertoniumProcessor &p) {
  section("Program change over MIDI");

  const auto names = ovt::presets::names();

  const auto setParam = [&p](const juce::String &id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  const auto play = [&p](juce::MidiBuffer midi) {
    juce::AudioBuffer<float> buffer(2, 64);
    buffer.clear();
    p.processBlock(buffer, midi);
  };

  const auto programChange = [](int channel, int program) {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::programChange(channel, program), 0);
    return m;
  };

  p.prepareToPlay(48000.0, 512);
  setParam(ovt::params::mpeId, 0.0f);
  play({});

  const int wurli = presetIndex("Wurli");
  const int cathedral = presetIndex("Cathedral");

  p.applyFactoryPreset(cathedral);
  play(programChange(1, wurli));

  check(p.getCurrentProgram() == cathedral,
        "the block a program change arrives in does not load the preset");

  p.applyPendingProgramChange();

  check(p.getCurrentProgram() == wurli, "the message thread loads it");
  check(p.presetName() == names[wurli],
        "and the name the window shows follows it");

  p.applyPendingProgramChange();
  check(p.getCurrentProgram() == wurli,
        "a tick with nothing waiting loads nothing");

  // A program change can name any of 128 programs, which is more than there
  // are presets, so most of what it can say names nothing.
  play(programChange(1, 127));
  p.applyPendingProgramChange();

  check(p.getCurrentProgram() == wurli,
        "a program past the last preset leaves what is loaded alone");

  // Two in one block is a clip whose first event was never meant to be heard.
  p.applyFactoryPreset(wurli);
  juce::MidiBuffer several;
  several.addEvent(juce::MidiMessage::programChange(1, cathedral), 0);
  several.addEvent(juce::MidiMessage::programChange(1, names.indexOf("Lo-fi")),
                   32);
  play(several);
  p.applyPendingProgramChange();

  check(p.getCurrentProgram() == names.indexOf("Lo-fi"),
        "several in one block leave the last one loaded");

  // What a program change does on an instrument with a panel: the preset it
  // names is loaded whether or not it is the one already showing.
  auto *volume = p.apvts.getParameter(
      ovt::params::oscParamId(ovt::params::volumeSuffix, 0));

  const float fromPreset = volume->getValue();
  const float editedTo = fromPreset > 0.5f ? 0.1f : 0.9f;
  volume->setValueNotifyingHost(editedTo);

  check(std::abs(volume->getValue() - fromPreset) > 0.05f,
        "the edit moved the parameter away from what the preset set");

  play(programChange(1, names.indexOf("Lo-fi")));
  p.applyPendingProgramChange();

  check(std::abs(volume->getValue() - fromPreset) < 0.005f,
        "the preset already loaded is loaded again, discarding the edit");

  // An MPE controller sends a program change on a member channel, where the
  // parser would otherwise swallow it.
  setParam(ovt::params::mpeId, 1.0f);
  play({});

  p.applyFactoryPreset(cathedral);
  play(programChange(3, wurli));
  p.applyPendingProgramChange();

  check(p.getCurrentProgram() == wurli,
        "with MPE on a program change on a member channel still arrives");

  setParam(ovt::params::mpeId, 0.0f);
  play({});
  p.applyFactoryPreset(presetIndex("Init"));
}

/// The standalone's window, which the editor dresses and then undresses.
void testStandaloneWindow(OvertoniumProcessor &p) {
  section("The standalone window");

  // JUCE's own title bar, which is what the standalone gets: a grey-green bar
  // with a red cross and a yellow dash on it unless something says otherwise.
  juce::DocumentWindow window("Overtonium", juce::Colour(0xff323e44),
                              juce::DocumentWindow::minimiseButton |
                                  juce::DocumentWindow::closeButton);

  window.setSize(420, 120);

  auto *const stock = &window.getLookAndFeel();

  check(stock == &juce::LookAndFeel::getDefaultLookAndFeel(),
        "a window starts on whatever look and feel the application has");

  {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

    check(editor != nullptr, "the editor opens");
    if (editor == nullptr)
      return;

    // The width it opens at, which is the one that shows all 32 strips.
    const auto wanted = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 +
                        ovt::kNumHarmonics * ovt::ui::kStripWidth;

    check(editor->getWidth() == wanted,
          "the editor opens wide enough for every strip (" +
              std::to_string(editor->getWidth()) + " px)");

    // Handed to a window the way the standalone hands it over, which is the
    // step that used to cost it its size: a document window is 128 px square
    // until it is told otherwise, and anything that lays the content out
    // during the handover squashes the editor into that and leaves it there.
    juce::DocumentWindow host("Overtonium", juce::Colour(0xff323e44),
                              juce::DocumentWindow::closeButton);

    host.setContentNonOwned(editor, true);

    check(editor->getWidth() == wanted,
          "and keeps it when a window takes it as content (" +
              std::to_string(editor->getWidth()) + " px)");

    editor->dressWindow(host);

    check(editor->getWidth() == wanted,
          "and again once that window has been dressed (" +
              std::to_string(editor->getWidth()) + " px)");

    host.clearContentComponent();

    sizeEditor(*editor, 1340);
    editor->dressWindow(window);

    check(&window.getLookAndFeel() != stock,
          "and takes the editor's own once it has been dressed");

    check(window.findColour(juce::ResizableWindow::backgroundColourId) ==
              ovt::ui::colours::background,
          "in the colour the panel stands on");
  }

  // The window outlives the editor by however long the application takes to
  // close, and the look and feel it was given belongs to the editor. What
  // keeps that from being a dangling pointer is that a component holds its
  // look and feel by weak reference and falls back to the application's own.
  // Pinned here because it is the kind of thing only ever noticed by
  // crashing, and because it is what allows the editor to lend out something
  // it owns.
  check(&window.getLookAndFeel() == stock,
        "and falls back to the application's own once the editor has gone");
}

/// The oscillator character: one choice for all 32 partials.
void testCharacterControl(OvertoniumProcessor &p) {
  section("Oscillator character");

  auto *param = p.apvts.getParameter(ovt::params::characterId);

  check(param != nullptr, "the character parameter exists");
  if (param == nullptr)
    return;

  const auto set = [param](ovt::Character c) {
    param->setValueNotifyingHost(param->convertTo0to1((float)(int)c));
  };

  const auto current = [param] {
    return (ovt::Character)juce::roundToInt(
        param->convertFrom0to1(param->getValue()));
  };

  check(juce::roundToInt(param->convertFrom0to1(param->getDefaultValue())) ==
            (int)ovt::Character::Pure,
        "and starts on Pure, so nothing that existed before this sounds "
        "different");

  check(!ovt::params::isSessionParam(ovt::params::characterId),
        "it is part of the patch rather than part of the setup, since it is "
        "what the instrument sounds like");

  // Every name the panel can show has to come from the same list the engine
  // switches on, or a menu entry could select a character that is not there.
  check(ovt::params::characterChoices().size() ==
            (int)ovt::Character::NumCharacters,
        "the menu offers every character and no more");

  for (int i = 0; i < (int)ovt::Character::NumCharacters; ++i)
    check(ovt::params::characterChoices()[i] ==
              juce::String(ovt::characterName((ovt::Character)i)),
          "entry " + std::to_string(i) + " is named by the engine");

  // ---- a preset carries it -------------------------------------------------
  set(ovt::Character::Diode);
  p.apvts.copyState();

  juce::String error;
  check(ovt::presets::save(p.apvts, "Character Test", error),
        "a preset saves with a character set" + error.toStdString());

  set(ovt::Character::Pure);
  check(current() == ovt::Character::Pure, "and the panel moves off it");

  const auto file =
      ovt::presets::userDirectory().getChildFile("Character Test.ovtpreset");

  check(ovt::presets::load(p.apvts, file, error),
        "the preset loads back" + error.toStdString());

  check(current() == ovt::Character::Diode, "and brings its character with it");

  file.deleteFile();

  // ---- a factory preset decides it too -------------------------------------
  //
  // Init, which says nothing about the character and never will, since it is
  // the patch that clears everything. A patch saying nothing is a patch that
  // wants the plain oscillator, the same way one that says nothing about
  // STRETCH wants none, and that comes through the neutral base rather than
  // from any preset naming it.
  p.applyFactoryPreset(presetIndex("Init"));

  check(current() == ovt::Character::Pure,
        "a factory preset that says nothing puts it back to Pure");

  // The pair the site plays against each other to show what TUNE does. They
  // differ in that one control and in nothing else, so a character on either
  // of them would be demonstrating something other than tuning.
  for (auto *name : {"Just Saw", "Equal Saw"}) {
    set(ovt::Character::Valve);
    p.applyFactoryPreset(presetIndex(name));

    check(current() == ovt::Character::Pure,
          juce::String(name).toStdString() + " stays on Pure on purpose");
  }

  // And that the factory set asks for characters at all. Which preset gets
  // which was decided by ear and is not something to pin down here, but a
  // preset load that quietly dropped the character would otherwise show up
  // only as most of the factory set sounding wrong.
  int asking = 0;

  for (int i = 0; i < ovt::presets::names().size(); ++i) {
    p.applyFactoryPreset(i);
    asking += current() != ovt::Character::Pure ? 1 : 0;
  }

  check(asking > ovt::presets::names().size() / 2,
        "most of the factory presets ask for one (" + std::to_string(asking) +
            " of " + std::to_string(ovt::presets::names().size()) + ")");

  // ---- and the panel says which it is --------------------------------------
  set(ovt::Character::Rail);

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");

  if (editor != nullptr) {
    sizeEditor(*editor, 1340);

    std::function<ovt::ui::TopBar *(juce::Component &)> walk =
        [&walk](juce::Component &c) -> ovt::ui::TopBar * {
      if (auto *bar = dynamic_cast<ovt::ui::TopBar *>(&c))
        return bar;

      for (auto *child : c.getChildren())
        if (auto *found = walk(*child))
          return found;

      return nullptr;
    };

    auto *bar = walk(*editor);
    check(bar != nullptr, "and has a bar");

    if (bar != nullptr) {
      bar->updatePanelReadouts(48000.0);

      // In capitals on the button, where the menu it came from spells it as a
      // word. The bar is a row of switches and they are all shouted.
      check(bar->getCharacterName() == "RAIL",
            "which reads back what is set (" +
                bar->getCharacterName().toStdString() + ")");

      // The button is written from the parameter rather than when someone
      // picks from its menu, so a preset changing it underneath has to show.
      set(ovt::Character::Bulb);
      bar->updatePanelReadouts(48000.0);

      check(bar->getCharacterName() == "BULB",
            "and follows a change it did not make");

      // Lit in the character's own colour, and not lit at all on the one that
      // adds nothing. The colours are the mixer's own band, running down from
      // the yellow at the top of it to the red the fifth stands in, which is
      // the colour of channel 3.
      check(bar->isCharacterLit(), "a character lights the button");

      check(bar->getCharacterColour() ==
                ovt::ui::characterColour(ovt::Character::Bulb),
            "in its own colour");

      set(ovt::Character::Pure);
      bar->updatePanelReadouts(48000.0);

      check(!bar->isCharacterLit(), "and Pure does not light it");
    }
  }

  check(ovt::ui::characterColour(ovt::Character::Bulb) ==
            ovt::ui::intervalColour(11),
        "the gentlest character stands at the yellow end of the mixer's band");

  check(ovt::ui::characterColour(ovt::Character::Opamp) ==
            ovt::ui::intervalColour(ovt::harmonic(2).pitchClass),
        "and the hardest on the red of channel 3");

  set(ovt::Character::Pure);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// Folding a section away.
///
/// Everything in the mixer lays itself out from one RowBounds, so this is
/// mostly a test of layoutRows: get that right and the gutter, the strips, the
/// lamps and the hover all follow. What it has to prove is that a folded
/// section takes no height, that its heading stays to be clicked, and that the
/// height the window loses is exactly the height the rows gave up.
void testCollapsibleSections() {
  section("Collapsible sections");

  using namespace ovt::ui;

  const juce::Rectangle<int> area(0, 0, kStripWidth, preferredStripHeight());
  const auto open = layoutRows(area);

  const auto envMask = sectionBit(Section::Envelope);
  const auto folded = layoutRows(area, envMask);

  check(folded[(size_t)Row::EnvHeading].getHeight() ==
            open[(size_t)Row::EnvHeading].getHeight(),
        "the heading keeps its height, so there is something left to click");

  for (auto r :
       {Row::Strike, Row::Delay, Row::Attack, Row::Decay, Row::Sustain})
    check(folded[(size_t)r].getHeight() == 0,
          std::string("the folded ") + rowLabel(r) + " row takes no height");

  check(open[(size_t)Row::Delay].getHeight() > 0,
        "and takes height again when the section is open");

  // Only that section. A fold that quietly took a neighbour with it would be
  // hard to see and worse to use.
  for (auto r : {Row::PmRate, Row::Swell, Row::AmRate, Row::Velocity})
    check(folded[(size_t)r].getHeight() == open[(size_t)r].getHeight(),
          std::string("folding the envelope leaves ") + rowLabel(r) + " alone");

  check(collapsedRowsHeight(0) == 0, "nothing folded is no height");
  check(collapsedRowsHeight(envMask) ==
            open[(size_t)Row::Strike].getHeight() +
                open[(size_t)Row::Delay].getHeight() +
                open[(size_t)Row::Attack].getHeight() +
                open[(size_t)Row::Decay].getHeight() +
                open[(size_t)Row::Sustain].getHeight(),
        "the height given up is the sum of the rows that went");

  check(preferredStripHeight(envMask) ==
            preferredStripHeight() - collapsedRowsHeight(envMask),
        "the strip wants exactly that much less room");
  // And the shortest it will go is no longer anything to do with folding.
  // It used to be the height of every unfolded row at once, so folding was the
  // only way to get the window down. Now what does not fit scrolls, so the
  // floor is the pinned header and the band's own minimum whatever is folded.
  check(minimumStripHeight(envMask) == minimumStripHeight(),
        "and the shortest window no longer depends on what is folded");

  // The rows below close up rather than leaving a hole, and the fader keeps
  // the height it had rather than stretching into it.
  check(folded[(size_t)Row::KeyOffHeading].getY() <
            open[(size_t)Row::KeyOffHeading].getY(),
        "what was below the folded section moves up");

  const auto shorter = layoutRows(
      area.withHeight(area.getHeight() - collapsedRowsHeight(envMask)),
      envMask);
  check(shorter[(size_t)Row::Fader].getHeight() ==
            open[(size_t)Row::Fader].getHeight(),
        "in a window shortened to match, the fader is the size it was");

  // Every section folds, and all of them together still leaves a usable strip.
  SectionMask all = 0;
  for (int i = 0; i < kNumSections; ++i) {
    const auto one = sectionBit((Section)i);
    check(collapsedRowsHeight(one) > 0,
          std::string("section ") + std::to_string(i) + " has rows to fold");
    all |= one;
  }

  const auto allFolded = layoutRows(area, all);
  for (auto r : {Row::TuneKnob, Row::Phase, Row::MuteSolo, Row::Fader})
    check(allFolded[(size_t)r].getHeight() > 0,
          std::string(rowLabel(r) == nullptr ? "the fader" : rowLabel(r)) +
              " survives every section being folded");

  // Clicking. The heading is the target and nothing else is.
  check(headingSectionAt(open, open[(size_t)Row::EnvHeading].getCentre()) ==
            Section::Envelope,
        "a click on the envelope heading names the envelope");
  check(headingSectionAt(open, open[(size_t)Row::Attack].getCentre()) ==
            Section::NumSections,
        "a click on a knob row names no section");
  check(headingSectionAt(folded, folded[(size_t)Row::EnvHeading].getCentre()) ==
            Section::Envelope,
        "the heading of a folded section is still the way back");
}

/// Deciding whether a release is newer, and reading the feed that says so.
///
/// The fetch itself needs a network and is not exercised here. These two are
/// the parts that can be wrong quietly: a comparison that comes out backwards
/// nags every user forever, and one that is too eager treats a malformed feed
/// as an upgrade.
void testUpdateCheck() {
  section("Update check");

  using ovt::isNewerVersion;

  check(isNewerVersion("1.1.0", "1.0.0"), "a higher minor is newer");
  check(isNewerVersion("2.0.0", "1.9.9"), "a higher major is newer");
  check(isNewerVersion("1.0.1", "1.0.0"), "a higher patch is newer");
  check(!isNewerVersion("1.0.0", "1.0.0"), "the same version is not newer");
  check(!isNewerVersion("0.9.9", "1.0.0"), "an older version is not newer");
  check(!isNewerVersion("1.0.0", "1.0.1"), "and not in the other direction");

  // The one a string comparison gets wrong.
  check(isNewerVersion("1.0.10", "1.0.9"),
        "10 is newer than 9, which sorting as text would deny");
  check(!isNewerVersion("1.0.9", "1.0.10"), "and the reverse");

  check(isNewerVersion("v1.1.0", "1.0.0"), "a leading v is ignored");
  check(isNewerVersion("1.1.0", "v1.0.0"), "on either side");

  check(isNewerVersion("1.1", "1.0.9"), "a missing component counts as zero");
  check(!isNewerVersion("1.1", "1.1.0"), "so 1.1 and 1.1.0 are one release");

  // Nothing that fails to parse may read as an upgrade, or a web server having
  // a bad day turns into a badge nobody can clear.
  for (const char *junk : {"", "  ", "latest", "1.0.0-beta", "v", "1..0",
                           "<!doctype html>", "1.0.0a"})
    check(!isNewerVersion(junk, "1.0.0"),
          std::string("\"") + junk + "\" is not a newer version");

  check(!isNewerVersion("2.0.0", "garbage"),
        "an unreadable current version blocks the comparison too");

  // The feed.
  using ovt::parseReleaseJson;

  const auto good = parseReleaseJson(
      R"({"version":"1.1.0","url":"https://example.invalid/v1.1.0"})");
  check(good.has_value(), "a well-formed feed parses");
  check(good && good->version == "1.1.0", "and carries the version");
  check(good && good->url == "https://example.invalid/v1.1.0",
        "and the page to send someone to");

  const auto noUrl = parseReleaseJson(R"({"version":"1.1.0"})");
  check(noUrl.has_value() && noUrl->url.isNotEmpty(),
        "a feed with no url still works, falling back to the releases page");

  for (const char *bad : {"", "{}", "[]", "not json", R"({"version":""})",
                          R"({"version":null})", "null"})
    check(!parseReleaseJson(bad).has_value(),
          std::string("a feed of \"") + bad + "\" yields nothing");
}

/// Opening editors must not turn the update check on, or ask anything.
///
/// This is the shape that crashed pluginval: a host walking its plugin folder
/// makes instances and opens editors with nobody there to answer, and it does
/// it dozens of times. The preference is deliberately not asserted to any
/// particular value, since it is machine-wide and whoever is running the tests
/// may legitimately have it either way. What must hold is that none of this
/// changes it.
void testUpdateCheckIsQuiet(OvertoniumProcessor &p) {
  section("Update check stays quiet");

  const bool before = ovt::updateCheckAllowed();

  for (int instance = 0; instance < 3; ++instance) {
    OvertoniumProcessor fresh;

    for (int open = 0; open < 3; ++open) {
      std::unique_ptr<juce::AudioProcessorEditor> ed(fresh.createEditor());
      sizeEditor(*ed, 1340);
    }
  }

  check(ovt::updateCheckAllowed() == before,
        "nine editors across three instances leave the preference alone");

  // And the same for the one the rest of the suite is using, since an editor
  // on an instance that has been played is a different path through the
  // constructor.
  {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    sizeEditor(*ed, 1340);
  }

  check(ovt::updateCheckAllowed() == before,
        "and so does one on an instance that has been used");
}

/// The preference survives the object that wrote it.
///
/// It used to be held in one shared PropertiesFile that lived in a static,
/// which made this unfalsifiable: a write and a read went to the same object
/// in memory and agreed with each other whatever the file on disk said, or
/// whether there was one. That static was a deadlock on unload, so the file is
/// now built where it is used and destroyed there, and every read is a fresh
/// object reading the disk. Which means the two can now disagree, if the path
/// is ever computed differently between one call and the next, and that is
/// what this is here to catch.
///
/// The preference is put back afterwards. It is machine-wide and belongs to
/// whoever is running the tests, not to the tests.
void testThePreferenceOutlivesItsWriter() {
  section("The preference outlives its writer");

  const bool before = ovt::updateCheckAllowed();

  ovt::setUpdateCheckAllowed(true);
  check(ovt::updateCheckAllowed(),
        "a preference written by one file is read back by the next");

  ovt::setUpdateCheckAllowed(false);
  check(!ovt::updateCheckAllowed(), "and so is the other answer");

  ovt::setUpdateCheckAllowed(before);
  check(ovt::updateCheckAllowed() == before,
        "and the machine is left as it was found");
}

/// The fetch belongs to the instance, not to the window.
///
/// Which is what lets a window close without waiting for a socket. If the
/// editor owned the fetch, closing one would join a network thread on the
/// message thread, and a server that accepts and then goes quiet is allowed
/// five seconds of that: long enough for the join to expire, for JUCE to kill
/// the thread where it stands, and for whatever lock it held to take the
/// message thread with it.
///
/// Nothing here reaches the network, so what is pinned is the ownership rather
/// than the timing. A check that stays put across nine windows is one that
/// cannot be joined by any of them.
void testUpdateCheckOutlivesEditors(OvertoniumProcessor &p) {
  section("Update check outlives its editors");

  const auto *first = &p.updates();

  for (int open = 0; open < 9; ++open) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    sizeEditor(*ed, 1340);
  }

  check(&p.updates() == first,
        "nine windows come and go and the fetch is the same one throughout");

  // Both of the things a closing editor does, on a check that never started.
  // An editor closed before its fetch begins is the ordinary case, not an edge
  // one, since the offer to switch the check on is declined by default.
  p.updates().cancel();
  p.updates().cancel();
  p.updates().setListener(nullptr);

  check(!p.updates().newerRelease().has_value(),
        "cancelling an idle check is safe and finds nothing");
}

/// Every control says what it is.
///
/// JUCE reads a control's accessible name from Component::getTitle, gives it a
/// role and reads its value out on its own. The name is the one part it cannot
/// work out, and its own documentation says an element with neither a name nor
/// a description may be skipped by accessibility clients entirely. On a mixer
/// of six hundred sliders that is the difference between an instrument someone
/// can use without seeing it and one they cannot.
///
/// Buttons are excluded: JUCE falls back to their text, which is why the mute
/// and solo buttons are named explicitly here anyway. Their text is M and S.
void testEveryControlIsNamed(OvertoniumProcessor &p) {
  section("Accessible names");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  std::vector<std::string> nameless;
  std::set<std::string> names;
  int sliders = 0;

  std::function<void(juce::Component &)> walk = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *s = dynamic_cast<juce::Slider *>(child)) {
        ++sliders;

        if (s->getTitle().isEmpty())
          nameless.push_back(s->getName().toStdString());
        else
          names.insert(s->getTitle().toStdString());
      }

      walk(*child);
    }
  };
  walk(*editor);

  std::printf("  %d sliders, %d distinct names\n", sliders, (int)names.size());

  check(sliders > 600, "the walk reached the whole mixer");
  check(nameless.empty(), "every slider has a name (" +
                              std::to_string(nameless.size()) + " without)");

  // A name that repeats is a name that does not identify anything. Two
  // harmonics both called "attack" would leave a listener no better off.
  check((int)names.size() == sliders,
        "and no two sliders share one (" + std::to_string(sliders) +
            " sliders, " + std::to_string(names.size()) + " names)");

  // Spot checks, so the shape of a name is fixed rather than merely non-empty.
  check(names.count("Harmonic 1 attack") == 1, "a partial names its harmonic");
  check(names.count("Noise attack") == 1, "the noise channel names itself");
  check(names.count("Echo mix") == 1 && names.count("Reverb mix") == 1,
        "and the two mix knobs are told apart by their group");

  // ---- the controls that are not sliders -----------------------------------
  //
  // A button is named by the word on it, which for most of the bar is the
  // control's own name and needs nothing further. Two of them carry a value
  // instead, and a value with nothing saying what it is of is no name at all.
  // The same goes for the converter readouts, whose digits are drawn rather
  // than written and cannot be read any other way.
  std::map<std::string, std::string> titled;

  std::function<void(juce::Component &)> walkAll = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (child->getTitle().isNotEmpty())
        titled[child->getTitle().toStdString()] =
            child->getName().toStdString();

      walkAll(*child);
    }
  };
  walkAll(*editor);

  const auto named = [&titled](const std::string &prefix) {
    for (const auto &entry : titled)
      if (entry.first.rfind(prefix, 0) == 0)
        return entry.first;

    return std::string{};
  };

  // The shape rather than the reading, since what is loaded by the time this
  // runs depends on whatever the test before it was doing.
  const auto saysBoth = [&named](const std::string &label) {
    const auto title = named(label);
    return title.size() > label.size();
  };

  check(saysBoth("Preset: "),
        "the preset button says what it is and what is loaded (" +
            named("Preset: ") + ")");

  check(saysBoth("Character: "),
        "so does the character button (" + named("Character: ") + ")");

  check(saysBoth("Sample rate: ") && saysBoth("Bit depth: "),
        "and both converter readouts (" + named("Sample rate: ") + ", " +
            named("Bit depth: ") + ")");
}

/// MPE slide, both places it can go.
///
/// Driven through the real MIDI path rather than by poking the voice, because
/// the wiring is most of what could be wrong: a missing listener override, a
/// note that never gets its channel's timbre, a routing switch read from the
/// wrong parameter.
void testMpeSlide() {
  section("MPE slide");

  // Its own processor, not the one the rest of the suite shares. An earlier
  // test leaves notes decaying and a master gain of its own, and this
  // measures loudness, so borrowing that state measures the leftovers instead.
  OvertoniumProcessor p;

  const auto setParam = [&p](const juce::String &id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  setParam(ovt::params::echoOnId, 0.0f);
  setParam(ovt::params::reverbOnId, 0.0f);
  setParam(ovt::params::wobbleId, 0.0f);
  setParam(ovt::params::mpeId, 1.0f);

  // Half the series at full level, so thinning the top is a measurable change
  // in total energy, and tracking on, since brightness is what slide moves.
  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    setParam(ovt::params::oscParamId(ovt::params::volumeSuffix, i),
             i < 16 ? 1.0f : 0.0f);
    setParam(ovt::params::oscParamId(ovt::params::sustainSuffix, i), 1.0f);
    setParam(ovt::params::oscParamId(ovt::params::velSuffix, i), 0.0f);
    setParam(ovt::params::oscParamId(ovt::params::atSuffix, i), 0.0f);
  }
  setParam(ovt::params::trackId, 6.0f);

  p.setRateAndBufferSizeDetails(48000.0, 512);
  p.prepareToPlay(48000.0, 512);

  // A gesture rather than a position, because that is what slide now is: the
  // first value a note is given is its rest, and only movement away from it
  // reaches the sound. So the note is sent where the controller's axis sits
  // when it engages, and then where the finger takes it.
  const auto rms = [&](int restCC, int cc74) {
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::allNotesOff(2), 0);
    juce::AudioBuffer<float> flush(2, 512);

    for (int b = 0; b < 6; ++b) {
      flush.clear();
      p.processBlock(flush, off);
      off.clear();
    }

    p.reset();

    juce::AudioBuffer<float> buffer(2, 512);
    double sum = 0.0;
    int n = 0;

    for (int b = 0; b < 40; ++b) {
      // Built per block rather than cleared as it goes: the note and the rest
      // arrive together, and the move lands a block later, which is what a
      // finger does.
      juce::MidiBuffer midi;

      if (b == 0) {
        midi.addEvent(juce::MidiMessage::noteOn(2, 72, 0.9f), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(2, 74, restCC), 1);
      } else if (b == 1) {
        midi.addEvent(juce::MidiMessage::controllerEvent(2, 74, cc74), 0);
      }

      buffer.clear();
      p.processBlock(buffer, midi);

      // The first blocks are the attack, which says nothing about the
      // steady-state spectrum.
      if (b < 20)
        continue;

      for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const auto v = (double)buffer.getSample(0, i);
        sum += v * v;
        ++n;
      }
    }

    return n > 0 ? std::sqrt(sum / (double)n) : 0.0;
  };

  setParam(ovt::params::slideDestId, (float)ovt::SlideDestination::Brightness);

  // A controller that rests near the centre, which is what a Seaboard does.
  //
  // 63 rather than 64 on purpose. JUCE hands a new note the centre of the
  // timbre axis and only calls back when a value differs from what it holds, so
  // a rest of exactly 64 is indistinguishable from the note's own starting
  // value and passes without a word. No real controller sits on that one code,
  // and one that did would still reach the whole of the slide, since the travel
  // is measured from wherever the rest ends up.
  const auto back = rms(63, 0);
  const auto centre = rms(63, 63);
  const auto forward = rms(63, 127);

  check(back > 1.0e-5 && forward > 1.0e-5, "the note sounds at both ends");

  // Three points rather than two, so the direction and the middle are both
  // pinned. Forward takes tracking away and brightens, back adds more and
  // darkens, and the rest position leaves the patch alone.
  check(forward > centre * 1.1 && centre > back * 1.1,
        "brightness rises across the travel (" + std::to_string(back) + ", " +
            std::to_string(centre) + ", " + std::to_string(forward) + ")");

  // The rest position has to be the patch untouched, or every note from a
  // controller that never moves would sound wrong.
  setParam(ovt::params::slideDestId, (float)ovt::SlideDestination::Off);
  const auto untouched = rms(63, 63);

  check(std::abs(centre - untouched) < untouched * 0.01,
        "the rest position is the patch as dialled");

  // Off means off, whatever the controller sends.
  check(std::abs(rms(63, 0) - untouched) < untouched * 0.01 &&
            std::abs(rms(63, 127) - untouched) < untouched * 0.01,
        "and with slide off the whole travel is that same sound");

  // ---- a controller whose slide rests at one end --------------------------
  //
  // An Expressive E Osmose spends the first part of the key travel on pressure
  // and only then starts sending CC74, from the bottom of its range upward.
  // Read as a position that is the far dark end of the slide, so engaging the
  // axis used to take the note abruptly darker before it began to brighten.
  // Read as a movement it is the note's rest, and the patch is left alone until
  // the finger actually goes somewhere.
  setParam(ovt::params::slideDestId, (float)ovt::SlideDestination::Brightness);

  const auto engaged = rms(0, 0);
  const auto pressedOn = rms(0, 127);

  std::printf("  resting at the bottom: engaged %.4f, pressed on %.4f, "
              "patch %.4f\n",
              engaged, pressedOn, untouched);

  check(std::abs(engaged - untouched) < untouched * 0.01,
        "a slide that rests at the bottom starts on the patch rather than "
        "darker than it (" +
            std::to_string(engaged) + " against " + std::to_string(untouched) +
            ")");

  check(pressedOn > engaged * 1.1, "and pressing on from there brightens (" +
                                       std::to_string(engaged) + " to " +
                                       std::to_string(pressedOn) + ")");

  // The whole axis reaches the whole effect wherever it set out from, or a
  // controller resting at one end would have twice the reach of one resting in
  // the middle.
  check(std::abs(pressedOn - forward) < forward * 0.01,
        "a full push is the same brightness from either rest (" +
            std::to_string(pressedOn) + " against " + std::to_string(forward) +
            ")");

  // Aimed at tuning it moves pitch rather than level, so both ends still
  // sound. Which pitches is the tuning table's business, tested elsewhere.
  setParam(ovt::params::slideDestId, (float)ovt::SlideDestination::Tuning);

  check(rms(63, 0) > 1.0e-5 && rms(63, 127) > 1.0e-5,
        "both ends sound with slide aimed at tuning");
}

/// Clearing every mute or every solo at once.
///
/// The menu a right-click on an M or an S opens. Thirty-three strips is a lot
/// of places for a solo to be left on, and the whole point is not having to
/// find it.
void testClearMuteAndSolo(OvertoniumProcessor &p) {
  section("Clearing mutes and solos");

  using namespace ovt::ui;

  const auto on = [&p](const char *suffix) {
    return ovt::params::channelsSwitchedOn(p.apvts, suffix);
  };

  const auto set = [&p](const juce::String &id, bool state) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(state ? 1.0f : 0.0f);
  };

  ovt::params::clearChannelSwitch(p.apvts, ovt::params::muteSuffix);
  ovt::params::clearChannelSwitch(p.apvts, ovt::params::soloSuffix);

  check(on(ovt::params::muteSuffix) == 0 && on(ovt::params::soloSuffix) == 0,
        "nothing is muted or soloed to begin with");

  // Scattered rather than contiguous, and the noise channel among them, since
  // it carries the same two switches and sits outside the series.
  for (int i : {0, 7, 19, 31})
    set(ovt::params::oscParamId(ovt::params::muteSuffix, i), true);

  set(ovt::params::noiseParamId(ovt::params::muteSuffix), true);
  set(ovt::params::oscParamId(ovt::params::soloSuffix, 4), true);
  set(ovt::params::oscParamId(ovt::params::soloSuffix, 5), true);

  check(on(ovt::params::muteSuffix) == 5,
        "five channels muted, the noise channel among them (" +
            std::to_string(on(ovt::params::muteSuffix)) + ")");
  check(on(ovt::params::soloSuffix) == 2,
        "and two soloed (" + std::to_string(on(ovt::params::soloSuffix)) + ")");

  // The menu says how many there are to clear, which is the other question a
  // player opens it to ask.
  {
    juce::Component parent;
    MuteSoloButton button(p.apvts, "M");
    auto menu = button.buildMenu();

    std::vector<std::string> entries;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
      entries.push_back(it.getItem().text.toStdString());

    check(entries.size() == 2 && entries[0] == "Clear all mutes (5)" &&
              entries[1] == "Clear all solos (2)",
          "the menu counts what it would clear (" +
              (entries.empty() ? std::string("nothing")
                               : entries[0] + " / " + entries[1]) +
              ")");
  }

  ovt::params::clearChannelSwitch(p.apvts, ovt::params::soloSuffix);

  check(on(ovt::params::soloSuffix) == 0, "clearing the solos clears them all");
  check(on(ovt::params::muteSuffix) == 5,
        "and leaves the mutes alone (" +
            std::to_string(on(ovt::params::muteSuffix)) + ")");

  ovt::params::clearChannelSwitch(p.apvts, ovt::params::muteSuffix);
  check(on(ovt::params::muteSuffix) == 0, "and then the mutes go too");

  // Nothing to clear is not an error, and the entries say so rather than
  // disappearing.
  {
    MuteSoloButton button(p.apvts, "S");
    auto menu = button.buildMenu();

    std::vector<std::pair<std::string, bool>> entries;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
      entries.emplace_back(it.getItem().text.toStdString(),
                           it.getItem().isEnabled);

    check(entries.size() == 2 && entries[0].first == "Clear all mutes" &&
              !entries[0].second && !entries[1].second,
          "with none on, both entries are there and both are greyed out");
  }

  ovt::params::clearChannelSwitch(p.apvts, ovt::params::muteSuffix);
  check(on(ovt::params::muteSuffix) == 0, "and clearing nothing is harmless");
}

void testSoloAndMute(OvertoniumProcessor &p) {
  section("Solo and mute");

  // Just Saw: every partial up, so muting one is visible in the output.
  ovt::presets::apply(p.apvts, presetIndex("Just Saw"));

  // Headroom matters here: with the safety clipper engaged every variant would
  // peak at the same ceiling and the comparisons below would be meaningless.
  auto *master = p.apvts.getParameter(ovt::params::masterGainId);
  master->setValueNotifyingHost(master->convertTo0to1(-24.0f));
  p.apvts.getParameter(ovt::params::safetyClipId)->setValueNotifyingHost(0.0f);

  p.prepareToPlay(48000.0, 512);

  const auto full = renderBlocks(p, 30, 512, noteOnAt(57, 1.0f, 0));
  p.reset();
  renderBlocks(p, 4, 512);

  auto *mute1 =
      p.apvts.getParameter(ovt::params::oscParamId(ovt::params::muteSuffix, 0));
  mute1->setValueNotifyingHost(1.0f);

  const auto muted = renderBlocks(p, 30, 512, noteOnAt(57, 1.0f, 0));
  check(muted.peak < full.peak, "muting the fundamental reduces the peak");
  check(muted.peak > 0.0f, "the other partials still sound");

  p.reset();
  renderBlocks(p, 4, 512);
  mute1->setValueNotifyingHost(0.0f);

  // Solo one partial: the output should collapse to a single sine.
  auto *solo5 =
      p.apvts.getParameter(ovt::params::oscParamId(ovt::params::soloSuffix, 4));
  solo5->setValueNotifyingHost(1.0f);

  const auto soloed = renderBlocks(p, 30, 512, noteOnAt(57, 1.0f, 0));
  check(soloed.peak > 0.0f, "soloed partial sounds");
  check(soloed.peak < full.peak, "solo removes the rest of the spectrum");

  solo5->setValueNotifyingHost(0.0f);
  p.reset();
}

} // namespace

int main() {
  // See the same line in dsp_test: unbuffered, so a crash keeps whatever it
  // printed before it, and not _IOLBF, which the Windows CRT ignores.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  juce::ScopedJuceInitialiser_GUI juceInit;

  OvertoniumProcessor processor;

  testParameterWiring(processor);
  testChoiceParameterCounts(processor);
  testRendering(processor);
  testReleaseVelocity(processor);
  testBusStageFollowsTheCharacter(processor);
  testPresets(processor);
  testAftertouchMidi(processor);
  testMpe(processor);
  testMpeSlide();
  testActivityLamps(processor);
  testStrikeReadout();
  testSegmentReadouts(processor);
  testChannelHover(processor);
  testFactoryCodeGenerator(processor);
  testBothPresetKindsCarryTheSame();
  testOversizedBlocks(processor);
  testUserPresets(processor);
  testMasterEffects(processor);
  testLinkCurves();
  testRowHover();
  testMeterRepaint();
  testLinkMenu();
  testTopBarLayout();
  testOctaveChannelsSayTuneDoesNothing(processor);
  testModulatorsInPhase(processor);
  testSettingsMenu(processor);
  testFitAllChannels(processor);
  testZoomTickFollowsTheZoom(processor);
  testDrawingAcrossTheFaders(processor);
  testLinkSurvivesAReopen(processor);
  testFoldingAndUnfoldingIsSymmetric(processor);
  testFittingLandsWhereADragCanReturn(processor);
  testTheRulesFoldTheirSections(processor);
  testOneRightClickOpensOneMenu(processor);
  testWheelOverAKnobStaysOnTheKnob(processor);
  testASidewaysWheelMovesTheMixer(processor);
  testTheGutterBorderIsOneLine(processor);
  testTheBandsLayOutAColumn();
  testTheWheelScrollsTheParameters(processor);
  testTheGrainTileDoesNotOutliveTheWindows();
  testACorruptStateCannotPoisonTheOutput();
  testEveryHostRateStaysFinite();
  testTheSafetyClipHoldsUnity();
  testMidiLearnBindsControllers(processor);
  testMacrosMoveAWholeRow(processor);
  testSettingsNamesTheVersion(processor);
  testMachineMenusFollowTheirParameters(processor);
  testPresetMenuGroups(processor);
  testPresetsTellTheHostOnlyWhatChanged(processor);
  testShapeButtonFollowsTheParameter(processor);
  testFirstProgramIsReachable();
  testPresetNameOutlivesTheWindow();
  testBarButtonsFitTheirWords(processor);
  testBarComesOntoOneRow(processor);
  testTopBarAlignment(processor);
  testKnobSizes(processor);
  testNoDeadTravel(processor);
  testPresetsAreReproducible(processor);
  testUndo(processor);
  testAutomationLeavesNoHistory(processor);
  testUndoGrouping(processor);
  testCharacterControl(processor);
  testStandaloneWindow(processor);
  testBusLayouts(processor);
  testUndersizedBuffer();
  testMonoOutput();
  testStateRoundTrip(processor);
  testPrograms(processor);
  testProgramChangeMidi(processor);
  testCollapsibleSections();
  testUpdateCheck();
  testUpdateCheckIsQuiet(processor);
  testThePreferenceOutlivesItsWriter();
  testUpdateCheckOutlivesEditors(processor);
  testEveryControlIsNamed(processor);
  testSoloAndMute(processor);
  testClearMuteAndSolo(processor);

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
