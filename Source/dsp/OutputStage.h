#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Cassette.h"
#include "Console.h"

namespace ovt {

/// What the mixer runs into on its way out.
///
/// Seven types, each bounded by one, so the stage is also a safety clipper.
/// The master fader sits in front of it, so how hard it is driven is part of
/// the patch.
enum class ClipType {
  /// Linear to seven tenths, then a tanh knee. Symmetric, odd harmonics. The
  /// default.
  Soft,

  /// A brick wall at unity. Abrupt and bright, the same odd harmonics as Soft
  /// but all at once.
  Hard,

  /// One half of the wave bends over before the other, like a single-ended
  /// stage, which gives even harmonics where Soft and Hard give odd ones.
  Asymmetric,

  /// Gain reduction rather than waveshaping. It turns the whole signal down
  /// as it approaches the ceiling and lets it back up afterwards.
  Limiter,

  /// Past the threshold the wave turns back on itself instead of flattening,
  /// which gives many more harmonics that move with the level.
  Fold,

  /// A console's mix bus, measured from a real chain: transformer, bus
  /// compressor and tape stage, into the limiter with its ceiling at
  /// -0.1 dBFS. Unity for a quiet 1 kHz tone. See Console.
  Console,

  /// A four-track cassette recorder at 9.5 cm/s with Dolby B, measured from a
  /// real one, into the limiter with its ceiling at -0.1 dBFS. Unity for a
  /// quiet 1 kHz tone. See Cassette.
  Cassette,

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
  case ClipType::Console:
    return "Console";
  case ClipType::Cassette:
    return "Cassette";

  // Listed rather than left to a default, so adding one is a compiler error
  // here until it has a name.
  case ClipType::NumTypes:
    break;
  }

  return "Soft";
}

/// What the bar's button says. Some names are shortened to fit the 62 px
/// button and to read as one set: SOFT, HARD, ASYM, LIMIT, FOLD, CNSL, CASS.
/// The menu, the host and the documentation use the full names.
inline const char *clipTypeShortName(ClipType t) {
  switch (t) {
  case ClipType::Asymmetric:
    return "Asym";
  case ClipType::Limiter:
    return "Limit";
  case ClipType::Console:
    return "Cnsl";
  case ClipType::Cassette:
    return "Cass";

  case ClipType::Soft:
  case ClipType::Hard:
  case ClipType::Fold:
  case ClipType::NumTypes:
    break;
  }

  return clipTypeName(t);
}

/// The output stage. Stateful because the limiter, Console and Cassette
/// are.
///
/// The limiter's gain is shared between the two channels, so ducking one
/// side does not pull the stereo image towards the other.
///
/// The whole stage is late by kLookaheadSeconds for every type, so that the
/// latency the host sees does not depend on the patch (see
/// BusDrive::kLatency). A session setting can turn the lookahead off, and
/// then the stage has no latency at all. Switching does not allocate.
class OutputStage {
public:
  /// How far ahead the limiter looks, and so how late everything leaving here
  /// is. Two milliseconds is enough for the hardest presets to stop reaching
  /// the hard clip behind it and lets the gain move gently.
  static constexpr double kLookaheadSeconds = 0.002;

  /// The delay in samples at a given rate. The processor has to be able to
  /// ask for it before anything has been prepared.
  static int lookaheadSamples(double sampleRate) noexcept {
    return (int)(kLookaheadSeconds * std::max(1.0, sampleRate) + 0.5);
  }

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// How late this stage is, in samples, at the rate it was prepared with.
  int latency() const noexcept { return delay; }

  /// Shapes both channels in place, and delays them whether or not it shapes.
  ///
  /// @param type     which of the seven.
  /// @param shaping  false to pass the audio through at the same delay.
  /// @param ahead    false to give up the window and the delay with it. The
  ///                 processor reports the matching latency from the message
  ///                 thread.
  void process(float *left, float *right, int numSamples, ClipType type,
               bool shaping, bool ahead) noexcept;

private:
  /// Takes the window away or gives it back, without allocating. The state is
  /// cleared, since a delay line whose length has just changed would replay
  /// samples at the wrong moment.
  void applyLookahead(bool ahead) noexcept;
  /// The gain the limiter wants for the sample about to arrive, from the
  /// loudest sample in the window, against the type's ceiling.
  float gainFor(float peak, float ceiling) noexcept;

  float delayed(std::vector<float> &line, float in) noexcept;

  double rate = 48000.0;

  /// Samples of delay, and the two halves the gain path spends it on. A
  /// causal box of B samples carries its output back by (B - 1) / 2, so a
  /// window of W followed by two of them costs (W - 1) + (B - 1), which at
  /// W = B = delay / 2 is delay - 2 and arrives with a sample in hand.
  int delay = 0;
  int window = 1;
  int box = 1;

  /// The same values with the lookahead allowed, which is what the buffers
  /// are sized for.
  int fullDelay = 0;
  int fullWindow = 1;
  int fullBox = 1;
  bool looking = true;

  std::vector<float> lineLeft, lineRight;
  int writeIndex = 0;

  /// A monotonic wedge over the window: values decreasing from the front, so
  /// the front is the loudest sample still to be heard. A ring rather than a
  /// deque so nothing allocates on the audio thread.
  std::vector<float> wedgeValue;

  /// Counted rather than wrapped, so a sample's place in the window is one
  /// comparison. 64 bits because 32 overflow in twelve hours at 48 kHz, and
  /// long is 32 bits on Windows.
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

  /// The two machines with memory. Each runs only while it is chosen, to
  /// save CPU.
  Console console;
  Cassette cassette;

  /// Which of the two is running, if either.
  enum class Running { None, Console, Cassette };

  /// Runs one of them on a sample, or passes it through for None.
  void run(Running which, float &left, float &right) noexcept;

  /// Choosing one part way through starts it from rest and crossfades to it
  /// over this time while both run. A machine chosen from the start runs from
  /// the start, so a render that never switches stays bit-exact.
  static constexpr double kFadeSeconds = 0.030;
  Running running = Running::None, leaving = Running::None;
  bool started = false;
  int fadeLength = 1, fadeAt = 0;
};

} // namespace ovt
