// Standalone tests for the JUCE-free DSP core.
//
//   c++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp Source/dsp/*.cpp -o
//   dsp_test && ./dsp_test

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/Drift.h"
#include "dsp/Envelope.h"
#include "dsp/Harmonics.h"
#include "dsp/Reverb.h"
#include "dsp/SineTable.h"
#include "dsp/SynthEngine.h"
#include "dsp/Temperament.h"
#include "dsp/Wobble.h"
#include "dsp/TapeEcho.h"
#include "dsp/Voice.h"

using namespace ovt;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
  }
}

void section(const char *name) { std::printf("\n== %s ==\n", name); }

/// Single-bin DFT magnitude with a Hann window.
///
/// The window matters: with a rectangular window the sidelobes of a strong
/// partial only fall off as 1/f, which smears a neighbouring partial across the
/// whole spectrum at around -48 dB and drowns out exactly the low-level alias
/// images we are looking for.
double binMagnitude(const std::vector<float> &x, double freq,
                    double sampleRate) {
  double re = 0.0, im = 0.0, norm = 0.0;
  const double w = 6.283185307179586 * freq / sampleRate;
  const double N = (double)x.size();

  for (size_t n = 0; n < x.size(); ++n) {
    const double win =
        0.5 * (1.0 - std::cos(6.283185307179586 * (double)n / N));

    re += win * x[n] * std::cos(w * (double)n);
    im -= win * x[n] * std::sin(w * (double)n);
    norm += win;
  }

  return std::sqrt(re * re + im * im) / norm;
}

/// Mean magnitude across a band.
///
/// A single bin of a noise signal is Rayleigh distributed, so one bin carries
/// almost no information and the ratio of two of them is wildly variable.
/// Averaging many bins is what makes a spectral claim about noise meaningful.
double bandMagnitude(const std::vector<float> &x, double lowHz, double highHz,
                     double sampleRate, int bins = 40) {
  double sum = 0.0;

  for (int k = 0; k < bins; ++k) {
    const double t = (double)k / (double)(bins - 1);
    sum += binMagnitude(x, lowHz + t * (highHz - lowHz), sampleRate);
  }

  return sum / (double)bins;
}

SynthParams makeFlatParams(float volumePerPartial) {
  SynthParams p;

  for (auto &o : p.osc) {
    o.tuneBlend = 0.0f;
    o.pmRateHz = 4.0f;
    o.pmDepthCents = 0.0f;
    o.attack = 0.001f;
    o.decay = 0.100f;
    o.sustain = 1.0f;
    o.release = 0.100f;
    o.amRateHz = 4.0f;
    o.amDepth = 0.0f;
    o.velAmount = 0.0f; // full level regardless of velocity
    o.volume = volumePerPartial;
    o.audible = true;
  }

  p.global.masterGain = 1.0f;
  p.global.bendSemitones = 0.0f;
  p.global.phaseReset = true;
  p.global.safetyClip = false;

  return p;
}

} // namespace

// -----------------------------------------------------------------------------
// 1. The tuning table the synth derives must match the reference overtone
// table.
// -----------------------------------------------------------------------------
void testTuningTable() {
  section("Tuning table vs reference");

  struct Row {
    int n, semis, cents;
    const char *interval;
  };

  static const Row reference[32] = {
      {1, 0, 0, "prime/octave"},
      {2, 12, 0, "prime/octave"},
      {3, 19, 2, "fifth"},
      {4, 24, 0, "prime/octave"},
      {5, 28, -14, "major third"},
      {6, 31, 2, "fifth"},
      {7, 34, -31, "minor seventh"},
      {8, 36, 0, "prime/octave"},
      {9, 38, 4, "major second"},
      {10, 40, -14, "major third"},
      {11, 42, -49, "tritone"},
      {12, 43, 2, "fifth"},
      {13, 44, 41, "minor sixth"},
      {14, 46, -31, "minor seventh"},
      {15, 47, -12, "major seventh"},
      {16, 48, 0, "prime/octave"},
      {17, 49, 5, "minor second"},
      {18, 50, 4, "major second"},
      {19, 51, -2, "minor third"},
      {20, 52, -14, "major third"},
      {21, 53, -29, "fourth"},
      {22, 54, -49, "tritone"},
      {23, 54, 28, "tritone"},
      {24, 55, 2, "fifth"},
      {25, 56, -27, "minor sixth"},
      {26, 56, 41, "minor sixth"},
      {27, 57, 6, "major sixth"},
      {28, 58, -31, "minor seventh"},
      {29, 58, 30, "minor seventh"},
      {30, 59, -12, "major seventh"},
      {31, 59, 45, "major seventh"},
      {32, 60, 0, "prime/octave"},
  };

  for (int i = 0; i < kNumHarmonics; ++i) {
    const auto &h = harmonic(i);
    const auto &r = reference[i];

    check(h.harmonic == r.n, "harmonic number " + std::to_string(i));
    check(h.etSemitones == r.semis, "semitones for n=" + std::to_string(r.n) +
                                        " got " +
                                        std::to_string(h.etSemitones) +
                                        " want " + std::to_string(r.semis));
    check((int)std::lround(h.jiCents) == r.cents,
          "cents for n=" + std::to_string(r.n) + " got " +
              std::to_string(std::lround(h.jiCents)) + " want " +
              std::to_string(r.cents));
    check(std::string(intervalName(h.pitchClass)) == r.interval,
          "interval for n=" + std::to_string(r.n) + " got " +
              intervalName(h.pitchClass) + " want " + r.interval);
  }
}

// -----------------------------------------------------------------------------
// 2. blend == 1 must land on an exact n:1 ratio, blend == 0 on an exact 12-TET
// semitone.
// -----------------------------------------------------------------------------
void testStretch() {
  section("Stretch");

  // Zero has to be exactly the harmonic series, or every preset ever saved
  // changes underneath it.
  bool neutral = true;
  for (int i = 0; i < kNumHarmonics; ++i) {
    neutral &= inharmonicCents(i, 0.0) == 0.0;
    neutral &= semitoneOffset(i, 1.0, 0.0) == semitoneOffset(i, 1.0);
  }

  check(neutral, "zero stretch is the plain harmonic series");

  // The dial is calibrated on the top partial, so it should read back exactly.
  bool calibrated = true;
  for (double cents : {-600.0, -100.0, 25.0, 150.0, 1200.0})
    calibrated &=
        std::abs(inharmonicCents(kNumHarmonics - 1, cents) - cents) < 1.0e-9;

  check(calibrated, "the dial reads the displacement of the top partial");

  // The fundamental is the fundamental whatever else moves.
  bool rooted = true;
  for (double cents : {-600.0, -100.0, 150.0, 1200.0})
    rooted &= std::abs(inharmonicCents(0, cents)) < 3.0;

  check(rooted, "and barely moves the fundamental (" +
                    std::to_string(inharmonicCents(0, 1200.0)) + " cents at "
                    "full stretch)");

  // Rising faster than linearly up the series is what makes it a stiff string
  // rather than a detune: each gap has to be wider than the one below it.
  bool accelerating = true;
  double previous = 0.0;

  for (int i = 1; i < kNumHarmonics; ++i) {
    const auto gap = inharmonicCents(i, 300.0) - inharmonicCents(i - 1, 300.0);
    accelerating &= gap > previous;
    previous = gap;
  }

  check(accelerating, "every partial is pushed further than the one below it");

  // Negative compresses instead, and stays a real frequency all the way down.
  bool compresses = true;
  for (int i = 0; i < kNumHarmonics; ++i)
    compresses &= inharmonicCents(i, -600.0) <= 0.0 &&
                  std::isfinite(inharmonicCents(i, -600.0));

  check(compresses, "and negative pulls them in without collapsing");

  // A quarter of a semitone on the octave above the fundamental is roughly
  // where a real piano sits, so partial 2 should still be nearly clean when
  // partial 32 is 150 cents out.
  std::printf("  at +150 ct on the top: partial 2 %+.2f, partial 8 %+.2f, "
              "partial 32 %+.2f cents\n",
              inharmonicCents(1, 150.0), inharmonicCents(7, 150.0),
              inharmonicCents(31, 150.0));

  check(inharmonicCents(1, 150.0) < 1.0,
        "piano-sized stretch leaves the low partials alone");
}

void testTracking() {
  section("Keyboard tracking");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;

  // Off has to be exactly off.
  bool neutral = true;
  for (double hz : {50.0, 500.0, 5000.0, 20000.0})
    neutral &= trackingGain(hz, 100.0, 0.0) == 1.0f;

  check(neutral, "zero dB per octave changes nothing");

  // Everything at or below the fundamental is left alone, whatever the slope.
  check(trackingGain(100.0, 100.0, 12.0) == 1.0f,
        "the fundamental is never touched");

  // A bass note lives below the rolloff, so most of its series survives.
  const auto bassTop = trackingGain(55.0 * 32.0, 55.0, 6.0);
  const auto trebleTop = trackingGain(880.0 * 32.0, 880.0, 6.0);

  std::printf("  at 6 dB/oct, partial 32 keeps %.3f at A1 and %.3f at A5\n",
              bassTop, trebleTop);

  check(bassTop > trebleTop,
        "the same partial survives better on a low note than a high one");

  check(bassTop > 0.4f, "a bass note keeps most of its series");
  check(trebleTop < 0.1f, "a treble note loses the top of its");

  // Rendered, which is the claim that matters: the same patch played two
  // octaves apart should thin out, and should not simply get quieter.
  const auto ratioAt = [&](int note, float slope) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(4);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.sustain = 1.0f;
      o.tuneBlend = 1.0f;
    }

    p.global.trackDbPerOctave = slope;
    p.osc[0].volume = 0.4f;
    p.osc[7].volume = 0.4f;

    engine.noteOn(note, 1.0f, p);

    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);

    const auto f0 = 440.0 * std::exp2((double)(note - 69) / 12.0);

    return std::make_pair(binMagnitude(l, f0, sr),
                          binMagnitude(l, f0 * 8.0, sr));
  };

  const auto lowOff = ratioAt(45, 0.0f);  // A2
  const auto highOff = ratioAt(81, 0.0f); // A5
  const auto lowOn = ratioAt(45, 8.0f);
  const auto highOn = ratioAt(81, 8.0f);

  const auto balance = [](std::pair<double, double> v) {
    return v.second / std::max(1.0e-9, v.first);
  };

  std::printf("  partial 8 against the fundamental: off %.3f at A2, %.3f at "
              "A5; on %.3f and %.3f\n",
              balance(lowOff), balance(highOff), balance(lowOn),
              balance(highOn));

  check(std::abs(balance(lowOff) - balance(highOff)) < 0.1,
        "with tracking off the spectrum is the same at both pitches");

  check(balance(highOn) < 0.4 * balance(lowOn),
        "with it on the high note is markedly thinner");

  // Duller, not quieter: the fundamental must survive the move.
  check(lowOn.first > 0.5 * lowOff.first && highOn.first > 0.5 * highOff.first,
        "and the fundamental is left where it was at both pitches");
}

/// Start phase, which exists because zero is the softest onset a partial can
/// have and the attack knob cannot do anything about it.
void testStartPhase() {
  section("Start phase");

  constexpr double sr = 48000.0;
  constexpr int N = 4800;

  // A low note, where the period rather than the envelope sets the onset: the
  // fundamental of A1 needs 4.5 ms to reach its peak from a zero crossing, and
  // the shortest attack available is 0.5 ms.
  const auto onsetOf = [&](float startPhase) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(1);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.attack = 0.0005f;
      o.sustain = 1.0f;
      o.decay = 1.0f;
      o.velAmount = 0.0f;
    }

    p.osc[0].volume = 0.8f;
    p.osc[0].startPhase = startPhase;
    p.global.phaseReset = true;
    p.global.safetyClip = false;

    engine.noteOn(33, 1.0f, p); // A1, 55 Hz

    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);

    // How much has arrived one millisecond in, against everything it reaches.
    double early = 0.0, whole = 0.0;
    for (int n = 0; n < N; ++n) {
      const auto s = std::abs((double)l[(size_t)n]);
      whole = std::max(whole, s);
      if (n < (int)(0.001 * sr))
        early = std::max(early, s);
    }

    return whole > 0.0 ? early / whole : 0.0;
  };

  const auto atZero = onsetOf(0.0f);
  const auto atPeak = onsetOf(0.25f);

  std::printf("  A1, 0.5 ms attack: %.0f%% of the level is there after 1 ms "
              "from a zero crossing, %.0f%% from the peak\n",
              100.0 * atZero, 100.0 * atPeak);

  // A millisecond at 55 Hz is 20 degrees of the cycle, and sin(20) is 0.34.
  // That is the whole problem in one number: the envelope finished half a
  // millisecond ago and a third of the sound has arrived.
  check(atZero > 0.28 && atZero < 0.40,
        "from a zero crossing a low note is a third started after a "
        "millisecond, whatever the attack says (" +
            std::to_string(atZero) + ")");

  check(atPeak > 0.9,
        "from the peak it is essentially all there (" +
            std::to_string(atPeak) + ")");

  // Zero has to stay the default and stay exactly what it always did.
  SynthParams fresh;
  check(fresh.osc[0].startPhase == 0.0f,
        "and zero is the default, so nothing already made changes");

  // With phase reset off the setting cannot do anything, since there is no
  // reset for it to aim.
  const auto freeRunning = [&](float startPhase) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(1);

    auto p = makeFlatParams(0.0f);
    p.osc[0].volume = 0.8f;
    p.osc[0].startPhase = startPhase;
    p.osc[0].sustain = 1.0f;
    p.global.phaseReset = false;
    p.global.safetyClip = false;

    engine.noteOn(69, 1.0f, p);

    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    return l;
  };

  const auto a = freeRunning(0.0f);
  const auto b = freeRunning(0.25f);

  bool identical = true;
  for (size_t n = 0; n < a.size(); ++n)
    identical &= std::abs(a[n] - b[n]) < 1.0e-6f;

  check(identical,
        "with phase reset off it has nothing to aim and does nothing");
}

/// The keyboard's own tuning, as opposed to the partials above each note.
///
/// The temperaments are derived from the circle of fifths rather than copied
/// from a table of cents, so what is checked here is the property that defines
/// each one, not the numbers that happen to fall out.
void testTemperaments() {
  section("Temperaments");

  // The two commas everything else is built from.
  check(std::abs(tuning::kPythagoreanComma - 23.460) < 0.001,
        "the Pythagorean comma comes out at 23.460 cents (" +
            std::to_string(tuning::kPythagoreanComma) + ")");

  check(std::abs(tuning::kSyntonicComma - 21.506) < 0.001,
        "the syntonic comma comes out at 21.506 cents (" +
            std::to_string(tuning::kSyntonicComma) + ")");

  const auto interval = [](Temperament t, int semitones) {
    const auto &o = temperamentOffsets(t);
    return 100.0 * semitones + o[(size_t)semitones] - o[0];
  };

  const auto pureThird = tuning::kPureMajorThird;

  // Quarter-comma meantone exists to make the major third pure. If that is not
  // exact, the quarter is not a quarter.
  check(std::abs(interval(Temperament::QuarterComma, 4) - pureThird) < 0.001,
        "quarter-comma meantone's major third is pure (" +
            std::to_string(interval(Temperament::QuarterComma, 4)) + ")");

  check(std::abs(interval(Temperament::Just, 4) - pureThird) < 0.001,
        "and so is just intonation's");

  // Pythagorean exists to make the fifth pure, at the third's expense.
  check(std::abs(interval(Temperament::Pythagorean, 7) - tuning::kPureFifth) <
            0.001,
        "Pythagorean's fifth is pure (" +
            std::to_string(interval(Temperament::Pythagorean, 7)) + ")");

  check(interval(Temperament::Pythagorean, 4) > 407.0,
        "and its major third is wide, which is the price (" +
            std::to_string(interval(Temperament::Pythagorean, 4)) + ")");

  // The two well temperaments are known by their thirds.
  check(std::abs(interval(Temperament::Werckmeister3, 4) - 390.225) < 0.01,
        "Werckmeister III's major third is 390.2 cents (" +
            std::to_string(interval(Temperament::Werckmeister3, 4)) + ")");

  check(std::abs(interval(Temperament::Young, 4) - 392.18) < 0.01,
        "Young's is 392.2 (" +
            std::to_string(interval(Temperament::Young, 4)) + ")");

  std::printf("  major third and fifth, in cents:\n");
  for (int t = 0; t < (int)Temperament::NumTemperaments; ++t) {
    const auto which = (Temperament)t;
    std::printf("    %-24s %7.2f %8.2f\n", temperamentName(which),
                interval(which, 4), interval(which, 7));
  }

  // Every circle has to close, or the octaves drift.
  for (int t = 0; t < (int)Temperament::NumTemperaments; ++t) {
    const auto &o = temperamentOffsets((Temperament)t);

    bool sane = true;
    for (auto cents : o)
      sane &= std::abs(cents) < 60.0;

    check(sane, std::string(temperamentName((Temperament)t)) +
                    " stays within a semitone of equal");
  }

  // A is where the reference says it is, whatever the temperament, because a
  // tuner tunes A first and works outwards from it.
  bool anchored = true;
  for (int t = 0; t < (int)Temperament::NumTemperaments; ++t)
    for (int root = 0; root < 12; ++root)
      anchored &= std::abs(noteFrequency(69, (Temperament)t, root, 440.0) -
                           440.0) < 1.0e-9;

  check(anchored, "A stays on the reference in every temperament and root");

  // Equal temperament has to be exactly what it always was.
  bool unchanged = true;
  for (int note = 0; note < 128; ++note) {
    const auto was = 440.0 * std::exp2((double)(note - 69) / 12.0);
    unchanged &= std::abs(noteFrequency(note, Temperament::Equal, 0, 440.0) -
                          was) < 1.0e-9;
  }

  check(unchanged, "and equal temperament is bit for bit what it was before");

  // The reference pitch moves everything together.
  check(std::abs(noteFrequency(69, Temperament::Equal, 0, 415.0) - 415.0) <
            1.0e-9,
        "the reference pitch sets A");

  const auto c4at415 = noteFrequency(60, Temperament::Equal, 0, 415.0);
  const auto c4at440 = noteFrequency(60, Temperament::Equal, 0, 440.0);

  check(std::abs(c4at415 / c4at440 - 415.0 / 440.0) < 1.0e-9,
        "and moves the rest of the keyboard with it");

  // The root rotates the pattern rather than reshaping it.
  const auto thirdFromRoot = [](int root) {
    const auto t = Temperament::QuarterComma;

    return 1200.0 * std::log2(noteFrequency(64 + root, t, root, 440.0) /
                              noteFrequency(60 + root, t, root, 440.0));
  };

  bool rotates = true;
  for (int root = 0; root < 12; ++root)
    rotates &= std::abs(thirdFromRoot(root) - pureThird) < 0.001;

  check(rotates, "meantone's pure third follows the root wherever it is put");
}

