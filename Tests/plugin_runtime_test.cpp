// Headless integration tests for the plugin layer: parameter wiring, MIDI
// handling, factory presets, undo, bus layouts and state round-tripping, plus
// the parts of the editor that can be measured rather than looked at.
//
// It builds an editor for the layout checks but never gives it a window, so it
// runs on a CI box with no display. Nothing here may open a PopupMenu, which
// needs one.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "PluginEditor.h"
#include "PluginParameters.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "UI/ChannelStrip.h"
#include "UI/GlideMenu.h"
#include "UI/LearnMenu.h"
#include "UI/LookAndFeel.h"
#include "UI/MacroPanel.h"
#include "UI/NoiseStrip.h"
#include "UI/SettingsMenus.h"
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

/// A factory preset by name, since the menu is alphabetical and gains entries,
/// so a position does not stay the same sound.
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
// The tests stay inside the anonymous namespace the helpers opened, since
// nothing here is called from another translation unit.

/// Sizes an editor to its smallest legal height plus extraHeight, read from the
/// editor's own limits so it stays right when the mixer gains a row. A plain
/// setSize does not consult the constrainer.
///
/// The width is the caller's, since it decides how much mixer is on screen.
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

/// The bar out of an editor. Searched for rather than reached into, since the
/// depth it sits at is a layout decision.
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
/// A host stores a choice parameter as a value between zero and one, so adding
/// an entry to a list that has shipped moves every automation lane written
/// against it. A list that has to grow needs a new parameter beside the old one
/// instead.
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
      {ovt::params::polyphonyId, 8},    {ovt::params::characterId, 12},
      {ovt::params::temperamentId, 9},  {ovt::params::tuningRootId, 12},
      {ovt::params::referenceHzId, 11}, {ovt::params::atSourceId, 3},
      {ovt::params::slideDestId, 3},    {ovt::params::lofiRateId, 8},
      {ovt::params::lofiBitsId, 9},     {ovt::params::echoTypeId, 3},
      {ovt::params::reverbTypeId, 3},   {ovt::params::glideTriggerId, 2},
      {ovt::params::glideModeId, 2},    {ovt::params::clipTypeId, 8},
  };

  for (const auto &list : globals)
    check(entriesOf(list.id) == list.entries,
          juce::String(list.id).toStdString() + " offers " +
              std::to_string(list.entries) + " and has " +
              std::to_string(entriesOf(list.id)));

  // Retune is a time, from a jump to a second, with its default where the old
  // Fast was.
  if (auto *retune = dynamic_cast<juce::RangedAudioParameter *>(
          p.apvts.getParameter(ovt::params::adaptiveRetuneId));
      retune != nullptr &&
      dynamic_cast<juce::AudioParameterFloat *>(retune) != nullptr)
    check(std::abs(retune->getNormalisableRange().start) < 1.0e-6f &&
              std::abs(retune->getNormalisableRange().end - 1.0f) < 1.0e-6f &&
              std::abs(retune->convertFrom0to1(retune->getDefaultValue()) -
                       0.03f) < 1.0e-4f,
          "Adaptive Retune is a time from 0 to 1 s, 30 ms to begin with");
  else
    check(false, "Adaptive Retune is a time");

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

  // The amplitude has one shape fewer on purpose, since a unipolar square has
  // no second direction to go in.
  check(entriesOf(ovt::params::oscParamId(ovt::params::pmShapeSuffix, 0)) !=
            entriesOf(ovt::params::oscParamId(ovt::params::amShapeSuffix, 0)),
        "the two modulators deliberately do not offer the same list");
}

/// Every parameter the host sees, one JSON object per line, in the order the
/// host lists them. docs/parameters.json is this, and Tools/build_site.py
/// builds the site's parameter list from that file.
///
/// The range and the default are the text the plugin itself shows for them,
/// which is what a reader sees on the knob or in the host.
juce::String parameterReference(OvertoniumProcessor &p) {
  const auto quoted = [](const juce::String &text) {
    return juce::JSON::toString(juce::var(text));
  };

  juce::StringArray lines;

  for (auto *base : p.getParameters()) {
    auto *param = dynamic_cast<juce::RangedAudioParameter *>(base);

    if (param == nullptr)
      continue;

    juce::String line = "{\"id\": " + quoted(param->getParameterID()) +
                        ", \"name\": " + quoted(param->getName(100));
    const auto shown = [param](float normalised) {
      return param->getText(normalised, 100);
    };

    if (auto *choice = dynamic_cast<juce::AudioParameterChoice *>(param)) {
      juce::StringArray each;

      for (const auto &entry : choice->choices)
        each.add(quoted(entry));

      line += ", \"type\": \"choice\", \"choices\": [" +
              each.joinIntoString(", ") + "]";
    } else if (dynamic_cast<juce::AudioParameterBool *>(param) != nullptr) {
      line += ", \"type\": \"switch\"";
    } else {
      const bool whole =
          dynamic_cast<juce::AudioParameterInt *>(param) != nullptr;
      line += juce::String(", \"type\": \"") +
              (whole ? "whole" : "continuous") +
              "\", \"min\": " + quoted(shown(0.0f)) +
              ", \"max\": " + quoted(shown(1.0f));
    }

    line += ", \"default\": " + quoted(shown(param->getDefaultValue()));

    if (param->getLabel().isNotEmpty())
      line += ", \"unit\": " + quoted(param->getLabel());

    lines.add("    " + line + "}");
  }

  return "[\n" + lines.joinIntoString(",\n") + "\n]\n";
}

juce::File parameterReferenceFile() {
  return juce::File(juce::String(OVERTONIUM_SOURCE_DIR))
      .getChildFile("docs/parameters.json");
}

/// The published list against the real layout. Carriage returns are dropped
/// first because Git checks the file out with them on Windows.
void testTheParameterListIsCurrent(OvertoniumProcessor &p) {
  section("The published parameter list");

  const auto file = parameterReferenceFile();
  const auto published = file.loadFileAsString().removeCharacters("\r");

  const auto live = parameterReference(p);

  check(file.existsAsFile(), "docs/parameters.json exists");
  check(published == live,
        "docs/parameters.json is the layout. Regenerate it with "
        "overtonium_runtime_test --write-parameters, then run "
        "Tools/build_site.py");

  // Which line, and both versions of it, since a difference that shows on one
  // platform cannot be found by regenerating the file on another.
  if (published != live) {
    const auto was = juce::StringArray::fromLines(published);
    const auto is = juce::StringArray::fromLines(live);

    for (int i = 0; i < juce::jmax(was.size(), is.size()); ++i) {
      if (was[i] != is[i]) {
        std::printf("  line %d of docs/parameters.json\n"
                    "    published: %s\n"
                    "    this build: %s\n",
                    i + 1, was[i].toRawUTF8(), is[i].toRawUTF8());
        break;
      }
    }
  }
}

/// That every parameter has text of its own, which is what a host's automation
/// lane, the panel's value popup and a screen reader all show. A float
/// parameter with none prints seven decimal places, 4.0000005 for 4.
void testReadoutsAreWrittenAsAPersonWould(OvertoniumProcessor &p) {
  section("Readouts a person would write");

  const auto shown = [&p](const juce::String &id, float value) {
    auto *param = p.apvts.getParameter(id);
    return param == nullptr ? juce::String("(missing)")
                            : param->getText(param->convertTo0to1(value), 100);
  };

  struct Case {
    const char *id;
    float value;
    const char *expected;
  };

  const Case cases[] = {
      {"h01_pmRate", 4.0f, "4.00 Hz"},
      {"h01_pmRate", 0.01f, "0.01 Hz"},
      {"h01_pmRate", 30.0f, "30.0 Hz"},
      {"h17_amRate", 0.25f, "0.25 Hz"},
      {"noise_amRate", 12.5f, "12.5 Hz"},
      {"h01_pmDepth", 0.0f, "0 ct"},
      {"h01_pmDepth", 2.5f, "2.5 ct"},
      {"h01_pmDepth", 1200.0f, "1200 ct"},
      {"h32_drift", 25.0f, "25 ct"},
      {"h32_drift", 6.04f, "6.0 ct"},
      {"macro1_amount", 0.0f, "0.0 %"},
      {"macro1_amount", 1.0f, "+100.0 %"},
      {"macro8_amount", -1.0f, "-100.0 %"},
      {"macro4_amount", 0.123f, "+12.3 %"},
  };

  for (const auto &c : cases)
    check(shown(c.id, c.value) == c.expected,
          std::string(c.id) + " at " + std::to_string(c.value) + " reads \"" +
              shown(c.id, c.value).toStdString() + "\", expected \"" +
              c.expected + "\"");

  // The centre a hair low, which is where macOS puts it once the range has
  // snapped the value to its step. It has to print without a minus sign on
  // every platform.
  if (auto *amount = p.apvts.getParameter("macro1_amount"))
    check(amount->getText(0.4999999f, 100) == "0.0 %",
          "a macro amount a hair below its centre reads 0.0 %, and reads " +
              amount->getText(0.4999999f, 100).toStdString());

  // And none of the rest, at either end, the middle or the default, prints
  // more places than anyone would read: five digits after a point is the
  // signature of a parameter that has no text function.
  int long_ = 0, negativeZero = 0;
  juce::String first, firstZero;

  for (auto *base : p.getParameters()) {
    auto *param = dynamic_cast<juce::RangedAudioParameter *>(base);

    if (param == nullptr)
      continue;

    for (float at : {0.0f, 0.5f, 1.0f, param->getDefaultValue()}) {
      const auto text = param->getText(at, 100);
      const auto point = text.indexOfChar('.');
      int digits = 0;

      for (int i = point + 1; point >= 0 && i < text.length() &&
                              juce::CharacterFunctions::isDigit(text[i]);
           ++i)
        ++digits;

      if (digits >= 5) {
        ++long_;
        if (first.isEmpty())
          first = param->getParameterID() + " reads " + text;
      }

      // A minus sign in front of nothing but noughts is a value a hair below
      // zero printed without rounding first, and whether it is below zero
      // depends on the platform's float arithmetic.
      if (text.startsWithChar('-') &&
          text.retainCharacters("0123456789").containsOnly("0") &&
          text.containsAnyOf("0123456789")) {
        ++negativeZero;
        if (firstZero.isEmpty())
          firstZero = param->getParameterID() + " reads " + text;
      }
    }
  }

  check(negativeZero == 0, "no parameter reads as a negative zero (" +
                               std::to_string(negativeZero) + " do, first " +
                               firstZero.toStdString() + ")");

  check(long_ == 0, "no parameter reads to five or more places (" +
                        std::to_string(long_) + " do, first " +
                        first.toStdString() + ")");
}

/// The glide knob on every channel, and the two switches over all of them in
/// that knob's menu. The engine side is measured in the DSP suite. This holds
/// the parameters, the menu and the macro slot to what the engine expects.
void testGlideControls(OvertoniumProcessor &p) {
  section("Glide controls");

  p.applyFactoryPreset(presetIndex("Init"));

  const auto set = [&p](const juce::String &id, float plain) {
    if (auto *q = p.apvts.getParameter(id))
      q->setValueNotifyingHost(q->convertTo0to1(plain));
  };

  const auto snapshot = [&p] {
    ovt::SynthParams out;
    p.parameters().snapshot(out, 0.0f);
    return out;
  };

  int everywhere = 0;

  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (auto *q = p.apvts.getParameter(
            ovt::params::oscParamId(ovt::params::glideSuffix, i)))
      everywhere += q->getText(q->getDefaultValue(), 100) == "Off";

  check(everywhere == ovt::kNumHarmonics,
        "every channel has a glide time, off by default (" +
            std::to_string(everywhere) + " of 32)");

  check(p.apvts.getParameter(ovt::params::oscParamId("phase", 0)) == nullptr,
        "and the start phase it replaced is gone");

  // Through to what the engine reads.
  set(ovt::params::oscParamId(ovt::params::glideSuffix, 4), 0.3f);
  set(ovt::params::glideTriggerId, 1.0f);
  set(ovt::params::glideModeId, 1.0f);

  const auto played = snapshot();

  check(std::abs(played.osc[4].glideSeconds - 0.3f) < 0.01f,
        "a channel's glide time reaches the engine");
  check(played.global.glideTrigger == ovt::GlideTrigger::Legato &&
            played.global.glideMode == ovt::GlideMode::Time,
        "and so do the trigger and the mode");

  // The menu, as data, since one cannot be shown here.
  set(ovt::params::glideTriggerId, 0.0f);
  set(ovt::params::glideModeId, 0.0f);

  const auto glideId = ovt::params::oscParamId(ovt::params::glideSuffix, 4);

  juce::PopupMenu onGlide, onDrift;
  check(ovt::ui::glide::appendItems(onGlide, p.apvts, glideId),
        "a glide knob's menu carries the switches");
  check(!ovt::ui::glide::appendItems(
            onDrift, p.apvts,
            ovt::params::oscParamId(ovt::params::driftSuffix, 4)),
        "and no other knob's does");

  int ticked = 0, offered = 0;

  for (juce::PopupMenu::MenuItemIterator it(onGlide); it.next();) {
    const auto &item = it.getItem();

    if (item.itemID >= ovt::ui::glide::kAlways &&
        item.itemID <= ovt::ui::glide::kTime) {
      ++offered;
      ticked += item.isTicked;
    }
  }

  check(offered == 4 && ticked == 2,
        "four choices, one of each pair ticked (" + std::to_string(offered) +
            " offered, " + std::to_string(ticked) + " ticked)");

  check(ovt::ui::glide::applyChoice(ovt::ui::glide::kLegato, p.apvts) &&
            ovt::ui::glide::applyChoice(ovt::ui::glide::kTime, p.apvts),
        "both kinds of choice are taken");

  const auto chosen = snapshot();

  check(chosen.global.glideTrigger == ovt::GlideTrigger::Legato &&
            chosen.global.glideMode == ovt::GlideMode::Time,
        "and choosing them sets the switches");

  check(!ovt::ui::glide::applyChoice(ovt::ui::learn::kLearn, p.apvts),
        "a choice that is not one of these is left for the next handler");

  // Glide sits in the macro list where start phase did, so a macro stored
  // against any other row still points at the same one.
  check(
      juce::String(ovt::params::macroRowName(2)) == "Glide" &&
          juce::String(ovt::params::macroRowName(3)) == "Pitch mod rate",
      "Glide holds the macro slot start phase had, and nothing after it moved");

  p.applyFactoryPreset(presetIndex("Init"));
}

/// The ATTACK row reaching below its shortest time into where a partial
/// starts in its cycle. The sound is measured in the DSP suite. This holds the
/// parameter to it: what it reads, what typing into it does, that the range
/// converts both ways without drifting, and what the strike readout says.
void testAttackReachesIntoThePhase(OvertoniumProcessor &p) {
  section("Attack below its shortest time");

  auto *attack = p.apvts.getParameter(
      ovt::params::oscParamId(ovt::params::attackSuffix, 0));

  if (attack == nullptr) {
    check(false, "the first channel has an attack");
    return;
  }

  const auto reads = [attack](float plain) {
    return attack->getText(attack->convertTo0to1(plain), 100);
  };

  check(reads(-1.0f) == juce::String("90") + juce::String::charToString(0xb0) &&
            reads(-0.5f) ==
                juce::String("45") + juce::String::charToString(0xb0) &&
            reads(0.0002f) == "0.2 ms" && reads(0.005f) == "5.0 ms",
        "it reads in degrees below its shortest time and in time above it (" +
            reads(-1.0f).toStdString() + ", " + reads(-0.5f).toStdString() +
            ", " + reads(0.0002f).toStdString() + ", " +
            reads(0.005f).toStdString() + ")");

  check(attack->getText(0.0f, 100) ==
            juce::String("90") + juce::String::charToString(0xb0),
        "and the bottom of the knob is a partial starting on its peak");

  // Both ways round, across the travel, so a stored position and a stored
  // value describe the same onset.
  double worst = 0.0;
  for (int i = 0; i <= 100; ++i) {
    const auto n = (float)i / 100.0f;
    worst = std::max(
        worst, (double)std::abs(
                   attack->convertTo0to1(attack->convertFrom0to1(n)) - n));
  }

  check(worst < 1.0e-4, "the range converts both ways without drifting (" +
                            std::to_string(worst) + ")");

  const auto typed = [attack](const char *text) {
    return attack->convertFrom0to1(attack->getValueForText(text));
  };

  check(std::abs(typed("45\xc2\xb0") + 0.5f) < 1.0e-3f &&
            std::abs(typed("5 ms") - 0.005f) < 1.0e-4f &&
            std::abs(typed("1.2 s") - 1.2f) < 1.0e-3f &&
            std::abs(typed("12") - 0.012f) < 1.0e-4f,
        "a typed value is degrees, milliseconds or seconds as it says");

  // The readout under STRIKE, with the knob on the peak and a full amount: a
  // hard blow starts on the peak and the lightest travels up into a time.
  const auto readout = ovt::params::strikeRangeText(1.0f, 0.0f, -1.0f);

  check(readout.contains(juce::String("90") + juce::String::charToString(0xb0) +
                         " to ") &&
            readout.endsWith("ms"),
        "the strike readout names the phase at the hard end (" +
            readout.toStdString() + ")");
}

/// Legato as a switch beside the voice count, and the polyphony list's old
/// first entry, which still means monophonic legato. Checks what the engine is
/// told. The sound is measured in the DSP suite.
void testLegatoSwitch(OvertoniumProcessor &p) {
  section("The Legato switch");

  auto *legato = p.apvts.getParameter(ovt::params::legatoId);
  auto *count = p.apvts.getParameter(ovt::params::polyphonyId);

  if (legato == nullptr || count == nullptr) {
    check(false, "Legato and the voice count both exist");
    return;
  }

  check(legato->getDefaultValue() < 0.5f, "Legato is off by default");
  check(ovt::params::isSessionParam(ovt::params::legatoId),
        "and is a session setting like the count, so a preset never moves it");

  const auto setCount = [count](int index) {
    count->setValueNotifyingHost(count->convertTo0to1((float)index));
  };

  const auto &cache = p.parameters();

  legato->setValueNotifyingHost(0.0f);
  setCount(5); // 8 voices
  check(!cache.legatoValue() && cache.polyphonyValue() == 8,
        "off, with eight voices, is eight voices");

  legato->setValueNotifyingHost(1.0f);
  check(cache.legatoValue() && cache.polyphonyValue() == 8,
        "on, with eight, is the top line over eight");

  setCount(1);
  check(cache.legatoValue() && cache.polyphonyValue() == 1,
        "on, with one, is monophonic legato");

  // A session saved with the list's old first entry.
  legato->setValueNotifyingHost(0.0f);
  setCount(ovt::params::kLegatoIndex);
  check(cache.legatoValue() && cache.polyphonyValue() == 1,
        "the old Legato entry still plays as one voice with legato on");

  setCount(5);
  legato->setValueNotifyingHost(0.0f);
}

/// The glide caps in the gutter, beside the GLIDE caption: lit by the two
/// switches, firing their own callbacks, and gone with the row when its
/// section is folded.
void testGlideCaps() {
  section("The gutter caps");

  using namespace ovt::ui;

  RowGutter gutter;
  gutter.setBounds(0, 0, kGutterWidth, preferredStripHeight());

  auto *held =
      dynamic_cast<juce::Button *>(gutter.findChildWithID("glideLegato"));
  auto *perNote =
      dynamic_cast<juce::Button *>(gutter.findChildWithID("glideFixedTime"));

  if (held == nullptr || perNote == nullptr) {
    check(false, "the gutter has both glide caps");
    return;
  }

  const auto rows = layoutRows(gutter.getLocalBounds().reduced(0, kStripPadY));
  const auto glideRow = rows[(size_t)Row::Glide];

  check(held->isVisible() && perNote->isVisible() &&
            glideRow.contains(held->getBounds().getCentre()) &&
            glideRow.contains(perNote->getBounds().getCentre()),
        "both stand in the glide row");
  // Where the caption starts: right-aligned eight pixels in, as the gutter
  // paints it, in the weight it has when nothing is pointing at it.
  const auto captionLeft = [&rows](Row row) {
    return rows[(size_t)row].getRight() - 8 -
           juce::GlyphArrangement::getStringWidthInt(makeFont(9.5f),
                                                     rowLabel(row));
  };

  check(perNote->getRight() < captionLeft(Row::Glide) &&
            perNote->getRight() > captionLeft(Row::Glide) - 8 &&
            held->getRight() == perNote->getX(),
        "side by side, right up against the caption and clear of it");

  gutter.setGlideSwitches(true, false);
  check(held->getToggleState() && !perNote->getToggleState(),
        "and light from the switches, one at a time");

  gutter.setGlideSwitches(false, true);
  check(!held->getToggleState() && perNote->getToggleState(),
        "either way round");

  int heldClicks = 0, noteClicks = 0;
  gutter.onGlideLegatoClicked = [&heldClicks] { ++heldClicks; };
  gutter.onGlideFixedTimeClicked = [&noteClicks] { ++noteClicks; };

  held->onClick();
  perNote->onClick();
  perNote->onClick();

  check(heldClicks == 1 && noteClicks == 2, "each cap fires its own callback");

  gutter.setCollapsedSections(sectionBit(Section::PitchMod));
  check(!held->isVisible() && !perNote->isVisible(),
        "folding PITCH MOD takes them with the row");

  gutter.setCollapsedSections(0);
  check(held->isVisible() && perNote->isVisible(),
        "and unfolding brings them back");

  check(held->getWidth() == held->getHeight(),
        "square, the size of a channel's M and S");

  auto *pitch =
      dynamic_cast<juce::Button *>(gutter.findChildWithID("pitchInPhase"));
  auto *amp =
      dynamic_cast<juce::Button *>(gutter.findChildWithID("ampInPhase"));

  if (pitch == nullptr || amp == nullptr) {
    check(false, "the gutter has both in-phase caps");
    return;
  }

  check(
      pitch->isVisible() && amp->isVisible() &&
          rows[(size_t)Row::PmShape].contains(pitch->getBounds().getCentre()) &&
          rows[(size_t)Row::AmShape].contains(amp->getBounds().getCentre()),
      "an in-phase cap stands in each shape row");
  check(pitch->getRight() < captionLeft(Row::PmShape) &&
            pitch->getRight() > captionLeft(Row::PmShape) - 8 &&
            amp->getRight() == pitch->getRight() &&
            pitch->getWidth() == held->getWidth(),
        "right up against its caption, at the glide switches' size");

  gutter.setSharedModulators(true, false);
  check(pitch->getToggleState() && !amp->getToggleState(),
        "and lights from its own modulator's switch");

  gutter.setSharedModulators(false, true);
  check(!pitch->getToggleState() && amp->getToggleState(), "either way round");

  int pitchClicks = 0, ampClicks = 0;
  gutter.onPitchInPhaseClicked = [&pitchClicks] { ++pitchClicks; };
  gutter.onAmpInPhaseClicked = [&ampClicks] { ++ampClicks; };

  pitch->onClick();
  amp->onClick();
  amp->onClick();

  check(pitchClicks == 1 && ampClicks == 2,
        "each in-phase cap fires its own callback");

  gutter.setCollapsedSections(sectionBit(Section::AmpMod));
  check(pitch->isVisible() && !amp->isVisible(),
        "folding AMP MOD takes its cap and leaves the other");
  gutter.setCollapsedSections(0);
}

/// The parameter layout: how many there are, that every ID the audio thread
/// caches exists, and that the defaults and ranges read as the engine expects.
void testParameterWiring(OvertoniumProcessor &p) {
  section("Parameter wiring");

  // 23 per partial, 20 global, 18 for the noise channel, which has no pitch
  // controls, and 10 for the two master effects.
  //
  // Then four for the echo type, the reverb type, the output stage type and
  // whether the output stage may look ahead. Two for glide, one for Legato,
  // three for the adaptive temperament, four for the noise string and four for
  // the resonator.
  //
  // And six for each macro: the amount, the row it reaches, the channels it
  // reaches, how it is shared out, the channel a taper leans on, and its
  // colour.
  const int expected = ovt::kNumHarmonics * 23 + 20 + 18 + 10 + 4 + 2 + 1 + 1 +
                       1 + 1 + 4 + 4 + ovt::params::kNumMacros * 6;

  // No stack defaults to on. Asked of the parameter rather than of the tree, so
  // the answer does not depend on what an earlier test left behind.
  if (auto *v = p.apvts.getParameter(ovt::params::oneVoicePerKeyId))
    check(v->getDefaultValue() > 0.5f, "No stack defaults to on");
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
      ovt::params::tuneSuffix,    ovt::params::glideSuffix,
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
  // TapeEcho::kMinAge. Both directions are checked, so 0 % shown can be typed
  // back as 0 %.
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

/// That the speed a key comes up at reaches the tail, and that both ways a
/// keyboard says "no release velocity" leave the instrument as it was. A zero
/// must read as no information rather than the softest lift.
///
/// The shape of what it does is in the DSP suite.
void testReleaseVelocity(OvertoniumProcessor &p) {
  section("Release velocity");

  p.applyFactoryPreset(presetIndex("Init"));

  const auto put = [&p](const char *suffix, float value) {
    auto *param = p.apvts.getParameter(ovt::params::oscParamId(suffix, 0));

    if (param != nullptr)
      param->setValueNotifyingHost(param->convertTo0to1(value));

    return param != nullptr;
  };

  // Init has an 8 ms release, so this gives it half a second of tail, a short
  // decay so the key comes up at the sustain, and a sustain of 0.4 so a hard
  // lift has room before the envelope's ceiling at one.
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

/// That choosing a character brings its bus stage with it, and that the latency
/// this costs is told to the host and never moves. What each circuit does to a
/// signal is measured in the DSP suite.
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

  // Off by default, which leaves the bus stage's few samples and nothing
  // else.
  auto *ahead = p.apvts.getParameter(ovt::params::lookaheadId);

  check(ahead != nullptr && ahead->getDefaultValue() < 0.5f,
        "the lookahead is off by default");
  check(p.getLatencySamples() == ovt::BusDrive::kLatency,
        "so the plugin reports only the bus stage (" +
            std::to_string(p.getLatencySamples()) + " samples)");

  if (ahead == nullptr)
    return;

  // With it on, the figure is fixed whatever the patch does, so a preset loads
  // without the host re-planning its graph.
  ahead->setValueNotifyingHost(1.0f);
  p.prepareToPlay(48000.0, 512);

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

  // Every character, including None, costs the same few samples. The output
  // stage's lookahead is in the figure too and is paid on every clip type and
  // with the clipper off.
  const auto expected =
      ovt::BusDrive::kLatency + ovt::OutputStage::lookaheadSamples(48000.0);

  check(idle == expected, "both stages' latency is reported (" +
                              std::to_string(idle) + " samples, expected " +
                              std::to_string(expected) + ")");

  check(p.getLatencySamples() == idle,
        "and does not move when the character does (" +
            std::to_string(p.getLatencySamples()) + ")");

  // The lookahead belongs to four of the eight types and is paid by all of
  // them, so switching the stage off has to leave the figure alone.
  auto *clip = p.apvts.getParameter(ovt::params::safetyClipId);
  clip->setValueNotifyingHost(0.0f);
  p.prepareToPlay(48000.0, 512);

  check(p.getLatencySamples() == idle,
        "nor when the output stage is switched off (" +
            std::to_string(p.getLatencySamples()) + ")");

  clip->setValueNotifyingHost(1.0f);
  p.prepareToPlay(48000.0, 512);

  // The one thing allowed to move it, since it sits in Settings where no preset
  // and nothing on the bar can reach it. Turning it off gives the output
  // stage's whole share back.
  check(ovt::params::isSessionParam(ovt::params::lookaheadId),
        "the lookahead is a session setting, so no preset can move the "
        "latency");

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

  // Back to the default for whatever runs next.
  ahead->setValueNotifyingHost(0.0f);
  p.prepareToPlay(48000.0, 512);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// A note renders finite, audible and within full scale, frees its voice after
/// release, and starts at its timestamp within the block.
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

/// That a controller can be bound to a control, that binding one does not take
/// a controller the instrument already uses, and that the map survives saving
/// and reopening the session.
///
/// The map is reached directly, since that is what the menus call. The menu is
/// checked by the item it offers.
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
  // Armed and then handed the slide axis, which an MPE controller moves
  // whenever a finger does.
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

  // The message that binds it does not also move the control.
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
  // The map describes the controller rather than the sound, so a preset leaves
  // it alone.
  p.applyFactoryPreset(presetIndex("Big Saw"));

  check(p.midiLearn.controllerFor(afterLoad) == 20,
        "which a preset does not disturb");

  // ---- and it works where it would be easiest to break ------------------
  //
  // With MPE on, the parser is handed every message and only the mod wheel, a
  // program change and all-sound-off fall through to the ordinary handler. MPE
  // is off by default, so this is the only case that proves the binding is
  // checked before the parser.
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
  // A control has to know which parameter it moves, and the menu has to offer
  // the item.
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

      // A fader on the first channel and the master fader on the bar, which are
      // tagged in different places, so a tag missing from either fails here.
      const auto fader = ovt::params::oscParamId(ovt::params::volumeSuffix, 0);

      check(findTagged(*editor, fader) != nullptr,
            "a channel fader knows which parameter it moves");
      check(findTagged(*editor, ovt::params::masterGainId) != nullptr,
            "and so does the master fader on the bar");

      // ---- and a waiting control says so -------------------------------
      //
      // The window finds the armed control on its timer and marks it, and takes
      // the mark off when the wait ends. The look and feel draws it.
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

  // ---- including the macro amounts ---------------------------------------
  //
  // The amount is what a host automates, so it is the one worth binding. It is
  // reachable only if the macro panel is in the window and its fader carries
  // the same tag as every other control.
  {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

    if (editor != nullptr) {
      auto *amount =
          ovt::ui::learn::controlFor(*editor, ovt::params::macroAmountId(0));

      check(amount != nullptr,
            "a macro's amount can be found by the parameter it moves");

      auto *q = dynamic_cast<juce::RangedAudioParameter *>(
          p.apvts.getParameter(ovt::params::macroAmountId(0)));

      if (amount != nullptr && q != nullptr) {
        p.midiLearn.forget(q);
        p.midiLearn.arm(q);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 23, 64), 0);
        renderBlocks(p, 2, 256, midi);

        check(p.midiLearn.controllerFor(q) == 23,
              "and a controller binds to it (" +
                  std::to_string(p.midiLearn.controllerFor(q)) + ")");

        midi.clear();
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 23, 127), 0);
        renderBlocks(p, 2, 256, midi);

        check(q->getValue() > 0.9f,
              "and moves it (" + std::to_string(q->getValue()) + ")");

        p.midiLearn.forget(q);
        q->setValueNotifyingHost(q->convertTo0to1(0.0f));
      }
    }
  }

  p.midiLearn.clear();
  p.applyFactoryPreset(presetIndex("Init"));
}

