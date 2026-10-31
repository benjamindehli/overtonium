#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ovt {

/// A four-track cassette recorder, measured: Dolby B, a normal tape at
/// 9.5 cm/s, recorded onto and played back.
///
/// Fitted from passes with and without Dolby. See "The output stage" in
/// DESIGN.md for how. Levels are in the instrument's units (one is full
/// scale), and the whole is held at unity for a quiet 1 kHz tone.
namespace cassette {

/// Dolby B: the encoder lifts the quiet highs onto tape and the decoder lowers
/// them again, taking the hiss down with them. Each is a first order high
/// shelf of up to kDolbyMaxDb whose depth falls as the level in its band rises,
/// giving way around kDolbyThresholdDb over kDolbySpreadDb.
///
/// Each side chain reads its band through one more high pass at kDolbySenseHz,
/// so mostly the top sets the depth. The encoder and decoder corners differ,
/// and the decoder hears its input kDolbyDecodeHearsDb quieter than it was
/// sent. That mismatch is why quiet highs come back 2 to 5 dB duller, as the
/// machine does.
inline constexpr double kDolbyMaxDb = 10.0;
inline constexpr double kDolbyThresholdDb = -42.28;
inline constexpr double kDolbySpreadDb = 14.97;
inline constexpr double kDolbyEncodeHz = 1277.0;
inline constexpr double kDolbyDecodeHz = 1451.0;
inline constexpr double kDolbyDecodeHearsDb = -4.85;
inline constexpr double kDolbySenseHz = 10019.0;

/// How fast each side chain follows its band: a millisecond to rise, a fast
/// fall, and a slower follower at kDolbySlowShare that holds the end of a loud
/// passage.
inline constexpr double kDolbyAttack = 0.001;
inline constexpr double kDolbyFastRelease = 0.004;
inline constexpr double kDolbySlowRelease = 0.060;
inline constexpr double kDolbySlowShare = 0.25;

/// The tape saturates on its record side, lows first. The signal goes through
/// a low shelf of kPreLowDb below kPreLowHz and a high shelf of kPreHighDb
/// above kPreHighHz (the record equaliser's lift), into a soft limit at
/// kClipLimit with a knee of hardness kClipKnee, and back through the inverse
/// shelves. Fitted to the measured third harmonics from 30 Hz to 4 kHz.
inline constexpr double kPreLowDb = 6.49;
inline constexpr double kPreLowHz = 83.0;
inline constexpr double kPreHighDb = 15.74;
inline constexpr double kPreHighHz = 20000.0;
inline constexpr double kClipLimit = 1.196;
inline constexpr double kClipKnee = 4.15;

/// The tape's linear response, from the sweeps without Dolby: a head bump of
/// kBumpDb at kBumpHz, and a first and a second order low pass for the top.
inline constexpr double kBumpHz = 35.7;
inline constexpr double kBumpDb = 1.95;
inline constexpr double kBumpQ = 0.94;
inline constexpr double kTopHz = 10010.0;
inline constexpr double kGapHz = 13624.0;
inline constexpr double kGapQ = 0.97;

/// The tape's hiss without Dolby, as handed to the decoder: white at
/// kHissDbPerHz per channel (dB per hertz against a full-scale sine), rising
/// below 400 Hz as one shelf from kHissLowHz to kHissShelfHz. Uncorrelated
/// between the channels.
inline constexpr double kHissDbPerHz = -99.1;
inline constexpr double kHissLowHz = 148.0;
inline constexpr double kHissShelfHz = 600.0;

/// The hiss follows the signal: at its measured level once the signal reaches
/// kHissOpenDb, less below, none with nothing playing, letting go over
/// kHissRelease.
inline constexpr double kHissOpenDb = -40.0;
inline constexpr double kHissAttack = 0.005;
inline constexpr double kHissRelease = 0.300;

/// The noise the tape adds while it carries a signal, which follows the
/// signal's highs: white noise at kRiseDbPerHz against the level of the signal
/// through a first order high pass at kRiseHz, out through the tape's top,
/// letting go over kRiseRelease.
inline constexpr double kRiseHz = 1600.0;
inline constexpr double kRiseDbPerHz = -79.2;
inline constexpr double kRiseRelease = 0.018;

/// Wow and flutter, about 0.1% rms: two steady rates and noise in four bands,
/// shared by both channels. Played as a delay of kFlutterCentre that the
/// speed moves, held inside kFlutterSwing.
inline constexpr double kFlutterHz = 68.0;
inline constexpr double kFlutterRms = 0.00075;
inline constexpr double kFlutterSecondHz = 40.8;
inline constexpr double kFlutterSecondRms = 0.00025;

/// One band of flutter noise: its edges in Hz and its rms speed deviation.
struct FlutterBand {
  double lowHz, highHz, rms;
};

inline constexpr std::array<FlutterBand, 4> kFlutterBands{{
    {0.2, 2.0, 0.00021},
    {2.0, 6.0, 0.00022},
    {6.0, 20.0, 0.00026},
    {100.0, 300.0, 0.00070},
}};

inline constexpr double kFlutterCentre = 0.001;
inline constexpr double kFlutterSwing = 0.0009;

/// A slow level wobble near kWobbleHz from the tape's contact with the head,
/// shared by both channels.
inline constexpr double kWobbleHz = 0.37;
inline constexpr double kWobbleDb = 0.25;

} // namespace cassette

