#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ovt::presets {

/// Factory preset names, in menu order.
juce::StringArray names();

/// Applies a factory preset by index. Must be called from the message thread.
void apply(juce::AudioProcessorValueTreeState &apvts, int index);

// -----------------------------------------------------------------------------
// User presets
//
// Stored as one small XML file each, holding parameter values and nothing else.
// Deliberately not the whole state tree: that also carries the window size, the
// zoom and the LINK settings, and loading a sound should not move your window
// or change your tools. Values are stored plain rather than normalised, so a
// preset survives a parameter's range being widened later, and anything a file
// does not mention keeps its default rather than being reset.
// -----------------------------------------------------------------------------

/// Where user presets live. Created on demand.
juce::File userDirectory();

/// The saved presets, sorted by name.
juce::Array<juce::File> userPresets();

/// Strips what a filename cannot carry and trims what a menu cannot show.
/// Returns an empty string if nothing usable is left.
juce::String sanitiseName(const juce::String &);

/// The sound as it stands, ready to be written out.
///
/// Every parameter except the session, which is the same set a factory preset
/// decides. Saving a sound and picking one from the menu are the same
/// operation, so they carry the same things.
std::unique_ptr<juce::XmlElement> capture(juce::AudioProcessorValueTreeState &,
                                          const juce::String &name);

/// Applies a captured preset. Parameters the document does not mention are
/// left alone rather than reset, so an older file loads into a newer build
/// without silently zeroing whatever was added in between.
///
/// Session parameters are ignored even when a file does name them. Presets
/// saved before the rule existed carry the polyphony and the temperament of
/// whoever saved them, and honouring that now would reach over and change
/// settings the file has no business touching.
///
/// @returns how many parameters it recognised, or -1 if the document is not
/// one of ours.
int restore(juce::AudioProcessorValueTreeState &, const juce::XmlElement &);

/// Writes the current state to the user directory under `name`, replacing a
/// preset of the same name.
bool save(juce::AudioProcessorValueTreeState &, const juce::String &name,
          juce::String &error);

bool load(juce::AudioProcessorValueTreeState &, const juce::File &,
          juce::String &error);

/// The state every factory preset starts from, before it sets anything.
///
/// Not the same as the parameter defaults, which is the point of naming it:
/// the per-partial levels default to a 1/n spectrum and this takes them to
/// zero, so a preset says what it wants to hear rather than inheriting a
/// spectrum it never asked for. Exposed so the generator below can be held to
/// it, and so a test can prove the code it writes reproduces the patch.
void neutralBase(juce::AudioProcessorValueTreeState &);

/// The current state written out as the C++ that would add it to Presets.cpp
/// as a factory preset.
///
/// Factory presets stay as code rather than as embedded data because the ones
/// already there are worth reading: they say `1.0 / n` and `6.0 / (1 + 0.35 *
/// (n - 1))` rather than listing 32 numbers. This is how a patch dialled in by
/// hand becomes one of them, without anyone transcribing 640 values.
juce::String factoryCode(juce::AudioProcessorValueTreeState &,
                         const juce::String &name);

} // namespace ovt::presets
