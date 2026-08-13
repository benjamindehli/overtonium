#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ovt::presets {

/// Factory preset names, in menu order.
juce::StringArray names();

/// Applies a factory preset by index. Must be called from the message thread.
void apply(juce::AudioProcessorValueTreeState &apvts, int index);

} // namespace ovt::presets
