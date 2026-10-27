#pragma once

#include <array>

namespace ovt {

/// The harmonics of the six characters that come from recordings.
///
/// The other characters are circuits that are simulated once at startup.
/// These six are real instruments. The tonewheel and the two synthesizers
/// were recorded one sine at a time at 48 kHz and 24 bits, with a held note of
/// about four seconds per key and one take with no note, to measure the noise
/// floor. The harmonics were fitted by least squares over the steady part of
/// each note, after fitting the note's exact pitch the same way.
///
/// Each harmonic is stored the way the tables store it: the amplitude of the
/// sine and of the cosine at n times the fundamental. The take is first
/// shifted in time so that its fundamental is a sine with an amplitude of
/// exactly one. The phases are the ones the instrument produced, including
/// any phase shift from its output stage.
///
/// The 6581 has no sine to record, so what is stored for it is its
/// converter and the bow after it, measured from its other waveforms, and the
/// sine it would make is worked out from them. See kSidBitWeights.
///
/// Only the steady tone is stored here. Changes over time cannot be stored in
/// a table. The tonewheel's wobble and leakage, and the synthesizers' noise,
/// are added in the voice. One of the synthesizers had a vibrato in the takes,
/// which came from its LFO and not from its tone, so it is not included.
///
/// Room for ten harmonics. The tonewheel uses the first five, as the circuits
/// do, and the two synthesizers, the VCO and the combo organ use all ten.
/// See harmonicsFor.
inline constexpr int kMeasuredHarmonics = 10;

using MeasuredRecipe = std::array<std::array<float, 2>, kMeasuredHarmonics>;

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
///   6th -69.5, 7th -74.5, 8th -69.8, 9th -74.5, 10th -70.9
///
/// Ten rather than five, because the upper ones stay level at about -70 dB
/// instead of falling away. Together they hold 14% of the harmonic power.
///
/// That is thirty-five decibels below Rail's third harmonic, but it is still
/// not a pure sine. The noise under each note is added in the voice. See
/// kFmNoiseDbPerHz.
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

/// The oscillator of a phase distortion synthesizer, with the distortion
/// turned fully off.
///
/// Taken from the same notes as the operator, for the same reasons. It is the
/// most stable of the three. The levels stay within half a decibel from the
/// lowest key to the highest, and there is no modulation in the takes.
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
/// What was changed by ear on the two synthesizers, in dB, after playing them
/// next to the recordings. The measured figures above and below stay as they
/// were recorded, and these are added when the tables and the noise are
/// built.
///
/// FM's harmonics are 3 dB stronger than measured, and PD's 6. PD's noise is
/// 3 dB stronger than measured, and FM's is as
/// measured. The tonewheel has none of this, because it sounds like the
/// recordings as measured.
struct ByEar {
  double harmonicsDb; ///< added to every harmonic above the fundamental
  double noiseDb;     ///< added to the measured noise
};

inline constexpr ByEar kFmByEar{3.0, 0.0};
inline constexpr ByEar kPdByEar{6.0, 3.0};

/// Where PD's second harmonic comes from, which is the mix and not each
/// oscillator.
///
/// Held as a chord, the phase distortion synthesizer makes sum and difference
/// tones between its notes, every one of them at -51 to -53 dB against a note.
/// One quadratic shared by the whole mix makes those exactly 6 dB above the
/// second harmonic it gives a single note, and the single note's second
/// harmonic is -57.7, so the mix is where it comes from. Its third-order
/// products are 14 to 23 dB below what a shared curve would give for its
/// third harmonic, so the third and those above it belong to each oscillator
/// and stay in the table. The frequency modulation synthesizer's products are
/// 25 to 40 dB below what a shared curve would give, so its harmonics all
/// belong to each operator, and it has no curve on its mix.
///
/// So PD's second harmonic is made by a quadratic on the bus, sized so
/// that one partial at full fader and full velocity, which is
/// kBusReferenceAmplitude in each channel, gets the measured -58 dB. A chord
/// then makes sum and difference tones between its notes and partials, as the
/// instrument does. See BusDrive.
inline constexpr double kPdSummingH2Db = -58.0;

/// What one partial at full fader and full velocity, panned to the centre,
/// comes to in each channel where the bus sees it.
inline constexpr double kBusReferenceAmplitude = 0.70710678118654752;

/// The noise under each of the two synthesizers' notes, in dB per hertz
/// against the note's fundamental (dBc/Hz).
///
/// Measured between the harmonics, from 100 Hz to 20 kHz, leaving out 30 Hz
/// either side of every harmonic and 3 Hz either side of the mains hum, and
/// averaged over the notes from C4 to C6. Both are close to flat. The
/// operator's floor is within a decibel of -105.5 from 1 kHz to 10 kHz and
/// lifts by two above 12 kHz. PD's falls gently, from -112 at 100 Hz to
/// -117 at 10 kHz, and -114.5 is its middle. Over the whole band they add up
/// to about 62 and 72 dB under the note.
///
/// While a note sounds, both floors are about 5 dB above the take with no
/// note, so the noise follows the note. It is white, sized to the notes'
/// partials and silent when nothing plays, and how it grows as keys are added
/// is kFmFixedHiss. See kFmByEar for what is added to it.
///
/// Close to each tone there is also a faint skirt, which is not included. It
/// holds less power than the floor, and on the operator most of it is the
/// vibrato in the takes.
inline constexpr double kFmNoiseDbPerHz = -105.5;
inline constexpr double kPdNoiseDbPerHz = -114.5;

/// How much of each synthesizer's noise is a fixed hiss, which does not grow
/// as keys are added, below and above kHissSplitHz.
///
/// Both were recorded again as a chord built up one key every four seconds,
/// C3, C4, G4, C5 and E5, each held. The frequency modulation synthesizer's
/// noise between the lines fits a fixed part plus a part that follows the
/// signal's power, within a decibel above 400 Hz and three below. With one key
/// the fixed part is about a third of it below 6.4 kHz and more than three
/// quarters above, which is where hiss is heard. So from one key to five its
/// noise above 6.4 kHz grows 2.8 dB while the signal grows 7. The share above
/// is the one that growth gives at two, four and five keys alike. With the
/// two bands split as the engine splits them, the model grows within about a
/// decibel of the instrument at every count of keys.
///
/// The phase distortion synthesizer's fixed part is 2 to 10% with one key,
/// which is too little to hear, so its noise is all the part that follows the
/// signal. Its floor between the lines did jump when G4 and E5 came in, but
/// that was its sum and difference tones, which its bus makes. See
/// kPdSummingH2Db.
///
/// The fixed part is sized by the loudest note sounding rather than by
/// nothing at all, so a quiet patch hisses less and there is no hiss when
/// nothing plays. With one note the two parts add up to the measured level,
/// exactly as before.
struct FixedHiss {
  double low; ///< share of a single note's noise that is fixed, below the split
  double high; ///< and above it
};

inline constexpr FixedHiss kFmFixedHiss{0.32, 0.78};
inline constexpr FixedHiss kPdFixedHiss{0.0, 0.0};

/// Where the noise is split into its two bands, in Hz.
inline constexpr double kHissSplitHz = 6400.0;

/// The 6581's 12-bit converter: what each bit of a code adds to the output,
/// bit 0 first, scaled so that all twelve add up to one.
///
/// The chip was played with SidStation ASID
/// (https://github.com/benjamindehli/sidstation-asid), which writes its
/// registers directly, so each take is the chip as set and nothing else.
///
/// The chip makes every waveform as a 12-bit code and turns it into a voltage
/// with a resistor ladder whose bits are not weighted in exact powers of two.
/// A slow sawtooth steps through every code in order, so each step from one
/// code to the next was measured as the jump between straight lines fitted on
/// either side of it, which leaves out the slow sag the output's coupling puts
/// between steps. Every step changes a known set of bits, so the twelve weights
/// were fitted to 163,800 steps from the three slowest sawtooths, at 87, 46
/// and 23 samples per code at 192 kHz. Fitted one take at a time they agree on
/// the shape within about 1%.
///
/// Each bit is worth 1.94 to 1.96 times the one below it rather than two, and
/// the lowest few less still. So where many bits change at once the output
/// falls back instead of stepping up: by 82 average steps from code 2047 to
/// 2048, and by 18 to 26 at the quarters. That uneven staircase is most of
/// what the chip sounds like.
inline constexpr std::array<double, 12> kSidBitWeights{
    5.4357173444e-04, 7.6113214830e-04, 1.2904715623e-03, 2.3227781767e-03,
    4.4508006323e-03, 8.6247381393e-03, 1.6803809368e-02, 3.2777685621e-02,
    6.4136530806e-02, 1.2544682802e-01, 2.5281303293e-01, 4.9002862086e-01};

/// The bow after the converter: y = x - a x^2, with x the converter's level on
/// a swing from -1 to 1.
///
/// The chip's triangle steps up and back down through the same codes, so a
/// converter alone gives it no even harmonics at all, and the recorded
/// triangles have them: -23 dB at the second, falling as one over n squared,
/// which is what squaring a triangle gives. Fitted to the 16 Hz triangle, a
/// bow of 0.138 and the converter together give every harmonic of it within
/// 0.5 dB and of the 8 Hz one within 0.9, without refitting, and the
/// sawtooth's small pattern of even harmonics a few tenths of a decibel high.
///
/// Those takes had the chip's other two voices routed into its filter to keep
/// their leakage out. Recorded again with the filter bypassed, which is the
/// chip in its plain state, the voices come out 3.3 dB louder, and a voice at
/// C4 has its second harmonic 0.6 dB stronger and everything else within half
/// a decibel. So the bow is the one fitted to those takes, 0.146, and the
/// level change is a gain after it.
inline constexpr double kSidQuadratic = 0.146;

/// How much of that bow comes after the chip's voices are mixed, on the same
/// swing.
///
/// Recorded as two voices together, triangles at C4 and G4 with the filter
/// bypassed, the chip makes sum and difference tones between them at -26.7 dB
/// against each note. A bow entirely on the mix would make them at -17, and a
/// bow entirely inside each voice would make none, so it is split: this much
/// on the mix, 40% of it, and the rest of kSidQuadratic in each voice.
/// Simulated with both triangles, that gives the sum and difference tones and
/// a single voice's second harmonic as recorded. Each voice alone matches the
/// converter within a decibel, so both voices' converters are alike. With
/// the other voices routed into the filter, the mix's share came out at 50%.
///
/// The weaker tones, at a note's second harmonic less the other note, were
/// recorded at -41.3 dB, and at -47.7 with the filter in use. The model puts
/// them at about -37 whether the mix's bow comes before the voices' own bows
/// or after, so the order cannot be told from them. They are 15 dB or more
/// below the main tones, and with sines, as the partials are, lower still.
inline constexpr double kSidMixQuadratic = 0.058;

/// The 6581's noise, in dB per hertz against a full-scale sine's fundamental,
/// below and above kHissSplitHz.
///
/// From the take with no note, with the other two voices routed into the
/// filter to keep their leakage out. It falls with frequency, from -96 at 100
/// to 400 Hz to -115 above 6.4 kHz, and the band below is its power mean over
/// 100 Hz to 6.4 kHz. Above 6.4 kHz the floor while a note sounds is within a
/// decibel of the floor with none, so it does not follow the note: all of it
/// is fixed, sized by the loudest note so there is still none when nothing
/// plays. About 66 dB under a full-scale note in all.
inline constexpr double kSidNoiseLowDbPerHz = -104.4;
inline constexpr double kSidNoiseHighDbPerHz = -115.0;
inline constexpr FixedHiss kSidFixedHiss{1.0, 1.0};

/// A voltage-controlled oscillator's sine, straight from its output.
///
/// Recorded from a modulation oscillator that tracks the keyboard and makes a
/// sine, at 192 kHz and 24 bits, on every C and F# from about 4 Hz to 6 kHz.
/// Its shape hardly changes with pitch: from 16 Hz to 500 Hz every harmonic
/// stays within half a decibel. In the top octave the second harmonic falls
/// away, by 8 dB at 4.2 kHz, which is left out, since a partial that high has
/// almost no room for harmonics under Nyquist anyway. Taken from C3 to C5,
/// where the phases agree within five degrees. Below C3 the levels are the
/// same but the output's coupling capacitor turns the phases.
///
///   2nd -24.7, 3rd -25.9, 4th -46.6, 5th -42.0 dB on the fundamental
///   6th -56.1, 7th -53.1, 8th -62.1, 9th -57.2, 10th -64.6
///
/// A second and a third at about -25 dB is a sine made from a triangle by an
/// analogue shaper, and as much as Rail or Valve add. Each take holds its
/// pitch within a quarter of a cent. Between takes it moved by up to twenty,
/// since its tuning is a potentiometer, which is the room and not the tone.
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

/// The noise under the VCO's notes, in dB per hertz against the fundamental.
///
/// Measured between the harmonics as for the synthesizers, on the notes from
/// C3 to C5. Above 6.4 kHz it is -121.9 to -124.4, 25 dB above the recording
/// chain's own floor, so it is the oscillator's. Nearer each note it rises
/// into a skirt, which is the pitch moving very slightly all the time and is
/// added as that. See kVcoWanderCents. There was no way to silence the
/// oscillator, so whether the noise is there between notes could not be
/// measured, and it follows the note.
inline constexpr double kVcoNoiseDbPerHz = -122.0;
inline constexpr FixedHiss kVcoFixedHiss{0.0, 0.0};

/// How the VCO's pitch moves within a held note.
///
/// Read directly from the takes: the fundamental isolated, its phase taken
/// from the analytic signal, and a steady pitch's straight line taken off.
/// What is left moves about 0.2 cents rms, close to that in cents from C2 to
/// C5, so it scales with the pitch as a tuning voltage with a little noise on
/// it does. It is spread evenly over how fast it moves, from half a hertz to
/// 60 Hz, about 0.03 cents per root hertz, with no peak at the mains. Above
/// 60 Hz it could not be told from the recording's own floor, so it stops
/// there.
inline constexpr double kVcoWanderCents = 0.2;
inline constexpr double kVcoWanderHz = 60.0;

/// How much sooner the VCO's mixer clips its negative half than its positive
/// one.
///
/// The oscillator was recorded through the mixer it feeds, driven from a
/// voltage-controlled amplifier swept slowly from near silence to well past
/// clipping, with the amplifier's output and the mixer's recorded together so
/// the curve could be read sample against sample. Driven hard, the mixer is
/// straight until it bends over a short knee and then stops rising, at a rail
/// on each side, and the negative side's rail is 2.44 times nearer than the
/// positive side's. Fitted as the op-amp bus's cubic knee on each side, with
/// the output's coupling taken off, it matches the recording within 25 dB. A
/// knee whose hardness was fitted as well came out only 1.5 dB closer, so the
/// cubic the bus already has is used. A gentler sweep showed a soft bend well
/// below the rails as well, about -40 dB, which is 15 dB under the
/// oscillator's own harmonics and is left out.
inline constexpr double kVcoMixerMismatch = 2.44;

/// Which tonewheel band a partial at this frequency reads.
inline int wheelBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kWheelBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

/// Where each of the combo organ's bands below starts, in Hz.
///
/// A transistor combo organ divided down from one oscillator per pitch class,
/// recorded on its 8' lever, the one of its drawbar-like levers closest to a
/// sine, on every C and F# of the main keyboard, and on its 16' lever for the
/// octave below, where the two match within a few decibels at the same pitch.
/// The tone is a square wave rounded off by a filter, mostly odd harmonics,
/// and it darkens steadily up the keyboard, so the takes are grouped into
/// bands where their third, fifth and seventh harmonics stay within 2.5 dB.
/// Each edge is halfway in pitch between the bands either side. Above the 8'
/// lever's highest note the last band holds: the higher levers are filtered
/// brighter, so they are a different voicing and not more of this one.
inline constexpr std::array<double, 7> kComboBandEdges{
    77.8, 220.0, 311.1, 439.9, 622.2, 879.9, 1759.7};

/// The combo organ's bands, from the bottom.
///
/// The level of each harmonic is the power mean of the takes in its band, and
/// its phase the phase of their sum, as for the tonewheel.
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
/// The even harmonics are 53 to 80 dB down, except at C7, where the second
/// and sixth came out at -42 and -55 dB. That key leaked its 4' lever at
/// -42.7 dB with no levers out, so those two are the key's other levers and
/// not the 8' tone, and the top band takes the band below's instead, as the
/// rest of the organ's leakage is left out. Each key leaks its other levers
/// 55 to 75 dB under the 8' tone, the 2 2/3' most, which is quieter than the
/// tonewheel's leakage and is not added. The 50 Hz hum is left out as well.
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

/// Which combo organ band a partial at this frequency reads.
inline int comboBandFor(double freq) noexcept {
  int band = 0;

  for (double edge : kComboBandEdges)
    band += freq >= edge ? 1 : 0;

  return band;
}

/// The combo organ's hiss, in dB per hertz against a single note's
/// fundamental, below and above kHissSplitHz.
///
/// Measured between the harmonics and the mains hum, against C4 on the 8'
/// lever. With no key down it is within a decibel of what it is under a note,
/// -88.8 against -87.9 below the split and -92.6 against -92.4 above it, so
/// the organ hisses whether or not it is played, and all of it is fixed:
/// sized by the loudest note sounding, so there is still none when nothing
/// plays. About 50 dB under a note in all, which is what a transistor organ
/// sounds like and twelve decibels more than the frequency modulation
/// synthesizer.
inline constexpr double kComboNoiseLowDbPerHz = -87.9;
inline constexpr double kComboNoiseHighDbPerHz = -92.4;
inline constexpr FixedHiss kComboFixedHiss{1.0, 1.0};

} // namespace ovt