/// That one automatable parameter moves a whole row, so an edit across all 32
/// channels can be automated on one lane.
///
/// Read off the snapshot rather than off the sound, since a macro offsets what
/// the patch says on the way to the engine.
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

  // TUNE. One rather than zero, since the list opens with None, which is an
  // unmade macro.
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

  // Downward, because Init leaves TUNE at the top of its range, where pushing
  // up would clamp on every channel and read as nothing moved.
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

  // The parameters it offsets have not moved, so the patch is where it was and
  // the host has one lane rather than thirty-two.
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

  // ---- Selected reaches the channels it was given ------------------------
  //
  // Harmonics 3, 5, 9 and 12, which no other scope names together. They travel
  // with the macro's state: into the session, into a user preset, and out again
  // when a factory preset is chosen.
  {
    const auto given = ovt::params::channelListMask("3,5,9,12");
    p.macroChannels()[0].store(given);
    set(ovt::params::macroScopeId(0),
        (float)(int)ovt::params::MacroScope::Selected);

    const auto reaching = [&] {
      const auto now = snapshot();
      std::uint32_t mask = 0;

      for (int i = 0; i < ovt::kNumHarmonics; ++i)
        if (std::abs(now.osc[(size_t)i].tuneBlend -
                     resting.osc[(size_t)i].tuneBlend) > 1.0e-5f)
          mask |= 1u << i;

      return mask;
    };

    check(reaching() == given,
          "Selected reaches the channels it was given and no others (" +
              ovt::params::channelListText(reaching()).toStdString() + ")");

    check(ovt::params::channelListText(given) == "3,5,9,12" &&
              ovt::params::channelListMask(" 3, 5,x,9,12,40") == given,
          "and its channels are written as their numbers and read back");

    juce::MemoryBlock session;
    p.getStateInformation(session);
    p.macroChannels()[0].store(0);
    p.setStateInformation(session.getData(), (int)session.getSize());

    check(p.macroChannels()[0].load() == given, "the session keeps them");

    const auto doc =
        ovt::presets::capture(p.apvts, "Selected", &p.macroChannels());
    p.macroChannels()[0].store(0);
    p.macroChannels()[3].store(1u);
    ovt::presets::restore(p.apvts, *doc, &p.macroChannels());

    check(p.macroChannels()[0].load() == given &&
              p.macroChannels()[3].load() == 0,
          "and so does a preset of your own, which also clears a macro it "
          "names none for");

    p.applyFactoryPreset(presetIndex("Init"));

    check(p.macroChannels()[0].load() == 0,
          "while a factory preset, which has none, takes them away");

    // Undo puts them back with the scope that uses them: undoing a preset load
    // leaves macro 1 on Selected reaching the same four channels.
    set(ovt::params::macroRowId(0), (float)tuneRow);
    set(ovt::params::macroScopeId(0),
        (float)(int)ovt::params::MacroScope::Selected);
    p.macroChannels()[0].store(given);
    p.undo().clearUndoHistory();

    p.recordEdit("Load preset",
                 [&p] { p.applyFactoryPreset(presetIndex("Init")); });

    check(p.macroChannels()[0].load() == 0, "a preset load clears them");

    p.undo().undo();

    check(p.macroChannels()[0].load() == given &&
              juce::roundToInt(
                  p.apvts.getParameter(ovt::params::macroScopeId(0))
                      ->convertFrom0to1(
                          p.apvts.getParameter(ovt::params::macroScopeId(0))
                              ->getValue())) ==
                  (int)ovt::params::MacroScope::Selected,
          "undoing it puts back the scope and its channels together");

    p.undo().redo();

    check(p.macroChannels()[0].load() == 0, "and redoing it clears them again");

    // Taking new channels with the scope already on Selected moves no
    // parameter, and is still a step of its own.
    p.undo().clearUndoHistory();
    p.macroChannels()[0].store(given);
    p.recordEdit("Choose a macro's channels",
                 [&p] { p.macroChannels()[0].store(1u << 6); });
    p.undo().undo();

    check(p.macroChannels()[0].load() == given,
          "a change of channels alone can be undone");

    p.undo().clearUndoHistory();
    p.applyFactoryPreset(presetIndex("Init"));

    // Put back as it was before this block, for the curve below.
    set(ovt::params::macroRowId(0), (float)tuneRow);
    set(ovt::params::macroAmountId(0), -0.25f);
  }

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
  // Which is how one is removed: the row goes back to None and the amount stays
  // wherever the fader was.
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
  // The fifth, which is harmonics 3, 6, 12 and 24. Counted against the table,
  // so this cannot drift from what the mixer calls those channels.
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
  // The window works out which control belongs to which macro and tells the
  // strips. Read off the control itself, since the look and feel draws whatever
  // colour it has been given.
  {
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

    check(editor != nullptr, "the editor opens");

    if (editor != nullptr) {
      // The macro's colour is carried beside the control's own rather than
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

      // ---- and the bar follows the macros, not the tool ------------------
      //
      // Making a macro lights the button without the tool menu being touched.
      // Read off the button's name, which carries the count for a screen
      // reader.
      {
        juce::TextButton *bar = nullptr;

        std::function<void(juce::Component &)> look = [&](juce::Component &c) {
          for (auto *child : c.getChildren()) {
            if (auto *b = dynamic_cast<juce::TextButton *>(child))
              if (b->getTitle().startsWith("Macros"))
                bar = b;

            look(*child);
          }
        };
        look(*editor);

        check(bar != nullptr, "the bar carries a macros button");

        if (bar != nullptr) {
          set(ovt::params::macroRowId(0), 0.0f);
          editor->followMacroTints();

          check(!bar->getToggleState(), "which is dark with no macros made (" +
                                            bar->getTitle().toStdString() +
                                            ")");

          // The macro alone, with nothing touching the tool.
          set(ovt::params::macroRowId(0), (float)tuneRow);
          editor->followMacroTints();

          check(bar->getToggleState(),
                "and lights as soon as one is made, without the tool being "
                "touched (" +
                    bar->getTitle().toStdString() + ")");

          set(ovt::params::macroRowId(0), 0.0f);
          editor->followMacroTints();

          check(!bar->getToggleState(), "and goes out when the last one goes");
        }
      }

      // ---- and the ring shows what the engine is playing ----------------
      //
      // The ring is worked out by the window and the offset by the snapshot,
      // from the same arithmetic, and they must agree. The decay row rather
      // than tune, because decay is built by logRange and tune is linear. A
      // macro that moved a proportion of the units instead of the travel, or a
      // cached range rebuilt from its two ends, passes on a linear row and is
      // wrong by orders of magnitude here.
      const int decayRow = 9;

      set(ovt::params::macroRowId(0), (float)decayRow);
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
            *editor, ovt::params::oscParamId(ovt::params::decaySuffix, i));

        if (c == nullptr)
          continue;

        const auto shown = (float)(double)c->getProperties().getWithDefault(
            "macroResult", -1.0);

        auto *q =
            dynamic_cast<juce::RangedAudioParameter *>(p.apvts.getParameter(
                ovt::params::oscParamId(ovt::params::decaySuffix, i)));

        if (q == nullptr)
          continue;

        // Both as a proportion of the control's travel, which is what the
        // ring is drawn from.
        const auto engine = q->convertTo0to1(played.osc[(size_t)i].decay);

        if (std::abs(shown - engine) > 1.0e-3f)
          ++mismatched;
      }

      check(mismatched == 0,
            "the ring shows the value the engine is playing (" +
                std::to_string(mismatched) + " channels differ)");

      // ---- and follows the control, not only the macro ------------------
      //
      // The result is the patch's value plus the macro's offset, so turning a
      // knob moves the ring while every macro stands still.
      {
        auto *first =
            dynamic_cast<juce::RangedAudioParameter *>(p.apvts.getParameter(
                ovt::params::oscParamId(ovt::params::decaySuffix, 0)));

        auto *control = ovt::ui::learn::controlFor(
            *editor, ovt::params::oscParamId(ovt::params::decaySuffix, 0));

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

/// Every factory preset has a case of its own, applies, and sounds.
void testPresets(OvertoniumProcessor &p) {
  section("Factory presets");

  const auto names = ovt::presets::names();

  // Counted rather than spelt. Every name needs a case of its own, which is
  // what the loop below tests: a name with no case applies nothing and the
  // preset comes out silent.
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

/// Pressure, polyphonic aftertouch and the mod wheel reach the voices according
/// to Aftertouch from, without eating the pedal or the panic messages.
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

  // Both back to rest before the mirror image, since a wheel left at 127 would
  // make the next checks pass for the wrong reason.
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
/// Checks the routing: a message on channel N reaches the voice it was meant
/// for and no other, an ordinary keyboard still plays with the setting on, and
/// nothing changes with it off. The engine side is in the DSP suite.
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

  // ---- off: the channel means nothing -------------------------------------
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
  // Notes on the master channel are notes of the master channel, so the setting
  // does not silence a keyboard that knows nothing about it.
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
    // A panic leaves the wheel where it was, and the block above pushed it to
    // the top. Master bend reaches every note, so it is centred here to keep
    // the baseline unbent.
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
  // The parser takes CC 64 for itself, so the pool never sees it in this mode,
  // and the parser has to defer the release. Getting it wrong is a pedal that
  // does nothing or a note released twice.
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
  // The voices sounding were started through entry points the other mode cannot
  // reach, so without a release on the way through they would hold for ever.
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
/// Two things keep them cheap and both are checked: the lamps hold still
/// between steps instead of following their value exactly, and their bands are
/// merged by row rather than by neighbour.
void testActivityLamps(OvertoniumProcessor &p) {
  section("Strip lamps");

  using namespace ovt::ui;

  // ---- merging by row -----------------------------------------------------
  //
  // The lamps all sit at the same four heights, so the union of a row of them
  // is the pixels they occupy plus the gaps between strips.
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
  // Fixed to what the knobs can reach, so the constants the needle reads are
  // held against the ranges themselves.
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
    // jump. The other wanderer still must not peg it on its own.
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
    strip.setActivity(0.5f, 0.5f, 0.0f, 0.0f, 0.0f, bands);
    const auto first = bands.size();

    bands.clearQuick();
    strip.setActivity(0.5f, 0.5f, 0.0f, 0.0f, 0.0f, bands);

    check(first > 0, "a lamp that lights asks to be repainted");
    check(bands.isEmpty(), "and the same values again ask for nothing (" +
                               std::to_string(bands.size()) + " bands)");

    // A move too small to cross a step is a move nobody can see.
    bands.clearQuick();
    strip.setActivity(0.5f + 1.0f / (4.0f * ActivityLamp::kSteps), 0.5f, 0.0f,
                      0.0f, 0.0f, bands);

    check(bands.isEmpty(), "nor does a change smaller than one step");

    // A move of a whole step does.
    bands.clearQuick();
    strip.setActivity(0.5f + 1.5f / (float)ActivityLamp::kSteps, 0.5f, 0.0f,
                      0.0f, 0.0f, bands);

    check(!bands.isEmpty(), "a change of a whole step does");

    // ---- the two envelope lamps never both light --------------------------
    bands.clearQuick();
    strip.setActivity(-0.8f, 0.0f, 0.0f, 0.0f, 0.0f, bands);
    strip.setActivity(-0.8f, 0.0f, 0.0f, 0.0f, 0.0f, bands);

    // Reading the lamps back is not possible from here, so this is checked by
    // the shape of the request instead: flipping the sign has to move both
    // lamps, one out and one in, and nothing else.
    bands.clearQuick();
    strip.setActivity(0.8f, 0.0f, 0.0f, 0.0f, 0.0f, bands);

    check(bands.size() == 2,
          "flipping to the key-off half moves exactly two lamps, one out and "
          "one in (" +
              std::to_string(bands.size()) + ")");

    // ---- the two output lamps are gated on the note like the rest --------
    //
    // The velocity half needs the gate most: it reads full when the row is
    // taking nothing, so ungated it would sit lit on every channel of a silent
    // mixer.
    bands.clearQuick();
    strip.setActivity(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, bands);

    bands.clearQuick();
    strip.setActivity(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, bands);

    check(bands.isEmpty(),
          "a strip with nothing sounding keeps both output lamps dark, the "
          "velocity one included (" +
              std::to_string(bands.size()) + " bands)");

    // With a note under it each row moves its own lamp and only its own.
    bands.clearQuick();
    strip.setActivity(0.8f, 0.0f, 0.0f, 0.0f, 0.0f, bands);

    bands.clearQuick();
    strip.setActivity(0.8f, 0.0f, 0.0f, 1.0f, 0.0f, bands);

    check(bands.size() == 1,
          "with a note under it the velocity row moves exactly one lamp (" +
              std::to_string(bands.size()) + ")");

    // Which one, not just how many. The velocity lamp is the left half of the
    // rule and the pressure lamp the right, and swapping what they are fed
    // would leave every count above right.
    const auto velocityBand = bands.getFirst();

    bands.clearQuick();
    strip.setActivity(0.8f, 0.0f, 0.0f, 1.0f, 1.0f, bands);

    check(bands.size() == 1,
          "and the pressure row moves exactly the other one (" +
              std::to_string(bands.size()) + ")");

    const auto pressureBand = bands.getFirst();
    const auto middle = strip.getWidth() / 2;

    check(velocityBand.getCentreX() < middle &&
              pressureBand.getCentreX() > middle,
          "with velocity on the left of the rule and aftertouch on the right "
          "(" +
              std::to_string(velocityBand.getCentreX()) + " and " +
              std::to_string(pressureBand.getCentreX()) + " about " +
              std::to_string(middle) + ")");
  }
}

/// The STRIKE popup, which is the only reading on the panel worked out from
/// more than one control.
void testStrikeReadout() {
  section("Strike readout");

  using namespace ovt;

  // Off: nothing is done to the onset, so the reading is the value alone.
  check(params::strikeRangeText(0.0f, 0.0f, 0.005f) == "0 %",
        "at nought the reading is the percentage alone (" +
            params::strikeRangeText(0.0f, 0.0f, 0.005f).toStdString() + ")");

  // The case the control exists for: a short attack at a full amount.
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

  // The panel and the engine have to agree, so this is held against the
  // arithmetic the voice itself runs.
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

/// The seven-segment readouts under each channel and elsewhere on the panel.
///
/// A segment display shows an unlit digit for a character it cannot draw, so
/// every reading the panel can produce is held against what the display can
/// draw.
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

  // Three on the bar (the two converter readouts and the preset name), two on
  // each of the thirty-two channels, the level on the noise strip, and one per
  // macro in the panel. Then five in the Playing bar (the voices, the voices in
  // use, bend range, aftertouch and slide) and sixteen in the Tuning bar (the
  // temperament, root, reference, the twelve pitch classes and the drift). The
  // macro and bar displays are built whether or not they are in use.
  check(displays.size() ==
            3 + 2 * ovt::kNumHarmonics + 1 + ovt::params::kNumMacros + 5 + 16,
        "every readout is a segment display now (" +
            std::to_string(displays.size()) + ")");

  bool allDrawable = true;
  std::string firstBad;

  // Against its own cells. The preset name is on a fourteen-bar display, which
  // spells words a seven-bar one has no forms for.
  for (auto *d : displays)
    for (int i = 0; i < d->getReading().length(); ++i)
      if (!SegmentDisplay::canDraw((char)d->getReading()[i],
                                   d->howManyBars())) {
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
  check(level.getReading() == "-inF" && level.isActive(),
        "and all the way down it reads -inF, lit, a segment being lit or not "
        "rather than dimmer (" +
            level.getReading().toStdString() + ")");

  setVolume(0, 0.5f);
  check(level.getReading() == "-6.0" && level.isActive(),
        "half way is -6.0 dB (" + level.getReading().toStdString() + ")");

  // The readout counts down to the quietest figure the cells can hold rather
  // than giving up partway.
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
  check(atDb(-100.5f) == "-inF" && level.isActive(),
        "quieter than the floor reads -inF, lit (" +
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
  // The travel is spaced by decibels, so the quiet end has room rather than
  // being crushed into the last few pixels.
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

  // -66 dB sits well up the travel rather than in the bottom few pixels.
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

  check(cents.getReading() == "0.0" && cents.isActive(),
        "the fundamental has no cents to report, and says 0.0 (" +
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

  check(!readingFor(2, 1.0f).containsChar('+'),
        "and the display is never asked for one");

  // ---- a sign sharing the leading digit's cell -----------------------------
  //
  // A reading that runs to three digits and can be negative has a hundreds
  // place that is only ever a one or nothing, so the sign shares that cell
  // rather than taking one of its own. The macro panel's amount is the only
  // reading that does this.
  //
  // Checked by rendering the same number signed and unsigned: everything but
  // the first cell has to be identical, or the digits have shifted to make room
  // for the sign. Counting lit pixels would not do, since the ground is lit
  // too.
  const auto renderOf = [](const juce::String &reading) {
    ovt::ui::SegmentDisplay d({});
    d.setBounds(0, 0, 92, 30);
    d.setReading(reading, true);

    return d.createComponentSnapshot(d.getLocalBounds());
  };

  const auto signed_ = renderOf("~42.0");
  const auto unsigned_ = renderOf(" 42.0");

  int differing = 0;
  int to = -1;

  for (int y = 0; y < signed_.getHeight(); ++y)
    for (int x = 0; x < signed_.getWidth(); ++x)
      if (signed_.getPixelAt(x, y) != unsigned_.getPixelAt(x, y)) {
        ++differing;
        to = juce::jmax(to, x);
      }

  check(differing > 0, "a sign is drawn in the leading cell (" +
                           std::to_string(differing) + " pixels differ)");

  // And the sign sits in the middle of that cell when nothing shares it,
  // measured against a lit eight in the same cell, which marks the cell's
  // extent.
  const auto centreOf = [](const juce::Image &im) {
    int first = -1, last = -1;

    // The leading cell only, and inside the recess, whose own lit edge runs
    // round the perimeter. A cell is a quarter of the run and the run does not
    // fill the box, so the first cell ends a little past a quarter and short of
    // three tenths.
    for (int x = 0; x < im.getWidth() * 3 / 10; ++x) {
      bool any = false;

      for (int y = 4; y < im.getHeight() - 4 && !any; ++y)
        any = im.getPixelAt(x, y).getBlue() > 120;

      if (any) {
        if (first < 0)
          first = x;

        last = x;
      }
    }

    return first < 0 ? 0.0 : (first + last) / 2.0;
  };

  const auto cellCentre = centreOf(renderOf("830.0"));
  const auto signCentre = centreOf(signed_);

  check(std::abs(cellCentre - signCentre) < 1.0,
        "and sits in the middle of it (cell at " + std::to_string(cellCentre) +
            ", sign at " + std::to_string(signCentre) + ")");

  // Nothing past that cell, which is the first of four in a centred run, so
  // half the box is well clear of it and well short of the second digit.
  check(differing > 0 && to < signed_.getWidth() / 2,
        "and nothing beyond it moves to make room (last at " +
            std::to_string(to) + " of " + std::to_string(signed_.getWidth()) +
            ")");

  // And the cell still shows the hundreds digit when there is one. The sign is
  // the middle bar and the one is the pair of uprights, both drawn like any
  // segment.
  const auto hundred = renderOf("!00.0");
  const auto none = renderOf("~00.0");

  int gained = 0;

  for (int y = 0; y < hundred.getHeight(); ++y)
    for (int x = 0; x < hundred.getWidth(); ++x)
      gained += hundred.getPixelAt(x, y) != none.getPixelAt(x, y) ? 1 : 0;

  check(gained > 0, "and the one appears beside it at a hundred (" +
                        std::to_string(gained) + " pixels differ)");

  check(readingFor(6, 1.0f) == "-31.2",
        "the seventh harmonic being the one that goes the other way (" +
            readingFor(6, 1.0f).toStdString() + ")");
}

/// That the preset display can spell.
///
/// The name is on a fourteen-bar display, since seven bars cannot spell a name
/// like Glockenspiel. This holds that a hyphen is drawn as a letter rather than
/// taken as a sign, and that every character has a shape of its own.
void testThePresetDisplaySpells(OvertoniumProcessor &) {
  section("The preset display spells");

  using namespace ovt::ui;

  // Every name the instrument ships, which is where an unspellable character
  // would show up first.
  const auto names = ovt::presets::names();

  int unspellable = 0;
  juce::String firstBad;

  for (const auto &name : names)
    for (int i = 0; i < name.length(); ++i)
      if (!SegmentDisplay::canDraw((char)name[i],
                                   SegmentDisplay::Bars::Fourteen)) {
        ++unspellable;
        if (firstBad.isEmpty())
          firstBad = name;
      }

  check(unspellable == 0,
        "every factory name has a form for all of its characters (" +
            std::to_string(unspellable) + ", first in " +
            firstBad.toStdString() + ")");

  // ---- and fits every one of them to its nine cells ------------------------
  //
  // Spaces from the right, then vowels, then a cut, and never the first
  // character. Held against the names the instrument ships, since those are
  // what anyone will see.
  struct Fitted {
    const char *name;
    const char *shown;
  };

  const Fitted fitted[] = {
      // Shorter than the display, so it stands as it is with the rest of the
      // cells left unlit.
      {"Big Saw", "BIG SAW  "},
      {"Lo-fi", "LO-FI    "},
      {"Init", "INIT     "},

      // Exactly nine, which is the case that must not be touched.
      {"Cathedral", "CATHEDRAL"},
      {"Equal Saw", "EQUAL SAW"},

      // The space goes and nothing else has to.
      {"Tape Choir", "TAPECHOIR"},
      {"Tape Pluck", "TAPEPLUCK"},
      {"Vibraphone", "VIBRAPHON"},

      // The space, then vowels from the right.
      {"Glockenspiel", "GLOCKNSPL"},
      {"Glass Armonica", "GLASSRMNC"},
      {"Metallic Piano", "METALLCPN"},
      {"Odd Harmonics", "ODDHRMNCS"},
      {"Struck Bell", "STRUCKBLL"},
      {"8-bit Choir", "8-BITCHOR"},
      {"Combo Organ", "COMBOORGN"},

      // And a cut on top of both, which is the longest name there is.
      {"2-bit Fuzz Organ", "2-BTFZZRG"},
      {"KarplusStrong", "KRPLSSTRN"},
      {"Synth Ensemble", "SYNTHNSMB"},
  };

  int wrong = 0;
  juce::String firstWrong;

  for (const auto &f : fitted) {
    const auto got = SegmentDisplay::squeeze(f.name, 9);

    if (got != f.shown) {
      ++wrong;
      if (firstWrong.isEmpty())
        firstWrong = juce::String(f.name) + " -> " + got;
    }
  }

  check(wrong == 0, "every name is fitted to its cells as it should be (" +
                        std::to_string(wrong) + " wrong, " +
                        firstWrong.toStdString() + ")");

  // Every name comes out at exactly the cell count, long or short, which is
  // what keeps the letters the same size whatever is loaded.
  int misfits = 0;

  for (const auto &name : names)
    misfits += SegmentDisplay::squeeze(name, 9).length() == 9 ? 0 : 1;

  check(misfits == 0, "and every factory name comes out at nine cells (" +
                          std::to_string(misfits) + " did not)");

  // What the squeeze leaves has to be spellable too, since a cut landing
  // mid-name could expose a character the whole-name check above did not reach.
  int unshowable = 0;

  for (const auto &name : names) {
    const auto shown = SegmentDisplay::squeeze(name, 9);

    for (int i = 0; i < shown.length(); ++i)
      unshowable += SegmentDisplay::canDraw((char)shown[i],
                                            SegmentDisplay::Bars::Fourteen)
                        ? 0
                        : 1;
  }

  check(unshowable == 0, "and what is left of it is still spellable (" +
                             std::to_string(unshowable) + ")");

  // The display draws all of its cells, the unlit ones included, so INIT on
  // nine cells is four letters and five dark cells.
  const auto cellsShown = [](const juce::String &reading) {
    SegmentDisplay d({}, SegmentDisplay::Bars::Fourteen, 9);

    // The size the bar actually gives it. See TopBar::placeGroup.
    d.setBounds(0, 0, 132, 24);
    d.setReading(reading, true);
    d.createComponentSnapshot(d.getLocalBounds());

    return d.cellsDrawn();
  };

  check(cellsShown("Init") == 9 && cellsShown("Glockenspiel") == 9,
        "and a short name leaves the rest of the cells standing (" +
            std::to_string(cellsShown("Init")) + ")");

  // ---- every character is its own shape ------------------------------------
  //
  // A table of fourteen flags per character can give two characters the same
  // bars, and a render tells them apart whatever the table says. A bar that is
  // on, as against the ground or an unlit bar, which are both drawn: the accent
  // is around 206 against the 25 of the ground and the 35 to 59 of an unlit
  // bar.
  const auto isLit = [](juce::Colour px) { return px.getBlue() > 120; };

  const auto fingerprint = [&isLit](char c) {
    // One cell, so each glyph has the whole display to be told apart in.
    SegmentDisplay d({}, SegmentDisplay::Bars::Fourteen, 1);
    d.setBounds(0, 0, 40, 40);
    d.setReading(juce::String::charToString((juce::juce_wchar)c), true);

    const auto shot = d.createComponentSnapshot(d.getLocalBounds());
    juce::String bits;

    for (int y = 0; y < shot.getHeight(); ++y)
      for (int x = 0; x < shot.getWidth(); ++x)
        bits += isLit(shot.getPixelAt(x, y)) ? '1' : '0';

    return bits;
  };

  const juce::String alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  std::map<juce::String, char> seen;
  int collisions = 0;
  juce::String pair;

  for (int i = 0; i < alphabet.length(); ++i) {
    const auto c = (char)alphabet[i];
    const auto print = fingerprint(c);

    if (const auto found = seen.find(print); found != seen.end()) {
      // S and 5 are the same shape on every seven-bar display, so the pair is
      // allowed by name rather than loosening the rule for everything.
      const auto both =
          juce::String::charToString((juce::juce_wchar)found->second) +
          juce::String::charToString((juce::juce_wchar)c);

      if (both == "S5" || both == "5S")
        continue;

      ++collisions;
      if (pair.isEmpty())
        pair = juce::String::charToString((juce::juce_wchar)found->second) +
               " and " + juce::String::charToString((juce::juce_wchar)c);
    } else {
      seen[print] = c;
    }
  }

  check(collisions == 0, "and no two of the thirty-six draw the same (" +
                             std::to_string(collisions) + ", " +
                             pair.toStdString() + ")");
}

/// That a silenced channel loses the light out of its controls and not out of
/// its mute, which has to stay readable since it is usually what silenced the
/// channel.
void testSilencedChannel(OvertoniumProcessor &p) {
  section("A silenced channel");

  using namespace ovt::ui;

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  ChannelStrip *strip = nullptr;

  std::function<void(juce::Component &)> findStrip = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (strip == nullptr)
        if (auto *s = dynamic_cast<ChannelStrip *>(child))
          strip = s;

      findStrip(*child);
    }
  };

  findStrip(*editor);

  check(strip != nullptr, "and has channel strips in it");
  if (strip == nullptr)
    return;

  // Counts the two kinds apart, since they have to answer differently.
  const auto survey = [](juce::Component &c) {
    struct Count {
      int controlsUnlit = 0, controlsLit = 0, pairUnlit = 0;
    } n;

    std::function<void(juce::Component &)> walk = [&](juce::Component &in) {
      for (auto *child : in.getChildren()) {
        const bool unlit =
            (bool)child->getProperties().getWithDefault("unlit", false);

        if (dynamic_cast<MuteSoloButton *>(child) != nullptr) {
          n.pairUnlit += unlit ? 1 : 0;
        } else if (dynamic_cast<juce::Slider *>(child) != nullptr ||
                   dynamic_cast<ShapeButton *>(child) != nullptr) {
          (unlit ? n.controlsUnlit : n.controlsLit) += 1;
        }

        walk(*child);
      }
    };

    walk(c);
    return n;
  };

  const auto heard = survey(*strip);

  check(heard.controlsLit > 20 && heard.controlsUnlit == 0,
        "an audible channel has every control lit (" +
            std::to_string(heard.controlsLit) + " lit, " +
            std::to_string(heard.controlsUnlit) + " not)");

  strip->setSilencedByOthers(true);

  const auto cut = survey(*strip);

  check(cut.controlsUnlit == heard.controlsLit && cut.controlsLit == 0,
        "a silenced one has the light out of all of them (" +
            std::to_string(cut.controlsUnlit) + " of " +
            std::to_string(heard.controlsLit) + ")");

  check(cut.pairUnlit == 0, "and its mute and solo keep theirs");

  // The strip itself is not dimmed, since that would dim the pair with it.
  check(std::abs(strip->getAlpha() - 1.0f) < 0.001f,
        "with nothing washed over the strip as a whole (" +
            std::to_string(strip->getAlpha()) + ")");

  strip->setSilencedByOthers(false);

  const auto back = survey(*strip);

  check(back.controlsLit == heard.controlsLit && back.controlsUnlit == 0,
        "and it all comes back when the channel is audible again");

  // ---- and a macro does not relight it --------------------------------------
  //
  // Read off a render, since the ring is painted in either the macro's colour
  // or the control's own, and the silenced state has to win over both.
  const auto set = [&p](const juce::String &id, float v) {
    if (auto *q = dynamic_cast<juce::RangedAudioParameter *>(
            p.apvts.getParameter(id)))
      q->setValueNotifyingHost(q->convertTo0to1(v));
  };

  // Green, which is nowhere on the channel ramp: that runs blue to yellow and
  // never enters green or cyan, so any green pixel in a strip is the macro's.
  constexpr int green = 4;

  set(ovt::params::macroRowId(0), 1.0f);
  set(ovt::params::macroScopeId(0), (float)(int)ovt::params::MacroScope::All);
  set(ovt::params::macroColourId(0), (float)green);
  set(ovt::params::macroAmountId(0), 0.8f);
  editor->followMacroTints();

  const auto wanted = ovt::params::macroColour(green);

  const auto macroPixels = [&wanted](juce::Component &c) {
    const auto shot = c.createComponentSnapshot(c.getLocalBounds());
    int n = 0;

    for (int y = 0; y < shot.getHeight(); ++y)
      for (int x = 0; x < shot.getWidth(); ++x) {
        const auto px = shot.getPixelAt(x, y);

        if (std::abs(px.getRed() - wanted.getRed()) < 30 &&
            std::abs(px.getGreen() - wanted.getGreen()) < 30 &&
            std::abs(px.getBlue() - wanted.getBlue()) < 30)
          ++n;
      }

    return n;
  };

  const auto driven = macroPixels(*strip);

  check(driven > 0, "a macro paints its colour onto an audible channel (" +
                        std::to_string(driven) + " pixels)");

  strip->setSilencedByOthers(true);

  const auto drivenAndCut = macroPixels(*strip);

  check(drivenAndCut == 0, "and none of it onto a silenced one (" +
                               std::to_string(drivenAndCut) + " pixels)");

  strip->setSilencedByOthers(false);
  set(ovt::params::macroRowId(0), 0.0f);
  editor->followMacroTints();
}

/// That the mute and the solo share one moulding: the two buttons touch, and
/// each knows which half of the pair it is. A wrong answer still lays out,
/// clicks and lights, so nothing else would catch it.
void testMuteSoloGang(OvertoniumProcessor &p) {
  section("The mute and solo gang");

  using namespace ovt::ui;
  using LAF = OvertoniumLookAndFeel;

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1348, 160);

  // Every pair in the window, including the noise channel's, which lays its own
  // out.
  std::vector<std::pair<MuteSoloButton *, MuteSoloButton *>> pairs;

  std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
    MuteSoloButton *first = nullptr;

    for (auto *child : c.getChildren()) {
      if (auto *b = dynamic_cast<MuteSoloButton *>(child)) {
        if (first == nullptr)
          first = b;
        else
          pairs.emplace_back(first, b);
      }

      gather(*child);
    }
  };

  gather(*editor);

  check(pairs.size() >= 33,
        "every channel has a pair (" + std::to_string(pairs.size()) + ")");

  int apart = 0;
  int unganged = 0;
  int overlapping = 0;

  for (const auto &[mute, solo] : pairs) {
    if (mute->getBounds().getRight() != solo->getBounds().getX())
      ++apart;

    if (LAF::lampGangOf(*mute) != LAF::LampGang::Left ||
        LAF::lampGangOf(*solo) != LAF::LampGang::Right)
      ++unganged;

    // The caps themselves, where the half wall each gives up has to come out as
    // one divider rather than an overlap or a gap.
    const auto left =
        LAF::lampCapBounds(mute->getBounds().toFloat(), LAF::LampGang::Left);
    const auto right =
        LAF::lampCapBounds(solo->getBounds().toFloat(), LAF::LampGang::Right);

    const auto divider = right.getX() - left.getRight();
    const auto wall = left.getY() - (float)mute->getBounds().getY();

    if (divider < wall - 0.01f || divider > wall + 0.01f)
      ++overlapping;
  }

  check(apart == 0, "the two halves touch, so one moulding covers both (" +
                        std::to_string(apart) + " apart)");
  check(unganged == 0, "and each knows which half it is (" +
                           std::to_string(unganged) + " wrong)");
  check(overlapping == 0,
        "leaving a divider the width of the moulding around them (" +
            std::to_string(overlapping) + " wrong)");
}

