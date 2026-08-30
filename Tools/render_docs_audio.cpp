// The two clips on the project page, rendered by the plugin itself.
//
// Just Saw and Equal Saw differ in TUNE and in nothing else, which makes them
// the one pair worth hearing rather than reading about. Both are rendered from
// the shipped presets rather than recorded, so a change to either preset or to
// the tuning maths is one command away from being audible on the page rather
// than quietly out of date.
//
// Mono because neither patch pans anything, so a stereo file would be two
// copies of one signal. Ogg for the browsers that take it and WAV for the rest,
// which is the only pair of formats JUCE can write and every browser can read.
//
//   cmake --build build --target overtonium_render_docs_audio
//   ./build/overtonium_render_docs_audio_artefacts/
//       overtonium_render_docs_audio docs/audio
//
// CONTRIBUTING.md has the rest.

#include <cstdio>
#include <functional>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"
#include "Presets.h"

namespace {
constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

int indexOf(const juce::String &name) {
  return ovt::presets::names().indexOf(name);
}

/// What to do to the controls as a clip runs.
///
/// Called once per block with how far through the held section it is, 0 to 1,
/// which is what lets a clip sweep a knob rather than only sit at one end of
/// it. Empty for a clip that holds still.
using Move = std::function<void(OvertoniumProcessor &, double)>;

void setPlain(OvertoniumProcessor &p, const juce::String &id, float plain) {
  if (auto *param = p.apvts.getParameter(id))
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

/// Notes held together, then let go, with a short fade at each end so a file
/// cannot begin or end on a step.
juce::AudioBuffer<float> render(OvertoniumProcessor &p,
                                const juce::String &preset,
                                const std::vector<int> &notes,
                                double heldSeconds, double tailSeconds,
                                const Move &move = {}) {
  ovt::presets::apply(p.apvts, indexOf(preset));

  p.setRateAndBufferSizeDetails(kRate, kBlock);
  p.prepareToPlay(kRate, kBlock);
  p.reset();

  if (move)
    move(p, 0.0);

  const int held = (int)(heldSeconds * kRate);
  const int tail = (int)(tailSeconds * kRate);

  juce::AudioBuffer<float> out(2, held + tail);
  out.clear();

  juce::AudioBuffer<float> chunk(2, kBlock);
  juce::MidiBuffer midi;

  for (auto note : notes)
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.85f), 0);

  int done = 0;
  bool releasedYet = false;

  while (done < out.getNumSamples()) {
    const int n = juce::jmin(kBlock, out.getNumSamples() - done);

    if (!releasedYet && done >= held) {
      for (auto note : notes)
        midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);

      releasedYet = true;
    }

    if (move && !releasedYet)
      move(p, juce::jlimit(0.0, 1.0, (double)done / (double)held));

    chunk.setSize(2, n, false, false, true);
    chunk.clear();
    p.processBlock(chunk, midi);
    midi.clear();

    for (int c = 0; c < 2; ++c)
      out.copyFrom(c, done, chunk, c, 0, n);

    done += n;
  }

  const int fade = (int)(0.03 * kRate);
  out.applyGainRamp(out.getNumSamples() - fade, fade, 1.0f, 0.0f);
  out.applyGainRamp(0, (int)(0.005 * kRate), 0.0f, 1.0f);

  // None of these patches pans anything, so both channels carry the same
  // samples and a stereo file would be two copies of one signal. Checked
  // rather than assumed, and kept stereo if that ever stops being true.
  for (int n = 0; n < out.getNumSamples(); ++n)
    if (!juce::exactlyEqual(out.getSample(0, n), out.getSample(1, n)))
      return out;

  juce::AudioBuffer<float> mono(1, out.getNumSamples());
  mono.copyFrom(0, 0, out, 0, 0, out.getNumSamples());

  return mono;
}

/// One note in a run: which, when it goes down, and how long it is held.
struct Step {
  int note;
  double at;
  double hold;
};

