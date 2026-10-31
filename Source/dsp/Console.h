#pragma once

#include <array>
#include <cmath>

namespace ovt {

/// A console's mix bus, measured: its transformer, its bus compressor and a
/// tape stage, in that order, ahead of the output stage's limiter.
///
/// Each unit was fitted from the recorded input to its own output, at the
/// level it sees in the chain. See "The output stage" in DESIGN.md for how.
/// Levels are in the instrument's units (one is full scale) with the chain
/// held at unity for a quiet 1 kHz tone.
namespace console {

/// The transformer's core saturates on flux, the signal integrated over time,
/// so the lows saturate first. The input through one pole at kFluxHz runs into
/// a knee at kFluxLimit of hardness kFluxKnee, and what it cannot carry is lost
/// from the output. Fitted to the measured third harmonic from 30 to 120 Hz.
inline constexpr double kFluxHz = 15.24;
inline constexpr double kFluxLimit = 0.671;
inline constexpr double kFluxKnee = 2.35;

/// The transformer's passive high shelf: first order, kShelfDb above
/// kShelfHz, fitted to the measured response from 1 to 16 kHz.
inline constexpr double kShelfDb = 4.25;
inline constexpr double kShelfHz = 8127.0;

/// The bus compressor at its used settings: 4:1, 10 ms attack, automatic
/// release, sidechain high-passed at 120 Hz, stereo linked.
///
/// The reduction is worked out from the level at once and then smoothed by a
/// fast stage (attack and fast release) and a slow stage that charges while a
/// reduction is held, so a short hit lets go in tens of milliseconds and a
/// long one over seconds, as the automatic release measured.
inline constexpr double kCompThresholdDb = -5.26;
inline constexpr double kCompRatio = 4.0;
inline constexpr double kCompKneeDb = 10.33;
inline constexpr double kCompAttack = 0.020;
inline constexpr double kCompFastRelease = 0.02578;
inline constexpr double kCompSlowRelease = 3.47;
inline constexpr double kCompSlowCharge = 1.301;
inline constexpr double kCompSidechainHz = 120.0;

/// The tape stage, which has no time constant: kTapeShare of the signal,
/// through a low shelf of kTapeLowDb below kTapeLowHz, runs into a hard limit
/// at kTapeLimitDb and the clipped part is taken off. That gives a third
/// harmonic that rises fast just over the limit and then holds near -34 dB, as
/// measured.
inline constexpr double kTapeShare = 0.072;
inline constexpr double kTapeLimitDb = -11.0;
inline constexpr double kTapeLowDb = 6.4;
inline constexpr double kTapeLowHz = 634.0;

/// The chain's linear part: a gentle low shelf (mostly the tape stage's bump)
/// and the coupling capacitors' high-pass, fitted to the quiet response.
inline constexpr double kBumpDb = 1.75;
inline constexpr double kBumpHz = 1347.0;
inline constexpr double kCouplingHz = 15.1;

} // namespace console

/// The console's three units, run on both channels in place.
///
/// Adds no latency. The tape stage's hard limit is read as its average over
/// each step by its antiderivative, so it does not alias.
class Console {
public:
  /// Works out the coefficients for the sample rate and clears the state.
  void prepare(double sampleRate) noexcept;
  /// Clears the filters and the compressor's state.
  void reset() noexcept;

  /// Runs one stereo sample through the chain.
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