/// The channel highlight, which marks the column the pointer is on.
///
/// Crossed with the row band, it shows which knob a drag would move. Each strip
/// works it out from where the pointer is, so this checks that the answer
/// follows the pointer and that exactly one channel ever claims it.
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
  // answer has to come out the same in either order, which is why each strip
  // reads the pointer rather than the order of events.
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
  // The menu it opens is modal, so the strip is never told the pointer has
  // left. The highlight has to go out anyway.
  //
  // Driven through a mute button, which returns before any menu is shown. A
  // plain right-click on the strip opens the LINK menu, which needs a screen.
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
      // LINK menu. Without this the suite would crash rather than fail, since a
      // modal menu needs a screen.
      check(juce::Component::getCurrentlyModalComponent() == nullptr,
            "and the strip opens no menu of its own over the button's");

      // And it has to stay let go: the exit JUCE sends when a menu takes the
      // mouse arrives while the pointer is still inside the strip.
      lit.mouseExit(pointAt(lit, inside));

      check(!lit.isHovered() && litCount() == 0,
            "and an exit arriving with the pointer still inside does not "
            "light it again (" +
                std::to_string(litCount()) + ")");

      // The pointer coming back ends it, so the channel answers the mouse again
      // after a menu.
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
/// A parameter the generated code fails to mention is left wherever neutralBase
/// put it, so the test runs the generated code the way the compiler would and
/// checks the patch comes back.
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

  // Everything the generated case would do, read back out of the text it wrote.
  // Anything it did not say is a parameter left at neutralBase.
  //
  // Three forms, since the generator writes a row the whole series shares as
  // one allOsc and a dragged row as one oscTable.
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

  // ...and it must not carry the session across, such as a temperament set in
  // Settings.
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

/// A user preset carries exactly what a factory preset carries.
///
/// A factory preset decides what neutralBase() touches, and a user preset
/// stores every parameter capture() walks. The two sets have to match, or a
/// control moves when the other kind of preset loads. The session settings
/// belong to neither set.
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
  // Anything it writes lands on the same value twice, and anything it leaves
  // alone keeps its starting end.
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
/// The block is cut into pieces the scratch already holds rather than growing
/// it on the audio thread. The same notes at the same sample positions have to
/// come out the same whether they arrived in one block or several.
void testOversizedBlocks(OvertoniumProcessor &p) {
  section("Oversized blocks");

  // A sustained patch with nothing random in it. Drift is redrawn once per
  // control block, and cutting a block moves where control blocks fall, so a
  // patch with drift cannot be compared sample for sample.
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

  // The same block, from a host that promised far less.
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

/// The LINK curves and scopes: every curve leaves things alone at zero, keeps
/// the dragged strip at its full share, stays in range, and moves the faders in
/// decibels.
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

  // Grabbing either end gives a straight ramp across the whole mixer, one way
  // or the other.
  check(taper(0, 0) > 0.99f && taper(31, 0) < 0.01f,
        "grabbing the first channel tilts the mixer down across its width");
  check(taper(31, 31) > 0.99f && taper(0, 31) < 0.01f,
        "grabbing the last channel tilts it up instead");

  // Grabbing anywhere between peaks at the grab.
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
  // A level fader's travel is shaped for the hand, so an even move across it is
  // an uneven move in level. A uniform drag on the faders means the same number
  // of decibels on every channel, which leaves the balance of a patch alone.
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

/// Which row the pointer is on, which caption names it, and which LINK role
/// each row carries.
void testRowHover() {
  section("Row hover");

  using namespace ovt::ui;

  // The rectangle a strip lays its rows out in, at the height
  // preferredStripHeight asks for, so a new row cannot push the last one off
  // the bottom unnoticed.
  const auto rows =
      layoutRows(juce::Rectangle<int>(0, 0, kStripWidth, preferredStripHeight())
                     .reduced(kStripPadX, kStripPadY));

  const auto rowAtCentre = [&rows](Row r) {
    const auto band = rows[(size_t)r];
    return controlRowAt(rows, {kStripWidth / 2, band.getCentreY()});
  };

  bool identity = true;
  for (Row r : {Row::TuneKnob,   Row::Glide,   Row::PmRate,   Row::PmDepth,
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

/// Presets of your own: file names, capture and restore, files from another
/// build, and the factory code generator.
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

  // ---- a value that is not a number -----------------------------------------
  {
    // A range clamps an infinity, but a NaN passes through it to the audio.
    for (const char *text : {"nan", "inf", "-inf"}) {
      juce::XmlElement odd("OVERTONIUM_PRESET");
      auto *entry = odd.createNewChildElement("PARAM");
      entry->setAttribute("id", level->paramID);
      entry->setAttribute("value", text);

      presets::restore(p.apvts, odd);

      check(std::isfinite(level->getValue()),
            std::string("a file holding ") + text +
                " leaves the parameter a number");
    }

    juce::XmlElement nan("OVERTONIUM_PRESET");
    auto *entry = nan.createNewChildElement("PARAM");
    entry->setAttribute("id", level->paramID);
    entry->setAttribute("value", "nan");

    const auto before = level->getValue();
    presets::restore(p.apvts, nan);

    check(std::abs(level->getValue() - before) < 1.0e-9f,
          "and a NaN is skipped, not applied");
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

/// The echo, the reverb and the resonator ring on after a note, the tail the
/// host is told covers them, and Init puts them away again.
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

  // The resonator rings on after the note too, ahead of the effects, so the
  // tail is the resonator's and the effects' together.
  setParam(ovt::params::resonatorModeId,
           (float)(int)ovt::ResonatorMode::Octave);
  setParam(ovt::params::resonatorMixId, 1.0f);
  setParam(ovt::params::resonatorDecayId, 6.0f);

  const auto strung = tailAfterNote();
  check(strung > 1.0e-4f, "the resonator rings on after the note "
                          "(peak " +
                              std::to_string(strung) + ")");

  check(p.getTailLengthSeconds() > 5.0,
        "the reported tail covers the resonator (" +
            std::to_string(p.getTailLengthSeconds()) + " s)");

  setParam(ovt::params::resonatorModeId, (float)(int)ovt::ResonatorMode::Off);

  check(tailAfterNote() < 1.0e-5f, "switching them off takes them away");

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  p.reset();
}

/// The level meter repaints only the band that changed, and a frame that
/// reports nothing to repaint really did not change.
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

/// The LINK menu as data: its sections and items, the ticks, what choosing an
/// item does, and how the curve is saved.
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
  // The switch, the scopes, clearing the selection and the curves.
  check(items == 1 + (int)LinkScope::NumScopes + 1 + (int)LinkCurve::NumCurves,
        "the switch, every scope, clearing the selection and every curve are "
        "all there (" +
            std::to_string(items) + ")");

  // Exactly the two current settings are ticked, and nothing else.
  check(ticked == 2, "two items are ticked");
  check(tickedNames == "Odd harmonics Taper from the grab ",
        "and they are the current ones: " + tickedNames);

  // With LINK off the lists are shown but greyed, so the menu does not change
  // shape as the switch moves.
  check(enabled == 1, "with LINK off only the switch itself can be picked");

  settings.enabled = true;
  settings.hasSelection = true;
  int enabledOn = 0;
  auto onMenu = buildLinkMenu(settings);

  for (juce::PopupMenu::MenuItemIterator it(onMenu); it.next();)
    if (it.getItem().itemID != 0 && it.getItem().isEnabled &&
        !it.getItem().isSectionHeader)
      ++enabledOn;

  check(enabledOn == items,
        "with LINK on and a selection made, every item can be picked");

  check(isClearSelectionChoice(2) && !isClearSelectionChoice(1) &&
            !applyLinkMenuChoice(2, settings),
        "clearing the selection is its own choice, not one of LINK's "
        "settings");

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
  // The curve is written by name, since an index written by an older version
  // may mean a different curve.
  bool names = true;
  for (int i = 0; i < (int)LinkCurve::NumCurves; ++i)
    names &= linkCurveFromState(linkCurveId((LinkCurve)i), -1) == (LinkCurve)i;

  check(names, "every curve round-trips through its saved name");

  check(linkCurveFromState("", -1) == LinkCurve::Uniform,
        "a state with no curve at all opens on Uniform");
  check(linkCurveFromState("not a curve", -1) == LinkCurve::Uniform,
        "and so does a name this version does not know");

  // The old list ran Uniform, Tilt up, Tilt down, Spread. Both tilts map to
  // Taper, and Spread moves down a place.
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

/// The top bar's height against the window's width: a sensible minimum, at most
/// three rows, and never taller as the window grows.
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

  check(tallest <= 3 * 55 + 2 * 4 + 5 + 6,
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

/// The preset menu is two submenus and the actions, not one long list.
///
/// Checks that the top level stays short whatever is saved, that every factory
/// preset is one level down, and that a folder in the preset directory becomes
/// a group.
void testPresetMenuGroups(OvertoniumProcessor &p) {
  section("Preset menu");

  using namespace ovt::ui;

  // Real files in the real preset folder, since that is what the menu reads.
  // Named so nothing anyone has saved could collide with them, and removed at
  // the end.
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
/// getCurrentProgram answers zero from the start, so a host shows the first
/// entry as selected on an instance that has never loaded it. Picking that
/// entry has to load it.
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

/// That the tool a window was left on is the one the next window opens with.
///
/// The tool is derived from two flags rather than stored, LINK's own switch and
/// whether drawing is latched, so remembering the tool means writing both.
void testTheToolOutlivesTheWindow(OvertoniumProcessor &) {
  section("The tool outlives the window");

  using ovt::ui::PointerTool;

  OvertoniumProcessor proc;

  // What a window left on this tool reports when the next one opens, with the
  // session carried between them the way a host carries it.
  const auto reopenedOn = [&proc](PointerTool chosen) {
    {
      std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
      auto *editor = dynamic_cast<OvertoniumEditor *>(ed.get());

      if (editor == nullptr)
        return PointerTool::NumTools;

      sizeEditor(*editor, 1340);
      editor->chooseTool(chosen);
    }

    juce::MemoryBlock carried;
    proc.getStateInformation(carried);
    proc.setStateInformation(carried.getData(), (int)carried.getSize());

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(ed.get());

    if (editor == nullptr)
      return PointerTool::NumTools;

    sizeEditor(*editor, 1340);
    return editor->currentTool();
  };

  const auto name = [](PointerTool t) {
    return t == PointerTool::NumTools
               ? juce::String("(no window)")
               : juce::String(ovt::ui::pointerToolName(t));
  };

  // Every one of the three, in an order that leaves LINK in the session before
  // the other two are asked for.
  for (auto chosen : {PointerTool::Link, PointerTool::Pointer,
                      PointerTool::Draw, PointerTool::Pointer}) {
    const auto got = reopenedOn(chosen);

    check(got == chosen, "a window left on " + name(chosen).toStdString() +
                             " opens on it again (" + name(got).toStdString() +
                             ")");
  }

  // ---- and after LINK's own settings have been touched ---------------------
  //
  // Changing LINK's scope or curve writes its switch through the settings
  // callback, so choosing another tool afterwards has to write it again.
  {
    {
      std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
      auto *editor = dynamic_cast<OvertoniumEditor *>(ed.get());

      check(editor != nullptr, "a window opens to leave LINK's mark in");

      if (editor != nullptr) {
        sizeEditor(*editor, 1340);
        editor->chooseTool(PointerTool::Link);

        if (auto *bar = findTopBar(*editor))
          if (bar->onLinkSettingsChanged != nullptr)
            bar->onLinkSettingsChanged();

        editor->chooseTool(PointerTool::Pointer);
      }
    }

    juce::MemoryBlock carried;
    proc.getStateInformation(carried);
    proc.setStateInformation(carried.getData(), (int)carried.getSize());

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto *editor = dynamic_cast<OvertoniumEditor *>(ed.get());

    if (editor != nullptr) {
      sizeEditor(*editor, 1340);

      check(editor->currentTool() == PointerTool::Pointer,
            "and choosing another tool afterwards is what the next one opens "
            "on (" +
                name(editor->currentTool()).toStdString() + ")");
    }
  }
}

/// The preset button survives the window being shut.
///
/// The name lives on the processor, since a session restored from disk has no
/// window and a preset of your own has no program index for the editor to find
/// it by.
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
/// The repaint is scheduled through a ParameterAttachment and cannot be
/// observed here, with no peer and no message loop. What this holds is that the
/// button never caches the shape, so a value moved by a host or a preset is
/// what it draws.
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

  // And a factory preset. Every preset that ships leaves both modulators on
  // sine.
  p.applyFactoryPreset(ovt::presets::names().indexOf("Drawbar Organ"));

  check(shown() == "Sine", "and loading a preset puts it back to sine (" +
                               shown().toStdString() + ")");

  // On a channel nobody can hear the screen is off: two different shapes
  // draw the same dark ground, where lit they differ.
  ShapeButton *first = nullptr;

  for (auto *b : buttons)
    if (b->getTitle().contains("Harmonic 1 pitch"))
      first = b;

  if (first != nullptr) {
    const auto picture = [first, param](float shape, bool unlit) {
      param->setValueNotifyingHost(param->convertTo0to1(shape));
      first->getProperties().set("unlit", unlit);
      return first->createComponentSnapshot(first->getLocalBounds());
    };

    const auto same = [](const juce::Image &a, const juce::Image &b) {
      if (a.getBounds() != b.getBounds())
        return false;

      for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
          if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
            return false;

      return true;
    };

    check(!same(picture(0.0f, false), picture(4.0f, false)),
          "a lit screen shows which shape is chosen");
    check(same(picture(0.0f, true), picture(4.0f, true)),
          "and on a silenced channel the screen is off, whatever the shape");

    first->getProperties().set("unlit", false);
    param->setValueNotifyingHost(param->convertTo0to1(0.0f));
  }
}

/// A preset tells the host what changed, and nothing else.
///
/// A preset is written as a neutral base plus the rows that differ, which sent
/// as decided is over a thousand parameter changes, most overwritten or not
/// changes at all. The applier collects and sends once, skipping anything
/// already at the value asked for. Held here as a property: no parameter is
/// written twice, and nothing is reported that did not move.
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

  // The preset already loaded, where nothing should be reported.
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
/// so the blend does nothing on partials 1, 2, 4, 8, 16 and 32. The strip says
/// so on those and gives the cent figure on the rest.
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
/// A global switch reached from a per-channel menu, so this checks that the
/// pitch button and the amplitude button reach different switches. Both menus
/// would still tick and untick if they did not.
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
      if (it.getItem().text == "Shared by all notes")
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

/// What the SETTINGS menu is, and the order it says it in. The menu is
/// reachable only by clicking, so this is the only check on its grouping.
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

  // The version heads the list as a heading of its own, asked for by the same
  // call the menu builds it from.
  const auto wanted = TopBar::versionLine().toStdString() +
                      " / Polyphony / Pitch bend range / Expression / "
                      "Tuning / Output / ";

  check(joined(headers) == wanted,
        "the sections read in order (" + joined(headers) + ")");

  const auto at = [&entries](const std::string &name) {
    const auto it = std::find(entries.begin(), entries.end(), name);
    return it == entries.end() ? -1 : (int)(it - entries.begin());
  };

  // Legato is a tick beside the voice count, and the list's old first entry is
  // not offered: a session that stored it reads as one voice with the tick on.
  check(at("Legato") == at("No stack") + 1 && at("1 voice") >= 0,
        "Legato sits beside No stack, under the voice counts");
  check(std::count(entries.begin(), entries.end(), std::string("Legato")) == 1,
        "and is there once, as the switch rather than as a voice count");

  // The adaptive temperament is offered with the eight fixed ones, last, and
  // the root stays live for it, since its versions are placed around the
  // root. The menu lists the historical ones by age, while the parameter keeps
  // the order they were added in, so a session's stored choice keeps the
  // temperament it always named.
  {
    std::vector<std::string> temperaments;
    bool rootLive = false;

    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
      const auto &item = it.getItem();
      if (item.subMenu == nullptr)
        continue;
      if (item.text == "Temperament")
        for (juce::PopupMenu::MenuItemIterator sub(*item.subMenu); sub.next();)
          temperaments.push_back(sub.getItem().text.toStdString());
      if (item.text == "Root")
        rootLive = item.isEnabled;
    }

    check(joined(temperaments) ==
              "Equal / Just (major) / Pythagorean / Quarter-comma meantone / "
              "Werckmeister III / Vallotti / Kirnberger III / Young / "
              "Adaptive / ",
          "the Temperament menu lists the fixed ones by age and ends with "
          "Adaptive (" +
              joined(temperaments) + ")");

    if (auto *t = p.apvts.getParameter(ovt::params::temperamentId)) {
      t->setValueNotifyingHost(
          t->convertTo0to1((float)(int)ovt::Temperament::Adaptive));
      auto adaptiveMenu = bar.buildSettingsMenu();
      for (juce::PopupMenu::MenuItemIterator it(adaptiveMenu); it.next();)
        if (it.getItem().text == "Root")
          rootLive = it.getItem().isEnabled;
      t->setValueNotifyingHost(t->convertTo0to1(0.0f));
    }

    check(rootLive, "and with Adaptive chosen the Root menu stays live");

    // Retune is about Adaptive and nothing else, so it is live only there.
    const auto liveUnder = [&bar, &p](const juce::String &entry,
                                      int temperament) {
      auto *t = p.apvts.getParameter(ovt::params::temperamentId);
      t->setValueNotifyingHost(t->convertTo0to1((float)temperament));
      auto built = bar.buildSettingsMenu();
      bool live = false, seen = false;
      for (juce::PopupMenu::MenuItemIterator it(built); it.next();)
        if (it.getItem().text == entry) {
          seen = true;
          live = it.getItem().isEnabled;
        }
      t->setValueNotifyingHost(t->convertTo0to1(0.0f));
      return seen ? (live ? 1 : 0) : -1;
    };

    check(liveUnder("Retune", (int)ovt::Temperament::Adaptive) == 1 &&
              liveUnder("Retune", (int)ovt::Temperament::Just) == 0,
          "Retune is offered, and live only under Adaptive");
    check(liveUnder("Harmonic sevenths", (int)ovt::Temperament::Adaptive) ==
                  1 &&
              liveUnder("Harmonic sevenths", (int)ovt::Temperament::Just) == 0,
          "and so is Harmonic sevenths");
    check(liveUnder("No drift", (int)ovt::Temperament::Adaptive) == 1 &&
              liveUnder("No drift", (int)ovt::Temperament::Just) == 0,
          "and No drift");
  }

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
  // voice counts. Bounded by entries rather than headers, since the headers are
  // collected separately above.
  check(at("No stack") > at("16 voices") && at("No stack") < at("0 semitones"),
        "No stack sits with the voice counts (" +
            std::to_string(at("No stack")) + ")");
}

/// That Fit all 32 channels brings back a window that was left narrow and
/// short.
void testFitAllChannels(OvertoniumProcessor &p) {
  section("Fitting the mixer back in");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // The 8 is the gap before the noise channel and the 10 is the parameter
  // scrollbar beyond it, both private to the editor.
  const auto wanted = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 + 10 +
                      ovt::kNumHarmonics * ovt::ui::kStripWidth;

  // The size to come back to, taken from the editor, since the height follows
  // whatever is folded away. An editor opens at the size left in the state by
  // earlier tests, so it is fitted once first.
  editor->fitAllChannels();

  const auto fitted = editor->getBounds();

  check(fitted.getWidth() == wanted,
        "the window fits all 32 channels across (" +
            std::to_string(fitted.getWidth()) + ")");

  check(fitted.getHeight() > 600, "and the strips are their full height (" +
                                      std::to_string(fitted.getHeight()) + ")");

  // Dragged narrow and short, narrow enough that the bar reflows onto two rows,
  // so the height has to come back as well as the width.
  editor->setSize(700, fitted.getHeight() - 120);

  check(editor->getWidth() == 700, "a window can be dragged narrow (" +
                                       std::to_string(editor->getWidth()) +
                                       ")");

  // Short, and allowed to be: the rows scroll, so the floor is far below this
  // and the height asked for is taken.
  check(editor->getHeight() == fitted.getHeight() - 120,
        "and as short as it was asked for (" +
            std::to_string(editor->getHeight()) + ")");

  // setSize does not consult the constrainer, so the floor is not checked here.
  // It is checked in testFittingLandsWhereADragCanReturn.

  editor->fitAllChannels();

  check(editor->getBounds() == fitted,
        "and comes back to exactly the size it was (" +
            std::to_string(editor->getWidth()) + " x " +
            std::to_string(editor->getHeight()) + ")");

  editor->fitAllChannels();

  check(editor->getBounds() == fitted,
        "asking twice changes nothing the second time");
}

/// The machine menus say what is running.
///
/// The echo and the reverb each have an on switch and a type behind one button,
/// so a tick that read only one of them could say Off while a plate was
/// audible. This checks the pairing from the parameters the host writes to the
/// menu the player reads.
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

  // ---- the resonator, one parameter with Off among its modes ---------------
  {
    const auto resonatorButton = [bar]() -> juce::Button * {
      for (auto *c : bar->getChildren())
        if (c->getTitle().startsWith("Resonator:"))
          return dynamic_cast<juce::Button *>(c);

      return nullptr;
    };

    for (int i = 0; i < (int)ovt::ResonatorMode::NumModes; ++i) {
      write(ovt::params::resonatorModeId, (float)i);
      bar->updatePanelReadouts(48000.0);

      auto menu = bar->buildResonatorMenu();
      const juce::String wanted = ovt::resonatorModeName((ovt::ResonatorMode)i);
      auto *button = resonatorButton();

      check(tickedIn(menu) == wanted.toStdString(),
            "the resonator's menu names " + wanted.toStdString() +
                " when that is the mode (" + tickedIn(menu) + ")");

      check(button != nullptr &&
                button->getButtonText() == wanted.toUpperCase() &&
                button->getToggleState() == (i != 0),
            "and its button says " + wanted.toUpperCase().toStdString() +
                (i != 0 ? ", lit" : ", unlit"));
    }

    write(ovt::params::resonatorModeId, 0.0f);
    bar->updatePanelReadouts(48000.0);
  }

  // ---- and the output stage -------------------------------------------------
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
  // The button is 62 px with a label area of 46, so it carries short forms of
  // the shape names, and this checks they fit. Measured, since JUCE shaves the
  // ends of a label that overflows and it still looks like a word.
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

/// The Settings menu says which build this is, as the version the plugin was
/// built as. The bug report template asks people to read it off this menu.
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

  // A heading rather than an item, since an ordinary item reads as something to
  // press.
  check(items[0].isSectionHeader,
        "as a heading rather than as something to click");

  // Directly under it, since the two belong together: the build you have, and
  // whether to look for a newer one.
  check(items[1].text == "Check for new versions",
        "with the update check directly beneath it");

  // Below both. The version does not change while you use the plugin, so it
  // reads first and the undo pair follows.
  check(items[2].text == "Undo" && items[3].text == "Redo",
        "and undo and redo below, not above");
}

/// A wheel over a knob moves the knob and does not also scroll the mixer
/// sideways, when the window is narrow enough for the series to scroll.
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

  // And then what JUCE does next: every ancestor holding a deep mouse listener
  // is handed the same wheel after the target, whether the target took it or
  // not. The strip holds one so a pointer on a knob is reported by the strip.
  // See MouseListenerList::sendMouseEvent.
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
  // A wheel the strip is handed is not simply swallowed. The plain
  // wheel moves the parameters, and shift is free because a wheel over a fader
  // belongs to the fader and never reaches here.
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

/// The same for the three bars above the mixer. Each listens deeply for
/// right-clicks, so it hears every wheel its knobs take as well, and must not
/// hand those on to the editor to scroll the mixer or the parameters.
void testWheelOverABarKnobStaysOnTheKnob(OvertoniumProcessor &p) {
  section("A wheel over a knob in a bar does not scroll the mixer");

  p.applyFactoryPreset(presetIndex("Init"));

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Narrow and short enough that there is somewhere to scroll both ways.
  editor->setSize(900, 700);

  juce::Viewport *viewport = nullptr;
  ovt::ui::ChannelStrip *strip = nullptr;
  ovt::ui::TopBar *top = nullptr;
  ovt::ui::PlayingBar *playing = nullptr;
  ovt::ui::TuningBar *tuning = nullptr;
  juce::Button *playingButton = nullptr, *tuningButton = nullptr;

  std::function<void(juce::Component &)> look = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *v = dynamic_cast<juce::Viewport *>(child))
        viewport = v;
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(child))
        strip = strip == nullptr ? s : strip;
      if (auto *b = dynamic_cast<ovt::ui::TopBar *>(child))
        top = b;
      if (auto *b = dynamic_cast<ovt::ui::PlayingBar *>(child))
        playing = b;
      if (auto *b = dynamic_cast<ovt::ui::TuningBar *>(child))
        tuning = b;
      if (auto *b = dynamic_cast<juce::Button *>(child)) {
        if (b->getTitle() == "Playing")
          playingButton = b;
        if (b->getTitle() == "Tuning")
          tuningButton = b;
      }
      look(*child);
    }
  };
  look(*editor);

  check(viewport != nullptr && strip != nullptr && top != nullptr &&
            playing != nullptr && tuning != nullptr &&
            playingButton != nullptr && tuningButton != nullptr,
        "the editor has a mixer, a top bar and both bars");
  if (viewport == nullptr || strip == nullptr || top == nullptr ||
      playing == nullptr || tuning == nullptr || playingButton == nullptr ||
      tuningButton == nullptr)
    return;

  // A knob in each bar that can take the wheel: macro 1 made, so its knob
  // is live, and Adaptive, which is when RETUNE is shown.
  const auto setParam = [&p](const juce::String &id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };
  setParam(ovt::params::macroRowId(0), 1.0f);
  setParam(ovt::params::temperamentId, (float)ovt::Temperament::Adaptive);

  // Called rather than clicked, since a click needs a window to land in.
  playingButton->onClick();
  tuningButton->onClick();
  playing->refresh();
  tuning->refresh();

  const auto span =
      viewport->getViewedComponent()->getWidth() - viewport->getViewWidth();
  viewport->setViewPosition(span / 2, 0);

  // A control in the strip to follow up and down. See
  // testTheWheelScrollsTheParameters.
  juce::Slider *marker = nullptr;
  for (auto *child : strip->getChildren())
    if (auto *s = dynamic_cast<juce::Slider *>(child))
      if (marker == nullptr)
        marker = s;

  check(span > 0 && marker != nullptr && marker->isVisible(),
        "and something to scroll sideways, and a control to follow");
  if (marker == nullptr)
    return;

  // Every event a distinct moment, since a slider ignores a second wheel
  // stamped with the time of the last one.
  auto when = juce::Time::getCurrentTime();

  const auto eventFor = [&when](juce::Component &target,
                                juce::Component &original,
                                juce::Point<float> at) {
    when = when + juce::RelativeTime::milliseconds(50);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            at, juce::ModifierKeys(),
                            juce::MouseInputSource::defaultPressure,
                            juce::MouseInputSource::defaultOrientation,
                            juce::MouseInputSource::defaultRotation,
                            juce::MouseInputSource::defaultTiltX,
                            juce::MouseInputSource::defaultTiltY, &target,
                            &original, when, at, when, 1, false);
  };

  /// The first knob in a bar that takes the wheel, turned by it, and then the
  /// same wheel handed to the bar the way JUCE hands it to a deep listener.
  /// @returns whether the knob moved.
  const auto wheelOver = [&](juce::Component &bar, float dx, float dy) {
    juce::Slider *knob = nullptr;
    std::function<void(juce::Component &)> find = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *s = dynamic_cast<juce::Slider *>(child))
          if (knob == nullptr && s->isVisible() && !s->getBounds().isEmpty() &&
              s->isScrollWheelEnabled() && s->isRotary())
            knob = s;
        find(*child);
      }
    };
    find(bar);

    if (knob == nullptr)
      return false;

    // Towards the middle of its travel, so a push either way moves it.
    knob->setValue(0.5 * (knob->getMinimum() + knob->getMaximum()),
                   juce::sendNotificationSync);
    const auto before = knob->getValue();

    juce::MouseWheelDetails wheel{};
    wheel.deltaX = dx;
    wheel.deltaY = dy;

    const auto centre = knob->getLocalBounds().getCentre().toFloat();
    knob->mouseWheelMove(eventFor(*knob, *knob, centre), wheel);
    bar.mouseWheelMove(eventFor(bar, *knob, bar.getLocalPoint(knob, centre)),
                       wheel);

    return std::abs(knob->getValue() - before) > 1.0e-9;
  };

  const struct {
    juce::Component *bar;
    const char *name;
  } bars[] = {{top, "the top bar"},
              {playing, "the Playing bar"},
              {tuning, "the Tuning bar"}};

  for (const auto &b : bars) {
    for (const bool sideways : {true, false}) {
      const auto x = viewport->getViewPositionX();
      const auto y = marker->getY();
      const bool turned = sideways ? wheelOver(*b.bar, -0.4f, 0.0f)
                                   : wheelOver(*b.bar, 0.0f, -0.4f);

      // A row scrolled off the top is hidden rather than moved, so either
      // counts as the parameters having scrolled.
      const bool rowsStill = marker->isVisible() && marker->getY() == y;

      check(turned && viewport->getViewPositionX() == x && rowsStill,
            std::string("a ") + (sideways ? "sideways" : "vertical") +
                " wheel over a knob in " + b.name +
                " turns the knob and leaves the mixer where it was (view " +
                std::to_string(x) + " to " +
                std::to_string(viewport->getViewPositionX()) + ", row " +
                std::to_string(y) + " to " + std::to_string(marker->getY()) +
                ")");
    }
  }

  // A wheel on the top bar's own panel still goes on to the mixer.
  {
    viewport->setViewPosition(span / 2, 0);
    const auto x = viewport->getViewPositionX();
    juce::MouseWheelDetails wheel{};
    wheel.deltaX = -0.4f;
    top->mouseWheelMove(eventFor(*top, *top, {2.0f, 2.0f}), wheel);

    check(viewport->getViewPositionX() != x,
          "and a sideways wheel on the top bar's own panel still scrolls the "
          "mixer");
  }

  // Closed again, since which bars are open is kept with the session and the
  // editors opened after this one would otherwise find them open.
  playingButton->onClick();
  tuningButton->onClick();
  setParam(ovt::params::macroRowId(0), 0.0f);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// One right-click opens one LINK menu.
