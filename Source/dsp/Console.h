#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// A console's mix bus, measured: its transformer, its bus compressor and a
/// tape stage, in that order, which the output stage's limiter follows.
///
/// Recorded from a real chain at the settings it is used at, one point at a
/// time, with a test signal played from a DAW at 96 kHz: the transformer's
/// output, then the compressor's, then the tape stage's. Each unit was fitted
/// from the pass before it to its own, so each is measured at the level it
/// really sees in the chain, and the gain staging between them, which is most
/// of how hard each one works, is the chain's. The whole is held at unity for
/// a quiet 1 kHz tone, so choosing it does not change how loud a patch is. The
/// chain's polarity, which the transformer inverts, is not kept.
///
/// Levels below are in the instrument's own units, where one is full scale,
/// with the chain's gain taken to unity, so each fitted threshold and limit has
/// been moved from the level it was recorded at to the level it sees here.
namespace console {

/// The transformer's core saturates on flux, which is the signal integrated
/// over time, so the lows saturate long before the highs at the same level.
/// The input is taken through one pole at kFluxHz and that part runs into a
/// knee at kFluxLimit of hardness kFluxKnee, and what it cannot carry is lost
/// from the output. Fitted to the third harmonics it measured: -49.7 dB at 30
/// Hz and -6 dBFS, -34.8 at -1, -58.0 at 60 Hz and -71.3 at 120 Hz, which the
/// model gives within 2 to 4 dB, and below -79 dB from 250 Hz up. Below where
/// it saturates the transformer's third harmonic is level-independent, about
/// -61 dB at 30 Hz and 10 dB less each octave up, which is the core's
/// hysteresis and is left out as far below hearing. Shaped so it cannot turn
/// back past what was measured: driven to +12 dBFS a 30 Hz note loses 2.6 dB
/// with a third harmonic at -25 dB.
inline constexpr double kFluxHz = 15.24;
inline constexpr double kFluxLimit = 0.671;
inline constexpr double kFluxKnee = 2.35;

/// The transformer's passive high shelf, which lifts the top to make up for
/// the low end and mids the rest of the chain adds: a first-order shelf of
/// kShelfDb above kShelfHz, which fits the measured response within 0.11 dB
/// from 1 to 16 kHz.
inline constexpr double kShelfDb = 4.25;
inline constexpr double kShelfHz = 8127.0;

/// The bus compressor, at the settings it is used at: 4:1, a 10 ms attack,
/// automatic release and its sidechain filtered at 120 Hz, with the threshold
/// set for about 3 dB of reduction at most. Linked, as the recording showed: a
/// loud burst on one side alone turned both down by the same 0.30 dB.
///
/// Fitted by running the transformer's pass through the model and matching
/// the gain the compressor's pass shows against it in 5 ms windows, over 1
/// kHz steps from -60 to -1 dBFS and back and over bursts of 20 ms to 5 s,
/// within 0.12 dB rms and 0.48 dB at the worst. The reduction is worked out
/// from the level at once and then smoothed, which is what lets a 20 ms hit be
/// caught at all. It grows at the attack and shrinks at a fast release, and a
/// slow stage charges while it is held and lets go slowly, so a short hit lets
/// go in tens of milliseconds and a long one leaves a release of seconds, as
/// the automatic release measured: back to 37% in 50 ms after 20 ms, and in
/// about 2.5 s after two to five seconds. The ratio is the knob's. The attack
/// is held near the knob's, since the small reductions the bursts made could
/// not tell a slower attack from a gentler ratio.
inline constexpr double kCompThresholdDb = -5.26;
inline constexpr double kCompRatio = 4.0;
inline constexpr double kCompKneeDb = 10.33;
inline constexpr double kCompAttack = 0.020;
inline constexpr double kCompFastRelease = 0.02578;
inline constexpr double kCompSlowRelease = 3.47;
inline constexpr double kCompSlowCharge = 1.301;
inline constexpr double kCompSidechainHz = 120.0;

/// The tape stage, which has no time in it: its third harmonic is there at
/// full strength within 20 ms of a loud tone and holds for five seconds. Its
/// third harmonic rises 16 dB in 6 dB of level and then holds near -34 dB,
/// which a share of the signal running into a hard limit while the rest passes
/// does: just over the limit the clipped part grows fast, and well over it the
/// clipped part is a steady fraction. kTapeShare of the signal, taken through a
/// low shelf of kTapeLowDb below kTapeLowHz since the lows reach the limit
/// first, runs into kTapeLimit. Within about 1 to 6 dB of the measured third
/// harmonic at the three loudest levels, and within 0.3 dB of the measured
/// loss of level, which is up to 1.4 dB in the lows at the top.
inline constexpr double kTapeShare = 0.072;
inline constexpr double kTapeLimitDb = -11.0;
inline constexpr double kTapeLowDb = 6.4;
inline constexpr double kTapeLowHz = 634.0;

/// What the units add that is linear: a gentle low shelf, mostly the tape
/// stage's low bump, and the coupling capacitors' high-pass in the lows,
/// fitted against the whole model's quiet response. With them the model's
/// response follows the chain's within about 0.4 dB, and its phase within one
/// or two degrees below 1 kHz.
inline constexpr double kBumpDb = 1.75;
inline constexpr double kBumpHz = 1347.0;
inline constexpr double kCouplingHz = 15.1;

} // namespace console

/// The console's three units, run on both channels in place.
///
/// Called by the output stage ahead of its limiter, whose ceiling the chain
/// ends at. Nothing in here adds latency: the one place that could alias, the
/// tape stage's hard limit, is read as its average over each step by its
/// antiderivative, as the bus drive's curves are.
class Console {
public:
  void prepare(double sampleRate) noexcept;
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

  /// One second-order section, for the sidechain's high-pass.
  struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double run(double x) noexcept;
    void clear() noexcept { z1 = z2 = 0.0; }
  };

  struct Channel {
    double flux = 0.0; ///< the transformer's input through its one pole
    FirstOrder shelf, tapeLow, bump, coupling;
    Biquad sidechain;
    double lastTape = 0.0; ///< the tape stage's previous input, for its average
  };

  double transformer(Channel &, double x) const noexcept;
  double tape(Channel &, double x) const noexcept;

  std::array<Channel, 2> channels{};

  double fluxCoef = 0.0;
  double attack = 0.0, fastRelease = 0.0, slowRelease = 0.0, slowCharge = 0.0;
  double fast = 0.0, slow = 0.0; ///< the reduction's two stages, in dB
  double tapeLimit = 1.0;
  double trim = 1.0; ///< what holds a quiet 1 kHz tone at unity
};

} // namespace ovt
