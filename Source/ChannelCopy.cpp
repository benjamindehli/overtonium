#include "ChannelCopy.h"

#include "dsp/Harmonics.h"

#include <cmath>

namespace ovt::channelcopy {

namespace {
const juce::Identifier kTag{"OvertoniumChannel"};

/// What a modulator shape is stored as when the channel follows itself, which
/// is feedback. Stored as itself rather than as its own number, so feedback
/// pasted onto another channel is that channel's feedback, where a number
/// would make the other channel follow this one. No shape has a negative
/// index, so it cannot be taken for one.
constexpr float kItself = -1.0f;

/// The partial a modulator shape follows, or -1 for a plain shape. Only the
/// pitch and amplitude shapes can follow a partial.
int followedPartial(const juce::String &suffix, int index) {
  if (suffix == params::pmShapeSuffix)
    return params::pitchSourcePartial(index);

  if (suffix == params::amShapeSuffix)
    return params::ampSourcePartial(index);

  return -1;
}

/// Nine significant digits, which always read back as the same float. JUCE
/// writes numbers in the classic locale, so the point is a point whatever
/// language the host runs in, where printf would follow the host.
juce::String exactly(float value) {
  return juce::String((double)value, 8, true);
}
} // namespace

bool isCopied(const juce::String &parameterId, juce::String &suffix) {
  // "h", two digits, "_", then the suffix.
  if (parameterId.length() < 5 || parameterId[0] != 'h' ||
      !juce::CharacterFunctions::isDigit(parameterId[1]) ||
      !juce::CharacterFunctions::isDigit(parameterId[2]) ||
      parameterId[3] != '_')
    return false;

  const auto rest = parameterId.substring(4);

  if (rest == params::muteSuffix || rest == params::soloSuffix)
    return false;

  suffix = rest;
  return true;
}

Settings read(const juce::AudioProcessorValueTreeState &apvts, int channel) {
  Settings settings;

  if (channel < 0 || channel >= kNumHarmonics)
    return settings;

  const auto prefix = params::oscParamId("", channel);

  for (auto *base : apvts.processor.getParameters()) {
    auto *param = dynamic_cast<juce::RangedAudioParameter *>(base);
    juce::String suffix;

    if (param == nullptr || !param->getParameterID().startsWith(prefix) ||
        !isCopied(param->getParameterID(), suffix))
      continue;

    const auto value = param->convertFrom0to1(param->getValue());

    settings[suffix] =
        followedPartial(suffix, juce::roundToInt(value)) == channel ? kItself
                                                                    : value;
  }

  return settings;
}

juce::String toText(const Settings &settings) {
  juce::XmlElement xml(kTag);

  for (const auto &[suffix, value] : settings)
    xml.setAttribute(suffix, exactly(value));

  return xml.toString(juce::XmlElement::TextFormat().singleLine());
}

std::optional<Settings> fromText(const juce::String &text) {
  // Whole, since JUCE's reader takes a tag that is cut off, and half a copy
  // would paste half a channel.
  if (!text.trim().endsWith("/>"))
    return std::nullopt;

  const auto xml = juce::parseXML(text);

  if (xml == nullptr || !xml->hasTagName(kTag.toString()))
    return std::nullopt;

  Settings settings;

  for (int i = 0; i < xml->getNumAttributes(); ++i) {
    const auto value = xml->getAttributeValue(i).trim();

    // getFloatValue reads "abc" as 0, so the text is checked first.
    if (value.isEmpty() || !value.containsOnly("0123456789+-.eE"))
      continue;

    settings[xml->getAttributeName(i)] = value.getFloatValue();
  }

  if (settings.empty())
    return std::nullopt;

  return settings;
}

std::vector<params::Move> movesFor(juce::AudioProcessorValueTreeState &apvts,
                                   const Settings &settings, int channel) {
  std::vector<params::Move> moves;

  if (channel < 0 || channel >= kNumHarmonics)
    return moves;

  for (const auto &[suffix, value] : settings) {
    const auto id = params::oscParamId(suffix.toRawUTF8(), channel);
    juce::String checked;

    if (!isCopied(id, checked))
      continue;

    auto *param = apvts.getParameter(id);

    if (param == nullptr)
      continue;

    const auto range = param->getNormalisableRange();
    auto plain = value;

    // Itself, on this channel. The partials follow the shapes in the list,
    // and the noise channel follows them, so the channel's own entry is that
    // far from the end.
    if (value < 0.0f &&
        (suffix == params::pmShapeSuffix || suffix == params::amShapeSuffix))
      plain = range.end - (float)(kNumHarmonics - channel);

    const auto target =
        param->convertTo0to1(juce::jlimit(range.start, range.end, plain));

    if (std::abs(target - param->getValue()) > 1.0e-7f)
      moves.emplace_back(param, target);
  }

  return moves;
}

} // namespace ovt::channelcopy
