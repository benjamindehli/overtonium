#include "Presets.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <vector>

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

  /// One value per strip, in order, for a row that was drawn rather than
  /// derived.
  ///
  /// Some of these presets were dialled in by hand, often by dragging one
  /// knob with LINK fanning it across the series, and the result is thirty-two
  /// values with no formula behind them. Written out as a list the shape is at
  /// least visible, where thirty-two separate lines would bury it.
  void oscTable(const char *suffix,
                std::initializer_list<float> values) const {
    int i = 0;

    for (auto v : values) {
      if (i >= kNumHarmonics)
        break;

      osc(suffix, i++, v);
    }
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
    set(params::noiseParamId(params::amRateSuffix), 4.0f);
    set(params::noiseParamId(params::amDepthSuffix), 0.0f);
    set(params::noiseParamId(params::velSuffix), 0.7f);
    set(params::noiseParamId(params::atSuffix), 0.0f);
    set(params::noiseParamId(params::muteSuffix), 0.0f);
    set(params::noiseParamId(params::soloSuffix), 0.0f);
    set(params::noiseParamId(params::panSuffix), 0.0f);

    // Everything global that is part of the sound rather than part of the
    // setup. A preset decides what the instrument is, so it has to decide
    // these too: loading one with the series stretched into a bell, or the
    // converter down at eight bits, has to give the patch that was designed
    // rather than that patch through whatever was left over.
    //
    // What a preset must not touch is listed once, as kSessionParamIds, and
    // nothing here writes to any of it. The temperament is the one worth
    // spelling out, since this instrument is otherwise full of tunings and it
    // would be easy to file with them: it belongs with the reference pitch
    // instead. You set it once for the music you are playing, and loading a
    // sound in the middle of that should not drag you back to equal.
    set(params::stretchId, 0.0f);
    set(params::trackId, 0.0f);
    set(params::wobbleId, 0.0f);
    set(params::lofiRateId, 0.0f);
    set(params::lofiBitsId, 0.0f);

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
    "2-bit Fuzz Organ",
    "Big Saw",
    "Cathedral",
    "DigiLog",
    "Dire Dire EP",
    "Drawbar Organ",
    "EP Chimes",
    "Equal Saw",
    "FM Piano",
    "Glass Armonica",
    "Init",
    "Just Saw",
    "Lo-fi",
    "Metallic Piano",
    "Music Box",
    "Nylon EP",
    "Odd Harmonics",
    "Omni-84",
    "Shimmer",
    "Slow Pad",
    "Space Flute",
    "Struck Bell",
    "Synth Ensemble",
    "Tape Choir",
    "Vibraphone",
    "Wurli",
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

    // The same rule the factory presets follow. A preset someone saves is a
    // sound they liked, not the state of their keyboard, and writing the
    // polyphony and the temperament into it would mean recalling that sound
    // later reached over and changed both.
    if (params::isSessionParam(ranged->paramID))
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

    // Files written before the rule existed name these. Skipped rather than
    // honoured, so an old preset stops dragging its author's polyphony and
    // temperament along with the sound.
    if (params::isSessionParam(id))
      continue;

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

void neutralBase(APVTS &apvts) { Applier{apvts}.neutralBase(); }

juce::String factoryCode(APVTS &apvts, const juce::String &name) {
  const Applier ap{apvts};

  juce::StringArray lines;

  lines.add("  case N: // " + name);
  lines.add("  {");
  lines.add("    ap.neutralBase();");
  lines.add("");

  const auto number = [](float v) {
    auto text = juce::String(v, 4).trimCharactersAtEnd("0");

    // A float literal needs something after the point: 5.0f compiles and 5f
    // does not. Trimming all the way to the point and then past it is what
    // made every whole number in the generated code a compiler error.
    if (text.endsWithChar('.'))
      text += "0";

    return text + "f";
  };

  const auto plainOf = [](juce::RangedAudioParameter *r) {
    return r->convertFrom0to1(r->getValue());
  };

  // What the generated case has to differ from is what neutralBase leaves,
  // which is not what the parameters default to. The per-partial levels are
  // the case that matters: they default to a 1/n spectrum and neutralBase
  // takes them all to zero, so measuring against the defaults drops any
  // partial the patch happens to leave at its default and the preset comes
  // back missing it. A fundamental at full level is exactly that.
  //
  // So the baseline is read rather than assumed: the patch is put aside,
  // neutralBase is run, the result is recorded, and the patch is put back.
  // That does mean the instrument passes through a neutral state on the way,
  // which is momentary and only happens when this is deliberately called.
  const auto patch = capture(apvts, name);

  ap.neutralBase();

  std::map<juce::String, float> baseline;
  for (auto *p : apvts.processor.getParameters())
    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p))
      baseline[ranged->paramID] = plainOf(ranged);

  if (patch != nullptr)
    restore(apvts, *patch);

  // Only what differs, so the generated case reads as a description of the
  // patch rather than as a dump of all 640 values.
  std::map<juce::String, float> changed;

  for (auto *p : apvts.processor.getParameters()) {
    auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p);
    if (ranged == nullptr)
      continue;

    // A preset may not touch the session. Emitting these would put the
    // temperament and the polyphony of whoever dialled the patch in into a
    // factory preset, which the test for that rule would then fail.
    if (params::isSessionParam(ranged->paramID))
      continue;

    const auto found = baseline.find(ranged->paramID);
    const auto from = found == baseline.end() ? plainOf(ranged) : found->second;

    if (std::abs(plainOf(ranged) - from) >= 1.0e-6f)
      changed[ranged->paramID] = plainOf(ranged);
  }

  // A row the whole series shares is one line rather than thirty-two, and a
  // row that was dragged across the mixer is a table rather than thirty-two.
  // Both are what the presets already in this file do by hand, and a patch
  // dialled in with LINK produces a great many of each.
  //
  // In panel order, which is the order neutralBase sets them in, so a
  // generated case reads down the strip the way the strip is drawn.
  struct Row {
    const char *suffix;
    const char *constant;
  };

  static const Row rows[] = {
      {params::tuneSuffix, "tuneSuffix"},
      {params::phaseSuffix, "phaseSuffix"},
      {params::pmRateSuffix, "pmRateSuffix"},
      {params::pmDepthSuffix, "pmDepthSuffix"},
      {params::driftSuffix, "driftSuffix"},
      {params::delaySuffix, "delaySuffix"},
      {params::attackSuffix, "attackSuffix"},
      {params::decaySuffix, "decaySuffix"},
      {params::sustainSuffix, "sustainSuffix"},
      {params::swellSuffix, "swellSuffix"},
      {params::offLevelSuffix, "offLevelSuffix"},
      {params::releaseSuffix, "releaseSuffix"},
      {params::liftSuffix, "liftSuffix"},
      {params::amRateSuffix, "amRateSuffix"},
      {params::amDepthSuffix, "amDepthSuffix"},
      {params::velSuffix, "velSuffix"},
      {params::atSuffix, "atSuffix"},
      {params::muteSuffix, "muteSuffix"},
      {params::soloSuffix, "soloSuffix"},
      {params::volumeSuffix, "volumeSuffix"},
      {params::panSuffix, "panSuffix"},
  };

  for (const auto &row : rows) {
    std::vector<float> values;

    for (int i = 0; i < kNumHarmonics; ++i) {
      const auto found = changed.find(params::oscParamId(row.suffix, i));

      if (found == changed.end())
        break;

      values.push_back(found->second);
    }

    // Only when the whole series moved. A row where some strips are at the
    // neutral value and some are not is left as individual sets, since
    // pretending otherwise would write the neutral value in as if it had been
    // chosen.
    if ((int)values.size() != kNumHarmonics)
      continue;

    for (int i = 0; i < kNumHarmonics; ++i)
      changed.erase(params::oscParamId(row.suffix, i));

    const auto uniform =
        std::adjacent_find(values.begin(), values.end(),
                           [](float a, float b) {
                             return std::abs(a - b) >= 1.0e-6f;
                           }) == values.end();

    const juce::String constant = juce::String("params::") + row.constant;

    if (uniform) {
      // Without the f, because the ones already in this file return a double
      // and allOsc narrows it once rather than at every call site.
      lines.add("    ap.allOsc(" + constant + ", [](int) { return " +
                number(values.front()).dropLastCharacters(1) + "; });");
      continue;
    }

    juce::StringArray written;
    for (auto v : values)
      written.add(number(v));

    // Wrapped to the column limit the rest of the file keeps to, filling each
    // line before starting the next, which is what clang-format would do with
    // a braced list this long.
    const juce::String open = "    ap.oscTable(" + constant + ",";
    lines.add(open);

    const juce::String indent = "                ";
    juce::String line = indent + "{";

    for (int i = 0; i < written.size(); ++i) {
      const auto piece = written[i] + (i + 1 < written.size() ? "," : "});");

      if (line.length() + 1 + piece.length() > 80) {
        lines.add(line);
        line = indent + " " + piece;
      } else {
        line += (line.endsWithChar('{') ? "" : " ") + piece;
      }
    }

    lines.add(line);
  }

  // Whatever the rows above did not account for. Strips first and the rest
  // after, so a case reads as the mixer and then the bar above it, which is
  // the order the cases already here are written in.
  for (bool strips : {true, false})
    for (const auto &entry : changed)
      if (entry.first.startsWithChar('h') == strips)
        lines.add("    ap.set(\"" + entry.first + "\", " +
                  number(entry.second) + ");");

  lines.add("    break;");
  lines.add("  }");

  return lines.joinIntoString("\n") + "\n";
}

