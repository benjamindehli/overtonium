#include "MidiLearn.h"

namespace ovt {

const juce::Identifier MidiLearn::kTreeType{"midiLearn"};
const juce::Identifier MidiLearn::kBinding{"binding"};
const juce::Identifier MidiLearn::kController{"cc"};
const juce::Identifier MidiLearn::kParameter{"param"};

const char *MidiLearn::reservedFor(int controller) noexcept {
  switch (controller) {
  case 1:
    return "the mod wheel";
  case 64:
    return "the sustain pedal";
  case 74:
    return "the slide axis";
  case 120:
  case 123:
    return "stopping notes";
  case 6:
  case 38:
  case 98:
  case 99:
  case 100:
  case 101:
    return "how a controller declares its MPE zones";

  default:
    return "";
  }
}

int MidiLearn::controllerFor(
    const juce::RangedAudioParameter *parameter) const noexcept {
  if (parameter == nullptr)
    return -1;

  for (int cc = 0; cc < kNumControllers; ++cc)
    if (bound[(size_t)cc].load(std::memory_order_acquire) == parameter)
      return cc;

  return -1;
}

void MidiLearn::forget(const juce::RangedAudioParameter *parameter) noexcept {
  if (parameter == nullptr)
    return;

  for (auto &slot : bound)
    if (slot.load(std::memory_order_acquire) == parameter)
      slot.store(nullptr, std::memory_order_release);
}

void MidiLearn::bind(int controller,
                     juce::RangedAudioParameter *parameter) noexcept {
  if (!juce::isPositiveAndBelow(controller, kNumControllers) ||
      isReserved(controller))
    return;

  // One controller moves one control, so whatever this one moved before lets
  // go. Binding the same parameter to a second controller is left alone: two
  // ways to reach one knob is a thing somebody might want, and neither can
  // contradict the other.
  bound[(size_t)controller].store(parameter, std::memory_order_release);
}

void MidiLearn::clear() noexcept {
  for (auto &slot : bound)
    slot.store(nullptr, std::memory_order_release);

  arming.store(nullptr, std::memory_order_release);
}

bool MidiLearn::handle(int controller, int value) noexcept {
  if (!juce::isPositiveAndBelow(controller, kNumControllers))
    return false;

  // Reserved ones are not learned and not taken, whether or not anything is
  // waiting. Somebody arming a knob and then reaching for the slide to steady
  // a note should find the note steadied and the knob still waiting.
  if (isReserved(controller))
    return false;

  if (auto *waiting = arming.exchange(nullptr, std::memory_order_acq_rel)) {
    bound[(size_t)controller].store(waiting, std::memory_order_release);

    // Not moved by the message that bound it. A controller is wherever it
    // happens to be standing when you touch it, and jumping the knob there is
    // a surprise at the exact moment somebody is watching it.
    return true;
  }

  auto *parameter = bound[(size_t)controller].load(std::memory_order_acquire);

  if (parameter == nullptr)
    return false;

  // Straight to where the controller is, rather than picking the value up as
  // it passes. Takeover needs somewhere to show that it is waiting, and there
  // is nowhere on a knob this small to show it.
  parameter->setValueNotifyingHost((float)value / 127.0f);

  return true;
}

juce::ValueTree MidiLearn::toTree() const {
  juce::ValueTree tree{kTreeType};

  for (int cc = 0; cc < kNumControllers; ++cc) {
    auto *parameter = bound[(size_t)cc].load(std::memory_order_acquire);

    if (parameter == nullptr)
      continue;

    juce::ValueTree binding{kBinding};
    binding.setProperty(kController, cc, nullptr);
    binding.setProperty(kParameter, parameter->paramID, nullptr);
    tree.appendChild(binding, nullptr);
  }

  return tree;
}

void MidiLearn::fromTree(const juce::ValueTree &tree,
                         juce::AudioProcessor &owner) {
  clear();

  if (!tree.hasType(kTreeType))
    return;

  // Built once rather than searched per binding, since a parameter list this
  // long turns a handful of bindings into a few thousand comparisons.
  std::map<juce::String, juce::RangedAudioParameter *> byId;

  for (auto *raw : owner.getParameters())
    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(raw))
      byId[ranged->paramID] = ranged;

  for (const auto &binding : tree) {
    if (!binding.hasType(kBinding))
      continue;

    const int cc = binding.getProperty(kController, -1);
    const auto id = binding.getProperty(kParameter, juce::String()).toString();

    const auto found = byId.find(id);

    if (found != byId.end())
      bind(cc, found->second);
  }
}

} // namespace ovt
