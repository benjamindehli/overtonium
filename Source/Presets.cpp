#include "Presets.h"

#include <algorithm>
#include <cmath>

#include "PluginParameters.h"
#include "dsp/Harmonics.h"

namespace ovt::presets {

namespace {

using APVTS = juce::AudioProcessorValueTreeState;

struct Applier {
  APVTS &apvts;

  void set(const juce::String &id, float plainValue) const {
    if (auto *p = apvts.getParameter(id))
      p->setValueNotifyingHost(p->convertTo0to1(plainValue));
  }

  void osc(const char *suffix, int index0, float plainValue) const {
    set(params::oscParamId(suffix, index0), plainValue);
  }

  /// Applies fn(harmonicNumber) to every strip.
  template <typename Fn> void allOsc(const char *suffix, Fn &&fn) const {
    for (int i = 0; i < kNumHarmonics; ++i)
      osc(suffix, i, (float)fn(i + 1));
  }

  void resetAllToDefault() const {
    for (auto *p : apvts.processor.getParameters())
      if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p))
        ranged->setValueNotifyingHost(ranged->getDefaultValue());
  }

  /// Everything a preset does not explicitly set should start from a known
  /// state.
  void neutralBase() const {
    allOsc(params::tuneSuffix, [](int) { return 1.0; });
    allOsc(params::phaseSuffix, [](int) { return 0.0; });
    allOsc(params::pmRateSuffix, [](int) { return 4.0; });
    allOsc(params::pmDepthSuffix, [](int) { return 0.0; });
    allOsc(params::driftSuffix, [](int) { return 0.0; });
    allOsc(params::delaySuffix, [](int) { return 0.0; });
    allOsc(params::attackSuffix, [](int) { return 0.005; });
    allOsc(params::decaySuffix, [](int) { return 0.6; });
    allOsc(params::sustainSuffix, [](int) { return 1.0; });
    allOsc(params::swellSuffix, [](int) { return 0.005; });
    allOsc(params::offLevelSuffix, [](int) { return 0.0; });
    allOsc(params::releaseSuffix, [](int) { return 0.4; });
    allOsc(params::liftSuffix, [](int) { return 0.0; });
    allOsc(params::amRateSuffix, [](int) { return 4.0; });
    allOsc(params::amDepthSuffix, [](int) { return 0.0; });
    allOsc(params::velSuffix, [](int) { return 0.7; });
    allOsc(params::atSuffix, [](int) { return 0.0; });
    allOsc(params::muteSuffix, [](int) { return 0.0; });
    allOsc(params::soloSuffix, [](int) { return 0.0; });
    allOsc(params::volumeSuffix, [](int) { return 0.0; });

    allOsc(params::panSuffix, [](int) { return 0.0; });

    // Noise is off unless a preset asks for it.
    set(params::noiseParamId(params::volumeSuffix), 0.0f);
    set(params::noiseParamId(params::colourSuffix), 0.5f);
    set(params::noiseParamId(params::delaySuffix), 0.0f);
    set(params::noiseParamId(params::attackSuffix), 0.005f);
    set(params::noiseParamId(params::decaySuffix), 0.6f);
    set(params::noiseParamId(params::sustainSuffix), 1.0f);
    set(params::noiseParamId(params::swellSuffix), 0.005f);
    set(params::noiseParamId(params::offLevelSuffix), 0.0f);
    set(params::noiseParamId(params::releaseSuffix), 0.4f);
    set(params::noiseParamId(params::liftSuffix), 0.0f);
    set(params::noiseParamId(params::amDepthSuffix), 0.0f);
    set(params::noiseParamId(params::muteSuffix), 0.0f);
    set(params::noiseParamId(params::soloSuffix), 0.0f);
    set(params::noiseParamId(params::panSuffix), 0.0f);

    // Everything global that is part of the sound rather than part of the
    // setup. A preset decides what the instrument is, so it has to decide
    // these too: loading one with the series stretched into a bell, or the
    // converter down at eight bits, has to give the patch that was designed
    // rather than that patch through whatever was left over.
    //
    // Deliberately not here: master gain, polyphony, bend range and what feeds
    // aftertouch. Those are how you play it and how loud, not what it sounds
    // like, and a preset that moved them would be overstepping.
    set(params::stretchId, 0.0f);
    set(params::trackId, 0.0f);
    set(params::lofiRateId, 0.0f);
    set(params::lofiBitsId, 0.0f);
    set(params::phaseResetId, 1.0f);

    // The master effects are off unless a preset switches them on, and their
    // settings go back to the panel defaults either way, so loading a preset
    // never leaves the last one's tail behind.
    set(params::echoOnId, 0.0f);
    set(params::echoMixId, 0.25f);
    set(params::echoTimeId, 0.35f);
    set(params::echoFeedbackId, 0.35f);
    set(params::echoAgeId, 0.35f);

    set(params::reverbOnId, 0.0f);
    set(params::reverbMixId, 0.25f);
    set(params::reverbDecayId, 2.0f);
    set(params::reverbDampId, 0.5f);
    set(params::reverbPreDelayId, 0.0f);
    set(params::reverbWidthId, 1.0f);
  }

