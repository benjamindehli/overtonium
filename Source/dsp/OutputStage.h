#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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

  /// One half of the wave leans over before the other, the way a single-ended
  /// stage does. The asymmetry is the whole of it: where Soft and Hard fold
  /// the two halves alike and give odd harmonics, this gives even ones, which
  /// is a different kind of loud rather than more of it.
  ///
  /// Named for what it does to the wave rather than for the bias that makes
  /// it do that, which is the word BusDrive and Character already use, there
  /// for a property several circuits have rather than for a machine of its
  /// own.
  Asymmetric,

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
  case ClipType::Asymmetric:
    return "Asymmetric";
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

/// What the bar's button says, which is the name itself for three of the five.
///
/// Asymmetric has to be shortened. The button is 62 px wide and gives a label
/// 46 of them, and in that button's own font ASYMMETRIC is 54.3. The ten
/// pixels it would take have an owner: the converter's two readouts share the
/// row and are already at the 66 px they need to name their units, so the
/// button growing is those readouts shrinking. ASYM is 23.
///
/// Limiter is shortened because the other four are short. It fits at 32.6 and
/// could stay, but SOFT, HARD, ASYM, LIMIT and FOLD read as one set of
/// choices where a single long word among them reads as the odd one.
///
/// Only the button abbreviates. The menu it opens, the name the host shows on
/// the automation lane and the documentation all have room for the word.
inline const char *clipTypeShortName(ClipType t) {
  switch (t) {
  case ClipType::Asymmetric:
    return "Asym";
  case ClipType::Limiter:
    return "Limit";

  case ClipType::Soft:
  case ClipType::Hard:
  case ClipType::Fold:
  case ClipType::NumTypes:
    break;
  }

  return clipTypeName(t);
}

/// The output stage, which is stateful because one of the five is.
///
/// The four shapers are memoryless and could be free functions. The limiter
/// carries a gain that walks towards what the signal needs, and that gain is
/// shared between the two channels rather than kept per channel: a limiter
/// that ducked one side on its own would pull the stereo image towards the
/// quieter one every time something loud happened on the other.
///
/// The whole stage is late by kLookaheadSeconds, every type and no type
/// alike. That is the limiter's doing and the rest pay it, for the reason
/// BusDrive::kLatency gives: a stage whose latency depended on the patch would
/// have the host re-plan its graph every time a preset was chosen. So the
/// delay is unconditional and the four memoryless shapers run behind it,
/// which costs them nothing since delaying a memoryless curve and curving a
/// delayed signal are the same thing.
class OutputStage {
public:
  /// How far ahead the limiter is allowed to look, and therefore how late
  /// everything leaving here is.
  ///
  /// Two milliseconds. Measured against the presets that drive the limiter
  /// hardest, where the stage without it reached 1.294 and handed 1741 samples
  /// in two seconds to the hard clip behind it. One millisecond already takes
  /// that to none, and the rest of the choice is how gently the gain moves:
  /// 0.0041 per sample as it was, 0.0040 at one millisecond, 0.0020 at two and
  /// 0.0007 at five. Two is where the clipping is gone and the gain has
  /// visibly settled, and five would cost three more milliseconds of latency
  /// to improve something already below where it can be heard.
  static constexpr double kLookaheadSeconds = 0.002;

  /// The delay in samples at a given rate, which the processor has to be able
  /// to ask for before anything has been prepared.
  static int lookaheadSamples(double sampleRate) noexcept {
    return (int)(kLookaheadSeconds * std::max(1.0, sampleRate) + 0.5);
  }

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// How late this stage is, in samples, at the rate it was prepared with.
  int latency() const noexcept { return delay; }

  /// Shapes both channels in place, and delays them whether or not it shapes.
  ///
  /// @param type     which of the five.
  /// @param shaping  false to pass the audio through at the same delay and
  ///                 otherwise leave it alone, which is the stage switched
  ///                 off rather than a sixth type.
  void process(float *left, float *right, int numSamples, ClipType type,
               bool shaping) noexcept;

private:
  /// The gain the limiter wants for the sample about to arrive, from the
  /// loudest of the window still sitting in the delay line.
  float gainFor(float peak) noexcept;

  float delayed(std::vector<float> &line, float in) noexcept;

  double rate = 48000.0;

  /// Samples of delay, and the two halves the gain path spends it on. A
  /// causal box of B samples carries its output back by (B - 1) / 2, so a
  /// window of W followed by two of them costs (W - 1) + (B - 1), which at
  /// W = B = delay / 2 is delay - 2 and arrives with a sample in hand.
  int delay = 0;
  int window = 1;
  int box = 1;

  std::vector<float> lineLeft, lineRight;
  int writeIndex = 0;

  /// A monotonic wedge over the window: values decreasing from the front, so
  /// the front is the loudest sample still to be heard. Kept as a ring rather
  /// than a deque so that nothing allocates once this is running.
  std::vector<float> wedgeValue;

  /// Counted rather than wrapped, so a sample's place in the window is one
  /// comparison. Sixty-four bits because thirty-two of them is twelve hours
  /// at forty-eight kilohertz, and long is thirty-two bits on Windows.
  std::vector<std::int64_t> wedgeIndex;
  int wedgeHead = 0, wedgeTail = 0;
  std::int64_t seen = 0;

  /// The two box filters, each a ring and a running sum, and the reciprocal
  /// of their length so that the average is a multiply.
  double boxScale = 1.0;
  std::vector<float> boxOne, boxTwo;
  int boxOneAt = 0, boxTwoAt = 0;
  double boxOneSum = 0.0, boxTwoSum = 0.0;

  /// What the gain has fallen to before the boxes smooth it, and the
  /// coefficient it recovers by.
  float held = 1.0f;
  float release = 0.0f;
};

} // namespace ovt
