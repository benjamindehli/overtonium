// Turns a saved preset file into the C++ case that adds it to Presets.cpp,
// through presets::factoryCode, the same generator the plugin's own menu entry
// uses.
//
//   preset_to_code <file.ovtpreset> [case-number]
//
// It writes the case body to stdout and does not edit Presets.cpp, so the
// hand-written comments above each case stay as they are. CONTRIBUTING.md has
// the full procedure.

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

  // From the neutral base, which the generator measures against, since restore
  // leaves anything the file does not mention alone and a fresh processor's
  // defaults would otherwise end up in the generated case.
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