  /// The old stereo spread control, written out as pan positions.
  ///
  /// Partials go in mirrored pairs, 1 and 2 in the centre out to 31 and 32 at
  /// the edges, with the sides alternating so the louder of each pair does not
  /// always land on the same one. It was a good shape to start from, so the
  /// presets that used to dial in spread now write it into the pans, where it
  /// can be taken apart by hand.
  void fanOut(double width) const {
    constexpr int lastPair = kNumHarmonics / 2 - 1;

    for (int i = 0; i < kNumHarmonics; ++i) {
      const int pairIndex = i / 2;
      const bool second = (i % 2) != 0;
      const bool flip = (pairIndex % 2) != 0;

      const auto magnitude =
          std::sqrt((double)pairIndex / (double)lastPair) * width;

      osc(params::panSuffix, i,
          (float)((second != flip ? 1.0 : -1.0) * magnitude));
    }
  }

  /// Switches the reverb on. The room follows the decay, so there is nothing
  /// else to say about its size.
  void reverb(float mix, float decay, float damping) const {
    set(params::reverbOnId, 1.0f);
    set(params::reverbMixId, mix);
    set(params::reverbDecayId, decay);
    set(params::reverbDampId, damping);
  }

  /// What the series is made of, before anything is done to it.
  ///
  /// @param stretchCents  how far off harmonic the top partial sits.
  /// @param trackDbPerOctave  how fast the top of it goes as you play up.
  void series(float stretchCents, float trackDbPerOctave) const {
    set(params::stretchId, stretchCents);
    set(params::trackId, trackDbPerOctave);
  }

  void echo(float mix, float time, float feedback, float age) const {
    set(params::echoOnId, 1.0f);
    set(params::echoMixId, mix);
    set(params::echoTimeId, time);
    set(params::echoFeedbackId, feedback);
    set(params::echoAgeId, age);
  }
};

const char *const kNames[] = {
    "Init",      "Drawbar Organ", "Struck Bell", "Slow Pad",   "Odd Harmonics",
    "Just Saw",  "Equal Saw",     "Shimmer",     "Vibraphone", "Harpsichord",
    "Music Box", "Kalimba",       "Cathedral",   "Tape Choir", "Glass Armonica",
};

} // namespace

juce::StringArray names() {
  juce::StringArray a;
  for (auto *n : kNames)
    a.add(n);

  return a;
}

// -----------------------------------------------------------------------------
// User presets
// -----------------------------------------------------------------------------

namespace {
const juce::Identifier kPresetTag{"OVERTONIUM_PRESET"};
const juce::Identifier kParamTag{"PARAM"};
constexpr const char *kExtension = ".ovtpreset";
} // namespace

