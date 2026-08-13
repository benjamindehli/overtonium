#pragma once

#include <array>
#include <cstdint>

#include <atomic>

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

  /// Routes polyphonic aftertouch to whichever voices are holding that note.
  void setPolyPressure(int note, float pressure) noexcept;

  void allNotesOff() noexcept; ///< graceful release (MIDI CC 123)
  void
  allSoundOff() noexcept; ///< immediate silence (MIDI CC 120 / transport stop)

  /// Overwrites both channels with the rendered mix.
  void render(float *left, float *right, int numSamples,
              const SynthParams &p) noexcept;

  int getActiveVoiceCount() const noexcept;

  /// Loudest instance of each partial across the sounding voices, 0..1.
  ///
  /// The loudest rather than the sum, so the display shows the shape of the
  /// patch and does not simply pin itself the moment you play a chord.
  float getPartialLevel(int index0) const noexcept {
    return partialLevels[(size_t)index0].load(std::memory_order_relaxed);
  }

  /// Peak of the finished stereo output, after master gain and the clipper, so
  /// it reports what actually leaves the plugin.
  float getOutputLevel() const noexcept {
    return outputLevel.load(std::memory_order_relaxed);
  }

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
  std::array<std::atomic<float>, kNumHarmonics> partialLevels{};
  std::atomic<float> outputLevel{0.0f};

  float smoothedMasterGain = -1.0f;
};

} // namespace ovt
