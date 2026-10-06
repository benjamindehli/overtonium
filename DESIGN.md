# Design notes

Why the instrument is the way it is: what each part does, what the alternatives cost, and the measurements behind the numbers.

For what it is and how to get it, see the [readme](README.md). For how the code is arranged, see [ARCHITECTURE.md](ARCHITECTURE.md). For how to build and test it, see [CONTRIBUTING.md](CONTRIBUTING.md). The [project page](https://benjamindehli.github.io/overtonium/) covers the same ground for somebody playing the instrument rather than working on it, and this is the longer version that keeps the reasoning.

## Presets

Thirty-one ship with it, listed alphabetically. Most were dialled in by hand on the panel and converted straight from the saved file, so what ships is what was played rather than something written afterwards to approximate it. Where a row of the mixer has a shape the preset says so, as the saws do with `1.0 / n`, and where it was drawn by hand the thirty-two values are written out as a list so the curve is at least visible.

_Init_ is one of them rather than a reset to the parameter defaults. It clears the patch down to a short, bright three-partial pluck, which is a better place to start building from than silence. Like every other preset it leaves the session alone, so your tuning, polyphony, bend range and master fader survive loading it.

What counts as the session is drawn along the interface rather than decided case by case: everything the Settings menu offers, plus the master fader. The menu is where the instrument is set up and the panel is where the sound is made, so a control's place says which it is, and adding a setting means adding it to `kSessionParamIds` in the same commit. Both kinds of preset are held to that list and to each other. A preset you save carries the same set of controls a factory one does, which a test checks by comparing what `capture` writes against what `neutralBase` decides, in both directions.

Twenty-six of them use STRETCH or TRACK, since most of them are modelling something with a body. Picking odd partials was always a stand-in for inharmonicity, and six of these have the real thing on top of it:

| Preset           | Stretch  | Tracking   | Why                                                                                                               |
| ---------------- | -------- | ---------- | ----------------------------------------------------------------------------------------------------------------- |
| 2-bit Fuzz Organ | +7 ct    | 1.6 dB/oct |                                                                                                                   |
| Big Saw          | +2 ct    | 1.3 dB/oct | barely any of either, just enough to stop the partials locking dead in phase                                      |
| DigiLog          | +30 ct   | 3.0 dB/oct | the series steps in from the bottom over five seconds, so the stretch is heard as arrival rather than as detuning |
| Dire Dire EP     | +8 ct    | 6.0 dB/oct | steep tracking is what keeps it soft under the fingers rather than glassy                                         |
| Drawbar Organ    |          | 3.2 dB/oct |                                                                                                                   |
| Dream Phase      |          | 2.6 dB/oct |                                                                                                                   |
| EP Chimes        | -1200 ct | 1.4 dB/oct | a whole octave of collapse on the 32nd partial, which is an artefact rather than an instrument                    |
| Equal Saw        |          | 1.0 dB/oct |                                                                                                                   |
| FM Piano         |          | 2.1 dB/oct |                                                                                                                   |
| Glass Armonica   | +180 ct  | 4.0 dB/oct | barely any, but enough that the upper partials beat against the fundamental instead of locking to it              |
| Glockenspiel     | -294 ct  | 3.9 dB/oct | a bar rings nowhere near whole numbers, and here STRETCH pulls the series in rather than spreading it             |
| Just Saw         |          | 1.0 dB/oct |                                                                                                                   |
| Lo-fi            |          | 4.0 dB/oct |                                                                                                                   |
| Metallic Piano   | +24 ct   | 9.0 dB/oct | a piano string is stiff, and a thin one rings further from whole numbers                                          |
| Music Box        | -23 ct   | 1.5 dB/oct | a comb tooth is a bar and does not ring in whole numbers                                                          |
| Nylon EP         | +7 ct    | 3.2 dB/oct | almost nothing above the low partials already, so the tracking does the rest of the rounding                      |
| Omni-84          |          | 7.0 dB/oct |                                                                                                                   |
| Slow Pad         |          | 2.5 dB/oct |                                                                                                                   |
| Space Flute      | +154 ct  | 2.0 dB/oct | far enough out that the upper partials stop belonging to the note, which is what makes it breathy                 |
| Sparkle Pad      | +81 ct   | 0.7 dB/oct | the sparkle is the top of the series, so the tracking is kept out of its way                                      |
| Stepped          | +0.5 ct  | 2.0 dB/oct | a hair of it, so the partials that are not jumping still beat against each other                                  |
| StyloPoly        | +0.3 ct  | 0.4 dB/oct | barely any of either: a Stylophone is a reed rather than a body, and thinning it would take the buzz away         |
| Synth Ensemble   | +10 ct   | 3.0 dB/oct | just enough that no two partials lock, which is the difference between an ensemble and one player                 |
| Tape Choir       |          | 3.5 dB/oct | voices thin out at the top of a range rather than brightening                                                     |
| Vibraphone       |          | 3.0 dB/oct | a short bar carries less above its fundamental than a long one                                                    |
| Wurli            |          | 3.1 dB/oct |                                                                                                                   |

_Cathedral_ is a principal chorus arriving slowly, in a nine second room. _Glass Armonica_ is rubbed glass, with a key-off level above the sustain so lifting the finger lets the rim ring on. _Music Box_ is a comb of teeth, each ringing for a different length and sitting somewhere different in the field. _Tape Choir_ is a worn echo and drift, with the partials fanned across the field.

Three of them are after a particular instrument. _Wurli_ is a Wurlitzer 200A, _Metallic Piano_ a thin, metallic upright, and _Omni-84_ the SonicStrings Voice 2 from a Suzuki Omnichord OM-84 System Two.

Eleven of them have the converter on at all, and four are made of it. _Big Saw_ and _Metallic Piano_ quantise to 8 bits at the host's own rate. _Lo-fi_ runs the whole voice pool at 8 kHz and 8 bits, and _2-bit Fuzz Organ_ at 8 kHz and 2 bits, where the aliasing and the quantisation noise are the distortion rather than an effect laid over the top. On that one the aftertouch blends the root note towards a fifth.

_Drawbar Organ_ and _Cathedral_ are deliberately left alone by STRETCH. A drawbar organ is electric and a pipe organ is voiced rank by rank, so neither loses its top as you play up, and pretending otherwise would be modelling the wrong instrument. So are the saws, where the raw spectrum is the point.

**Saving your own.** The preset menu writes to `Dehli Musikk/Overtonium/Presets` under your user application data folder, one small `.ovtpreset` file each, and lists whatever it finds there under the factory list. The menu reads the folder every time it opens, so a preset saved a moment ago is in it and one deleted in Finder is not.

A preset holds parameter values and nothing else. Not the window size, the zoom or the LINK settings, since loading a sound should not move your window or change your tools.

Factory presets start from a neutral base rather than from wherever you happened to be, so one always gives the instrument it describes. That covers the globals that are part of the sound: STRETCH, TRACK, the converter and phase reset all go back to neutral unless the preset asks for otherwise.

What it will not touch is listed once, as `kSessionParamIds`, and holds for every preset including Init: polyphony, bend range, the aftertouch source, what the slide reaches, one voice per key, phase reset, the temperament, its root, the reference pitch and MPE. How you play the instrument and what it is tuned to are not part of a patch.

How loud it is used to be on that list, and the master fader and the safety clipper sat there together on those grounds. They came off it when the clipper became five shapes rather than one with a switch. The fader is the drive into that stage, so a patch that wants to be heard leaning on the Asymmetric curve, or arriving under the Limiter's ceiling, has to be able to say how hard it pushes, and a preset that could choose the shape but not the level could not describe either. Loading a preset therefore moves the output level, which is a real cost and is written down here rather than discovered. Both halves are tested from that same list, so the code and the test cannot come to disagree about what the rule is: every preset is loaded twice, once clean and once after deliberately making a mess of everything, and required to come out identical, and then all thirty-two presets are loaded in turn against a session set to Werckmeister on F at 415 Hz, which has to survive.

Values are stored plain rather than normalised, so a preset survives a parameter's range being widened later, and anything a file does not mention keeps its default rather than being reset, so a preset saved by an older build loads into a newer one without silently zeroing whatever was added in between. The tests cover both of those directly.

**Choosing one over MIDI.** A program change loads a factory preset by its position in the alphabetical list, counting from zero. A number past the last preset is ignored rather than wrapped back to the start, and it arrives on any channel, MPE included, where the parser has no use for it and would otherwise swallow it. Presets of your own have no number to be called by, since their folder is yours to add to and rename at any time and nothing in it stays put long enough to be worth pointing a clip at.

Loading a preset is hundreds of parameter moves, each of which has to be reported to the host, so the audio thread only writes the number down and a 20 Hz timer on the processor does the work. Posting a message from the audio thread instead would take the message queue's lock, which is the same objection that keeps the scratch buffer from growing there. Up to fifty milliseconds late cannot be heard, and a preset change was never a sample-accurate event: it is hundreds of parameters moving, not a note.

It loads whether or not the preset is the one already showing, which is what the plugin's own menu does and what an instrument with a panel does. A clip that opens with a program change therefore sounds the same on every pass, at the cost of replacing anything you changed by hand since it last fired.

**Undo.** The parameter tree carries an undo history, which matters most because of LINK: one drag can move the same knob on all 32 channels, and without a history the only way back from a drag you did not mean is to reload the preset.

**The history holds what a person did, and nothing else.** The value tree is deliberately not given the undo manager, because a tree that holds one records every write to it and most writes are not somebody editing. A host playing an automation lane writes continuously for as long as the piece lasts, and a history filling up with a fader that was automated three minutes ago is a history of nothing: the undo you wanted is a hundred steps back and you will never find it.

What tells the two apart is a gesture. Every control opens one before it writes and closes one after, which is how a host is told that a move has begun and ended, and automation does not: it sets values and says nothing. So the processor listens to itself for gesture begin and end, takes a baseline of every parameter when the first one opens, and when the last one closes puts whatever actually moved into the history as one step. A drag, a scroll wheel and a LINK drag across 32 channels are each one gesture and therefore each one step, however many values they moved and however long they took. A gesture that ends where it began is not a step at all.

Only from the message thread. A host is allowed to open a gesture from the audio thread, and taking a baseline of 836 parameters there would allocate on it, so one that arrives from anywhere else is left alone. Nobody is turning a knob from the audio thread.

The things a person does that are not one gesture go through `recordEdit`, which takes the same baseline around whatever it is given: loading a preset writes hundreds of parameters and has to come back in one undo. The caller decides, and that is the point of it. Loading a preset from the menu is recorded and a clip firing a program change at the same `applyFactoryPreset` is not, because one of them is editing and the other is playing.

An earlier arrangement let the tree record everything and tried to cut it into steps afterwards, by watching for the tree going still. It worked, once the stillness was measured from the right thing, but it could only ever have been a way of grouping writes rather than of telling them apart, and it recorded automation along with everything else.
Cmd-Z and Cmd-Shift-Z work where the host lets them through, which many do not, since a DAW usually keeps those for its own history. Undo and Redo are therefore also at the top of the Settings menu, which always works. Loading a session clears the history, since undoing your way back into someone else's edits is not useful.

**Making a factory one.** Configure with `-DOVERTONIUM_PRESET_AUTHORING=ON`, which adds "Copy as factory preset code" to the preset menu. It is off by default, since what it produces is of no use to anyone who is not about to rebuild the plugin. Dial in a patch and pick it, and the C++ for that patch goes on the clipboard as a `case` that starts from `neutralBase()` and then sets only what differs from the default. Paste it into `Presets.cpp`, add the name to `kNames`, and tidy it by hand where the shape has a formula rather than 32 separate numbers. Factory presets stay as code rather than as embedded data precisely so they can say `1.0 / n` instead of listing values.

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

The test suite asserts this table, so the derivation cannot silently drift from it.

**Six of the thirty-two knobs have nothing to move**, and the table is why: 1, 2, 4, 8, 16 and 32 read zero cents, because an octave is 1200 cents in equal temperament and in just intonation alike. Their TUNE knobs are not idle, they are agreeing with themselves, and the strip's tooltip says so on those channels rather than leaving a knob that appears broken.

It cannot be given a second job, which is worth writing down because it is the obvious idea. Nought on that knob means the tempered position, which for an octave is the exact ratio, so redefining that end would make equal temperament mean something that is not equal temperament, and _Equal Saw_ sits at nought on all six. One is the default that twenty-seven of the thirty-two presets and every untouched patch sit at. The other five hold something else there, seventy-seven knobs between them, so either end would move sound that is already written. Nor is the knob greyed out: a LINK drag down the TUNE row would then move twenty-six channels and refuse six, which is how a patch like Equal Saw gets dialled in.

What does move an octave partial is STRETCH, on its own curve, along with DRIFT, the pitch modulator, and the character's rack, which sits every partial but the first a few cents off spec. The gap this leaves is a static per-channel detune, which no control here offers: the blend reaches the exact ratio and stops.

### Tuning the keyboard

Everything above is about where a partial sits over the note you played. This is about where that note sits, which until now was always twelve-tone equal temperament, hard-wired as `440 * 2^((n - 69) / 12)`.

For an instrument whose whole subject is tuning, that was a hole: you could put the partials in just intonation but not the keyboard they were played from. Settings now has a Temperament entry with six:

|                        | Major third | Fifth  |                                                             |
| ---------------------- | ----------- | ------ | ----------------------------------------------------------- |
| Equal                  | 400.00      | 700.00 | the reference everything else is measured against           |
| Just (major)           | 386.31      | 701.96 | pure by construction, chosen interval by interval           |
| Pythagorean            | 407.82      | 701.96 | every fifth pure, the third pays for it                     |
| Quarter-comma meantone | 386.31      | 696.58 | fifths narrowed a quarter comma, which makes the third pure |
| Werckmeister III       | 390.22      | 696.09 | four fifths narrowed, no wolf, every key playable           |
| Young                  | 392.18      | 698.04 | six narrowed by a sixth, gentler again                      |

They are derived from the circle of fifths rather than copied out as tables of cents. Almost every historical temperament is described by how much each of the twelve fifths is narrowed, so that is what the code says, and the pitch classes fall out of it. The two commas it is all built from come out at 23.460 and 21.506 cents, which are the published figures, and the tests assert the property that defines each temperament rather than the numbers that happen to result: meantone's third pure to a thousandth of a cent, Pythagorean's fifth likewise, Werckmeister at its characteristic 390.2.

**Root** picks which pitch class the temperament is built on, since an unequal temperament is only in tune in the keys near its centre, and that is the point of it. It is greyed out for equal temperament, which has no centre to have.

**Reference pitch** offers 415 through 466 Hz. A stays exactly there in every temperament, because the offsets are taken relative to A's own: a tuner tunes A first and works outwards. Only the other eleven notes move.

Equal temperament at 440 is bit for bit what it was before, which the tests check across all 128 notes.

None of the three travels with a preset. A temperament is a property of the music you are playing rather than of any one sound in it: you set it once and work, and having a patch drag you back to equal in the middle of that would be no help. Init included, since Init is still a preset and clears the patch rather than the session.

### The shape a modulator traces

Both per-partial modulators pick a shape as well as a rate and a depth. Pitch offers eight, amplitude seven, and each of the thirty-three channels carries its own, which is the point: gating the fifth partial while the fourth breathes is not something one global LFO could do.

Two of the eight cost almost nothing. The smooth random contour already existed as DRIFT, drawn as a Catmull-Rom spline through random points, and sample and hold is the same points read without the interpolation. They now share one implementation, so the two cannot come to disagree about what random-but-smooth sounds like.

The square and stepped shapes are affordable because of where modulation already happens. Every modulator is stepped once per 32-sample control block, and the gain a partial is given slides linearly across that block rather than jumping to it. An edge therefore arrives as a slew of about two thirds of a millisecond: short enough to read as an edge, long enough not to tick. Measured, the worst sample-to-sample step across every shape at full depth is 2.2 times the slope of the tone itself, against a threshold of 3 that the click tests already used.

Amplitude gets seven rather than eight because a tremolo only ever comes down from the fader. There is nowhere for a unipolar shape to go that a bipolar one does not already reach, so that side has one square and it gates the whole depth. Everything on that side is also read a quarter turn ahead, which is what keeps Sine the cosine the tremolo has always been: a note begins at full level and dips. Without that, every preset carrying a tremolo would have started somewhere new.

Random is a spline through its points and overshoots them by a few percent, so the amplitude clamps. Otherwise the one shape that is meant to duck a partial could briefly lift it above its own fader.

The pitch depth reaches an octave, which is for the shapes that step rather than sweep. A square on the pitch is a trill and sample and hold is a run of random notes, and both are worth having at real intervals rather than at a vibrato's worth of one: at full depth the bipolar square jumps an octave either side of the note, the unipolar one an octave up, and sample and hold lands anywhere in between, measured at -1167 to +1098 cents over a few seconds. The range belongs to the knob rather than to the shape beside it, so a sine can sweep further than anyone needs. That is the lesser evil. A knob whose end moved with the shape would either read out a number it was not doing or grow a dead stretch at the top, and the taper keeps the bottom where it was in any case: both settings reach 25 cents at half travel, and what used to be the whole range now ends at about three quarters.

LINK does not reach the shapes. It drags a value across the series along a weighted curve, and half a sawtooth is not a shape. The shape menu offers to set every channel at once instead, which is the same intent by the only means that makes sense for a list.

### One modulator for the keyboard, or one per note

A modulator belongs to a note by default: strike a chord a note at a time and each note's tremolo starts where that note started, so the three of them breathe out of step for as long as they sound. That is what a digital instrument does, and it is what nearly every patch here wants, since thirty-two partials breathing out of step is most of what makes the mixer sound like a mixer.

It is not what an instrument with one tremolo circuit in it does. A Wurlitzer's wobble is in the amplifier, after the reeds, so a chord breathes as one thing however it was played, and a Stylophone has one oscillator and therefore one vibrato with nothing to be out of step with. Two switches turn each modulator into that: the phase stops belonging to the note and belongs to the channel, and a note arriving late joins whatever is already running.

**Two switches rather than one**, because the two modulators are two circuits. Everything else about them is already separate, down to their shape lists, and a patch can reasonably want a tremolo the whole keyboard shares over a vibrato each note keeps to itself.

**A channel's modulator rather than the instrument's.** Each of the thirty-three keeps its own rate and its own shape, so what is shared is one circuit per channel and not one for the lot. Partial 7's tremolo is one thing that every note played through partial 7 hears, which is the same idea as the rack of oscillators a character brings, and it is what makes a preset like _Shimmer_, whose whole design is thirty-two rates that never line up, still sound like itself with the switch on.

**Worked out before any voice runs**, which is what the implementation turns on. A voice renders a whole buffer at a time, so a shared phase stepped inside one voice's loop would be stepped again by the next voice and the two would hear different things. Instead the engine walks the same control blocks the voices are about to walk and writes each channel's value at every block boundary into a table, one boundary more than there are blocks so a block has both the value it starts on and the one it ends on. The voices read it. That is also what makes it exact for the two random shapes, which draw a fresh point each time their phase wraps and could not be kept in step by handing a note a phase to start from.

The table is a fixed member, sized for 2048 frames at a time, so the audio thread never allocates and a host asking for its usual buffer gets the whole thing in one pass. Anything larger is taken in passes of that size.

**On the panel it is in the shape button's own menu**, ticked, under the entry that sets every channel at once. That entry is likewise a switch over all thirty-three reached from one channel, so the pair read alike, and the alternative was two buttons on the bar. Settings was the wrong place, since everything behind it is session rather than patch and two of the presets need this one.

The bar was also out of room at the time. It is not any more: the master fader moved onto the output meter and gave back the forty-eight pixels its knob was using, so the bar comes onto one row at 1252 px against a window that opens at 1350, and the two switches would now fit in the 98 px of slack. The menu is still the right place for them, but on the grounds that a per-channel switch belongs on the channel rather than on the grounds that there is nowhere else to put it.

**And the group's heading lights when it is on**, because a mode you cannot see is a mode you forget you are in, and this one travels in a preset: without it a patch could arrive with a shared tremolo and nothing on the panel would say so. PITCH MOD or AMP MOD in the gutter goes from the plain caption white to the accent every other switched-on thing here comes up in, measured on the rendered gutter at 200, 206, 215 against 96, 175, 203. One mark per modulator rather than one on each of the thirty-three shape buttons, since all thirty-three answer to the one switch, and it is the heading of the group the switch is about. The editor reads the two parameters back on its housekeeping tick and once more before the first paint, so an editor opened on a patch that shares a modulator says so straight away rather than a quarter of a second later.

**It costs about half a percent of a core.** Thirty-three modulators are stepped per control block whatever the polyphony is, and each voice still steps its own alongside reading the shared one, so that handing a channel its modulator back lands on where that note would have been rather than on where it was when the switch was thrown. Measured at eight voices, taking the minimum of several runs on a contended machine: 8.48% of a core with a modulator per note against 8.87% with both shared.

Two of the factory presets ask for it. _Wurli_ shares its tremolo and _StyloPoly_ its vibrato, which are the two cases the switches were built for.

### Stretch

TUNE decides how the series is spelled. STRETCH decides whether it is a series at all.

Nothing real rings at integer multiples of anything. A string with any bending stiffness has partials at `n * f0 * sqrt(1 + B * n^2)`, sharp of the harmonic and increasingly so up the series, and it is that, not the hammer or the soundboard, that makes a piano sound like a piano rather than like a sawtooth. It is also what a tuner is matching when they stretch the octaves: tune the top of the instrument to the theory and it beats against its own overtones.

STRETCH is B, dialled by what it does to the top partial rather than by its own value, which lives between 0.00003 and 0.008 and means nothing to anyone. At +150 cents, which is a real piano:

| Partial                 | 2    | 8     | 16    | 32     |
| ----------------------- | ---- | ----- | ----- | ------ |
| Cents sharp of harmonic | +0.6 | +10.2 | +40.0 | +150.0 |

The bottom of the series barely moves and the top of it walks away, which is the shape that matters. Push further and the partials stop agreeing on a fundamental, and the sound stops being a note with a timbre and becomes a bell. Negative pulls them inward instead, which no physical string does and which is worth having anyway.

It is a separate axis from TUNE rather than more of it, and the two combine: equal temperament with heavy stretch is a different object from just intonation with the same stretch. Zero is the plain harmonic series, so no preset made before it existed sounds any different, and the table above is what zero means.

### Tracking

Without TRACK, a patch has the same spectrum at every pitch. Nothing real does. Play up a piano and the top of the series thins out, not because the note changed but because the body has a rolloff that stays where it is and the partials climb up through it.

TRACK is that rolloff, in dB per octave above a fixed corner of 1 kHz, which is around C6. It is measured against the fundamental rather than absolutely, and that is the part that matters: taken absolutely it would be a shelf, making high notes quieter rather than duller, which is not the thing worth having. Normalised, the fundamental keeps its level at every pitch and only the spectrum above it thins.

The consequence falls out of where the corner sits. A bass note has most of its series below 1 kHz and keeps nearly all of it. A treble note whose fundamental is already above the corner loses the full slope across every partial it has. At 6 dB per octave, the 32nd partial keeps 57% of its level at A1 and 4% at A5.

Zero is off, and off is exact.

Knobs come in three sizes and only three: 36 px in the top bar, 32 px for the headline tuning knob at the head of each strip, and 26 px for everything below it. Nothing else varies, in either the channel strips or the noise strip. That is worth stating because a knob takes the size of whatever row it lands in, so a row height typed two pixels off is a knob two pixels off and nothing complains. A test walks the built editor and asserts the count.

Strips are colour-coded by interval class, which keeps the structure of the series visible while you scroll. The twelve classes sit in chromatic order along a narrow band running from blue at the octave, through magenta and red in the middle, to yellow at the major seventh. Octaves are therefore blue, fifths rose, major thirds magenta, sevenths amber and yellow.

The band is deliberately narrow, a crop from the middle of a full blue to yellow sweep, so 32 channels read as one family rather than as a rainbow. Saturation and value fall towards the warm end because yellow reads far brighter than blue at the same nominal value, which keeps the sevenths from visually swamping the octaves. Nothing in the band enters green or cyan, which is where the accent used by the global controls lives, so chrome never reads as one of the channels.

Every channel stands on the same grey. Alternating two shades to tell one strip from the next would put a stripe behind every knob's interval colour, behind the lit meters, the lamps and the readouts, competing with all of it. The strips are told apart by their own lit and shadowed edges instead, a one pixel groove at every boundary, which is how a console does it.

**There is one grey in the mixer and nothing stands on anything else**, the noise channel included.

Two things used to take a shade up from it: the octaves, so the shape of the series would read when you are scrolled out at harmonic 28, and the noise channel, because it is not a partial. Both had to go for the same reason. The hover highlight is a wash across a strip and of much the same weight, so a mixer carrying either of those had two kinds of lightened channel and no way to tell which was which without counting.

Nothing was lost by taking them out, because in both cases something louder was already saying it. The colour bar at the head of every strip says which interval it is, octaves included, in a language nothing else on the panel uses. And the noise channel is the only strip with no interval colour anywhere on it and the only one headed with a name rather than a number, which is a larger difference than three per cent of brightness ever was.

**Every switch on the panel is a lamp behind a square plastic cap**, the way the buttons on an old mixer or tape machine are. ECHO, REVERB, the character button, the tool, the two on the bar that open something, and the M and S on all thirty-three channels. The cap sits in a moulded well, and the well is a bevel rather than a hole with straight sides: four facets sloping in to the opening, mitred at the corners.

**The moulding stands on the panel rather than being cut into it**, so it is lit like every other raised thing here: its faces slope down and outwards from the opening, the top tilts towards the light and the bottom away, and top and left come up lit while bottom and right fall into shadow. Its outer edge says the same thing, lit along the top and the left and shading to a shadow along the bottom and the right.

The reference's own bezels are shaded the other way about, bottom and right lit, which is a block whose faces slope down and inwards instead. Both are real mouldings and the photograph is not wrong. It is simply the other one, and on a panel where everything stands proud, a bezel that alone reads as a hole is the thing that looks out of place.

It is also lighter than the photographs, which are of machines whose panels are near black. The same trap as the well's own colour: the values do not carry across to a panel that is (20, 24, 29).

It was a dark stroke outside and a light one inside, which says there is an edge without saying which way the edge faces, and the cap read as sitting on the moulding rather than down inside it. The corners are where a bevel is read, so the facets are mitred: four rectangles butted together overlap at the corners, and the overlap is a tone that belongs to neither, which is a seam in the wrong place and the one thing that gives away four faces being drawn rather than moulded.

The outer corners are square but for the fewest pixels that stop them looking cut with scissors. A radius big enough to read as a chamfer rounds the whole block off, and the chamfer belongs on the facets.

**The cap does not move when the lamp comes on.** These are indicator lamps rather than latching switches, and a cap that sinks into its well while its thing is on reads as a button whose press turns that thing off, which is the wrong way round for ECHO and for a mute. So a cap stands proud whatever its lamp is doing, with the shadow it throws onto the floor of the well and the lip of light along its top edge, and the only thing that puts it down is a finger on it, for as long as the finger is there.

One shading serves both states, since the geometry is the same either way: lit from above like any raised surface, brightest just below the top edge and falling to the bottom, with the light held further down the face when there is a lamp behind it. The light also falls off towards the ends, because shading a cap only from top to bottom left a wide one an even bar of colour, which is what a painted face looks like rather than a lit one. That falloff is a horizontal wash rather than a radial gradient, these running from a 14 px square mute to a 62 px CHARACTER, where a circular hotspot would be a disc.

A lit cap puts a trace of its colour on the moulding around it, and that is all it does: an even wash over the whole bezel rather than a halo hugging the cap. The halo is the better physics and the worse picture, because light fading out from a cap's edge is light escaping around the cap, which happens only if the cap has sunk below the moulding. A lit button wearing one reads as pressed in however faint the glow is, so the shape is the problem rather than the strength. There is nowhere to put a real bloom in any case: the button ends at the bezel's outside edge, some three pixels out, which is too little to fall off in and is exactly the ring that reads as a gap.

The bezel is pitched against this panel rather than copied from the photographs. Those are of machines whose panels are near black, so theirs can be too, and copying the value put a pit in a panel that is (20, 24, 29).

**The caps are a white translucent and the lamp shines through them.** Unlit, every cap on the panel is therefore the same grey, (111, 117, 124), that being what white plastic looks like in an unlit room. The only colour anywhere is what is switched on, so a mixer at rest has none of it and reading the row is reading where the colour is.

A lit cap is bright, because the lamp is behind the plastic and the whole face carries it, and milky, because it has come through a diffuser. Saturation goes up with the light rather than down, which is the counter-intuitive part: these are chrome colours chosen to be read as small text on a dark panel, so they are already pale, and raising the value without the saturation walks them towards white before the diffuser gets to.

**The legend is the same near-black whatever the lamp is doing.** It is printed on the plastic rather than being part of what lights, so a word never changes colour under its own lamp and the state is read off the cap alone. It sits at 4.1 to 1 against the unlit plastic and between 4.9 and 11.4 to 1 against the lit.

What that costs is brightness as a cue. Against the unlit grey a lit gold is 2.8 times the luminance and a lit cyan 2.5, but a lit red is only 1.2, red being a dark colour at any value. The mute therefore announces itself by hue rather than by getting brighter, which works at the size it is drawn because the thing it changes from is a neutral. How white the unlit plastic is trades these against each other directly, and it is one constant in `lampFace`: lighter reads more as white plastic and leaves the reds less room, darker does the reverse.

The mutes and solos are the loudest thing the cap does, 66 of them across the mixer, and that is the point: what they answer has to be findable without reading anything. Red is what a cut channel means, gold is what a soloed one means, and at that size the colour is all there is to go on.

**They are a two-gang block, one moulding around the pair**, the way a console's are. At the size they are drawn, two bezels facing each other across a gap spend most of the pair's width on moulding, and the wall is already at its two pixel floor, so those two facing bezels are the only pixels there are to recover. They stay two components, each lighting on its own, and both draw the well as the whole block: what each one shows of it is its own half, because a component clips to itself. Drawing half a well each would mean mitring the joint and suppressing two edge strokes along it, for the same picture.

**A channel nobody can hear loses the light out of its controls, not a wash over its face.** Muting a channel, or soloing another, greys that strip's rings, readouts and waveform displays the way the echo and reverb knobs grey when their machine is off and the converter readouts grey when they are following the host: every value stays where it was set and the light comes out of it. The mute and the solo keep theirs, which is the point, since the mute is usually what silenced the channel and dimming it hides the answer along with the question. A wash over the whole strip cannot make that distinction, because it takes the pair down with everything else however carefully the rest is handled.

A macro's colour goes out with the rest of it. A macro is a light like any other, and a ring glowing on a channel nobody can hear says the opposite of the truth whoever lit it. The ring still stands where the macro has taken the value, which is the same bargain the knobs of a switched-off effect make: the setting is kept and the light is not.

The meters and the activity lamps need no telling. A silenced channel gives them nothing to show, so they go dark by themselves. The colour bar at the head of the strip stays lit, because what it says is which interval the channel is rather than whether it is sounding.

**The tool is lit and stays lit.** A lamp going out says a thing is off, and there is no off here: the pointer is as much a choice as Link and Draw, and a dark cap read as a tool that had been disabled rather than as the plain pointer being the one selected. Which tool it holds is said by the icon on it.

One thing outside the mixer borrows the band, and only one: the character button on the bar lights in it, running down from the yellow at the top to the red the fifth stands in, which is the colour of channel 3. Nothing else does, because a colour from the band means a partial and reading it as anything else would be a second language on the same panel. A character is close enough to be worth saying in it: it is what the partials are.

**The standalone wears the same panel up to the top of its window.** Its title bar is JUCE's rather than the platform's, so left alone it is the grey-green every unstyled JUCE application wears, with a red cross and a yellow dash for its buttons, which reads as somebody else's window with this instrument inside it. The editor hands that window its own look and feel instead, so the bar is the same lit and grained panel as everything below it and the two marks are drawn the way every other small mark here is: dim until the pointer is on them, and then lit, with the close going red because it is the one worth being able to hit by accident.

Only ever its own window. In a host the top level window belongs to the host, and a plugin that restyled it would be redecorating someone else's application, so the whole path is behind a check on which wrapper this is running as.

Two details that took measuring. The name is centred on the window rather than in the space JUCE offers for it, which is what is left between the title bar's own buttons: they are all at one end, and the standalone adds an Options button at the other without telling the title bar about it, so a name centred in what is left sits left of the middle. And that Options button is put back where it belongs after every layout, since the standalone places it at a fixed six pixels from the top of a bar whose height it then subtracts eight from, which leaves it sitting off centre whatever the bar is. There is no hook for either, so the window is dressed and then corrected.

**The standalone remembers its size**, because the editor's own width and height travel in the plugin's state and the standalone saves that state between runs. A fresh one opens wide enough for all 32 channels. One that has been resized opens where it was left, which is the point, and on macOS the file holding that is `~/Library/Application Support/Overtonium.settings`.

Colour is then left to do one job, and does it at full strength. Every knob on a strip carries the channel's own colour in its value arc and its pointer, not only the tuning knob at the head. On one flat grey the colour is the only thing separating a channel from its neighbours, so desaturating nineteen knobs out of twenty to give the head of the strip a hierarchy would spend the one thing that is working.

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

**Clearing them.** Right-clicking an M or an S offers to clear either switch across the whole mixer, and says how many there are to clear before you do. Thirty-three strips is a great many places for a solo to be left on, and finding it by eye means reading thirty-three pairs of buttons four pixels apart. The entries stay in the menu when there is nothing to clear, greyed out, because the count is also the answer to the question that made you open it.

The buttons carry that menu rather than the strip, since a right-click anywhere else in the mixer opens the LINK menu and these swallow the click on its way past. Right-clicking a switch acts on switches. Only the channels actually switched on are written to, so the host sees a gesture on those alone and an undo step covers what moved rather than all thirty-three.

Velocity being per partial is what lets a soft note be a different timbre rather than just a quieter one. Set the fundamental to 0% and the upper partials to 100% and the tone opens up as you play harder, which is roughly what a struck string or bar does. _Struck Bell_ and _Odd Harmonics_ ship with that curve already dialled in.

Both controls run either side of zero. A positive amount means harder or heavier is louder. A negative amount inverts that, so the partial is at its loudest when you play softly or lift off the key. The two halves are exact mirrors, so -50% at a given velocity matches +50% at the opposite velocity.

The reason to want the negative half is crossfading. Give one set of partials a positive velocity amount and another set a negative one, and the two timbres trade places across the velocity range instead of one simply fading in. The test suite plays that case and measures the spectral balance swinging by a factor of a hundred between a soft and a hard note.

Letting go of a key runs its own little envelope rather than simply fading out. The KEY OFF rows say where the level goes when the key is released and how long it takes to get there, and only then does the release run from that level down to silence.

Above the sustain that is a release click or a bloom, the sound a damper landing or a hammer returning makes, and it can be louder than the note was while you were holding it. _Drawbar Organ_ uses it that way: the upper drawbars jump to 55% for two milliseconds as the contacts break. Below the sustain it is the fast initial drop into a long tail that a piano or a struck bell actually has, which is the half of it that probably gets more use.

A key-off level of zero skips the stage entirely and releases from wherever the level sat, which is the default and is exactly what the envelope did before it had one. So nothing you have already made sounds different, and the knob reads honestly: zero means no key-off stage.

Two details that fall out of it. The swell time is exact rather than the "within 1%" the other stages use, because the release has to start when the knob says it does rather than whenever an exponential happens to arrive. And letting go of a key during the delay now still makes a key-off sound if you have asked for one, which is what a release click does on a real instrument, while a level of zero cancels the partial as before.

### How fast the key came up

The speed of a release scales the level the tail starts from, on the keyboards that can sense one. Sixty-four is neutral, the hardest lift doubles that level and the softest halves it, geometrically, so two steps down and two steps up undo each other. There is no knob for it and no parameter: it is a property of the gesture rather than of the patch, the way velocity is, and nothing about a patch changes because of it.

It scales the level the tail would have started from rather than the KEY OFF level itself. Zero on that knob means "release from wherever you are", so on a patch that sets one this moves that, and on a patch that does not it moves the sustain, which is every patch. Scaling the knob instead would have done nothing at all on the default and on most of the factory presets, since zero times anything is zero.

The envelope runs to one, so a partial already sounding at full has nowhere for a hard lift to go and simply stops there. The room is where the tail is quiet, which is where a bloom is worth having: a music box, a bell, an electric piano. A tail louder than the note it came from would have to come out of the fader, with the whole series behind it.

**Nothing happens without a keyboard that senses it**, and that has to be true for the two different things "no release velocity" looks like on the wire. Plenty of keyboards send a note-off carrying zero, and plenty send a note-on of velocity zero instead of a note-off at all, which arrives as a release of zero as well. So zero is read as no information rather than as the softest possible lift, which would otherwise halve the tail of every note those players ever release. Nothing is lost by it, since a lift of 1 is the same gesture as a lift of 0. The test suite renders both forms and compares them with the neutral case sample for sample.

This is what LIFT was aimed at before 1.7.0 removed it, and the reasons it went are still good ones: it cost a row on every strip, it was a parameter nobody could automate usefully, and no factory preset used it. What is left is the part that needed neither, a keyboard that can sense a release being answered when it does.

A third case is the one worth watching for, since it is the shape a music box or a thumb piano has: no sustain at all, so the partial decays to silence while the key is still down, and then a key-off level that brings it back. Reaching zero is not the same as being finished. A partial in that state holds at silence and waits for the key rather than freeing itself, and costs nothing while it waits, since there is no point running an oscillator to produce zeroes.

The envelope's delay stage holds a partial silent before its attack begins. Staggering it across the series makes the spectrum unfold rather than arrive all at once, which is how _Slow Pad_ and _Shimmer_ now open up. It is latched in samples at note-on, so moving the knob cannot retime a note already waiting, and releasing a key before the delay elapses cancels that partial rather than letting it burst in afterwards.

STRIKE sits at the head of the section because it is about the blow rather than about the shape, and it aims that blow at the two rows under it, the way the velocity row aims it at the fader. Zero ignores the gesture entirely. Positive means a hard note starts sooner and arrives faster, and a soft one waits and then opens slowly. Negative inverts it.

The two halves lean opposite ways on purpose, and each knob stays the limit of its own row. Velocity only ever lengthens the attack, so ATTACK goes on meaning the fastest the partial gets. Velocity only ever pulls the delay in, so DELAY goes on meaning the latest it ever arrives. Turning STRIKE up cannot outrun either figure, which means the rows below it still read honestly with the amount at full.

Both halves span eight octaves of time at full amount, a range of 256 to 1, and it has to be that wide because the attacks worth stretching are short. A partial set to 2 ms attacks in 2 ms under a hard blow and takes half a second under the softest, which is a struck note against a swelled one rather than two kinds of snap. A 400 ms delay stays 400 ms under the softest and comes in to under two milliseconds under the hardest, so a hard blow simply cancels the wait. Octaves rather than a straight scaling, because these times are heard in ratios: the step from 5 ms to 10 ms is the audible change that the step from 2 s to 2.005 s is not.

The stretched attack stops at five seconds, which is the longest the ATTACK row itself goes. Two hundred and fifty-six times a short attack is a long one and exactly what the control is for, but the same ratio on an attack that was already long is half a minute, which no setting of the knob alone could have produced. The cost is that a full amount on an attack above about 20 ms gives the very softest notes one ceiling rather than a longer figure each, and that is the right end to lose resolution at, since they are already slower than anything the patch was built around.

Setting it per partial is what separates it from a velocity curve over the whole instrument. Give the upper partials a strike amount and leave the fundamental at zero, and a hard chord lands as one bright block while a soft one unfolds, the fundamental first and the series arriving behind it. That is close to what a struck string does, and it is not something a single delay or attack knob can say.

Aftertouch works the same way but **adds** to the fader instead of scaling it, and it ignores velocity entirely. That means a strip with its fader all the way down is silent until you lean on the key, and then it fades in under your finger, while a negative amount fades an open strip back out again. Put a few upper partials on positive aftertouch and the note grows brighter the harder you press, without touching the partials you left alone. Both channel pressure and polyphonic aftertouch are accepted, and whichever is higher wins. Pressure is smoothed over about 15 ms, so seven-bit MIDI does not step the gain.

A note begins at the pressure the channel is already holding rather than at nothing. The smoothing is there so that moving the controller does not step the gain, and a key struck while the controller is already somewhere has nothing to smooth towards: ramping up to meet it spends the first 15 ms at a level the player never asked for, which is a thump on every note under a negative amount and an audible fade in under a positive one. Per-note pressure is the other case and does start at nothing, because it is a fact about that key and the key has only just gone down.

The mod wheel can stand in for it, and by default does. Most keyboards have no aftertouch at all, and the wheel is the control your hand already goes to, so CC1 feeds the same destination. Nothing changes for a controller that does send pressure, since a wheel left alone reads zero. Settings has an "Aftertouch from" entry if you would rather have one or the other on its own. Polyphonic aftertouch is not on that list: it is per note rather than per channel, there is nothing ambiguous about where it should go, and it stays routed whatever the setting says.

### MPE

Off by default, and under Settings. With it on, a controller that gives every note its own MIDI channel can bend and press each note separately, which on this instrument means each finger gets its own copy of all 33 aftertouch destinations. Press into one note in a chord and only that note's upper partials come in.

The reason it is a setting rather than something switched on permanently is that it changes what a channel number means. With it off, a channel is ignored: a key-up on channel 7 releases a note that went down on channel 1, which is what a single-channel keyboard needs. With it on, the channel is part of who the note is, so the same key can be held twice on two channels, bent in two directions, as two voices rather than one retriggering the other.

An ordinary keyboard plays either way. With MPE on, notes arriving on the master channel are notes of the master channel rather than notes of nowhere, so they sound, one voice per key, moved together by the wheel exactly as they would be with the setting off. That is worth stating because the alternative, which is what happens if you route only the member channels, is a plugin that goes silent when a normal keyboard is plugged into it.

Three numbers describe the layout, and only one of them comes from the panel. The zone is the lower one with all fifteen remaining channels as members, which is what a controller sends unless it says otherwise, and it is free to say otherwise: the layout messages it sends are parsed and replace this. The per-note bend range is the 48 semitones the specification asks for, since that one belongs to the controller. The master range is taken from the BEND setting, so the wheel spans what the panel says it does whether MPE is on or not. The tests measure both ends of that: a wheel at maximum against a range of 2 takes A4 from 440.0 to 493.9 Hz, and a member-channel bend at maximum takes it to 7040.0 Hz, which is the four octaves 48 semitones buys.

Switching the setting either way releases whatever is sounding. Voices started through one set of entry points cannot be found by the other, so without that they would hold with no key left to lift.

Slide, the third MPE dimension, is parsed but not routed anywhere yet. Bend and pressure are.

The top bar holds everything that is not per partial, in signal order from left to right. **LINK** is the exception and stands at the top of the caption gutter instead, over the column of names it gangs.

| Group    | Contains                                                                                                                                                                                                       |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Preset   | the preset menu: factory, saved, and somewhere to put the one you are working on                                                                                                                               |
| Settings | which version this is, undo, polyphony, bend range, MPE, tuning, what feeds aftertouch, phase reset, the safety clipper, the limiter's lookahead, zoom and the way back to a window that shows all 32 channels |
| Series   | **STRETCH**, **TRACK** and **WOBBLE**, what the instrument does before anything is done to it. See below                                                                                                       |
| Echo     | the tape echo. See below                                                                                                                                                                                       |
| Reverb   | the reverb. See below                                                                                                                                                                                          |
| Output   | **MASTER**, the stereo meter, and the converter readouts under it                                                                                                                                              |

Zoom lives in the Settings menu rather than on the bar, and that is worth eighty-eight pixels, which go to the output meter instead and are what makes the converter readouts wide enough to keep their units.

That was once an argument about room: the bar came onto one row at 1256 px against a window opening at 1340, so eighty-eight more would have wrapped it. It comes onto one row at 1252 against a window of 1350 now, so they would fit. What keeps zoom in the menu is what keeps everything else there, which is that it is set once and left, and a bar is for the controls a piece is played with.

The entry under the zoom puts the window back to the size that shows the whole mixer. Window size is remembered with the session, which is what makes a window you dragged stay where you put it and also makes a narrowed one permanent, since nothing else here ever sets it. Its height comes from which groups of rows are folded rather than from a stored number, so folding a group away and then fitting leaves no empty band under the strips.

Echo, Reverb and Output are drawn as boxes, because a box is what says "these belong together" and there is something in each of them to group. The rest are single controls standing on their own: a box around one button says nothing the button was not already saying, and four of them in a row turn the bar into a fence. Buttons, lists and the meter all stand on the line the knob dials stand on, rather than in the middle of their row, since a knob carries its caption underneath and anything centred beside one reads as sagging.

Settings and Link are menus rather than panels. Everything behind Settings is set once and then left, and a short list of whole numbers reads better written out than dialled in on a knob. Phase reset gives a coherent, percussive attack by restarting partial phase on each note, and it is not something you sit and adjust, so it is not worth the width of a button.

The safety clipper was in that list on the same grounds and has since come out of it, onto the bar as CLIP, under the meter beside the converter readouts. The argument was that it is set once and left, which is true of how it ends up and not of how you arrive there: it is the one thing behind Settings you reach for while listening, because whether the output is being caught is a question about what you are hearing right now. It also had somewhere to go, which it did not before. The master fader moving onto the meter freed the group its knob had, and the three facts about the output stage now stand together under the lamps: the rate, the bits, and whether anything is being held back. It keeps its entry in the menu as well, since a switch with two homes costs nothing and both follow the one parameter.

It has since stopped being a switch at all. CLIP chooses between five shapes the way ECHO and REVERB choose between their machines, and the Settings entry keeps the switch that decides whether any of them is running, which is the one question that still has a yes and a no. The button abbreviates where it must: ASYM, because Asymmetric is 54 px of label in a space that gives 46, and the ten pixels it would take belong to the converter readouts beside it, which are already at the width they need to name their units. LIMIT follows it so the five read as one set of choices rather than four short words and a long one. Only the button shortens them. The menu, the host's automation lane and the documentation have room for the words.

Everything fits across one row above about 1200 px of logical width. Below that the bar reflows onto further rows rather than dropping controls or letting captions collide. It fills each row as far as it will go, so it stays compact and anchored to the title, and the rows below the first run the full width since the title is above them rather than beside them. One row per group is the worst case, and the window will not shrink past 512 px, which is where it stops fitting in three.

The output meter is the one element that flexes. It takes any width left over on its row up to a limit, since uncapped it swallowed a whole row and stopped reading as a meter, and it gives up to 40 px back when a row is slightly too tight. That last part matters: a window a few pixels short of fitting a row narrows the meter instead of wrapping.

The exception to filling greedily is a last row that would come out nearly empty, since one small group alone on a row of its own reads as a mistake. In that case a group is pulled down to join it.

Double-clicking any knob restores its default. Hovering a harmonic number shows its interval and exact cent deviation.

Pointing at a knob picks out its row all the way across the mixer, through every strip and the noise channel, and brightens the one caption in the gutter that names it. Out at harmonic 28 that caption is a long way to the left, and following a band back to it beats counting rows. The two readouts count as part of the control above them, so drifting off the tuning knob onto its cent figure does not put the highlight out.

The channel under the pointer is marked the same way, bracketed by a rule down each side with the same wash between them, and its number at the head of the strip goes accent exactly as the gutter caption does for the row. The row answers which of the twenty-two control rows you are on and the column answers which of the thirty-three channels, and where the two cross is the knob a drag would actually move. Thirty-eight pixels of strip is not much to aim at with a mixer this wide.

The lit number is what makes the rest of it work, and it is the consistent answer as well as the legible one: the row's whole gesture is that the name of the control lights up, and the number is the name of the channel. The wash then only has to join the lit number to the rest of the strip, which it does at the row's own weight now that every channel stands on the same grey and a hovered strip differs from its neighbours by the wash alone. Both weights are named once and shared by the two, since a pair meant to read as one idea is worth nothing if the halves can drift apart.

One thing about the column does have to differ. It is drawn over the children rather than behind them. The row can sit underneath because what it crosses is knobs, which have transparent corners for it to show through, but a column crosses the meter and the lamps, and those paint their own backgrounds so the strip beneath them does not have to. Behind those it would simply disappear, leaving the channel marked everywhere except the parts with something in them.

Each strip works out whether it is the hovered one from where the pointer is, rather than being told by the editor. Leaving one strip for the next fires an exit and an enter that can arrive either way round, and reading the pointer gives the same answer whichever order they come in. The test drives both orders.

Knowing where the pointer is costs the strip a deep mouse listener, and that listener has now caused two separate faults, so it is worth stating the shape of it once. A listener registered for every nested child is handed every event those children get, whatever it was registered for, and it is handed the component's own events too. So a handler on the strip runs once for a click on a child and twice for a click on the strip itself, and it runs for kinds of event nobody had it in mind for.

The first was the wheel. A listener registered for every nested child is handed every event those children get, wheels included, and the default handler passes whatever it is given up to the parent. So a scroll that a knob had already taken was passed on again by the strip that owns it, reached the viewport, and dragged the series sideways under the hand that was turning the knob. It only showed when the window was narrow enough for there to be anything to scroll, which is why it survived a long time: at the width the window opens at there is nothing to see. The strip now passes on only a wheel that landed on the strip itself, so the background scrolls the series and a control keeps its own.

The second was the right-click. A right-click anywhere in a strip opens the LINK menu, which is what the listener is doing there: a knob would otherwise swallow it. On the strip's own background the handler ran twice and opened two menus stacked on each other, and choosing an item on the front one left the one behind it standing with its ticks unmoved, so the setting looked to have been refused when the instrument had already taken it. Only the gaps between sections showed it, since everywhere else on a strip has a control over it.

Nothing on the two events tells them apart. The listener is handed an event rebuilt from the same click, so the component, the position and the modifiers all match, which leaves only when it happened. That is what separates them, and it is what JUCE itself does with the duplicate wheel events it sometimes receives.

The faders and the mute and solo buttons are left out of it. Those two rows are unmistakable already, and a wash the height of a whole fader was a lot of paint to say so.

Knobs show their value as a ring of discrete ticks rather than a continuous arc, which suits an instrument that is itself built from 32 discrete partials and reads more like a measurement device than a mixing desk. Faders carry a scale in the same tick language.

The panel is lit from the top left throughout. Knob caps are flat dark discs with a fluted rim, a wide stripe in the channel's colour running from the middle of the face out to that rim, and a soft shadow cast down and to the right. Channels have a bright left edge and a shadowed right one so a run of strips reads as raised columns, fader grooves darken at the top where they are cut into the panel, and buttons cast a shadow when raised and lose it when engaged.

**The knob is the desk's, not the pedal's.** A flat face rather than a dome, because a strong radial gradient with a specular bloom reads as a ball bearing and not as the top of a control. What roundness there is lives in a lit arc along the upper left of the rim and a dark one along the lower right, which is the mirror of where the reference photograph has them: that knob is lit from the upper right and this panel is lit from the upper left, and a rim catching the light from the other side would be the only thing in the window disagreeing about where the light is.

**The flutes turn with the knob, and that is what makes it a knob turning rather than a stripe sliding round a disc.** They sat at fixed angles to begin with, so the only thing that moved was the pointer and the cap read as a dial face with a hand on it. What sells the turn is that the lighting does not turn with them: the lit arc stays on the upper left and the shadow on the lower right while the teeth travel through, so each one brightens and darkens as it comes round. A texture moving against a fixed light is what a turning surface looks like.

The silhouette helps too, which is the other half of the same idea. The flutes reach a little past the face, so the outline is bumpy rather than round and the bumps move with the value. It is less than a pixel of relief on a channel knob and it still counts, because what the eye is reading is that the shape is not the same shape it was a moment ago.

**The notches are cut out of the collar, not drawn onto it.** The knob is a wall of flutes standing around a smaller cap, so between two of them there is nothing, and what shows through is the panel and the shadow the cap is throwing onto it. The collar is one path with the notches taken out of its outline and one gradient across the whole of it, which is one fill rather than one per segment on six hundred knobs. A linear wash across a band two pixels deep is indistinguishable from shading it by angle.

Drawn as dark marks instead they are paint on a disc, and then there is no right answer for how dark, only a guess. The guess was wrong in the direction that gives the whole thing away: the marks measured 4 of 255 at their floor, against the 12 of the deepest shadow the cap throws. Nothing on a lit panel is darker than the shadow of the thing standing on it, so they read as holes punched through the panel rather than as gaps between flutes. Cut, there is nothing to choose. The gap is whatever is behind it.

The band is an eighth of the radius, which is the reference's own proportion, and the face plate is all the rest, and a notch bites part of the way into that band rather than all of it, so a ring of collar survives unbroken between the notches and the face. Cut all the way in, the flutes are separate teeth with the face showing between them, which is a gear and not a knurled edge. A collar taking a third of the cap goes the same way.

**The reference was measured by its silhouette rather than its shading**, which is the only part of it that answers the question. Its cap ends at 0.878 of the wall's radius, its notches take 0.32 of the pitch, and they cut 0.28 of the way into the wall band. Its shading says something else entirely: there is a dark band at the foot of the wall that looks like the colour of a groove and is the cap's own shadow falling on the wall behind it, and reading the collar's colour off that is how the first version came to be so dark.

The depth is the reference's too, and it was not at first. A bite of 0.28 of the band is under half a pixel on a cap this size, so it was cut to 0.62 instead on the usual grounds that everything about this collar is the reference exaggerated to survive 26 pixels. That was answering a problem the cut had already solved. A notch drawn as a dark mark has to be deep to be seen, because it competes with the collar it is painted on. A notch that goes through shows the panel behind it, and panel against collar is legible at a depth that paint would not have been, so the proportion carries straight across after all. Worth remembering the next time something here looks like it needs exaggerating: check what it is competing with first.

**The cap is the lighter of the two, and the collar is nearly flat.** They are the same plastic. The cap is the flat top of the knob and takes the light square on, the collar is the wall below it and is turned away, and that is the whole of the difference. A photograph of the knob puts its wall at 0.95 of its cap and the drawing puts it at 0.93. Measured all the way round, the drawing's wall is a single tone with a spread of zero, and the photograph's turns by a few values of 255, which is what a wall does and is not enough to see from straight above. It carries that much.

This was backwards, and badly: the collar was 3.5 times the cap's brightness and carried a wash of 51 across its width. A wash across the band is what a chamfer looks like, and a chamfer is what the thing read as, which is why the cap looked like a thick slab with a bevelled edge sitting on a flat ring.

The collar was lit that way because it had no other way to show itself. A band two pixels deep, shaded like the face, is two pixels of nothing. It has one now. The notches are cut through, so the collar is read by its scalloped outline and by the panel showing between its flutes, and it no longer has to be the bright part to be seen. Those two changes are the same change arriving a month apart.

**A dark knob needs light to carry its shape, because tone cannot.** The knobs sit at 39 for the cap and 35 for the wall against a panel of 31, which is what it takes for them to read as dark objects on a dark panel rather than as pale discs stuck to it. At that distance the notches have nothing left to say: a notch shows the panel through it, so what makes the knob look rugged is the wall's tone less the panel's, and four values of 255 is not a difference anybody can see. Darkening the knob and keeping it faceted look like opposites.

The lit edge is what reconciles them. The collar's own outline is stroked, notches and all, so every flute gets a lit edge and every notch is a break in it, laid on with a wash that is brightest where the light comes from. It peaks at 110 against the wall's 35 on the lit side and still carries 64 on the shaded side, because the shaded flutes have to be flutes too and over there the edge is the only thing saying so. It is a highlight on a turned edge rather than an outline drawn round a disc, and the difference between those two is that a highlight has a side it comes from.

**The cap's shadow falls outside the cap**, which is the difference between a thin cap and a thick one. A cap standing proud of its wall throws a shadow onto the wall. A dark band drawn inside the cap's own edge is not that shadow, it is a bevel. The reference's cap throws its shadow across at most 14 percent of the wall and only on the side away from the light, which is a cap with almost no height to it, and that is what the photograph shows too.

**There are eight slots**, as the reference has, whatever size the knob is drawn at. Taking the count off the radius ran it to sixteen on a bar knob, and at that pitch the slots are a pixel apart, they alias into a shimmer as they turn, and no single one of them can be followed round, which is exactly what the eye needs in order to see the knob turning at all. Eight is also far enough apart that what sits between two of them is a face rather than a tooth.

**The first slot sits at the stripe's own angle**, so the stripe runs into the gap between two flutes rather than over the top of one, and it stops partway along that gap so the two flutes either side stand a little proud of its tip. The part of it past the face plate is a step down and takes the collar's light rather than the face's, bright where the stripe points into the light and dark where it points away. That is the one place where the stripe and the lighting have to agree, and it is what stops the mark reading as painted across the flutes instead of let in between them.

The whole effect is slight, the collar being two or three pixels on a knob this size. It does not have to be large. What the eye is reading is that the shape is not the shape it was a moment ago, and a slot moving a pixel says that as well as a tooth moving five.

There is no room for a proper skirt standing clear of the cap, which is what the reference has. The tick ring owns everything from 0.76 of the radius outwards and earns it, carrying the value and what a macro is doing to it.

**The stripe reaches the rim and is drawn as a rectangle rather than a stroked line.** Reaching the rim is what lines it up with the lit ticks beyond, so the two read as one statement rather than as a mark on a cap and a separate arc around it. The rectangle is for the ends: a line with round caps puts a dome at the centre of the face, which is where the eye is least willing to forgive one. Pointers and fader caps are translucent glass, so the tick ring and the meter read straight through them.

**Every screen and every lamp is set into the panel**, which is the other half of a panel lit from one corner: the raised things show the two walls nearest the light and the sunk things show the two furthest, so a channel is lit along its left edge while a hole cut in one is lit along its right. The cents readout, the shape glyph, the five activity lamps and the pitch needle all get a dark edge along their top and left and a lit one along their bottom and right.

It is drawn around each of them rather than inside, because they are a few pixels across and a border taken out of the picture would leave no picture. None of them had to move to make room: each was already standing in a margin of its own that nothing was using, so the lip costs nothing but the one pixel on each side that the margin already held.

Two whole shapes rather than four arcs, the lit one and then the shadow over it offset by the depth, because the face that lands on top is what cuts the middle out of both. The shadow is the size of the opening and the lit shape the size of the lip, which is what makes the lip one pixel on every side: offsetting the larger of the two instead gives two pixels of shadow above and to the left against one of light below, which reads as the element sitting off centre in its own hole and takes a pixel its neighbour was using.

Both shapes run under the opening as well as around it, so a face drawn at low alpha shows the shadow through itself. The lamps and the needle are exactly that when they are dark, so both blend against their own backdrop instead, which is the colour they came out as anyway and covers the floor of the hole while it is at it.

**It is not free, and the lamps are what pays.** Five lamps and a needle to a strip across thirty-three strips are the one thing here that repaints thirty times a second, and two shapes is half of what a lamp's whole paint used to cost. Measured on one lamp, alternating builds and taking the minimum of six runs each: 4.3 microseconds without the recess and 6.9 with it, which across all 198 of them is 0.85 ms of a frame against 1.37, so the mixer's lamps went from 2.6 percent of a core to 4.1. Two things that were tried and did not help: ellipses in place of rounded rectangles save nothing, both being general path fills, and giving the recess a third shape to put an opaque floor under those two faces costs 1.3 microseconds a lamp where blending the faces themselves costs nothing.

What would pay for itself is caching the static half of a lamp's paint, which is the backdrop, the rule, its lit lip and the recess, as a nine by nine image and blitting it, leaving only the bloom and the lamp itself to draw. That would end up cheaper than before any of this, and it is not done because the editor paints under a zoom transform and the display has a scale of its own, so the cache would have to be keyed on the physical pixel scale and rebuilt when it changes. Blitted under a transform the image is resampled rather than copied, which could cost more than the fills it replaced, and everything that can be checked here is a 1x software render, so a cache that was wrong only at 2x would look perfect in every screenshot.

Cast shadows are built from a few overlapping shapes rather than from a real blur. JUCE has a proper `DropShadow`, but it is a software Gaussian and running one on several hundred controls every repaint would be far too slow.

**Slide.** The third MPE dimension, which is the forward and back axis under a finger, arriving as CC74 on the note's own channel. Settings has a **Slide to** entry with three answers.

**Brightness** is the default, because it is what a player's hands expect from that axis. It moves the keyboard tracking for that note alone: pushing forward takes tracking away and the note opens up, pulling back adds more and it thins. It cannot brighten past what the patch already is, since there is no negative tracking, so a patch with TRACK at zero is already as open as it gets and slide can only darken it. The travel is six dB per octave either way, half the range of the control itself.

**Tuning** is the one only this instrument can offer. Slide moves that note's blend between equal temperament and just intonation, so holding a chord and pushing one finger forward fuses that note while the rest stay smeared. It is not the default because it will surprise anyone whose hands expect brightness.

**Off** ignores it.

Slide is read as movement rather than as a position. The first value a note is given becomes that note's nought, and what reaches the sound is how far the axis has travelled from it since. A keyboard with no slide axis sends nothing and changes nothing.

That is not how it started, and the reason it changed is that the controllers disagree about what the axis is. A Seaboard reports where the finger sits on the keywave and rests at the centre. An Expressive E Osmose spends the first part of the key travel on pressure and only then begins sending CC74, from the bottom of its range upward, so its rest is an end of the axis rather than the middle of it. Read as a position, that second kind lurches the instant the axis engages: the note went abruptly darker and only then began to brighten, which is the opposite of the gesture being made. Read as movement, both start the note on the patch and go where the finger goes.

The travel is scaled so that the longer of the two directions away from the rest is the whole of the slide. A note whose rest is the bottom of the axis therefore reaches full slide at the top of it, where without any scaling it would have had twice the reach of a note resting at the centre. One scale for the note rather than one per direction, which is the part that took two goes: scaling each direction to fill the slide on its own leaves a rest near the bottom with a sliver of travel underneath it, and dividing by that sliver turns it into a hair-trigger. Ten codes of lift swung the note from untouched to fully dark while twenty codes of press moved it a sixth of the way, so pressing in was smooth and coming back out snapped. The short side now keeps the sensitivity of the long one and simply reaches less, which is honest, since there is less of it.

Which destination is chosen does not travel with a preset: it is a fact about the controller in front of you, like the aftertouch source and the temperament.

### Legato

The first entry in the polyphony list, above one voice, and the one place where the instrument stops being polyphonic at all. One voice, and a key going down while another is still held moves the note rather than starting it again: the envelopes, the phases and the drift all carry on, so a run keeps the shape the first key gave it. The velocity of the note that began the phrase is kept, since the key that would set a new one was never released and a run that changed timbre under the fingers is not a run.

Letting a key go while others are still down falls back to the most recent of them rather than stopping, which is what makes a trill work. Only the last key coming up releases the note. The keys that are down are kept in the order they went down, in a fixed array of the 128 there are, because this is worked out on the audio thread and nothing there may allocate.

It is one voice, so it heads the polyphony list rather than sitting beside it as a switch. Legato and one voice are the same count and differ only in what a second key does.

### One voice per key

The instrument is polyphonic across the keyboard and monophonic within a key. A string, a tine or a bar is one object, and striking it again takes over whatever it was already doing rather than starting a second copy beside the first, so playing the same key twice does the same here.

That includes tails. A key with a long release that is tapped repeatedly must not leave every tap ringing and sum them, which no physical instrument does. Five overlapping taps of one key peak at 0.412 against 0.424 for a single tap. Left to stack they reach 0.961, which is two coherent copies of the same note.

A tail being taken over gets the same four millisecond fade a stolen voice gets, quick enough to read as instant and slow enough not to click. Measured across a retrigger, the largest jump between neighbouring samples is 0.01453, against 0.01455 for the steepest part of the waveform itself, so the cut adds nothing the signal was not already doing.

A key that is still down, or held by the sustain pedal, is retriggered where it stands instead. That keeps two things a fade would lose: with phase reset off, a legato retrigger carries on from the level the envelope is at rather than restarting from silence, and re-striking a pedalled note takes it back off the pedal.

All of that is a switch, in Settings beside the voice counts, because it answers the other half of the same question: how many voices there are, and how many one key may take. It is on by default. Turned off, every strike gets a voice of its own and nothing about the previous strike is disturbed, so the tails sum and a repeatedly struck bell builds up. That is a sound rather than a fault, which is why it is offered. It belongs to the session rather than to a patch, so loading a preset never changes it.

It also keeps tails out of the voice pool, which is the more expensive half of the problem. A tap that held one of the 24 voices for the whole of its release would let one repeatedly tapped key fill the pool by itself, and past that the allocator has nothing free and takes the oldest voice outright, with no fade and no regard for whether a key is still down on it, which is audible as notes being cut off mid-hold. Four keys held and a fifth tapped 25 times ends with five voices in use.

**When the pool runs out.** Under the polyphony limit a stolen voice is handed a four millisecond fade and keeps rendering out of the eight surplus voices, so stealing never clicks. Past that surplus there is nothing left to fade into: the new note cannot wait, so a voice has to go this instant, and that is a step in the output whichever one it is. The size of the step is that voice's current level, so the only lever is which voice gets taken.

It takes the quietest, preferring one already on its way out. Taking the oldest is the right rule for stealing under the limit and the wrong one here, because the oldest voice is often a note still being held while a tail three quarters of the way through its release sits beside it costing almost nothing to lose. With one loud note held among quiet tails and the pool overflowing, the held note survives.

### The noise channel

Pinned on the right, after the series it does not belong to, is a noise channel marked NZ. It has the same envelope, tremolo, velocity, aftertouch, pan, mute, solo, level and meter as a partial, and takes part in solo alongside them. What it does not have is pitch, so the tuning, pitch modulation and drift rows are empty.

COLOUR takes the tuning row instead. It tilts the noise from dark rumble at one end, through flat in the middle, to bright hiss at the other. The middle really is flat rather than merely filtered less: the two halves of a complementary one-pole pair are summed at unity there, which reconstructs the original white noise exactly. The tests measure a high to low band ratio of 0.13 dark, 1.06 flat and 4.35 bright.

Each voice runs its own noise stream, so playing a chord does not layer 8 copies of the identical signal. Two voices measure about sqrt(2) times the energy of one, which is what independent sources give.

LINK gangs the 32 partials only. Its tuning, pitch modulation and drift have no noise counterpart, and having some ganged controls reach the noise channel while others could not would be worse than having none of them do.

### The two readouts

**A segment is an elongated hexagon with mitred ends**, which is the shape a real one is and most of what makes a drawn display look drawn. A rounded rectangle has the same footprint and reads as a lozenge laid on a panel, where a mitred bar reads as one of a set cut from a single mask: the ends slope so neighbouring segments point at each other across the gap between them, which is what turns seven separate bars into a figure. The mitre is capped at a share of the bar's length, since a half-middle on a narrow cell is barely longer than it is thick and a full mitre on that is two triangles meeting at a point.

The gap between segments is a quarter of a bar's thickness and wants to be about that. It is what the eye reads as the join between two bars, and a wide one reads as seven marks that happen to be near each other rather than as a figure. It cannot close much further: the mitred ends already point at each other, so the gap at a corner is around seven tenths of the one along a straight edge, and a fifth of a bar's thickness is where the corners meet and a figure becomes a blob. The channel readouts are the binding case, their bars being under two pixels thick, so what is a comfortable gap on the preset display is already sub-pixel on those.

There is no bloom around a lit segment, though a real one has it. A bar here is about three pixels thick, so any spill worth seeing is wider than the bar it comes from: what it reads as is blur, and a blurred figure is further from a real display than a crisp one is.

Each channel carries two figures, the cents its tuning knob is worth and the level its fader is at, and both are drawn as seven-segment displays rather than as text. They are the same component as the converter readouts on the bar, which is the point: a number the instrument is reporting back looks different from a number you typed, and there are enough of them on a mixer this wide for that to be worth saying.

Two of the things they show are not numbers. A partial left in equal temperament reads **Et**, and a fader all the way down reads **-inF**, both dimmed, because a statement of fact is not a level. The lower case t is what a real display does with a letter that has no seven-bar form, rather than leaving the cell blank. The fundamental and the octaves read a dimmed **0.0**: their just interval is the note itself, so there is nothing for the knob to do and saying so beats implying a choice.

The minus rides on a narrow cell of its own rather than taking a digit, again the way a real display does it, so **-2.0** spends no more of the display on its sign than **-13.7** does. There is no plus: seven bars cannot draw one that reads as anything but a speck beside the minus, so a partial sharp of equal temperament is the one with no sign, which is how a tuner writes it too. Nothing shuffles as a result, since a harmonic sits on one side of equal temperament and stays there.

**A sign can share the leading digit's cell**, and on the one reading that runs to three digits and changes sign it does. The macro panel's amount reaches a hundred either way, so its hundreds place is only ever a one or nothing and its sign is only ever a minus or nothing: both live in that cell together and the reading saves the whole width of a separate sign.

The minus is the middle segment and the one is the pair of uprights, both lit exactly as any other segment is. Nothing is drawn specially for either.

That took three goes to arrive at, and the two it replaced were both an attempt to dodge a problem that did not survive the display being drawn properly. A minus made of the middle segment used to meet the uprights of the one and the three together read as a four, so -100.0 looked like 400.0, and the answer seemed to be a shorter bar drawn by hand beside the digit. It was not. The gap between neighbouring segments is what tells them apart on a real display, and once the segments were shaped like real ones the gap was there and the four went away. A bespoke mark is a thing a display does not have.

Both rows grew from thirteen pixels to sixteen. Seven-segment digits are about as wide as they are tall, so the row height caps the cell width, and at thirteen a reading like -13.7 ran its figures into each other. Three pixels on each of the two rows is what it costs to be able to read them.

The risk in replacing a label with one of these is a reading the display has no way to draw: a label renders anything, a segment display quietly shows an unlit digit instead. So the test generates every reading the two readouts can produce across both their ranges and asserts that each character has a form.

The noise channel's level reads the same way.

**The preset name is a display too, with fourteen bars to a cell instead of seven.** Seven manage the digits and about five letters, which is everything a readout on a channel has to say, and a preset is called Glockenspiel. Fourteen is what the hardware that had to show words used: the middle bar splits in two and four diagonals and two uprights fill the cell, which carries the whole alphabet.

The two halves of that middle bar are mirror images and had to be made so. The right one started half a bar past the centre where the left one started a whole bar from its end, so it reached a bar's width too far left and the middle of every cell sat off to that side, which shows on a hyphen more than on a letter. They are also longer than they were: a half-middle is squeezed between an outer upright and the centre one, and at the inset the other bars use it came out three pixels, a dot rather than a bar, so a hyphen read as two specks. The cells have no lower case, so the name is upper-cased on its way in and kept as it was given for saving.

**It has nine cells and always shows nine.** Sizing itself to the reading meant the letters grew and shrank as presets were loaded, so the same display was a different instrument depending on what was in it. Nine is where it looks best: wide enough that most of the names stand whole and narrow enough that the cells are the size they want to be. A short name leaves the rest of them standing unlit, which is what a display with a real number of digits in it does.

A longer one is fitted rather than simply cut. The spaces go from the right first, because a space is a cell saying nothing and a vowel is a cell saying something, then the vowels, then what is left is cut. Y is not a vowel here, which is what keeps SYNTH and NYLON and STYLOPOLY readable. The first character is never dropped.

What that buys over cutting is the end of a name, which is often the half that identifies it: Tape Choir keeps every letter as TAPECHOIR where a cut would have given TAPE CHOI, and Dire Dire EP keeps the EP. What it costs is the word boundaries, so Glass Armonica reads GLASSRMNC. Of the thirty-two names, fourteen need fitting and three of those end up cut as well.

Three things here came out of looking at a render rather than out of reasoning. The diagonals were trimmed by the same step in x and in y, which on a cell half as wide as it is tall left them a third short, so X was four marks around a hole and V did not close. A hyphen was counted as a sign riding between cells, the way the minus does on the seven-bar displays, which left the cells one short of the characters and took the last letter off Lo-fi. And the five had two of its bars wrong. The display reports how many cells it drew so a test can hold the second of those: counting marks in a picture cannot, a K having a wider gap down its own middle than there is between one cell and the next.

S and 5 are the same shape, here and on every seven-bar display ever built. The test that holds the thirty-six characters apart names that pair rather than loosening the rule, since writing them apart would mean inventing a five no hardware has.

### Lamps on the rules

The rules that divide a strip into groups carry a lamp each, showing what the group under them is doing to this partial right now. They cost no height, because the rule was already using that row to draw a line.

| Rule      | Shows                                                                                                                             |
| --------- | --------------------------------------------------------------------------------------------------------------------------------- |
| PITCH MOD | a needle on a track, flat to the left and sharp to the right, where modulation and drift have the partial now                     |
| ENVELOPE  | how far up its envelope the partial is, while the key is down                                                                     |
| KEY OFF   | the same, once the key is up and the key-off stage has taken over                                                                 |
| AMP MOD   | how far the tremolo has pulled the level down, so it pulses at the rate and swings further at greater depth                       |
| OUTPUT    | two lamps, one for each of the rows beneath it: what the blow has left of the partial, and what the pressure on the key is adding |

The two envelope lamps hand over rather than both being lit. The value they are fed is signed: positive while the key is down, negative once the swell and release have it, and a lamp reading zero is dark either way, so the one value that says nothing about the stage is also the one where nothing needs saying.

Two choices are worth knowing about. The lamps read from the voice pool rather than from the knobs, so they describe a note rather than a setting, and nothing pulses over silence. And they follow the loudest voice on that partial, which is the one the meter follows, because a lamp taking the maximum across a chord would describe no note in particular.

That rule is a comparison between voices and not a threshold for reporting at all, and the difference is the whole channel. Read as a threshold, a partial sitting at a peak of exactly zero never beats the nothing the gathering starts from, so every lamp on it reads that nothing instead. A partial gets there while still sounding whenever a negative AFTERTOUCH amount takes its level to the bottom of the clamp, and also with the fader down or above the Nyquist fade. The symptom is the one that points away from the cause: the aftertouch lamp dies at the moment it has the most to say, and takes the velocity lamp beside it with it, which is two independent readings appearing to be coupled.

The tremolo lamp shows what the tremolo has taken off rather than what it has left, which is why a partial with no tremolo on it reads dark instead of sitting fully lit and never moving.

**The output rule carries two lamps rather than one**, sitting side by side, the left for VELOCITY and the right for AFTERTOUCH. Each reads its own row and nothing else. A single lamp over both was tried first and the trouble with it was never the arithmetic: two rows that behave differently cannot be summed into one reading without the reading meaning something neither row means.

**Each lamp is the shape of the knob above it**, which is what makes the pair readable without a legend.

VELOCITY can only take level away, so its lamp shows what the row has left: full is a partial the blow has not cost anything, dark is one the row has taken entirely. At the centre the row leaves every note alone at every velocity and the lamp is full throughout. At one end a blow of nothing takes the whole partial and a blow at full velocity takes none of it, and at the other end those two swap, which is what the row itself promises and is the one thing worth holding a test against. Full at rest is the right way round for a row that subtracts: a lamp that lit as the row did more would be showing the gap rather than the thing.

AFTERTOUCH adds rather than subtracts, so its lamp goes the other way. It rests dark and comes up with the hand, as the amount against the pressure on the key. Unsigned, since a row set to push a partial down is doing as much as one set to lift it.

Both are gated on the envelope like the tremolo. The velocity half is the one that needs it and the one a reader would expect not to: it reads full when its row is taking nothing, so ungated it would sit lit on every channel of a silent mixer.

**The slot leaves itself a pixel at each end.** The lip of a recess is drawn around the opening rather than inside it, so an opening taken out to its component's own edge has nowhere to put its side walls and loses its rounded ends with them. What is left is a bar that looks sheared off rather than milled, which is invisible at a glance and obvious the moment anybody zooms in. The travel is measured off the same inset, so the needle still reaches both ends of the slot and no further.

**The needle's scale.** Fixed, and the same on every strip, so two channels can be compared by eye. Full deflection is 225 cents, a vibrato and a drift at full stretch together.

This is the one place in the instrument where a readout and the control feeding it are allowed to disagree, and it is deliberate. The depth knob reaches 1200 cents so that a square can trill at a real interval, but almost nothing anyone dials lives up there: scaled to the octave, an ordinary vibrato of five cents would sit inside a single pixel of centre and the lamp would show nothing at all. Past 225 the needle pegs, which is what a needle should do off the end of its scale. The scale is named once and a test holds it above the drift range, so it cannot quietly shrink below what the other wanderer alone produces.

It is compressed rather than linear, and that is deliberate. The travel is about fifteen pixels either side of centre. Spread linearly over 225 cents an ordinary vibrato of five cents moves the needle by a third of a pixel, so every subtle setting on the instrument would look the same as no setting at all. A square root keeps the ends where they belong and gives the shallow half of the range somewhere to be:

| Depth     | Linear  | As drawn |
| --------- | ------- | -------- |
| 5 cents   | 0.3 px  | 2.2 px   |
| 25 cents  | 1.7 px  | 5.0 px   |
| 50 cents  | 3.3 px  | 7.1 px   |
| 200 cents | 13.3 px | 14.1 px  |

A scale normalised to each strip's own depth would run the needle to both edges whatever the depth was set to, saying nothing about how deep the modulation goes. The needle parks only when nothing is sounding, so a partial with no modulation on it reads dead centre, which means in tune.

At the display's fifteen frames a second, an LFO above about seven Hz is faster than the lamp can follow and reads as a shimmer rather than as a pulse. That is a limit of the frame rate rather than of the lamp, and raising the frame rate to fix it would cost far more than the lamps do.

**What they cost.** Nothing measurable on the audio thread: every value they show was already worked out by the render loop for its own use, so capturing it is five stores per partial per control block. Against a control that renders 16 voices, the build with the lamps benchmarks inside the run-to-run noise of the one without, its fastest run being the faster of the two.

The drawing is where the care went. Brightness is quantised to twelve steps and the needle to whole pixels, for the same reason the meters are segmented: a lamp that follows its value exactly repaints on every frame in which the value moves at all, which for anything modulated is every frame.

The merging matters more than the quantising, and not in the way it looks. The lamps are handed back to the editor rather than invalidating themselves, and they are merged by row rather than by neighbour, because every strip's lamps sit at the same five heights. Putting them through the general merge alongside the meter bands costs 823,000 pixels a frame on a mixer with all 32 channels modulating, since six rectangles is not enough to keep the rows apart and each merge pairs a lamp at the top of a strip with a meter band at the bottom. Merged by row it is 75,000, against the meters' own 28,000. On the factory presets it is far smaller: 11,000 for _Slow Pad_ and 19,000 for _Shimmer_, played as a four note chord, against a mixer of 1,278,000 pixels.

The second lamp on the output rule is free in that accounting, and that is the merge doing its job rather than a coincidence. Six lamps now sit at five heights, so the band a row merges to is the one it was already, and the row-merged figure is the same to the pixel with the lamp as without it. Only the general merge notices, and it notices by getting better rather than worse: more rectangles sharing a row give it more to pair usefully.

The output lamps cost almost nothing on a preset being played, either. Driven by a ramp they move every frame like the rest, but their rows sit still and so does the controller, so each lamp quantises to the same step frame after frame and asks for nothing: _Slow Pad_ is unchanged by the pair and _Shimmer_ moves by under 200 pixels a frame.

### Meters

Each channel meters what that partial is actually putting out, on a decibel scale floored at -48 dB. Since it reflects the final gain, it shows the envelope, tremolo, velocity, aftertouch, the Nyquist fade and mute or solo all at once, so the spectrum can be watched evolving as a note decays.

The meter fills the fader's own track rather than sitting beside it. A fader that also drew its set level would be showing you something the cap already says, so the whole track is given over to output instead. The cap is drawn as translucent glass, so the meter reads straight through it, and the knob pointers are drawn the same way so the value arc shows through them.

It reads as glass: a light body the lit segments show through, a softer edge, and a lip along the top rather than a divider across the middle. A bright line across the middle for reading the exact position would sit at nearly the weight of the edge around it, three light lines inside ten pixels, and the cap would come out as a pill with a slot cut in it. Nothing needs that line, because the exact position is printed in dB directly under the fader.

It reads the loudest instance of a partial across the sounding voices rather than the sum, so it shows the shape of the patch instead of pinning itself the moment you play a chord.

The cost is close to nothing on either side. The audio thread samples a value it has already computed once per 32-sample control block, which measured inside run-to-run noise on the benchmark.

Drawing them cost rather more than that until it was measured properly. Meters are the only thing in the window that changes on its own, so they set the cost of playing a note, and several things were wrong at once. They are worth writing down because almost none of them were about drawing being slow.

The meters are segmented rather than continuous, which is the old spectrum-analyser look and also means the display only changes when the level crosses a segment boundary. A lamp lights when the level reaches it and goes out only once the level has fallen clear of it, so a note sitting on a boundary cannot flicker. On a slow decay 138 frames out of 152 have nothing to redraw at all.

Nothing repaints itself. Each meter hands the band that changed up to the editor, which collects all 33 and invalidates a handful of rectangles once. Merging them into a single rectangle is nearly as bad as leaving them scattered, since the bands sit at different heights and their union is most of the mixer. Six rectangles is the setting that both stays specific and stays cheap: it invalidates 7,000 pixels a frame where one merged rectangle invalidates 54,000.

The meters are opaque, painting their own slice of the channel gradient, so the strip behind them is not redrawn underneath. And they are read fifteen times a second rather than thirty, which is more than a segmented meter can show anyway and halves the number of frames in which anything is invalidated at all.

Together those took a decaying chord from 141% of a core to 1%. And it was still visibly sluggish, which is the interesting part: the cost was never CPU. It was that since around macOS 10.13 CoreGraphics answers a list of scattered dirty rectangles by redrawing the one rectangle that encloses them, and for this layout that is the whole window, every frame, whatever care went into the rectangles. JUCE's own note in `juce_NSViewComponentPeer_mac.mm` says as much, and points at the way out: a Metal-backed layer, which keeps the rectangles apart and puts the compositing on the GPU. `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` turns it on and has no default, so it is set in CMakeLists.txt for Apple builds. That was the one that made the difference on the machine, and none of the work above would have shown up without it.

One thing that looks like it should help and does not: splitting the mixer into halves that update on alternate frames. Each meter still updates at the same rate and each frame covers half the width, so the area per frame halves, but the number of frames doubles. Frames are the more expensive of the two.

### Folding the mixer down

Clicking a section heading in the caption gutter folds that group of rows away, and the window loses exactly the height those rows were taking. So does clicking the rule that section draws across any strip, which is the same act reached from where the hand already is: the gutter is at the far left, and getting to it from the partial you are working on means crossing the mixer and finding your way back, by which time you have lost which column you were in. The rules line up with the headings because both are laid out by the same call, so the strip needs no geometry of its own and cannot drift out of step with the gutter.

The window's height follows either way, and it has to be measured before it is allowed to move. Applying the resize limits is itself a resize: JUCE ends `setResizeLimits` by constraining the current bounds to the new ones, and unfolding raises the floor by exactly the rows coming back. On a window already squeezed against that floor the limits grew it by those rows, and then the arithmetic added them a second time. A window squeezed to 997 folded to 847 and came back at 1147. Everything spare goes to the fader, since that is the one flexible row, so what it looked like was the faders swelling to fill the screen and disappearing under the dock. Folding never showed it: folding lowers the floor, so nothing is constrained and only the arithmetic moves.

**A rule under the pointer says so, the way everything else here does.** The pointer becomes the same hand the gutter's headings show, and that section's caption in the gutter lights in the accent along with its disclosure mark. The mark is lit with the caption rather than left dim beside it, since the two are one control and half of it coming up reads as a rendering fault.

That needed the rules to be part of the answer to what the pointer is on, which they were not: the hover reports a row, and rows are looked up by what control stands in them, which for a rule is nothing. So the one part of a strip that could be clicked was the one part that said nothing when you pointed at it.

Lighting the band took three more things, each of which had been true for a while and only showed once a heading became something you point at. A heading's wash goes over the children rather than behind them, since every one of them carries an activity lamp that fills the whole row and paints an opaque backdrop: underneath it, the headings did not appear to highlight at all. The noise channel had never drawn a row wash at all, so the band stopped at the thirty-second column. And the gutter had always been a passive display lit by whichever strip the pointer was on, so pointing at one of its own headings lit nothing, which is odd for the one part of it that can be clicked.

The hand is given back as the parent's cursor rather than as a plain arrow, which is the part that is easy to get wrong. A strip asks for its parent's cursor precisely so that LINK and the drawing tool can set one across the whole mixer at once, and an arrow set on the strip would mask theirs for everything in it.

Nothing had to be made clickable for it. The rules are the one part of a strip with nothing standing on them, and the lamps they carry already let clicks through so that the rule underneath reads as a continuous line. The pointer shows the same hand over a rule that it shows over a heading. Pitch modulation, envelope, key off, amp mod and output each fold. All five together is 480 pixels. The tuning at the head of the strip and the faders at the foot never fold: the first is what the instrument is for and the second is what you mix with, so neither is ever the thing in the way.

It folds across the whole mixer rather than per channel. The strips are columns sharing one set of rows, and folding a group on one channel and not the next would put every row below it out of step with the gutter captions, which are the only thing naming the knobs.

A folded heading keeps its activity lamp, so a group you cannot see still says whether it is doing anything. The state is remembered with the session rather than with the patch, alongside the window size, the zoom and the three LINK settings, so loading a preset never rearranges your screen.

LINK is three settings and not two, which is what went wrong with it. The scope and the curve say what a drag would reach and how it would be shared out, and the switch says whether it reaches anything at all. The first two were written to the session and the third was not, so a window reopened remembering exactly how a drag would be distributed, with the drag switched off. They are written together now, and everything that depends on any of them is brought into step by one function rather than by the tail of the callback that happened to notice: restoring a window and choosing from the menu have to arrive at the same place, and a second list of things to update is a second list to forget something from.

Rows in a folded section are hidden rather than left at zero height. A knob with no height still takes the mouse and still answers a hover, so it would go on lighting gutter captions and opening LINK menus for controls nobody can see.

### Ganging the channels

**LINK** gangs the strips: dragging any knob moves the same knob on the others. It works relatively, applying an offset to wherever each strip already sits rather than dragging everything to one shared value, so a spectrum you have shaped by hand keeps its shape.

The button stands at the head of the caption gutter, in the band the strips beside it use for their channel numbers. That is the column it belongs to, since what it gangs is the rows the captions name, and it is the only control not on the bar. What it left behind there is worth having: the bar now lays out in one row at the width the window opens at rather than two, and the converter readouts have the room to say kHz and bit rather than only the figures.

It opens a menu rather than toggling, since what a drag reaches and how it shares itself out matter as much as whether it is on at all, and it lights when the switch inside is engaged. The same menu is on a right-click anywhere in the mixer, which is where you are when you want it. Both settings are latched when a drag begins, so changing one midway cannot half-apply two different rules.

While LINK is on, the pointer over the mixer says which curve is loaded: five bars, level for uniform, peaked in the middle for taper and scattered for spread. A mode you cannot see is a mode you forget you are in, and this one changes what every drag does.

With LINK engaged, pointing at a knob arms every knob a drag from it would move, before you touch anything. How brightly each one lights follows the curve, so under taper the strips nearest the one you are pointing at glow most and the far end of the mixer barely at all, and a scope that leaves channels out leaves them dark. Changing the scope or the curve redraws it under the pointer, which makes the difference between the scopes and the curves something you can see rather than something you have to try.

**LINK SCOPE** picks the channels:

| Scope          | Reaches                                                                                                              |
| -------------- | -------------------------------------------------------------------------------------------------------------------- |
| All            | every partial                                                                                                        |
| Same interval  | only the strips sharing the interval class of the one you grab, so you can move just the fifths, or just the octaves |
| Odd harmonics  | 1, 3, 5 and so on, the hollow half of the series                                                                     |
| Even harmonics | 2, 4, 6 and so on                                                                                                    |

**LINK CURVE** picks how the drag is shared out:

| Curve               | Effect                                                                                                                  |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Uniform             | every selected strip moves by the same amount                                                                           |
| Taper from the grab | a strip moves less the further along the series it sits from the one you grabbed                                        |
| Spread / gather     | pushing up scatters the strips apart along random directions, pulling down gathers them onto the strip you are dragging |

Taper is anchored on the strip you grabbed, which always gets exactly its full share of the drag. That has to be true: the knob under the mouse follows the mouse, so any other weighting would put it visibly out of step with the strips beside it. From there a strip's share falls off in a straight line with distance, reaching nothing thirty-one channels away.

Where you reach in is therefore the whole control. Grab channel 1 and the mixer tilts down across its full width, from everything to nothing. Grab channel 32 and it tilts up instead. Grab channel 10 and the curve peaks under your hand and falls away on both sides, to 0.71 at channel 1 and 0.29 at channel 32. The distance is always measured against the full width rather than against whichever end is nearer, so the reach stays the same wherever you grab: scaling it to the nearer end would make a grab on channel 2 drop channel 1 to nothing in a single step.

Spread draws its scatter directions once when the drag begins, so the pattern holds still while you move rather than boiling. Gathering collapses onto the strip you are dragging rather than onto a fixed average, which keeps the knob in your hand as the thing everything converges towards. Half a drag downwards is enough to arrive.

The offset is always measured from the values captured when the drag started, so returning the knob to where you began restores the strips exactly, even if some of them hit an end stop along the way. That holds for every curve.

**Holding shift draws the faders instead of ganging them.** A drag across the fader area with the modifier held sets every channel it passes over from the pointer's height. That is a different gesture from LINK rather than a variant of it: LINK distributes one relative move across a scope by a rule, and drawing sets absolute values freehand. A formant cannot be drawn with LINK, and "everything up three decibels" cannot be drawn.

It works on the faders and nothing else, and that is a fact about faders rather than a scope that was cut. A fader's value is where it stands, which is what lets a pointer's height mean something. A knob has no such reading, so drawing across the tune row would not be a smaller version of this feature, it would be meaningless.

Three things decide whether it feels like drawing rather than like poking. The pointer belongs to the fader the drag began on until the button comes up, so no other strip ever hears about it: the positions go to the editor, in screen coordinates, since a drag that crosses strips cannot be described in any one strip's. Every column between one position and the next is filled in, because two mouse events can be several strips apart and drawing only where they landed leaves holes exactly where the hand moved fastest. And the whole stroke opens one gesture and closes one, so it is a single step in the history however many faders it moved, which is the rule a LINK drag across 32 channels already follows.

The noise channel draws with the rest. It is not a harmonic, but it is a fader, and a stroke that crossed it and left it alone would be stranger than one that did not.

**Two ways to arm it, and the panel does not distinguish.** Shift held is the quick way, and the DRAW switch under the LEVEL caption is the way that stays, and is remembered with the session as LINK is. Either arms it, both light the same switch, and a tool you left on is still on when you come back to it. A mode nobody can see is a mode nobody remembers, which is the whole reason the switch is lit the entire time it is armed rather than only while a key is down.

**The modifier is polled rather than listened for**, which is not the obvious way round and is the only one that works. JUCE delivers a modifier change to the component under the pointer, and `Slider` handles it without passing it up, so over a fader or a knob the editor never hears about it at all. What that produced was a tool that could only be armed with the pointer in one of the gaps between channels, and which latched on for good if the key was let go anywhere else. Reading the modifier on the housekeeping tick cannot be swallowed by anything.

**Armed, every fader lights and nothing else does.** That is the same preview LINK uses rather than a second kind of highlight, and reusing it is the point: both answer the one question, which is what the next drag would reach. Two ways of saying that would be two things to keep in step.

What differs is the colour and the grading. LINK's preview is per channel and graded by the curve, since it is saying how much each one would take. The drawing's is one colour at full across the whole row, since every fader is equally drawable. It is lit in the accent, which is what the switch that armed it is lit in, so the band and the switch read as one statement rather than two.

It marks the tracks rather than the caps, and that is the honest signal. A cap is what you grab, which is the right thing to light for LINK; drawing never grabs anything, it sweeps a band, so the band is what has to be visible. The noise channel lights with the rest, since it draws with the rest.

**While it is armed, LINK reads as off and the pointer becomes a pencil.** Both are the same point: a drag can be a link or a drawing and not both, so offering two accounts of what it would do at once would be one too many. LINK is not actually switched off, and letting go of the tool gives it back exactly as it was. Leaving its switch lit for a gesture that has been taken away from it would be a promise the mouse-up would break.

### The output meter

The top bar carries a horizontal output meter, split into left and right. It is split because the two channels are identical until something is panned off centre, and a summed meter would hide precisely that. It reads the finished output after master gain and the clipper, so it reports what actually leaves the plugin.

It is segmented like the channel meters, for the same two reasons: it reads at a glance, and it only asks to be redrawn when a lamp changes rather than on every frame in which the level moves at all. Lamps run from teal through amber to orange as they approach full scale, with the colours anchored to their decibel positions rather than stretching with the level, so a lamp keeps its colour whatever the bar is doing. Unlit lamps hold their colour dimmed rather than going dark, which means the top of the scale is visible before you reach it. There is a scale beneath marked at -48, -36, -24, -12, -6 and 0 dB.

Each bar carries a peak hold, since what an output meter is mostly wanted for is what it hit a moment ago. The peak stays alight as a single lamp above the bar for a couple of seconds, then falls with it.

### Panning

Every channel has a PAN, the noise channel included, rather than one width control fanning the series out. A single knob can only ever make one shape. Placing the partials by hand is what lets you put a partial opposite the one a semitone away from it, or the octaves left and the sevenths right, and none of those are shapes a width control could have produced.

The law is equal power, so the number on the knob is the number in the audio and the level holds as a partial crosses the field. The tests measure both, reading the positions back out of the rendered audio rather than taking them on trust: hard left and hard right land within 0.01 of the ends, half left images at half left within 0.02, and a partial swept across the whole field varies in loudness by under 0.1 dB.

A good shape to start from is mirrored pairs, 1 and 2 in the centre widening out to 31 and 32 at the edges, with the sides alternating so the louder of each pair does not always land on the same one. _Slow Pad_ and _Shimmer_ write it into their pans, where it can be taken apart by hand. It is worth understanding before you leave it: neighbouring partials have near-identical levels in any normal spectrum, so putting each pair on opposite sides keeps the image centred whatever shape you dial in, and because every position has a mirror, no partial ends up hard panned with nothing facing it.

### Character

Which oscillator each partial is. One choice for all 32, on the bar at the head of the group that says what the series is, because an instrument is built out of one circuit repeated rather than out of a different one per channel.

Every entry is a sine oscillator. What differs is how it fails to be one, which is the only thing that ever told two analogue oscillators apart: no circuit produces a mathematically perfect sine, and the ways each design misses are what people mean when they call one warm and another sterile.

| Character | What it is                                           | What it does                                                                     |
| --------- | ---------------------------------------------------- | -------------------------------------------------------------------------------- |
| Pure      | the table as sampled                                 | nothing. What every partial was before this existed, and the default             |
| Bulb      | a Wien bridge held steady by a lamp                  | no harmonics at all, and a level that lags half a second behind every pitch move |
| Rail      | a phase-shift oscillator grown into its own supply   | a third harmonic at -23 dB and a fifth at -45, and no even ones                  |
| Diode     | a triangle bent into a sine by two that do not match | a second, third and fourth all near -32 dB, which is a kink rather than a warmth |
| Valve     | a triode, biased so one half leans over first        | a second harmonic at -20 dB over a third at -23, which is the warm one           |
| Op-amp    | one that cannot move as fast as it is asked to       | nothing below a kilohertz, and a third harmonic climbing to -19 dB above it      |

On the panel it is a button at the head of the series group, in the capitals everything else on the bar is shouted in, and it lights in its own colour: yellow for Bulb through to the red of channel 3 for Op-amp. Pure does not light at all, which is the honest thing for the one that adds nothing.

**It costs nothing per sample, and that decided the design.** The oscillator is one interpolated table read, an envelope tick and a gain, running 512 times per sample at 16 voices, so a waveshaper in that loop is the whole engine again and four times oversampling to keep it from aliasing is three more. A fixed waveshape does not need to be in the loop at all: run the circuit over a sine once at startup, read off the harmonics it leaves, and build a table from them. The inner loop then reads a different table and is otherwise the same instructions. Measured against Pure at eight voices, on a machine quiet enough for the figure to mean something: Diode costs two percent more, Rail and Valve three, and Op-amp six, because one note's partials fall in different bands and read several tables where the others read one. The reading is trustworthy to about two points, which is what Bulb comes out at and Bulb reads the same plain sine table Pure does.

What the harmonics cost instead is cache. One 16 kB table shared by 512 oscillators sits very comfortably in L1, and a character puts several in play at once, which is the only reason any difference shows up at all.

**The harmonics alias like any others**, so each character is built several times over, each stopping at a different harmonic, and a partial reads the highest one that still fits under Nyquist at the pitch it is at this moment. A partial at a kilohertz has room for its 23rd, one near the top of the range has room for none and reads the plain sine, and one bent upwards drops its top harmonic on the way up rather than folding it back down. With the converter's rate turned down the full table is used, since folding is then the sound being asked for.

**Changing table is a click unless it is a cross-fade.** Two tables hold different numbers at the same phase, so swapping between them at the edge of a control block puts a step in the wave, and a step is a click. A vibrato sitting across one of the lines the tables are divided by crosses it twice a cycle and ticks at twice the vibrato rate, which is how this was found. So the block that changes table plays as a cross-fade from the old one to the new, the same way the gain slides across a block rather than stepping at the edge of it. It costs a second table read for 32 samples on the rare block that crosses and nothing at all on the ones that do not, which is why the rate-limited one, whose partials cross the most lines, is still within five percent of Pure. Measured as the largest step from one sample to the next, a vibrato held across the first slew line reads 0.145 with the table swapped and 0.082 with it cross-faded, against 0.083 for the same note on a plain sine. A test holds it there.

It covers the other two ways a table can change as well: the line where a harmonic runs out of room under Nyquist, which every character has, and someone choosing a different character while a note is sounding.

Two things every table is held to, and a test checks both across every character and every band. The fundamental comes out at the amplitude a plain sine would have, so choosing a character is not choosing a level and the fader goes on meaning what it meant. And the DC the analysis finds is simply not built back in: 512 oscillators each carrying a small offset is headroom quietly disappearing.

**What it does that a fader cannot.** This is an additive instrument, so a second harmonic on partial 3 lands where partial 6 already is. It does not land on top of it: TUNE and STRETCH move the real partials off exact whole-number ratios, and a character's harmonics sit at exact multiples of the partial that produced them, so the two beat against each other. That is the part worth having, and it is also why the recipes are small. Thirty-two partials each adding a few harmonics crowds the top of the spectrum quickly.

**A rack of thirty-two is not one oscillator thirty-two times.** Every character except Pure carries a fixed spread with it: each partial sits a few cents and a fraction of a decibel off the spec the rack was built to, by the same amount in every session, in every host and on every machine. It is not DRIFT, which wanders. This is where the units were the moment they were switched on and where they are again next time. The first partial is the one the other thirty-one were tuned against, so it is exactly on spec in both, which is what keeps a note landing on the key that asked for it and keeps a patch from changing level when the character changes.

| Character | Pitch   | Level    | Why that one                                                                              |
| --------- | ------- | -------- | ----------------------------------------------------------------------------------------- |
| Bulb      | ±1.5 ct | ±0.1 dB  | the lamp holds the level and nothing else here does, and it heats the bridge it regulates |
| Rail      | ±2.5 ct | ±0.5 dB  | three RC stages set the frequency and their errors stack, and the rail sets the level     |
| Diode     | ±2 ct   | ±0.4 dB  | an integrator sets the frequency, which is accurate, and two diodes set the level         |
| Valve     | ±3 ct   | ±0.35 dB | a heater in every unit and nothing regulating either end of it                            |
| Op-amp    | ±2 ct   | ±0.3 dB  | an ordinary amplifier around an ordinary core                                             |

The scale is what a tuned rack holds rather than what the parts are made to. Component tolerance is a percent or more, which is eighty cents, and a rack eighty cents wide is a rack nobody has tuned. What is left after tuning is a couple of cents, and the order between the characters is which part of each circuit sets its frequency and which sets its level.

The pitch figures are half what they first shipped at, and that was decided by playing rather than by measuring: at three to six cents several patches read as detuned rather than as a rack of units, which is to say as a rack that wants tuning again. The level spread was right the first time and has not moved.

**And no two units distort by quite the same amount.** A rack whose thirty-two circuits are all tuned a little differently would still be one circuit repeated if every one of them clipped, folded or slewed by exactly as much, so each unit is also built to one of three drives, 15% either side of the nominal, drawn by index like the rest of it. A third of the partials sit on the nominal, which is the drive every figure written down about these characters describes.

| Character | Its own drive         | The third or second harmonic across the three |
| --------- | --------------------- | --------------------------------------------- |
| Rail      | how far into the rail | -25.8, -23.5 and -21.6 dB                     |
| Diode     | how badly matched     | -35.7, -33.2 and -30.9 dB                     |
| Valve     | how hard it is driven | -22.0, -20.1 and -18.6 dB                     |
| Op-amp    | how fast it can move  | -27.3, -24.5 and -22.6 dB                     |

**Three rather than thirty-two**, and that is the whole design of it. The drive is baked into the table, so a figure per channel would mean thirty-two tables per character and a note reading thirty-two of them per sample, where the reason a character costs nothing at all is that 512 oscillators read one 16 kB table that stays in cache. Three keeps the idea and keeps the tables countable: 72 of them now against 24, 1.15 MB against 400 kB, and 31 ms to build at startup against 8. The per-sample work is exactly what it was, one interpolated read, since all that changed is which table the pointer is pointing at.

**What the three cost is two to three percent of a character**, which is the one figure here that had to be measured rather than reasoned about, since the whole risk was cache. Two builds, one drive against three, ten runs of one and five of the other, taking the minimum of each. At eight voices: Rail 7.13 to 7.24% of a core, Diode 7.13 to 7.27, Valve 7.06 to 7.26 and Op-amp 7.31 to 7.52. What makes those readable is the pair that cannot have moved, since they have no tables to multiply: Pure went 7.04 to 6.98 and Bulb 7.01 to 7.04, so the floor is about a percent and the four that grew are above it.

**The rate limit takes a third of that spread rather than all of it.** Its harmonics do not grow steadily with the drive, they arrive all at once as the wave begins to be limited, so the same 15% either side ran from -41.7 dB to -20.5 on the third harmonic, which is not one circuit built twice. At a third of the depth it spreads about as far as the other three do.

The drive is drawn from a stream of its own rather than from the one the pitch and level come off. Sharing it would shift every draw after the first and re-roll where all thirty-two units sit, which is a different rack rather than the same rack with its circuits built to three drives. That showed up as every preset on Bulb changing sound, eleven of them, and Bulb has no harmonics to drive at all.

What it buys is the paragraph above being true of a patch nobody has detuned. With every TUNE at zero the partials are exact multiples of each other, so a character's harmonics land exactly on the partials above and add or cancel by phase rather than doing anything. A couple of cents of spread makes them beat instead. The rate follows the frequency, since a cent is a fraction of a hertz at the bottom of the series and several at the top: on a low A, partial 1's second harmonic meets partial 2 at 220 Hz and beats well under a hertz, while partial 16's meets partial 32 at 3.5 kHz and beats at ten or so. It costs one add and one multiply per partial per control block, which is a thirty-second of one sample's work, and the spread itself is a few hundred draws at startup. Pure has none of it, which is what keeps every patch written before any of this existed playing exactly as it did.

**Bulb is the one with no harmonics.** A Wien bridge is the cleanest sine any of these circuits makes, because holding the amplitude steady is the whole job of the lamp in it. What the lamp costs is not distortion but time: its resistance follows how hard the loop drives it, only as fast as a filament heats and cools, and the gain the loop needs changes with frequency because no two ganged parts track exactly. So the level sags when the pitch moves and settles once the lamp has caught up. One pole per partial, chasing the pitch with a half-second time constant, and the error left over takes up to a quarter of the level.

How far out of balance the lamp has to be for that is the one number here that is not the circuit's. A bench oscillator is swept by a knob across a decade and its lamp answers a frequency that has doubled. This one is moved by vibrato, drift and a finger, which is to say by cents, and a model faithful to the bench does nothing at all at that scale: at a semitone of full scale it gave 0.8 dB under a 25 cent vibrato and a hundredth of a decibel under drift, which is a character nobody can hear. Scaled to a quarter of a semitone it reads as the circuit it is named after. Measured on a held note, from the quietest the partial gets to the loudest: nothing at all while the pitch is still, 4.3 dB under a 25 cent vibrato, and a 2.5 dB dip across a two-semitone bend that recovers once the lamp catches up. A test holds those figures.

Which is to say it answers gestures rather than sitting there. A held note with no vibrato, no bend and no hand on it is exactly Pure, because an amplitude that is already steady is one a lamp has nothing to do about. Drift moves it least of all, since a wander that slow is precisely what an automatic gain control exists to remove.

**Valve is the one about the second harmonic.** A triode's curve is not symmetrical about anything, so a wave sitting on it leans over on one side before the other, and an asymmetry is what makes even harmonics. Where on the curve the wave sits decides which harmonic the character is about: further along it, the second grows while the third falls away. Biased to put the second at -20 dB and the third at -23, it is the only one here whose loudest addition is an octave rather than a twelfth, which is the whole of what people mean by valve warmth.

**Op-amp is the one that is not a fixed waveshape**, and it is why the table is chosen by frequency as well as by how much room is left under Nyquist. A rate limit does nothing at all to a wave that never asks the amplifier for more than it has, and turns a fast one into a triangle, so the shape depends on the pitch. The corner sits at a kilohertz, and that is the second number here scaled to the instrument rather than taken from the part. A 741 slews at half a volt per microsecond, so at ten volts peak it stops keeping up somewhere around 8 kHz, and a partial up there has no room left under Nyquist for the odd harmonics slewing makes: the character would be real, correct and completely inaudible. A kilohertz is where it can be heard, and it happens to be where the keyboard tracking rolloff sits, being about C6 and so in the middle of where anyone plays. Below it the partials are untouched. Above it they harden as they climb, which is what playing a slew-limited oscillator up the keyboard does.

Three tables cover it, at a quarter, six tenths and twice again past the corner, and anything above the last reads the last. That is not a corner cut: measured, the third harmonic reaches -19.1 dB by twice the corner and does not move again however much harder the limit bites, because the wave is a triangle by then and a triangle at a given fundamental is a triangle. It is also the character that costs the most, about five percent over Pure at eight voices, since one note's partials land in different bands and read several tables where the others read one.

A rate limit is the one imperfection here with a memory, so its cycle is simulated rather than shaped: a limiter run over a sine for several turns, with the steady state it settles into being what gets analysed.

**What the factory presets ask for.** Twenty-six of the thirty-two, decided by ear at a keyboard and by nothing else: eleven on Bulb, eight on Op-amp, four on Valve, two on Rail, one on Diode, and six left on Pure. Three of those six are deliberate rather than left over. Init is the neutral patch. Just Saw and Equal Saw are played against each other to demonstrate tuning, and a timbre difference between them would be demonstrating something else.

The shape of that list says something about the six. Bulb and Op-amp take more than half of it between them, and they are the two that are not fixed waveshapes: one answers the hand and the other answers the register, so both do something on a keyboard that no spectrum sitting still can. Diode came out of that first pass with nothing at all, and picked up its one patch a release later: _60s Organ_ wanted a kink rather than a warmth, which is what a pair of mismatched diodes is for.

### The bus the series is summed onto

The thirty-three channels are not simply added together. They are summed onto a bus that drives, by an amount and in a way that belongs to the character, because the character is meant to say what circuit you are playing through and a summing amplifier is part of that circuit.

It is a separate thing from the tables, and it has to be. A table is one partial's waveform and cannot know what the other thirty-two are doing, so nothing baked into it can answer to the mix. This can: it works on the sum, at twice the sample rate through a half-band filter so nothing it makes folds back down, and it answers to level. Play quietly and it is barely there. Lean on the keyboard and it arrives. That is what a desk does and what a table cannot.

**How much each character asks for** was found by ear at a keyboard, one character at a time, with a development control that is not in the shipped build:

| Character | Drive on the bus |
| --------- | ---------------- |
| Bulb      | 15%              |
| Rail      | 13%              |
| Valve     | 10%              |
| Op-amp    | 10%              |
| Diode     | 7%               |
| Pure      | none             |

Pure sums cleanly and does nothing, which is what Pure is for. There is no knob for the rest, because how hard a summing amplifier is driven is part of what choosing a character means rather than a setting on top of it.

**The op-amp took three goes.** A slew limit is the right model for an amplifier that cannot keep up, and it is the wrong model for a bus. On one partial it hardens the top of the series, which is what the character is for. On a mix it is reacting to the sum's slope rather than to any one note, so it broke up on high notes at settings that sounded fine on low ones, and measured -67, -67, -53 and -73 dB of inharmonic rubbish across A2, A4, A6 and A7. A cubic corner replaced it and does what was wanted without the register deciding how much.

**Two measurement mistakes are worth recording**, since both looked like the design being wrong. Moving the amount spiked the output, because the antiderivative the antialiasing depends on was being carried across blocks instead of recomputed from the stored input. And at small drives the gain was 1.4 dB out, which was float cancellation in the antiderivative rather than anything about the curve: it is computed in doubles now, with a series for the log of cosh where the two terms are close enough to cancel.

**It sits before the master fader**, like the effects after it, so the fader stays a true output level. That also means the bus sees the raw sum, which on a full patch runs 1.5 to 1.9 rather than anywhere near one. Several things that looked like bugs were this: the tape echo's permanent compression, and a preset deliberately mixed low to keep the echo clean sounding wrong once the bus arrived underneath it.

### Wobble

A warped record under the whole instrument. Pitch is bent by reading the output back through a delay line whose length keeps moving, which is what happens when a platter runs eccentric or a capstan slips: the medium arrives early or late and the pitch goes with it.

Three things move it at once. A slow warp near once round a 33 rpm record, a faster wobble on top that is too quick to follow and too slow to be vibrato, and now and then a nudge, a sharp slip that bends hard and settles back. The slips are the glitch, and both their rate and their size climb with the square of the control, so the bottom of the knob is a tired turntable and the top is a transport falling over itself.

| Setting | Typical bend | Worst bend |
| ------- | ------------ | ---------- |
| 25%     | 5 cents      | 65 cents   |
| 100%    | 34 cents     | 322 cents  |

That gap between the typical and the worst is the whole character. A vibrato would have them close together.

It sits between the voices and the echo, so the repeats inherit what the wobble did rather than adding a wobble of their own, which is the difference between a warped record being played and a warped recording of one. Both channels are driven from the same modulation, since it is one platter, and a centred source stays centred. That is deliberately the opposite of the echo below it, where the two sides are meant to disagree.

The delay it introduces grows with the control rather than being switched in, so there is none at all at zero and turning it up cannot step. Getting that right took a fix: coming up from bypass, the line held nothing yet, and reading even one sample behind the write head returned the silence it had been cleared to, which was a step of 0.8 into a signal of 1. The read point is now allowed to sit exactly on the write head, and the glide guarantees it recedes more slowly than the line fills. Measured across a knob moved from zero to full over a second, the largest jump between neighbouring samples is 0.0164 against 0.0144 for the steepest part of the waveform itself.

At zero it is bypassed and passes the signal through untouched, bit for bit, which the tests check rather than infer. At full it costs 0.17% of a core.

### The master effects

Two of the groups in the top bar work on the finished mix rather than on any one partial: an echo and a reverb. They sit where they are in the signal, after everything per partial and before the output group, and both are ahead of the master fader, so the fader is a true output level and moving it cannot change the wet to dry balance underneath it.

Each is three machines behind one button rather than one machine behind a switch, and the button says which is running. Behind each button are two parameters and not one. The switch that turns the thing on is older than the choice of machine, and every patch saved before there was a choice stores it while storing nothing about a type, so the switch stayed where it was and the type arrived beside it. A patch from 1.9.0 therefore loads with the tape it was made on and the room it was made in, which is the only answer that leaves an old patch sounding the way it did.

All three of each are asked to run on every block, and each decides for itself whether the chosen type is its own. The two that are not chosen empty their loops rather than holding a tail that would reappear the moment you switched to them, which is the same rule the single switch had.

**ECHO** is one of three machines. What they share is the panel:

| Control | Does                                                |
| ------- | --------------------------------------------------- |
| MIX     | how much of the output is repeats                   |
| TIME    | distance between the heads, 20 ms to 2 s            |
| FDBK    | how much of each repeat goes round again, up to 95% |
| AGE     | how worn the machine is, and what that means varies |

AGE is the control that makes three machines worth having rather than three names for one. It is the same knob and the same parameter on all of them, and it asks each for the thing that machine gets worse at:

| Machine | What AGE does to it                                              |
| ------- | ---------------------------------------------------------------- |
| TAPE    | top end lost per pass, motor wander, and how hard the tape leans |
| BBD     | the clock slowing, and the companding breathing                  |
| DIGITAL | fewer bits and fewer of them per second                          |

**The tape** is a loop rather than a digital delay. TIME is reached by winding rather than by jumping, so moving the knob slides the repeats in pitch on the way to the new setting, which is the sound a tape delay is mostly wanted for.

AGE is one control for the three things that go together as a tape machine wears: the top end it loses on every pass, how far the motor wanders, and how hard the tape leans over when it is driven. New is clean, bright and steady, old is dark, unsteady and compressed. They were three knobs that were nearly always turned together. The tests measure all three separately: a new machine hands back four times the top end of a worn one after ten passes, holds its pitch to 0.01% where a worn one wanders by over 1%, and passes its repeats through at full level where a worn one compresses them.

The compression is in two stages, and they are not the same thing. The first is character and follows AGE, so a new machine really is untouched. The second is a backstop that is always there, because at 95% feedback a steady tone can otherwise pile up to twenty times what went into it. The tests drive it at maximum feedback with a new machine for twenty seconds and the output stays bounded.

Stereo is two tape paths side by side rather than one repeat walking across the image. Each takes its own channel, feeds only itself and comes back hard on the side it went out on, so nothing crosses over at any point and wherever the mixer put a partial is where its repeats stay. Hard panning is safe to do precisely because the two play at very nearly the same moment.

What separates them is the transport. The two motors run at slightly different speeds, 0.70 against 0.83 Hz of wow and 6.3 against 5.31 Hz of flutter, wander by slightly different amounts, and draw from separate random streams so they can never fall into step. Two takes of the same part never drift together and neither do these: a centred source comes back as a pair that agrees about the note and disagrees about everything else, which is what doubling is.

The rates are fixed rather than offered as controls. What matters is that they differ, not by how much, and a pair of knobs whose only wrong setting is "equal" is a pair of knobs nobody needs.

How far the two disagree follows AGE, since holding speed is what a machine in good order does. That gives the control a floor. A transport that held speed exactly would have both paths wander by the same nothing and put the repeat back in mono, and there is no such transport, so AGE stops 8% up its own range and never reaches one. The knob does not show that. It reads 0 to 100 across the travel it has, because where the floor sits is a fact about the machine rather than a number the player should be made to carry, and the offset is folded in on the way through in both directions.

Measured as the correlation between the two channels on a sustained tone, the bottom of the knob sits at 0.77 and the top at 0.19, so the doubling is always there and always has somewhere to go.

| AGE                       | Channel correlation |
| ------------------------- | ------------------- |
| no wander, below the knob | 1.000               |
| 0, the bottom of the knob | 0.768               |
| 13%                       | 0.019               |
| 100%                      | 0.186               |

The tests check both ends: that a perfect transport would collapse to mono, which is why the panel cannot ask for one, and that the lowest setting it can ask for is already doubled without being a chorus. Feeding one channel only leaves the other at exactly zero, since nothing crosses over.

**The bucket brigade** is a line of capacitors handing a charge along, one step per tick of a clock, and modelling it as a darker tape would miss everything that makes people keep the pedals.

It is darker the longer it is set to, and that is not a stylistic choice. The line holds a fixed number of buckets, so a longer delay can only be had by clocking them more slowly, and the filter that reconstructs the signal has to come down with the clock to keep its own aliasing out. Every unit ever built does this: short settings are nearly clean, long ones are murk. TIME therefore has a second job on this type that it has on no other.

Its stereo is two clocks rather than two tapes. Each side is modulated by a slow sine of its own, 0.31 against 0.43 Hz, sharing no factor so the two never fall into step. That is a gentle vibrato where the tape has drift, which is what a pair of these actually sounds like.

AGE is three things again, and none of them is the one that was tried first. A clock whine was the obvious artefact and the wrong one: it is a fault rather than a character, it sits at a fixed pitch a chord has to be in tune with, and nobody buys one of these for it. What it does instead is get darker still on top of what the clock already costs, dirtier on every pass, since the line clips early and the repeat goes round again so a tail starts clean and ends up growling, and noisier in the way an old compander is noisy. Every bucket brigade has one wrapped around it, compressing in and expanding out, and a worn one mistracks: the pull-down arrives late, so hiss swells in behind a chord and ducks away as the repeats die. A silent patch stays silent, which is why it is keyed to the signal rather than run free.

**The digital delay** is the one that does not pretend to be anything. No tape to wear, no buckets to clock, no motor and no lamp: what goes in comes back out, later and quieter, and the only thing it loses on each pass is level. That is worth having beside the other two precisely because it is the plain one.

Its stereo is ping-pong, which is a different machine rather than a setting of one. The input arrives summed to the middle and every repeat crosses, so a note becomes a line of repeats alternating left and right. On the tape and the bucket brigade nothing crosses between the sides at any point and the width comes from the two paths disagreeing. Here the two sides are one path and the crossing is the whole topology.

AGE does the one thing neither of the others can. At nothing it is exactly a copy, which is what a digital delay is for, and the clean end has to be exactly that and not nearly that. Turned up, the repeats come back through fewer bits and at a lower rate, down to six bits and an eighth of the host rate, so a tail starts as the signal and ends as a memory of it.

That wear is applied on the way out and not on the way round, which matters more than it looks. A quantiser inside a feedback loop has a fixed point: one step times a feedback of a half rounds back up to one step, so the tail reaches the bottom bit and sits there forever. It was audible as a low sound that never went away at every feedback from a half upwards, and still there twenty-five seconds later. Nothing is lost by moving it, because the step stays the same size while the repeats get quieter, so the tenth repeat is crushed against a far coarser grid than the first. That is what a converter does to something fading away, and it is the same progression the wear had when it was inside the loop.

**Both echoes colour the first repeat**, which sounds obvious and was not true at first. The output was taken from the delay line before the stage that ages it, so repeat one came back clean and every repeat after it was worn, and the effect was of a machine that only switches on once you have heard it work. Both now take their output from the coloured signal.

**REVERB** is one of three machines, on the same kind of button as the echo. The panel is the same on all three:

| Control | Does                                                  |
| ------- | ----------------------------------------------------- |
| MIX     | how much of the output is reverb                      |
| DECAY   | how long the tail takes to fall 60 dB, 0.2 to 20 s    |
| DAMP    | how quickly the top end dies out of the tail          |
| PRE     | silence between the note and its reverb, up to 250 ms |

DECAY means the same length of time on all three, which is not something that happens by itself. Measured from a click at the same setting, the room takes 2.23 seconds, the plate 2.38 and the spring 2.46. Two of those numbers were wrong twice over before they were right, and the tests hold each machine against the room rather than against a figure typed in, so the next one to be added has to agree as well.

The wet levels are matched too. On a held chord with the mix full up the three come back at 0.2545, 0.2547 and 0.2546, which is the same to a hundredth of a decibel. One MIX knob serves all three, and a switch between them that changed the level would read as one machine being better than another rather than different from it.

**The room** is a feedback delay network: eight delay lines fed back through a Householder matrix, with four allpass stages per side in front of it to scatter a hit into a wash before it reaches the network.

The two sides are drawn from different lines in different polarities, so the tail is wide by construction and there is nothing on the panel to narrow it. There is no width control, on the same grounds as the stereo spread the mixer does without: a knob with one setting anyone reaches for is not a knob. The test checks the thing worth checking, that a mono hit still comes back with the two sides largely independent.

The room is sized from the decay rather than set separately. A long tail in a small room is a spring rather than a place, and a short one in a hall is a gate, so the two were always turned together anyway.

The matrix is orthogonal, so the network neither gains nor loses energy of its own accord and the decay is entirely the doing of the per-line gains, which is why DECAY can be trusted. The tests measure it: a 1 s setting falls silent in 1.0 s and a 5 s setting in 4.3 s.

The choice of a network rather than a bank of combs is about this instrument in particular. Thirty-two pure sines held indefinitely will find every resonance a fixed network has, and a comb reverb answers them with a metallic pitch. The line lengths are therefore mutually prime and each is slowly modulated at its own rate, so the tail keeps moving underneath a held chord. The tests check for exactly that failure, measuring the loudest bin of the tail against the average across the spectrum.

The input is cut off below 175 Hz for the same reason, and that is fixed rather than offered as a control. A fundamental at full level feeding a long tail floods everything above it, and the reverb becomes a rumble the moment you play low, so it is never wanted open. On an instrument built from 32 partials the interest is above there anyway.

**The plate** is a sheet of steel under tension with a driver at one corner and pickups at two others. What makes it not a room is that it has no geometry to speak of: a hit spreads across the whole sheet almost at once, so there are no early reflections to count and no build-up to hear. Measured on a click, the first fiftieth of a second carries 1.48 times the average of the whole tail in the plate and 0.00 in the room, which is the difference between a tail that is simply there and one still filling up. The shape is Dattorro's, which is the one everybody uses because it is the one that works: four allpasses scatter the input, then a tank of two branches passes it round in a figure of eight.

DECAY is the tank's own gain rather than a room size, since a plate has no size to set, and sizing that gain is where the two mistakes were. The gain is applied four times in a circuit of the figure of eight and not once, and the circuit is every line it passes through, allpasses included. Getting the first wrong made a two-and-a-half second setting last half of one, and getting the second wrong made it last three.

**It is a modulated plate, and the menu says so.** The two allpasses in the tank wander about a millisecond, which is six times what the paper gives them and far more than a sheet of steel under tension does. The reason is the circuit: it is three quarters of a second long, so the same fixed set of arrivals comes back round again and again, and at a long decay that is heard as a pattern repeating rather than as a wash. A test measures exactly that, taking the tail's envelope, flattening it so only its shape is left, and holding it against itself at every lag from fifty milliseconds to a second and a half:

| How the output is read                           | Repeats at |
| ------------------------------------------------ | ---------- |
| four taps, all on one branch's first delay       | 0.34       |
| Dattorro's seven taps, across the whole tank     | 0.23       |
| and the modulation opened from 8 samples to 1 ms | 0.11       |

The first line is a bug and sounded like one: four taps on a single line are four echoes of one circulating signal however many taps it is, and at a short decay nothing goes round often enough to notice while at a long one it is a delay with some reverb on it. The third line is where it stops paying and starts costing, since the excursion is a pitch deviation of twelve cents at this setting and twenty-four at twice it. Twelve is movement in the tail, which a lush plate wants. Twenty-four is vibrato, which it does not, and calling the machine what it is seemed better than pretending the number is Dattorro's.

**The spring** is three helical springs in a tray, a transducer shaking one end of each and a pickup listening at the other. It is the cheapest reverb ever built and the least like a room of any of them, which is exactly why it has its own sound rather than being a worse plate.

What makes it a spring is dispersion. A wave travelling down a helix does not carry all its frequencies at the same speed: the top of the band arrives first and the bottom drags behind it, so a hit comes back not as a hit but as a descending chirp. Every bounce between the ends adds another chirp on top of the last, so a spring gets less like its input as it decays rather than merely quieter, which neither of the other two does. Measured on a click, the bottom of the band lands 6.4 ms behind the top where the plate has no such lag at all.

That is built as a chain of first-order allpasses inside each spring's loop. An allpass passes every frequency at full level and delays each by a different amount, which is dispersion written down, and the loop puts the signal through the chain again on every bounce. The chain is the one expensive thing in the file, so the count was measured rather than chosen:

| Sections | Coefficients | Chirp  | Cost of all three springs |
| -------- | ------------ | ------ | ------------------------- |
| 100      | 0.60 to 0.70 | 7.3 ms | 4.95%                     |
| 70       | 0.70 to 0.79 | 6.9 ms | 3.61%                     |
| 60       | 0.74 to 0.82 | 6.5 ms | 2.23%                     |
| 50       | 0.78 to 0.86 | 5.7 ms | 1.92%                     |

Sixty is where the curve turns: nine tenths of the boing for less than half the work, and below it the chirp goes faster than the saving. For scale, eight voices come to around eight per cent, so the hundred-section version cost as much as five voices and this one costs two.

The band it works over is a transducer's rather than a room's, and where each half of that limit sits turned out to matter more than what it is set to. The pickup is two poles at five kilohertz, outside the loop, met once on the way out. The spring's own loss is one pole inside the loop, met on every bounce, and that is what DAMP moves. Both used to be inside, and a tail that had crossed the tray thirty times had met them sixty times:

| Where the poles are                      | 6 kHz against 1 kHz |
| ---------------------------------------- | ------------------- |
| both in the loop at 3600 Hz              | -10.6 dB            |
| one in the loop at 3600, pickup outside  | -11.2 dB            |
| one in the loop at 10000, pickup outside | -6.5 dB             |

The middle line is the one worth reading, and it is the opposite of what it looks like: moving a pole out of the loop brightens nothing whatsoever on its own. What it buys is permission to open the loop's corner, because the pickup is now what holds the band rather than the loop filter. DAMP also sweeps geometrically rather than in a straight line up the frequency axis, since a corner swept linearly from ten kilohertz spends three quarters of its travel above three, where the pickup already has the band covered and nothing audible happens.

## Notes on CPU

Polyphony is the multiplier that matters, since eight voices means 256 sine oscillators. Measured on one core of an x86 container at 48 kHz with every modulator running:

| Voices | Oscillators | Load      |
| ------ | ----------- | --------- |
| 1      | 32          | about 1%  |
| 8      | 256         | about 7%  |
| 16     | 512         | 14 to 15% |

Those figures are with everything running at once: both LFOs, drift, velocity, aftertouch, the meters and the noise channel. The master effects are on top of that and cost the same whatever the polyphony, since they work on the sum rather than per voice. Most of them are far below the voices:

| Machine         | On one core |
| --------------- | ----------- |
| room            | about 0.6%  |
| modulated plate | about 0.7%  |
| tray of springs | about 2.4%  |

The spring is the exception and the only thing in the release worth a second look. Its dispersion is a chain of allpasses per spring, three springs, run at the host rate, so it costs about as much as two voices where the other two reverbs cost a fraction of one. It was worse: the chain started at a hundred sections and 5% before the count was measured against what it bought.

That leaves enough headroom for the engine to stay a plain bank of oscillators with nothing clever in the signal path. What keeps it cheap:

- A 4096-point interpolated sine table, measured error -129 dB, instead of `std::sin` per sample.
- LFOs, envelope coefficients and gain ramps update once per 32-sample control block. Only the oscillator and envelope run at sample rate.
- Silent partials, whether muted, faded out above Nyquist or sitting at zero, skip the oscillator entirely and only advance their envelope.

The attack knob is logarithmic rather than skewed towards a midpoint, so equal turns are equal ratios: the step from 1 ms to 2 ms gets the same travel as the one from 100 ms to 200 ms. That matters here because its range spans four and a half decades, and fitting a power curve through a midpoint across that needs an exponent of about 7.4, which leaves the bottom eighth of the knob flat enough that nudging it does nothing. Measured, that dead travel went from 13.7% to 0.1%, and 30 ms stayed within half a percent of the middle where it was. The trade is that sub-millisecond attacks now occupy 9% of the travel rather than 27%, of which half was unusable anyway. A test asserts the dead travel, so the next wide range somebody adds cannot quietly reintroduce it.

Every other skewed control got the same treatment, and `setSkewForCentre` is now gone from the project. Which curve depends only on whether the range can reach zero:

|                                                                 | Curve                      | Worst dead travel, was | Now   |
| --------------------------------------------------------------- | -------------------------- | ---------------------- | ----- |
| attack, decay, release, pitch mod rate, echo time, reverb decay | logarithmic                | 15.5%                  | 0.3%  |
| swell, delay, reverb pre-delay, pitch mod depth, drift          | exponential from the floor | 29.4%                  | 15.7% |

The second family cannot do as well and never will. A control that has to reach zero has no ratio to grow by, so its bottom is compressed by construction: to give five seconds of swell any resolution at the top, the first millisecond has to go slowly. What the exponential curve fixes is the part that was actually broken, a slope of exactly zero at the low end, where turning the knob moved the value by nothing whatsoever. The slope is now small but never nought, and that is what the test asserts across all 635 continuous parameters: the thinnest is the attack, whose bottom moves at 1/23766 of the rate its top does, against a power curve's exact zero.

### Where a partial starts

PHASE sets where in its own cycle each partial begins, from 0 to 360 degrees, when phase reset is on. It defaults to zero, which is a rising zero crossing, and zero is the softest onset a partial can possibly have: from there it cannot reach its own peak until a quarter of its period has gone by.

That is longer than the shortest attack available for most of the keyboard:

| Note       | Quarter cycle | What sets the onset   |
| ---------- | ------------- | --------------------- |
| A1, 55 Hz  | 4.55 ms       | the note's own period |
| A2, 110 Hz | 2.27 ms       | the note's own period |
| C4, 262 Hz | 0.96 ms       | the note's own period |
| A4, 440 Hz | 0.57 ms       | the note's own period |
| A5, 880 Hz | 0.28 ms       | the attack knob       |

So below about 500 Hz, turning the attack down past a millisecond does nothing at all, and the note still arrives softly. Measured: at A1 with the shortest attack the sound is a third of the way up a millisecond in, which is just sin(20 degrees), the envelope having finished half a millisecond earlier.

A quarter turn starts the partial at its own peak instead, and the same note is 99% there after that millisecond. The reason it is per channel rather than one global switch is the arithmetic of putting every partial at its peak at once: for a 1/n spectrum of eight that is a first sample 2.7 times the steady peak, for 32 partials it is 4.1, and for a flat 32 it is 32. Staggering the phase across the series gives the edge without the spike, and LINK will spread it across the channels in one drag.

With phase reset switched off there is no reset for it to aim, and it does nothing.

Partials fade out as they approach Nyquist. Without that, the 32nd harmonic of a high note would fold back down as aliasing, since it lands near 67 kHz for a C7. Measured alias images sit at -122 dB. Turning the converter down, below, deliberately switches that guard off.

### The converter

Two settings under Settings, both defaulting to whatever the host is running at, and both cuts rather than effects. They are the one part of the instrument where turning something down does less work rather than more.

Both are on the panel rather than in a menu, as two seven-segment readouts under the output meter, which is where a converter sits in the chain and roughly what one looks like. They have to be visible because they travel with a preset: loading one can change them, and a setting that changes underneath you without saying so is worse than no setting. Clicking either opens its list.

Lit means something is being cut. Left alone they show what the host is running at, dimmed, so the readout says what is happening either way rather than going blank when it is not in use. A rate above the host's own is not something anyone can be given, so it reads as off rather than pretending.

**Sample rate** picks from 32 kHz down to 4 kHz. What it does is not a filter over the top of a finished signal: the whole voice pool renders at the lower rate and the result is held between frames. That is worth doing because sampling a sinusoid at 8 kHz produces one particular sequence of numbers whatever rate you were nominally computing at, so the samples that survive the hold are the only ones worth computing at all. Rendering at the host rate and then decimating would sound the same and cost full price.

Measured on one core at 48 kHz, eight voices, the same patch throughout:

| Render rate  | Load | Against full rate |
| ------------ | ---- | ----------------- |
| host, 48 kHz | 7.8% |                   |
| 22.05 kHz    | 3.8% | 48%               |
| 11.025 kHz   | 2.0% | 25%               |
| 8 kHz        | 1.5% | 19%               |

The aliasing is the point, so the Nyquist guard comes off with it. A converter running at 8 kHz does not quietly mute everything above 4 kHz, it wraps it back down, and so does this: the 32nd partial of A3 sits at 7040 Hz and reappears at 960. The original frequency stays faintly audible above it, because holding a sample puts an image either side of the rate, about seven times quieter at that spacing.

Modulation gets coarser along with everything else. The LFOs, drift and gain ramps still land once per 32-sample control block, which at 8 kHz is 4 ms rather than 0.7. Everything is still in the right place in real time, since every coefficient is derived from the rate being rendered at, so a one-second attack is still a second.

**Bit depth** picks from 16 bits down to 2, quantising to 2^(n-1) steps either side of zero. It sits with the rate, ahead of the echo, the reverb and the master fader, so those behave like outboard on a lo-fi source rather than being crushed themselves. It does not clip: that is the safety clipper's job further down, and a bit-depth setting that also distorted would be doing something the panel does not mention.

Both settings are stored with a preset, since at that point they are part of the sound rather than part of the setup.

### The output stage

The last thing that happens to the signal, after the master fader, which is therefore the drive into it. It began as one soft clipper with a switch, there so that pushing 32 faders up could not hand the host something louder than it asked for. It is five machines now, which makes it part of the sound rather than only a guard, and that is why the fader in front of it travels with a preset.

Every one of them is bounded by full scale. That is not a coincidence of the curves chosen but the point of the stage, and it is tested: a safety clipper that could be driven past unity would not be one. Each is held to it at drives up to 400 times, both on its own and through the whole instrument at three sample rates.

**Soft** runs straight to seven tenths and then takes a tanh knee. Gradual, symmetric, odd harmonics arriving as you lean on it. It is the curve that was already there and it is still the default, and it came through the change bit for bit, compared against the old formula written out longhand rather than against the code, so no patch dialled in against it sounds different.

**Hard** is a wall at unity. Nothing happens until it does, which is a converter with no headroom left: abrupt, bright, the same odd harmonics as Soft but all at once.

**Asymmetric** lets one half of the wave lean over before the other, half against nine tenths, the way a single-ended stage runs out of supply in one direction first. Where Soft and Hard treat both halves alike and give odd harmonics, this gives even ones: measured against Soft at the same drive, the second harmonic is a hundred times larger. It is called Asymmetric rather than Bias because bias is the mechanism and this file already uses that word for the offset several of the characters have. The name on the bar says what comes out.

**Limiter** does not bend the wave at all. It turns the level down as the signal approaches the ceiling and lets it back up afterwards, so what you hear is the level moving rather than the shape, and it is the only one here a mastering engineer would recognise.

**Fold** turns the wave back on itself past the threshold instead of flattening it. Not protection that happens to colour but an effect that happens to be bounded: it makes far more harmonics than the others and moves them with the level, so it sings where the rest crunch. Done in closed form rather than by looping, so a sample arriving at a hundred costs what one at a half costs.

**The limiter looks ahead, and that is why the plugin reports latency.** Without it the gain only starts moving once the peak has arrived, so what actually held the signal down was the hard clip behind it: on the presets that drive it hardest, two seconds of audio reached 1.294 and had 1741 samples cut off. A limiter whose work is done by a clipper is a clipper.

Two milliseconds, and the whole gain path fits inside it. A causal box filter of B samples carries its output back by (B-1)/2, so a detector window of W followed by two boxes costs (W-1)+(B-1), which at W = B = half the delay comes to two samples less than the budget. One box would turn the gain step into a straight ramp with a corner at each end, and the second rounds those corners off, which is the difference between a gain that arrives in time and one that arrives in time without being heard doing it. There is no attack coefficient any more: how fast the gain can fall is set by how far ahead it can see.

| Lookahead | Peak before the backstop | Samples clipped | Gain movement |
| --------- | ------------------------ | --------------- | ------------- |
| none      | 1.294                    | 1741            | 0.0041        |
| 1 ms      | 0.980                    | 0               | 0.0040        |
| 2 ms      | 0.981                    | 0               | 0.0020        |
| 5 ms      | 0.983                    | 0               | 0.0007        |

Every size removes the clipping, so the choice was how gently the gain moves against how much latency to charge for it. Five milliseconds was the first thought and would have bought a curve two and a half times gentler still, which is below where it can be heard, for three more milliseconds. At two, an abrupt onset at twice full scale holds at 0.98, and the third harmonic the limiter adds falls from 0.00259 to 0.00012.

**Every shape pays that latency, and so does the stage switched off**, for the reason the bus drive already gives: one that reported a different figure depending on the patch would have the host re-plan its graph every time a preset was chosen. The two stages together come to 108 samples at 48 kHz, a little over two milliseconds.

**Settings can refuse it**, and that is the one thing allowed to move the figure, because a session setting is not something a preset or the bar can reach. It is worth having because only one of the five shapes uses the window: anyone who never reaches for the Limiter is paying two milliseconds for nothing. Off, the detector sees a single sample rather than a window, so the gain it applies is exact and immediate and the ceiling still holds with nothing for the clip behind it to do. What is lost is the smoothing, and a gain that steps is itself a distortion: the third harmonic goes from 0.00012 to 0.00185, about fifteen times. That is a rougher limiter rather than the one from before the lookahead, which overshot to 1.294 and left the clip to catch it.

The new figure is reported from the message thread, polled on the timer the processor already runs at 20 Hz rather than pushed from a parameter listener. A listener fires on whichever thread moved the parameter, and a host automating this one would move it from the audio thread, which is no place to tell a host its graph has changed. Hosts differ in how they take a latency change while running: most re-plan, some only notice on playback stop or on reload, so it is a setting to choose and leave rather than to ride. The hard clip stays behind the limiter, with nothing left to do, because the stage has to be bounded at a rate low enough that the window is a couple of samples.

### Learning a controller

Right-click a control, choose MIDI Learn, move something on the controller. The map is one parameter per controller number: a knob can be reached from more than one controller, and a controller only ever moves one knob. The other way round would need a list per controller and a rule for what happens when two knobs disagree about where it is standing.

**Where the lookup sits is the whole of the difficulty.** With MPE on, the parser is handed every message and only the mod wheel, a program change and all-sound-off fall through to the ordinary handler. A binding checked after that would be dead on exactly the controller this instrument is usually played from, so it is checked before. That is only safe because eleven numbers are refused rather than bound: 1 the mod wheel, 64 the sustain pedal, 74 the slide axis, 120 and 123 for stopping notes, and 6, 38, 98, 99, 100 and 101, which are how a keyboard declares its zones. Binding any of those would leave the instrument unable to be told what it is plugged into, and the slide is the one most easily taken by accident, since it moves whenever a finger does.

Refusing is silent in the useful sense: a reserved controller is neither learned nor consumed, so arming a knob and then reaching for the slide to steady a note leaves the note steadied and the knob still waiting.

**The table is atomic pointers rather than anything that locks**, since lookups happen on the audio thread as messages arrive and edits happen on the message thread as menus are chosen. The pointers are safe to hold because an AudioProcessor builds its parameters once in its constructor and they outlive everything in the map.

**Saved with the session, never with a patch.** Which controller moves which knob describes the desk rather than the sound. A binding records the parameter's id rather than its position, so a later build adding controls cannot repoint somebody's controller at something else, and an id that no longer exists is dropped rather than refused.

Two behaviours chosen rather than fallen into. A control jumps to where the controller is standing instead of picking the value up as it passes, because takeover needs somewhere to show that it is waiting and there is nowhere on a knob this size to show it. And the message that binds a controller does not also move the control: a controller is wherever it happens to be when you touch it, and jumping the knob there is a surprise at the exact moment somebody is watching it.

**A control knows which parameter it moves**, set where its attachment is made, which is the one place that already knows. That is hung on the component the way the look and feel's other flags are, so a right-click resolves outwards from whatever was hit: a click on a knob's caption lands on a label whose parent is the thing with the id.

**The waiting ring has a colour of its own**, `colours::learning`, because the three it might have borrowed are all saying something that can be true of the same control at the same moment: the accent is what LINK warms and what the draw tool lights, red is a cut channel and amber a soloed one. The window hangs the ring on its timer rather than being told to, since what ends the wait is a controller arriving on the audio thread, which is no place to repaint from.

Marking a control means marking whatever inside it the look and feel draws. A knob on the bar is a slider inside a frame that carries the caption, and the tag is on the frame so the caption is clickable, so the mark reaches inwards to the part with a paint method that reads it. The master fader needed its own line for the same kind of reason: it is drawn as a cap over the meter rather than as a track, through a branch that returns before the shared marker, and it rings its cap rather than its bounds, which would enclose the meter and read as marking that.

### Macros

Issue #24 asked for it, and asked for the right thing: a LINK drag across the tuning knobs moves 32 parameters and a host catches only the last one touched, so a relationship the instrument lets you edit by hand cannot be automated at all. A macro gives that relationship one control a host can draw.

**It offsets rather than writes**, which is the decision the whole feature turns on. A macro lays its amount over what the patch says on the way to the engine, so the host has one lane instead of thirty-two parameter changes per move, and the patch underneath is untouched. Writing would have reproduced the very problem the issue reports.

The visible consequence is that the knobs do not move when a macro does, which is normal for anything that works this way and surprising the first time. So the controls say both things at once: the tick ring lights to where the macro has taken the value in the macro's colour, and the pointer on the cap stays at what the patch holds in the channel's. The gap between them is the macro. At rest the two coincide, so an assigned but idle macro looks like no macro at all. A fader says it in the gutters either side of its track, which are the nearest thing a fader has to a knob's ring: the one part of it that is neither the groove, nor the meter, nor the cap. A driven fader grows two narrow columns of segments there and lights them from the foot of the track up to the level the macro has taken it to, exactly as a knob's ring lights from its anchor. The glass of the cap takes a wash of the same colour.

**They are the meter's own segments, narrower.** Same count, same pitch, same gaps, same rounding, and unlit in the macro's colour held down low the way the meter's unlit segments are the channel's. The fader and the meter are given one rectangle between them, which is what makes lining them up possible at all, and `meterSegments` is in `Theme.h` rather than on the meter so the two cannot disagree about how many there are.

Sharing the rectangle and the arithmetic was still not enough. **A look and feel is not handed the slider's rectangle**, it is handed the track, which JUCE has already inset by the thumb's radius: a few pixels shorter at each end. Laying the segments out across that gives the right number of them, each a little short, covering a little less of the strip than the meter does, which is a mismatch that looks like nothing in particular until it is put beside the thing it is meant to match. The gutters measure `slider.getLocalBounds()` instead, which is the rectangle the meter measures.

**An undriven fader has none of this.** It was a row of scale ticks, which is what the macro's level was first hung on, and once the macro had a proper column of its own the ticks were a third scale beside a meter and a cap that already say where the level is.

It used to be a bar straight across the groove and an edge round the cap, and both were wrong in the same way. The groove is the meter, so a line through the middle of it cuts a meter in half and belongs to neither thing. The edge sat right against the cap's own white one, two hard rings a pixel apart, and read as a sticker put on the cap rather than as the cap being driven. Glass is the one part of a fader that can take a colour without becoming a line.

The window works the result out and the snapshot works the offset out, from one shared piece of arithmetic, and a test holds the ring to the value the engine is playing on all 32 channels. A knob showing a value nothing is playing would be worse than a knob showing nothing, because it would be a lie told confidently.

**The panel is built out of the mixer it covers.** It is a dialog, the one this instrument has, and a dialog is where a window most easily stops looking like itself: a column of plain buttons and plain labels over a panel of lit caps and segment displays reads as a different program borrowing the window.

So its captions are set the way every caption over the mixer is, upper case at nine point bold in the dim grey. Its amount is a channel fader laid on its side, with its own row of segments rather than a meter's underneath it, lit from the middle out because the amount is signed and what it says is a distance. Its reading is a seven-bar display like every other number in the window, which is also why a positive amount shows no sign: seven bars cannot draw a plus that reads as anything but a speck beside the minus, which is the same reason a partial sharp of equal temperament has no sign either.

**Every element of that reading keeps its place.** Four cells and a point, with the sign sharing the first of them and a leading zero shown as a blank cell. Left to centre itself the reading walks left and right as the amount passes ten and a hundred, and shuffles along by half a cell the moment it goes negative, which is the one thing that gives a drawn display away: real cells do not move. A test renders the same number signed and unsigned and holds everything outside that first cell to being pixel for pixel identical.

It carries no unit. A per cent sign is not a thing seven bars can make, so it would have been drawn in text beside the digits and been the one mark in the box that is not a segment. The column is headed PERCENT instead, which is how the channel readouts leave their units to the gutter caption beside them.

The button that puts a macro back is square, like the colour swatch at the other end of the row. Both are a mark rather than a word, and the row is thirty pixels tall, so anything narrower read as a button that had been squeezed.

**A scope lights in the colour of the thing it reaches.** A scope naming an interval takes that interval's own colour, the one every channel of it is drawn in across the mixer, so the word and the channels it is about to move are the same colour. All, Odd and Even reach the whole series rather than one interval, so they take the accent, which is what the chrome uses for anything that is not one partial. The taper's anchor does the same with the channel it names, so the number and the strip agree without anyone having to count along the mixer to check.

That had to be done through `buttonOnColourId` rather than through the colour the button lights its word in. JUCE hands a look and feel `buttonOnColourId` for a button that is toggled, and `drawButtonFace` lets a background that has been named win over the lamp, which is the door the colour swatches already go through. Setting only the lamp leaves every one of them the scheme's own blue, which looks deliberate and is not.

**Eight is a pool, not a count.** A plugin declares its parameters once in its constructor and neither VST3 nor AU can add one later, so "make a macro" takes one of eight that already exist and "remove it" puts it back. A macro pointing its row at None is one nobody has made, which is why there is no second parameter saying whether it exists. Raising eight later disturbs nothing, since parameters added on the end leave every lane already written pointing where it pointed. Lowering it would strand automation.

**The offset moves through a control's travel rather than its units**, and the reason is a trap worth writing down. Several rows are built by `logRange`, which carries its curve in conversion functions and leaves `skew` at 1, so a range rebuilt from its two endpoints comes back linear and silently so. That is what made a quarter turn of a macro add five seconds to a decay: linear in seconds across a range spanning four orders of magnitude, so the whole negative half of the fader sat on the floor and the positive half leapt. Checking `skew` to find out whether a row is linear answers the wrong question.

**The amount is signed and spans the whole range.** Signed so a macro on LEVEL can duck a group rather than only lift it. The whole range because one macro reaches up to 32 controls which may be set anywhere, so there is no smaller fraction that would be right for all of them: scaling it down would buy resolution for a patch dialled mid-range by taking reach from every other one. The cost is that a patch already at an end of its range has a dead stretch of fader in that direction, which is the price and not a fault.

**Two of LINK's ideas did not survive the crossing.** Spread scatters each strip along the direction a drag gave it relative to the strip under the mouse, and a macro has neither, so it is not offered. Taper does translate, but it had to be told where to lean, since the strip under the mouse is what LINK measures from: a macro names an anchor channel instead, and a test holds its share to what `ui::linkCurveWeight` gives, so the two cannot drift into meaning different things by the same name.

Colour is a parameter rather than state because a preset file carries parameters and nothing else, so a patch that makes four macros has to be able to say what colour they are. Which macro owns a control is worked out by the window, because it is a fact about all eight and no strip can see past its own column, and pushed to the strips the way the LINK glow already is. The lowest-numbered macro reaching a control takes it: two colours mixed would usually name a third macro, and a control saying "more than one" says nothing about which.

### One tool rather than two switches

LINK and DRAW were never independent. A drag cannot be a link and a drawing at once, and the two switches had to work around that by showing LINK as off while drawing was armed without it being off, which the code called a lie the mouse-up would expose. One button choosing between Pointer, Link and Draw deletes the impossible state rather than papering over it, and the settings that belong to Link sit under the three in the same menu, so the tool and its behaviour are one click apart.

**The tool is derived from two flags rather than stored**, LINK's own switch and whether drawing is latched, so there is no fourth place for the three to disagree. Drawing wins, including when the modifier arms it rather than a choice. The cost of deriving it is that remembering the tool means remembering both flags, and that is a thing to forget: it was forgotten, and every path that moved either of them now goes through one function that writes both. Before that, choosing LINK wrote its switch through the settings callback and choosing anything afterwards wrote nothing, so a session said LINK for ever and every window opened on it.

The button wears the pointer it gives rather than a word. It drew the cursors themselves at first, which did not work: a cursor's artwork hangs down and to the right of its hotspot, so each tool landed at its own size in its own place. It is one arrow at one size now, with a mark standing to its right and centred on it. The mark for drawing is a contour rather than a pencil, because two pencils both read as a tick at twenty pixels, and because a contour is the truer picture of a tool that sweeps a shape across the series.

**SETTINGS and MACROS wear icons for a reason that is arithmetic rather than taste.** The words want 62 and 54 px, the group can spare 58 each, and every pixel taken there is half a pixel off each converter readout, which stop being able to name their kilohertz and their bits below 53. A cog and a rack of faders say the same thing in a third of the room, and the words are still what a screen reader is given and what the tooltip says.
