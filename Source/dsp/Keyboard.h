#pragma once

#include <algorithm>
#include <cmath>

#include "Exact.h"

namespace ovt {

/// What the KEYBOARD row does to a partial's level: where on the keyboard the
/// note is played turns it down, on one side of a centre key.
///
/// Shared by the voice and the panel, whose readout names the slope and the
/// centre and must agree with the voice exactly. JUCE-free like the rest of
/// the core.
///
/// The amount runs from -2 to 2. Up to 1 either way it tilts around middle C
/// with a slope that grows as the square of the amount, so the gentle slopes
/// used for voicing get most of the travel. Past 1 the slope holds at its
/// steepest and the centre moves instead, an octave for every 0.2, so the
/// ends of the travel leave only the top or only the bottom of the keyboard.
/// Positive turns down the keys below the centre, negative the keys above.

/// The key the tilt turns around up to full amount, middle C.
inline constexpr float kKeyboardCentreKey = 60.0f;

/// The slope at full amount, and past it, in dB per octave.
inline constexpr float kKeyboardMaxSlopeDb = 24.0f;

/// The furthest the amount goes either way.
inline constexpr float kKeyboardMaxAmount = 2.0f;

/// How far the centre moves for each whole unit of amount past 1, in keys.
/// An octave for every 0.2, so it reaches key 120 at 2 and key 0 at -2.
inline constexpr float kKeyboardKeysPerAmount = 60.0f;

/// Where an amount puts the tilt.
struct KeyboardTilt {
  /// How steeply the level falls away from the centre, in dB per octave.
  float dbPerOctave = 0.0f;
  /// The key the fall starts from, which may lie between keys.
  float centre = kKeyboardCentreKey;
  /// True when the keys below the centre are turned down, false for the keys
  /// above it.
  bool below = true;
};

inline KeyboardTilt keyboardTilt(float amount) noexcept {
  const auto a = std::clamp(amount, -kKeyboardMaxAmount, kKeyboardMaxAmount);
  const auto reach = std::abs(a);

  KeyboardTilt t;
  t.below = a >= 0.0f;

  if (reach <= 1.0f) {
    t.dbPerOctave = kKeyboardMaxSlopeDb * reach * reach;
    return t;
  }

  t.dbPerOctave = kKeyboardMaxSlopeDb;
  t.centre = kKeyboardCentreKey +
             std::copysign((reach - 1.0f) * kKeyboardKeysPerAmount, a);
  return t;
}

/// How far the KEYBOARD row takes a partial down on this key, in dB, zero or
/// below. The key can lie between keys, for a note gliding to a new one.
inline float keyboardDecibels(float amount, double key) noexcept {
  const auto t = keyboardTilt(amount);

  if (t.dbPerOctave <= 0.0f)
    return 0.0f;

  const auto past = t.below ? (double)t.centre - key : key - (double)t.centre;

  if (past <= 0.0)
    return 0.0f;

  return -(float)(past / 12.0) * t.dbPerOctave;
}

/// What the KEYBOARD row leaves of a partial's level on this key, 0 to 1. It
/// never raises a level, and zero amount leaves it as set.
inline float keyboardGain(float amount, double key) noexcept {
  const auto db = keyboardDecibels(amount, key);

  return db < 0.0f ? std::pow(10.0f, db / 20.0f) : 1.0f;
}

/// keyboardGain for one partial of one note, kept with the amount and key it
/// was worked out for, so a held note pays for the power only when the knob
/// turns or a glide moves the key.
struct KeyboardCache {
  float gain = 1.0f;
  float amount = 0.0f;
  double key = kKeyboardCentreKey;

  float at(float newAmount, double newKey) noexcept {
    // Exact compares, since anything else is a change worth following.
    if (!exactly(newAmount, amount) || !exactly(newKey, key)) {
      amount = newAmount;
      key = newKey;
      gain = keyboardGain(newAmount, newKey);
    }

    return gain;
  }
};

} // namespace ovt