void apply(APVTS &apvts, int index) {
  const Applier ap{apvts};

  switch (index) {
  case 0: // 2-bit Fuzz Organ
  {
    ap.neutralBase();

    // An organ at 8 kHz and 2 bits. The aliasing and the quantisation noise are
    // the distortion, so the converter is the sound rather than an effect on
    // it. Aftertouch blends the root note towards a fifth.
    ap.oscTable(params::attackSuffix,
                {0.0002f, 0.0002f, 0.0002f, 0.0008f, 0.0008f, 0.0008f, 0.0008f,
                 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f,
                 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f,
                 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f, 0.0008f,
                 0.0008f, 0.0008f, 0.0008f, 0.0008f});
    ap.allOsc(params::releaseSuffix, [](int) { return 0.008; });
    ap.set("h01_aftertouch", -1.0f);
    ap.set("h01_drift", 1.911f);
    ap.set("h01_pan", -0.7987f);
    ap.set("h01_sustain", 0.5087f);
    ap.set("h01_vel", 0.0003f);
    ap.set("h01_volume", 0.9967f);
    ap.set("h02_pan", 0.7985f);
    ap.set("h02_vel", 0.0017f);
    ap.set("h02_volume", 0.4468f);
    ap.set("h03_aftertouch", 1.0f);
    ap.set("h03_drift", 1.1388f);
    ap.set("h03_pan", -0.7971f);
    ap.set("h03_sustain", 0.5084f);
    ap.set("h03_vel", 0.0021f);
    ap.set("echoAge", 0.3247f);
    ap.set("echoFeedback", 0.057f);
    ap.set("echoMix", 0.1557f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.0616f);
    ap.set("lofiBits", 8.0f);
    ap.set("lofiRate", 5.0f);
    ap.set("noise_attack", 0.0002f);
    ap.set("noise_colour", 0.063f);
    ap.set("noise_decay", 0.0478f);
    ap.set("noise_offLevel", 1.0f);
    ap.set("noise_release", 0.1781f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_swell", 0.0f);
    ap.set("noise_volume", 0.0846f);
    ap.set("reverbDamp", 0.3249f);
    ap.set("reverbDecay", 0.3399f);
    ap.set("reverbMix", 0.1221f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0031f);
    ap.set("stretch", 6.9869f);
    ap.set("track", 1.6f);
    ap.set("wobble", 0.0762f);
    break;
  }
  case 1: // Big Saw
  {
    ap.neutralBase();

    // A sawtooth with a little randomness in it, so it drifts and beats rather
    // than sitting still.
    ap.oscTable(params::tuneSuffix,
                {0.9873f, 0.9868f, 0.9863f, 0.9858f, 0.9853f, 0.9848f, 0.9842f,
                 0.9837f, 0.9831f, 0.9825f, 0.9818f, 0.9812f, 0.9805f, 0.9798f,
                 0.9791f, 0.9783f, 0.9775f, 0.9767f, 0.9759f, 0.975f, 0.9741f,
                 0.9732f, 0.9722f, 0.9712f, 0.9702f, 0.9691f, 0.968f, 0.9668f,
                 0.9656f, 0.9644f, 0.9631f, 0.9618f});
    ap.oscTable(params::pmRateSuffix,
                {1.7915f, 0.9317f, 1.071f, 1.0881f, 3.0699f, 1.0402f, 1.597f,
                 0.9564f, 0.5954f, 0.6407f, 1.0824f, 2.0914f, 0.7898f, 1.3132f,
                 3.0113f, 1.5697f, 1.9827f, 2.6223f, 0.8398f, 1.4885f, 1.3851f,
                 2.9402f, 0.7304f, 0.8508f, 1.0324f, 1.8328f, 2.6676f, 0.8274f,
                 1.2262f, 0.7063f, 2.1673f, 2.4451f});
    ap.oscTable(params::pmDepthSuffix,
                {5.3178f, 6.938f, 6.7333f, 6.7724f, 6.9951f, 5.4263f, 5.3793f,
                 5.9278f, 5.4725f, 6.0772f, 5.6127f, 6.6078f, 6.9192f, 6.9737f,
                 6.9843f, 5.338f, 4.9724f, 6.2468f, 6.3841f, 6.1523f, 6.8957f,
                 5.8485f, 5.1044f, 4.9621f, 6.8055f, 5.113f, 6.0504f, 5.1587f,
                 5.7398f, 5.5542f, 6.0119f, 5.415f});
    ap.allOsc(params::driftSuffix, [](int) { return 2.3012; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.0005; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.0001; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.0091; });
    ap.oscTable(params::amRateSuffix,
                {0.4542f, 0.5054f, 1.1596f, 0.5264f, 1.1346f, 0.8222f, 1.1718f,
                 1.1098f, 1.124f, 0.2293f, 1.0871f, 0.7674f, 0.3457f, 0.3881f,
                 0.4234f, 0.4125f, 0.3606f, 0.3815f, 0.311f, 0.7699f, 0.2498f,
                 0.2824f, 0.8247f, 0.8086f, 0.231f, 0.9646f, 0.3289f, 1.1644f,
                 0.3794f, 0.4768f, 1.1093f, 0.4142f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.3399; });
    ap.oscTable(params::velSuffix,
                {-0.0138f, 0.0202f, 0.0544f, 0.0887f, 0.1234f, 0.1584f,
                 0.1937f, 0.2294f, 0.2656f, 0.3023f, 0.3395f, 0.344f, 0.3496f,
                 0.3818f, 0.4145f, 0.4478f, 0.4816f, 0.516f, 0.551f, 0.5868f,
                 0.6233f, 0.6606f, 0.6987f, 0.7377f, 0.7776f, 0.8185f, 0.8605f,
                 0.9035f, 0.9476f, 0.993f, 1.0f, 1.0f});
    ap.allOsc(params::volumeSuffix, [](int n) { return 1.0 / n; });
    ap.oscTable(params::panSuffix,
                {-0.1097f, 0.3008f, 0.6037f, -0.5251f, 0.0844f, 0.8891f, -1.0f,
                 0.2821f, 0.0925f, -0.873f, -0.3979f, -0.907f, -0.7676f,
                 0.5129f, -0.9594f, -0.3613f, 0.2443f, -0.9202f, -0.9353f,
                 -0.7972f, 0.2833f, -0.8729f, 0.5899f, -0.0412f, 0.4507f,
                 0.7306f, -0.955f, 0.1488f, -0.9092f, 0.5894f, -0.1532f,
                 -0.4559f});
    ap.set("stretch", 2.0447f);
    ap.set("track", 1.3f);
    ap.set("wobble", 0.1038f);
    ap.set("lofiBits", 4.0f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.3853f);
    ap.set("echoTime", 0.061f);
    ap.set("echoFeedback", 0.1319f);
    ap.set("echoAge", 0.2864f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.1834f);
    ap.set("reverbDecay", 2.7018f);
    ap.set("reverbDamp", 0.1522f);
    ap.set("reverbPreDelay", 0.0051f);
    ap.set("noise_colour", 1.0f);
    ap.set("noise_attack", 0.0005f);
    ap.set("noise_decay", 0.4818f);
    ap.set("noise_sustain", 0.5519f);
    ap.set("noise_swell", 0.0f);
    ap.set("noise_release", 0.009f);
    ap.set("noise_amDepth", 0.6765f);
    ap.set("noise_volume", 0.0337f);
    break;
  }
  case 2: // Cathedral
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
  case 3: // DigiLog
  {
    ap.neutralBase();

    // A digital instrument through analogue gear that has seen better days,
    // which is where the name comes from. The envelope delay climbs with the
    // partial, from almost nothing on the fundamental to five seconds on the
    // 32nd, so the series arrives from the bottom up and reads as a filter
    // opening in steps rather than sweeping. The converter at 16 kHz and 8
    // bits is what makes the steps audible as steps, and the echo, at as much
    // feedback as it will take, carries what is left of them.

    ap.oscTable(params::pmRateSuffix,
                {1.2239f, 1.3432f, 1.4753f, 1.6203f, 1.6266f, 1.6328f, 1.6391f,
                 1.6454f, 1.6517f, 1.6581f, 1.6644f, 1.6708f, 1.6772f, 1.6837f,
                 1.6901f, 1.6966f, 1.7032f, 1.7097f, 1.7163f, 1.7229f, 1.7295f,
                 1.7361f, 1.7428f, 1.7495f, 1.7562f, 1.7629f, 1.7697f, 1.7765f,
                 1.7833f, 1.7902f, 1.7971f, 1.8027f});
    ap.oscTable(params::pmDepthSuffix,
                {2.2198f, 2.5433f, 2.8887f, 3.252f, 3.6339f, 4.0354f, 4.4577f,
                 4.9017f, 5.3685f, 5.8594f, 6.3755f, 6.9182f, 7.4888f, 8.0889f,
                 8.7198f, 9.3831f, 10.0808f, 10.8142f, 11.5855f, 12.3963f,
                 13.2491f, 14.1456f, 15.0882f, 16.0794f, 17.1217f, 18.2177f,
                 19.3701f, 20.5819f, 21.8559f, 23.1956f, 24.6042f, 26.0618f});
    ap.oscTable(params::driftSuffix,
                {0.8449f, 0.8787f, 0.9131f, 0.9478f, 0.9828f, 1.0182f, 1.0538f,
                 1.0899f, 1.1262f, 1.1629f, 1.2f, 1.2374f, 1.2751f, 1.3132f,
                 1.3517f, 1.3905f, 1.4297f, 1.4693f, 1.5092f, 1.5495f, 1.5902f,
                 1.6313f, 1.6728f, 1.7146f, 1.7569f, 1.7995f, 1.8426f, 1.886f,
                 1.9299f, 1.9741f, 2.0188f, 2.0459f});
    ap.oscTable(params::delaySuffix,
                {0.0001f, 0.002f, 0.0044f, 0.0074f, 0.0111f, 0.0155f, 0.0211f,
                 0.0278f, 0.0361f, 0.0463f, 0.0589f, 0.0743f, 0.0931f, 0.1163f,
                 0.1447f, 0.1797f, 0.2225f, 0.2751f, 0.3397f, 0.419f, 0.5164f,
                 0.6359f, 0.7825f, 0.9626f, 1.1836f, 1.455f, 1.7881f, 2.197f,
                 2.6989f, 3.3151f, 4.0715f, 5.0f});
    ap.oscTable(params::attackSuffix,
                {0.0228f, 0.0199f, 0.0173f, 0.0151f, 0.0132f, 0.0115f, 0.01f,
                 0.0087f, 0.0076f, 0.0066f, 0.0057f, 0.005f, 0.0044f, 0.0038f,
                 0.0033f, 0.0029f, 0.0025f, 0.0022f, 0.0019f, 0.0017f, 0.0014f,
                 0.0013f, 0.0011f, 0.001f, 0.0008f, 0.0007f, 0.0006f, 0.0006f,
                 0.0005f, 0.0004f, 0.0004f, 0.0003f});
    ap.allOsc(params::decaySuffix, [](int) { return 0.278; });
    ap.oscTable(params::sustainSuffix,
                {0.7177f, 0.6958f, 0.6739f, 0.652f, 0.6301f, 0.6082f, 0.5863f,
                 0.5644f, 0.5425f, 0.5207f, 0.4988f, 0.4769f, 0.455f, 0.4331f,
                 0.4112f, 0.3893f, 0.3674f, 0.3455f, 0.3236f, 0.3017f, 0.2798f,
                 0.2579f, 0.236f, 0.2141f, 0.1922f, 0.1703f, 0.1484f, 0.1265f,
                 0.1047f, 0.0828f, 0.0609f, 0.0396f});
    ap.oscTable(params::swellSuffix,
                {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0004f,
                 0.0009f, 0.0015f});
    ap.oscTable(params::releaseSuffix,
                {0.681f, 0.5768f, 0.4885f, 0.4138f, 0.3504f, 0.2968f, 0.2514f,
                 0.2129f, 0.1803f, 0.1527f, 0.1294f, 0.1096f, 0.0928f, 0.0786f,
                 0.0666f, 0.0564f, 0.0477f, 0.0404f, 0.0343f, 0.029f, 0.0246f,
                 0.0208f, 0.0176f, 0.0149f, 0.0126f, 0.0107f, 0.0091f, 0.0077f,
                 0.0065f, 0.0055f, 0.0047f, 0.004f});
    ap.oscTable(params::amRateSuffix,
                {0.455f, 0.4903f, 0.5284f, 0.5694f, 0.6136f, 0.6613f, 0.7126f,
                 0.768f, 0.8276f, 0.8919f, 0.9611f, 1.0358f, 1.1162f, 1.2029f,
                 1.2963f, 1.397f, 1.5055f, 1.6224f, 1.7484f, 1.8842f, 2.0305f,
                 2.1882f, 2.3582f, 2.5413f, 2.7387f, 2.9514f, 3.1806f, 3.4276f,
                 3.6938f, 3.9806f, 4.2898f, 4.6129f});
    ap.oscTable(params::amDepthSuffix,
                {0.5523f, 0.5572f, 0.5623f, 0.5673f, 0.5723f, 0.5774f, 0.5824f,
                 0.5875f, 0.5925f, 0.5976f, 0.6026f, 0.6077f, 0.6127f, 0.6178f,
                 0.6228f, 0.6279f, 0.6329f, 0.638f, 0.643f, 0.6481f, 0.6531f,
                 0.6581f, 0.6632f, 0.6682f, 0.6733f, 0.6783f, 0.6834f, 0.6884f,
                 0.6935f, 0.6985f, 0.7036f, 0.7086f});
    ap.oscTable(params::velSuffix,
                {0.1063f, 0.1599f, 0.2144f, 0.2689f, 0.3235f, 0.378f, 0.4325f,
                 0.487f, 0.5416f, 0.5961f, 0.6506f, 0.7051f, 0.7597f, 0.8142f,
                 0.8687f, 0.8949f, 0.9015f, 0.908f, 0.9146f, 0.9212f, 0.9277f,
                 0.9343f, 0.9409f, 0.9475f, 0.954f, 0.9606f, 0.9672f, 0.9737f,
                 0.9803f, 0.9869f, 0.9934f, 0.9998f});
    ap.allOsc(params::volumeSuffix, [](int) { return 1.0; });
    ap.set("h02_pan", 0.2531f);
    ap.set("h03_pan", -0.5048f);
    ap.set("h04_pan", 0.7488f);
    ap.set("h05_pan", -1.0f);
    ap.set("h06_pan", 1.0f);
    ap.set("h07_pan", -1.0f);
    ap.set("h08_pan", 1.0f);
    ap.set("h09_pan", -1.0f);
    ap.set("h10_pan", 1.0f);
    ap.set("h11_pan", -1.0f);
    ap.set("h12_pan", 1.0f);
    ap.set("h13_pan", -1.0f);
    ap.set("h14_pan", 1.0f);
    ap.set("h15_pan", -1.0f);
    ap.set("h16_pan", 1.0f);
    ap.set("h17_pan", -1.0f);
    ap.set("h18_pan", 1.0f);
    ap.set("h19_pan", -1.0f);
    ap.set("h20_pan", 1.0f);
    ap.set("h21_pan", -1.0f);
    ap.set("h22_pan", 1.0f);
    ap.set("h23_pan", -1.0f);
    ap.set("h24_pan", 1.0f);
    ap.set("h25_pan", -1.0f);
    ap.set("h26_pan", 1.0f);
    ap.set("h27_pan", -1.0f);
    ap.set("h28_pan", 1.0f);
    ap.set("h29_pan", -1.0f);
    ap.set("h30_pan", 1.0f);
    ap.set("h31_pan", -1.0f);
    ap.set("h32_pan", 1.0f);
    ap.set("echoAge", 0.6872f);
    ap.set("echoFeedback", 0.95f);
    ap.set("echoMix", 0.2348f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.1882f);
    ap.set("lofiBits", 4.0f);
    ap.set("lofiRate", 3.0f);
    ap.set("noise_amDepth", 1.0f);
    ap.set("noise_amRate", 11.6296f);
    ap.set("noise_attack", 2.5622f);
    ap.set("noise_colour", 0.4131f);
    ap.set("noise_decay", 4.1039f);
    ap.set("noise_release", 3.5761f);
    ap.set("noise_sustain", 0.0388f);
    ap.set("noise_swell", 0.0045f);
    ap.set("noise_vel", 0.4984f);
    ap.set("noise_volume", 0.3251f);
    ap.set("reverbDamp", 0.0214f);
    ap.set("reverbDecay", 3.9958f);
    ap.set("reverbMix", 0.3029f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0528f);
    ap.set("stretch", 29.8091f);
    ap.set("track", 3.0f);
    ap.set("wobble", 0.1476f);
    break;
  }
  case 4: // Dire Dire EP
  {
    ap.neutralBase();

    // A soft, round electric piano of the kind written for underwater music.
    // The body is the fundamental and a few low partials, with the 13th and
    // the 15th left standing well clear of their neighbours, which is the
    // tine ringing over the top of an otherwise dark sound. Tracking at
    // 6 dB per octave is what keeps the right hand from turning glassy.

    ap.allOsc(params::tuneSuffix, [](int) { return 0.9655; });
    ap.oscTable(params::pmRateSuffix,
                {1.4995f, 2.0553f, 1.1046f, 3.5403f, 1.7097f, 1.75f, 0.7069f,
                 1.0506f, 1.0513f, 1.9392f, 0.9656f, 0.7603f, 1.6567f, 1.4603f,
                 1.8324f, 1.1882f, 1.6998f, 2.3397f, 1.7239f, 1.833f, 0.8004f,
                 2.6916f, 1.5028f, 1.9406f, 1.8941f, 0.6804f, 1.5446f, 1.7461f,
                 2.2189f, 2.3081f, 0.6875f, 1.1212f});
    ap.allOsc(params::pmDepthSuffix, [](int) { return 2.8852; });
    ap.allOsc(params::driftSuffix, [](int) { return 1.4501; });
    ap.oscTable(params::attackSuffix,
                {0.0338f, 0.0293f, 0.251f, 0.2223f, 0.0244f, 0.0158f, 0.0138f,
                 0.0161f, 0.0152f, 0.0139f, 0.0146f, 0.0154f, 0.0328f, 0.0136f,
                 0.0205f, 0.008f, 0.0145f, 0.0136f, 0.0136f, 0.0136f, 0.0136f,
                 0.0136f, 0.0136f, 0.0136f, 0.0136f, 0.0136f, 0.0163f, 0.0136f,
                 0.0136f, 0.0136f, 0.0136f, 0.0119f});
    ap.oscTable(params::decaySuffix,
                {1.587f, 1.4717f, 2.1773f, 1.349f, 2.0845f, 1.587f, 1.5019f,
                 1.251f, 1.2012f, 1.6055f, 1.6102f, 1.6148f, 1.0181f, 1.4717f,
                 0.9171f, 1.2223f, 1.1236f, 1.1074f, 1.648f, 1.6528f, 1.6576f,
                 1.6624f, 1.6672f, 1.6721f, 1.6769f, 1.6818f, 1.6867f, 1.6916f,
                 1.6965f, 1.7014f, 1.7064f, 1.3765f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::releaseSuffix,
                {0.4193f, 0.4015f, 0.3664f, 0.367f, 0.3357f, 0.3213f, 0.3075f,
                 0.3067f, 0.2817f, 0.2696f, 0.258f, 0.247f, 0.2364f, 0.2262f,
                 0.2165f, 0.2142f, 0.1983f, 0.1898f, 0.1817f, 0.1739f, 0.1664f,
                 0.1593f, 0.1525f, 0.1459f, 0.1397f, 0.1337f, 0.1279f, 0.1225f,
                 0.1172f, 0.1122f, 0.1074f, 0.1045f});
    ap.oscTable(params::amRateSuffix,
                {2.0505f, 0.0739f, 1.6377f, 0.2203f, 0.112f, 5.9227f, 4.7012f,
                 1.6992f, 0.7001f, 3.5127f, 1.4771f, 0.0739f, 0.1476f, 1.5035f,
                 0.9039f, 0.1121f, 0.0739f, 0.4694f, 2.1529f, 0.0739f, 0.0739f,
                 0.3548f, 0.0923f, 2.7419f, 10.3041f, 1.0737f, 3.5348f, 0.0739f,
                 1.3994f, 0.0739f, 0.1197f, 0.1328f});
    ap.oscTable(params::amDepthSuffix,
                {0.5319f, 0.5319f, 0.5827f, 0.5319f, 0.5319f, 0.5827f, 0.5319f,
                 0.5319f, 0.5319f, 0.5319f, 0.5319f, 0.5827f, 0.5319f, 0.5319f,
                 0.5319f, 0.5319f, 0.5319f, 0.5319f, 0.5319f, 0.5319f, 0.5319f,
                 0.5319f, 0.5319f, 0.5827f, 0.5319f, 0.5319f, 0.5319f, 0.5319f,
                 0.5319f, 0.5319f, 0.5319f, 0.5319f});
    ap.oscTable(params::velSuffix,
                {0.0401f, 0.1531f, 0.2946f, 0.2554f, 0.3661f, 0.4129f, 0.4663f,
                 0.4896f, 0.5789f, 0.6194f, 0.6788f, 0.7382f, 0.7976f, 0.8468f,
                 0.8936f, 0.8594f, 0.9096f, 0.9136f, 0.9161f, 0.9172f, 0.925f,
                 0.9266f, 0.9377f, 0.945f, 0.9468f, 0.9585f, 0.9553f, 0.9703f,
                 0.9795f, 0.9864f, 1.0f, 0.9622f});
    ap.oscTable(params::volumeSuffix,
                {0.9995f, 0.1429f, 0.3142f, 0.2962f, 0.1049f, 0.0767f, 0.0283f,
                 0.0102f, 0.0042f, 0.0039f, 0.0041f, 0.0033f, 0.321f, 0.0066f,
                 0.2578f, 0.0144f, 0.0128f, 0.0114f, 0.0096f, 0.0088f, 0.007f,
                 0.006f, 0.0071f, 0.0116f, 0.0117f, 0.0135f, 0.0148f, 0.011f,
                 0.0098f, 0.0091f, 0.0097f, 0.0228f});
    ap.set("h01_aftertouch", 0.0016f);
    ap.set("h02_pan", 0.2552f);
    ap.set("h03_aftertouch", 0.0016f);
    ap.set("h03_pan", -0.5046f);
    ap.set("h04_pan", 0.7443f);
    ap.set("h05_aftertouch", 0.0015f);
    ap.set("h05_pan", -1.0f);
    ap.set("h06_aftertouch", 0.0014f);
    ap.set("h06_pan", 1.0f);
    ap.set("h07_aftertouch", 0.0014f);
    ap.set("h07_pan", -1.0f);
    ap.set("h08_aftertouch", 0.0013f);
    ap.set("h08_pan", 1.0f);
    ap.set("h09_aftertouch", 0.0013f);
    ap.set("h09_pan", -1.0f);
    ap.set("h10_aftertouch", 0.0012f);
    ap.set("h10_pan", 1.0f);
    ap.set("h11_aftertouch", 0.0012f);
    ap.set("h11_pan", -1.0f);
    ap.set("h12_aftertouch", 0.0011f);
    ap.set("h12_pan", 1.0f);
    ap.set("h13_aftertouch", 0.0011f);
    ap.set("h13_pan", -1.0f);
    ap.set("h14_aftertouch", 0.001f);
    ap.set("h14_pan", -1.0f);
    ap.set("h15_aftertouch", 0.0009f);
    ap.set("h15_lift", -0.0013f);
    ap.set("h15_pan", 1.0f);
    ap.set("h16_aftertouch", 0.0009f);
    ap.set("h16_pan", 1.0f);
    ap.set("h17_aftertouch", 0.0008f);
    ap.set("h17_pan", -1.0f);
    ap.set("h18_aftertouch", 0.0008f);
    ap.set("h18_pan", 1.0f);
    ap.set("h19_aftertouch", 0.0007f);
    ap.set("h19_pan", -1.0f);
    ap.set("h20_aftertouch", 0.0007f);
    ap.set("h20_pan", 1.0f);
    ap.set("h21_aftertouch", 0.0006f);
    ap.set("h21_pan", -1.0f);
    ap.set("h22_aftertouch", 0.0006f);
    ap.set("h22_pan", 1.0f);
    ap.set("h23_aftertouch", 0.0005f);
    ap.set("h23_pan", -1.0f);
    ap.set("h24_aftertouch", 0.0005f);
    ap.set("h24_pan", 1.0f);
    ap.set("h25_aftertouch", 0.0004f);
    ap.set("h25_pan", -1.0f);
    ap.set("h26_aftertouch", 0.0004f);
    ap.set("h26_pan", 1.0f);
    ap.set("h27_aftertouch", 0.0003f);
    ap.set("h27_pan", -1.0f);
    ap.set("h28_aftertouch", 0.0003f);
    ap.set("h28_pan", 1.0f);
    ap.set("h29_aftertouch", 0.0002f);
    ap.set("h29_pan", -1.0f);
    ap.set("h30_aftertouch", 0.0002f);
    ap.set("h30_pan", 1.0f);
    ap.set("h31_aftertouch", 0.0001f);
    ap.set("h31_pan", -1.0f);
    ap.set("h32_aftertouch", 0.0001f);
    ap.set("h32_pan", -1.0f);
    ap.set("echoAge", 0.3847f);
    ap.set("echoFeedback", 0.1458f);
    ap.set("echoMix", 0.2236f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.0882f);
    ap.set("lofiBits", 1.0f);
    ap.set("noise_attack", 0.0088f);
    ap.set("noise_colour", 0.0f);
    ap.set("noise_decay", 1.6672f);
    ap.set("noise_release", 0.3174f);
    ap.set("noise_sustain", 0.0165f);
    ap.set("noise_vel", 0.8135f);
    ap.set("noise_volume", 0.055f);
    ap.set("reverbDamp", 0.186f);
    ap.set("reverbDecay", 0.473f);
    ap.set("reverbMix", 0.2511f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0132f);
    ap.set("stretch", 7.9525f);
    ap.set("track", 6.0f);
    break;
  }
  case 5: // Drawbar Organ
  {
    ap.neutralBase();

    // Nine drawbars, with the key-off stage used as a release click: the upper
    // drawbars jump for two milliseconds as the contacts break.
    ap.oscTable(params::tuneSuffix,
                {0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f,
                 0.051f, 0.0504f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f,
                 0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f,
                 0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f, 0.051f,
                 0.051f, 0.051f, 0.051f, 0.051f});
    ap.oscTable(params::pmRateSuffix,
                {0.1924f, 0.6835f, 0.1908f, 2.0106f, 1.2654f, 0.2465f, 2.4615f,
                 0.2566f, 0.8773f, 2.3143f, 1.2027f, 0.1709f, 0.7525f, 0.4563f,
                 0.3095f, 1.271f, 0.8649f, 1.3264f, 0.5464f, 0.856f, 0.3618f,
                 0.1528f, 0.2403f, 0.2119f, 0.9838f, 1.6601f, 0.6853f, 1.4073f,
                 0.8813f, 0.1804f, 0.1609f, 0.5087f});
    ap.oscTable(params::pmDepthSuffix,
                {1.2698f, 1.0871f, 1.4779f, 1.303f, 1.1223f, 1.5308f, 1.6078f,
                 1.2847f, 1.0221f, 1.2079f, 1.3563f, 0.9006f, 1.0719f, 0.9118f,
                 1.0206f, 1.0137f, 1.0572f, 1.2578f, 1.3106f, 1.2526f, 1.2849f,
                 1.1053f, 0.9204f, 1.0831f, 1.0635f, 1.2268f, 1.0749f, 1.0997f,
                 1.2473f, 1.3392f, 1.3917f, 1.3788f});
    ap.allOsc(params::driftSuffix, [](int) { return 1.053; });
    ap.oscTable(params::delaySuffix,
                {0.0f, 0.0008f, 0.0027f, 0.0017f, 0.0035f, 0.0054f, 0.0f,
                 0.0045f, 0.0f, 0.0067f, 0.0f, 0.0083f, 0.0f, 0.0f, 0.0f,
                 0.0076f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    ap.oscTable(params::attackSuffix,
                {0.0005f, 0.0004f, 0.0003f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f});
    ap.allOsc(params::swellSuffix, [](int) { return 0.002; });
    ap.oscTable(params::offLevelSuffix,
                {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.55f,
                 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f,
                 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f,
                 0.55f, 0.55f, 0.55f, 0.55f});
    ap.allOsc(params::releaseSuffix, [](int) { return 0.0072; });
    ap.oscTable(params::amRateSuffix,
                {0.2714f, 0.2149f, 0.0408f, 0.4443f, 0.8287f, 0.3727f, 0.0204f,
                 0.0322f, 0.0369f, 0.0464f, 0.0522f, 0.0198f, 0.3181f, 0.2727f,
                 0.0732f, 0.7501f, 0.5429f, 0.0418f, 0.0741f, 0.038f, 0.471f,
                 0.2192f, 0.0653f, 0.031f, 0.1977f, 0.0185f, 0.6338f, 0.7105f,
                 0.0366f, 0.2238f, 0.7394f, 0.6442f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.0563; });
    ap.allOsc(params::velSuffix, [](int) { return -0.0001; });
    ap.oscTable(params::volumeSuffix,
                {0.7117f, 0.708f, 0.4097f, 0.5913f, 0.0f, 0.1717f, 0.0f,
                 0.2271f, 0.0f, 0.0892f, 0.0f, 0.0492f, 0.0f, 0.0f, 0.0f,
                 0.109f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    ap.set("stretch", -0.2168f);
    ap.set("track", 3.2f);
    ap.set("wobble", 0.0211f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.1203f);
    ap.set("echoTime", 0.0423f);
    ap.set("echoFeedback", 0.2253f);
    ap.set("echoAge", 0.5121f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.138f);
    ap.set("reverbDecay", 0.3142f);
    ap.set("reverbDamp", 0.2588f);
    ap.set("reverbPreDelay", 0.0031f);
    ap.set("noise_colour", 0.0563f);
    ap.set("noise_attack", 0.0007f);
    ap.set("noise_decay", 0.0154f);
    ap.set("noise_sustain", 0.0456f);
    ap.set("noise_offLevel", 0.3948f);
    ap.set("noise_release", 0.0029f);
    ap.set("noise_volume", 0.5171f);
    break;
  }
  case 6: // EP Chimes
  {
    ap.neutralBase();

    // A round electric piano with bright chimes above it, and the converter
    // at 16 kHz and 8 bits left on hard enough that the artefacts belong to
    // the sound rather than sitting on top of it.

    ap.oscTable(params::pmRateSuffix,
                {2.0212f, 2.0445f, 2.0681f, 2.092f, 2.1162f, 2.1406f, 2.1653f,
                 2.1406f, 2.1162f, 2.092f, 2.0681f, 2.0445f, 2.0212f, 1.9981f,
                 1.9753f, 1.9527f, 1.9305f, 1.9084f, 1.8866f, 1.8651f, 1.8438f,
                 1.8228f, 1.802f, 1.7814f, 1.7611f, 1.7409f, 1.7211f, 1.7014f,
                 1.682f, 1.6628f, 1.6438f, 1.6251f});
    ap.oscTable(params::pmDepthSuffix,
                {0.0602f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f,
                 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f,
                 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f,
                 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f, 2.0839f,
                 2.0839f, 2.0839f, 2.0839f, 2.0839f});
    ap.oscTable(params::driftSuffix,
                {0.2939f, 1.2828f, 1.9086f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f,
                 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f,
                 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f,
                 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f});
    ap.oscTable(params::attackSuffix,
                {0.0007f, 0.0005f, 0.0003f, 0.2243f, 0.0002f, 1.3673f, 0.0002f,
                 0.2243f, 0.0002f, 0.0002f, 0.0002f, 1.3673f, 0.0002f, 0.0002f,
                 0.0002f, 0.2243f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 1.3673f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.2243f});
    ap.oscTable(params::decaySuffix,
                {10.7627f, 4.8774f, 2.2301f, 0.8181f, 0.2377f, 1.5354f, 0.2377f,
                 0.8181f, 0.2377f, 0.2377f, 0.2377f, 1.5354f, 0.2377f, 0.2377f,
                 0.2377f, 0.8181f, 0.2377f, 0.2377f, 0.2377f, 0.2377f, 0.2377f,
                 0.2377f, 0.2377f, 1.5354f, 0.2377f, 0.2377f, 0.2377f, 0.2377f,
                 0.2377f, 0.2377f, 0.2377f, 0.8181f});
    ap.oscTable(params::sustainSuffix,
                {0.2128f, 0.2073f, 0.1966f, 0.1962f, 0.1023f, 0.1799f, 0.0994f,
                 0.1739f, 0.1014f, 0.0745f, 0.0754f, 0.1466f, 0.0766f, 0.0605f,
                 0.0731f, 0.1295f, 0.0834f, 0.0514f, 0.0722f, 0.0189f, 0.0611f,
                 0.0143f, 0.0087f, 0.0799f, 0.0099f, 0.0043f, 0.0278f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0425f});
    ap.oscTable(params::offLevelSuffix,
                {0.675f, 0.558f, 0.4963f, 0.0735f, 0.0735f, 0.0735f, 0.0735f,
                 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f,
                 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f,
                 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f, 0.0735f,
                 0.0735f, 0.0735f, 0.0735f, 0.0735f});
    ap.oscTable(params::releaseSuffix,
                {0.0526f, 0.0306f, 0.0288f, 0.1675f, 0.1675f, 0.1675f, 0.1675f,
                 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f,
                 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f,
                 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f, 0.1675f,
                 0.1675f, 0.1675f, 0.1675f, 0.1675f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 1.0; });
    ap.oscTable(params::velSuffix,
                {-0.0103f, 0.2514f, 0.4978f, 0.2438f, 0.2763f, 0.3087f, 0.3412f,
                 0.3737f, 0.4062f, 0.4387f, 0.4712f, 0.5037f, 0.5362f, 0.5687f,
                 0.6012f, 0.6337f, 0.6661f, 0.6986f, 0.7311f, 0.7636f, 0.7961f,
                 0.8286f, 0.8611f, 0.8936f, 0.9261f, 0.9586f, 0.9911f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.8316f, 0.8701f, 0.0001f, 0.0001f, 0.0003f, 0.0005f,
                 0.0009f, 0.0016f, 0.0027f, 0.0046f, 0.0075f, 0.0121f, 0.0191f,
                 0.0293f, 0.0438f, 0.064f, 0.0912f, 0.1268f, 0.1721f, 0.228f,
                 0.2946f, 0.3717f, 0.4575f, 0.5495f, 0.6441f, 0.7368f, 0.8224f,
                 0.8959f, 0.9523f, 0.9879f, 1.0f});
    ap.set("h02_delay", 0.0096f);
    ap.set("h02_pan", -0.2194f);
    ap.set("h03_delay", 0.0048f);
    ap.set("h03_pan", 0.3014f);
    ap.set("h03_swell", 0.0032f);
    ap.set("h04_amRate", 4.9055f);
    ap.set("h04_delay", 0.0265f);
    ap.set("h04_pan", -1.0f);
    ap.set("h05_amRate", 5.1774f);
    ap.set("h05_delay", 0.0852f);
    ap.set("h05_pan", 1.0f);
    ap.set("h06_amRate", 4.2217f);
    ap.set("h06_delay", 0.3104f);
    ap.set("h06_pan", -1.0f);
    ap.set("h07_amRate", 2.514f);
    ap.set("h07_delay", 0.0358f);
    ap.set("h07_pan", 1.0f);
    ap.set("h08_amRate", 5.3881f);
    ap.set("h08_delay", 0.0265f);
    ap.set("h08_pan", -1.0f);
    ap.set("h09_amRate", 2.8136f);
    ap.set("h09_delay", 0.0745f);
    ap.set("h09_pan", 1.0f);
    ap.set("h10_amRate", 6.7805f);
    ap.set("h10_delay", 0.0265f);
    ap.set("h10_pan", -1.0f);
    ap.set("h11_amRate", 2.8268f);
    ap.set("h11_delay", 0.3872f);
    ap.set("h11_pan", 1.0f);
    ap.set("h12_amRate", 5.3252f);
    ap.set("h12_delay", 0.1107f);
    ap.set("h12_pan", -1.0f);
    ap.set("h13_amRate", 3.0543f);
    ap.set("h13_delay", 0.1749f);
    ap.set("h13_pan", 1.0f);
    ap.set("h14_amRate", 5.8628f);
    ap.set("h14_delay", 0.1333f);
    ap.set("h14_pan", -1.0f);
    ap.set("h15_amRate", 1.1757f);
    ap.set("h15_delay", 0.1339f);
    ap.set("h15_pan", 1.0f);
    ap.set("h16_amRate", 5.214f);
    ap.set("h16_delay", 0.0265f);
    ap.set("h16_pan", -1.0f);
    ap.set("h17_amRate", 5.503f);
    ap.set("h17_delay", 0.0265f);
    ap.set("h17_pan", 1.0f);
    ap.set("h18_amRate", 0.7654f);
    ap.set("h18_delay", 0.0265f);
    ap.set("h18_pan", -1.0f);
    ap.set("h19_amRate", 1.5005f);
    ap.set("h19_delay", 0.1248f);
    ap.set("h19_pan", 1.0f);
    ap.set("h20_amRate", 6.5769f);
    ap.set("h20_delay", 0.0265f);
    ap.set("h20_pan", -1.0f);
    ap.set("h21_amRate", 1.8018f);
    ap.set("h21_delay", 0.0453f);
    ap.set("h21_pan", 1.0f);
    ap.set("h22_amRate", 5.1171f);
    ap.set("h22_delay", 0.084f);
    ap.set("h22_pan", -1.0f);
    ap.set("h23_amRate", 1.4487f);
    ap.set("h23_delay", 0.09f);
    ap.set("h23_pan", 1.0f);
    ap.set("h24_amRate", 5.1291f);
    ap.set("h24_delay", 0.0265f);
    ap.set("h24_pan", -0.7346f);
    ap.set("h25_amRate", 2.514f);
    ap.set("h25_delay", 0.0265f);
    ap.set("h25_pan", 1.0f);
    ap.set("h26_amRate", 5.1291f);
    ap.set("h26_delay", 0.1138f);
    ap.set("h26_pan", -1.0f);
    ap.set("h27_amRate", 4.3219f);
    ap.set("h27_delay", 0.087f);
    ap.set("h27_pan", 1.0f);
    ap.set("h28_amRate", 3.254f);
    ap.set("h28_delay", 0.0265f);
    ap.set("h28_pan", -1.0f);
    ap.set("h29_amRate", 3.6503f);
    ap.set("h29_delay", 0.1107f);
    ap.set("h29_pan", 1.0f);
    ap.set("h30_amRate", 4.1725f);
    ap.set("h30_delay", 0.1772f);
    ap.set("h30_pan", -1.0f);
    ap.set("h31_amRate", 3.7545f);
    ap.set("h31_delay", 0.0573f);
    ap.set("h31_pan", 1.0f);
    ap.set("h32_amRate", 6.504f);
    ap.set("h32_delay", 0.0333f);
    ap.set("h32_pan", 0.8254f);
    ap.set("echoAge", 0.5331f);
    ap.set("echoFeedback", 0.7306f);
    ap.set("echoMix", 0.2036f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.1111f);
    ap.set("lofiBits", 5.0f);
    ap.set("lofiRate", 3.0f);
    ap.set("noise_amDepth", 0.2146f);
    ap.set("noise_attack", 0.4561f);
    ap.set("noise_colour", 0.0f);
    ap.set("noise_decay", 4.5598f);
    ap.set("noise_release", 1.9861f);
    ap.set("noise_sustain", 0.2844f);
    ap.set("noise_volume", 0.2953f);
    ap.set("reverbDamp", 0.0595f);
    ap.set("reverbDecay", 2.5658f);
    ap.set("reverbMix", 0.2589f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0235f);
    ap.set("stretch", -1200.0f);
    ap.set("track", 1.4f);
    ap.set("wobble", 0.1275f);
    break;
  }
  case 7: // Equal Saw
  {
    ap.neutralBase();

    // The other half of a pair to be A/B'd: the same sound as Just Saw
    // but with every partial pulled back to equal temperament.
    ap.allOsc(params::tuneSuffix, [](int) { return 0.0; });
    ap.allOsc(params::attackSuffix, [](int) { return 0.0002; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.0; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.001; });
    ap.allOsc(params::velSuffix, [](int) { return 0.0009; });
    ap.allOsc(params::volumeSuffix, [](int n) { return 1.0 / n; });
    ap.set("stretch", -0.0515f);
    ap.set("track", 1.0f);
    break;
  }
  case 8: // FM Piano
  {
    ap.neutralBase();

    // The electric piano an FM synthesiser makes rather than the instrument
    // it is imitating, in the manner of a DX7: a hard, metallic attack over a
    // body that is very nearly a sine, and none of the mechanical noise a
    // real one would bring with it.

    ap.oscTable(params::pmRateSuffix,
                {1.2435f, 1.0877f, 1.2055f, 1.183f, 1.7273f, 1.2281f, 1.2413f,
                 1.2522f, 1.1691f, 1.2177f, 1.1633f, 1.2417f, 1.1634f, 1.2007f,
                 1.1684f, 1.2354f, 1.2022f, 1.2484f, 1.2646f, 1.1916f, 1.1955f,
                 1.3187f, 1.1851f, 1.2198f, 1.2068f, 1.1997f, 1.2944f, 1.2163f,
                 1.1961f, 1.2879f, 1.1693f, 1.1692f});
    ap.allOsc(params::pmDepthSuffix, [](int) { return 4.12; });
    ap.allOsc(params::driftSuffix, [](int) { return 0.9608; });
    ap.oscTable(params::delaySuffix,
                {0.0028f, 0.0027f, 0.0026f, 0.0026f, 0.0025f, 0.0024f, 0.0024f,
                 0.0023f, 0.0022f, 0.0022f, 0.0021f, 0.002f, 0.002f, 0.0019f,
                 0.0018f, 0.0018f, 0.0017f, 0.0016f, 0.0016f, 0.0015f, 0.0014f,
                 0.0014f, 0.0013f, 0.0013f, 0.0012f, 0.0011f, 0.0011f, 0.001f,
                 0.0009f, 0.0009f, 0.0008f, 0.0008f});
    ap.oscTable(params::attackSuffix,
                {0.0006f, 0.0051f, 0.0003f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f});
    ap.oscTable(params::decaySuffix,
                {6.8352f, 10.5209f, 7.0829f, 5.8589f, 4.241f, 2.7255f, 2.146f,
                 1.5489f, 1.2446f, 1.163f, 1.0679f, 0.9978f, 1.1357f, 0.8317f,
                 0.9887f, 0.7325f, 0.717f, 0.668f, 0.637f, 0.5424f, 0.5708f,
                 0.5475f, 0.5057f, 0.4446f, 0.4402f, 0.491f, 0.5167f, 0.3895f,
                 0.4357f, 0.2858f, 0.2905f, 0.3193f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::offLevelSuffix,
                {0.1092f, 0.1152f, 0.1211f, 0.1271f, 0.1331f, 0.139f, 0.145f,
                 0.151f, 0.1569f, 0.1629f, 0.1688f, 0.1748f, 0.1808f, 0.1702f,
                 0.1597f, 0.1492f, 0.1386f, 0.1281f, 0.1175f, 0.107f, 0.0965f,
                 0.0859f, 0.0754f, 0.0649f, 0.0543f, 0.0438f, 0.0332f, 0.0227f,
                 0.0122f, 0.0016f, 0.0006f, 0.0006f});
    ap.oscTable(params::releaseSuffix,
                {0.0446f, 0.0408f, 0.0373f, 0.0342f, 0.0313f, 0.0286f, 0.0262f,
                 0.024f, 0.022f, 0.0201f, 0.0184f, 0.0169f, 0.0154f, 0.0141f,
                 0.0129f, 0.0118f, 0.0108f, 0.0099f, 0.0091f, 0.0083f, 0.0076f,
                 0.007f, 0.0064f, 0.0058f, 0.0053f, 0.0049f, 0.0045f, 0.0041f,
                 0.0038f, 0.0034f, 0.0031f, 0.0029f});
    ap.oscTable(params::amRateSuffix,
                {2.1275f, 2.1575f, 5.6868f, 2.254f, 1.7752f, 1.996f, 1.8788f,
                 1.6722f, 2.0979f, 2.2037f, 1.6736f, 1.9153f, 2.6665f, 1.9615f,
                 2.1298f, 1.826f, 2.0965f, 2.045f, 2.2632f, 1.593f, 2.2169f,
                 1.9549f, 2.1513f, 1.7142f, 1.6222f, 1.8771f, 1.9052f, 2.006f,
                 1.8367f, 1.9262f, 2.1904f, 1.8642f});
    ap.oscTable(params::amDepthSuffix,
                {0.066f, 0.0902f, 0.1147f, 0.1392f, 0.1637f, 0.1882f, 0.2126f,
                 0.2371f, 0.2616f, 0.2861f, 0.3106f, 0.3351f, 0.3596f, 0.384f,
                 0.4085f, 0.433f, 0.4575f, 0.482f, 0.5065f, 0.531f, 0.5555f,
                 0.5799f, 0.6044f, 0.6289f, 0.6534f, 0.6779f, 0.7024f, 0.7269f,
                 0.7513f, 0.7758f, 0.8003f, 0.8244f});
    ap.oscTable(params::velSuffix,
                {-0.0154f, 0.9429f, 0.946f, 0.962f, 0.9768f, 0.9893f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.5898f, 0.3532f, 0.394f, 0.3633f, 0.4792f, 0.2375f,
                 0.3199f, 0.2007f, 0.1309f, 0.0884f, 0.0774f, 0.0835f, 0.0472f,
                 0.1112f, 0.0361f, 0.0365f, 0.0324f, 0.0274f, 0.0324f, 0.0203f,
                 0.0169f, 0.0167f, 0.0131f, 0.0116f, 0.0092f, 0.0125f, 0.0124f,
                 0.0179f, 0.0124f, 0.0123f, 0.0099f});
    ap.oscTable(params::panSuffix,
                {-0.0014f, 0.2998f, -0.4736f, 0.618f, -0.7979f, -0.833f,
                 0.8219f, 0.8318f, -0.8703f, -0.9529f, 0.898f, 1.0f, -1.0f,
                 -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f,
                 1.0f, 1.0f, -1.0f, -0.9084f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
                 -0.0014f});
    ap.set("h03_aftertouch", -0.0035f);
    ap.set("echoAge", 0.5969f);
    ap.set("echoFeedback", 0.4577f);
    ap.set("echoMix", 0.2131f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.1318f);
    ap.set("lofiBits", 2.0f);
    ap.set("noise_attack", 0.0009f);
    ap.set("noise_colour", 0.0311f);
    ap.set("noise_decay", 0.0348f);
    ap.set("noise_offLevel", 0.6442f);
    ap.set("noise_release", 0.0182f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_volume", 0.1653f);
    ap.set("reverbDamp", 0.5012f);
    ap.set("reverbDecay", 1.6182f);
    ap.set("reverbMix", 0.1683f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0142f);
    ap.set("stretch", -0.3302f);
    ap.set("track", 2.1f);
    ap.set("wobble", 0.0823f);
    break;
  }
  case 9: // Glass Armonica
  {
    ap.neutralBase();

    // Wet fingers on glass. Each partial arrives later and vibrates faster than
    // the one below it.
    ap.oscTable(params::pmRateSuffix,
                {4.8f, 5.1f, 5.4f, 5.7f, 6.0f, 6.3f, 6.6f, 6.9f, 7.2f, 7.5f,
                 7.8f, 8.1f, 8.4f, 8.7f, 9.0f, 9.3f, 9.6f, 9.9f, 10.2f, 10.5f,
                 10.8f, 11.1f, 11.4f, 11.7f, 12.0f, 12.3f, 12.6f, 12.9f, 13.2f,
                 13.5f, 13.8f, 14.1f});
    ap.allOsc(params::pmDepthSuffix, [](int) { return 3.0; });
    ap.allOsc(params::driftSuffix, [](int) { return 9.0; });
    ap.oscTable(params::attackSuffix,
                {0.59f, 0.68f, 0.77f, 0.86f, 0.95f, 1.04f, 1.13f, 1.22f, 1.31f,
                 1.4f, 1.49f, 1.58f, 1.67f, 1.76f, 1.85f, 1.94f, 2.03f, 2.12f,
                 2.21f, 2.3f, 2.39f, 2.48f, 2.57f, 2.66f, 2.75f, 2.84f, 2.93f,
                 3.02f, 3.11f, 3.2f, 3.29f, 3.38f});
    ap.allOsc(params::decaySuffix, [](int) { return 6.0; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.75; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.4; });
    ap.allOsc(params::offLevelSuffix, [](int) { return 0.85; });
    ap.allOsc(params::releaseSuffix, [](int) { return 2.5; });
    ap.oscTable(params::volumeSuffix,
                {0.9f, 0.0f, 0.2317f, 0.0f, 0.1621f, 0.0f, 0.1281f, 0.0f,
                 0.1074f, 0.0f, 0.0933f, 0.0f, 0.083f, 0.0f, 0.0751f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    ap.oscTable(params::panSuffix,
                {0.0f, 0.0f, 0.2324f, -0.2324f, -0.3286f, 0.3286f, 0.4025f,
                 -0.4025f, -0.4648f, 0.4648f, 0.5196f, -0.5196f, -0.5692f,
                 0.5692f, 0.6148f, -0.6148f, -0.6573f, 0.6573f, 0.6971f,
                 -0.6971f, -0.7348f, 0.7348f, 0.7707f, -0.7707f, -0.805f,
                 0.805f, 0.8379f, -0.8379f, -0.8695f, 0.8695f, 0.9f, -0.9f});
    ap.set("stretch", 180.0f);
    ap.set("track", 4.0f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.2385f);
    ap.set("echoAge", 0.2727f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.45f);
    ap.set("reverbDecay", 6.0f);
    ap.set("reverbDamp", 0.25f);
    break;
  }
  case 10: // Init
  {
    ap.neutralBase();

    // The neutral starting point: the first three partials and nothing else.
    ap.allOsc(params::attackSuffix, [](int) { return 0.0008; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.008; });
    ap.oscTable(params::volumeSuffix,
                {0.7072f, 0.5005f, 0.2509f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f});
    break;
  }
  case 11: // Just Saw
  {
    ap.neutralBase();

    // The other half of a pair to be A/B'd: the same sound as Equal Saw
    // but with every partial at its just interval.
    ap.allOsc(params::attackSuffix, [](int) { return 0.0002; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.0; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.001; });
    ap.allOsc(params::velSuffix, [](int) { return 0.0009; });
    ap.allOsc(params::volumeSuffix, [](int n) { return 1.0 / n; });
    ap.set("stretch", -0.0515f);
    ap.set("track", 1.0f);
    break;
  }
  case 12: // Lo-fi
  {
    ap.neutralBase();

    // 8 kHz and 8 bits, soft, with very short partials arriving at different
    // times. The territory of a Roland D-50, a Korg Wavestation or a granular
    // synthesiser.
    ap.oscTable(params::pmRateSuffix,
                {3.8834f, 21.2852f, 0.0972f, 4.9283f, 17.7814f, 2.8067f,
                 0.1676f, 0.9272f, 1.6633f, 3.7316f, 0.1116f, 14.545f, 6.5233f,
                 4.7824f, 1.2715f, 0.4711f, 0.0608f, 0.1101f, 0.1461f, 2.0788f,
                 0.043f, 0.6334f, 0.0661f, 0.1739f, 1.3854f, 0.0197f, 4.2228f,
                 0.0672f, 0.3472f, 0.0387f, 0.1428f, 1.2742f});
    ap.allOsc(params::pmDepthSuffix, [](int) { return 2.289; });
    ap.allOsc(params::driftSuffix, [](int) { return 1.924; });
    ap.oscTable(params::delaySuffix,
                {0.0f, 0.0039f, 0.0056f, 0.2881f, 0.5021f, 0.9843f, 0.1565f,
                 2.3765f, 2.6168f, 0.1476f, 0.1542f, 1.4611f, 0.3912f, 0.177f,
                 0.186f, 1.0756f, 1.3428f, 0.2332f, 0.4968f, 0.3882f, 0.5911f,
                 2.008f, 1.0308f, 0.7547f, 0.3369f, 2.8682f, 1.6032f, 0.4201f,
                 0.4545f, 2.0399f, 3.0282f, 0.5853f});
    ap.oscTable(params::attackSuffix,
                {0.0003f, 0.0002f, 0.0002f, 0.0018f, 0.0006f, 0.0005f, 0.0004f,
                 0.0004f, 0.0011f, 0.0003f, 0.0005f, 0.0022f, 0.0011f, 0.0017f,
                 0.0003f, 0.0019f, 0.0012f, 0.0007f, 0.0008f, 0.0003f, 0.0004f,
                 0.0021f, 0.0003f, 0.002f, 0.0013f, 0.0004f, 0.0008f, 0.0003f,
                 0.0004f, 0.0006f, 0.0004f, 0.0007f});
    ap.oscTable(params::decaySuffix,
                {3.1228f, 1.5068f, 0.5113f, 0.2802f, 0.2479f, 0.2147f, 0.3166f,
                 0.224f, 0.2823f, 0.2899f, 0.2739f, 0.3249f, 0.2454f, 0.2596f,
                 0.2843f, 0.3139f, 0.206f, 0.2131f, 0.2841f, 0.2468f, 0.27f,
                 0.2899f, 0.2586f, 0.242f, 0.2999f, 0.2208f, 0.2639f, 0.2074f,
                 0.2711f, 0.2482f, 0.2425f, 0.2892f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.0035; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.008; });
    ap.oscTable(params::amRateSuffix,
                {0.2388f, 0.4069f, 4.9033f, 0.4017f, 0.2053f, 3.028f, 6.2469f,
                 2.5264f, 0.3047f, 0.9233f, 1.2613f, 0.3841f, 0.6482f, 0.8451f,
                 3.0387f, 0.3679f, 0.1329f, 0.1558f, 0.2956f, 0.2195f, 2.5854f,
                 2.3155f, 1.3273f, 0.5301f, 1.9995f, 2.1026f, 0.2337f, 3.4038f,
                 0.896f, 0.7067f, 4.0197f, 0.16f});
    ap.oscTable(params::amDepthSuffix,
                {0.0f, 0.0f, 0.0307f, 0.4538f, 0.4538f, 0.4538f, 0.4538f,
                 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f,
                 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f,
                 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f, 0.4538f,
                 0.4538f, 0.4538f, 0.4538f, 0.4538f});
    ap.oscTable(params::velSuffix,
                {0.1802f, 0.5089f, 0.8207f, 0.4095f, 0.4241f, 0.4393f, 0.455f,
                 0.4713f, 0.4882f, 0.5057f, 0.5238f, 0.5425f, 0.562f, 0.5821f,
                 0.603f, 0.6246f, 0.647f, 0.6702f, 0.6943f, 0.7192f, 0.745f,
                 0.7718f, 0.7995f, 0.8282f, 0.8579f, 0.8888f, 0.9207f, 0.9538f,
                 0.9881f, 1.0f, 1.0f, 1.0f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.7056f, 0.3536f, 0.1045f, 0.0426f, 0.0719f, 0.0443f,
                 0.1099f, 0.0462f, 0.0472f, 0.0482f, 0.0794f, 0.0505f, 0.0517f,
                 0.0529f, 0.1238f, 0.0557f, 0.0571f, 0.0587f, 0.0603f, 0.062f,
                 0.0638f, 0.0656f, 0.1023f, 0.0697f, 0.0719f, 0.0742f, 0.0766f,
                 0.0791f, 0.0818f, 0.0846f, 0.172f});
    ap.oscTable(params::panSuffix,
                {0.0f, -0.3032f, 0.4974f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f,
                 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f,
                 -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f,
                 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f});
    ap.set("track", 4.0f);
    ap.set("wobble", 0.286f);
    ap.set("lofiRate", 5.0f);
    ap.set("lofiBits", 4.0f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.2973f);
    ap.set("echoTime", 0.1252f);
    ap.set("echoFeedback", 0.95f);
    ap.set("echoAge", 0.4795f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.2487f);
    ap.set("reverbDecay", 4.7067f);
    ap.set("reverbDamp", 0.1636f);
    ap.set("reverbPreDelay", 0.0098f);
    ap.set("noise_delay", 0.0008f);
    ap.set("noise_attack", 0.0036f);
    ap.set("noise_decay", 0.0582f);
    ap.set("noise_sustain", 0.045f);
    ap.set("noise_amDepth", 0.7308f);
    ap.set("noise_volume", 0.0635f);
    break;
  }
  case 13: // Metallic Piano
  {
    ap.neutralBase();

    // A thin, metallic upright piano.
    ap.allOsc(params::tuneSuffix, [](int) { return 0.9749; });
    ap.allOsc(params::driftSuffix, [](int) { return 2.4495; });
    ap.oscTable(params::attackSuffix,
                {0.0012f, 0.0011f, 0.0011f, 0.001f, 0.001f, 0.0009f, 0.0009f,
                 0.0008f, 0.0008f, 0.0008f, 0.0007f, 0.0007f, 0.0007f, 0.0006f,
                 0.0006f, 0.0006f, 0.0005f, 0.0005f, 0.0005f, 0.0005f, 0.0004f,
                 0.0004f, 0.0004f, 0.0004f, 0.0004f, 0.0003f, 0.0003f, 0.0003f,
                 0.0003f, 0.0003f, 0.0003f, 0.0003f});
    ap.oscTable(params::decaySuffix,
                {15.1891f, 3.3865f, 3.5589f, 1.343f, 2.414f, 2.5004f, 2.286f,
                 1.4501f, 2.146f, 1.4637f, 1.1309f, 0.9986f, 0.8418f, 0.7158f,
                 0.7392f, 0.7373f, 0.7102f, 0.6289f, 0.5348f, 0.6554f, 0.6151f,
                 0.6261f, 0.5308f, 0.4385f, 0.7859f, 0.4069f, 0.6861f, 0.1577f,
                 0.5653f, 0.2591f, 0.5891f, 0.2654f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::swellSuffix,
                {0.2175f, 0.1886f, 0.1634f, 0.1416f, 0.1227f, 0.1063f, 0.092f,
                 0.0796f, 0.0688f, 0.0595f, 0.0514f, 0.0443f, 0.0382f, 0.0329f,
                 0.0283f, 0.0243f, 0.0208f, 0.0178f, 0.0152f, 0.0129f, 0.0109f,
                 0.0092f, 0.0077f, 0.0064f, 0.0053f, 0.0043f, 0.0035f, 0.0028f,
                 0.0021f, 0.0016f, 0.0011f, 0.0007f});
    ap.oscTable(params::offLevelSuffix,
                {0.0228f, 0.0222f, 0.0216f, 0.0209f, 0.0203f, 0.0196f, 0.019f,
                 0.0183f, 0.0177f, 0.0171f, 0.0164f, 0.0158f, 0.0151f, 0.0145f,
                 0.0138f, 0.0132f, 0.0126f, 0.0119f, 0.0113f, 0.0106f, 0.01f,
                 0.0093f, 0.0087f, 0.008f, 0.0074f, 0.0068f, 0.0061f, 0.0055f,
                 0.0048f, 0.0042f, 0.0035f, 0.0029f});
    ap.oscTable(params::releaseSuffix,
                {1.5044f, 1.1373f, 0.8582f, 0.6476f, 0.4887f, 0.3688f, 0.2783f,
                 0.21f, 0.1585f, 0.1196f, 0.0902f, 0.0681f, 0.0514f, 0.0388f,
                 0.0293f, 0.0221f, 0.0167f, 0.0126f, 0.0095f, 0.0072f, 0.0054f,
                 0.0041f, 0.0031f, 0.0023f, 0.0019f, 0.0018f, 0.0016f, 0.0015f,
                 0.0014f, 0.0013f, 0.0012f, 0.0011f});
    ap.oscTable(params::amRateSuffix,
                {4.2728f, 0.1641f, 1.4611f, 2.2503f, 4.0449f, 1.1424f, 8.2452f,
                 1.0735f, 1.8779f, 3.0976f, 3.4988f, 0.1528f, 0.1587f, 0.3564f,
                 0.101f, 0.6607f, 0.1287f, 1.0774f, 1.622f, 1.5115f, 0.1671f,
                 0.1553f, 0.1489f, 2.7681f, 1.0753f, 0.3813f, 3.6027f, 3.556f,
                 5.3563f, 7.6005f, 0.4627f, 0.1292f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.0165; });
    ap.oscTable(params::velSuffix,
                {-0.1614f, 0.5684f, 0.5848f, 0.7705f, 0.8185f, 0.8707f, 0.8878f,
                 0.9822f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.9505f, 0.3854f, 0.8446f, 0.5784f, 0.8864f, 0.4661f,
                 0.9818f, 1.0f, 0.2028f, 0.3564f, 0.4916f, 0.4501f, 0.5157f,
                 0.3276f, 0.6199f, 0.1477f, 0.1888f, 0.2845f, 0.1996f, 0.1741f,
                 0.1794f, 0.233f, 0.2092f, 0.0358f, 0.3446f, 0.0708f, 0.1753f,
                 0.0282f, 0.0448f, 0.0216f, 0.0446f});
    ap.oscTable(params::panSuffix,
                {0.0025f, -0.0034f, -0.4226f, 0.7182f, -0.4834f, -0.0918f,
                 -0.4774f, 0.347f, -0.1423f, -0.3079f, 0.1958f, 0.3844f,
                 -0.4706f, 0.7775f, 0.452f, -0.3878f, 0.3212f, 0.0989f, 0.6731f,
                 0.4332f, 0.5056f, 0.4062f, -0.3261f, -0.4446f, 0.5059f,
                 -0.3157f, -0.5079f, 0.7629f, 0.7901f, 0.2312f, -0.3993f,
                 0.0536f});
    ap.set("h05_delay", 0.004f);
    ap.set("h07_delay", 0.0005f);
    ap.set("h09_delay", 0.0067f);
    ap.set("h10_delay", 0.0049f);
    ap.set("h19_delay", 0.0004f);
    ap.set("h21_delay", 0.0013f);
    ap.set("h22_delay", 0.0019f);
    ap.set("h23_delay", 0.0021f);
    ap.set("h25_delay", 0.0036f);
    ap.set("h26_delay", 0.0024f);
    ap.set("h27_delay", 0.0009f);
    ap.set("h28_delay", 0.0029f);
    ap.set("h29_delay", 0.0036f);
    ap.set("h30_delay", 0.0039f);
    ap.set("h31_delay", 0.006f);
    ap.set("h32_delay", 0.0063f);
    ap.set("echoAge", 0.595f);
    ap.set("echoFeedback", 0.2911f);
    ap.set("echoMix", 0.1588f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.0935f);
    ap.set("lofiBits", 4.0f);
    ap.set("noise_attack", 0.0002f);
    ap.set("noise_colour", 0.0f);
    ap.set("noise_decay", 2.7494f);
    ap.set("noise_offLevel", 1.0f);
    ap.set("noise_release", 0.5654f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_swell", 0.0018f);
    ap.set("noise_volume", 0.0769f);
    ap.set("reverbDamp", 0.2502f);
    ap.set("reverbDecay", 1.394f);
    ap.set("reverbMix", 0.2915f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0115f);
    ap.set("stretch", 23.901f);
    ap.set("track", 9.0f);
    break;
  }
  case 14: // Music Box
  {
    ap.neutralBase();

    // Plucked steel teeth: irregular vibrato per partial, with one bent far
    // further than the rest.
    ap.oscTable(params::pmRateSuffix,
                {0.7705f, 1.3255f, 2.0219f, 4.0f, 4.0f, 4.0f, 1.6645f, 4.0f,
                 4.0f, 4.0f, 1.4444f, 4.0f, 1.209f, 1.7045f, 1.3253f, 4.0f,
                 4.0f, 4.0f, 0.7486f, 4.0f, 1.0603f, 0.4441f, 4.0f, 4.0f,
                 0.5363f, 4.0f, 1.016f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f});
    ap.oscTable(params::pmDepthSuffix,
                {11.3437f, 12.4657f, 7.8628f, 0.0f, 13.642f, 7.7758f,
                 176.3964f, 0.0f, 0.0f, 0.0f, 26.4225f, 0.0f, 14.5537f,
                 25.4971f, 29.8053f, 0.0f, 0.0f, 0.0f, 25.9064f, 0.0f, 5.4393f,
                 6.0212f, 0.0f, 0.0f, 8.6211f, 0.0f, 15.5094f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f});
    ap.oscTable(params::driftSuffix,
                {10.0247f, 7.6098f, 8.3018f, 1.048f, 4.4581f, 8.0801f,
                 15.8077f, 1.048f, 1.0794f, 1.048f, 15.2791f, 1.048f, 7.5406f,
                 6.3176f, 17.873f, 1.048f, 1.048f, 1.048f, 11.4169f, 1.048f,
                 9.786f, 5.8201f, 1.048f, 1.048f, 6.0295f, 1.048f, 10.7948f,
                 1.048f, 10.3209f, 12.3294f, 1.048f, 1.113f});
    ap.oscTable(params::delaySuffix,
                {0.0011f, 0.008f, 0.0069f, 0.0018f, 0.0f, 0.0008f, 0.0019f,
                 0.0f, 0.0f, 0.0f, 0.0092f, 0.0f, 0.0505f, 0.0461f, 0.0036f,
                 0.0f, 0.0f, 0.0f, 0.0312f, 0.0f, 0.0012f, 0.0076f, 0.0f, 0.0f,
                 0.0008f, 0.0f, 0.0125f, 0.0f, 0.0011f, 0.0041f, 0.0f, 0.0f});
    ap.oscTable(params::attackSuffix,
                {0.0016f, 0.0008f, 0.0008f, 0.001f, 0.0005f, 0.0003f, 0.0003f,
                 0.0002f, 0.0002f, 0.0002f, 0.001f, 0.005f, 0.0003f, 0.0003f,
                 0.0002f, 0.0002f, 0.0002f, 0.8321f, 0.0009f, 0.0002f, 0.0004f,
                 0.0003f, 0.0002f, 0.0002f, 0.0002f, 0.6796f, 0.0009f, 0.0002f,
                 0.0005f, 0.0002f, 0.0002f, 0.0002f});
    ap.oscTable(params::decaySuffix,
                {0.0076f, 0.008f, 0.0044f, 1.7642f, 0.006f, 0.0065f, 0.0094f,
                 1.0534f, 1.8445f, 1.8801f, 0.011f, 0.6f, 0.0076f, 0.0075f,
                 0.0243f, 1.1764f, 0.8865f, 0.7204f, 0.0185f, 1.1526f, 0.0076f,
                 0.0044f, 1.1667f, 1.0814f, 0.0125f, 1.9489f, 0.0129f, 0.8775f,
                 0.0072f, 0.0059f, 0.6123f, 0.9353f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::swellSuffix,
                {0.0015f, 0.0015f, 0.0016f, 0.0015f, 0.0007f, 0.0015f, 0.0015f,
                 0.0015f, 0.0015f, 0.0008f, 0.0024f, 0.0015f, 0.0015f, 0.0015f,
                 0.0015f, 0.0015f, 0.0015f, 0.8327f, 0.0015f, 0.0015f, 0.0015f,
                 0.0015f, 0.0015f, 0.0015f, 0.0015f, 0.6246f, 0.0015f, 0.0015f,
                 0.0015f, 0.0015f, 0.0015f, 0.0015f});
    ap.oscTable(params::offLevelSuffix,
                {0.4781f, 0.4005f, 0.4883f, 0.0f, 0.1711f, 0.1122f, 0.192f,
                 0.0f, 0.0f, 0.0f, 0.1079f, 0.0f, 0.3f, 0.3f, 0.2231f, 0.0f,
                 0.0f, 0.3312f, 0.039f, 0.0f, 0.1643f, 0.1496f, 0.0f, 0.0f,
                 0.076f, 0.3927f, 0.0808f, 0.0f, 0.0399f, 0.0664f, 0.0f,
                 0.0f});
    ap.oscTable(params::releaseSuffix,
                {0.0131f, 0.0091f, 0.0121f, 1.7516f, 0.0136f, 0.0151f, 0.0104f,
                 1.0465f, 1.82f, 1.8719f, 0.0021f, 0.6022f, 0.0114f, 0.0128f,
                 0.0472f, 1.1909f, 0.889f, 0.7209f, 0.0171f, 1.1469f, 0.0727f,
                 0.0603f, 1.1667f, 1.0755f, 0.0333f, 2.6027f, 0.0311f, 0.8884f,
                 0.0136f, 0.0108f, 0.6253f, 0.936f});
    ap.oscTable(params::amRateSuffix,
                {0.3329f, 1.06f, 0.7471f, 0.8879f, 0.7664f, 0.4487f, 1.1793f,
                 0.5461f, 0.8566f, 1.0152f, 1.0159f, 0.292f, 0.9547f, 0.7176f,
                 0.5957f, 1.8877f, 1.3195f, 1.964f, 9.3864f, 1.3069f, 0.4234f,
                 0.263f, 0.4009f, 0.3566f, 1.4875f, 1.9641f, 1.0626f, 2.0752f,
                 3.4909f, 0.3071f, 0.276f, 0.8054f});
    ap.oscTable(params::amDepthSuffix,
                {0.6083f, 0.8139f, 0.7177f, 0.0171f, 0.6111f, 0.7783f, 0.7055f,
                 0.1728f, 0.1724f, 0.1715f, 0.7551f, 0.0f, 0.6632f, 0.5876f,
                 0.8705f, 0.0f, 0.0f, 0.0f, 0.8723f, 0.0f, 0.4229f, 0.4873f,
                 0.0f, 0.0f, 0.439f, 0.0f, 0.752f, 0.0f, 0.8888f, 0.9166f,
                 0.0f, 0.0f});
    ap.oscTable(params::velSuffix,
                {0.4105f, 0.4179f, 0.4256f, 0.4361f, 0.4418f, 0.4504f, 0.6963f,
                 0.4684f, 0.4779f, 0.4878f, 0.498f, 0.5085f, 0.5195f, 0.5308f,
                 0.5426f, 0.5547f, 0.5674f, 0.5804f, 0.594f, 0.608f, 0.6226f,
                 0.6376f, 0.6532f, 0.6694f, 0.6861f, 0.7035f, 0.7215f, 0.7401f,
                 0.5555f, 0.5773f, 0.8002f, 0.8216f});
    ap.oscTable(params::volumeSuffix,
                {0.0206f, 0.0246f, 0.0207f, 1.0f, 0.0179f, 0.0173f, 0.0174f,
                 0.0238f, 0.017f, 0.0048f, 0.0111f, 0.1709f, 0.0468f, 0.0452f,
                 0.0478f, 0.011f, 0.0059f, 0.0005f, 0.0086f, 0.0105f, 0.0239f,
                 0.024f, 0.0016f, 0.0073f, 0.036f, 0.0007f, 0.04f, 0.0104f,
                 0.0623f, 0.0601f, 0.004f, 0.0048f});
    ap.oscTable(params::panSuffix,
                {0.002f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f, -1.0f, -0.4313f,
                 0.7762f, -0.6829f, 1.0f, 0.3037f, 1.0f, -1.0f, -0.3229f,
                 0.1875f, -0.71f, 0.1436f, -0.3988f, 0.4998f, -1.0f, 1.0f,
                 0.85f, -0.5015f, -1.0f, -0.2024f, 1.0f, 0.493f, -1.0f, 1.0f,
                 -0.5536f, 0.0f});
    ap.set("stretch", -22.6682f);
    ap.set("track", 1.5f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.1226f);
    ap.set("echoTime", 0.0975f);
    ap.set("echoFeedback", 0.2528f);
    ap.set("echoAge", 0.7626f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.1006f);
    ap.set("reverbDecay", 0.9241f);
    ap.set("reverbDamp", 0.5062f);
    ap.set("reverbPreDelay", 0.0155f);
    ap.set("noise_colour", 1.0f);
    ap.set("noise_delay", 0.0001f);
    ap.set("noise_attack", 0.0029f);
    ap.set("noise_decay", 0.0143f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_offLevel", 0.1591f);
    ap.set("noise_release", 0.021f);
    ap.set("noise_amDepth", 0.4371f);
    ap.set("noise_volume", 0.0992f);
    break;
  }
  case 15: // Nylon EP
  {
    ap.neutralBase();

    // A very round electric piano, with almost nothing above the low
    // partials, as though the tines were nylon rather than steel.

    ap.oscTable(params::pmRateSuffix,
                {0.1924f, 0.6835f, 0.1908f, 2.0106f, 1.2654f, 0.2465f, 2.4615f,
                 0.2566f, 0.8773f, 2.3143f, 1.2027f, 0.1709f, 0.7525f, 0.4563f,
                 0.3095f, 1.271f, 0.8649f, 1.3264f, 0.5464f, 0.856f, 0.3618f,
                 0.1528f, 0.2403f, 0.2119f, 0.9838f, 1.6601f, 0.6853f, 1.4073f,
                 0.8813f, 0.1804f, 0.1609f, 0.5087f});
    ap.oscTable(params::pmDepthSuffix,
                {4.0594f, 4.0314f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.1706f,
                 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f,
                 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f,
                 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f, 4.0594f,
                 4.0594f, 4.0594f, 4.0594f, 4.0594f});
    ap.allOsc(params::driftSuffix, [](int) { return 1.9507; });
    ap.oscTable(params::attackSuffix,
                {0.0015f, 0.0006f, 0.0003f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f});
    ap.oscTable(params::decaySuffix,
                {0.0817f, 4.1684f, 1.6022f, 3.1345f, 1.5865f, 2.3494f, 1.5173f,
                 1.7918f, 1.4511f, 1.1786f, 1.3878f, 0.9389f, 1.2415f, 1.1624f,
                 1.0122f, 0.6223f, 0.6283f, 0.4857f, 0.546f, 0.9512f, 0.5598f,
                 0.4681f, 0.474f, 1.0384f, 0.3238f, 0.4524f, 0.507f, 0.4047f,
                 0.4219f, 0.2955f, 0.8883f, 0.5541f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::swellSuffix,
                {0.002f, 0.0058f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f,
                 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f,
                 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f,
                 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f, 0.002f,
                 0.002f});
    ap.oscTable(params::offLevelSuffix,
                {0.048f, 0.048f, 0.048f, 0.048f, 0.048f, 0.048f, 0.048f, 0.598f,
                 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f,
                 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f,
                 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f, 0.598f,
                 0.598f});
    ap.allOsc(params::releaseSuffix, [](int) { return 0.0072; });
    ap.oscTable(params::amRateSuffix,
                {1.0589f, 0.8387f, 0.1593f, 1.7338f, 3.2337f, 1.4543f, 0.0796f,
                 0.1256f, 0.1441f, 0.1811f, 0.2038f, 0.0773f, 1.2413f, 1.0639f,
                 0.2857f, 2.9269f, 2.1182f, 0.1632f, 0.2891f, 0.1482f, 1.8377f,
                 0.8554f, 0.2548f, 0.121f, 0.7715f, 0.0723f, 2.473f, 2.7724f,
                 0.1429f, 0.8732f, 2.8851f, 2.5137f});
    ap.oscTable(params::amDepthSuffix,
                {0.3447f, 0.3462f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f,
                 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f,
                 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f,
                 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f, 0.3447f,
                 0.3447f, 0.3447f, 0.3447f, 0.3447f});
    ap.oscTable(params::velSuffix,
                {-0.0187f, 0.0835f, 0.1861f, 0.2888f, 0.3914f, 0.494f, 0.5966f,
                 0.6992f, 0.8019f, 0.9045f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    ap.set("h01_volume", 0.096f);
    ap.set("h02_delay", 0.0008f);
    ap.set("h02_volume", 1.0f);
    ap.set("h03_delay", 0.0027f);
    ap.set("h03_pan", -0.252f);
    ap.set("h04_delay", 0.0017f);
    ap.set("h04_pan", 0.2496f);
    ap.set("h04_volume", 0.0663f);
    ap.set("h05_delay", 0.0035f);
    ap.set("h05_pan", 0.5004f);
    ap.set("h06_delay", 0.0054f);
    ap.set("h06_pan", -0.7501f);
    ap.set("h06_volume", 0.0452f);
    ap.set("h07_pan", -1.0f);
    ap.set("h08_delay", 0.0045f);
    ap.set("h08_pan", 0.509f);
    ap.set("h08_volume", 0.0184f);
    ap.set("h09_pan", 1.0f);
    ap.set("h10_delay", 0.0038f);
    ap.set("h10_pan", -1.0f);
    ap.set("h10_volume", 0.0125f);
    ap.set("h11_pan", -1.0f);
    ap.set("h12_delay", 0.0f);
    ap.set("h12_pan", 0.4429f);
    ap.set("h12_volume", 0.0093f);
    ap.set("h13_pan", 0.915f);
    ap.set("h13_volume", 0.002f);
    ap.set("h14_pan", -0.9854f);
    ap.set("h14_volume", 0.0016f);
    ap.set("h15_pan", -0.9801f);
    ap.set("h15_volume", 0.0011f);
    ap.set("h16_delay", 0.0076f);
    ap.set("h16_pan", 0.8506f);
    ap.set("h16_volume", 0.0071f);
    ap.set("h17_pan", 1.0f);
    ap.set("h17_volume", 0.0003f);
    ap.set("h18_pan", -1.0f);
    ap.set("h18_volume", 0.0021f);
    ap.set("h19_pan", -0.9966f);
    ap.set("h19_volume", 0.0007f);
    ap.set("h20_pan", 1.0f);
    ap.set("h20_volume", 0.0032f);
    ap.set("h21_pan", 1.0f);
    ap.set("h21_volume", 0.0008f);
    ap.set("h22_pan", -1.0f);
    ap.set("h22_volume", 0.0023f);
    ap.set("h23_pan", -1.0f);
    ap.set("h23_volume", 0.0006f);
    ap.set("h24_pan", 0.9449f);
    ap.set("h24_volume", 0.003f);
    ap.set("h25_pan", 0.9801f);
    ap.set("h25_volume", 0.0012f);
    ap.set("h26_pan", -1.0f);
    ap.set("h26_volume", 0.0053f);
    ap.set("h27_pan", -1.0f);
    ap.set("h27_volume", 0.0003f);
    ap.set("h28_pan", 1.0f);
    ap.set("h28_volume", 0.0014f);
    ap.set("h29_pan", 1.0f);
    ap.set("h29_volume", 0.0002f);
    ap.set("h30_pan", -1.0f);
    ap.set("h30_volume", 0.0006f);
    ap.set("h31_pan", -1.0f);
    ap.set("h31_volume", 0.0001f);
    ap.set("h32_volume", 0.0003f);
    ap.set("echoAge", 0.5121f);
    ap.set("echoFeedback", 0.2587f);
    ap.set("echoMix", 0.1569f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.0528f);
    ap.set("noise_aftertouch", 0.0f);
    ap.set("noise_amRate", 0.3122f);
    ap.set("noise_attack", 0.0005f);
    ap.set("noise_colour", 0.0322f);
    ap.set("noise_decay", 0.013f);
    ap.set("noise_offLevel", 0.3948f);
    ap.set("noise_release", 0.0029f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_vel", 0.4097f);
    ap.set("noise_volume", 0.1882f);
    ap.set("reverbDamp", 0.2438f);
    ap.set("reverbDecay", 0.4754f);
    ap.set("reverbMix", 0.2022f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0073f);
    ap.set("stretch", 7.3773f);
    ap.set("track", 3.2f);
    ap.set("wobble", 0.1946f);
    break;
  }
  case 16: // Odd Harmonics
  {
    ap.neutralBase();

    // Every even partial silent, which is the hollow tone of a stopped pipe.
    // Velocity opens the top of the series.
    ap.allOsc(params::attackSuffix, [](int) { return 0.0002; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.001; });
    ap.oscTable(params::velSuffix,
                {0.3f, 0.35f, 0.4f, 0.45f, 0.5f, 0.55f, 0.6f, 0.65f, 0.7f,
                 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                 1.0f, 1.0f, 1.0f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.0f, 0.3333f, 0.0f, 0.2f, 0.0f, 0.1429f, 0.0f, 0.1111f,
                 0.0f, 0.0909f, 0.0f, 0.0769f, 0.0f, 0.0667f, 0.0f, 0.0588f,
                 0.0f, 0.0526f, 0.0f, 0.0476f, 0.0f, 0.0435f, 0.0f, 0.04f,
                 0.0f, 0.037f, 0.0f, 0.0345f, 0.0f, 0.0323f, 0.0f});
    break;
  }
  case 17: // Omni-84
  {
    ap.neutralBase();

    // The SonicStrings Voice 2 from a Suzuki Omnichord OM-84 System Two.
    ap.allOsc(params::phaseSuffix, [](int) { return 0.0114; });
    ap.allOsc(params::driftSuffix, [](int) { return 0.2417; });
    ap.oscTable(params::attackSuffix,
                {0.0005f, 0.0004f, 0.0003f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f,
                 0.0002f, 0.0002f, 0.0002f, 0.0002f});
    ap.allOsc(params::decaySuffix, [](int) { return 2.3037; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.allOsc(params::swellSuffix, [](int) { return 0.0; });
    ap.oscTable(params::releaseSuffix,
                {2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f,
                 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f,
                 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f,
                 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f, 2.3001f,
                 2.3001f, 2.3001f, 2.3001f, 2.3001f});
    ap.oscTable(params::amRateSuffix,
                {0.2619f, 1.774f, 0.3473f, 0.4424f, 0.9838f, 1.1717f, 2.9009f,
                 4.4105f, 4.7935f, 0.8237f, 0.5926f, 5.1681f, 4.9136f, 0.2446f,
                 4.3225f, 1.4464f, 2.5852f, 4.0523f, 0.2432f, 2.0366f, 2.6037f,
                 2.6913f, 0.4514f, 1.4237f, 0.491f, 4.3752f, 0.2557f, 2.579f,
                 1.2267f, 4.5499f, 2.6531f, 1.2223f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.0208; });
    ap.allOsc(params::velSuffix, [](int) { return 0.0021; });
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.3038f, 0.2582f, 0.14f, 0.1476f, 0.1f, 0.1088f, 0.0726f,
                 0.0757f, 0.0657f, 0.0556f, 0.0454f, 0.0491f, 0.0368f, 0.0349f,
                 0.03f, 0.0315f, 0.0246f, 0.026f, 0.0185f, 0.0201f, 0.0154f,
                 0.016f, 0.0124f, 0.0131f, 0.0098f, 0.0111f, 0.0082f, 0.0071f,
                 0.0069f, 0.0065f, 0.0053f});
    ap.set("echoAge", 0.572f);
    ap.set("echoFeedback", 0.3946f);
    ap.set("echoMix", 0.1338f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.0934f);
    ap.set("noise_amDepth", 0.5819f);
    ap.set("noise_attack", 0.0002f);
    ap.set("noise_colour", 0.0012f);
    ap.set("noise_decay", 0.0181f);
    ap.set("noise_release", 0.0188f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_swell", 0.0f);
    ap.set("noise_volume", 0.2971f);
    ap.set("reverbDamp", 0.5222f);
    ap.set("reverbDecay", 0.5972f);
    ap.set("reverbMix", 0.1235f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0011f);
    ap.set("track", 7.0f);
    break;
  }
  case 18: // Shimmer
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
  case 19: // Slow Pad
  {
    ap.neutralBase();

    // Staggered envelope delays, so the spectrum unfolds rather than arriving
    // at once, with mirrored pans across the series.
    ap.oscTable(params::pmRateSuffix,
                {1.2542f, 1.6849f, 2.4576f, 0.153f, 0.2013f, 2.5563f, 3.209f,
                 0.5424f, 0.1855f, 3.0829f, 0.4622f, 0.1945f, 1.9676f, 2.1286f,
                 0.5098f, 0.4064f, 0.59f, 0.2945f, 1.1357f, 0.1476f, 2.0863f,
                 0.1762f, 0.9226f, 3.1499f, 0.7736f, 1.3763f, 3.0784f, 0.461f,
                 0.3086f, 1.9705f, 1.5774f, 1.4023f});
    ap.oscTable(params::pmDepthSuffix,
                {0.0518f, 0.8223f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f,
                 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f,
                 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f,
                 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f, 2.1418f,
                 2.1418f, 2.1418f, 2.1418f, 2.1418f});
    ap.allOsc(params::driftSuffix, [](int) { return 7.0; });
    ap.oscTable(params::delaySuffix,
                {0.0f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f,
                 0.45f, 0.5f, 0.55f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f,
                 0.9f, 0.95f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.25f, 1.3f,
                 1.35f, 1.4f, 1.45f, 1.5f, 1.55f});
    ap.allOsc(params::attackSuffix, [](int) { return 0.8; });
    ap.allOsc(params::decaySuffix, [](int) { return 4.0; });
    ap.allOsc(params::sustainSuffix, [](int) { return 0.8; });
    ap.allOsc(params::releaseSuffix, [](int) { return 3.0; });
    ap.oscTable(params::amRateSuffix,
                {0.37f, 0.44f, 0.51f, 0.58f, 0.65f, 0.72f, 0.79f, 0.86f, 0.93f,
                 1.0f, 1.07f, 1.14f, 1.21f, 1.28f, 1.35f, 1.42f, 1.49f, 1.56f,
                 1.63f, 1.7f, 1.77f, 1.84f, 1.91f, 1.98f, 2.05f, 2.12f, 2.19f,
                 2.26f, 2.33f, 2.4f, 2.47f, 2.54f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.15; });
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.5f, 0.3333f, 0.25f, 0.2f, 0.1667f, 0.1429f, 0.125f,
                 0.1111f, 0.1f, 0.0909f, 0.0833f, 0.0769f, 0.0714f, 0.0667f,
                 0.0625f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    ap.oscTable(params::panSuffix,
                {0.0f, 0.0f, 0.1807f, -0.1807f, -0.2556f, 0.2556f, 0.313f,
                 -0.313f, -0.3615f, 0.3615f, 0.4041f, -0.4041f, -0.4427f,
                 0.4427f, 0.4782f, -0.4782f, -0.5112f, 0.5112f, 0.5422f,
                 -0.5422f, -0.5715f, 0.5715f, 0.5994f, -0.5994f, -0.6261f,
                 0.6261f, 0.6517f, -0.6517f, -0.6763f, 0.6763f, 0.7f, -0.7f});
    ap.set("track", 2.5f);
    ap.set("wobble", 0.1463f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.3429f);
    ap.set("echoAge", 0.5593f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.35f);
    ap.set("reverbDecay", 5.0f);
    ap.set("reverbDamp", 0.55f);
    ap.set("reverbPreDelay", 0.0262f);
    ap.set("noise_colour", 0.7012f);
    ap.set("noise_delay", 0.0401f);
    ap.set("noise_attack", 1.5788f);
    ap.set("noise_decay", 3.1351f);
    ap.set("noise_sustain", 0.4765f);
    ap.set("noise_amDepth", 0.6246f);
    ap.set("noise_volume", 0.015f);
    break;
  }
  case 20: // Space Flute
  {
    ap.neutralBase();

    // A synthesised pan flute a long way off. The breath is the noise
    // channel, held rather than struck so it stays under the note, and the
    // echo and the reverb between them do the work of putting it somewhere.

    ap.oscTable(params::pmRateSuffix,
                {1.3544f, 2.1602f, 2.7113f, 3.7712f, 7.7323f, 0.3224f, 2.9698f,
                 4.5685f, 1.4947f, 0.5585f, 2.9589f, 0.6277f, 1.3749f, 4.194f,
                 0.5581f, 1.3022f, 0.9668f, 3.5491f, 1.328f, 1.3924f, 0.4854f,
                 5.1963f, 0.7553f, 3.1087f, 1.4386f, 3.986f, 3.5834f, 5.5584f,
                 4.1209f, 3.085f, 6.2335f, 1.9861f});
    ap.oscTable(params::pmDepthSuffix,
                {5.5184f, 5.8528f, 6.1986f, 6.5565f, 6.9267f, 6.5565f, 6.1986f,
                 5.8528f, 5.5184f, 5.1952f, 4.8828f, 4.5808f, 4.2889f, 4.0067f,
                 3.734f, 3.4703f, 3.2155f, 2.9691f, 2.731f, 2.5008f, 2.2783f,
                 2.0633f, 1.8554f, 1.6544f, 1.4602f, 1.2724f, 1.0909f, 0.9154f,
                 0.7458f, 0.5819f, 0.4235f, 0.2703f});
    ap.allOsc(params::driftSuffix, [](int) { return 1.6123; });
    ap.oscTable(params::attackSuffix,
                {0.0557f, 0.0471f, 0.0392f, 0.0304f, 0.0467f, 0.0342f, 0.0523f,
                 0.0712f, 0.0469f, 0.0672f, 0.0409f, 0.101f, 0.0366f, 0.1181f,
                 0.0694f, 0.1054f, 0.0724f, 0.0513f, 0.0398f, 0.0634f, 0.0398f,
                 0.0406f, 0.0435f, 0.0155f, 0.0286f, 0.0288f, 0.0295f, 0.0165f,
                 0.007f, 0.0051f, 0.0165f, 0.0241f});
    ap.oscTable(params::sustainSuffix,
                {0.7145f, 0.2628f, 0.2763f, 0.053f, 0.2089f, 0.0577f, 0.1523f,
                 0.069f, 0.0214f, 0.0146f, 0.0305f, 0.009f, 0.0193f, 0.0117f,
                 0.0085f, 0.0138f, 0.0094f, 0.0144f, 0.0111f, 0.0214f, 0.0366f,
                 0.0328f, 0.0548f, 0.0932f, 0.1641f, 0.0f, 0.0876f, 0.114f,
                 0.082f, 0.0908f, 0.0858f, 0.0f});
    ap.allOsc(params::releaseSuffix, [](int) { return 0.008; });
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.4792; });
    ap.oscTable(params::velSuffix,
                {-0.126f, -0.0906f, -0.0543f, -0.0179f, 0.0184f, 0.0548f,
                 0.0911f, 0.1275f, 0.1638f, 0.2002f, 0.2365f, 0.2729f, 0.3093f,
                 0.3456f, 0.382f, 0.4183f, 0.4547f, 0.491f, 0.5274f, 0.5637f,
                 0.6001f, 0.6365f, 0.6728f, 0.7092f, 0.7455f, 0.7819f, 0.8182f,
                 0.8546f, 0.8909f, 0.9273f, 0.9636f, 0.9999f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.0409f, 0.332f, 0.0138f, 0.1832f, 0.0228f, 0.1528f,
                 0.015f, 0.1055f, 0.04f, 0.0774f, 0.0549f, 0.106f, 0.0466f,
                 0.0903f, 0.0634f, 0.0746f, 0.085f, 0.076f, 0.0222f, 0.0121f,
                 0.0143f, 0.0054f, 0.0042f, 0.0024f, 0.0187f, 0.0037f, 0.0028f,
                 0.0031f, 0.002f, 0.0018f, 0.0077f});
    ap.set("h02_decay", 0.2074f);
    ap.set("h02_pan", -1.0f);
    ap.set("h03_pan", 0.2326f);
    ap.set("h04_decay", 0.4813f);
    ap.set("h04_pan", 1.0f);
    ap.set("h05_pan", -0.7375f);
    ap.set("h06_pan", -1.0f);
    ap.set("h07_decay", 0.3087f);
    ap.set("h07_pan", 0.943f);
    ap.set("h08_decay", 0.5518f);
    ap.set("h08_pan", -1.0f);
    ap.set("h09_decay", 0.0399f);
    ap.set("h09_pan", 1.0f);
    ap.set("h10_decay", 0.2134f);
    ap.set("h10_pan", 1.0f);
    ap.set("h11_decay", 0.331f);
    ap.set("h11_pan", 1.0f);
    ap.set("h12_decay", 0.0912f);
    ap.set("h12_pan", -1.0f);
    ap.set("h13_decay", 0.1252f);
    ap.set("h13_pan", -1.0f);
    ap.set("h14_decay", 0.0534f);
    ap.set("h14_pan", 1.0f);
    ap.set("h15_decay", 0.0127f);
    ap.set("h15_pan", -1.0f);
    ap.set("h16_decay", 0.0218f);
    ap.set("h16_pan", 1.0f);
    ap.set("h17_decay", 0.0154f);
    ap.set("h17_pan", -1.0f);
    ap.set("h18_decay", 0.0116f);
    ap.set("h18_pan", 1.0f);
    ap.set("h19_decay", 0.0145f);
    ap.set("h19_pan", -1.0f);
    ap.set("h20_decay", 0.4036f);
    ap.set("h20_pan", 1.0f);
    ap.set("h21_decay", 0.4702f);
    ap.set("h21_pan", -1.0f);
    ap.set("h22_decay", 0.2632f);
    ap.set("h22_pan", 1.0f);
    ap.set("h23_decay", 0.6719f);
    ap.set("h23_pan", -1.0f);
    ap.set("h24_pan", -1.0f);
    ap.set("h25_decay", 0.5645f);
    ap.set("h25_pan", 1.0f);
    ap.set("h26_decay", 0.1013f);
    ap.set("h26_pan", -1.0f);
    ap.set("h27_decay", 0.4502f);
    ap.set("h27_pan", 1.0f);
    ap.set("h28_pan", -1.0f);
    ap.set("h29_decay", 0.558f);
    ap.set("h29_pan", 1.0f);
    ap.set("h30_pan", -1.0f);
    ap.set("h31_pan", 1.0f);
    ap.set("h32_decay", 0.0926f);
    ap.set("h32_pan", -1.0f);
    ap.set("echoAge", 0.4097f);
    ap.set("echoFeedback", 0.7189f);
    ap.set("echoMix", 0.2533f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.3583f);
    ap.set("lofiBits", 4.0f);
    ap.set("noise_attack", 0.0645f);
    ap.set("noise_colour", 0.1192f);
    ap.set("noise_decay", 0.1374f);
    ap.set("noise_sustain", 0.2331f);
    ap.set("noise_vel", 0.8135f);
    ap.set("noise_volume", 0.1882f);
    ap.set("reverbDamp", 0.0716f);
    ap.set("reverbDecay", 3.5185f);
    ap.set("reverbMix", 0.1989f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0233f);
    ap.set("stretch", 154.4952f);
    ap.set("track", 2.0f);
    ap.set("wobble", 0.2795f);
    break;
  }
  case 21: // Struck Bell
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
  case 22: // Synth Ensemble
  {
    ap.neutralBase();

    // Strings and brass in one patch, synthesised rather than either of them
    // sampled. A third of a second of attack across the whole series is what
    // makes it an ensemble rather than one player, since nothing in it
    // arrives at the same moment.

    ap.oscTable(params::pmDepthSuffix,
                {3.0539f, 7.5661f, 10.6856f, 10.8007f, 10.9168f, 11.0337f,
                 11.1516f, 11.2704f, 11.3901f, 11.5107f, 11.6322f, 11.7547f,
                 11.8782f, 12.0026f, 12.128f, 12.2543f, 12.3816f, 12.5099f,
                 12.6392f, 12.7695f, 12.9009f, 13.0332f, 13.1666f, 13.3009f,
                 13.4364f, 13.5729f, 13.7105f, 13.8491f, 13.9887f, 14.1295f,
                 14.2714f, 14.4099f});
    ap.oscTable(params::driftSuffix,
                {1.0325f, 2.4834f, 3.2579f, 3.2727f, 3.2876f, 3.3026f, 3.3175f,
                 3.3325f, 3.3476f, 3.3627f, 3.3778f, 3.3929f, 3.4081f, 3.4233f,
                 3.4386f, 3.4539f, 3.4692f, 3.4846f, 3.5f, 3.5155f, 3.531f,
                 3.5465f, 3.5621f, 3.5777f, 3.5933f, 3.609f, 3.6247f, 3.6405f,
                 3.6563f, 3.6721f, 3.688f, 3.7034f});
    ap.allOsc(params::attackSuffix, [](int) { return 0.3332; });
    ap.allOsc(params::decaySuffix, [](int) { return 2.5173; });
    ap.allOsc(params::releaseSuffix, [](int) { return 0.252; });
    ap.oscTable(params::amRateSuffix,
                {1.6249f, 1.6249f, 1.1034f, 1.6249f, 1.3157f, 1.1034f, 0.9767f,
                 1.6249f, 1.435f, 1.3157f, 1.9557f, 1.1034f, 1.3692f, 0.9767f,
                 1.7434f, 1.6249f, 1.8058f, 1.435f, 2.2094f, 1.3157f, 2.3705f,
                 1.9557f, 1.9557f, 1.1034f, 1.3692f, 1.3692f, 2.4382f, 0.9767f,
                 0.9767f, 1.7434f, 1.7434f, 1.6249f});
    ap.oscTable(params::amDepthSuffix,
                {0.0009f, 0.0297f, 0.0595f, 0.0892f, 0.119f, 0.1487f, 0.1784f,
                 0.2082f, 0.2379f, 0.2677f, 0.2974f, 0.3271f, 0.3569f, 0.3866f,
                 0.4164f, 0.4461f, 0.4758f, 0.5056f, 0.5353f, 0.565f, 0.5948f,
                 0.6245f, 0.6543f, 0.684f, 0.7137f, 0.7435f, 0.7732f, 0.803f,
                 0.8327f, 0.8624f, 0.8922f, 0.9219f});
    ap.oscTable(params::velSuffix,
                {0.1084f, 0.6396f, 0.7476f, 0.8385f, 0.9294f, 0.932f, 0.9347f,
                 0.9373f, 0.9399f, 0.9425f, 0.9451f, 0.9477f, 0.9503f, 0.953f,
                 0.9556f, 0.9582f, 0.9608f, 0.9634f, 0.966f, 0.9686f, 0.9713f,
                 0.9739f, 0.9765f, 0.9791f, 0.9817f, 0.9843f, 0.9869f, 0.9895f,
                 0.9922f, 0.9948f, 0.9974f, 0.9999f});
    ap.oscTable(params::volumeSuffix,
                {0.9945f, 0.969f, 0.9224f, 0.8584f, 0.7809f, 0.6944f, 0.6036f,
                 0.5129f, 0.426f, 0.3459f, 0.2745f, 0.213f, 0.1615f, 0.1198f,
                 0.0868f, 0.0615f, 0.0426f, 0.0288f, 0.0191f, 0.0123f, 0.0078f,
                 0.0048f, 0.0029f, 0.0017f, 0.001f, 0.0006f, 0.0003f, 0.0002f,
                 0.0001f, 0.0f, 0.0f, 0.0f});
    ap.set("h01_aftertouch", 0.0023f);
    ap.set("h02_pan", 0.246f);
    ap.set("h02_sustain", 0.7132f);
    ap.set("h03_delay", 0.05f);
    ap.set("h03_pan", -0.5008f);
    ap.set("h03_pmRate", 3.2236f);
    ap.set("h03_sustain", 0.4396f);
    ap.set("h04_pan", 0.749f);
    ap.set("h04_sustain", 0.9032f);
    ap.set("h05_delay", 0.1004f);
    ap.set("h05_pan", -1.0f);
    ap.set("h05_pmRate", 2.6973f);
    ap.set("h05_sustain", 0.3751f);
    ap.set("h06_delay", 0.05f);
    ap.set("h06_pan", -1.0f);
    ap.set("h06_pmRate", 3.2236f);
    ap.set("h06_sustain", 0.8387f);
    ap.set("h07_delay", 0.1509f);
    ap.set("h07_pan", 1.0f);
    ap.set("h07_pmRate", 2.9626f);
    ap.set("h07_sustain", 0.3106f);
    ap.set("h08_pan", 1.0f);
    ap.set("h08_sustain", 0.7742f);
    ap.set("h09_delay", 0.1999f);
    ap.set("h09_pan", -1.0f);
    ap.set("h09_pmRate", 2.0403f);
    ap.set("h09_sustain", 0.2461f);
    ap.set("h10_delay", 0.1004f);
    ap.set("h10_pan", -1.0f);
    ap.set("h10_pmRate", 2.6973f);
    ap.set("h10_sustain", 0.7097f);
    ap.set("h11_delay", 0.2504f);
    ap.set("h11_pan", 1.0f);
    ap.set("h11_pmRate", 4.4038f);
    ap.set("h11_sustain", 0.1815f);
    ap.set("h12_delay", 0.05f);
    ap.set("h12_pan", 1.0f);
    ap.set("h12_pmRate", 3.2236f);
    ap.set("h12_sustain", 0.6452f);
    ap.set("h13_delay", 0.3f);
    ap.set("h13_pan", -1.0f);
    ap.set("h13_pmRate", 1.4318f);
    ap.set("h13_sustain", 0.117f);
    ap.set("h14_delay", 0.1509f);
    ap.set("h14_pan", -1.0f);
    ap.set("h14_pmRate", 2.9626f);
    ap.set("h14_sustain", 0.5806f);
    ap.set("h15_delay", 0.35f);
    ap.set("h15_pan", 1.0f);
    ap.set("h15_pmRate", 3.8436f);
    ap.set("h15_sustain", 0.0525f);
    ap.set("h16_pan", 1.0f);
    ap.set("h16_sustain", 0.5161f);
    ap.set("h17_delay", 0.4f);
    ap.set("h17_pan", -1.0f);
    ap.set("h17_pmRate", 4.5083f);
    ap.set("h17_sustain", 0.0f);
    ap.set("h18_delay", 0.1999f);
    ap.set("h18_pan", -1.0f);
    ap.set("h18_pmRate", 2.0403f);
    ap.set("h18_sustain", 0.4516f);
    ap.set("h19_delay", 0.45f);
    ap.set("h19_pan", 1.0f);
    ap.set("h19_pmRate", 2.7099f);
    ap.set("h19_sustain", 0.0f);
    ap.set("h20_delay", 0.1004f);
    ap.set("h20_pan", 1.0f);
    ap.set("h20_pmRate", 2.6973f);
    ap.set("h20_sustain", 0.3871f);
    ap.set("h21_delay", 0.5001f);
    ap.set("h21_pan", -1.0f);
    ap.set("h21_pmRate", 1.5076f);
    ap.set("h21_sustain", 0.0f);
    ap.set("h22_delay", 0.2504f);
    ap.set("h22_pan", -1.0f);
    ap.set("h22_pmRate", 4.4038f);
    ap.set("h22_sustain", 0.3226f);
    ap.set("h23_delay", 0.2504f);
    ap.set("h23_pan", 1.0f);
    ap.set("h23_pmRate", 4.4038f);
    ap.set("h23_sustain", 0.0f);
    ap.set("h24_delay", 0.05f);
    ap.set("h24_pan", 1.0f);
    ap.set("h24_pmRate", 3.2236f);
    ap.set("h24_sustain", 0.2581f);
    ap.set("h25_delay", 0.3f);
    ap.set("h25_pan", -1.0f);
    ap.set("h25_pmRate", 1.4318f);
    ap.set("h25_sustain", 0.0f);
    ap.set("h26_delay", 0.3f);
    ap.set("h26_pan", -1.0f);
    ap.set("h26_pmRate", 1.4318f);
    ap.set("h26_sustain", 0.1935f);
    ap.set("h27_delay", 0.55f);
    ap.set("h27_pan", 1.0f);
    ap.set("h27_pmRate", 4.4557f);
    ap.set("h27_sustain", 0.0f);
    ap.set("h28_delay", 0.1509f);
    ap.set("h28_pan", 1.0f);
    ap.set("h28_pmRate", 2.9626f);
    ap.set("h28_sustain", 0.129f);
    ap.set("h29_delay", 0.1509f);
    ap.set("h29_pan", -1.0f);
    ap.set("h29_pmRate", 2.9626f);
    ap.set("h29_sustain", 0.0f);
    ap.set("h30_delay", 0.35f);
    ap.set("h30_pan", -1.0f);
    ap.set("h30_pmRate", 3.8436f);
    ap.set("h30_sustain", 0.0645f);
    ap.set("h31_delay", 0.35f);
    ap.set("h31_pan", 1.0f);
    ap.set("h31_pmRate", 3.8436f);
    ap.set("h31_sustain", 0.0f);
    ap.set("h32_sustain", 0.001f);
    ap.set("echoAge", 0.6465f);
    ap.set("echoFeedback", 0.6923f);
    ap.set("echoMix", 0.248f);
    ap.set("echoOn", 1.0f);
    ap.set("echoTime", 0.1305f);
    ap.set("noise_amDepth", 0.7486f);
    ap.set("noise_amRate", 0.2475f);
    ap.set("noise_attack", 0.9905f);
    ap.set("noise_colour", 0.1525f);
    ap.set("noise_decay", 2.7863f);
    ap.set("noise_sustain", 0.4616f);
    ap.set("noise_vel", 0.7797f);
    ap.set("noise_volume", 0.157f);
    ap.set("reverbDamp", 0.2763f);
    ap.set("reverbDecay", 2.9417f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbPreDelay", 0.0228f);
    ap.set("stretch", 9.5576f);
    ap.set("track", 3.0f);
    ap.set("wobble", 0.1011f);
    break;
  }
  case 23: // Tape Choir
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
  case 24: // Vibraphone
  {
    ap.neutralBase();

    // A fast strike where the fundamental rings for seconds and everything
    // above it is gone inside one.
    ap.oscTable(params::driftSuffix,
                {0.0183f, 0.0f, 0.0f, 0.0637f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0647f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f});
    ap.allOsc(params::attackSuffix, [](int) { return 0.002; });
    ap.oscTable(params::decaySuffix,
                {3.0f, 0.6f, 0.6f, 1.5f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.8f,
                 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f,
                 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f,
                 0.6f, 0.6f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::releaseSuffix,
                {3.0f, 0.4f, 0.4f, 1.5f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.8f,
                 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f,
                 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f,
                 0.4f, 0.4f});
    ap.oscTable(params::amRateSuffix,
                {5.0f, 4.0f, 4.0f, 5.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 5.0f,
                 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f,
                 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f,
                 4.0f, 4.0f});
    ap.oscTable(params::amDepthSuffix,
                {0.6f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f});
    ap.oscTable(params::velSuffix,
                {0.602f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 1.0f,
                 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f,
                 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f,
                 0.7f, 0.7f});
    ap.oscTable(params::volumeSuffix,
                {1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f});
    ap.oscTable(params::panSuffix,
                {0.0f, 0.0f, 0.0f, 0.1598f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 -0.357f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 0.0f});
    ap.set("track", 3.0f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.1163f);
    ap.set("echoTime", 0.0873f);
    ap.set("echoFeedback", 0.0544f);
    ap.set("echoAge", 0.5274f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.1853f);
    ap.set("reverbDecay", 1.3049f);
    ap.set("reverbPreDelay", 0.0047f);
    ap.set("noise_colour", 0.1791f);
    ap.set("noise_attack", 0.0006f);
    ap.set("noise_decay", 0.0053f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_amDepth", 0.711f);
    ap.set("noise_volume", 0.1882f);
    break;
  }
  case 25: // Wurli
  {
    ap.neutralBase();

    // A Wurlitzer 200A electric piano.
    ap.allOsc(params::driftSuffix, [](int) { return 1.463; });
    ap.oscTable(params::delaySuffix,
                {0.0f, 0.0005f, 0.0036f, 0.0034f, 0.0032f, 0.003f, 0.0028f,
                 0.0026f, 0.0024f, 0.0023f, 0.0035f, 0.002f, 0.0019f, 0.0017f,
                 0.0015f, 0.0014f, 0.0013f, 0.0012f, 0.0011f, 0.001f, 0.0009f,
                 0.0008f, 0.0007f, 0.0006f, 0.0005f, 0.0005f, 0.0004f, 0.0003f,
                 0.0002f, 0.0002f, 0.0001f, 0.0001f});
    ap.oscTable(params::attackSuffix,
                {0.0193f, 0.0018f, 0.0011f, 0.0011f, 0.4406f, 0.0011f, 0.001f,
                 0.0009f, 0.0009f, 0.0008f, 0.0008f, 0.0008f, 0.001f, 0.0008f,
                 0.0007f, 0.0007f, 0.0008f, 0.0008f, 0.0009f, 0.0008f, 0.001f,
                 0.0008f, 0.0007f, 0.0008f, 0.0008f, 0.0008f, 0.0007f, 0.0008f,
                 0.0008f, 0.0009f, 0.0007f, 0.0008f});
    ap.oscTable(params::decaySuffix,
                {17.8235f, 6.8692f, 6.0151f, 4.6458f, 4.9433f, 4.2602f,
                 3.5376f, 3.7996f, 2.8692f, 3.7686f, 2.3638f, 3.8018f, 2.4036f,
                 2.8405f, 1.9962f, 3.0232f, 1.7166f, 3.2789f, 1.9723f, 2.008f,
                 1.5554f, 1.9113f, 1.5109f, 2.1526f, 1.277f, 2.3079f, 1.8298f,
                 1.7569f, 2.0609f, 2.0971f, 1.8512f, 2.1827f});
    ap.allOsc(params::sustainSuffix, [](int) { return 0.0; });
    ap.oscTable(params::swellSuffix,
                {0.0043f, 0.0042f, 0.0041f, 0.004f, 0.0039f, 0.0038f, 0.0037f,
                 0.0037f, 0.0036f, 0.0035f, 0.0035f, 0.0034f, 0.0033f, 0.0033f,
                 0.0032f, 0.0032f, 0.0031f, 0.0031f, 0.003f, 0.003f, 0.0029f,
                 0.0029f, 0.0028f, 0.0028f, 0.0028f, 0.0027f, 0.0027f, 0.0027f,
                 0.0026f, 0.0026f, 0.0026f, 0.0026f});
    ap.oscTable(params::offLevelSuffix,
                {0.668f, 0.6466f, 0.6193f, 0.5929f, 0.5674f, 0.5428f, 0.519f,
                 0.4961f, 0.474f, 0.4526f, 0.432f, 0.4121f, 0.3929f, 0.3744f,
                 0.3565f, 0.3393f, 0.3226f, 0.3065f, 0.291f, 0.276f, 0.2616f,
                 0.2476f, 0.2342f, 0.2212f, 0.2086f, 0.1965f, 0.1848f, 0.1735f,
                 0.1627f, 0.1521f, 0.142f, 0.1322f});
    ap.allOsc(params::releaseSuffix, [](int) { return 0.0626; });
    ap.oscTable(params::amRateSuffix,
                {5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f,
                 5.6956f, 5.6956f, 5.6956f, 5.6957f, 5.6956f, 5.6956f, 5.6956f,
                 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f,
                 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f, 5.6956f,
                 5.6956f, 5.6956f, 5.6956f, 5.6956f});
    ap.allOsc(params::amDepthSuffix, [](int) { return 0.4893; });
    ap.oscTable(params::velSuffix,
                {-0.4489f, 0.0005f, 0.1726f, 0.3963f, 0.6886f, 0.8068f,
                 0.8628f, 0.887f, 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f,
                 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f,
                 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f,
                 0.9467f, 0.9467f, 0.9467f, 0.9467f, 0.9467f});
    ap.oscTable(params::volumeSuffix,
                {0.9984f, 0.4176f, 0.5227f, 0.1632f, 0.2148f, 0.0923f, 0.2002f,
                 0.0691f, 0.1642f, 0.0579f, 0.1238f, 0.0218f, 0.0976f, 0.0353f,
                 0.0845f, 0.0222f, 0.067f, 0.0149f, 0.0393f, 0.0231f, 0.0452f,
                 0.0231f, 0.0294f, 0.0175f, 0.0246f, 0.0085f, 0.0115f, 0.0101f,
                 0.0052f, 0.0059f, 0.0071f, 0.005f});
    ap.set("stretch", 0.1305f);
    ap.set("track", 3.1f);
    ap.set("echoOn", 1.0f);
    ap.set("echoMix", 0.0986f);
    ap.set("echoTime", 0.0925f);
    ap.set("echoFeedback", 0.3841f);
    ap.set("echoAge", 0.44f);
    ap.set("reverbOn", 1.0f);
    ap.set("reverbMix", 0.0843f);
    ap.set("reverbDecay", 0.9978f);
    ap.set("reverbDamp", 0.1765f);
    ap.set("reverbPreDelay", 0.0059f);
    ap.set("noise_colour", 0.0f);
    ap.set("noise_delay", 0.0035f);
    ap.set("noise_attack", 0.0002f);
    ap.set("noise_decay", 2.2004f);
    ap.set("noise_sustain", 0.0f);
    ap.set("noise_swell", 0.0677f);
    ap.set("noise_offLevel", 1.0f);
    ap.set("noise_release", 0.2451f);
    ap.set("noise_volume", 0.0234f);
    break;
  }
  default:
    break;
  }
}

} // namespace ovt::presets
