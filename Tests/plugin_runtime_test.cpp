// Headless integration tests for the plugin layer: parameter wiring, MIDI
// handling, factory presets, bus layouts and state round-tripping. No editor is
// created, so this runs on a CI box with no display.

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "PluginParameters.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "UI/Theme.h"

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

juce::MidiBuffer noteOnAt(int note, float velocity, int sample) {
  juce::MidiBuffer m;
  m.addEvent(juce::MidiMessage::noteOn(1, note, velocity), sample);
  return m;
}

} // namespace

// -----------------------------------------------------------------------------

void testParameterWiring(OvertoniumProcessor &p) {
  section("Parameter wiring");

  // 16 per partial, 6 global, 13 for the noise channel, 15 for the two
  // master effects.
  const int expected = ovt::kNumHarmonics * 16 + 6 + 13 + 15;
  check(p.getParameters().size() == expected,
        "parameter count is " + std::to_string(p.getParameters().size()) +
            ", expected " + std::to_string(expected));

  // Every ID the audio thread caches must actually exist in the layout. A typo
  // here is a null atomic pointer and a crash on the first block.
  const char *globals[] = {
      ovt::params::masterGainId, ovt::params::polyphonyId,
      ovt::params::spreadId,     ovt::params::bendRangeId,
      ovt::params::phaseResetId, ovt::params::safetyClipId};

  const char *effects[] = {
      ovt::params::echoOnId,       ovt::params::echoMixId,
      ovt::params::echoTimeId,     ovt::params::echoFeedbackId,
      ovt::params::echoToneId,     ovt::params::echoWobbleId,
      ovt::params::echoSpreadId,   ovt::params::reverbOnId,
      ovt::params::reverbMixId,    ovt::params::reverbSizeId,
      ovt::params::reverbDecayId,  ovt::params::reverbDampId,
      ovt::params::reverbLowCutId, ovt::params::reverbPreDelayId,
      ovt::params::reverbWidthId};

  for (auto *id : effects)
    check(p.apvts.getRawParameterValue(id) != nullptr,
          std::string("effect param ") + id);

  for (auto *id : globals)
    check(p.apvts.getRawParameterValue(id) != nullptr,
          std::string("global param ") + id);

  const char *suffixes[] = {
      ovt::params::tuneSuffix,    ovt::params::pmRateSuffix,
      ovt::params::pmDepthSuffix, ovt::params::driftSuffix,
      ovt::params::delaySuffix,   ovt::params::attackSuffix,
      ovt::params::decaySuffix,   ovt::params::sustainSuffix,
      ovt::params::releaseSuffix, ovt::params::amRateSuffix,
      ovt::params::amDepthSuffix, ovt::params::velSuffix,
      ovt::params::atSuffix,      ovt::params::muteSuffix,
      ovt::params::soloSuffix,    ovt::params::volumeSuffix};

  bool allPresent = true;
  for (int i = 0; i < ovt::kNumHarmonics; ++i)
    for (auto *s : suffixes)
      allPresent &= p.apvts.getRawParameterValue(
                        ovt::params::oscParamId(s, i)) != nullptr;

  check(allPresent, "all 512 per-partial parameters resolve");

  const char *noiseSuffixes[] = {
      ovt::params::colourSuffix,  ovt::params::delaySuffix,
      ovt::params::attackSuffix,  ovt::params::decaySuffix,
      ovt::params::sustainSuffix, ovt::params::releaseSuffix,
      ovt::params::amRateSuffix,  ovt::params::amDepthSuffix,
      ovt::params::velSuffix,     ovt::params::atSuffix,
      ovt::params::muteSuffix,    ovt::params::soloSuffix,
      ovt::params::volumeSuffix};

  bool noisePresent = true;
  for (auto *n : noiseSuffixes)
    noisePresent &=
        p.apvts.getRawParameterValue(ovt::params::noiseParamId(n)) != nullptr;

  check(noisePresent, "the noise channel's parameters resolve");

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
  check(names.size() == 9, "nine factory presets");

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

  ovt::presets::apply(p.apvts, 0); // back to Init
}

void testAftertouchMidi(OvertoniumProcessor &p) {
  section("Aftertouch over MIDI");

  ovt::presets::apply(p.apvts, 0);

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

  p.reset();
  renderBlocks(p, 4, 512);
  ovt::presets::apply(p.apvts, 0);
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

  // The same rectangle a strip lays its rows out in.
  const auto rows =
      layoutRows(juce::Rectangle<int>(0, 0, kStripWidth, 620).reduced(2, 4));

  const auto rowAtCentre = [&rows](Row r) {
    const auto band = rows[(size_t)r];
    return controlRowAt(rows, {kStripWidth / 2, band.getCentreY()});
  };

  bool identity = true;
  for (Row r : {Row::TuneKnob, Row::PmRate, Row::PmDepth, Row::Drift,
                Row::Delay, Row::Attack, Row::Decay, Row::Sustain, Row::Release,
                Row::AmRate, Row::AmDepth, Row::Velocity, Row::Aftertouch,
                Row::MuteSolo, Row::Fader})
    identity &= rowAtCentre(r) == r;

  check(identity, "every control row reports itself");

  bool quiet = true;
  for (Row r : {Row::Header, Row::PitchModHeading, Row::EnvHeading,
                Row::AmpModHeading, Row::OutputHeading})
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

  check(oneEach, "each of the 14 linkable roles sits on exactly one row");

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

  ovt::presets::apply(p.apvts, 0);

  const auto dry = tailAfterNote();
  check(dry < 1.0e-5f, "with the effects off the note stops when it stops");

  setParam(ovt::params::reverbOnId, 1.0f);
  setParam(ovt::params::reverbMixId, 0.5f);
  setParam(ovt::params::reverbDecayId, 6.0f);

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

  ovt::presets::apply(p.apvts, 0);
  p.reset();

  check(tailAfterNote() < 1.0e-5f, "Init puts both of them away again");
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

void testSoloAndMute(OvertoniumProcessor &p) {
  section("Solo and mute");

  ovt::presets::apply(p.apvts, 5); // Just Saw: every partial up

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
  testMasterEffects(processor);
  testLinkCurves();
  testRowHover();
  testBusLayouts(processor);
  testStateRoundTrip(processor);
  testSoloAndMute(processor);

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
