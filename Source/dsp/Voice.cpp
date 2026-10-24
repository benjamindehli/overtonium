#include "Voice.h"

#include <type_traits>

#include <algorithm>

#include "Character.h"
#include "Exact.h"
#include "SineTable.h"

namespace ovt {

namespace {
/// Drift is meant to read as gentle chorusing rather than vibrato, so the
/// rates sit well below anything you would hear as a pitch wobble. Each
/// partial of each note draws its own rate from this range, log distributed.
constexpr double kDriftMinHz = 0.08;
constexpr double kDriftMaxHz = 1.10;

/// Corner of the one-pole pair that tilts the noise. Around a kilohertz splits
/// it into a usable "body" and "air" either side.
constexpr double kNoiseTiltHz = 1000.0;

/// What the noise channel adds to its string, +12 dB. A pluck rings at about
/// the level of the burst that struck it, and a burst a few milliseconds long
/// on a channel that is one layer among thirty-three is faint, so without it
/// a pluck could not stand beside a full organ even with the channel's fader
/// at the top. A breath, which builds up of its own accord, is the setting
/// that wants the fader brought down instead.
constexpr float kStringGain = 4.0f;

/// Below this an envelope is silent, so restarting it costs nothing. The same
/// figure the envelope itself calls the end of a release.
constexpr float kSilent = 1.0e-5f;

/// How long the Bulb character's filament takes to settle at a new pitch.
///
/// A tungsten filament of the size used for this has a thermal time constant
/// in the tens to hundreds of milliseconds, and the loop around it settles
/// slower than the filament does, which is what makes the bounce famous
/// enough to be the first thing anyone says about these oscillators. Half a
/// second is long enough to hear the level walking behind a bend rather than
/// moving with it.
constexpr double kBulbSettleSeconds = 0.5;

/// How far out of balance the lamp can get before it has nothing left to give,
/// in semitones.
///
/// A quarter of one, which is deliberately far less than the physics of a
/// Wien bridge would give you, and the reason is what moves the pitch here. A
/// bench oscillator is swept by a knob across a decade, and its lamp answers a
/// frequency that has doubled. This one is moved by vibrato, drift and a
/// finger, which is to say by cents, and a model faithful to the bench does
/// nothing at all at that scale: at a semitone of full scale, a 25 cent
/// vibrato came to 0.8 dB and drift to a hundredth of one, which is a
/// character nobody can hear. Scaled to the movements the instrument actually
/// makes, the same model reads as the circuit it is named after.
constexpr float kBulbFullScale = 0.25f;

/// How much of the level it gives up once it is that far behind.
///
/// A quarter of it at the limit, which is a little over two decibels, and
/// proportional below that. It has to stop
/// somewhere: this is an instrument with 32 oscillators playing at once, and
/// an amplitude movement that reads as character on one reads as a fault on
/// all of them together.
constexpr float kBulbDepth = 0.25f;

/// What the tremolo leaves of the fader.
///
/// Only ever takes away. At the top of its travel the shape reads 1 and
/// nothing is removed, at the bottom it reads -1 and the whole depth is, which
/// is why the amplitude offers one square rather than two: there is no second
/// direction for a unipolar one to go in.
///
/// Clamped because Random is a spline through its points and overshoots them
/// by a few percent, which would otherwise lift a partial above its own fader.
inline float tremoloGain(float shape, float depth) noexcept {
  return std::clamp(1.0f - depth * 0.5f * (1.0f - shape), 0.0f, 1.0f);
}
} // namespace

void Voice::setRenderRate(double newSampleRate) noexcept {
  // Drift is the one thing not recomputed here. Its step is latched at
  // note-on, so a note already sounding when the rate changes wanders at the
  // old speed until it is played again. Re-deriving it would need the rate
  // knob's value per partial, which is a lot of plumbing for an artefact that
  // lasts one note and only on a setting nobody changes mid-phrase.
  sampleRate = std::max(1.0, newSampleRate);

  for (auto &pt : partials)
    pt.env.setSampleRate(sampleRate);

  noise.env.setSampleRate(sampleRate);
  noise.string.setRate(sampleRate);

  // Roughly a 15 ms time constant, stepped once per control block.
  pressureCoef =
      1.0f - (float)std::exp(-(double)kControlBlock / (0.015 * sampleRate));

  lowpassCoef =
      1.0f - (float)std::exp(-6.283185307179586 * kNoiseTiltHz / sampleRate);
}

void Voice::prepare(double newSampleRate, uint32_t seed) noexcept {
  // Sized for the host's rate, the fastest the voice is ever run at, before
  // the render rate is set. See KarplusString::setRate.
  noise.string.prepare(newSampleRate);
  setRenderRate(newSampleRate);

  rng.reseed(seed);
  noise.rng.reseed(seed ^ 0x5bf03635u);
  wheelRng.reseed(seed ^ 0x2545f491u);

  reset();
}

void Voice::reset() noexcept {
  retuneSemis = 0.0;
  retuneStep = 0.0;

  for (auto &pt : partials) {
    pt.env.reset();
    pt.drift.reset();
    pt.phase = 0.0;
    pt.pitchLfo.reset();
    pt.ampLfo.reset();
    pt.lastGain = 0.0f;
    pt.gainPrimed = false;
    pt.lastWave = nullptr;
    pt.glideSemis = 0.0;
    pt.glideStep = 0.0;
  }

  noise.env.reset();
  noise.ampLfo.reset();
  noise.lowpassState = 0.0f;
  noise.lastGain = 0.0f;
  noise.gainPrimed = false;
  noise.string.reset();
  noise.stringAlive = false;
  noise.damperSeconds = 0.0f;
  leaks.fill(Leak{});
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;
  noiseVelocity = 0.0f;
  noisePressure = 0.0f;

  active = false;
  released = false;
  midiNote = -1;
  midiChannel = 0;
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;
  slide = 0.0f;
  slideRest = 0.0f;
  slideRested = false;
  pressureSmoothed = 0.0f;
}

void Voice::noteOn(int channel, int note, float velocity, const SynthParams &p,
                   double frequency) noexcept {
  // A different note on this voice starts from a silent string. The same key
  // struck again keeps it ringing and strikes it again, as a re-struck string
  // does.
  if (note != midiNote || channel != midiChannel) {
    noise.string.reset();
    noise.stringAlive = false;
  }

  noise.damperSeconds = 0.0f;

  // The leaking wheels turn all the time, so a note finds each at some point
  // in its turn. Drawn on every note whatever the character, like the shafts,
  // and kept where a leak is still sounding so it does not jump.
  for (auto &leak : leaks) {
    const auto start = (double)wheelRng.unipolar();

    if (leak.lastGain <= 0.0f)
      leak.phase = start;
  }

  midiNote = note;
  midiChannel = channel;
  retuneSemis = 0.0;
  retuneStep = 0.0;
  baseFreq = frequency > 0.0
                 ? frequency
                 : noteFrequency(note, p.global.temperament,
                                 p.global.tuningRoot, p.global.referenceHz);

  const float vel = std::clamp(velocity, 0.0f, 1.0f);

  // A fresh note starts unbent and under no pressure of its own. A retrigger
  // lands here too, which is what stops the previous note's bend carrying into
  // the new one on a channel being reused.
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;

  // The smoothing starts where the channel is already holding it rather than
  // at nothing.
  //
  // Per-note pressure is a fact about this key and begins at zero, which is
  // what the line above says. Channel pressure is a controller that was
  // already standing somewhere when the key went down, and a note that ramps
  // up to meet it spends the first 15 ms somewhere the player never asked
  // for. With a negative AFTERTOUCH amount that is a note starting at full
  // level and falling to silence, which is a thump on every key struck under
  // held pressure, and with a positive one it is an audible fade in.
  //
  // The smoothing is still what it was. It is there so that moving the
  // controller does not step the gain, and nothing about that wants the first
  // block of a note to start from a value the controller is not at.
  pressureSmoothed = std::clamp(p.global.aftertouch, 0.0f, 1.0f);

  // And the slide axis forgets where the last note started it, so the next one
  // takes its nought from wherever the controller puts it. See setSlide.
  slide = 0.0f;
  slideRest = 0.0f;
  slideRested = false;

  active = true;
  released = false;

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &pt = partials[(size_t)i];
    const auto &op = p.osc[(size_t)i];

    // A note starts at its own pitch. The engine sets it gliding afterwards
    // when one is due, from wherever it decides the glide comes from.
    pt.glideSemis = 0.0;
    pt.glideStep = 0.0;

    // Each strip decides for itself how much of the key velocity it takes,
    // latched here so a velocity change cannot alter a note already sounding.
    // The same shape the two strike scales below are built on, which is what
    // makes the three rows read alike: see Velocity.h.
    pt.velGain = velocityGain(op.velAmount, vel);

    // The rest of what the blow is worth. The gain above says how loud the
    // partial comes out, these two say how soon it starts and how quickly it
    // gets there.
    pt.delayScale = strikeDelayScale(op.strikeAmount, vel);
    pt.attackScale = strikeAttackScale(op.strikeAmount, vel);
    pt.onsetStale = true;

    // Where the partial starts and how fast it rises, the knob and the blow
    // together. See struckOnset.
    const auto onset = struckOnset(op.attack, pt.attackScale);

    pt.env.configure(op.delay * pt.delayScale, onset.seconds, op.decay,
                     op.sustain, op.swell, op.offLevel, op.release);

    // Starting from silence is only free while the partial is silent.
    //
    // Almost always it is: a note lands on a voice nothing else is using. The
    // exception is a note struck again while it is still sounding, which one
    // voice per key answers by retriggering where it stands rather than by
    // taking a second voice. Zeroing the level there, or moving the phase,
    // puts a step in the output the size of whatever was ringing, and a note
    // held by the sustain pedal is ringing at its sustain level.
    //
    // So the choice is made per partial from what it is actually doing. One
    // that has decayed away starts clean, with the coherent attack the setting
    // asks for. One still sounding keeps its level and its phase, and the new
    // note's attack takes over from there.
    const bool fresh = pt.env.getLevel() <= kSilent;

    pt.env.noteOn(fresh && p.global.phaseReset);

    // The lamp reads the pitch of whatever note this partial is now playing.
    // Left alone, a voice taken over by a note an octave away would open with
    // the sag of a bend it never made. Only when the partial had stopped: one
    // still ringing is a continuous tone, and a real oscillator handed a new
    // frequency is exactly the case this character exists to show.
    if (fresh)
      pt.semisPrimed = false;

    // Drawn on every note whatever the character, so the stream does not
    // depend on which one is chosen.
    const auto shaftStart = (double)wheelRng.unipolar();

    if (fresh)
      pt.shaftTurns = shaftStart;

    // A fresh rate per partial per note. Reusing one rate would turn 32
    // independent wanders into a single detune.
    const double rate = kDriftMinHz * std::pow(kDriftMaxHz / kDriftMinHz,
                                               (double)rng.unipolar());
    pt.drift.restart(rng, rate, sampleRate / (double)kControlBlock);

    if (fresh && p.global.phaseReset) {
      pt.phase = (double)onset.turns;

      // Fresh points per note per partial, so the random shapes wander
      // independently rather than all 32 tracing one contour, which is the
      // same reason the drift draws its own.
      pt.pitchLfo.restart(rng);
      pt.ampLfo.restart(rng);
    }

    // The gain ramp restarts from whatever this partial's strip currently asks
    // for rather than from the previous note's trailing value. Only for a
    // partial starting clean: one carrying on wants the ramp, since a second
    // strike at a different velocity moves the gain and the ramp is what keeps
    // that from being a step of its own.
    if (fresh)
      pt.gainPrimed = false;
  }