///
/// The strip listens to everything inside it, so a right-click on a knob opens
/// the LINK menu rather than being swallowed by the control. JUCE hands such a
/// listener the component's own events too, so a click on the strip's own
/// background, such as the gap between sections, arrives twice and has to open
/// one menu.
///
/// Counted through a stub, since a real PopupMenu needs a window.
void testOneRightClickOpensOneMenu(OvertoniumProcessor &p) {
  section("One right-click, one LINK menu");

  struct CountingLink final : ovt::ui::LinkTarget {
    int opened = 0;

    bool isLinkEnabled() const override { return true; }
    void linkDragStarted(ovt::ui::Role, int) override {}
    void linkValueChanged(ovt::ui::Role, int, float) override {}
    void linkDragEnded(ovt::ui::Role, int) override {}
    void showLinkMenu(const juce::String &) override { ++opened; }
    // Nowhere to scroll, so the wheel falls through.
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

/// A click on the rule between two sections folds that section, as the gutter's
/// headings do, so the fold is reachable without leaving the column.
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
  // The rules are the one part of a strip that can be clicked without carrying
  // a control, so they light their caption in the gutter like any row.
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
  // The rules have nothing standing on them, so a right-click there reaches the
  // strip. One button each.
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

/// Energy at one frequency, by Goertzel, normalised by the sample count.
///
/// Enough to ask whether the note is where it should be, which a sample rate
/// fault gets wrong while every level and finiteness check passes.
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
/// Every column scrolls together, so the gutter's captions stay beside their
/// knobs.
void testTheWheelScrollsTheParameters(OvertoniumProcessor &p) {
  section("The wheel scrolls the parameters");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Short enough that not every row fits, and well above the floor. Set
  // outright rather than through sizeEditor, whose usual slack would give a
  // window where nothing scrolls.
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

  // One named control as the marker. The top of the band is always at the same
  // height, so the first visible slider never moves however far it scrolls.
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

  // And every column with it, or a caption names the wrong knob. The gutter and
  // the noise channel lay out the same rows from the same scroll, so they have
  // to agree on where a row starts.
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
    // So the topmost visible control in each has to sit at the same height.
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

  // Back up again, and the top row returns. Scrolling past either end has to be
  // a no-op rather than a drift.
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
/// A trackpad sends one as deltaX with deltaY near zero, and the vertical
/// handler must not take it. On a screen too narrow for 32 channels that is the
/// only way across.
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
/// under it, and the gutter's cap has to repeat the divider down its right
/// edge. Read off a render, since the fault is a colour in one band of pixels.
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

/// The three bands lay a column out exactly as one column would.
///
/// At or above the preferred height nothing scrolls, the rows run from the top
/// of the strip to the bottom without a gap or an overlap, and the fader takes
/// the slack.
///
/// Below it the fader keeps its floor and the rows scroll, so the fader is
/// never pushed off the bottom.
void testTheBandsLayOutAColumn() {
  section("The bands lay out a column");

  using namespace ovt::ui;

  // Every fold state, since the bands are worked out from the fold mask.
  int states = 0, scrolling = 0;

  for (SectionMask mask = 0; mask < (1 << kNumSections); ++mask) {
    // The rows' own height: the bands are laid out inside the strip's
    // padding, which preferredStripHeight includes.
    const int preferred = preferredStripHeight(mask) - 2 * kStripPadY;

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

  // The shortest window the bands allow, which the resize limits are built
  // from. It does not have to fit every row at once.
  const int shortest = minimumStripHeight(0);
  const auto squeezed = layoutBands({0, 0, kStripWidth, shortest}, 0);

  check(squeezed.middle.getHeight() >= 120,
        "the band keeps its four rows at the shortest window (" +
            std::to_string(squeezed.middle.getHeight()) + ")");
  check(squeezed.maxScroll > 0, "and scrolls there");

  // ---- what scrolling looks like from the outside -------------------------
  //
  // Two properties no height or total would catch.
  {
    const juce::Rectangle<int> area(0, 0, kStripWidth, 600);
    const auto bands = layoutBands(area, 0);

    check(bands.maxScroll > 0, "a 600px column has somewhere to scroll");

    int runs = 0, gaps = 0, overhang = 0;

    for (int scroll = 0; scroll <= bands.maxScroll; scroll += 17) {
      const auto rows = layoutRows(area, 0, scroll);

      // The rows on screen have to be one unbroken run in layout order, so a
      // row too tall to fit is not skipped for a shorter one behind it.
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

      // And the run has to reach the foot of the band rather than stopping
      // short when the next row is too tall for what is left.
      if (lastBottom < bands.middle.getBottom())
        ++gaps;

      if (lastBottom > bands.middle.getBottom())
        ++overhang;
    }

    check(runs == 0, "the visible rows are one unbroken run at every scroll (" +
                         std::to_string(runs) + " broken)");
    check(gaps == 0, "and always reach the foot of the band (" +
                         std::to_string(gaps) + " short)");
    // Dragging the bar is a plain pixel offset, so a row can sit part way over
    // the top of the band, which the column's header cap exists to cover.
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
/// A juce::Image on JUCE 9 under Windows is backed by Direct2D, and a static
/// image is released under the loader lock when a host unloads the binary,
/// which hangs. So the tile is built again after the last look and feel goes,
/// rather than surviving it.
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

  // If this reads equal, the tile survived the look and feel that was holding
  // it, which is the state that hangs a host on unload.
  check(ovt::ui::grainTileBuildCount() == afterFirst + 1,
        "and built afresh for the next one, having been released with the "
        "last (" +
            std::to_string(ovt::ui::grainTileBuildCount()) + " builds)");

  // Two at once share one.
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
/// A file that is our XML with nonsense in the values gets through to the
/// parameters. NaN passes every clamp in JUCE's range handling, since both
/// comparisons are false against it, and would reach the oscillators and output
/// NaN for the rest of the session.
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

    // If this reads zero the test is passing on an empty state rather than a
    // poisoned one.
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

/// The instrument plays at every rate a host can ask for.
///
/// Every delay line in the effects sizes itself from the sample rate. Finite
/// output from a plugin that rendered silence proves nothing, so this also
/// checks the preset sounds at every rate, at the same level and in tune.
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

      // And in tune, which finiteness and loudness cannot see: an engine that
      // assumed 48 kHz would render a good note two octaves down at 192 kHz.
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

        // Measured across every rate, the fundamental runs 150 to 400 times the
        // subharmonic and 12 to 17 times the fourth harmonic, so these leave a
        // factor of three either way.
        check(at > 20.0 * below,
              "and middle C at " + std::to_string((int)rate) +
                  " Hz is not two octaves flat (" + std::to_string(at) +
                  " against " + std::to_string(below) + ")");
        check(at > 4.0 * above,
              "nor two octaves sharp (" + std::to_string(above) + ")");
      }

      // 8 kHz is left out of the level comparison, since most of the series is
      // above its Nyquist and correctly not there.
      if (rate > 20000.0) {
        lowest = std::min(lowest, (double)stats.peak);
        highest = std::max(highest, (double)stats.peak);
      }
    }
  }

  // Measured at 0.457 to 0.462 across 22 kHz to 192 kHz, so a tenth is slack
  // rather than a target.
  check(highest < lowest * 1.1,
        "and the same patch is the same loudness at every rate above 20 kHz (" +
            std::to_string(lowest) + " to " + std::to_string(highest) + ")");
}

/// The safety clip holds the output to unity on every clip type, since that
/// bound is its whole job.
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

    // Every parameter at its maximum would include the envelope's delay and
    // attack, and nothing would sound inside the render. The master at +12 dB
    // over a patch that already sounds, with several notes, is what makes it
    // loud.
    set(ovt::params::masterGainId, 1.0f);
    set(ovt::params::safetyClipId, 1.0f);

    // Every one of the eight. The limiter, and Console, Cassette and Print,
    // which end in it, keep the bound by seeing the peak coming, with a hard
    // clip behind it for whatever the window is too short to see.
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

  // Without this the two checks above pass on silence.
  check(sounded == 3, "and it was driven hard enough to mean it (peak " +
                          std::to_string(worst) + ")");
}

/// Fit all 32 channels shows every row, with nothing left to scroll, and is
/// the shortest window that does: a pixel shorter and the rows scroll. Asked
/// with each combination of the two bars open, since each adds chrome.
void testFittingShowsEveryRow(OvertoniumProcessor &p) {
  section("Fitting shows every row");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  juce::ScrollBar *rowsBar = nullptr;
  juce::Button *playing = nullptr, *tuning = nullptr;

  std::function<void(juce::Component &)> look = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // The rows' scrollbar is the vertical one that does not belong to the
      // mixer's viewport.
      if (auto *s = dynamic_cast<juce::ScrollBar *>(child))
        if (s->isVertical() && dynamic_cast<juce::Viewport *>(&c) == nullptr)
          rowsBar = s;
      if (auto *b = dynamic_cast<juce::Button *>(child)) {
        if (b->getTitle() == "Playing")
          playing = b;
        if (b->getTitle() == "Tuning")
          tuning = b;
      }
      look(*child);
    }
  };
  look(*editor);

  check(rowsBar != nullptr && playing != nullptr && tuning != nullptr,
        "and has a scrollbar for the rows and both bar buttons");
  if (rowsBar == nullptr || playing == nullptr || tuning == nullptr)
    return;

  const auto setOpen = [](juce::Button *b, bool open) {
    if (b->getToggleState() != open)
      b->onClick();
  };

  const auto scrolls = [rowsBar] {
    return rowsBar->getCurrentRangeSize() <
           rowsBar->getMaximumRangeLimit() - rowsBar->getMinimumRangeLimit();
  };

  for (int bars = 0; bars < 4; ++bars) {
    setOpen(playing, (bars & 1) != 0);
    setOpen(tuning, (bars & 2) != 0);
    editor->fitAllChannels();

    const auto fitted = editor->getHeight();
    const auto fits = !scrolls();

    editor->setSize(editor->getWidth(), fitted - 1);
    const auto tighter = scrolls();

    check(fits && tighter,
          std::string("with ") + ((bars & 1) ? "Playing" : "no Playing bar") +
              " and " + ((bars & 2) ? "Tuning" : "no Tuning bar") +
              ", the fitted window shows every row and a pixel less does "
              "not (" +
              std::to_string(fitted) + " px)");
  }

  setOpen(playing, false);
  setOpen(tuning, false);
}

/// Fit all 32 channels lands somewhere a drag can get back to.
///
/// setSize consults no limits, so the fit could put the window below the
/// shortest one the constrainer allows. The floor follows the width, since the
/// top bar takes more rows on a narrow window, so a wide window can be dragged
/// shorter than a narrow one.
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

  // It must still be a floor: narrow the window to where the bar needs another
  // row and it has to rise.
  const int fittedWidth = editor->getWidth();

  editor->setSize(700, editor->getHeight());

  const int narrowFloor = limits->getMinimumHeight();

  check(
      narrowFloor > wideFloor,
      "and narrowing it, where the bar takes another row, raises the floor (" +
          std::to_string(wideFloor) + " to " + std::to_string(narrowFloor) +
          ")");

  // The window has to rise with it, or the bar gains a row into space the mixer
  // is still using and the strips run off the bottom.
  check(editor->getHeight() >= narrowFloor,
        "and takes the window up with it rather than leaving it short");

  // Back out again, and the fit's height is no longer the shortest available,
  // since the floor follows the width.
  editor->setSize(fittedWidth, editor->getHeight());

  check(limits->getMinimumHeight() == wideFloor,
        "widening it puts the floor back");
  check(limits->getMinimumHeight() < fitted,
        "so a wide window can be dragged shorter than fitting leaves it");
}

/// Folding and unfolding a section puts the window back where it was.
///
/// With the window shrunk so the faders are squeezed, folding a section leaves
/// the faders their height, and unfolding it again must not make them taller
/// than they were. The window's height is the rows plus what is left for the
/// fader, so a fader that grew means the window grew by more than the rows it
/// got back.
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

  // Squeezed down to the shortest the window will go, where the fader has given
  // up everything it can. Asked of the constrainer, since setSize on its own
  // does not go through it and a host's drag does.
  auto *limits = editor->getConstrainer();

  check(limits != nullptr, "and has resize limits to squeeze it against");
  if (limits == nullptr)
    return;

  editor->setSize(editor->getWidth(), limits->getMinimumHeight());

  const auto squeezed = editor->getHeight();

  // A second apart, since a strip throws away a click bearing the same time as
  // the one before it, which is how the echo of its own listener is told from a
  // real click.
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

  // The rows scroll, so folding shows more at once and leaves the window alone.
  check(folded == squeezed, "folding leaves the window where it is (" +
                                std::to_string(squeezed) + " to " +
                                std::to_string(folded) + ")");

  check(back == squeezed, "and unfolding puts it back, not past it (" +
                              std::to_string(squeezed) + " to " +
                              std::to_string(back) + ")");

  // ---- and from a window with room to spare ------------------------------
  //
  // The window holds still at both heights here too.
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

/// The Playing and Tuning bars, opened from the top bar.
///
/// Both start closed, open and close from their buttons, light the button
/// while open, come back open in a reopened window, and take their height from
/// the mixer rather than from the window, unless the window is already as short
/// as it can be.
void testOptionBars(OvertoniumProcessor &p) {
  section("The Playing and Tuning bars");

  check(ovt::ui::OptionBar::kHeight == ovt::ui::TopBar::heightForWidth(2600),
        "a bar is as tall as one row of the top bar (" +
            std::to_string(ovt::ui::OptionBar::kHeight) + " and " +
            std::to_string(ovt::ui::TopBar::heightForWidth(2600)) + ")");

  const auto open = [&p] {
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    sizeEditor(*editor, 1348, 240);
    return editor;
  };

  struct Parts {
    juce::TextButton *settings = nullptr, *macros = nullptr, *playing = nullptr,
                     *tuning = nullptr;
    ovt::ui::PlayingBar *playingBar = nullptr;
    ovt::ui::TuningBar *tuningBar = nullptr;
    ovt::ui::ChannelStrip *strip = nullptr;
    ovt::ui::TopBar *top = nullptr;
  };

  const auto partsOf = [](juce::Component &editor) {
    Parts parts;
    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *b = dynamic_cast<juce::TextButton *>(child)) {
          if (b->getButtonText() == "SETTINGS")
            parts.settings = b;
          if (b->getTitle().startsWith("Macros"))
            parts.macros = b;
          if (b->getTitle() == "Playing")
            parts.playing = b;
          if (b->getTitle() == "Tuning")
            parts.tuning = b;
        }
        if (auto *bar = dynamic_cast<ovt::ui::PlayingBar *>(child))
          parts.playingBar = bar;
        if (auto *bar = dynamic_cast<ovt::ui::TuningBar *>(child))
          parts.tuningBar = bar;
        if (auto *strip = dynamic_cast<ovt::ui::ChannelStrip *>(child))
          if (parts.strip == nullptr)
            parts.strip = strip;
        if (auto *top = dynamic_cast<ovt::ui::TopBar *>(child))
          parts.top = top;
        look(*child);
      }
    };
    look(editor);
    return parts;
  };

  auto editor = open();
  auto parts = partsOf(*editor);

  check(parts.settings != nullptr && parts.macros != nullptr &&
            parts.playing != nullptr && parts.tuning != nullptr &&
            parts.playingBar != nullptr && parts.tuningBar != nullptr &&
            parts.strip != nullptr && parts.top != nullptr,
        "the top bar has PLAYING and TUNING buttons and the editor both bars");

  if (parts.playing == nullptr || parts.tuning == nullptr ||
      parts.playingBar == nullptr || parts.tuningBar == nullptr ||
      parts.strip == nullptr || parts.top == nullptr ||
      parts.settings == nullptr || parts.macros == nullptr)
    return;

  // ---- two by two, in the space the first two had ----------------------
  {
    const auto s = parts.settings->getBounds(), m = parts.macros->getBounds(),
               pl = parts.playing->getBounds(), t = parts.tuning->getBounds();

    check(s.getX() == pl.getX() && m.getX() == t.getX() &&
              s.getY() == m.getY() && pl.getY() == t.getY() &&
              pl.getY() > s.getBottom() && s.getRight() < m.getX(),
          "SETTINGS and MACROS stand over PLAYING and TUNING");
    check(s.getWidth() == pl.getWidth() && m.getWidth() == t.getWidth() &&
              s.getHeight() == pl.getHeight() && s.getHeight() >= 22,
          "all four the same size, " + std::to_string(s.getWidth()) + " by " +
              std::to_string(s.getHeight()));
    check(parts.top->getHeight() == ovt::ui::TopBar::heightForWidth(1348),
          "and the top bar is no taller for them");
  }

  // ---- closed to begin with ----------------------------------------------
  check(!parts.playingBar->isVisible() && !parts.tuningBar->isVisible() &&
            !parts.playing->getToggleState() && !parts.tuning->getToggleState(),
        "both bars start closed, with their buttons dark");

  const auto size = editor->getBounds();
  const auto stripHeight = parts.strip->getHeight();
  const auto zoom = (float)editor->getWidth() / (float)parts.top->getWidth();
  const auto barHeight = (float)ovt::ui::OptionBar::kHeight;

  // ---- opening one, then the other -----------------------------------------
  parts.playing->onClick();

  check(parts.playingBar->isVisible() && parts.playing->getToggleState() &&
            !parts.tuningBar->isVisible() && !parts.tuning->getToggleState(),
        "PLAYING opens its bar and lights");
  check(parts.playingBar->getY() == parts.top->getBottom() &&
            parts.playingBar->getHeight() == ovt::ui::OptionBar::kHeight,
        "straight under the top bar");
  check(editor->getBounds() == size &&
            parts.strip->getHeight() ==
                stripHeight - ovt::ui::OptionBar::kHeight,
        "and takes its height from the mixer, the window holding still (" +
            std::to_string(stripHeight) + " to " +
            std::to_string(parts.strip->getHeight()) + ")");

  parts.tuning->onClick();

  check(parts.tuningBar->isVisible() && parts.tuning->getToggleState() &&
            parts.tuningBar->getY() == parts.playingBar->getBottom(),
        "TUNING opens its bar under the Playing one");

  // ---- a reopened window has them both ------------------------------------
  editor.reset();
  editor = open();
  parts = partsOf(*editor);

  check(parts.playingBar != nullptr && parts.tuningBar != nullptr &&
            parts.playingBar->isVisible() && parts.tuningBar->isVisible() &&
            parts.playing->getToggleState() && parts.tuning->getToggleState(),
        "a reopened window comes back with both bars open and lit");

  // ---- closing puts the mixer back ------------------------------------------
  const auto withBoth = parts.strip->getHeight();
  parts.playing->onClick();
  parts.tuning->onClick();

  check(!parts.playingBar->isVisible() && !parts.tuningBar->isVisible() &&
            !parts.playing->getToggleState() &&
            !parts.tuning->getToggleState() &&
            parts.strip->getHeight() ==
                withBoth + 2 * ovt::ui::OptionBar::kHeight,
        "closing both gives the mixer its height back");

  // ---- at the shortest a window can be, the window grows
  // ---------------------
  sizeEditor(*editor, 1348);
  const auto shortest = editor->getHeight();
  const auto floor = editor->getConstrainer()->getMinimumHeight();

  parts.tuning->onClick();

  const auto grown = editor->getHeight() - shortest;
  const auto raised = editor->getConstrainer()->getMinimumHeight() - floor;

  check(std::abs((float)raised - barHeight * zoom) <= 1.0f &&
            std::abs((float)grown - barHeight * zoom) <= 1.0f,
        "a window already at its shortest grows by the bar, and so does the "
        "floor (" +
            std::to_string(grown) + " and " + std::to_string(raised) + ")");

  parts.tuning->onClick();
  check(editor->getConstrainer()->getMinimumHeight() == floor,
        "and the floor comes back down when it closes");
}

/// Puts every session setting back where a fresh instance has it. A preset
/// leaves these alone, so a test that moves the temperament or the root has to
/// put them back itself.
void restoreSessionSettings(OvertoniumProcessor &p) {
  for (const auto *id : ovt::params::kSessionParamIds)
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->getDefaultValue());
}

