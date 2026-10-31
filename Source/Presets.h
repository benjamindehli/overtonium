#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginParameters.h"

namespace ovt::presets {

/// Factory preset names, in menu order.
juce::StringArray names();

/// Applies a factory preset by index. Must be called from the message thread.
void apply(juce::AudioProcessorValueTreeState &apvts, int index);

// -----------------------------------------------------------------------------
// User presets
//
// Stored as one small XML file each, holding plain parameter values and the
// channels each macro on Selected reaches. Not the whole state tree, so
// loading a sound does not change the window, zoom or LINK settings.
// -----------------------------------------------------------------------------

/// Where user presets live. Created on demand.
juce::File userDirectory();

/// The saved presets, sorted by name.
juce::Array<juce::File> userPresets();

/// Strips what a filename cannot carry and trims what a menu cannot show.
/// Returns an empty string if nothing usable is left.
juce::String sanitiseName(const juce::String &);

/// The sound as it stands, ready to be written out: every parameter except
/// the session ones, the same set a factory preset decides.
///
/// @param channels  what each macro on Selected reaches. Left out when null.
std::unique_ptr<juce::XmlElement>
capture(juce::AudioProcessorValueTreeState &, const juce::String &name,
        const params::MacroChannels *channels = nullptr);

/// Applies a captured preset. Parameters the document does not mention are
/// left alone rather than reset, so an older file loads into a newer build.
/// Session parameters are ignored even when a file names them. A macro on
/// Selected that the document names no channels for reaches none.
///
/// @returns how many parameters it recognised, or -1 if the document is not
/// one of ours.
int restore(juce::AudioProcessorValueTreeState &, const juce::XmlElement &,
            params::MacroChannels *channels = nullptr);

/// Writes the current state to the user directory under `name`, replacing a
/// preset of the same name.
bool save(juce::AudioProcessorValueTreeState &, const juce::String &name,
          juce::String &error, const params::MacroChannels *channels = nullptr);

/// Reads a user preset file and restores it.
bool load(juce::AudioProcessorValueTreeState &, const juce::File &,
          juce::String &error, params::MacroChannels *channels = nullptr);

/// The state every factory preset starts from, before it sets anything.
///
/// Differs from the parameter defaults: the per-partial levels go to zero
/// rather than a 1/n spectrum, so a preset states the spectrum it wants.
void neutralBase(juce::AudioProcessorValueTreeState &);

/// The current state written out as the C++ that would add it to Presets.cpp
/// as a factory preset.
juce::String factoryCode(juce::AudioProcessorValueTreeState &,
                         const juce::String &name);

} // namespace ovt::presets