void testBlendEndpoints() {
  section("Tuning blend endpoints");

  for (int i = 0; i < kNumHarmonics; ++i) {
    const int n = i + 1;

    const double ji = std::exp2(semitoneOffset(i, 1.0) / 12.0);
    const double et = std::exp2(semitoneOffset(i, 0.0) / 12.0);
    const double etRef = std::exp2((double)harmonic(i).etSemitones / 12.0);

    check(std::abs(ji - (double)n) < 1.0e-12 * n,
          "JI ratio for n=" + std::to_string(n) + " = " + std::to_string(ji));
    check(std::abs(et - etRef) < 1.0e-12,
          "ET ratio for n=" + std::to_string(n));
  }

  // Halfway must sit halfway in cents, not halfway in Hz.
  const int i7 = 6; // 7th harmonic, -31.17 cents
  const double half = semitoneOffset(i7, 0.5);
  check(std::abs(half - (34.0 + 0.5 * harmonic(i7).jiCents * 0.01)) < 1.0e-12,
        "50% blend is linear in cents");
}

// -----------------------------------------------------------------------------
// 3. Sine lookup accuracy.
// -----------------------------------------------------------------------------
void testSineTable() {
  section("Sine table");

  const auto &sine = SineTable::instance();

  double worst = 0.0;
  for (int k = 0; k < 100000; ++k) {
    const double ph = (double)k / 100000.0;
    const double ref = std::sin(6.283185307179586 * ph);
    worst = std::max(worst, std::abs(ref - (double)sine(ph)));
  }

  std::printf("  peak sine error: %.3e (%.1f dB)\n", worst,
              20.0 * std::log10(worst));
  check(worst < 1.0e-4, "sine table error below -80 dB");

  check(std::abs((double)sine.cosine(0.0) - 1.0) < 1.0e-5, "cosine(0) == 1");
  check(std::abs((double)sine(0.25) - 1.0) < 1.0e-5, "sine(0.25) == 1");

  // Out-of-range phases must wrap rather than read out of bounds.
  check(std::abs((double)sine(1.25) - (double)sine(0.25)) < 1.0e-6,
        "phase wraps above 1");
  check(std::abs((double)wrapPhase(-0.25) - 0.75) < 1.0e-12,
        "wrapPhase handles negatives");
}

// -----------------------------------------------------------------------------
// 4. A rendered note must be finite, audible and correctly tuned.
// -----------------------------------------------------------------------------
void testRenderedSpectrum() {
  section("Rendered spectrum");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(8);

  auto p = makeFlatParams(0.02f);
  for (auto &o : p.osc)
    o.tuneBlend =
        1.0f; // pure just intonation: partial n sits exactly at n * f0

  engine.noteOn(57, 1.0f, p); // A3 = 220 Hz
  std::vector<float> l((size_t)N), r((size_t)N);
  engine.render(l.data(), r.data(), N, p);

  bool finite = true;
  float peak = 0.0f;
  for (int n = 0; n < N; ++n) {
    finite &= std::isfinite(l[(size_t)n]) && std::isfinite(r[(size_t)n]);
    peak = std::max(peak, std::abs(l[(size_t)n]));
  }

  check(finite, "output is finite");
  check(peak > 0.05f, "output is audible (peak " + std::to_string(peak) + ")");

  const double f0 = 220.0;
  const double m1 = binMagnitude(l, f0, sr);

  // Every partial up to the Nyquist guard should be present at a comparable
  // level.
  for (int n = 1; n <= 16; ++n) {
    const double m = binMagnitude(l, f0 * n, sr);
    check(m > 0.3 * m1, "partial " + std::to_string(n) + " present (" +
                            std::to_string(m / m1) + " of f0)");
  }

  // ...and nothing meaningful should sit between them.
  const double between = binMagnitude(l, f0 * 3.5, sr);
  check(between < 0.01 * m1, "no energy between partials");
}

// -----------------------------------------------------------------------------
// 5. Aliasing: partials pushed past Nyquist must not fold back down.
// -----------------------------------------------------------------------------
void testAliasing() {
  section("Nyquist guard / aliasing");

  check(nyquistGain(1000.0, 44100.0) == 1.0f, "low partial passes");
  check(nyquistGain(22000.0, 44100.0) == 0.0f,
        "partial above Nyquist silenced");
  check(nyquistGain(44100.0 * 0.455, 44100.0) > 0.0f &&
            nyquistGain(44100.0 * 0.455, 44100.0) < 1.0f,
        "fade region is gradual");

  constexpr double sr = 44100.0;
  constexpr int N = 22050;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.02f);
  for (auto &o : p.osc)
    o.tuneBlend = 1.0f;

  engine.noteOn(96, 1.0f,
                p); // C7 = 2093 Hz; the 32nd partial wants to sit at 67 kHz
  std::vector<float> l((size_t)N), r((size_t)N);
  engine.render(l.data(), r.data(), N, p);

  const double f0 = 440.0 * std::exp2((96 - 69) / 12.0);
  const double m1 = binMagnitude(l, f0, sr);

  double worstAlias = 0.0;
  int worstN = 0;

  for (int n = 11; n <= 32; ++n) {
    // Where partial n would fold back to if it were not muted.
    double f = f0 * n;
    while (f > sr * 0.5)
      f = std::abs(sr - std::fmod(f, sr));

    if (f < 20.0)
      continue;

    const double m = binMagnitude(l, f, sr);
    if (m > worstAlias) {
      worstAlias = m;
      worstN = n;
    }
  }

  const double dB = 20.0 * std::log10(worstAlias / m1);
  std::printf("  worst alias image: partial %d at %.1f dB relative to f0\n",
              worstN, dB);
  check(dB < -70.0, "alias images below -70 dB");
}

// -----------------------------------------------------------------------------
// 6. Envelopes, mute and solo.
// -----------------------------------------------------------------------------
void testEnvelopeAndMuteSolo() {
  section("Envelope, mute, solo");

  Envelope env;
  env.setSampleRate(1000.0);
  env.configure(0.0f, 0.1f, 0.1f, 0.5f, 0.0f, 0.0f, 0.1f);
  env.noteOn(true);

  for (int i = 0; i < 100; ++i)
    env.tick();

  check(std::abs(env.getLevel() - 1.0f) < 1.0e-3f, "attack reaches full level");

  for (int i = 0; i < 200; ++i)
    env.tick();

  // "Decay time" means 99% of the distance covered, so two decay times in we
  // are close but the stage does not formally latch to sustain until the
  // remainder is negligible.
  check(std::abs(env.getLevel() - 0.5f) < 1.0e-2f, "decays to sustain");

  for (int i = 0; i < 300; ++i)
    env.tick();

  check(env.getStage() == Envelope::Stage::Sustain, "settles in sustain");

  env.noteOff();
  for (int i = 0; i < 500; ++i)
    env.tick();

  check(!env.isActive(), "release completes and voice frees");

  // A zero-sustain envelope must free itself without a note-off.
  Envelope perc;
  perc.setSampleRate(1000.0);
  perc.configure(0.0f, 0.001f, 0.05f, 0.0f, 0.0f, 0.0f, 0.1f);
  perc.noteOn(true);
  for (int i = 0; i < 1000; ++i)
    perc.tick();

  check(!perc.isActive(), "zero-sustain envelope frees itself");

  // --- delay before the attack
  // ------------------------------------------------
  {
    Envelope d;
    d.setSampleRate(1000.0);
    d.configure(0.2f, 0.05f, 0.1f, 1.0f, 0.0f, 0.0f, 0.1f); // 200 ms delay
    d.noteOn(true);

    check(d.isActive(), "a delayed envelope counts as active straight away");

    for (int i = 0; i < 190; ++i)
      d.tick();
    check(d.getLevel() < 1.0e-6f, "nothing sounds during the delay");
    check(d.getStage() == Envelope::Stage::Delay, "still waiting");

    for (int i = 0; i < 20; ++i)
      d.tick();
    check(d.getStage() != Envelope::Stage::Delay, "the delay ends on time");
    check(d.getLevel() > 0.0f, "the attack starts once the delay is up");

    for (int i = 0; i < 200; ++i)
      d.tick();
    check(std::abs(d.getLevel() - 1.0f) < 1.0e-3f, "it still reaches full");

    // Letting go before the delay elapses has to cancel the note outright.
    Envelope cancelled;
    cancelled.setSampleRate(1000.0);
    cancelled.configure(0.5f, 0.05f, 0.1f, 1.0f, 0.0f, 0.0f, 0.1f);
    cancelled.noteOn(true);
    for (int i = 0; i < 50; ++i)
      cancelled.tick();
    cancelled.noteOff();
    for (int i = 0; i < 50; ++i)
      cancelled.tick();

    check(!cancelled.isActive(),
          "releasing during the delay cancels the note without a burst");

    // Retiming the knob must not reach back into a note already waiting.
    Envelope latched;
    latched.setSampleRate(1000.0);
    latched.configure(0.2f, 0.05f, 0.1f, 1.0f, 0.0f, 0.0f, 0.1f);
    latched.noteOn(true);
    latched.configure(2.0f, 0.05f, 0.1f, 1.0f, 0.0f, 0.0f,
                      0.1f); // moved mid-note
    for (int i = 0; i < 260; ++i)
      latched.tick();

    check(latched.getLevel() > 0.0f,
          "the delay is latched at note-on, not re-read while waiting");
  }

  // --- mute / solo
  // ------------------------------------------------------------------
  constexpr double sr = 48000.0;
  constexpr int N = 4800;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.05f);
  for (auto &o : p.osc)
    o.tuneBlend = 1.0f;

  // Only partial 3 audible, as if it were soloed.
  for (int i = 0; i < kNumHarmonics; ++i)
    p.osc[(size_t)i].audible = (i == 2);

  engine.noteOn(57, 1.0f, p);
  std::vector<float> l((size_t)N), r((size_t)N);
  engine.render(l.data(), r.data(), N, p);

  const double f0 = 220.0;
  check(binMagnitude(l, f0 * 3.0, sr) > 0.005, "soloed partial sounds");
  check(binMagnitude(l, f0 * 1.0, sr) < 1.0e-4, "fundamental silenced by solo");
  check(binMagnitude(l, f0 * 5.0, sr) < 1.0e-4,
        "other partials silenced by solo");
}

// -----------------------------------------------------------------------------
// 7. Muting mid-note must ramp, not step.
// -----------------------------------------------------------------------------
void testNoClickOnMute() {
  section("Click-free parameter changes");

  constexpr double sr = 48000.0;
  constexpr int N = 2048;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.03f);
  engine.noteOn(45, 1.0f,
                p); // A2, low enough that a legitimate slope stays small

  std::vector<float> a((size_t)N), b((size_t)N);
  engine.render(a.data(), b.data(), N, p); // settle
  engine.render(a.data(), b.data(), N, p);

  float steadyDelta = 0.0f;
  for (int n = 1; n < N; ++n)
    steadyDelta =
        std::max(steadyDelta, std::abs(a[(size_t)n] - a[(size_t)n - 1]));

  for (auto &o : p.osc)
    o.audible = false; // hard mute of all 32 strips at once

  std::vector<float> c((size_t)N), d((size_t)N);
  engine.render(c.data(), d.data(), N, p);

  float muteDelta = 0.0f;
  for (int n = 1; n < N; ++n)
    muteDelta = std::max(muteDelta, std::abs(c[(size_t)n] - c[(size_t)n - 1]));

  std::printf("  steady max delta %.5f, mute max delta %.5f\n", steadyDelta,
              muteDelta);
  check(muteDelta <= steadyDelta * 1.5f + 1.0e-4f,
        "muting does not introduce a step discontinuity");

  float tail = 0.0f;
  for (int n = N - 64; n < N; ++n)
    tail = std::max(tail, std::abs(c[(size_t)n]));

  check(tail < 1.0e-6f, "fully muted output reaches silence");
}

// -----------------------------------------------------------------------------
// 8. Voice allocation and stealing.
// -----------------------------------------------------------------------------
/// One key is one voice. Tapping a key with a long release used to leave every
/// tap ringing and sum them, which no physical instrument does.
void testOneVoicePerKey() {
  section("One voice per key");

  constexpr double sr = 48000.0;

  auto p = makeFlatParams(0.0f);
  p.osc[0].volume = 0.6f;
  p.osc[0].attack = 0.002f;
  p.osc[0].decay = 8.0f;
  p.osc[0].sustain = 1.0f;
  p.osc[0].release = 8.0f; // a tail that is still loud a long time later
  p.osc[0].velAmount = 0.0f;
  p.global.masterGain = 1.0f;
  p.global.safetyClip = false;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(8);

  const auto render = [&](double seconds, std::vector<float> &l) {
    const auto n = (size_t)(seconds * sr);
    l.assign(n, 0.0f);
    std::vector<float> r(n, 0.0f);
    engine.render(l.data(), r.data(), (int)n, p);
  };

  const auto peakOf = [](const std::vector<float> &v) {
    double m = 0.0;
    for (auto x : v)
      m = std::max(m, std::abs((double)x));
    return m;
  };

  std::vector<float> buf;

  // One tap, left to ring.
  engine.noteOn(60, 1.0f, p);
  render(0.2, buf);
  engine.noteOff(60);
  render(0.2, buf);

  const auto single = peakOf(buf);
  check(single > 0.05, "a tap rings on after the key is up (" +
                           std::to_string(single) + ")");

  // Tap the same key four more times while the first is still ringing.
  for (int i = 0; i < 4; ++i) {
    engine.noteOn(60, 1.0f, p);
    render(0.05, buf);
    engine.noteOff(60);
    render(0.05, buf);
  }

  render(0.05, buf);
  const auto stacked = peakOf(buf);

  std::printf("  one tap peaks at %.3f, five overlapping taps at %.3f\n",
              single, stacked);

  // Two coherent copies of one note is exactly twice the amplitude, so
  // anything approaching that is a stack rather than a retrigger.
  check(stacked < 1.4 * single,
        "repeated taps do not pile up on top of each other (" +
            std::to_string(stacked / single) + " times one tap)");

  check(engine.getActiveVoiceCount() == 1,
        "and only one voice is left holding the key (" +
            std::to_string(engine.getActiveVoiceCount()) + ")");

  // The cut has to be quick but not a click: no step bigger than the signal.
  engine.noteOn(60, 1.0f, p);
  std::vector<float> across;
  render(0.03, across);

  double biggestStep = 0.0;
  for (size_t n = 1; n < across.size(); ++n)
    biggestStep =
        std::max(biggestStep, std::abs((double)across[n] - across[n - 1]));

  const auto perSample = 2.0 * 3.14159265 * 261.6 / sr * peakOf(across);

  std::printf("  retrigger: biggest sample step %.5f against %.5f for the "
              "waveform itself\n",
              biggestStep, perSample);

  check(biggestStep < 3.0 * perSample,
        "and the cut does not put a step in the output");

  // Different keys still stack, which is the whole point of polyphony.
  SynthEngine chord;
  chord.prepare(sr);
  chord.setPolyphony(8);

  for (int note : {60, 64, 67})
    chord.noteOn(note, 1.0f, p);

  std::vector<float> cl((size_t)(0.1 * sr)), cr((size_t)(0.1 * sr));
  chord.render(cl.data(), cr.data(), (int)cl.size(), p);

  check(chord.getActiveVoiceCount() == 3,
        "three different keys are still three voices");

  // ---- and it stops tails eating the pool --------------------------------
  //
  // The stacking was not only loud, it was expensive. Every tap held a voice
  // for the whole of its release, so a repeatedly tapped key with a long tail
  // could fill the pool on its own. Past that the allocator has nothing free
  // and takes the oldest voice outright, with no fade and no regard for
  // whether a key is still down on it.
  SynthEngine pool;
  pool.prepare(sr);
  pool.setPolyphony(8);

  for (int note : {48, 52, 55, 59})
    pool.noteOn(note, 1.0f, p);

  std::vector<float> pl((size_t)(0.02 * sr)), pr((size_t)(0.02 * sr));
  pool.render(pl.data(), pr.data(), (int)pl.size(), p);

  const auto heldBefore = pool.getActiveVoiceCount();

  for (int i = 0; i < 25; ++i) {
    pool.noteOn(72, 1.0f, p);
    pool.render(pl.data(), pr.data(), (int)pl.size(), p);
    pool.noteOff(72);
    pool.render(pl.data(), pr.data(), (int)pl.size(), p);
  }

  pool.render(pl.data(), pr.data(), (int)pl.size(), p);

  std::printf("  four keys held, one tapped 25 times: %d voices before, %d "
              "after (pool holds %d)\n",
              heldBefore, pool.getActiveVoiceCount(), SynthEngine::kPoolSize);

  check(heldBefore == 4, "four held keys are four voices");

  // Four still held plus at most the one tail from the last tap.
  check(pool.getActiveVoiceCount() <= 5,
        "and tapping a fifth key 25 times does not fill the pool (" +
            std::to_string(pool.getActiveVoiceCount()) + ")");
}

