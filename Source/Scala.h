#pragma once

#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "dsp/Temperament.h"

namespace ovt::scala {

/// What reading a Scala file gave: the tuning, or why there is none.
struct Reading {
  std::optional<ScaleTable> table;

  /// Why the file cannot be used, in words for a message. Empty when it can.
  juce::String error;
};

/// Reads the text of a Scala file (.scl).
///
/// The format: lines starting with "!" are comments. The first other line is a
/// description, which may be empty. The next holds how many notes follow. Each
/// note is a line in cents, with a point, or a ratio such as 3/2 or a whole
/// number such as 2. Anything after the number on a line is ignored. The last
/// note is where the scale repeats.
///
/// Only scales of twelve notes are accepted, the twelfth being the period,
/// since Root and the Tuning bar are built around twelve pitch classes.
Reading read(const juce::String &text);

/// Where the session keeps the loaded file's text and the name it is shown by.
/// On the parameter tree, so it is saved and restored with the session and a
/// preset does not touch it.
inline const juce::Identifier kTextProperty{"overtoniumScala"};
inline const juce::Identifier kNameProperty{"overtoniumScalaName"};

/// The name of the file the session holds, or empty when it holds none.
juce::String nameIn(const juce::ValueTree &state);

/// The tuning the session holds, or equal temperament when it holds none or
/// its text cannot be read.
ScaleTable tableIn(const juce::ValueTree &state);

/// Files longer than this are refused before they are read, since no real
/// Scala file of twelve notes comes near it.
inline constexpr juce::int64 kMaxFileBytes = 256 * 1024;

} // namespace ovt::scala
