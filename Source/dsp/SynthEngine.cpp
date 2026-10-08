#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace ovt {

void SynthEngine::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);
  renderRate = sampleRate;

  outputStage.prepare(sampleRate);

  // Builds the oscillator tables if nothing has yet, which takes about eight
  // milliseconds: each circuit run over a sine, the harmonics read off it, and
  // a table built for every count of them that fits under Nyquist. They are
  // built once and then only read, but the first read has to pay for them, and
  // the first read would otherwise be the first note. Eight milliseconds is
  // most of a block.
  (void)CharacterTables::instance();

  // And the racks those circuits were built into, which is a few hundred
  // draws rather than eight milliseconds but is on the same first-read path.
  (void)UnitSpread::instance();

  // Distinct seeds so two voices never draw the same drift contour.
  for (size_t i = 0; i < voices.size(); ++i)
    voices[i].prepare(sampleRate, (uint32_t)(i + 1) * 2654435761u);

  wobble.prepare(sampleRate);
  busDrive.prepare(sampleRate);
  echo.prepare(sampleRate);
  bucket.prepare(sampleRate);
  digital.prepare(sampleRate);
  plate.prepare(sampleRate);
  spring.prepare(sampleRate);
  reverb.prepare(sampleRate);

  reset();
}

void SynthEngine::reset() noexcept {
  for (auto &v : voices)
    v.reset();

  wobble.reset();
  busDrive.reset();
  outputStage.reset();
  echo.reset();
  bucket.reset();
  digital.reset();
  plate.reset();
  spring.reset();
  reverb.reset();

  heldBySustain.fill(false);
  sustainDown = false;
  ageCounter = 0;
  lastNoteFrequency = 0.0;
  clock = 0;
  pressedAt.fill(0);
  pressedWithOthers.fill(false);
  forgetLine();
  smoothedMasterGain = -1.0f;

  // Past 1 so the first host sample of the next block draws a fresh frame
  // rather than a stale held one.
  resamplePhase = 1.0;
  heldL = heldR = 0.0f;

  publish({});

  for (auto &level : partialLevels)
    level.store(0.0f, std::memory_order_relaxed);

  noiseLevel.store(0.0f, std::memory_order_relaxed);
  outputLevelL.store(0.0f, std::memory_order_relaxed);
  outputLevelR.store(0.0f, std::memory_order_relaxed);
}

void SynthEngine::setPolyphony(int n) noexcept {
  const auto was = topLine();
  polyphony = std::clamp(n, 1, kMaxPolyphony);

  if (was != topLine())
    forgetLine();
}

void SynthEngine::setLegato(bool on) noexcept {
  const auto was = topLine();
  legato = on;

  if (was != topLine())
    forgetLine();
}

void SynthEngine::forgetLine() noexcept {
  handedOff.fill(false);
  takenFrom.fill(-1);
}

void SynthEngine::letGo(size_t index, float lift) noexcept {
  auto &v = voices[index];

  // Under the top line, a note whose phrase a key above carried on, and the
  // upper note of a trill whose lower key is still down, both go quickly: the
  // phrase is somewhere else now, and a full release under it is a smear.
  bool quiet = false;

  if (topLine()) {
    const auto from = takenFrom[index];

    quiet = handedOff[index] || (from >= 0 && voices[(size_t)from].isActive() &&
                                 !voices[(size_t)from].isReleasing());
  }

  if (quiet)
    v.fade((float)kHandoffFadeSeconds);
  else
    v.noteOff(lift);
}

int SynthEngine::countSounding() const noexcept {
  int c = 0;
  for (const auto &v : voices)
    if (v.isActive() && !v.isReleasing())
      ++c;

  return c;
}

int SynthEngine::getActiveVoiceCount() const noexcept {
  int c = 0;
  for (const auto &v : voices)
    if (v.isActive())
      ++c;

  return c;
}