/// A run of notes rather than a chord, for the things that only show up as you
/// play across the keyboard.
juce::AudioBuffer<float> renderRun(OvertoniumProcessor &p,
                                   const juce::String &preset,
                                   const std::vector<Step> &steps,
                                   double totalSeconds, const Move &move = {}) {
  ovt::presets::apply(p.apvts, indexOf(preset));

  p.setRateAndBufferSizeDetails(kRate, kBlock);
  p.prepareToPlay(kRate, kBlock);
  p.reset();

  if (move)
    move(p, 0.0);

  juce::AudioBuffer<float> out(2, (int)(totalSeconds * kRate));
  out.clear();

  juce::AudioBuffer<float> chunk(2, kBlock);

  int done = 0;

  while (done < out.getNumSamples()) {
    const int n = juce::jmin(kBlock, out.getNumSamples() - done);

    // Whatever falls inside this block, placed at the sample it belongs on
    // rather than at the top of the block.
    juce::MidiBuffer midi;

    for (const auto &step : steps) {
      const auto on = (int)(step.at * kRate);
      const auto off = (int)((step.at + step.hold) * kRate);

      if (on >= done && on < done + n)
        midi.addEvent(juce::MidiMessage::noteOn(1, step.note, 0.85f),
                      on - done);

      if (off >= done && off < done + n)
        midi.addEvent(juce::MidiMessage::noteOff(1, step.note), off - done);
    }

    if (move)
      move(p, juce::jlimit(0.0, 1.0,
                           (double)done / (double)out.getNumSamples()));

    chunk.setSize(2, n, false, false, true);
    chunk.clear();
    p.processBlock(chunk, midi);

    for (int c = 0; c < 2; ++c)
      out.copyFrom(c, done, chunk, c, 0, n);

    done += n;
  }

  const int fade = (int)(0.03 * kRate);
  out.applyGainRamp(out.getNumSamples() - fade, fade, 1.0f, 0.0f);
  out.applyGainRamp(0, (int)(0.005 * kRate), 0.0f, 1.0f);

  for (int n = 0; n < out.getNumSamples(); ++n)
    if (!juce::exactlyEqual(out.getSample(0, n), out.getSample(1, n)))
      return out;

  juce::AudioBuffer<float> mono(1, out.getNumSamples());
  mono.copyFrom(0, 0, out, 0, 0, out.getNumSamples());

  return mono;
}

bool write(const juce::AudioBuffer<float> &buffer, const juce::File &file,
           juce::AudioFormat &format, int quality) {
  file.deleteFile();

  std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
  if (stream == nullptr)
    return false;

  const auto options = juce::AudioFormatWriterOptions()
                           .withSampleRate(kRate)
                           .withNumChannels(buffer.getNumChannels())
                           .withBitsPerSample(16)
                           .withQualityOptionIndex(quality);

  const auto writer = format.createWriterFor(stream, options);

  if (writer == nullptr)
    return false;

  return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
} // namespace