/// Every choice the Settings menu and the bars share goes through one place,
/// and lands on the parameter its list says it does.
void testSharedSettingsChoices(OvertoniumProcessor &p) {
  section("The choices the Settings menu and the bars share");

  namespace st = ovt::ui::settings;
  auto &apvts = p.apvts;

  const auto index = [&apvts](const char *id) {
    return st::choiceIndex(apvts, id);
  };

  // Each list's last entry, which is the one an off-by-one misses.
  const struct {
    int id;
    const char *param;
    int expect;
  } lists[] = {
      {st::kVoices + 7, ovt::params::polyphonyId, 7},
      {st::kVoices + 2, ovt::params::polyphonyId, 2},
      {st::kBendRange + 24, ovt::params::bendRangeId, 24},
      {st::kBendRange + 7, ovt::params::bendRangeId, 7},
      {st::kAftertouchFrom + 2, ovt::params::atSourceId, 2},
      {st::kSlideTo + 2, ovt::params::slideDestId, 2},
      {st::kTemperament + 8, ovt::params::temperamentId, 8},
      {st::kRoot + 11, ovt::params::tuningRootId, 11},
      {st::kReferencePitch + 3, ovt::params::referenceHzId, 3},
  };

  for (const auto &l : lists) {
    const bool handled = st::apply(apvts, l.id);
    check(handled && index(l.param) == l.expect,
          "item " + std::to_string(l.id) + " sets " + l.param + " to choice " +
              std::to_string(l.expect) + " (" + std::to_string(index(l.param)) +
              ")");
  }

  // Every item in every shared list is one apply carries out, and points at
  // what it ticks.
  {
    bool all = true;
    for (const auto &list :
         {st::voices(apvts), st::bendRange(apvts), st::aftertouchFrom(apvts),
          st::slideTo(apvts), st::temperament(apvts), st::root(apvts),
          st::referencePitch(apvts), st::retune(apvts)})
      for (juce::PopupMenu::MenuItemIterator it(list); it.next();)
        all &= st::apply(apvts, it.getItem().itemID);
    check(all, "every item in every shared list is carried out");
  }

  // Retune is a time, and its two named settings set it to theirs, each ticked
  // only while the knob is on it.
  {
    auto *retune = apvts.getParameter(ovt::params::adaptiveRetuneId);
    const auto seconds = [retune] {
      return retune->convertFrom0to1(retune->getValue());
    };
    const auto ticked = [&apvts](int id) {
      const auto list = st::retune(apvts);
      for (juce::PopupMenu::MenuItemIterator it(list); it.next();)
        if (it.getItem().itemID == id)
          return it.getItem().isTicked;
      return false;
    };

    st::apply(apvts, st::kRetune + 1);
    const auto smooth = seconds();
    const bool smoothTicked = ticked(st::kRetune + 1) && !ticked(st::kRetune);
    st::apply(apvts, st::kRetune);
    const auto fast = seconds();
    retune->setValueNotifyingHost(retune->convertTo0to1(0.1f));

    check(std::abs(smooth - 0.3f) < 0.001f && std::abs(fast - 0.03f) < 0.001f,
          "Smooth sets Retune to 300 ms and Fast to 30 ms (" +
              std::to_string(smooth) + ", " + std::to_string(fast) + ")");
    check(smoothTicked && !ticked(st::kRetune) && !ticked(st::kRetune + 1),
          "and each is ticked only while the knob is on it");
  }

  // And what the knob says is what the engine is handed, across its range and
  // at both ends, a jump at zero included.
  {
    auto *retune = apvts.getParameter(ovt::params::adaptiveRetuneId);
    bool reaches = true;
    std::string seen;

    for (const auto seconds : {0.0f, 0.03f, 0.25f, 1.0f}) {
      retune->setValueNotifyingHost(retune->convertTo0to1(seconds));
      ovt::SynthParams out{};
      p.parameters().snapshot(out, 0.0f);
      reaches &= std::abs(out.global.adaptiveRetuneSeconds - seconds) < 1.0e-4;
      seen += std::to_string(out.global.adaptiveRetuneSeconds) + " ";
    }

    check(reaches,
          "the Retune time reaches the engine as set, from 0 to 1 s (" + seen +
              ")");
  }

  // The switches flip, and nothing outside the lists is claimed, so the
  // Settings menu can hand everything to apply first.
  for (const auto &sw :
       {std::pair<int, const char *>{st::kMpe, ovt::params::mpeId},
        {st::kOneVoicePerKey, ovt::params::oneVoicePerKeyId},
        {st::kPhaseReset, ovt::params::phaseResetId},
        {st::kSafetyClip, ovt::params::safetyClipId},
        {st::kLookahead, ovt::params::lookaheadId}}) {
    const bool before = st::isOn(apvts, sw.second);
    st::apply(apvts, sw.first);
    const bool after = st::isOn(apvts, sw.second);
    st::apply(apvts, sw.first);
    check(after != before && st::isOn(apvts, sw.second) == before,
          std::string("item ") + std::to_string(sw.first) + " flips " +
              sw.second + " and flips it back");
  }

  check(!st::apply(apvts, 700) && !st::apply(apvts, 800) &&
            !st::apply(apvts, 820) && !st::apply(apvts, st::kTemperament + 9) &&
            !st::apply(apvts, st::kVoices),
        "undo, zoom, Fit all 32 channels and ids past a list are left to the "
        "menu");

  // Legato from the voice list's old entry: one voice and the switch, and
  // unticking keeps the one voice and drops the switch.
  if (auto *count = apvts.getParameter(ovt::params::polyphonyId)) {
    count->setValueNotifyingHost(
        count->convertTo0to1((float)ovt::params::kLegatoIndex));
    if (auto *sw = apvts.getParameter(ovt::params::legatoId))
      sw->setValueNotifyingHost(0.0f);

    check(st::legatoOn(apvts) && st::voiceCount(apvts) == 1,
          "the old legato entry reads as Legato with one voice");

    st::apply(apvts, st::kLegato);
    check(!st::legatoOn(apvts) && index(ovt::params::polyphonyId) == 1,
          "and unticking Legato leaves one voice with the switch off");

    st::apply(apvts, st::kLegato);
    check(st::legatoOn(apvts), "and ticking it again is the switch");
  }

  restoreSessionSettings(p);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// The noise channel's string, reached through its parameters: STRING turns
/// a burst of noise into a ringing note, DECAY decides for how long, and a
/// preset puts it back to nought.
void testNoiseString(OvertoniumProcessor &p) {
  section("The noise channel's string");

  namespace pp = ovt::params;

  const auto setParam = [&p](const juce::String &id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  // A burst of noise and nothing else: every partial silent, the noise
  // channel struck for a few milliseconds and then holding at silence.
  const auto ringing = [&](float amount, float decay) {
    p.applyFactoryPreset(presetIndex("Init"));
    for (int i = 0; i < ovt::kNumHarmonics; ++i)
      setParam(pp::oscParamId(pp::volumeSuffix, i), 0.0f);
    setParam(pp::noiseParamId(pp::volumeSuffix), 0.8f);
    setParam(pp::noiseParamId(pp::attackSuffix), 0.0005f);
    setParam(pp::noiseParamId(pp::decaySuffix), 0.01f);
    setParam(pp::noiseParamId(pp::sustainSuffix), 0.0f);
    setParam(pp::noiseParamId(pp::stringSuffix), amount);
    setParam(pp::noiseParamId(pp::stringDecaySuffix), decay);

    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    double late = 0.0;
    int counted = 0;

    for (int b = 0; b < 60; ++b) {
      juce::MidiBuffer midi =
          b == 0 ? noteOnAt(57, 1.0f, 0) : juce::MidiBuffer{};
      buffer.clear();
      p.processBlock(buffer, midi);

      // From a fifth of a second on, long after the burst.
      if (b >= 19 && b < 40) {
        for (int n = 0; n < 512; ++n)
          late += (double)buffer.getSample(0, n) * buffer.getSample(0, n);
        counted += 512;
      }
    }

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    buffer.clear();
    p.processBlock(buffer, off);
    return std::sqrt(late / std::max(1, counted));
  };

  const auto without = ringing(0.0f, 1.5f);
  const auto with = ringing(1.0f, 1.5f);
  const auto brief = ringing(1.0f, 0.1f);

  check(without < 1.0e-6 && with > 1.0e-3,
        "Noise String turns a burst of noise into a ringing note (" +
            std::to_string(without) + ", " + std::to_string(with) + ")");
  check(brief < 0.1 * with,
        "and Noise String Decay decides how long it rings (" +
            std::to_string(brief) + " against " + std::to_string(with) + ")");

  if (auto *decay =
          p.apvts.getParameter(pp::noiseParamId(pp::stringDecaySuffix)))
    check(decay->getText(decay->convertTo0to1(1.5f), 16) == "1.50 s",
          "and reads as a time (" +
              decay->getText(decay->convertTo0to1(1.5f), 16).toStdString() +
              ")");

  // A preset is a whole sound, so it puts the string back where every patch
  // without one has it.
  setParam(pp::noiseParamId(pp::stringSuffix), 0.7f);
  p.applyFactoryPreset(presetIndex("Init"));
  auto *amount = p.apvts.getParameter(pp::noiseParamId(pp::stringSuffix));
  check(amount != nullptr && amount->getValue() < 1.0e-6f,
        "and loading a preset puts Noise String back to nought");

  p.applyFactoryPreset(presetIndex("Init"));
}

/// The noise strip's string knobs: named, wired to their parameters, and
/// standing in order in the block under colour, gone when that block is
/// folded away.
void testNoiseStringKnobs(OvertoniumProcessor &p) {
  section("The noise strip's string knobs");

  struct WatchingHover final : ovt::ui::HoverTarget {
    ovt::ui::Row last = ovt::ui::kNoRow;
    void hoverChanged(int, ovt::ui::Row row) override { last = row; }
  };

  WatchingHover hover;
  juce::Component popupParent;
  ovt::ui::NoiseStrip strip(p.apvts, hover, popupParent);
  strip.setSize(40, 1000);

  const char *const titles[] = {"Noise string", "Noise string decay",
                                "Noise string damp", "Noise string point"};
  const char *const suffixes[] = {
      ovt::params::stringSuffix, ovt::params::stringDecaySuffix,
      ovt::params::stringDampSuffix, ovt::params::stringPointSuffix};

  std::array<juce::Slider *, 4> knobs{};
  const juce::Component *colourCaption = nullptr;
  for (auto *c : strip.getChildren()) {
    if (auto *sl = dynamic_cast<juce::Slider *>(c))
      for (size_t i = 0; i < 4; ++i)
        if (sl->getTitle() == titles[i])
          knobs[i] = sl;
    if (auto *label = dynamic_cast<juce::Label *>(c))
      if (label->getText() == "colour")
        colourCaption = label;
  }

  const bool all = std::all_of(knobs.begin(), knobs.end(),
                               [](auto *k) { return k != nullptr; });
  check(all && colourCaption != nullptr,
        "the noise strip has string, decay, damp and point, named");
  if (!all || colourCaption == nullptr)
    return;

  // Each moves its own parameter.
  bool wired = true;
  for (size_t i = 0; i < 4; ++i) {
    auto *param = p.apvts.getParameter(ovt::params::noiseParamId(suffixes[i]));
    const auto before = param->getValue();
    knobs[i]->setValue(knobs[i]->getMaximum(), juce::sendNotificationSync);
    wired &= param->getValue() > before + 0.01f || before > 0.99f;
    knobs[i]->setValue(knobs[i]->getMinimum(), juce::sendNotificationSync);
    wired &= param->getValue() < 0.01f;
  }
  check(wired, "and each turns its own parameter");
  p.applyFactoryPreset(presetIndex("Init"));

  // In order down the block under colour, evenly, each the size every knob
  // in a row is, none overlapping.
  bool ordered = knobs[0]->getY() >= colourCaption->getBottom();
  std::vector<int> gaps;
  for (size_t i = 1; i < 4; ++i) {
    ordered &= knobs[i]->getY() >= knobs[i - 1]->getBottom();
    gaps.push_back(knobs[i]->getY() - knobs[i - 1]->getY());
  }
  const auto [lo, hi] = std::minmax_element(gaps.begin(), gaps.end());
  check(ordered && *hi - *lo <= 1,
        "they stand in order under colour, evenly spaced (" +
            std::to_string(*lo) + " to " + std::to_string(*hi) + " px apart)");

  // Colour and the string are this strip's own, not its share of a row every
  // channel has, so hovering them lights no row across the mixer, and a row
  // lit from elsewhere is not drawn over them. Attack is still shared.
  {
    juce::Slider *colourKnob = nullptr, *attack = nullptr;
    for (auto *c : strip.getChildren())
      if (auto *sl = dynamic_cast<juce::Slider *>(c)) {
        if (sl->getTitle() == "Noise colour")
          colourKnob = sl;
        if (sl->getTitle() == "Noise attack")
          attack = sl;
      }

    const auto hoverOver = [&](juce::Component &c) {
      const auto at = c.getBounds().getCentre().toFloat();
      const auto now = juce::Time::getCurrentTime();
      strip.mouseMove(juce::MouseEvent(
          juce::Desktop::getInstance().getMainMouseSource(), at,
          juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
          juce::MouseInputSource::defaultOrientation,
          juce::MouseInputSource::defaultRotation,
          juce::MouseInputSource::defaultTiltX,
          juce::MouseInputSource::defaultTiltY, &strip, &c, now, at, now, 0,
          false));
      return hover.last;
    };

    bool quiet = colourKnob != nullptr && attack != nullptr &&
                 hoverOver(*colourKnob) == ovt::ui::kNoRow;
    for (auto *k : knobs)
      quiet &= hoverOver(*k) == ovt::ui::kNoRow;
    const bool shared =
        attack != nullptr && hoverOver(*attack) == ovt::ui::Row::Attack;

    check(quiet && shared, "hovering colour or the string lights no row "
                           "across the mixer, and attack still does");

    // Drawn the same with a row lit from elsewhere as with none.
    const auto picture = [&](ovt::ui::Row lit, juce::Rectangle<int> area) {
      strip.setHighlightedRow(lit);
      return strip.createComponentSnapshot(area, true, 1.0f);
    };
    const auto same = [](const juce::Image &a, const juce::Image &b) {
      for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
          if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
            return false;
      return true;
    };

    const auto block = knobs[0]->getBounds().getUnion(knobs[3]->getBounds());
    const bool untouched = same(picture(ovt::ui::kNoRow, block),
                                picture(ovt::ui::Row::PmRate, block));
    const bool attackLit =
        attack != nullptr &&
        !same(picture(ovt::ui::kNoRow, attack->getBounds()),
              picture(ovt::ui::Row::Attack, attack->getBounds()));
    strip.setHighlightedRow(ovt::ui::kNoRow);

    check(untouched && attackLit,
          "and a row lit from elsewhere is not drawn over the string, where "
          "attack's still is");
  }

  // Folding the pitch block folds them away with it.
  strip.setCollapsedSections(ovt::ui::sectionBit(ovt::ui::Section::PitchMod));
  const bool folded = std::none_of(knobs.begin(), knobs.end(),
                                   [](auto *k) { return k->isVisible(); });
  strip.setCollapsedSections(0);
  const bool back = std::all_of(knobs.begin(), knobs.end(),
                                [](auto *k) { return k->isVisible(); });
  check(folded && back, "and fold away with the block they stand in");
}

/// The resonator's parameters: the mode lists every mode by name,
/// the knobs read as what they are, and a preset turns the resonator off.
void testResonatorParameters(OvertoniumProcessor &p) {
  section("The resonator's parameters");

  namespace pp = ovt::params;

  auto *mode = dynamic_cast<juce::AudioParameterChoice *>(
      p.apvts.getParameter(pp::resonatorModeId));
  check(
      mode != nullptr &&
          mode->choices ==
              juce::StringArray{"Off", "Piano", "Open", "Octave", "Series"} &&
          mode->getIndex() == (int)ovt::ResonatorMode::Off,
      "Resonator Mode is Off, Piano, Open, Octave or Series, and Off to begin "
      "with");

  if (auto *decay = p.apvts.getParameter(pp::resonatorDecayId))
    check(decay->getText(decay->getDefaultValue(), 16) == "4.00 s",
          "Resonator Decay reads as a time (" +
              decay->getText(decay->getDefaultValue(), 16).toStdString() + ")");

  if (auto *mix = p.apvts.getParameter(pp::resonatorMixId))
    check(mix->getText(mix->getDefaultValue(), 16) == "50 %",
          "Resonator Mix reads as a share (" +
              mix->getText(mix->getDefaultValue(), 16).toStdString() + ")");

  // A preset is a whole sound, so it decides whether the resonator is on.
  if (mode != nullptr)
    mode->setValueNotifyingHost(
        mode->convertTo0to1((float)(int)ovt::ResonatorMode::Piano));
  p.applyFactoryPreset(presetIndex("Init"));
  check(mode != nullptr && mode->getIndex() == (int)ovt::ResonatorMode::Off,
        "and loading a preset switches them off");
}

/// What the processor publishes for the Playing bar's readouts: the pedal,
/// the bend, the wheel and the pressure, with MPE off and on.
void testPlayingReadouts(OvertoniumProcessor &p) {
  section("What the Playing bar is handed");

  using ovt::params::AftertouchSource;

  const auto setParam = [&p](const char *id, float plain) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1(plain));
  };

  const auto send = [&p](std::initializer_list<juce::MidiMessage> messages) {
    juce::MidiBuffer midi;
    for (const auto &m : messages)
      midi.addEvent(m, 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    p.processBlock(buffer, midi);
  };

  const auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };

  p.applyFactoryPreset(presetIndex("Init"));
  setParam(ovt::params::bendRangeId, 2.0f);
  setParam(ovt::params::atSourceId, (float)AftertouchSource::ChannelPressure);
  send({juce::MidiMessage::allSoundOff(1),
        juce::MidiMessage::controllerEvent(1, 1, 0),
        juce::MidiMessage::pitchWheel(1, 8192)});

  check(!p.isSustainDown() && near(p.getBendPosition(), 0.0f) &&
            near(p.getWheelPosition(), 0.0f) &&
            near(p.getPressurePosition(), 0.0f),
        "everything rests with nothing touched");

  // The pedal, one bit per channel, so letting go on one channel leaves it
  // down on another, and a panic lets go of all of them.
  send({juce::MidiMessage::controllerEvent(1, 64, 127)});
  const bool downOnOne = p.isSustainDown();
  send({juce::MidiMessage::controllerEvent(3, 64, 127),
        juce::MidiMessage::controllerEvent(1, 64, 0)});
  const bool stillDownOnThree = p.isSustainDown();
  send({juce::MidiMessage::controllerEvent(3, 64, 0)});
  const bool up = !p.isSustainDown();
  send({juce::MidiMessage::controllerEvent(1, 64, 127),
        juce::MidiMessage::allNotesOff(1)});
  const bool panicLetsGo = !p.isSustainDown();

  check(downOnOne && stillDownOnThree && up && panicLetsGo,
        "SUSTAIN follows the pedal on every channel, and a panic lets it go");

  // A note held only by the pedal still counts as sounding.
  send({juce::MidiMessage::controllerEvent(1, 64, 127),
        juce::MidiMessage::noteOn(1, 60, 0.8f)});
  send({juce::MidiMessage::noteOff(1, 60)});
  check(p.getActiveVoiceCount() == 1,
        "a note the pedal holds is in use (" +
            std::to_string(p.getActiveVoiceCount()) + ")");
  send({juce::MidiMessage::controllerEvent(1, 64, 0),
        juce::MidiMessage::allSoundOff(1)});

  // The wheels, across their travel.
  bool bendFollows = true;
  const struct {
    int value;
    float position;
  } wheel[] = {
      {0, -1.0f}, {4096, -0.5f}, {8192, 0.0f}, {12288, 0.5f}, {16383, 1.0f}};
  for (const auto &w : wheel) {
    send({juce::MidiMessage::pitchWheel(1, w.value)});
    bendFollows &= near(p.getBendPosition(), w.position);
  }
  check(bendFollows, "BEND follows the pitch wheel from one end to the other");

  bool wheelFollows = true;
  for (const auto value : {0, 32, 64, 127}) {
    send({juce::MidiMessage::controllerEvent(1, 1, value)});
    wheelFollows &= near(p.getWheelPosition(), (float)value / 127.0f);
  }
  check(wheelFollows, "WHEEL follows the mod wheel from one end to the other");

  // Pressure is what the keyboard sends, whatever Aftertouch from passes on,
  // so the wheel moves WHEEL and nothing else.
  bool pressureAlone = true;
  for (const auto source :
       {AftertouchSource::ChannelPressure, AftertouchSource::ModWheel,
        AftertouchSource::Either}) {
    setParam(ovt::params::atSourceId, (float)source);
    send({juce::MidiMessage::pitchWheel(1, 8192),
          juce::MidiMessage::controllerEvent(1, 1, 120),
          juce::MidiMessage::channelPressureChange(1, 30)});
    pressureAlone &= near(p.getPressurePosition(), 30.0f / 127.0f);
  }
  check(pressureAlone,
        "PRESSURE is the keyboard's pressure, not the wheel, whatever "
        "Aftertouch from follows (" +
            std::to_string(p.getPressurePosition()) + ")");

  // And polyphonic aftertouch on a held key, the strongest one.
  setParam(ovt::params::atSourceId, (float)AftertouchSource::ChannelPressure);
  send({juce::MidiMessage::channelPressureChange(1, 0),
        juce::MidiMessage::controllerEvent(1, 1, 0),
        juce::MidiMessage::noteOn(1, 60, 0.8f),
        juce::MidiMessage::noteOn(1, 64, 0.8f)});
  send({juce::MidiMessage::aftertouchChange(1, 60, 40),
        juce::MidiMessage::aftertouchChange(1, 64, 90)});
  const auto strongest = p.getPressurePosition();
  send({juce::MidiMessage::noteOff(1, 64)});
  const auto afterLetGo = p.getPressurePosition();
  check(near(strongest, 90.0f / 127.0f) && near(afterLetGo, 40.0f / 127.0f),
        "PRESSURE shows the strongest key held, and drops as it comes up (" +
            std::to_string(strongest) + ", " + std::to_string(afterLetGo) +
            ")");
  send({juce::MidiMessage::allSoundOff(1)});

  // Under MPE each key bends and presses on its own channel. The member
  // channels bend over 48 semitones, so a bend of one semitone is 1/48 of
  // the way, and against a BEND range of 2 it reads halfway.
  setParam(ovt::params::mpeId, 1.0f);
  send({});

  const auto semitones = [](float st) {
    return 8192 + (int)std::lround(st / 48.0f * 8192.0f);
  };

  send({juce::MidiMessage::noteOn(2, 60, 0.8f),
        juce::MidiMessage::noteOn(3, 64, 0.8f)});
  send({juce::MidiMessage::pitchWheel(2, semitones(-0.5f)),
        juce::MidiMessage::pitchWheel(3, semitones(1.0f)),
        juce::MidiMessage::channelPressureChange(2, 90),
        juce::MidiMessage::channelPressureChange(3, 30)});
  const auto widest = p.getBendPosition();
  const auto pressed = p.getPressurePosition();
  send({juce::MidiMessage::noteOff(3, 64)});
  const auto left = p.getBendPosition();

  check(near(widest, 0.5f) && near(left, -0.25f),
        "under MPE, BEND shows the key bent furthest, either way (" +
            std::to_string(widest) + ", " + std::to_string(left) + ")");
  check(near(pressed, 90.0f / 127.0f),
        "and PRESSURE the key pressed hardest (" + std::to_string(pressed) +
            ")");

  // The parser takes the pedal under MPE, and the lamp still sees it.
  send({juce::MidiMessage::controllerEvent(1, 64, 127)});
  const bool mpeDown = p.isSustainDown();
  send({juce::MidiMessage::controllerEvent(1, 64, 0)});
  check(mpeDown && !p.isSustainDown(), "and SUSTAIN follows the pedal");

  send({juce::MidiMessage::allSoundOff(1)});
  setParam(ovt::params::mpeId, 0.0f);
  send({});

  // The editor reads all of this from the message thread while the audio thread
  // writes it. This gives a run under the thread sanitizer two threads to
  // watch.
  {
    std::atomic<bool> playing{true};
    std::thread audio([&] {
      for (int b = 0; b < 200; ++b) {
        const auto on = (b % 2) == 0;
        send({juce::MidiMessage::controllerEvent(1, 64, on ? 127 : 0),
              juce::MidiMessage::controllerEvent(1, 1, b % 128),
              juce::MidiMessage::pitchWheel(1, (b * 80) % 16384),
              on ? juce::MidiMessage::noteOn(1, 60 + b % 12, 0.8f)
                 : juce::MidiMessage::noteOff(1, 60 + (b - 1) % 12)});
      }
      playing = false;
    });

    float seen = 0.0f;
    while (playing)
      seen += (float)p.getActiveVoiceCount() + (float)p.isSustainDown() +
              p.getBendPosition() + p.getWheelPosition() +
              p.getPressurePosition();

    audio.join();
    check(std::isfinite(seen), "and all of it can be read while it is played");
  }

  send({juce::MidiMessage::allSoundOff(1)});
  restoreSessionSettings(p);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// The Playing bar shows what the parameters say and changes them.
void testPlayingBar(OvertoniumProcessor &p) {
  section("The Playing bar");

  p.applyFactoryPreset(presetIndex("Init"));
  namespace st = ovt::ui::settings;

  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  sizeEditor(*editor, 1348, 240);

  ovt::ui::PlayingBar *bar = nullptr;
  juce::TextButton *opener = nullptr;
  std::function<void(juce::Component &)> look = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *b = dynamic_cast<ovt::ui::PlayingBar *>(child))
        bar = b;
      if (auto *b = dynamic_cast<juce::TextButton *>(child))
        if (b->getTitle() == "Playing")
          opener = b;
      look(*child);
    }
  };
  look(*editor);

  check(bar != nullptr && opener != nullptr, "there is a Playing bar");
  if (bar == nullptr || opener == nullptr)
    return;

  opener->onClick();

  ovt::ui::SegmentDisplay *voices = nullptr, *bend = nullptr,
                          *aftertouch = nullptr, *slide = nullptr;
  juce::TextButton *legato = nullptr, *perKey = nullptr, *mpe = nullptr;

  for (auto *child : bar->getChildren()) {
    if (auto *d = dynamic_cast<ovt::ui::SegmentDisplay *>(child)) {
      const auto t = d->getTitle();
      if (t.startsWith("Voices"))
        voices = d;
      if (t.startsWith("Pitch bend"))
        bend = d;
      if (t.startsWith("Aftertouch"))
        aftertouch = d;
      if (t.startsWith("Slide"))
        slide = d;
    }
    if (auto *b = dynamic_cast<juce::TextButton *>(child)) {
      if (b->getTitle() == "Legato")
        legato = b;
      if (b->getTitle() == "No stack")
        perKey = b;
      if (b->getTitle() == "MPE")
        mpe = b;
    }
  }

  check(voices != nullptr && bend != nullptr && aftertouch != nullptr &&
            slide != nullptr && legato != nullptr && perKey != nullptr &&
            mpe != nullptr,
        "it has voices, bend range, aftertouch and slide, and Legato, No "
        "stack and MPE");
  if (voices == nullptr || bend == nullptr || aftertouch == nullptr ||
      slide == nullptr || legato == nullptr || perKey == nullptr ||
      mpe == nullptr)
    return;

  check(voices->isVisible() && slide->isVisible() && mpe->isVisible(),
        "all of it fits at 1348 px");
  {
    bool natural = true;
    for (auto *d : {voices, bend, aftertouch, slide})
      natural &= d->getWidth() >= d->naturalWidth(d->getHeight());
    check(natural, "with every display at the width its cells need");
  }

  // ---- it reads the parameters ---------------------------------------------
  st::apply(p.apvts, st::kVoices + 5);
  st::apply(p.apvts, st::kBendRange + 12);
  st::apply(p.apvts, st::kAftertouchFrom + 1);
  st::apply(p.apvts, st::kSlideTo + 2);
  bar->refresh();

  check(voices->getReading().trim() == "8" &&
            bend->getReading().trim() == "12" &&
            aftertouch->getReading().trim() == "WHEEL" &&
            slide->getReading().trim() == "TUNING",
        "it shows 8 voices, 12 semitones, the wheel and tuning (" +
            voices->getReading().toStdString() + ", " +
            bend->getReading().toStdString() + ", " +
            aftertouch->getReading().toStdString() + ", " +
            slide->getReading().toStdString() + ")");

  check(voices->onClick != nullptr && bend->onClick != nullptr &&
            aftertouch->onClick != nullptr && slide->onClick != nullptr,
        "and each display opens its list");

  // ---- the switches change the parameters and light from them -----------
  const auto pressAndRead = [&](juce::TextButton *b, const char *param) {
    const bool before = st::isOn(p.apvts, param);
    b->onClick();
    return std::pair<bool, bool>{st::isOn(p.apvts, param) != before,
                                 b->getToggleState() ==
                                     st::isOn(p.apvts, param)};
  };

  for (const auto &sw : {std::pair<juce::TextButton *, const char *>{
                             legato, ovt::params::legatoId},
                         {perKey, ovt::params::oneVoicePerKeyId},
                         {mpe, ovt::params::mpeId}}) {
    const auto [flipped, lit] = pressAndRead(sw.first, sw.second);
    check(flipped && lit, sw.first->getButtonText().toStdString() +
                              " flips its parameter and lights from it");
    sw.first->onClick();
  }

  // A preset, a host or the Settings menu moves one, and the bar follows on
  // its next refresh.
  st::apply(p.apvts, st::kMpe);
  bar->refresh();
  check(mpe->getToggleState() == st::isOn(p.apvts, ovt::params::mpeId),
        "and follows a change made somewhere else");
  st::apply(p.apvts, st::kMpe);

  // ---- how it is being played ----------------------------------------------
  const auto &inUse = bar->inUseDisplay();
  const auto &sustain = bar->sustainLamp();

  check(inUse.isVisible() && sustain.isVisible() &&
            inUse.getY() == voices->getY() &&
            inUse.getX() > voices->getRight() &&
            inUse.getRight() < legato->getX() &&
            bar->getHeight() == ovt::ui::OptionBar::kHeight,
        "IN USE stands between VOICES and LEGATO, SUSTAIN is there, and it "
        "is all still one row");

  bar->showActivity({3, true});
  const bool three = inUse.getReading().trim() == "3" && inUse.isActive() &&
                     inUse.getTitle() == "In use: 3 voices" &&
                     sustain.getToggleState();
  bar->showActivity({});
  const bool none = inUse.getReading().trim() == "0" && inUse.isActive() &&
                    !sustain.getToggleState();

  check(three, "it shows the voices in use and lights SUSTAIN (" +
                   inUse.getReading().toStdString() + ", " +
                   inUse.getTitle().toStdString() + ")");
  check(none, "and reads a lit nought with SUSTAIN dark when nothing sounds");

  // The meters, after SUSTAIN in its group, each across its whole travel.
  const auto &pitchMeter = bar->bendMeter();
  const auto &wheelMeter = bar->wheelMeter();
  const auto &pressureMeter = bar->pressureMeter();

  check(pitchMeter.isVisible() && wheelMeter.isVisible() &&
            pressureMeter.isVisible() &&
            sustain.getRight() < pitchMeter.getX() &&
            pitchMeter.getRight() < wheelMeter.getX() &&
            wheelMeter.getRight() < pressureMeter.getX() &&
            pitchMeter.getBounds().getCentreY() ==
                sustain.getBounds().getCentreY(),
        "PITCH, WHEEL and PRESSURE stand after SUSTAIN, on its line");

  {
    // Where a needle's two ends and middle are, from its own width.
    const auto left = ovt::ui::ActivityNeedle::kSlotInset + 1;
    const auto right = [](const ovt::ui::ActivityNeedle &n) {
      return n.getWidth() - ovt::ui::ActivityNeedle::kSlotInset - 2;
    };
    const auto middle = [&](const ovt::ui::ActivityNeedle &n) {
      return (left + right(n)) / 2;
    };
    const auto near = [](int a, int b) { return std::abs(a - b) <= 1; };

    bar->showActivity({0, false, -1.0f, 0.0f, 0.0f});
    const bool atRest = pitchMeter.getColumn() == left &&
                        wheelMeter.getColumn() == left &&
                        pressureMeter.getColumn() == left;
    bar->showActivity({0, false, 0.0f, 0.5f, 0.5f});
    const bool halfway = near(pitchMeter.getColumn(), middle(pitchMeter)) &&
                         near(wheelMeter.getColumn(), middle(wheelMeter)) &&
                         near(pressureMeter.getColumn(), middle(pressureMeter));
    bar->showActivity({0, false, 1.0f, 1.0f, 1.0f});
    const bool atTheEnd = pitchMeter.getColumn() == right(pitchMeter) &&
                          wheelMeter.getColumn() == right(wheelMeter) &&
                          pressureMeter.getColumn() == right(pressureMeter);

    check(atRest && halfway && atTheEnd,
          "each needle reaches both ends of its travel and its middle (" +
              std::to_string(pitchMeter.getColumn()) + " of " +
              std::to_string(right(pitchMeter)) + ")");

    // The pitch wheel rests in the middle and is marked there. The other two
    // rest at the left and are not, which shows as the middle column of a
    // needle at the far end matching the column beside it.
    const auto markedAtMiddle = [](const ovt::ui::ActivityNeedle &n) {
      auto &needle = const_cast<ovt::ui::ActivityNeedle &>(n);
      const auto image =
          needle.createComponentSnapshot(needle.getLocalBounds(), true, 1.0f);
      const auto x = needle.getWidth() / 2;
      const auto y = needle.getHeight() / 2;
      return image.getPixelAt(x, y) != image.getPixelAt(x - 3, y);
    };

    check(markedAtMiddle(pitchMeter) && !markedAtMiddle(wheelMeter) &&
              !markedAtMiddle(pressureMeter),
          "PITCH is marked at its middle and WHEEL and PRESSURE are not");

    bar->showActivity({});

    // A bar is handed its first reading before it has a width, and has to put
    // its needles where they belong once it is laid out.
    juce::Component popupHost;
    ovt::ui::PlayingBar fresh(p.apvts, popupHost);
    fresh.setSize(1348, ovt::ui::OptionBar::kHeight);
    check(near(fresh.bendMeter().getColumn(), middle(fresh.bendMeter())) &&
              fresh.wheelMeter().getColumn() == left,
          "and a new bar's needles start where they rest (" +
              std::to_string(fresh.bendMeter().getColumn()) + ")");
  }

  // ---- the macros
  // -------------------------------------------------------------
  {
    namespace pp = ovt::params;

    bool inOrder = true;
    for (int m = 0; m < pp::kNumMacros; ++m) {
      const auto &knob = bar->macroKnob(m);
      inOrder &= knob.isVisible() &&
                 knob.getX() > (m == 0 ? pressureMeter.getRight()
                                       : bar->macroKnob(m - 1).getRight() - 1);
    }
    check(inOrder && bar->getHeight() == ovt::ui::OptionBar::kHeight,
          "the eight macro knobs stand after the meters, still on one row");

    const auto setChoice = [&p](const juce::String &id, int index) {
      if (auto *param = p.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1((float)index));
    };

    // Macro 2 made, aimed at the first row and given the third colour, and
    // the rest left unmade.
    setChoice(pp::macroRowId(1), 1);
    setChoice(pp::macroColourId(1), 3);
    bar->refresh();

    auto &made = bar->macroKnob(1).slider;
    auto &unmade = bar->macroKnob(0).slider;

    check(made.isEnabled() && !(bool)made.getProperties()["blank"] &&
              made.findColour(juce::Slider::rotarySliderFillColourId) ==
                  pp::macroColour(3),
          "a made macro's knob is live and wears its colour");
    check(!unmade.isEnabled() && (bool)unmade.getProperties()["blank"],
          "and one nobody has made is blank and does nothing");

    // Captioned with its row once made, with its number until then.
    check(bar->macroKnob(1).getCaption() == "TUNE" &&
              bar->macroKnob(0).getCaption() == "1" &&
              made.getTitle() == "Macro 2 amount, Tune, Playing bar" &&
              unmade.getTitle() == "Macro 1 amount, Playing bar",
          "a made macro is captioned and named after its row, an unmade one "
          "by its number (" +
              bar->macroKnob(1).getCaption().toStdString() + ", " +
              made.getTitle().toStdString() + ")");

    // Every row's caption fits under a knob, in the font it is drawn in.
    {
      const auto font = ovt::ui::makeFont(9.0f, true);
      juce::String tooWide;
      for (int row = 1; row <= pp::kNumMacroRows; ++row) {
        const auto caption = ovt::ui::PlayingBar::macroCaption(row);
        if (caption.isEmpty() ||
            juce::GlyphArrangement::getStringWidth(font, caption) >
                (float)bar->macroKnob(1).getWidth())
          tooWide << caption << " (" << pp::macroRowName(row) << ") ";
      }
      check(tooWide.isEmpty() && bar->macroKnob(1).getWidth() ==
                                     ovt::ui::PlayingBar::kMacroWidth,
            "every row a macro can aim at has a caption that fits under its "
            "knob (" +
                tooWide.toStdString() + ")");
    }

    // The knob and the parameter move together, both ways.
    auto *amount = p.apvts.getParameter(pp::macroAmountId(1));
    made.setValue(0.5, juce::sendNotificationSync);
    const auto fromKnob = amount->convertFrom0to1(amount->getValue());
    amount->setValueNotifyingHost(amount->convertTo0to1(-0.25f));
    const auto fromParameter = made.getValue();

    check(std::abs(fromKnob - 0.5f) < 0.001f &&
              std::abs(fromParameter + 0.25) < 0.001,
          "the knob turns the macro's amount, and the amount turns the knob (" +
              std::to_string(fromKnob) + ", " + std::to_string(fromParameter) +
              ")");

    // Blank means nothing on it to read: the same picture wherever the
    // amount stands, where a made knob moves its pointer.
    const auto picture = [](juce::Slider &slider, double value) {
      slider.setValue(value, juce::dontSendNotification);
      return slider.createComponentSnapshot(slider.getLocalBounds(), true,
                                            1.0f);
    };
    const auto same = [](const juce::Image &a, const juce::Image &b) {
      for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
          if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
            return false;
      return true;
    };

    const bool blankStill = same(picture(unmade, -1.0), picture(unmade, 1.0)) &&
                            same(picture(unmade, 0.0), picture(unmade, 1.0));
    const bool madeMoves = !same(picture(made, -1.0), picture(made, 1.0));

    check(blankStill && madeMoves,
          "a blank knob shows no amount, where a made one does");

    unmade.setValue(0.0, juce::dontSendNotification);
    amount->setValueNotifyingHost(amount->getDefaultValue());
    setChoice(pp::macroRowId(1), 0);
    setChoice(pp::macroColourId(1), 0);
    bar->refresh();
  }

  // ---- a narrow window wraps, as the top bar does --------------------------
  //
  // Nothing is left out. The groups that do not fit across go onto another
  // row, the bar grows by it, and the mixer gives up the height.
  {
    const auto oneRow = bar->getHeight();
    sizeEditor(*editor, ovt::ui::TopBar::minimumWidth());

    bool all = true;
    for (auto *c : {static_cast<const juce::Component *>(voices),
                    static_cast<const juce::Component *>(legato),
                    static_cast<const juce::Component *>(perKey),
                    static_cast<const juce::Component *>(bend),
                    static_cast<const juce::Component *>(mpe),
                    static_cast<const juce::Component *>(aftertouch),
                    static_cast<const juce::Component *>(slide),
                    static_cast<const juce::Component *>(&inUse),
                    static_cast<const juce::Component *>(&sustain),
                    static_cast<const juce::Component *>(&pitchMeter),
                    static_cast<const juce::Component *>(&wheelMeter),
                    static_cast<const juce::Component *>(&pressureMeter)})
      all &= c->isVisible() && bar->getLocalBounds().contains(c->getBounds());

    for (int m = 0; m < ovt::params::kNumMacros; ++m)
      all &= bar->macroKnob(m).isVisible() &&
             bar->getLocalBounds().contains(bar->macroKnob(m).getBounds());

    check(all && bar->getHeight() > oneRow &&
              bar->getHeight() == bar->heightForWidth(bar->getWidth()),
          "at the narrowest window every control is still there, the bar "
          "wrapping onto " +
              std::to_string(bar->getHeight()) + " px from " +
              std::to_string(oneRow));
    check(slide->getY() > voices->getY(),
          "with what does not fit across on a row below");

    // And the floor counts the rows, so the narrowest legal window still has
    // its mixer.
    const auto *limits = editor->getConstrainer();
    sizeEditor(*editor, ovt::ui::TopBar::minimumWidth());
    check(limits != nullptr &&
              editor->getHeight() >= limits->getMinimumHeight(),
          "and the window's floor counts the bar's rows");
  }

  opener->onClick();
  p.applyFactoryPreset(presetIndex("Init"));
}

