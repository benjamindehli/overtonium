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
  adaptiveVersion.fill(0);
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
  adaptiveOn = p.global.temperament == Temperament::Adaptive;
  adaptiveRetune = p.global.adaptiveRetuneSeconds;

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
  // pure size a chord is reaching for and are left out.
  struct Pure {
    int steps;
    int weight;
  };

  static constexpr Pure kByClass[12] = {
      {0, 4},  // unison, octave
      {0, 0},  // minor second
      {0, 0},  // major second
      {9, 2},  // minor third: a fifth up, a major third down
      {-8, 2}, // major third
      {-1, 3}, // fourth
      {0, 0},  // tritone
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

  if (count == 0)
    return 0.0;

  // ---- the harmonic seventh -------------------------------------------------
  //
  // A root's minor seventh is tuned 7:4 above it, the harmonic seventh, where
  // nothing in the chord is spoiled by that: so a dominant seventh is 4:5:6:7,
  // four harmonics of one note an octave below its root, and the seventh
  // agrees with the seventh harmonic every note here already carries. With the
  // major third held it is a dominant chord, ninths and a sharp ninth
  // included. Without one, a bare seventh or one over the fifth reads the same
  // way. A minor third keeps the 9:5 the chain gives it, since 7:4 would leave
  // the fifth from that third to the seventh 48 cents flat, and so does a
  // fourth above the root, as in a suspended seventh, whose fourth to the
  // seventh would be 30 cents off. It is the only pitch here off the chain of
  // fifths, which has no 7 in it, so it is left out of what the chain solves
  // and placed on the root afterwards. Where more than one root qualifies,
  // the one lowest on the keyboard is the chord.
  //
  // A note already sounding on the chain is not bent onto it, since that is a
  // slide of 48 cents on a held key, and it comes most often in passing, as G
  // minor turns into C and the held B flat is for a moment a seventh over C
  // and its fifth. A seventh struck after its root is 7:4 only over the major
  // third, as when G major becomes G7. Without the third it has to be struck
  // with its root, since as one chord turns into the next a seventh over a
  // root on its way out is common, and would be 7:4 only to bend 48 cents
  // back when the root lets go. One that is already 7:4 follows its root.
  bool held[12] = {};
  int lowestOf[12];
  for (auto &l : lowestOf)
    l = 1 << 20;
  for (int i = 0; i < count; ++i) {
    const auto pc = classOf(nodes[(size_t)i].key);
    held[(size_t)pc] = true;
    lowestOf[(size_t)pc] = std::min(lowestOf[(size_t)pc], nodes[(size_t)i].key);
  }

  int dominant = -1;
  for (int pc = 0; pc < 12; ++pc)
    if (held[(size_t)pc] && held[(size_t)((pc + 10) % 12)] &&
        (held[(size_t)((pc + 4) % 12)] ||
         (!held[(size_t)((pc + 3) % 12)] && !held[(size_t)((pc + 5) % 12)])) &&
        (dominant < 0 || lowestOf[(size_t)pc] < lowestOf[(size_t)dominant]))
      dominant = pc;

  const auto offChain = [&hzOf, &versionOf](const Voice &v) {
    return std::abs(1200.0 * std::log2(v.getFrequency() /
                                       hzOf(v.getNote(), versionOf(v)))) > 1.0;
  };

  if (dominant >= 0) {
    bool rootYoung = false;
    for (int i = 0; i < count; ++i)
      if (nodes[(size_t)i].key == lowestOf[(size_t)dominant])
        rootYoung = nodes[(size_t)i].young;
    const auto joins = held[(size_t)((dominant + 4) % 12)] || rootYoung;

    for (int i = 0; i < count; ++i) {
      auto &node = nodes[(size_t)i];
      node.harmonic = classOf(node.key) == (dominant + 10) % 12 &&
                      ((node.young && joins) ||
                       (node.voice != nullptr && offChain(*node.voice)));
    }
  }

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

  for (const int weight : {4, 3, 2, 1})
    for (int a = 0; a < count; ++a)
      for (int b = a + 1; b < count; ++b) {
        const auto &na = nodes[(size_t)a];
        const auto &nb = nodes[(size_t)b];
        if (na.harmonic || nb.harmonic)
          continue;
        const auto span = std::abs(na.key - nb.key);
        const auto &pure = kByClass[(size_t)(span % 12)];
        if (pure.weight != weight)
          continue;

        // In commas, how far above the lower key's version the higher key's
        // has to be for the pair to be pure.
        const auto &low = na.key <= nb.key ? na : nb;
        const auto lowIndex = na.key <= nb.key ? a : b;
        const auto highIndex = na.key <= nb.key ? b : a;
        const auto &high = na.key <= nb.key ? nb : na;
        const auto steps =
            pure.steps - (kMiddleStep[(size_t)classOf(high.key)] -
                          kMiddleStep[(size_t)classOf(low.key)]);
        const auto commas = steps / 12; // always whole: see kMiddleStep

        const auto [lowRoot, lowAt] = find(lowIndex);
        const auto [highRoot, highAt] = find(highIndex);

        if (lowRoot == highRoot)
          continue; // agreed, or contradicted, by stronger ones already

        // Hang the high key's group off the low key's.
        group[(size_t)highRoot] = lowRoot;
        offset[(size_t)highRoot] = lowAt + commas - highAt;
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

    double bestScore[3] = {1.0e18, 1.0e18, 1.0e18};
    int bestPlace = from;

    for (int place = from; place <= to; ++place) {
      double moved = 0.0, ringing = 0.0, habit = 0.0;

      for (int i = 0; i < count; ++i) {
        const auto [r, at] = find(i);
        if (r != g)
          continue;

        const auto version = std::clamp(place + at, -kReach, kReach);
        const auto &node = nodes[(size_t)i];

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

      const double score[3] = {moved, ringing, habit};
      if (std::lexicographical_compare(score, score + 3, bestScore,
                                       bestScore + 3)) {
        std::copy(score, score + 3, bestScore);
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

      if (node.voice == nullptr)
        result = hz;
      else if (std::abs(1200.0 * std::log2(node.voice->getFrequency() / hz)) >
               1.0e-3)
        node.voice->retuneTo(hz, node.young ? 0.0 : adaptiveRetune);
      continue;
    }

    const auto version = chosen[(size_t)i];
    adaptiveVersion[(size_t)classOf(node.key)] = version;

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

  return result;
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
