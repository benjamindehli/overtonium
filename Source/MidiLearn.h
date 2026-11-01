#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

namespace ovt {

/// Which MIDI controller moves which parameter.
///
/// One parameter per controller number. Lookups happen on the audio thread and
/// edits on the message thread, so the table is an array of atomic parameter
/// pointers that never allocates or locks. Controller moves are not undoable.
/// See OvertoniumProcessor::audioProcessorParameterChangeGestureBegin.
class MidiLearn {
public:
  static constexpr int kNumControllers = 128;

  /// Controller numbers this will not bind because something already listens
  /// for them: the mod wheel (1), sustain (64), the MPE slide axis (74), the
  /// note stops (120, 123) and the RPN and NRPN messages an MPE controller uses
  /// to declare its zones (6, 38, 98 to 101).
  static bool isReserved(int controller) noexcept {
    switch (controller) {
    case 1:
    case 6:
    case 38:
    case 64:
    case 74:
    case 98:
    case 99:
    case 100:
    case 101:
    case 120:
    case 123:
      return true;

    default:
      return false;
    }
  }

  /// What a reserved controller is already doing, for the menu.
  static const char *reservedFor(int controller) noexcept;

  // ---- the message thread's side -----------------------------------------

  /// Waits for the next controller that is not reserved and binds it to this
  /// parameter. Passing nullptr cancels.
  void arm(juce::RangedAudioParameter *parameter) noexcept {
    arming.store(parameter, std::memory_order_release);
  }

  /// The parameter waiting for a controller, or nullptr.
  juce::RangedAudioParameter *armed() const noexcept {
    return arming.load(std::memory_order_acquire);
  }

  /// Which controller moves this parameter, or -1.
  int controllerFor(const juce::RangedAudioParameter *parameter) const noexcept;

  /// Unbinds every controller pointing at this parameter.
  void forget(const juce::RangedAudioParameter *parameter) noexcept;

  /// Binds a controller to a parameter, releasing whatever it moved before.
  void bind(int controller, juce::RangedAudioParameter *parameter) noexcept;

  /// Unbinds every controller.
  void clear() noexcept;

  // ---- the audio thread's side -------------------------------------------

  /// Takes the message if this map has a use for it.
  ///
  /// @return true when the controller was bound or has just been learned, in
  ///         which case the caller must not pass it on.
  bool handle(int controller, int value) noexcept;

  // ---- what the host saves ------------------------------------------------

  juce::ValueTree toTree() const;

  /// @param owner  where the parameters are looked up by id. An id that no
  ///               longer exists is dropped, since a session saved by a later
  ///               build may name controls this one does not have.
  void fromTree(const juce::ValueTree &tree, juce::AudioProcessor &owner);

  static const juce::Identifier kTreeType;
  static const juce::Identifier kBinding;
  static const juce::Identifier kController;
  static const juce::Identifier kParameter;

private:
  std::array<std::atomic<juce::RangedAudioParameter *>, kNumControllers>
      bound{};
  std::atomic<juce::RangedAudioParameter *> arming{nullptr};
};

} // namespace ovt