/// A right-click on a knob in the bars: MIDI Learn on Retune, and on a macro
/// knob a menu that offers to make the macro, or to bind a controller to it
/// and change it in MACROS once it is made.
void testBarRightClicks(OvertoniumProcessor &p) {
  section("Right-clicks in the bars");

  namespace pp = ovt::params;
  using ovt::ui::PlayingBar;

  p.applyFactoryPreset(presetIndex("Init"));

  const auto setChoice = [&p](const juce::String &id, int index) {
    if (auto *param = p.apvts.getParameter(id))
      param->setValueNotifyingHost(param->convertTo0to1((float)index));
  };
  const auto chosen = [&p](const juce::String &id) {
    auto *param = p.apvts.getParameter(id);
    return (int)std::lround(param->convertFrom0to1(param->getValue()));
  };

  // Delivered as JUCE delivers it to a listening ancestor: the control under
  // the pointer as the original component, the bar as the one hearing it.
  const auto click = [](juce::Component &landedOn, juce::Component &bar,
                        bool right) {
    const auto at = bar.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), at,
        juce::ModifierKeys(right ? juce::ModifierKeys::rightButtonModifier
                                 : juce::ModifierKeys::leftButtonModifier),
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, &bar, &landedOn, now, at, now, 1,
        false);
  };

  juce::Component popupHost;
  PlayingBar bar(p.apvts, popupHost);
  bar.setSize(1348, ovt::ui::OptionBar::kHeight);

  setChoice(pp::macroRowId(1), 8);
  bar.refresh();

  int asked = -1;
  juce::String learnFor;
  bar.onMacroMenuRequested = [&asked](int m) { asked = m; };
  bar.onLearnRequested = [&learnFor](const juce::String &id) { learnFor = id; };

  bar.mouseDown(click(bar.macroKnob(1).slider, bar, true));
  const auto onMade = asked;
  asked = -1;
  bar.mouseDown(click(bar.macroKnob(0).slider, bar, true));
  const auto onUnmade = asked;
  asked = -1;
  bar.mouseDown(click(bar.macroKnob(1).slider, bar, false));
  const auto onLeft = asked;
  bar.mouseDown(
      click(const_cast<ovt::ui::ScreenSwitch &>(bar.sustainLamp()), bar, true));

  check(onMade == 1 && onUnmade == 0 && onLeft == -1 && asked == -1 &&
            learnFor.isEmpty(),
        "a right-click on a macro knob asks for that macro's menu, and "
        "nothing else does (" +
            std::to_string(onMade) + ", " + std::to_string(onUnmade) + ")");

  // The menu itself, read as data since a menu cannot be shown here.
  const auto ids = [](const juce::PopupMenu &menu) {
    std::set<int> found;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
      if (it.getItem().itemID != 0)
        found.insert(it.getItem().itemID);
    return found;
  };

  auto *amount = dynamic_cast<juce::RangedAudioParameter *>(
      p.apvts.getParameter(pp::macroAmountId(1)));

  const auto unmadeMenu = PlayingBar::macroMenu(0, false, p.midiLearn, amount);
  const auto madeMenu = PlayingBar::macroMenu(1, true, p.midiLearn, amount);
  const auto unmadeIds = ids(unmadeMenu);
  const auto madeIds = ids(madeMenu);

  check(unmadeIds == std::set<int>{PlayingBar::kAssignMacro},
        "an unmade macro's menu offers only to assign it");
  check(madeIds.count(ovt::ui::learn::kLearn) == 1 &&
            madeIds.count(PlayingBar::kEditMacro) == 1 &&
            madeIds.count(PlayingBar::kAssignMacro) == 0,
        "and a made one MIDI Learn and a way into MACROS");

  p.midiLearn.arm(amount);
  const auto waitingIds =
      ids(PlayingBar::macroMenu(1, true, p.midiLearn, amount));
  p.midiLearn.arm(nullptr);
  check(waitingIds.count(ovt::ui::learn::kCancel) == 1,
        "which says so while it waits for a controller");

  check(bar.macroKnob(0).slider.getTooltip().contains("not assigned") &&
            bar.macroKnob(0).slider.getTooltip().contains("Right-click") &&
            bar.macroKnob(1).slider.getTooltip().contains("Right-click"),
        "and the tooltips say what a right-click does");

  // Assigning from the knob makes that macro, and only if nobody has.
  {
    ovt::ui::MacroPanel panel(p.apvts);
    panel.make(4);
    setChoice(pp::macroRowId(5), 7);
    panel.make(5);

    check(chosen(pp::macroRowId(4)) == 1 && chosen(pp::macroRowId(5)) == 7,
          "assigning makes the macro on the first row, and leaves a made one "
          "where it is aimed");
  }

  // Retune is the other knob in the bars, and offers MIDI Learn.
  {
    ovt::ui::TuningBar tuning(p.apvts, popupHost);
    tuning.setSize(1348, ovt::ui::OptionBar::kHeight);

    juce::Slider *retune = nullptr;
    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *s = dynamic_cast<juce::Slider *>(child))
          if (s->getTitle() == "Adaptive retune time")
            retune = s;
        look(*child);
      }
    };
    look(tuning);

    juce::String tuningLearn;
    tuning.onLearnRequested = [&tuningLearn](const juce::String &id) {
      tuningLearn = id;
    };

    if (retune != nullptr)
      tuning.mouseDown(click(*retune, tuning, true));

    check(retune != nullptr && tuningLearn == pp::adaptiveRetuneId,
          "a right-click on Retune offers MIDI Learn for it (" +
              tuningLearn.toStdString() + ")");
  }

  for (int m = 0; m < pp::kNumMacros; ++m)
    setChoice(pp::macroRowId(m), 0);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// The Tuning bar shows the temperament, root and reference pitch, and Retune
/// only where it means something.
void testTuningBar(OvertoniumProcessor &p) {
  section("The Tuning bar");

  p.applyFactoryPreset(presetIndex("Init"));
  namespace st = ovt::ui::settings;
  using ovt::ui::SegmentDisplay;

  // ---- what it has to be able to say --------------------------------------
  {
    juce::String undrawable;
    for (const auto *name : ovt::params::kPitchClassNames)
      for (const auto *c = name; *c != 0; ++c)
        if (!SegmentDisplay::canDraw(*c, SegmentDisplay::Bars::Fourteen))
          undrawable += *c;
    check(undrawable.isEmpty(),
          "every root, sharps included, can be spelled in the root's cells (" +
              undrawable.toStdString() + ")");

    bool fit = true;
    for (int i = 0; i < (int)ovt::Temperament::NumTemperaments; ++i) {
      const juce::String word = ovt::ui::TuningBar::temperamentWord(i);
      fit &= word.length() <= 8;
      for (int c = 0; c < word.length(); ++c)
        fit &= SegmentDisplay::canDraw((char)word[c],
                                       SegmentDisplay::Bars::Fourteen);
    }
    check(fit, "every temperament has a word that fits its eight cells");
  }

  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  sizeEditor(*editor, 1348, 240);

  ovt::ui::TuningBar *bar = nullptr;
  juce::TextButton *opener = nullptr;
  std::function<void(juce::Component &)> look = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *b = dynamic_cast<ovt::ui::TuningBar *>(child))
        bar = b;
      if (auto *b = dynamic_cast<juce::TextButton *>(child))
        if (b->getTitle() == "Tuning")
          opener = b;
      look(*child);
    }
  };
  look(*editor);

  check(bar != nullptr && opener != nullptr, "there is a Tuning bar");
  if (bar == nullptr || opener == nullptr)
    return;

  opener->onClick();

  SegmentDisplay *temperament = nullptr, *root = nullptr, *reference = nullptr;
  ovt::ui::LabelledKnob *retune = nullptr;
  for (auto *child : bar->getChildren()) {
    if (auto *d = dynamic_cast<SegmentDisplay *>(child)) {
      const auto t = d->getTitle();
      if (t.startsWith("Temperament"))
        temperament = d;
      if (t.startsWith("Root"))
        root = d;
      if (t.startsWith("Reference"))
        reference = d;
    }
    if (auto *k = dynamic_cast<ovt::ui::LabelledKnob *>(child))
      if (k->slider.getTitle() == "Adaptive retune time")
        retune = k;
  }

  check(temperament != nullptr && root != nullptr && reference != nullptr &&
            retune != nullptr,
        "it has temperament, root, reference pitch and retune");
  if (temperament == nullptr || root == nullptr || reference == nullptr ||
      retune == nullptr)
    return;

  const auto pick = [&](int id) {
    st::apply(p.apvts, id);
    bar->refresh();
  };

  // ---- under a fixed temperament ------------------------------------------
  pick(st::kTemperament + (int)ovt::Temperament::Just);
  pick(st::kRoot + 6);
  pick(st::kReferencePitch + 0);

  const auto hz = ovt::params::kReferenceHzChoices[0];

  check(temperament->getReading().trim() == "JUST" &&
            root->getReading().trim() == "F#" &&
            reference->getReading().trim() == juce::String(hz),
        "it shows Just on F# at " + std::to_string(hz) + " Hz (" +
            temperament->getReading().trim().toStdString() + ", " +
            root->getReading().trim().toStdString() + ", " +
            reference->getReading().trim().toStdString() + ")");
  check(root->isActive(), "with the root lit");

  // ---- the twelve displays chart the scale
  // -----------------------------------
  pick(st::kRoot + 0);
  {
    const auto read = [bar](int pc) {
      return bar->scaleDisplay(pc).getReading().trim();
    };
    bool allLit = true;
    for (int pc = 0; pc < 12; ++pc)
      allLit &= bar->scaleDisplay(pc).isActive();

    check(read(9) == "0.0" && read(0) == "15.6" && read(4) == "2.0" &&
              read(7) == "17.6" && allLit,
          "Just on C reads A 0.0, C 15.6, E 2.0 and G 17.6, all lit (" +
              read(9).toStdString() + ", " + read(0).toStdString() + ", " +
              read(4).toStdString() + ", " + read(7).toStdString() + ")");

    pick(st::kTemperament + (int)ovt::Temperament::Equal);
    bool allZero = true;
    for (int pc = 0; pc < 12; ++pc)
      allZero &= read(pc) == "0.0";
    check(allZero, "and equal temperament reads 0.0 on every one");
    pick(st::kTemperament + (int)ovt::Temperament::Just);
  }
  check(!retune->isVisible(), "and no Retune, which only Adaptive has");

  // ---- under equal temperament, the root says nothing ---------------------
  pick(st::kTemperament + (int)ovt::Temperament::Equal);
  check(temperament->getReading().trim() == "EQUAL" && !root->isActive(),
        "under Equal the root is dimmed, as its Settings entry is greyed out");

  // ---- unless the resonator's Series is built on it -------------------------
  {
    ovt::ui::TopBar *topBar = nullptr;
    std::function<void(juce::Component &)> findTop = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *t = dynamic_cast<ovt::ui::TopBar *>(child))
          topBar = t;
        findTop(*child);
      }
    };
    findTop(*editor);

    const auto settingsRootLive = [&topBar] {
      bool live = false;
      if (topBar != nullptr) {
        auto menu = topBar->buildSettingsMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
          if (it.getItem().text == "Root")
            live = it.getItem().isEnabled;
      }
      return live;
    };

    auto *mode = p.apvts.getParameter(ovt::params::resonatorModeId);
    const auto setMode = [mode](ovt::ResonatorMode m) {
      mode->setValueNotifyingHost(mode->convertTo0to1((float)(int)m));
    };

    const bool deadBefore = !settingsRootLive();
    setMode(ovt::ResonatorMode::Series);
    bar->refresh();
    const bool liveForSeries = root->isActive() && settingsRootLive();
    setMode(ovt::ResonatorMode::Piano);
    bar->refresh();
    const bool deadAgain = !root->isActive() && !settingsRootLive();
    setMode(ovt::ResonatorMode::Off);
    bar->refresh();

    check(topBar != nullptr && deadBefore && liveForSeries && deadAgain,
          "and under Equal the root lights, on the bar and in Settings, while "
          "the resonator is in Series, which rings the root's harmonics, and "
          "only then");
  }

  // ---- under Adaptive, Retune joins it -------------------------------------
  pick(st::kTemperament + (int)ovt::Temperament::Adaptive);
  pick(st::kRetune + 1);

  check(retune->isVisible() && retune->getX() > reference->getRight() &&
            std::abs(retune->slider.getValue() - 0.3) < 0.001,
        "under Adaptive the Retune knob shows, after the reference pitch, and "
        "follows the parameter (" +
            std::to_string(retune->slider.getValue()) + " s)");
  check(retune->getHeight() > ovt::ui::OptionBar::kHeight / 2,
        "standing the height of its column, as the top bar's knobs do");

  // Every display at the width its cells need, so a figure in this bar is the
  // same width as one in the Playing bar or on the preset name, and all of
  // it fits across the window that shows all 32 channels.
  {
    bool natural = true, shown = true;
    std::string narrow;
    for (auto *barHere :
         {static_cast<juce::Component *>(bar),
          static_cast<juce::Component *>(bar->getParentComponent())}) {
      for (auto *child : barHere->getChildren())
        if (auto *other = dynamic_cast<ovt::ui::OptionBar *>(child);
            other != nullptr && other->isVisible())
          for (auto *c : other->getChildren())
            if (auto *d = dynamic_cast<SegmentDisplay *>(c)) {
              shown &= d->isVisible();
              if (d->isVisible() &&
                  d->getWidth() < d->naturalWidth(d->getHeight())) {
                natural = false;
                narrow = d->getTitle().toStdString() + ", " +
                         std::to_string(d->getWidth()) + " of " +
                         std::to_string(d->naturalWidth(d->getHeight())) +
                         " at " + std::to_string(d->getHeight());
              }
            }
    }
    check(natural, "every display in the open bar stands at its natural width" +
                       (narrow.empty() ? std::string() : " (" + narrow + ")"));
    check(shown, "and under Adaptive the whole bar fits at 1348 px");
  }

  pick(st::kTemperament + (int)ovt::Temperament::Equal);
  check(!retune->isVisible(), "and goes again when Adaptive does");

  // ---- the order, and the white keys
  {
    pick(st::kTemperament + (int)ovt::Temperament::Adaptive);
    const auto &drift = bar->driftDisplay();
    auto &noDrift = bar->homeSwitch();
    const auto &c = bar->scaleDisplay(0);

    check(drift.isVisible() && drift.getX() > reference->getRight() &&
              drift.getY() == reference->getY() &&
              noDrift.getX() > drift.getRight() &&
              retune->getX() > noDrift.getRight() &&
              c.getX() > retune->getRight(),
          "under Adaptive the bar reads temperament, root, reference and "
          "drift, then the switches, PURE and Retune, then the twelve");
    check(noDrift.getButtonText() == "NO DRIFT" &&
              noDrift.getTitle() == "No drift",
          "with the drift switch called NO DRIFT");
    check(bar->getHeight() == ovt::ui::OptionBar::kHeight,
          "all on one row at 1348 px");

    // The ground beside a white key's display is lighter than beside a black
    // key's, measured just outside each display.
    const auto shot = bar->createComponentSnapshot(bar->getLocalBounds());
    const auto beside = [&shot](const SegmentDisplay &d) {
      return shot.getPixelAt(d.getX() - 1, d.getBounds().getCentreY())
          .getBrightness();
    };
    check(beside(bar->scaleDisplay(0)) > beside(bar->scaleDisplay(1)) + 0.03f &&
              beside(bar->scaleDisplay(4)) >
                  beside(bar->scaleDisplay(3)) + 0.03f,
          "and the white keys stand on a lighter ground than the black");

    pick(st::kTemperament + (int)ovt::Temperament::Just);
    check(!drift.isVisible() && !noDrift.isVisible(),
          "under a fixed temperament the drift and the Adaptive switches go");
    pick(st::kTemperament + (int)ovt::Temperament::Equal);
  }

  // ---- the rows follow what is shown
  // -----------------------------------------
  //
  // In a narrow window Adaptive's groups need more rows than a fixed
  // temperament's, and the bar is laid out again as they come and go, not
  // only when the window moves.
  {
    sizeEditor(*editor, ovt::ui::TopBar::minimumWidth());
    const auto fixedHeight = bar->getHeight();

    pick(st::kTemperament + (int)ovt::Temperament::Adaptive);
    const auto adaptiveHeight = bar->getHeight();
    const auto fits = bar->getHeight() == bar->heightForWidth(bar->getWidth());

    // The twelve are wider together than the narrowest window, so they wrap
    // onto a second line inside their own frame rather than running off it.
    bool twelve = true;
    for (int pc = 0; pc < 12; ++pc) {
      const auto &d = bar->scaleDisplay(pc);
      twelve &= d.isVisible() && bar->getLocalBounds().contains(d.getBounds());
    }
    check(twelve && bar->scaleDisplay(11).getY() > bar->scaleDisplay(0).getY(),
          "and all twelve cents displays stay in the bar, wrapping onto a "
          "second line");

    pick(st::kTemperament + (int)ovt::Temperament::Equal);
    check(adaptiveHeight > fixedHeight && fits &&
              bar->getHeight() == fixedHeight,
          "a narrow Tuning bar takes more rows under Adaptive and gives them "
          "back after (" +
              std::to_string(fixedHeight) + " to " +
              std::to_string(adaptiveHeight) + ")");

    sizeEditor(*editor, 1348, 240);
  }

  opener->onClick();
  restoreSessionSettings(p);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// The Adaptive readouts say what the engine did, chord by chord.