  {
    const auto &np = p.noise;

    noise.velGain = velocityGain(np.velAmount, vel);

    noise.delayScale = strikeDelayScale(np.strikeAmount, vel);
    noise.attackScale = strikeAttackScale(np.strikeAmount, vel);

    noise.env.configure(np.delay * noise.delayScale,
                        struckAttack(np.attack, noise.attackScale), np.decay,
                        np.sustain, np.swell, np.offLevel, np.release);

    // The same rule as the partials. Noise has no phase worth resetting, but
    // its level steps just as audibly.
    const bool fresh = noise.env.getLevel() <= kSilent;

    noise.env.noteOn(fresh && p.global.phaseReset);

    if (fresh && p.global.phaseReset)
      noise.ampLfo.restart(noise.rng);

    if (fresh)
      noise.gainPrimed = false;
  }
}

void Voice::retune(int channel, int note, double frequency) noexcept {
  // A key going down while the phrase is still sounding says nothing about
  // bend or pressure, and on one channel the ones already in hand belong to
  // the phrase rather than to the key that started it, so they are left
  // alone. A key on a channel of its own is another matter: with MPE every
  // note brings its own bend, pressure and slide, and a phrase carried onto
  // that key takes the new finger's rather than keeping the last one's.
  if (channel != midiChannel) {
    noteBendSemitones = 0.0f;
    polyPressure = 0.0f;
    slide = 0.0f;
    slideRest = 0.0f;
    slideRested = false;
  }

  midiNote = note;
  midiChannel = channel;
  baseFreq = frequency;
  retuneSemis = 0.0;
  retuneStep = 0.0;

  released = false;
}

