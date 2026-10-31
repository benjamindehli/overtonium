#pragma once

#include <array>

namespace ovt {

/// The harmonics of the six characters that come from recordings, as opposed
/// to the circuits simulated at startup. See "Character" in DESIGN.md for how
/// they were recorded and fitted.
///
/// Each harmonic is stored as the tables store it: the amplitude of the sine
/// and of the cosine at n times the fundamental, with the take shifted so the
/// fundamental is a sine of amplitude one. The phases are the instrument's own.
/// Only the steady tone is here. Wobble, leakage and noise are added in the
/// voice.
///
/// Room for ten harmonics. See harmonicsFor for how many each one uses.
inline constexpr int kMeasuredHarmonics = 10;

/// Sine and cosine amplitude of each harmonic, the fundamental first.
using MeasuredRecipe = std::array<std::array<float, 2>, kMeasuredHarmonics>;

/// Where each of the tonewheel bands below starts, in Hz. Each edge is halfway
/// in pitch between the recorded wheels either side, and the outer bands
/// extend to cover everything below and above.
inline constexpr std::array<double, 6> kWheelBandEdges{77.7,  123.4, 239.8,
                                                       439.8, 879.9, 1760.4};

/// The wheels, a band at a time from the bottom, from the 8' and 1' drawbars.
/// Each harmonic is the power mean of the takes in its band, with the phase of
/// their sum. Harmonics above 20 kHz are left out of the mean.
///
///   band  wheels            2nd    3rd    4th    5th, dB on the fundamental
///      0    65 to   73 Hz  -13.5  -31.5  -45.5  -52.0
///      1    82 to  116 Hz  -17.3  -39.8  -47.0  -52.2
///      2   131 to  220 Hz  -24.9  -48.1  -57.0  -55.5
///      3   262 to  370 Hz  -43.1  -45.8  -61.4  -60.6
///      4   523 to  740 Hz  -46.3  -56.3  -50.0  -70.3
///      5  1047 to 1480 Hz  -49.6  -60.8  -82.2  -81.6
///      6  2094 to 5925 Hz  -41.5  -65.9  -87.6 -100.2
inline constexpr std::array<MeasuredRecipe, 7> kWheelRecipes{{
    {{{1.0f, 0.0f},
      {1.5471e-01f, 1.4257e-01f},
      {-1.4595e-02f, 2.2169e-02f},
      {1.3336e-03f, 5.1681e-03f},
      {2.4543e-03f, 4.9221e-04f}}},
    {{{1.0f, 0.0f},
      {2.0118e-02f, 1.3453e-01f},
      {-1.8331e-03f, 1.0098e-02f},
      {-1.3136e-03f, 4.2858e-03f},
      {2.3991e-03f, 5.0946e-04f}}},
    {{{1.0f, 0.0f},
      {-2.1848e-02f, 5.2389e-02f},
      {3.0600e-03f, 2.4933e-03f},
      {-1.4141e-03f, -7.8996e-05f},
      {1.5342e-03f, 6.5850e-04f}}},
    {{{1.0f, 0.0f},
      {6.7780e-03f, -1.7171e-03f},
      {2.7876e-03f, 4.3035e-03f},
      {-3.7961e-04f, 7.6660e-04f},
      {8.1145e-04f, 4.5653e-04f}}},
    {{{1.0f, 0.0f},
      {3.6071e-04f, 4.8536e-03f},
      {1.2361e-03f, 8.9311e-04f},
      {2.7308e-03f, -1.5776e-03f},
      {2.9167e-04f, 9.2888e-05f}}},
    {{{1.0f, 0.0f},
      {-3.7275e-04f, -3.3037e-03f},
      {7.5630e-04f, 5.0125e-04f},
      {7.6901e-05f, 7.5487e-06f},
      {7.6737e-05f, 3.1025e-05f}}},
    {{{1.0f, 0.0f},
      {-4.0483e-04f, -8.3562e-03f},
      {4.8881e-04f, -1.3307e-04f},
      {4.1137e-05f, 5.4205e-06f},
      {7.7867e-06f, 5.9569e-06f}}},
}};

/// One operator of a frequency modulation synthesizer, unmodulated, from the
/// notes between C4 and C6. Ten harmonics because the upper ones stay near
/// -70 dB rather than falling away.
///
///   2nd -57.1, 3rd -66.7, 4th -72.5, 5th -71.1 dB on the fundamental
///   6th -69.5, 7th -74.5, 8th -69.8, 9th -74.5, 10th -70.9
inline constexpr MeasuredRecipe kFmRecipe{{{1.0f, 0.0f},
                                           {-8.2557e-04f, -1.1348e-03f},
                                           {4.4523e-04f, -1.2926e-04f},
                                           {-2.2984e-04f, 5.4516e-05f},
                                           {-2.4276e-04f, 1.3594e-04f},
                                           {-2.9924e-04f, -1.5054e-04f},
                                           {1.3757e-04f, -1.2954e-04f},
                                           {-2.7770e-04f, -1.6513e-04f},
                                           {-1.1735e-04f, 1.4676e-04f},
                                           {-2.7895e-04f, -5.8257e-05f}}};

/// The oscillator of a phase distortion synthesizer with the distortion off,
/// from the notes between C4 and C6.
///
///   2nd -58.0, 3rd -63.7, 4th -67.9, 5th -73.2 dB on the fundamental
///   6th -79.1, 7th -75.3, 8th -73.1, 9th -118.0, 10th -81.4
inline constexpr MeasuredRecipe kPdRecipe{{{1.0f, 0.0f},
                                           {1.3230e-04f, 1.2575e-03f},
                                           {-6.4099e-04f, 1.3650e-04f},
                                           {1.1715e-04f, 3.8723e-04f},
                                           {-2.0258e-04f, 8.0492e-05f},
                                           {-4.9999e-05f, -9.9016e-05f},
                                           {-1.4300e-04f, 9.4750e-05f},
                                           {-1.3438e-04f, -1.7572e-04f},
                                           {1.0274e-06f, 7.4000e-07f},
                                           {-6.5028e-05f, -5.5200e-05f}}};

/// How a tonewheel moves as it turns, from sidebands fitted around each wheel
/// from C3 up. Figures are peak values.
///
/// The motor turns at 25 Hz and moves every wheel together by about 0.9 cent.
inline constexpr double kWheelMotorHz = 25.0;
inline constexpr double kWheelMotorCents = 0.9;

/// The ranges each wheel's own shaft movement is drawn from, evenly in log:
/// once a turn in cents and dB, twice a turn in Hz and dB.
inline constexpr std::array<double, 2> kWheelShaftCents{0.25, 4.0};
inline constexpr std::array<double, 2> kWheelShaftDb{0.025, 0.33};
inline constexpr std::array<double, 2> kWheelTwiceHz{0.05, 0.5};
inline constexpr std::array<double, 2> kWheelTwiceDb{0.01, 0.2};

/// How fast the shaft under a wheel at this frequency turns, in turns a
/// second: the note's frequency folded by octaves into 16 to 32 Hz, since the
/// wheels of one note share a gear ratio and double their teeth each octave.
inline double wheelShaftHz(double freq) noexcept {
  if (!(freq > 0.0))
    return 16.0;

  while (freq >= 32.0)
    freq *= 0.5;

  while (freq < 16.0)
    freq *= 2.0;

  return freq;
}

/// One wheel that leaks into a held note, and how loud it is against it.
struct WheelLeak {
  double ratio; ///< the leaking wheel's frequency against the played one
  double db;    ///< and its level against it
};

/// The four loudest and most common leaks in the recordings, at their median
/// level: two octaves down, a fourth down, an octave down and a fifth down.
inline constexpr std::array<WheelLeak, 4> kWheelLeaks{{
    {0.25, -47.7},
    {0.7491535384383408, -49.6},
    {0.5, -51.9},
    {0.6674199270850172, -55.4},
}};

/// The lowest wheel on the organ, C1. A leak that would fall below it is
/// silent.
inline constexpr double kLowestWheelHz = 32.70;

/// What was changed by ear after playing beside the recordings, in dB, added
/// to the measured figures when the tables and noise are built.
struct ByEar {
  double harmonicsDb; ///< added to every harmonic above the fundamental
  double noiseDb;     ///< added to the measured noise
};

inline constexpr ByEar kFmByEar{3.0, 0.0};
inline constexpr ByEar kPdByEar{6.0, 3.0};

/// PD's second harmonic, in dB, made by a quadratic on the bus rather than in
/// each oscillator, since the recorded chords showed it comes from the mix.
/// Sized so one partial at kBusReferenceAmplitude gets the measured level, and
/// a chord then makes sum and difference tones. See BusDrive.
inline constexpr double kPdSummingH2Db = -58.0;

/// What one partial at full fader and full velocity, panned to the centre,
/// comes to in each channel where the bus sees it.
inline constexpr double kBusReferenceAmplitude = 0.70710678118654752;

/// The noise under each of the two synthesizers' notes, in dB per hertz
/// against the note's fundamental (dBc/Hz), measured between the harmonics
/// over the notes from C4 to C6. White, and follows the notes. See kFmFixedHiss
/// for how it grows with more keys and kFmByEar for what is added.
inline constexpr double kFmNoiseDbPerHz = -105.5;
inline constexpr double kPdNoiseDbPerHz = -114.5;

/// How much of a character's noise is a fixed hiss that does not grow as keys
/// are added, below and above kHissSplitHz, fitted from chords built up one
/// key at a time. The rest follows the signal.
///
/// The fixed part is sized by the loudest note sounding, so there is none when
/// nothing plays. With one note the two parts add up to the measured level.
struct FixedHiss {
  double low; ///< share of a single note's noise that is fixed, below the split
  double high; ///< and above it
};

inline constexpr FixedHiss kFmFixedHiss{0.32, 0.78};
inline constexpr FixedHiss kPdFixedHiss{0.0, 0.0};

/// Where the noise is split into its two bands, in Hz.
inline constexpr double kHissSplitHz = 6400.0;

/// The 6581's 12-bit converter: what each bit of a code adds to the output,
/// bit 0 first, scaled so all twelve add up to one.
///
/// Fitted from the steps of slow sawtooths. Each bit is worth a little less
/// than twice the one below, so the output falls back where many bits change
/// at once, and that uneven staircase is most of the chip's sound.
inline constexpr std::array<double, 12> kSidBitWeights{
    5.4357173444e-04, 7.6113214830e-04, 1.2904715623e-03, 2.3227781767e-03,
    4.4508006323e-03, 8.6247381393e-03, 1.6803809368e-02, 3.2777685621e-02,
    6.4136530806e-02, 1.2544682802e-01, 2.5281303293e-01, 4.9002862086e-01};

/// The bow after the converter: y = x - a x^2, with x the converter's level on
/// a swing from -1 to 1. Fitted from the even harmonics of the chip's
/// triangle, which the converter alone cannot make, with the filter bypassed.
inline constexpr double kSidQuadratic = 0.146;

/// How much of that bow comes after the chip's voices are mixed, on the same
/// swing. Set from the sum and difference tones of two recorded voices, and
/// the rest of kSidQuadratic is inside each voice.
inline constexpr double kSidMixQuadratic = 0.058;

/// The 6581's noise, in dB per hertz against a full-scale sine's fundamental,
/// below and above kHissSplitHz, from the take with no note. It does not
/// follow the note, so all of it is fixed.
inline constexpr double kSidNoiseLowDbPerHz = -104.4;
inline constexpr double kSidNoiseHighDbPerHz = -115.0;
inline constexpr FixedHiss kSidFixedHiss{1.0, 1.0};

/// A voltage-controlled oscillator's sine, straight from its output, from the
/// takes between C3 and C5. Its shape hardly changes with pitch.
///
///   2nd -24.7, 3rd -25.9, 4th -46.6, 5th -42.0 dB on the fundamental
///   6th -56.1, 7th -53.1, 8th -62.1, 9th -57.2, 10th -64.6
inline constexpr MeasuredRecipe kVcoRecipe{{{1.0f, 0.0f},
                                            {4.6386e-03f, -5.8213e-02f},
                                            {5.0505e-02f, -6.4706e-03f},
                                            {-1.4862e-03f, -4.4627e-03f},
                                            {7.9406e-03f, -2.0611e-04f},
                                            {9.2417e-04f, -1.2586e-03f},
                                            {-2.1810e-03f, -3.2029e-04f},
                                            {-7.4257e-04f, 2.6270e-04f},
                                            {1.3764e-03f, 1.7846e-04f},
                                            {5.8038e-04f, -9.2275e-05f}}};

/// The noise under the VCO's notes, in dB per hertz against the fundamental,
/// measured as for the synthesizers. It follows the note. The skirt near each
/// note is added as pitch wander instead. See kVcoWanderCents.
inline constexpr double kVcoNoiseDbPerHz = -122.0;
inline constexpr FixedHiss kVcoFixedHiss{0.0, 0.0};

/// How the VCO's pitch moves within a held note: about 0.2 cents rms, spread
/// evenly from half a hertz up to kVcoWanderHz, scaling with pitch.
inline constexpr double kVcoWanderCents = 0.2;
inline constexpr double kVcoWanderHz = 60.0;

/// How much sooner the VCO's mixer clips its negative half than its positive
/// one, as a ratio of the distances to its two rails. Each side uses the
/// op-amp bus's cubic knee.
inline constexpr double kVcoMixerMismatch = 2.44;

/// Which tonewheel band a partial at this frequency reads.
inline int wheelBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kWheelBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

/// Where each of the combo organ's bands below starts, in Hz, halfway in
/// pitch between the bands either side.
///
/// The organ's filter belongs to the key rather than the lever, so a band is
/// chosen by the key played and a partial high in a low note's series keeps
/// the low key's shape.
inline constexpr std::array<double, 7> kComboBandEdges{
    77.8, 220.0, 311.1, 439.9, 622.2, 879.9, 1759.7};

/// The combo organ's bands, from the bottom, from its 8' lever (16' for the
/// lowest band). Each harmonic is the power mean of the takes in its band,
/// with the phase of their sum.
///
///   band  notes              3rd    5th    7th    9th, dB on the fundamental
///      0  C2 (16')          -16.2  -26.1  -33.4  -39.5
///      1  F#2 (16') to F#3  -18.5  -29.4  -37.4  -43.8
///      2  C4                -22.2  -35.0  -44.4  -51.9
///      3  F#4               -24.9  -39.2  -49.5  -57.5
///      4  C5                -27.4  -43.1  -54.4  -62.8
///      5  F#5               -29.8  -46.9  -58.6  -67.1
///      6  C6 and F#6        -33.0  -51.0  -63.6  -68.5
///      7  C7 and above      -35.1  -55.3  -68.9  -71.6
///
/// The top band takes the band below's even harmonics, since at C7 those were
/// leakage from the key's other levers.
inline constexpr std::array<MeasuredRecipe, 8> kComboRecipes{{
    {{{1.0000e+00f, 0.0000e+00f},
      {4.3350e-04f, -1.6374e-03f},
      {-1.4397e-01f, -5.7099e-02f},
      {-3.8345e-04f, 4.0171e-04f},
      {-1.6360e-02f, 4.6995e-02f},
      {1.2239e-04f, 3.2587e-04f},
      {1.8232e-02f, 1.0989e-02f},
      {2.3708e-04f, -3.1959e-04f},
      {8.1102e-03f, -6.8985e-03f},
      {2.5475e-05f, -6.7907e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {1.6760e-03f, -1.8782e-04f},
      {1.0084e-01f, -6.1964e-02f},
      {4.2237e-04f, -3.4460e-04f},
      {2.9089e-02f, -1.7487e-02f},
      {1.8823e-04f, 2.6161e-04f},
      {7.0462e-03f, -1.1509e-02f},
      {-2.0245e-04f, -2.8221e-04f},
      {4.1344e-03f, -4.9122e-03f},
      {6.7831e-05f, -5.4604e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {8.3722e-04f, 4.3105e-04f},
      {3.1623e-02f, 7.1331e-02f},
      {3.2009e-05f, 3.6852e-04f},
      {-1.7663e-02f, 1.0811e-03f},
      {-1.5301e-04f, -1.0677e-04f},
      {2.8460e-03f, -5.3076e-03f},
      {2.8553e-04f, -1.3158e-04f},
      {1.0511e-03f, 2.3104e-03f},
      {-2.3881e-05f, 4.4986e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {6.8508e-04f, 5.7662e-04f},
      {-1.9458e-02f, 5.3717e-02f},
      {-3.1993e-04f, 1.4591e-04f},
      {-4.9861e-04f, -1.0970e-02f},
      {1.1437e-04f, -2.0053e-04f},
      {7.4023e-04f, 3.2814e-03f},
      {-2.2396e-04f, 1.5813e-04f},
      {-3.8204e-04f, -1.2748e-03f},
      {4.9494e-05f, 2.5614e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {4.2192e-04f, 7.7239e-04f},
      {-3.6519e-02f, 2.1655e-02f},
      {-3.0088e-04f, -2.9927e-04f},
      {6.8576e-03f, -1.3758e-03f},
      {-1.0964e-04f, 5.4763e-05f},
      {-1.7936e-03f, -6.7326e-04f},
      {3.8332e-04f, -2.7742e-05f},
      {3.4700e-04f, 6.3771e-04f},
      {-3.4101e-05f, -3.0139e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {6.5109e-05f, 1.3709e-03f},
      {-3.0862e-02f, -9.3330e-03f},
      {3.3316e-04f, -3.6308e-04f},
      {4.2997e-04f, 4.5141e-03f},
      {1.5009e-04f, -1.3410e-04f},
      {1.1158e-03f, -3.4642e-04f},
      {-6.2677e-04f, -5.2486e-05f},
      {-2.7888e-04f, -3.4353e-04f},
      {5.2872e-06f, 5.9567e-05f}}},
    {{{1.0000e+00f, 0.0000e+00f},
      {-1.4682e-03f, 1.1620e-03f},
      {-7.4154e-03f, -2.1245e-02f},
      {-1.2970e-05f, 2.3279e-03f},
      {-2.8005e-03f, -4.1146e-04f},
      {1.5940e-04f, 2.1635e-04f},
      {-4.0455e-04f, 5.1966e-04f},
      {4.0480e-05f, 4.3361e-05f},
      {4.7693e-05f, 3.7488e-04f},
      {1.2707e-04f, 1.6867e-04f}}},
    // The top band's even harmonics are the band below's. See above.
    {{{1.0000e+00f, 0.0000e+00f},
      {-1.4682e-03f, 1.1620e-03f},
      {9.3192e-03f, -1.4922e-02f},
      {-1.2970e-05f, 2.3279e-03f},
      {2.6157e-04f, -1.6983e-03f},
      {1.5940e-04f, 2.1635e-04f},
      {-1.9236e-04f, -3.0213e-04f},
      {4.0480e-05f, 4.3361e-05f},
      {-1.4385e-04f, -2.1924e-04f},
      {1.2707e-04f, 1.6867e-04f}}},
}};

/// Which combo organ band a key at this frequency reads.
inline int comboBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kComboBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

/// The combo organ's hiss, in dB per hertz against a single note's
/// fundamental, below and above kHissSplitHz, measured against C4. It is the
/// same with no key down, so all of it is fixed. Played kComboByEar quieter.
inline constexpr double kComboNoiseLowDbPerHz = -87.9;
inline constexpr double kComboNoiseHighDbPerHz = -92.4;
inline constexpr FixedHiss kComboFixedHiss{1.0, 1.0};
inline constexpr ByEar kComboByEar{0.0, -10.0};

} // namespace ovt
