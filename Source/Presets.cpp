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
    "Drawbar Organ",
    "Equal Saw",
    "Glass Armonica",
    "Init",
    "Just Saw",
    "Lo-fi",
    "Metallic Piano",
    "Music Box",
    "Odd Harmonics",
    "Omni-84",
    "Shimmer",
    "Slow Pad",
    "Struck Bell",
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
    bool session = false;
    for (auto *id : params::kSessionParamIds)
      session |= ranged->paramID == id;

    if (session)
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

  case 3: // Drawbar Organ
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

  case 4: // Equal Saw
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

  case 5: // Glass Armonica
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

  case 6: // Init
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

  case 7: // Just Saw
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

  case 8: // Lo-fi
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

  case 9: // Metallic Piano
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

  case 10: // Music Box
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

  case 11: // Odd Harmonics
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

  case 12: // Omni-84
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

  case 13: // Shimmer
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

  case 14: // Slow Pad
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

  case 15: // Struck Bell
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

  case 16: // Tape Choir
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

  case 17: // Vibraphone
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

  case 18: // Wurli
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