/// What the lamps between the knob groups are told.
///
/// They are fed from the voice pool rather than from the parameters, so they
/// show what is happening to a note rather than what the knobs are set to. The
/// difference matters most at the ends: a lamp driven from the knobs would go
/// on pulsing over silence, and would say nothing at all about which half of
/// the envelope is running.
void testActivity() {
  section("Strip activity");

  constexpr double sr = 48000.0;

  auto p = makeFlatParams(0.0f);
  p.osc[0].volume = 0.5f;
  p.osc[0].attack = 0.005f;
  p.osc[0].decay = 8.0f;
  p.osc[0].sustain = 1.0f;
  p.osc[0].swell = 0.005f;
  p.osc[0].offLevel = 0.0f;
  p.osc[0].release = 0.4f;
  p.noise.release = 0.02f;
  p.global.masterGain = 1.0f;
  p.global.safetyClip = false;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(8);

  std::vector<float> l, r;

  const auto render = [&](double seconds) {
    const auto n = (size_t)(seconds * sr);
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    engine.render(l.data(), r.data(), (int)n, p);
  };

  // ---- silence says nothing -------------------------------------------------
  render(0.05);

  check(engine.getPartialEnvelope(0) == 0.0f,
        "with nothing sounding the envelope lamp is dark");
  check(engine.getPartialTremolo(0) == 0.0f, "and so is the tremolo lamp");
  check(engine.getPartialPitch(0) == 0.0f, "and the needle has nothing to say");

  // ---- the two envelope lamps hand over at the key ------------------------
  engine.noteOn(60, 1.0f, p);
  render(0.2);

  const auto held = engine.getPartialEnvelope(0);
  std::printf("  key down: envelope reads %+.3f\n", held);

  check(held > 0.5f,
        "a held note lights the envelope lamp (" + std::to_string(held) + ")");

  engine.noteOff(60);
  render(0.05);

  const auto letGo = engine.getPartialEnvelope(0);
  std::printf("  key up:   envelope reads %+.3f\n", letGo);

  check(letGo < 0.0f,
        "letting go flips the sign, which is the key-off lamp taking over (" +
            std::to_string(letGo) + ")");

  check(std::abs(letGo) > 0.1f,
        "and the level carries over rather than restarting");

  // In blocks, the way a host calls it. One long call would end with whatever
  // the envelope was doing on its last sample still published, which is not
  // wrong but is not what a lamp sitting on screen afterwards would show.
  for (int i = 0; i < 8; ++i)
    render(0.25);

  check(engine.getPartialEnvelope(0) == 0.0f,
        "once the release is done both lamps are dark again (" +
            std::to_string(engine.getPartialEnvelope(0)) + ")");

  // ---- the tremolo lamp shows the excursion, not the level ----------------
  //
  // Zero depth has to read zero rather than full, or every strip with no
  // tremolo on it would sit there lit and never move.
  {
    engine.allSoundOff();
    p.osc[0].amDepth = 0.0f;
    p.osc[0].amRateHz = 5.0f;

    engine.noteOn(60, 1.0f, p);
    render(0.3);

    check(engine.getPartialTremolo(0) == 0.0f,
          "a partial with no tremolo on it reads zero (" +
              std::to_string(engine.getPartialTremolo(0)) + ")");

    engine.allSoundOff();
    p.osc[0].amDepth = 1.0f;
    engine.noteOn(60, 1.0f, p);

    // Sample across a cycle and check it uses the range rather than sitting
    // at one value.
    float lowest = 2.0f, highest = -1.0f;
    for (int i = 0; i < 40; ++i) {
      render(0.005);
      const auto t = engine.getPartialTremolo(0);
      lowest = std::min(lowest, t);
      highest = std::max(highest, t);
    }

    std::printf("  at full depth the tremolo lamp swings %.2f to %.2f\n",
                lowest, highest);

    check(lowest < 0.15f && highest > 0.85f,
          "at full depth it swings across the whole range");

    p.osc[0].amDepth = 0.0f;
  }

  // ---- the needle follows the modulation ----------------------------------
  {
    engine.allSoundOff();
    p.osc[0].pmDepthCents = 0.0f;
    p.osc[0].driftCents = 0.0f;
    p.osc[0].pmRateHz = 6.0f;

    engine.noteOn(60, 1.0f, p);
    render(0.3);

    check(engine.getPartialPitch(0) == 0.0f,
          "a partial with nothing modulating it parks at zero");

    engine.allSoundOff();
    p.osc[0].pmDepthCents = 50.0f;
    engine.noteOn(60, 1.0f, p);

    float lowest = 1000.0f, highest = -1000.0f;
    for (int i = 0; i < 40; ++i) {
      render(0.005);
      const auto c = engine.getPartialPitch(0);
      lowest = std::min(lowest, c);
      highest = std::max(highest, c);
    }

    std::printf("  at 50 cents the needle covers %+.1f to %+.1f cents\n",
                lowest, highest);

    check(lowest < -40.0f && highest > 40.0f,
          "and one set to 50 cents swings both ways by nearly that much");

    // Never past what the strip is set to do, or the needle would peg.
    check(lowest >= -50.5f && highest <= 50.5f,
          "without ever exceeding the depth the knob asks for");
  }

  // ---- the lamps follow the loudest voice, like the meter ------------------
  //
  // Two notes, one much louder. The lamp should describe the one you can
  // actually hear rather than the maximum of both, which would describe
  // neither.
  {
    engine.allSoundOff();
    p.osc[0].pmDepthCents = 0.0f;
    p.osc[0].attack = 0.005f;

    engine.noteOn(60, 1.0f, p);
    render(0.3); // this one is up at sustain

    engine.noteOn(67, 0.05f, p); // and this one has barely started
    render(0.002);

    const auto reading = engine.getPartialEnvelope(0);
    std::printf("  a loud held note beside a quiet new one reads %+.3f\n",
                reading);

    check(reading > 0.5f,
          "the lamp follows the voice the meter follows, not the newest one");
  }
}

/// Notes that own a channel each, which is what an MPE controller sends.
///
/// The rule for an ordinary keyboard is one voice per key. That rule is wrong
/// for a controller giving every finger its own channel, where the same key
/// being down twice on two channels is two notes bent in two directions rather
/// than one note retriggering itself. Both rules have to hold at once, because
/// both kinds of controller can be playing.
void testPerNoteChannels() {
  section("Per-note channels");

  constexpr double sr = 48000.0;

  auto p = makeFlatParams(0.0f);
  p.osc[0].volume = 0.5f;
  p.osc[0].tuneBlend = 0.0f; // the fundamental alone, so pitch is measurable
  p.osc[0].attack = 0.002f;
  p.osc[0].decay = 8.0f;
  p.osc[0].sustain = 1.0f;
  p.osc[0].release = 0.05f;
  // The noise channel is silent here but its envelope still runs, and its
  // default release is long enough to hold a voice open for a second after the
  // key is up. Shortened so the checks below do not have to sit through it.
  p.noise.release = 0.02f;
  p.global.masterGain = 1.0f;
  p.global.safetyClip = false;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(8);

  std::vector<float> l, r;

  const auto render = [&](double seconds) {
    const auto n = (size_t)(seconds * sr);
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    engine.render(l.data(), r.data(), (int)n, p);
  };

  // Pitch from interpolated zero crossings. Whole crossings only resolve about
  // ten cents, which is not enough to tell a bend of a semitone from one of a
  // semitone and a bit.
  const auto measureHz = [&]() {
    std::vector<double> crossings;
    for (size_t i = 1; i < l.size(); ++i)
      if (l[i - 1] <= 0.0f && l[i] > 0.0f) {
        const auto frac = -l[i - 1] / (l[i] - l[i - 1]);
        crossings.push_back(((double)(i - 1) + frac) / sr);
      }

    if (crossings.size() < 2)
      return 0.0;

    return (double)(crossings.size() - 1) /
           (crossings.back() - crossings.front());
  };

  const auto peak = [&]() {
    double m = 0.0;
    for (auto x : l)
      m = std::max(m, std::abs((double)x));
    return m;
  };

  // ---- the same key on two channels is two notes ---------------------------
  {
    engine.noteOnPerNote(2, 60, 1.0f, p);
    render(0.05);
    const auto one = engine.getActiveVoiceCount();

    engine.noteOnPerNote(3, 60, 1.0f, p);
    render(0.05);
    const auto two = engine.getActiveVoiceCount();

    check(one == 1, "one channel holding a key is one voice (" +
                        std::to_string(one) + ")");
    check(two == 2, "the same key on a second channel is a second voice (" +
                        std::to_string(two) + ")");

    // ...and the same key twice on one channel is still one voice, because
    // within a channel the ordinary rule has not gone anywhere.
    engine.noteOnPerNote(3, 60, 1.0f, p);
    render(0.05);

    check(engine.getActiveVoiceCount() == 2,
          "but retriggering it on the same channel is not a third (" +
              std::to_string(engine.getActiveVoiceCount()) + ")");

    engine.noteOffPerNote(2, 60);
    engine.noteOffPerNote(3, 60);
    render(0.5);
  }

  // ---- an ordinary note and a per-note note do not find each other ---------
  //
  // The case that breaks if the channel is not part of the identity: a note
  // arriving on the master channel would take over the per-note voice, or its
  // key-up would silence it.
  {
    engine.allSoundOff();

    engine.noteOnPerNote(2, 60, 1.0f, p);
    engine.noteOn(60, 1.0f, p);
    render(0.05);

    check(engine.getActiveVoiceCount() == 2,
          "an ordinary note on the same key is its own voice (" +
              std::to_string(engine.getActiveVoiceCount()) + ")");

    // The ordinary key comes up. The per-note one is still held.
    engine.noteOff(60);
    render(0.4);

    check(engine.getActiveVoiceCount() == 1,
          "letting the ordinary key up leaves the per-note voice sounding (" +
              std::to_string(engine.getActiveVoiceCount()) + ")");

    check(peak() > 0.05, "and it is still making sound (" +
                             std::to_string(peak()) + ")");

    engine.allSoundOff();
  }

  // ---- bend belongs to its own note ----------------------------------------
  {
    const auto a4 = 440.0;

    engine.allSoundOff();
    engine.noteOnPerNote(2, 69, 1.0f, p); // A4
    render(0.3);

    const auto plain = measureHz();

    // Addressed to a channel that is not holding it. Nothing should move.
    engine.setNoteBend(3, 69, 2.0f);
    render(0.3);
    const auto elsewhere = measureHz();

    // Addressed to the channel that is.
    engine.setNoteBend(2, 69, 2.0f);
    render(0.3);
    const auto bent = measureHz();

    std::printf("  A4 renders at %.2f Hz, %.2f after a bend sent to another "
                "channel, %.2f after one sent to its own\n",
                plain, elsewhere, bent);

    check(std::abs(plain - a4) < 1.0, "an unbent note renders at its pitch");
    check(std::abs(elsewhere - a4) < 1.0,
          "a bend on another channel leaves it alone");

    const auto wanted = a4 * std::exp2(2.0 / 12.0);
    check(std::abs(bent - wanted) < 2.0,
          "and a bend on its own channel moves it two semitones (" +
              std::to_string(bent) + " against " + std::to_string(wanted) +
              ")");

    // The wheel still works, and the two add rather than one winning.
    p.global.bendSemitones = -2.0f;
    render(0.3);
    const auto cancelled = measureHz();

    std::printf("  a wheel of -2 against a note bend of +2 gives %.2f Hz\n",
                cancelled);

    check(std::abs(cancelled - a4) < 1.0,
          "the wheel adds to the note's own bend rather than replacing it");

    p.global.bendSemitones = 0.0f;

    // A new note on a channel starts unbent, or the last note's bend would be
    // inherited by whatever lands on that channel next.
    engine.noteOffPerNote(2, 69);
    render(0.3);
    engine.noteOnPerNote(2, 69, 1.0f, p);
    render(0.3);

    const auto fresh = measureHz();
    check(std::abs(fresh - a4) < 1.0,
          "and a new note on that channel starts unbent (" +
              std::to_string(fresh) + ")");

    engine.allSoundOff();
  }

  // ---- pressure belongs to its own note ------------------------------------
  //
  // Measured through a partial that is silent until pressed, so any level at
  // all is the pressure arriving.
  {
    auto pressed = makeFlatParams(0.0f);
    pressed.osc[0].volume = 0.0f;
    pressed.osc[0].atAmount = 1.0f;
    pressed.osc[0].attack = 0.002f;
    pressed.osc[0].decay = 8.0f;
    pressed.osc[0].sustain = 1.0f;
    pressed.global.masterGain = 1.0f;
    pressed.global.safetyClip = false;

    SynthEngine e2;
    e2.prepare(sr);
    e2.setPolyphony(8);

    const auto renderInto = [&](double seconds) {
      const auto n = (size_t)(seconds * sr);
      l.assign(n, 0.0f);
      r.assign(n, 0.0f);
      e2.render(l.data(), r.data(), (int)n, pressed);
    };

    e2.noteOnPerNote(2, 60, 1.0f, pressed);
    renderInto(0.3);

    check(peak() < 0.01, "a partial held down by pressure alone starts silent");

    e2.setNotePressure(3, 60, 1.0f);
    renderInto(0.3);

    check(peak() < 0.01, "pressure on another channel does not reach it (" +
                             std::to_string(peak()) + ")");

    e2.setNotePressure(2, 60, 1.0f);
    renderInto(0.3);

    const auto pressedLevel = peak();
    std::printf("  pressed on its own channel it reaches %.3f\n",
                pressedLevel);

    check(pressedLevel > 0.2,
          "pressure on its own channel brings it in (" +
              std::to_string(pressedLevel) + ")");
  }
}


/// What happens when the pool genuinely runs out.
///
/// Under the polyphony limit a voice is stolen with a fade and keeps rendering
/// out of the surplus. Past that surplus there is nothing left to fade into: a
/// voice has to go this instant, which is a step in the output whichever one it
/// is. The size of that step is the voice's current level, so the only lever is
/// which voice gets taken.
void testPoolExhaustion() {
  section("Pool exhaustion");

  constexpr double sr = 48000.0;

  auto p = makeFlatParams(0.0f);
  for (auto &o : p.osc) {
    o.attack = 0.002f;
    o.decay = 6.0f;
    o.sustain = 1.0f;
    o.release = 6.0f; // long enough that nothing frees itself during the test
    o.velAmount = 0.0f;
  }

  p.osc[0].volume = 0.5f;
  p.osc[1].volume = 0.25f;
  p.global.masterGain = 1.0f;
  p.global.safetyClip = false;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(SynthEngine::kMaxPolyphony);

  std::vector<float> l(256), r(256);
  const auto run = [&](int blocks) {
    for (int i = 0; i < blocks; ++i)
      engine.render(l.data(), r.data(), (int)l.size(), p);
  };

  // Fill the pool: sixteen held, then let them all go so they are all
  // releasing, then start sixteen more on top.
  for (int n = 0; n < SynthEngine::kMaxPolyphony; ++n)
    engine.noteOn(40 + n, 1.0f, p);

  run(8);

  for (int n = 0; n < SynthEngine::kMaxPolyphony; ++n)
    engine.noteOff(40 + n);

  run(4);

  double biggestStep = 0.0, loudest = 0.0;

  for (int n = 0; n < SynthEngine::kMaxPolyphony; ++n) {
    engine.noteOn(64 + n, 1.0f, p);

    engine.render(l.data(), r.data(), (int)l.size(), p);

    for (size_t i = 1; i < l.size(); ++i) {
      biggestStep = std::max(biggestStep, std::abs((double)l[i] - l[i - 1]));
      loudest = std::max(loudest, std::abs((double)l[i]));
    }
  }

  check(engine.getActiveVoiceCount() == SynthEngine::kPoolSize,
        "the pool really did fill up (" +
            std::to_string(engine.getActiveVoiceCount()) + " of " +
            std::to_string(SynthEngine::kPoolSize) + ")");

  std::printf("  pool full, 16 more notes forced in: biggest step %.4f "
              "against a %.4f signal\n",
              biggestStep, loudest);

  // A step the size of the whole signal is a voice at full level vanishing.
  check(biggestStep < 0.25 * loudest,
        "the step stays small when the tails are the old ones (" +
            std::to_string(biggestStep / std::max(1.0e-9, loudest)) +
            " of the signal)");

  // ---- and when the oldest voice is the one you least want taken ---------
  //
  // Above, the oldest voices were also the quietest, so either rule picks the
  // same one. This is the case that separates them: one loud note held down
  // since before everything else, with the rest of the pool full of quiet
  // tails. The oldest is exactly the wrong voice to take.
  auto q = makeFlatParams(0.0f);
  for (auto &o : q.osc) {
    o.attack = 0.002f;
    o.decay = 6.0f;
    o.sustain = 1.0f;
    o.release = 6.0f;
    o.velAmount = 1.0f; // so the tails can be played quietly
  }

  q.osc[0].volume = 0.7f;
  q.global.masterGain = 1.0f;
  q.global.safetyClip = false;

  SynthEngine keeper;
  keeper.prepare(sr);
  keeper.setPolyphony(SynthEngine::kMaxPolyphony);

  constexpr int kHeld = 60; // C4
  const auto heldHz = 440.0 * std::exp2((double)(kHeld - 69) / 12.0);

  keeper.noteOn(kHeld, 1.0f, q);

  std::vector<float> kl(512), kr(512);
  for (int i = 0; i < 20; ++i)
    keeper.render(kl.data(), kr.data(), (int)kl.size(), q);

  // Fill the rest of the pool with quiet tails, played and let go at once.
  for (int i = 0; i < SynthEngine::kPoolSize - 1; ++i) {
    keeper.noteOn(24 + i, 0.05f, q);
    keeper.render(kl.data(), kr.data(), (int)kl.size(), q);
    keeper.noteOff(24 + i);
  }

  keeper.render(kl.data(), kr.data(), (int)kl.size(), q);

  check(keeper.getActiveVoiceCount() == SynthEngine::kPoolSize,
        "the pool is full with one loud note held among quiet tails");

  // One more note, which has nowhere to go.
  keeper.noteOn(80, 1.0f, q);

  std::vector<float> after((size_t)(0.25 * sr)), afterR(after.size());
  keeper.render(after.data(), afterR.data(), (int)after.size(), q);

  const auto heldAfter = binMagnitude(after, heldHz, sr);

  std::printf("  a loud held note among quiet tails: %.4f left at its "
              "fundamental after the pool overflowed\n",
              heldAfter);

  check(heldAfter > 0.02,
        "the note still being held survives the overflow (" +
            std::to_string(heldAfter) + ")");
}

