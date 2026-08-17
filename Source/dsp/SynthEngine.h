#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <atomic>

#include "Reverb.h"
#include "TapeEcho.h"
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
  /// @param velocity  how fast the key came up, 0 to 1.
  void noteOff(int note, float velocity = 0.5f) noexcept;
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

  /// Loudest instance of the noise channel across the sounding voices.
  float getNoiseLevel() const noexcept {
    return noiseLevel.load(std::memory_order_relaxed);
  }

  /// Peak of the finished output per channel, after master gain and the
  /// clipper, so it reports what actually leaves the plugin. Kept separate
  /// because stereo spread is the whole reason the two can differ.
  float getOutputLevelLeft() const noexcept {
    return outputLevelL.load(std::memory_order_relaxed);
  }

  float getOutputLevelRight() const noexcept {
    return outputLevelR.load(std::memory_order_relaxed);
  }

  /// How long the master effects would take to fall silent under the settings
  /// they are holding, so the host can pad an offline bounce correctly.
  float effectsTailSeconds(const SynthParams &p) const noexcept {
    return std::max(echo.tailSeconds(p.echo), reverb.tailSeconds(p.reverb));
  }

private:
  Voice *findFreeVoice() noexcept;
  Voice *findOldestSounding() noexcept;
  int countSounding() const noexcept;

  /// The voice pool and the lo-fi converter, which are the same stage.
  ///
  /// Overwrites both channels and publishes the partial meters.
  void renderVoices(float *left, float *right, int numSamples,
                    const SynthParams &p) noexcept;

  /// Sums every sounding voice into the buffers and takes the meter peaks.
  void sumVoices(float *left, float *right, int numFrames, const SynthParams &p,
                 std::array<float, kNumHarmonics> &peaks,
                 float &noisePeak) noexcept;

  /// Moves the whole pool to a new render rate, if it is not there already.
  void setRenderRate(double rate) noexcept;

  /// The render rate the settings ask for, clamped to something the host can
  /// actually carry. Equal to the host rate when the setting is off.
  double lofiRenderRate(const SynthParams &p) const noexcept;

  std::array<Voice, kPoolSize> voices{};
  std::array<bool, kPoolSize> heldBySustain{};

  /// How much of a block the reduced-rate path takes at a time. Fixed, so the
  /// scratch is a member and the audio thread never allocates, and large
  /// enough that the per-chunk setup is lost in the noise.
  static constexpr int kLofiChunk = 512;

  std::array<float, kLofiChunk> lofiScratchL{};
  std::array<float, kLofiChunk> lofiScratchR{};

  /// Where the reduced rate has got to between host samples, carried across
  /// blocks so the hold pattern does not restart every buffer.
  double resamplePhase = 1.0;
  float heldL = 0.0f, heldR = 0.0f;

  /// What the pool is currently rendering at, which is the host rate unless
  /// the lo-fi setting says otherwise.
  double renderRate = 44100.0;

  double sampleRate = 44100.0;
  int polyphony = 8;
  uint64_t ageCounter = 0;
  bool sustainDown = false;

  /// Negative == "snap to target on the next block".
  std::array<std::atomic<float>, kNumHarmonics> partialLevels{};
  std::atomic<float> noiseLevel{0.0f};
  std::atomic<float> outputLevelL{0.0f};
  std::atomic<float> outputLevelR{0.0f};

  float smoothedMasterGain = -1.0f;

  // The master effects. They sit after the voices and before the master fader,
  // so the fader is a true output level and moving it cannot change the wet to
  // dry balance underneath it.
  TapeEcho echo;
  Reverb reverb;
};

} // namespace ovt
