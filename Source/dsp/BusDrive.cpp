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
/// The two that are left are not curves and will not be written as ones.
/// Op-amp is a slew limit, so it answers to how fast the mix moves rather
/// than to how big it is, and it is the only one of the five that is
/// frequency-dependent. Bulb is not distortion at all but a lamp, so its bus
/// behaviour is the mix leaning back as it gets loud and recovering over
/// about half a second, which adds no overtones and cannot alias. Until those
/// exist both read the triode, and tuning either against that is tuning a
/// stand-in.
///
/// **What each circuit wants**, which is the whole point of the knob and is
/// what replaces it. One figure per character, since a rail and a triode bend
/// a waveform differently enough that the same number through both would be
/// two different amounts of push:
///
///   Valve   10%, found by ear on 26 September 2026
///   Rail    not found yet
///   Diode   not found yet
///   Op-amp  not found yet
///   Bulb    not found yet
///   Pure    none, and never any: it is the oscillator that is not a circuit
///
/// While the knob exists it says what the amount is and this table only
/// records what has been decided. When the last line is filled the table
/// becomes the code and the knob goes, which is the one parameter change that
/// has to happen before a release rather than after one.
BusDrive::Curve BusDrive::curveFor(Character c, float amount) noexcept {
  Curve k;

  if (c == Character::Pure || amount <= 0.0f)
    return k;

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
    // Bulb and Op-amp are here because their own machines are not written
    // yet, and neither of them is really this one: a lamp adds no overtones
    // at all and a slew limit is not a curve. Tuning either against this
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

  return k;
}

void BusDrive::process(float *left, float *right, int numSamples, Character c,
                       float amount) noexcept {
  const auto k = curveFor(c, amount);

  if (k.up.drive <= 0.0 || numSamples <= 0)
    return;

  // About 5 Hz, which is below anything the series can produce and above the
  // rate at which a chord arrives.
  const auto dcCoef = std::exp(-6.2831853071795862 * 5.0 / sampleRate);

  for (int n = 0; n < numSamples; ++n) {
    const auto l = processSample(k, (double)left[n], lastL);
    const auto r = processSample(k, (double)right[n], lastR);

    dcL = l - dcInL + dcCoef * dcL;
    dcInL = l;

    dcR = r - dcInR + dcCoef * dcR;
    dcInR = r;

    left[n] = (float)dcL;
    right[n] = (float)dcR;
  }
}

} // namespace ovt
