#pragma once

#include <map>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginParameters.h"

namespace ovt::channelcopy {

/// What a copied channel holds: each setting's suffix, such as "attack", and
/// its plain value, as the session stores it.
using Settings = std::map<juce::String, float>;

/// Whether a parameter belongs on a copy: one of a partial's own, "h01_" to
/// "h32_", and not its mute or solo, which are about the mix and not the
/// sound. Read off the id, so a parameter added to the strips later is copied
/// without a change here.
///
/// @param suffix  set to what follows the underscore when it does
bool isCopied(const juce::String &parameterId, juce::String &suffix);

/// Every copied setting of one partial.
///
/// @param channel  0 to kNumHarmonics - 1
Settings read(const juce::AudioProcessorValueTreeState &, int channel);

/// The text that goes on the clipboard: one XML element whose attributes are
/// the settings.
juce::String toText(const Settings &);

/// The settings in a text from toText, or nothing when the text is not one,
/// including one that is cut off. Attributes that are not a number are
/// skipped. An empty copy counts as no copy.
std::optional<Settings> fromText(const juce::String &);

/// The moves that set one partial to the settings. Only settings the channel
/// has and that change are included, so pasting onto an identical channel
/// makes no undo step. Values outside a range are set to its nearest end.
std::vector<params::Move> movesFor(juce::AudioProcessorValueTreeState &,
                                   const Settings &, int channel);

} // namespace ovt::channelcopy
