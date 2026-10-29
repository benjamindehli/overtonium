#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ovt {

/// A four-track cassette recorder, measured: Dolby B, a normal tape at
/// 9.5 cm/s, recorded onto and played back, as it is used.
///
/// A test signal was played from a DAW onto two of its tracks at a normal
/// recording level and played back into the interface, once with Dolby and
/// once without, with a loopback of the interface alone to take its own
/// response out. Each pass was lined up by a chirp at both ends, which also
/// gave the tape's speed. The two passes agree on everything the tape does,
/// so what Dolby does is what differs between them.
///
/// Levels below are in the instrument's own units, where one is full scale,
/// with the machine's gain taken to unity, so a figure in dBFS is where it
/// was measured against the test signal. The whole is held at unity for a
/// quiet 1 kHz tone, and the machine's polarity, which it inverts, is not
/// kept.
namespace cassette {

/// Dolby B: the encoder lifts the quiet highs on the way onto tape and the
/// decoder lowers them again on the way off, taking the tape's hiss down with
/// them. Each is a first order high shelf of up to 10 dB whose depth falls as
/// the level in its own high band rises, so loud highs pass untouched.
///
/// Each side chain reads its band through one more high pass, at
/// kDolbySenseHz, so what sets the depth is mostly the top: under a loud
/// 315 Hz tone the machine still took 9 to 10 dB of hiss off above 4 kHz,
/// which a side chain reading the shelf's own band cannot do.
///
/// Fitted to what Dolby changed: the highs at four levels on the tones, the
/// 10 kHz steps and the sweeps, the mids it leaves alone, how much hiss it
/// removed in five bands, and how much it removed under three tones, within
/// 1.1 dB rms. The shelf gives
/// way around kDolbyThresholdDb, over kDolbySpreadDb. The encoder and decoder
/// corners differ, and the decoder hears its input kDolbyDecodeHearsDb
/// quieter than the encoder sent it, which is the machine's calibration and
/// the tape's loss together. That mismatch is why quiet highs come back 2 to
/// 5 dB duller and loud ones barely change, which is how this machine sounds.
/// It takes the hiss down by 10 dB above 4 kHz and 5 dB around 1 kHz, keeps
/// 9 of the 10 under a loud 315 Hz tone, and lets about half of it back under
/// a -18 dBFS tone at 3150 Hz.
inline constexpr double kDolbyMaxDb = 10.0;
inline constexpr double kDolbyThresholdDb = -42.28;
inline constexpr double kDolbySpreadDb = 14.97;
inline constexpr double kDolbyEncodeHz = 1277.0;
inline constexpr double kDolbyDecodeHz = 1451.0;
inline constexpr double kDolbyDecodeHearsDb = -4.85;
inline constexpr double kDolbySenseHz = 10019.0;

/// How fast each side chain follows its high band: a millisecond to rise, a
/// fast fall, and a slower follower at a quarter of it that holds the end of
/// a loud passage. Dolby B's own timing, checked against how the hiss above
/// 5 kHz died away after a 2 s burst of 1 kHz at -6 dBFS: +7.4 dB as it
/// ended, +3 after 10 ms and nothing by 100 ms, which the model follows
/// within about 1.5 dB. Most of that is the tape's added noise going, below.
inline constexpr double kDolbyAttack = 0.001;
inline constexpr double kDolbyFastRelease = 0.004;
inline constexpr double kDolbySlowRelease = 0.060;
inline constexpr double kDolbySlowShare = 0.25;

/// The tape saturates on its record side, lows first: at -3 dBFS the third
/// harmonic is -18 dB at 30 Hz, -29 at 120 Hz, -38 at 500 Hz and about -45 at
/// 1 kHz, and a 315 Hz tone is clean to -8 dBFS and at -28 by -1. So the
/// signal goes through a low shelf of kPreLowDb below kPreLowHz and a high
/// shelf of kPreHighDb above kPreHighHz, which is the record equaliser's lift
/// at the top, into a soft limit at kClipLimit with a knee of hardness
/// kClipKnee, and the inverse of the shelves after it. Within about 2 dB of
/// the third harmonics at -3 dBFS from 30 Hz to 4 kHz, and 0.5 dB of the 0.7
/// a 315 Hz tone loses at -1 dBFS. Driven to +6 dBFS a 1 kHz tone loses
/// 3.8 dB with a third harmonic at -18 dB, and a 30 Hz one at -9.
inline constexpr double kPreLowDb = 6.49;
inline constexpr double kPreLowHz = 83.0;
inline constexpr double kPreHighDb = 15.74;
inline constexpr double kPreHighHz = 20000.0;
inline constexpr double kClipLimit = 1.196;
inline constexpr double kClipKnee = 4.15;

/// What the tape does that is linear, from the sweeps without Dolby: a head
/// bump of kBumpDb at kBumpHz, and a first and a second order low pass for
/// the top, -1.4 dB at 8 kHz, -3.6 at 12 kHz, -8 at 16 kHz and -10.3 at
/// 18 kHz. Within 0.16 dB from 30 Hz to 18 kHz.
inline constexpr double kBumpHz = 35.7;
inline constexpr double kBumpDb = 1.95;
inline constexpr double kBumpQ = 0.94;
inline constexpr double kTopHz = 10010.0;
inline constexpr double kGapHz = 13624.0;
inline constexpr double kGapQ = 0.97;

/// The tape's hiss without Dolby, which is what the decoder is handed: white
/// at kHissDbPerHz per channel above 400 Hz, rising below it, 7 dB more at 150
/// to 400 Hz and 12 dB more at 60 to 150 Hz, as one shelf from kHissLowHz to
/// kHissShelfHz. The two tracks' hiss is not correlated. In dB per hertz
/// against a full-scale sine, at the level of what was sent to the machine:
/// the recording read -104.6, and the interface it was played and recorded
/// through takes 5.47 dB off everything on the way round.
inline constexpr double kHissDbPerHz = -99.1;
inline constexpr double kHissLowHz = 148.0;
inline constexpr double kHissShelfHz = 600.0;

/// The hiss follows the notes rather than running whenever Cassette is
/// chosen: it is at its measured level once the signal reaches kHissOpenDb,
/// less below that, and none with nothing playing, letting go over
/// kHissRelease.
inline constexpr double kHissOpenDb = -40.0;
inline constexpr double kHissAttack = 0.005;
inline constexpr double kHissRelease = 0.300;

/// The noise the tape adds while it carries a signal. Without Dolby, a 3150 Hz
/// tone at -18 dBFS and a 315 Hz tone at -4 raised the noise above 4 kHz by
/// exactly the same, -103.2 dB per hertz at 4 to 8 kHz and -107.4 at 8 to
/// 16 kHz on the recording. The two tones are 14 dB apart, and they are the
/// same once taken through a first order high pass at kRiseHz, so the noise
/// follows the signal's highs, as tape noise does where the recorded
/// wavelength is short. White noise at kRiseDbPerHz against the level of
/// that band, through the tape's top like everything it carries, letting go
/// over kRiseRelease, which is what the burst above took to fall quiet.
inline constexpr double kRiseHz = 1600.0;
inline constexpr double kRiseDbPerHz = -79.2;
inline constexpr double kRiseRelease = 0.018;

/// Wow and flutter, read from a 3150 Hz tone as its instantaneous frequency:
/// about 0.1% rms below 100 Hz, the same in both passes. Most of it is two
/// steady rates, 68 Hz at 0.075% rms and 40.8 Hz at 0.025%, with noise in
/// four bands besides: 0.021% from 0.2 to 2 Hz, 0.022% from 2 to 6 Hz,
/// 0.026% from 6 to 20 Hz and 0.07% of scrape flutter from 100 to 300 Hz. The
/// two tracks share one tape, so both channels move together. Played as a
/// delay of kFlutterCentre that the speed moves, which swung by up to 0.35 ms
/// on the tape, held inside kFlutterSwing.
inline constexpr double kFlutterHz = 68.0;
inline constexpr double kFlutterRms = 0.00075;
inline constexpr double kFlutterSecondHz = 40.8;
inline constexpr double kFlutterSecondRms = 0.00025;

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

/// The level wobbled slowly as well, 0.25 to 0.3 dB rms, mostly near
/// kWobbleHz, which is the tape's contact with the head rather than its
/// speed. Shared by both channels, like the speed.
inline constexpr double kWobbleHz = 0.37;
inline constexpr double kWobbleDb = 0.25;

} // namespace cassette

/// The recorder, run on both channels in place.
///
/// Called by the output stage ahead of its limiter, as Console is. The
/// flutter is a delay of about a millisecond, which is part of the machine
/// as the phase of an analogue filter is, and is not reported as latency:
/// nothing in a session lines up against an output machine.
class Cassette {
public:
  void prepare(double sampleRate);
  void reset() noexcept;

  void process(float &left, float &right) noexcept;

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
