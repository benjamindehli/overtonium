Page: The Overtonium tuning system: TUNE, stretch and nine temperaments
URL: https://benjamindehli.github.io/overtonium/tuning/
Last changed: 2026-10-18

# The Overtonium tuning system

Three separate controls. TUNE sets how the overtone series is tuned, STRETCH sets how far it moves away from a true harmonic series, and the temperament sets how the keyboard itself is tuned. They work together and do not overlap.

## TUNE, from equal to just

A drawbar organ is the easiest way to explain this control, because it solves the same problem the other way round. Each drawbar is a harmonic, but an organ only has the twelve notes of the keyboard to build it from, so each drawbar is tuned to the nearest one. The second, fifth and eighth are all fifths an octave apart, so holding a C puts three G's into the sound. A harmonic series is not spaced like that. Its intervals are whole-number ratios, 2:1, 3:2, 4:3, and apart from the octaves none of them lands on a key. TUNE moves between those two tunings. It is a continuous knob, not a switch.

For harmonic `n`, the just interval above the fundamental is `1200 * log2(n)` cents. Overtonium rounds that to the nearest semitone to get the equal-tempered position. The remainder is the cent offset that the TUNE knob adds.

```
semitoneOffset(n, blend) = round(1200*log2(n)/100) + blend * (1200*log2(n) mod 100) / 100
```

At `blend = 1` this is exactly `n` times the fundamental, so the stack blends into one timbre. At `blend = 0` every partial is on a semitone of the chromatic scale and the same stack sounds like a chord. None of the values are stored as constants. Rounded to whole cents, the formula gives the familiar table, and the test suite checks this so it cannot change by accident.

The same patch on the same note, with only TUNE changed. First every partial at its exact whole-number ratio, then each one moved to the nearest semitone, then the knob turned slowly from one to the other.

**Just Saw**

**Equal Saw**

**The whole sweep**

[Diagram: Where each partial sits against the nearest semitone. Thirty-two rows, one per partial. The line down the middle is the tempered semitone that TUNE moves the partial to at one end of its range. Each dot is where that partial falls as a whole-number ratio. The knob moves the partial across the distance between them. Only the octaves, partials 1, 2, 4, 8, 16 and 32, sit exactly on the line. The table below gives the exact figures. -50 -25 the semitone +25 +50 n cents from the nearest semitone 1 2 3 4 5 6 7 -31 8 9 10 11 -49 12 13 +41 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32.]

Every partial compared with the semitone TUNE moves it to. The dot is the whole-number ratio and the line is the tempered position. The knob moves the partial across the distance between them. Only the octaves sit on the line.

This means that on channels 1, 2, 4, 8, 16 and 32 the knob does not change the sound. An octave is 1200 cents in both tunings, so the blend has nothing to move on those six, and their readouts stay at 0.0 wherever the knob is. Hovering over one of those strips tells you this. The channels are not stuck. Only that knob leaves them alone. STRETCH moves them, and so do DRIFT, the pitch modulator and the oscillator character, whose rack puts every partial except the first a few cents off its exact pitch.

| n   | Semitones | Cents | Interval      | n   | Semitones | Cents | Interval      |
| --- | --------- | ----- | ------------- | --- | --------- | ----- | ------------- |
| 1   | 0         | 0     | prime/octave  | 17  | 49        | +5    | minor second  |
| 2   | 12        | 0     | prime/octave  | 18  | 50        | +4    | major second  |
| 3   | 19        | +2    | fifth         | 19  | 51        | -2    | minor third   |
| 4   | 24        | 0     | prime/octave  | 20  | 52        | -14   | major third   |
| 5   | 28        | -14   | major third   | 21  | 53        | -29   | fourth        |
| 6   | 31        | +2    | fifth         | 22  | 54        | -49   | tritone       |
| 7   | 34        | -31   | minor seventh | 23  | 54        | +28   | tritone       |
| 8   | 36        | 0     | prime/octave  | 24  | 55        | +2    | fifth         |
| 9   | 38        | +4    | major second  | 25  | 56        | -27   | minor sixth   |
| 10  | 40        | -14   | major third   | 26  | 56        | +41   | minor sixth   |
| 11  | 42        | -49   | tritone       | 27  | 57        | +6    | major sixth   |
| 12  | 43        | +2    | fifth         | 28  | 58        | -31   | minor seventh |
| 13  | 44        | +41   | minor sixth   | 29  | 58        | +30   | minor seventh |
| 14  | 46        | -31   | minor seventh | 30  | 59        | -12   | major seventh |
| 15  | 47        | -12   | major seventh | 31  | 59        | +45   | major seventh |
| 16  | 48        | 0     | prime/octave  | 32  | 60        | 0     | prime/octave  |

