// Headless integration tests for the plugin layer: parameter wiring, MIDI
// handling, factory presets, undo, bus layouts and state round-tripping, plus
// the parts of the editor that can be measured rather than looked at.
//
// It does build an editor, for the layout checks, but never gives it a window,
// so it still runs on a CI box with no display. Nothing here may open a
// PopupMenu, which does need one.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "PluginEditor.h"
#include "PluginParameters.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "UI/ChannelStrip.h"
#include "UI/NoiseStrip.h"
#include "UI/Theme.h"
#include "UI/TopBar.h"
#include "dsp/Exact.h"
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

} // namespace

// -----------------------------------------------------------------------------

void testParameterWiring(OvertoniumProcessor &p) {
  section("Parameter wiring");

  // 21 per partial, 15 global, 17 for the noise channel, 10 for the two master
  // effects. Start phase is not among the noise channel's, since noise has no
  // phase to start at.
  const int expected = ovt::kNumHarmonics * 21 + 15 + 17 + 10;
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
      ovt::params::driftSuffix,
      ovt::params::delaySuffix,   ovt::params::attackSuffix,
      ovt::params::decaySuffix,   ovt::params::sustainSuffix,
      ovt::params::swellSuffix,   ovt::params::offLevelSuffix,
      ovt::params::releaseSuffix, ovt::params::liftSuffix,
      ovt::params::amRateSuffix,
      ovt::params::amDepthSuffix, ovt::params::velSuffix,
      ovt::params::atSuffix,      ovt::params::muteSuffix,
      ovt::params::soloSuffix,    ovt::params::volumeSuffix,
      ovt::params::panSuffix};

  bool allPresent = true;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    for (auto *s : suffixes)
      allPresent &= p.apvts.getRawParameterValue(
                        ovt::params::oscParamId(s, i)) != nullptr;

  check(allPresent, "all 672 per-partial parameters resolve");

  const char *noiseSuffixes[] = {
      ovt::params::colourSuffix,   ovt::params::delaySuffix,
      ovt::params::attackSuffix,   ovt::params::decaySuffix,
      ovt::params::sustainSuffix,  ovt::params::swellSuffix,
      ovt::params::offLevelSuffix, ovt::params::releaseSuffix,
      ovt::params::liftSuffix,     ovt::params::amRateSuffix,
      ovt::params::amDepthSuffix,
      ovt::params::velSuffix,      ovt::params::atSuffix,
      ovt::params::muteSuffix,     ovt::params::soloSuffix,
      ovt::params::volumeSuffix,   ovt::params::panSuffix};

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

    check(textAt(0.0f) == "0 %", "the bottom of the age knob reads 0 %, not " +
                                     textAt(0.0f));
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