Voice *SynthEngine::findFreeVoice() noexcept {
  for (auto &v : voices)
    if (!v.isActive())
      return &v;

  return nullptr;
}

Voice *SynthEngine::findOldestSounding(int spare) noexcept {
  Voice *oldest = nullptr;

  for (size_t i = 0; i < voices.size(); ++i) {
    auto &v = voices[i];

    if (v.isActive() && !v.isReleasing() && (int)i != spare)
      if (oldest == nullptr || v.getAge() < oldest->getAge())
        oldest = &v;
  }

  // With nothing else to take, the spared one goes after all.
  if (oldest == nullptr && spare >= 0 && voices[(size_t)spare].isActive())
    oldest = &voices[(size_t)spare];

  return oldest;
}

Voice *SynthEngine::findQuietestExpendable() noexcept {
  Voice *best = nullptr;
  bool bestReleasing = false;

  for (auto &v : voices) {
    if (!v.isActive())
      return &v;

    // A voice on its way out is always a better thing to take than one with a
    // key still down on it, however loud the two happen to be right now.
    const bool releasing = v.isReleasing();

    if (best == nullptr || (releasing && !bestReleasing) ||
        (releasing == bestReleasing && v.lastLevel() < best->lastLevel())) {
      best = &v;
      bestReleasing = releasing;
    }
  }

  return best;
}

bool SynthEngine::matches(const Voice &v, int channel, int note) noexcept {
  return v.isActive() && v.getNote() == note && v.getChannel() == channel;
}

void SynthEngine::noteOn(int note, float velocity,
                         const SynthParams &p) noexcept {
  noteOnImpl(0, note, velocity, p);
}

void SynthEngine::noteOnPerNote(int channel, int note, float velocity,
                                const SynthParams &p) noexcept {
  noteOnImpl(channel, note, velocity, p);
}

void SynthEngine::legatoRelease(int note) noexcept {
  for (int i = 0; i < legatoDepth; ++i) {
    if (legatoHeld[(size_t)i] != note)
      continue;

    for (int j = i; j + 1 < legatoDepth; ++j)
      legatoHeld[(size_t)j] = legatoHeld[(size_t)j + 1];

    --legatoDepth;
    return;
  }
}