void testVoiceAllocation() {
  section("Voice allocation");

  constexpr double sr = 48000.0;
  constexpr int N = 256;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(4);

  auto p = makeFlatParams(0.02f);
  p.osc[0].release = 2.0f;

  std::vector<float> l((size_t)N), r((size_t)N);

  for (int note = 40; note < 60; ++note) {
    engine.noteOn(note, 1.0f, p);
    engine.render(l.data(), r.data(), N, p);
  }

  check(engine.getActiveVoiceCount() <= SynthEngine::kPoolSize,
        "pool is never over-subscribed");

  // Let the stolen tails finish, then only the polyphony limit should still be
  // held.
  for (int i = 0; i < 40; ++i)
    engine.render(l.data(), r.data(), N, p);

  check(engine.getActiveVoiceCount() <= 4,
        "polyphony limit respected after fades (" +
            std::to_string(engine.getActiveVoiceCount()) + " active)");

  engine.allSoundOff();
  check(engine.getActiveVoiceCount() == 0, "all-sound-off silences the pool");

  // Retriggering the same note must reuse its voice rather than stack.
  engine.noteOn(60, 1.0f, p);
  engine.render(l.data(), r.data(), N, p);
  const int after1 = engine.getActiveVoiceCount();
  engine.noteOn(60, 1.0f, p);
  engine.render(l.data(), r.data(), N, p);
  check(engine.getActiveVoiceCount() == after1,
        "repeated note-on reuses the same voice");

  // Sustain pedal holds released notes.
  engine.allSoundOff();
  p.osc[0].release =
      0.05f; // the 2 s release set above would outlast this sub-test
  engine.setSustainPedal(true);
  engine.noteOn(62, 1.0f, p);
  engine.render(l.data(), r.data(), N, p);
  engine.noteOff(62);
  engine.render(l.data(), r.data(), N, p);
  check(engine.getActiveVoiceCount() == 1, "sustain pedal holds the note");
  engine.setSustainPedal(false);
  for (int i = 0; i < 200; ++i)
    engine.render(l.data(), r.data(), N, p);
  check(engine.getActiveVoiceCount() == 0,
        "releasing the pedal releases the note");
}

// -----------------------------------------------------------------------------
// 9. Modulation actually modulates.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// 10. Velocity is per partial, so a soft note can be a different timbre and not
//     merely a quieter one.
// -----------------------------------------------------------------------------
void testPerPartialVelocity() {
  section("Per-partial velocity");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;

  auto measure = [&](float velocity) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc)
      o.tuneBlend = 1.0f;

    // Partial 1 ignores velocity entirely, partial 4 follows it completely.
    p.osc[0].volume = 0.4f;
    p.osc[0].velAmount = 0.0f;
    p.osc[3].volume = 0.4f;
    p.osc[3].velAmount = 1.0f;

    engine.noteOn(57, velocity, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);

    return std::pair<double, double>{binMagnitude(l, 220.0, sr),
                                     binMagnitude(l, 880.0, sr)};
  };

  const auto hard = measure(1.0f);
  const auto soft = measure(0.25f);

  check(std::abs(hard.first - soft.first) < 0.02 * hard.first,
        "the insensitive partial is unchanged by velocity");
  check(soft.second < 0.4 * hard.second,
        "the sensitive partial drops when played softly");

  // The point of the feature: the balance between partials moves, so the tone
  // itself changes rather than just the level.
  const double hardRatio = hard.second / hard.first;
  const double softRatio = soft.second / soft.first;
  std::printf("  partial 4 / partial 1: %.3f hard, %.3f soft\n", hardRatio,
              softRatio);
  check(softRatio < 0.5 * hardRatio, "the spectral balance shifts with touch");

  // A uniform setting must still behave like a plain velocity control.
  SynthEngine engine;
  engine.prepare(sr);
  auto p = makeFlatParams(0.02f);
  for (auto &o : p.osc)
    o.velAmount = 1.0f;

  engine.noteOn(57, 0.5f, p);
  std::vector<float> l((size_t)N), r((size_t)N);
  engine.render(l.data(), r.data(), N, p);

  float peakHalf = 0.0f;
  for (int n = 0; n < N; ++n)
    peakHalf = std::max(peakHalf, std::abs(l[(size_t)n]));

  SynthEngine full;
  full.prepare(sr);
  full.noteOn(57, 1.0f, p);
  std::vector<float> l2((size_t)N), r2((size_t)N);
  full.render(l2.data(), r2.data(), N, p);

  float peakFull = 0.0f;
  for (int n = 0; n < N; ++n)
    peakFull = std::max(peakFull, std::abs(l2[(size_t)n]));

  check(std::abs(peakHalf / peakFull - 0.5f) < 0.05f,
        "uniform full sensitivity scales level linearly with velocity");

  // --- the inverted half --------------------------------------------------
  auto atVelocity = [&](float amount, float velocity) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.tuneBlend = 1.0f;
      o.volume = 0.0f;
    }
    p.osc[0].volume = 0.5f;
    p.osc[0].velAmount = amount;

    engine.noteOn(57, velocity, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    return binMagnitude(l, 220.0, sr);
  };

  const double invertedSoft = atVelocity(-1.0f, 0.05f);
  const double invertedHard = atVelocity(-1.0f, 1.0f);
  check(invertedSoft > 5.0 * invertedHard,
        "a negative amount makes soft playing the loud end");
  check(invertedHard < 0.02 * invertedSoft,
        "full negative silences a hard hit");

  // Zero is the hinge: neither direction should touch the level there.
  check(std::abs(atVelocity(0.0f, 0.1f) - atVelocity(0.0f, 1.0f)) <
            0.02 * atVelocity(0.0f, 1.0f),
        "zero amount ignores velocity from either side");

  // Mirror image: -0.5 at velocity v should match +0.5 at 1 - v.
  check(std::abs(atVelocity(-0.5f, 0.25f) - atVelocity(0.5f, 0.75f)) <
            0.02 * atVelocity(0.5f, 0.75f),
        "the two halves are mirror images");

  // The point of the feature: opposite signs crossfade two sets of partials.
  {
    auto balance = [&](float velocity) {
      SynthEngine engine;
      engine.prepare(sr);

      auto p = makeFlatParams(0.0f);
      for (auto &o : p.osc) {
        o.tuneBlend = 1.0f;
        o.volume = 0.0f;
      }
      p.osc[0].volume = 0.5f;
      p.osc[0].velAmount = -1.0f; // soft timbre
      p.osc[3].volume = 0.5f;
      p.osc[3].velAmount = 1.0f; // hard timbre

      engine.noteOn(57, velocity, p);
      std::vector<float> l((size_t)N), r((size_t)N);
      engine.render(l.data(), r.data(), N, p);
      return binMagnitude(l, 880.0, sr) / binMagnitude(l, 220.0, sr);
    };

    const double soft = balance(0.15f);
    const double hard = balance(0.95f);
    std::printf("  partial 4 / partial 1: %.3f soft, %.3f hard\n", soft, hard);
    check(hard > 10.0 * soft,
          "opposite signs crossfade between two timbres across the range");
  }
}

// -----------------------------------------------------------------------------
// 11. Stereo spread must be symmetric, balanced, and actually wide where a real
//     spectrum keeps its energy.
// -----------------------------------------------------------------------------
void testPanning() {
  section("Panning");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;
  constexpr double f0 = 110.0; // A2, so all 32 partials stay under Nyquist

  struct Rendered {
    std::vector<float> l, r;
  };

  /// Renders with a pan position per partial, taken from fn(index).
  const auto render = [&](auto &&fn, bool rollOff) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.02f);
    for (int i = 0; i < kNumHarmonics; ++i) {
      p.osc[(size_t)i].tuneBlend = 1.0f;
      p.osc[(size_t)i].pan = (float)fn(i);

      if (rollOff)
        p.osc[(size_t)i].volume = i < 8 ? 0.02f / (float)(i + 1) : 0.0f;
    }

    engine.noteOn(45, 1.0f, p);
    Rendered out{std::vector<float>((size_t)N), std::vector<float>((size_t)N)};
    engine.render(out.l.data(), out.r.data(), N, p);
    return out;
  };

  /// Where each partial ended up, as -1 hard left to +1 hard right, read back
  /// out of the rendered audio rather than taken on trust.
  ///
  /// Measured as a balance of power rather than of amplitude, which is what an
  /// equal-power pan law is defined against: half left means half the power on
  /// the right, not half the amplitude.
  const auto balances = [&](const Rendered &x) {
    std::vector<double> b;

    for (int n = 1; n <= kNumHarmonics; ++n) {
      const double ml = binMagnitude(x.l, f0 * n, sr);
      const double mr = binMagnitude(x.r, f0 * n, sr);
      const double pl = ml * ml, pr = mr * mr;

      b.push_back(pl + pr > 1.0e-24 ? (pr - pl) / (pr + pl) : 0.0);
    }

    return b;
  };

  const auto imbalanceDb = [&](const Rendered &x) {
    double sl = 0.0, sr = 0.0;

    for (int n = 0; n < N; ++n) {
      sl += (double)x.l[(size_t)n] * x.l[(size_t)n];
      sr += (double)x.r[(size_t)n] * x.r[(size_t)n];
    }

    return 20.0 * std::log10(std::sqrt(sr / N) / std::sqrt(sl / N));
  };

  // Everything centred must be exactly mono.
  {
    const auto mono = render([](int) { return 0.0; }, false);
    float worst = 0.0f;

    for (int n = 0; n < N; ++n)
      worst = std::max(worst, std::abs(mono.l[(size_t)n] - mono.r[(size_t)n]));

    check(worst < 1.0e-7f, "centred partials leave the channels identical");
  }

  // A partial put somewhere has to arrive there. Odd partials hard left, even
  // hard right, which is a placement no width control could have made.
  {
    const auto split =
        render([](int i) { return (i % 2) == 0 ? -1.0 : 1.0; }, false);
    const auto b = balances(split);

    double worstLeft = 0.0, worstRight = 0.0;

    for (int i = 0; i < kNumHarmonics; ++i) {
      if ((i % 2) == 0)
        worstLeft = std::max(worstLeft, b[(size_t)i] + 1.0);
      else
        worstRight = std::max(worstRight, 1.0 - b[(size_t)i]);
    }

    std::printf("  hard panned: worst error left %.4f, right %.4f\n", worstLeft,
                worstRight);

    check(worstLeft < 0.01 && worstRight < 0.01,
          "each partial arrives where it was put");
  }

  // Half left has to land half left, not somewhere near it: equal power means
  // the number on the knob and the number in the audio agree.
  {
    const auto half = render([](int) { return -0.5; }, false);
    const auto b = balances(half);

    double worst = 0.0;
    for (auto v : b)
      worst = std::max(worst, std::abs(v + 0.5));

    std::printf("  half left: worst error %.4f\n", worst);
    check(worst < 0.02, "a partial at half left images at half left");
  }

  // Equal power, so a partial sweeping across does not get louder in the
  // middle or thinner at the edges.
  {
    double loudest = 0.0, quietest = 1.0e9;

    for (float pan : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
      const auto x = render([pan](int) { return (double)pan; }, false);

      double energy = 0.0;
      for (int n = 0; n < N; ++n)
        energy += (double)x.l[(size_t)n] * x.l[(size_t)n] +
                  (double)x.r[(size_t)n] * x.r[(size_t)n];

      loudest = std::max(loudest, energy);
      quietest = std::min(quietest, energy);
    }

    const auto spreadDb = 10.0 * std::log10(loudest / quietest);
    std::printf("  loudness across the sweep varies by %.2f dB\n", spreadDb);

    check(spreadDb < 0.1, "panning holds the level as it crosses the field");
  }

  // The shape the old spread control used to make, now something a preset
  // writes into the pans: mirrored pairs, widening up the series.
  const auto fan = [](int i) {
    constexpr int lastPair = kNumHarmonics / 2 - 1;

    const int pairIndex = i / 2;
    const bool second = (i % 2) != 0;
    const bool flip = (pairIndex % 2) != 0;

    return (second != flip ? 1.0 : -1.0) *
           std::sqrt((double)pairIndex / (double)lastPair);
  };

  const auto wide = render(fan, false);
  auto sorted = balances(wide);
  std::sort(sorted.begin(), sorted.end());

  double worstMirror = 0.0;
  for (int k = 0; k < kNumHarmonics / 2; ++k)
    worstMirror = std::max(
        worstMirror,
        std::abs(sorted[(size_t)k] + sorted[(size_t)(kNumHarmonics - 1 - k)]));

  std::printf("  mirrored fan: worst mirror error %.4f\n", worstMirror);
  check(worstMirror < 0.01, "every position in the fan has an opposite number");
  check(sorted.front() < -0.99, "the field reaches hard left");
  check(sorted.back() > 0.99, "the field reaches hard right");

  const double flatDb = imbalanceDb(wide);
  std::printf("  channel imbalance: %+.2f dB flat", flatDb);
  check(std::abs(flatDb) < 0.2, "a flat spectrum stays centred");

  // The reason the pairing exists: a rolled-off spectrum must stay centred too,
  // and must still be placed where its energy actually lives.
  const auto rolled = render(fan, true);
  const double rolledDb = imbalanceDb(rolled);
  std::printf(", %+.2f dB rolled off\n", rolledDb);
  check(std::abs(rolledDb) < 0.5, "a 1/n spectrum stays centred");

  auto rb = balances(rolled);
  double widest = 0.0;
  for (int n = 0; n < 8; ++n)
    widest = std::max(widest, std::abs(rb[(size_t)n]));

  std::printf("  widest of the first eight partials: %.3f\n", widest);
  check(widest > 0.2,
        "the partials carrying the energy are actually placed, not bunched "
        "in the middle");
}

// -----------------------------------------------------------------------------
// 12. Aftertouch adds to the fader rather than scaling it, so a strip parked at
//     silence can be brought in by pressure alone.
// -----------------------------------------------------------------------------
void testAftertouch() {
  section("Aftertouch");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;

  auto build = [&](float atAmount, float volume) {
    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.tuneBlend = 1.0f;
      o.volume = 0.0f;
      o.atAmount = 0.0f;
      o.velAmount = 0.0f;
    }
    p.osc[0].volume = volume;
    p.osc[0].atAmount = atAmount;
    p.osc[0].velAmount = 1.0f;
    return p;
  };

  auto level = [&](float atAmount, float volume, float pressure, float velocity,
                   bool poly) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = build(atAmount, volume);
    if (!poly)
      p.global.aftertouch = pressure;

    engine.noteOn(57, velocity, p);
    if (poly)
      engine.setPolyPressure(57, pressure);

    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    return binMagnitude(l, 220.0, sr);
  };

  check(level(1.0f, 0.0f, 0.0f, 1.0f, false) < 1.0e-4,
        "a fader at zero with no pressure is silent");

  const double pressed = level(1.0f, 0.0f, 1.0f, 1.0f, false);
  check(pressed > 0.1, "pressure alone brings the partial in");

  // The whole point of adding rather than scaling: velocity must not gate it.
  const double softKey = level(1.0f, 0.0f, 1.0f, 0.05f, false);
  check(std::abs(softKey - pressed) < 0.02 * pressed,
        "aftertouch is independent of velocity");

  const double viaPoly = level(1.0f, 0.0f, 1.0f, 1.0f, true);
  check(std::abs(viaPoly - pressed) < 0.02 * pressed,
        "polyphonic aftertouch reaches the voice");

  // Additive: pressure lifts a partly open fader further.
  const double faderOnly = level(0.0f, 0.4f, 0.0f, 1.0f, false);
  const double faderPlus = level(0.3f, 0.4f, 1.0f, 1.0f, false);
  check(faderPlus > faderOnly * 1.2, "pressure adds on top of the fader");

  check(level(0.0f, 0.4f, 1.0f, 1.0f, false) < faderOnly * 1.02,
        "a strip with no aftertouch amount ignores pressure");

  // A negative amount subtracts, so pressure fades an open strip out.
  check(level(-0.3f, 0.4f, 1.0f, 1.0f, false) < faderOnly * 0.8,
        "negative aftertouch fades a partial out under pressure");
  check(level(-1.0f, 0.4f, 1.0f, 1.0f, false) < 1.0e-4,
        "enough negative aftertouch silences it entirely");

  // Seven-bit pressure arriving at MIDI rate must not step the gain.
  {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = build(1.0f, 0.0f);
    engine.noteOn(45, 1.0f, p); // low note, so a legitimate slope stays small

    constexpr int block = 2048;
    std::vector<float> a((size_t)block), b((size_t)block);
    engine.render(a.data(), b.data(), block, p);

    p.global.aftertouch = 1.0f; // slam it from nothing to full
    std::vector<float> c((size_t)block), d((size_t)block);
    engine.render(c.data(), d.data(), block, p);

    float worst = 0.0f;
    for (int n = 1; n < block; ++n)
      worst = std::max(worst, std::abs(c[(size_t)n] - c[(size_t)n - 1]));

    std::printf("  max step on a full pressure jump: %.5f\n", worst);
    check(worst < 0.05f, "a pressure jump is smoothed, not stepped");
  }
}