void testAdaptiveReadouts(OvertoniumProcessor &p) {
  section("The Adaptive readouts");

  namespace st = ovt::ui::settings;
  p.applyFactoryPreset(presetIndex("Init"));
  restoreSessionSettings(p);
  p.prepareToPlay(48000.0, 256);
  st::apply(p.apvts, st::kTemperament + (int)ovt::Temperament::Adaptive);

  juce::AudioBuffer<float> buffer(2, 256);
  ovt::AdaptiveSnapshot lastChord;

  // PURE and 7:4 as the Tuning bar lights them for the chord last played.
  juce::Component lampHost;
  ovt::ui::TuningBar lampBar(p.apvts, lampHost);
  const auto pureLit = [&] {
    lampBar.showChord(lastChord);
    return lampBar.pureLamp().getToggleState();
  };
  const auto play = [&](std::initializer_list<int> on,
                        std::initializer_list<int> off) {
    juce::MidiBuffer midi;
    for (const auto k : off)
      midi.addEvent(juce::MidiMessage::noteOff(1, k), 0);
    for (const auto k : on)
      midi.addEvent(juce::MidiMessage::noteOn(1, k, 0.8f), 0);
    for (int block = 0; block < 4; ++block) {
      buffer.clear();
      p.processBlock(buffer, midi);
      midi.clear();
    }

    ovt::AdaptiveSnapshot chord;
    while (p.readAdaptiveSnapshot(chord))
      lastChord = chord;
    return ovt::ui::AdaptiveReading::of(lastChord);
  };

  // ---- a dominant seventh ---------------------------------------------------
  {
    const auto r = play({48, 52, 55, 58}, {});

    check(r.harmonics == "4:5:6:7" && r.seventhOn == "C" &&
              r.verdict == "NOTHING BEATS",
          "C7 reads as harmonics 4:5:6:7 with the seventh on C, nothing "
          "beating (" +
              r.harmonics.toStdString() + ", " + r.seventhOn.toStdString() +
              ", " + r.verdict.toStdString() + ")");

    check(pureLit(), "and lights PURE");

    // The twelve displays light the four held and say where they sit, in
    // cents from equal temperament with A on the reference. Just major on C
    // puts C itself a sixth below A, about 15.6 cents above equal, and the
    // rest sit where the chord puts them from there: the third 13.7 below
    // equal's third over C and the seventh 31.2 below.
    {
      juce::Component popupHost;
      ovt::ui::TuningBar bar(p.apvts, popupHost);
      bar.showChord(lastChord);

      const auto at = [&bar](int pc) {
        return bar.scaleDisplay(pc).getReading().trim().getDoubleValue();
      };
      bool lit = true;
      for (int pc = 0; pc < 12; ++pc)
        lit &= bar.scaleDisplay(pc).isActive() ==
               (pc == 0 || pc == 4 || pc == 7 || pc == 10);

      check(lit, "the four held light and the other eight do not");
      check(bar.driftDisplay().getReading().trim() == "0" &&
                bar.driftDisplay().isActive(),
            "and the drift reads 0, the chord sitting at home");

      // A chord a comma below home, as a progression that pumps leaves one.
      ovt::AdaptiveSnapshot low = lastChord;
      for (int i = 0; i < low.count; ++i)
        low.notes[(size_t)i].version = -1;
      bar.showChord(low);
      check(bar.driftDisplay().getReading().trim() == "-1" &&
                bar.driftDisplay().getTitle() == "Drift: -1 comma",
            "a chord a comma down reads -1 (" +
                bar.driftDisplay().getReading().trim().toStdString() + ")");

      bar.showChord({});
      check(bar.driftDisplay().getReading().isEmpty() &&
                !bar.driftDisplay().isActive(),
            "and nothing held leaves its cells unlit");
      bar.showChord(lastChord);
      check(std::abs(at(0) - 15.6) < 0.3 &&
                std::abs(at(4) - at(0) + 13.7) < 0.15 &&
                std::abs(at(10) - at(0) + 31.2) < 0.15,
            "and C, E and B flat read " + std::to_string(at(0)) + ", " +
                std::to_string(at(4)) + " and " + std::to_string(at(10)) +
                " cents from equal");
    }
    check(r.spoken().contains("harmonics 4:5:6:7") &&
              r.spoken().contains("harmonic seventh on C"),
          "and says so to a screen reader: " + r.spoken().toStdString());

    play({}, {48, 52, 55, 58});
  }

  // ---- a chord that cannot be pure all through ---------------------------
  //
  // C11 cannot be pure all through: pure fifths and pure thirds never meet,
  // and somewhere in it one interval has to give a comma. Which one is the
  // solver's to decide, so what is checked is that it is named and a comma.
  {
    const auto r = play({48, 52, 55, 58, 62, 65}, {});

    check(r.harmonics.isEmpty() && r.givenUp.isNotEmpty() &&
              r.givenUp.contains(" 21") && r.verdict == r.givenUp,
          "C11 names the interval it gives up, a comma off (" +
              r.givenUp.toStdString() + ")");
    check(r.seventhOn.isEmpty(),
          "and has no harmonic seventh, the fourth above C ruling it out");
    check(!pureLit() && lampBar.pureLamp().getTitle().contains("given up"),
          "so PURE stays dark, and says what was given up "
          "to a screen reader (" +
              lampBar.pureLamp().getTitle().toStdString() + ")");

    play({}, {48, 52, 55, 58, 62, 65});
  }

  // ---- a minor triad, which is a series too ------------------------------
  {
    const auto r = play({57, 60, 64}, {});
    check(r.harmonics == "10:12:15" && r.verdict == "NOTHING BEATS",
          "A minor reads as 10:12:15 (" + r.harmonics.toStdString() + ")");
    check(pureLit(), "and lights PURE");
    const auto empty = play({}, {57, 60, 64});
    check(empty.harmonics.isEmpty() && empty.verdict == "NO KEYS HELD",
          "and letting go reads as nothing held");
  }

  // ---- the harmonic seventh switched off -----------------------------------
  {
    auto &cap = lampBar.seventhsSwitch();
    lampBar.refresh();
    check(cap.getToggleState(), "the 7:4 switch starts lit, the seventh on");

    cap.onClick();
    check(!st::isOn(p.apvts, ovt::params::adaptiveSeventhId) &&
              !cap.getToggleState(),
          "a click switches it off and darkens the cap");

    const auto r = play({48, 52, 55, 58}, {});
    check(r.seventhOn.isEmpty() && r.harmonics != "4:5:6:7",
          "and C7 then has no harmonic seventh (" + r.harmonics.toStdString() +
              ")");
    play({}, {48, 52, 55, 58});

    cap.onClick();
    check(st::isOn(p.apvts, ovt::params::adaptiveSeventhId) &&
              cap.getToggleState(),
          "and a second click puts it back");
  }

  // ---- Home
  // ---------------------------------------------------------------------
  {
    auto &cap = lampBar.homeSwitch();
    lampBar.refresh();
    check(!cap.getToggleState(),
          "HOME starts dark, the keyboard free to drift");

    cap.onClick();
    check(st::isOn(p.apvts, ovt::params::adaptiveHomeId) &&
              cap.getToggleState(),
          "a click turns it on and lights the cap");

    ovt::SynthParams out{};
    p.parameters().snapshot(out, 0.0f);
    check(out.global.adaptiveHome, "and the engine is handed it");

    cap.onClick();
    check(!st::isOn(p.apvts, ovt::params::adaptiveHomeId) &&
              !cap.getToggleState(),
          "and a second click turns it off");
  }

  // ---- only under Adaptive -------------------------------------------------
  {
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    sizeEditor(*editor, 1348, 240);

    ovt::ui::TuningBar *bar = nullptr;
    juce::TextButton *opener = nullptr;
    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *b = dynamic_cast<ovt::ui::TuningBar *>(child))
          bar = b;
        if (auto *b = dynamic_cast<juce::TextButton *>(child))
          if (b->getTitle() == "Tuning")
            opener = b;
        look(*child);
      }
    };
    look(*editor);

    check(bar != nullptr && opener != nullptr, "the Tuning bar is there");

    if (bar != nullptr && opener != nullptr) {
      opener->onClick();
      bar->refresh();
      check(bar->pureLamp().isVisible() && bar->seventhsSwitch().isVisible() &&
                bar->homeSwitch().isVisible() &&
                bar->driftDisplay().isVisible() &&
                !bar->pureLamp().getWantsKeyboardFocus(),
            "PURE, 7:4 and DRIFT show under Adaptive, as indicators that "
            "take no focus");

      st::apply(p.apvts, st::kTemperament + (int)ovt::Temperament::Just);
      bar->refresh();
      check(!bar->pureLamp().isVisible() && !bar->driftDisplay().isVisible(),
            "and go under a fixed temperament");

      opener->onClick();
    }
  }

  restoreSessionSettings(p);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// A display has a fixed set of cells, each with its bars and a decimal
/// point, and a reading only lights some of them, the way a real one does.
void testSegmentCells() {
  section("Segment displays have cells, and light them");

  using ovt::ui::SegmentDisplay;

  const auto render = [](const juce::String &reading, bool lit,
                         SegmentDisplay::Bars bars =
                             SegmentDisplay::Bars::Seven,
                         int cells = 4) {
    SegmentDisplay d({}, bars, cells);
    d.setBounds(0, 0, 92, 30);
    d.setReading(reading, lit);
    return d.createComponentSnapshot(d.getLocalBounds());
  };

  const auto differ = [](const juce::Image &a, const juce::Image &b, int fromX,
                         int toX) {
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
      for (int x = fromX; x < toX; ++x)
        n += a.getPixelAt(x, y) != b.getPixelAt(x, y) ? 1 : 0;
    return n;
  };

  // ---- the figures keep their size and place --------------------------------
  //
  // A number stands to the right, so its last two cells are the same whether
  // a tens digit stands in front of them or not.
  {
    const auto short_ = render("8.8", true), long_ = render("88.8", true);
    const auto half = short_.getWidth() / 2;
    check(differ(short_, long_, half, short_.getWidth()) == 0 &&
              differ(short_, long_, 0, half) > 0,
          "8.8 and 88.8 draw their last cells alike, the figures keeping "
          "their size");
  }

  // ---- a point is a cell's own
  // ------------------------------------------------
  {
    const auto pointed = render("88.8", true), plain = render("888", true);
    check(differ(pointed, plain, 0, pointed.getWidth()) > 0,
          "a point lights the point of the cell before it");

    SegmentDisplay d({}, SegmentDisplay::Bars::Seven, 4);
    d.setBounds(0, 0, 92, 30);
    d.setReading("-12.5", true);
    d.createComponentSnapshot(d.getLocalBounds());
    check(d.cellsDrawn() == 5, "and -12.5 fits four cells, the point riding "
                               "on the two (" +
                                   std::to_string(d.cellsDrawn()) + ")");
  }

  // ---- off is unlit, not grey
  // -------------------------------------------------
  {
    const auto off = render("88.8", false), empty = render({}, true);
    check(differ(off, empty, 0, off.getWidth()) == 0,
          "a display with nothing to say shows its unlit bars and nothing "
          "else, whatever its reading");

    const auto offName =
                   render("ADAPTIVE", false, SegmentDisplay::Bars::Fourteen, 8),
               emptyName = render({}, true, SegmentDisplay::Bars::Fourteen, 8);
    check(differ(offName, emptyName, 0, offName.getWidth()) == 0,
          "fourteen bars as well as seven");
  }

  // ---- the sharp is a sign, not a letter
  // -------------------------------------
  {
    const auto withSign = [](const juce::String &reading) {
      SegmentDisplay d({}, SegmentDisplay::Bars::Fourteen, 1);
      d.showSharpSign(true);
      d.setBounds(0, 0, 40, 30);
      d.setReading(reading, true);
      return std::pair<juce::String, juce::Image>{
          d.getReading(), d.createComponentSnapshot(d.getLocalBounds())};
    };

    const auto [sharpReading, sharp] = withSign("F#");
    const auto [plainReading, plain] = withSign("F");

    // The letter's cell is the left half or so, the sign the right.
    const auto signFrom = sharp.getWidth() / 2;
    check(sharpReading == "F#" && plainReading == "F" &&
              differ(sharp, plain, signFrom, sharp.getWidth()) > 0 &&
              differ(sharp, plain, 0, signFrom - 4) == 0,
          "F# lights the sharp sign beside an F, and the F is the same F");
  }

  // ---- a middle bar reads as a bar
  //
  // Each half runs to the centre of the cell, so the middle of an H is a bar
  // and not two dots. Measured at the size the names are drawn, nine cells in a
  // bar's height, against the bottom bar, which runs the whole width of a cell:
  // the middle has to be lit nearly as much.
  {
    // How much light a reading adds over the same display with nothing on
    // it, so the screen's own ground and rim count for nothing.
    const auto shotOf = [](const juce::String &reading) {
      SegmentDisplay d({}, SegmentDisplay::Bars::Fourteen, 9);
      d.setBounds(0, 0, 108, 24);
      d.setReading(reading, true);
      return d.createComponentSnapshot(d.getLocalBounds());
    };
    const auto empty = shotOf({});
    const auto litIn = [&](const juce::String &reading) {
      const auto im = shotOf(reading);
      int n = 0;
      for (int y = 0; y < im.getHeight(); ++y)
        for (int x = 0; x < im.getWidth(); ++x)
          n += juce::jmax(0, (int)im.getPixelAt(x, y).getBlue() -
                                 (int)empty.getPixelAt(x, y).getBlue());
      return n;
    };

    const auto middle = litIn("---------"), bottom = litIn("_________");
    std::printf("  middle bars add %d of light, bottom bars %d\n", middle,
                bottom);
    check(middle * 10 >= bottom * 6,
          "a fourteen-bar middle is lit nearly as much as its bottom bar (" +
              std::to_string(middle) + " against " + std::to_string(bottom) +
              ")");
  }

  // ---- over range
  // -------------------------------------------------------------
  {
    SegmentDisplay d({}, SegmentDisplay::Bars::Seven, 4);
    d.setBounds(0, 0, 92, 30);
    d.setReading("12345", true);
    const auto over = d.createComponentSnapshot(d.getLocalBounds());
    const auto dashes = render("----", true);

    check(d.cellsDrawn() == 0 && differ(over, dashes, 0, over.getWidth()) == 0,
          "a reading longer than its cells shows a minus in every cell");
  }
}

/// The tool button shows the tool as soon as it is chosen.
///
/// The button draws from a cache of its last looks, keyed by what its drawing
/// reads, so the icon has to be part of the key.
void testToolButtonShowsTheTool(OvertoniumProcessor &p) {
  section("The tool button shows the tool");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Found by having no words on it: it wears the tool's cursor instead.
  juce::TextButton *button = nullptr;
  std::function<void(juce::Component &)> look = [&](juce::Component &c) {
    if (auto *b = dynamic_cast<juce::TextButton *>(&c))
      if (b->getButtonText().isEmpty() &&
          b->getTitle().containsIgnoreCase("tool"))
        button = b;
    for (auto *child : c.getChildren())
      look(*child);
  };

  editor->chooseTool(ovt::ui::PointerTool::Pointer);
  look(*editor);

  check(button != nullptr, "the gutter carries the tool button");
  if (button == nullptr)
    return;

  const auto shot = [button] {
    return button->createComponentSnapshot(button->getLocalBounds(), true,
                                           1.0f);
  };

  const auto same = [](const juce::Image &a, const juce::Image &b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
      return false;

    for (int y = 0; y < a.getHeight(); ++y)
      for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
          return false;

    return true;
  };

  const auto pointer = shot();

  editor->chooseTool(ovt::ui::PointerTool::Link);
  const auto link = shot();

  editor->chooseTool(ovt::ui::PointerTool::Draw);
  const auto draw = shot();

  editor->chooseTool(ovt::ui::PointerTool::Pointer);
  const auto back = shot();

  check(!same(pointer, link), "choosing Link changes the icon at once");
  check(!same(link, draw), "and so does choosing Draw");
  check(same(pointer, back), "and the pointer comes back as it was");
}

/// Picking channels by their numbers, for the Selected scope.
///
/// A command-click on a number picks that channel, and a drag from there
/// across the numbers picks every channel it crosses. Making a selection
/// chooses LINK with the Selected scope, and a LINK drag then moves only the
/// channels picked. Starting on a picked channel takes channels away instead,
/// the selection is kept with the session, and the menu can clear it.
void testSelectingChannels(OvertoniumProcessor &p) {
  section("Selecting channels");

  const auto open = [&p] {
    return std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());
  };

  const auto stripsOf = [](juce::Component &root) {
    std::vector<ovt::ui::ChannelStrip *> strips;
    std::function<void(juce::Component &)> gather = [&](juce::Component &c) {
      if (auto *s = dynamic_cast<ovt::ui::ChannelStrip *>(&c))
        strips.push_back(s);
      for (auto *child : c.getChildren())
        gather(*child);
    };
    gather(root);
    return strips;
  };

  const auto picked = [](const std::vector<ovt::ui::ChannelStrip *> &strips) {
    std::string out;
    for (size_t i = 0; i < strips.size(); ++i)
      if (strips[i]->isSelected())
        out += std::to_string(i + 1) + " ";
    return out;
  };

  {
    auto base = open();
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());
    auto *bar = editor != nullptr ? findTopBar(*editor) : nullptr;
    auto *target = dynamic_cast<ovt::ui::LinkTarget *>(editor);

    check(editor != nullptr && bar != nullptr && target != nullptr,
          "the editor opens and takes selections");
    if (editor == nullptr || bar == nullptr || target == nullptr)
      return;

    auto strips = stripsOf(*editor);

    if (strips.size() != (size_t)ovt::kNumHarmonics)
      return;

    editor->chooseTool(ovt::ui::PointerTool::Pointer);

    const auto number = [&strips](int i) {
      return strips[(size_t)i]->localPointToGlobal(
          juce::Point<int>(strips[(size_t)i]->getWidth() / 2, 10));
    };

    // ---- a drag across the numbers picks a run of channels -----------------
    target->selectionStarted(2, number(2));
    target->selectionMovedTo(number(5));
    target->selectionEnded();

    check(picked(strips) == "3 4 5 6 ",
          "a drag from channel 3 to 6 picks the four (" + picked(strips) + ")");

    check(editor->currentTool() == ovt::ui::PointerTool::Link &&
              bar->getLinkScope() == ovt::ui::LinkScope::Selected,
          "and making a selection chooses LINK with the Selected scope");

    // ---- a LINK drag moves only the channels picked ------------------------
    //
    // On the pan knobs, which LINK moves in a straight line from wherever each
    // one is. The faders are shared out in decibels, so one standing at the
    // bottom would stay there and look left out.
    const auto panOf = [&p](int i) {
      auto *param = p.apvts.getParameter(
          ovt::params::oscParamId(ovt::params::panSuffix, i));
      return param != nullptr ? param->getValue() : -1.0f;
    };

    std::array<float, ovt::kNumHarmonics> before{};
    for (int i = 0; i < ovt::kNumHarmonics; ++i)
      before[(size_t)i] = panOf(i);

    auto *source =
        dynamic_cast<juce::RangedAudioParameter *>(p.apvts.getParameter(
            ovt::params::oscParamId(ovt::params::panSuffix, 3)));

    if (source == nullptr)
      return;

    // A little either way of where it stands, so it cannot meet an end.
    const auto to = source->convertFrom0to1(
        source->getValue() + (source->getValue() > 0.5f ? -0.1f : 0.1f));

    target->linkDragStarted(ovt::ui::Role::Pan, 3);
    source->setValueNotifyingHost(source->convertTo0to1(to));
    target->linkValueChanged(ovt::ui::Role::Pan, 3, to);
    target->linkDragEnded(ovt::ui::Role::Pan, 3);

    std::string moved;
    for (int i = 0; i < ovt::kNumHarmonics; ++i)
      if (std::abs(panOf(i) - before[(size_t)i]) > 1.0e-4f)
        moved += std::to_string(i + 1) + " ";

    check(moved == "3 4 5 6 ",
          "a LINK drag on channel 4 moves the picked channels and no others (" +
              moved + ")");

    // Put back, so the tests after this one find the pans they expect.
    for (int i = 0; i < ovt::kNumHarmonics; ++i)
      if (auto *param = p.apvts.getParameter(
              ovt::params::oscParamId(ovt::params::panSuffix, i)))
        param->setValueNotifyingHost(before[(size_t)i]);

    // ---- starting on a picked channel takes channels away ------------------
    target->selectionStarted(4, number(4));
    target->selectionMovedTo(number(5));
    target->selectionEnded();

    check(picked(strips) == "3 4 ",
          "a drag that starts on a picked channel takes channels away (" +
              picked(strips) + ")");
  }

  // ---- kept with the session ------------------------------------------------
  {
    auto base = open();
    auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());
    auto *bar = editor != nullptr ? findTopBar(*editor) : nullptr;

    if (editor == nullptr || bar == nullptr)
      return;

    auto strips = stripsOf(*editor);

    check(picked(strips) == "3 4 ",
          "the selection is still there when the window opens again (" +
              picked(strips) + ")");

    check(bar->onSelectionCleared != nullptr,
          "and the menu can ask for it to be cleared");

    if (bar->onSelectionCleared)
      bar->onSelectionCleared();

    check(picked(strips).empty(), "which leaves no channel picked");

    editor->chooseTool(ovt::ui::PointerTool::Pointer);
  }

  {
    auto base = open();
    auto strips = stripsOf(*base);

    check(picked(strips).empty(), "and that is kept with the session too");
  }
}

/// LINK comes back the way it was left: its switch, scope and curve all survive
/// the window being closed and opened. All three are checked so a new setting
/// cannot be the one forgotten.
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
  // A setting written only when it is true reads as working until it is turned
  // off.
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
/// Held, a drag across the fader area sets each column it crosses from the
/// pointer's height instead of moving one of them. Unlike LINK, which shares
/// one relative move out across a scope by a rule, this sets absolute values
/// freehand.
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
  // latches the tool on without the modifier, since a headless run cannot hold
  // a key.
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

  // One button for the three tools, wearing the cursor rather than a word,
  // so it is found by what it is called rather than by what it says.
  auto *toolSwitch = switchNamed("");

  check(toolSwitch != nullptr, "the gutter carries the tool button");

  if (toolSwitch == nullptr)
    return;

  // Chosen through the editor rather than through the menu the button opens,
  // since a PopupMenu needs a real window.
  const auto choose = [editor](ovt::ui::PointerTool which) {
    editor->chooseTool(which);
  };

  check(editor->currentTool() == ovt::ui::PointerTool::Pointer ||
            editor->currentTool() == ovt::ui::PointerTool::Link,
        "and starts on a tool that is not drawing");

  // ---- the menu knows which tool is current -------------------------------
  //
  // The tick follows the current tool, and LINK's two lists are live when LINK
  // is chosen. Read as data, since a menu needs a window to open in.
  {
    // Only the three tools. The scope and the curve carry ticks of their
    // own, which are theirs to carry and say nothing about the tool.
    const auto toolTickedIn = [](juce::PopupMenu &menu) {
      std::string found;

      for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        for (int i = 0; i < (int)ovt::ui::PointerTool::NumTools; ++i)
          if (it.getItem().isTicked &&
              it.getItem().text ==
                  ovt::ui::pointerToolName((ovt::ui::PointerTool)i))
            found += it.getItem().text.toStdString() + " ";

      return found;
    };

    // Asked for by the name the menu gives it, "Odd harmonics".
    const auto scopeEnabled = [](juce::PopupMenu &menu) {
      const auto wanted =
          juce::String(ovt::ui::linkScopeName(ovt::ui::LinkScope::Odd));

      for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text == wanted)
          return it.getItem().isEnabled;

      return false;
    };

    for (auto which :
         {ovt::ui::PointerTool::Pointer, ovt::ui::PointerTool::Link,
          ovt::ui::PointerTool::Draw}) {
      editor->chooseTool(which);

      auto menu = ovt::ui::buildToolMenu(editor->currentTool(),
                                         {which == ovt::ui::PointerTool::Link,
                                          ovt::ui::LinkScope::All,
                                          ovt::ui::LinkCurve::Uniform});

      const auto wanted = std::string(ovt::ui::pointerToolName(which)) + " ";

      check(toolTickedIn(menu) == wanted,
            std::string("the menu ticks ") + ovt::ui::pointerToolName(which) +
                " when it is the tool (" + toolTickedIn(menu) + ")");

      check(scopeEnabled(menu) == (which == ovt::ui::PointerTool::Link),
            std::string("and LINK's lists are live only for Link (") +
                ovt::ui::pointerToolName(which) + ")");
    }

    editor->chooseTool(ovt::ui::PointerTool::Pointer);
  }

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
    choose(ovt::ui::PointerTool::Draw);

    check(editor->currentTool() == ovt::ui::PointerTool::Draw,
          "choosing Draw is what latches it");

    // ---- and the faders light to say where the drawing reaches ------------
    //
    // The same preview LINK uses, in the accent rather than each channel's own
    // colour, since every fader is equally drawable.
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
  // The setting itself is untouched and comes back the moment the tool is let
  // go.
  {
    check(editor->currentTool() != ovt::ui::PointerTool::Link,
          "LINK reads as off while drawing has the drag");

    choose(ovt::ui::PointerTool::Pointer);

    check(editor->currentTool() == ovt::ui::PointerTool::Pointer,
          "and choosing the plain pointer gives the drag back");

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

/// The tick in the Zoom submenu, which is the only thing on the panel that says
/// which zoom you are at.
///
/// The bar holds a copy of the editor's zoom to draw the tick from, so every
/// zoom change has to reach it. Driven through the callback the menu item
/// calls, since opening the menu needs a window.
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

  // Every zoom the menu offers, in turn, ending at 100% for whatever runs next.
  for (const auto factor : {1.25f, 0.75f, 1.5f, 1.0f}) {
    const auto reads = std::to_string(juce::roundToInt(factor * 100.0f)) + "%";

    bar->onZoomChanged(factor);

    check(ticked() == reads,
          "the window at " + reads + " ticks " + reads + " (" + ticked() + ")");
  }
}