juce::File userDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("Dehli Musikk")
          .getChildFile("Overtonium")
          .getChildFile("Presets");

  dir.createDirectory();
  return dir;
}

juce::Array<juce::File> userPresets() {
  auto found = userDirectory().findChildFiles(juce::File::findFiles, false,
                                              juce::String("*") + kExtension);

  // Sorted, so the menu does not reorder itself when the filesystem feels like
  // handing them back in another order.
  found.sort();
  return found;
}

juce::String sanitiseName(const juce::String &raw) {
  // Everything a filename cannot carry on some platform, plus the leading and
  // trailing whitespace that makes two presets look identical in a menu.
  auto name = raw.removeCharacters("\\/:*?\"<>|").trim();

  return name.substring(0, 64).trim();
}

std::unique_ptr<juce::XmlElement> capture(APVTS &apvts,
                                          const juce::String &name) {
  auto doc = std::make_unique<juce::XmlElement>(kPresetTag);
  doc->setAttribute("name", name);
  doc->setAttribute("version", 1);

  for (auto *p : apvts.processor.getParameters()) {
    auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p);
    if (ranged == nullptr)
      continue;

    auto *entry = doc->createNewChildElement(kParamTag);
    entry->setAttribute("id", ranged->paramID);

    // Plain rather than normalised, so widening a range later does not move
    // every preset that was saved before it.
    entry->setAttribute("value",
                        (double)ranged->convertFrom0to1(ranged->getValue()));
  }

  return doc;
}

int restore(APVTS &apvts, const juce::XmlElement &doc) {
  if (!doc.hasTagName(kPresetTag))
    return -1;

  int applied = 0;

  for (auto *entry : doc.getChildWithTagNameIterator(kParamTag)) {
    const auto id = entry->getStringAttribute("id");

    if (auto *p = apvts.getParameter(id)) {
      const auto plain = (float)entry->getDoubleAttribute("value");
      p->setValueNotifyingHost(p->convertTo0to1(plain));
      ++applied;
    }
  }

  return applied;
}

bool save(APVTS &apvts, const juce::String &name, juce::String &error) {
  const auto clean = sanitiseName(name);

  if (clean.isEmpty()) {
    error = "That name has nothing in it that a file can be called.";
    return false;
  }

  const auto file = userDirectory().getChildFile(clean + kExtension);
  const auto doc = capture(apvts, clean);

  if (!doc->writeTo(file)) {
    error = "Could not write to " + file.getFullPathName();
    return false;
  }

  return true;
}

bool load(APVTS &apvts, const juce::File &file, juce::String &error) {
  const auto doc = juce::XmlDocument::parse(file);

  if (doc == nullptr) {
    error = file.getFileName() + " is not readable.";
    return false;
  }

  if (restore(apvts, *doc) < 0) {
    error = file.getFileName() + " is not an Overtonium preset.";
    return false;
  }

  return true;
}

juce::String factoryCode(APVTS &apvts, const juce::String &name) {
  juce::StringArray lines;

  lines.add("  case N: // " + name);
  lines.add("  {");
  lines.add("    ap.neutralBase();");
  lines.add("");

  const auto number = [](float v) {
    return juce::String(v, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(
               ".") +
           "f";
  };

  // Only what differs from the default, so the generated case reads as a
  // description of the patch rather than as a dump of all 640 values. It is a
  // starting point to tidy by hand, which is why the ones already in this file
  // use formulas where the shape has one.
  for (auto *p : apvts.processor.getParameters()) {
    auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p);
    if (ranged == nullptr)
      continue;

    const auto plain = ranged->convertFrom0to1(ranged->getValue());
    const auto fallback = ranged->convertFrom0to1(ranged->getDefaultValue());

    if (std::abs(plain - fallback) < 1.0e-6f)
      continue;

    lines.add("    ap.set(\"" + ranged->paramID + "\", " + number(plain) +
              ");");
  }

  lines.add("    break;");
  lines.add("  }");

  return lines.joinIntoString("\n") + "\n";
}