The two large offsets are the ones you hear most. The seventh partial is 31 cents flat of a minor seventh, and the eleventh is 49 cents flat of a tritone, about halfway between two keys. These partials make a just-tuned stack sound unlike anything a keyboard can play.

In Norway the eleventh has a name. Its pitch, between the fourth and the tritone, is the natural fourth of Norwegian folk music. The willow flute plays it because it can only play the harmonic series. The composer Eivind Groven spent much of his life on it. From 1936 he built organs with up to three pitches for every key, chosen automatically as the music moved. This let an ordinary keyboard play pure triads in every key, with the eleventh partial exactly in tune. In Overtonium it is the eleventh row with TUNE at just, 551 cents above the octave below it. His way of keeping every chord pure on an ordinary keyboard is the [Adaptive](https://benjamindehli.github.io/overtonium/tuning/#adaptive) temperament further down.

## Stretch

Real instruments do not ring at exact whole-number multiples. A string with any stiffness has partials at `n * f0 * sqrt(1 + B * n^2)`. They are sharp of the harmonic, and more so further up the series. This, more than the hammer or the soundboard, is what makes a piano sound like a piano and not like a sawtooth. It is also why a tuner stretches the octaves. If the top of the instrument is tuned to the theory, it beats against its own overtones.

STRETCH sets that stiffness. It is set by how far it moves the top partial, not by the stiffness value itself, which runs from 0.00003 to 0.008 and is hard to read. At +150 cents, which is a real piano:

| Partial                 | 2    | 8     | 16    | 32     |
| ----------------------- | ---- | ----- | ----- | ------ |
| Cents sharp of harmonic | +0.6 | +10.2 | +40.0 | +150.0 |

The bottom of the series barely moves and the top moves a long way. Turn it further and the partials no longer share a fundamental, so the sound stops being a note with a timbre and becomes a bell. Negative values pull the partials inward. No real string does this, but it is still a useful sound.

It is separate from TUNE, and the two work together. Equal temperament with heavy stretch sounds different from just intonation with the same stretch. Zero is the plain harmonic series.

One note while STRETCH turns from a harmonic series to beyond a piano. The bottom of the series barely moves and the top moves a long way. Along the way the partials stop sharing a fundamental.

**STRETCH from 0 to 500 cents**

## Tuning the keyboard

Everything above is about where a partial sits above the note you play. This section is about how that note is tuned. Settings and the [Tuning bar](https://benjamindehli.github.io/overtonium/playing/#bars) have nine temperaments: eight fixed ones, and one that retunes as you play.

| Temperament                                                             | Major third | Fifth  | What it is                                                                      |
| ----------------------------------------------------------------------- | ----------- | ------ | ------------------------------------------------------------------------------- |
| Equal                                                                   | 400.00      | 700.00 | the reference everything else is measured against                               |
| Just (major)                                                            | 386.31      | 701.96 | pure by construction, chosen interval by interval                               |
| Pythagorean                                                             | 407.82      | 701.96 | every fifth pure, at the cost of a wide third                                   |
| Quarter-comma meantone                                                  | 386.31      | 696.58 | fifths narrowed a quarter comma, which makes the third pure                     |
| Werckmeister III                                                        | 390.22      | 696.09 | four fifths narrowed, no wolf, every key playable                               |
| Vallotti                                                                | 392.18      | 698.04 | six fifths, F-C to E-B, narrowed by a sixth of a comma, milder still            |
| Kirnberger III                                                          | 386.31      | 696.58 | four fifths narrowed a quarter comma, so the third on the root is pure, no wolf |
| Young                                                                   | 392.18      | 698.04 | Vallotti's six fifths a fifth higher, C-G to B-F#                               |
| [Adaptive](https://benjamindehli.github.io/overtonium/tuning/#adaptive) | 386.31      | 701.71 | pure in every key, the held chord retuned as you play                           |

They are worked out from the circle of fifths, not copied from tables of cents. Almost every historical temperament is described by how much each of the twelve fifths is narrowed. The code stores that, and the pitch classes are calculated from it. The two commas everything is built from come out at 23.460 and 21.506 cents, which match the published figures. The tests check the property that defines each temperament, not the resulting numbers: meantone's third pure to a thousandth of a cent, Pythagorean's fifth the same, Werckmeister's third at 390.2, and Kirnberger III's third pure.

Vallotti and Young narrow the same six fifths, one starting a fifth lower than the other. So Vallotti on C is Young on F, note for note. Both are in the list because players know them by both names.

### Adaptive

Every fixed temperament is in tune in some keys and out of tune in others. Just major is pure in the keys near its root, and a chord in a key further away is a comma out. This is the oldest problem in tuning. Adaptive solves it the way Eivind Groven's organs did. It gives every key more than one pitch and chooses between them as you play. Every pitch class has five versions: its place in just major on the root, and one and two commas either side of it. The chord you hold uses whichever versions make it purest.

The chord is retuned every time a key goes down or comes up. A chord that changes into the next one a note at a time stays in tune all the way, as well as a chord struck all at once. Keys struck together are tuned together. If the new chord needs a held note at a different pitch, the note slides there over the time set by the **Retune** knob in the Tuning bar, from an instant jump at zero up to a second. At 30 ms, where the knob starts, the note sounds as if it was always there, and at around 300 ms you can hear the chord settle. Settings offers those two as _Fast_ and _Smooth_. Adaptive moves as few notes as it can, and new notes take whichever version fits the notes already held. The keys you hold decide how the chord is tuned, not the order you pressed them in. A chord struck at once, played a key at a time or reached from another chord comes out the same, interval for interval. Every chord type in [music-utils](https://github.com/benjamindehli/music-utils), my library for naming chords from the notes held, is played through it in all twelve keys on every build. That is 125 of them, from a bare third to an altered thirteenth. The test checks that each chord comes out the same every way, and that it is tuned as purely as it can be.

It tunes against everything you can hear: the keys that are down and the ones the pedal is holding. When the held keys allow more than one version, a chord still ringing after its keys came up decides between them. A key struck into silence takes the version its pitch class used last. This keeps a progression from wandering away a comma at a time. No key can go further than its outer versions, so the keyboard never drifts more than two commas from where it started, however long you play. If every chord were tuned pure to the last one with no limit, a C major, A minor, D minor, G progression played four times would end up over two and a half semitones sharp. If you want no drift at all, turn on the **NO DRIFT** switch in the Tuning bar, or No drift in Settings. It keeps the chord as close to home as it can. When a progression would carry it away, a held note slides a comma instead, and a key struck into silence goes back home.

Like Groven's organs, it narrows each fifth by an eighth of a schisma, a quarter of a cent, so that eight fifths and a pure major third meet exactly. Without this, chords far from the root would be two cents out, which on pure partials gives slow but clearly audible beating. The major thirds are pure. A quarter of a cent on a fifth beats about once every eighteen seconds on the third partial of a note in the middle of the keyboard. The same chord reached by a different route can end up a comma higher or lower, with every interval in it the same, so a piece can end a comma or two from where it began. Root sets where the middle versions sit, and Reference pitch sets where A's middle version sits.

Some chords cannot be pure all the way through, because pure fifths and pure thirds never quite fit together. In C11 the fifth from G to D and the minor third from D to F cannot both be pure. Adaptive then gives up as little as it can. It ranks a fifth or a fourth above a third or a sixth, and those above a seventh. Counted that way, C6 with an added ninth keeps its thirds and sixths and lets the fourth from A to D go a comma wide. Keeping that fourth would cost three thirds.

A minor seventh is tuned to the harmonic seventh, 7:4 above its root, 969 cents, unless that would put it out of tune with another held note. A dominant seventh becomes 4:5:6:7. These are four harmonics of one note an octave below its root, the same seventh that every partial here already has as its seventh harmonic, and the smoothest a seventh chord can be. It is noticeably lower than the equal-tempered seventh, and that is most of why it sounds so settled. A ninth adds the ninth harmonic. A seventh over its root alone or over its fifth is 7:4 as well. With a minor third above the root, the seventh stays at 9:5, because 7:4 would put it 48 cents flat of a pure fifth above that third. A sharp ninth is held back the same way, for the same reason. A suspended seventh keeps 16:9, two pure fourths. With a minor second or a tritone above the root, the seventh also keeps the size the chain gives it. A major seventh is 15:8. Like everything else here, it depends only on the keys held. C and B flat are 7:4 however they were played, and a held note slides to or away from the harmonic seventh when the chord around it changes type. The **7:4** switch in the Tuning bar, and Harmonic sevenths in Settings, turn this off. Every seventh then takes the size the chain gives it.

The [Tuning bar](https://benjamindehli.github.io/overtonium/playing/#bars) shows how Adaptive is tuning the chord you hold, and updates as each key goes down and comes up. **PURE** lights while every interval in the chord is pure. It goes dark when a chord cannot be pure all the way through and one interval has been given up. **7:4** lights while a seventh is tuned to the harmonic seventh, and **DRIFT** shows how far the keyboard has drifted, in commas. Next to those, there is one display for each of the twelve notes, showing how far it is from equal temperament in cents. The notes you hold are lit, and the rest stay unlit until you play them. Under every other temperament the same twelve displays show the whole scale, all of them lit. So the Tuning bar shows what Werckmeister III or Just on D does to each note before you play it.

[Image: The Tuning bar under Adaptive with C7 held: the temperament reading Adaptive on a root of C at 440 Hz with a drift of 0, NO DRIFT off, 7:4 lit, PURE lit and the Retune knob near its start, then twelve displays for the notes, C, E, G and B flat lit at 15.4, 1.7, 17.1 and minus 15.8 cents, the white keys on a light ground.]

C7 under Adaptive. C is 15.4 cents above equal, where just major on C puts it with A at 440. E and G are a pure third and fifth above it, and B flat is the harmonic seventh, 31.2 cents below C's equal-tempered seventh.

### Root

Sets which pitch class the temperament is built on. An unequal temperament is only in tune in the keys near its centre, so you choose that centre here. For Adaptive it sets where the middle versions are placed. It is greyed out for equal temperament, which has no centre, unless the [resonator](https://benjamindehli.github.io/overtonium/effects/#resonator) is in Series, which is built on the root in every temperament.

On a root of C every temperament is laid out the way organs were tuned. Pythagorean and quarter-comma meantone each have one fifth that cannot be used, the wolf, and on C it is G#-Eb. That keeps C, F, Bb, G, D and A major in tune. On another root the wolf moves with it, a minor sixth above the root.

### Reference pitch

One of eleven pitches for A: 415, 430, 432, 435, 438, 440, 442, 443, 444, 446 or 466 Hz. A stays exactly at this pitch in every fixed temperament, because the offsets are measured relative to A, the same way a tuner tunes A first and works outwards. Only the other eleven notes move. Under Adaptive, A's middle version sits on the reference, and an A in a chord that needs it takes the version a comma either side.

The [resonator](https://benjamindehli.github.io/overtonium/effects/#resonator)'s strings are tuned the same way, so they follow the temperament, the root and the reference pitch. Its Series mode is built on the root's own harmonics.

None of the three is saved with a [preset](https://benjamindehli.github.io/overtonium/presets/). A temperament belongs to the music you are playing, not to any one sound in it. You set it once and keep working, and a patch that reset it to equal while you worked would get in the way. This includes Init, which clears the patch but not the session.

One C major chord, twice, on a temperament built on C. Equal puts the third at 400 cents and Werckmeister III at 390, which is most of the way to the pure 386. The beating between the third and the root slows almost to a stop.

**Equal**

**Werckmeister III**

## Tracking

Without TRACK, a patch has the same spectrum at every pitch. Real instruments do not. Play up a piano and the top of the series thins out. This happens because the body has a rolloff at a fixed frequency, and the partials climb up through it as the notes get higher.

TRACK sets that rolloff, in dB per octave above a fixed corner of 1 kHz, which is around C6. It is measured relative to the fundamental, not as an absolute level. An absolute rolloff would act as a shelf and make high notes quieter instead of duller. Measured this way, the fundamental keeps its level at every pitch and only the spectrum above it thins.

The effect depends on where the corner sits. A bass note has most of its series below 1 kHz and keeps nearly all of it. A treble note whose fundamental is already above the corner gets the full slope on every partial. At 6 dB per octave, the 32nd partial keeps 57% of its level at A1 and 4% at A5. Zero is off, and off changes nothing.

The same five notes an octave apart, twice. With TRACK off the spectrum is the same at every pitch. With it on, the bass note keeps nearly all of its series, because most of it sits below the corner, and most of the top note's series is in the rolloff. Measured on these two files, the partials above the fundamental add up to 1.43 times the fundamental at C2 in both. At C6 they add up to 1.45 with tracking off and 0.49 with it on.

**TRACK off**

**TRACK at 6 dB/oct**

## Drift, the fourth kind of detuning

DRIFT works per channel, not globally, and it is random, not periodic. It moves a partial's pitch in a smooth random path of up to 25 cents that keeps changing as the note plays. This is the cassette warble that the earlier instruments in this line got from tape. Every partial has its own random stream, so a stack with drift never quite settles, and no two notes drift the same way.

Drift is why a held chord keeps moving with no modulation set. It is also why the pitch lamp on the PITCH MOD rule shows a needle and not a pulse. The needle shows where modulation and drift together put the partial at that moment.

One held chord, twice. With drift off, each partial stays exactly at its pitch. With it on, every partial moves on its own and the stack never settles. Measured across these files, the eighth partial stays at 0 cents in the first and moves over 12 cents in the second.

**DRIFT off**

**DRIFT at 18 cents**