void Voice::retuneTo(double frequency, double seconds) noexcept {
  if (frequency <= 0.0 || baseFreq <= 0.0)
    return;

  retuneSemis += 12.0 * std::log2(baseFreq / frequency);
  baseFreq = frequency;
  retuneStep = seconds > 0.0 ? std::abs(retuneSemis) / (seconds * sampleRate)
                             : std::abs(retuneSemis);
}

Voice::Pitches Voice::notePitches() const noexcept {
  Pitches out{};
  const double at = 12.0 * std::log2(baseFreq);

  for (int i = 0; i < kNumHarmonics; ++i)
    out[(size_t)i] = at + partials[(size_t)i].glideSemis;

  return out;
}

Voice::Pitches Voice::uniformPitches(double frequency) noexcept {
  Pitches out{};
  out.fill(12.0 * std::log2(std::max(frequency, 1.0e-3)));
  return out;
}

void Voice::startGlide(const Pitches &from,
                       const GlideSettings &glide) noexcept {
  const double to = 12.0 * std::log2(baseFreq);
  const double blocksPerSecond = sampleRate / (double)kControlBlock;

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &pt = partials[(size_t)i];
    const double seconds = (double)glide.seconds[(size_t)i];
    const double offset = from[(size_t)i] - to;

    if (seconds <= 0.0 || std::abs(offset) < 1.0e-6) {
      pt.glideSemis = 0.0;
      pt.glideStep = 0.0;
      continue;
    }

    // How long this one takes. Under Rate the time is per octave, so the step
    // comes out the same whatever the distance and only the arrival moves.
    const double span = glide.mode == GlideMode::Rate
                            ? seconds * std::abs(offset) / 12.0
                            : seconds;

    pt.glideSemis = offset;
    pt.glideStep = std::abs(offset) / std::max(1.0, span * blocksPerSecond);
  }
}

void Voice::noteOff(float lift) noexcept {
  if (!active)
    return;

  released = true;

  // The same figure to all thirty-three, since it is one gesture. What each
  // does with it is its own, because each carries its own key-off level and
  // its own level to be sitting at when the key comes up.
  for (auto &pt : partials)
    pt.env.noteOff(lift);

  noise.env.noteOff(lift);
}

void Voice::fade(float seconds) noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.forceRelease(seconds);

  noise.env.forceRelease(seconds);
  noise.damperSeconds = seconds;
}

void Voice::takeOver(const Voice &from) noexcept {
  for (size_t i = 0; i < partials.size(); ++i) {
    auto &pt = partials[i];
    const auto &src = from.partials[i];

    pt.env = src.env;
    pt.pitchLfo = src.pitchLfo;
    pt.ampLfo = src.ampLfo;
    pt.drift = src.drift;
    pt.velGain = src.velGain;
    pt.delayScale = src.delayScale;
    pt.attackScale = src.attackScale;
    pt.onsetStale = true;
    pt.bulbSettled = src.bulbSettled;
    pt.semisPrimed = src.semisPrimed;
    pt.shaftTurns = src.shaftTurns;

    // Already sounding, so the gain ramp starts from where the other voice's
    // was rather than priming itself afresh, and the oscillator starts its
    // cycle on a crossing so the level it arrives at cannot step.
    pt.lastGain = src.lastGain;
    pt.gainPrimed = src.gainPrimed;
    pt.phase = 0.0;
  }

  noise.env = from.noise.env;
  noise.ampLfo = from.noise.ampLfo;
  noise.velGain = from.noise.velGain;
  noise.delayScale = from.noise.delayScale;
  noise.attackScale = from.noise.attackScale;
  noise.lastGain = from.noise.lastGain;
  noise.gainPrimed = from.noise.gainPrimed;

  leaks = from.leaks;

  pressureSmoothed = from.pressureSmoothed;
}

