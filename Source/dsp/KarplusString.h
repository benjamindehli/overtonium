#pragma once

#include <cstddef>
#include <vector>

namespace ovt {

/// A string the noise channel strikes, after Karplus and Strong.
///
/// A delay line one period long fed back through a one-zero lowpass, so each
/// pass round the loop is one cycle of the note and the top of the series dies
/// before the bottom does, which is what makes a struck string sound struck.
/// The noise channel's own output, after COLOUR and its envelope, is what
/// strikes it.
///
/// Tuned continuously rather than once per note. The note it plays bends,
/// glides and is slid to a new version by Adaptive while it sounds, so the
/// loop is read at a fractional delay, by third-order Lagrange interpolation,
/// that moves sample by sample across each block. The lowpass delays the loop
/// too, by an amount that depends on the pitch, and its exact delay at the
/// fundamental is taken out of the line's so the string is in tune rather
/// than flat by however much the damping is.
///
/// Where the loop gets too short to tune, at the top of the keyboard and more
/// so at low sample rates, the string fades out and leaves the noise as it
/// was, rather than playing out of tune. Below the lowest pitch it was sized
/// for it fades out too, since the line is not long enough to reach.
class KarplusString {
public:
  /// The lowest pitch the line is long enough for, unless told otherwise.
  static constexpr double kLowestHz = 10.0;

  /// Sizes the line for the host's rate, long enough for `lowestHz`. The only
  /// place that allocates. A string that only ever plays one note, as the
  /// sympathetic strings do, is sized for that note rather than for the
  /// bottom of the keyboard.
  void prepare(double sampleRate, double lowestHz = kLowestHz) noexcept;

  /// The rate it is actually being run at, which the converter can lower
  /// below the host's but never raise above it, so the line is always long
  /// enough. Called from the audio thread, and allocates nothing.
  void setRate(double rate) noexcept;

  /// Silences it, as a new note on the voice does.
  void reset() noexcept;

  /// Where the string is for the next `len` samples.
  ///
  /// @param hz            the pitch it is tuned to, moved to across the block
  /// @param decaySeconds  how long it rings, to a thousandth
  /// @param damp          0 bright to 1 dark, how quickly it loses its top
  /// @param point         0 to 1, where along it the noise strikes, from near
  ///                      the end to the middle, or below nought for a string
  ///                      nobody strikes at a point, which takes what it is
  ///                      fed as it is
  void setBlock(double hz, double decaySeconds, float damp, float point,
                int len) noexcept;

  /// One sample: the noise striking it in, the string out.
  float tick(float excitation) noexcept;

  /// How much of the string is heard rather than faded away at this pitch, 0
  /// to 1, ramped across the block as the pitch is. The noise takes the rest.
  float audibility() const noexcept { return fade; }

  /// How much of itself the loop keeps each pass. A steady sine at the
  /// string's own pitch builds up by 1 / (1 - this), which is what a string
  /// driven rather than struck has to be scaled by.
  float loopGain() const noexcept { return feedback; }

  /// Where a loop this many samples long stops being heard. Public for the
  /// tests, which check the fade against it.
  static constexpr double kFadeShortest = 8.0;
  static constexpr double kFadeLongest = 24.0;

  /// The delay the lowpass adds at this frequency, in samples, for a damping
  /// coefficient b. Public for the tests.
  static double lowpassDelay(double omega, double b) noexcept;

private:
  std::vector<float> line, struck;
  std::size_t mask = 0, write = 0;
  double sampleRate = 48000.0;
  double lowest = kLowestHz;

  double delay = 0.0, delayStep = 0.0;
  float fade = 0.0f, fadeStep = 0.0f;
  float feedback = 0.0f, feedbackStep = 0.0f, inputGain = 0.0f, b = 0.25f;
  float lowpassPrevious = 0.0f;
  int pointDelay = 1;
  bool primed = false;

  /// Whether anything has been through it since it was last cleared. Clearing
  /// is a sweep of the whole line, so a string nobody has played is not swept
  /// on every new note.
  bool touched = false;

  float read(double samples) const noexcept;
};

} // namespace ovt
