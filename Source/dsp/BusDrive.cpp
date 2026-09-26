#include "BusDrive.h"

#include <algorithm>

namespace ovt {

/// One curve so far, the triode: biased so that one half of the wave leans
/// over before the other, which is what puts an octave on top of every partial
/// rather than a twelfth. It is the same shape Character::Valve renders its
/// own table through, at the same bias, so a patch on Valve is running into
/// the same kind of circuit twice: once per partial and once where they meet.
///
/// The other four are not written yet. Rail runs out of supply, symmetrically;
/// Diode has a kink rather than a knee; Op-amp is a slew limit on the bus
/// rather than a curve at all, which is the only one of the five that is
/// frequency-dependent; and Bulb is not distortion but a lamp, so its bus
/// behaviour is a slow sag as the mix gets loud. Until they are here, every
/// character but Pure reads this one.
BusDrive::Curve BusDrive::curveFor(Character c, float amount) noexcept {
  Curve k;

  if (c == Character::Pure || amount <= 0.0f)
    return k;

  // Two, because the knob should spend its travel somewhere useful. Measured
  // against a 1 kHz partial at 80% of full scale, a second one at 2% of it, a
  // 1 kHz and 1.4 kHz pair, and an 18 kHz partial whose octave can only fold
  // back to 12 kHz, all at the full amount of the drive named:
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
  k.drive = std::clamp(amount, 0.0f, 1.0f) * 2.0f;
  k.bias = 0.2f;

  const auto atRest = std::tanh(k.drive * k.bias);

  k.offset = atRest;

  // The slope at zero, which is what a signal too small to bend the curve sees.
  // Dividing it back out is what keeps this an overtone control rather than a
  // second volume.
  k.scale = 1.0f / (k.drive * (1.0f - atRest * atRest));

  return k;
}

void BusDrive::process(float *left, float *right, int numSamples, Character c,
                       float amount) noexcept {
  const auto k = curveFor(c, amount);

  if (k.drive <= 0.0f || numSamples <= 0)
    return;

  if (!primed) {
    lastL = lastR = 0.0f;
    lastFL = integral(k, 0.0f);
    lastFR = lastFL;
    primed = true;
  }

  // About 5 Hz, which is below anything the series can produce and above the
  // rate at which a chord arrives.
  const auto dcCoef = (float)std::exp(-6.2831853 * 5.0 / sampleRate);

  for (int n = 0; n < numSamples; ++n) {
    const auto l = processSample(k, left[n], lastL, lastFL);
    const auto r = processSample(k, right[n], lastR, lastFR);

    dcL = l - dcInL + dcCoef * dcL;
    dcInL = l;

    dcR = r - dcInR + dcCoef * dcR;
    dcInR = r;

    left[n] = dcL;
    right[n] = dcR;
  }
}

} // namespace ovt
