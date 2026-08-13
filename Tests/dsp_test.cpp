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

#include "dsp/Harmonics.h"
#include "dsp/SineTable.h"
#include "dsp/SynthEngine.h"
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
  p.global.stereoSpread = 0.0f;
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
  env.configure(0.1f, 0.1f, 0.5f, 0.1f);
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
  perc.configure(0.001f, 0.05f, 0.0f, 0.1f);
  perc.noteOn(true);
  for (int i = 0; i < 1000; ++i)
    perc.tick();

  check(!perc.isActive(), "zero-sustain envelope frees itself");

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
}

// -----------------------------------------------------------------------------
// 11. Stereo spread must be symmetric, balanced, and actually wide where a real
//     spectrum keeps its energy.
// -----------------------------------------------------------------------------
void testStereoSpread() {
  section("Stereo spread");

  constexpr double sr = 48000.0;
  constexpr int N = 24000;
  constexpr double f0 = 110.0; // A2, so all 32 partials stay under Nyquist

  struct Rendered {
    std::vector<float> l, r;
  };

  auto render = [&](float spread, bool rollOff) {
    SynthEngine engine;
    engine.prepare(sr);

    auto p = makeFlatParams(0.02f);
    for (int i = 0; i < kNumHarmonics; ++i) {
      p.osc[(size_t)i].tuneBlend = 1.0f;
      if (rollOff)
        p.osc[(size_t)i].volume = i < 8 ? 0.02f / (float)(i + 1) : 0.0f;
    }
    p.global.stereoSpread = spread;

    engine.noteOn(45, 1.0f, p);
    Rendered out{std::vector<float>((size_t)N), std::vector<float>((size_t)N)};
    engine.render(out.l.data(), out.r.data(), N, p);
    return out;
  };

  auto balances = [&](const Rendered &x) {
    std::vector<double> b;
    for (int n = 1; n <= kNumHarmonics; ++n) {
      const double ml = binMagnitude(x.l, f0 * n, sr);
      const double mr = binMagnitude(x.r, f0 * n, sr);
      b.push_back(ml + mr > 1.0e-12 ? (mr - ml) / (mr + ml) : 0.0);
    }
    return b;
  };

  auto imbalanceDb = [&](const Rendered &x) {
    double sl = 0.0, sr = 0.0;
    for (int n = 0; n < N; ++n) {
      sl += (double)x.l[(size_t)n] * x.l[(size_t)n];
      sr += (double)x.r[(size_t)n] * x.r[(size_t)n];
    }
    return 20.0 * std::log10(std::sqrt(sr / N) / std::sqrt(sl / N));
  };

  // Spread off must be exactly mono.
  {
    const auto mono = render(0.0f, false);
    float worst = 0.0f;
    for (int n = 0; n < N; ++n)
      worst = std::max(worst, std::abs(mono.l[(size_t)n] - mono.r[(size_t)n]));

    check(worst < 1.0e-7f, "spread at zero leaves the channels identical");
  }

  const auto wide = render(1.0f, false);
  auto b = balances(wide);

  // Every placement needs a mirror, or the image leans and the widest partial
  // ends up alone on one side.
  auto sorted = b;
  std::sort(sorted.begin(), sorted.end());

  double worstMirror = 0.0;
  for (int k = 0; k < kNumHarmonics / 2; ++k)
    worstMirror = std::max(
        worstMirror,
        std::abs(sorted[(size_t)k] + sorted[(size_t)(kNumHarmonics - 1 - k)]));

  std::printf("  worst mirror error %.4f\n", worstMirror);
  check(worstMirror < 0.01, "every pan position has an opposite number");

  check(sorted.front() < -0.99, "the field reaches hard left");
  check(sorted.back() > 0.99, "the field reaches hard right");

  const double flatDb = imbalanceDb(wide);
  std::printf("  channel imbalance: %+.2f dB flat", flatDb);
  check(std::abs(flatDb) < 0.2, "a flat spectrum stays centred");

  // The reason the pairing exists: a rolled-off spectrum must stay centred too,
  // and must still be spread where its energy actually lives.
  const auto rolled = render(1.0f, true);
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

  bool anyLeft = false, anyRight = false;
  for (int n = 0; n < 8; ++n) {
    anyLeft |= rb[(size_t)n] < -0.1;
    anyRight |= rb[(size_t)n] > 0.1;
  }
  check(anyLeft, "audible partials appear on the left");
  check(anyRight, "audible partials appear on the right");
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
// 10. CPU: the whole point of the exercise.
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
      o.tuneBlend = 0.5f;
    }

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
}

int main() {
  testTuningTable();
  testBlendEndpoints();
  testSineTable();
  testRenderedSpectrum();
  testAliasing();
  testEnvelopeAndMuteSolo();
  testNoClickOnMute();
  testVoiceAllocation();
  testModulation();
  testPerPartialVelocity();
  testStereoSpread();
  benchmark();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