void Voice::steal() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.forceRelease(0.004f);

  noise.env.forceRelease(0.004f);
  noise.damperSeconds = 0.004f;
}

void Voice::render(float *left, float *right, int numSamples,
                   const SynthParams &p,
                   const SharedModulation &shared) noexcept {
  if (!active || numSamples <= 0)
    return;

  const auto &characters = CharacterTables::instance();

  // Which thirty-two units this character's rack came out at. One lookup for
  // the whole call: the spread is fixed, so it is the same rack for every
  // block and every note. See UnitSpread.
  const auto &unit = UnitSpread::instance().rack(p.global.character);

  const bool wheel = p.global.character == Character::Wheel;
  const auto &sine = SineTable::instance();

  // One pole per control block rather than per sample, which is where every
  // other slow thing in here is worked out.
  const float bulbCoef =
      sampleRate > 0.0
          ? (float)(1.0 - std::exp(-(double)kControlBlock /
                                   (kBulbSettleSeconds * sampleRate)))
          : 1.0f;

  // Equal-power pan positions, one per partial.
  //
  // Placing them by hand rather than fanning them from one control is the
  // difference between a width setting and an arrangement: a partial can be
  // put opposite the one a semitone away from it, or the octaves left and the
  // sevenths right, which is not a shape any single knob could have produced.
  std::array<float, kNumHarmonics> panL{}, panR{};

  for (int i = 0; i < kNumHarmonics; ++i) {
    const float pan = std::clamp(p.osc[(size_t)i].pan, -1.0f, 1.0f);

    panL[(size_t)i] = std::sqrt(0.5f * (1.0f - pan));
    panR[(size_t)i] = std::sqrt(0.5f * (1.0f + pan));
  }

  // How much of each partial the keyboard tracking leaves at this pitch.
  //
  // Taken from the nominal position of the partial rather than from the
  // frequency it is momentarily at, so vibrato and drift do not modulate the
  // brightness, and worked out once per call rather than once per control
  // block, since none of it moves while a note sounds.
  std::array<float, kNumHarmonics> track{};

  // MPE slide, where it is aimed at brightness. Tracking thins the top of the
  // series, so a finger pushed forward takes tracking away and pulled back
  // adds more. It cannot brighten past what the patch already is: there is no
  // negative tracking, since trackingGain returns 1 for anything at or below
  // zero. A patch with tracking off is therefore as bright as it gets, and
  // slide can only darken it.
  const auto tracking =
      p.global.slideDest == SlideDestination::Brightness
          ? std::max(0.0f, p.global.trackDbPerOctave - slide * kSlideTrackDb)
          : p.global.trackDbPerOctave;

  if (tracking > 0.0f) {
    for (int i = 0; i < kNumHarmonics; ++i) {
      const auto semis = semitoneOffset(i, (double)blendOf(p, i),
                                        (double)p.global.stretchCents);

      track[(size_t)i] = trackingGain(baseFreq * std::exp2(semis / 12.0),
                                      baseFreq, (double)tracking);
    }
  } else {
    track.fill(1.0f);
  }

  partialPeaks.fill(0.0f);
  partialEnvelopes.fill(0.0f);
  partialTremolos.fill(0.0f);
  partialPitches.fill(0.0f);
  partialVelocities.fill(0.0f);
  partialPressures.fill(0.0f);
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;
  noiseVelocity = 0.0f;
  noisePressure = 0.0f;

  const float pressureTarget =
      std::max(std::clamp(p.global.aftertouch, 0.0f, 1.0f), polyPressure);

  // With the render rate turned down, the guard that keeps partials from
  // folding is exactly the wrong thing to have on: folding is the sound being
  // asked for. A converter running at 8 kHz does not quietly mute everything
  // above 4 kHz, it wraps it back down, and so does this.
  const bool foldAliases = p.lofi.rateHz > 0.0;

  // Which control block of this call we are on, for reading the modulators
  // every voice shares. The engine filled them for exactly these blocks.
  int block = 0;

  for (int start = 0; start < numSamples; start += kControlBlock, ++block) {
    const int len = std::min(kControlBlock, numSamples - start);

    pressureSmoothed += (pressureTarget - pressureSmoothed) * pressureCoef;
    const float pressure = pressureSmoothed;

    // How loud the note's partials are together this block, as power, for
    // the wheels that leak into it. Each partial's wheel leaks on its own and
    // the leaks do not line up in phase, so their powers add.
    double leakPower = 0.0;

    for (int i = 0; i < kNumHarmonics; ++i) {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];

      // Scaled by what the note-on made of the velocity, so turning either
      // knob under a sounding note moves it by the same proportion the blow
      // earned rather than throwing that away. The delay is latched in samples
      // at note-on and only read again by the next one, so it is scaled here to
      // keep the two calls agreeing rather than because this one uses it.
      if (pt.onsetStale || !exactly(op.attack, pt.onsetFor)) {
        pt.onsetFor = op.attack;
        pt.onsetSeconds = struckOnset(op.attack, pt.attackScale).seconds;
        pt.onsetStale = false;
      }

      pt.env.configure(op.delay * pt.delayScale, pt.onsetSeconds, op.decay,
                       op.sustain, op.swell, op.offLevel, op.release);

      if (!pt.env.isActive())
        continue;

      // ---- pitch ------------------------------------------------------------
      const double pmPhaseInc = (double)op.pmRateHz / sampleRate;

      // Read where the shape is now and step it at the end of the block, so
      // the figure used is the one the block starts on. Which shape it is
      // changes what comes out and nothing about the timing: the square ones
      // land their edge on a block boundary, two thirds of a millisecond of
      // grid, which no ear finds on a modulator running at a few hertz.
      //
      // Off the channel's own modulator when the keyboard is sharing one,
      // which the engine worked out for this very block before any voice ran.
      // See SharedModulation.
      const auto pmShapeValue =
          shared.pitch != nullptr
              ? shared.pitch[(size_t)(i * shared.stride + block)]
              : pt.pitchLfo.value(op.pmShape);

      const double pmCents = op.pmDepthCents > 0.0f
                                 ? (double)(pmShapeValue * op.pmDepthCents)
                                 : 0.0;

      // Advanced unconditionally so that turning the knob up mid-note joins the
      // wander already in progress instead of jumping.
      const double driftCents = (double)(pt.drift.advance(rng) * op.driftCents);

      // The unit's own tuning error goes in with the modulation rather than
      // beside it, so everything downstream of this line sees the pitch the
      // partial is actually at: the table the character reads, how hard a slew
      // limit bites, and what the lamp is settling towards.
      const double semis =
          semitoneOffset(i, (double)blendOf(p, i),
                         (double)p.global.stretchCents) +
          (pmCents + driftCents + (double)unit.cents[(size_t)i]) * 0.01 +
          (double)(p.global.bendSemitones + noteBendSemitones) + pt.glideSemis +
          retuneSemis;

      // Read at the start of the block and stepped for the next, as the
      // modulators are. A glide that is not running costs a compare.
      // Nought is set exactly by the line below, so the compare is exact too.
      // How far the glide still has to go, for the needle, taken before this
      // block's step so it matches the pitch the block plays at.
      const double glideCents = (pt.glideSemis + retuneSemis) * 100.0;

      if (!exactly(pt.glideSemis, 0.0)) {
        const double remaining = std::abs(pt.glideSemis) - pt.glideStep;
        pt.glideSemis =
            remaining > 0.0 ? std::copysign(remaining, pt.glideSemis) : 0.0;
      }

      // ---- the shaft the tonewheel turns on
      // ----------------------------------
      //
      // Pitch and level move once a turn and a little at twice that, by what
      // this unit was drawn with, and the motor that drives every wheel moves
      // the pitch at its own rate on top. The shaft rate comes from the pitch
      // the partial is at before any of this, so the wobble cannot change the
      // rate it wobbles at. Worked out once a block, like the modulators: a
      // shaft turns at most 32 times a second and a block is 1500 a second.
      double wobbleCents = 0.0;
      float wobbleGain = 1.0f;

      if (wheel) {
        const double unwobbled = baseFreq * std::exp2(semis / 12.0);
        const double turn = pt.shaftTurns;
        const double motor =
            wrapPhase(shared.motorTurns + (double)start * shared.motorStep);

        // The twice-a-turn pitch was measured in hertz, which is the same on
        // every wheel, and is turned into cents at this one's pitch.
        const double twiceCents = 1731.2340490667560 *
                                  (double)unit.twiceHz[(size_t)i] /
                                  std::max(1.0, unwobbled);

        wobbleCents =
            (double)unit.shaftCents[(size_t)i] * (double)sine.at(turn) +
            twiceCents * (double)sine.at(wrapPhase(2.0 * turn)) +
            kWheelMotorCents * (double)sine.at(motor);

        wobbleGain = 1.0f +
                     unit.shaftLevel[(size_t)i] *
                         sine.at(wrapPhase(
                             turn + (double)unit.shaftLevelTurns[(size_t)i])) +
                     unit.twiceLevel[(size_t)i] *
                         sine.at(wrapPhase(2.0 * turn +
                                           (double)unit.twiceTurns[(size_t)i]));

        pt.shaftTurns = wrapPhase(turn + wheelShaftHz(unwobbled) * (double)len /
                                             sampleRate);
      }

      const double freq =
          baseFreq * std::exp2((semis + wobbleCents * 0.01) / 12.0);

      // Reduced into the first turn of the table. Sampling a sinusoid above
      // the rate produces the same numbers as sampling the one it folds to, so
      // this is not an approximation of aliasing, it is the aliasing, and the
      // inner loop keeps its single compare instead of a modulo per sample.
      double inc = freq / sampleRate;
      inc -= std::floor(inc);

      // ---- amplitude --------------------------------------------------------
      const double amPhaseInc = (double)op.amRateHz / sampleRate;

      // Both ends of the block, because the gain ramps between them: where the
      // shape is now, and where stepping it lands. That ramp is also what makes
      // a square edge survivable here. The level does not jump, it slides
      // across the block, which at 32 samples is a couple of thirds of a
      // millisecond and reads as an edge rather than as a click.
      //
      // Stepped here whether or not it is the modulator being read, so that a
      // channel handed back its own picks up where this note would have been
      // rather than from wherever it was left. The same reason the drift is
      // advanced unconditionally above.
      const auto ownStart = pt.ampLfo.value(op.amShape, kAmpShapeOffset);
      const auto ownEnd = pt.ampLfo.advance(rng, amPhaseInc * (double)len,
                                            op.amShape, kAmpShapeOffset);

      const auto sharedAmp = shared.amp != nullptr;
      const auto at = (size_t)(i * shared.stride + block);

      const float amStart =
          tremoloGain(sharedAmp ? shared.amp[at] : ownStart, op.amDepth);

      const float amEnd =
          tremoloGain(sharedAmp ? shared.amp[at + 1] : ownEnd, op.amDepth);

      const float nyq = foldAliases ? 1.0f : nyquistGain(freq, sampleRate);

      // ---- which oscillator this is ----------------------------------------
      //
      // The character's harmonics are harmonics like any others and fold like
      // any others, so the table is chosen by how much room this partial has
      // left under Nyquist at the pitch it is at this moment. A partial low
      // enough gets all of them, one near the top gets a plain sine, and the
      // ones in between lose them from the top down. With the converter's rate
      // turned down, folding is the sound being asked for and the full table
      // is what folds.
      //
      // The frequency goes in as well as the room above it, because one
      // character is a rate limit rather than a shape, and how hard that bites
      // depends on how fast the wave is asking the amplifier to move.
      const auto &wave =
          characters.table(p.global.character, freq,
                           foldAliases ? kMostCharacterHarmonics
                                       : highestHarmonicUnder(freq, sampleRate),
                           unit.drive[(size_t)i]);

      // ---- what the lamp has not caught up with ----------------------------
      //
      // A Wien bridge holds its level with a lamp. The filament's resistance
      // follows how hard the loop is driving it, but only as fast as a
      // filament can heat and cool, and the gain the loop needs changes with
      // the frequency because no two ganged parts track each other exactly.
      // Move the pitch and the level sags until the lamp has settled at the
      // new one.
      //
      // One pole, chasing the pitch with the filament's own time constant.
      // What is left over is the amplitude error the loop has not corrected
      // yet. Here the pitch is moved constantly, by vibrato, drift, the wheel
      // and a finger on an MPE key, so the lamp is never quite caught up and
      // the level breathes behind everything the hand does.
      float bulb = 1.0f;

      if (p.global.character == Character::Bulb) {
        if (!pt.semisPrimed) {
          pt.bulbSettled = semis; // a note starts in balance, not sagging
          pt.semisPrimed = true;
        }

        pt.bulbSettled += (semis - pt.bulbSettled) * bulbCoef;

        // Clamped rather than curved: a filament runs out of range too, and a
        // clamp is a compare where a soft limit is a divide, on something that
        // runs 512 times per control block.
        const float behind = std::clamp(
            (float)(semis - pt.bulbSettled) / kBulbFullScale, -1.0f, 1.0f);

        bulb = 1.0f - behind * kBulbDepth;
      }

      // Aftertouch adds to the fader instead of scaling it, which is what lets
      // a strip sitting at zero be brought in by pressure alone. Velocity only
      // ever touches the fader's own contribution.
      // Aftertouch adds, so a negative amount subtracts and the clamp floors
      // it at silence. No special case needed for the inverted direction.
      const float level =
          std::clamp(op.volume * pt.velGain +
                         std::clamp(op.atAmount, -1.0f, 1.0f) * pressure,
                     0.0f, 1.0f);
      // The lamp joins the fader, the tracking and the Nyquist fade here, so
      // it rides the same per-sample ramp they do and a level that is settling
      // slides rather than steps from block to block. The unit's own level
      // error joins them as a plain multiply, being a property of the
      // oscillator rather than of anything the player is doing.
      const float base = op.audible ? level * nyq * track[(size_t)i] * bulb *
                                          unit.gain[(size_t)i] * wobbleGain
                                    : 0.0f;
      const float gEnd = base * amEnd;

      if (!pt.gainPrimed) {
        pt.lastGain = base * amStart;
        pt.gainPrimed = true;
      }

      // Ramping from the previous block's gain to this one covers tremolo,
      // volume moves, mute/solo and the Nyquist fade with a single
      // interpolation.
      float g = pt.lastGain;
      const float gInc = (gEnd - g) / (float)len;

      // Stepped whatever the depth says, so turning the knob up mid-note joins
      // the motion already under way instead of starting it again. The
      // amplitude was stepped above, where its value was needed.
      pt.pitchLfo.advance(rng, pmPhaseInc * (double)len, op.pmShape);

      pt.lastGain = gEnd;

      if (wheel) {
        const double amplitude = (double)(pt.env.getLevel() * gEnd);
        leakPower += amplitude * amplitude;
      }

      // One multiply and one compare per partial per control block. Both terms
      // are already to hand, so metering costs essentially nothing here.
      partialPeaks[(size_t)i] =
          std::max(partialPeaks[(size_t)i], pt.env.getLevel() * gEnd);

      // The same again for the lamps: everything here was worked out above for
      // the oscillator's own use, so this is four stores and a compare.
      //
      // The envelope carries its stage in its sign. Swell and release are the
      // two that run after the key is up, and they are the ones the second
      // lamp takes over.
      const auto stage = pt.env.getStage();
      const auto afterKeyOff =
          stage == Envelope::Stage::Swell || stage == Envelope::Stage::Release;

      partialEnvelopes[(size_t)i] =
          afterKeyOff ? -pt.env.getLevel() : pt.env.getLevel();

      // What the tremolo has taken off, rather than what it has left. A
      // partial with no tremolo on it then reads zero instead of full, which
      // is a lamp that is dark rather than one that is on and never moves.
      partialTremolos[(size_t)i] = 1.0f - amEnd;
      // Where the partial is against its note: the modulator, the drift and
      // whatever of a glide is still to travel. Pitch bend is left out, since
      // it moves every partial alike and is already on the wheel.
      partialPitches[(size_t)i] = (float)(pmCents + driftCents + glideCents);

      // One lamp for each of the two rows under the heading, each showing
      // what its own row is doing and nothing else. The velocity half is the
      // gain itself, which the note-on already latched, so it costs a read.
      partialVelocities[(size_t)i] = pt.velGain;
      partialPressures[(size_t)i] =
          std::abs(std::clamp(op.atAmount, -1.0f, 1.0f) * pressure);

      if ((g <= 1.0e-7f && gEnd <= 1.0e-7f) || pt.env.isSilentlyHolding()) {
        // Inaudible right now: muted, faded out above Nyquist, fader at zero,
        // or decayed to a sustain of nothing and waiting for the key to come
        // up. Keep the envelope and phase moving so unmuting or letting go
        // mid-note picks up in the right place, but skip the oscillator.
        for (int n = 0; n < len; ++n)
          pt.env.tick();

        pt.phase = wrapPhase(pt.phase + inc * (double)len);

        // Whatever it would have been reading, so that coming back audible
        // does not cross-fade from a table it has not been heard on.
        pt.lastWave = &wave;
        continue;
      }

      float *l = left + start;
      float *r = right + start;

      double ph = pt.phase;
      const float pl = panL[(size_t)i];
      const float pr = panR[(size_t)i];

      // Which table it was reading last block, and which it is reading now.
      //
      // They differ whenever a partial crosses one of the lines the tables are
      // divided by: how much room is left under Nyquist for every character,
      // and how hard the limit is biting for the rate-limited one. Two tables
      // hold different numbers at the same phase, so swapping between them puts
      // a step in the wave, and a step is a click. A vibrato sitting across one
      // of those lines crosses it twice a cycle and clicks at twice the
      // vibrato rate.
      //
      // So the block that changes tables is played as a cross-fade from one to
      // the other, the same way the gain slides across a block rather than
      // stepping at the edge of it. It costs a second table read for 32
      // samples, on the rare block that crosses, and nothing at all on the
      // ones that do not.
      const Wave *const previous = pt.lastWave;
      pt.lastWave = &wave;

      if (previous == nullptr || previous == &wave) {
        for (int n = 0; n < len; ++n) {
          const float s = wave.at(ph) * pt.env.tick() * g;

          l[n] += s * pl;
          r[n] += s * pr;

          ph += inc;
          if (ph >= 1.0)
            ph -= 1.0;

          g += gInc;
        }
      } else {
        float mix = 0.0f;
        const float mixInc = 1.0f / (float)len;

        for (int n = 0; n < len; ++n) {
          const float was = previous->at(ph);
          const float is = wave.at(ph);
          const float s = (was + (is - was) * mix) * pt.env.tick() * g;

          l[n] += s * pl;
          r[n] += s * pr;

          ph += inc;
          if (ph >= 1.0)
            ph -= 1.0;

          g += gInc;
          mix += mixInc;
        }
      }

      pt.phase = ph;
    }

    // Where the key itself sits, which is what the leaking wheels are placed
    // against: the note, the bends, the retune and the first partial's glide,
    // and none of the partials' own tuning.
    if (wheel)
      renderLeaks(left + start, right + start, len,
                  baseFreq * std::exp2(((double)(p.global.bendSemitones +
                                                 noteBendSemitones) +
                                        partials[0].glideSemis + retuneSemis) /
                                       12.0),
                  leakPower, foldAliases);

    renderNoise(left + start, right + start, len, p, pressure, shared, block);

    // The adaptive retune moves the whole note, so it steps once a block
    // rather than once a partial.
    if (!exactly(retuneSemis, 0.0)) {
      const double remaining = std::abs(retuneSemis) - retuneStep * (double)len;
      retuneSemis =
          remaining > 0.0 ? std::copysign(remaining, retuneSemis) : 0.0;
    }
  }

  active = noise.env.isActive() || noise.stringAlive ||
           std::any_of(partials.begin(), partials.end(),
                       [](const Partial &pt) { return pt.env.isActive(); });

  if (!active) {
    released = false;
    midiNote = -1;

    // The partials it followed have finished, so whatever is left of it is
    // below anything audible, and the next note finds its wheels afresh.
    for (auto &leak : leaks)
      leak.lastGain = 0.0f;
  }
}

