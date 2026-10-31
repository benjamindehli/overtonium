#include "Voice.h"

#include <type_traits>

#include <algorithm>

#include "Character.h"
#include "Exact.h"
#include "SineTable.h"

namespace ovt {

namespace {
/// Range of drift rates, low enough to read as gentle chorusing rather than
/// vibrato. Each partial of each note draws its own rate, log distributed.
constexpr double kDriftMinHz = 0.08;
constexpr double kDriftMaxHz = 1.10;

/// Corner of the one-pole pair that tilts the noise, splitting it into
/// "body" and "air".
constexpr double kNoiseTiltHz = 1000.0;

/// What the noise channel adds to its string, +12 dB, so a short pluck can
/// stand beside a full organ.
constexpr float kStringGain = 4.0f;

/// Below this an envelope is silent, so restarting it costs nothing. The same
/// figure the envelope itself calls the end of a release.
constexpr float kSilent = 1.0e-5f;

/// How long the Bulb character's filament takes to settle at a new pitch,
/// long enough to hear the level follow a bend.
constexpr double kBulbSettleSeconds = 0.5;

/// How far out of balance the lamp can get before it has nothing left to
/// give, in semitones. Far less than a real Wien bridge, scaled to the
/// cents that vibrato, drift and a finger move the pitch by, so the effect
/// can be heard.
constexpr float kBulbFullScale = 0.25f;

/// How much of the level it gives up once it is that far behind, a little
/// over two decibels at the limit and proportional below it. Kept small
/// since 32 oscillators move together.
constexpr float kBulbDepth = 0.25f;

/// What the tremolo leaves of the fader. It only takes away: a shape of 1
/// removes nothing and -1 removes the whole depth.
///
/// Clamped because Random is a spline that overshoots its points by a few
/// percent, which would otherwise lift a partial above its own fader.
inline float tremoloGain(float shape, float depth) noexcept {
  return std::clamp(1.0f - depth * 0.5f * (1.0f - shape), 0.0f, 1.0f);
}
} // namespace

void Voice::setRenderRate(double newSampleRate) noexcept {
  // Drift is the one thing not recomputed here. Its step is latched at
  // note-on, so a sounding note keeps the old drift speed until it is played
  // again.
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
  vcoRng.reseed(seed ^ 0x7f4a7c15u);

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
    pt.wanderCents = 0.0;
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
  blockPowers.fill(0.0f);
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

  // A fresh note starts unbent and under no pressure of its own, so a reused
  // channel does not carry the previous note's bend.
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;

  // The smoothing starts where channel pressure already is rather than at
  // zero, so a note struck under held pressure does not fade in or thump.
  pressureSmoothed = std::clamp(p.global.aftertouch, 0.0f, 1.0f);

  // And the slide axis forgets where the last note started it, so the next one
  // takes its zero from wherever the controller puts it. See setSlide.
  slide = 0.0f;
  slideRest = 0.0f;
  slideRested = false;

  active = true;
  released = false;

  for (int i = 0; i < kNumHarmonics; ++i) {
    auto &pt = partials[(size_t)i];
    const auto &op = p.osc[(size_t)i];

    // A note starts at its own pitch. The engine sets it gliding afterwards
    // when one is due.
    pt.glideSemis = 0.0;
    pt.glideStep = 0.0;

    // Each strip's own velocity sensitivity, latched so a velocity change
    // cannot alter a note already sounding. See Velocity.h.
    pt.velGain = velocityGain(op.velAmount, vel);

    // How soon the partial starts and how quickly it gets there, from the same
    // velocity.
    pt.delayScale = strikeDelayScale(op.strikeAmount, vel);
    pt.attackScale = strikeAttackScale(op.strikeAmount, vel);
    pt.onsetStale = true;

    // Where the partial starts and how fast it rises, the knob and the
    // velocity together. See struckOnset.
    const auto onset = struckOnset(op.attack, pt.attackScale);

    pt.env.configure(op.delay * pt.delayScale, onset.seconds, op.decay,
                     op.sustain, op.swell, op.offLevel, op.release);

    // Starting from silence is only safe while the partial is silent. A note
    // struck again while still sounding, such as one held by the sustain
    // pedal, keeps its level and phase, since resetting them would put a step
    // in the output. So the choice is made per partial.
    const bool fresh = pt.env.getLevel() <= kSilent;

    pt.env.noteOn(fresh && p.global.phaseReset);

    // The lamp reads the pitch of the note this partial now plays, so a reused
    // voice does not open with the sag of a bend it never made. Only when the
    // partial had stopped, since one still ringing is a continuous tone.
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
      // independently rather than all 32 tracing one contour.
      pt.pitchLfo.restart(rng);
      pt.ampLfo.restart(rng);
    }

    // The gain ramp restarts from what this partial's strip asks for now, but
    // only for a partial starting clean. One carrying on keeps the ramp, so a
    // second strike at a different velocity does not step.
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
  // On the same channel the bend and pressure belong to the phrase and are
  // left alone. Under MPE every note has its own channel, so a phrase carried
  // onto a new key takes that finger's bend, pressure and slide.
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
    // is the same whatever the distance and only the arrival moves.
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

  // The same figure to all thirty-three, since it is one gesture. Each
  // applies it to its own key-off and sustain levels.
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
    // was, and the oscillator starts its cycle on a crossing so the level
    // cannot step.
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

  // Which thirty-two units this character's rack came out at. The spread is
  // fixed, so one lookup serves the whole call. See UnitSpread.
  const auto &unit = UnitSpread::instance().rack(p.global.character);

  const bool wheel = p.global.character == Character::Wheel;
  const bool vco = p.global.character == Character::Vco;

  // The VCO's wander, noise through one pole at kVcoWanderHz once a block.
  // A uniform draw has a variance of a third, and one pole leaves a / (2 - a)
  // of what goes in, so this scale gives kVcoWanderCents rms.
  const double blockRate = sampleRate / (double)kControlBlock;
  const double wanderPole = 1.0 - std::exp(-6.283185307179586 * kVcoWanderHz /
                                           std::max(1.0, blockRate));
  const double wanderScale =
      kVcoWanderCents * std::sqrt(3.0 * (2.0 - wanderPole) / wanderPole);

  // The characters whose noise the engine adds from every note's power at
  // once. See SynthEngine::renderHiss.
  const bool hiss = p.global.character == Character::Fm ||
                    p.global.character == Character::Pd ||
                    p.global.character == Character::Sid ||
                    p.global.character == Character::Vco ||
                    p.global.character == Character::Combo;
  const auto &sine = SineTable::instance();

  // One pole per control block rather than per sample, like every other
  // slow value here.
  const float bulbCoef =
      sampleRate > 0.0
          ? (float)(1.0 - std::exp(-(double)kControlBlock /
                                   (kBulbSettleSeconds * sampleRate)))
          : 1.0f;

  // Equal-power pan positions, one per partial.
  std::array<float, kNumHarmonics> panL{}, panR{};

  for (int i = 0; i < kNumHarmonics; ++i) {
    const float pan = std::clamp(p.osc[(size_t)i].pan, -1.0f, 1.0f);

    panL[(size_t)i] = std::sqrt(0.5f * (1.0f - pan));
    panR[(size_t)i] = std::sqrt(0.5f * (1.0f + pan));
  }

  // How much of each partial the keyboard tracking leaves at this pitch.
  // From the partial's nominal position, so vibrato and drift do not
  // modulate the brightness, and worked out once per call.
  std::array<float, kNumHarmonics> track{};

  // MPE slide, where it is aimed at brightness. Pushing forward takes
  // tracking away and pulling back adds more. Since tracking cannot go below
  // zero, slide can only darken a patch with tracking off.
  const auto tracking =
      p.global.slideDest == SlideDestination::Brightness
          ? std::max(0.0f, p.global.trackDbPerOctave - slide * kSlideTrackDb)
          : p.global.trackDbPerOctave;

  // Where each partial sits in the series before anything moves it, worked
  // out once per call since the TUNE blend, the slide and STRETCH are held
  // for it. With STRETCH on that saves an exp2 and a log2 per block.
  std::array<double, kNumHarmonics> nominal{};

  for (int i = 0; i < kNumHarmonics; ++i)
    nominal[(size_t)i] =
        semitoneOffset(i, (double)blendOf(p, i), (double)p.global.stretchCents);

  if (tracking > 0.0f) {
    for (int i = 0; i < kNumHarmonics; ++i)
      track[(size_t)i] =
          trackingGain(baseFreq * std::exp2(nominal[(size_t)i] / 12.0),
                       baseFreq, (double)tracking);
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

  // With the render rate turned down, folding is the sound being asked for,
  // so the Nyquist guard is turned off, as a low-rate converter wraps rather
  // than mutes.
  const bool foldAliases = p.lofi.rateHz > 0.0;

  // Which control block of this call we are on, for reading the modulators
  // every voice shares. The engine filled them for exactly these blocks.
  int block = 0;

  for (int start = 0; start < numSamples; start += kControlBlock, ++block) {
    const int len = std::min(kControlBlock, numSamples - start);

    pressureSmoothed += (pressureTarget - pressureSmoothed) * pressureCoef;
    const float pressure = pressureSmoothed;

    // How loud the note's partials are together this block, as the sum of
    // their squared amplitudes. The leaking wheels and the synthesizers' noise
    // are sized from it. The leaks are not in phase, so their powers add.
    double notePower = 0.0;

    for (int i = 0; i < kNumHarmonics; ++i) {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];

      // Scaled by what the note-on made of the velocity, so turning either knob
      // under a sounding note keeps that proportion. The delay is only read at
      // the next note-on, and is scaled here to keep the two calls agreeing.
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
      // the value used is the one the block starts on. Square shapes land their
      // edge on a block boundary, which is inaudible on a slow modulator.
      //
      // Off the channel's shared modulator when the keyboard is sharing one.
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

      // The unit's own tuning error goes in with the modulation, so everything
      // below sees the pitch the partial is actually at: the table, the slew
      // limit and the lamp.
      const double semis =
          nominal[(size_t)i] +
          (pmCents + driftCents + (double)unit.cents[(size_t)i]) * 0.01 +
          (double)(p.global.bendSemitones + noteBendSemitones) + pt.glideSemis +
          retuneSemis;

      // Where the key itself is, bent and gliding but without this partial's
      // place in the series or its own wander. The combo organ filters each
      // key once, whichever lever plays it, so its shape goes by this.
      const double keySemis =
          (double)(p.global.bendSemitones + noteBendSemitones) + pt.glideSemis +
          retuneSemis;

      // Read at the start of the block and stepped for the next, as the
      // modulators are. Zero is set exactly by the line below, so the compare
      // is exact too. The remaining glide for the needle is taken before this
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
      // this unit was drawn with, and the shared motor moves the pitch at its
      // own rate on top. The shaft rate comes from the pitch before any of
      // this, so the wobble cannot change its own rate. Worked out once a
      // block.
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

      if (vco) {
        pt.wanderCents += wanderPole * (wanderScale * (double)vcoRng.bipolar() -
                                        pt.wanderCents);
        wobbleCents = pt.wanderCents;
      }

      const double freq =
          baseFreq * std::exp2((semis + wobbleCents * 0.01) / 12.0);

      // Reduced into the first turn of the table. Above the rate this gives the
      // same samples as the frequency it folds to, which is the aliasing the
      // lo-fi rate asks for. The inner loop steps it in fixed point, where it
      // wraps by itself.
      double inc = freq / sampleRate;
      inc -= std::floor(inc);

      // ---- amplitude --------------------------------------------------------
      const double amPhaseInc = (double)op.amRateHz / sampleRate;

      // Both ends of the block, because the gain ramps between them, which also
      // keeps a square edge from clicking.
      //
      // Stepped whether or not it is the modulator being read, so a channel
      // that stops sharing picks up where this note would have been.
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
      // The table is chosen by how much room this partial has under Nyquist at
      // its current pitch, so a high partial loses harmonics from the top down
      // rather than aliasing. On the lo-fi rate the full table is used, since
      // folding is wanted.
      //
      // The frequency also goes in, since the rate-limited character bites
      // harder on faster waves. The combo organ's filter belongs to the key, so
      // every partial of a note reads the shape of the key played.
      const double shapeFreq = p.global.character == Character::Combo
                                   ? baseFreq * std::exp2(keySemis / 12.0)
                                   : freq;

      const auto &wave =
          characters.table(p.global.character, shapeFreq,
                           foldAliases ? kMostCharacterHarmonics
                                       : highestHarmonicUnder(freq, sampleRate),
                           unit.drive[(size_t)i]);

      // ---- what the lamp has not caught up with ----------------------------
      //
      // A Wien bridge holds its level with a lamp that heats and cools slowly,
      // so when the pitch moves the level sags until the lamp settles. One pole
      // chases the pitch with the filament's time constant, and what is left
      // over is the amplitude error.
      float bulb = 1.0f;

      if (p.global.character == Character::Bulb) {
        if (!pt.semisPrimed) {
          pt.bulbSettled = semis; // a note starts in balance, not sagging
          pt.semisPrimed = true;
        }

        pt.bulbSettled += (semis - pt.bulbSettled) * bulbCoef;

        // Clamped rather than curved, since a clamp is a compare where a soft
        // limit is a divide, and this runs for every partial.
        const float behind = std::clamp(
            (float)(semis - pt.bulbSettled) / kBulbFullScale, -1.0f, 1.0f);

        bulb = 1.0f - behind * kBulbDepth;
      }

      // Aftertouch adds to the fader instead of scaling it, so a strip at zero
      // can be brought in by pressure alone, and a negative amount is floored
      // at silence by the clamp. Velocity only scales the fader's own part.
      const float level =
          std::clamp(op.volume * pt.velGain +
                         std::clamp(op.atAmount, -1.0f, 1.0f) * pressure,
                     0.0f, 1.0f);
      // The lamp and the unit's level error join the fader, the tracking and
      // the Nyquist fade here, so they ride the same per-sample ramp.
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
      // the motion already under way. The amplitude was stepped above.
      pt.pitchLfo.advance(rng, pmPhaseInc * (double)len, op.pmShape);

      pt.lastGain = gEnd;

      if (wheel || hiss) {
        const double amplitude = (double)(pt.env.getLevel() * gEnd);
        notePower += amplitude * amplitude;
      }

      // Metering costs one multiply and one compare per partial per block.
      partialPeaks[(size_t)i] =
          std::max(partialPeaks[(size_t)i], pt.env.getLevel() * gEnd);

      // The lamp values, all worked out above for the oscillator's own use.
      // The envelope carries its stage in its sign: swell and release, which
      // run after the key is up, are negative.
      const auto stage = pt.env.getStage();
      const auto afterKeyOff =
          stage == Envelope::Stage::Swell || stage == Envelope::Stage::Release;

      partialEnvelopes[(size_t)i] =
          afterKeyOff ? -pt.env.getLevel() : pt.env.getLevel();

      // What the tremolo has taken off, so a partial with no tremolo reads
      // zero.
      partialTremolos[(size_t)i] = 1.0f - amEnd;
      // Where the partial is against its note: the modulator, the drift and
      // whatever of a glide is still to travel. Pitch bend is left out, since
      // it moves every partial alike.
      partialPitches[(size_t)i] = (float)(pmCents + driftCents + glideCents);

      // One lamp for each of the two rows under the heading. The velocity half
      // is the gain latched at note-on.
      partialVelocities[(size_t)i] = pt.velGain;
      partialPressures[(size_t)i] =
          std::abs(std::clamp(op.atAmount, -1.0f, 1.0f) * pressure);

      if ((g <= 1.0e-7f && gEnd <= 1.0e-7f) || pt.env.isSilentlyHolding()) {
        // Inaudible right now: muted, faded out above Nyquist, fader at zero,
        // or decayed to a sustain of nothing. Keep the envelope and phase
        // moving so it picks up in the right place, but skip the oscillator.
        for (int n = 0; n < len; ++n)
          pt.env.tick();

        // Stepped as the loop below steps it, so a partial that comes back
        // audible is where it would have been had it played throughout.
        pt.phase = fromFixedPhase(toFixedPhase(pt.phase) +
                                  toFixedStep(inc) * (std::uint64_t)len);

        // Whatever it would have been reading, so that coming back audible
        // does not cross-fade from a table it has not been heard on.
        pt.lastWave = &wave;
        continue;
      }

      float *l = left + start;
      float *r = right + start;

      // In fixed point inside the loop. See toFixedPhase.
      std::uint64_t ph = toFixedPhase(pt.phase);
      const std::uint64_t step = toFixedStep(inc);
      const float pl = panL[(size_t)i];
      const float pr = panR[(size_t)i];

      // Which table it was reading last block, and which it is reading now.
      // Swapping tables at the same phase would put a click in the wave, so a
      // block that changes tables cross-fades from one to the other. It costs a
      // second read only on the blocks that cross.
      const Wave *const previous = pt.lastWave;
      pt.lastWave = &wave;

      // The envelope is stepped through a local copy for the block. The output
      // pointers could alias it as far as the compiler knows, so stepping it in
      // place would store and reload its level every sample. Same samples.
      Envelope env = pt.env;

      if (previous == nullptr || previous == &wave) {
        for (int n = 0; n < len; ++n) {
          const float s = wave.at(ph) * env.tick() * g;

          l[n] += s * pl;
          r[n] += s * pr;

          ph += step;
          g += gInc;
        }
      } else {
        float mix = 0.0f;
        const float mixInc = 1.0f / (float)len;

        for (int n = 0; n < len; ++n) {
          const float was = previous->at(ph);
          const float is = wave.at(ph);
          const float s = (was + (is - was) * mix) * env.tick() * g;

          l[n] += s * pl;
          r[n] += s * pr;

          ph += step;
          g += gInc;
          mix += mixInc;
        }
      }

      pt.env = env;
      pt.phase = fromFixedPhase(ph);
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
                  notePower, foldAliases);

    if (hiss && block < kMaxPowerBlocks)
      blockPowers[(size_t)block] = (float)notePower;

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
    // inaudible, and the next note finds its wheels afresh.
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
      leak.phase = fromFixedPhase(toFixedPhase(leak.phase) +
                                  toFixedStep(inc) * (std::uint64_t)len);
      leak.lastGain = 0.0f;
      continue;
    }

    // A leaking wheel has the shape the wheels at its pitch have. A leak is
    // quiet enough that changing table without a cross-fade is inaudible.
    const auto &wave =
        tables.table(Character::Wheel, freq,
                     foldAliases ? kMostCharacterHarmonics
                                 : highestHarmonicUnder(freq, sampleRate));

    float g = leak.lastGain;
    const float gInc = (target - g) / (float)len;
    std::uint64_t ph = toFixedPhase(leak.phase);
    const std::uint64_t step = toFixedStep(inc);

    for (int n = 0; n < len; ++n) {
      const float s = wave.at(ph) * g * kCentre;

      left[n] += s;
      right[n] += s;

      ph += step;
      g += gInc;
    }

    leak.phase = fromFixedPhase(ph);
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

  // The same two reads the partials take, since the gain ramps between them.
  // Off the shared modulator when the keyboard is sharing one. The noise
  // channel's run sits after the 32 partials.
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
  // Both at unity give back the original white noise, so the centre of the
  // knob is flat.
  const float colour = std::clamp(np.colour, 0.0f, 1.0f);
  const float lowMix = colour < 0.5f ? 1.0f : 1.0f - (colour - 0.5f) * 2.0f;
  const float highMix = colour < 0.5f ? colour * 2.0f : 1.0f;

  // Panned the same way a partial is.
  const float pan = std::clamp(np.pan, -1.0f, 1.0f);
  const float panL = std::sqrt(0.5f * (1.0f - pan));
  const float panR = std::sqrt(0.5f * (1.0f + pan));

  // The string, struck by the noise after COLOUR and its envelope have shaped
  // it. Tuned to the pitch the fundamental is sounding at, with bends, glide
  // and Adaptive's slide, but without the fundamental's own vibrato and
  // drift.
  if (stringOn) {
    const double semis = (double)(p.global.bendSemitones + noteBendSemitones) +
                         partials[0].glideSemis + retuneSemis;

    // RELEASE is the damper. Once the key is up the string also dies away over
    // the channel's release time, so it has gone by the time the envelope has.
    // A steal or a fade brings a damper of its own.
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

  // Alive while it is being struck or is still above -120 dBFS.
  noise.stringAlive = stringOn && (noise.env.isActive() || rungPeak >= 1.0e-6f);
}

} // namespace ovt
