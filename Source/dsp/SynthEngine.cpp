#include "SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace ovt {

void SynthEngine::prepare(double newSampleRate) noexcept {
  sampleRate = std::max(1.0, newSampleRate);
  renderRate = sampleRate;

  outputStage.prepare(sampleRate);
  sympathetic.prepare(sampleRate);

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
  sympathetic.reset();
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
  adaptiveVersion.fill(0);
  adaptiveSnapshots.publish(AdaptiveSnapshot{});
  clock = 0;
  motorTurns = 0.0;
  hissLowGain = hissHighGain = hissFirst = hissLowState = 0.0f;
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

SynthEngine::HeldExpression SynthEngine::heldExpression() const noexcept {
  HeldExpression held;

  for (const auto &v : voices) {
    if (!v.isActive() || v.isReleasing())
      continue;

    if (std::abs(v.getNoteBend()) > std::abs(held.bendSemitones))
      held.bendSemitones = v.getNoteBend();

    held.pressure = std::max(held.pressure, v.getPolyPressure());
  }

  return held;
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
  adaptiveOn = p.global.temperament == Temperament::Adaptive;
  adaptiveRetune = p.global.adaptiveRetuneSeconds;
  harmonicSevenths = p.global.harmonicSevenths;
  adaptiveHome = p.global.adaptiveHome;

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
      v.retune(channel, note,
               frequencyFor(note, p.global.temperament, p.global.tuningRoot,
                            p.global.referenceHz, &v));
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
      held->noteOn(channel, note, velocity, p,
                   frequencyFor(note, p.global.temperament, p.global.tuningRoot,
                                p.global.referenceHz, held));

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

  target->noteOn(channel, note, velocity, p,
                 frequencyFor(note, p.global.temperament, p.global.tuningRoot,
                              p.global.referenceHz, target));

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

double SynthEngine::frequencyFor(int note, Temperament temperament, int root,
                                 double referenceHz,
                                 const Voice *self) noexcept {
  if (temperament != Temperament::Adaptive)
    return noteFrequency(note, temperament, root, referenceHz);

  return settleAdaptive(note, root, referenceHz, self);
}

double SynthEngine::settleAdaptive(int note, int root, double referenceHz,
                                   const Voice *self) noexcept {
  // How each interval the keys spell is pure, as steps along the chain of
  // tempered fifths from the lower key to the higher, and how much it counts.
  // The octave and the fifth carry most, since a mistuned one is the first
  // thing anyone hears. A seventh counts least, only deciding what nothing
  // else does, so it comes out the same whether or not the fifth beneath it
  // is held: 9:5 for a minor seventh, a fifth and a minor third, and 15:8 for
  // a major, a fifth and a major third. Seconds and the tritone have no one
  // pure size a chord is reaching for, so they come last of all, at the sizes
  // the just major scale gives them, 16:15, 9:8 and 45:32. They only tie
  // together notes nothing stronger does, so every held key is in one group
  // and the chord comes out the same however it was played.
  struct Pure {
    int steps;
    int weight;
  };

  static constexpr Pure kByClass[12] = {
      {0, 4},  // unison, octave
      {7, 0},  // minor second: a fourth up, a major third down
      {2, 0},  // major second: two fifths
      {9, 2},  // minor third: a fifth up, a major third down
      {-8, 2}, // major third
      {-1, 3}, // fourth
      {-6, 0}, // tritone: two fifths and a major third
      {1, 3},  // fifth
      {8, 2},  // minor sixth
      {-9, 2}, // major sixth
      {10, 1}, // minor seventh
      {-7, 1}, // major seventh
  };

  // Where each pitch class's middle version sits on that chain, as fifths and
  // major thirds from the root, a third counting as eight fifths down. The
  // same just-major places Temperament.h builds the middle versions from.
  static constexpr int kMiddleStep[12] = {0,  7, 2, 9,  -8, -1,
                                          -6, 1, 8, -9, 10, -7};

  // The same intervals as cents off their equal-tempered size, for weighing
  // what is still ringing, which has a frequency rather than a place on the
  // chain.
  static constexpr double kFifthOff = tuning::kPureFifth - 700.0;
  static constexpr double kThirdOff = tuning::kPureMajorThird - 400.0;
  static constexpr double kMinorOff = kFifthOff - kThirdOff;
  static constexpr double kSeventhOff = kFifthOff + kMinorOff;
  static constexpr double kMajorSeventhOff = kFifthOff + kThirdOff;
  static constexpr double kOffEqual[12] = {
      0.0, 0.0,       0.0,        kMinorOff,  kThirdOff,   -kFifthOff,
      0.0, kFifthOff, -kThirdOff, -kMinorOff, kSeventhOff, kMajorSeventhOff};

  // Two commas either side of the middle. A comma is twelve steps.
  static constexpr int kReach = 2;

  // A tail is heard while it is still a fair part of what is sounding and
  // above -60 dB. See the tests for why the floor is there.
  static constexpr float kAudibleShare = 0.25f;
  static constexpr float kInaudible = 0.001f;

  const auto classOf = [root](int key) {
    return ((key - root) % 12 + 12) % 12;
  };

  const auto hzOf = [root, referenceHz](int key, int version) {
    return noteFrequency(key, Temperament::Adaptive, root, referenceHz) *
           std::exp2((double)version * tuning::kAdaptiveComma / 1200.0);
  };

  const auto versionOf = [&hzOf](const Voice &v) {
    return (int)std::lround(1200.0 *
                            std::log2(v.getFrequency() / hzOf(v.getNote(), 0)) /
                            tuning::kAdaptiveComma);
  };

  // ---- who takes part
  // --------------------------------------------------------
  struct Node {
    Voice *voice; // null for the key going down
    int key;
    int now;       // its version as it stands, unused for the key going down
    bool young;    // inside the chord window, so it moves at once and cheaply
    bool harmonic; // a seventh tuned 7:4 above its root, off the chain
  };

  std::array<Node, kPoolSize + 1> nodes{};
  int count = 0;

  const auto window = (uint64_t)(kChordWindowSeconds * sampleRate);
  float loudest = 0.0f;

  for (size_t i = 0; i < voices.size(); ++i) {
    auto &v = voices[i];
    if (!v.isActive() || &v == self)
      continue;

    loudest = std::max(loudest, v.lastLevel());

    if (!v.isReleasing())
      nodes[(size_t)count++] = {&v, v.getNote(), versionOf(v),
                                clock - pressedAt[i] < window, false};
  }

  if (note >= 0)
    nodes[(size_t)count++] = {nullptr, note, 0, true, false};

  if (count == 0) {
    // Nothing held, which the screen needs to know as much as anything else.
    AdaptiveSnapshot none;
    none.referenceHz = referenceHz;
    none.remembered = adaptiveVersion;
    adaptiveSnapshots.publish(none);
    return 0.0;
  }

  // Lowest key first, so where a chord cannot all be pure the interval left
  // out is the same whatever order the keys went down in.
  std::sort(nodes.begin(), nodes.begin() + count,
            [](const Node &a, const Node &b) { return a.key < b.key; });

  // ---- the harmonic seventh -------------------------------------------------
  //
  // A root's minor seventh is tuned 7:4 above it, the harmonic seventh, unless
  // another held note would then sit out of tune with it. Over the root, its
  // major third, its fifth and its ninth, the seventh makes a chord whose
  // notes are harmonics 4, 5, 6, 7 and 9 of one note two octaves below the
  // root, so every partial of every note lands on another and nothing beats,
  // where the chain would leave the seventh 9:5 against the seventh harmonic
  // of the root. A note a minor third, a fourth, a minor second or a tritone
  // above the root would meet the seventh at a fifth, a fourth or a third
  // well off pure, which beats much harder, so with one of those held the
  // seventh keeps the size the chain gives it. It is the only pitch here off
  // the chain of fifths, which has no 7 in it, so it is left out of what the
  // chain solves and placed on the root afterwards. Where more than one root
  // qualifies, the one lowest on the keyboard is the chord.
  bool held[12] = {};
  int lowestOf[12];
  for (auto &l : lowestOf)
    l = 1 << 20;
  for (int i = 0; i < count; ++i) {
    const auto pc = classOf(nodes[(size_t)i].key);
    held[(size_t)pc] = true;
    lowestOf[(size_t)pc] = std::min(lowestOf[(size_t)pc], nodes[(size_t)i].key);
  }

  // None at all when the harmonic seventh is switched off, and every seventh
  // then takes the size the chain gives it.
  int dominant = -1;
  for (int pc = 0; pc < 12 && harmonicSevenths; ++pc)
    if (held[(size_t)pc] && held[(size_t)((pc + 10) % 12)] &&
        !held[(size_t)((pc + 1) % 12)] && !held[(size_t)((pc + 3) % 12)] &&
        !held[(size_t)((pc + 5) % 12)] && !held[(size_t)((pc + 6) % 12)] &&
        (dominant < 0 || lowestOf[(size_t)pc] < lowestOf[(size_t)dominant]))
      dominant = pc;

  if (dominant >= 0)
    for (int i = 0; i < count; ++i)
      nodes[(size_t)i].harmonic =
          classOf(nodes[(size_t)i].key) == (dominant + 10) % 12;

  // ---- the strongest intervals first, each kept if it agrees
  // -----------------
  //
  // A union of groups, each node carrying how many commas it sits from its
  // group's first node. Two nodes in one group already have a difference;
  // an interval that wants another one is a contradiction the chord cannot
  // be pure without, like the third an augmented triad has one too many of,
  // and is left out.
  std::array<int, kPoolSize + 1> group{}, offset{};
  for (int i = 0; i < count; ++i) {
    group[(size_t)i] = i;
    offset[(size_t)i] = 0;
  }

  const auto find = [&group, &offset](int i) {
    int total = 0;
    while (group[(size_t)i] != i) {
      total += offset[(size_t)i];
      i = group[(size_t)i];
    }
    return std::pair<int, int>{i, total};
  };

  // In commas, how far above the lower key's version the higher key's has to
  // be for the pair to be pure. The keys are sorted, so a is the lower.
  const auto wanted = [&nodes, &classOf](int a, int b) {
    const auto &low = nodes[(size_t)a];
    const auto &high = nodes[(size_t)b];
    const auto &pure = kByClass[(size_t)((high.key - low.key) % 12)];
    const auto steps = pure.steps - (kMiddleStep[(size_t)classOf(high.key)] -
                                     kMiddleStep[(size_t)classOf(low.key)]);
    return steps / 12; // always whole: see kMiddleStep
  };

  const auto weightOf = [&nodes](int a, int b) {
    if (nodes[(size_t)a].harmonic || nodes[(size_t)b].harmonic)
      return -1;
    return kByClass[(size_t)((nodes[(size_t)b].key - nodes[(size_t)a].key) %
                             12)]
        .weight;
  };

  for (const int weight : {4, 3, 2, 1, 0})
    for (int lowIndex = 0; lowIndex < count; ++lowIndex)
      for (int highIndex = lowIndex + 1; highIndex < count; ++highIndex) {
        if (weightOf(lowIndex, highIndex) != weight)
          continue;
        const auto commas = wanted(lowIndex, highIndex);

        const auto [lowRoot, lowAt] = find(lowIndex);
        const auto [highRoot, highAt] = find(highIndex);

        if (lowRoot == highRoot)
          continue; // agreed, or contradicted, by stronger ones already

        // Hang the high key's group off the low key's.
        group[(size_t)highRoot] = lowRoot;
        offset[(size_t)highRoot] = lowAt + commas - highAt;
      }

  // ---- then as little out of tune as it can be ------------------------------
  //
  // Strongest first can leave out more than it has to. In C E G A D the
  // fourths E to A and A to D are taken before any third, and between them
  // they put E and A a comma from where C wants them, three thirds and sixths
  // given up to keep one fourth. So each key is then tried a comma either
  // way, alone or with one other, and kept there if that leaves less out of
  // tune, each interval weighed as above, until no move helps. It looks only at
  // the keys, so the same keys always come out the same.
  std::array<int, kPoolSize + 1> top{}, settled{};
  for (int i = 0; i < count; ++i)
    std::tie(top[(size_t)i], settled[(size_t)i]) = find(i);

  // Every pair's weight and the difference it wants, worked out once.
  std::array<std::array<int, kPoolSize + 1>, kPoolSize + 1> pairWeight{},
      pairWants{};
  for (int a = 0; a < count; ++a)
    for (int b = a + 1; b < count; ++b) {
      const auto weight = weightOf(a, b);
      pairWeight[(size_t)a][(size_t)b] =
          top[(size_t)a] == top[(size_t)b] && weight > 0 ? weight : 0;
      pairWants[(size_t)a][(size_t)b] = wanted(a, b);
    }

  // How far out of tune the pair a, b is, a below b, at the versions given.
  const auto strain = [&](int a, int b, int lowAt, int highAt) {
    return pairWeight[(size_t)a][(size_t)b] *
           std::abs(highAt - lowAt - pairWants[(size_t)a][(size_t)b]);
  };

  // What moving i, and j with it, by step does to the whole chord. The two
  // move together, so the pair between them is left as it is.
  const auto change = [&](int i, int j, int step) {
    int sum = 0;
    const int movers[2] = {i, j};
    for (int m = 0; m < (j == i ? 1 : 2); ++m) {
      const auto moving = movers[m];
      for (int k = 0; k < count; ++k) {
        if (k == i || k == j)
          continue;
        const auto low = std::min(moving, k), high = std::max(moving, k);
        const auto lowAt = settled[(size_t)low];
        const auto highAt = settled[(size_t)high];
        const auto lowStep = low == moving ? step : 0;
        const auto highStep = high == moving ? step : 0;
        sum += strain(low, high, lowAt + lowStep, highAt + highStep) -
               strain(low, high, lowAt, highAt);
      }
    }
    return sum;
  };

  // One key a comma either way, or two a consonance apart together, since a
  // third and the fifth above it can be out of tune with the rest only as a
  // pair.
  for (bool better = true; better;) {
    better = false;
    for (int i = 0; i < count; ++i)
      for (int j = i; j < count; ++j) {
        if (nodes[(size_t)i].harmonic || nodes[(size_t)j].harmonic ||
            (j != i && pairWeight[(size_t)i][(size_t)j] == 0))
          continue;
        for (const int step : {-1, 1})
          if (change(i, j, step) < 0) {
            settled[(size_t)i] += step;
            if (j != i)
              settled[(size_t)j] += step;
            better = true;
          }
      }
  }

  for (int i = 0; i < count; ++i) {
    group[(size_t)i] = top[(size_t)i];
    offset[(size_t)i] = settled[(size_t)i] - settled[(size_t)top[(size_t)i]];
  }

  // ---- where each group sits ------------------------------------------------
  std::array<int, kPoolSize + 1> chosen{};

  for (int g = 0; g < count; ++g) {
    if (find(g).first != g)
      continue;

    int lowest = 1 << 20, highest = -(1 << 20);
    for (int i = 0; i < count; ++i) {
      const auto [r, at] = find(i);
      if (r == g) {
        lowest = std::min(lowest, at);
        highest = std::max(highest, at);
      }
    }

    // Every placement that keeps the whole group inside its reach, or failing
    // that the one that overshoots least.
    int from = -kReach - lowest, to = kReach - highest;
    if (from > to)
      from = to = (from + to) / 2;

    // With Home on, how near the chord sits to home comes before everything
    // else, so a progression that would pump moves a held note a comma
    // instead of taking the keyboard with it. Off, it counts for nothing and
    // the chord stays wherever the music has taken it.
    double bestScore[4] = {1.0e18, 1.0e18, 1.0e18, 1.0e18};
    int bestPlace = from;

    for (int place = from; place <= to; ++place) {
      double moved = 0.0, ringing = 0.0, habit = 0.0;
      int fromHome = 0;

      for (int i = 0; i < count; ++i) {
        const auto [r, at] = find(i);
        if (r != g)
          continue;

        const auto version = std::clamp(place + at, -kReach, kReach);
        const auto &node = nodes[(size_t)i];
        fromHome += version;

        if (node.voice != nullptr)
          moved += (node.young ? 0.01 : 1.0) * std::abs(version - node.now);
        else
          habit += version == adaptiveVersion[(size_t)classOf(node.key)]
                       ? 0.0
                       : 1.0 + std::abs(version);

        // What is still ringing after its key came up only settles a choice
        // the held keys leave open.
        for (const auto &v : voices) {
          if (!v.isActive() || &v == self || !v.isReleasing() ||
              v.lastLevel() < kAudibleShare * loudest ||
              v.lastLevel() < kInaudible)
            continue;

          const auto span = std::abs(node.key - v.getNote());
          const auto &pure = kByClass[(size_t)(span % 12)];
          if (pure.weight == 0)
            continue;

          const auto high = node.key >= v.getNote();
          const auto cents =
              1200.0 *
              std::log2(high ? hzOf(node.key, version) / v.getFrequency()
                             : v.getFrequency() / hzOf(node.key, version));
          ringing += (double)pure.weight *
                     std::abs(cents - (100.0 * (double)span +
                                       kOffEqual[(size_t)(span % 12)]));
        }
      }

      const double home = adaptiveHome ? (double)std::abs(fromHome) : 0.0;
      const double score[4] = {home, moved, ringing, habit};
      if (std::lexicographical_compare(score, score + 4, bestScore,
                                       bestScore + 4)) {
        std::copy(score, score + 4, bestScore);
        bestPlace = place;
      }
    }

    for (int i = 0; i < count; ++i) {
      const auto [r, at] = find(i);
      if (r == g)
        chosen[(size_t)i] = std::clamp(bestPlace + at, -kReach, kReach);
    }
  }

  // ---- move what has to move ------------------------------------------------
  double result = 0.0;

  // What it all came to, for the screen, filled in as each key is placed.
  AdaptiveSnapshot outcome;
  outcome.count = count;
  outcome.referenceHz = referenceHz;

  // The root the harmonic seventh is tuned on, where the chain put it.
  int rootNode = -1;
  if (dominant >= 0)
    for (int i = 0; i < count; ++i)
      if (nodes[(size_t)i].key == lowestOf[(size_t)dominant])
        rootNode = i;

  for (int i = 0; i < count; ++i) {
    const auto &node = nodes[(size_t)i];

    if (node.harmonic && rootNode >= 0) {
      const auto &rootKey = nodes[(size_t)rootNode].key;
      const auto octaves = (node.key - rootKey - 10) / 12;
      const auto hz = hzOf(rootKey, chosen[(size_t)rootNode]) * 1.75 *
                      std::exp2((double)octaves);

      outcome.notes[(size_t)i] = {node.key, hz, 0, true};
      outcome.harmonicRoot = rootKey;

      if (node.voice == nullptr)
        result = hz;
      else if (std::abs(1200.0 * std::log2(node.voice->getFrequency() / hz)) >
               1.0e-3)
        node.voice->retuneTo(hz, node.young ? 0.0 : adaptiveRetune);
      continue;
    }

    const auto version = chosen[(size_t)i];
    adaptiveVersion[(size_t)classOf(node.key)] = version;
    outcome.notes[(size_t)i] = {node.key, hzOf(node.key, version), version,
                                false};

    if (node.voice == nullptr) {
      result = hzOf(node.key, version);
      continue;
    }

    // Compared as frequencies rather than versions, so a note leaving a
    // dominant seventh, which sat off the chain, is moved back onto it.
    const auto hz = hzOf(node.key, version);
    if (std::abs(1200.0 * std::log2(node.voice->getFrequency() / hz)) > 1.0e-3)
      node.voice->retuneTo(hz, node.young ? 0.0 : adaptiveRetune);
  }

  outcome.remembered = adaptiveVersion;
  adaptiveSnapshots.publish(outcome);
  return result;
}

double SynthEngine::restingFrequency(int note,
                                     const SynthParams &p) const noexcept {
  const auto t = p.global.temperament;
  const auto root = p.global.tuningRoot;
  const auto reference = p.global.referenceHz;

  if (t != Temperament::Adaptive)
    return noteFrequency(note, t, root, reference);

  const auto version = adaptiveVersion[(size_t)(((note % 12) + 12) % 12)];
  return noteFrequency(note, t, root, reference) *
         std::exp2((double)version * tuning::kAdaptiveComma / 1200.0);
}

SympatheticStrings::Plan
SynthEngine::planStrings(const SynthParams &p) const noexcept {
  SympatheticStrings::Plan plan;
  constexpr int kStrings = SympatheticStrings::kStrings;

  const auto set = [&plan](int key, double hz, bool free) {
    const auto s = SympatheticStrings::stringOf(key);
    // The class's constant rather than the local beside it, which MSVC would
    // need captured.
    if (s >= 0 && s < SympatheticStrings::kStrings) {
      plan.hz[(size_t)s] = hz;
      plan.free[(size_t)s] = free;
    }
  };

  switch (p.resonator.mode) {
  case ResonatorMode::Piano:
  case ResonatorMode::Open: {
    // Open is Piano with the sustain pedal always down.
    const bool pedal = p.pedalDown || p.resonator.mode == ResonatorMode::Open;

    // A held key's string is where that key is sounding, before any bend:
    // a piano's strings do not follow the wheel. The rest wait where a key
    // struck into silence would sound.
    std::array<double, kStrings> held{};
    for (const auto &v : voices) {
      const auto s = SympatheticStrings::stringOf(v.getNote());
      if (v.isActive() && !v.isReleasing() && s >= 0 && s < kStrings)
        held[(size_t)s] = v.getFrequency();
    }

    for (int s = 0; s < kStrings; ++s) {
      const auto key = SympatheticStrings::keyOf(s);
      const bool isHeld = held[(size_t)s] > 0.0;
      set(key, isHeld ? held[(size_t)s] : restingFrequency(key, p),
          isHeld || pedal);
      plan.pedalled[(size_t)s] = !isHeld && pedal;
    }
    break;
  }

  case ResonatorMode::Octave:
    for (int key = 48; key < 60; ++key)
      set(key, restingFrequency(key, p), true);
    break;

  case ResonatorMode::Series: {
    // The root's harmonics from its second octave, pure, each on the string
    // of the key nearest to it.
    const auto rootKey = 36 + ((p.global.tuningRoot % 12) + 12) % 12;
    const auto fundamental = restingFrequency(rootKey, p);

    for (int h = 1; h <= 8; ++h)
      set(rootKey + (int)std::lround(12.0 * std::log2((double)h)),
          fundamental * (double)h, true);
    break;
  }

  case ResonatorMode::Off:
  case ResonatorMode::NumModes:
    break;
  }

  return plan;
}

double SynthEngine::frequencyOfNote(int note) const noexcept {
  for (const auto &v : voices)
    if (v.isActive() && !v.isReleasing() && v.getNote() == note)
      return v.getFrequency();

  return 0.0;
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
                   frequencyFor(back, legatoTemperament, legatoRoot,
                                legatoReferenceHz, &v));
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

  // What is left held may sit better now that this key has gone: a note
  // tuned to fit a key that is no longer there can take the version the
  // chord now wants.
  if (adaptiveOn)
    settleAdaptive(-1, legatoRoot, legatoReferenceHz, nullptr);
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

  if (adaptiveOn)
    settleAdaptive(-1, legatoRoot, legatoReferenceHz, nullptr);
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

  sympathetic.reset();

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

  // Turned whatever the character, so switching to Wheel joins a motor that
  // was already running rather than starting one.
  out.motorTurns = motorTurns;
  out.motorStep = kWheelMotorHz / renderRate;
  motorTurns = wrapPhase(motorTurns + out.motorStep * (double)numFrames);

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
  std::array<bool, kPoolSize> rendered{};

  for (size_t index = 0; index < voices.size(); ++index) {
    auto &v = voices[index];

    if (!v.isActive())
      continue;

    v.render(left, right, numFrames, p, shared);
    rendered[index] = true;

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
      // is at zero. See Activity::claimed. The louder voice wins, and on a
      // tie the newer note. See Activity::ages.
      if (into.claimed[i] &&
          (peaks[i] < into.peaks[i] ||
           (exactly(peaks[i], into.peaks[i]) && v.getAge() <= into.ages[i])))
        continue;

      into.claimed[i] = true;
      into.ages[i] = v.getAge();
      into.peaks[i] = peaks[i];
      into.envelopes[i] = envelopes[i];
      into.tremolos[i] = tremolos[i];
      into.pitches[i] = pitches[i];
      into.velocities[i] = velocities[i];
      into.pressures[i] = pressures[i];
    }

    if (!into.noiseClaimed || v.getNoisePeak() > into.noisePeak ||
        (exactly(v.getNoisePeak(), into.noisePeak) &&
         v.getAge() > into.noiseAge)) {
      into.noiseClaimed = true;
      into.noiseAge = v.getAge();
      into.noisePeak = v.getNoisePeak();
      into.noiseEnvelope = v.getNoiseEnvelope();
      into.noiseTremolo = v.getNoiseTremolo();
      into.noiseVelocity = v.getNoiseVelocity();
      into.noisePressure = v.getNoisePressure();
    }
  }

  if (p.global.character == Character::Fm ||
      p.global.character == Character::Pd ||
      p.global.character == Character::Sid ||
      p.global.character == Character::Vco)
    renderHiss(left, right, numFrames, p, rendered);
}

