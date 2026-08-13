#pragma once

#include <array>
#include <cstdint>

#include "Voice.h"

namespace ovt {

/// Polyphonic voice pool.
///
/// The pool is deliberately larger than the maximum user-selectable polyphony:
/// when a note is stolen the old voice is handed a 4 ms fade and keeps
/// rendering out of the surplus, so stealing never clicks.
class SynthEngine {
public:
  static constexpr int kMaxPolyphony = 16;
  static constexpr int kPoolSize = kMaxPolyphony + 8;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setPolyphony(int n) noexcept;
  int getPolyphony() const noexcept { return polyphony; }

  void noteOn(int note, float velocity, const SynthParams &p) noexcept;
  void noteOff(int note) noexcept;
  void setSustainPedal(bool down) noexcept;

  void allNotesOff() noexcept; ///< graceful release (MIDI CC 123)
  void
  allSoundOff() noexcept; ///< immediate silence (MIDI CC 120 / transport stop)

  /// Overwrites both channels with the rendered mix.
  void render(float *left, float *right, int numSamples,
              const SynthParams &p) noexcept;

  int getActiveVoiceCount() const noexcept;

private:
  Voice *findFreeVoice() noexcept;
  Voice *findOldestSounding() noexcept;
  int countSounding() const noexcept;

  std::array<Voice, kPoolSize> voices{};
  std::array<bool, kPoolSize> heldBySustain{};

  double sampleRate = 44100.0;
  int polyphony = 8;
  uint64_t ageCounter = 0;
  bool sustainDown = false;

  /// Negative == "snap to target on the next block".
  float smoothedMasterGain = -1.0f;
};

} // namespace ovt
