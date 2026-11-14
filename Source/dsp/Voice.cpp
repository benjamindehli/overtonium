#include "Voice.h"

#include <algorithm>
#include <cmath>

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

/// How high a partial whose phase another one moves may reach, as a share of
/// the sample rate, so its highest pair of tones stays under Nyquist with room
/// for the fade the tables already have.
constexpr double kPhaseModTop = 0.45;

constexpr double kRadiansToTurns = 0.15915494309189535; // 1 / 2 pi

/// A phase offset in 2^-32 of a turn, as the loops work it out, moved up into
/// the 64-bit fixed-point phase. Negative offsets wrap round, as the phase
/// does. Truncated, which at 2^-32 of a turn is far below hearing.
inline std::uint64_t phaseOffset(float turns32) noexcept {
  return (std::uint64_t)(std::int64_t)turns32 << 32;
}

/// A partial that moves its own phase, by the average of its last two
/// samples as the DX7 does, goes from a sine towards a sawtooth as the index
/// rises to about 1.5, and from about 2 turns to noise.
constexpr double kFeedbackNoise = 2.0;

/// The deepest index at which a partial moving its own phase, with `period`
/// samples to a cycle, folds nothing back over 55 dB under its harmonics.
/// Fitted just under that loop measured at twelve pitches from 4.5 to 873
/// samples a cycle: about 0.06 an extra sample on short cycles, and 0.18 an
/// octave of longer ones. 0.16 at a quarter of the sample rate, 1.1 at a
/// 27th, 2 at an 870th.
inline double feedbackIndexFor(double period) noexcept {
  if (period <= 1.8)
    return 0.0;

  return std::min(0.06 * (period - 1.8),
                  0.97 + 0.18 * std::log2(period / 18.0));
}

/// The other way round: how many samples a cycle needs for `index` to fold
/// nothing back, which is about how many harmonics fit under Nyquist at it.
inline double feedbackPeriodFor(double index) noexcept {
  return index <= 0.97 ? index / 0.06 + 1.8
                       : 18.0 * std::exp2((index - 0.97) / 0.18);
}

/// Everything a partial that modulates or is modulated steps through its
/// block, so the block can be played one sample at a time beside the other
/// partials in a loop. See Voice::render.
struct PartialRun {
  /// False for a partial nobody hears that still passes on its wave.
  bool heard = true;

  const Wave *wave = nullptr;
  const Wave *previous = nullptr;
  bool crossing = false;
  float mix = 0.0f, mixInc = 0.0f;

  bool feedback = false;
  bool centred = false;
  float centreCoef = 0.0f;

  std::uint64_t ph = 0, step = 0;
  Envelope env;

  float g = 0.0f, gInc = 0.0f;
  float pl = 0.0f, pr = 0.0f;
  float *l = nullptr;
  float *r = nullptr;

  float depth = 0.0f, depthInc = 0.0f;
  float passed = 0.0f, passedInc = 0.0f;
  float back = 0.0f, backInc = 0.0f;
  float moved = 0.0f, movedInc = 0.0f;

  /// The rings it reads and the one it writes. A ring is read a sample back
  /// when its partial plays after this one in a loop, so it holds this
  /// sample's value only once that partial has had its turn.
  const float *from = nullptr;
  const float *mover = nullptr;
  float *mine = nullptr;
  std::size_t fromLag = 0, moverLag = 0;

  std::array<float, 2> fed{};
  float fedMean = 0.0f;
};

/// One sample of a PartialRun, at place k in the rings and n in the block.
inline void tickPartial(PartialRun &run, std::size_t k, int n) noexcept {
  constexpr std::size_t wrap = Voice::kControlBlock - 1;

  // Where the table is read: the partial's own phase, moved by the other
  // partial's wave or by its own last two samples. The phase itself runs on
  // untouched, so the partial stays at its pitch.
  std::uint64_t read = run.ph;

  if (run.mover != nullptr) {
    read += phaseOffset(run.moved * run.mover[(k - run.moverLag) & wrap]);
    run.moved += run.movedInc;
  } else if (run.feedback) {
    read += phaseOffset(run.moved * 0.5f * (run.fed[0] + run.fed[1]));
    run.moved += run.movedInc;
  }

  float w = run.wave->at(read);

  if (run.crossing) {
    const float was = run.previous->at(read);
    w = was + (w - was) * run.mix;
    run.mix += run.mixInc;
  }

  if (run.centred) {
    run.fedMean += (w - run.fedMean) * run.centreCoef;
    w -= run.fedMean;
  }

  if (!run.heard) {
    // Unheard, so only what it passes on.
    float v = w * run.env.tick() * run.passed;

    if (run.from != nullptr) {
      const float kept = run.depth * (1.0f - run.back);
      v *= 1.0f - kept + kept * run.from[(k - run.fromLag) & wrap];
      run.depth += run.depthInc;
      run.back += run.backInc;
    }

    run.mine[k] = v;
    run.fed = {v, run.fed[0]};
    run.passed += run.passedInc;
    run.ph += run.step;
    return;
  }

  const float shaped = w * run.env.tick();

  // Depth one is the product alone, which is ring modulation. What is passed
  // on keeps less of it round a loop. See loopBack in Voice::render.
  float ring = 1.0f;
  float passedRing = 1.0f;

  if (run.from != nullptr) {
    const float by = run.from[(k - run.fromLag) & wrap];
    ring = 1.0f - run.depth + run.depth * by;

    const float kept = run.depth * (1.0f - run.back);
    passedRing = 1.0f - kept + kept * by;

    run.depth += run.depthInc;
    run.back += run.backInc;
  }

  const float s = shaped * run.g * ring;

  // What is passed on has this partial's own modulation in it, and its
  // expression, but not the fader or the mute, so a muted partial modulates
  // as strongly as a loud one.
  if (run.mine != nullptr || run.feedback) {
    const float out = shaped * passedRing * run.passed;
    run.passed += run.passedInc;

    if (run.mine != nullptr)
      run.mine[k] = out;

    run.fed = {out, run.fed[0]};
  }

  run.l[n] += s * run.pl;
  run.r[n] += s * run.pr;

  run.ph += run.step;
  run.g += run.gInc;
}
} // namespace