void SynthEngine::renderHiss(
    float *left, float *right, int numFrames, const SynthParams &p,
    const std::array<bool, kPoolSize> &rendered) noexcept {
  // Each instrument's level per hertz below and above the split, and how
  // much of each is fixed. FM's, PD's and the VCO's are flat, the 6581's
  // falls.
  double lowDb = kFmNoiseDbPerHz + kFmByEar.noiseDb, highDb = lowDb;
  auto fixed = kFmFixedHiss;

  if (p.global.character == Character::Pd) {
    lowDb = highDb = kPdNoiseDbPerHz + kPdByEar.noiseDb;
    fixed = kPdFixedHiss;
  } else if (p.global.character == Character::Sid) {
    lowDb = kSidNoiseLowDbPerHz;
    highDb = kSidNoiseHighDbPerHz;
    fixed = kSidFixedHiss;
  } else if (p.global.character == Character::Vco) {
    lowDb = highDb = kVcoNoiseDbPerHz;
    fixed = kVcoFixedHiss;
  }

  // The level per hertz was measured against the fundamental of a mono
  // recording. A centred partial of amplitude a puts a / sqrt 2 in each
  // channel, so each channel's note power is the sum of a squared over four,
  // and white noise spread over half the rate has that much per hertz times
  // half the rate. A uniform draw between -1 and 1 has a variance of a third,
  // which the three makes up.
  const double lowPerPower =
      std::pow(10.0, lowDb / 10.0) * 0.25 * 0.5 * renderRate * 3.0;
  const double highPerPower =
      std::pow(10.0, highDb / 10.0) * 0.25 * 0.5 * renderRate * 3.0;

  // Two poles for the low band, and the rest of the noise is the high band,
  // so the two add back up to the noise itself when their gains are equal.
  // One pole would leave too much of the low band above the split, which
  // would make the hiss there grow faster than the instrument's does.
  const auto split =
      (float)(1.0 - std::exp(-6.283185307179586 * kHissSplitHz / renderRate));

  const int blocks =
      (numFrames + Voice::kControlBlock - 1) / Voice::kControlBlock;

  for (int b = 0; b < blocks && b < Voice::kMaxPowerBlocks; ++b) {
    const int start = b * Voice::kControlBlock;
    const int len = std::min(Voice::kControlBlock, numFrames - start);

    // Every note's power this block, added up and at its loudest.
    double sum = 0.0, loudest = 0.0;

    for (size_t v = 0; v < voices.size(); ++v) {
      if (!rendered[v])
        continue;

      const auto power = (double)voices[v].getBlockPowers()[(size_t)b];
      sum += power;
      loudest = std::max(loudest, power);
    }

    const auto lowTarget = (float)std::sqrt(
        lowPerPower * (fixed.low * loudest + (1.0 - fixed.low) * sum));
    const auto highTarget = (float)std::sqrt(
        highPerPower * (fixed.high * loudest + (1.0 - fixed.high) * sum));

    if (hissLowGain <= 0.0f && hissHighGain <= 0.0f && lowTarget <= 0.0f &&
        highTarget <= 0.0f)
      continue;

    // Both gains slide across the block, as a partial's does, so a note
    // coming in or going out moves the noise without a step.
    float low = hissLowGain, high = hissHighGain;
    const float lowInc = (lowTarget - low) / (float)len;
    const float highInc = (highTarget - high) / (float)len;

    for (int n = start; n < start + len; ++n) {
      const float white = hissRandom.bipolar();
      hissFirst += split * (white - hissFirst);
      hissLowState += split * (hissFirst - hissLowState);

      // One noise for both channels, since both synthesizers are mono.
      const float s = low * hissLowState + high * (white - hissLowState);

      left[n] += s;
      right[n] += s;

      low += lowInc;
      high += highInc;
    }

    hissLowGain = lowTarget;
    hissHighGain = highTarget;
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

  // ---- the resonator --------------------------------------------------------
  // On the summed channels and before the bus drive, so they ring inside the
  // instrument and the character drives them with everything else. Off, and
  // faded out, the stage is not there at all.
  if (p.resonator.mode != ResonatorMode::Off || sympathetic.isBusy())
    sympathetic.process(left, right, numSamples, p.resonator, planStrings(p));

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
