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

/// Which tonewheel band a partial at this frequency reads.
inline int wheelBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kWheelBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

} // namespace ovt