// -----------------------------------------------------------------------------
// 13. Pitch drift: smooth random wander, a different rate per partial and per
//     note, and no stepping.
// -----------------------------------------------------------------------------
void testDrift() {
  section("Pitch drift");

  // --- the generator on its own
  // -----------------------------------------------
  {
    Xorshift rng(12345u);
    SmoothRandom sr;
    sr.restart(rng, 1.0, 1000.0); // one new point per 1000 steps

    float previous = sr.current();
    float worstStep = 0.0f, lowest = 1.0f, highest = -1.0f;

    for (int i = 0; i < 200000; ++i) {
      const float v = sr.advance(rng);
      worstStep = std::max(worstStep, std::abs(v - previous));
      lowest = std::min(lowest, v);
      highest = std::max(highest, v);
      previous = v;
    }

    std::printf("  generator: range %.3f to %.3f, largest step %.5f\n", lowest,
                highest, worstStep);

    // Sample and hold would show steps approaching the full 2.0 range here.
    check(worstStep < 0.02f, "the contour is smooth, not stepped");
    check(lowest < -0.5f && highest > 0.5f, "it uses the range");
    check(lowest > -1.35f && highest < 1.35f, "it stays near +-1");

    // It must actually wander rather than settle.
    double sum = 0.0;
    for (int i = 0; i < 20000; ++i)
      sum += std::abs(sr.advance(rng));
    check(sum / 20000.0 > 0.1, "the contour keeps moving");
  }

  // --- two streams must not correlate
  // ----------------------------------------- Correlation is only meaningful
  // over many independent draws. A slow contour yields very few points per run,
  // so the raw generator is checked directly and the contour is run long enough
  // to gather thousands of points.
  {
    Xorshift a(1u), b(2u); // adjacent seeds are the hard case for xorshift
    double dot = 0.0, ea = 0.0, eb = 0.0;

    for (int i = 0; i < 200000; ++i) {
      const double u = a.bipolar(), v = b.bipolar();
      dot += u * v;
      ea += u * u;
      eb += v * v;
    }

    const double raw = dot / std::sqrt(ea * eb);
    std::printf("  raw generator correlation: %+.4f\n", raw);
    check(std::abs(raw) < 0.02, "adjacent seeds give uncorrelated streams");
  }

  {
    Xorshift a(1u), b(2u);
    SmoothRandom x, y;
    x.restart(a, 10.0, 1000.0); // a new point every 100 steps
    y.restart(b, 10.0, 1000.0);

    constexpr int steps = 500000; // about 5000 points each
    double dot = 0.0, ex = 0.0, ey = 0.0;

    for (int i = 0; i < steps; ++i) {
      const double u = x.advance(a), v = y.advance(b);
      dot += u * v;
      ex += u * u;
      ey += v * v;
    }

    const double correlation = dot / std::sqrt(ex * ey);
    std::printf("  contour correlation over ~5000 points: %+.4f\n",
                correlation);
    check(std::abs(correlation) < 0.08, "separate contours are uncorrelated");
  }

  // --- in the synth
  // -------------------------------------------------------------
  constexpr double sr = 48000.0;
  constexpr int N = 48000;

  auto renderOne = [&](float driftCents, int note) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.tuneBlend = 1.0f;
      o.volume = 0.0f;
    }
    p.osc[0].volume = 0.5f;
    p.osc[0].driftCents = driftCents;

    engine.noteOn(note, 1.0f, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    return l;
  };

  // Drift at zero must leave the pitch untouched.
  const auto clean = renderOne(0.0f, 57);
  const auto drifted = renderOne(25.0f, 57);

  const double cleanCarrier = binMagnitude(clean, 220.0, sr);
  const double driftedCarrier = binMagnitude(drifted, 220.0, sr);

  check(cleanCarrier > 0.1, "the undrifted partial is present");
  check(driftedCarrier < 0.98 * cleanCarrier,
        "drift smears the carrier out of its bin");

  bool finite = true;
  for (int n = 0; n < N; ++n)
    finite &= std::isfinite(drifted[(size_t)n]);
  check(finite, "drifted output is finite");

  // The wander is shallow by design: a hard cap keeps this chorusing rather
  // than vibrato.
  check(binMagnitude(drifted, 220.0 * 1.02, sr) < 0.05 * cleanCarrier,
        "drift stays close to the nominal pitch");

  // Two notes on the same engine must not receive the same contour. This has to
  // share one engine: a fresh one is reseeded and would legitimately repeat.
  {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc) {
      o.tuneBlend = 1.0f;
      o.volume = 0.0f;
    }
    p.osc[0].volume = 0.5f;
    p.osc[0].driftCents = 25.0f;

    auto play = [&] {
      engine.allSoundOff();
      engine.noteOn(57, 1.0f, p);
      std::vector<float> l((size_t)N), r((size_t)N);
      engine.render(l.data(), r.data(), N, p);
      return l;
    };

    const auto first = play();
    const auto second = play();

    double difference = 0.0;
    for (int n = 0; n < N; ++n)
      difference += std::abs(first[(size_t)n] - second[(size_t)n]);

    std::printf("  divergence between two notes: %.1f\n", difference);
    check(difference > 100.0, "consecutive notes drift differently");
  }
}

// -----------------------------------------------------------------------------
// 14. The per-partial meter has to follow what is actually coming out, so the
//     display cannot drift away from the sound.
// -----------------------------------------------------------------------------
void testPartialMetering() {
  section("Per-partial metering");

  constexpr double sr = 48000.0;
  constexpr int block = 512;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.0f);
  for (int i = 0; i < kNumHarmonics; ++i) {
    p.osc[(size_t)i].tuneBlend = 1.0f;
    p.osc[(size_t)i].volume = i < 4 ? 0.8f / (float)(i + 1) : 0.0f;
  }

  std::vector<float> l((size_t)block), r((size_t)block);

  engine.render(l.data(), r.data(), block, p);
  bool allQuiet = true;
  for (int i = 0; i < kNumHarmonics; ++i)
    allQuiet &= engine.getPartialLevel(i) < 1.0e-6f;
  check(allQuiet, "meters read zero while nothing sounds");

  engine.noteOn(45, 1.0f, p);
  for (int b = 0; b < 20; ++b)
    engine.render(l.data(), r.data(), block, p);

  // The meter should trace the fader shape it was given.
  for (int i = 0; i < 4; ++i)
    check(engine.getPartialLevel(i) > 0.5f * 0.8f / (float)(i + 1),
          "partial " + std::to_string(i + 1) + " meters near its fader");

  for (int i = 4; i < kNumHarmonics; ++i)
    check(engine.getPartialLevel(i) < 1.0e-6f,
          "silent partial " + std::to_string(i + 1) + " meters zero");

  check(engine.getPartialLevel(0) > engine.getPartialLevel(1),
        "the meters follow the spectral shape");

  // Muting has to show, since the meter is meant to say what you can hear.
  p.osc[0].audible = false;
  for (int b = 0; b < 4; ++b)
    engine.render(l.data(), r.data(), block, p);
  check(engine.getPartialLevel(0) < 1.0e-6f, "a muted partial meters zero");

  p.osc[0].audible = true;

  // And it has to fall away with the release rather than stick.
  engine.allNotesOff();
  for (int b = 0; b < 400; ++b)
    engine.render(l.data(), r.data(), block, p);

  bool decayed = true;
  for (int i = 0; i < kNumHarmonics; ++i)
    decayed &= engine.getPartialLevel(i) < 1.0e-4f;
  check(decayed, "meters fall to zero once the note has released");

  // The master meter has to report the finished output, after master gain and
  // the clipper, so it says what actually leaves the plugin.
  {
    SynthEngine out;
    out.prepare(sr);

    auto q = makeFlatParams(0.05f);
    q.global.masterGain = 1.0f;
    q.global.safetyClip = false;

    out.render(l.data(), r.data(), block, q);
    check(out.getOutputLevelLeft() < 1.0e-6f &&
              out.getOutputLevelRight() < 1.0e-6f,
          "output meter is zero when idle");

    out.noteOn(45, 1.0f, q);
    for (int b = 0; b < 20; ++b)
      out.render(l.data(), r.data(), block, q);

    float actual = 0.0f;
    for (int n = 0; n < block; ++n)
      actual = std::max(actual, std::abs(l[(size_t)n]));

    check(out.getOutputLevelLeft() >= actual - 1.0e-6f,
          "output meter is at least the buffer peak");
    check(out.getOutputLevelLeft() > 0.01f,
          "output meter reads a sounding patch");

    // Halving the master gain has to halve the reading.
    const float loud = out.getOutputLevelLeft();
    q.global.masterGain = 0.5f;
    for (int b = 0; b < 40; ++b)
      out.render(l.data(), r.data(), block, q);

    check(std::abs(out.getOutputLevelLeft() / loud - 0.5f) < 0.05f,
          "output meter tracks master gain");

    // Mono until spread is dialled in, which is why the two are reported
    // separately at all.
    check(std::abs(out.getOutputLevelLeft() - out.getOutputLevelRight()) <
              1.0e-6f,
          "the channels match with no spread");

    q.global.masterGain = 1.0f;
    for (auto &o : q.osc)
      o.volume = 0.0f;

    q.osc[3].volume = 0.5f; // a partial placed off centre
    q.osc[3].pan = 0.9f;

    SynthEngine wide;
    wide.prepare(sr);
    wide.noteOn(45, 1.0f, q);
    for (int b = 0; b < 20; ++b)
      wide.render(l.data(), r.data(), block, q);

    check(std::abs(wide.getOutputLevelLeft() - wide.getOutputLevelRight()) >
              0.05f * wide.getOutputLevelLeft(),
          "spread drives the channels apart");
  }
}

// -----------------------------------------------------------------------------
// 15. A delayed partial has to stay out of the mix until its time comes.
// -----------------------------------------------------------------------------
void testEnvelopeDelay() {
  section("Envelope delay");

  constexpr double sr = 48000.0;
  constexpr int block = 4800; // 100 ms

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.0f);
  for (auto &o : p.osc) {
    o.tuneBlend = 1.0f;
    o.volume = 0.0f;
  }
  p.osc[0].volume = 0.5f; // immediate
  p.osc[3].volume = 0.5f; // held back
  p.osc[3].delay = 0.30f;
  p.osc[3].attack = 0.01f;

  std::vector<float> l((size_t)block), r((size_t)block);
  engine.noteOn(45, 1.0f, p);

  auto slice = [&] {
    engine.render(l.data(), r.data(), block, p);
    return std::pair<double, double>{binMagnitude(l, 110.0, sr),
                                     binMagnitude(l, 440.0, sr)};
  };

  const auto first = slice(); // 0 to 100 ms
  check(first.first > 0.05, "the undelayed partial sounds immediately");
  check(first.second < 0.002, "the delayed partial is absent while it waits (" +
                                  std::to_string(first.second) + ")");

  slice(); // 100 to 200 ms, still waiting
  slice(); // 200 to 300 ms, arriving

  const auto after = slice(); // 300 to 400 ms
  check(after.second > 0.05, "the delayed partial arrives once its time is up");
  check(after.first > 0.05, "the undelayed partial is still going");

  engine.allSoundOff();
}

// -----------------------------------------------------------------------------
// 16. The noise channel: broadband, enveloped like a strip, tilted by colour.
// -----------------------------------------------------------------------------
/// Runs one envelope directly and reports the level at every sample, which is
/// the only way to see the shape rather than infer it from a spectrum.
std::vector<float> envelopeTrace(double sr, float sustain, float swell,
                                 float offLevel, float release,
                                 double holdSeconds, double tailSeconds) {
  Envelope env;
  env.setSampleRate(sr);
  env.configure(0.0f, 0.002f, 0.05f, sustain, swell, offLevel, release);
  env.noteOn(true);

  std::vector<float> out;
  out.reserve((size_t)((holdSeconds + tailSeconds) * sr));

  for (int n = 0; n < (int)(holdSeconds * sr); ++n)
    out.push_back(env.tick());

  env.noteOff();

  for (int n = 0; n < (int)(tailSeconds * sr); ++n)
    out.push_back(env.tick());

  return out;
}

void testKeyOffEnvelope() {
  section("Key-off envelope");

  constexpr double sr = 48000.0;
  constexpr double hold = 0.5;

  const auto at = [&](const std::vector<float> &v, double seconds) {
    const auto i = (size_t)(seconds * sr);
    return i < v.size() ? (double)v[i] : 0.0;
  };

  const auto peakAfter = [&](const std::vector<float> &v, double from,
                             double to) {
    double m = 0.0;
    for (auto i = (size_t)(from * sr); i < (size_t)(to * sr) && i < v.size();
         ++i)
      m = std::max(m, (double)v[i]);

    return m;
  };

  // ---- a key-off level of zero leaves the old behaviour exactly as it was ---
  {
    const auto trace = envelopeTrace(sr, 0.5f, 0.2f, 0.0f, 0.4f, hold, 2.0);

    check(std::abs(at(trace, 0.4) - 0.5) < 1.0e-4,
          "it sustains at the sustain level");

    // Straight down from the sustain, with nothing above it on the way.
    check(peakAfter(trace, hold, hold + 2.0) <= 0.5 + 1.0e-6,
          "the release never rises above the sustain");

    // Within 1% after the release time, which is the convention everywhere
    // else in this envelope.
    check(at(trace, hold + 0.4) < 0.5 * 0.02,
          "and is down to 1% after the release time (" +
              std::to_string(at(trace, hold + 0.4)) + ")");
  }

  // ---- a key-off level above the sustain: the point of the exercise ---------
  {
    const auto trace = envelopeTrace(sr, 0.3f, 0.15f, 0.9f, 0.5f, hold, 3.0);

    const auto peak = peakAfter(trace, hold, hold + 3.0);
    std::printf("  sustain 0.30, key-off level 0.90: peak after the key is "
                "let go %.3f\n",
                peak);

    check(peak > 0.85, "letting go can be louder than holding");
    check(std::abs(at(trace, hold + 0.15) - 0.9) < 1.0e-3,
          "the swell arrives at the key-off level on time (" +
              std::to_string(at(trace, hold + 0.15)) + ")");

    // Rising the whole way there, not overshooting and coming back.
    // Up to the last sample of the swell itself: one further along is the
    // first sample of the release, which is meant to be lower.
    bool rising = true;
    for (auto i = (size_t)(hold * sr); i + 1 < (size_t)((hold + 0.15) * sr);
         ++i)
      rising &= trace[i + 1] >= trace[i] - 1.0e-6f;

    check(rising, "and gets there without wobbling on the way");

    check(at(trace, hold + 0.15 + 0.5) < 0.9 * 0.02,
          "then releases from there on schedule (" +
              std::to_string(at(trace, hold + 0.15 + 0.5)) + ")");
  }

  // ---- a key-off level below the sustain: the piano case -------------------
  {
    const auto trace = envelopeTrace(sr, 0.9f, 0.03f, 0.2f, 4.0f, hold, 6.0);

    check(at(trace, hold + 0.03) < 0.25,
          "a level below the sustain drops fast (" +
              std::to_string(at(trace, hold + 0.03)) + ")");

    check(at(trace, hold + 1.0) > 0.02,
          "and then hangs on in a long tail (" +
              std::to_string(at(trace, hold + 1.0)) + ")");

    // The whole point: the tail outlasts what a single release from 0.9 would
    // have given at the same setting, because it starts from a low level and
    // takes the full release time from there.
    check(at(trace, hold + 4.0) < 0.2 * 0.05, "before falling silent");
  }

  // ---- the key-off sound happens even if the note never really started -----
  {
    Envelope env;
    env.setSampleRate(sr);
    env.configure(1.0f, 0.01f, 0.1f, 0.5f, 0.05f, 0.8f, 0.3f);
    env.noteOn(true);

    for (int n = 0; n < (int)(0.1 * sr); ++n)
      env.tick(); // still inside the delay

    check(env.getLevel() < 1.0e-6f, "nothing sounds during the delay");

    env.noteOff();

    double peak = 0.0;
    for (int n = 0; n < (int)(1.0 * sr); ++n)
      peak = std::max(peak, (double)env.tick());

    std::printf("  released inside the delay, key-off level 0.80: peak %.3f\n",
                peak);

    check(peak > 0.7,
          "a key let go before the note arrives still makes its key-off sound");
  }

  // ---- and does not, when there is no key-off stage -------------------------
  {
    Envelope env;
    env.setSampleRate(sr);
    env.configure(1.0f, 0.01f, 0.1f, 0.5f, 0.05f, 0.0f, 0.3f);
    env.noteOn(true);

    for (int n = 0; n < (int)(0.1 * sr); ++n)
      env.tick();

    env.noteOff();

    double peak = 0.0;
    for (int n = 0; n < (int)(1.0 * sr); ++n)
      peak = std::max(peak, (double)env.tick());

    check(peak < 1.0e-6, "with no key-off stage it is cancelled as before");
    check(!env.isActive(), "and the partial frees itself");
  }

  // ---- a decay that lands on silence still has a key-off to come ------------
  //
  // The music box case, and the one that was broken: with no sustain, the
  // decay reaches zero while the key is still down. Reaching zero is not the
  // same as being finished, because the key-off level is what the patch is
  // actually about.
  {
    Envelope env;
    env.setSampleRate(sr);
    env.configure(0.0f, 0.002f, 0.05f, 0.0f, 0.02f, 0.6f, 0.4f);
    env.noteOn(true);

    // Long past the end of a 50 ms decay to nothing.
    for (int n = 0; n < (int)(0.5 * sr); ++n)
      env.tick();

    check(env.getLevel() < 1.0e-6f, "a decay to no sustain reaches silence");
    check(env.isActive(),
          "but the partial stays alive while the key is still down");
    check(env.isSilentlyHolding(),
          "and says it is holding silently, so the oscillator can be skipped");

    env.noteOff();

    double peak = 0.0;
    for (int n = 0; n < (int)(0.1 * sr); ++n)
      peak = std::max(peak, (double)env.tick());

    std::printf("  decayed to silence, key-off level 0.60: peak %.3f\n", peak);

    check(peak > 0.55,
          "letting go brings it back up to the key-off level (" +
              std::to_string(peak) + ")");

    for (int n = 0; n < (int)(2.0 * sr); ++n)
      env.tick();

    check(!env.isActive(), "and after the release it frees itself");
  }

  // ---- with no key-off level it still frees itself at once ------------------
  {
    Envelope env;
    env.setSampleRate(sr);
    env.configure(0.0f, 0.002f, 0.05f, 0.0f, 0.02f, 0.0f, 0.4f);
    env.noteOn(true);

    for (int n = 0; n < (int)(0.5 * sr); ++n)
      env.tick();

    check(!env.isActive(),
          "no sustain and no key-off level is still a finished note");
  }
}

