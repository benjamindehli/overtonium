// Renders the pictures the project page uses.
//
// The plugin has no dependency on a display, so this builds the editor, plays
// a chord into it, hands the strips the levels the engine measured and writes
// the window out with createComponentSnapshot. That means the pictures can be
// regenerated after a layout change instead of going stale, and it works on a
// machine with no window server.
//
//   render_docs_images <output-directory> [scale]
//
// The scale is a multiplier on the window, and it is a genuine redraw rather
// than an upscale: at 4 every knob tick and every segment of a readout is
// drawn at four times the size. Use it for artwork that needs the window
// larger than the page shows it.
//
// Two things it cannot do. It writes PNG, because JUCE has no WebP encoder,
// so turning the results into what the pages actually load is a separate step
// that CONTRIBUTING.md spells out. And DRIFT is random per voice, so the
// meters and the lamps land somewhere slightly different every run. Everything
// that is a setting rather than a measurement comes out identical.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "UI/ChannelStrip.h"
#include "UI/NoiseStrip.h"
#include "UI/Theme.h"
#include "UI/TopBar.h"

namespace {

/// The editor takes its size from the saved state and the zoom, so a picture
/// has to ask for a size rather than accept whatever a session left behind.
constexpr int kWindowWidth = 1348;
constexpr int kWindowHeight = 1010;

/// What the pages show. Big Saw puts a 1/n spectrum on the faders and its own
/// table on the tuning knobs, so the mixer shows a shape rather than a row of
/// identical channels.
constexpr const char *kPreset = "Big Saw";

/// Held while the picture is taken, so the meters have something to show.
constexpr int kChord[] = {48, 55, 64};

/// Long enough at 48 kHz for the envelopes to reach sustain.
constexpr int kBlocks = 220;

/// How many channels the strip detail shows beside the gutter. Six is as many
/// as stay readable at the width the page gives it.
constexpr int kDetailChannels = 6;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

/// Every component of one kind, in the order the editor laid them out.
template <typename T> std::vector<T *> descendantsOf(juce::Component &root) {
  std::vector<T *> found;

  std::function<void(juce::Component &)> walk = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      if (auto *match = dynamic_cast<T *>(child))
        found.push_back(match);

      walk(*child);
    }
  };

  walk(root);
  return found;
}

