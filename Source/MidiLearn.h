#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

namespace ovt {

/// Which controller moves which control.
///
/// One parameter per controller number, so a knob can be reached from more
/// than one controller but a controller only ever moves one knob. The other
/// way round would need a list per controller and a rule for what happens
/// when two knobs disagree about where the controller is.
///
/// **Threads.** Lookups happen on the audio thread as the messages arrive and
/// edits happen on the message thread as menus are chosen, so the table is an
/// array of atomic parameter pointers rather than anything that allocates or
/// locks. The pointers are safe to hold: an AudioProcessor builds its
/// parameters once in its constructor and they outlive everything here.
///
/// **What it does not do.** A controller moving a knob is not an edit anybody
/// can undo, for the same reason automation is not: it arrives on the audio
/// thread, where this codebase deliberately ignores gestures. See
/// OvertoniumProcessor::audioProcessorGestureChanged.
class MidiLearn {
public:
  static constexpr int kNumControllers = 128;

  /// Controller numbers this will not bind, because something is already
  /// listening for them and losing it would be felt while playing rather than
  /// noticed later.
  ///
  /// 1 is the mod wheel, which feeds aftertouch. 64 is the sustain pedal. 74
  /// is the slide axis, which is how an MPE controller phrases a note and is
  /// the one most likely to be reached for by accident, since it moves
  /// whenever a finger does. 120 and 123 stop notes. The rest are how an MPE
  /// controller declares its zones: 100 and 101 select the registered
  /// parameter and 6 and 38 carry its value, 98 and 99 the unregistered pair.
  /// Binding any of those would leave the instrument unable to be told what
  /// the keyboard is.
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

  /// What that controller is already doing, for a menu that has to say why it
  /// will not take it.
  static const char *reservedFor(int controller) noexcept;

  // ---- the message thread's side -----------------------------------------

  /// Waits for the next controller that is not reserved and binds it to this
  /// parameter. Passing nullptr cancels.
  void arm(juce::RangedAudioParameter *parameter) noexcept {
    arming.store(parameter, std::memory_order_release);
  }

  juce::RangedAudioParameter *armed() const noexcept {
    return arming.load(std::memory_order_acquire);
  }

  /// Which controller moves this parameter, or -1.
  int controllerFor(const juce::RangedAudioParameter *parameter) const noexcept;

  /// Unbinds every controller pointing at this parameter.
  void forget(const juce::RangedAudioParameter *parameter) noexcept;

  void bind(int controller, juce::RangedAudioParameter *parameter) noexcept;

  void clear() noexcept;

  // ---- the audio thread's side -------------------------------------------

  /// Takes the message if it is one this map has a use for.
  ///
  /// @return true when the controller was bound or has just been learned, in
  ///         which case the caller must not pass it on: a controller moving a
  ///         knob is not also a mod wheel.
  bool handle(int controller, int value) noexcept;

  // ---- what the host saves ------------------------------------------------

  juce::ValueTree toTree() const;

  /// @param owner  where the parameters come from, since a tree stores the id
  ///               a binding was made against rather than a pointer. An id
  ///               that no longer exists is dropped rather than refused: a
  ///               session saved by a later build may name controls this one
  ///               has never heard of.
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