void Voice::renderLeaks(float *left, float *right, int len, double keyFreq,
                        double power, bool foldAliases) noexcept {
  const auto &tables = CharacterTables::instance();
  const auto loudness = (float)std::sqrt(power);

  // Centred, as the leaks come from the whole generator rather than from
  // where any one partial is panned.
  constexpr float kCentre = 0.70710678f;

  for (size_t k = 0; k < leaks.size(); ++k) {
    auto &leak = leaks[k];
    const double freq = keyFreq * kWheelLeaks[k].ratio;

    const float target =
        freq >= kLowestWheelHz
            ? loudness * (float)std::pow(10.0, kWheelLeaks[k].db / 20.0) *
                  (foldAliases ? 1.0f : nyquistGain(freq, sampleRate))
            : 0.0f;

    double inc = freq / sampleRate;
    inc -= std::floor(inc);

    if (leak.lastGain <= 1.0e-9f && target <= 1.0e-9f) {
      leak.phase = wrapPhase(leak.phase + inc * (double)len);
      leak.lastGain = 0.0f;
      continue;
    }

    // A leaking wheel is a wheel, so it has the shape the wheels at its pitch
    // have. A step between two tables at -50 dB is far below hearing, so a
    // leak that glides across a band edge changes table without the
    // cross-fade a partial uses.
    const auto &wave =
        tables.table(Character::Wheel, freq,
                     foldAliases ? kMostCharacterHarmonics
                                 : highestHarmonicUnder(freq, sampleRate));

    float g = leak.lastGain;
    const float gInc = (target - g) / (float)len;
    double ph = leak.phase;

    for (int n = 0; n < len; ++n) {
      const float s = wave.at(ph) * g * kCentre;

      left[n] += s;
      right[n] += s;

      ph += inc;
      if (ph >= 1.0)
        ph -= 1.0;

      g += gInc;
    }

    leak.phase = ph;
    leak.lastGain = target;
  }
}

