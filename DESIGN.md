# Design notes

Why the instrument is the way it is: what each part does, what the alternatives cost, and the measurements behind the numbers.

For what it is and how to get it, see the [readme](README.md). For how the code is arranged, see [ARCHITECTURE.md](ARCHITECTURE.md). For how to build and test it, see [CONTRIBUTING.md](CONTRIBUTING.md). The [project page](https://benjamindehli.github.io/overtonium/) covers the same ground for someone playing the instrument rather than working on it. This is the longer version, with the reasoning.

## Presets

Thirty-eight presets ship with it, listed alphabetically. Most were set up by hand on the panel and converted directly from the saved file, so what ships is what was played, not something written afterwards to approximate it. Where a row of the mixer follows a formula, the preset gives it, as the saws do with `1.0 / n`. Where it was drawn by hand, the thirty-two values are written out as a list so the curve is at least visible.

_Init_ is a preset like the others, not a reset to the parameter defaults. It clears the patch down to a short, bright three-partial pluck, which is a better starting point than silence. Like every other preset it leaves the session alone, so your tuning, polyphony, bend range and master fader stay as they are when you load it.

The session is defined by where a control sits in the interface, not case by case: everything the Settings menu offers, plus the master fader. The menu is where the instrument is set up and the panel is where the sound is made, so a control's place says which kind it is. Adding a setting means adding it to `kSessionParamIds` in the same commit. Both kinds of preset follow that list and match each other. A preset you save carries the same set of controls as a factory one. A test checks this by comparing what `capture` writes against what `neutralBase` decides, in both directions.

Twenty-nine of them use STRETCH or TRACK, since most of them model something with a body. Picking odd partials is only a stand-in for inharmonicity, and six of these add the real thing on top of it:

| Preset           | Stretch  | Tracking   | Why                                                                                                     |
| ---------------- | -------- | ---------- | ------------------------------------------------------------------------------------------------------- |
| 2-bit Fuzz Organ | +7 ct    | 1.6 dB/oct |                                                                                                         |
| 60s Organ        |          | 6.0 dB/oct |                                                                                                         |
| Big Saw          | +2 ct    | 1.3 dB/oct | barely any of either, just enough to stop the partials locking in phase                                 |
| DigiLog          | +30 ct   | 3.0 dB/oct | the series comes in from the bottom over five seconds, so the stretch is heard as arrival, not detuning |
| Dire Dire EP     | +8 ct    | 6.0 dB/oct | steep tracking keeps it soft under the fingers rather than glassy                                       |
| Drawbar Organ    |          | 3.2 dB/oct |                                                                                                         |
| Dream Phase      |          | 2.6 dB/oct |                                                                                                         |
| EP Chimes        | -1200 ct | 1.4 dB/oct | the 32nd partial pulled down a whole octave, an artefact rather than an instrument                      |
| Equal Saw        |          | 1.0 dB/oct |                                                                                                         |
| FM Piano         |          | 2.1 dB/oct |                                                                                                         |
| Glass Armonica   | +180 ct  | 4.0 dB/oct | barely any, but enough that the upper partials beat against the fundamental instead of locking to it    |
| Glide            | +0.2 ct  | 2.6 dB/oct |                                                                                                         |
| Glockenspiel     | -294 ct  | 3.9 dB/oct | a bar rings nowhere near whole numbers, and here STRETCH pulls the series in rather than spreading it   |
| Just Saw         |          | 1.0 dB/oct |                                                                                                         |
| Lo-fi            |          | 4.0 dB/oct |                                                                                                         |
| Metallic Piano   | +24 ct   | 9.0 dB/oct | a piano string is stiff, and a thin one rings further from whole numbers                                |
| Music Box        | -23 ct   | 1.5 dB/oct | a comb tooth is a bar and does not ring in whole numbers                                                |
| Nylon EP         | +7 ct    | 3.2 dB/oct | there is little above the low partials already, so the tracking finishes rounding it off                |
| Omni-84          |          | 7.0 dB/oct |                                                                                                         |
| Slow Pad         |          | 2.5 dB/oct |                                                                                                         |
| Space Cowboy     | +0.06 ct | 3.8 dB/oct |                                                                                                         |
| Space Flute      | +154 ct  | 2.0 dB/oct | far enough out that the upper partials stop sounding part of the note, which makes it breathy           |
| Sparkle Pad      | +81 ct   | 0.7 dB/oct | the sparkle is the top of the series, so the tracking is kept low                                       |
| Stepped          | +0.5 ct  | 2.0 dB/oct | a tiny amount, so the partials that are not jumping still beat against each other                       |
| StyloPoly        | +0.3 ct  | 0.4 dB/oct | barely any of either: a Stylophone is a reed, not a body, and thinning it would remove the buzz         |
| Synth Ensemble   | +10 ct   | 3.0 dB/oct | just enough that no two partials lock, which is what makes it sound like an ensemble, not one player    |
| Tape Choir       |          | 3.5 dB/oct | voices thin out at the top of a range rather than getting brighter                                      |
| Vibraphone       |          | 3.0 dB/oct | a short bar carries less above its fundamental than a long one                                          |
| Wurli            |          | 3.1 dB/oct |                                                                                                         |

_Space Cowboy_ and _KarplusStrong_ use both strings. _Space Cowboy_ is a tremolo guitar plucked from the noise channel's string, over a pad and a whistle that glides, with the resonator in Open ringing under all of it. _KarplusStrong_ is a lone fundamental and the noise channel's string struck together, with the resonator in Piano answering both. _Metallic Piano_ uses the resonator in Piano mode, so the strings of held keys ring and the pedal frees the rest. _Cathedral_ is a principal chorus that builds slowly, in a nine second room. _Glass Armonica_ is rubbed glass, with a key-off level above the sustain so the rim keeps ringing when you lift your finger. _Music Box_ is a comb of teeth, each ringing for a different length and placed at a different point in the field. _Tape Choir_ is worn echo and drift, with the partials spread across the field.

Three of them model a particular instrument. _Wurli_ is a Wurlitzer 200A, _Metallic Piano_ a thin, metallic upright, and _Omni-84_ the SonicStrings Voice 2 from a Suzuki Omnichord OM-84 System Two.

Eleven of them use the converter at all, and four are built on it. _Big Saw_ and _Metallic Piano_ quantise to 8 bits at the host's own rate. _Lo-fi_ runs the whole voice pool at 8 kHz and 8 bits, and _2-bit Fuzz Organ_ at 8 kHz and 2 bits. In those two the aliasing and the quantisation noise are the distortion itself, not an effect added on top. On _2-bit Fuzz Organ_ the aftertouch blends the root note towards a fifth.

_Drawbar Organ_ and _Cathedral_ are deliberately left without STRETCH. A drawbar organ is electric and a pipe organ is voiced rank by rank, so neither loses its top as you play up, and adding that would model the wrong instrument. The same goes for the saws, where the raw spectrum is what you want.

**Saving your own.** The preset menu saves to `Dehli Musikk/Overtonium/Presets` in your user application data folder, one small `.ovtpreset` file per preset, and lists whatever it finds there below the factory list. The menu reads the folder every time it opens, so a preset you just saved is in it and one you deleted in Finder is not.

A preset holds parameter values, and beside them only the channels each macro on Selected reaches, which are part of the sound but not parameters. It does not hold the window size, the zoom or the LINK settings, because loading a sound should not move your window or change your tools.

Factory presets start from a neutral base, not from your current settings, so a preset always gives the instrument it describes. That includes the globals that are part of the sound: STRETCH, TRACK, the converter and phase reset all go back to neutral unless the preset sets them.

What a preset never touches is listed once, as `kSessionParamIds`, and this applies to every preset including Init: polyphony, Legato, No stack, bend range, the aftertouch source, what the slide reaches, MPE, phase reset, the limiter's lookahead, the temperament, its root, the reference pitch, and Adaptive's Retune, harmonic sevenths and No drift. How you play the instrument and what it is tuned to are not part of a patch.

The output level is not on that list. The safety clipper has eight machines, and the master fader is the drive into that stage. A patch meant to lean on the Asymmetric curve, or to sit under the Limiter's ceiling, has to set how hard it pushes. A preset that could choose the shape but not the level could describe neither. So loading a preset moves the output level. That is a real cost, and it is written down here so it does not come as a surprise. Both halves are tested from that same list, so the code and the test cannot disagree about the rule. Every preset is loaded twice, once from a clean state and once after deliberately scrambling everything, and must come out identical. Then all thirty-eight presets are loaded in turn against a session set to Werckmeister on F at 415 Hz, and that session must survive.

Values are stored plain, not normalised, so a preset still loads correctly if a parameter's range is widened later. Anything a file does not mention keeps its default rather than being reset, so a preset saved by an older build loads into a newer one without silently zeroing whatever was added since. The tests cover both directly.

**Choosing one over MIDI.** A program change loads a factory preset by its position in the alphabetical list, counting from zero. A number past the last preset is ignored, not wrapped back to the start. Program changes are accepted on any channel, MPE included, where the parser has no use for them and would otherwise swallow them. Your own presets have no number, because you can add to and rename that folder at any time, so nothing in it stays in place long enough for a clip to point at it.

Loading a preset means hundreds of parameter changes, and each one has to be reported to the host. So the audio thread only writes the number down, and a 20 Hz timer on the processor does the loading. Posting a message from the audio thread instead would take the message queue's lock, which is the same reason the scratch buffer cannot grow there. A delay of up to fifty milliseconds cannot be heard, and a preset change is not a sample-accurate event anyway: it is hundreds of parameters moving, not a note.

It loads even if the preset is already the one showing, which matches the plugin's own menu and how an instrument with a panel behaves. A clip that starts with a program change therefore sounds the same on every pass. The cost is that it replaces anything you changed by hand since it last fired.

**Undo.** The parameter tree has an undo history. This matters most because of LINK: one drag can move the same knob on all 32 channels, and without a history the only way back from an accidental drag is to reload the preset.

**The history holds what a person did, and nothing else.** The value tree is deliberately not given the undo manager, because a tree with one records every write to it, and most writes are not someone editing. A host playing an automation lane writes continuously for the whole piece. A history full of a fader that was automated three minutes ago is useless: the undo you wanted is a hundred steps back and you will never find it.

Gestures tell the two apart. Every control opens a gesture before it writes and closes it after, which is how a host learns that a move has started and ended. Automation does not: it sets values without a gesture. So the processor listens for its own gesture begin and end. It takes a baseline of every parameter when the first gesture opens, and when the last one closes it puts whatever actually changed into the history as one step. A drag, a scroll wheel and a LINK drag across 32 channels are each one gesture and so each one step, however many values they changed and however long they took. A gesture that ends where it began is not a step.

This only happens on the message thread. A host may open a gesture from the audio thread, and taking a baseline of 850 parameters there would allocate on it, so a gesture from any other thread is ignored. Nobody turns a knob from the audio thread.

Edits that are not one gesture go through `recordEdit`, which takes the same baseline around whatever it is given. Loading a preset writes hundreds of parameters and has to undo in one step. The caller decides whether to record, and that is the purpose of the function. Loading a preset from the menu is recorded. A clip firing a program change at the same `applyFactoryPreset` is not, because the first is editing and the second is playing.

Letting the tree record everything and splitting it into steps afterwards, by waiting for the tree to go quiet, can be made to work if the quiet is measured from the right thing. But it can only group writes, not tell them apart, so it records automation along with everything else.
Cmd-Z and Cmd-Shift-Z work where the host passes them through, and many hosts do not, since a DAW usually keeps those keys for its own history. So Undo and Redo are also at the top of the Settings menu, which always works. Loading a session clears the history, since undoing your way back into someone else's edits is not useful.

**Making a factory one.** Configure with `-DOVERTONIUM_PRESET_AUTHORING=ON`, which adds "Copy as factory preset code" to the preset menu. It is off by default, because its output is only useful to someone about to rebuild the plugin. Set up a patch and choose that entry, and the C++ for the patch goes on the clipboard as a `case` that starts from `neutralBase()` and sets only what differs from the default. Paste it into `Presets.cpp`, add the name to `kNames`, and tidy it by hand where the shape has a formula rather than 32 separate numbers. Factory presets are kept as code rather than as embedded data so they can say `1.0 / n` instead of listing values.

## The tuning system

For harmonic `n`, the just interval above the fundamental is `1200 * log2(n)` cents. Overtonium rounds that to the nearest semitone to get the equal-tempered position, and the remainder is the cent offset that the TUNE knob dials in:

```
semitoneOffset(n, blend) = round(1200*log2(n)/100) + blend * (1200*log2(n) mod 100) / 100
```

At `blend = 1` this is exactly `n` times the fundamental. Nothing is hard-coded, but rounded to whole cents the table comes out as the familiar one:

| n   | semis | cents | interval      |     | n   | semis | cents | interval      |
| --- | ----- | ----- | ------------- | --- | --- | ----- | ----- | ------------- |
| 1   | 0     | 0     | prime/octave  |     | 17  | 49    | +5    | minor second  |
| 2   | 12    | 0     | prime/octave  |     | 18  | 50    | +4    | major second  |
| 3   | 19    | +2    | fifth         |     | 19  | 51    | -2    | minor third   |
| 4   | 24    | 0     | prime/octave  |     | 20  | 52    | -14   | major third   |
| 5   | 28    | -14   | major third   |     | 21  | 53    | -29   | fourth        |
| 6   | 31    | +2    | fifth         |     | 22  | 54    | -49   | tritone       |
| 7   | 34    | -31   | minor seventh |     | 23  | 54    | +28   | tritone       |
| 8   | 36    | 0     | prime/octave  |     | 24  | 55    | +2    | fifth         |
| 9   | 38    | +4    | major second  |     | 25  | 56    | -27   | minor sixth   |
| 10  | 40    | -14   | major third   |     | 26  | 56    | +41   | minor sixth   |
| 11  | 42    | -49   | tritone       |     | 27  | 57    | +6    | major sixth   |
| 12  | 43    | +2    | fifth         |     | 28  | 58    | -31   | minor seventh |
| 13  | 44    | +41   | minor sixth   |     | 29  | 58    | +30   | minor seventh |
| 14  | 46    | -31   | minor seventh |     | 30  | 59    | -12   | major seventh |
| 15  | 47    | -12   | major seventh |     | 31  | 59    | +45   | major seventh |
| 16  | 48    | 0     | prime/octave  |     | 32  | 60    | 0     | prime/octave  |

The test suite checks this table, so the formula cannot drift away from it unnoticed.

**Six of the thirty-two knobs have nothing to move**, and the table shows why: 1, 2, 4, 8, 16 and 32 read zero cents, because an octave is 1200 cents in both equal temperament and just intonation. On those channels both ends of the TUNE knob give the same pitch. The strip's tooltip says so on those channels, so the knob does not look broken.

Giving those knobs a second job is the obvious idea, and it does not work. Zero on the knob means the tempered position, which for an octave is the exact ratio. Redefining that end would make equal temperament mean something other than equal temperament, and _Equal Saw_ sits at zero on all six. One is the default, where thirty-one of the thirty-eight presets and every untouched patch sit. The other five hold something else there, seventy-seven knobs between them, so changing either end would change sounds that already exist. The knob is not greyed out either. A LINK drag down the TUNE row would then move twenty-six channels and skip six, and that drag is how a patch like Equal Saw is set up.

What does move an octave partial is STRETCH, on its own curve, along with DRIFT, the pitch modulator, and the character's rack, which puts every partial except the first a few cents off spec. What is missing is a static per-channel detune, which no control here offers: the blend reaches the exact ratio and stops.

### Tuning the keyboard

Everything above is about where a partial sits above the note you played. This section is about where that note sits. The default is twelve-tone equal temperament, `440 * 2^((n - 69) / 12)`.

An instrument about tuning should be able to tune the keyboard too, not only put the partials in just intonation. Settings has a Temperament entry with nine, eight fixed and one that retunes as you play:

|                        | Major third | Fifth  |                                                             |
| ---------------------- | ----------- | ------ | ----------------------------------------------------------- |
| Equal                  | 400.00      | 700.00 | the reference everything else is measured against           |
| Just (major)           | 386.31      | 701.96 | pure by design, chosen interval by interval                 |
| Pythagorean            | 407.82      | 701.96 | every fifth pure, at the cost of the third                  |
| Quarter-comma meantone | 386.31      | 696.58 | fifths narrowed a quarter comma, which makes the third pure |
| Werckmeister III       | 390.22      | 696.09 | four fifths narrowed, no wolf, every key playable           |
| Vallotti               | 392.18      | 698.04 | six narrowed by a sixth, F-C to E-B, gentler still          |
| Kirnberger III         | 386.31      | 696.58 | four narrowed a quarter comma, so the root's third is pure  |
| Young                  | 392.18      | 698.04 | Vallotti's six a fifth higher, C-G to B-F#                  |
| Adaptive               | 386.31      | 701.96 | pure in every key, chosen note by note as you play          |

They are derived from the circle of fifths, not copied from tables of cents. Almost every historical temperament is described by how much each of the twelve fifths is narrowed, so that is what the code specifies, and the pitch classes follow from it. The two commas everything is built from come out at 23.460 and 21.506 cents, which are the published figures. The tests check the property that defines each temperament rather than the numbers that result: meantone's third pure to a thousandth of a cent, Pythagorean's fifth likewise, Werckmeister at its characteristic 390.2, Kirnberger III's third pure.

Vallotti and Young are one pattern of fifths on roots a fifth apart, so Vallotti on C is Young on F, note for note, and a test checks this on every root. Both are offered because players know them by both names, and a player who knows they want Vallotti should not have to work out that it is Young on another root.

**The menu order is not the parameter's order.** The menu lists equal and just first, the historical temperaments by age, and Adaptive last. The parameter lists them in the order they were added, with Kirnberger III and Vallotti after Adaptive. A saved session stores the position in that list, so a new temperament anywhere but the end would change what old sessions are tuned to. A host's automation stores a fraction of the list's length instead, so adding to the end still moves a lane drawn against the shorter list, and the release notes say so.

**Root** picks which pitch class the temperament is built on. An unequal temperament is only in tune in the keys near its centre, and that is intended. Root is greyed out for equal temperament, which has no centre, unless the resonator is in Series. Series rings the root's own harmonics whatever the temperament, so the root decides what it rings, and it stays live on the Tuning bar and in Settings for as long as Series is chosen.

**On C, each temperament has its standard layout.** For the well temperaments this follows from starting the chain of fifths on the root. Werckmeister III on C tempers C-G, G-D, D-A and B-F#, as Werckmeister wrote it. Pythagorean and quarter-comma meantone have a wolf, and a chain started on the root would put it on F-C, which leaves F and Bb major unusable. Those two start their chain three fifths below the root instead, `kWolfBelow`, so on C it runs from Eb to G# and the wolf is G#-Eb. That is where organs and harpsichords put it, and it keeps the keys most of their music is written in. Before 1.15.0 the chain started on the root, so a session saved with either temperament sounds different when opened in 1.15.0.

**Reference pitch** offers 415 to 466 Hz. A stays exactly at that pitch in every fixed temperament, because the offsets are measured from A: a tuner tunes A first and works outwards. Only the other eleven notes move. Under Adaptive, A's middle version sits at the reference pitch.

Equal temperament at 440 matches `440 * 2^((n - 69) / 12)` bit for bit, and the tests check this across all 128 notes.

None of the three is saved with a preset. A temperament belongs to the music you are playing, not to any one sound in it. You set it once and work, and a patch that put you back in equal temperament partway through would get in the way. That includes Init, since Init is still a preset and clears the patch, not the session.

### Adaptive

Every fixed temperament is in tune in some keys and out in others: just major on C plays I–IV–V–I in G with a chord 21.51 cents out, a whole syntonic comma. Adaptive follows Eivind Groven's solution, from the organs he built from 1936 onwards: every key has more than one pitch, and the instrument chooses between them as you play. `SynthEngine::settleAdaptive` makes the choice, and every way a voice gets its pitch goes through it.

**Groven's tempered fifths.** Each fifth is narrowed by an eighth of a schisma, 0.244 cents, so eight fifths and a pure major third close five octaves exactly. With pure fifths the gap stays at 1.95 cents, and chords far from the root are that far out. On pure partials that is slow beating anyone can hear: a randomised test of chords with no release found a held interval two cents out in one sequence in fifteen. With tempered fifths the major thirds stay pure and a fifth is a quarter of a cent narrow, which beats about once every eighteen seconds on the third partial of a note in the middle of the keyboard. That is why "pure" in the tests means within a quarter of a cent.

**One chain of fifths.** With the schisma gone, every pitch the temperament can play sits on one chain of these fifths. A fifth is one step up and a major third eight steps down, and the versions of a pitch class are twelve steps apart, one comma of 20.53 cents. A pair of keys is pure exactly when their versions are a fixed number of commas apart, set by the interval the keys spell: seven semitones is a fifth however it is tuned. Judging a pair by whichever consonance it lands near would be wrong, because a version two commas out lands near nothing and scores as free, and a test that judges the same way would pass it.

**The held chord is solved.** Every key that is down or held by the pedal, plus the one going down, is a node, sorted lowest first. Every pair is an edge that wants a fixed difference, set by the interval its keys spell. The edges are taken strongest first: octaves, then fifths and fourths, then thirds and sixths, then sevenths. Each edge is kept if it agrees with those already taken. Seconds and the tritone come last, at 16:15, 9:8 and 45:32, the sizes just major gives them. They count for nothing, since there is no one pure size a chord aims for, but they connect notes that nothing stronger connects, so every held key is in one group. Without them a D between C and E would be unconnected and sit wherever its pitch class last was, so C D E would come out two ways depending on what came before. Taking the strongest first can give up more than it needs to: in C E G A D the fourths E to A and A to D are taken before any third, which leaves three thirds and sixths a comma out to keep one fourth. So each key is then tried a comma either way, alone or together with one key a consonance away from it, and kept there if the chord is less out of tune. Octaves weigh 4, fifths and fourths 3, thirds and sixths 2 and sevenths 1, and this repeats until no move helps. Compared with a brute-force search of every assignment, over the 125 chord types of [music-utils](https://github.com/benjamindehli/music-utils), a library for naming chords from the MIDI notes held, in all twelve keys, this finds the least out-of-tune tuning for all but the Petrushka chord. Sorting the keys means a chord that cannot be fully pure gives up the same interval whatever order its keys went down in. What is left free is where the whole chord sits. That is chosen to move the fewest held keys, then to agree with what is still ringing, then to keep each pitch class where it was last. So the keys held decide every interval, and the order they were played in decides only where the chord sits, in whole commas. Struck three ways, all at once, one key at a time from the top down, and one key at a time in a turned order, every one of those 1,500 chords comes out the same. `testAdaptiveChordTypes` in the DSP suite plays them all on every build, with the list copied from music-utils into `Tests/music_utils_chord_types.h`, and holds the engine to both results. A key going down or up costs 28 µs on average with sixteen held, against 17 µs without the search.

**It runs on every key, down and up.** A key decided only once, when it goes down, is tuned to whatever it went down against. A G sharp struck while an F is still held becomes an A flat, and the E major it ends up in is two commas out. Without settling the chord on every change, a chord turning into the next one note at a time came out like that in four sequences out of five. Keys struck within the chord window, 40 ms, are settled together, since otherwise the first key of a chord chooses with nothing to go on. A held key that has to move slides there over `adaptiveRetune`, a time from zero, where it jumps, to one second, 30 ms by default. Inside the chord window it moves at once. The knob is in the Tuning bar, and the Settings menu offers 30 and 300 ms as Fast and Smooth. In chords changing one note at a time between related keys, a held note slides on about one change in four, mostly to or from the harmonic seventh as the chord around it changes. A chord struck and released together never moves one of its notes.

**Five versions, two commas either side.** Three are enough for chords struck at once but not for arpeggios, where a key arrives after the chord window: with three, half of all arpeggiated progressions in a set of thirty-two ended a comma out. Five keep every one pure. The cost is that the keyboard can drift two commas from where it started rather than one.

**No drift, for when drift is unwanted.** A session switch, `adaptiveHome`, off by default, makes closeness to home the first priority when choosing where a chord sits, ahead even of keeping held notes still. A progression that would otherwise pump then moves a held note by a comma, at the Retune speed, instead of taking the keyboard with it. A key struck into silence goes home instead of to where its pitch class last was. Played legato twice round C, A minor, D minor and G, with the shared notes held across each change, C ends 41 cents low with the switch off, two commas, and exactly where it started with it on, with every chord pure either way. It is a NO DRIFT cap in the Tuning bar and No drift in Settings, and the host calls it Adaptive No Drift.

**What it weighs.** Tails count while they are at least a quarter of the loudest voice and above -60 dB, and only to settle what the held keys leave open. If a dying chord could outvote the key under the finger, it would put a held fifth a comma out. The floor matters too. Without it, the inaudible remains of the last chord are each a quarter of the loudest of themselves, and a key struck into silence tunes against them instead of taking the version its pitch class used last. That memory is what keeps the comma pump bounded once chords stop overlapping, and it is cleared on reset.

**Sevenths.** A seventh counts least of the intervals, a minor one at 9:5 and a major one at 15:8, so it only decides what nothing stronger decides, and it comes out the same with or without the fifth below it. The harmonic seventh is the exception. It was chosen by ear from four clips of G7 resolving to C, using equal, 9:5, 16:9 and 7:4: a minor seventh 7:4 above its root makes a dominant seventh 4:5:6:7 and matches the seventh harmonic every partial already has. There is no 7 on a chain of fifths, so that one note is left out of the solve and placed against the root afterwards. A seventh is harmonic unless a held note would then be out of tune with it. That happens with a minor third or a fourth above the root, which would sit a fifth or a fourth from the seventh, 48 and 30 cents off, or with a minor second or a tritone above the seventh, which would meet it as a third well off pure. The ninth, the major third and the fifth stay pure with it, as harmonics 9, 5 and 6 over the root as 4. So a dominant seventh, a ninth, a bare seventh and a seventh over a fifth are all 7:4 and do not beat at all, while a minor seventh chord keeps 9:5, a suspended one 16:9 and a sharp ninth 9:5. The exceptions are defined by what they would put out of tune, not by chord. A rule based on chord shapes reads C E flat D, a minor chord with an added ninth, as a seventh over D and puts the C 28 cents off its own minor third. Playing every chord type in music-utils through the engine shows this, and it is why naming the root the way that library does is not used to decide sevenths. The lowest qualifying root wins. Like the rest of the solve it depends only on the keys held, so a held note slides to 7:4 or away from it when the chord around it changes. Keeping a held note off 7:4 unless it was struck with its root would avoid those slides, but would make C then B flat beat where C and B flat together do not. Left to the chain, the seventh would be 9:5, 17.6 cents sharp of equal, which is the tense, bright seventh the clips made obvious. A session switch, `adaptiveSeventh`, on by default, turns the harmonic seventh off for anyone who wants the chain's sevenths. It is a 7:4 cap in the Tuning bar and Harmonic sevenths in Settings.

**The root is not the key.** It places the middle versions and centres the limit on drift, and every key is as pure as every other: I–IV–V–I comes out pure in all twelve on a root of C. A root that followed the music would move the centre with every modulation, and a progression travelling round the keys could then drift away without limit.

**The tests** measure what the engine tuned, by the interval the keys spell: I–IV–V–I in G pure where fixed just is 21.51 out, and in all twelve keys, the comma pump four times within its bound, a held A unmoved when the chord under it changes, a lone D taking its last version, a D fitting a ringing F major, random chords struck and arpeggiated with and without tails, chords changing a key at a time, a held note moving one comma when C and F sharp are joined by an A, nine chords struck six ways and one reached from G minor all tuned the same, C6 with an added ninth keeping its thirds and C minor with an added ninth its minor third, and the two retune speeds on one voice. Each mechanism has been switched off to show that its test fails.

### The shape a modulator traces

Both per-partial modulators have a shape as well as a rate and a depth. Pitch offers eight, amplitude seven, and each of the thirty-three channels has its own. That is what makes it useful: gating the fifth partial while the fourth pulses is not something one global LFO could do.

Two of the eight cost almost nothing. The smooth random contour is DRIFT, drawn as a Catmull-Rom spline through random points, and sample and hold reads the same points without the interpolation. They share one implementation, so the two cannot disagree about what smooth random sounds like.

The square and stepped shapes are affordable because of where modulation happens. Every modulator is stepped once per 32-sample control block, and a partial's gain ramps linearly across that block rather than jumping. So an edge arrives as a ramp of about two thirds of a millisecond: short enough to sound like an edge, long enough not to click. Measured, the worst sample-to-sample step across every shape at full depth is 2.2 times the slope of the tone itself, against the threshold of 3 that the click tests use.

Amplitude has seven rather than eight because a tremolo only ever brings the level down from the fader. A unipolar shape can reach nothing that a bipolar one does not already reach, so that side has one square, and it gates the whole depth. Everything on that side is also read a quarter cycle ahead, which keeps Sine the cosine the tremolo has always been: a note starts at full level and dips. Without that, every preset with a tremolo would start at a different point.

Random is a spline through its points and overshoots them by a few percent, so the amplitude is clamped. Otherwise the one shape meant to duck a partial could briefly push it above its own fader.

The pitch depth reaches an octave, which is for the shapes that step rather than sweep. A square on the pitch is a trill and sample and hold is a run of random notes, and both are worth having at real intervals, not only at vibrato depth. At full depth the bipolar square jumps an octave either side of the note, the unipolar one an octave up, and sample and hold lands anywhere in between, measured at -1167 to +1098 cents over a few seconds. The range belongs to the knob, not to the shape next to it, so a sine can sweep further than anyone needs. That is the smaller problem. A knob whose range changed with the shape would either show a number it was not doing or have a dead section at the top. The taper keeps the bottom where it was anyway: both settings reach 25 cents at half travel, and the earlier full range ends at about three quarters.

LINK does not reach the shapes. It drags a value across the series along a weighted curve, and there is no such thing as half a sawtooth. Instead the shape menu offers to set every channel at once, which does the same job in the only way that works for a list.

### One modulator for the keyboard, or one per note

A modulator belongs to a note by default. Strike a chord one note at a time and each note's tremolo starts where that note started, so the three of them pulse out of step for as long as they sound. That is what a digital instrument does, and it is what nearly every patch here wants, since thirty-two partials pulsing out of step is most of what makes the mixer sound like a mixer.

An instrument with one tremolo circuit behaves differently. A Wurlitzer's tremolo is in the amplifier, after the reeds, so a chord pulses as one however it was played. A Stylophone has one oscillator and so one vibrato, with nothing to be out of step with. Two switches give each modulator that behaviour: the phase belongs to the channel instead of the note, and a note that arrives late joins whatever is already running.

**Two switches rather than one**, because the two modulators are two separate circuits. Everything else about them is already separate, down to their shape lists, and a patch may want a tremolo shared across the keyboard with a vibrato that each note keeps to itself.

**A channel's modulator, not the instrument's.** Each of the thirty-three keeps its own rate and shape, so what is shared is one circuit per channel, not one for everything. Partial 7's tremolo is one thing that every note played through partial 7 hears. That is the same idea as the rack of oscillators a character brings, and it is why a preset like _Shimmer_, built on thirty-two rates that never line up, still sounds like itself with the switch on.

**It is worked out before any voice runs**, and the implementation depends on that. A voice renders a whole buffer at a time, so a shared phase stepped inside one voice's loop would be stepped again by the next voice, and the two would hear different things. Instead the engine walks the same control blocks the voices are about to walk and writes each channel's value at every block boundary into a table. The table has one more boundary than there are blocks, so each block has both its start and end values. The voices read from it. This also makes it exact for the two random shapes, which draw a new point each time their phase wraps and could not be kept in step by giving a note a starting phase.

The table is a fixed member, sized for 2048 frames at a time, so the audio thread never allocates and a host's usual buffer is handled in one pass. Anything larger is processed in passes of that size.

**On the panel it is in the shape button's own menu**, as a ticked entry below the one that sets every channel at once. That entry is also a switch over all thirty-three reached from one channel, so the two match. The alternative was two buttons on the bar. Settings was the wrong place, since everything there is session rather than patch, and two of the presets need this one.

The bar fits on one row at 1285 px, and the window opens at 1350, which leaves 65 px, not enough for the two switches. The menu would be the right place even with the room, since a per-channel switch belongs on the channel, not on the bar.

**A switch next to the shape caption lights when it is on**, because it is easy to forget a mode you cannot see, and this one is saved in a preset: without it a patch could load with a shared tremolo and nothing on the panel would show it. It is the same switch as the menu entry, drawn as three strokes merging into one, and clicking it toggles it. There is one per modulator rather than a mark on each of the thirty-three shape buttons, since all thirty-three follow the one switch. The editor reads the two parameters on its housekeeping tick and once more before the first paint, so an editor opened on a patch that shares a modulator shows it immediately rather than a quarter of a second later.

**It costs about half a percent of a core.** Thirty-three modulators are stepped per control block whatever the polyphony, and each voice still steps its own alongside reading the shared one. That way, giving a channel its own modulator back lands where that note would have been, not where it was when the switch was flipped. Measured at eight voices, taking the minimum of several runs on a busy machine: 8.48% of a core with a modulator per note against 8.87% with both shared.

Two of the factory presets use it. _Wurli_ shares its tremolo and _StyloPoly_ its vibrato, the two cases the switches were built for.

### Stretch

TUNE chooses how the series is spelled. STRETCH decides whether the partials form a harmonic series at all.

Nothing real vibrates at exact integer multiples of anything. A string with any bending stiffness has partials at `n * f0 * sqrt(1 + B * n^2)`, sharp of the harmonic and more so further up the series. That, not the hammer or the soundboard, is what makes a piano sound like a piano rather than a sawtooth. It is also what a tuner matches when stretching the octaves: tune the top of the instrument to the theory and it beats against its own overtones.

STRETCH is B, set by its effect on the top partial rather than by its own value, which runs from 0.00003 to 0.008 and is not a useful number to work with. At +150 cents, which is a real piano:

| Partial                 | 2    | 8     | 16    | 32     |
| ----------------------- | ---- | ----- | ----- | ------ |
| Cents sharp of harmonic | +0.6 | +10.2 | +40.0 | +150.0 |

The bottom of the series barely moves and the top moves a long way, and that shape is what matters. Push further and the partials stop agreeing on a fundamental, and the sound turns from a note with a timbre into a bell. Negative values pull the partials inward instead, which no physical string does, but it is still useful.

It is a separate axis from TUNE, and the two combine: equal temperament with heavy stretch is a different sound from just intonation with the same stretch. Zero is the plain harmonic series, so a preset that does not set STRETCH sounds the same as without it, and the table above is what zero means.

### Tracking

Without TRACK, a patch has the same spectrum at every pitch, and nothing real does. Play up a piano and the top of the series thins out. The note has not changed: the body has a rolloff that stays fixed, and the partials climb up through it.

TRACK is that rolloff, in dB per octave above a fixed corner of 1 kHz, which is around C6. It is measured relative to the fundamental rather than absolutely, and that is the important part. Measured absolutely it would be a shelf, making high notes quieter rather than duller, which is not useful. Relative to the fundamental, the fundamental keeps its level at every pitch and only the spectrum above it thins.

The result follows from where the corner sits. A bass note has most of its series below 1 kHz and keeps nearly all of it. A treble note whose fundamental is already above the corner loses the full slope across every partial it has. At 6 dB per octave, the 32nd partial keeps 57% of its level at A1 and 4% at A5.

At zero TRACK is off, and has no effect at all.

Knobs come in three sizes and only three: 36 px in the top bar, 32 px for the main tuning knob at the head of each strip, and 26 px for everything below it. Nothing else varies, in the channel strips or the noise strip. This needs stating because a knob takes the size of the row it is in, so a row height typed two pixels off gives a knob two pixels off with no warning. A test walks the built editor and checks the count.

Strips are colour-coded by interval class, which keeps the structure of the series visible while you scroll. The twelve classes sit in chromatic order along a narrow band from blue at the octave, through magenta and red in the middle, to yellow at the major seventh. So octaves are blue, fifths rose, major thirds magenta, sevenths amber and yellow.

The band is deliberately narrow, a crop from the middle of a full blue to yellow sweep, so 32 channels look like one family rather than a rainbow. Saturation and value drop towards the warm end because yellow looks much brighter than blue at the same nominal value, and this keeps the sevenths from overpowering the octaves. Nothing in the band reaches green or cyan, where the accent colour of the global controls is, so the interface chrome never looks like one of the channels.

Every channel sits on the same grey. Alternating two shades to separate the strips would put a stripe behind every knob's interval colour, the lit meters, the lamps and the readouts, competing with all of them. Instead the strips are separated by their own lit and shadowed edges, a one pixel groove at every boundary, as on a mixing console.

**There is one grey in the mixer, and no strip is lighter than another**, the noise channel included.

Lightening the octaves, so the shape of the series would show when you are scrolled out at harmonic 28, or the noise channel, because it is not a partial, would both fail for the same reason. The hover highlight is a wash across a strip of about the same weight, so a mixer with either would have two kinds of lightened channel and no way to tell them apart without counting.

Neither is needed, because something more visible already shows it in both cases. The colour bar at the head of every strip shows which interval it is, octaves included, in colours nothing else on the panel uses. And the noise channel is the only strip with no interval colour on it and the only one headed with a name rather than a number, which is a bigger difference than three per cent of brightness.

**Every switch on the panel is a lamp behind a square plastic cap**, like the buttons on an old mixer or tape machine. That covers the character button, RESONATOR, ECHO, REVERB, CLIP, the tool, the four buttons that open something, and the M and S on all thirty-three channels. The cap sits in a moulded well. The well is bevelled rather than a hole with straight sides: four facets slope in to the opening, mitred at the corners.

**The moulding is raised from the panel rather than cut into it**, so it is lit like every other raised part here. Its faces slope down and outwards from the opening, the top tilts towards the light and the bottom away, and top and left are lit while bottom and right are in shadow. Its outer edge matches this, lit along the top and the left and shading to shadow along the bottom and the right.

The reference's own bezels are shaded the other way round, bottom and right lit, which is a block whose faces slope down and inwards. Both are real mouldings and the photograph is not wrong. It shows the other kind, and on a panel where everything is raised, a bezel that alone looks like a hole looks out of place.

It is also lighter than in the photographs, which show machines with near-black panels. This is the same trap as the well's own colour: the values do not carry over to a panel that is (20, 24, 29).

A dark stroke outside and a light one inside would show that there is an edge without showing which way it faces, and the cap would look as if it sat on the moulding rather than down inside it. The corners are where a bevel is read, so the facets are mitred. Four rectangles butted together overlap at the corners, and the overlap is a tone that belongs to neither. That is a seam in the wrong place, and it is the one thing that shows the four faces were drawn rather than moulded.

The outer corners are square except for the fewest pixels needed to stop them looking cut with scissors. A radius large enough to read as a chamfer rounds off the whole block, and the chamfer belongs on the facets.

**The amount of relief is measured from the reference, not chosen.** Relative to its own panel, the reference's top facet is at 1.70, its left at 1.26, and its right and bottom both at 0.67, so the whole frame spans about one panel's worth of tone. A frame spanning nearly one and a half, with a very bright top and a bottom barely darker than the panel, looks tall rather than like a moulding a few millimetres high.

**The moulding is narrow, which also makes it low.** The cap is what you look at, and the frame only has to be wide enough to read as a frame. Width and apparent height go together here: the facets carry the relief, and a wide facet looks tall, so a frame trimmed to look thinner also looks lower.

It is a seventh of the button's height, clamped between two and four pixels, and the clamp does most of the work. A tenth is too little once the seam is there. The seam takes the innermost pixel of the moulding, so a frame of two would be one pixel of facet and one of seam, which is an outline rather than a moulding. The tenth was picked before the seam existed and not re-checked against it.

The minimum applies to the small ones. A mute is 14 px and gets the two pixel minimum where a bar button gets three or four, so the mixer's frames are lighter than the bar's. Raising the minimum would even them out, but at the cost of the cap on every one of the sixty-six mutes and solos.

**The cap is snapped to whole pixels, and the frame is not.** A button's bounds are inset by half a pixel so the stroked outer edge of the moulding lands on one pixel rather than across two. A stroke needs that, and a fill needs the opposite. Left on half-pixels, the cap would have a row of partial coverage all the way round, darker than the face by whatever is behind it, which is a border nobody drew. The one place the snap is deliberately not applied is a gang's shared edge, where two halves have to add up to one moulding, or the divider down the middle comes out a different width from the frame around it.

CLIP subtracts its own bezel's width from the space around it. The two readouts beside it are plain recessed panels and CLIP is a lamp in a moulding, so with the same bounds its lit cap would be shorter than them by exactly that much, and the three would look like two sizes. The width is measured from the look and feel rather than written into the layout, so a moulding that changes width cannot quietly put the row out of line. It is also clamped to the bar: the space above is the meter's own rectangle, not spare room. The runtime suite checks that all three sit under the meter and inside the bar, which is what caught an earlier version taking its height from the wrong side.

**The cap does not move when the lamp comes on.** These are indicator lamps, not latching switches. A cap that sinks into its well while its function is on looks like a button whose press turns that function off, which is the wrong way round for ECHO and for a mute. So a cap stays raised whatever its lamp is doing, with the shadow it casts on the floor of the well and the line of light along its top edge. Only a finger pressing it moves it down, for as long as the finger is there.

**The cap is brightest across its middle and darker towards every edge.** The lamp is a point behind the centre of the plastic, so that is where the light is, and two washes show it. One runs down the face, peaking a little above the middle so the light still comes from above, as it does everywhere else. The other runs across it, flat through the middle third so a word sits on an even face and only the ends darken. The horizontal one is a wash rather than a radial gradient because these caps range from a 14 px square mute to a 62 px CHARACTER, and a circular hotspot would look like a disc on the wide ones.

**Nothing else is drawn on the face.** A white lip along the cap's top edge or a sheen over its upper half would both be bevels in disguise: a lit edge and a top-weighted wash are what a rounded or chamfered surface shows. Together they would bring back the roundness the flat fill removes. What shows the cap is raised is the moulding around it and the seam it sits in, which are both outside the cap, so nothing needs drawing on its face.

This took five rounds to settle, and the reason is worth keeping. "It still looks bevelled" can have several causes drawn in several different places, and each time one was removed the next was still there: the lip, the sheen, the end wash, the gradient, and a dark rounded rectangle stroked inside the cap's own edge, which was drawn last and so sat on top of all of them. Four of the five were changed on a guess before the fifth was found by reading every draw call in the function in order. Reading the draw calls should come first.

**A dark seam runs between the frame and the cap.** Every button in the reference has one. It is the gap left when a part is dropped into a moulding. Without it the two meet tone to tone and the cap looks laid on top of the frame rather than set into it.

A lit cap puts a trace of its colour on the moulding around it, and nothing more: an even wash over the whole bezel, not a halo around the cap. A halo is better physics but looks worse, because light fading out from a cap's edge is light escaping around the cap, which only happens if the cap has sunk below the moulding. A lit button with a halo looks pressed in however faint the glow is, so the problem is the shape, not the strength. There is no room for a real bloom anyway: the button ends at the bezel's outer edge, about three pixels out, which is too little to fade out in and is exactly the ring that looks like a gap.

The bezel is matched to this panel rather than copied from the photographs. Those machines have near-black panels, so their bezels can be near black too, and copying the value would put a pit in a panel that is (20, 24, 29).

**The caps are white translucent plastic and the lamp shines through them.** Unlit, every cap on the panel is therefore the same grey, (111, 117, 124), which is what white plastic looks like in an unlit room. The only colour anywhere is what is switched on, so a mixer at rest has none, and you read a row by where the colour is.

A lit cap is bright, because the lamp is behind the plastic and lights the whole face, and milky, because the light has come through a diffuser. Saturation goes up with the light, not down, which is the counter-intuitive part. These are chrome colours chosen to be read as small text on a dark panel, so they are already pale, and raising the value without the saturation pushes them towards white before the diffuser has any effect.

**The legend is the same near-black whatever the lamp is doing.** It is printed on the plastic, not part of what lights, so a word never changes colour under its own lamp and the state is read from the cap alone. Its contrast is 4.1 to 1 against the unlit plastic and between 4.9 and 11.4 to 1 against the lit.

**It is printed rather than painted, so it does not block all of the light.** Nothing is opaque at that thickness, and a word that blocked all the light would be the one part of a lit cap behaving like paint. It is thinnest over the middle, where the lamp is and the most light gets through, and denser towards the ends. Unlit, there is nothing behind it to let through and it is plain ink again.

An icon gets the same treatment but as a flat value rather than a ramp, and solid rather than translucent. Icons are drawn as overlapping shapes, three rules with a handle on each. With a colour that has alpha in it, every overlap is composited twice and the rule shows straight through the handle that should sit on top of it. That is a drawing bug, not translucency. The ink is mixed against the plastic and laid on solid, which gives the same colour on the face with no seams.

The cost is that brightness does not work as a cue. Against the unlit grey a lit gold is 2.8 times the luminance and a lit cyan 2.5, but a lit red only 1.2, since red is a dark colour at any value. So the mute shows itself by hue rather than brightness, which works at the size it is drawn because it changes from a neutral grey. How white the unlit plastic is trades these off directly, and it is one constant in `lampFace`: lighter looks more like white plastic and leaves the reds less room, darker does the opposite.

The mutes and solos are the most visible use of the cap, 66 of them across the mixer, and that is intended: their state has to be findable without reading anything. Red means a muted channel, gold means a soloed one, and at that size the colour is all there is to go on.

**They are a two-gang block, one moulding around the pair**, as on a console. At their size, two bezels facing each other across a gap would use most of the pair's width on moulding, and the wall is already at its two pixel minimum, so those two facing bezels are the only pixels left to recover. They stay two components, each lighting on its own, and both draw the well as the whole block. Each one shows only its own half, because a component clips to its own bounds. Drawing half a well each would mean mitring the joint and suppressing two edge strokes along it, for the same result.

**A channel that cannot be heard turns its lights off, rather than getting a wash over its face.** Muting a channel, or soloing another, puts out that strip's knob rings, both readouts and the macro columns beside its fader, and switches off its waveform screens, which show their ground and nothing on it. A lamp that is off is dark, not grey, so every tick of a ring is drawn unlit, the same as the knobs of the resonator, the echo or the reverb while it is off. Every value stays where it was set, and the pointer on each knob and the fader cap still show it. Only the light goes. The mute and the solo keep their light, because the mute is usually what silenced the channel, and dimming it would hide the reason the channel is silent. A wash over the whole strip cannot make that distinction, because it dims the pair along with everything else however carefully the rest is handled.

A macro's colour goes out with the rest. A macro is a light like any other, and a ring glowing on a channel nobody can hear would be misleading, whoever set it. So on a silenced channel where a macro has moved the value is not shown at all, and the pointer shows only what the patch holds. The pointer keeps the plain grey of the panel's writing rather than the channel's colour, since a lit pointer in a dark ring would read as one lamp left on.

The meters and the activity lamps need no special handling. A silenced channel gives them nothing to show, so they go dark on their own. The colour bar at the head of the strip stays lit, because it shows which interval the channel is, not whether it is sounding.

**The tool button is lit and stays lit.** A lamp going out means something is off, and there is no off here: the pointer is as much a choice as Link and Draw, and a dark cap would look like a disabled tool rather than the plain pointer being selected. The icon on it shows which tool is selected.

Nothing outside the mixer uses the band, because a colour from the band means a partial, and using it for anything else would give the colours a second meaning on the same panel.

**The standalone uses the same panel up to the top of its window.** Its title bar is JUCE's, not the platform's, so by default it is the grey-green of every unstyled JUCE application, with a red cross and a yellow dash for buttons, and it looks like another program's window with this instrument inside it. The editor gives that window its own look and feel instead. The bar is the same lit and grained panel as everything below it, and the two buttons are drawn like every other small mark here: dim until the pointer is over them, then lit, with the close button turning red because it is the one that matters if hit by accident.

This applies only to its own window. In a host the top-level window belongs to the host, and a plugin that restyled it would be changing someone else's application, so the whole path is behind a check on which wrapper it is running in.

Two details needed measuring. The name is centred on the window, not in the space JUCE offers for it, which is what is left between the title bar's own buttons. Those buttons are all at one end, and the standalone adds an Options button at the other end without telling the title bar, so a name centred in the remaining space sits left of the middle. The Options button is also moved back into place after every layout, because the standalone places it a fixed six pixels from the top of a bar and then subtracts eight from the bar's height, which leaves it off centre whatever the bar's height is. There is no hook for either, so the window is styled and then corrected.

**The standalone remembers its size**, because the editor's width and height are saved in the plugin's state and the standalone saves that state between runs. A fresh one opens wide enough for all 32 channels. One that has been resized opens at the size it was left at, and on macOS the file holding that is `~/Library/Application Support/Overtonium.settings`.

That leaves colour with one job, which it does at full strength. Every knob on a strip carries the channel's colour in its value arc and pointer, not only the tuning knob at the head. On one flat grey the colour is the only thing separating a channel from its neighbours, so desaturating nineteen knobs out of twenty to give the head of the strip more weight would waste the one thing that works.

## Controls

Each of the 32 strips has, top to bottom:

| Control                         | Range                                                           | Notes                                                                               |
| ------------------------------- | --------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| TUNE                            | equal to just                                                   | Readout shows the resulting cent offset                                             |
| PITCH MOD shape, rate and depth | Eight shapes, 0.01 to 30 Hz, 0 to 1200 cents                    | Per-partial vibrato, or a trill at any interval up to an octave                     |
| DRIFT                           | 0 to 25 cents                                                   | Smooth random pitch wander. See below                                               |
| ENVELOPE strike, delay, A, D, S | -100 to +100%, 0 to 5 s, 0.2 ms to 5 s, 1 ms to 20 s, 0 to 100% | Exponential decay. STRIKE aims key velocity at the front of it. See below           |
| KEY OFF swell, level, release   | 0 to 5 s, 0 to 100%, 1 ms to 20 s                               | A second envelope for letting go. See below                                         |
| AMP MOD shape, rate and depth   | Seven shapes, 0.01 to 30 Hz, 0 to 100%                          | Per-partial tremolo                                                                 |
| VELOCITY                        | -100 to +100%                                                   | How much key velocity scales this partial. Negative inverts it                      |
| AFTERTOUCH                      | -100 to +100%                                                   | How much key pressure moves this partial. Negative fades it out                     |
| PAN                             | hard left to hard right                                         | Where this partial sits in the field. Equal power, so the level holds as it crosses |
| M and S                         |                                                                 | Mute wins over solo. Right-click either to clear them across the mixer              |
| LEVEL                           | -inf to 0 dB                                                    | Fader spaced by decibels, with the meter filling its track                          |

**Clearing them.** Right-clicking an M or an S offers to clear either switch across the whole mixer, and shows how many there are to clear before you do. Thirty-three strips give a solo many places to be left on, and finding it by eye means reading thirty-three pairs of buttons four pixels apart. The entries stay in the menu, greyed out, when there is nothing to clear, because the count also answers the question that made you open the menu.

The menu belongs to the buttons rather than the strip. A right-click anywhere else in the mixer opens the LINK menu, and the buttons catch the click before it reaches the strip. A right-click on a switch acts on switches. Only the channels that are switched on are written to, so the host sees a gesture on those alone and an undo step covers what changed rather than all thirty-three.

Per-partial velocity lets a soft note have a different timbre, not just a lower level. Set the fundamental to 0% and the upper partials to 100% and the tone opens up as you play harder, which is roughly what a struck string or bar does. _Struck Bell_ and _Odd Harmonics_ ship with that curve already set.

Both controls run either side of zero. A positive amount means playing harder or pressing heavier makes the partial louder. A negative amount inverts that, so the partial is loudest when you play softly or lift off the key. The two halves are exact mirrors, so -50% at a given velocity matches +50% at the opposite velocity.

The negative half is there for crossfading. Give one set of partials a positive velocity amount and another set a negative one, and the two timbres swap across the velocity range instead of one fading in. The test suite plays that case and measures the spectral balance changing by a factor of a hundred between a soft and a hard note.

Letting go of a key runs its own short envelope rather than just fading out. The KEY OFF rows set where the level goes when the key is released and how long it takes to get there. Only then does the release run from that level down to silence.

Set above the sustain, it gives a release click or a bloom, the sound of a damper landing or a hammer returning, and it can be louder than the held note. _Drawbar Organ_ uses it that way: the upper drawbars jump to 55% for two milliseconds as the contacts break. Set below the sustain, it gives the fast initial drop into a long tail that a piano or a struck bell has. That is probably the half that gets more use.

A key-off level of zero skips the stage and releases from wherever the level is. That is the default, and it behaves exactly like an envelope without the stage. So patches made without it sound the same, and zero on the knob means no key-off stage.

Two details follow from it. The swell time is exact, not "within 1%" like the other stages, because the release has to start when the knob says, not whenever an exponential happens to arrive. Letting go of a key during the delay still makes a key-off sound if you have set one, as a release click does on a real instrument. A level of zero cancels the partial.

### How fast the key came up

On keyboards that sense release speed, it scales the level the tail starts from. Sixty-four is neutral. The hardest lift doubles that level and the softest halves it, on a geometric scale, so two steps down and two steps up cancel out. There is no knob or parameter for it. It belongs to the gesture, not the patch, like velocity, and it changes nothing stored in a patch.

It scales the level the tail would have started from, not the KEY OFF level itself. Zero on that knob means "release from wherever you are". So on a patch that sets a KEY OFF level this scales that level, and on a patch that does not it scales the sustain, which is every patch. Scaling the knob instead would do nothing on the default or on most of the factory presets, because zero times anything is zero.

The envelope tops out at one, so a partial already at full level has no room to go higher on a hard lift and stays there. The room is where the tail is quiet, which is where a bloom is useful: a music box, a bell, an electric piano. A tail louder than its note would have to come from the fader, with the whole series behind it.

**Nothing happens without a keyboard that senses release speed**, and that has to hold for both ways "no release velocity" arrives over MIDI. Many keyboards send a note-off carrying zero, and many send a note-on of velocity zero instead of a note-off, which also arrives as a release of zero. So zero is read as no information, not as the softest possible lift. Otherwise it would halve the tail of every note those players release. Nothing is lost, since a lift of 1 is the same gesture as a lift of 0. The test suite renders both forms and compares them sample for sample with the neutral case.

This covers what LIFT aimed at, a control removed in 1.7.0. LIFT cost a row on every strip, it was a parameter nobody could automate usefully, and no factory preset used it. What remains needs neither a row nor a parameter: when a keyboard senses a release, the instrument responds to it.

A third case to watch for is the shape of a music box or a thumb piano: no sustain, so the partial decays to silence while the key is still down, and then a key-off level brings it back. Reaching zero does not mean finished. A partial in that state holds at silence and waits for the key instead of freeing itself. It costs nothing while it waits, since there is no point running an oscillator that outputs zeroes.

The envelope's delay stage holds a partial silent before its attack begins. Staggering it across the series makes the spectrum unfold rather than arrive all at once, which is how _Slow Pad_ and _Shimmer_ open. The delay is fixed in samples at note-on, so moving the knob cannot retime a note already waiting. Releasing a key before the delay has passed cancels that partial instead of letting it come in afterwards.

STRIKE sits at the top of the section because it is about how hard the key is hit, not about the envelope shape. It applies that to the two rows under it, the way the velocity row applies it to the fader. Zero ignores velocity. Positive means a hard note starts sooner and rises faster, and a soft one waits and then opens slowly. Negative inverts it.

The two halves work in opposite directions on purpose, so each knob stays the limit of its own row. Velocity only ever lengthens the attack, so ATTACK stays the fastest the partial gets. Velocity only ever shortens the delay, so DELAY stays the latest the partial arrives. Turning STRIKE up cannot go past either value, so the rows below it still mean what they say with the amount at full.

At full amount both halves span eight octaves of time, a range of 256 to 1. It has to be that wide because the attacks worth stretching are short. A partial set to 2 ms attacks in 2 ms on a hard note and takes half a second on the softest, which is the difference between a struck note and a swelled one rather than two kinds of snap. A 400 ms delay stays 400 ms on the softest note and drops to under two milliseconds on the hardest, so a hard note cancels the wait. The scaling is in octaves rather than linear, because these times are heard as ratios: going from 5 ms to 10 ms is audible, and going from 2 s to 2.005 s is not.

The stretched attack stops at five seconds, the longest the ATTACK row itself goes. Two hundred and fifty-six times a short attack gives a long one, which is what the control is for, but the same ratio on an attack that is already long gives half a minute, which the knob alone could never produce. The cost is that at full amount, with an attack above about 20 ms, the softest notes all share one ceiling instead of each getting a longer time. That is the right end to lose resolution at, since those notes are already slower than anything the patch was built around.

Setting it per partial is what makes it different from a velocity curve over the whole instrument. Give the upper partials a strike amount and leave the fundamental at zero, and a hard chord arrives as one bright block while a soft one unfolds, the fundamental first and the rest of the series after it. That is close to what a struck string does, and a single delay or attack knob cannot do it.

Aftertouch works the same way but **adds** to the fader instead of scaling it, and it ignores velocity. So a strip with its fader all the way down is silent until you press into the key, and then it fades in, while a negative amount fades an open strip out. Put a few upper partials on positive aftertouch and the note gets brighter the harder you press, without changing the other partials. Both channel pressure and polyphonic aftertouch are accepted, and the higher of the two is used. Pressure is smoothed over about 15 ms, so seven-bit MIDI does not step the gain.

A note starts at the pressure the channel already has, not at zero. The smoothing stops the gain stepping when the controller moves. A key struck while the controller is already somewhere has nothing to smooth towards: ramping up would spend the first 15 ms at a level the player did not ask for, which is a thump on every note with a negative amount and an audible fade-in with a positive one. Per-note pressure is different and does start at zero, because it belongs to that key and the key has only just gone down.

The mod wheel can stand in for aftertouch, and does by default. Most keyboards have no aftertouch, and the wheel is the control your hand already goes to, so CC1 feeds the same destination. A controller that does send pressure behaves the same, since a wheel left alone reads zero. Settings has an "Aftertouch from" entry if you want only one of the two. Polyphonic aftertouch is not on that list: it is per note, not per channel, so where it goes is never ambiguous, and it stays routed whatever the setting.

### MPE

Off by default, in Settings. When it is on, a controller that gives every note its own MIDI channel can bend and press each note separately. On this instrument that gives each finger its own copy of all 33 aftertouch destinations. Press into one note of a chord and only that note's upper partials come in.

It is a setting rather than always on because it changes what a channel number means. With it off, the channel is ignored: a key-up on channel 7 releases a note that went down on channel 1, which is what a single-channel keyboard needs. With it on, the channel is part of the note's identity, so the same key can be held on two channels, bent in two directions, as two voices instead of one retriggering the other.

An ordinary keyboard works either way. With MPE on, notes on the master channel are treated as master-channel notes rather than ignored, so they sound, one voice per key, and the wheel moves them together exactly as it would with the setting off. This matters because the alternative, routing only the member channels, gives a plugin that goes silent when a normal keyboard is plugged in.

Three numbers describe the layout, and only one comes from the panel. The zone is the lower zone with all fifteen remaining channels as members, which is what a controller sends unless it says otherwise. A controller can say otherwise: the layout messages it sends are parsed and replace this. The per-note bend range is the 48 semitones the specification asks for, since that range belongs to the controller. The master range comes from the BEND setting, so the wheel covers what the panel says whether MPE is on or not. The tests measure both: a wheel at maximum with a range of 2 takes A4 from 440.0 to 493.9 Hz, and a member-channel bend at maximum takes it to 7040.0 Hz, the four octaves that 48 semitones give.

Switching the setting either way releases every sounding note. Voices started through one set of entry points cannot be found by the other, so otherwise they would hang with no key left to release.

Bend, pressure and slide are all routed. Slide goes to brightness or tuning, as the Slide to setting in the Playing bar says. See **Slide** below.

The top bar holds everything that is not per partial. **LINK** is the exception. It sits at the top of the caption gutter, above the column of names it links together.

| Group     | Contains                                                                                                                                                                                                                                                                                                                                                                    |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Settings  | **SETTINGS**: which version this is, undo, polyphony, bend range, MPE, tuning, what feeds aftertouch, phase reset, the safety clipper, the limiter's lookahead, zoom and the way back to a window that shows all 32 channels. **MACROS** beside it, and under the two **PLAYING** and **TUNING**, which open the bars below. SETTINGS lights while a new release is waiting |
| Preset    | the wordmark, and under it the preset menu: factory, saved, and somewhere to put the one you are working on                                                                                                                                                                                                                                                                 |
| Series    | **CHARACTER**, then **STRETCH**, **TRACK** and **WOBBLE**, which shape the series as a whole. See below                                                                                                                                                                                                                                                                     |
| Resonator | **RESONATOR**, its mode, and **MIX**, **DECAY** and **DAMP**. See below                                                                                                                                                                                                                                                                                                     |
| Echo      | the tape echo. See below                                                                                                                                                                                                                                                                                                                                                    |
| Reverb    | the reverb. See below                                                                                                                                                                                                                                                                                                                                                       |
| Output    | **MASTER**, the stereo meter, and the converter readouts under it                                                                                                                                                                                                                                                                                                           |

**The wordmark sits above the preset name.** Both are the width of the preset screen, stacked in one group, so the instrument's name and the preset's name read as one block. At that width the wordmark is 24 px tall, and with the screen under it and a gap of two the pair fits the 55 px row, so the bar is no taller. The wordmark sits at the top of its space, so its tallest letter is level with the top of the buttons and the frames, and the screen's bottom edge is level with theirs. There is no tagline, since it would tell someone already looking at the instrument what it is, and update notices come through a dialog and the lit SETTINGS. As an ordinary group the pair wraps like the others, with no special case for the first row. The bar fits on one row at 1285 px, on two at 675 and on three at the 475 px floor.

Zoom is in the Settings menu rather than on the bar, which saves eighty-eight pixels. They go to the output meter instead, and they make the converter readouts wide enough to keep their units.

### The Playing and Tuning bars

The settings that get changed while playing are the voice count, Legato, No stack, bend range, MPE, aftertouch and slide, and the temperament, root, reference and retune. In the menu they are hidden until it is opened and take two or three clicks to change. Two bars under the top bar show them at a glance and change them in one click. Each bar is one row of the top bar, drawn the same way with framed groups, segment displays and lit caps, so it reads as part of that panel rather than a window laid over it.

**The buttons are arranged two by two.** SETTINGS and MACROS sit above PLAYING and TUNING, shown as a keyboard and a tuning fork. Four in a line would make that group 80 pixels wider and push the bar onto a second row at the width that shows all 32 channels, which costs the mixer 59 pixels whether a bar is open or not. The block fills the row's height instead of standing on the dial line. It is the only thing on the bar that does not stand on that line.

**Two bars, not one.** One bar with two halves side by side would only save a row where both fit across, and with the Adaptive readout they do not fit at 1350 pixels, so they would stack anyway. Two bars are simpler and give each room to grow.

**Both start closed, and the window keeps its size.** A bar takes its height from the mixer, which scrolls, so opening one never resizes the plugin window, which hosts handle badly. The exception is a window already at its minimum size, which grows by the bar's height, since the minimum includes the open bars. Which bars are open is saved with the session, next to the fold mask.

**A bar uses the Settings menu's own lists.** Each display opens the same list as its Settings entry, built by `ui::settings` in `SettingsMenus.cpp`, and both apply a choice through the same `settings::apply` with the same item ids, so the two cannot drift apart. The menu keeps every entry. In a narrow window a bar wraps its groups onto more rows like the top bar does, each the height of a top bar row, and the window's minimum includes those rows. A group wider than the whole bar, which never happens on the top bar, wraps its own controls onto lines inside its frame, shared out evenly, so the twelve cents displays come out as two lines of six rather than eleven and one.

**The readouts are calculated off the audio thread.** After every settle the engine publishes the held chord, each key's frequency and version and whether it is the harmonic seventh, through a triple buffer. The editor reads it at 30 Hz whether or not the bar is open, so the bar opens showing the chord being held. From it, `AdaptiveReading::of` works out whether any interval was given up, whether a seventh is on 7:4, and the drift as the chord's average version. The bar shows these as PURE and 7:4, two words lit in screens like the gutter's switches, and a two-cell DRIFT display. When PURE is dark, a screen reader is told which interval gave way. A panel of lit words and segments has no room for a line of free text such as 4:5:6:7, so the harmonic numbers are not shown.

**The Playing bar shows what the hands are doing.** IN USE, SUSTAIN and three needles, PITCH, WHEEL and PRESSURE, read values the processor publishes at the end of every block, as atomics next to the voice count. Each is a single number, so nothing has to be passed over as a set. The processor tracks the pedal itself, one bit per MIDI channel, instead of asking the engine, because under MPE the parser takes CC 64 and the engine never receives it. PITCH shows the pitch wheel or, under MPE, the key bent furthest, against the BEND range either way, so it means the same with MPE on or off. PRESSURE shows the keyboard's own pressure, the channel's or the hardest-pressed key's, whichever is higher. It does not show what Aftertouch from passes on, because that can be the mod wheel, which would then move two needles at once. Only keys still down count, including those held by the pedal, so a needle drops as a key comes up rather than when its tail ends. The needles are the same as the strips' PITCH MOD needle: PITCH rests in the middle and is marked there, and WHEEL and PRESSURE rest at the left end with no mark, since a centre means nothing for a reading that only rises.

**The macro knobs control the same parameters.** Each turns its macro's amount, the same parameter the slider in MACROS turns, so the two cannot disagree. It uses the macro's colour and its row's skew. A macro that has not been set up has a blank knob: its ring unlit, no pointer, and its cap at rest, since the collar turns with the knob and would show a setting. It shows no value on hover either, and its tooltip says it is not assigned and that a right-click assigns it. Right-clicking a knob opens its own menu: MIDI Learn for the amount and Edit macro, which opens MACROS. For a macro not yet set up the menu has only Assign macro, which sets it up on the first row and opens MACROS, since a controller bound to a macro that drives nothing would do nothing. Each knob is captioned with the row its macro targets, using the row's own name where it fits under a 50 pixel knob and the mixer's shorter names where it does not, PM DEPTH, AM RATE, SWELL, OFF LVL and AT. An unassigned knob shows its number, so an empty slot still says which macro it is. The scope is left to MACROS, and the spoken name gives the full row. With the macros, the Playing bar takes four rows at the narrowest window.

**Twelve displays, one per pitch class, in every temperament.** Each shows how far its note is from equal temperament in cents, with A on the reference, measured in middle C's octave, since a pitch class is the same distance from equal in every octave. Two keys an octave apart always read the same under Adaptive too, since octaves are settled first and never given up. Under a fixed temperament all twelve are lit and show the scale, because a key's tuning does not depend on what is held. Under Adaptive the held notes light and show where they are sounding, and the rest stay unlit. For each unlit note, the pitch it would have if played on its own, which is the version its pitch class last used and which the engine publishes with the chord, is still calculated and given to a screen reader. Cents rather than ratios, because half the temperaments have no ratios, and the display has to mean the same thing under all of them. The seven white keys sit on the unlit plastic that every cap on the panel is made of, with their names in the caps' near-black ink, and the five black keys sit on the pocket's dark floor, so the notes read the way they do on a keyboard. The background is drawn on the panel behind each display and its name, not in the display. It runs to the pocket's edges and halfway into the gaps, so E and F, and B and C, share one continuous background. The pocket's shading goes over it, with the same shadow under the top edge and light along the bottom as the dark floor, so the white keys sit in the same recess as the black ones rather than on top of it.

That is also a question of room: the bar fits on one row at 1285 px and the window opens at 1350, so eighty-eight more would wrap it. But zoom stays in the menu for the same reason as everything else there. It is set once and left, and a bar is for the controls used while playing.

The entry under zoom resets the window to the size that shows the whole mixer. Window size is saved with the session, which keeps a window you resized at that size and also keeps a narrowed one narrow, since nothing else here sets it. Its height comes from which groups of rows are folded, not from a stored number, so folding a group away and then fitting leaves no empty band under the strips.

The character with the series knobs, the resonator, the echo, the reverb and the output are drawn as boxes, because a box shows that controls belong together, and each of them has something to group. The four buttons and the wordmark with the preset screen are not. The buttons are four separate things rather than one setting, and boxing everything would make the bar look like a fence. Buttons, lists and the meter all sit on the same line as the knob dials rather than in the middle of their row, because a knob has its caption underneath and anything centred next to one looks like it is sagging.

Settings and Link are menus rather than panels. Everything in Settings is set once and left, and a short list of whole numbers is easier to read written out than set on a knob. Phase reset restarts partial phase on each note for a coherent, percussive attack. It is not something you keep adjusting, so it is not worth the width of a button.

The safety clipper is the exception. It is on the bar as CLIP, under the meter next to the converter readouts, and not only in Settings. It does end up set once and left, but you choose it while listening, because whether the output is being caught depends on what you are hearing right now. It is the one thing in Settings you reach for while listening. The master fader sits on the meter, which leaves free the group its knob would take, so the three facts about the output stage stand together under the lamps: the rate, the bits, and whether anything is being held back. It keeps its entry in the menu as well, since a control in two places costs nothing and both follow the one parameter.

CLIP is not a plain switch. It chooses between eight machines the way ECHO and REVERB choose between their machines, and the Settings entry keeps the switch that decides whether any of them runs, which is the only part that is still yes or no. The button abbreviates where it must: ASYM, because Asymmetric is 54 px of label in a space of 46, and the ten pixels it would need belong to the converter readouts next to it, which are already at the width they need to show their units. LIMIT is shortened to match, and Console, Cassette and Print to CNSL, CASS and PRNT, so the eight read as one set of choices rather than short words and long ones. Only the button shortens them. The menu, the host's automation lane and the documentation have room for the full words.

Everything fits on one row from 1285 px of logical width. Below that the bar wraps onto more rows instead of dropping controls or letting captions overlap. It fills each row as far as it can. The window will not shrink below 475 px, which is where the bar stops fitting in three rows.

The bar keeps 7 px on the left and right, 5 px above its row and 6 below it, where the bottom edge is a 1 px line, and the four buttons have 7 px between them. The two groups without a frame, the buttons and the wordmark with the preset screen, have 19 px between them and before the first frame, so the wordmark is centred between the buttons and the character's frame. Frames have 6 px between them. Inside a frame the dials sit 3 px below the top edge and the captions 2 px above the bottom one. The Playing and Tuning bars use the same margins and row height.

The output meter is the one element that changes width. It takes any width left on its row up to a limit, since without a limit it would fill a whole row and stop looking like a meter. It gives back up to 40 px when a row is slightly too tight, so a window a few pixels too narrow for a row narrows the meter instead of wrapping.

The exception to filling rows greedily is a last row that would be nearly empty, since one small group alone on a row looks like a mistake. In that case a group is moved down to join it.

Double-clicking any knob restores its default. Hovering a harmonic number shows its interval and exact cent deviation.

Pointing at a knob highlights its row across the whole mixer, through every strip and the noise channel, and brightens the caption in the gutter that names it. COLOUR and the noise channel's string are the exception, since the gutter's names do not apply to them. At harmonic 28 that caption is far to the left, and following the highlighted band back to it is easier than counting rows. The two readouts count as part of the control above them, so moving off the tuning knob onto its cent value keeps the highlight.

The channel under the pointer is marked the same way, with a line down each side and the same wash between them, and its number at the top of the strip turns to the accent colour, as the gutter caption does for the row. The row shows which of the twenty-two control rows you are on and the column shows which of the thirty-three channels, and where they cross is the knob a drag would move. Thirty-eight pixels of strip is not much to aim at in a mixer this wide.

The lit number is what makes this work, and it is consistent as well as easy to read: for the row, the control's name lights up, and the number is the channel's name. The wash then only has to connect the lit number to the rest of the strip. It does that at the same strength as the row, since every channel stands on the same grey and a hovered strip differs from its neighbours only by the wash. Both strengths are defined once and shared by the row and the column, so they cannot drift apart.

One thing about the column has to differ: it is drawn over the child components, not behind them. The row can sit underneath because it crosses knobs, which have transparent corners it can show through. A column crosses the meter and the lamps, which paint their own backgrounds so the strip does not have to. Drawn behind them it would disappear, leaving the channel marked everywhere except where there is something in it.

Each strip decides whether it is the hovered one from the pointer position rather than being told by the editor. Moving from one strip to the next fires an exit and an enter that can arrive in either order, and reading the pointer gives the same answer in both cases. The test covers both orders.

Knowing where the pointer is needs a deep mouse listener on the strip, and that listener has caused two separate faults, so here is how it behaves. A listener registered for every nested child receives every event those children get, whatever it was registered for, and it also receives the component's own events. So a handler on the strip runs once for a click on a child and twice for a click on the strip itself, and it runs for kinds of event it was not written for.

The first fault was the wheel. The listener receives every event the children get, wheel events included, and the default handler passes whatever it gets up to the parent. So a scroll a knob had already handled was passed on again by its strip, reached the viewport, and scrolled the series sideways while the knob was being turned. It only showed when the window was narrow enough to have something to scroll, which is why it went unnoticed for a long time: at the width the window opens at there is nothing to scroll. The strip passes on only a wheel event that landed on the strip itself, so the background scrolls the series and a control keeps its own. The top bar and the Playing and Tuning bars listen the same way and pass a wheel on by the same rule, so turning a knob there with the wheel does not scroll the mixer under it.

The second fault was the right-click. A right-click anywhere in a strip opens the LINK menu, which is why the listener is there: a knob would otherwise swallow it. On the strip's own background the handler ran twice and opened two menus on top of each other. Choosing an item in the front one left the one behind it open with its ticks unchanged, so the setting looked refused when the instrument had already applied it. Only the gaps between sections showed it, since everywhere else on a strip has a control on top.

Nothing in the two events tells them apart. The listener receives an event rebuilt from the same click, so the component, position and modifiers all match, which leaves only the time. The time is what separates them, and JUCE itself uses it for the duplicate wheel events it sometimes receives.

The faders and the mute and solo buttons are left out. Those two rows are already easy to spot, and a wash the height of a whole fader would be a lot of paint for nothing.

Knobs show their value as a ring of separate ticks rather than a continuous arc. That suits an instrument built from 32 discrete partials and looks more like a measuring instrument than a mixing desk. Faders have a scale in the same tick style.

The panel is lit from the top left throughout. Knob caps are flat dark discs with a fluted rim, a wide stripe in the channel's colour running from the middle of the face out to that rim, and a soft shadow cast down and to the right. Channels have a bright left edge and a shadowed right one so a run of strips reads as raised columns, fader grooves darken at the top where they are cut into the panel, and buttons cast a shadow when raised and lose it when engaged.

**The knob looks like a mixing desk's, not a pedal's.** It has a flat face rather than a dome, because a strong radial gradient with a specular highlight looks like a ball bearing, not the top of a control. The roundness comes from a lit arc along the upper left of the rim and a dark one along the lower right. That is the mirror of the reference photograph: its knob is lit from the upper right and this panel is lit from the upper left, and a rim lit from the other side would be the only thing in the window with the light in the wrong place.

**The flutes turn with the knob, and that is what makes it look like a knob turning rather than a stripe moving round a disc.** With fixed flutes only the pointer moves, and the cap looks like a dial face with a hand on it. The lighting does not turn with the flutes. The lit arc stays at the upper left and the shadow at the lower right while the teeth pass through, so each one brightens and darkens as it comes round. A texture moving under a fixed light is what a turning surface looks like.

The outline helps too. The flutes reach a little past the face, so the outline is bumpy rather than round and the bumps move with the value. That is less than a pixel of relief on a channel knob and it still counts, because the eye notices that the shape has changed.

**The notches are cut out of the collar, not drawn on it.** The knob is a ring of flutes around a smaller cap, so between two flutes there is nothing, and what shows through is the panel and the cap's shadow on it. The collar is one path with the notches removed from its outline and one gradient across all of it, which is one fill rather than one per segment on six hundred knobs. A linear gradient across a band two pixels deep looks the same as shading by angle.

Drawn as dark marks, the notches are paint on a disc, and there is no right answer for how dark they should be, only a guess. Dark marks measured 4 of 255 at their darkest, against 12 for the deepest shadow the cap throws. Nothing on a lit panel is darker than the shadow of something standing on it, so marks that dark look like holes punched through the panel rather than gaps between flutes. Cut out, there is nothing to choose. The gap shows whatever is behind it.

The band is about a seventh of the radius, a little more than the reference's eighth, and the face plate is the rest. A notch cuts only part of the way into that band, so an unbroken ring of collar remains between the notches and the face. Cut all the way in, the flutes become separate teeth with the face showing between them, which looks like a gear rather than a knurled edge. A collar taking a third of the cap looks like a gear too.

**The reference was measured by its outline, not its shading**, because only the outline answers the question. Its cap ends at 0.878 of the wall's radius, its notches take 0.32 of the pitch, and they cut 0.28 of the way into the wall band. The shading is misleading. A dark band at the foot of the wall looks like the colour of a groove but is the cap's own shadow on the wall behind it, and reading the collar's colour from that band gives a collar far too dark.

The depth also matches the reference. A cut of 0.28 of the band is under half a pixel on a cap this size. Cutting it to 0.62 instead, on the grounds that everything about this collar is the reference exaggerated to survive 26 pixels, would solve a problem the cut already solves. A notch drawn as a dark mark has to be deep to be seen, because it competes with the collar it is painted on. A notch cut through shows the panel behind it, and panel against collar is visible at a depth that paint would not be, so the reference proportion works as it is. When something here looks like it needs exaggerating, check first what it is competing with.

**The wall is the lighter part and the cap is the dark mass.** They are the same plastic, and from directly above most of the visible wall is the light catching its edge, so the wall reads as the lit part and the flat top as the body. Both references have the wall slightly darker than the cap, 0.95 in the photograph and 0.93 in the drawing, but both are drawn much larger than this panel draws a knob. At 26 pixels the wall is about a pixel and a half wide and the highlight is most of it.

The other way round, a dark wall with a bright edge stroked round its outline, comes out on a ring that thin as three bands rather than one surface: the cap, then a dark gap, then a light rim. The gap is what gives it away. So the wall takes the light tone throughout and the rim only adds to it on the side facing the light.

**Nothing is drawn between the cap and the wall.** A dark arc under the cap's edge and a lit one over it would together put a ring round the cap that looks like a seam between two parts. A knob is one piece of plastic. Without the ring the body is continuous from the middle out to the flutes, and only its outline alternates, which is what a knurled edge looks like from above.

**A dark knob needs light to show its shape, because tone alone cannot.** The cap sits at 39 against a panel of 31, which is what it takes to look like a dark object on a dark panel rather than a pale disc stuck on it. At that difference a notch shows almost nothing through tone alone. A notch shows the panel through it, so what makes the knob look rugged is the difference between the wall's tone and the panel's. That is why the wall carries the light, and why the rim is there.

**There are eight slots**, as on the reference, whatever size the knob is drawn at. Deriving the count from the radius gives sixteen on a bar knob. At that spacing the slots are a pixel apart, they alias into a shimmer as they turn, and no single slot can be followed round, which is what the eye needs to see the knob turning at all. Eight is also far enough apart that the space between two slots is a face rather than a tooth.

**The first slot sits at the stripe's angle**, so the stripe runs into the gap between two flutes rather than over one. It runs the full depth of the wall, out to the edge. A stripe that stops partway, so the flutes either side stand out past its tip, leaves the wall's lit edge continuing past the end of the mark: a grey line outside the blue, which cannot be anything real on a knob.

The part of the stripe past the face plate is a step down and takes the wall's lighting rather than the face's, bright where the stripe points towards the light and dark where it points away. That is the one place where the stripe and the lighting have to agree, and it stops the mark looking painted across the flutes instead of set in between them.

The whole effect is small, since the collar is two or three pixels on a knob this size. It does not need to be larger. The eye notices that the shape has changed, and a slot moving one pixel shows that as well as a tooth moving five.

There is no room for a proper skirt standing clear of the cap, as the reference has. The tick ring takes everything from 0.76 of the radius outwards, and it needs that space for the value and for what a macro is doing to it.

**The stripe reaches the rim and is drawn as a rectangle, not a stroked line.** Reaching the rim lines it up with the lit ticks beyond, so the two read as one mark rather than a mark on a cap plus a separate arc around it. The rectangle is for the ends: a line with round caps puts a dome at the centre of the face, which is where one is most noticeable. Pointers and fader caps are translucent glass, so the tick ring and the meter show through them.

**Every screen and every lamp is set into the panel.** With a panel lit from one corner, raised things show the two walls nearest the light and sunk things show the two furthest. So a channel is lit along its left edge while a hole cut in it is lit along its right. The cents readout, the shape glyph, the five activity lamps and the pitch needle all have a dark edge along the top and left and a lit one along the bottom and right.

The edge is drawn around each of them rather than inside, because they are only a few pixels across and a border taken from inside would leave almost nothing of the picture. None of them had to move to make room. Each already had an unused margin, so the lip costs only the one pixel on each side that the margin already had.

It is drawn as two whole shapes rather than four arcs, the lit one first and then the shadow on top offset by the depth, because the face drawn last cuts the middle out of both. The shadow is the size of the opening and the lit shape the size of the lip, which makes the lip one pixel on every side. Offsetting the larger shape instead gives two pixels of shadow above and to the left against one of light below, which makes the element look off centre in its hole and takes a pixel its neighbour uses.

Both shapes also run under the opening, so a face drawn at low alpha shows the shadow through it. The lamps and the needle are like that when they are dark, so both are blended against their own backdrop instead, which gives the colour they had anyway and also covers the floor of the hole.

**It has a cost, and the lamps pay it.** Five lamps and a needle per strip across thirty-three strips are the only thing here that repaints fifteen times a second, and two shapes is half of what a lamp's whole paint costs without the recess. Measured on one lamp, alternating builds and taking the minimum of six runs each: 4.3 microseconds without the recess and 6.9 with it. Across all 198 of them that is 0.85 ms of a frame against 1.37, so the mixer's lamps go from 2.6 percent of a core to 4.1. Two things were tried and did not help. Ellipses instead of rounded rectangles save nothing, since both are general path fills. Adding a third shape for an opaque floor under those two faces costs 1.3 microseconds per lamp, where blending the faces costs nothing.

The lamps are not drawn from a picture, though most of the panel is. A lamp changes whenever its group's activity crosses a step, so while a note plays a picture of it would be drawn again about as often as the lamp itself, and the cost would only move.

**Most controls are drawn from pictures of themselves.** Soloing a channel, scrolling the mixer or resizing the window redraws every strip, which is hundreds of knobs, faders, buttons and readouts that mostly look exactly as they did. So knobs, faders, the shape buttons, the segment readouts and every lamp button keep their last two looks as pictures in a `PaintCache`, and copy one when nothing that decides the look has changed. The key is a hash of everything the drawing reads: the value, whether the control is enabled, its colours, every property set on it, its size, and the display's physical pixel scale. Every property is in it, so a property added later cannot leave a stale picture behind. What a function draws cannot be hashed, so a lamp button's icon is in the key as a count of the times it has changed, and the one button whose icon does change, the tool switch in the gutter, says so when it does. The picture is drawn at the display's own resolution, so under the zoom transform it is copied pixel for pixel rather than resampled. Two looks rather than one, because soloing swaps each control between its lit and its unlit look, and both should be ready when it is switched back. The pictures belong to the controls, so they go with the window. Nothing here may be static, for the reason [ARCHITECTURE.md](ARCHITECTURE.md#threading) gives.

The strips are opaque, so JUCE does not repaint the panel behind each one first. The look and feel keeps the strip background, its gradient, shadow and grain, as a picture for each size and colour it is asked for, up to six, so 32 strips draw one picture. Together these take a full redraw of the 32 strips from 73 ms to 15 ms in a software render, and the solo that redraws them all from 70 ms to 15. Shading drawn into a picture and then onto the panel rounds differently from shading drawn onto the panel, by up to a few levels in 255 at the edge of a soft shadow, which nobody can see but which shows in a byte comparison of the screenshots.

Cast shadows are built from a few overlapping shapes rather than a real blur. JUCE has a proper `DropShadow`, but it is a software Gaussian, and running one on several hundred controls every repaint would be far too slow.

**Slide.** The third MPE dimension, the forward and back axis under a finger, arriving as CC74 on the note's own channel. Settings has a **Slide to** entry with three answers.

**Brightness** is the default, because it is what players expect from that axis. It changes the keyboard tracking for that note only. Pushing forward reduces tracking and the note opens up, and pulling back adds tracking and it thins. It cannot brighten past what the patch already is, since there is no negative tracking, so a patch with TRACK at zero is already as open as it gets and slide can only darken it. The travel is six dB per octave either way, half the range of the control itself.

**Tuning** is the option only this instrument offers. Slide moves that note's blend between equal temperament and just intonation, so holding a chord and pushing one finger forward fuses that note while the rest stay smeared. It is not the default because it will surprise players who expect brightness.

**Off** ignores it.

Slide is read as movement, not position. The first value a note gets becomes its zero, and what reaches the sound is how far the axis has moved from it since. A keyboard with no slide axis sends nothing and changes nothing.

It works this way because controllers disagree about what the axis is. A Seaboard reports where the finger sits on the keywave and rests at the centre. An Expressive E Osmose uses the first part of the key travel for pressure and only then starts sending CC74, from the bottom of its range upward, so it rests at an end of the axis rather than the middle. Read as a position, that second kind jumps the moment the axis engages: the note goes suddenly darker and only then starts to brighten, which is the opposite of the gesture. Read as movement, both start the note at the patch's sound and follow the finger.

The travel is scaled so that the longer of the two directions from the rest point is the full slide. A note resting at the bottom of the axis therefore reaches full slide at the top, where without scaling it would have twice the reach of a note resting at the centre. There is one scale per note, not one per direction. Scaling each direction to fill the slide on its own leaves a rest near the bottom with only a sliver of travel below it, and dividing by that sliver makes it a hair-trigger. Ten codes of lift swung the note from untouched to fully dark while twenty codes of press moved it a sixth of the way, so pressing in was smooth and coming back out snapped. With one scale, the short side keeps the sensitivity of the long one and just reaches less.

The chosen destination is not saved with a preset. It depends on the controller you are using, like the aftertouch source and the temperament.

### Legato

A switch next to the voice count, and what it does depends on the count. With one voice it is the one place where the instrument stops being polyphonic. A key pressed while another is still held moves the note instead of restarting it, with the envelopes, phases and drift all carrying on, so a run keeps the shape the first key gave it. Releasing a key while others are still down falls back to the most recent of them, which is what makes a trill work, and only releasing the last key releases the note. The held keys are kept in the order they were pressed, in a fixed array of the 128 there are, because this runs on the audio thread.

**With more voices it plays a top line.** A key pressed above every held note except the highest continues the highest note's phrase, and the rest of the keyboard plays as usual, so a legato melody can run over a staccato chord that retriggers under it.

**The phrase continues by handing over, not by moving the voice.** The key starts its own voice, which takes over the highest note's envelopes, modulators and drift where they are, with the velocity the phrase started with, and glides from its pitch. The note it took over from is never moved. So when the instrument mistakes a chord note for the melody, that key still sounds, and the only cost is an attack. That note keeps sounding while its key is down and fades over 20 ms when it is released, instead of ringing out its release under the melody. The same happens to the upper note of a trill when the player falls back to the key still held below it.

Moving the voice instead, as one voice does, would turn every wrong guess into a silent key with the melody jumping onto it. The rules needed to make that rare would break the line whenever the melody lands together with a chord.

**When a key goes down below the highest note**, a melody stepping down and a chord struck under a held melody look the same, and only what the old melody key does next would tell them apart. So the rule goes by what is likely:

| Where the key lands                                                   | Carries on when                                                       | Why                                                                                                          |
| --------------------------------------------------------------------- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Above the highest note                                                | Always                                                                | The melody going up, or the top of a new chord over it                                                       |
| Under it, with a chord held and no other key just struck              | It is above that chord, however far down                              | Nothing else it could be                                                                                     |
| Under it, alone under a lone melody or among keys struck within 40 ms | It is within a major third and the highest of the keys struck with it | Melodies move by seconds and thirds. The top of a chord struck under a held melody usually sits further down |

No key waits, so nothing is ever late. When the voices run out, stealing skips the highest held note, since losing the melody to make room for the chord under it is the one steal that is always heard. A note held only by the pedal keeps sounding but does not count as part of the line. With MPE on the line works the same, and a voice moved to a key on another channel takes that key's bend, pressure and slide, since with MPE every note has its own. On one channel they belong to the phrase and are left alone, as with one voice.

The polyphony list still starts with a Legato entry that the menu does not show, because a stored choice is a position in the list, and removing the entry would shift every saved session and automation lane to the next count up. A session that stored it plays as the switch on with one voice, which is what the entry means.

### Glide

Every channel has a glide time, the last row under PITCH MOD, and a new note reaches each partial's pitch over that channel's time. It is per channel rather than one knob because the series does not have to arrive together. That is what glide on an additive instrument can do and portamento on other instruments cannot. Short times above a long fundamental bring the brightness first and let the pitch follow, the reverse brings the note first and lets its colour catch up, and settings in between have the partials disagreeing about the fundamental while they move and agreeing when they arrive.

**A straight line in pitch, stepped per control block.** Each partial stores how far it still is from its note in semitones and how much of that it closes every 32 samples, the block every modulator is stepped on. A fraction of a semitone every two thirds of a millisecond sounds like a smooth slide, as it does for the vibrato. The offset goes into the same sum as the vibrato, the drift, the unit's own error and both bends, so everything downstream uses the pitch the partial has actually reached. The character's table is chosen by the room under Nyquist at that frequency, and the Bulb lamp follows a long slide and lags behind it. Measured on single partials, an octave in 0.4 s is within a tenth of a semitone of halfway at 0.2 s and exactly on the note at the end.

**Where a glide starts from is decided before anything is stolen.** The voice a glide starts from can be the one about to be taken, so the engine reads every partial's position first and only then steals or retriggers. Played polyphonically, a new note glides from the last note played, and from that note's plain frequency once its voice has ended. In Legato the one voice glides from wherever it is, partial by partial, so a key pressed mid-slide turns it round from where it has got to. A trill falling back to the key still held glides back. That needs the glide times at a note-off, which carries no parameters, so the engine keeps its own copy of them, refreshed on every block and every note.

**Two switches apply to every channel**, and both are saved in the patch. _When_ is Always or Legato. Always glides from the last note even after its key has been released, as a portamento circuit with one voice does. Legato glides only from a key still held, so a phrase played detached lands on each note and a phrase played joined slides between them. A note held only by the sustain pedal does not count as held. _What the time means_ is Fixed rate or Fixed time. Fixed rate, the default, makes the time per octave, so a wide leap takes longer than a narrow one, like a finger sliding along a string at a steady speed. Fixed time makes every slide take the set time however far it goes. They are in the GLIDE knob's right-click menu rather than in Settings, because Settings holds what a preset never changes and a legato lead has to be able to set how it glides. The modulators' in-phase switches are in their shape button's menu for the same reason.

Off is the default on every channel, so patches made without glide play the same. Each channel's glide parameter is declared in the place of the start phase parameter it replaces, so every other parameter keeps its index for hosts that go by position. In the macro list Glide takes that same slot, so a macro stored against any other row still points at it.

### No stack

The instrument is polyphonic across the keyboard and monophonic within a key. A string, a tine or a bar is one object, and striking it again takes over whatever it is doing instead of starting a second copy next to the first, so playing the same key twice does the same here.

That includes tails. A key with a long release that is tapped repeatedly must not leave every tap ringing and add them up, which no physical instrument does. Five overlapping taps of one key peak at 0.412 against 0.424 for a single tap. Allowed to stack, they reach 0.961, which is two coherent copies of the same note.

A tail being taken over gets the same four millisecond fade as a stolen voice, fast enough to sound instant and slow enough not to click. Measured across a retrigger, the largest jump between neighbouring samples is 0.01453, against 0.01455 for the steepest part of the waveform itself, so the cut adds nothing the signal was not already doing.

A key that is still down, or held by the sustain pedal, is retriggered in place instead. That keeps two things a fade would lose. With phase reset off, a legato retrigger continues from the envelope's current level instead of restarting from silence, and re-striking a pedalled note takes it off the pedal.

All of that is a switch, No stack, in Settings and in the Playing bar next to the voice counts, because it answers the other half of the same question: how many voices there are, and how many one key may use. It is on by default. Turned off, every strike gets its own voice and the previous strike is left alone, so the tails add up and a repeatedly struck bell builds up. That is a sound, not a fault, which is why it is offered. It belongs to the session, not to a patch, so loading a preset never changes it.

It also keeps tails out of the voice pool, which is the more expensive half of the problem. If a tap held one of the 24 voices for its whole release, one repeatedly tapped key could fill the pool by itself. After that the allocator has nothing free and takes the oldest voice outright, with no fade and no check for whether a key is still down on it, which is heard as notes cut off while held. Four keys held and a fifth tapped 25 times ends with five voices in use.

**When the pool runs out.** Under the polyphony limit a stolen voice gets a four millisecond fade and keeps rendering in one of the eight surplus voices, so stealing never clicks. Beyond that surplus there is nothing left to fade into. The new note cannot wait, so a voice has to go immediately, and that is a step in the output whichever voice it is. The size of the step is that voice's current level, so the only choice is which voice is taken.

It takes the quietest, preferring one already releasing. Taking the oldest is the right rule for stealing under the limit and the wrong one here, because the oldest voice is often a note still held, next to a tail three quarters through its release that costs almost nothing to lose. With one loud note held among quiet tails and the pool overflowing, the held note survives.

### The noise channel

A noise channel marked NZ is pinned on the right, after the series, since it is not part of it. It has the same envelope, tremolo, velocity, aftertouch, pan, mute, solo, level and meter as a partial, and solo works on it the same way. It has no place in the series, so it has no tuning, pitch modulation or drift of its own.

COLOUR takes the place of the tuning row. It tilts the noise from a dark rumble at one end, through flat in the middle, to a bright hiss at the other. The middle is truly flat, not just less filtered. There the two halves of a complementary one-pole pair are summed at unity, which gives back the original white noise exactly. The tests measure a high to low band ratio of 0.13 dark, 1.06 flat and 4.35 bright.

Each voice runs its own noise stream, so playing a chord does not layer 8 copies of the identical signal. Two voices measure about sqrt(2) times the energy of one, which is what independent sources give.

**A string the noise strikes.** The block under COLOUR, where a partial's pitch modulation would be, holds a Karplus-Strong string, `KarplusString`. It is a delay line one period long, fed back through a one-zero lowpass. Each pass round the loop is one cycle, and the top of the series dies first, which is what makes a struck string sound struck. It lives in the noise channel and not in a channel of its own. The instrument is built on every partial being a channel you can see and shape, and a string makes all its harmonics at once, which no row of the strips could do. In the noise channel it uses the noise as its exciter, with the envelope, velocity, aftertouch, tremolo and pan the noise already has, and it adds four knobs instead of a whole column.

The noise strikes it after COLOUR and the envelope. COLOUR sets how bright the strike is, and the envelope sets whether the string is struck once or blown into. A pluck and a pitched breath are settings, not modes. STRING crossfades from the noise alone to the string alone. At zero the channel is exactly what it would be with no string, so every preset that leaves STRING at zero, which is all of them but _Space Cowboy_ and _KarplusStrong_, renders bit for bit the same as without it.

The four knobs are STRING, DECAY, DAMP and POINT. POINT is where along the string the noise strikes, from 0.02 of its length to the middle. A strike at a point cancels every harmonic with a node there, so near the end keeps nearly every harmonic and the middle loses the even ones. It is made as a comb, the noise minus a copy of itself delayed by that share of the period, which doubles the power of the noise, so the input is scaled by the square root of a half to take that back off. It is called POINT rather than PICK because the noise can be blown into the string as well as plucked, and POINT says where without saying how.

It is tuned to the fundamental's sounding pitch. That includes the note with both bends, the fundamental's glide and Adaptive's slide, but not partial 1's own vibrato and drift, which belong to that partial. The pitch moves while a note sounds, so the loop is read at a fractional delay by third-order Lagrange interpolation that slides sample by sample. The lowpass's own delay at the fundamental is subtracted from the line's. Without that, a dark string at 1000 Hz plays 19.6 cents flat. The tests hold it to under a cent at five pitches, three rates and three dampings. DECAY is the time the note itself takes, because the lowpass's loss at the fundamental is given back in the loop gain. It holds to within 15% low and high, bright and dark.

A pluck keeps the energy it was struck with, so it rings at about the level of the burst that struck it, however long DECAY is. A steady breath builds up the longer the string rings, as a resonating body does. Scaling the strike to keep a breath at the noise's level would leave a pluck 20 dB under its own strike. The noise channel then adds 12 dB, because a burst a few milliseconds long on one channel of thirty-three is faint next to a full organ.

The string outlives the noise that struck it and keeps its voice alive while it rings. So a burst on an envelope that holds at silence still leaves a note. RELEASE is the damper. Once the key is up, the string's decay is shortened to the release time, so the string is gone by the time the envelope is. A stolen or faded voice damps it as fast as its envelopes are told to go. A very short loop cannot be tuned. So where the loop is under 24 samples the string starts fading back to the noise, and by 8 it is gone. At 48 kHz that is from 2 kHz up. The line is sized once in `prepare` for the host's rate, which the converter can only lower, so nothing allocates on the audio thread. A string nobody has played is never cleared.

The four knobs share the block evenly instead of standing in the partials' rows, each with its name under it, because the gutter names those rows for the partials. For the same reason COLOUR and the string are left out of the row highlight. Pointing at them lights no row across the mixer, and a row lit from another channel is not drawn over them.

LINK gangs the 32 partials only. Their tuning, pitch modulation and drift have no noise counterpart, and some ganged controls reaching the noise channel while others could not would be worse than none of them doing so.

### The two readouts

**A segment is an elongated hexagon with mitred ends.** That is the shape a real segment has, and the shape is most of what decides whether a drawn display looks drawn. A rounded rectangle has the same footprint but looks like a lozenge laid on a panel. A mitred bar looks like one of a set cut from a single mask. The ends slope, so neighbouring segments point at each other across the gap, and that turns seven separate bars into a figure. The mitre is capped at a share of the bar's length. A half-middle on a narrow cell is barely longer than it is thick, and a full mitre on that would be two triangles meeting at a point.

The gap between segments is a quarter of a bar's thickness and should stay about that. The eye reads it as the join between two bars. A wide gap reads as seven marks that happen to be near each other, not as a figure. It cannot close much further. The mitred ends already point at each other, so the gap at a corner is around seven tenths of the gap along a straight edge. At a fifth of a bar's thickness the corners meet and the figure becomes a blob. The channel readouts are the tightest case, with bars under two pixels thick, so a comfortable gap on the preset display is already under a pixel on those.

There is no bloom around a lit segment, though a real one has it. A bar here is about three pixels thick, so any glow wide enough to see is wider than the bar itself. It reads as blur, and a blurred figure looks less like a real display than a crisp one does.

Each channel shows two figures: the cents its tuning knob is worth and the level its fader is at. Both are drawn as seven-segment displays, not as text. They are the same component as the converter readouts on the bar. A number the instrument reports back looks different from a number you typed, and a mixer this wide has enough of them for the difference to matter.

Two of the things they show are not numbers. A partial left in equal temperament reads **Et**, and a fader all the way down reads **-inF**. The lower case t is what a real display does with a letter that has no seven-bar form, instead of leaving the cell blank. The fundamental and the octaves read **0.0**. Their just interval is the note itself, so the knob has nothing to do, and showing that is better than implying a choice.

The minus is a cell's middle bar and takes a cell like a digit. The decimal point is the point every cell has at its foot, lit for the cell before it. A real display does both the same way. On fourteen bars the two halves of the middle run from the outer uprights to the centre of the cell and end in a short bevel, not a point. At the size names are drawn, a full point on each end leaves a pixel or two of bar, and the middle of an H reads as two dots. A sharp is not a letter at all. Fourteen bars can only write it as a broken one, so the Root display has a sharp sign beside its cell, a fixed shape lit or unlit, like the one printed into a tuner's screen. There is no plus. Seven bars cannot draw one that reads as more than a speck beside the minus, so a partial sharp of equal temperament is the one with no sign, which is also how a tuner writes it. Nothing jumps around because of this, since a harmonic sits on one side of equal temperament and stays there.

**A display is a fixed row of cells, and a reading lights some of them.** Each display has its own number of cells, each with its bars and a decimal point. All of them are drawn whatever the reading is, so the figures stay the same size whatever they say. A number is right-aligned, the way a meter's is, so its units digit keeps its place as it grows. A name starts at the left. A reading longer than its cells shows a minus in every cell, the way a meter shows going over range, instead of a number with its front cut off. A display is also made as wide as its cells need at its height, which `SegmentDisplay::naturalWidth` gives, so a figure in the Tuning bar is the same width as one on the preset name. A display given less width squeezes its cells, and its figures read worse than the ones beside it. The channel readouts are the one exception. A strip is 38 pixels wide, and they take what it has.

**Off is unlit, never grey.** A segment is lit or it is not. A display with nothing to say shows its unlit bars and nothing else: Root under equal temperament with Series off, the notes not held under Adaptive, the drift with nothing held, and a channel silenced by another's solo. A value that is still true stays lit. The converter readouts show the host's own rate when nothing is being cut, and a fader all the way down reads -inF.

**The block is centred on the unit's text, not on the space reserved for it.** The reserve is wide enough for the longest unit any of them carries. Centring on the reserve would put the leftover space inside the block, so a short unit like kHz would push the digits left and the reading would sit against the inside of the screen looking cropped.

**A sign can share the leading digit's cell**, and on the one reading that runs to three digits and changes sign, it does. The macro panel's amount reaches a hundred either way. Its hundreds place is only ever a one or nothing, and its sign is only ever a minus or nothing. Both fit in that cell together, and the reading saves the width of a separate sign.

The minus is the middle segment and the one is the pair of uprights, both lit exactly like any other segment. Nothing is drawn specially for either.

With segments that are not shaped like real ones, a minus made of the middle segment meets the uprights of the one, and the three together read as a four, so -100.0 looks like 400.0. A shorter bar drawn by hand beside the digit looks like the fix, but it is not. The gap between neighbouring segments is what tells them apart on a real display. With the segments shaped like real ones the gap is there and the four goes away. A real display has no custom marks.

Both rows are sixteen pixels tall, not thirteen. Seven-segment digits are about as wide as they are tall, so the row height limits the cell width, and at thirteen a reading like -13.7 runs its figures into each other. Three pixels on each of the two rows is what it costs to make them readable.

The risk in replacing a label with one of these is a reading the display cannot draw. A label renders anything, but a segment display quietly shows an unlit digit instead. So the test generates every reading the two readouts can produce across both their ranges and checks that each character has a form.

The noise channel's level reads the same way.

**The preset name is a display too, with fourteen bars to a cell instead of seven.** Seven bars manage the digits and about five letters. That covers everything a channel readout has to say, but a preset can be called Glockenspiel. Fourteen is what hardware that had to show words used. The middle bar splits in two, and four diagonals and two uprights fill the cell, which covers the whole alphabet.

The two halves of that middle bar are exact mirror images. If the right one started half a bar past the centre while the left one started a whole bar from its end, it would reach a bar's width too far left, and the middle of every cell would sit off to that side, which shows more on a hyphen than on a letter. The halves are also longer than the inset the other bars use would make them. A half-middle is squeezed between an outer upright and the centre one, and at that inset it comes out three pixels, a dot rather than a bar, so a hyphen would read as two specks. The cells have no lower case, so the name is upper-cased for display and saved as it was given.

**It has nine cells and always shows nine.** A display that sized itself to the reading would grow and shrink its letters as presets loaded, so the same display would look like a different instrument depending on what was in it. Nine looks best. It is wide enough that most names fit whole and narrow enough that the cells are the right size. A short name leaves the rest of the cells unlit, as a display with a fixed number of digits does.

A longer name is fitted, not just cut. Spaces go first, starting from the right, because a space is a cell saying nothing and a vowel is a cell saying something. Then the vowels go, and then whatever is still too long is cut. Y is not a vowel here, which keeps SYNTH and NYLON and STYLOPOLY readable. The first character is never dropped.

Compared with cutting, fitting keeps the end of a name, which is often the part that identifies it. Tape Choir keeps every letter as TAPECHOIR, where a cut would give TAPE CHOI, and Dire Dire EP keeps the EP. The cost is the word boundaries, so Glass Armonica reads GLASSRMNC. Of the thirty-eight names, nineteen need fitting and three of those, _2-bit Fuzz Organ_, _KarplusStrong_ and _Synth Ensemble_, end up cut as well.

Three details here come from looking at a render, not from reasoning. The diagonals cannot be trimmed by the same step in x and in y. On a cell half as wide as it is tall that leaves them a third short, so X is four marks around a hole and V does not close. A hyphen cannot be counted as a sign riding between cells, the way the minus does on the seven-bar displays. That leaves the cells one short of the characters and takes the last letter off Lo-fi. And a render showed two of the five's bars wrong. The display reports how many cells it drew, so a test can check the second of those. Counting marks in a picture cannot, because a K has a wider gap down its own middle than there is between one cell and the next.

S and 5 are the same shape, here and on every seven-bar display ever built. The test that keeps the thirty-six characters distinct names that pair as an exception instead of loosening the rule. Drawing them differently would mean inventing a five no hardware has.

### Lamps on the rules

The rules that divide a strip into groups each carry a lamp, showing what the group under them is doing to this partial right now. They cost no height, because the rule already uses that row to draw a line.

| Rule      | Shows                                                                                                                                   |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| PITCH MOD | a needle on a track, flat to the left and sharp to the right, where modulation, drift and a glide still on its way have the partial now |
| ENVELOPE  | how far up its envelope the partial is, while the key is down                                                                           |
| KEY OFF   | the same, once the key is up and the key-off stage has taken over                                                                       |
| AMP MOD   | how far the tremolo has pulled the level down, so it pulses at the rate and swings further at greater depth                             |
| OUTPUT    | two lamps, one for each of the rows beneath it: what the blow has left of the partial, and what the pressure on the key is adding       |

The two envelope lamps hand over to each other instead of both being lit. The value they are fed is signed: positive while the key is down, negative once the swell and release take over. A lamp reading zero is dark either way, and zero is the one value that says nothing about the stage.

Two choices are worth knowing about. The lamps read from the voice pool, not from the knobs, so they describe a note and not a setting, and nothing pulses over silence. They also follow the loudest voice on that partial, the same one the meter follows, because a lamp taking the maximum across a chord would describe no note in particular. Where voices tie, the newest note wins. On a partial whose fader is down every note is at zero, so it is always a tie there, and a note whose partials have finished still counts as sounding while its noise channel releases, 0.4 s by default whatever its fader. A tie that went to the first voice in the pool showed that finished note on those lamps for most of the next one, on some notes and not others, depending on which voice each note got.

That rule is a comparison between voices, not a threshold for reporting, and the difference affects the whole channel. If it were a threshold, a partial sitting at a peak of exactly zero would never beat the empty value the gathering starts from, so every lamp on it would read that empty value instead. A partial reaches zero while still sounding whenever a negative AFTERTOUCH amount takes its level to the bottom of the clamp, and also with the fader down or above the Nyquist fade. The symptom points away from the cause. The aftertouch lamp dies at the moment it has most to show, and takes the velocity lamp beside it down too, so two independent readings appear to be coupled.

The tremolo lamp shows what the tremolo has taken off, not what it has left. That way a partial with no tremolo reads dark instead of sitting fully lit and never moving.

**The output rule carries two lamps side by side**, the left for VELOCITY and the right for AFTERTOUCH. Each reads its own row and nothing else. One lamp for both does not work, and the problem is not the arithmetic. Two rows that behave differently cannot be summed into one reading without the reading meaning something neither row means.

**Each lamp is the shape of the knob above it**, so the pair can be read without a legend.

VELOCITY can only take level away, so its lamp shows what the row has left. Full is a partial the blow has cost nothing, and dark is one the row has taken entirely. At the centre the row leaves every note alone at every velocity, and the lamp is full throughout. At one end a blow of nothing takes the whole partial and a blow at full velocity takes none of it, and at the other end those two swap. That is what the row itself promises, and it is the one thing worth testing. Full at rest is the right way round for a row that subtracts. A lamp that lit up as the row did more would be showing what was taken, not what is left.

AFTERTOUCH adds instead of subtracting, so its lamp works the other way. It rests dark and comes up with the hand, showing the amount against the pressure on the key. It is unsigned, since a row set to push a partial down is doing as much as one set to lift it.

Both are gated on the envelope, like the tremolo lamp. The velocity lamp is the one that needs it, which is not what you might expect. It reads full when its row is taking nothing, so without the gate it would sit lit on every channel of a silent mixer.

**The slot leaves itself a pixel at each end.** The lip of a recess is drawn around the opening, not inside it. An opening that runs out to its component's own edge has nowhere to put its side walls and loses its rounded ends with them. What is left is a bar that looks sheared off instead of milled, which is invisible at a glance and obvious as soon as anyone zooms in. The travel is measured from the same inset, so the needle still reaches both ends of the slot and no further.

**The needle's scale.** It is fixed and the same on every strip, so two channels can be compared by eye. Full deflection is 225 cents, a vibrato and a drift at full stretch together.

This is the one place in the instrument where a readout and the control feeding it are allowed to disagree, on purpose. The depth knob reaches 1200 cents so that a square can trill at a real interval, but almost nothing anyone dials goes that high. On a scale of an octave, an ordinary vibrato of five cents would sit within a single pixel of centre, and the lamp would show nothing at all. Past 225 the needle pegs, as a needle should off the end of its scale, and a wide glide pegs it until the partial is within a couple of semitones of its note. The scale is defined in one place, and a test keeps it above the drift range, so it cannot drop below what drift alone produces.

It is compressed, not linear, also on purpose. The travel is about fifteen pixels either side of centre. Spread linearly over 225 cents, an ordinary vibrato of five cents moves the needle a third of a pixel, so every subtle setting would look the same as no setting at all. A square root keeps the ends where they are and gives the shallow part of the range more room:

| Depth     | Linear  | As drawn |
| --------- | ------- | -------- |
| 5 cents   | 0.3 px  | 2.2 px   |
| 25 cents  | 1.7 px  | 5.0 px   |
| 50 cents  | 3.3 px  | 7.1 px   |
| 200 cents | 13.3 px | 14.1 px  |

A scale normalised to each strip's own depth would run the needle to both edges whatever the depth, and say nothing about how deep the modulation is. The needle parks only when nothing is sounding, so a partial with no modulation reads dead centre, which means in tune.

At the display's fifteen frames a second, an LFO above about seven Hz is faster than the lamp can follow and looks like a shimmer, not a pulse. That is a limit of the frame rate, not of the lamp, and raising the frame rate to fix it would cost far more than the lamps do.

**What they cost.** Nothing measurable on the audio thread. Every value they show is already worked out by the render loop for its own use, so capturing it is five stores per partial per control block. Against a control that renders 16 voices, the build with the lamps benchmarks within the run-to-run noise of the build without them, and its fastest run is the faster of the two.

The drawing is where the care goes. Brightness is quantised to twelve steps and the needle to whole pixels, for the same reason the meters are segmented. A lamp that follows its value exactly repaints on every frame in which the value moves at all, and for anything modulated that is every frame.

Merging matters more than quantising, and not in the way you might think. The lamps are handed back to the editor instead of invalidating themselves, and they are merged by row, not by neighbour, because every strip's lamps sit at the same five heights. Putting them through the general merge with the meter bands costs 823,000 pixels a frame on a mixer with all 32 channels modulating. Six rectangles are not enough to keep the rows apart, and each merge pairs a lamp at the top of a strip with a meter band at the bottom. Merged by row it is 75,000, against the meters' own 28,000. On the factory presets it is far smaller: 11,000 for _Slow Pad_ and 19,000 for _Shimmer_, played as a four note chord, against a mixer of 1,278,000 pixels.

The second lamp on the output rule costs nothing in that count, and that is the merge working as intended. Six lamps sit at five heights, so the band a row merges to does not change, and the row-merged figure is the same to the pixel with or without the second lamp. Only the general merge notices, and it gets better, not worse: more rectangles sharing a row give it more to pair usefully.

The output lamps also cost almost nothing on a preset being played. Driven by a ramp they move every frame like the rest, but their rows sit still and so does the controller, so each lamp quantises to the same step frame after frame and asks for no redraw. _Slow Pad_ is unchanged by the pair and _Shimmer_ moves by under 200 pixels a frame.

### Meters

Each channel meters what that partial is actually putting out, on a decibel scale floored at -48 dB. It reflects the final gain, so it shows the envelope, tremolo, velocity, aftertouch, the Nyquist fade and mute or solo all at once, and you can watch the spectrum change as a note decays.

The meter fills the fader's own track instead of sitting beside it. A fader that also drew its set level would repeat what the cap already shows, so the whole track shows output instead. The cap is drawn as translucent glass so the meter shows through it, and the knob pointers are drawn the same way so the value arc shows through them.

The cap has a light body the lit segments show through, a softer edge, and a lip along the top instead of a line across the middle. A bright line across the middle to mark the exact position would be almost as heavy as the edge around it, making three light lines within ten pixels, and the cap would look like a pill with a slot cut in it. The line is not needed, because the exact position is printed in dB directly under the fader.

It reads the loudest instance of a partial across the sounding voices, not the sum, so it shows the shape of the patch instead of hitting the top as soon as you play a chord.

The cost on the audio thread is close to nothing. It samples a value it has already computed, once per 32-sample control block, and this measured within run-to-run noise on the benchmark.

Drawing them costs more, and getting that cost down took careful measurement. Meters are the only thing in the window that changes on its own, so they set the cost of playing a note. Several things had to be right at once, and almost none of them are about drawing being slow.

The meters are segmented, not continuous. That is the classic spectrum-analyser look, and it also means the display only changes when the level crosses a segment boundary. A lamp lights when the level reaches it and goes out only once the level has fallen clear of it, so a note sitting on a boundary cannot flicker. On a slow decay, 138 frames out of 152 have nothing to redraw.

Nothing repaints itself. Each meter passes the band that changed up to the editor, which collects all 33 and invalidates a handful of rectangles once. Merging them into a single rectangle is nearly as bad as leaving them scattered, since the bands sit at different heights and their union covers most of the mixer. Six rectangles is the setting that stays both specific and cheap. It invalidates 7,000 pixels a frame, where one merged rectangle invalidates 54,000.

The meters are opaque and paint their own slice of the channel gradient, so the strip behind them is not redrawn. They are read fifteen times a second, not thirty. That is more than a segmented meter can show anyway, and it halves the number of frames in which anything is invalidated.

Together those take a decaying chord from 141% of a core to 1%. It can still look sluggish, because the cost was never the CPU. Since around macOS 10.13, CoreGraphics answers a list of scattered dirty rectangles by redrawing the one rectangle that encloses them. For this layout that is the whole window, every frame, however carefully the rectangles are chosen. JUCE's own note in `juce_NSViewComponentPeer_mac.mm` says so and points to the fix: a Metal-backed layer, which keeps the rectangles separate and does the compositing on the GPU. `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` turns it on and has no default, so it is set in CMakeLists.txt for Apple builds. That setting is what makes the difference on the machine, and none of the work above would show without it.

One idea looks like it should help and does not: splitting the mixer into halves that update on alternate frames. Each meter still updates at the same rate. Each frame covers half the width, so the area per frame halves, but the number of frames doubles, and frames are the more expensive of the two.

### Folding the mixer down

Clicking a section heading in the caption gutter folds that group of rows away, and the window shrinks by exactly the height those rows took. Clicking the rule that section draws across any strip does the same, from where your hand already is. The gutter is at the far left, and reaching it from the partial you are working on means crossing the mixer and finding your way back, by which time you have lost track of which column you were in. The rules line up with the headings because the same call lays out both, so the strip needs no geometry of its own and cannot drift out of step with the gutter.

The window's height follows either way, and it has to be measured before it is allowed to change. Applying the resize limits is itself a resize. JUCE ends `setResizeLimits` by constraining the current bounds to the new ones, and unfolding raises the minimum height by exactly the rows coming back. On a window already squeezed to that minimum, the limits grow it by those rows, and the arithmetic must not add them a second time. If it does, a window squeezed to 997 folds to 847 and comes back at 1147. All spare height goes to the fader, the one flexible row, so the faders swell to fill the screen and disappear under the dock. Folding never shows this, because folding lowers the minimum, so nothing is constrained and only the arithmetic changes.

**A rule under the pointer shows it, like everything else here.** The pointer becomes the same hand the gutter's headings show, and that section's caption in the gutter lights in the accent colour along with its disclosure mark. The mark lights with the caption instead of staying dim beside it, because the two are one control, and half of it lighting up looks like a rendering fault.

For that, the rules have to count when working out what the pointer is on. The hover reports a row, and a row is found by the control standing in it, which for a rule is nothing. Without that, the one part of a strip that can be clicked would be the one part that shows nothing when you point at it.

Lighting the band takes three more things. A heading's highlight is drawn over the children, not behind them, because each child carries an activity lamp that fills the whole row and paints an opaque background. Drawn underneath, the headings would not seem to highlight at all. The noise channel draws a row highlight too, so the band does not stop at the thirty-second column. And the gutter is otherwise a passive display lit by whichever strip the pointer is on, so it also has to light when the pointer is on one of its own headings, the one part of it that can be clicked.

The hand is given back as the parent's cursor, not as a plain arrow, and this is easy to get wrong. A strip asks for its parent's cursor so that LINK and the drawing tool can set one across the whole mixer at once. An arrow set on the strip would hide theirs for everything in it.

Nothing has to be made clickable for it. The rules are the one part of a strip with nothing standing on them, and their lamps already let clicks through so the rule underneath reads as a continuous line. The pointer shows the same hand over a rule as over a heading. Pitch modulation, envelope, key off, amp mod and output each fold. All five together are 480 pixels. The tuning at the top of the strip and the faders at the bottom never fold. The tuning is what the instrument is for and the faders are what you mix with, so neither is ever in the way.

It folds across the whole mixer, not per channel. The strips are columns sharing one set of rows. Folding a group on one channel and not the next would put every row below it out of step with the gutter captions, which are the only labels for the knobs.

A folded heading keeps its activity lamp, so a group you cannot see still shows whether it is doing anything. The fold state is saved with the session, not with the patch, along with the window size, the zoom and the three LINK settings, so loading a preset never rearranges your screen.

LINK has three settings, not two. The scope and the curve say what a drag would reach and how it would be shared out, and the switch says whether it reaches anything at all. If the switch were not saved with the other two, a reopened window would remember exactly how a drag is distributed, with the drag switched off. So all three are saved together, and everything that depends on any of them is updated by one function, not by whichever callback happens to notice. Restoring a window and choosing from the menu have to end up in the same state, and a second list of things to update is a second list to forget something from.

Rows in a folded section are hidden, not set to zero height. A knob with no height still takes the mouse and still responds to hover, so it would keep lighting gutter captions and opening LINK menus for controls nobody can see.

### Ganging the channels

**LINK** gangs the strips: dragging any knob moves the same knob on the others. It works relatively. It adds an offset to wherever each strip already is, instead of dragging everything to one shared value, so a spectrum you have shaped by hand keeps its shape.

The button sits at the head of the caption gutter, in the band the strips beside it use for their channel numbers. That column is where it belongs, since what it gangs is the rows the captions name, and it is the only control not on the bar. Having it there also helps the bar. The bar fits in one row at the width the window opens at, not two, and the converter readouts have room to show kHz and bit, not only the figures.

It opens a menu instead of toggling, because what a drag reaches and how it is shared out matter as much as whether it is on. It lights when the switch inside is on. The same menu opens on a right-click anywhere in the mixer, which is where you are when you need it. Both settings are fixed when a drag begins, so changing one midway cannot half-apply two different rules.

While LINK is on, the pointer over the mixer shows which curve is loaded with five bars: level for uniform, peaked in the middle for taper and scattered for spread. A mode you cannot see is easy to forget you are in, and this one changes what every drag does.

With LINK on, pointing at a knob lights every knob a drag from it would move, before you touch anything. How brightly each one lights follows the curve. Under taper the strips nearest the one you point at glow most and the far end of the mixer barely at all, and channels outside the scope stay dark. Changing the scope or the curve redraws this under the pointer, so you can see the difference between the scopes and the curves instead of having to try them.

**LINK SCOPE** picks the channels:

| Scope          | Reaches                                                                                                              |
| -------------- | -------------------------------------------------------------------------------------------------------------------- |
| All            | every partial                                                                                                        |
| Same interval  | only the strips sharing the interval class of the one you grab, so you can move just the fifths, or just the octaves |
| Odd harmonics  | 1, 3, 5 and so on, the hollow half of the series                                                                     |
| Even harmonics | 2, 4, 6 and so on                                                                                                    |
| Selected       | the channels picked by their numbers, whichever they are                                                             |

**Selected reaches the channels you pick.** The other scopes reach channels by a rule, and none of them can move an arbitrary set, partials 3, 5, 9 and 12 and nothing else. Command-click a channel's number, Ctrl-click on Windows and Linux, to pick it or put it back, and drag from there across the numbers to do the same to every channel crossed. The channel the drag starts on decides which: starting on one that is not picked picks every channel crossed, and starting on one that is takes them away, the way a stroke works in a spreadsheet, so a drag never flickers a channel back and forth. A picked channel has a short bar in the accent under its number. That is separate from the lit number, which shows what the next drag would reach and changes with the scope, where the bar shows what was picked and stays. Picking a channel chooses LINK with the Selected scope, as if both had been chosen from the menu, because a selection is only for that scope. Taking one away leaves the tool alone. The selection is saved with the session beside LINK's other settings, and "Clear selection" in the LINK menu empties it.

It picks channels and not single knobs, and on the numbers and not on the knobs. A drag moves one row across the channels it reaches, which is what LINK does, and a set of knobs from different rows has no single drag that means anything for all of them: there is no one move that suits a tuning knob and a decay knob together. The number is the channel's name, and nothing else uses a click on it. The command key is the one modifier free there: shift draws, a click with alt resets a knob, and the command key on a knob asks for fine movement, which is why picking happens on the numbers.

**LINK CURVE** picks how the drag is shared out:

| Curve               | Effect                                                                                                                  |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Uniform             | every selected strip moves by the same amount                                                                           |
| Taper from the grab | a strip moves less the further along the series it sits from the one you grabbed                                        |
| Spread / gather     | pushing up scatters the strips apart along random directions, pulling down gathers them onto the strip you are dragging |

Taper is anchored on the strip you grabbed, which always gets exactly its full share of the drag. It has to: the knob under the mouse follows the mouse, so any other weighting would put it visibly out of step with the strips beside it. From there a strip's share falls off in a straight line with distance, reaching nothing thirty-one channels away.

So where you grab decides the shape. Grab channel 1 and the mixer tilts down across its full width, from everything to nothing. Grab channel 32 and it tilts up instead. Grab channel 10 and the curve peaks under your hand and falls away on both sides, to 0.71 at channel 1 and 0.29 at channel 32. The distance is always measured against the full width, not against whichever end is nearer, so the reach stays the same wherever you grab. Scaling it to the nearer end would make a grab on channel 2 drop channel 1 to nothing in one step.

Spread picks its scatter directions once when the drag begins, so the pattern holds still while you move instead of churning. Gathering collapses onto the strip you are dragging, not onto a fixed average, so the knob in your hand is what everything converges on. Half a drag downwards is enough to get there.

The offset is always measured from the values captured when the drag started, so returning the knob to where you began restores the strips exactly, even if some of them hit an end stop on the way. That holds for every curve.

**Holding shift draws the faders instead of ganging them.** Dragging across the fader area with the modifier held sets every channel it passes over from the pointer's height. This is a separate gesture from LINK, not a variant of it. LINK spreads one relative move across a scope by a rule, and drawing sets absolute values freehand. A formant cannot be drawn with LINK, and "everything up three decibels" cannot be drawn.

It works on the faders and nothing else, because of what a fader is. A fader's value is where it stands, which is what lets a pointer's height mean something. A knob has no such reading, so drawing across the tune row would be meaningless, not a smaller version of this feature.

Three things make it feel like drawing and not like poking. The pointer belongs to the fader the drag began on until the button comes up, so no other strip ever hears about it. The positions go to the editor in screen coordinates, since a drag that crosses strips cannot be described in any one strip's coordinates. Every column between one position and the next is filled in, because two mouse events can be several strips apart, and drawing only where they land leaves gaps exactly where the hand moved fastest. And the whole stroke opens one gesture and closes one, so it is a single step in the history however many faders it moved, the same rule a LINK drag across 32 channels follows.

The noise channel draws with the rest. It is not a harmonic, but it is a fader, and a stroke that crossed it and left it alone would be stranger than one that changed it.

**There are two ways to arm it, and the panel treats them the same.** Holding shift is the quick way. The DRAW switch under the LEVEL caption is the way that stays on, and it is saved with the session like LINK. Either one arms it, both light the same switch, and a tool you left on is still on when you come back. The switch stays lit the whole time drawing is armed, not only while a key is down, because a mode you cannot see is easy to forget.

**The modifier is polled, not listened for**, which is not the obvious way but is the only one that works. JUCE delivers a modifier change to the component under the pointer, and `Slider` handles it without passing it on, so over a fader or a knob the editor never hears about it. Listening would give a tool that can only be armed with the pointer in a gap between channels, and that stays on permanently if the key is released anywhere else. Reading the modifier on the housekeeping tick cannot be blocked by anything.

**When armed, every fader lights and nothing else does.** This is the same preview LINK uses, not a second kind of highlight, because both answer the same question: what would the next drag reach. Two ways of showing that would be two things to keep in step.

What differs is the colour and the grading. LINK's preview is per channel and graded by the curve, since it shows how much each channel would take. The drawing preview is one colour at full across the whole row, since every fader can be drawn equally. It is lit in the accent colour, the same as the tool button, so the band and the button read as one signal.

It marks the tracks, not the caps. A cap is what you grab, so it is the right thing to light for LINK. Drawing never grabs anything. It sweeps across a band, so the band is what has to be visible. The noise channel lights with the rest, since it draws with the rest.

**There is one switch and it shows one tool.** A drag can be a link or a drawing, not both, so one button at the head of the gutter shows whichever of the three is current: the plain pointer, LINK, or the pencil. It shows what the next drag would do, and the pointer over the mixer shows the same.

One switch avoids a problem two separate switches would have. A separate LINK switch would have to read as off while drawing was armed. It would still carry its scope and curve, but the gesture would be taken from it, so a lit switch would promise something the mouse-up would not do. With one control there is nothing to keep consistent.

**Holding the modifier is a tool you are holding, not one you chose.** Drawing can be armed either way. Picking the pencil latches it, and it stays. Holding the modifier makes the button show the pencil for exactly as long as the key is down, and gives the previous tool back on release. That is why drawing takes priority over LINK while it is armed instead of replacing it. LINK keeps its scope and curve throughout, so going back to it finds what you left.

### The output meter

The top bar has a horizontal output meter, split into left and right. It is split because the two channels are identical until something is panned off centre, and a summed meter would hide exactly that. It reads the finished output after master gain and the clipper, so it shows what actually leaves the plugin.

It is segmented like the channel meters, for the same two reasons: it reads at a glance, and it only asks for a redraw when a lamp changes, not on every frame in which the level moves. Lamps run from teal through amber to orange as they near full scale. The colours are fixed to their decibel positions instead of stretching with the level, so a lamp keeps its colour whatever the bar is doing. Unlit lamps show their colour dimmed instead of going dark, so the top of the scale is visible before you reach it. A scale underneath is marked at -48, -36, -24, -12, -6 and 0 dB.

Each bar has a peak hold, because an output meter is mostly wanted for what it hit a moment ago. The peak stays lit as a single lamp above the bar for a couple of seconds, then falls with it.

### Panning

Every channel has a PAN, the noise channel included, instead of one width control spreading the series out. A single knob can only make one shape. Placing the partials by hand lets you put a partial opposite the one a semitone away from it, or the octaves left and the sevenths right, and a width control could not produce any of those.

The pan law is equal power, so the number on the knob is the number in the audio and the level holds steady as a partial moves across the field. The tests measure both, reading the positions back from the rendered audio: hard left and hard right land within 0.01 of the ends, half left lands at half left within 0.02, and a partial swept across the whole field varies in loudness by under 0.1 dB.

A good starting shape is mirrored pairs, 1 and 2 in the centre widening out to 31 and 32 at the edges, with the sides alternating so the louder of each pair does not always land on the same side. _Slow Pad_ and _Shimmer_ use it in their pans, where it can be changed by hand. It is worth understanding before you change it. Neighbouring partials have nearly the same level in any normal spectrum, so putting each pair on opposite sides keeps the image centred whatever shape you dial in. And because every position has a mirror, no partial ends up hard panned with nothing on the other side.

### Character

Sets which kind of oscillator every partial uses. There is one choice for all 32, on the bar at the head of the series group, because an instrument is built from one circuit repeated, not from a different circuit per channel.

Every entry is a sine oscillator. They differ in how each one falls short of a perfect sine. That is the only thing that tells two analogue oscillators apart. No circuit produces a mathematically perfect sine, and the way each design misses is what people mean when they call one warm and another sterile. Pure is the plain sine. The next five are circuits, simulated once at startup. The last six are measured from recordings of real instruments.

| Character | What it is                                            | What it does                                                                     |
| --------- | ----------------------------------------------------- | -------------------------------------------------------------------------------- |
| Pure      | the table as sampled                                  | nothing. The plain sine, and the default                                         |
| Bulb      | a Wien bridge held steady by a lamp                   | no harmonics at all, and a level that lags half a second behind every pitch move |
| Rail      | a phase-shift oscillator driven into its own supply   | a third harmonic at -23 dB and a fifth at -45, and no even ones                  |
| Diode     | a triangle shaped into a sine by two mismatched ones  | a second, third and fourth all near -32 dB, which is a kink rather than a warmth |
| Valve     | a triode, biased so one half of the wave bends first  | a second harmonic at -20 dB over a third at -23, which is the warm one           |
| Op-amp    | one that cannot change its output as fast as asked    | nothing below a kilohertz, and a third harmonic climbing to -19 dB above it      |
| Wheel     | a tonewheel organ's generator, recorded               | a second harmonic of -13 dB on the lowest C, and -42 to -50 dB above C4          |
| FM        | one operator of an FM synthesizer, recorded           | a second harmonic at -54 dB and the rest below -63                               |
| PD        | a phase distortion synthesizer's oscillator, recorded | a second harmonic at -52 dB and the rest below -57                               |
| 6581      | a sine through a 6581 chip's measured converter       | a second harmonic at -23 dB and odd ones at -55 to -63 dB up to the 32nd         |
| VCO       | an analogue oscillator's sine, recorded               | a second and third harmonic near -25 dB, and a mixer that clips one side first   |
| Combo     | a transistor combo organ's 8' lever, recorded         | odd harmonics only, a third from -16 dB at C2 falling to -35 dB above C7         |

On the panel it is a button at the head of the series group, in capitals like everything else on the bar. It lights in its own colour, and the colours are there to tell the characters apart at a glance. FM, PD and 6581 light in a colour of the instrument they were measured from: FM in the green of the synthesizer's membrane buttons (#00825f), PD in the colour of the model name printed on the synthesizer (#d5df7c), and 6581 in the blue of the startup screen of the computer the chip was made for (#867ade). The other eight are spread as far apart in hue as the gaps those three leave allow, about thirty degrees apart, all at one lightness and saturation in OKLCH so that none looks brighter than the rest. The warm end keeps roughly the order they had before, with Op-amp on red, Valve on orange and Bulb on amber. Pure does not light at all, because it adds nothing.

The machine buttons light the same way. Each of RESONATOR, ECHO, REVERB and CLIP has its machines spread evenly round the hue wheel at the characters' lightness and saturation, so the four resonator modes are a quarter of the wheel apart, the three echoes and three reverbs a third, and the eight output machines an eighth. Each set is turned so that the first machine of each, which is what a new patch has, lands on a different colour from the others and from Op-amp's red: Piano purple, Tape green, Room amber and Soft teal. The reverb's three sit between the echo's, so the two buttons beside each other never light in the same colour.

The knobs in each frame light their rings in the button's colour, so they read as setting that machine. The character's STRETCH, TRACK and WOBBLE still work on Pure, which does not light, so on Pure their rings keep the accent.

**It costs nothing per sample, and that decided the design.** The oscillator is one interpolated table read, an envelope tick and a gain, and it runs 512 times per sample at 16 voices. A waveshaper in that loop would cost as much as the whole engine again, and four times oversampling to keep it from aliasing would cost three engines more. A fixed waveshape does not need to be in the loop at all. The circuit is run over a sine once at startup, the harmonics it adds are measured, and a table is built from them. The inner loop then reads a different table and otherwise runs the same instructions. Measured against Pure at eight voices, on a machine quiet enough for the figure to mean something, Diode costs two percent more, Rail and Valve three, and Op-amp six. Op-amp costs more because one note's partials fall in different bands and read several tables where the others read one. The measurement is accurate to about two points. Bulb comes out at two, and Bulb reads the same plain sine table as Pure.

**The phase is stepped in fixed point.** Inside the loop each oscillator's phase is a 64-bit fraction of a turn, so stepping it is one integer add that wraps round the turn by itself, and its top twelve bits are the table entry and the rest the fraction between two. Between blocks it is kept as a double, which is what everything that sets or reads it works in. This took about a tenth off the engine against a double stepped and wrapped by hand. Thirty-two bits would have been as fast, but would round each step to a hundred thousandth of a hertz, enough for a tone to drift measurably off its pitch in a second. At sixty-four the clips on the site moved by under -83 dB, and most not at all.

The harmonics cost cache instead. One 16 kB table shared by 512 oscillators fits easily in L1. A character uses several tables at once, and that is the only reason any difference shows up.

**The harmonics alias like any others.** So each character is built several times, each version stopping at a different harmonic, and a partial reads the highest version that still fits under Nyquist at its current pitch. A partial at a kilohertz has room for its 23rd harmonic. One near the top of the range has room for none and reads the plain sine. One bent upwards drops its top harmonic on the way up instead of folding it back down. When the converter's rate is turned down, the full table is used, because folding is then the sound being asked for.

**Changing table clicks unless it is a cross-fade.** Two tables hold different values at the same phase, so swapping between them at the edge of a control block puts a step in the wave, and a step is a click. A vibrato that sits across one of the lines between tables crosses it twice a cycle and ticks at twice the vibrato rate. That is how the problem was found. So the block that changes table plays a cross-fade from the old table to the new one, in the same way the gain slides across a block instead of stepping at its edge. It costs a second table read for 32 samples on the rare block that crosses a line, and nothing on the blocks that do not. That is why Op-amp, whose partials cross the most lines, is still within five percent of Pure. Measured as the largest step from one sample to the next, a vibrato held across the first slew line reads 0.145 with the table swapped and 0.082 with it cross-faded, against 0.083 for the same note on a plain sine. A test checks this.

The cross-fade also covers the other two ways a table can change: the line where a harmonic runs out of room under Nyquist, which every character has, and a different character being chosen while a note is sounding.

Every table meets two rules, and a test checks both across every character and every band. The fundamental comes out at the amplitude a plain sine would have, so choosing a character does not change the level and the fader keeps its meaning. And any DC the analysis finds is left out of the table, because 512 oscillators each carrying a small offset would use up headroom.

**What it does that a fader cannot.** This is an additive instrument, so a second harmonic on partial 3 lands near partial 6. It does not land exactly on it. TUNE and STRETCH move the real partials off exact whole-number ratios, while a character's harmonics sit at exact multiples of the partial that produced them, so the two beat against each other. That beating is the useful part, and it is also why the recipes are small. Thirty-two partials each adding a few harmonics fill the top of the spectrum quickly.

**A rack of thirty-two is not one oscillator copied thirty-two times.** Each of the five circuits and the VCO has a fixed spread. Each partial sits a few cents and a fraction of a decibel off the rack's specification, by the same amount in every session, in every host and on every machine. It is not DRIFT, which wanders. The offsets are where the units sit when they are switched on, and they are the same every time. The first partial is the reference the other thirty-one were tuned against, so its pitch and level are exactly on spec. That keeps a note on the key that played it and keeps a patch from changing level when the character changes.

| Character | Pitch   | Level    | Why that one                                                                              |
| --------- | ------- | -------- | ----------------------------------------------------------------------------------------- |
| Bulb      | ±1.5 ct | ±0.1 dB  | the lamp holds the level and nothing else here does, and it heats the bridge it regulates |
| Rail      | ±2.5 ct | ±0.5 dB  | three RC stages set the frequency and their errors stack, and the rail sets the level     |
| Diode     | ±2 ct   | ±0.4 dB  | an integrator sets the frequency, which is accurate, and two diodes set the level         |
| Valve     | ±3 ct   | ±0.35 dB | a heater in every unit and nothing regulating either end of it                            |
| Op-amp    | ±2 ct   | ±0.3 dB  | an ordinary amplifier around an ordinary core                                             |
| Wheel     | none    | ±1 dB    | every wheel is geared to one motor, and the recorded wheels' levels vary                  |
| FM        | none    | none     | a crystal clock and levels calculated digitally                                           |
| PD        | none    | none     | the same                                                                                  |
| 6581      | none    | none     | the same                                                                                  |
| VCO       | ±2 ct   | ±0.3 dB  | one was recorded, so there is no rack to measure, and it takes the op-amp's               |
| Combo     | none    | none     | every octave of a note comes from one oscillator, and its levels follow a smooth curve    |

Wheel's level spread comes from the recordings. The 8' wheels vary by 2 dB rms around a straight line through their levels. Part of that comes from the keyboard's wiring and not from the wheels, so the spread uses about half of it.

The spread is what a tuned rack holds, not the tolerance of the parts. Component tolerance is a percent or more, which is eighty cents, and a rack spread across eighty cents has not been tuned. What remains after tuning is a couple of cents. The order of the characters comes from which part of each circuit sets its frequency and which sets its level.

The pitch figures were set by playing, not by measuring. At twice these figures, three to six cents, several patches sounded detuned, like a rack that needs tuning again. The level spread did not need changing.

**No two units distort by exactly the same amount.** If all thirty-two circuits clipped, folded or slewed by exactly the same amount, the rack would still be one circuit repeated, even with each one tuned a little differently. So each unit is also built to one of three drives, 15% either side of the nominal, chosen by index like the rest of the spread. A third of the partials sit on the nominal drive, which is the drive every other figure given for these characters describes. The six recorded characters are built at one drive, because a recording has one fixed amount of distortion.

| Character | Its own drive         | The third or second harmonic across the three |
| --------- | --------------------- | --------------------------------------------- |
| Rail      | how far into the rail | -25.8, -23.5 and -21.6 dB                     |
| Diode     | how badly matched     | -35.7, -33.2 and -30.9 dB                     |
| Valve     | how hard it is driven | -22.0, -20.1 and -18.6 dB                     |
| Op-amp    | how fast it can move  | -27.3, -24.5 and -22.6 dB                     |

**Three drives, not thirty-two.** The drive is built into the table, so a drive per channel would mean thirty-two tables per character and a note reading thirty-two of them per sample. A character costs nothing only because 512 oscillators read one 16 kB table that stays in cache. Three drives keep the idea and keep the number of tables small: the circuits take 84 against 28 with one drive, 1.4 MB against 460 kB. The recorded characters add 158 tables, 2.6 MB: 72 are Combo's eight bands, 31 are the 6581's 32 harmonics, 28 are Wheel's seven bands, and 9 each are FM's, PD's and the VCO's ten harmonics. Every table together takes about 50 ms to build at startup. The per-sample work is the same single interpolated read, because only the table being read differs.

**The three drives cost two to three percent of a character.** This is the one figure here that had to be measured, because the risk was cache. Two builds were compared, one drive against three, with ten runs of one and five of the other, taking the minimum of each. At eight voices: Rail 7.13 to 7.24% of a core, Diode 7.13 to 7.27, Valve 7.06 to 7.26 and Op-amp 7.31 to 7.52. Pure and Bulb show the measurement noise, because they have no tables to multiply and cannot have changed. Pure went 7.04 to 6.98 and Bulb 7.01 to 7.04, so the noise floor is about a percent and the four that grew are above it.

**The rate limit gets a third of that spread.** Its harmonics do not grow steadily with the drive. They appear all at once when the wave starts to be limited. At the full 15% either side, the third harmonic would range from -41.7 dB to -20.5, which is too far apart to be one circuit built more than once. At a third of the depth it spreads about as far as the other three characters do.

The drive is drawn from its own random stream, separate from the one for pitch and level. Sharing the stream would shift every draw after the first and change where all thirty-two units sit, which would make a different rack, not the same rack with three drives. With a shared stream every preset on Bulb changes sound, all eleven of them, even though Bulb has no harmonics to drive.

The spread makes the beating described above happen even in a patch with no detuning. With every TUNE at zero the partials are exact multiples of each other, so a character's harmonics land exactly on the partials above and only add or cancel depending on phase. A couple of cents of spread makes them beat instead. The beat rate follows frequency, because a cent is a fraction of a hertz at the bottom of the series and several hertz at the top. On a low A, partial 1's second harmonic meets partial 2 at 220 Hz and beats at well under a hertz, while partial 16's meets partial 32 at 3.5 kHz and beats at about ten. It costs one add and one multiply per partial per control block, which is a thirty-second of one sample's work, and the spread itself is a few hundred random draws at startup. Pure has no spread, so patches made before characters existed play exactly as they always have.

**Bulb has no harmonics.** A Wien bridge makes the cleanest sine of any of these circuits, because the lamp in it is there to hold the amplitude steady. The lamp costs time, not distortion. Its resistance follows how hard the loop drives it, but only as fast as a filament heats and cools. The gain the loop needs also changes with frequency, because no two ganged parts track exactly. So the level sags when the pitch moves and settles once the lamp catches up. It is modelled as one pole per partial that follows the pitch with a half-second time constant, and the remaining error can take up to a quarter of the level.

How far the lamp is thrown out of balance is the one number here not taken from the circuit. A bench oscillator is swept across a decade by a knob, and its lamp responds to a frequency that has doubled. This one is moved by vibrato, drift and a hand, which means by cents. A model true to the bench does almost nothing at that scale. With full scale at a semitone it gave 0.8 dB under a 25 cent vibrato and a hundredth of a decibel under drift, which nobody can hear. Scaled to a quarter of a semitone it sounds like the circuit it is named after. Measured on a held note, from the quietest the partial gets to the loudest: nothing while the pitch is still, 4.3 dB under a 25 cent vibrato, and a 2.5 dB dip across a two-semitone bend that recovers once the lamp catches up. A test checks those figures.

So it responds to playing gestures. A held note with no vibrato, no bend and no hand on it is exactly Pure, because a lamp has nothing to correct in an amplitude that is already steady. Drift moves it least, because an automatic gain control exists to remove a change that slow.

**Valve is the second-harmonic character.** A triode's curve is not symmetrical, so a wave on it bends over on one side before the other, and asymmetry is what makes even harmonics. Where the wave sits on the curve decides which harmonic dominates. Further along the curve, the second grows while the third falls away. Biased to put the second at -20 dB and the third at -23, it is the only character here whose loudest added harmonic is an octave rather than a twelfth. That is what people mean by valve warmth.

**Op-amp is not a fixed waveshape.** That is why the table is chosen by frequency as well as by how much room is left under Nyquist. A rate limit does nothing to a wave that never asks the amplifier for more than it can give, and it turns a fast wave into a triangle, so the shape depends on the pitch. The corner sits at a kilohertz, and that is the second number here scaled to the instrument rather than taken from the part. A 741 slews at half a volt per microsecond, so at ten volts peak it stops keeping up at around 8 kHz. A partial up there has no room under Nyquist for the odd harmonics slewing makes, so the character would be correct but inaudible. At a kilohertz it can be heard. That is also where the keyboard tracking rolloff sits, at about C6, in the middle of where people play. Below the corner the partials are untouched. Above it they get harder as they climb, as a slew-limited oscillator does when played up the keyboard.

Three tables cover it, at a quarter, six tenths and twice again past the corner, and anything above the last one reads the last one. This loses nothing. Measured, the third harmonic reaches -19.1 dB by twice the corner and does not change however much harder the limit works, because by then the wave is a triangle, and a triangle at a given fundamental is always the same triangle. Op-amp is also the character that costs the most, about six percent over Pure at eight voices, because one note's partials land in different bands and read several tables where the others read one.

A rate limit is the only imperfection here with a memory, so its cycle is simulated rather than shaped. A limiter runs over a sine for several cycles, and the steady state it settles into is what gets analysed.

**Wheel, FM and PD are recordings.** Each instrument was recorded one sine at a time, from its direct output, at 48 kHz and 24 bits. Each take is a held note of about four seconds, and each instrument has one take with no note, to measure the noise floor. The pitch of each take is fitted by least squares, and then the level and phase of harmonics 1 to 10 are fitted over the steady part of the note. The take is shifted in time so its fundamental is a sine of amplitude one, which is the form the tables use. `Measured.h` holds the results, with a table of the levels for each one.

A tonewheel organ has a separate wheel for every pitch, so Wheel is not one shape. The 8' drawbar was recorded on every C and F# and on four more keys in the bottom octave, and the 1' drawbar on the C and F# of the top two octaves. The takes are grouped into seven bands where their harmonics are similar, and a partial reads the band for its own frequency, in the same way Op-amp reads a slew band. A partial that bends or glides across a band edge changes table with the same cross-fade as every other table change. **The wheels move as they turn.** In the recordings every wheel carries a pair of sidebands at the rate its shaft turns, and a smaller pair at twice that. Fitted at the exact frequencies and split into the part that moves the level and the part that moves the pitch, they come to about a cent of pitch and a tenth of a decibel of level once a turn, with each wheel different, from a quarter of a cent to four. The wheels of one note differ only in their number of teeth, so a shaft turns at the note's frequency folded down into the octave from 16 to 32 Hz: 16.35 for C up to 30.87 for B. On top of that, every wheel moves by about 0.9 cent at 25 Hz, which is the motor at 1500 rpm on 50 Hz mains. The motor drives every wheel, so that part is the same for every partial and every note, and the engine keeps one motor phase that every voice reads. Each shaft starts at a random point in its turn when a key goes down, because a key can be pressed at any point. The depths are drawn once per unit from the measured ranges, on a random stream of their own, so no other character's rack changes. The five takes of one wheel described below agree on these figures within half a cent and a hundredth of a decibel, which is what showed them to be the wheel and not noise. Below C3 a wheel's sidebands land exactly on other wheels of the same note, which leak into the recording, so those were not used. All of it is worked out once per control block, at most 32 turns a second against 1500 blocks, and only on Wheel.

**Other wheels leak into a note.** Every wheel in the generator turns all the time, and a little of the wheels near the one being played gets into its pickup and wiring. Each candidate was measured as a peak at its ratio to the played wheel, within half a percent since the gears are near equal temperament but not on it, and counted only where it stood 10 dB above the floor around it. Four are found in most takes and are the loudest: the same note two octaves down at -47.7 dB, a fourth down at -49.6, an octave down at -51.9 and a fifth down at -55.4. The fourth down is the wheel a fifth up, an octave lower. Those four are added under each note, drawn with Wheel's own tables, since a leaking wheel is a wheel. They sit at their ratios to the key and follow bends and glides. Their level follows the note's partials with the powers added, because each wheel leaks on its own and the leaks are not in phase, so they follow the envelopes and the faders too. A leak that would fall below C1, the organ's lowest wheel, is silent. The leaks are worked out per note rather than per partial, which costs four table reads per note against the partials' thirty-two, and only on Wheel. The 50 Hz hum in the recordings is left out, because it does not belong to a note.

Five more takes played the same wheel, C5, from five different drawbars. Their harmonics agree within two decibels and their levels differ by up to eight, so a drawbar changes the level of a wheel and not its shape, and one set of harmonics per wheel covers every drawbar.

FM and PD come from the notes between C4 and C6. Below C4 the output's coupling capacitor shifts the phases of the low notes. On the FM synthesizer the takes also carry a vibrato of ±1 cent at 5.6 Hz, which restarts at the same point on every key press, so it comes from the synthesizer's LFO and not from its tone. Above C6 that vibrato blurs the upper harmonics. The vibrato is not part of the character. Neither is the noise floor, which rises by 6 dB on the FM synthesizer while a note sounds.

Both synthesizers' sines were measured with a second harmonic at -57 and -58 dB. Both carry ten harmonics where the circuits and Wheel carry five, because their upper harmonics stay around -70 dB instead of falling away. On FM the 6th to 10th hold 14% of the harmonic power. That costs ten more tables, and each band still stops at the harmonic it is named for, so nothing extra folds on a high partial. Played next to the recordings, FM's harmonics sounded right 3 dB stronger than measured and PD's 6 dB stronger, and PD's noise 3 dB stronger. Those figures are in `kFmByEar` and `kPdByEar`, kept apart from the measurements and marked as set by ear.

**PD's second harmonic comes from the mix.** Recorded as a held chord, the phase distortion synthesizer makes sum and difference tones between its notes, every one at -51 to -53 dB against a note. One quadratic on a whole mix makes those exactly 6 dB above the second harmonic it gives a single note, and the single note's second harmonic is -57.7 dB. So its second harmonic is not in each oscillator but in its mix, and PD makes it the same way: its table has no second harmonic, and its bus is a quadratic, x minus a small multiple of x squared, sized so that one partial at full fader and full velocity gets the measured -58 dB plus the 6 dB added by ear. A chord then gets sum and difference tones 6 dB above that, as the instrument does, and a quieter patch gets less of all of it. The quadratic's antiderivative is a polynomial, so the antialiasing is exact everywhere, and it only turns back at about seventy times full scale. The third-order products are 14 to 23 dB below what a shared curve would give for the third harmonic, so the third and above stay in the table. The frequency modulation synthesizer's products are 25 to 40 dB below what a shared curve would give, so FM's harmonics all stay in its table and its bus stays clean.

**The 6581 has no sine, so its converter is what is measured.** The chip makes every waveform as a 12-bit code, from a phase counter, and turns it into a voltage with a resistor ladder. On the 6581 the ladder's bits are not weighted in exact powers of two, which is most of why the chip sounds as it does. A slow sawtooth steps through all 4096 codes in order, so a sawtooth at about half a hertz, recorded at 192 kHz, gives 87 samples on every code. The output's coupling blocks very slow movement, so the ramp sags between steps, and each step is measured as the jump between straight lines fitted on either side of it. A resistor ladder's level is a weighted sum of a code's bits, so the twelve weights are fitted to 163,800 steps from the three slowest sawtooths at once. Fitted one take at a time, they agree on the shape within about 1%. Each bit is worth 1.94 to 1.96 times the one below, so where many bits change at once the output falls back: by 82 average steps from code 2047 to 2048, and by 18 to 26 at the quarters.

The chip's triangle is the test. It steps up and back down through the same codes, so a converter alone gives it no even harmonics, and the recorded triangles have them: -23 dB at the second, falling as one over n squared, which is what squaring a triangle gives. So there is a slight bow after the converter, y = x - a x squared, and a = 0.138 matches the 16 Hz triangle's even harmonics. Those takes had the chip's other voices routed into its filter. With the filter bypassed, which is the chip in its plain state, the voices come out 3.3 dB louder, the bow comes out at 0.146, and everything else stays within half a decibel, so the character uses 0.146. Without refitting, the converter and the bow together give every harmonic of the 16 Hz triangle within 0.5 dB and of the 8 Hz one within 0.9, including the odd harmonics the converter moves away from a perfect triangle's, and the sawtooth's small pattern of even harmonics a few tenths of a decibel high. A bow barely moves a sawtooth's harmonics, because what it adds is at right angles to what is there, which is why the triangle and not the sawtooth shows it.

The character is a full-scale sine rounded to the chip's 12-bit codes and run through both, worked out at startup over 65,536 points so the converter's sharp steps do not fold into the harmonics that are kept. Its second harmonic is the bow's, a over two, at -23 dB. Recorded as two voices together, triangles at C4 and G4 with the filter bypassed, the chip makes sum and difference tones between them at -26.7 dB, where a bow wholly on the mix would make them at -17 and a bow wholly inside each voice none. So the bow is split, simulated with both triangles: 0.058 of it, 40%, on the mix and the rest inside each voice, which gives the sum and difference tones and a single voice's second harmonic as recorded. With the other voices routed into the filter the mix's share came out at 50%, so it depends a little on how the chip's filter is routed. The table carries the part inside each voice, and the 6581's bus is a quadratic carrying the part on the mix, sized against one partial at full fader as PD's is, so a full partial still gets the whole -23 dB and a chord gets the sum and difference tones. The converter adds odd harmonics that do not fall away: -43 dB at the third and between -55 and -63 dB from the 11th to beyond the 30th. That tail is the chip's grit, so the 6581 carries 32 harmonics where every other character carries five or ten. Its hiss, measured from a take with no note and the other two voices filtered to keep their leakage out, falls with frequency, about -104 dB per hertz below 6.4 kHz and -115 above, and does not rise while a note sounds, so all of it is fixed: one source for the whole instrument, sized by the loudest note. The recordings were made on the chip inside a SidStation, played with [SidStation ASID](https://github.com/benjamindehli/sidstation-asid), a plugin by the same maker that writes the chip's registers directly over MIDI. So every setting reached the chip as set, with no patch logic in between, and each voice could be recorded on its own. What is measured is that unit as a whole, the chip and the output stage after it.

**The VCO is an analogue oscillator and the mixer after it.** A modulation oscillator that tracks the keyboard and makes a sine was recorded straight from its output at 192 kHz and 24 bits, on every C and F# from about 4 Hz to 6 kHz. Its shape hardly changes with pitch: from 16 Hz to 500 Hz every harmonic stays within half a decibel, with a second at -24.7 dB, a third at -25.9 and the rest falling away to -65 by the tenth. A second and a third that strong and that even is a sine made from a triangle by an analogue shaper, and it is as much as Rail or Valve add. In the top octave the second harmonic falls away, by 8 dB at 4.2 kHz, which is left out, since a partial that high has almost no room for harmonics anyway. The phases are taken from C3 to C5, where they agree within five degrees. Below that the output's coupling capacitor turns them. Between takes it moved by up to twenty cents, because its tuning is a potentiometer, but within a held note it moves only about 0.2 cents rms. Read directly from the takes, as the fundamental's instantaneous phase with a steady pitch's straight line taken off, that wander is spread evenly from half a hertz to 60 Hz, with no peak at the mains, and is about the same in cents from C2 to C5. Each partial on VCO wanders that much on its own, from noise through one pole at 60 Hz, so a rack of them moves the way a rack of analogue oscillators does.

The mixer it feeds was recorded too, driven from a voltage-controlled amplifier swept slowly from near silence to well past clipping, with the amplifier's output and the mixer's recorded together. That gives the curve sample against sample, without guessing what went in. Driven hard, the mixer is straight until it bends over a short knee and then stops rising at a rail, and its negative side reaches its rail 2.44 times sooner than its positive side. The output is AC-coupled at about 1 Hz, so a clip that lopsided also moves the whole wave by what it rectifies, which the fit takes off. Fitted as the op-amp bus's cubic knee on each side it matches the recording within 25 dB, and a knee whose hardness was fitted as well came out only 1.5 dB closer, so the VCO's bus is that cubic with the negative half driven 2.44 times harder. A gentler sweep showed a soft bend well below the rails, about -40 dB, which is 15 dB under the oscillator's own harmonics and is left out. The voltage-controlled amplifier used to drive it adds harmonics of its own at its very top, but it was recorded as the input, so that is part of what went in and not of the curve.

**Combo is a transistor combo organ's 8' lever.** Its footage levers are its sine-like voices, 16' to 1', and each was recorded alone at 192 kHz and 24 bits: the 8' on every C and F# of the keyboard, the 16' for the octave below it, the 1' on the top two octaves, and the other levers on keys where they sound near the same pitch. It is a divider organ. Every C sits 0.20 cents flat and every F# 0.37 cents flat, in every octave, so each note's octaves come from one oscillator divided down. A divider makes a square wave, and the levers are that square through a filter, so their harmonics are odd. The even ones are 53 to 80 dB down.

The filter does not move with the key, so the shape changes with pitch: the third harmonic is -16.2 dB at C2 and -33.0 at C6, and the fifth falls from -26.1 to -51.0. So Combo is eight bands, in the same way as Wheel, from C2 to C7, with the 16' takes for the octave below the 8' lever's lowest key. Above C7 it holds the 8' voicing.

**The filter belongs to the key, not to the lever.** At the same sounding pitch, C6, the third harmonic is -33.9 dB on 16', -32.2 on 8', -27.3 on 4', -20.4 on 2' and -18.2 on 1'. Each of those is played from a different key, and each matches the 8' lever's shape on its own key within about 2 dB: the 4' from C5 has the 8' on C5's -27.4, and the 1' from C3 the 8' on C3's -17.8. The two tempered levers fall between the 8' takes either side of their keys. So the organ filters each key once and every lever on it shares that filter, and Combo chooses its band by the key that is played, not by each partial's own pitch. Partial 2 of a note then has the 4' lever's tone, partial 4 the 2', and partial 8 the 1', and the partials between them follow the same rule. A high partial of a low note keeps the low key's brighter shape, as pulling out a high lever does. It carries ten harmonics, since the odd ones fall away slowly in the low bands.

The 8' level falls about 4.4 dB per octave, which is the organ's voicing and is what TRACK is for, so the table is held at the level of a plain sine. Each key also leaks its other levers 55 to 75 dB under the note, and the organ carries a 50 Hz hum at -48 dBFS. Both are left out. A held chord mixes cleanly: the difference tone between E5 and G4 is about 60 dB under the notes, so Combo has no bus drive. Its noise is about -88 dB per hertz against the note below 6.4 kHz and -92 above, and it is the same with no key held and with five, so all of it is the fixed share, `kComboFixedHiss`, and it does not build up as keys are added. Beside the other characters that much hiss stood out, so it is played 10 dB under what was measured, set by ear in `kComboByEar`.

**The synthesizers' noise is measured too.** A table holds only harmonics, so each note on FM and PD also carries the noise found under the instruments' own notes. It was measured between the harmonics, leaving out 30 Hz either side of every harmonic and the mains hum, from 100 Hz to 20 kHz over the notes from C4 to C6. Both are close to flat: about -105.5 dB per hertz against the fundamental on FM, lifting by two above 12 kHz, and about -114.5 on PD, falling gently from -112 at 100 Hz to -117 at 10 kHz. Over the whole band that is about 62 and 72 dB under the note, and 69 on PD with what was added by ear. While a note sounds, both floors are about 5 dB above the take with no note, so the noise follows the note. White noise, the same in both channels since both instruments are mono, sized from the notes' partials with their powers added, as the tonewheel's leakage is. So it follows the envelopes and the faders, and there is no hiss when nothing plays. A faint skirt close to each tone is not included, because it holds less power than the floor and on FM it is mostly the vibrato in the takes.

**How the noise grows as keys are added was recorded too.** Both instruments were recorded again as a chord built up one key every four seconds, C3, C4, G4, C5 and E5. On the frequency modulation synthesizer the noise fits a fixed part plus a part that follows the signal's power, within a decibel above 400 Hz: the fixed part is about a third of a single note's noise below 6.4 kHz and more than three quarters above. So its hiss, which is mostly above 6.4 kHz, hardly builds up: five keys add 2.8 dB there while the signal adds 7. The noise is therefore one source for the whole instrument, made by the engine after the voices are summed, not one per note. Every voice reports its notes' power each control block, and in each of two bands, split at 6.4 kHz by two poles with the high band the rest of the noise, a fixed share follows the loudest note and the rest follows the sum. With one note the two are the same, so a single note is exactly as measured. With more, the model grows within about a decibel of the instrument at every count of keys above 400 Hz: five keys add 3.6 dB above the split where a noise per note would add 7. The fixed share follows the loudest note rather than nothing at all, so a quiet patch hisses less and there is still no hiss when nothing plays. Both shares are in `kFmFixedHiss`. On the phase distortion synthesizer the fixed part is 2 to 10% with one key, so all of its noise follows the signal. Its floor between the lines did jump when G4 and E5 came in, but that was its sum and difference tones, which its bus makes. The VCO's noise was measured the same way, at about -122 dB per hertz against the note above 6.4 kHz, which is 25 dB above the recording chain's own floor. Nearer each note it rises into a skirt, which is the wander described above. The oscillator could not be silenced, so whether its noise is there between notes could not be measured, and it follows the note. The noise costs one random draw per sample for the whole instrument, and only on these five. Wheel is played as measured. Even lifted, the two synthesizers are about thirty decibels quieter in harmonics than Rail.

**What the factory presets use.** Thirty-three of the thirty-eight use a character, chosen by ear at a keyboard and by nothing else: ten on Op-amp, nine on Bulb, five on Valve, two each on Rail, Diode and 6581, one each on Wheel, FM and PD, and five left on Pure. Three of those five are on Pure on purpose. Init is the neutral patch. Just Saw and Equal Saw are played against each other to demonstrate tuning, and a difference in timbre between them would distract from that.

Bulb and Op-amp take more than half of that list between them, and they are the two that are not fixed waveshapes. One responds to the hand and the other to the register, so both do something on a keyboard that a static spectrum cannot. Diode has two. _60s Organ_ wanted a kink rather than a warmth, and a pair of mismatched diodes gives exactly that. _KarplusStrong_ puts it on its one partial. The measured characters each went to the preset of their own instrument: Wheel to _Drawbar Organ_, FM to _FM Piano_, PD to _Nylon EP_, and 6581 to _6581 Sawtooth_ and _6581 Triangle_. _6581 Pulse_ is on Op-amp, chosen by ear over the 6581 character.

### Resonator

The resonator is eighty-eight sympathetic strings, one for each key of a piano from A0 to C8, that resonate with what you play. Each is a `KarplusString` sized for its own note, in `SympatheticStrings`. They sit after the thirty-three channels are summed and before the bus drive. They are fed the whole instrument and mixed back in with it, so the drive and the effects treat them as part of what is played. The resonator is a separate stage, not a reverb type, because a reverb is a room behind an instrument and these are the instrument's own strings. A piano's pedal is something the player does, not a place.

The engine decides for each block what each string is tuned to and which dampers are up, because only the engine knows which keys are held and where Adaptive has tuned them. **Piano** frees the strings of the held keys, tuned to where each key sounds before any bend, because a piano's strings do not follow the pitch wheel. The sustain pedal frees every string. **Open** is Piano with the sustain pedal always down, so every string rings. The pedal reaches the engine as a mask over channels, so it works under MPE, where no pedal message ever does. **Octave** is the twelve strings from C3, always free. **Series** is the tuning root's first eight harmonics from its second octave, tuned pure, each on the string of the nearest key. A string that is not held waits at the pitch a key struck into silence would sound at, which under Adaptive is the last tuning its pitch class had.

A string driven at its own pitch builds up by 1 / (1 - g), where g is the fraction the loop keeps each pass. On a low string ringing for ten seconds that is nearly two hundred, so at full strength the strings would run away to 36 times the chord under the pedal. So how fast each string is fed decides how loud the resonator is, and DECAY pulls two ways. A held note drives a string continuously, so it settles at the feed divided by the string's loss, and a longer DECAY makes it louder. A short note only gets in the feed times its length, whatever DECAY is. Feeding each string in proportion to its loss keeps held notes even across DECAY, but a string with a long DECAY then takes in almost nothing during a staccato note. A fixed feed keeps staccato notes even, but a held chord at full MIX then ranges from 24 dB under the instrument at 0.2 s to 7 dB over at 10 s. No feed keeps both even, so it follows the loss to the power 0.75, which splits the difference. Measured on a chord, held and staccato, with the pedal up and down, at half MIX the level stays within 4.5 dB of the instrument from 0.2 to 10 s of DECAY. The cost is that with the pedal up a staccato note at full MIX leaves more resonance at a short DECAY than at a long one, because the damper ends both after release. A string freed only by the pedal is fed 0.08 of what a held key's string is, so pressing the pedal changes a held chord by about 1.3 dB at any DECAY. Fed a quarter, the pedal would raise it by 4 to 5 dB.

A damper does not stop a string at once. A damped string rings out over 0.8 s at A0 and 0.25 s at C8, changing evenly with pitch in between, or over DECAY if that is shorter, so a released bass key leaves the short hum that a felt damper leaves on a heavy string. Once a damped string falls below a millionth it is skipped. With the pedal up, only the held keys' strings cost anything. At eight voices, Octave and Piano add about a point of a core to the 9.5% without them, and Piano with every string free under the pedal adds about five. Whenever a string's damper comes down or goes up, its feed and its loop gain move across a block rather than change in one step. A loop gain that steps leaves a step in the line where the last pass at the old gain meets the first at the new one, and eighty-seven strings doing that at once, as the pedal goes down or up, crackle. Measured above 16 kHz, a pedal change is no louder than steady play. With the steps it is up to 53 dB louder.

MIX crossfades from the instrument alone to the resonator alone, like the echo's and the reverb's, so turning it up should not turn the sound down. Two things make that hold. Each mode is turned up on the way out by its own fixed amount, measured so that a held chord is centred on the instrument's level across the range of DECAY: 8 dB for Piano, 7.5 dB for Open, 4.5 dB for Series, and 11 dB for Octave, whose twelve tempered strings most partials miss. And the crossfade puts the instrument about 4.5 dB down half way. A straight crossfade, 6 dB down, dips in the middle, because the resonator and the instrument do not add up in phase. An equal-power one, 3 dB down, bulges by 2.5 dB, because they partly do. With both, a held chord at the default DECAY stays within about 3 dB of the instrument across the knob. Staccato notes still fall off towards 100 %, where the notes themselves are gone and only what the resonator rings back is left. The levels slide rather than step. MIX, the mode and switching the resonator off move the instrument's level and the resonator's with a time constant of 20 ms, because a level that changes at the start of a block is a click, and switched off the resonator fades out while the instrument comes back to its own level. Once that has faded, the strings are stopped, so a tail cannot come back if it is switched on again, and the stage is not there at all. Every preset that leaves it off renders bit for bit the same as without it. _Metallic Piano_ and _Space Cowboy_ are the two that use it.

The strings are fed one mono sum of the two channels, and what they ring back goes to both channels equally. The resonator is part of the instrument's body, not a space around it, so the stereo picture comes from the partials' panning and from the echo and reverb after it.

The four parameters are part of the patch and come last in the layout. On the bar the resonator has its own frame after the character's, so its three knobs read as belonging to its button and not as more of the character's controls. The button shows the mode, opens a menu of the five, and is lit whenever the mode is not Off. It is called RESONATOR rather than STRINGS, because on a synthesizer STRINGS reads as a string-ensemble sound.

### The bus the series is summed onto

The thirty-three channels are not simply added together. They are summed onto a bus with drive, and the amount and type of drive belong to the character. The character is meant to tell you what circuit you are playing through, and a summing amplifier is part of that circuit.

The bus drive is separate from the tables, and it has to be. A table is one partial's waveform and cannot know what the other thirty-two are doing, so nothing built into it can respond to the mix. The bus can. It works on the sum, at twice the sample rate through a half-band filter so nothing it adds folds back down, and it responds to level. Play quietly and it is barely there. Play hard and it comes in. A mixing desk does that and a table cannot.

**How much each character uses** was found by ear at a keyboard, one character at a time, with a development control that is not in the shipped build:

| Character | Drive on the bus |
| --------- | ---------------- |
| Bulb      | 15%              |
| Rail      | 13%              |
| Valve     | 10%              |
| Op-amp    | 10%              |
| Diode     | 7%               |
| Wheel     | 10%              |
| Pure      | none             |
| FM        | none             |
| PD        | measured bow     |
| 6581      | measured bow     |
| VCO       | 7%               |
| Combo     | none             |

Pure sums cleanly and adds nothing, which is its purpose. FM and Combo sum cleanly too, because the frequency modulation synthesizer's mix and the combo organ's measured clean. PD's and the 6581's buses are measured quadratics, described below. The VCO's is the op-amp's cubic knee with the negative half driven 2.44 times harder, the shape of the mixer it was recorded through, at a drive a little under the op-amp's, set by ear. Wheel uses the triode's curve at Valve's amount, because the organ's own amplifier uses valves. There is no knob for the rest, because how hard a summing amplifier is driven is part of choosing a character, not a separate setting.

**The op-amp bus uses a cubic corner, not a slew limit.** A slew limit is the right model for an amplifier that cannot keep up, and the wrong model for a bus. On one partial it hardens the top of the series, which is the purpose of the character. On a mix it responds to the slope of the sum rather than to any one note, so it breaks up on high notes at settings that sound fine on low ones. It measured -67, -67, -53 and -73 dB of inharmonic noise across A2, A4, A6 and A7. A cubic corner does what is wanted without the register deciding how much.

**The quadratics run out of room past what was measured.** PD's and the 6581's bows were measured on what each instrument can play, and the 6581 has three voices. A parabola carried past that does what no output stage does. Past its top the positive half turns back, so a chord played harder comes out quieter on that side, and the negative half bends further without limit. On the 6581 the top is about eight full partials in phase, which an eight-note chord played hard just grazes and anything denser goes past. So past that point the positive half holds the top it reached, where its slope is already nothing, and from the matching point on the negative half the slope falls from two to nothing over the same distance again and then holds. Inside those edges the curve is the measured one exactly, and that is where ordinary playing stays: an eight-note chord at full velocity on _6581 Sawtooth_ peaks within a hundredth of where it did before the edges were added. PD's edges are about seventy times full scale out, so no PD patch reaches them. The edges are a choice, not a measurement: a chip with three voices cannot show what it would do with eight.

**The antialiasing has a low pass of its own, and the bus undoes it.** The curve is read as its average over the step from the last sample, which is what keeps it from aliasing. On anything quiet enough to stay on the straight part of a curve, that average is just the mean of two neighbouring samples at the doubled rate, and the mean of two samples is a low pass: cos(w / 2), which at 48 kHz takes 0.4 dB off 9 kHz, 1.1 dB off 15 kHz and 2 dB off 20 kHz, on every character with a bus stage. The filter that brings the doubled rate back down has a lift folded into it, [-k, 1 + 2k, -k] at the doubled rate with k = 0.17, which together with the averaging stays within 0.2 dB from 20 Hz to 20 kHz at 44.1, 48 and 96 kHz. Folded into the down filter, it makes that filter two taps longer and still symmetric, and its window is taken one sample earlier so its middle lands where it did, so the stage's latency does not move. The lift also lifts what the curve makes above the old Nyquist before the filter removes it, and measured with a 3.1 kHz partial at 80% through the hardest knee, the loudest thing that folds back below 20 kHz goes from -90.1 dB to -89.3. What is left at the top is the half-band filter's own edge, -1.4 dB at 19 kHz and -2.5 at 20 at 48 kHz. Measure any of this as rms over whole seconds: a sine's sample peaks, at a few samples a cycle, can miss its real peak by 3 dB and show a loss that is not there.

**Two implementation details look like design faults when they go wrong.** If the antiderivative the antialiasing depends on is carried across blocks instead of recomputed from the stored input, moving the amount spikes the output. And at small drives, float cancellation in the antiderivative puts the gain 1.4 dB out, which has nothing to do with the curve. So it is computed in doubles, with a series for the log of cosh where the two terms are close enough to cancel.

**It sits before the master fader**, like the effects after it, so the fader stays a true output level. That also means the bus sees the raw sum, which on a full patch runs 1.5 to 1.9, well above one. Several things that looked like bugs come from this: the tape echo's permanent compression, and a preset mixed low on purpose to keep the echo clean sounding wrong with the bus in front of it.

### Wobble

Wobble makes the whole instrument sound like a warped record. Pitch is bent by reading the output back through a delay line whose length keeps changing. That is what happens when a platter runs off-centre or a capstan slips: the sound arrives early or late and the pitch moves with it.

Three things move it at once: a slow warp at about once per turn of a 33 rpm record, a faster wobble on top that is too fast to follow and too slow to be vibrato, and an occasional nudge, a sharp slip that bends hard and settles back. The slips are the glitch. Both their rate and their size grow with the square of the control, so the bottom of the knob sounds like a tired turntable and the top like a transport breaking down.

| Setting | Typical bend | Worst bend |
| ------- | ------------ | ---------- |
| 25%     | 5 cents      | 65 cents   |
| 100%    | 34 cents     | 322 cents  |

The gap between the typical and the worst bend is what gives it its character. A vibrato would have them close together.

It sits between the voices and the echo, so the repeats carry the wobble instead of adding a wobble of their own. That is the difference between playing a warped record and a warped recording of one. Both channels are driven from the same modulation, because it is one platter, so a centred source stays centred. This is the opposite of the echo, where the two sides are meant to differ.

The delay grows with the control instead of being switched in, so there is none at zero and turning it up cannot cause a step. Coming up from bypass, the line holds nothing yet, and reading even one sample behind the write head would return the silence it was cleared to, a step of 0.8 into a signal of 1. So the read point is allowed to sit exactly on the write head, and the glide guarantees it moves back more slowly than the line fills. Measured with the knob moved from zero to full over a second, the largest jump between neighbouring samples is 0.0164, against 0.0144 for the steepest part of the waveform itself.

At zero it is bypassed and passes the signal through untouched, bit for bit, which the tests check directly. At full it costs 0.17% of a core.

### The master effects

Two of the groups in the top bar work on the finished mix rather than on any one partial: an echo and a reverb. They come after everything per partial and before the output group, and both are before the master fader, so the fader is a true output level and moving it cannot change the wet to dry balance.

Each is three machines behind one button, not one machine behind a switch, and the button shows which one is running. Behind each button are two parameters. The on switch is older than the choice of machine, and every patch saved before the choice existed stores the switch but no type. So the switch stays as it is and the type is a separate parameter beside it. A patch from 1.9.0 therefore loads with the tape and the room it was made with, which is the only way an old patch keeps sounding the way it did.

All three machines of each effect run on every block, and each checks whether it is the chosen type. The two that are not chosen empty their loops instead of holding a tail that would reappear the moment you switched to them, the same rule the on switch follows.

**ECHO** is one of three machines. They share the same panel:

| Control | Does                                                |
| ------- | --------------------------------------------------- |
| MIX     | how much of the output is repeats                   |
| TIME    | distance between the heads, 20 ms to 2 s            |
| FDBK    | how much of each repeat goes round again, up to 95% |
| AGE     | how worn the machine is, and what that means varies |

AGE is what makes three machines worth having instead of three names for one. It is the same knob and the same parameter on all of them, and on each it controls whatever that machine gets worse at:

| Machine | What AGE does to it                                              |
| ------- | ---------------------------------------------------------------- |
| TAPE    | top end lost per pass, motor wander, and how hard the tape leans |
| BBD     | the clock slowing, and the companding breathing                  |
| DIGITAL | fewer bits and fewer of them per second                          |

**The tape** is a loop, not a digital delay. TIME is reached by winding, not by jumping, so moving the knob slides the repeats in pitch on the way to the new setting. That pitch slide is the main reason people want a tape delay.

AGE controls the three things that change together as a tape machine wears: the top end it loses on every pass, how far the motor wanders, and how hard the tape compresses when driven. New is clean, bright and steady. Old is dark, unsteady and compressed. As three knobs they would nearly always be turned together. The tests measure all three separately: after ten passes a new machine returns four times the top end of a worn one, it holds its pitch to 0.01% where a worn one wanders by over 1%, and it passes its repeats at full level where a worn one compresses them.

The compression has two stages, and they do different jobs. The first is character and follows AGE, so a new machine is untouched. The second is a safety limit that is always on, because at 95% feedback a steady tone can otherwise build up to twenty times its input. The tests drive a new machine at maximum feedback for twenty seconds and the output stays bounded.

Stereo is two tape paths side by side, not one repeat moving across the image. Each takes its own channel, feeds only itself and returns hard on the side it went out on, so nothing crosses over and a partial's repeats stay where the mixer put it. Hard panning is safe because the two play at very nearly the same moment.

The transport is what makes them differ. The two motors run at slightly different speeds, 0.70 against 0.83 Hz of wow and 6.3 against 5.31 Hz of flutter, wander by slightly different amounts, and draw from separate random streams so they never fall into step. Two takes of the same part never drift together, and neither do these. A centred source comes back as a pair that agrees on the note and differs in everything else, which is what doubling is.

The rates are fixed, not offered as controls. What matters is that they differ, not by how much, and a pair of knobs whose only wrong setting is "equal" is not worth adding.

How far the two differ follows AGE, because holding speed is what a machine in good order does. That gives the control a floor. A transport that held speed exactly would have both paths wander by the same nothing and put the repeats back in mono. No real transport does that, so AGE starts 8% up its own range and never reaches it. The knob does not show this. It reads 0 to 100 across the travel it has, because the floor is a property of the machine, not a number the player needs to know, and the offset is applied in both directions.

Measured as the correlation between the two channels on a sustained tone, the bottom of the knob gives 0.77 and the top 0.19, so the doubling is always there and always has room to change.

| AGE                       | Channel correlation |
| ------------------------- | ------------------- |
| no wander, below the knob | 1.000               |
| 0, the bottom of the knob | 0.768               |
| 13%                       | 0.019               |
| 100%                      | 0.186               |

The tests check both ends: that a perfect transport would collapse to mono, which is why the panel cannot ask for one, and that the lowest setting the panel allows is already doubled without sounding like a chorus. Feeding only one channel leaves the other at exactly zero, because nothing crosses over.

**The bucket brigade** is a line of capacitors passing a charge along, one step per clock tick. Modelling it as a darker tape would miss everything that makes people keep these pedals.

It gets darker the longer the delay, and that follows from how it works. The line has a fixed number of buckets, so a longer delay needs a slower clock, and the reconstruction filter has to come down with the clock to keep its own aliasing out. Every unit ever built does this: short settings are nearly clean, long ones are murky. So on this type TIME also controls brightness, which it does on no other.

Its stereo comes from two clocks, not two tapes. Each side is modulated by its own slow sine, 0.31 against 0.43 Hz, with no common factor so the two never fall into step. That gives a gentle vibrato where the tape has drift, which is what a pair of these really sounds like.

AGE controls three things again, and a clock whine is not one of them. A whine is the obvious artefact and the wrong one: it is a fault, not a character, it sits at a fixed pitch that a chord has to be in tune with, and nobody buys one of these for it. Instead AGE makes it darker on top of what the clock already costs. It makes it dirtier on every pass, because the line clips early and the repeat goes round again, so a tail starts clean and ends up growling. And it makes it noisier in the way an old compander is noisy. Every bucket brigade has a compander around it, compressing on the way in and expanding on the way out, and a worn one mistracks. The pull-down arrives late, so hiss swells in behind a chord and ducks away as the repeats die. A silent patch stays silent, because the noise is keyed to the signal instead of running free.

**The digital delay** is the plain one. There is no tape to wear, no buckets to clock, no motor and no lamp. What goes in comes back out later and quieter, and the only thing it loses on each pass is level. It is worth having beside the other two because it is plain.

Its stereo is ping-pong, which is a different design, not a setting. The input is summed to the middle and every repeat crosses sides, so a note becomes a line of repeats alternating left and right. On the tape and the bucket brigade nothing crosses between the sides, and the width comes from the two paths differing. Here the two sides are one path, and the crossing is the whole design.

AGE does something neither of the others can. At zero the repeat is an exact copy, which is what a digital delay is for, and the clean end has to be exact, not nearly exact. Turned up, the repeats come back through fewer bits and at a lower rate, down to six bits and an eighth of the host rate, so a tail starts as the signal and ends as a rough copy of it.

The wear is applied on the way out, not inside the feedback loop, and that matters. A quantiser inside a feedback loop has a fixed point: one step times a feedback of a half rounds back up to one step, so the tail reaches the bottom bit and stays there forever. That would be audible as a low sound that never goes away, at every feedback from a half upwards, still there twenty-five seconds later. Nothing is lost by taking it out of the loop. The step stays the same size while the repeats get quieter, so the tenth repeat is quantised against a far coarser grid than the first. That is what a converter does to a fading sound, and it gives the same progression as wear inside the loop.

**Both echoes colour the first repeat.** This is easy to get wrong. If the output is taken from the delay line before the ageing stage, repeat one comes back clean and every later repeat is worn, so the machine seems to switch on only after you have heard it work. Both take their output from the coloured signal.

**REVERB** is one of three machines, on the same kind of button as the echo. The panel is the same on all three:

| Control | Does                                                  |
| ------- | ----------------------------------------------------- |
| MIX     | how much of the output is reverb                      |
| DECAY   | how long the tail takes to fall 60 dB, 0.2 to 20 s    |
| DAMP    | how quickly the top end dies out of the tail          |
| PRE     | silence between the note and its reverb, up to 250 ms |

DECAY means the same length of time on all three, which does not happen by itself. Measured from a click at the same setting, the room takes 2.23 seconds, the plate 2.38 and the spring 2.46. The tests hold each machine against the room, not against a fixed number, so any machine added later has to match as well.

The wet levels are matched too. On a held chord with the mix full up, the three come back at 0.2545, 0.2547 and 0.2546, the same to within a hundredth of a decibel. One MIX knob serves all three, and if switching between them changed the level, one machine would sound better than another instead of just different.

**The room** is a feedback delay network: eight delay lines fed back through a Householder matrix, with four allpass stages per side in front to spread a hit into a wash before it reaches the network.

The two sides are taken from different lines with different polarities, so the tail is wide by design and there is nothing on the panel to narrow it. There is no width control, for the same reason the mixer has no stereo spread control: a knob with only one setting anyone uses is not needed. The test checks what matters, that a mono hit still comes back with the two sides largely independent.

The room size follows the decay instead of being set separately. A long tail in a small room sounds like a spring, not a place, and a short one in a hall sounds like a gate, so the two would always be turned together anyway.

The matrix is orthogonal, so the network neither gains nor loses energy by itself, and the decay comes entirely from the per-line gains. That is why DECAY can be trusted. The tests measure it: a 1 s setting falls silent in 1.0 s and a 5 s setting in 4.3 s.

A network is used instead of a bank of combs because of this instrument in particular. Thirty-two pure sines held indefinitely will find every resonance a fixed network has, and a comb reverb answers them with a metallic pitch. So the line lengths are mutually prime and each is slowly modulated at its own rate, which keeps the tail moving under a held chord. The tests check for exactly that failure by measuring the loudest bin of the tail against the average across the spectrum.

The input is cut below 175 Hz for the same reason, and the cutoff is fixed, not a control. A fundamental at full level feeding a long tail floods everything above it, and the reverb turns into rumble as soon as you play low, so nobody would want it open. On an instrument built from 32 partials the interesting part is above that anyway.

**The plate** is a sheet of steel under tension with a driver at one corner and pickups at two others. It differs from a room in having almost no geometry: a hit spreads across the whole sheet almost at once, so there are no early reflections and no build-up. Measured on a click, the first fiftieth of a second carries 1.48 times the average of the whole tail in the plate and 0.00 in the room. The plate's tail is there at once and the room's is still filling up. The design is Dattorro's, which everyone uses because it works: four allpasses scatter the input, then a tank of two branches passes it round in a figure of eight.

DECAY sets the tank's own gain instead of a room size, because a plate has no size to set. That gain is easy to get wrong in two ways. It is applied four times per circuit of the figure of eight, not once, and the circuit includes every line it passes through, allpasses included. Getting the first wrong makes a two-and-a-half second setting last half a second, and getting the second wrong makes it last three.

**It is a modulated plate, and the menu says so.** The two allpasses in the tank wander by about a millisecond, which is six times what the paper gives them and far more than a real steel sheet does. The reason is the circuit. It is three quarters of a second long, so the same fixed set of arrivals comes round again and again, and at a long decay that is heard as a repeating pattern instead of a wash. A test measures exactly that. It takes the tail's envelope, flattens it so only its shape is left, and compares it with itself at every lag from fifty milliseconds to a second and a half:

| How the output is read                           | Repeats at |
| ------------------------------------------------ | ---------- |
| four taps, all on one branch's first delay       | 0.34       |
| Dattorro's seven taps, across the whole tank     | 0.23       |
| and the modulation opened from 8 samples to 1 ms | 0.11       |

The first line is a bug and sounds like one. Four taps on a single line are four echoes of one circulating signal, so at a short decay nothing goes round often enough to notice, while at a long one it is a delay with some reverb on it. The third line is where more modulation stops helping and starts costing, because the excursion is a pitch deviation of twelve cents at this setting and twenty-four at twice it. Twelve cents is movement in the tail, which a lush plate wants. Twenty-four is vibrato, which it does not. Naming the machine for what it is seemed better than claiming the figure is Dattorro's.

**The spring** is three helical springs in a tray, with a transducer shaking one end of each and a pickup at the other. It is the cheapest reverb ever built and the least like a room, which is why it has its own sound and is not just a worse plate.

What makes it a spring is dispersion. A wave travelling down a helix does not carry all frequencies at the same speed. The top of the band arrives first and the bottom lags behind, so a hit comes back as a descending chirp. Every bounce between the ends adds another chirp on top of the last, so a spring sounds less like its input as it decays, not only quieter, which neither of the other two does. Measured on a click, the bottom of the band arrives 6.4 ms after the top, where the plate has no such lag.

It is built as a chain of first-order allpasses inside each spring's loop. An allpass passes every frequency at full level and delays each by a different amount, which is dispersion, and the loop sends the signal through the chain again on every bounce. The chain is the one expensive part of the file, so the number of sections was measured, not chosen:

| Sections | Coefficients | Chirp  | Cost of all three springs |
| -------- | ------------ | ------ | ------------------------- |
| 100      | 0.60 to 0.70 | 7.3 ms | 4.95%                     |
| 70       | 0.70 to 0.79 | 6.9 ms | 3.61%                     |
| 60       | 0.74 to 0.82 | 6.5 ms | 2.23%                     |
| 50       | 0.78 to 0.86 | 5.7 ms | 1.92%                     |

Sixty is where the curve bends: nine tenths of the chirp for less than half the work, and below sixty the chirp drops faster than the saving grows. For scale, eight voices come to around eight per cent, so the hundred-section version costs as much as five voices and this one costs two.

The band it works over is a transducer's, not a room's, and where each of its two limits sits matters more than what it is set to. The pickup is two poles at five kilohertz, outside the loop, met once on the way out. The spring's own loss is one pole inside the loop, met on every bounce, and that is what DAMP moves. With both inside the loop, a tail that has crossed the tray thirty times would have met them sixty times:

| Where the poles are                      | 6 kHz against 1 kHz |
| ---------------------------------------- | ------------------- |
| both in the loop at 3600 Hz              | -10.6 dB            |
| one in the loop at 3600, pickup outside  | -11.2 dB            |
| one in the loop at 10000, pickup outside | -6.5 dB             |

The middle line matters most, and it shows the opposite of what you might expect: moving a pole out of the loop does not brighten anything by itself. What it does is allow the loop's corner to be opened, because the pickup then holds the band instead of the loop filter. DAMP also sweeps geometrically, not in a straight line up the frequency axis, because a corner swept linearly from ten kilohertz spends three quarters of its travel above three, where the pickup already covers the band and nothing audible happens.

## Notes on CPU

The cost grows mainly with the number of voices, since eight voices means 256 sine oscillators. These figures are measured on one core of an x86 container at 48 kHz with every modulator running:

| Voices | Oscillators | Load      |
| ------ | ----------- | --------- |
| 1      | 32          | about 1%  |
| 8      | 256         | about 7%  |
| 16     | 512         | 14 to 15% |

Everything is running at once in those figures: both LFOs, drift, velocity, aftertouch, the meters and the noise channel. The master effects come on top of that. They cost the same however many voices play, because they work on the sum rather than on each voice. Most of them cost far less than the voices:

| Machine         | On one core |
| --------------- | ----------- |
| room            | about 0.6%  |
| modulated plate | about 0.7%  |
| tray of springs | about 2.4%  |

The spring is the exception among the reverbs. Each of its three springs gets its dispersion from a chain of allpasses run at the host rate. That costs about as much as two voices, while the other two reverbs cost a fraction of one. A chain of a hundred sections would cost 5%, so the number of sections is set by measuring it against what it adds.

The resonator costs more than any of them when all its strings are free. At eight voices, Octave and Piano with the pedal up add about a point of a core, and Open, or Piano with the pedal down, adds about five, which is about half of what the eight voices cost. A damped string that has gone quiet is skipped, so with the pedal up only the held keys' strings cost anything. The noise channel's string is one more delay line per voice.

That leaves enough headroom for the engine to stay a plain bank of oscillators with no tricks in the signal path. These things keep it cheap:

- A 4096-point interpolated sine table, measured error -129 dB, instead of `std::sin` per sample.
- LFOs, envelope coefficients and gain ramps update once per 32-sample control block. Only the oscillator and envelope run at sample rate.
- Silent partials, whether muted, faded out above Nyquist or sitting at zero, skip the oscillator entirely and only advance their envelope.

The attack knob is logarithmic rather than skewed towards a midpoint, so equal turns give equal ratios. The step from 1 ms to 2 ms gets the same travel as the step from 100 ms to 200 ms. This matters because the range spans four and a half decades. A power curve through a midpoint across that range needs an exponent of about 7.4, which leaves the bottom eighth of the knob so flat that nudging it does nothing. Measured, the dead travel is 0.1% against the power curve's 13.7%, and 30 ms stays within half a percent of the middle of the knob, where the power curve put it. In exchange, sub-millisecond attacks take 9% of the travel rather than 27%, and half of that 27% was unusable. A test checks the dead travel, so a wide range added later cannot bring it back unnoticed.

Every other control with a skewed range is handled the same way, and the project does not use `setSkewForCentre`. Which curve a control gets depends only on whether its range can reach zero:

|                                                                                                | Curve                      | Worst dead travel, power curve | This curve |
| ---------------------------------------------------------------------------------------------- | -------------------------- | ------------------------------ | ---------- |
| attack, decay, release, pitch mod rate, echo time, reverb decay, string decay, resonator decay | logarithmic                | 15.5%                          | 0.3%       |
| swell, delay, reverb pre-delay, pitch mod depth, drift                                         | exponential from the floor | 29.4%                          | 15.7%      |

The second group cannot do as well. A control that has to reach zero has no ratio to grow by, so the bottom of its range is always compressed. To give five seconds of swell any resolution at the top, the first millisecond has to move slowly. The exponential curve fixes the real fault of a power curve, which is a slope of exactly zero at the low end, where turning the knob does not change the value at all. With the exponential curve the slope is small but never zero, and the test checks this across all 651 continuous parameters. The smallest is the attack, whose bottom moves at 1/23766 of the rate of its top, where a power curve's bottom moves at exactly zero.

### Where a partial starts

With phase reset on, a partial starting from silence begins its cycle at a rising zero crossing. This is the softest possible start, because from there the partial cannot reach its peak until a quarter of its period has passed.

For most of the keyboard, that quarter cycle is longer than the shortest attack:

| Note       | Quarter cycle | What sets the onset   |
| ---------- | ------------- | --------------------- |
| A1, 55 Hz  | 4.55 ms       | the note's own period |
| A2, 110 Hz | 2.27 ms       | the note's own period |
| C4, 262 Hz | 0.96 ms       | the note's own period |
| A4, 440 Hz | 0.57 ms       | the note's own period |
| A5, 880 Hz | 0.28 ms       | the attack knob       |

So below about 500 Hz, turning the attack below a millisecond changes nothing, and the note still starts softly. Measured at A1 with the shortest attack, the sound is a third of the way up after a millisecond. That is sin(20 degrees), and the envelope finished half a millisecond before.

**So ATTACK goes below its shortest time and sets where the cycle starts.** The bottom of the knob is a phase region. It runs from a rising zero crossing at 0.2 ms down to the partial's own peak at the bottom of the knob, with no attack time at all. The same note started on its peak is at full level after that millisecond. The phase region continues the onset past the point where the envelope stops mattering, so it sits on the row that already sets how hard a note begins, not on a row of its own. The value is in seconds above zero and a fraction of the region below zero, with -1 as the peak, so every stored time still means the same time.

The knob is linear in one scale: octaves of attack above the shortest time, with the phase region two octaves deep below it. A quarter of the cycle gets the same travel as quartering the attack. STRIKE moves the attack in octaves, so it moves along this whole scale too. A partial set to its peak starts there when struck hard. Struck softly, it moves up through the zero crossing into a short attack, about 13 ms at full amount. Within the times, one octave of the scale is a doubling of time, so that part behaves exactly like a time knob. Positions on the knob are not where they would be on a knob of times alone, because the phase region sits below the times. Automation drawn as positions for a times-only knob therefore lands on different attacks. A stored value does not change.

The setting is per channel, because every partial starting at its peak at once makes a spike. The first sample is 2.7 times the steady peak for a 1/n spectrum of eight, 4.1 times for 32 partials and 32 times for a flat 32. A single switch for all of them would only make spikes. Per channel, the sharp start goes only where you want it, and LINK can stagger it across the series in one drag. Measured at A1 with phase reset on, a millisecond after the start: a third of the level from the zero crossing, nine tenths from 45 degrees, and all of it from the peak.

With phase reset off, a partial continues from wherever its cycle was. This stops a note played over a ringing tail from making a step.

Partials fade out as they approach Nyquist. Without that, the 32nd harmonic of a high note would fold back down as aliasing. For a C7 it lands near 67 kHz. Measured alias images are at -122 dB. Turning the converter down, described below, switches this guard off on purpose.

### The converter

There are two settings under Settings. Both default to whatever the host is running at, and both take something away rather than add an effect. They are the only part of the instrument where turning something down makes it do less work.

Both are shown on the panel, not in a menu, as two seven-segment readouts under the output meter. That is where a converter sits in the signal chain, and roughly what one looks like. They have to be visible because they are saved with a preset, so loading a preset can change them. A setting that changes without showing it is worse than no setting. Clicking either readout opens its list.

When not set, they show what the host is running at, so the readout always shows what is happening and never goes blank. A rate above the host's own rate cannot be delivered, so it reads as off.

**Sample rate** offers rates from 32 kHz down to 4 kHz. It is not a filter applied to a finished signal. The whole voice pool renders at the lower rate, and the result is held between frames. This works because sampling a sinusoid at 8 kHz gives the same sequence of numbers whatever rate the engine is nominally running at. The samples that survive the hold are therefore the only ones worth computing. Rendering at the host rate and then decimating would sound the same and cost the full amount.

Measured on one core at 48 kHz, eight voices, the same patch throughout:

| Render rate  | Load | Against full rate |
| ------------ | ---- | ----------------- |
| host, 48 kHz | 7.8% |                   |
| 22.05 kHz    | 3.8% | 48%               |
| 11.025 kHz   | 2.0% | 25%               |
| 8 kHz        | 1.5% | 19%               |

The aliasing is wanted, so the Nyquist guard is switched off with it. A converter running at 8 kHz does not mute everything above 4 kHz. It folds it back down, and this does the same. The 32nd partial of A3 is at 7040 Hz and comes back at 960. The original frequency stays faintly audible above it, because holding a sample puts an image either side of the rate, about seven times quieter at that spacing.

Modulation also gets coarser. The LFOs, drift and gain ramps still update once per 32-sample control block, which at 8 kHz is 4 ms rather than 0.7. Timing is still correct in real time, because every coefficient is worked out from the rate being rendered at, so a one-second attack still lasts a second.

**Bit depth** offers depths from 16 bits down to 2, quantising to 2^(n-1) steps either side of zero. It sits with the rate, before the resonator, the echo, the reverb and the master fader, so the resonator's strings ring on the lo-fi signal. Those then behave like outboard effects on a lo-fi source, and are not crushed themselves. It does not clip. Clipping is the job of the safety clipper further down, and a bit-depth setting that also distorted would be doing something the panel does not show.

Both settings are saved with a preset, because once they are in use they are part of the sound, not part of the setup.

### The output stage

The output stage is the last thing that happens to the signal. It comes after the master fader, so the master fader sets how hard it is driven. Its first job is to stop 32 faders pushed up from handing the host something louder than it asked for. It offers eight machines, which makes it part of the sound and not only a guard, and that is why the master fader is saved with a preset.

Every one of them stays within full scale. That is the purpose of the stage, and it is tested, because a safety clipper that could be driven past unity would not be a safety clipper. Each is tested at drives up to 400 times, both on its own and through the whole instrument at three sample rates.

**Soft** is straight up to seven tenths and then bends into a tanh knee. It is gradual and symmetric, and adds odd harmonics as you drive it harder. It is the default. A test checks it bit for bit against the formula of the single soft clipper that came before the five, written out longhand rather than taken from the code, so a patch made for that clipper sounds the same.

**Hard** cuts the signal flat at unity. Nothing happens below that, like a converter with no headroom left. It is abrupt and bright, with the same odd harmonics as Soft but all at once.

**Asymmetric** starts to bend one half of the wave before the other, at a half against nine tenths, the way a single-ended stage runs out of supply in one direction first. Soft and Hard treat both halves alike and give odd harmonics. Asymmetric gives even ones. Measured against Soft at the same drive, its second harmonic is a hundred times larger. It is called Asymmetric rather than Bias because bias is the mechanism, and this file already uses that word for the offset that several of the characters have. The name on the bar describes the result.

**Limiter** does not change the shape of the wave. It turns the level down as the signal approaches the ceiling and lets it back up afterwards, so you hear the level move, not the shape change. It is the only one of the eight a mastering engineer would recognise on its own.

**Fold** folds the wave back on itself past the threshold instead of flattening it. It is an effect that happens to stay within full scale, more than a protection. It makes far more harmonics than the others, and they move with the level, so its tone changes as you drive it where the others only get rougher. It is computed in closed form rather than in a loop, so a sample arriving at a hundred costs the same as one at a half.

**Console** is a console's mix bus, measured from a real chain at the settings it is used at: a passive transformer, a bus compressor and a tape stage, into the limiter with its ceiling at -0.1 dBFS. It changes over time as well as by level, because a compressor does. The chain was recorded one point at a time with a test signal from a DAW, at the transformer's output, the compressor's and the tape stage's, so each unit was fitted from what really went into it to what came out, at the level it sees in the chain. The gain staging between them is most of how hard each one works, and it is the chain's. The whole is held at unity for a quiet 1 kHz tone, so choosing it does not change how loud a patch is, and the chain's polarity, which the transformer inverts, is not kept.

The transformer saturates in the lows and nowhere else, because a core saturates on flux, the signal integrated over time. Its third harmonic is -35 dB at 30 Hz and -1 dBFS, -58 at 60 Hz and below -79 from 250 Hz up. It is modelled as the input through one pole at 15 Hz running into a knee, with what the core cannot carry lost from the output, which matches the measured harmonics within 2 to 4 dB and, unlike the polynomial fitted first, cannot turn back past the loudest level measured. Its passive high shelf, +4.25 dB above 8.1 kHz, is a first-order filter after it.

The compressor is 4:1, linked, with its sidechain high-passed at 120 Hz and an automatic release. It was fitted by running the transformer's pass through a model and matching the gain the compressor's pass shows in 5 ms windows, over level steps and bursts of 20 ms to 5 s, within 0.12 dB rms. The reduction is worked out from the level at once and then smoothed: up at a 20 ms attack, down at a 26 ms release, and a slow stage that charges over 1.3 s while the reduction is held and lets go over 3.5 s. So a short hit lets go in tens of milliseconds and a long one leaves a release of seconds, as the recording showed. Smoothing the level instead of the reduction could not catch a 20 ms hit at all, which the recording shows it does.

The tape stage has no time in it: its third harmonic is there at full strength within 20 ms of a loud tone and holds for five seconds. Once driven it holds near -34 dB however much harder it is pushed, which no smooth curve does. A share of the signal running into a hard limit while the rest passes does: just over the limit the clipped part grows fast, and well over it the clipped part is a steady fraction. So 7% of the signal, through a low shelf of +6.4 dB below 634 Hz since the lows reach the limit first, runs into a limit at -11 dBFS. The limit's corner is read as its average over each step, by its antiderivative, as the bus drive's curves are, so it does not alias and adds no latency. A gentle low shelf, mostly the tape stage's bump, and the coupling capacitors' high-pass at 15 Hz come after, and with them the model's response follows the chain's within about 0.4 dB.

Put together and run over the whole test signal, the model follows the chain's level changes within about half a decibel at every frequency and level, and its third harmonic mostly within 1 to 6 dB at the loud levels. It runs only while it is chosen, and the other machines come out exactly as they did before it existed. Chosen part way through, it starts from rest and the output fades to it from what was running over 30 ms, which covers its filters settling. Only the compressor's slow release stage, seconds long, has not settled by then, and it only acts on something loud.

**Cassette** is a four-track cassette recorder at 9.5 cm/s with Dolby B on a normal tape, measured the way it is used, into the limiter with its ceiling at -0.1 dBFS. A test signal was recorded onto two of its tracks from a DAW at a normal level and played back, once with Dolby and once without, with a loopback of the interface to take its own response out. Each pass was lined up by a chirp at both ends, searched over three seconds, since a tape starts late and runs at its own speed: these ran 0.06 to 0.09% fast, wandering along the take. Reading a tone at exactly its own frequency failed above 8 kHz for the same reason, so every tone is read as the energy within about a percent of it. The interface takes 5.47 dB off on the way round, which the hiss is corrected for.

The record side saturates the lows first: at -3 dBFS the third harmonic is -18 dB at 30 Hz, -29 at 120 Hz and about -45 at 1 kHz, and a 315 Hz tone is clean to -8 dBFS. It is a low and a high shelf into a soft limit with the inverse shelves after it, within about 2 dB of the measured harmonics. Only what the limit takes away is averaged over each step by its antiderivative, since averaging the whole signal is a low pass of its own, 2 dB down at 10 kHz at 48 kHz, and the antiderivative is tabulated for that part alone, since tabulating the whole curve left an error the size of a -40 dBFS signal. The tape's own response is a head bump of +1.95 dB at 36 Hz and a roll-off to -10 dB at 18 kHz, within 0.16 dB. At 44.1 and 48 kHz the very top falls faster than the tape's, -12.6 dB at 16 kHz against -8, since the filters are bilinear and close to Nyquist there. At 96 kHz they follow it.

**Dolby B is two shelves that do not quite cancel.** The encoder lifts the quiet highs by up to 10 dB onto the tape and the decoder lowers them again, taking the hiss with them, each with a depth that gives way as the level in its high band rises. On this machine the decoder hears its input 4.85 dB quieter than the encoder sent it and their corners differ, so quiet highs come back 2 to 5 dB duller while loud ones barely change, which is how it sounds and is kept. Under a loud 315 Hz tone the machine still took 9 to 10 dB of hiss off above 4 kHz, which a side chain reading the shelf's own band cannot do. A sliding corner, which is how Dolby B is usually described, fitted worse here. What fits is a side chain that reads its level through a second high pass near 10 kHz. Each side chain averages the rectified signal rather than following its peaks, because the hiss's own peaks are four times its average and a peak follower let a third of the reduction go. Fitted to the highs at every level, the hiss removed in silence and under three tones, within 1.1 dB rms.

**The hiss follows the notes.** It is the tape's measured hiss without Dolby, white at -99.1 dB per hertz per channel above 400 Hz and rising below, independent on the two tracks, decoded by Dolby with everything else. It is at its full level once the output reaches -40 dBFS, less below that, and gone over 300 ms once nothing plays, so a patch at rest is silent. Under a signal the tape adds noise of its own, and it follows the signal's highs: a 3150 Hz tone at -18 dBFS and a 315 Hz tone at -4 raised it by exactly the same, and those two are the same through a first order high pass at 1.6 kHz. So it is white noise at -79.2 dB per hertz against that band's level, letting go over 18 ms, which is how fast it fell away after a burst. With it the noise under every tone matches within about 1 dB without Dolby, and within 1.5 to 5 dB with it, highest under loud tones where the tone hides it.

**The transport is shared by both tracks.** The speed, read from a 3150 Hz tone as its instantaneous frequency, wanders by about 0.1% rms: steady flutter at 68 Hz and 40.8 Hz, noise in three bands from 0.2 to 20 Hz, and scrape flutter from 100 to 300 Hz. It moves a delay centred on a millisecond, which swung by up to 0.35 ms on the tape. That delay is part of the machine as an analogue filter's phase is, and is not reported as latency. The level wobbles by 0.25 dB rms at about 0.37 Hz as well. Crosstalk between the tracks measured -73 dB and the 50 and 100 Hz hum -85 to -92 dBFS, and both are left out.

The test signal was run through the C++ and through the same analysis as the recordings. The level follows the tape within about 1 dB from 30 Hz to 12 kHz at every level, the third harmonic within 2 to 3 dB at the loud levels, the flutter at 0.09% against 0.11% with the same rates, and the hiss coming back after a burst within about 1.5 dB at every step. Like Console it runs only while it is chosen, costing about 0.9% of one core at 48 kHz, and chosen part way through it starts from rest with the output fading to it over 30 ms, which covers the delay starting empty and the filters settling. Dolby's depths and the wobble are worked out every eight samples, since each is a power and none moves in less than a millisecond.

**Print** is the console's chain printed to the cassette recorder, in the order they were patched: Console, a limiter at -0.1 dBFS, then Cassette, into the limiter at the end. The outboard chain was measured at the level of the DAW and the deck at a normal recording level, which are the same reference, so the two models go together as they are with nothing in between but the limiter the outboard chain ends in. That limiter has no lookahead: its gain falls at once to what the sample needs and recovers over 100 ms, as the output stage's own does with the lookahead off. From moderate levels it has the console's steady third harmonic, -39 dB at 1 kHz and -9 dBFS where the deck alone is still at -60, and at the top the compressor turns the loudest parts down before the deck, so a full-scale 1 kHz tone saturates the tape a little less than Cassette alone, -36 dB against -31, while 60 Hz still saturates it first, at -17. Print has its own console and deck, so switching between it and Console or Cassette fades with both running, and it costs about what the two do together, only while it is chosen.

**The limiter looks ahead, and that is why the plugin reports latency.** Without lookahead, the gain only starts to move once the peak has arrived, so the hard clip behind the limiter does the real work. On the presets that drive it hardest, two seconds of audio reach 1.294 and have 1741 samples cut off. In that case the limiter is in effect a clipper.

The lookahead is two milliseconds, and the whole gain path fits inside it. A causal box filter of B samples delays its output by (B-1)/2, so a detector window of W followed by two boxes costs (W-1)+(B-1). With W = B = half the delay, that is two samples less than the budget. One box would turn the gain step into a straight ramp with a corner at each end. The second box rounds off those corners, so the gain arrives in time without being heard. There is no attack coefficient. How fast the gain can fall is set by how far ahead it can see.

| Lookahead | Peak before the backstop | Samples clipped | Gain movement |
| --------- | ------------------------ | --------------- | ------------- |
| none      | 1.294                    | 1741            | 0.0041        |
| 1 ms      | 0.980                    | 0               | 0.0040        |
| 2 ms      | 0.981                    | 0               | 0.0020        |
| 5 ms      | 0.983                    | 0               | 0.0007        |

Every size removes the clipping, so the choice is between how gently the gain moves and how much latency it costs. Five milliseconds would give a curve two and a half times gentler still, a difference too small to hear, for three more milliseconds. At two, an abrupt onset at twice full scale holds at 0.98, and the third harmonic the limiter adds falls from 0.00259 to 0.00012.

**Every shape pays that latency, and so does the stage when it is off**, for the same reason the bus drive gives. A stage that reported a different figure for each patch would make the host re-plan its graph every time a preset was chosen. With the lookahead on, the two stages together come to 108 samples at 48 kHz, a little over two milliseconds. With it off, the figure is the bus stage's 12.

**The lookahead is off by default, and Settings switches it on.** That setting is the only thing allowed to change the figure, because a preset or the bar cannot reach a session setting. It is off by default because only the Limiter, Console, Cassette and Print use the window, and no factory preset uses them, so with it on everyone else would pay two milliseconds for nothing. With it off, the detector sees a single sample rather than a window. The gain it applies is then exact and immediate, and the ceiling still holds without the clip behind it having anything to do. What is lost is the smoothing, and a gain that steps is itself a distortion. The third harmonic goes from 0.00012 to 0.00185, about fifteen times higher. That makes a rougher limiter, but not the one with no lookahead at all, which overshoots to 1.294 and leaves the clip to catch it.

A change in latency is reported from the message thread. The processor's existing 20 Hz timer polls for it, instead of a parameter listener pushing it. A listener runs on whichever thread moved the parameter, and a host automating this one would move it from the audio thread, which is the wrong thread for telling a host its graph has changed. Hosts handle a latency change during playback differently. Most re-plan, and some only notice when playback stops or on reload, so it is a setting to choose once and leave, not one to automate. The hard clip stays behind the limiter, with nothing to do, because the stage has to stay within full scale even at a rate low enough that the window is only a couple of samples.

### Learning a controller

Right-click a control, choose MIDI Learn, and move something on the controller. The map holds one parameter per controller number. A knob can be moved by more than one controller, but a controller only ever moves one knob. The other way round would need a list per controller, and a rule for when two knobs disagree about the controller's position.

**The hard part is where the lookup happens.** With MPE on, the parser gets every message, and only the mod wheel, a program change and all-sound-off pass through to the ordinary handler. A binding checked after the parser would never fire on the kind of controller this instrument is usually played from, so bindings are checked before it. That is only safe because eleven numbers are refused rather than bound: 1 the mod wheel, 64 the sustain pedal, 74 the slide axis, 120 and 123 for stopping notes, and 6, 38, 98, 99, 100 and 101, which a keyboard uses to declare its zones. Binding any of those would stop the instrument from learning what it is plugged into. The slide is the easiest to bind by accident, because it moves whenever a finger does.

A refused controller is not learned and still does its normal job. So if you arm a knob and then touch the slide to steady a note, the note is steadied and the knob is still waiting.

**The table is atomic pointers, with no locks**, because lookups happen on the audio thread as messages arrive, and edits happen on the message thread as menu items are chosen. The pointers are safe to keep because an AudioProcessor builds its parameters once in its constructor, and they outlive everything in the map.

**Bindings are saved with the session, never with a patch.** Which controller moves which knob describes the setup, not the sound. A binding stores the parameter's id rather than its position, so a later build that adds controls cannot point someone's controller at something else. An id that no longer exists is dropped rather than refused.

Two more behaviours are deliberate. A control jumps to the controller's position instead of picking the value up as the controller passes it. Pickup would need a way to show that the control is waiting, and a knob this size has no room for that. The message that binds a controller does not also move the control. A controller is wherever it happens to be when you touch it, and a knob jumping there would surprise you just as you are watching it.

**A control knows which parameter it moves.** This is set where its attachment is made, which is the one place that already knows. It is stored on the component the same way as the look and feel's other flags. A right-click then searches outwards from whatever was hit. A click on a knob's caption lands on a label, and the label's parent is the component with the id.

**Whatever handles the right-click has to register for it.** JUCE sends a click to the control under the pointer, and to no parent unless that parent registered for its children's clicks. So the strips, the noise channel, the top bar and both bars each listen to everything inside them. Without that, a right-click reaches only the background, where nothing is tagged, and the menu never opens.

**The ring that shows a control waiting has its own colour**, `colours::learning`. The three colours it could have borrowed already mean things that can be true of the same control at the same time. The accent marks what LINK highlights and what the draw tool lights, red marks a cut channel and amber a soloed one. The window updates the ring on its timer instead of being told to, because what ends the wait is a controller arriving on the audio thread, and the audio thread is the wrong place to repaint from.

Marking a control means marking the part of it that the look and feel draws. A knob on the bar is a slider inside a frame that carries the caption. The tag is on the frame so that the caption is clickable, so the mark is passed inwards to the part whose paint method reads it. The master fader needs its own line for a similar reason. It is drawn as a cap over the meter rather than as a track, through a branch that returns before the shared marker. It draws the ring round its cap, not round its bounds, because a ring round the bounds would enclose the meter and look like it marked the meter.

### Macros

Macros come from issue #24, and it asked for the right thing. A LINK drag across the tuning knobs moves 32 parameters, and a host records only the last one touched. So a relationship you can edit by hand in the instrument cannot be automated at all. A macro gives that relationship one control a host can draw automation for.

**A macro offsets values and does not write them.** This is the key decision. A macro adds its amount to the patch's values on the way to the engine, so the host has one lane instead of thirty-two parameter changes per move, and the patch itself stays as it is. Writing the values would bring back the problem the issue reports.

As a result, the knobs do not move when a macro does. That is normal for anything that works this way, but surprising the first time. So the controls show both values at once. The tick ring lights up to the macro's value in the macro's colour, and the pointer on the cap stays at the patch's value in the channel's colour. The gap between them is the macro. At rest the two match, so a macro that is assigned but at rest looks the same as no macro. A fader shows it in the gutters on either side of its track. These are the closest thing a fader has to a knob's ring, since they are the only part of it that is not the groove, the meter or the cap. A fader driven by a macro shows two narrow columns of segments there. They light from the foot of the track up to the level the macro sets, the same way a knob's ring lights from its anchor. The glass of the cap gets a tint of the same colour. On a silenced channel the columns go dark with the rest of the strip's lights.

**The columns use the meter's own segments, made narrower.** They have the same count, pitch, gaps and rounding. Unlit, they show the macro's colour turned down low, the same way the meter's unlit segments show the channel's colour. The fader and the meter share one rectangle, which is what makes it possible to line them up. `meterSegments` is in `Theme.h` rather than on the meter, so the two cannot disagree about how many segments there are.

Sharing the rectangle and the arithmetic is not enough on its own. **A look and feel is given the track, not the slider's rectangle.** JUCE has already inset the track by the thumb's radius, so it is a few pixels shorter at each end. Laying the segments out across the track gives the right number, but each is a little short, and together they cover a little less of the strip than the meter does. The mismatch is only noticeable next to the meter. The gutters measure `slider.getLocalBounds()` instead, which is the rectangle the meter measures.

**A fader with no macro on it shows none of this.** It has no scale ticks either. Ticks would be a third scale beside a meter and a cap that already show where the level is.

A bar across the groove or an edge round the cap would both look wrong, for the same reason. The groove is the meter, so a line through the middle of it would cut the meter in half and belong to neither. An edge would sit right against the cap's own white edge, two hard rings a pixel apart, and look like a sticker on the cap rather than the cap being driven. The glass is the only part of a fader that can take a colour without becoming a line.

The window works out the result and the snapshot works out the offset, using one shared piece of arithmetic. A test checks that the ring matches the value the engine plays on all 32 channels. A knob showing a value that is not being played would be worse than a knob showing nothing, because it would look right and be wrong.

**The macro panel is built from the same parts as the mixer it covers.** It is one of two dialogs, with the notice that asks about updates, and a dialog is where a window most easily stops looking consistent. A column of plain buttons and labels over a panel of lit caps and segment displays would look like a different program.

So its captions are set like every caption over the mixer, upper case at nine point bold in the dim grey. Its amount is a channel fader laid on its side. Under it is its own row of segments, not a meter's. They light from the middle outwards, because the amount is signed and shows a distance. Its reading is a seven-bar display like every other number in the window. That is why a positive amount shows no sign. Seven bars cannot draw a plus that looks like more than a speck next to the minus. For the same reason, a partial tuned sharp of equal temperament has no sign either.

**Every part of the reading stays in place.** It has four cells and a point. The sign shares the first cell, and a leading zero shows as a blank cell. A centred reading would move left and right as the amount passes ten and a hundred, and shift by half a cell when it goes negative. That would give away a drawn display, because the cells of a real one do not move. A test renders the same number signed and unsigned and checks that everything outside the first cell is identical pixel for pixel.

The reading has no unit. Seven bars cannot make a per cent sign, so it would have to be drawn as text beside the digits, the one mark in the box that is not a segment. The column is headed PERCENT instead, the same way the channel readouts leave their units to the gutter caption beside them.

The button that removes a macro is square, like the colour swatch at the other end of the row. Both show a mark rather than a word. The row is thirty pixels tall, so anything narrower looks like a squeezed button.

**A macro can reach the channels picked for LINK.** Its last scope, Selected, reaches the channels that were picked by their numbers when it was chosen. The macro keeps its own copy, so it is not LINK's selection and picking other channels later moves nothing. Choosing Selected again takes the new ones. The list is not a parameter: eight lists of 32 channels would be 256 switches in every host's automation menu, and nobody automates a list of channels. So it is kept beside the parameters in `MacroChannels`, read by the audio thread from there, written into the session as one property per macro and into a preset file as a `MACRO_CHANNELS` entry beside the parameters, in harmonic numbers as the LINK selection is written. A factory preset clears it, since no factory preset uses Selected. Selected goes on the end of the scope list, so a lane drawn before it existed still names the scope it did. Undo carries the lists along with the parameters, so undoing a preset load puts each scope back with its channels, and taking new channels with the scope already on Selected is a step of its own.

**A scope lights in the colour of what it affects.** A scope naming an interval takes that interval's colour, which every channel of it is drawn in across the mixer. So the word and the channels it will move are the same colour. All, Odd, Even and Selected are not one interval, so they take the accent colour, which the interface uses for anything that is not one partial. The taper's anchor takes the colour of the channel it names, so you can match the number to the strip without counting along the mixer.

This is done through `buttonOnColourId`, not through the colour the button lights its word in. JUCE gives a look and feel `buttonOnColourId` for a toggled button, and `drawButtonFace` lets a named background colour override the lamp. The colour swatches use the same route. Setting only the lamp leaves every scope in the scheme's own blue, which looks deliberate but is not.

**The eight macros are a fixed pool.** A plugin declares its parameters once in its constructor, and neither VST3 nor AU can add one later. So "make a macro" takes one of the eight that already exist, and "remove it" returns it. A macro whose row is set to None is one that has not been made, so no second parameter is needed to say whether it exists. Raising the number above eight later changes nothing, because parameters added at the end leave every existing automation lane pointing where it did. Lowering it would leave automation pointing at nothing.

**The offset moves along a control's travel, not its units.** The reason is easy to miss. Several rows are built by `logRange`, which keeps its curve in conversion functions and leaves `skew` at 1. A range rebuilt from its two endpoints therefore comes back linear, with no warning. With a linear offset, a quarter turn of a macro adds five seconds to a decay. It is linear in seconds across a range of four orders of magnitude, so the whole negative half of the fader sits at the bottom and the positive half jumps. Checking `skew` does not tell you whether a row is linear.

**The amount is signed and covers the whole range.** It is signed so a macro on LEVEL can lower a group as well as raise it. It covers the whole range because one macro reaches up to 32 controls, which may be set anywhere, so no smaller fraction would suit all of them. Scaling it down would give more resolution to a patch set mid-range and take reach from every other one. The cost is that a patch already at one end of its range has a stretch of fader that does nothing in that direction. This is expected and not a fault.

**Two of LINK's modes do not carry over directly.** Spread scatters each strip in the direction a drag gave it relative to the strip under the mouse. A macro has neither a drag nor a strip under the mouse, so Spread is not offered. Taper does carry over, but it needs to know where to centre, because LINK measures from the strip under the mouse. A macro names an anchor channel instead. A test checks its share against what `ui::linkCurveWeight` gives, so the two cannot come to mean different things under the same name.

A macro's colour is a parameter rather than state, because a preset file carries parameters and, beside them, only the channels a macro on Selected reaches. A patch that makes four macros has to be able to store their colours. The window works out which macro owns a control, because that depends on all eight and no strip can see past its own column. It then passes the result to the strips, the same way as the LINK glow. When several macros reach a control, the lowest-numbered one owns it. Two colours mixed would usually look like a third macro, and a mark meaning "more than one" would not say which.

### One tool rather than two switches

LINK and DRAW are not independent, because a drag cannot be a link and a drawing at once. Two separate switches would have to show LINK as off while drawing is armed, without it really being off. One button choosing between Pointer, Link and Draw removes that state altogether. The settings that belong to Link sit under the three tools in the same menu, so the tool and its behaviour are one click apart.

**The tool is worked out from two flags, not stored.** The flags are LINK's own switch and whether drawing is latched, so there is no separate stored value that could disagree with them. Drawing takes priority, including when the modifier key arms it instead of a menu choice. The cost is that saving the tool means saving both flags, which is easy to forget. Every path that changes either flag goes through one function that writes both. If a path wrote only LINK's switch, a session could keep LINK after another tool was chosen, and every window would open on it.

The button shows the pointer the tool gives, not a word. It shows one arrow at one size, with a mark to its right, centred on it. Drawing the real cursors would not work, because a cursor's artwork hangs down and to the right of its hotspot, so each tool would sit at its own size in its own place. The mark for drawing is a contour rather than a pencil, because two pencils both look like a tick at twenty pixels, and because a contour better shows a tool that sweeps a shape across the series.

**The four buttons show icons because the words do not fit.** SETTINGS and MACROS need 62 and 54 px, and each button is 33. A cog, a rack of faders, a keyboard and a tuning fork say the same thing in the room there is. The words are still what a screen reader is given and what the tooltip says.

### The update check

It is off until you say yes. The first window asks once, in a notice drawn inside the window, not a window of its own, because a host that scans the plugin folder at startup opens editors with nobody there to answer, and pluginval opens several in a row. For the same reason the question is marked as asked when it is answered, not when it is shown. An editor opened by a scan shows it to nobody, and that should not use up the one time it is asked. Both buttons count as an answer.

With the check on, the plugin reads `latest.json` from the project page when a window opens, in the background, and sends nothing but the request. A newer version is announced once, in the same kind of notice, marked when it is answered. After that SETTINGS stays lit, its tooltip says which version is out, and the first entry in its menu opens the release, until the update is installed. The answer is kept in `Overtonium.settings` next to the presets, not in the session, because it belongs to the machine, not to a song.