void SynthEngine::noteOnImpl(int channel, int note, float velocity,
                             const SynthParams &p) noexcept {
  glide = GlideSettings::of(p);

  // The tuning this note is under, which a note-off falling back to a held key
  // needs and does not carry.
  legatoTemperament = p.global.temperament;
  legatoRoot = p.global.tuningRoot;
  legatoReferenceHz = p.global.referenceHz;

  // ---- where a glide comes from ---------------------------------------------
  //
  // Worked out before anything below steals or retriggers a voice, since the
  // one a glide starts from can be the one about to be taken.
  //
  // Always glides from the note played last, held or not, and from its bare
  // frequency once its voice has finished. Legato glides only from a key that
  // is still down. A key held by the pedal does not count: the pedal holds the
  // sound, not the hand, and a phrase played detached over it is detached.
  const Voice *latest = nullptr;
  const Voice *latestHeld = nullptr;

  for (size_t i = 0; i < voices.size(); ++i) {
    const auto &v = voices[i];

    if (!v.isActive())
      continue;

    if (latest == nullptr || v.getAge() > latest->getAge())
      latest = &v;

    if (!v.isReleasing() && !heldBySustain[i] &&
        (latestHeld == nullptr || v.getAge() > latestHeld->getAge()))
      latestHeld = &v;
  }

  bool glides = false;
  Voice::Pitches from{};

  if (p.global.glideTrigger == GlideTrigger::Legato) {
    if (latestHeld != nullptr) {
      from = latestHeld->notePitches();
      glides = true;
    }
  } else if (latest != nullptr) {
    from = latest->notePitches();
    glides = true;
  } else if (lastNoteFrequency > 0.0) {
    from = Voice::uniformPitches(lastNoteFrequency);
    glides = true;
  }

  // ---- legato ---------------------------------------------------------------
  //
  // One voice, and a key going down while another is still held moves the note
  // rather than starting it again: the envelopes carry on and a run keeps the
  // shape the first key gave it. The phrase ends when the last key comes up,
  // which is the whole difference from one voice without it.
  if (legato && polyphony <= 1) {
    legatoRelease(note);

    if (legatoDepth < (int)legatoHeld.size())
      legatoHeld[(size_t)legatoDepth++] = note;

    legatoTemperament = p.global.temperament;
    legatoRoot = p.global.tuningRoot;
    legatoReferenceHz = p.global.referenceHz;

    // Whatever is still sounding and has not been let go carries the phrase.
    for (auto &v : voices) {
      if (!v.isActive() || v.isReleasing())
        continue;

      // From wherever it is, which mid-glide is somewhere between two notes.
      // A key is held by definition here, so both triggers glide.
      const auto own = v.notePitches();
      v.noteOnLegato(channel, note, p);
      v.startGlide(own, glide);
      v.setAge(++ageCounter);
      lastNoteFrequency = v.getFrequency();
      return;
    }

    // Nothing to carry it, so this key starts the phrase. Any tail still
    // fading from the last one is cut, since a second voice is what legato
    // exists to avoid.
    for (auto &v : voices)
      if (v.isActive())
        v.steal();
  }

  // ---- the top line ---------------------------------------------------------
  //
  // Legato with more than one voice. A key landing above every held note but
  // the highest carries the highest note's phrase on: it starts a voice of its
  // own that takes over that note's envelopes where they stand and glides from
  // its pitch, so a melody played joined over a chord slides from note to note
  // without starting again. The note it carried on from is never moved, so a
  // key that turns out to be part of the chord still sounds, and it fades once
  // its key comes up rather than ringing out under the melody. Every other key
  // plays as it always does.
  const auto now = clock;
  const auto window = (uint64_t)(kChordWindowSeconds * sampleRate);
  bool withOthers = false;

  for (size_t i = 0; i < voices.size(); ++i)
    if (voices[i].isActive() && now - pressedAt[i] < window) {
      pressedWithOthers[i] = true;
      withOthers = true;
    }

  // The voice whose phrase this key carries on, if it does, and the top line
  // the steal below must leave alone.
  int carryFrom = -1;
  int spare = -1;

  if (topLine()) {
    // The highest note a hand is holding, and the highest beneath it, and the
    // highest of the keys struck in the last moment, which are a chord being
    // played or the melody landing on one. The pedal does not count: it holds
    // the sound, not the line.
    int top = -1, beneath = -1, struckTop = -1;

    for (size_t i = 0; i < voices.size(); ++i) {
      const auto &v = voices[i];

      if (!v.isActive() || v.isReleasing() || heldBySustain[i])
        continue;

      if (top < 0 || v.getNote() > voices[(size_t)top].getNote()) {
        if (top >= 0)
          beneath = std::max(beneath, voices[(size_t)top].getNote());
        top = (int)i;
      } else {
        beneath = std::max(beneath, v.getNote());
      }

      if (now - pressedAt[i] < window)
        struckTop = std::max(struckTop, v.getNote());
    }

    spare = top;

    if (top >= 0) {
      const auto head = voices[(size_t)top].getNote();

      // When a key goes down under the top note, the melody stepping down and
      // a chord struck under a held melody look the same, and only what the
      // old melody key does afterwards would tell them apart. So the rule
      // leans on what is likely, and a wrong guess costs no more than an
      // attack, since the note it carried on from goes on sounding.
      bool carries = false;

      if (note > head) {
        // The melody going up, or the top of a new chord over it.
        carries = true;
      } else if (note < head && note > beneath) {
        if (!withOthers && beneath >= 0) {
          // Over a chord that is being held, with nothing else just struck:
          // the melody stepping down, however far.
          carries = true;
        } else {
          // Alone under a lone melody, or among keys struck together: the
          // melody only if it is a step away and the highest of what was just
          // struck, since a melody moves by seconds and thirds and the top of
          // a chord sits further down.
          carries = head - note <= kStepDown && note > struckTop;
        }
      }

      if (carries)
        carryFrom = top;
    }
  }

  // One key, one voice. The instrument is polyphonic across the keyboard and
  // monophonic within a key, because a string or a tine or a bar is one
  // object: striking it again takes over whatever it was already doing rather
  // than starting a second copy of it beside the first.
  //
  // Tails count. A key with a long release that is tapped repeatedly would
  // otherwise leave every tap ringing and sum them, which no physical
  // instrument does and which is also the quickest way to reach the clipper.
  //
  // Switchable, because summing the tails is a sound as well as a fault: a
  // bell struck over and over builds up, and somebody wanting that should be
  // able to have it. The whole rule goes with the switch rather than half of
  // it, so that off means every strike gets a voice of its own and nothing
  // about the previous strike is disturbed.
  if (p.global.oneVoicePerKey) {
    Voice *held = nullptr;
    size_t heldIndex = 0;

    for (size_t i = 0; i < voices.size(); ++i) {
      auto &v = voices[i];

      if (!matches(v, channel, note))
        continue;

      if (v.isReleasing()) {
        // Cut with the same short fade a stolen voice gets. Fast enough to
        // read as instant, slow enough not to click.
        v.steal();
      } else {
        held = &v;
        heldIndex = i;
      }
    }

    // A key still down, or held by the pedal, is retriggered where it stands.
    // That keeps two things the fade would lose: a legato retrigger continues
    // from the level the envelope is at rather than restarting from silence,
    // and re-striking a pedalled note takes it back off the pedal.
    if (held != nullptr) {
      heldBySustain[heldIndex] = false;
      pressedAt[heldIndex] = now;
      pressedWithOthers[heldIndex] = withOthers;
      held->noteOn(channel, note, velocity, p);

      if (glides)
        held->startGlide(from, glide);

      held->setAge(++ageCounter);
      lastNoteFrequency = held->getFrequency();
      return;
    }
  }

  if (countSounding() >= polyphony)
    if (auto *victim = findOldestSounding(spare))
      victim->steal();

  auto *target = findFreeVoice();

  if (target == nullptr) {
    // Pool exhausted: every voice is doing something and the new note cannot
    // wait for a fade, so one of them has to go this instant. That is a step
    // in the output whichever it is, and the size of the step is that voice's
    // current level, so take the quietest.
    //
    // It used to take the oldest, which is the right rule for stealing under
    // the polyphony limit and the wrong one here: the oldest voice is often a
    // note still being held, while a tail three quarters of the way through
    // its release is sitting right beside it costing almost nothing to lose.
    target = findQuietestExpendable();

    if (target == nullptr)
      return;

    target->reset();
  }

  const auto index = (size_t)std::distance(voices.data(), target);
  heldBySustain[index] = false;

  // A fresh note, so not a line yet, and if it was the line's voice the line
  // has gone with it.
  pressedAt[index] = now;
  pressedWithOthers[index] = withOthers;
  handedOff[index] = false;
  takenFrom[index] = -1;

  // Anything that remembered taking its phrase from whatever played on this
  // voice before has nothing to remember now.
  for (auto &source : takenFrom)
    if (source == (int)index)
      source = -1;

  target->noteOn(channel, note, velocity, p);

  if (carryFrom >= 0 && carryFrom != (int)index &&
      voices[(size_t)carryFrom].isActive()) {
    // The phrase carried on: where the note below it has got to, envelopes,
    // modulators and pitch, with a glide from that pitch. A key is held by
    // definition here, so both triggers glide.
    const auto &source = voices[(size_t)carryFrom];
    target->takeOver(source);
    target->startGlide(source.notePitches(), glide);

    handedOff[(size_t)carryFrom] = true;
    takenFrom[index] = carryFrom;
  } else if (glides) {
    target->startGlide(from, glide);
  }

  target->setAge(++ageCounter);
  lastNoteFrequency = target->getFrequency();
}

