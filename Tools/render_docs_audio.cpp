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
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"
#include "Presets.h"

namespace {
constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

int indexOf(const juce::String &name) {
  return ovt::presets::names().indexOf(name);
}

/// One note, held, then let go, with the last few milliseconds faded so the
/// file cannot end on a step.
juce::AudioBuffer<float> renderNote(OvertoniumProcessor &p,
                                    const juce::String &preset, int note,
                                    double heldSeconds, double tailSeconds) {
  ovt::presets::apply(p.apvts, indexOf(preset));

  p.setRateAndBufferSizeDetails(kRate, kBlock);
  p.prepareToPlay(kRate, kBlock);
  p.reset();

  const int held = (int)(heldSeconds * kRate);
  const int tail = (int)(tailSeconds * kRate);

  juce::AudioBuffer<float> out(2, held + tail);
  out.clear();

  juce::AudioBuffer<float> chunk(2, kBlock);
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.85f), 0);

  int done = 0;
  bool releasedYet = false;

  while (done < out.getNumSamples()) {
    const int n = juce::jmin(kBlock, out.getNumSamples() - done);

    if (!releasedYet && done >= held) {
      midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
      releasedYet = true;
    }

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

  // These two patches pan nothing, so both channels carry the same samples and
  // a stereo file would be two copies of one signal. Checked rather than
  // assumed: the two channels are identical to the last bit.
  for (int n = 0; n < out.getNumSamples(); ++n)
    jassert(out.getSample(0, n) == out.getSample(1, n));

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

  for (const auto *name : {"Just Saw", "Equal Saw"}) {
    OvertoniumProcessor p;
    const auto buffer = renderNote(p, name, 48, 3.2, 0.6);

    auto slug = juce::String(name).toLowerCase().replace(" ", "-");

    const auto peak = buffer.getMagnitude(0, 0, buffer.getNumSamples());

    std::printf("  %-10s peak %.3f  %d samples\n", name, peak,
                buffer.getNumSamples());

    if (!write(buffer, dir.getChildFile(slug + ".ogg"), ogg, 5))
      std::printf("    ogg FAILED\n");

    if (!write(buffer, dir.getChildFile(slug + ".wav"), wav, 0))
      std::printf("    wav FAILED\n");
  }

  return 0;
}
