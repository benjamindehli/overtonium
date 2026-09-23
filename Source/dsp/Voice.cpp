#include "Voice.h"

#include <type_traits>

#include <algorithm>

#include "Character.h"
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

  // Roughly a 15 ms time constant, stepped once per control block.
  pressureCoef =
      1.0f - (float)std::exp(-(double)kControlBlock / (0.015 * sampleRate));

  lowpassCoef =
      1.0f - (float)std::exp(-6.283185307179586 * kNoiseTiltHz / sampleRate);
}

void Voice::prepare(double newSampleRate, uint32_t seed) noexcept {
  setRenderRate(newSampleRate);

  rng.reseed(seed);
  noise.rng.reseed(seed ^ 0x5bf03635u);

  reset();
}

void Voice::reset() noexcept {
  for (auto &pt : partials) {
    pt.env.reset();
    pt.drift.reset();
    pt.phase = 0.0;
    pt.pitchLfo.reset();
    pt.ampLfo.reset();
    pt.lastGain = 0.0f;
    pt.gainPrimed = false;
    pt.lastWave = nullptr;
  }

  noise.env.reset();
  noise.ampLfo.reset();
  noise.lowpassState = 0.0f;
  noise.lastGain = 0.0f;
  noise.gainPrimed = false;
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;

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

void Voice::noteOn(int channel, int note, float velocity,
                   const SynthParams &p) noexcept {
  midiNote = note;
  midiChannel = channel;
  baseFreq = noteFrequency(note, p.global.temperament, p.global.tuningRoot,
                           p.global.referenceHz);

  const float vel = std::clamp(velocity, 0.0f, 1.0f);

  // A fresh note starts unpressed and unbent, and ramps in if the key is
  // already leaned on. A retrigger lands here too, which is what stops the
  // previous note's bend carrying into the new one on a channel being reused.
  noteBendSemitones = 0.0f;
  polyPressure = 0.0f;
  pressureSmoothed = 0.0f;

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

    pt.env.configure(op.delay * pt.delayScale,
                     struckAttack(op.attack, pt.attackScale), op.decay,
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

    // A fresh rate per partial per note. Reusing one rate would turn 32
    // independent wanders into a single detune.
    const double rate = kDriftMinHz * std::pow(kDriftMaxHz / kDriftMinHz,
                                               (double)rng.unipolar());
    pt.drift.restart(rng, rate, sampleRate / (double)kControlBlock);

    if (fresh && p.global.phaseReset) {
      pt.phase = (double)std::clamp(op.startPhase, 0.0f, 1.0f);

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

void Voice::noteOnLegato(int channel, int note, const SynthParams &p) noexcept {
  retune(channel, note,
         noteFrequency(note, p.global.temperament, p.global.tuningRoot,
                       p.global.referenceHz));
}

void Voice::retune(int channel, int note, double frequency) noexcept {
  midiNote = note;
  midiChannel = channel;
  baseFreq = frequency;

  // A key going down while the phrase is still sounding says nothing about
  // bend or pressure, and the ones already in hand belong to the phrase rather
  // than to the key that started it. Left alone on purpose.
  released = false;
}

void Voice::noteOff() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.noteOff();

  noise.env.noteOff();
}

void Voice::steal() noexcept {
  if (!active)
    return;

  released = true;

  for (auto &pt : partials)
    pt.env.forceRelease(0.004f);

  noise.env.forceRelease(0.004f);
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
  noisePeak = 0.0f;
  noiseEnvelope = 0.0f;
  noiseTremolo = 0.0f;

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

    for (int i = 0; i < kNumHarmonics; ++i) {
      auto &pt = partials[(size_t)i];
      const auto &op = p.osc[(size_t)i];

      // Scaled by what the note-on made of the velocity, so turning either
      // knob under a sounding note moves it by the same proportion the blow
      // earned rather than throwing that away. The delay is latched in samples
      // at note-on and only read again by the next one, so it is scaled here to
      // keep the two calls agreeing rather than because this one uses it.
      pt.env.configure(op.delay * pt.delayScale,
                       struckAttack(op.attack, pt.attackScale), op.decay,
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
          (double)(p.global.bendSemitones + noteBendSemitones);

      const double freq = baseFreq * std::exp2(semis / 12.0);

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
                           foldAliases ? kMaxCharacterHarmonic
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
                                          unit.gain[(size_t)i]
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

      // One multiply and one compare per partial per control block. Both terms
      // are already to hand, so metering costs essentially nothing here.
      partialPeaks[(size_t)i] =
          std::max(partialPeaks[(size_t)i], pt.env.getLevel() * gEnd);

      // The same again for the lamps: everything here was worked out above for
      // the oscillator's own use, so this is three stores and a compare.
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
      partialPitches[(size_t)i] = (float)(pmCents + driftCents);

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

    renderNoise(left + start, right + start, len, p, pressure, shared, block);
  }

  active = noise.env.isActive() ||
           std::any_of(partials.begin(), partials.end(),
                       [](const Partial &pt) { return pt.env.isActive(); });

  if (!active) {
    released = false;
    midiNote = -1;
  }
}

void Voice::renderNoise(float *left, float *right, int len,
                        const SynthParams &p, float pressure,
                        const SharedModulation &shared, int block) noexcept {
  const auto &np = p.noise;

  noise.env.configure(np.delay * noise.delayScale,
                      struckAttack(np.attack, noise.attackScale), np.decay,
                      np.sustain, np.swell, np.offLevel, np.release);

  if (!noise.env.isActive())
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
  }

  if (g <= 1.0e-7f && gEnd <= 1.0e-7f) {
    for (int n = 0; n < len; ++n)
      noise.env.tick();

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

  for (int n = 0; n < len; ++n) {
    const float white = noise.rng.bipolar();

    noise.lowpassState += lowpassCoef * (white - noise.lowpassState);
    const float high = white - noise.lowpassState;

    const float s =
        (lowMix * noise.lowpassState + highMix * high) * noise.env.tick() * g;

    left[n] += s * panL;
    right[n] += s * panR;

    g += gInc;
  }
}

} // namespace ovt
