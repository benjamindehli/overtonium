#pragma once

#include "Drift.h"
#include "SineTable.h"

namespace ovt {

/// What shape a modulator traces.
///
/// The order is the order a menu shows and, more importantly, the order the
/// parameter stores. The index travels in every preset and every session, so
/// entries are appended rather than inserted and none is ever removed.
///
/// Not every destination offers all of them. Pitch takes the lot. Amplitude
/// only ever reduces from the fader, so there is nowhere for a unipolar shape
/// to go that a bipolar one does not already reach, and it offers one square
/// rather than two. See kPitchShapes and kAmpShapes in PluginParameters.h,
/// which is where each destination's own list lives.
enum class LfoShape {
  Sine = 0,
  Triangle,
  Sawtooth,
  ReverseSawtooth,
  BipolarSquare,
  UnipolarSquare,
  SampleAndHold,
  Random,
  NumShapes
};

inline constexpr int kNumLfoShapes = (int)LfoShape::NumShapes;

/// Whether a shape needs the random stream, which decides whether it has to be
/// stepped rather than simply evaluated at a phase.
inline constexpr bool isRandomShape(LfoShape s) noexcept {
  return s == LfoShape::SampleAndHold || s == LfoShape::Random;
}

/// One low-frequency modulator: a phase, and the points the random shapes
/// wander between.
///
/// Stepped once per control block rather than per sample, which is where every
/// other modulator in this instrument runs. At 32 samples that is 1.5 kHz, far
/// above anything an LFO does, and it is what makes the square shapes cheap:
/// the amplitude gain already ramps across a block, so an edge arrives as a
/// slew of two thirds of a millisecond rather than as a step, which is the
/// difference between a square tremolo and a click on every edge.
class Lfo {
public:
  void reset() noexcept {
    phase = 0.0;
    for (auto &p : points)
      p = 0.0f;
  }

  /// At note-on, for the shapes that need somewhere to have come from.
  ///
  /// Fresh points per note per partial, the same as the drift: reusing one
  /// stream would turn 32 independent wanders into a single one.
  void restart(Xorshift &rng) noexcept {
    phase = 0.0;
    for (auto &p : points)
      p = rng.bipolar();
  }

  /// Moves on by `increment` turns and returns where that lands.
  ///
  /// The random shapes draw a new point as the phase wraps, so they have to be
  /// stepped rather than asked about an arbitrary phase. The rest do not care,
  /// and share the same phase so that changing shape mid-note carries on from
  /// where the last one was rather than restarting.
  float advance(Xorshift &rng, double increment, LfoShape shape,
                double offsetTurns = 0.0) noexcept {
    phase += increment;

    while (phase >= 1.0) {
      phase -= 1.0;

      points[0] = points[1];
      points[1] = points[2];
      points[2] = points[3];
      points[3] = rng.bipolar();
    }

    return value(shape, offsetTurns);
  }

  /// Where it is sitting, without moving it.
  ///
  /// @param offsetTurns  added to the phase before the shape is read. The
  ///                     amplitude destination passes a quarter turn, which
  ///                     turns Sine into the cosine it has always used: a note
  ///                     begins at full level and dips, rather than starting
  ///                     half attenuated. Every other shape inherits the same
  ///                     quarter turn rather than each having a rule of its
  ///                     own.
  float value(LfoShape shape, double offsetTurns = 0.0) const noexcept {
    // The random shapes step with the phase rather than being read off it, so
    // an offset would only slide the window and is ignored.
    if (shape == LfoShape::Random)
      return catmullRom(points, (float)phase);

    if (shape == LfoShape::SampleAndHold)
      return points[1];

    auto p = phase + offsetTurns;
    p -= std::floor(p);

    switch (shape) {
    case LfoShape::Sine:
      return SineTable::instance()((double)p);

    case LfoShape::Triangle:
      // Up from zero, over at a quarter, down through zero at a half.
      return p < 0.5 ? (float)(1.0 - 4.0 * std::abs(p - 0.25))
                     : (float)(4.0 * std::abs(p - 0.75) - 1.0);

    case LfoShape::Sawtooth:
      return (float)(2.0 * p - 1.0);

    case LfoShape::ReverseSawtooth:
      return (float)(1.0 - 2.0 * p);

    case LfoShape::BipolarSquare:
      return p < 0.5 ? 1.0f : -1.0f;

    case LfoShape::UnipolarSquare:
      return p < 0.5 ? 1.0f : 0.0f;

    case LfoShape::SampleAndHold:
    case LfoShape::Random:
    case LfoShape::NumShapes:
      break;
    }

    return 0.0f;
  }

  double turns() const noexcept { return phase; }

private:
  double phase = 0.0;
  float points[4]{};
};

} // namespace ovt