/// Release velocity, aimed at the key-off level. Letting a key go slowly and
/// snatching it back are different gestures and a jack or a damper knows the
/// difference.
void testReleaseVelocity() {
  section("Release velocity");

  constexpr double sr = 48000.0;

  const auto keyOffPeakFor = [&](float amount, float lift) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(4);

    auto p = makeFlatParams(0.0f);
    p.osc[0].volume = 0.6f;
    p.osc[0].attack = 0.002f;
    p.osc[0].decay = 0.05f;
    p.osc[0].sustain = 0.0f; // silent under the key, so only the key-off shows
    p.osc[0].swell = 0.01f;
    p.osc[0].offLevel = 0.9f;
    p.osc[0].release = 0.3f;
    p.osc[0].liftAmount = amount;
    p.osc[0].velAmount = 0.0f;
    p.global.masterGain = 1.0f;
    p.global.safetyClip = false;

    engine.noteOn(60, 1.0f, p);

    std::vector<float> l((size_t)(0.3 * sr)), r(l.size());
    engine.render(l.data(), r.data(), (int)l.size(), p);

    engine.noteOff(60, lift);

    std::vector<float> al((size_t)(0.2 * sr)), ar(al.size());
    engine.render(al.data(), ar.data(), (int)al.size(), p);

    double peak = 0.0;
    for (auto v : al)
      peak = std::max(peak, std::abs((double)v));

    return peak;
  };

  // Off by default: the gesture is ignored and every release is the same.
  const auto ignoredSlow = keyOffPeakFor(0.0f, 0.05f);
  const auto ignoredFast = keyOffPeakFor(0.0f, 1.0f);

  check(std::abs(ignoredSlow - ignoredFast) < 1.0e-4,
        "at zero amount the release speed changes nothing (" +
            std::to_string(ignoredSlow) + " against " +
            std::to_string(ignoredFast) + ")");

  check(ignoredSlow > 0.05, "and the key-off still sounds");

  // Turned up, snatching the key back is the loud one.
  const auto slow = keyOffPeakFor(1.0f, 0.05f);
  const auto fast = keyOffPeakFor(1.0f, 1.0f);

  std::printf("  key-off peak: slow lift %.3f, fast lift %.3f, ignored %.3f\n",
              slow, fast, ignoredSlow);

  check(fast > 4.0 * slow,
        "at full amount a fast release is far louder than a slow one (" +
            std::to_string(fast / std::max(1.0e-9, slow)) + " times)");

  check(std::abs(fast - ignoredFast) < 1.0e-4,
        "and a full-speed release is what the level was already set to");

  // Negative inverts it, the way the note-on velocity row does.
  const auto invertedSlow = keyOffPeakFor(-1.0f, 0.05f);
  const auto invertedFast = keyOffPeakFor(-1.0f, 1.0f);

  check(invertedSlow > 4.0 * invertedFast,
        "a negative amount makes the slow release the loud one (" +
            std::to_string(invertedSlow / std::max(1.0e-9, invertedFast)) +
            " times)");

  // Nothing in a fresh patch asks for it.
  SynthParams fresh;
  check(fresh.osc[0].liftAmount == 0.0f && fresh.noise.liftAmount == 0.0f,
        "and it is off in a fresh patch, so nothing already made changes");
}

/// The same fault from the outside, where it was heard: a whole voice going
/// quiet before it could make its key-off sound.
void testKeyOffAfterSilentDecay() {
  section("Key-off after a silent decay");

  constexpr double sr = 48000.0;

  SynthEngine engine;
  engine.prepare(sr);
  engine.setPolyphony(8);

  auto p = makeFlatParams(0.05f);
  for (auto &o : p.osc) {
    o.attack = 0.002f;
    o.decay = 0.05f;
    o.sustain = 0.0f; // dies away under a held key
    o.swell = 0.02f;
    o.offLevel = 0.6f; // and speaks again when the key comes up
    o.release = 0.3f;
  }

  engine.noteOn(60, 1.0f, p);

  // Half a second of holding, by which time a 50 ms decay to nothing is long
  // over.
  constexpr int held = (int)(0.5 * sr);
  std::vector<float> l((size_t)held), r((size_t)held);
  engine.render(l.data(), r.data(), held, p);

  double tailWhileHeld = 0.0;
  for (auto i = (size_t)(0.3 * sr); i < l.size(); ++i)
    tailWhileHeld = std::max(tailWhileHeld, std::abs((double)l[i]));

  check(tailWhileHeld < 1.0e-4,
        "the note goes quiet under a held key as the patch asks (" +
            std::to_string(tailWhileHeld) + ")");

  check(engine.getActiveVoiceCount() == 1,
        "but the voice is still there to be let go of");

  engine.noteOff(60);

  constexpr int after = (int)(0.5 * sr);
  std::vector<float> al((size_t)after), ar((size_t)after);
  engine.render(al.data(), ar.data(), after, p);

  double keyOff = 0.0;
  for (auto v : al)
    keyOff = std::max(keyOff, std::abs((double)v));

  std::printf("  held peak %.6f, key-off peak %.4f\n", tailWhileHeld, keyOff);

  check(keyOff > 0.05,
        "and letting go sounds the key-off stage (" + std::to_string(keyOff) +
            ")");

  // It has to end, or a patch like this would pile up voices forever.
  std::vector<float> el((size_t)after), er((size_t)after);
  for (int i = 0; i < 6; ++i)
    engine.render(el.data(), er.data(), after, p);

  check(engine.getActiveVoiceCount() == 0, "then the voice frees itself");
}

void testNoiseChannel() {
  section("Noise channel");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;

  auto render = [&](float volume, float colour, float velocity = 1.0f) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc)
      o.volume = 0.0f; // noise only, so nothing else colours the measurement

    p.noise.volume = volume;
    p.noise.colour = colour;
    p.noise.velAmount = 0.0f;
    p.noise.attack = 0.001f;
    p.noise.sustain = 1.0f;
    p.noise.audible = true;

    engine.noteOn(45, velocity, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    return l;
  };

  // Silent at zero, and it needs a note like everything else.
  {
    const auto quiet = render(0.0f, 0.5f);
    float peak = 0.0f;
    for (int n = 0; n < N; ++n)
      peak = std::max(peak, std::abs(quiet[(size_t)n]));
    check(peak < 1.0e-6f, "the noise channel is silent at zero level");
  }

  const auto flat = render(0.6f, 0.5f);

  float peak = 0.0f;
  bool finite = true;
  for (int n = 0; n < N; ++n) {
    peak = std::max(peak, std::abs(flat[(size_t)n]));
    finite &= std::isfinite(flat[(size_t)n]);
  }

  check(finite, "noise output is finite");
  check(peak > 0.05f, "noise sounds");

  // Broadband, unlike a partial: energy everywhere rather than in one bin.
  const double low = bandMagnitude(flat, 150.0, 350.0, sr);
  const double mid = bandMagnitude(flat, 1800.0, 2200.0, sr);
  const double high = bandMagnitude(flat, 7000.0, 11000.0, sr);
  check(low > 1.0e-5 && mid > 1.0e-5 && high > 1.0e-5,
        "noise covers the spectrum rather than one frequency");

  // Colour tilts it. Dark should favour the bottom, bright the top.
  const auto dark = render(0.6f, 0.0f);
  const auto bright = render(0.6f, 1.0f);

  const auto tilt = [&](const std::vector<float> &x) {
    return bandMagnitude(x, 7000.0, 11000.0, sr) /
           bandMagnitude(x, 150.0, 350.0, sr);
  };

  const double darkTilt = tilt(dark);
  const double flatTilt = tilt(flat);
  const double brightTilt = tilt(bright);

  std::printf("  high/low ratio: %.3f dark, %.3f flat, %.3f bright\n", darkTilt,
              flatTilt, brightTilt);

  check(darkTilt < 0.5 * flatTilt, "colour at zero rolls the top off");
  check(brightTilt > 2.0 * flatTilt, "colour at full rolls the bottom off");

  // The centre is genuinely flat, not merely filtered less. The complementary
  // pair has to reconstruct the original white noise there.
  check(flatTilt > 0.7 && flatTilt < 1.4,
        "the centre of the colour knob is flat (" + std::to_string(flatTilt) +
            ")");

  // It answers to velocity like a strip.
  {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc)
      o.volume = 0.0f;

    p.noise.volume = 0.6f;
    p.noise.velAmount = 1.0f;
    p.noise.attack = 0.001f;

    engine.noteOn(45, 0.25f, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);

    float soft = 0.0f;
    for (int n = 0; n < N; ++n)
      soft = std::max(soft, std::abs(l[(size_t)n]));

    check(soft < 0.5f * peak, "the noise channel follows velocity");
  }

  // Muting it silences it, and the meter agrees.
  {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc)
      o.volume = 0.0f;

    p.noise.volume = 0.6f;
    p.noise.velAmount = 0.0f;
    p.noise.attack = 0.001f;

    engine.noteOn(45, 1.0f, p);
    std::vector<float> l((size_t)N), r((size_t)N);
    engine.render(l.data(), r.data(), N, p);
    check(engine.getNoiseLevel() > 0.05f, "the noise meter reads it");

    // Muting ramps over one control block rather than cutting, so measure once
    // that ramp is behind us.
    p.noise.audible = false;
    engine.render(l.data(), r.data(), N, p);
    engine.render(l.data(), r.data(), N, p);

    float muted = 0.0f;
    for (int n = 0; n < N; ++n)
      muted = std::max(muted, std::abs(l[(size_t)n]));

    check(muted < 1.0e-6f, "muting the noise channel silences it");
    check(engine.getNoiseLevel() < 1.0e-6f, "and its meter falls to zero");
  }

  // Two voices must not layer the identical noise.
  {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.0f);
    for (auto &o : p.osc)
      o.volume = 0.0f;

    p.noise.volume = 0.4f;
    p.noise.velAmount = 0.0f;
    p.noise.attack = 0.001f;

    engine.noteOn(45, 1.0f, p);
    std::vector<float> one((size_t)N), r1((size_t)N);
    engine.render(one.data(), r1.data(), N, p);

    engine.allSoundOff();
    engine.noteOn(45, 1.0f, p);
    engine.noteOn(52, 1.0f, p);
    std::vector<float> two((size_t)N), r2((size_t)N);
    engine.render(two.data(), r2.data(), N, p);

    double energyOne = 0.0, energyTwo = 0.0;
    for (int n = 0; n < N; ++n) {
      energyOne += (double)one[(size_t)n] * one[(size_t)n];
      energyTwo += (double)two[(size_t)n] * two[(size_t)n];
    }

    // Independent streams add in power, so two voices land near sqrt(2) rather
    // than at exactly double, which is what identical noise would give.
    const double ratio = std::sqrt(energyTwo / energyOne);
    std::printf("  two voices against one: %.3f\n", ratio);
    check(ratio > 1.2 && ratio < 1.7,
          "each voice gets its own noise instead of layering the same signal");
  }
}

void testModulation() {
  section("Pitch and amplitude modulation");

  constexpr double sr = 48000.0;
  constexpr int N = 48000;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.0f);
  p.osc[0].volume = 0.5f;
  p.osc[0].amRateHz = 5.0f;
  p.osc[0].amDepth = 1.0f;

  engine.noteOn(57, 1.0f, p);
  std::vector<float> l((size_t)N), r((size_t)N);
  engine.render(l.data(), r.data(), N, p);

  // A 5 Hz tremolo puts sidebands 5 Hz either side of the carrier.
  const double carrier = binMagnitude(l, 220.0, sr);
  const double sideband = binMagnitude(l, 225.0, sr);
  check(sideband > 0.1 * carrier, "tremolo produces sidebands");

  // Pitch modulation should smear the carrier bin.
  auto p2 = makeFlatParams(0.0f);
  p2.osc[0].volume = 0.5f;
  p2.osc[0].pmRateHz = 5.0f;
  p2.osc[0].pmDepthCents = 100.0f;

  SynthEngine engine2;
  engine2.prepare(sr);
  engine2.noteOn(57, 1.0f, p2);
  std::vector<float> l2((size_t)N), r2((size_t)N);
  engine2.render(l2.data(), r2.data(), N, p2);

  const double clean = binMagnitude(l, 220.0, sr);
  const double vibed = binMagnitude(l2, 220.0, sr);
  check(vibed < clean, "vibrato spreads energy out of the carrier bin");

  bool finite = true;
  for (int n = 0; n < N; ++n)
    finite &= std::isfinite(l2[(size_t)n]);
  check(finite, "modulated output is finite");
}

// -----------------------------------------------------------------------------
// 10. The master effects.
// -----------------------------------------------------------------------------
namespace {

struct Stereo {
  std::vector<float> l, r;

  explicit Stereo(size_t n) : l(n, 0.0f), r(n, 0.0f) {}
  size_t size() const { return l.size(); }
};

/// Runs an effect over a whole signal in blocks, the way a host would.
template <typename Fx, typename P>
void runBlocks(Fx &fx, Stereo &s, const P &p, int block = 256) {
  for (size_t start = 0; start < s.size(); start += (size_t)block) {
    const auto n = (int)std::min((size_t)block, s.size() - start);
    fx.process(s.l.data() + start, s.r.data() + start, n, p);
  }
}

double rms(const std::vector<float> &x, size_t from, size_t to) {
  to = std::min(to, x.size());
  if (to <= from)
    return 0.0;

  double sum = 0.0;
  for (size_t n = from; n < to; ++n)
    sum += (double)x[n] * (double)x[n];

  return std::sqrt(sum / (double)(to - from));
}

double peak(const std::vector<float> &x, size_t from, size_t to) {
  to = std::min(to, x.size());
  double m = 0.0;

  for (size_t n = from; n < to; ++n)
    m = std::max(m, (double)std::abs(x[n]));

  return m;
}

bool allFinite(const Stereo &s) {
  for (size_t n = 0; n < s.size(); ++n)
    if (!std::isfinite(s.l[n]) || !std::isfinite(s.r[n]))
      return false;

  return true;
}

/// Frequency from zero crossings. Coarse, but a delay line wandering by a
/// percent is a big effect and this measures it without any windowing games.
double crossingFrequency(const std::vector<float> &x, size_t from, size_t to,
                         double sampleRate) {
  to = std::min(to, x.size());
  int crossings = 0;
  size_t first = 0, last = 0;

  for (size_t n = from + 1; n < to; ++n) {
    if (x[n - 1] <= 0.0f && x[n] > 0.0f) {
      if (crossings == 0)
        first = n;

      last = n;
      ++crossings;
    }
  }

  if (crossings < 2)
    return 0.0;

  return (double)(crossings - 1) * sampleRate / (double)(last - first);
}

/// A slice of copies-of-the-input, for feeding an effect.
Stereo impulse(size_t length, float amplitude = 1.0f) {
  Stereo s(length);
  s.l[0] = amplitude;
  s.r[0] = amplitude;
  return s;
}

Stereo tone(size_t length, double freq, double sampleRate,
            float amplitude = 0.25f) {
  Stereo s(length);

  for (size_t n = 0; n < length; ++n) {
    const auto v = (float)(amplitude * std::sin(6.283185307179586 * freq *
                                                (double)n / sampleRate));
    s.l[n] = v;
    s.r[n] = v;
  }

  return s;
}

} // namespace

/// The per-channel outputs.
///
/// Mono, dry, and taken before the master effects, so a tap is that channel
/// and nothing else. The things worth pinning are that a tap carries only its
/// own channel, that it holds up at the same rate as the mix when the
/// converter is reducing, and that asking for none costs nothing.
void testChannelTaps() {
  section("Channel taps");

  constexpr double sr = 48000.0;
  constexpr int block = 512;

  SynthEngine engine;
  engine.prepare(sr);

  auto p = makeFlatParams(0.0f);
  for (int i = 0; i < kNumHarmonics; ++i) {
    p.osc[(size_t)i].tuneBlend = 1.0f;
    // Two partials only, at different levels, so a tap that picked up its
    // neighbour would be obvious.
    p.osc[(size_t)i].volume = i == 0 ? 0.8f : (i == 3 ? 0.4f : 0.0f);
  }

  std::vector<float> l((size_t)block), r((size_t)block);
  std::array<std::vector<float>, kNumHarmonics + 1> tapBuf;
  ChannelTaps taps;

  for (int i = 0; i < kNumHarmonics + 1; ++i) {
    tapBuf[(size_t)i].assign((size_t)block, 0.0f);
    taps.out[(size_t)i] = tapBuf[(size_t)i].data();
  }

  const auto peak = [](const std::vector<float> &v) {
    float m = 0.0f;
    for (auto s : v)
      m = std::max(m, std::abs(s));
    return m;
  };

  engine.noteOn(45, 1.0f, p);
  for (int b = 0; b < 20; ++b)
    engine.render(l.data(), r.data(), taps, block, p);

  check(peak(tapBuf[0]) > 1.0e-4f, "the first partial's tap carries it");
  check(peak(tapBuf[3]) > 1.0e-4f, "and so does the fourth's");

  // Level follows the fader, so a tap is the channel rather than a copy of
  // the mix. The fourth partial is half the first.
  const auto ratio = peak(tapBuf[3]) / peak(tapBuf[0]);
  check(ratio > 0.4f && ratio < 0.6f,
        "each tap is at its own channel's level (" + std::to_string(ratio) +
            ")");

  // Everything else asked for nothing to be played on it.
  bool othersSilent = true;
  for (int i = 0; i < kNumHarmonics; ++i)
    if (i != 0 && i != 3)
      othersSilent &= peak(tapBuf[(size_t)i]) < 1.0e-6f;

  check(othersSilent, "a silent channel's tap is silent");

  // Dry: the tap has no pan on it, so a hard-panned partial still comes out
  // whole rather than at the level its side of the mix gets.
  p.osc[0].pan = -1.0f;
  engine.allNotesOff();
  engine.noteOn(45, 1.0f, p);
  for (int b = 0; b < 20; ++b)
    engine.render(l.data(), r.data(), taps, block, p);

  const auto panned = peak(tapBuf[0]);
  p.osc[0].pan = 0.0f;
  engine.allNotesOff();
  engine.noteOn(45, 1.0f, p);
  for (int b = 0; b < 20; ++b)
    engine.render(l.data(), r.data(), taps, block, p);

  const auto centred = peak(tapBuf[0]);
  check(std::abs(panned - centred) < centred * 0.02f,
        "pan does not reach the tap (" + std::to_string(panned) + " vs " +
            std::to_string(centred) + ")");

  // Under the converter the pool renders slowly and the mix is held between
  // frames. A tap has to be held the same way or it would run at a rate of
  // its own.
  p.lofi.rateHz = 8000.0;
  engine.allNotesOff();
  engine.noteOn(45, 1.0f, p);
  for (int b = 0; b < 20; ++b)
    engine.render(l.data(), r.data(), taps, block, p);

  check(peak(tapBuf[0]) > 1.0e-4f, "a tap still sounds with the rate reduced");

  int mixSteps = 0, tapSteps = 0;
  for (int n = 1; n < block; ++n) {
    if (l[(size_t)n] != l[(size_t)n - 1])
      ++mixSteps;
    if (tapBuf[0][(size_t)n] != tapBuf[0][(size_t)n - 1])
      ++tapSteps;
  }

  check(mixSteps > 0 && std::abs(mixSteps - tapSteps) <= 2,
        "and holds at the same rate as the mix (" + std::to_string(mixSteps) +
            " against " + std::to_string(tapSteps) + ")");
}

