#include "BusDrive.h"

#include <algorithm>

namespace ovt {

/// How hard each character's bus stage is run, set by ear per character
/// since the same figure through different curves is a different amount of
/// push. The curves are the same shapes each character's own tables are
/// rendered through. Pure, FM and Combo have no stage, and PD and the 6581
/// use their measured bow at full strength.
double BusDrive::amountFor(Character c) noexcept {
  switch (c) {
  case Character::Rail:
    return 0.13;
  case Character::Diode:
    return 0.07;
  case Character::Valve:
    return 0.10;
  case Character::Opamp:
    return 0.10;
  case Character::Bulb:
    return 0.15;
  case Character::Wheel:
    return 0.10;
  // Set by ear, a little under the op-amp.
  case Character::Vco:
    return 0.07;
  case Character::Pd:
  case Character::Sid:
    return 1.0;
  // The combo organ mixed its notes cleanly, with a difference tone 60 dB
  // under the notes.
  case Character::Fm:
  case Character::Combo:
  case Character::Pure:
  case Character::NumCharacters:
    break;
  }

  return 0.0;
}

BusDrive::Recipe BusDrive::recipeFor(Character c, float amount,
                                     double sampleRate) noexcept {

  Recipe out;
  Curve k;

  // Pure is the oscillator that is not a circuit, so it has no stage at all.
  if (c == Character::Pure || amount <= 0.0f)
    return out;

  if (c == Character::Bulb) {
    // A Wien bridge's lamp: the filament warms as the signal grows and the
    // gain comes down with it, over about half a second, the same figure as
    // the per-partial Bulb. It changes the level and not the waveform. See
    // Character::Bulb.
    out.machine = Machine::Lamp;

    // Warm enough that a mix at full scale leans back by six decibels at the
    // full amount.
    out.depth = (double)std::clamp(amount, 0.0f, 1.0f) * 2.0;
    out.warming = 1.0 - std::exp(-1.0 / (0.5 * sampleRate));

    return out;
  }

  // The phase distortion synthesizer's mix, which bows by a measured amount.
  // Sized so that one partial at full fader gets the second harmonic of the
  // instrument's single notes. See kPdSummingH2Db.
  //
  // The second harmonic of a sine of amplitude A through x - c x^2 is c A / 2
  // against the fundamental, and the bend divided by its drive d comes to
  // c = d / 2.
  if (c == Character::Pd) {
    out.machine = Machine::Curve;

    const double secondHarmonic =
        std::pow(10.0, (kPdSummingH2Db + kPdByEar.harmonicsDb) / 20.0);
    const double d = (double)std::clamp(amount, 0.0f, 1.0f) * 4.0 *
                     secondHarmonic / kBusReferenceAmplitude;

    k.shape = Shape::Quadratic;
    k.up.drive = k.down.drive = d;
    k.up.scale = k.down.scale = 1.0 / d;
    out.curve = k;

    return out;
  }

  // The 6581's mix bows too, by the share of its bow measured as sum and
  // difference tones between two voices. Its level was measured on a swing of
  // -1 to 1, which is kBusReferenceAmplitude here, so x - a (s / r)^2 in the
  // bus's own units is s - (a / r) s^2, and the bend divided by its drive d
  // comes to a / r = d / 2. See kSidMixQuadratic.
  if (c == Character::Sid) {
    out.machine = Machine::Curve;

    const double d = (double)std::clamp(amount, 0.0f, 1.0f) * 2.0 *
                     kSidMixQuadratic / kBusReferenceAmplitude;

    k.shape = Shape::Quadratic;
    k.up.drive = k.down.drive = d;
    k.up.scale = k.down.scale = 1.0 / d;
    out.curve = k;

    return out;
  }

  out.machine = Machine::Curve;

  // At most two, since past about 1.5 the curve adds aliasing faster than
  // overtones.
  const auto drive = (double)std::clamp(amount, 0.0f, 1.0f) * 2.0;

  const auto evenly = [&k](double d) {
    k.up.drive = k.down.drive = d;
    k.up.scale = k.down.scale = 1.0 / d;
  };

  switch (c) {
  case Character::Rail:
    // A rail clips both halves alike: odd harmonics, a twelfth and no octave.
    evenly(drive);
    break;

  case Character::Diode:
    // Two mismatched diodes, one for each half of the wave, so one side comes
    // back rounder than the other. Both halves keep a slope of one where they
    // meet. A mismatch of three makes the octave and the twelfth arrive
    // together, as in the Diode tables.
    {
      constexpr double kMismatch = 3.0;

      k.up.drive = drive;
      k.up.scale = 1.0 / drive;

      k.down.drive = drive * kMismatch;
      k.down.scale = 1.0 / (drive * kMismatch);
    }
    break;

  case Character::Opamp:
    // An op-amp holds a straight line until it reaches the rail, so it gives
    // way at a corner where the others bend gradually. It is not a rate limit,
    // since on a shared bus that cannot tell a low note's upper partials from a
    // high note's fundamental. See Character::Opamp.
    evenly(drive);
    k.shape = Shape::Cubic;
    break;

  case Character::Vco:
    // The mixer the VCO was recorded through: the op-amp's corner, with the
    // negative half reaching its rail 2.44 times sooner, which gives an octave
    // as well as a twelfth. See kVcoMixerMismatch.
    k.shape = Shape::Cubic;
    k.up.drive = drive;
    k.up.scale = 1.0 / drive;
    k.down.drive = drive * kVcoMixerMismatch;
    k.down.scale = 1.0 / (drive * kVcoMixerMismatch);
    break;

  case Character::Valve:
  case Character::Wheel:
  case Character::Fm:
  case Character::Combo:
  case Character::Pd:
  case Character::Sid:
  case Character::Bulb:
  case Character::Pure:
  case Character::NumCharacters:
    // The triode, biased so one half of the wave leans over before the other,
    // which puts an octave on top of every partial. Wheel's organ amplifier
    // also uses valves.
    //
    // The rest are listed so that adding a character is a compiler error here
    // until it has been decided. None of them reaches this.
    evenly(drive);
    k.bias = 0.2;

    {
      const auto atRest = std::tanh(drive * k.bias);

      k.offset = atRest;

      // The slope at rest. Dividing it out keeps this an overtone control
      // rather than a second volume.
      k.up.scale = k.down.scale = 1.0 / (drive * (1.0 - atRest * atRest));
    }
    break;
  }

  out.curve = k;

  return out;
}

/// The samples that arrived, handed back late by exactly what the filters
/// would have cost if they had been needed.
double BusDrive::delayed(Channel &c, double x) noexcept {
  for (int i = (int)c.in.size() - 1; i > 0; --i)
    c.in[(size_t)i] = c.in[(size_t)i - 1];

  c.in[0] = x;

  return c.in[(size_t)Halfband::kHalf];
}

/// One sample in, one out, with the circuit run twice in between.
///
/// The half-band's middle coefficient is exactly a half and every second
/// one is zero, so one of the two samples is the input passed through and
/// only the one between has to be computed.
double BusDrive::run(Channel &c, const Curve &k, double x,
                     double dcCoef) noexcept {
  const auto &h = Halfband::kernel();
  constexpr int half = Halfband::kHalf;

  const auto late = delayed(c, x);

  // Nothing to run, so the samples are handed back delayed by the same amount
  // the filters would have cost, keeping the plugin's latency fixed.
  if (k.up.drive <= 0.0)
    return late;

  // The one that was already there, and the one between it and the next.
  const auto even = c.in[(size_t)(half / 2)];
  double odd = 0.0;

  for (int m = 0; m < half; ++m)
    odd += h[(size_t)(2 * m + 1)] * c.in[(size_t)m];

  odd *= 2.0;

  const auto first = processSample(k, even, c.last);
  const auto second = processSample(k, odd, c.last);

  for (int i = (int)c.up.size() - 1; i > 1; --i)
    c.up[(size_t)i] = c.up[(size_t)i - 2];

  c.up[1] = first;
  c.up[0] = second;

  // Back down, through the half-band with the lift folded into it. Its middle
  // lands on the one the host would have had rather than between two of them.
  // See downKernel.
  const auto &down = downKernel();
  double out = 0.0;

  for (size_t m = 0; m < down.size(); ++m)
    out += down[m] * c.up[m];

  c.dc = out - c.dcIn + dcCoef * c.dc;
  c.dcIn = out;

  return c.dc;
}

void BusDrive::process(float *left_, float *right_, int numSamples, Character c,
                       float amount) noexcept {
  if (numSamples <= 0)
    return;

  // At the doubled rate, since the blocker is expressed per sample.
  const auto inner = sampleRate * 2.0;
  const auto recipe = recipeFor(c, amount, sampleRate);

  // About 5 Hz, below anything the series produces and above the rate at
  // which a chord arrives.
  const auto dcCoef = std::exp(-6.2831853071795862 * 5.0 / inner);

  // A lamp changes a level rather than a shape, so it needs neither the
  // doubled rate nor the blocker. The samples come through the bypass delay
  // with one slow gain on them.
  if (recipe.machine == Machine::Lamp) {
    for (int n = 0; n < numSamples; ++n) {
      const auto l = delayed(left, (double)left_[n]);
      const auto r = delayed(right, (double)right_[n]);

      // Power heats the filament, from both channels at once, since there is
      // one lamp.
      const auto power = 0.5 * (l * l + r * r);

      heat += (power - heat) * recipe.warming;

      const auto gain = 1.0 / (1.0 + recipe.depth * heat);

      left_[n] = (float)(l * gain);
      right_[n] = (float)(r * gain);
    }

    return;
  }

  for (int n = 0; n < numSamples; ++n) {
    left_[n] = (float)run(left, recipe.curve, (double)left_[n], dcCoef);
    right_[n] = (float)run(right, recipe.curve, (double)right_[n], dcCoef);
  }
}

} // namespace ovt