/// The recorder, run on both channels in place, ahead of the output stage's
/// limiter.
///
/// The flutter delay of about a millisecond is part of the sound and is not
/// reported as latency.
class Cassette {
public:
  /// Works out the coefficients and allocates the delay line and the clip
  /// table. Not for the audio thread.
  void prepare(double sampleRate);
  /// Clears the filters, the delay line and the transport.
  void reset() noexcept;

  /// Runs one stereo sample through the machine.
  void process(float &left, float &right) noexcept;

  /// Moves the tape's own hiss, the floor under everything, by this many dB.
  /// Nought is the deck as measured. The noise that rises with the signal is
  /// left as it is.
  void setHissDb(double db) noexcept { hissTrim = std::pow(10.0, db / 20.0); }

private:
  /// One first-order section, from an analogue prototype by the bilinear
  /// transform with its corner prewarped.
  struct FirstOrder {
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double x1 = 0.0, y1 = 0.0;

    /// (c1 s + c0) / (d1 s + d0), with s in radians per second.
    void design(double c1, double c0, double d1, double d0, double k) noexcept;
    double run(double x) noexcept;
    void clear() noexcept { x1 = y1 = 0.0; }
  };

  /// One second-order section.
  struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double run(double x) noexcept;
    void clear() noexcept { z1 = z2 = 0.0; }
  };

  /// A Dolby side chain's level: a fast follower, and a slow one at a share
  /// of it that holds the end of a loud passage.
  struct Follower {
    double fast = 0.0, slow = 0.0;

    double run(double x, double attack, double fastRelease,
               double slowRelease) noexcept;
    double value() const noexcept;
    void clear() noexcept { fast = slow = 0.0; }
  };

  struct Channel {
    FirstOrder encodeBand, decodeBand, encodeSense, decodeSense;
    Follower encodeLevel, decodeLevel;
    /// The decoder's output and its band, a step ago.
    double decodeIn = 0.0, decodeBandOut = 0.0;

    /// Each side's shelf depth, as last worked out.
    double encodeDepth = 0.0, decodeDepth = 0.0;

    FirstOrder preLow, preHigh, postLow, postHigh;
    double lastClip = 0.0; ///< the limit's previous input, for its average

    Biquad bump, gap;
    FirstOrder top;

    /// The band the tape's added noise follows, its level, and the top the
    /// noise goes out through.
    FirstOrder riseBand, riseTop;
    Follower riseLevel;

    FirstOrder hissShelf;
    std::uint32_t rng = 1;

    std::vector<float> line;
  };

  /// The depth of a Dolby shelf, as a gain on its band, for a level.
  double shelfDepth(double level) const noexcept;

  /// The tape's limit, averaged over the step from the last sample through
  /// its antiderivative, so it does not alias.
  double clip(Channel &, double u) const noexcept;
  double clipCurve(double u) const noexcept;

  /// The antiderivative of what the limit takes away, from the table.
  double lostIntegral(double u) const noexcept;

  double tape(Channel &, double x) noexcept;

  /// What the tape adds to the hiss while it carries this, from its highs.
  double rise(Channel &, double x) noexcept;
  double decode(Channel &, double x) noexcept;

  static double uniform(std::uint32_t &state) noexcept;

  std::array<Channel, 2> channels{};

  double sampleRate = 48000.0;
  double trim = 1.0; ///< what holds a quiet 1 kHz tone at unity

  double attack = 0.0, fastRelease = 0.0, slowRelease = 0.0;
  double decodeHears = 1.0;
  double thresholdScale = 1.0, spreadPower = 1.0;
  double maxDepth = 0.0;

  /// The antiderivative of what the limit takes away, tabulated over
  /// kClipTableSpan limits, since the curve has none in closed form. Built
  /// once in prepare.
  static constexpr int kClipTableSize = 4096;
  static constexpr double kClipTableSpan = 32.0;
  std::vector<double> clipTable;
  double clipStep = 1.0;

  /// The speed and the level, shared by both channels.
  double flutterPhase = 0.0, flutterStep = 0.0;
  double secondPhase = 0.0, secondStep = 0.0;
  std::array<Biquad, cassette::kFlutterBands.size()> flutterBands{};
  std::array<double, cassette::kFlutterBands.size()> flutterBandScale{};
  Biquad wobble;
  double wobbleScale = 0.0;
  double delaySamples = 0.0, centreSamples = 0.0, swingSamples = 0.0;
  double leak = 0.0;
  std::uint32_t transportRng = 1;

  Follower gate;
  double gateAttack = 0.0, gateRelease = 0.0, gateOpen = 1.0;
  double hissScale = 0.0, riseScale = 0.0, riseRelease = 0.0;
  double hissTrim = 1.0;

  int writeIndex = 0;
  int lineLength = 1;

  /// How often the depths and the wobble are worked out, in samples, and
  /// where in that this sample is.
  static constexpr int kControlStep = 8;
  int controlCount = 0;
  bool refresh = true;
  double wobbleGain = 1.0;
};

} // namespace ovt