void testTapeEcho() {
  section("Tape echo");

  constexpr double sr = 48000.0;

  TapeEcho echo;
  echo.prepare(sr);

  EchoParams p;
  p.enabled = false;
  p.mix = 1.0f;

  {
    auto s = tone(4800, 440.0, sr);
    const auto before = s.l;
    runBlocks(echo, s, p);

    bool identical = true;
    for (size_t n = 0; n < s.size(); ++n)
      identical &= s.l[n] == before[n];

    check(identical, "switched off, the echo passes the signal untouched");
  }

  // ---- where the repeats land ----------------------------------------------
  p.enabled = true;
  p.timeSeconds = 0.25f;
  p.feedback = 0.6f;
  p.age = 0.0f;

  echo.reset();

  {
    auto s = impulse((size_t)(sr * 1.2));
    runBlocks(echo, s, p);

    const auto expected = (size_t)(0.25 * sr);
    size_t found = 0;
    double best = 0.0;

    for (size_t n = 10; n < (size_t)(0.4 * sr); ++n) {
      if (std::abs(s.l[n]) > best) {
        best = std::abs(s.l[n]);
        found = n;
      }
    }

    const auto errorMs =
        std::abs((double)found - (double)expected) * 1000.0 / sr;

    std::printf("  first repeat at %.1f ms, wanted %.1f ms\n",
                (double)found * 1000.0 / sr, 250.0);

    check(errorMs < 2.0, "the first repeat lands at the head distance");
    check(best > 0.5, "the first repeat carries the signal at full level");

    const auto first = peak(s.l, (size_t)(0.2 * sr), (size_t)(0.3 * sr));
    const auto second = peak(s.l, (size_t)(0.45 * sr), (size_t)(0.55 * sr));
    const auto third = peak(s.l, (size_t)(0.7 * sr), (size_t)(0.8 * sr));

    check(second < first && third < second, "the repeats fall away");
    check(second > 0.2 * first, "feedback at 60% is still clearly audible");
  }

  // ---- the loop must not run away, however new the machine is --------------
  {
    echo.reset();

    p.feedback = 0.95f;
    p.mix = 1.0f;
    p.timeSeconds = 0.12f;
    p.age = 0.0f; // no character compression, so only the backstop is left

    auto s = tone((size_t)(sr * 20.0), 220.0, sr, 0.7f);
    runBlocks(echo, s, p);

    check(allFinite(s), "the loop stays finite at maximum feedback");
    check(peak(s.l, 0, s.size()) < 2.0,
          "the loop stays bounded at maximum feedback (peak " +
              std::to_string(peak(s.l, 0, s.size())) + ")");
  }

  // ---- age: the top end it hands back --------------------------------------
  {
    const auto measure = [&](float age) {
      echo.reset();

      EchoParams e;
      e.enabled = true;
      e.mix = 1.0f;
      e.timeSeconds = 0.1f;
      e.feedback = 0.8f;
      e.age = age;

      // A click carries every frequency, so what comes back says what the loop
      // did to the top end.
      auto s = impulse((size_t)(sr * 2.0));
      runBlocks(echo, s, e);

      std::vector<float> tail(s.l.begin() + (size_t)(sr * 0.9), s.l.end());

      return bandMagnitude(tail, 4000.0, 9000.0, sr) /
             std::max(1.0e-9, bandMagnitude(tail, 200.0, 600.0, sr));
    };

    const auto worn = measure(1.0f);
    const auto fresh = measure(0.0f);

    std::printf("  high to low after ~10 passes: worn %.4f, new %.4f\n", worn,
                fresh);

    check(fresh > worn * 4.0,
          "a worn machine loses the top end and a new one keeps it");
  }

  // ---- age: how steady the motor is ----------------------------------------
  {
    const auto wander = [&](float age) {
      echo.reset();

      EchoParams e;
      e.enabled = true;
      e.mix = 1.0f;
      e.timeSeconds = 0.5f;
      e.feedback = 0.0f;
      e.age = age;

      auto s = tone((size_t)(sr * 4.0), 1000.0, sr);
      runBlocks(echo, s, e);

      double lowest = 1.0e9, highest = 0.0;

      for (int w = 0; w < 12; ++w) {
        const auto from = (size_t)(sr * (1.0 + 0.2 * w));
        const auto f =
            crossingFrequency(s.l, from, from + (size_t)(sr * 0.2), sr);

        lowest = std::min(lowest, f);
        highest = std::max(highest, f);
      }

      return (highest - lowest) / 1000.0;
    };

    const auto fresh = wander(0.0f);
    const auto worn = wander(1.0f);

    std::printf("  pitch swing of the repeats: new %.4f%%, worn %.2f%%\n",
                fresh * 100.0, worn * 100.0);

    check(fresh < 0.002, "a new machine holds the pitch of its repeats");
    check(worn > 0.004, "a worn one wanders audibly");
  }

  // ---- age: how hard the tape leans over -----------------------------------
  {
    const auto compression = [&](float age) {
      echo.reset();

      EchoParams e;
      e.enabled = true;
      e.mix = 1.0f;
      e.timeSeconds = 0.15f;
      e.feedback = 0.85f;
      e.age = age;

      // Loud enough that a worn machine has something to lean on.
      auto s = impulse((size_t)(sr * 1.2), 0.95f);
      runBlocks(echo, s, e);

      const auto first = peak(s.l, (size_t)(0.1 * sr), (size_t)(0.2 * sr));
      const auto second = peak(s.l, (size_t)(0.25 * sr), (size_t)(0.35 * sr));

      return second / std::max(1.0e-9, first);
    };

    const auto fresh = compression(0.0f);
    const auto worn = compression(1.0f);

    std::printf("  second repeat against the first: new %.3f, worn %.3f\n",
                fresh, worn);

    check(fresh > worn * 1.05,
          "a worn machine compresses what goes round and a new one does not");
  }

  // ---- two tape paths rather than one bouncing across ----------------------
  //
  // A centred source is the case that matters, since almost everything this
  // thing is fed will be. Two independent motors have to pull the two repeats
  // apart even though the same signal went into both.
  {
    EchoParams e;
    e.enabled = true;
    e.mix = 1.0f;
    e.timeSeconds = 0.2f;
    e.feedback = 0.7f;

    // A worn machine: the wander follows AGE, so this is where the doubling
    // lives.
    e.age = 0.8f;

    echo.reset();
    auto worn = impulse((size_t)(sr * 2.0));
    runBlocks(echo, worn, e);

    // How far apart the two sides have drifted, against how loud they are.
    const auto from = (size_t)(0.15 * sr), to = worn.l.size();

    double difference = 0.0, loudest = 0.0;
    for (auto n = from; n < to; ++n) {
      difference =
          std::max(difference, std::abs((double)worn.l[n] - worn.r[n]));
      loudest = std::max(loudest, std::abs((double)worn.l[n]));
    }

    std::printf("  worn, centred source: sides differ by %.3f against a %.3f "
                "signal\n",
                difference, loudest);

    check(difference > 0.3 * loudest,
          "two motors pull the repeats apart on a centred source (" +
              std::to_string(difference / std::max(1.0e-9, loudest)) + ")");

    // How far apart the two sides have come, on a sustained tone where the
    // repeats overlap. An impulse cannot measure this: any timing difference
    // at all makes two sharp repeats miss each other completely, so it reads
    // the same at two percent of wear as at a hundred.
    const auto correlationAt = [&](float wear) {
      echo.reset();

      EchoParams tone = e;
      tone.age = wear;

      Stereo s((size_t)(sr * 3.0));
      for (size_t n = 0; n < s.l.size(); ++n)
        s.l[n] = s.r[n] =
            0.4f * std::sin(6.2831853 * 440.0 * (double)n / sr);

      runBlocks(echo, s, tone);

      double ll = 0.0, rr = 0.0, lr = 0.0;
      for (auto n = (size_t)(2.0 * sr); n < s.l.size(); ++n) {
        ll += (double)s.l[n] * s.l[n];
        rr += (double)s.r[n] * s.r[n];
        lr += (double)s.l[n] * s.r[n];
      }

      return lr / std::max(1.0e-12, std::sqrt(ll * rr));
    };

    // Zero wear means a transport that holds speed exactly, so the two paths
    // wander by the same nothing and the repeat is mono. That is the DSP's
    // contract, and it is why the panel is not allowed to ask for it.
    check(std::abs(correlationAt(0.0f) - 1.0) < 1.0e-6,
          "a perfect transport would put the repeat back in mono");

    const auto atFloor = correlationAt(TapeEcho::kMinAge);

    // The floor is where the knob reads 0, so these are the two ends of the
    // travel plus a point part way up. The readme quotes them.
    const auto partWayUp =
        TapeEcho::kMinAge + 0.13f * (1.0f - TapeEcho::kMinAge);

    std::printf("  channel correlation: %.3f at the bottom of the AGE knob, "
                "%.3f at 13 %%, %.3f at the top\n",
                atFloor, correlationAt(partWayUp), correlationAt(1.0f));

    // Which is the reason for the floor: the least worn setting available has
    // to be doubled already.
    check(atFloor < 0.9,
          "the lowest AGE the panel allows is already doubled (" +
              std::to_string(atFloor) + ")");

    check(atFloor > 0.4,
          "but not so far that the floor is a chorus (" +
              std::to_string(atFloor) + ")");
  }

  // ---- each side stays on its side ------------------------------------------
  //
  // No crossfeed anywhere, so a repeat comes back where it went out and the
  // panning the mixer dialled in survives into the echo.
  {
    EchoParams e;
    e.enabled = true;
    e.mix = 1.0f;
    e.timeSeconds = 0.2f;
    e.feedback = 0.7f;
    e.age = 0.6f;

    echo.reset();

    Stereo s((size_t)(sr * 1.2));
    s.l[100] = 1.0f; // left only, right silent

    runBlocks(echo, s, e);

    const auto from = (size_t)(0.15 * sr);
    const auto leftEcho = peak(s.l, from, s.l.size());
    const auto rightEcho = peak(s.r, from, s.r.size());

    std::printf("  left-only source: repeats L %.3f R %.3f\n", leftEcho,
                rightEcho);

    check(leftEcho > 0.1, "a repeat comes back on the side it went out on");

    check(rightEcho < 1.0e-6,
          "and nothing crosses to the other (" + std::to_string(rightEcho) +
              ")");
  }
}

/// The warped record under the whole instrument.
void testWobble() {
  section("Wobble");

  constexpr double sr = 48000.0;
  constexpr double f0 = 440.0;

  // Cycle-by-cycle pitch of a steady tone put through it, which is the thing
  // the control is for.
  struct Bend {
    double worst = 0.0, typical = 0.0;
  };

  const auto bendAt = [&](float amount) {
    Wobble w;
    w.prepare(sr);

    const auto n = (size_t)(sr * 12.0);
    std::vector<float> l(n), r(n);

    for (size_t i = 0; i < n; ++i)
      l[i] = r[i] = (float)std::sin(6.283185307179586 * f0 * (double)i / sr);

    for (size_t at = 0; at < n; at += 256)
      w.process(l.data() + at, r.data() + at,
                (int)std::min<size_t>(256, n - at), amount);

    std::vector<double> crossings;
    for (size_t i = (size_t)sr + 1; i < n; ++i)
      if (l[i - 1] <= 0.0f && l[i] > 0.0f) {
        const auto frac = -l[i - 1] / (l[i] - l[i - 1]);
        crossings.push_back(((double)(i - 1) + frac) / sr);
      }

    Bend b;
    double sum = 0.0;

    for (size_t k = 1; k < crossings.size(); ++k) {
      const auto cents =
          1200.0 * std::log2(1.0 / (crossings[k] - crossings[k - 1]) / f0);

      b.worst = std::max(b.worst, std::abs(cents));
      sum += std::abs(cents);
    }

    b.typical =
        crossings.size() > 1 ? sum / (double)(crossings.size() - 1) : 0.0;
    return b;
  };

  // Bit for bit, rather than by measuring the pitch: reading a sampled sine's
  // period back has a jitter of its own, around a thousandth of a cent, which
  // would be the only thing this saw.
  {
    Wobble w;
    w.prepare(sr);

    std::vector<float> l(4096), r(4096), wasL(4096);
    for (size_t i = 0; i < l.size(); ++i)
      l[i] = r[i] = wasL[i] =
          (float)std::sin(6.283185307179586 * f0 * (double)i / sr);

    w.process(l.data(), r.data(), (int)l.size(), 0.0f);

    bool untouched = true;
    for (size_t i = 0; i < l.size(); ++i)
      untouched &= l[i] == wasL[i];

    check(untouched, "at zero it passes the signal through untouched");
  }

  const auto low = bendAt(0.25f);
  const auto high = bendAt(1.0f);

  std::printf("  pitch bend: %.0f ct worst and %.0f ct typical at a quarter, "
              "%.0f and %.0f at full\n",
              low.worst, low.typical, high.worst, high.typical);

  check(low.typical > 1.0 && low.typical < 15.0,
        "a quarter turn is a wander rather than a warble (" +
            std::to_string(low.typical) + " cents)");

  check(high.typical > 2.0 * low.typical,
        "and turning it up bends further (" + std::to_string(high.typical) +
            " against " + std::to_string(low.typical) + ")");

  // The slips are the point of the top of the knob: the worst moment has to be
  // far worse than the average one, or it is only a vibrato.
  check(high.worst > 4.0 * high.typical,
        "the slips are much larger than the wander they sit on (" +
            std::to_string(high.worst / std::max(1.0e-9, high.typical)) +
            " times)");

  // One platter, so a centred source stays centred. This is what separates it
  // from the echo, where the two sides are meant to disagree.
  {
    Wobble w;
    w.prepare(sr);

    const auto n = (size_t)(sr * 4.0);
    std::vector<float> l(n), r(n);

    for (size_t i = 0; i < n; ++i)
      l[i] = r[i] = (float)std::sin(6.283185307179586 * 220.0 * (double)i / sr);

    for (size_t at = 0; at < n; at += 256)
      w.process(l.data() + at, r.data() + at,
                (int)std::min<size_t>(256, n - at), 1.0f);

    double apart = 0.0;
    for (size_t i = 0; i < n; ++i)
      apart = std::max(apart, std::abs((double)l[i] - r[i]));

    check(apart < 1.0e-6,
          "both channels bend together, since it is one platter (" +
              std::to_string(apart) + ")");
  }

  // Turning it up from nothing must not step, since the delay it introduces
  // grows with the control rather than being switched in.
  {
    Wobble w;
    w.prepare(sr);

    const auto n = (size_t)(sr * 2.0);
    std::vector<float> l(n), r(n);

    for (size_t i = 0; i < n; ++i)
      l[i] = r[i] = (float)std::sin(6.283185307179586 * 110.0 * (double)i / sr);

    // Ramped on over a second, the way a hand moves a knob.
    for (size_t at = 0; at < n; at += 64) {
      const auto len = (int)std::min<size_t>(64, n - at);
      const auto amount = std::min(1.0f, (float)at / (float)(sr * 1.0));

      w.process(l.data() + at, r.data() + at, len, amount);
    }

    double step = 0.0;
    for (size_t i = 1; i < n; ++i)
      step = std::max(step, std::abs((double)l[i] - l[i - 1]));

    // The waveform's own steepest slope, for comparison.
    const auto perSample = 6.283185307179586 * 110.0 / sr;

    std::printf("  ramping it on: biggest sample step %.5f against %.5f for "
                "the waveform\n",
                step, perSample);

    check(step < 3.0 * perSample,
          "and coming up from zero puts no step in the output");
  }
}

