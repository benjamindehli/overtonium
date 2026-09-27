#include "BusDrive.h"

#include <algorithm>

namespace ovt {

/// Three of the five so far, each the same shape its own tables are rendered
/// through, so a patch runs into the same kind of circuit twice: once per
/// partial and once where they all meet.
///
/// What separates them is what each does with the two halves of a wave. A
/// rail treats them alike and therefore cannot tell them apart, so it has a
/// twelfth to give and no octave at all. A triode leans, so the octave leads.
/// A mismatched pair of diodes rounds one half more than the other, so the
/// two arrive together. Measured through a 1 kHz partial at 80% of full scale
/// at the full amount:
///
///   Rail    octave -164 dB, twelfth -18 dB, halves identical
///   Diode   octave  -15 dB, twelfth -15 dB, halves differ by 27%
///   Valve   octave  -17 dB, twelfth -20 dB, halves differ by 30%
///
/// The op-amp is not a curve and is not written as one. It is a rate limit,
/// so it answers to how fast the mix is moving rather than to how big it is,
/// which makes it the only one of the five that is frequency-dependent and
/// the only one that cares what the rate of the session is.
///
/// Bulb is the last one left. It is not distortion at all but a lamp, so its
/// bus behaviour is the mix leaning back as it gets loud and recovering over
/// about half a second, which adds no overtones and cannot alias. Until that
/// machine exists it reads the triode, and tuning it against that reading is
/// tuning a stand-in.
///
/// **What each circuit wants**, which is the whole point of the knob and is
/// what replaces it. One figure per character, since a rail and a triode bend
/// a waveform differently enough that the same number through both would be
/// two different amounts of push:
///
///   Rail    13%
///   Diode    7%
///   Valve   10%
///   Op-amp  not found yet
///   Bulb    not found yet
///   Pure    none, and never any: it is the oscillator that is not a circuit
///
/// The three that have a figure were found by ear on 26 September 2026, and
/// they are not close to each other for a reason: a rail is the gentlest of
/// the shapes at a given drive, since a limit that treats both halves alike
/// has only odd harmonics to give, so it takes more push to say anything. A
/// mismatched pair of diodes is the busiest, giving an octave and a twelfth
/// together, so it needs the least.
///
/// While the knob exists it says what the amount is and this table only
/// records what has been decided. When the last line is filled the table
/// becomes the code and the knob goes, which is the one parameter change that
/// has to happen before a release rather than after one.
BusDrive::Recipe BusDrive::recipeFor(Character c, float amount,
                                     double sampleRate) noexcept {
  Recipe out;
  Curve k;

  if (c == Character::Pure || amount <= 0.0f)
    return out;

  if (c == Character::Opamp) {
    // An amplifier that cannot move as fast as a fast mix asks it to. The
    // amount is the corner it stops keeping up at, in kilohertz and upside
    // down: a tenth puts it at 10 kHz, where only the top of the series is
    // affected, and the whole way up puts it at one, which is where the
    // per-partial op-amp already places it and is in the middle of where
    // anyone plays.
    //
    // Expressed as how far one sample may be from the one before it, which is
    // the slope of a full-scale wave at that corner. A mix moving slower than
    // that never meets the limit at all, which is why this one is not a curve:
    // it answers to speed rather than to size, so a quiet passage played fast
    // hardens where a loud one played slowly does not.
    out.kind = Kind::Slew;
    out.step =
        6.283185307179586 * (kSlewCornerHz / (double)amount) / sampleRate;

    return out;
  }

  out.kind = Kind::Curve;

  // Two, because the knob should spend its travel somewhere useful. Measured
  // through the triode against a 1 kHz partial at 80% of full scale, a second
  // one at 2% of it, a 1 kHz and 1.4 kHz pair, and an 18 kHz partial whose
  // octave can only fold back to 12 kHz, all at the full amount of the drive
  // named:
  //
  //   drive  loud 2nd  quiet 2nd  difference tone  folded octave
  //     0.5    -34.6      -51.8           -33.5          -43.5
  //     1.0    -24.1      -68.6           -22.9          -33.3
  //     1.5    -19.2      -48.0           -17.8          -28.6
  //     2.5    -15.0      -38.9           -12.9          -25.0
  //     5.0    -12.6      -28.4            -9.2          -23.6
  //
  // Past about 1.5 the curve has stopped adding overtones and started folding
  // instead: two and a half drives buys three more decibels of octave and four
  // of aliasing. So the whole knob lives under that, and the useful end of it
  // is the bottom third.
  const auto drive = (double)std::clamp(amount, 0.0f, 1.0f) * 2.0;

  const auto evenly = [&k](double d) {
    k.up.drive = k.down.drive = d;
    k.up.scale = k.down.scale = 1.0 / d;
  };

  switch (c) {
  case Character::Rail:
    // An amplifier that has run out of supply clips both halves of the wave
    // alike, and a limit that treats them alike cannot tell them apart: odd
    // harmonics, a twelfth on top of every partial and no octave at all. The
    // same shape the Rail tables are rendered through, and the plainest of
    // the five.
    evenly(drive);
    break;

  case Character::Diode:
    // Two diodes across the feedback path, which are not the same diode. Each
    // half of the wave runs into its own one and they do not give way at the
    // same place, so the wave comes back with one side rounder than the
    // other. Both halves keep a slope of one where they meet, so what differs
    // is how fast each bends rather than a corner between them: a kink in the
    // shape of the wave rather than a step in it.
    //
    // Three, because a pair that mismatched is what gives this one a voice of
    // its own rather than a quieter version of somebody else's. Measured at
    // full drive against a 1 kHz partial at 80% of full scale:
    //
    //   mismatch  octave  twelfth  fourth
    //        1.6   -21.4    -15.7   -34.1
    //        2.2   -17.0    -15.1   -27.5
    //        3.0   -14.6    -15.1   -23.7
    //        4.0   -13.2    -15.3   -21.5
    //        6.0   -12.0    -15.7   -19.7
    //
    // Under about two it is a rail with a lean on it, since the twelfth still
    // leads by five or six decibels. At three the octave and the twelfth
    // arrive together with the fourth beneath them, which is the shape the
    // Diode tables have and is what a kink sounds like.
    {
      constexpr double kMismatch = 3.0;

      k.up.drive = drive;
      k.up.scale = 1.0 / drive;

      k.down.drive = drive * kMismatch;
      k.down.scale = 1.0 / (drive * kMismatch);
    }
    break;

  case Character::Valve:
  case Character::Bulb:
  case Character::Opamp:
  case Character::Pure:
  case Character::NumCharacters:
    // The triode, biased so one half of the wave leans over before the other,
    // which is what puts an octave on top of every partial rather than a
    // twelfth.
    //
    // Bulb is here because its own machine is not written yet, and it is not
    // really this one: a lamp adds no overtones at all. Tuning it against this
    // reading is tuning a stand-in.
    evenly(drive);
    k.bias = 0.2;

    {
      const auto atRest = std::tanh(drive * k.bias);

      k.offset = atRest;

      // The slope at rest, which is what a signal too small to bend the curve
      // sees. Dividing it back out is what keeps this an overtone control
      // rather than a second volume.
      k.up.scale = k.down.scale = 1.0 / (drive * (1.0 - atRest * atRest));
    }
    break;
  }

  out.curve = k;

  return out;
}

void BusDrive::process(float *left, float *right, int numSamples, Character c,
                       float amount) noexcept {
  const auto recipe = recipeFor(c, amount, sampleRate);

  if (recipe.kind == Kind::None || numSamples <= 0)
    return;

  // About 5 Hz, which is below anything the series can produce and above the
  // rate at which a chord arrives.
  const auto dcCoef = std::exp(-6.2831853071795862 * 5.0 / sampleRate);

  const auto &k = recipe.curve;
  const auto slew = recipe.kind == Kind::Slew;

  for (int n = 0; n < numSamples; ++n) {
    const auto l = slew ? slewSample(recipe.step, (double)left[n], slewL)
                        : processSample(k, (double)left[n], lastL);
    const auto r = slew ? slewSample(recipe.step, (double)right[n], slewR)
                        : processSample(k, (double)right[n], lastR);

    dcL = l - dcInL + dcCoef * dcL;
    dcInL = l;

    dcR = r - dcInR + dcCoef * dcR;
    dcInR = r;

    left[n] = (float)dcL;
    right[n] = (float)dcR;
  }
}

} // namespace ovt