void SynthEngine::noteOff(int note, float lift) noexcept {
  noteOffImpl(0, note, lift);
}

void SynthEngine::noteOffPerNote(int channel, int note, float lift) noexcept {
  noteOffImpl(channel, note, lift);
}

void SynthEngine::noteOffImpl(int channel, int note, float lift) noexcept {
  if (legato && polyphony <= 1) {
    legatoRelease(note);

    // Another key is still down, so the phrase moves to it rather than
    // stopping. Falling back to the most recent is what makes a trill work.
    if (legatoDepth > 0) {
      for (auto &v : voices)
        if (v.isActive() && !v.isReleasing()) {
          const auto back = legatoHeld[(size_t)legatoDepth - 1];
          const auto own = v.notePitches();

          v.retune(channel, back,
                   noteFrequency(back, legatoTemperament, legatoRoot,
                                 legatoReferenceHz));
          v.startGlide(own, glide);
          lastNoteFrequency = v.getFrequency();
          return;
        }

      return;
    }
  }

  for (size_t i = 0; i < voices.size(); ++i) {
    auto &v = voices[i];

    if (matches(v, channel, note) && !v.isReleasing()) {
      if (sustainDown) {
        heldBySustain[i] = true;
        heldLift[i] = lift;
      } else {
        letGo(i, lift);
      }
    }
  }
}

