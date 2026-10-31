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

  // A controller moves one parameter, so this replaces its old binding.
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

  // Reserved controllers are neither learned nor taken, even while armed.
  if (isReserved(controller))
    return false;

  if (auto *waiting = arming.exchange(nullptr, std::memory_order_acq_rel)) {
    bound[(size_t)controller].store(waiting, std::memory_order_release);

    // The binding message does not move the knob, so it does not jump to
    // wherever the controller happened to be.
    return true;
  }

  auto *parameter = bound[(size_t)controller].load(std::memory_order_acquire);

  if (parameter == nullptr)
    return false;

  // Jumps straight to the controller value. There is no room on the knob to
  // show a soft takeover waiting.
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

  // Built once rather than searched per binding.
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
