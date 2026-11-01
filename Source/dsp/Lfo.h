#pragma once

#include "Drift.h"
#include "SineTable.h"

namespace ovt {

/// What shape a modulator traces.
///
/// The index is stored in presets and sessions, so entries are only ever
/// appended. Each destination's own list is kPitchShapes and kAmpShapes in
/// PluginParameters.h.
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

/// One low-frequency modulator: a phase, and the points the random shapes
/// wander between.
///
/// Stepped once per control block. The amplitude gain ramps across a block,
/// which is what keeps the square shapes from clicking.
class Lfo {
public:
  void reset() noexcept {
    phase = 0.0;
    for (auto &p : points)
      p = 0.0f;
  }

  /// At note-on. Fresh random points per note per partial, so the partials
  /// wander independently.
  void restart(Xorshift &rng) noexcept {
    phase = 0.0;
    for (auto &p : points)
      p = rng.bipolar();
  }

  /// Moves on by `increment` turns and returns where that lands. The random
  /// shapes draw a new point each time the phase wraps. All shapes share the
  /// phase, so changing shape mid-note does not restart it.
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
  ///                     amplitude destination passes a quarter turn so a note
  ///                     starts at full level and dips.
  float value(LfoShape shape, double offsetTurns = 0.0) const noexcept {
    // The random shapes ignore the offset.
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

  /// The phase in turns, [0, 1).
  double turns() const noexcept { return phase; }

private:
  double phase = 0.0;
  float points[4]{};
};

} // namespace ovt