void SynthEngine::setSustainPedal(bool down) noexcept {
  sustainDown = down;

  if (down)
    return;

  for (size_t i = 0; i < voices.size(); ++i) {
    if (heldBySustain[i]) {
      letGo(i, heldLift[i]);
      heldBySustain[i] = false;
      heldLift[i] = 1.0f;
    }
  }
}

void SynthEngine::setPolyPressure(int note, float pressure) noexcept {
  for (auto &v : voices)
    if (matches(v, 0, note))
      v.setPolyPressure(pressure);
}

void SynthEngine::setNotePressure(int channel, int note,
                                  float pressure) noexcept {
  for (auto &v : voices)
    if (matches(v, channel, note))
      v.setPolyPressure(pressure);
}

void SynthEngine::setNoteSlide(int channel, int note, float slide) noexcept {
  for (auto &v : voices)
    if (matches(v, channel, note))
      v.setSlide(slide);
}

void SynthEngine::setNoteBend(int channel, int note, float semitones) noexcept {
  for (auto &v : voices)
    if (matches(v, channel, note))
      v.setNoteBend(semitones);
}

void SynthEngine::allNotesOff() noexcept {

  for (size_t i = 0; i < voices.size(); ++i) {
    voices[i].noteOff();
    heldBySustain[i] = false;
  }

  sustainDown = false;
}

void SynthEngine::allSoundOff() noexcept {

  for (auto &v : voices)
    v.reset();

  heldBySustain.fill(false);
  sustainDown = false;
}

namespace {
/// Rounds to the nearest of 2^(bits-1) steps either side of zero.
///
/// Deliberately does not clamp. This sits ahead of the master fader and the
/// safety clipper, which is where clipping belongs, and a quantiser that also
/// clipped would turn a bit-depth setting into a distortion the panel does not
/// mention.
inline void quantise(float *left, float *right, int n, int bits) noexcept {
  const float levels = (float)(1 << (bits - 1));
  const float step = 1.0f / levels;

  for (int i = 0; i < n; ++i) {
    left[i] = std::round(left[i] * levels) * step;
    right[i] = std::round(right[i] * levels) * step;
  }
}
} // namespace

