#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <atomic>

#include "Reverb.h"
#include "TapeEcho.h"
#include "Wobble.h"
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

  // ---- notes that own a channel each ---------------------------------------
  //
  // For a controller that gives every note its own MIDI channel so it can bend
  // and press each one separately. The channel is part of the note's identity
  // here, which is the whole difference: the same key can be down twice at
  // once on two channels, bent in two directions, and the two are different
  // voices rather than one retriggering the other.
  //
  // These sit alongside the ordinary entry points rather than replacing them,
  // and the two can be in use at the same time. A voice started by the calls
  // above belongs to no channel and is only ever found by them, so an ordinary
  // keyboard playing at the same time can neither steal nor stop one of these.

  /// @param channel  1 to 16, and never 0, which is what an ordinary note uses.
  void noteOnPerNote(int channel, int note, float velocity,
                     const SynthParams &p) noexcept;
  void noteOffPerNote(int channel, int note, float velocity = 0.5f) noexcept;
  void setNotePressure(int channel, int note, float pressure) noexcept;

  /// Per-note slide, routed like per-note pressure.
  void setNoteSlide(int channel, int note, float slide) noexcept;

  /// Bend belonging to one note, in semitones, on top of the wheel.
  void setNoteBend(int channel, int note, float semitones) noexcept;

  void allNotesOff() noexcept; ///< graceful release (MIDI CC 123)
  void
  allSoundOff() noexcept; ///< immediate silence (MIDI CC 120 / transport stop)

  /// Overwrites both channels with the rendered mix.
  void render(float *left, float *right, const ChannelTaps &taps,
              int numSamples,
              const SynthParams &p) noexcept;

  /// The stereo mix alone, for a host that asked for no channel outputs.
  void render(float *left, float *right, int numSamples,
              const SynthParams &p) noexcept {
    render(left, right, ChannelTaps{}, numSamples, p);
  }

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

  // ---- what the strip lamps show -------------------------------------------
  //
  // All taken from the same voice that produced the meter reading for that
  // partial, which is the loudest one. Reading each from whichever voice
  // happened to be furthest through its own envelope would have the lamps
  // describing a note you cannot pick out of the chord.

  /// Envelope position, signed negative once the key-off stage has taken over.
  float getPartialEnvelope(int index0) const noexcept {
    return partialEnvelopes[(size_t)index0].load(std::memory_order_relaxed);
  }

  /// How far the tremolo has pulled this partial down, 0 to 1.
  float getPartialTremolo(int index0) const noexcept {
    return partialTremolos[(size_t)index0].load(std::memory_order_relaxed);
  }

  /// Pitch displacement in cents, modulation and drift together.
  float getPartialPitch(int index0) const noexcept {
    return partialPitches[(size_t)index0].load(std::memory_order_relaxed);
  }

  float getNoiseEnvelope() const noexcept {
    return noiseEnvelope.load(std::memory_order_relaxed);
  }

  float getNoiseTremolo() const noexcept {
    return noiseTremolo.load(std::memory_order_relaxed);
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
  /// Is this the voice holding that note on that channel?
  ///
  /// Channel 0 means an ordinary keyboard note, which belongs to no channel,
  /// so the same comparison serves both kinds and keeps them from finding each
  /// other. See the per-note entry points above.
  static bool matches(const Voice &v, int channel, int note) noexcept;

  void noteOnImpl(int channel, int note, float velocity,
                  const SynthParams &p) noexcept;
  void noteOffImpl(int channel, int note, float velocity) noexcept;

  Voice *findFreeVoice() noexcept;
  Voice *findOldestSounding() noexcept;

  /// The voice it costs least to take outright, for when the pool has nothing
  /// free and the new note cannot wait for a fade.
  Voice *findQuietestExpendable() noexcept;
  int countSounding() const noexcept;

  /// The voice pool and the lo-fi converter, which are the same stage.
  ///
  /// Overwrites both channels and publishes the partial meters.
  void renderVoices(float *left, float *right, const ChannelTaps &taps,
                    int numSamples,
                    const SynthParams &p) noexcept;

  /// What one render pass found worth showing, before it is published.
  ///
  /// Gathered rather than stored straight into the atomics because the lo-fi
  /// path calls sumVoices several times for one block, and a lamp should show
  /// where the block ended rather than flickering through its chunks.
  struct Activity {
    std::array<float, kNumHarmonics> peaks{};
    std::array<float, kNumHarmonics> envelopes{};
    std::array<float, kNumHarmonics> tremolos{};
    std::array<float, kNumHarmonics> pitches{};

    float noisePeak = 0.0f;
    float noiseEnvelope = 0.0f;
    float noiseTremolo = 0.0f;
  };

  /// Sums every sounding voice into the buffers and takes the meter peaks.
  void sumVoices(float *left, float *right, const ChannelTaps &taps,
                 int numFrames, const SynthParams &p,
                 Activity &into) noexcept;

  void publish(const Activity &) noexcept;

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

  /// Low-rate scratch for the per-channel outputs, and what each is holding
  /// between frames.
  ///
  /// The converter renders the whole pool slowly and holds the result, so a
  /// tap has to be held the same way or it would run at a different rate from
  /// the mix it belongs to. One buffer per channel, sized like the mix's, and
  /// only the ones a host actually asked for are touched.
  std::array<std::array<float, kLofiChunk>, kNumHarmonics + 1> lofiTap{};
  std::array<float, kNumHarmonics + 1> heldTap{};

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
  std::array<std::atomic<float>, kNumHarmonics> partialEnvelopes{};
  std::array<std::atomic<float>, kNumHarmonics> partialTremolos{};
  std::array<std::atomic<float>, kNumHarmonics> partialPitches{};
  std::atomic<float> noiseLevel{0.0f};
  std::atomic<float> noiseEnvelope{0.0f};
  std::atomic<float> noiseTremolo{0.0f};
  std::atomic<float> outputLevelL{0.0f};
  std::atomic<float> outputLevelR{0.0f};

  float smoothedMasterGain = -1.0f;

  // The master effects. They sit after the voices and before the master fader,
  // so the fader is a true output level and moving it cannot change the wet to
  // dry balance underneath it.
  //
  // Wobble is first of the three. It bends the instrument itself, so the echo
  // repeats what the wobble did rather than adding a wobble of their own,
  // which is the difference between a warped record being played and a warped
  // recording of one.
  Wobble wobble;
  TapeEcho echo;
  Reverb reverb;
};

} // namespace ovt