int main(int argc, char **argv) {
  juce::ScopedJuceInitialiser_GUI init;

  const juce::File dir{juce::String(argc > 1 ? argv[1] : ".")};

  juce::WavAudioFormat wav;
  juce::OggVorbisAudioFormat ogg;

  const auto save = [&](const juce::String &slug,
                        const juce::AudioBuffer<float> &buffer) {
    const auto peak = buffer.getMagnitude(0, 0, buffer.getNumSamples());

    double sum = 0.0;
    for (int n = 0; n < buffer.getNumSamples(); ++n)
      sum += (double)buffer.getSample(0, n) * buffer.getSample(0, n);

    const auto rms = std::sqrt(sum / (double)buffer.getNumSamples());

    std::printf("  %-22s %d ch  peak %.3f  rms %6.1f dBFS  %5.1f s\n",
                slug.toRawUTF8(), buffer.getNumChannels(), peak,
                20.0 * std::log10(juce::jmax(1.0e-9, rms)),
                buffer.getNumSamples() / kRate);

    if (!write(buffer, dir.getChildFile(slug + ".ogg"), ogg, 5))
      std::printf("    ogg FAILED\n");

    if (!write(buffer, dir.getChildFile(slug + ".wav"), wav, 0))
      std::printf("    wav FAILED\n");
  };

  // ---- TUNE, at each end and then across the whole of it --------------------
  for (const auto *name : {"Just Saw", "Equal Saw"}) {
    OvertoniumProcessor p;
    save(juce::String(name).toLowerCase().replace(" ", "-"),
         render(p, name, {48}, 3.2, 0.6));
  }

  // The two above give the ends. This gives the middle, which is the part the
  // page actually claims: that it sweeps rather than switches.
  //
  // Equal first and just last, so it starts where every other synthesiser
  // starts and arrives at the thing this one can do, rather than the reverse.
  {
    OvertoniumProcessor p;
    save("tune-sweep",
         render(p, "Just Saw", {48}, 7.0, 0.6, [](auto &proc, double t) {
           for (int i = 0; i < ovt::kNumHarmonics; ++i)
             setPlain(proc, ovt::params::oscParamId(ovt::params::tuneSuffix, i),
                      (float)t);
         }));
  }

  // ---- STRETCH, from a harmonic series out past a piano ---------------------
  //
  // Through +150, which the page calls a real piano, and on to where the
  // partials stop agreeing on a fundamental at all.
  {
    OvertoniumProcessor p;
    save("stretch-sweep",
         render(p, "Just Saw", {48}, 7.0, 0.6, [](auto &proc, double t) {
           setPlain(proc, ovt::params::stretchId, (float)(t * 500.0));
         }));
  }

  // ---- a keyboard temperament, on one chord --------------------------------
  //
  // C major on a temperament built on C, so this is the home key a well
  // temperament is meant to flatter rather than a remote one it roughens.
  for (const auto temperament :
       {ovt::Temperament::Equal, ovt::Temperament::Werckmeister3}) {
    OvertoniumProcessor p;
    const auto slug = temperament == ovt::Temperament::Equal
                          ? "chord-equal"
                          : "chord-werckmeister";

    save(slug, render(p, "Just Saw", {48, 52, 55}, 4.0, 0.6,
                      [temperament](auto &proc, double) {
                        setPlain(proc, ovt::params::temperamentId,
                                 (float)(int)temperament);
                        setPlain(proc, ovt::params::tuningRootId, 0.0f);
                      }));
  }

  // ---- TRACK, heard by playing up the keyboard -----------------------------
  //
  // The rolloff sits at a fixed 1 kHz while the partials climb through it, so
  // it only shows across a run: a bass note keeps nearly all of its series and
  // a treble note has walked most of its into the rolloff. One note would show
  // nothing, since the fundamental holds its level either way.
  {
    const std::vector<Step> run{{36, 0.0, 1.0},  {48, 1.2, 1.0},
                                {60, 2.4, 1.0},  {72, 3.6, 1.0},
                                {84, 4.8, 1.4}};

    for (const auto track : {0.0f, 6.0f}) {
      OvertoniumProcessor p;
      save(track > 0.0f ? "track-on" : "track-off",
           renderRun(p, "Just Saw", run, 6.6, [track](auto &proc, double) {
             setPlain(proc, ovt::params::trackId, track);
           }));
    }
  }

  // ---- DRIFT, which is a texture rather than an event ----------------------
  //
  // A held chord, which is where the page says it shows: every partial walks
  // its own way, so the stack never settles.
  for (const auto drift : {0.0f, 18.0f}) {
    OvertoniumProcessor p;
    save(drift > 0.0f ? "drift-on" : "drift-off",
         render(p, "Just Saw", {48, 55, 60}, 5.0, 0.6,
                [drift](auto &proc, double) {
                  for (int i = 0; i < ovt::kNumHarmonics; ++i)
                    setPlain(
                        proc,
                        ovt::params::oscParamId(ovt::params::driftSuffix, i),
                        drift);
                }));
  }

  return 0;
}
