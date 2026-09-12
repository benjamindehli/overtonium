// Turns a saved preset file into the C++ that adds it to Presets.cpp.
//
// Factory presets live as code rather than as embedded data, which is worth
// having and costs a step: a patch dialled in by hand has to become a case in
// ovt::presets::apply. The generator that does it, presets::factoryCode, has
// always been there, but the only way to reach it was a build with
// OVERTONIUM_PRESET_AUTHORING=1 and a click in a menu, which meant keeping a
// second build of the plugin around to maintain the first one's presets.
//
//   preset_to_code <file.ovtpreset> [case-number]
//
// It writes the case body to stdout, so it pipes and diffs. Updating a preset
// is then: save it from the plugin, run this, and paste over the case. There
// is no transcription anywhere in that, which is the point: the values are
// four-decimal roundings of 769 parameters and nothing reading them would
// notice one going wrong.
//
// Deliberately the same code path the plugin's own menu entry uses rather than
// a reimplementation. A generator that agreed with the plugin most of the time
// would be worse than no generator, and the runtime test already holds
// factoryCode to reproducing every factory preset it is given.
//
// It does not edit Presets.cpp. The cases carry hand-written comments above
// them saying what the patch is and why, which no generator can write and
// which a generator editing the file in place would quietly lose.

#include <cstdio>
#include <memory>

#include "PluginProcessor.h"
#include "Presets.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: preset_to_code <file.ovtpreset> [case-number]\n");
    return 1;
  }

  const juce::ScopedJuceInitialiser_GUI juceInit;

  const auto file =
      juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);

  if (!file.existsAsFile()) {
    std::fprintf(stderr, "no such file: %s\n",
                 file.getFullPathName().toRawUTF8());
    return 1;
  }

  const auto doc = juce::XmlDocument::parse(file);

  if (doc == nullptr) {
    std::fprintf(stderr, "not XML: %s\n", file.getFullPathName().toRawUTF8());
    return 1;
  }

  OvertoniumProcessor plugin;

  // From the neutral base rather than from whatever a fresh processor defaults
  // to. restore leaves anything the file does not mention alone, which is what
  // lets an older preset load into a newer build, and here that would silently
  // carry a default the patch never chose into the generated case. The
  // generator measures against the neutral base, so starting there means the
  // two agree about what "unchanged" is.
  ovt::presets::neutralBase(plugin.apvts);

  const auto recognised = ovt::presets::restore(plugin.apvts, *doc);

  if (recognised < 0) {
    std::fprintf(stderr, "not an Overtonium preset: %s\n",
                 file.getFullPathName().toRawUTF8());
    return 1;
  }

  // The name in the file rather than the filename, since that is the one the
  // menu shows and the one the case is commented with.
  const auto name =
      doc->getStringAttribute("name", file.getFileNameWithoutExtension());

  auto code = ovt::presets::factoryCode(plugin.apvts, name);

  // factoryCode writes "case N:", since it has no way to know where the case
  // will be filed. Fill it in when told.
  if (argc > 2)
    code = code.replaceFirstOccurrenceOf("case N:",
                                         "case " + juce::String(argv[2]) + ":");

  std::fprintf(stderr, "%s: %d parameters read from %s\n", name.toRawUTF8(),
               recognised, file.getFileName().toRawUTF8());

  std::fputs(code.toRawUTF8(), stdout);

  return 0;
}