/// What the noise channel steps through its block, so the block can be
/// played one sample at a time beside the partials in a loop. See
/// Voice::render and PartialRun.
struct Voice::NoiseRun {
  float *mine = nullptr;
  const float *from = nullptr;
  std::size_t fromLag = 0;

  float depth = 0.0f, depthInc = 0.0f;
  float back = 0.0f, backInc = 0.0f;
  float passed = 0.0f, passedInc = 0.0f;
  float g = 0.0f, gInc = 0.0f;
  float panL = 0.0f, panR = 0.0f;
  float *left = nullptr;
  float *right = nullptr;

  float lowMix = 0.0f, highMix = 0.0f;
  float amount = 0.0f;
  bool stringOn = false;
  float rungPeak = 0.0f;
};

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

void Voice::prepare(double newSampleRate, uint32_t seed) {
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
  noise.lastFreq = 0.0;
  noise.lastRingDepth = 0.0f;
  noise.lastLoopBack = 0.0f;
  noise.lastSourceGain = -1.0f;
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
  noiseKeyboard = 0.0f;

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
    pt.velGain = velocityGain(op.velAmount, vel, p.global.velocityCurve);

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
    if (fresh) {
      pt.semisPrimed = false;

      // Nothing of the last note is left for a partial it modulates to read.
      ringSource[(size_t)i].fill(0.0f);
      pt.lastRingDepth = 0.0f;
      pt.lastLoopBack = 0.0f;
      pt.lastPhaseDepth = 0.0f;
      pt.fedBack = {};
      pt.fedMean = 0.0f;
      pt.lastSourceGain = -1.0f;
    }

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

    noise.velGain = velocityGain(np.velAmount, vel, p.global.velocityCurve);

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

    if (fresh) {
      noise.gainPrimed = false;

      // Nothing of the last note is left for a partial it modulates to read.
      ringSource[(size_t)kNoiseSource].fill(0.0f);
      noise.lastRingDepth = 0.0f;
      noise.lastLoopBack = 0.0f;
      noise.lastSourceGain = -1.0f;
    }
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

void Voice::setPedalFade(float rate) noexcept {
  for (auto &pt : partials)
    pt.env.setPedalFade(rate);

  noise.env.setPedalFade(rate);
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

    pt.lastFreq = src.lastFreq;
    pt.lastRingDepth = src.lastRingDepth;
    pt.lastLoopBack = src.lastLoopBack;
    pt.lastPhaseDepth = src.lastPhaseDepth;
    pt.fedBack = src.fedBack;
    pt.fedMean = src.fedMean;
    pt.lastSourceGain = src.lastSourceGain;
  }

  ringSource = from.ringSource;
  ringAt = from.ringAt;

  noise.env = from.noise.env;
  noise.ampLfo = from.noise.ampLfo;
  noise.velGain = from.noise.velGain;
  noise.delayScale = from.noise.delayScale;
  noise.attackScale = from.noise.attackScale;
  noise.lastGain = from.noise.lastGain;
  noise.gainPrimed = from.noise.gainPrimed;
  noise.lastFreq = from.noise.lastFreq;
  noise.lastRingDepth = from.noise.lastRingDepth;
  noise.lastLoopBack = from.noise.lastLoopBack;
  noise.lastSourceGain = from.noise.lastSourceGain;

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

  const bool hiss = hasSynthHiss(p.global.character);
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
  partialKeyboards.fill(0.0f);
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;
  noiseVelocity = 0.0f;
  noisePressure = 0.0f;
  noiseKeyboard = 0.0f;

  const float pressureTarget =
      std::max(std::clamp(p.global.aftertouch, 0.0f, 1.0f), polyPressure);

  // With the render rate turned down, folding is the sound being asked for,
  // so the Nyquist guard is turned off, as a low-rate converter wraps rather
  // than mutes.
  const bool foldAliases = p.lofi.rateHz > 0.0;

  // Which control block of this call we are on, for reading the modulators
  // every voice shares. The engine filled them for exactly these blocks.
  int block = 0;

  // The note's own pitch as octaves above 1 Hz, which each partial's place is
  // added to for the resonator. See getPartialOctaves.
  const double baseOctave = std::log2(std::max(1.0e-3, baseFreq));

  // Which partials, or the noise channel, another one takes its amplitude or
  // its phase from, and the order they play in: each after the one it
  // follows, so it reads this block's samples, and otherwise in their own
  // order. With none, the order is 1 to 32 and the noise channel plays after
  // them, as it always has.
  //
  // A source is named by its place, the partials 0 to 31 and the noise
  // channel at kNoiseSource.
  constexpr int kPlaces = kNumHarmonics + 1;

  //
  // A link at no depth that was at none at the end of the last block does
  // nothing, so it counts as no link at all. Otherwise it would still decide
  // the order and close a loop, and a knob at nothing would change the sound.
  const auto amFrom = [this, &p](int at) {
    const bool isNoise = at == kNoiseSource;
    const int from = isNoise ? p.noise.amSource : p.osc[(size_t)at].amSource;
    const bool live = isNoise
                          ? p.noise.amDepth > 0.0f || noise.lastRingDepth > 0.0f
                          : p.osc[(size_t)at].amDepth > 0.0f ||
                                partials[(size_t)at].lastRingDepth > 0.0f;

    return live && from >= 0 && from <= kNoiseSource && from != at ? from : -1;
  };

  const auto pmFrom = [this, &p](int at) {
    if (at == kNoiseSource)
      return -1;

    const auto &op = p.osc[(size_t)at];
    const bool live =
        op.pmDepthCents > 0.0f || partials[(size_t)at].lastPhaseDepth > 0.0f;
    const int from = op.pmSource;

    return live && from >= 0 && from <= kNoiseSource && from != at ? from : -1;
  };

  std::array<int, kPlaces> amLink{};
  std::array<int, kPlaces> pmLink{};

  for (int i = 0; i < kPlaces; ++i) {
    amLink[(size_t)i] = amFrom(i);
    pmLink[(size_t)i] = pmFrom(i);
  }

  std::array<bool, kPlaces> isSource{};
  bool anySource = false;

  for (int i = 0; i < kPlaces; ++i)
    for (const int from : {amLink[(size_t)i], pmLink[(size_t)i]})
      if (from >= 0) {
        isSource[(size_t)from] = true;
        anySource = true;
      }

  // A place nothing reads this block holds nothing, so one that becomes a
  // source while a note sounds is not read from what it held long ago.
  for (int i = 0; i < kPlaces; ++i)
    if (!isSource[(size_t)i])
      ringSource[(size_t)i].fill(0.0f);

  // The noise channel joins the order only where it is part of it, and
  // otherwise plays after everything else, as before there was an order.
  const bool noiseInOrder =
      isSource[(size_t)kNoiseSource] || amLink[(size_t)kNoiseSource] >= 0;
  const int places = noiseInOrder ? kPlaces : kNumHarmonics;

  // A partial passes on what another partial does to it, so a chain
  // multiplies all the way along. Round a loop at full depth it would
  // multiply itself by itself each time round and die away, so what it passes
  // on loses the ring by as much as the loop comes back round to it: the
  // depths of the other links in the loop, multiplied. With any of them at
  // nothing it passes on its ring as in a chain, and with all of them at full
  // its own wave alone, with no step anywhere between.
  std::array<float, kPlaces> loopBack{};

  const auto ringDepthOf = [this](int at) {
    return at == kNoiseSource ? noise.lastRingDepth
                              : partials[(size_t)at].lastRingDepth;
  };

  if (anySource)
    for (int i = 0; i < places; ++i) {
      int at = amLink[(size_t)i];
      float back = 1.0f;

      for (int steps = 0; steps < kNumHarmonics && at >= 0 && at != i;
           ++steps) {
        back *= ringDepthOf(at);
        at = amLink[(size_t)at];
      }

      loopBack[(size_t)i] = at == i ? std::clamp(back, 0.0f, 1.0f) : 0.0f;
    }

  // The loops: places that follow each other round, however far, play
  // together a sample at a time, so each hears the others one sample late at
  // most, as a DX7's feedback does, rather than a whole block. loopOf names a
  // place's loop by its lowest place, and a place in no loop by itself.
  // follows holds, for each place, every place it follows along the links.
  std::array<int, kPlaces> loopOf{};
  std::array<std::uint64_t, kPlaces> follows{};

  for (int i = 0; i < kPlaces; ++i)
    loopOf[(size_t)i] = i;

  if (anySource) {
    for (int i = 0; i < places; ++i)
      for (const int from : {amLink[(size_t)i], pmLink[(size_t)i]})
        if (from >= 0)
          follows[(size_t)i] |= std::uint64_t{1} << from;

    for (int via = 0; via < places; ++via)
      for (int i = 0; i < places; ++i)
        if ((follows[(size_t)i] >> via) & 1u)
          follows[(size_t)i] |= follows[(size_t)via];

    for (int i = 0; i < places; ++i)
      for (int j = 0; j < i; ++j)
        if (((follows[(size_t)i] >> j) & 1u) &&
            ((follows[(size_t)j] >> i) & 1u)) {
          loopOf[(size_t)i] = j;
          break;
        }
  }

  // Each in turn as soon as what it follows has played, the lowest first. A
  // loop goes in as one, once everything its places follow outside it has
  // played, and inside it the same rule sets who has the first turn each
  // sample. Round the loop nothing is ready, so the lowest left goes first
  // and hears the others a sample late. loopSize is how many places from
  // there on play together, at a loop's first place, and 1 everywhere else.
  std::array<int, kPlaces> order{};
  std::array<int, kPlaces> loopSize{};
  std::array<int, kPlaces> placeOf{};

  for (int i = 0; i < kPlaces; ++i) {
    order[(size_t)i] = i;
    loopSize[(size_t)i] = 1;
    placeOf[(size_t)i] = i;
  }

  if (anySource) {
    std::array<bool, kPlaces> placed{};

    const auto readyIn = [&](int i, int loop) {
      for (const int from : {amLink[(size_t)i], pmLink[(size_t)i]})
        if (from >= 0 && !placed[(size_t)from] &&
            (loop < 0 || loopOf[(size_t)from] != loop))
          return false;

      return true;
    };

    for (int next = 0; next < places;) {
      int pick = -1;

      for (int i = 0; i < places && pick < 0; ++i) {
        if (placed[(size_t)i] || loopOf[(size_t)i] != i)
          continue;

        bool ready = true;

        for (int m = i; m < places && ready; ++m)
          if (loopOf[(size_t)m] == i)
            ready = readyIn(m, i);

        if (ready)
          pick = i;
      }

      for (int i = 0; i < places && pick < 0; ++i)
        if (!placed[(size_t)i] && loopOf[(size_t)i] == i)
          pick = i;

      const int first = next;

      for (bool more = true; more;) {
        more = false;
        int member = -1;

        for (int m = pick; m < places && member < 0; ++m)
          if (loopOf[(size_t)m] == pick && !placed[(size_t)m] && readyIn(m, -1))
            member = m;

        for (int m = pick; m < places && member < 0; ++m)
          if (loopOf[(size_t)m] == pick && !placed[(size_t)m])
            member = m;

        if (member >= 0) {
          placed[(size_t)member] = true;
          placeOf[(size_t)member] = next;
          order[(size_t)next++] = member;
          more = true;
        }
      }

      loopSize[(size_t)first] = next - first;
    }
  }

  // The frequency a source sounds at, for the Nyquist guards.
  const auto sourceFreq = [this](int from) {
    return from == kNoiseSource ? noise.lastFreq
                                : partials[(size_t)from].lastFreq;
  };

  // Where a loop's places keep their blocks while they play them together.
  std::array<PartialRun, kNumHarmonics> runs{};
  NoiseRun noiseRun;

  for (int start = 0; start < numSamples; start += kControlBlock, ++block) {
    const int len = std::min(kControlBlock, numSamples - start);

    pressureSmoothed += (pressureTarget - pressureSmoothed) * pressureCoef;
    const float pressure = pressureSmoothed;

    // How loud the note's partials are together this block, as the sum of
    // their squared amplitudes. The leaking wheels and the synthesizers' noise
    // are sized from it. The leaks are not in phase, so their powers add.
    double notePower = 0.0;

    const auto finishPartial = [](Partial &pt, const PartialRun &run) {
      pt.phase = fromFixedPhase(run.ph);
      pt.env = run.env;
      pt.fedBack = run.fed;
      pt.fedMean = run.fedMean;
    };

    // Plays partial i's block. Given somewhere to put it, a partial that
    // modulates or is modulated sets its block up there and leaves the
    // samples to the caller, which is how a loop plays its partials side by
    // side. Says whether it did.
    const auto playPartial = [&](int i, PartialRun *defer) -> bool {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];
      const bool source = isSource[(size_t)i];

      // The partial or the noise channel this one takes its amplitude from,
      // if any. See OscParams::amSource.
      const int ringFrom = amLink[(size_t)i];

      // And the one that moves its phase. See OscParams::pmSource.
      const int phaseFrom = pmLink[(size_t)i];

      // Or whether it moves its own, from its own last samples.
      const bool feedback = op.pmSource == i;

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

      // The half-pedal's fade, moved once for the block and ridden on the gain
      // ramp below. It can end the note.
      const float pedal = pt.env.advancePedal(len);

      if (!pt.env.isActive()) {
        // Silent, so a partial it modulates is multiplied by nothing.
        if (source)
          for (int n = 0; n < len; ++n)
            ringSource[(size_t)i]
                      [(size_t)((ringAt + n) & (kControlBlock - 1))] = 0.0f;

        pt.lastRingDepth = 0.0f;

        pt.lastLoopBack = 0.0f;
        return false;
      }

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

      // A partial whose phase another one moves has no vibrato of its own,
      // and neither does one that moves its own.
      const double pmCents = op.pmDepthCents > 0.0f && op.pmSource < 0
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
      partialOctaves[(size_t)i] = baseOctave + semis / 12.0;

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

      // The key the KEYBOARD row reads, which a glide carries smoothly from
      // the last note's key to this one. The key number rather than the
      // pitch, so bend, TUNE, the temperament and drift leave the level alone.
      // Read before this block's step, with the pitch.
      const float keyGain =
          pt.keyboard.at(op.keyAmount, (double)midiNote + pt.glideSemis);

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

      // A partial modulated by another has no tremolo of its own. The other
      // partial's wave does that, sample by sample, below. One that names
      // itself has no modulation at all.
      const bool noTremolo = op.amSource >= 0;

      const float amStart =
          noTremolo
              ? 1.0f
              : tremoloGain(sharedAmp ? shared.amp[at] : ownStart, op.amDepth);

      const float amEnd =
          noTremolo ? 1.0f
                    : tremoloGain(sharedAmp ? shared.amp[at + 1] : ownEnd,
                                  op.amDepth);

      const float nyq = foldAliases ? 1.0f : nyquistGain(freq, sampleRate);

      // How deep the other partial's wave multiplies this one by the end of
      // the block. Multiplying two partials makes their sum and their
      // difference, so the depth fades out as the sum nears Nyquist, as a
      // partial does, unless folding is wanted.
      pt.lastFreq = freq;

      const float ringEnd =
          ringFrom < 0
              ? 0.0f
              : op.amDepth * (foldAliases
                                  ? 1.0f
                                  : nyquistGain(freq + sourceFreq(ringFrom),
                                                sampleRate));
      const float ringStart = pt.lastRingDepth;
      pt.lastRingDepth = ringEnd;

      // How much of that ring is kept out of what it passes on, for a loop.
      // See loopBack.
      const float backEnd = loopBack[(size_t)i];
      const float backStart = pt.lastLoopBack;
      pt.lastLoopBack = backEnd;

      // How far the other partial's wave moves this one's phase, as the
      // modulation index FM synthesizers use, in radians at the wave's peak.
      // Phase modulation spreads a partial into pairs of tones either side of
      // it, one source frequency apart, and nearly all of it lies within the
      // index plus one pairs. So the index is held to what keeps the highest
      // pair under the top of the band, unless folding is wanted.
      //
      // A partial that moves its own phase makes its own harmonics instead,
      // more of them the deeper it goes and the further they fold, so the
      // index is held to what folds nothing back that could be heard. Past
      // kFeedbackNoise it is noise, which folding does not put out of tune,
      // so it goes as deep as the knob asks.
      double phaseIndex = 0.0;

      if (phaseFrom >= 0) {
        phaseIndex = (double)(op.pmDepthCents / kCentsPerIndex);

        const double by = sourceFreq(phaseFrom);

        if (!foldAliases && by > 0.0)
          phaseIndex = std::clamp((kPhaseModTop * sampleRate - freq) / by - 1.0,
                                  0.0, phaseIndex);
      } else if (feedback) {
        phaseIndex = (double)(op.pmDepthCents / kCentsPerIndex);

        if (!foldAliases && phaseIndex < kFeedbackNoise)
          phaseIndex =
              std::min(phaseIndex, feedbackIndexFor(sampleRate / freq));
      }

      // In 2^-32 of a turn per unit of the wave, which the loop below shifts
      // up into the fixed-point phase.
      const float phaseEnd =
          (float)(phaseIndex * kRadiansToTurns * 4294967296.0);
      const float phaseStart = pt.lastPhaseDepth;
      pt.lastPhaseDepth = phaseEnd;

      // The highest the moved partial reaches, which the table is chosen for.
      // Moving itself, its harmonics reach as far as the index needs room for.
      const double reach = phaseFrom >= 0
                               ? freq + phaseIndex * sourceFreq(phaseFrom)
                           : feedback && phaseIndex > 0.0
                               ? freq * 0.5 * feedbackPeriodFor(phaseIndex)
                               : freq;

      // Moving its own phase a sample and a half late makes the wave lean to
      // one side, which puts in an offset that grows with the index and the
      // pitch, as on a DX7, whose output is AC-coupled. A one-pole follows it,
      // an eighth of the partial's frequency up so the fundamental keeps all
      // but 0.07 dB, and the wave loses it before anything hears it or it is
      // fed back. Not at index nothing, which stays a plain sine.
      const bool centred = feedback && (phaseStart > 0.0f || phaseEnd > 0.0f);
      const float centreCoef =
          centred ? (float)(1.0 - std::exp(-6.283185307179586 * freq / 8.0 /
                                           sampleRate))
                  : 0.0f;

      if (!centred)
        pt.fedMean = 0.0f;

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

      const auto &wave = characters.table(
          p.global.character, shapeFreq,
          foldAliases ? kMostCharacterHarmonics
                      : highestHarmonicUnder(reach, sampleRate),
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
      // The keyboard scales the lot, so pressure cannot bring a partial back
      // on keys the row has faded it out of.
      const float level =
          std::clamp(op.volume * pt.velGain +
                         std::clamp(op.atAmount, -1.0f, 1.0f) * pressure,
                     0.0f, 1.0f) *
          keyGain;
      // The lamp and the unit's level error join the fader, the tracking and
      // the Nyquist fade here, so they ride the same per-sample ramp.
      const float base = op.audible ? level * nyq * track[(size_t)i] * bulb *
                                          unit.gain[(size_t)i] * wobbleGain
                                    : 0.0f;
      const float gEnd = base * amEnd;

      // What is heard, with the half-pedal's fade, which the meters below
      // already have in the envelope's level.
      const float gHeard = gEnd * pedal;

      // What this partial passes on as a modulator is scaled by: the level
      // above as if the fader were up, so velocity, aftertouch and the
      // KEYBOARD row count and the fader and mute do not, then its own
      // tremolo and the half-pedal. Its own envelope goes in per sample.
      float sourceStart = 0.0f, sourceEnd = 0.0f;

      if (source || feedback) {
        const float expression =
            std::clamp(pt.velGain +
                           std::clamp(op.atAmount, -1.0f, 1.0f) * pressure,
                       0.0f, 1.0f) *
            keyGain * pedal;

        sourceEnd = expression * amEnd;

        if (pt.lastSourceGain < 0.0f)
          pt.lastSourceGain = expression * amStart;

        sourceStart = pt.lastSourceGain;
        pt.lastSourceGain = sourceEnd;
      }

      if (!pt.gainPrimed) {
        pt.lastGain = base * amStart * pedal;
        pt.gainPrimed = true;
      }

      // Ramping from the previous block's gain to this one covers tremolo,
      // volume moves, mute/solo, the Nyquist fade and the half-pedal with a
      // single interpolation.
      float g = pt.lastGain;
      const float gInc = (gHeard - g) / (float)len;

      // Stepped whatever the depth says, so turning the knob up mid-note joins
      // the motion already under way. The amplitude was stepped above.
      pt.pitchLfo.advance(rng, pmPhaseInc * (double)len, op.pmShape);

      pt.lastGain = gHeard;

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
      // zero. Modulated by another partial, how deep that goes.
      partialTremolos[(size_t)i] = ringFrom >= 0 ? ringEnd : 1.0f - amEnd;
      // Where the partial is against its note: the modulator, the drift and
      // whatever of a glide is still to travel. Pitch bend is left out, since
      // it moves every partial alike.
      partialPitches[(size_t)i] = (float)(pmCents + driftCents + glideCents);

      // One lamp for each of the two rows under the heading. The velocity half
      // is the gain latched at note-on.
      partialVelocities[(size_t)i] = pt.velGain;
      partialPressures[(size_t)i] =
          std::abs(std::clamp(op.atAmount, -1.0f, 1.0f) * pressure);
      partialKeyboards[(size_t)i] = keyGain;

      if ((g <= 1.0e-7f && gHeard <= 1.0e-7f) || pt.env.isSilentlyHolding()) {
        // Inaudible right now: muted, faded out above Nyquist, fader at zero,
        // or decayed to a sustain of nothing. Keep the envelope and phase
        // moving so it picks up in the right place, but skip the oscillator,
        // unless another partial takes its amplitude from this one, which
        // still needs its wave.
        if (source) {
          PartialRun run;
          run.heard = false;
          run.wave = &wave;
          run.feedback = feedback;
          run.centred = centred;
          run.centreCoef = centreCoef;
          run.ph = toFixedPhase(pt.phase);
          run.step = toFixedStep(inc);
          run.env = pt.env;

          run.passed = sourceStart;
          run.passedInc = (sourceEnd - sourceStart) / (float)len;

          // Modulated itself, so what it passes on is too, less as much as
          // a loop comes back round to it.
          run.from =
              ringFrom >= 0 ? ringSource[(size_t)ringFrom].data() : nullptr;
          run.depth = ringStart;
          run.depthInc = (ringEnd - ringStart) / (float)len;
          run.back = backStart;
          run.backInc = (backEnd - backStart) / (float)len;

          // Its phase moved by another partial, so what it passes on is too.
          run.mover =
              phaseFrom >= 0 ? ringSource[(size_t)phaseFrom].data() : nullptr;
          run.moved = phaseStart;
          run.movedInc = (phaseEnd - phaseStart) / (float)len;

          run.mine = ringSource[(size_t)i].data();
          run.fed = pt.fedBack;
          run.fedMean = pt.fedMean;

          // Whatever it would have been reading, so that coming back audible
          // does not cross-fade from a table it has not been heard on.
          pt.lastWave = &wave;

          if (defer != nullptr) {
            *defer = run;
            return true;
          }

          for (int n = 0; n < len; ++n)
            tickPartial(run, (size_t)((ringAt + n) & (kControlBlock - 1)), n);

          finishPartial(pt, run);
        } else {
          for (int n = 0; n < len; ++n)
            pt.env.tick();

          // Unheard and moving nothing else, so its own phase starts again
          // from still when it comes back.
          pt.fedBack = {};
          pt.fedMean = 0.0f;

          // Stepped as the loop below steps it, so a partial that comes back
          // audible is where it would have been had it played throughout.
          pt.phase = fromFixedPhase(toFixedPhase(pt.phase) +
                                    toFixedStep(inc) * (std::uint64_t)len);
        }

        // Whatever it would have been reading, so that coming back audible
        // does not cross-fade from a table it has not been heard on.
        pt.lastWave = &wave;
        return false;
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

      if (source || ringFrom >= 0 || phaseFrom >= 0 || feedback) {
        // Modulating or modulated, which the plain loops below leave out to
        // stay as quick as they are. One loop for all of it, with the
        // cross-fade when the table changes.
        PartialRun run;
        run.wave = &wave;
        run.previous = previous;
        run.crossing = previous != nullptr && previous != &wave;
        run.mixInc = run.crossing ? 1.0f / (float)len : 0.0f;
        run.feedback = feedback;
        run.centred = centred;
        run.centreCoef = centreCoef;
        run.ph = ph;
        run.step = step;
        run.env = env;
        run.g = g;
        run.gInc = gInc;
        run.pl = pl;
        run.pr = pr;
        run.l = l;
        run.r = r;

        run.depth = ringStart;
        run.depthInc = (ringEnd - ringStart) / (float)len;
        run.passed = sourceStart;
        run.passedInc = (sourceEnd - sourceStart) / (float)len;
        run.back = backStart;
        run.backInc = (backEnd - backStart) / (float)len;

        run.from =
            ringFrom >= 0 ? ringSource[(size_t)ringFrom].data() : nullptr;
        run.mine = source ? ringSource[(size_t)i].data() : nullptr;
        run.mover =
            phaseFrom >= 0 ? ringSource[(size_t)phaseFrom].data() : nullptr;
        run.moved = phaseStart;
        run.movedInc = (phaseEnd - phaseStart) / (float)len;
        run.fed = pt.fedBack;
        run.fedMean = pt.fedMean;

        if (defer != nullptr) {
          *defer = run;
          return true;
        }

        for (int n = 0; n < len; ++n)
          tickPartial(run, (size_t)((ringAt + n) & (kControlBlock - 1)), n);

        finishPartial(pt, run);
        return false;
      } else if (previous == nullptr || previous == &wave) {
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
      return false;
    };

    for (int place = 0; place < places;) {
      const int size = loopSize[(size_t)place];

      if (size == 1) {
        const int i = order[(size_t)place++];

        if (i == kNoiseSource)
          renderNoise(left + start, right + start, len, p, pressure, shared,
                      block, isSource[(size_t)kNoiseSource],
                      amLink[(size_t)kNoiseSource],
                      loopBack[(size_t)kNoiseSource], foldAliases, nullptr);
        else
          playPartial(i, nullptr);

        continue;
      }

      // A loop. Each place sets its block up in turn, then they play it a
      // sample at a time in their order, so a place reading one that plays
      // after it reads that one's last sample.
      std::array<bool, kPlaces> deferred{};

      for (int m = place; m < place + size; ++m) {
        const int at = order[(size_t)m];

        deferred[(size_t)at] =
            at == kNoiseSource
                ? renderNoise(left + start, right + start, len, p, pressure,
                              shared, block, isSource[(size_t)kNoiseSource],
                              amLink[(size_t)kNoiseSource],
                              loopBack[(size_t)kNoiseSource], foldAliases,
                              &noiseRun)
                : playPartial(at, &runs[(size_t)at]);
      }

      const auto lag = [&placeOf](int at, int from) -> std::size_t {
        return from >= 0 && placeOf[(size_t)from] > placeOf[(size_t)at] ? 1 : 0;
      };

      for (int m = place; m < place + size; ++m) {
        const int at = order[(size_t)m];

        if (at == kNoiseSource)
          noiseRun.fromLag = lag(at, amLink[(size_t)at]);
        else {
          runs[(size_t)at].fromLag = lag(at, amLink[(size_t)at]);
          runs[(size_t)at].moverLag = lag(at, pmLink[(size_t)at]);
        }
      }

      for (int n = 0; n < len; ++n) {
        const auto k = (size_t)((ringAt + n) & (kControlBlock - 1));

        for (int m = place; m < place + size; ++m) {
          const int at = order[(size_t)m];

          if (!deferred[(size_t)at])
            continue;

          if (at == kNoiseSource)
            tickNoise(noiseRun, n);
          else
            tickPartial(runs[(size_t)at], k, n);
        }
      }

      for (int m = place; m < place + size; ++m) {
        const int at = order[(size_t)m];

        if (!deferred[(size_t)at])
          continue;

        if (at == kNoiseSource)
          finishNoise(noiseRun);
        else
          finishPartial(partials[(size_t)at], runs[(size_t)at]);
      }

      place += size;
    }

    // Every partial has written its block, so the rings move on by it.
    if (anySource)
      ringAt = (ringAt + len) & (kControlBlock - 1);

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

    if (!noiseInOrder)
      renderNoise(left + start, right + start, len, p, pressure, shared, block,
                  false, -1, 0.0f, foldAliases, nullptr);

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

void Voice::tickNoise(NoiseRun &run, int n) noexcept {
  const auto k = (size_t)((ringAt + n) & (kControlBlock - 1));
  const float white = noise.rng.bipolar();

  noise.lowpassState += lowpassCoef * (white - noise.lowpassState);
  const float high = white - noise.lowpassState;

  float out =
      (run.lowMix * noise.lowpassState + run.highMix * high) * noise.env.tick();

  // Crossfaded by STRING, and by how much of the string can be heard at this
  // pitch: where the loop is too short to tune, the noise is left as it was.
  if (run.stringOn) {
    const float rung = noise.string.tick(out) * kStringGain;
    run.rungPeak = std::max(run.rungPeak, std::abs(rung));
    out += run.amount * noise.string.audibility() * (rung - out);
  }

  // Depth one is the product alone, which is ring modulation. What is passed
  // on keeps less of it round a loop. See Voice::render.
  float ring = 1.0f;
  float passedRing = 1.0f;

  if (run.from != nullptr) {
    const float by = run.from[(k - run.fromLag) & (kControlBlock - 1)];
    ring = 1.0f - run.depth + run.depth * by;

    const float kept = run.depth * (1.0f - run.back);
    passedRing = 1.0f - kept + kept * by;

    run.depth += run.depthInc;
    run.back += run.backInc;
  }

  // What is passed on has its expression and what the partial does to it,
  // but not the fader or the mute.
  if (run.mine != nullptr) {
    run.mine[k] = out * passedRing * run.passed;
    run.passed += run.passedInc;
  }

  const float s = out * run.g * ring;

  run.left[n] += s * run.panL;
  run.right[n] += s * run.panR;

  run.g += run.gInc;
}

void Voice::finishNoise(const NoiseRun &run) noexcept {
  // Alive while it is being struck or is still above -120 dBFS.
  noise.stringAlive =
      run.stringOn && (noise.env.isActive() || run.rungPeak >= 1.0e-6f);
}

bool Voice::renderNoise(float *left, float *right, int len,
                        const SynthParams &p, float pressure,
                        const SharedModulation &shared, int block, bool source,
                        int ringFrom, float backEnd, bool foldAliases,
                        NoiseRun *defer) noexcept {
  const auto &np = p.noise;

  // What it passes on as a source, written for every sample of the block.
  float *const mine =
      source ? ringSource[(size_t)kNoiseSource].data() : nullptr;

  // ringFrom is the partial whose wave multiplies it, or -1 for none or for
  // a link at no depth. See NoiseParams::amSource and Voice::render.

  noise.env.configure(np.delay * noise.delayScale,
                      struckAttack(np.attack, noise.attackScale), np.decay,
                      np.sustain, np.swell, np.offLevel, np.release);

  const float amount = std::clamp(np.stringAmount, 0.0f, 1.0f);
  const bool stringOn = amount > 0.0f;

  if (!stringOn)
    noise.stringAlive = false;

  // The string can still be ringing after the noise has stopped.
  if (!noise.env.isActive() && !noise.stringAlive) {
    // Silent, so a partial it modulates is multiplied by nothing.
    if (mine != nullptr)
      for (int n = 0; n < len; ++n)
        mine[(size_t)((ringAt + n) & (kControlBlock - 1))] = 0.0f;

    noise.lastRingDepth = 0.0f;

    noise.lastLoopBack = 0.0f;
    return false;
  }

  const double amPhaseInc = (double)np.amRateHz / sampleRate;

  // The same two reads the partials take, since the gain ramps between them.
  // Off the shared modulator when the keyboard is sharing one. The noise
  // channel's run sits after the 32 partials.
  const auto ownStart = noise.ampLfo.value(np.amShape, kAmpShapeOffset);
  const auto ownEnd = noise.ampLfo.advance(noise.rng, amPhaseInc * (double)len,
                                           np.amShape, kAmpShapeOffset);

  const auto at = (size_t)(kNumHarmonics * shared.stride + block);

  // Following a partial, which takes the tremolo's place.
  const float amStart =
      ringFrom >= 0
          ? 1.0f
          : tremoloGain(shared.amp != nullptr ? shared.amp[at] : ownStart,
                        np.amDepth);

  const float amEnd =
      ringFrom >= 0
          ? 1.0f
          : tremoloGain(shared.amp != nullptr ? shared.amp[at + 1] : ownEnd,
                        np.amDepth);

  // The noise has no pitch of its own, so it takes its key from the first
  // partial's, glide and all, as the resonator does.
  const float keyGain = noise.keyboard.at(
      np.keyAmount, (double)midiNote + partials[0].glideSemis);

  const float level =
      std::clamp(np.volume * noise.velGain +
                     std::clamp(np.atAmount, -1.0f, 1.0f) * pressure,
                 0.0f, 1.0f) *
      keyGain;
  const float gEnd = (np.audible ? level : 0.0f) * amEnd;

  // The half-pedal's fade, as for the partials.
  const float pedal = noise.env.advancePedal(len);
  const float gHeard = gEnd * pedal;

  // What it passes on as a source is scaled as a partial's is: everything
  // its level has but the fader and the mute. See Voice::render.
  float sourceStart = 0.0f, sourceEnd = 0.0f;

  if (mine != nullptr) {
    const float expression =
        std::clamp(noise.velGain +
                       std::clamp(np.atAmount, -1.0f, 1.0f) * pressure,
                   0.0f, 1.0f) *
        keyGain * pedal;

    sourceEnd = expression * amEnd;

    if (noise.lastSourceGain < 0.0f)
      noise.lastSourceGain = expression * amStart;

    sourceStart = noise.lastSourceGain;
    noise.lastSourceGain = sourceEnd;
  }

  // The string's pitch, which is what a partial it modulates could fold out
  // of tune. Noise alone has none. See Noise::lastFreq.
  const double stringPitch =
      baseFreq *
      std::exp2(((double)(p.global.bendSemitones + noteBendSemitones) +
                 partials[0].glideSemis + retuneSemis) /
                12.0);
  noise.lastFreq = stringOn ? stringPitch : 0.0;

  // How deep the partial's wave multiplies it by the end of the block. Noise
  // folded back is still noise, so only the string fades the depth near
  // Nyquist, as a partial does.
  const float ringEnd =
      ringFrom < 0
          ? 0.0f
          : np.amDepth *
                (foldAliases || !stringOn
                     ? 1.0f
                     : nyquistGain(noise.lastFreq +
                                       partials[(size_t)ringFrom].lastFreq,
                                   sampleRate));
  const float ringStart = noise.lastRingDepth;
  noise.lastRingDepth = ringEnd;

  const float backStart = noise.lastLoopBack;
  noise.lastLoopBack = backEnd;

  if (!noise.gainPrimed) {
    noise.lastGain = (np.audible ? level : 0.0f) * amStart * pedal;
    noise.gainPrimed = true;
  }

  float g = noise.lastGain;
  const float gInc = (gHeard - g) / (float)len;

  noise.lastGain = gHeard;

  noisePeak = std::max(noisePeak, noise.env.getLevel() * gEnd);

  {
    const auto stage = noise.env.getStage();
    const auto afterKeyOff =
        stage == Envelope::Stage::Swell || stage == Envelope::Stage::Release;

    noiseEnvelope = afterKeyOff ? -noise.env.getLevel() : noise.env.getLevel();
    noiseTremolo = ringFrom >= 0 ? ringEnd : 1.0f - amEnd;
    noiseVelocity = noise.velGain;
    noisePressure = std::abs(std::clamp(np.atAmount, -1.0f, 1.0f) * pressure);
    noiseKeyboard = keyGain;
  }

  // Unheard, unless a partial follows it and still needs its wave.
  if (g <= 1.0e-7f && gHeard <= 1.0e-7f && mine == nullptr) {
    for (int n = 0; n < len; ++n)
      noise.env.tick();

    // Nothing of the channel is heard, so neither is its string.
    noise.stringAlive = false;
    return false;
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
    // RELEASE is the damper. Once the key is up the string also dies away over
    // the channel's release time, so it has gone by the time the envelope has.
    // A steal or a fade brings a damper of its own.
    double decay = std::max(1.0e-3, (double)np.stringDecay);
    if (released)
      decay = 1.0 / (1.0 / decay + 1.0 / std::max(5.0e-3, (double)np.release));
    if (noise.damperSeconds > 0.0f)
      decay = 1.0 / (1.0 / decay + 1.0 / (double)noise.damperSeconds);

    noise.string.setBlock(stringPitch, decay, np.stringDamp, np.stringPoint,
                          len);
  }

  NoiseRun run;
  run.mine = mine;
  run.from = ringFrom >= 0 ? ringSource[(size_t)ringFrom].data() : nullptr;
  run.depth = ringStart;
  run.depthInc = (ringEnd - ringStart) / (float)len;
  run.back = backStart;
  run.backInc = (backEnd - backStart) / (float)len;
  run.passed = sourceStart;
  run.passedInc = (sourceEnd - sourceStart) / (float)len;
  run.g = g;
  run.gInc = gInc;
  run.panL = panL;
  run.panR = panR;
  run.left = left;
  run.right = right;
  run.lowMix = lowMix;
  run.highMix = highMix;
  run.amount = amount;
  run.stringOn = stringOn;

  if (defer != nullptr) {
    *defer = run;
    return true;
  }

  for (int n = 0; n < len; ++n)
    tickNoise(run, n);

  finishNoise(run);
  return false;
}

} // namespace ovt