void testReverb() {
  section("Reverb");

  constexpr double sr = 48000.0;

  Reverb reverb;
  reverb.prepare(sr);

  ReverbParams p;
  p.enabled = false;

  {
    auto s = tone(4800, 440.0, sr);
    const auto before = s.l;
    runBlocks(reverb, s, p);

    bool identical = true;
    for (size_t n = 0; n < s.size(); ++n)
      identical &= s.l[n] == before[n];

    check(identical, "switched off, the reverb passes the signal untouched");
  }

  // ---- decay time
  // ------------------------------------------------------------
  const auto measureDecay = [&](float decaySeconds) {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = decaySeconds;
    r.damping = 0.0f;
    r.preDelaySeconds = 0.0f;

    auto s = impulse((size_t)(sr * 25.0));
    runBlocks(reverb, s, r);

    if (!allFinite(s))
      return -1.0;

    // Reference taken just after the input has scattered, then the point where
    // the tail has fallen 60 dB below it.
    const auto reference = rms(s.l, (size_t)(sr * 0.05), (size_t)(sr * 0.15));
    const auto floorLevel = reference * 0.001;

    for (double t = 0.15; t < 24.0; t += 0.05) {
      const auto from = (size_t)(sr * t);
      if (rms(s.l, from, from + (size_t)(sr * 0.05)) < floorLevel)
        return t;
    }

    return 24.0;
  };

  const auto short_ = measureDecay(1.0f);
  const auto long_ = measureDecay(5.0f);

  std::printf("  measured decay: %.2f s at RT60 1 s, %.2f s at RT60 5 s\n",
              short_, long_);

  check(short_ > 0.5 && short_ < 2.0, "a 1 s decay falls silent in about 1 s");
  check(long_ > 3.0 && long_ < 9.0, "a 5 s decay falls silent in about 5 s");
  check(long_ > short_ * 2.5, "the decay control does what it says");

  // ---- the tail has to be dense, not a pitch --------------------------------
  {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = 3.0f;
    r.damping = 0.2f;

    auto s = impulse((size_t)(sr * 3.0));
    runBlocks(reverb, s, r);

    std::vector<float> tail(s.l.begin() + (size_t)(sr * 0.5),
                            s.l.begin() + (size_t)(sr * 1.5));

    // A network that has settled onto a resonance puts everything into one
    // place in the spectrum. A dense one does not.
    double loudest = 0.0, total = 0.0;
    constexpr int bins = 60;

    for (int k = 0; k < bins; ++k) {
      const auto f = 200.0 + 100.0 * k;
      const auto m = binMagnitude(tail, f, sr);

      loudest = std::max(loudest, m);
      total += m;
    }

    const auto ratio = loudest / (total / bins);
    std::printf("  loudest bin is %.2fx the average of the tail spectrum\n",
                ratio);

    check(ratio < 6.0, "the tail is broadband rather than a ringing pitch");
  }

  // ---- pre-delay
  // -------------------------------------------------------------
  {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = 2.0f;
    r.damping = 0.3f;
    r.preDelaySeconds = 0.1f;

    auto s = impulse((size_t)(sr * 1.0));
    runBlocks(reverb, s, r);

    check(peak(s.l, 2, (size_t)(sr * 0.09)) < 1.0e-6,
          "nothing comes back before the pre-delay is up");
    check(peak(s.l, (size_t)(sr * 0.1), (size_t)(sr * 0.3)) > 1.0e-4,
          "the tail arrives after it");
  }

  // ---- damping and low cut
  // ---------------------------------------------------
  {
    const auto tailBand = [&](float damping, double lo, double hi) {
      reverb.reset();

      ReverbParams r;
      r.enabled = true;
      r.mix = 1.0f;
      r.decaySeconds = 3.0f;
      r.damping = damping;

      auto s = impulse((size_t)(sr * 2.0));
      runBlocks(reverb, s, r);

      std::vector<float> tail(s.l.begin() + (size_t)(sr * 0.5), s.l.end());
      return bandMagnitude(tail, lo, hi, sr);
    };

    const auto open = tailBand(0.0f, 5000.0, 10000.0);
    const auto damped = tailBand(1.0f, 5000.0, 10000.0);

    std::printf("  top end in the tail: open %.2e, damped %.2e\n", open,
                damped);
    check(damped < open * 0.5, "damping takes the top off the tail");

    // The low cut is fixed at 175 Hz now, so what reaches the tail below it
    // has to be well down on what reaches it above.
    const auto below = tailBand(0.3f, 60.0, 110.0);
    const auto above = tailBand(0.3f, 400.0, 800.0);

    std::printf("  tail below the cut %.2e, above it %.2e\n", below, above);
    check(below < above * 0.5,
          "the fixed low cut keeps the fundamental out of the tail");
  }

  // ---- spread
  // -----------------------------------------------------------------
  //
  // There is no control over this. The two sides are drawn from different
  // lines in different polarities, so the tail is wide because of how it is
  // built, and the only thing worth checking is that it still is. A mono input
  // is the hard case: if the network were symmetric this would come back
  // perfectly correlated.
  {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = 2.0f;
    r.damping = 0.3f;

    auto s = impulse((size_t)(sr * 1.5));
    runBlocks(reverb, s, r);

    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (size_t n = (size_t)(sr * 0.1); n < s.size(); ++n) {
      ll += (double)s.l[n] * s.l[n];
      rr += (double)s.r[n] * s.r[n];
      lr += (double)s.l[n] * s.r[n];
    }

    const auto correlation = lr / std::sqrt(std::max(1.0e-12, ll * rr));

    std::printf("  channel correlation %.3f\n", correlation);

    check(correlation < 0.6, "the two sides of the tail are largely "
                             "independent from a mono input");
  }

  // ---- level
  // -----------------------------------------------------------------
  {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = 2.0f;
    r.damping = 0.4f;

    // Broadband input, so this is a fair comparison rather than one frequency
    // landing on a mode.
    Xorshift rng(7u);
    Stereo s((size_t)(sr * 4.0));
    for (size_t n = 0; n < s.size(); ++n) {
      s.l[n] = 0.2f * rng.bipolar();
      s.r[n] = 0.2f * rng.bipolar();
    }

    const auto dry = rms(s.l, (size_t)(sr * 1.0), (size_t)(sr * 3.0));
    runBlocks(reverb, s, r);
    const auto wet = rms(s.l, (size_t)(sr * 1.0), (size_t)(sr * 3.0));

    std::printf("  fully wet is %.2fx the dry level it replaced\n", wet / dry);

    check(wet / dry > 0.4 && wet / dry < 2.0,
          "turning the mix up does not change how loud the thing is");
  }

  // ---- stability -----------------------------------------------------------
  // A 30 s reverb fed a continuous tone builds up, and should: that is what a
  // long tail does. What must not happen is that it keeps building, so the
  // test is not a peak but a direction. Feed it, stop, and watch it fall.
  {
    reverb.reset();

    ReverbParams r;
    r.enabled = true;
    r.mix = 1.0f;
    r.decaySeconds = 30.0f;
    r.damping = 0.0f;

    Stereo s((size_t)(sr * 60.0));

    for (size_t n = 0; n < (size_t)(sr * 30.0); ++n) {
      const auto v =
          (float)(0.5 * std::sin(6.283185307179586 * 300.0 * (double)n / sr));
      s.l[n] = v;
      s.r[n] = v;
    }

    runBlocks(reverb, s, r);

    const auto driven = rms(s.l, (size_t)(sr * 28.0), (size_t)(sr * 30.0));
    const auto justAfter = rms(s.l, (size_t)(sr * 31.0), (size_t)(sr * 33.0));
    const auto muchLater = rms(s.l, (size_t)(sr * 55.0), (size_t)(sr * 57.0));

    std::printf("  driven %.3f, a second later %.3f, half a minute later "
                "%.4f\n",
                driven, justAfter, muchLater);

    check(allFinite(s), "the network stays finite at the longest decay");
    check(peak(s.l, 0, s.size()) < 20.0,
          "and bounded (peak " + std::to_string(peak(s.l, 0, s.size())) + ")");

    check(justAfter < driven, "it starts falling the moment the input stops");
    check(muchLater < justAfter * 0.03,
          "and is 30 dB further down half a minute after that");
  }
}

// -----------------------------------------------------------------------------
// 11. CPU: the whole point of the exercise.
// -----------------------------------------------------------------------------
void benchmark() {
  section("CPU benchmark");

  constexpr double sr = 48000.0;
  constexpr int block = 128;
  constexpr double secs = 10.0;

  for (int poly : {1, 8, 16}) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(poly);

    auto p = makeFlatParams(0.02f);
    for (auto &o : p.osc) {
      o.sustain = 1.0f;
      o.amDepth = 0.3f; // worst case: every modulator running
      o.pmDepthCents = 5.0f;
      o.driftCents = 8.0f;
      o.velAmount = 0.5f;
      o.atAmount = 0.5f;
      o.tuneBlend = 0.5f;
    }

    // The noise channel is part of the worst case, so it runs too.
    p.noise.volume = 0.3f;
    p.noise.colour = 0.35f;
    p.noise.amDepth = 0.2f;
    p.noise.sustain = 1.0f;
    p.noise.audible = true;

    for (int v = 0; v < poly; ++v)
      engine.noteOn(48 + v, 1.0f, p);

    std::vector<float> l((size_t)block), r((size_t)block);
    const int blocks = (int)(secs * sr / block);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
      engine.render(l.data(), r.data(), block, p);
    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double load = 100.0 * elapsed / secs;

    std::printf("  %2d voices (%3d partials): %.3f s for %.0f s of audio  ->  "
                "%.2f%% of one core\n",
                poly, poly * kNumHarmonics, elapsed, secs, load);

    check(engine.getActiveVoiceCount() == poly,
          "all benchmark voices sounding");
  }

  // What the master effects cost on top, measured against the same patch with
  // them switched off rather than guessed at.
  for (int withEffects = 0; withEffects < 2; ++withEffects) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(8);

    auto p = makeFlatParams(0.02f);
    for (auto &o : p.osc)
      o.sustain = 1.0f;

    p.echo.enabled = withEffects != 0;
    p.echo.mix = 0.3f;
    p.echo.feedback = 0.6f;
    p.echo.age = 0.5f;

    p.reverb.enabled = withEffects != 0;
    p.reverb.mix = 0.3f;
    p.reverb.decaySeconds = 4.0f;

    for (int v = 0; v < 8; ++v)
      engine.noteOn(48 + v, 1.0f, p);

    std::vector<float> l((size_t)block), r((size_t)block);
    const int blocks = (int)(secs * sr / block);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
      engine.render(l.data(), r.data(), block, p);
    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::printf("   8 voices, effects %s: %.2f%% of one core\n",
                withEffects ? "on " : "off", 100.0 * elapsed / secs);
  }
}

// -----------------------------------------------------------------------------
// The lo-fi converter: a rate that genuinely reduces work, and a quantiser.
// -----------------------------------------------------------------------------
void testLofi() {
  section("Lo-fi converter");

  constexpr double sr = 48000.0;
  constexpr int N = 12000;

  // sr is captured rather than left to the constexpr rule. Using a constexpr
  // as a plain value does not odr-use it, so no capture is needed and GCC and
  // Clang both accept it, but MSVC asks for one regardless and the build only
  // finds out on Windows.
  const auto renderNote = [sr](const SynthParams &p, int samples,
                               std::vector<float> &l, std::vector<float> &r) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(8);

    auto sounding = p;
    engine.noteOn(57, 1.0f, sounding); // A3

    l.assign((size_t)samples, 0.0f);
    r.assign((size_t)samples, 0.0f);
    engine.render(l.data(), r.data(), samples, sounding);
  };

  auto base = makeFlatParams(0.02f);
  for (auto &o : base.osc)
    o.sustain = 1.0f;

  // ---- off by default -------------------------------------------------------
  std::vector<float> plainL, plainR;
  renderNote(base, N, plainL, plainR);

  int plainRuns = 0;
  for (int n = 1; n < N; ++n)
    if (plainL[(size_t)n] == plainL[(size_t)n - 1])
      ++plainRuns;

  check(base.lofi.rateHz == 0.0 && base.lofi.bits == 0,
        "the converter defaults to the host's own rate and depth");

  check(plainRuns * 100 < N,
        "and leaves the output as a continuous signal rather than a staircase");

  // ---- rate reduction holds samples ----------------------------------------
  auto reduced = base;
  reduced.lofi.rateHz = 8000.0;

  std::vector<float> heldL, heldR;
  renderNote(reduced, N, heldL, heldR);

  // 8 kHz into 48 kHz is one fresh sample in six, so five in six repeat.
  int repeats = 0;
  for (int n = 1; n < N; ++n)
    if (heldL[(size_t)n] == heldL[(size_t)n - 1])
      ++repeats;

  const double heldFraction = (double)repeats / (double)(N - 1);

  check(heldFraction > 0.79 && heldFraction < 0.87,
        "8 kHz into a 48 kHz host holds five samples in six (" +
            std::to_string(heldFraction) + ")");

  // Runs must be the same length throughout rather than drifting, which is
  // what a resampler phase that restarts every block would produce.
  int longest = 1, run = 1;
  for (int n = 1; n < N; ++n) {
    run = heldL[(size_t)n] == heldL[(size_t)n - 1] ? run + 1 : 1;
    longest = std::max(longest, run);
  }

  check(longest <= 6, "and never holds longer than the ratio calls for (" +
                          std::to_string(longest) + ")");

  bool finite = true;
  float peak = 0.0f;
  for (int n = 0; n < N; ++n) {
    finite &= std::isfinite(heldL[(size_t)n]);
    peak = std::max(peak, std::abs(heldL[(size_t)n]));
  }

  check(finite && peak > 0.05f,
        "the reduced-rate output is finite and audible (peak " +
            std::to_string(peak) + ")");

  // ---- partials fold rather than being culled -------------------------------
  //
  // The 32nd partial of A3 sits at 7040 Hz, well above the 4 kHz ceiling an
  // 8 kHz converter has. A Nyquist guard would silence it. A converter wraps
  // it back down to 960 Hz, and that is the sound being asked for.
  auto single = makeFlatParams(0.0f);
  for (auto &o : single.osc)
    o.sustain = 1.0f;

  single.osc[31].volume = 0.5f;
  single.osc[31].tuneBlend = 1.0f; // exactly 32 * f0

  std::vector<float> cleanL, cleanR, foldedL, foldedR;
  renderNote(single, N, cleanL, cleanR);

  single.lofi.rateHz = 8000.0;
  renderNote(single, N, foldedL, foldedR);

  constexpr double partial = 7040.0;       // 32 * 220
  constexpr double fold = 8000.0 - 7040.0; // 960, where it lands

  const auto cleanAtFold = binMagnitude(cleanL, fold, sr);
  const auto foldedAtFold = binMagnitude(foldedL, fold, sr);

  check(foldedAtFold > 20.0 * cleanAtFold,
        "a partial above the reduced Nyquist folds down instead of being muted "
        "(" +
            std::to_string(foldedAtFold) + " against " +
            std::to_string(cleanAtFold) + " at the host rate)");

  // The original frequency is still faintly there, because holding a sample
  // puts an image either side of the rate. It is the fold that is loud: the
  // hold's own response falls as sin(pi f / rate) / (pi f / rate), which is
  // 0.98 down at 960 Hz and 0.13 at 7040, so about seven to one.
  const auto foldedAtOriginal = binMagnitude(foldedL, partial, sr);

  check(foldedAtFold > 4.0 * foldedAtOriginal,
        "and the fold is what you hear rather than the image above it (" +
            std::to_string(foldedAtFold / foldedAtOriginal) + " to one)");

  // ---- quantiser ------------------------------------------------------------
  for (int bits : {8, 4, 2}) {
    auto crushed = base;
    crushed.lofi.bits = bits;

    std::vector<float> qL, qR;
    renderNote(crushed, N, qL, qR);

    const double step = 1.0 / (double)(1 << (bits - 1));

    bool onGrid = true;
    for (int n = 0; n < N; ++n) {
      const double v = (double)qL[(size_t)n] / step;
      onGrid &= std::abs(v - std::round(v)) < 1.0e-4;
    }

    check(onGrid, "at " + std::to_string(bits) +
                      " bits every sample lands on the quantiser grid");
  }

  // Fewer bits has to mean more error against the same signal, or the setting
  // is not doing what it says.
  double previous = 0.0;
  bool monotonic = true;

  for (int bits : {12, 8, 6, 4}) {
    auto crushed = base;
    crushed.lofi.bits = bits;

    std::vector<float> qL, qR;
    renderNote(crushed, N, qL, qR);

    double error = 0.0;
    for (int n = 0; n < N; ++n)
      error += std::abs((double)qL[(size_t)n] - (double)plainL[(size_t)n]);

    monotonic &= error > previous;
    previous = error;
  }

  check(monotonic,
        "and each step down the list quantises harder than the last");

  // ---- the two settings compose --------------------------------------------
  auto both = base;
  both.lofi.rateHz = 11025.0;
  both.lofi.bits = 6;

  std::vector<float> bothL, bothR;
  renderNote(both, N, bothL, bothR);

  const double bothStep = 1.0 / 32.0;
  bool composed = true;
  int bothRepeats = 0;

  for (int n = 0; n < N; ++n) {
    const double v = (double)bothL[(size_t)n] / bothStep;
    composed &= std::abs(v - std::round(v)) < 1.0e-4;

    if (n > 0 && bothL[(size_t)n] == bothL[(size_t)n - 1])
      ++bothRepeats;
  }

  check(composed && bothRepeats > N / 2,
        "rate and depth together give a held, quantised output");

  // ---- asking for more than the host has is not a thing ---------------------
  auto tooFast = base;
  tooFast.lofi.rateHz = 96000.0;

  std::vector<float> fastL, fastR;
  renderNote(tooFast, N, fastL, fastR);

  int fastRepeats = 0;
  for (int n = 1; n < N; ++n)
    if (fastL[(size_t)n] == fastL[(size_t)n - 1])
      ++fastRepeats;

  check(fastRepeats == plainRuns,
        "a rate above the host's reads as off rather than as an upsample");
}

/// The point of doing the reduction at the source rather than over the top.
void benchmarkLofi() {
  section("Lo-fi cost");

  constexpr double sr = 48000.0;
  constexpr int block = 512;
  constexpr double secs = 4.0;

  double fullRate = 0.0;

  for (int hz : {0, 22050, 11025, 8000}) {
    SynthEngine engine;
    engine.prepare(sr);
    engine.setPolyphony(8);

    auto p = makeFlatParams(0.02f);
    for (auto &o : p.osc)
      o.sustain = 1.0f;

    p.lofi.rateHz = (double)hz;

    for (int v = 0; v < 8; ++v)
      engine.noteOn(48 + v, 1.0f, p);

    std::vector<float> l((size_t)block), r((size_t)block);
    const int blocks = (int)(secs * sr / block);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
      engine.render(l.data(), r.data(), block, p);
    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double load = 100.0 * elapsed / secs;

    if (hz == 0)
      fullRate = load;

    std::printf("  8 voices at %-10s %.2f%% of one core%s\n",
                hz == 0 ? "host rate:" : (std::to_string(hz) + " Hz:").c_str(),
                load,
                hz == 0 ? ""
                        : ("   (" + std::to_string((int)std::lround(
                                        100.0 * load / fullRate)) +
                           "% of full rate)")
                              .c_str());

    if (hz == 8000)
      check(load < fullRate * 0.6,
            "8 kHz costs well under half of what the host rate costs");
  }
}

int main() {
  testTuningTable();
  testTemperaments();
  testBlendEndpoints();
  testStretch();
  testStartPhase();
  testTracking();
  testSineTable();
  testRenderedSpectrum();
  testAliasing();
  testEnvelopeAndMuteSolo();
  testNoClickOnMute();
  testVoiceAllocation();
  testOneVoicePerKey();
  testPerNoteChannels();
  testActivity();
  testPoolExhaustion();
  testModulation();
  testPerPartialVelocity();
  testPanning();
  testAftertouch();
  testDrift();
  testPartialMetering();
  testEnvelopeDelay();
  testKeyOffEnvelope();
  testKeyOffAfterSilentDecay();
  testReleaseVelocity();
  testNoiseChannel();
  testChannelTaps();
  testTapeEcho();
  testWobble();
  testReverb();
  testLofi();
  benchmark();
  benchmarkLofi();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