double SynthEngine::lofiRenderRate(const SynthParams &p) const noexcept {
  if (p.lofi.rateHz <= 0.0)
    return sampleRate;

  // Asking for more than the host is running at is not a thing anyone can
  // have, so it reads as off rather than as an upsample.
  return std::clamp(p.lofi.rateHz, 1000.0, sampleRate);
}

void SynthEngine::setRenderRate(double rate) noexcept {
  if (exactly(rate, renderRate))
    return;

  renderRate = rate;

  for (auto &v : voices)
    v.setRenderRate(rate);
}

SharedModulation
SynthEngine::advanceSharedModulators(int numFrames,
                                     const SynthParams &p) noexcept {
  SharedModulation out;

  if (!p.global.pitchModInPhase && !p.global.ampModInPhase)
    return out;

  // The same blocks, of the same lengths, that every voice is about to walk.
  const int blocks =
      (numFrames + Voice::kControlBlock - 1) / Voice::kControlBlock;
  out.stride = kModBoundaries;

  // One channel at a time, because a channel's rate and shape are its own even
  // though the switch over them is not.
  const auto fill =
      [&](std::array<Lfo, kNumHarmonics + 1> &modulators,
          std::array<float, (kNumHarmonics + 1) * kModBoundaries> &table,
          int channel, float rateHz, LfoShape shape, double offsetTurns) {
        auto &lfo = modulators[(size_t)channel];
        const double increment = (double)rateHz / renderRate;
        const auto at = (size_t)(channel * kModBoundaries);

        table[at] = lfo.value(shape, offsetTurns);

        for (int b = 0; b < blocks; ++b) {
          const int len = std::min(Voice::kControlBlock,
                                   numFrames - b * Voice::kControlBlock);

          table[at + (size_t)b + 1] = lfo.advance(
              sharedRandom, increment * (double)len, shape, offsetTurns);
        }
      };

  if (p.global.pitchModInPhase) {
    for (int i = 0; i < kNumHarmonics; ++i)
      fill(pitchModulators, sharedPitch, i, p.osc[(size_t)i].pmRateHz,
           p.osc[(size_t)i].pmShape, 0.0);

    // The noise channel has no pitch of its own to modulate, so its slot is
    // left where it is rather than stepped.
    out.pitch = sharedPitch.data();
  }

  if (p.global.ampModInPhase) {
    for (int i = 0; i < kNumHarmonics; ++i)
      fill(ampModulators, sharedAmp, i, p.osc[(size_t)i].amRateHz,
           p.osc[(size_t)i].amShape, kAmpShapeOffset);

    fill(ampModulators, sharedAmp, kNumHarmonics, p.noise.amRateHz,
         p.noise.amShape, kAmpShapeOffset);

    out.amp = sharedAmp.data();
  }

  return out;
}

void SynthEngine::sumVoices(float *left, float *right, int numFrames,
                            const SynthParams &p, Activity &into) noexcept {
  std::fill(left, left + numFrames, 0.0f);
  std::fill(right, right + numFrames, 0.0f);

  for (int done = 0; done < numFrames;) {
    // Everything the pool can take in one pass, which for a host asking its
    // usual buffer size is the whole of it.
    const int len = std::min(kModChunk, numFrames - done);
    const auto shared = advanceSharedModulators(len, p);

    sumChunk(left + done, right + done, len, p, into, shared);
    done += len;
  }
}