void apply(APVTS &apvts, int index) {
  const Applier ap{apvts};

  switch (index) {
  case 0: // Init
    ap.resetAllToDefault();
    break;

  case 1: // Drawbar Organ
  {
    ap.neutralBase();

    // A registration in the spirit of 88 8000 000: octaves and fifths, no
    // sevenths.
    static const float levels[kNumHarmonics] = {
        1.00f, 0.85f, 0.70f, 0.60f, 0.00f, 0.45f, 0.00f, 0.35f,
        0.00f, 0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 0.00f, 0.15f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.08f};

    for (int i = 0; i < kNumHarmonics; ++i)
      ap.osc(params::volumeSuffix, i, levels[i]);

    ap.allOsc(params::attackSuffix, [](int) { return 0.008; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.06; });

    // The click a drawbar organ makes as the contacts break. It is on the
    // upper drawbars only, it is louder than the note was holding, and it is
    // over in a few tens of milliseconds.
    ap.allOsc(params::swellSuffix, [](int) { return 0.002; });
    ap.allOsc(params::offLevelSuffix,
              [](int n) { return n >= 8 ? 0.55 : 0.0; });
    break;
  }

  case 2: // Struck Bell
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return 0.9 / std::pow((double)n, 0.8); });
    ap.allOsc(params::attackSuffix, [](int) { return 0.001; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    // Higher partials die away first, which is what makes a struck body sound
    // struck.
    ap.allOsc(params::decaySuffix,
              [](int n) { return 6.0 / (1.0 + 0.35 * (n - 1)); });
    ap.allOsc(params::releaseSuffix,
              [](int n) { return 6.0 / (1.0 + 0.35 * (n - 1)); });
    // Strike it harder and the upper partials arrive, the way a real bar or
    // string brightens with force.
    ap.allOsc(params::velSuffix,
              [](int n) { return std::min(1.0, 0.2 + 0.06 * (n - 1)); });

    // A small, quick room. Struck things are heard somewhere.
    ap.reverb(0.22f, 1.8f, 0.5f);
    break;
  }

  case 3: // Slow Pad
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return n <= 16 ? 1.0 / n : 0.0; });
    // Staggered entries: the spectrum unfolds over a couple of seconds while
    // every partial keeps the same attack shape.
    ap.allOsc(params::delaySuffix, [](int n) { return 0.05 * (n - 1); });
    ap.allOsc(params::attackSuffix, [](int) { return 0.8; });
    ap.allOsc(params::decaySuffix, [](int) { return 4.0; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.8; });
    ap.allOsc(params::releaseSuffix, [](int) { return 3.0; });
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.15; });
    ap.allOsc(params::amRateSuffix, [](int n) { return 0.3 + 0.07 * n; });
    ap.allOsc(params::driftSuffix, [](int) { return 7.0; });

    ap.fanOut(0.7);
    ap.reverb(0.35f, 5.0f, 0.55f);
    break;
  }

  case 4: // Odd Harmonics
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix,
              [](int n) { return (n % 2) ? 1.0 / n : 0.0; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.02; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.2; });
    ap.allOsc(params::velSuffix,
              [](int n) { return std::min(1.0, 0.3 + 0.05 * (n - 1)); });
    break;
  }

  case 5: // Just Saw
  case 6: // Equal Saw
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix, [](int n) { return 1.0 / n; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.003; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.15; });

    // The pair exists to be A/B'd: identical but for the tuning of every
    // partial.
    const float blend = (index == 5) ? 1.0f : 0.0f;
    ap.allOsc(params::tuneSuffix, [blend](int) { return blend; });
    break;
  }

  case 7: // Shimmer
  {
    ap.neutralBase();

    ap.allOsc(params::volumeSuffix, [](int n) {
      return n == 1 ? 0.6 : (n >= 8 ? 0.5 / std::sqrt((double)n) : 0.0);
    });
    ap.allOsc(params::delaySuffix, [](int n) { return 0.04 * (n - 1); });
    ap.allOsc(params::attackSuffix, [](int) { return 1.5; });
    ap.allOsc(params::releaseSuffix, [](int) { return 4.0; });
    // Every partial breathes at its own rate, so the spectrum never repeats.
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.5; });
    ap.allOsc(params::amRateSuffix, [](int n) { return 0.15 + 0.05 * n; });
    ap.allOsc(params::pmDepthSuffix, [](int) { return 4.0; });
    ap.allOsc(params::pmRateSuffix, [](int n) { return 0.2 + 0.03 * n; });
    ap.allOsc(params::driftSuffix, [](int) { return 12.0; });

    ap.fanOut(1.0);
    ap.reverb(0.45f, 8.0f, 0.4f);
    ap.echo(0.28f, 0.66f, 0.55f, 0.6f);
    break;
  }

  case 8: // Vibraphone
  {
    ap.neutralBase();

    ap.allOsc(params::attackSuffix, [](int) { return 0.002; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });

    // The bar's three strongest modes, plus the motor.
    const int partials[3] = {1, 4, 10};
    const float levels[3] = {1.0f, 0.5f, 0.25f};
    const float decays[3] = {3.0f, 1.5f, 0.8f};

    for (int k = 0; k < 3; ++k) {
      const int i = partials[k] - 1;

      ap.osc(params::volumeSuffix, i, levels[k]);
      ap.osc(params::decaySuffix, i, decays[k]);
      ap.osc(params::releaseSuffix, i, decays[k]);
      ap.osc(params::amDepthSuffix, i, 0.6f);
      ap.osc(params::amRateSuffix, i, 5.0f);
    }

    // A short bar has less above its fundamental than a long one, which is why
    // the top of a vibraphone is a much plainer sound than the bottom.
    ap.series(0.0f, 3.0f);
    break;
  }

  case 9: // Harpsichord
  {
    ap.neutralBase();

    // Bright and thin, plucked rather than struck, so almost nothing decays
    // away while the key is held.
    ap.allOsc(params::volumeSuffix, [](int n) {
      return n <= 20 ? 0.75 / std::pow((double)n, 0.6) : 0.0;
    });
    ap.allOsc(params::attackSuffix, [](int) { return 0.001; });
    ap.allOsc(params::decaySuffix, [](int n) { return 8.0 / (1.0 + 0.2 * n); });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.25; });

    // The jack falling back onto the string when the key is let go. Louder
    // than the note was holding, and gone in a moment. This is the sound the
    // key-off stage exists for.
    ap.allOsc(params::swellSuffix, [](int) { return 0.001; });
    ap.allOsc(params::offLevelSuffix,
              [](int n) { return std::min(0.8, 0.25 + 0.05 * n); });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.09; });

    // A short treble string carries far less of the pluck's edge than a long
    // bass one, which is why a harpsichord's top octave sounds thin rather
    // than merely high.
    ap.series(0.0f, 2.5f);

    ap.fanOut(0.35);
    ap.reverb(0.18f, 1.2f, 0.45f);
    break;
  }

  case 10: // Music Box
  {
    ap.neutralBase();

    // A comb tooth: a handful of inharmonic-sounding upper partials, each
    // ringing for a different length of time, and nothing holding at all.
    static const int teeth[6] = {1, 3, 7, 11, 17, 23};
    static const float levels[6] = {1.0f, 0.5f, 0.35f, 0.25f, 0.18f, 0.12f};

    for (int k = 0; k < 6; ++k) {
      const int i = teeth[k] - 1;

      ap.osc(params::volumeSuffix, i, levels[k]);
      ap.osc(params::attackSuffix, i, 0.0008f);
      ap.osc(params::decaySuffix, i, 3.5f / (1.0f + 0.4f * (float)k));
      ap.osc(params::releaseSuffix, i, 3.5f / (1.0f + 0.4f * (float)k));
      ap.osc(params::sustainSuffix, i, 0.0f);

      // Each tooth somewhere different, which is what a comb sounds like from
      // a foot away.
      ap.osc(params::panSuffix, i,
             (k % 2 == 0 ? -1.0f : 1.0f) * (0.15f + 0.14f * (float)k));
    }

    // The cylinder pin releasing the tooth.
    ap.allOsc(params::swellSuffix, [](int) { return 0.0015; });
    ap.allOsc(params::offLevelSuffix, [](int n) { return n >= 7 ? 0.3 : 0.0; });

    // Picking odd teeth was always a stand-in for the real thing: a steel comb
    // tooth is a bar, and a bar does not ring in whole-number ratios. Now the
    // teeth are pushed off harmonic as well as picked, so the top of the comb
    // shimmers instead of fusing. The 23rd sits about 170 cents sharp.
    //
    // A small comb also has very little left up top, which is what stops the
    // high end of the cylinder sounding like the low end transposed.
    ap.series(300.0f, 3.0f);
    break;
  }

  case 11: // Kalimba
  {
    ap.neutralBase();

    // A tine: strong fundamental, a couple of stretched upper modes, and the
    // thumb leaving the metal.
    ap.osc(params::volumeSuffix, 0, 1.0f);
    ap.osc(params::volumeSuffix, 3, 0.35f);
    ap.osc(params::volumeSuffix, 8, 0.22f);
    ap.osc(params::volumeSuffix, 14, 0.12f);

    ap.allOsc(params::attackSuffix, [](int) { return 0.003; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.allOsc(params::decaySuffix,
              [](int n) { return 2.2 / (1.0 + 0.25 * n); });
    ap.allOsc(params::releaseSuffix,
              [](int n) { return 2.2 / (1.0 + 0.25 * n); });

    // A soft thud rather than a click: slower, and below where the note was.
    ap.allOsc(params::swellSuffix, [](int) { return 0.02; });
    ap.allOsc(params::offLevelSuffix, [](int n) { return n == 1 ? 0.2 : 0.0; });

    ap.osc(params::panSuffix, 0, -0.2f);
    ap.osc(params::panSuffix, 3, 0.35f);
    ap.osc(params::panSuffix, 8, -0.5f);
    ap.osc(params::panSuffix, 14, 0.6f);

    // The gourd.
    ap.set(params::noiseParamId(params::volumeSuffix), 0.12f);
    ap.set(params::noiseParamId(params::colourSuffix), 0.25f);
    ap.set(params::noiseParamId(params::attackSuffix), 0.001f);
    ap.set(params::noiseParamId(params::decaySuffix), 0.09f);
    ap.set(params::noiseParamId(params::sustainSuffix), 0.0f);
    ap.set(params::noiseParamId(params::releaseSuffix), 0.09f);

    // Tines are more inharmonic than a music box comb, not less: the second
    // mode of a real one sits nowhere near the octave. Partials 4, 9 and 15
    // were already a hand-picked stand-in for that, and this pushes them
    // further out, the top one by well over a semitone.
    ap.series(500.0f, 3.0f);

    ap.reverb(0.24f, 1.0f, 0.55f);
    break;
  }

  case 12: // Cathedral
  {
    ap.neutralBase();

    // Principal chorus: octaves and fifths only, the registration an organ
    // builder would recognise, arriving slowly as the pipes speak.
    static const float levels[kNumHarmonics] = {
        1.00f, 0.80f, 0.55f, 0.60f, 0.00f, 0.40f, 0.00f, 0.45f,
        0.00f, 0.00f, 0.00f, 0.28f, 0.00f, 0.00f, 0.00f, 0.30f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.18f,
        0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.12f};

    // Eight ranks in phase add up fast. Scaled so a chord has somewhere to go
    // rather than living on the clipper.
    for (int i = 0; i < kNumHarmonics; ++i)
      ap.osc(params::volumeSuffix, i, 0.62f * levels[i]);

    ap.allOsc(params::attackSuffix, [](int n) { return 0.04 + 0.004 * n; });
    ap.allOsc(params::delaySuffix, [](int n) { return 0.004 * (n - 1); });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.25; });
    ap.allOsc(params::driftSuffix, [](int) { return 3.0; });

    // The pipes stop speaking before the room stops answering.
    ap.allOsc(params::swellSuffix, [](int) { return 0.05; });
    ap.allOsc(params::offLevelSuffix,
              [](int n) { return n >= 12 ? 0.2 : 0.0; });

    ap.fanOut(0.6);
    ap.reverb(0.5f, 9.0f, 0.35f);
    break;
  }

  case 13: // Tape Choir
  {
    ap.neutralBase();

    // Vowel-ish: a formant hump in the low partials rather than a straight
    // roll-off, breathing at its own rate on every channel.
    ap.allOsc(params::volumeSuffix, [](int n) {
      const double formant = std::exp(-0.5 * std::pow((n - 3.0) / 2.6, 2.0));
      return n <= 14 ? 0.25 + 0.75 * formant : 0.0;
    });

    ap.allOsc(params::attackSuffix, [](int) { return 0.35; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.9; });
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.22; });
    ap.allOsc(params::amRateSuffix, [](int n) { return 0.4 + 0.09 * n; });
    ap.allOsc(params::driftSuffix, [](int) { return 14.0; });
    ap.allOsc(params::velSuffix, [](int) { return 0.4; });

    // Voices thin out at the top of a range rather than getting brighter, and
    // the machine takes a little more off after them.
    ap.series(0.0f, 3.5f);

    ap.fanOut(0.85);

    // An old machine: dark, unsteady repeats a beat and a half behind.
    ap.echo(0.33f, 0.5f, 0.55f, 0.8f);
    ap.reverb(0.4f, 4.5f, 0.5f);
    break;
  }

  case 14: // Glass Armonica
  {
    ap.neutralBase();

    // Rubbed glass: a strong fundamental with a few high partials that take
    // their time to speak, and never quite hold still.
    ap.allOsc(params::volumeSuffix, [](int n) {
      if (n == 1)
        return 0.9;

      return (n % 2 == 1 && n <= 15) ? 0.5 / std::pow((double)n, 0.7) : 0.0;
    });

    ap.allOsc(params::attackSuffix, [](int n) { return 0.5 + 0.09 * n; });
    ap.allOsc(params::decaySuffix, [](int) { return 6.0; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.75; });
    ap.allOsc(params::releaseSuffix, [](int) { return 2.5; });
    ap.allOsc(params::driftSuffix, [](int) { return 9.0; });
    ap.allOsc(params::pmDepthSuffix, [](int) { return 3.0; });
    ap.allOsc(params::pmRateSuffix, [](int n) { return 4.5 + 0.3 * n; });

    // Lifting the finger lets the rim ring on a moment longer than it was.
    ap.allOsc(params::swellSuffix, [](int) { return 0.4; });
    ap.allOsc(params::offLevelSuffix, [](int) { return 0.85; });

    // Glass is nearly pure, so barely any: enough that the upper partials beat
    // against the fundamental instead of locking to it, which is most of what
    // separates a rubbed rim from a sine wave. A smaller bowl is a purer one,
    // hence the tracking.
    ap.series(180.0f, 4.0f);

    ap.fanOut(0.9);
    ap.reverb(0.45f, 6.0f, 0.25f);
    break;
  }

  default:
    jassertfalse;
    break;
  }
}

} // namespace ovt::presets