void Voice::renderNoise(float *left, float *right, int len,
                        const SynthParams &p, float pressure,
                        const SharedModulation &shared, int block) noexcept {
  const auto &np = p.noise;

  noise.env.configure(np.delay * noise.delayScale,
                      struckAttack(np.attack, noise.attackScale), np.decay,
                      np.sustain, np.swell, np.offLevel, np.release);

  const float amount = std::clamp(np.stringAmount, 0.0f, 1.0f);
  const bool stringOn = amount > 0.0f;

  if (!stringOn)
    noise.stringAlive = false;

  // The string can still be ringing after the noise has stopped.
  if (!noise.env.isActive() && !noise.stringAlive)
    return;

  const double amPhaseInc = (double)np.amRateHz / sampleRate;

  // The same two reads the partials take, for the same reason: the gain ramps
  // between them across the block.
  // Off the shared modulator when the keyboard is sharing one, the same as
  // every partial above. The noise channel's run sits after the 32 of them.
  const auto ownStart = noise.ampLfo.value(np.amShape, kAmpShapeOffset);
  const auto ownEnd = noise.ampLfo.advance(noise.rng, amPhaseInc * (double)len,
                                           np.amShape, kAmpShapeOffset);

  const auto at = (size_t)(kNumHarmonics * shared.stride + block);

  const float amStart = tremoloGain(
      shared.amp != nullptr ? shared.amp[at] : ownStart, np.amDepth);

  const float amEnd = tremoloGain(
      shared.amp != nullptr ? shared.amp[at + 1] : ownEnd, np.amDepth);

  const float level =
      std::clamp(np.volume * noise.velGain +
                     std::clamp(np.atAmount, -1.0f, 1.0f) * pressure,
                 0.0f, 1.0f);
  const float gEnd = (np.audible ? level : 0.0f) * amEnd;

  if (!noise.gainPrimed) {
    noise.lastGain = (np.audible ? level : 0.0f) * amStart;
    noise.gainPrimed = true;
  }

  float g = noise.lastGain;
  const float gInc = (gEnd - g) / (float)len;

  noise.lastGain = gEnd;

  noisePeak = std::max(noisePeak, noise.env.getLevel() * gEnd);

  {
    const auto stage = noise.env.getStage();
    const auto afterKeyOff =
        stage == Envelope::Stage::Swell || stage == Envelope::Stage::Release;

    noiseEnvelope = afterKeyOff ? -noise.env.getLevel() : noise.env.getLevel();
    noiseTremolo = 1.0f - amEnd;
    noiseVelocity = noise.velGain;
    noisePressure = std::abs(std::clamp(np.atAmount, -1.0f, 1.0f) * pressure);
  }

  if (g <= 1.0e-7f && gEnd <= 1.0e-7f) {
    for (int n = 0; n < len; ++n)
      noise.env.tick();

    // Nothing of the channel is heard, so neither is its string.
    noise.stringAlive = false;
    return;
  }

  // Colour tilts between the two halves of a complementary one-pole pair.
  // Both at unity reconstructs the original white noise, so the centre of the
  // knob is genuinely flat rather than merely filtered less.
  const float colour = std::clamp(np.colour, 0.0f, 1.0f);
  const float lowMix = colour < 0.5f ? 1.0f : 1.0f - (colour - 0.5f) * 2.0f;
  const float highMix = colour < 0.5f ? colour * 2.0f : 1.0f;

  // Placed the same way a partial is, so the one channel that is not part of
  // the series still sits somewhere rather than always up the middle.
  const float pan = std::clamp(np.pan, -1.0f, 1.0f);
  const float panL = std::sqrt(0.5f * (1.0f - pan));
  const float panR = std::sqrt(0.5f * (1.0f + pan));

  // The string, struck by the noise after COLOUR and its envelope have shaped
  // it, so COLOUR decides how hard and bright the strike is and the envelope
  // decides whether it is struck once or blown into. Tuned to the pitch the
  // fundamental is sounding at, bends, its glide and Adaptive's slide
  // included, without the fundamental's own vibrato and drift, which belong to
  // that partial rather than to the note.
  if (stringOn) {
    const double semis = (double)(p.global.bendSemitones + noteBendSemitones) +
                         partials[0].glideSemis + retuneSemis;

    // RELEASE is the damper. Once the key is up the string dies away over
    // the channel's release time as well as its own, so it has gone by the
    // time the envelope has. A steal or a fade brings a damper of its own.
    double decay = std::max(1.0e-3, (double)np.stringDecay);
    if (released)
      decay = 1.0 / (1.0 / decay + 1.0 / std::max(5.0e-3, (double)np.release));
    if (noise.damperSeconds > 0.0f)
      decay = 1.0 / (1.0 / decay + 1.0 / (double)noise.damperSeconds);

    noise.string.setBlock(baseFreq * std::exp2(semis / 12.0), decay,
                          np.stringDamp, np.stringPoint, len);
  }

  float rungPeak = 0.0f;

  for (int n = 0; n < len; ++n) {
    const float white = noise.rng.bipolar();

    noise.lowpassState += lowpassCoef * (white - noise.lowpassState);
    const float high = white - noise.lowpassState;

    float out =
        (lowMix * noise.lowpassState + highMix * high) * noise.env.tick();

    // Crossfaded by STRING, and by how much of the string can be heard at
    // this pitch: where the loop is too short to tune, the noise is left as
    // it was.
    if (stringOn) {
      const float rung = noise.string.tick(out) * kStringGain;
      rungPeak = std::max(rungPeak, std::abs(rung));
      out += amount * noise.string.audibility() * (rung - out);
    }

    const float s = out * g;

    left[n] += s * panL;
    right[n] += s * panR;

    g += gInc;
  }

  // Alive while it is being struck or is still above a millionth of full
  // scale, a hundred and twenty decibels down.
  noise.stringAlive = stringOn && (noise.env.isActive() || rungPeak >= 1.0e-6f);
}

} // namespace ovt