void SynthEngine::sumChunk(float *left, float *right, int numFrames,
                           const SynthParams &p, Activity &into,
                           const SharedModulation &shared) noexcept {
  for (auto &v : voices) {
    if (!v.isActive())
      continue;

    v.render(left, right, numFrames, p, shared);

    // The lamps follow the meter rather than being gathered on their own
    // terms. Whichever voice is loudest on a partial is the one you are
    // listening to on that partial, so it is the one whose envelope and
    // modulation are worth showing. Taking the maximum of each separately
    // would describe no note in particular.
    const auto &peaks = v.getPartialPeaks();
    const auto &envelopes = v.getPartialEnvelopes();
    const auto &tremolos = v.getPartialTremolos();
    const auto &pitches = v.getPartialPitches();
    const auto &velocities = v.getPartialVelocities();
    const auto &pressures = v.getPartialPressures();

    for (size_t i = 0; i < peaks.size(); ++i) {
      // A voice that reaches here is sounding, so it has something to say
      // about this partial even when what it is saying is that the partial
      // is at zero. See Activity::claimed.
      if (into.claimed[i] && peaks[i] <= into.peaks[i])
        continue;

      into.claimed[i] = true;
      into.peaks[i] = peaks[i];
      into.envelopes[i] = envelopes[i];
      into.tremolos[i] = tremolos[i];
      into.pitches[i] = pitches[i];
      into.velocities[i] = velocities[i];
      into.pressures[i] = pressures[i];
    }

    if (!into.noiseClaimed || v.getNoisePeak() > into.noisePeak) {
      into.noiseClaimed = true;
      into.noisePeak = v.getNoisePeak();
      into.noiseEnvelope = v.getNoiseEnvelope();
      into.noiseTremolo = v.getNoiseTremolo();
      into.noiseVelocity = v.getNoiseVelocity();
      into.noisePressure = v.getNoisePressure();
    }
  }
}

void SynthEngine::publish(const Activity &a) noexcept {
  for (size_t i = 0; i < a.peaks.size(); ++i) {
    partialLevels[i].store(a.peaks[i], std::memory_order_relaxed);
    partialEnvelopes[i].store(a.envelopes[i], std::memory_order_relaxed);
    partialTremolos[i].store(a.tremolos[i], std::memory_order_relaxed);
    partialPitches[i].store(a.pitches[i], std::memory_order_relaxed);
    partialVelocities[i].store(a.velocities[i], std::memory_order_relaxed);
    partialPressures[i].store(a.pressures[i], std::memory_order_relaxed);
  }

  noiseLevel.store(a.noisePeak, std::memory_order_relaxed);
  noiseEnvelope.store(a.noiseEnvelope, std::memory_order_relaxed);
  noiseTremolo.store(a.noiseTremolo, std::memory_order_relaxed);
  noiseVelocity.store(a.noiseVelocity, std::memory_order_relaxed);
  noisePressure.store(a.noisePressure, std::memory_order_relaxed);
}

void SynthEngine::renderVoices(float *left, float *right, int numSamples,
                               const SynthParams &p) noexcept {
  const auto target = lofiRenderRate(p);
  const int bits = std::clamp(p.lofi.bits, 0, 24);

  Activity activity;

  if (target >= sampleRate) {
    setRenderRate(sampleRate);
    sumVoices(left, right, numSamples, p, activity);

    if (bits > 0)
      quantise(left, right, numSamples, bits);
  } else {
    // The whole pool renders slowly and the result is held between frames.
    //
    // This is where the setting pays for itself. The obvious way to build a
    // rate reducer is to render everything at the host rate and then hold the
    // output, but sampling a sinusoid at 8 kHz gives one particular sequence
    // of numbers whatever rate you were nominally computing at, so the samples
    // that survive holding are the only ones worth computing. Thirty-two
    // oscillators and their envelopes are what this instrument costs, and
    // against a 48 kHz host they now run a sixth as often.
    //
    // Not quite identical to the expensive way: the per-control-block work,
    // the LFOs and the gain ramps, still lands every 32 frames, which is now
    // 4 ms rather than 0.7. Modulation is coarser, at the rate the rest of it
    // is coarser. Everything is still in the right place in real time,
    // because the coefficients are all derived from the rate being rendered
    // at.
    setRenderRate(target);

    const double ratio = target / sampleRate;

    for (int done = 0; done < numSamples;) {
      const int len = std::min(kLofiChunk, numSamples - done);

      // How many frames this chunk of output will draw from. Counted first
      // rather than estimated, so the expansion below never runs off the end
      // of what was rendered.
      int frames = 0;
      {
        double ph = resamplePhase;

        for (int n = 0; n < len; ++n) {
          ph += ratio;

          if (ph >= 1.0) {
            ph -= 1.0;
            ++frames;
          }
        }
      }

      sumVoices(lofiScratchL.data(), lofiScratchR.data(), frames, p, activity);

      if (bits > 0)
        quantise(lofiScratchL.data(), lofiScratchR.data(), frames, bits);

      int src = 0;

      for (int n = 0; n < len; ++n) {
        resamplePhase += ratio;

        if (resamplePhase >= 1.0) {
          resamplePhase -= 1.0;
          heldL = lofiScratchL[(size_t)src];
          heldR = lofiScratchR[(size_t)src];
          ++src;
        }

        left[done + n] = heldL;
        right[done + n] = heldR;
      }

      done += len;
    }
  }

  publish(activity);
}

