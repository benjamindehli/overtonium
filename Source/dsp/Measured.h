#pragma once

#include <array>

namespace ovt {

/// The harmonics of the three characters that come from recordings.
///
/// The other characters are circuits that are simulated once at startup.
/// These three are real instruments. Each one was recorded one sine at a
/// time at 48 kHz and 24 bits, with a held note of about four seconds per key
/// and one take with no note, to measure the noise floor. The harmonics were
/// fitted by least squares over the steady part of each note, after fitting
/// the note's exact pitch the same way.
///
/// Each harmonic is stored the way the tables store it: the amplitude of the
/// sine and of the cosine at n times the fundamental. The take is first
/// shifted in time so that its fundamental is a sine with an amplitude of
/// exactly one. The phases are the ones the instrument produced, including
/// any phase shift from its output stage.
///
/// Only the steady tone is stored here. Changes over time cannot be stored in
/// a table. The tonewheel's flutter is handled in the voice. One of the
/// synthesizers had a vibrato in the takes, which came from its LFO and not
/// from its tone, so it is not included.
using MeasuredRecipe = std::array<std::array<float, 2>, 5>;

/// Where each of the tonewheel bands below starts, in Hz.
///
/// A tonewheel organ has a separate wheel for every pitch, and the wheels do
/// not all have the same harmonics, so one recipe would not fit them. The 8'
/// drawbar was recorded on every C and F#, plus four more keys in the bottom
/// octave, and the 1' drawbar on the C and F# of the top two octaves. The
/// takes are grouped into bands where their harmonics are similar. Each edge
/// is halfway in pitch between the last wheel of one band and the first wheel
/// of the next. A partial below the first edge uses the lowest band, and one
/// above the last edge uses the highest.
inline constexpr std::array<double, 6> kWheelBandEdges{77.7,  123.4, 239.8,
                                                       439.8, 879.9, 1760.4};

/// The wheels, a band at a time from the bottom.
///
/// The level of each harmonic is the power mean of the takes in its band. Its
/// phase is the phase of their sum. A harmonic above 20 kHz is above what the
/// recording can capture, so it is left out of that band's mean instead of
/// being counted as silence.
///
///   band  wheels            2nd    3rd    4th    5th, dB on the fundamental
///      0    65 to   73 Hz  -13.5  -31.5  -45.5  -52.0
///      1    82 to  116 Hz  -17.3  -39.8  -47.0  -52.2
///      2   131 to  220 Hz  -24.9  -48.1  -57.0  -55.5
///      3   262 to  370 Hz  -43.1  -45.8  -61.4  -60.6
///      4   523 to  740 Hz  -46.3  -56.3  -50.0  -70.3
///      5  1047 to 1480 Hz  -49.6  -60.8  -82.2  -81.6
///      6  2094 to 5925 Hz  -41.5  -65.9  -87.6 -100.2
///
/// The low wheels have a strong second harmonic, stronger than the one Valve
/// adds. It gets weaker up to A3, and then drops by fifteen decibels somewhere
/// in the three keys between A3 and C4. Above 1 kHz, no harmonic above the
/// third reaches -80 dB, so five harmonics are enough here, as they are for
/// the circuits.
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

/// One operator of a frequency modulation synthesizer, with nothing
/// modulating it.
///
/// Taken from the notes between C4 and C6. Below C4 the output's coupling
/// capacitor shifts the phases, and above C6 the vibrato in the takes blurs
/// the upper harmonics. Across the whole keyboard the second harmonic stays
/// within two decibels and the others within five:
///
///   2nd -57.1, 3rd -66.7, 4th -72.5, 5th -71.1 dB on the fundamental
///
/// That is thirty-five decibels below Rail's third harmonic, but it is still
/// not a pure sine. The noise between the harmonics also rises by six
/// decibels while a note sounds. That is noise and not a harmonic, so it is
/// not included.
inline constexpr MeasuredRecipe kOperatorRecipe{{{1.0f, 0.0f},
                                                 {-8.2557e-04f, -1.1348e-03f},
                                                 {4.4523e-04f, -1.2926e-04f},
                                                 {-2.2984e-04f, 5.4516e-05f},
                                                 {-2.4276e-04f, 1.3594e-04f}}};

/// The oscillator of a phase distortion synthesizer, with the distortion
/// turned fully off.
///
/// Taken from the same notes as the operator, for the same reasons. It is the
/// most stable of the three. The levels stay within half a decibel from the
/// lowest key to the highest, and there is no modulation in the takes.
///
///   2nd -58.0, 3rd -63.7, 4th -67.9, 5th -73.2 dB on the fundamental
inline constexpr MeasuredRecipe kDcoRecipe{{{1.0f, 0.0f},
                                            {1.3230e-04f, 1.2575e-03f},
                                            {-6.4099e-04f, 1.3650e-04f},
                                            {1.1715e-04f, 3.8723e-04f},
                                            {-2.0258e-04f, 8.0492e-05f}}};

/// How a tonewheel moves as it turns.
///
/// Every wheel sits on a shaft geared to one motor, and none of it is
/// perfectly round or perfectly steady. A wheel that is slightly off centre
/// moves in pitch and level once per turn of its shaft, and a little at twice
/// that. The motor itself speeds up and slows down slightly, and since it
/// drives every wheel, that part moves every wheel together. All of it shows
/// in the recordings as pairs of sidebands either side of each wheel's tone,
/// which were fitted at the exact frequencies and split into the part that
/// changes the level and the part that changes the pitch.
///
/// The figures are peak values. They come from the wheels from C3 up. Below
/// that, a wheel's sidebands land exactly on other wheels of the same note,
/// which leak into the recording, so the two cannot be told apart.
///
/// The motor turns at 25 Hz, which is 1500 rpm on 50 Hz mains, and moves
/// every wheel by about 0.9 cent at that rate. The takes spread from 0.6 to
/// 2 cents around that, as the same motor measured at different moments.
inline constexpr double kWheelMotorHz = 25.0;
inline constexpr double kWheelMotorCents = 0.9;

/// What each wheel's own shaft does, which differs from wheel to wheel, so
/// each unit draws its own from these ranges. Drawn evenly in log, since the
/// recorded wheels spread over more than a decade and cluster around the
/// middle of it in log terms.
///
///   once a turn, pitch    0.25 to 4 cents, about 1 typically
///   once a turn, level    0.025 to 0.33 dB, about 0.1 typically
///   twice a turn, pitch   0.05 to 0.5 Hz, which is several cents on the
///                         middle wheels and a fraction of one at the top
///   twice a turn, level   0.01 to 0.2 dB
inline constexpr std::array<double, 2> kWheelShaftCents{0.25, 4.0};
inline constexpr std::array<double, 2> kWheelShaftDb{0.025, 0.33};
inline constexpr std::array<double, 2> kWheelTwiceHz{0.05, 0.5};
inline constexpr std::array<double, 2> kWheelTwiceDb{0.01, 0.2};

/// How fast the shaft under a wheel at this frequency turns, in turns a
/// second.
///
/// The wheels of one note share a gear ratio and differ only in how many teeth
/// they have, two on the lowest and twice as many each octave up. So the shaft
/// turns at the note's frequency folded down by octaves, into the octave
/// between 16 and 32 Hz, where the twelve notes' shafts sit: 16.35 for C up to
/// 30.87 for B.
inline double wheelShaftHz(double freq) noexcept {
  if (!(freq > 0.0))
    return 16.0;

  while (freq >= 32.0)
    freq *= 0.5;

  while (freq < 16.0)
    freq *= 2.0;

  return freq;
}

/// The wheels that leak into a held note, and how loud each one is against
/// it.
///
/// In a tonewheel generator every wheel turns all the time, and a little of
/// the wheels near the one being played gets into its pickup and wiring. The
/// recordings show which: the same note's wheels in the octaves below, and the
/// wheels a fifth away, which sit next to it in the generator. Each candidate
/// was found as a peak within half a percent of its ratio to the played wheel
/// and counted only where it stood 10 dB above the floor around it. These are
/// the four found in most takes and loudest, at the median of the takes that
/// had them:
///
///   two octaves down              18 of 20 takes, -47.7 dB
///   a fourth down                 19 of 20 takes, -49.6 dB
///   an octave down                20 of 20 takes, -51.9 dB
///   a fifth down                  14 of 20 takes, -55.4 dB
///
/// The fourth down is the wheel a fifth up, an octave lower. The rest were
/// quieter or found in fewer takes: three octaves down at -57.5, a fifth up at
/// -57.2, and the fifth down an octave or two lower at -60 and -68. The hum
/// at 50 Hz is left out, since it does not belong to a note.
struct WheelLeak {
  double ratio; ///< the leaking wheel's frequency against the played one
  double db;    ///< and its level against it
};

inline constexpr std::array<WheelLeak, 4> kWheelLeaks{{
    {0.25, -47.7},
    {0.7491535384383408, -49.6},
    {0.5, -51.9},
    {0.6674199270850172, -55.4},
}};

/// The lowest wheel on the organ, C1. A leak that would fall below it has no
/// wheel to come from, so it is silent.
inline constexpr double kLowestWheelHz = 32.70;

/// How much stronger the two synthesizers' harmonics are played than they
/// were measured, in dB.
///
/// The tables hold the measured harmonics exactly, and next to the recordings
/// they still sound a little too clean. A table cannot hold what the
/// recordings have besides their harmonics: the noise under each note, and on
/// the operator the 6th to 10th harmonics, which stay at about -70 dB and are
/// past where the tables stop. So the harmonics that are in the tables are
/// lifted a little to make up for it. The figure was set by ear, not measured.
/// The tonewheel is not lifted, because it does not sound too clean.
inline constexpr double kSynthesizerLiftDb = 6.0;

/// Which tonewheel band a partial at this frequency reads.
inline int wheelBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kWheelBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

} // namespace ovt