bool writePng(juce::Component &editor, juce::Rectangle<int> area, float scale,
              const juce::File &file) {
  const auto image = editor.createComponentSnapshot(area, true, scale);

  file.deleteFile();
  juce::FileOutputStream stream(file);

  if (stream.failedToOpen()) {
    std::printf("cannot write %s\n", file.getFullPathName().toRawUTF8());
    return false;
  }

  if (!juce::PNGImageFormat().writeImageToStream(image, stream)) {
    std::printf("encoding failed for %s\n",
                file.getFullPathName().toRawUTF8());
    return false;
  }

  stream.flush();
  std::printf("  %-28s %d x %d\n", file.getFileName().toRawUTF8(),
              image.getWidth(), image.getHeight());
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: render_docs_images <output-directory> [scale]\n");
    return 2;
  }

  const juce::File outputDirectory =
      juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);

  if (!outputDirectory.createDirectory()) {
    std::printf("cannot use %s\n",
                outputDirectory.getFullPathName().toRawUTF8());
    return 1;
  }

  const float scale = argc > 2 ? (float)std::atof(argv[2]) : 1.0f;

  if (scale <= 0.0f) {
    std::printf("scale must be greater than zero\n");
    return 2;
  }

  juce::ScopedJuceInitialiser_GUI juceInit;

  OvertoniumProcessor plugin;

  const int preset = ovt::presets::names().indexOf(kPreset);

  if (preset < 0) {
    std::printf("no preset called %s\n", kPreset);
    return 1;
  }

  // Before prepareToPlay, or the bar has no sample rate to report.
  plugin.setRateAndBufferSizeDetails(kSampleRate, kBlockSize);
  plugin.prepareToPlay(kSampleRate, kBlockSize);
  plugin.applyFactoryPreset(preset);

  std::unique_ptr<juce::AudioProcessorEditor> base(plugin.createEditor());
  auto *editor = dynamic_cast<OvertoniumEditor *>(base.get());

  if (editor == nullptr) {
    std::printf("the editor did not open\n");
    return 1;
  }

  editor->setSize(kWindowWidth, kWindowHeight);

  auto strips = descendantsOf<ovt::ui::ChannelStrip>(*editor);
  auto noise = descendantsOf<ovt::ui::NoiseStrip>(*editor);
  auto bars = descendantsOf<ovt::ui::TopBar>(*editor);

  if ((int)strips.size() != ovt::kNumHarmonics || noise.empty() ||
      bars.empty()) {
    std::printf("the mixer is not all there: %d strips, %d noise, %d bars\n",
                (int)strips.size(), (int)noise.size(), (int)bars.size());
    return 1;
  }

  juce::AudioBuffer<float> buffer(2, kBlockSize);
  juce::MidiBuffer midi;

  for (int note : kChord)
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);

  for (int block = 0; block < kBlocks; ++block) {
    buffer.clear();
    plugin.processBlock(buffer, midi);
    midi.clear();
  }

  // What the editor's timer would do. Driving it instead of waiting for it is
  // not a shortcut: a timer needs a message loop, and running one needs
  // JUCE_MODAL_LOOPS_PERMITTED, which this build does not set.
  juce::Array<juce::Rectangle<int>> lamps;

  for (int i = 0; i < ovt::kNumHarmonics; ++i) {
    auto &strip = *strips[(size_t)i];

    strip.setMeterLevel(plugin.getPartialLevel(i));
    strip.setActivity(plugin.getPartialEnvelope(i),
                      plugin.getPartialTremolo(i), plugin.getPartialPitch(i),
                      lamps);
    lamps.clearQuick();
  }

  noise.front()->setMeterLevel(plugin.getNoiseLevel());
  noise.front()->setActivity(plugin.getNoiseEnvelope(),
                             plugin.getNoiseTremolo(), lamps);

  bars.front()->setOutputLevels(plugin.getOutputLevelLeft(),
                                plugin.getOutputLevelRight());
  bars.front()->updateConverterReadouts(plugin.getSampleRate());

  // Measured rather than assumed, so a taller bar or a wider strip moves these
  // with it instead of slicing the next picture through the middle of a row.
  const int barHeight = ovt::ui::TopBar::heightForWidth(kWindowWidth);
  const int detailWidth =
      ovt::ui::kGutterWidth + kDetailChannels * ovt::ui::kStripWidth;

  // Not called "near": the Windows SDK has carried a macro of that name since
  // the segmented memory models, and this builds there too.
  const auto closeTo = [](float a, float b) {
    return std::abs(a - b) < 1.0e-6f;
  };

  const juce::String suffix =
      closeTo(scale, 1.0f)
          ? juce::String()
          : "@" +
                juce::String(scale, closeTo(scale, std::round(scale)) ? 0 : 1) +
                "x";

  std::printf("preset %s, %d voices, %.0f Hz, scale %g\n", kPreset,
              plugin.getActiveVoiceCount(), plugin.getSampleRate(), scale);

  const auto png = [&](const char *stem) {
    return outputDirectory.getChildFile(stem + suffix + ".png");
  };

  const bool ok =
      writePng(*editor, {0, 0, kWindowWidth, kWindowHeight}, scale,
               png("overtonium")) &&
      writePng(*editor, {0, barHeight, detailWidth, kWindowHeight - barHeight},
               scale, png("overtonium-strips")) &&
      writePng(*editor, {0, 0, kWindowWidth, barHeight}, scale,
               png("overtonium-bar"));

  return ok ? 0 : 1;
}