/// That every word the bar has to say fits in the button that says it.
///
/// The bar picks its font from its own height, so the widest name a control can
/// show decides how wide it has to be. Measured, since a name that does not fit
/// is drawn with the middle taken out and nothing says so.
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
  // when the effect is off.
  juce::StringArray echoWords{"Off"};
  for (int i = 0; i < (int)ovt::EchoType::NumTypes; ++i)
    echoWords.add(ovt::echoTypeShortName((ovt::EchoType)i));

  // The short names again, which is what the button shows.
  juce::StringArray reverbWords{"Off"};
  for (int i = 0; i < (int)ovt::ReverbType::NumTypes; ++i)
    reverbWords.add(ovt::reverbTypeShortName((ovt::ReverbType)i));

  juce::StringArray resonatorWords;
  for (int i = 0; i < (int)ovt::ResonatorMode::NumModes; ++i)
    resonatorWords.add(ovt::resonatorModeName((ovt::ResonatorMode)i));

  const auto resonatorText = widest(resonatorWords);

  std::printf("  the resonator's longest word is %d px, in a button of %d\n",
              resonatorText, TopBar::kResonatorWidth);

  check(resonatorText + 8 <= TopBar::kResonatorWidth,
        "every mode the resonator can be in fits in its button (" +
            std::to_string(resonatorText) + " px in " +
            std::to_string(TopBar::kResonatorWidth) + ")");

  const auto reverbText = widest(reverbWords);
  const auto echoText = widest(echoWords);
  const auto characterText = widest(ovt::params::characterChoices());

  // Each of them, since the comment on kCharacterWidth lists them.
  for (const auto &name : ovt::params::characterChoices())
    std::printf("  %-9s is %d px\n", name.toUpperCase().toRawUTF8(),
                widest({name}));

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
  // buttons get, so it is measured at that.
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
  // The group is named underneath the button in the band the knob captions
  // occupy, at their font. A word wider than its button would run into the knob
  // caption six pixels to its right.
  {
    const auto captionFont = makeFont(9.0f, true);

    const int widths[] = {TopBar::kCharacterWidth, TopBar::kResonatorWidth,
                          TopBar::kEchoWidth, TopBar::kReverbWidth};

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
void testBarComesOntoOneRow(OvertoniumProcessor &p) {
  section("One row");

  using namespace ovt::ui;

  const auto tall = TopBar::heightForWidth(900);
  int onOneRow = 0;

  for (int w = 900; w <= 1600; ++w)
    if (TopBar::heightForWidth(w) < tall) {
      onOneRow = w;
      break;
    }

  const auto opensAt = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 +
                       ovt::kNumHarmonics * ovt::ui::kStripWidth +
                       ovt::ui::kScrollBarThickness;

  std::printf("  the bar comes onto one row at %d px, and the window opens at "
              "%d, so there are %d px of slack\n",
              onOneRow, opensAt, opensAt - onOneRow);

  check(onOneRow > 0 && onOneRow <= opensAt,
        "the bar is on one row at the width the window opens at (" +
            std::to_string(onOneRow) + ")");

  // The wordmark stands over the preset screen at the screen's width, and the
  // row has to hold both. The wordmark is 5.5 times as wide as it is tall.
  {
    juce::Component popupParent;
    TopBar bar(p.apvts, popupParent);
    bar.setPresetName("Big Saw");
    bar.setSize(1350, TopBar::heightForWidth(1350));

    const juce::Component *screen = nullptr;
    for (auto *c : bar.getChildren())
      if (c->getTitle().startsWith("Preset:"))
        screen = c;

    const auto room = screen != nullptr ? screen->getY() : 0;
    const auto wordmark =
        screen != nullptr
            ? juce::roundToInt((float)screen->getWidth() * 448.0f / 2464.0f)
            : 1000;

    check(screen != nullptr && room >= wordmark + 2 + 6,
          "the wordmark fits over the preset screen at its width (" +
              std::to_string(room) + " px above the screen for a " +
              std::to_string(wordmark) + " px wordmark)");
  }

  // And where it comes onto two and three, which the design notes also give.
  for (int w = 475, lastHeight = TopBar::heightForWidth(475); w < onOneRow; ++w)
    if (TopBar::heightForWidth(w) < lastHeight) {
      lastHeight = TopBar::heightForWidth(w);
      std::printf("  it gets a row shorter at %d px\n", w);
    }

  /// The figure the design notes quote, which moves whenever a control on the
  /// bar changes width.
  check(onOneRow == 1285, "and comes onto it at the width written down (" +
                              std::to_string(onOneRow) + ")");
}

/// A knob carries its caption underneath, so its dial does not sit in the
/// middle of the row. Checks that the bar's controls line up with the knobs'
/// dials rather than with the middle of the row.
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
      // rather than to the row. It is checked against the meter below.
      if (auto *slider = dynamic_cast<juce::Slider *>(child))
        if (slider->getSliderStyle() == juce::Slider::LinearHorizontal)
          continue;

      // Nor the clipper's switch, which stands in the caption band beside the
      // readouts rather than on the line with the other buttons. Checked with
      // them below, for the same reason they are.
      if (auto *button = dynamic_cast<juce::TextButton *>(child))
        if (button->getButtonText() == TopBar::kClipName)
          continue;

      // Nor the four buttons that open something, which stand two by two and
      // fill the row's height rather than lining up with the dials. Their own
      // two lines are checked in testOptionBars.
      if (auto *button = dynamic_cast<juce::TextButton *>(child)) {
        const auto word = button->getButtonText();
        if (word == "SETTINGS" || word == "MACROS" || word == "PLAYING" ||
            word == "TUNING")
          continue;
      }

      // A knob's line is its dial, not the control, which reaches further down
      // to hold the caption.
      if (auto *knob = dynamic_cast<LabelledKnob *>(child))
        centres.push_back(knob->getY() + knob->slider.getBounds().getCentreY());
      else
        centres.push_back(child->getBounds().getCentreY());
    }

    return centres;
  };

  // The fader has to stand on the lamps rather than on the whole meter, which
  // keeps its decibel marks in a strip under the bars.
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

  // And the three things under the meter sit under it, inside the row, at every
  // width the bar can be given, since the rule above leaves them out.
  for (int width : {1412, 1100, 900, TopBar::minimumWidth()}) {
    bar.setSize(width, TopBar::heightForWidth(width));

    juce::Rectangle<int> meterBounds;
    juce::Array<juce::Rectangle<int>> under;

    for (auto *child : bar.getChildren()) {
      if (dynamic_cast<StereoOutputMeter *>(child) != nullptr)
        meterBounds = child->getBounds();

      // The converter readouts, which count. The preset name is a display on
      // this bar too and does not sit under the meter, and it is told apart
      // by having the cells to spell with.
      if (auto *d = dynamic_cast<SegmentDisplay *>(child))
        if (d->howManyBars() == SegmentDisplay::Bars::Seven)
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

  // At the width the window opens at, both readouts have to be wide enough to
  // name what they are counting as well as the figure.
  {
    // The width the window opens at, scrollbar included, since the converter
    // readouts live on that margin.
    const int width = ovt::ui::kGutterWidth + ovt::ui::kStripWidth + 8 +
                      ovt::kNumHarmonics * ovt::ui::kStripWidth +
                      ovt::ui::kScrollBarThickness;

    bar.setSize(width, TopBar::heightForWidth(width));

    // A window this wide can hold the whole bar on one line. Anything wider
    // cannot need fewer rows than this, so it is the comparison to make.
    check(TopBar::heightForWidth(width) == TopBar::heightForWidth(2400),
          "the bar lays out in one row at the width the window opens at (" +
              std::to_string(width) + " px)");

    int named = 0;

    // The two that have a unit to name. The preset display is the third on
    // this bar and has none: a name is not measured in anything.
    for (auto *child : bar.getChildren())
      if (auto *d = dynamic_cast<SegmentDisplay *>(child))
        if (d->howManyBars() == SegmentDisplay::Bars::Seven)
          named += SegmentDisplay::hasRoomForUnit(child->getWidth()) ? 1 : 0;

    check(named == 2, "and both converter readouts can name their unit there");
  }
}

/// A factory preset gives the same instrument whatever was loaded before it.
///
/// Checked against every parameter rather than a list, so it catches a new
/// global that a preset forgets to reset.
void testPresetsAreReproducible(OvertoniumProcessor &p) {
  section("Presets start from a known state");

  // What a preset deliberately leaves alone is how you play it, as opposed to
  // what it sounds like, which includes how loud it is. That comes from the one
  // shared list.

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

    // The character is in here too. A preset that says nothing about it asks
    // for Pure rather than whatever the last patch had.
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
      put(ovt::params::oscParamId(ovt::params::glideSuffix, i), 1.5f);
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

  // What a preset is not allowed to touch has to still be there afterwards,
  // which the comparison above does not check.
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

/// Knob sizes. A knob takes the size of the row it lands in, so the sizes that
/// differ should differ on purpose.
void testKnobSizes(OvertoniumProcessor &p) {
  section("Knob sizes");

  std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  check(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  // Tall enough that nothing scrolls, since a knob scrolled out of the band is
  // not drawn at all.
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

/// No parameter has travel at the bottom of its knob that does nothing.
///
/// A power curve fitted through a midpoint across several decades, such as
/// setSkewForCentre gives, leaves its low end with a slope of exactly zero. So
/// every parameter is checked.
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
  // A host sets a parameter to its default and reads it straight back, and
  // auval reports a parameter that does not hold it. Exact equality on purpose,
  // since the discrepancy a float conversion gives is a single bit.
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

  // And the shortest attack time really is 0.2 ms. On a partial it is where the
  // phase region meets the times, so it has to convert there and back exactly.
  // The noise channel has no cycle to start in, so its range begins there.
  const auto attackId = ovt::params::oscParamId(ovt::params::attackSuffix, 0);

  if (auto *param = p.apvts.getParameter(attackId)) {
    const auto &range = param->getNormalisableRange();
    const auto there = range.convertFrom0to1(range.convertTo0to1(0.0002f));

    check(std::abs(there - 0.0002f) < 1.0e-7f && range.start < 0.0f,
          "a partial's shortest attack time is 0.2 ms, with the phase region "
          "below it");
  }

  if (auto *noise = p.apvts.getParameter(
          ovt::params::noiseParamId(ovt::params::attackSuffix)))
    check(std::abs(noise->getNormalisableRange().start - 0.0002f) < 1.0e-7f,
          "and the noise channel's attack starts at 0.2 ms");
}

/// The undo history, which holds what a person did and nothing else, and brings
/// a LINK drag across 32 channels back in one step.
///
/// A control opens a gesture around whatever it writes, which is what the
/// processor listens for, and automation writes with no gesture. So these drive
/// the parameters the way a control does, and the next test does the opposite.
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
/// Automation playback is not an edit and leaves no history.
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

  // Parameter values reach the value tree on a timer. Flushed by hand here, so
  // this cannot pass by asking before anything was written.
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

/// The bus layouts the plugin offers: one output bus, stereo or mono, no
/// inputs, MIDI in and none out.
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
/// Not every host hands over a channel for every enabled bus, so nothing may
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
/// Mono is the only output that runs a sum.
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

  // The default patch is centred, so both channels carry the same thing and the
  // fold gives back what either held. A fold that dropped a channel or halved
  // the sum would land at half this.
  check(mono > stereo * 0.8f && mono < stereo * 1.25f,
        "and the fold keeps the level of a centred patch rather than halving "
        "it");
}

/// The session properties the editor keeps on the state tree, written from the
/// message thread while a host may be saving from another one.
///
/// Run ordinarily this proves little, since the window in which the two threads
/// collide is a few instructions wide. Its value is under the thread sanitiser,
/// which reports a race from the accesses rather than from a collision.
///
/// Both guarded paths are exercised: the background thread saves, and this one
/// restores between editor actions.
void testTheSessionSurvivesAHostSavingUnderIt() {
  section("The session survives a host saving under it");

  using ovt::ui::PointerTool;

  OvertoniumProcessor proc;

  std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(ed.get());

  check(editor != nullptr, "a window opens for the host to save under");

  if (editor == nullptr)
    return;

  sizeEditor(*editor, 1340);

  std::atomic<bool> running{true};
  std::atomic<int> saves{0};
  std::atomic<int> malformed{0};
  std::atomic<int> incomplete{0};

  // What a host does when it autosaves: ask for the state, over and over,
  // from whichever thread it keeps for the purpose.
  std::thread saver([&] {
    while (running.load()) {
      juce::MemoryBlock block;
      proc.getStateInformation(block);

      auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(),
                                                        (int)block.getSize());

      if (xml == nullptr) {
        ++malformed;
        continue;
      }

      // A save that came back without the parameters is a save that would
      // have restored an empty instrument.
      if (juce::ValueTree::fromXml(*xml).getNumChildren() == 0)
        ++incomplete;

      ++saves;
    }
  });

  // And what the player does meanwhile: the four things in the window that
  // write to that tree.
  const PointerTool tools[] = {PointerTool::Link, PointerTool::Draw,
                               PointerTool::Pointer};

  for (int round = 0; round < 400; ++round) {
    editor->scrollParameters(round % 2 == 0 ? 3 : -3);
    editor->chooseTool(tools[round % 3]);

    if (auto *bar = findTopBar(*editor))
      if (bar->onLinkSettingsChanged != nullptr)
        bar->onLinkSettingsChanged();

    sizeEditor(*editor, 1200 + (round % 8) * 20);

    // The other guarded path, from this thread, since one restore racing
    // another is not something a host does.
    if (round % 50 == 0) {
      juce::MemoryBlock carried;
      proc.getStateInformation(carried);
      proc.setStateInformation(carried.getData(), (int)carried.getSize());
    }
  }

  running = false;
  saver.join();

  check(saves.load() > 0,
        "the host got its saves (" + std::to_string(saves.load()) + ")");
  check(malformed.load() == 0, "none of them came back unreadable (" +
                                   std::to_string(malformed.load()) + ")");
  check(incomplete.load() == 0, "and none came back without the parameters (" +
                                    std::to_string(incomplete.load()) + ")");

  // The window is still the one that was being driven.
  editor->chooseTool(PointerTool::Draw);

  juce::MemoryBlock carried;
  proc.getStateInformation(carried);
  proc.setStateInformation(carried.getData(), (int)carried.getSize());

  ed.reset();

  std::unique_ptr<juce::AudioProcessorEditor> reopened(proc.createEditor());
  auto *second = dynamic_cast<OvertoniumEditor *>(reopened.get());

  check(second != nullptr, "and a window opens again afterwards");

  if (second != nullptr) {
    sizeEditor(*second, 1340);
    check(second->currentTool() == PointerTool::Draw,
          "on the tool it was left on");
  }
}

/// The state round trip: parameters survive being saved and restored, and a
/// chunk that is not ours changes nothing.
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
/// A VST3 session restore sets every parameter, the program among them, so
/// reopening a session built on a preset has to come back with the edits, not
/// with the preset.
void testPrograms(OvertoniumProcessor &p) {
  section("Programs");

  const auto names = ovt::presets::names();

  // Programs are the Audio Unit's alone. Everything else reports one, so the
  // VST3 wrapper publishes no Program parameter. This harness is a console app,
  // so the wrapper type is undefined and the count is the collapsed one.
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

  // A parameter the presets set, so the value read below is the preset's.
  // Anything a preset left alone would survive the reload and pass without
  // proving anything.
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

  // The host then tells the plugin which program the session was on. It is the
  // one already loaded, so this has to be a no-op, or the edit above would be
  // lost.
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
  // Factory presets sort alphabetically, so adding one moves the index of every
  // preset after it. The saved name decides and the index is the fallback.
  // Simulated by writing a wrong index into a state that carries the right
  // name.
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

  // A name that is not a factory preset is a user preset or an edited patch, so
  // the index is all there is to go on.
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
/// is called by hand.
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

    // Handed to a window the way the standalone hands it over. A document
    // window is 128 px square until told otherwise, and the editor must not be
    // squashed into that during the handover.
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

  // The window outlives the editor, and the look and feel it was given belongs
  // to the editor. A component holds its look and feel by weak reference and
  // falls back to the application's own, which is what keeps this from
  // dangling.
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
  // Init says nothing about the character, which means the plain oscillator,
  // set by the neutral base.
  p.applyFactoryPreset(presetIndex("Init"));

  check(current() == ovt::Character::Pure,
        "a factory preset that says nothing puts it back to Pure");

  // The pair the site plays against each other to show what TUNE does. They
  // differ in that one control only, so neither has a character.
  for (auto *name : {"Just Saw", "Equal Saw"}) {
    set(ovt::Character::Valve);
    p.applyFactoryPreset(presetIndex(name));

    check(current() == ovt::Character::Pure,
          juce::String(name).toStdString() + " stays on Pure on purpose");
  }

  // And the factory set asks for characters at all, so a preset load that
  // dropped the character would fail here.
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
      // adds nothing.
      check(bar->isCharacterLit(), "a character lights the button");

      check(bar->getCharacterColour() ==
                ovt::ui::characterColour(ovt::Character::Bulb),
            "in its own colour");

      check(bar->getCharacterRingColour() == bar->getCharacterColour(),
            "and the knobs in its frame light with it");

      set(ovt::Character::Pure);
      bar->updatePanelReadouts(48000.0);

      check(!bar->isCharacterLit(), "and Pure does not light it");

      check(bar->getCharacterRingColour() == ovt::ui::colours::accent,
            "while its knobs, which still work on Pure, keep the accent");
    }
  }

  // The three measured from a synthesizer or a chip light in their
  // instrument's colour instead.
  check(ovt::ui::characterColour(ovt::Character::Sid) ==
                juce::Colour(0xff867ade) &&
            ovt::ui::characterColour(ovt::Character::Pd) ==
                juce::Colour(0xffd5df7c) &&
            ovt::ui::characterColour(ovt::Character::Fm) ==
                juce::Colour(0xff00825f),
        "6581, PD and FM light in their instrument's colour");

  // Each is there to be told apart from the others, so a list that left one
  // out, or gave two the same, would defeat it.
  {
    std::vector<juce::Colour> seen;

    for (int c = 1; c < (int)ovt::Character::NumCharacters; ++c) {
      const auto colour = ovt::ui::characterColour((ovt::Character)c);

      check(std::find(seen.begin(), seen.end(), colour) == seen.end(),
            std::string(ovt::characterName((ovt::Character)c)) +
                " has a colour of its own");

      seen.push_back(colour);
    }
  }

  // The machines likewise, within each set, and the echo and the reverb
  // beside each other never on the same colour.
  {
    const auto distinct = [](const std::vector<juce::Colour> &colours) {
      for (size_t i = 0; i < colours.size(); ++i)
        for (size_t j = i + 1; j < colours.size(); ++j)
          if (colours[i] == colours[j])
            return false;

      return true;
    };

    std::vector<juce::Colour> resonator, echo, reverb, clip, beside;

    for (int m = 1; m < (int)ovt::ResonatorMode::NumModes; ++m)
      resonator.push_back(ovt::ui::resonatorColour((ovt::ResonatorMode)m));

    for (int t = 0; t < (int)ovt::EchoType::NumTypes; ++t)
      echo.push_back(ovt::ui::echoColour((ovt::EchoType)t));

    for (int t = 0; t < (int)ovt::ReverbType::NumTypes; ++t)
      reverb.push_back(ovt::ui::reverbColour((ovt::ReverbType)t));

    for (int t = 0; t < (int)ovt::ClipType::NumTypes; ++t)
      clip.push_back(ovt::ui::clipColour((ovt::ClipType)t));

    beside = echo;
    beside.insert(beside.end(), reverb.begin(), reverb.end());

    check(distinct(resonator) && distinct(echo) && distinct(reverb) &&
              distinct(clip) && distinct(beside),
          "every machine lights in a colour of its own within its set");
  }

  set(ovt::Character::Pure);
  p.applyFactoryPreset(presetIndex("Init"));
}

/// Folding a section away.
///
/// Everything in the mixer lays itself out from one RowBounds, so this mostly
/// tests layoutRows: a folded section takes no height, its heading stays to be
/// clicked, and the rows below close up.
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

  // Only that section, not a neighbour with it.
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
  // The shortest it will go does not depend on folding: what does not fit
  // scrolls, so the floor is the pinned header and the band's own minimum.
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
  for (auto r : {Row::TuneKnob, Row::MuteSolo, Row::Fader})
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
/// The fetch needs a network and is not exercised here. A comparison that comes
/// out backwards nags every user, and one that is too eager treats a malformed
/// feed as an upgrade.
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

  // Nothing that fails to parse may read as an upgrade.
  for (const char *junk : {"", "  ", "latest", "1.0.0-beta", "v", "1..0",
                           "<!doctype html>", "1.0.0a"})
    check(!isNewerVersion(junk, "1.0.0"),
          std::string("\"") + junk + "\" is not a newer version");

  check(!isNewerVersion("2.0.0", "garbage"),
        "an unreadable current version blocks the comparison too");

  // The feed.
  using ovt::parseReleaseJson;

  const auto good = parseReleaseJson(
      R"({"version":"1.1.0","url":"https://github.com/benjamindehli/overtonium/releases/tag/v1.1.0"})");
  check(good.has_value(), "a well-formed feed parses");
  check(good && good->version == "1.1.0", "and carries the version");
  check(good && good->url == "https://github.com/benjamindehli/overtonium/"
                             "releases/tag/v1.1.0",
        "and the page to send someone to");

  const auto noUrl = parseReleaseJson(R"({"version":"1.1.0"})");
  check(noUrl.has_value() && noUrl->url == ovt::kLatestRelease,
        "a feed with no url still works, falling back to the releases page");

  // The link is opened in a browser, so only this project's pages pass.
  for (const char *elsewhere :
       {"https://example.invalid/v1.1.0",
        "http://github.com/benjamindehli/overtonium/", "file:///etc/passwd",
        "javascript:alert(1)",
        "https://github.com/benjamindehli/overtonium.evil.example/",
        "https://github.com/someone-else/overtonium/releases",
        "https://github.com/benjamindehli/overtonium/../../someone-else"}) {
    const auto moved = parseReleaseJson(
        juce::String(R"({"version":"1.1.0","url":")") + elsewhere + "\"}");
    check(moved.has_value() && moved->url == ovt::kLatestRelease,
          std::string("a link to ") + elsewhere +
              " is replaced by the releases page");
  }

  for (const char *bad : {"", "{}", "[]", "not json", R"({"version":""})",
                          R"({"version":null})", "null"})
    check(!parseReleaseJson(bad).has_value(),
          std::string("a feed of \"") + bad + "\" yields nothing");
}

/// Opening editors must not turn the update check on, or ask anything.
///
/// A host scanning its plugins makes instances and opens editors many times
/// with nobody there to answer. The preference is machine-wide and not asserted
/// to any value, only that none of this changes it.
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

/// The update check asks in the window, once, and says when a release is out.
///
/// Run against the machine's own settings, as the tests above are, so each
/// value is put back as it was found. "Check for updates" is never pressed,
/// since that would reach the network.
void testUpdateDialogs(OvertoniumProcessor &p) {
  section("Update dialogs");

  const bool allowedBefore = ovt::updateCheckAllowed();
  const bool offeredBefore = ovt::updateCheckOffered();
  const auto announcedBefore = ovt::announcedVersion();

  const auto open = [&p] {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    sizeEditor(*ed, 1350);
    return ed;
  };
  const auto noticeOf = [](juce::AudioProcessorEditor &ed) -> auto & {
    return dynamic_cast<OvertoniumEditor &>(ed).updateNotice();
  };

  // ---- the first time ------------------------------------------------------
  ovt::setUpdateCheckAllowed(false);
  ovt::setUpdateCheckOffered(false);
  {
    auto ed = open();
    auto &notice = noticeOf(*ed);
    const bool asked = notice.isShowing() &&
                       notice.getMessage().contains("check for a new version");
    const bool notYet = !ovt::updateCheckOffered();

    check(asked, "the first window opened asks whether to check for updates");
    check(notYet, "and opening it marks nothing, so a host scanning the "
                  "plugin folder does not use the question up");

    notice.secondaryButton().onClick();
    check(!notice.isShowing(), "an answer puts the dialog away");
  }
  check(ovt::updateCheckOffered() && !ovt::updateCheckAllowed(),
        "Not now is an answer: it is remembered, and nothing is checked");
  {
    auto ed = open();
    check(!noticeOf(*ed).isShowing(), "so the next window does not ask again");
  }

  // ---- a release found -----------------------------------------------------
  ovt::markVersionAnnounced({});
  {
    auto ed = open();
    auto &editor = dynamic_cast<OvertoniumEditor &>(*ed);
    editor.announceRelease("99.0.0",
                           "https://example.invalid/overtonium/releases");

    ovt::ui::TopBar *bar = nullptr;
    std::function<void(juce::Component &)> look = [&](juce::Component &c) {
      for (auto *child : c.getChildren()) {
        if (auto *b = dynamic_cast<ovt::ui::TopBar *>(child))
          bar = b;
        look(*child);
      }
    };
    look(editor);

    check(bar != nullptr && bar->isSettingsLit() &&
              bar->availableUpdate() == "99.0.0",
          "a release found lights SETTINGS");

    if (bar != nullptr) {
      auto menu = bar->buildSettingsMenu();
      juce::PopupMenu::MenuItemIterator it(menu);
      const bool first = it.next() && it.getItem().itemID == 703 &&
                         it.getItem().text.contains("99.0.0");
      check(first, "and is the first item in its menu");
    }

    auto &notice = noticeOf(*ed);
    check(notice.isShowing() && notice.getMessage().contains("99.0.0"),
          "and a dialog says which release is out");

    notice.secondaryButton().onClick();
  }
  check(ovt::announcedVersion() == "99.0.0",
        "Later is an answer too, remembered for that release");
  {
    auto ed = open();
    auto &editor = dynamic_cast<OvertoniumEditor &>(*ed);
    editor.announceRelease("99.0.0",
                           "https://example.invalid/overtonium/releases");
    const bool quiet = !noticeOf(*ed).isShowing();
    editor.announceRelease("99.0.1",
                           "https://example.invalid/overtonium/releases");
    const bool next = noticeOf(*ed).isShowing();

    check(quiet, "so the same release is not announced twice");
    check(next, "and the next one is");
  }

  ovt::setUpdateCheckAllowed(allowedBefore);
  ovt::setUpdateCheckOffered(offeredBefore);
  ovt::markVersionAnnounced(announcedBefore);
  check(ovt::updateCheckAllowed() == allowedBefore &&
            ovt::updateCheckOffered() == offeredBefore &&
            ovt::announcedVersion() == announcedBefore,
        "and the machine's own answers are put back");
}

/// The preference survives the object that wrote it.
///
/// The PropertiesFile is built where it is used and destroyed there, so every
/// read is a fresh object reading the disk. This catches the path being
/// computed differently between one call and the next.
///
/// The preference is put back afterwards, since it is machine-wide.
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
/// So a closing window never joins a network thread on the message thread.
/// Nothing here reaches the network, so what is pinned is the ownership: a
/// check that stays put across nine windows cannot be joined by any of them.
void testUpdateCheckOutlivesEditors(OvertoniumProcessor &p) {
  section("Update check outlives its editors");

  const auto *first = &p.updates();

  for (int open = 0; open < 9; ++open) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    sizeEditor(*ed, 1340);
  }

  check(&p.updates() == first,
        "nine windows come and go and the fetch is the same one throughout");

  // Both of the things a closing editor does, on a check that never started,
  // which is the ordinary case since the offer to switch the check on is
  // declined by default.
  p.updates().cancel();
  p.updates().cancel();
  p.updates().setListener(nullptr);

  check(!p.updates().newerRelease().has_value(),
        "cancelling an idle check is safe and finds nothing");
}

/// Every control says what it is.
///
/// JUCE reads a control's accessible name from Component::getTitle, and an
/// element with neither a name nor a description may be skipped by
/// accessibility clients. Buttons are excluded, since JUCE falls back to their
/// text, but the mute and solo buttons, whose text is M and S, are named here
/// anyway.
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
  // A button is named by the word on it, but two of them carry a value instead,
  // which needs a name saying what it is of. So do the converter readouts,
  // whose digits are drawn rather than written.
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
/// Driven through the real MIDI path rather than by poking the voice, since the
/// wiring is most of what could be wrong.
void testMpeSlide() {
  section("MPE slide");

  // Its own processor, since an earlier test leaves notes decaying and a master
  // gain of its own, and this measures loudness.
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

  // Slide is a gesture: the first value a note is given is its rest, and only
  // movement away from it reaches the sound. So the note is sent where the axis
  // sits when it engages, and then where the finger takes it.
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

  // A controller that rests near the centre.
  //
  // 63 rather than 64 on purpose. JUCE hands a new note the centre of the
  // timbre axis and calls back only when a value differs, so a rest of exactly
  // 64 would pass unnoticed.
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
  // Some controllers start sending CC74 from the bottom of the range once a key
  // is pressed. Read as a movement that is the note's rest, and the patch is
  // left alone until the finger moves.
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

/// Clearing every mute or every solo at once, from the menu a right-click on an
/// M or an S opens.
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

/// Muting a partial removes it from the output, and soloing one leaves only
/// that partial.
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

int main(int argc, char **argv) {
  // See the same line in dsp_test: unbuffered, so a crash keeps whatever it
  // printed before it, and not _IOLBF, which the Windows CRT ignores.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  juce::ScopedJuceInitialiser_GUI juceInit;

  OvertoniumProcessor processor;

  // Writes docs/parameters.json from the layout and stops, rather than
  // testing. The check further down then holds the file to it.
  if (argc == 2 && std::strcmp(argv[1], "--write-parameters") == 0) {
    const auto file = parameterReferenceFile();
    const bool written =
        file.replaceWithText(parameterReference(processor), false, false, "\n");
    std::printf("%s %s\n", written ? "wrote" : "could not write",
                file.getFullPathName().toRawUTF8());
    return written ? 0 : 1;
  }

  testParameterWiring(processor);
  testTheParameterListIsCurrent(processor);
  testReadoutsAreWrittenAsAPersonWould(processor);
  testGlideControls(processor);
  testAttackReachesIntoThePhase(processor);
  testLegatoSwitch(processor);
  testGlideCaps();
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
  testMuteSoloGang(processor);
  testSilencedChannel(processor);
  testThePresetDisplaySpells(processor);
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
  testSelectingChannels(processor);
  testToolButtonShowsTheTool(processor);
  testLinkSurvivesAReopen(processor);
  testSegmentCells();
  testOptionBars(processor);
  testSharedSettingsChoices(processor);
  testNoiseString(processor);
  testResonatorParameters(processor);
  testNoiseStringKnobs(processor);
  testPlayingReadouts(processor);
  testPlayingBar(processor);
  testBarRightClicks(processor);
  testTuningBar(processor);
  testAdaptiveReadouts(processor);
  testFoldingAndUnfoldingIsSymmetric(processor);
  testFittingLandsWhereADragCanReturn(processor);
  testFittingShowsEveryRow(processor);
  testTheRulesFoldTheirSections(processor);
  testOneRightClickOpensOneMenu(processor);
  testWheelOverAKnobStaysOnTheKnob(processor);
  testWheelOverABarKnobStaysOnTheKnob(processor);
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
  testTheToolOutlivesTheWindow(processor);
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
  testTheSessionSurvivesAHostSavingUnderIt();
  testPrograms(processor);
  testProgramChangeMidi(processor);
  testCollapsibleSections();
  testUpdateCheck();
  testUpdateCheckIsQuiet(processor);
  testUpdateDialogs(processor);
  testThePreferenceOutlivesItsWriter();
  testUpdateCheckOutlivesEditors(processor);
  testEveryControlIsNamed(processor);
  testSoloAndMute(processor);
  testClearMuteAndSolo(processor);

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