void SynthEngine::render(float *left, float *right, int numSamples,
                         const SynthParams &p) noexcept {
  if (numSamples <= 0)
    return;

  glide = GlideSettings::of(p);
  renderVoices(left, right, numSamples, p);
  clock += (uint64_t)numSamples;

  // ---- the bus the series is summed onto -----------------------------------
  // Before the effects, since this is the summing amplifier rather than
  // something applied to what comes out of one. See BusDrive.
  busDrive.process(left, right, numSamples, p.global.character,
                   p.global.busDrive);

  // ---- master effects, ahead of the fader ----------------------------------
  // The channel meters above read the partials themselves, so they are taken
  // before this point. The output meter is taken after it, which is why the
  // two disagree once a tail is ringing: that is the effects, and it should
  // show.
  wobble.process(left, right, numSamples, p.global.wobbleAmount);
  // Both are asked and each decides whether the type is its own, so the one
  // that is not chosen empties its loop rather than holding a tail that would
  // come back if you switched to it.
  echo.process(left, right, numSamples, p.echo);
  bucket.process(left, right, numSamples, p.echo);
  digital.process(left, right, numSamples, p.echo);
  reverb.process(left, right, numSamples, p.reverb);
  plate.process(left, right, numSamples, p.reverb);
  spring.process(left, right, numSamples, p.reverb);

  // ---- master gain, smoothed over ~10 ms so fader moves do not zipper -------
  const float target = std::max(0.0f, p.global.masterGain);

  if (smoothedMasterGain < 0.0f)
    smoothedMasterGain = target;

  const auto coef = (float)std::exp(-1.0 / (0.01 * sampleRate));

  if (std::abs(target - smoothedMasterGain) < 1.0e-6f) {
    smoothedMasterGain = target;

    for (int n = 0; n < numSamples; ++n) {
      left[n] *= target;
      right[n] *= target;
    }
  } else {
    float g = smoothedMasterGain;

    for (int n = 0; n < numSamples; ++n) {
      g = target + (g - target) * coef;
      left[n] *= g;
      right[n] *= g;
    }

    smoothedMasterGain = g;
  }

  // Always, because the stage delays whether or not it shapes: its latency is
  // declared once and must not depend on whether this switch is on. See
  // OutputStage::kLookaheadSeconds.
  outputStage.process(left, right, numSamples, p.global.clipType,
                      p.global.safetyClip, p.global.lookahead);

  float peakL = 0.0f, peakR = 0.0f;
  for (int n = 0; n < numSamples; ++n) {
    peakL = std::max(peakL, std::abs(left[n]));
    peakR = std::max(peakR, std::abs(right[n]));
  }

  outputLevelL.store(peakL, std::memory_order_relaxed);
  outputLevelR.store(peakR, std::memory_order_relaxed);
}

} // namespace ovt
