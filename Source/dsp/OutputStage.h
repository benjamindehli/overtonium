#pragma once

#include <algorithm>
#include <cmath>

namespace ovt {

/// What the mixer runs into on its way out.
///
/// It began as one soft clipper with a switch, there to stop the instrument
/// handing a host something louder than it asked for. Five of them now, which
/// makes it part of the sound rather than only a guard: the master fader sits
/// in front of this, so how hard you drive it is a thing a patch decides.
///
/// Every one of them is bounded by one. That is not a coincidence of the
/// shapes chosen but the point of the stage, and there is a test on it: a
/// safety clipper that could be driven past unity would not be one.
enum class ClipType {
  /// Linear to seven tenths, then a tanh knee. Gradual, symmetric, odd
  /// harmonics arriving as you lean on it. What every patch had before there
  /// was a choice, so it is first and it is the default.
  Soft,

  /// A brick wall at unity. Nothing happens until it does, which is the sound
  /// of a converter with no headroom left: abrupt, bright, and the same odd
  /// harmonics as Soft but all at once.
  Hard,

  /// Biased so one half of the wave leans over before the other, the way a
  /// single-ended stage does. The asymmetry is the whole of it: where Soft and
  /// Hard fold the two halves alike and give odd harmonics, this gives even
  /// ones, which is a different kind of loud rather than more of it.
  Bias,

  /// Gain reduction rather than waveshaping. It does not distort at all: it
  /// turns the whole signal down as it approaches the ceiling and lets it back
  /// up afterwards, so what you hear is the level moving rather than the
  /// shape. The only one here that a mastering engineer would recognise.
  Limiter,

  /// Past the threshold the wave turns back on itself instead of flattening.
  /// Not protection that happens to colour, but an effect that happens to be
  /// bounded: it generates far more harmonics than the others and they move
  /// with the level, so it sings rather than crunches.
  Fold,

  NumTypes
};

inline const char *clipTypeName(ClipType t) {
  switch (t) {
  case ClipType::Soft:
    return "Soft";
  case ClipType::Hard:
    return "Hard";
  case ClipType::Bias:
    return "Bias";
  case ClipType::Limiter:
    return "Limiter";
  case ClipType::Fold:
    return "Fold";

  // Listed rather than left to a default, so adding one is a compiler error
  // here until it has a name.
  case ClipType::NumTypes:
    break;
  }

  return "Soft";
}

/// The output stage, which is stateful because one of the five is.
///
/// The four shapers are memoryless and could be free functions. The limiter
/// carries a gain that walks towards what the signal needs, and that gain is
/// shared between the two channels rather than kept per channel: a limiter
/// that ducked one side on its own would pull the stereo image towards the
/// quieter one every time something loud happened on the other.
class OutputStage {
public:
  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Shapes both channels in place.
  ///
  /// @param type  which of the five.
  void process(float *left, float *right, int numSamples,
               ClipType type) noexcept;

  /// How much the limiter is pulling down, 0 to 1, for a meter to read. One
  /// for every other type, which do not pull down at all.
  float reduction() const noexcept { return gain; }

private:
  void processLimiter(float *left, float *right, int numSamples) noexcept;

  double rate = 48000.0;

  /// The limiter's current gain, and the two coefficients it moves by.
  float gain = 1.0f;
  float attack = 0.0f;
  float release = 0.0f;
};

} // namespace ovt