void testPresets(OvertoniumProcessor &p) {
  section("Factory presets");

  const auto names = ovt::presets::names();
  check(names.size() == 16, "sixteen factory presets");

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

  check((int)std::lround(p.apvts.getRawParameterValue(ovt::params::atSourceId)
                             ->load()) ==
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
          "(" + std::to_string(p.getActiveVoiceCount()) + ")");

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
          "range (" + std::to_string(moved) + " Hz from " +
              std::to_string(plain) + ")");

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

    check(peak() < 0.01f,
          "pressure on another channel does not reach it (" +
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
                             "bands (" + std::to_string(bands.size()) + ")");

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

    check(ovt::exactly(rangeEnd(ovt::params::driftSuffix),
                       ovt::params::kMaxDriftCents),
          "and so does the drift maximum (" +
              std::to_string(rangeEnd(ovt::params::driftSuffix)) + ")");

    check(ovt::exactly(ChannelStrip::needlePosition(0.0f), 0.0f),
          "an unmodulated partial sits dead centre");

    check(std::abs(ChannelStrip::needlePosition(
                       ovt::params::kMaxPitchDisplacementCents) -
                   1.0f) < 1.0e-6f,
          "both wanders at once put it exactly at the end");

    check(std::abs(ChannelStrip::needlePosition(
                       -ovt::params::kMaxPitchDisplacementCents) +
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

    check(shallow < 0.25f,
          "a five cent wander stays near the middle (" +
              std::to_string(shallow) + ")");

    check(deep > 0.9f, "and a deep one nearly fills the travel (" +
                           std::to_string(deep) + ")");

    // ...but not so compressed that a shallow setting is invisible. Over
    // fifteen pixels of travel, a tenth is a pixel and a half.
    check(shallow > 0.1f,
          "while still being far enough out to see (" +
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

  editor->setSize(1348, 1000);

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
    check(bands.isEmpty(),
          "and the same values again ask for nothing (" +
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
          "one in (" + std::to_string(bands.size()) + ")");
  }
}

/// The seven-segment readouts under each channel.
///
/// They replaced two plain labels, and the risk in that is a reading the
/// display has no way to draw: a label will render anything, a segment display
/// will silently show an unlit digit. So every reading the two can produce is
/// held against what the display can draw.
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
        readings.add((juce::String)(juce::Decibels::gainToDecibels(v) >= 0.0f
                                        ? ""
                                        : "-") +
                     juce::String(std::abs(juce::Decibels::gainToDecibels(v)),
                                  1));

      // Cents run to about fifty either way, which is as far as any harmonic's
      // just interval sits from equal temperament.
      const auto cents = -50.0f + v * 100.0f;
      readings.add((juce::String)(cents >= 0.0f ? "+" : "-") +
                   juce::String(std::abs(cents), 1));
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

  editor->setSize(1348, 1000);

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

  check(readingFor(2, 1.0f) == "+2.0",
        "and tuned across to just intonation it reads its offset (" +
            readingFor(2, 1.0f).toStdString() + ")");

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

  editor->setSize(1348, 1010);

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
  check(litCount() == 1, "and it is the only one (" +
                             std::to_string(litCount()) + ")");

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
  check(litCount() == 1, "and nothing else is (" + std::to_string(litCount()) +
                             ")");

  // ---- leaving the mixer --------------------------------------------------
  nz.mouseExit(pointAt(nz, {inNoise.x, nz.getHeight() + 40}));

  check(litCount() == 0, "and taking the pointer off the mixer clears it (" +
                             std::to_string(litCount()) + ")");

  // A point inside the strip but on a row that has nothing to point at, such
  // as a section rule, still belongs to that channel: the column says which
  // channel, not which control.
  auto &fourth = *strips[7];
  const auto rules =
      layoutRows(juce::Rectangle<int>(0, 0, kStripWidth, fourth.getHeight())
                     .reduced(2, 4));
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
  const auto parse = [](const juce::String &code) {
    std::map<std::string, float> sets;

    for (const auto &line : juce::StringArray::fromLines(code)) {
      if (!line.contains("ap.set(\""))
        continue;

      const auto id = line.fromFirstOccurrenceOf("ap.set(\"", false, false)
                          .upToFirstOccurrenceOf("\"", false, false);
      const auto value = line.fromFirstOccurrenceOf(", ", false, false)
                             .upToFirstOccurrenceOf("f)", false, false);

      sets[id.toStdString()] = value.getFloatValue();
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

  std::printf("  %d presets round-tripped, worst error %.6f%s\n", count,
              worstError,
              worstParam.empty()
                  ? ""
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

  check(loudest > 0.01, "the passage is audible (" + std::to_string(loudest) +
                            ")");

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

  // Tilts weight by distance from the grabbed strip, in opposite directions.
  const int src = 15;
  const auto upAbove = linkCurveWeight(LinkCurve::TiltUp, 31, src);
  const auto upBelow = linkCurveWeight(LinkCurve::TiltUp, 0, src);
  const auto downAbove = linkCurveWeight(LinkCurve::TiltDown, 31, src);
  const auto downBelow = linkCurveWeight(LinkCurve::TiltDown, 0, src);

  std::printf("  grabbing h16, tilt up: h1 %.2f h32 %.2f, tilt down: h1 %.2f "
              "h32 %.2f\n",
              upBelow, upAbove, downBelow, downAbove);

  check(upAbove > 1.0f && upBelow < 1.0f,
        "tilt up moves partials above the grab more and below it less");
  check(downAbove < 1.0f && downBelow > 1.0f, "tilt down does the opposite");
  check(upBelow > 0.0f && downAbove > 0.0f,
        "neither tilt freezes its quiet end completely");

  // Geometric, so equal distances either side are reciprocal.
  check(std::abs(upAbove * linkCurveWeight(LinkCurve::TiltUp, 0, 16) - 1.0f) <
            0.2f,
        "the tilt is symmetric in proportion either side of the grab");

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
  const auto rows = layoutRows(
      juce::Rectangle<int>(0, 0, kStripWidth, preferredStripHeight() + 8)
          .reduced(2, 4));

  const auto rowAtCentre = [&rows](Row r) {
    const auto band = rows[(size_t)r];
    return controlRowAt(rows, {kStripWidth / 2, band.getCentreY()});
  };

  bool identity = true;
  for (Row r :
       {Row::TuneKnob, Row::Phase, Row::PmRate, Row::PmDepth, Row::Drift,
        Row::Delay,
        Row::Attack, Row::Decay, Row::Sustain, Row::Swell, Row::OffLevel,
        Row::Release, Row::Lift, Row::AmRate, Row::AmDepth,
        Row::Velocity, Row::Aftertouch,
        Row::Pan, Row::MuteSolo, Row::Fader})
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

  check(oneEach, "each of the 17 linkable roles sits on exactly one row");

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
  settings.curve = LinkCurve::TiltDown;

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
        "the switch, four scopes and four curves are all there (" +
            std::to_string(items) + ")");

  // Exactly the two current settings are ticked, and nothing else.
  check(ticked == 2, "two items are ticked");
  check(tickedNames == "Odd harmonics Tilt down ",
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
  applyLinkMenuChoice(203, state);
  check(state.scope == LinkScope::SameInterval,
        "picking a curve leaves the scope alone");
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

      // A knob's line is its dial, not the control, which reaches further down
      // to hold the caption.
      if (auto *knob = dynamic_cast<LabelledKnob *>(child))
        centres.push_back(knob->getY() + knob->slider.getBounds().getCentreY());
      else
        centres.push_back(child->getBounds().getCentreY());
    }

    return centres;
  };

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

  // And the readouts sit under the meter, inside the row, at every width the
  // bar can be given. Excluding them from the rule above would otherwise be a
  // hole rather than a decision.
  for (int width : {1412, 1100, 900, TopBar::minimumWidth()}) {
    bar.setSize(width, TopBar::heightForWidth(width));

    juce::Rectangle<int> meterBounds;
    juce::Array<juce::Rectangle<int>> readouts;

    for (auto *child : bar.getChildren()) {
      if (dynamic_cast<StereoOutputMeter *>(child) != nullptr)
        meterBounds = child->getBounds();

      if (dynamic_cast<SegmentDisplay *>(child) != nullptr)
        readouts.add(child->getBounds());
    }

    const auto at = " (" + std::to_string(width) + " px)";

    check(readouts.size() == 2, "both converter readouts are placed" + at);

    bool below = !meterBounds.isEmpty();
    for (const auto &r : readouts)
      below &= !r.isEmpty() && r.getY() >= meterBounds.getBottom() &&
               r.getBottom() <= bar.getHeight();

    check(below, "and both sit under the meter without leaving the bar" + at);

    // Side by side rather than one on top of the other or overlapping.
    if (readouts.size() == 2)
      check(!readouts[0].intersects(readouts[1]),
            "and do not overlap each other" + at);
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

  // What a preset deliberately leaves alone: how you play it and how loud, as
  // opposed to what it sounds like.
  const juce::StringArray preserved{
      ovt::params::masterGainId, ovt::params::polyphonyId,
      ovt::params::bendRangeId,   ovt::params::atSourceId,
      ovt::params::safetyClipId,  ovt::params::referenceHzId,
      ovt::params::temperamentId, ovt::params::tuningRootId};

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
      if (preserved.contains(clean[k].first))
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

  for (int i = 0; i < names.size(); ++i)
    ovt::presets::apply(p.apvts, i);

  check(read(ovt::params::temperamentId) ==
                (int)ovt::Temperament::Werckmeister3 &&
            read(ovt::params::tuningRootId) == 5,
        "loading every preset in turn leaves the temperament alone");

  check(read(ovt::params::referenceHzId) == 0 &&
            read(ovt::params::polyphonyId) == 6,
        "and the reference pitch and polyphony with it");

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

  editor->setSize(1348, 1000);

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

  // And the shortest attack really is 0.2 ms, not a number that rounds to it.
  const auto attackId = ovt::params::oscParamId(ovt::params::attackSuffix, 0);

  if (auto *param = p.apvts.getParameter(attackId))
    check(std::abs(param->getNormalisableRange().start - 0.0002f) < 1.0e-7f,
          "the shortest attack is 0.2 ms");
}

void testUndo(OvertoniumProcessor &p) {
  section("Undo");

  auto &undo = p.undo();

  // Parameter moves reach the value tree, and so the undo manager, on a timer.
  // copyState flushes them synchronously, which makes this a test of the round
  // trip rather than of how long to wait.
  const auto settle = [&p] { p.apvts.copyState(); };

  const auto tuneOf = [&p](int i) {
    return p.apvts
        .getRawParameterValue(
            ovt::params::oscParamId(ovt::params::tuneSuffix, i))
        ->load();
  };

  const auto setTune = [&p](int i, float v) {
    p.apvts.getParameter(ovt::params::oscParamId(ovt::params::tuneSuffix, i))
        ->setValueNotifyingHost(v);
  };

  ovt::presets::apply(p.apvts, presetIndex("Init"));
  settle();
  undo.clearUndoHistory();
  undo.beginNewTransaction();

  const auto before = tuneOf(0);
  setTune(0, before > 0.5f ? 0.1f : 0.9f);
  settle();

  const auto after = tuneOf(0);
  check(std::abs(after - before) > 0.1f, "the parameter moved to begin with");
  check(undo.canUndo(), "and the move is on the undo stack");

  undo.beginNewTransaction();
  check(undo.undo(), "undo reports that it did something");
  settle();

  check(std::abs(tuneOf(0) - before) < 1.0e-4f,
        "and puts the parameter back (" + std::to_string(tuneOf(0)) +
            " against " + std::to_string(before) + ")");

  check(undo.redo(), "redo reports that it did something");
  settle();

  check(std::abs(tuneOf(0) - after) < 1.0e-4f, "and moves it forward again");

  // The one that matters: a gesture that moves every channel has to come back
  // as a single step, not as 32.
  undo.beginNewTransaction();

  std::array<float, ovt::kNumHarmonics> baseline{};
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    baseline[(size_t)i] = tuneOf(i);

  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    setTune(i, baseline[(size_t)i] > 0.5f ? 0.2f : 0.8f);

  settle();

  int moved = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(tuneOf(i) - baseline[(size_t)i]) > 0.1f)
      ++moved;

  check(moved == ovt::kNumHarmonics, "a ganged move reaches all 32 channels");

  undo.beginNewTransaction();
  undo.undo();
  settle();

  int restored = 0;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    if (std::abs(tuneOf(i) - baseline[(size_t)i]) < 1.0e-4f)
      ++restored;

  check(restored == ovt::kNumHarmonics,
        "and one undo brings all 32 back (" + std::to_string(restored) + ")");

  undo.clearUndoHistory();
}

void testBusLayouts(OvertoniumProcessor &p) {
  section("Bus layouts");

  juce::AudioProcessor::BusesLayout stereo;
  stereo.outputBuses.add(juce::AudioChannelSet::stereo());
  check(p.checkBusesLayoutSupported(stereo), "stereo output supported");

  juce::AudioProcessor::BusesLayout mono;
  mono.outputBuses.add(juce::AudioChannelSet::mono());
  check(p.checkBusesLayoutSupported(mono), "mono output supported");

  juce::AudioProcessor::BusesLayout surround;
  surround.outputBuses.add(juce::AudioChannelSet::create5point1());
  check(!p.checkBusesLayoutSupported(surround), "5.1 output rejected");

  check(p.acceptsMidi(), "accepts MIDI");
  check(!p.producesMidi(), "produces no MIDI");
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

  check(p.getNumPrograms() == names.size(),
        "every factory preset is offered as a program (" +
            std::to_string(p.getNumPrograms()) + ")");
  check(p.getProgramName(0) == names[0],
        "program 0 is named " + names[0].toStdString());

  // Out of range on both sides, since hosts ask about cached indices.
  check(p.getProgramName(-1).isEmpty() &&
            p.getProgramName(names.size()).isEmpty(),
        "an index that does not exist has no name rather than a crash");

  const int chosen = names.indexOf("Wurli");
  check(chosen > 0, "the preset the rest of this test uses exists");

  p.applyFactoryPreset(chosen);
  check(p.getCurrentProgram() == chosen,
        "choosing a preset reports it as the current program");

  // A parameter the presets actually set. Master gain is a session parameter
  // and presets leave it alone, so an edit to it survives everything here and
  // would make every check below pass without proving anything.
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

  // A state from before programs existed has no stored index.
  const int before = p.getCurrentProgram();
  p.applyFactoryPreset(names.indexOf("Big Saw"));
  juce::MemoryBlock legacy;
  p.getStateInformation(legacy);

  const auto size = (int)legacy.getSize();

  auto xml = juce::AudioProcessor::getXmlFromBinary(legacy.getData(), size);

  if (xml != nullptr) {
    xml->removeAttribute("overtoniumProgram");
    juce::MemoryBlock stripped;
    juce::AudioProcessor::copyXmlToBinary(*xml, stripped);

    p.setCurrentProgram(before == 0 ? 1 : 0);
    p.setStateInformation(stripped.getData(), (int)stripped.getSize());

    check(p.getCurrentProgram() == 0,
          "a state saved without a program lands on the first one");
  } else {
    check(false, "the legacy state could be reread");
  }
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

  for (auto r : {Row::Delay, Row::Attack, Row::Decay, Row::Sustain})
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
            open[(size_t)Row::Delay].getHeight() +
                open[(size_t)Row::Attack].getHeight() +
                open[(size_t)Row::Decay].getHeight() +
                open[(size_t)Row::Sustain].getHeight(),
        "the height given up is the sum of the rows that went");

  check(preferredStripHeight(envMask) ==
            preferredStripHeight() - collapsedRowsHeight(envMask),
        "the strip wants exactly that much less room");
  check(minimumStripHeight(envMask) ==
            minimumStripHeight() - collapsedRowsHeight(envMask),
        "and will go exactly that much shorter");

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
  check(headingSectionAt(folded,
                         folded[(size_t)Row::EnvHeading].getCentre()) ==
            Section::Envelope,
        "the heading of a folded section is still the way back");
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

int main() {
  juce::ScopedJuceInitialiser_GUI juceInit;

  OvertoniumProcessor processor;

  testParameterWiring(processor);
  testRendering(processor);
  testPresets(processor);
  testAftertouchMidi(processor);
  testMpe(processor);
  testActivityLamps(processor);
  testSegmentReadouts(processor);
  testChannelHover(processor);
  testFactoryCodeGenerator(processor);
  testOversizedBlocks(processor);
  testUserPresets(processor);
  testMasterEffects(processor);
  testLinkCurves();
  testRowHover();
  testMeterRepaint();
  testLinkMenu();
  testTopBarLayout();
  testTopBarAlignment(processor);
  testKnobSizes(processor);
  testNoDeadTravel(processor);
  testPresetsAreReproducible(processor);
  testUndo(processor);
  testBusLayouts(processor);
  testStateRoundTrip(processor);
  testPrograms(processor);
  testCollapsibleSections();
  testSoloAndMute(processor);

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
