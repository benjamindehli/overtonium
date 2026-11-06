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
/// The pitch can move while the note sounds, so the loop is read at a
/// fractional delay by third-order Lagrange interpolation, moved sample by
/// sample across each block. The lowpass's own delay at the fundamental is
/// taken out of the line's so the string stays in tune.
///
/// Where the loop gets too short to tune, or the pitch falls below what the
/// line was sized for, the string fades out and leaves the noise as it was.
class KarplusString {
public:
  /// The lowest pitch the line is long enough for, unless told otherwise.
  static constexpr double kLowestHz = 10.0;

  /// Sizes the line for the host's rate, long enough for `lowestHz`. The only
  /// place that allocates.
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
  ///
  /// With `stiff` set, the string's overtones are spread the way STRETCH
  /// spreads the partials: mode k at k * hz * sqrt(1 + stiffness * k^2). Two
  /// allpasses in the loop do it. See kStiffSections.
  void setBlock(double hz, double decaySeconds, float damp, float point,
                int len, double stiffness = 0.0) noexcept;

  /// Whether the string can be made stiff. Off, it is as it always was, which
  /// is what the noise channel's is. On, the allpasses are only in the loop
  /// while it has stiffness, so a string without any costs what it did, and
  /// they go in and out without a trace: with no coefficient they are a delay
  /// of a sample each, and the line is moved back or on by as much, so the
  /// loop is the same sample for sample. Set before prepare.
  void setStiffable(bool on) noexcept { stiffable = on; }

  /// How many first-order allpasses a stiff string has in its loop. Fitted so
  /// its first and kStiffFitMode-th modes land exactly where the law puts
  /// them. Two keep the first sixteen within a third of a cent of the law at
  /// 24 cents of STRETCH, where the plain string misses them by up to six,
  /// and within about two and a half at 80. Past that the upper ones drift
  /// further, and a low string cannot be squeezed much at all, since an
  /// allpass can add at most a sample of delay to the top of the loop.
  static constexpr int kStiffSections = 2;
  static constexpr int kStiffFitMode = 16;

  /// The allpass coefficient that gives a string at `hz` its stiffness, for
  /// the tests. Nought for none.
  double stiffCoefficient() const noexcept { return allpass; }

  /// The phase delay of a first-order allpass with coefficient a, in samples.
  static double allpassDelay(double omega, double a) noexcept;

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

  bool stiffable = false;

  /// Whether the allpasses are in the loop now.
  bool stiffOn = false;

  /// Puts the allpasses into the loop, or takes them out, at a coefficient of
  /// nought, where either leaves the loop exactly as it was.
  void engageStiffness() noexcept;
  void disengageStiffness() noexcept;

  /// The allpasses' coefficient, moved across the block, and each one's last
  /// input and output.
  double allpass = 0.0;
  float apCoeff = 0.0f, apStep = 0.0f;
  float apIn[kStiffSections] = {}, apOut[kStiffSections] = {};

  /// The coefficient for a string at `hz` with this stiffness, found by
  /// bisection. Only worked out again when either changes.
  double fitStiffness(double hz, double stiffness, double longest) noexcept;
  double fittedHz = -1.0, fittedStiffness = 0.0;
  bool primed = false;

  /// Whether anything has been through it since it was last cleared. Clearing
  /// is a sweep of the whole line, so a string nobody has played is not swept
  /// on every new note.
  bool touched = false;

  float read(double samples) const noexcept;
};

} // namespace ovt
