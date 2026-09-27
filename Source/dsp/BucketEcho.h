#pragma once

#include <cstdint>
#include <vector>

#include "Drift.h"
#include "Params.h"

namespace ovt {

/// A bucket brigade delay across the master output.
///
/// A bucket brigade is a line of capacitors handing a charge along, one step
/// per tick of a clock. There is no tape and no motor, so none of what makes
/// the tape echo what it is applies here, and modelling it as a darker tape
/// would miss everything that makes people keep the pedals.
///
/// **It is darker the longer it is set to.** The line has a fixed number of
/// buckets, so a longer delay can only be had by clocking them more slowly,
/// and the filter that reconstructs the signal has to come down with the
/// clock to keep its own aliasing out. Every unit ever built does this: short
/// settings are nearly clean and long ones are murk. TIME therefore has a
/// second job on this type that it has on no other.
///
/// **Its stereo is two clocks, not two tapes.** Each side is modulated by a
/// slow sine of its own, close in rate to the other and sharing no factor
/// with it, so the two never fall into step. That is a gentle vibrato rather
/// than the tape's drift, which is what a pair of these actually sounds like.
///
/// **What AGE does here** is three things that go together, the way wear does
/// on tape:
///
/// Darker still, on top of what the clock already costs.
///
/// Dirtier every pass. The line clips early and the repeat goes round again,
/// so a tail starts clean and ends up growling. The filter is inside the loop
/// and ahead of it, which keeps what the clipping makes from climbing out of
/// the top of the spectrum.
///
/// And the hiss. A bucket brigade is noisy enough that every one of them has a
/// compander wrapped around it, compressing on the way in and expanding on the
/// way out, and an expander pulls the noise down when there is nothing to hide
/// it. Old ones mistrack: the pull-down arrives late, so the hiss swells in
/// behind a chord and ducks away as the repeats die rather than sitting there.
/// Nothing is heard on a silent patch, which is the whole reason it is keyed
/// this way rather than run free.
class BucketEcho {
public:
  static constexpr float kMaxTimeSeconds = 2.0f;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Processes in place. Runs even with no input, since a tail is still a tail.
  void process(float *outL, float *outR, int numSamples,
               const EchoParams &p) noexcept;

  /// Longest tail the current settings can produce, for the host's benefit.
  float tailSeconds(const EchoParams &) const noexcept;

private:
  /// One line of buckets: its charge, its tone and its own clock.
  struct Line {
    std::vector<float> buffer;
    int write = 0;

    float damp = 0.0f; ///< the reconstruction filter, which is most of this
    float dc = 0.0f;   ///< and the coupling that keeps the charge centred

    double phase = 0.0; ///< where its clock has wandered to
    double rateHz = 0.31;

    Xorshift rng{1u}; ///< the hiss, which is its own per side

    void resize(int length);
    void clear() noexcept;

    /// Reads `delaySamples` back, interpolated, since a clock that wanders
    /// does not land on samples.
    float read(float delaySamples) const noexcept;
  };

  double sampleRate = 44100.0;
  int bufferLength = 0;

  Line left, right;

  /// The clock is wound to rather than set, like the tape's head distance: a
  /// line of buckets cannot change its rate instantly either.
  float smoothedDelay = -1.0f;

  /// What the expander is working from, which is the loudest thing it has
  /// heard lately. Shared between the sides, since one compander sees both.
  float envelope = 0.0f;

  bool wasEnabled = false;
};

} // namespace ovt
