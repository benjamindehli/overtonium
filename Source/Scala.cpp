#include "Scala.h"

#include <cmath>

namespace ovt::scala {

namespace {
/// One note's line in cents, or nothing when it is not one. A number with a
/// point is cents, anything else a ratio, as Scala reads them.
std::optional<double> centsOf(const juce::String &line) {
  // The number is the first word. Scala ignores the rest of the line.
  const auto word = line.trim()
                        .upToFirstOccurrenceOf(" ", false, false)
                        .upToFirstOccurrenceOf("\t", false, false)
                        .trim();

  if (word.isEmpty())
    return std::nullopt;

  if (word.containsChar('.')) {
    if (!word.containsOnly("0123456789.+-") || word == "." ||
        word.indexOfChar('.') != word.lastIndexOfChar('.'))
      return std::nullopt;

    const auto cents = word.getDoubleValue();
    return std::isfinite(cents) ? std::optional<double>(cents) : std::nullopt;
  }

  // A ratio, or a whole number standing for one over one.
  const auto top = word.upToFirstOccurrenceOf("/", false, false);
  const auto bottom = word.containsChar('/')
                          ? word.fromFirstOccurrenceOf("/", false, false)
                          : juce::String("1");

  if (top.isEmpty() || bottom.isEmpty() || !top.containsOnly("0123456789") ||
      !bottom.containsOnly("0123456789"))
    return std::nullopt;

  const auto n = top.getDoubleValue();
  const auto d = bottom.getDoubleValue();

  if (n <= 0.0 || d <= 0.0)
    return std::nullopt;

  return 1200.0 * std::log2(n / d);
}
} // namespace

Reading read(const juce::String &text) {
  Reading out;

  // Comments out, and the lines that are left in order. Blank lines count,
  // since the description may be one.
  juce::StringArray lines;
  lines.addLines(text);

  juce::StringArray kept;
  for (const auto &line : lines)
    if (!line.startsWithChar('!'))
      kept.add(line);

  // A file that ends without a newline after its last note gives no blank
  // line, but one ending with several gives some. Those are not notes.
  while (!kept.isEmpty() && kept[kept.size() - 1].trim().isEmpty())
    kept.remove(kept.size() - 1);

  if (kept.size() < 2) {
    out.error = "This is not a Scala file. It has no count of notes.";
    return out;
  }

  // Line 0 is the description. Line 1 the count.
  const auto countWord =
      kept[1].trim().upToFirstOccurrenceOf(" ", false, false);

  if (countWord.isEmpty() || !countWord.containsOnly("0123456789")) {
    out.error = "This is not a Scala file. Its second line is not a count of "
                "notes.";
    return out;
  }

  const auto count = countWord.getIntValue();

  if (count != 12) {
    out.error = "Overtonium reads Scala files with twelve notes to the period, "
                "the last being the period. This file has " +
                juce::String(count) + ".";
    return out;
  }

  if (kept.size() - 2 < count) {
    out.error = "This Scala file says it has " + juce::String(count) +
                " notes but lists " + juce::String(kept.size() - 2) + ".";
    return out;
  }

  ScaleTable table;
  table.cents[0] = 0.0;

  for (int i = 0; i < count; ++i) {
    const auto cents = centsOf(kept[2 + i]);

    if (!cents.has_value()) {
      out.error =
          "Note " + juce::String(i + 1) +
          " in this Scala file is not a number of cents or a ratio: \"" +
          kept[2 + i].trim() + "\".";
      return out;
    }

    if (i + 1 < count)
      table.cents[(size_t)(i + 1)] = *cents;
    else
      table.periodCents = *cents;
  }

  if (table.periodCents <= 0.0) {
    out.error = "The last note in this Scala file, where it repeats, is not "
                "above the first.";
    return out;
  }

  out.table = table;
  return out;
}

juce::String nameIn(const juce::ValueTree &state) {
  return state.getProperty(kTextProperty).toString().isNotEmpty()
             ? state.getProperty(kNameProperty).toString()
             : juce::String();
}

ScaleTable tableIn(const juce::ValueTree &state) {
  const auto reading = read(state.getProperty(kTextProperty).toString());
  return reading.table.value_or(ScaleTable{});
}

} // namespace ovt::scala
