Page: Playing Overtonium: MPE, automation, macros and MIDI Learn
URL: https://benjamindehli.github.io/overtonium/playing/
Last changed: 2026-10-18

# Playing Overtonium

How to play the instrument and control it from outside the window: MPE, legato and glide, the Playing and Tuning bars, the Settings menu, automation from a host with every parameter listed, macros that move a whole row from one control, and MIDI Learn for playing it from a controller.

## MPE

Off by default. It is in Settings and in the Playing bar. With MPE on, a controller that gives every note its own MIDI channel can bend and press each note separately. On this instrument, that means each finger gets its own copy of all 33 aftertouch destinations. Press into one note in a chord and only that note's upper partials come in.

Slide, the forward and back movement of a finger on the key, has its own destination setting in Settings and in the Playing bar. By default it changes that note's brightness, thinning or opening up the top of the series, which is what players expect from that axis. It can control the tuning instead, so that pushing one finger forward moves that note alone [between equal temperament and just intonation](https://benjamindehli.github.io/overtonium/tuning/) while the rest of the chord stays where it is. A keyboard without a slide axis sends nothing, so nothing changes.

Slide is read as movement, not as a position. The first value a note receives becomes that note's zero, and what affects the sound is how far the axis has moved from it since. This is because controllers handle the axis differently. A Seaboard reports where the finger is and rests at the centre. An Expressive E Osmose uses the first part of the key travel for pressure and only then starts sending slide, from the bottom of its range upward. Read as a position, the Osmose would jump the moment the axis starts sending. Read as movement, both start the note as the patch sets it and follow the finger from there.

The movement is scaled so that the longer of the two directions from the resting point covers the full slide, and both directions use that same rate. A resting point near the bottom of the axis leaves very little room below it. Giving that small range its own scale would make it far too sensitive: pressing in would feel smooth, but coming back out would jump straight to dark. So the short side has the same sensitivity as the long side and reaches less far.

MPE is a setting, not always on, because it changes what a channel number means. With it off, the channel is ignored: a key-up on channel 7 releases a note that started on channel 1, which is what a single-channel keyboard needs. With it on, the channel is part of the note's identity, so the same key can be held on two channels at once and bent in two directions, as two voices instead of one retriggering the other.

## Legato

A switch in Settings and in the Playing bar, beside the voice count. What it does depends on that count. **With one voice** the instrument is monophonic. Pressing a key while another is still held moves the note instead of starting it again, so the envelopes continue and a run keeps the shape the first key gave it, without restarting the attack. Releasing a key while others are still held goes back to the most recent of them, which makes trills work. Only releasing the last key releases the note.

**With more than one voice it plays a top line.** A key pressed above every held note except the highest one continues that highest note's phrase. It starts its own voice, which takes over the highest note's envelopes at their current point and glides from its pitch, so the sound does not start again. Every other key plays as normal. This means you can play a legato melody over a staccato chord. The chord retriggers each time its keys are released and pressed again, and the melody carries on from note to note above it, up or down, as long as it stays above the chord.

A legato melody over chords played below it, two notes to a bar, one with the chord and one in between. It is played softly on Synth Ensemble, where each new note takes about half a second to swell in. With Legato off, every note of the melody swells in again. With it on and eight voices, the melody carries on from note to note, with or without the chord, while the chords still swell in underneath.

**Legato off**

**Legato on**

The note a melody carries on from is never cut off, so a key that turns out to be part of the chord always sounds. It keeps sounding while its key is down and fades quickly when the key is released, instead of playing its full release under the melody. The same goes for the upper note of a trill when you return to the lower key.

A key pressed below the highest note could be the melody stepping down, or a chord played under a held melody. Only what the previous melody key does next would tell them apart, so the instrument goes by what is most likely. Above the highest note, a key always carries on. Below it, with a chord held and no other key just struck, it carries on if it is above that chord, however far down it goes. Alone under a single melody note, or among keys struck together, it carries on only if it is within a major third and is the highest of the keys struck with it. A melody usually moves by seconds and thirds, and the top of a chord usually sits further down. A wrong guess costs no more than a new attack. No key is ever delayed.

When the instrument runs out of voices, the melody is the last voice taken. The sustain pedal holds the sound but not the melody, so a note held only by the pedal does not carry anything on. With MPE on it works the same way, and a note carried onto a key on its own channel takes on that key's bend, pressure and slide.

Legato moves the note in a single step. With a [glide](https://benjamindehli.github.io/overtonium/playing/#glide) time on the channels, it slides there instead.

## Glide

**Every channel has its own glide**, the last knob under PITCH MOD. It sets how long that partial takes to move from the previous note to the new one, from Off, the default on every channel, to five seconds. Because each partial has its own time, the partials do not have to arrive together. Short times on the upper partials and a long one on the fundamental bring the brightness in first, with the pitch following. The other way round, the pitch arrives first and the tone follows. With settings in between, the partials stop agreeing on a fundamental while they move and settle on one as they arrive. A single portamento cannot do this.

The same phrase twice on the [Glide](https://benjamindehli.github.io/overtonium/presets/) preset: up a fifth, up a fourth and down the octave. With every glide at zero, each note arrives straight away. With the preset's settings, from nothing on the fundamental to two and a half seconds on the thirty-second partial, the pitch arrives at once and you can hear the upper partials still moving.

**Glide off**

**Glide on**

A partial glides in a straight line in pitch, and everything that depends on pitch follows the glide. The oscillator character is chosen for the frequency the partial has reached, and the Bulb character's lamp lags behind a long slide as a real lamp would. Vibrato, drift and both kinds of bend are added on top. The needle on each partial's PITCH MOD rule follows the glide and swings back to centre as the partial arrives, so with different glide times you can watch the partials arrive one at a time.

**Two switches control every channel at once.** They are the two letters **L** and **T** beside the GLIDE caption on the left of the mixer. They are also at the end of the menu that opens when you right-click any GLIDE knob, under _Glide (every channel)_. A letter is lit when its switch is on. The switches are part of the patch, so a preset saves them and a legato lead can set them. The letters show their state as soon as a preset loads.

| Switch                 | Choices                                        | What it decides                                                                                                                                                                                                                                                                                                                    |
| ---------------------- | ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| When, L                | Always, the default, or Legato (L lit)         | Always glides from the last note played, even if its key was released long ago. Legato glides only when the new key is pressed while another key is still held, so a phrase played detached jumps to each note and the same phrase played legato slides between them. A note held only by the sustain pedal does not count as held |
| What the time means, T | Fixed rate, the default, or Fixed time (T lit) | Fixed rate makes the time the length of a slide over one octave, so a fifth takes a little over half of it and two octaves take twice as long, like a finger sliding along a string at a steady speed. Fixed time makes every slide take the set time however far it goes, so a leap and a step arrive at the same moment          |

Played polyphonically, each new note glides from the last note played. In [Legato](https://benjamindehli.github.io/overtonium/playing/#legato), the voice carrying the phrase glides from wherever it is: the single voice when one voice is set, and the top line when more are set. A key pressed halfway through a slide reverses it from the current point instead of jumping to either end. A trill that returns to a key still held also glides back to it.

GLIDE is a row like any other, so [LINK](https://benjamindehli.github.io/overtonium/controls/#link) sets it across the series in one drag and a [macro](https://benjamindehli.github.io/overtonium/playing/#macros) can change every channel's glide time from one control.

## The Playing and Tuning bars

The lower two of the four buttons at the left end of the top bar, a keyboard and a tuning fork, each open a bar between the top bar and the mixer. The bars hold the settings you change while playing, as opposed to the ones you set once. Both bars start closed, and each instance remembers which are open. A bar takes its height from the mixer, which scrolls, so the window stays the same size. If the window is already at its minimum height, it grows by the height of the bar.

**Playing** has the voice count with IN USE, which shows how many voices are sounding, release tails included. Beside them are Legato and No stack, the pitch bend range, and MPE with Aftertouch from and Slide to. After these it shows what your hands are doing. SUSTAIN lights while the pedal is down. PITCH shows the pitch wheel's position from the centre against the bend range, or with MPE on, the key bent furthest. WHEEL and PRESSURE fill from the left. PRESSURE shows the keyboard's own pressure, from the channel or from the key pressed hardest, so the wheel moves only WHEEL even when Aftertouch from follows it. Last are the eight [macros](https://benjamindehli.github.io/overtonium/playing/#macros) as knobs in their colours. Each is named after the row it controls and turns by the same amount as its slider in MACROS. A macro not yet made shows a blank knob with its number. Right-click a knob to assign its macro, to edit it in MACROS, or to bind a controller to it. **Tuning** has the temperament, root and reference pitch, and under Adaptive, the drift beside them. Under Adaptive it also has a NO DRIFT switch that keeps the keyboard home, a 7:4 switch for the harmonic seventh, a PURE lamp and the Retune knob. Together they show [what Adaptive is doing](https://benjamindehli.github.io/overtonium/tuning/#adaptive) to the chord you hold. Last are twelve displays, one for each note, showing how far it is from equal temperament in cents, with the white keys on a lighter background as on a keyboard. Each setting's display opens the same list as its entry in Settings, and each switch lights while it is on, so a bar is another way to reach the same settings, not a separate copy of them. If the window is too narrow for a whole bar, the bar wraps onto more rows like the top bar does, and takes the extra height from the mixer.

[Image: The Playing bar with C7 held on the pedal: eight voices with four in use, Legato off and No stack lit, a bend range of 2 semitones, MPE off with aftertouch from Either and slide to Brightness, SUSTAIN lit, the pitch wheel at rest in the middle, the mod wheel and pressure part way up, and three macros made, Level in red, Pitch mod depth in green and Release in blue, followed by five blank knobs numbered 4 to 8.] [Image: The Tuning bar under Adaptive with C7 held: the temperament reading Adaptive on a root of C at 440 Hz with a drift of 0, NO DRIFT off, 7:4 lit, PURE lit and the Retune knob near its start, then twelve displays for the notes, C, E, G and B flat lit at 15.4, 1.7, 17.1 and minus 15.8 cents, the white keys on a light ground.]

## The Settings menu

Settings you set once and then leave. The menu is where you set up the instrument, and the panel is where you make the sound, so where a control sits tells you which kind it is. The ones you change while playing are also in [the Playing and Tuning bars](https://benjamindehli.github.io/overtonium/playing/#bars).

| Entry                              | Choices                                                | What it decides                                                                                                                                                                                                                                                                                                                                                                 |
| ---------------------------------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Overtonium X is available...       |                                                        | Only there when a newer version is out, above the version. It opens the release on GitHub, and SETTINGS stays lit until you update                                                                                                                                                                                                                                              |
| Which version this is              |                                                        | A heading near the top of the menu, not an entry, because it describes the build and is not a choice. A bug report asks for this number                                                                                                                                                                                                                                         |
| Check for new versions             | off by default                                         | The only entry that connects to anything outside your computer. [What it sends](https://benjamindehli.github.io/overtonium/install/) is only a request for a file                                                                                                                                                                                                               |
| Undo, Redo                         |                                                        | The same steps as Cmd-Z, for hosts that keep that shortcut for themselves                                                                                                                                                                                                                                                                                                       |
| Polyphony                          | 1, 2, 4, 6, 8, 12 or 16 voices. 8 by default           | How many notes can sound at once                                                                                                                                                                                                                                                                                                                                                |
| No stack                           | on by default                                          | Playing a key again takes over the note it is already sounding instead of adding a second copy, as a string or a bar does. When off, every strike gets its own voice and the tails add up, so a bell struck repeatedly builds up                                                                                                                                                |
| Legato                             | off by default                                         | With one voice, a key pressed while another is held moves the note without starting it again. With more voices, the highest held note does this for a key above the rest, and everything below it plays as normal. See [Legato](https://benjamindehli.github.io/overtonium/playing/#legato)                                                                                     |
| Pitch bend range                   | 0, 1, 2, 3, 4, 5, 7, 12 or 24 semitones. 2 by default  | How far the wheel bends. Only the commonly used ranges are offered, not all twenty-five                                                                                                                                                                                                                                                                                         |
| MPE                                | off by default                                         | Whether a note's channel is part of its identity, which lets pitch bend and pressure work per note                                                                                                                                                                                                                                                                              |
| Aftertouch from                    | Channel pressure, Mod wheel, Either. Either by default | What drives the AT row on every strip. Polyphonic aftertouch is not on the list because it is always routed, whatever this is set to                                                                                                                                                                                                                                            |
| Slide to                           | Off, Brightness, Tuning. Brightness by default         | What the forward and back movement of an MPE finger controls                                                                                                                                                                                                                                                                                                                    |
| Temperament, Root, Reference pitch | Nine temperaments, twelve roots, 415 to 466 Hz         | What the keyboard is tuned to, as explained in [the tuning system](https://benjamindehli.github.io/overtonium/tuning/). Root is greyed out for equal temperament, which has no centre                                                                                                                                                                                           |
| Retune                             | Fast, 30 ms, the default, or Smooth, 300 ms            | How long a held note takes to slide to its new tuning when the [Adaptive](https://benjamindehli.github.io/overtonium/tuning/#adaptive) temperament retunes the chord around it. These are two quick settings for the Retune knob in the Tuning bar, which runs from an instant jump at zero up to one second. Greyed out for every other temperament                            |
| Harmonic sevenths                  | on by default                                          | Whether Adaptive tunes a minor seventh 7:4 above its root, the harmonic seventh, when nothing else in the chord would beat against it. When off, every seventh has the size the chain of fifths gives it. Also the 7:4 switch in the Tuning bar. Greyed out for every other temperament                                                                                         |
| No drift                           | off by default                                         | Whether Adaptive keeps the chord you hold as close to home as it can. When on, a progression that would shift the keyboard by a comma moves a held note by a comma instead, and a key played after silence goes home. When off, the keyboard follows the music, up to two commas either way. Also the NO DRIFT switch in the Tuning bar. Greyed out for every other temperament |
| Phase reset                        | on by default                                          | When a note begins from silence, starts each partial at the point its ATTACK sets: a rising zero crossing, or nearer its peak when the attack is turned below its shortest time. When off, a partial carries on from wherever its cycle was                                                                                                                                     |
| Safety clip                        | on by default                                          | Whether the output stage at the end of the chain is on. Best left on when you push 32 faders up. Which of its five shapes it uses is chosen on the bar, under the meter                                                                                                                                                                                                         |
| Limiter lookahead                  | off by default                                         | Whether the Limiter can look ahead at the incoming signal. On makes it smoother but adds two milliseconds of latency on every shape. Only the Limiter uses the lookahead, but the latency must not change with the patch. The only setting here that changes what the plugin reports to your host                                                                               |
| Zoom                               | 75, 100, 125 or 150%                                   | How big the window is on your screen. It sits at the bottom with the entry below it, because everything above these two is about the instrument and these are not                                                                                                                                                                                                               |
| Fit all 32 channels                |                                                        | Resets the window to the width that shows the whole mixer, and to the height your unfolded rows need. A window remembers the size you dragged it to, so a narrow window opens narrow in every later session. This entry resets it                                                                                                                                               |

Almost none of these settings are saved in a [preset](https://benjamindehli.github.io/overtonium/presets/). This is a rule, not a list that might get out of date: how you play the instrument and what it is tuned to belong to the session, not to any one sound in it. For the same reason, presets do not save the zoom, the window size or which groups of rows you have folded away.

The safety clip is the exception in this table, and the master fader is the exception outside it. The output stage has five shapes to choose from, and the master fader sets how hard the signal drives into it, so how hard a patch drives that stage is part of the patch, not of your setup. As a result, **loading a preset changes your output level**.

## Automating it from the host

Eight hundred and fifty parameters reach the host: forty-four global ones, twenty-three on each of the 32 partials, twenty-two on the noise channel, and six for each of the eight macros. They are listed in the same order as the panel, with the globals first, then each channel in turn, and the noise channel last.

Every per-channel name includes its channel, so you can search a parameter list either way. Type `H7` to find that partial, or type `Tune` to find that row across the whole mixer. A screen reader reads the same names, which is why they are words, not numbers.

| What you are after              | What the host calls it |
| ------------------------------- | ---------------------- |
| The master fader                | `Master`               |
| The seventh partial's tuning    | `H7 Tune`              |
| The eleventh partial's fader    | `H11 Level`            |
| The noise channel's brightness  | `Noise Colour`         |
| How long the echo's repeats are | `Echo Time`            |

The noise channel has twenty-two instead of twenty-three because it has no tuning, pitch modulation, drift or glide, and has COLOUR and the four knobs of its string instead.

Everything in Settings is in the list too, even though no preset changes it. The temperament, the polyphony and the bend range can all be automated, so a host can change the temperament partway through a piece.

**Each parameter is identified by its own id, not by its position**, in the Audio Unit and in the VST3. This lets a release add controls without breaking automation you have already written. 1.6.0 added 65 new parameters among the existing ones, not after them, and every automation lane written before it still points at the same control.

LINK is not a parameter, because it is a mouse tool, not part of the sound. A LINK drag starts a gesture on every channel it reaches, writes each one, and ends them all together, so the host records what actually moved: thirty-two lanes if the drag moved thirty-two channels.

### Every parameter

All 850 as the host lists them, with a control that every partial or every macro carries given one row for all of them. The range and the default are what the plugin itself shows. The same list, one parameter per line with its id, is [parameters.json](https://benjamindehli.github.io/overtonium/parameters.json), written by the plugin rather than by hand.

**Global**, forty-four of them.

| What the host calls it       | Id                | Range                                                                                                                              | Default    |
| ---------------------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| `Master`                     | `masterGain`      | -60.0 to 12.0 dB                                                                                                                   | -12.0 dB   |
| `Polyphony`                  | `polyphony`       | Legato, 1 voice, 2 voices, 4 voices, 6 voices, 8 voices, 12 voices, 16 voices                                                      | 8 voices   |
| `No Stack`                   | `oneVoicePerKey`  | Off or on                                                                                                                          | On         |
| `Pitch Bend Range`           | `bendRange`       | 0 to 24                                                                                                                            | 2          |
| `Phase Reset`                | `phaseReset`      | Off or on                                                                                                                          | On         |
| `Safety Clip`                | `safetyClip`      | Off or on                                                                                                                          | On         |
| `Lookahead`                  | `lookahead`       | Off or on                                                                                                                          | Off        |
| `MPE`                        | `mpe`             | Off or on                                                                                                                          | Off        |
| `Stretch`                    | `stretch`         | -1200 ct to +1200 ct                                                                                                               | Harmonic   |
| `Tracking`                   | `track`           | Off to 12.0 dB/oct                                                                                                                 | Off        |
| `Character`                  | `character`       | Pure, Bulb, Rail, Diode, Valve, Op-amp                                                                                             | Pure       |
| `Wobble`                     | `wobble`          | 0 % to 100 %                                                                                                                       | 0 %        |
| `Temperament`                | `temperament`     | Equal, Just (major), Pythagorean, Quarter-comma meantone, Werckmeister III, Young, Adaptive, Kirnberger III, Vallotti              | Equal      |
| `Tuning Root`                | `tuningRoot`      | C, C#, D, D#, E, F, F#, G, G#, A, A#, B                                                                                            | C          |
| `Reference Pitch`            | `referenceHz`     | A = 415 Hz, A = 430 Hz, A = 432 Hz, A = 435 Hz, A = 438 Hz, A = 440 Hz, A = 442 Hz, A = 443 Hz, A = 444 Hz, A = 446 Hz, A = 466 Hz | A = 440 Hz |
| `Aftertouch From`            | `atSource`        | Channel pressure, Mod wheel, Either                                                                                                | Either     |
| `Slide To`                   | `slideDest`       | Off, Brightness, Tuning                                                                                                            | Brightness |
| `Sample Rate`                | `lofiRate`        | Host, 32 kHz, 22.050 kHz, 16 kHz, 11.025 kHz, 8 kHz, 6 kHz, 4 kHz                                                                  | Host       |
| `Bit Depth`                  | `lofiBits`        | Host, 16 bit, 12 bit, 10 bit, 8 bit, 6 bit, 4 bit, 3 bit, 2 bit                                                                    | Host       |
| `Echo`                       | `echoOn`          | Off or on                                                                                                                          | Off        |
| `Echo Mix`                   | `echoMix`         | 0 % to 100 %                                                                                                                       | 25 %       |
| `Echo Time`                  | `echoTime`        | 20.0 ms to 2.00 s                                                                                                                  | 350 ms     |
| `Echo Feedback`              | `echoFeedback`    | 0 % to 95 %                                                                                                                        | 35 %       |
| `Echo Age`                   | `echoAge`         | 0 % to 100 %                                                                                                                       | 29 %       |
| `Reverb`                     | `reverbOn`        | Off or on                                                                                                                          | Off        |
| `Reverb Mix`                 | `reverbMix`       | 0 % to 100 %                                                                                                                       | 25 %       |
| `Reverb Decay`               | `reverbDecay`     | 200 ms to 20.00 s                                                                                                                  | 2.00 s     |
| `Reverb Damping`             | `reverbDamp`      | 0 % to 100 %                                                                                                                       | 50 %       |
| `Reverb Pre-delay`           | `reverbPreDelay`  | 0.0 ms to 250 ms                                                                                                                   | 0.0 ms     |
| `Pitch Mod Shared`           | `pmInPhase`       | Off or on                                                                                                                          | Off        |
| `Amp Mod Shared`             | `amInPhase`       | Off or on                                                                                                                          | Off        |
| `Echo Type`                  | `echoType`        | Tape, Bucket brigade, Digital                                                                                                      | Tape       |
| `Clip Type`                  | `clipType`        | Soft, Hard, Asymmetric, Limiter, Fold                                                                                              | Soft       |
| `Reverb Type`                | `reverbType`      | Room, Modulated Plate, Spring                                                                                                      | Room       |
| `Glide Trigger`              | `glideTrigger`    | Always, Legato                                                                                                                     | Always     |
| `Glide Mode`                 | `glideMode`       | Fixed rate, Fixed time                                                                                                             | Fixed rate |
| `Legato`                     | `legato`          | Off or on                                                                                                                          | Off        |
| `Adaptive Retune`            | `adaptiveRetune`  | 0.0 ms to 1.00 s                                                                                                                   | 30.0 ms    |
| `Adaptive Harmonic Sevenths` | `adaptiveSeventh` | Off or on                                                                                                                          | On         |
| `Adaptive No Drift`          | `adaptiveHome`    | Off or on                                                                                                                          | Off        |
| `Resonator Mode`             | `resonatorMode`   | Off, Piano, Open, Octave, Series                                                                                                   | Off        |
| `Resonator Mix`              | `resonatorMix`    | 0 % to 100 %                                                                                                                       | 50 %       |
| `Resonator Decay`            | `resonatorDecay`  | 200 ms to 10.00 s                                                                                                                  | 4.00 s     |
| `Resonator Damp`             | `resonatorDamp`   | 0 % to 100 %                                                                                                                       | 40 %       |

**On every partial**, twenty-three controls, each one on all 32 partials.

| What the host calls it                      | Id                                 | Range                                                                                  | Default                                            |
| ------------------------------------------- | ---------------------------------- | -------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `H1 Tune to H32 Tune`                       | `h01_tune to h32_tune`             | Equal temp. to Just                                                                    | Just                                               |
| `H1 Pitch Mod Rate to H32 Pitch Mod Rate`   | `h01_pmRate to h32_pmRate`         | 0.01 Hz to 30.0 Hz                                                                     | 4.00 Hz                                            |
| `H1 Pitch Mod Depth to H32 Pitch Mod Depth` | `h01_pmDepth to h32_pmDepth`       | 0 ct to 1200 ct                                                                        | 0 ct                                               |
| `H1 Pitch Mod Shape to H32 Pitch Mod Shape` | `h01_pmShape to h32_pmShape`       | Sine, Triangle, Sawtooth, Reverse Sawtooth, Square, Square (up), Sample & Hold, Random | Sine                                               |
| `H1 Glide to H32 Glide`                     | `h01_glide to h32_glide`           | Off to 5.00 s                                                                          | Off                                                |
| `H1 Drift to H32 Drift`                     | `h01_drift to h32_drift`           | 0 ct to 25 ct                                                                          | 0 ct                                               |
| `H1 Strike Velocity to H32 Strike Velocity` | `h01_strike to h32_strike`         | -100 % to +100 %                                                                       | 0 %                                                |
| `H1 Delay to H32 Delay`                     | `h01_delay to h32_delay`           | 0.0 ms to 5.00 s                                                                       | 0.0 ms                                             |
| `H1 Attack to H32 Attack`                   | `h01_attack to h32_attack`         | 90° to 5.00 s                                                                          | 5.0 ms                                             |
| `H1 Decay to H32 Decay`                     | `h01_decay to h32_decay`           | 1.0 ms to 20.00 s                                                                      | 600 ms                                             |
| `H1 Sustain to H32 Sustain`                 | `h01_sustain to h32_sustain`       | 0 % to 100 %                                                                           | 100 %                                              |
| `H1 Key-off Swell to H32 Key-off Swell`     | `h01_swell to h32_swell`           | 0.0 ms to 5.00 s                                                                       | 5.0 ms                                             |
| `H1 Key-off Level to H32 Key-off Level`     | `h01_offLevel to h32_offLevel`     | 0 % to 100 %                                                                           | 0 %                                                |
| `H1 Release to H32 Release`                 | `h01_release to h32_release`       | 1.0 ms to 20.00 s                                                                      | 400 ms                                             |
| `H1 Amp Mod Rate to H32 Amp Mod Rate`       | `h01_amRate to h32_amRate`         | 0.01 Hz to 30.0 Hz                                                                     | 4.00 Hz                                            |
| `H1 Amp Mod Depth to H32 Amp Mod Depth`     | `h01_amDepth to h32_amDepth`       | 0 % to 100 %                                                                           | 0 %                                                |
| `H1 Amp Mod Shape to H32 Amp Mod Shape`     | `h01_amShape to h32_amShape`       | Sine, Triangle, Sawtooth, Reverse Sawtooth, Square, Sample & Hold, Random              | Sine                                               |
| `H1 Velocity to H32 Velocity`               | `h01_vel to h32_vel`               | -100 % to +100 %                                                                       | +70 %                                              |
| `H1 Aftertouch to H32 Aftertouch`           | `h01_aftertouch to h32_aftertouch` | -100 % to +100 %                                                                       | 0 %                                                |
| `H1 Mute to H32 Mute`                       | `h01_mute to h32_mute`             | Off or on                                                                              | Off                                                |
| `H1 Solo to H32 Solo`                       | `h01_solo to h32_solo`             | Off or on                                                                              | Off                                                |
| `H1 Pan to H32 Pan`                         | `h01_pan to h32_pan`               | L100 to R100                                                                           | Centre                                             |
| `H1 Level to H32 Level`                     | `h01_volume to h32_volume`         | -inf dB to 0.0 dB                                                                      | Differs by channel, 0.0 dB on H1 to -inf dB on H32 |

**On the noise channel**, twenty-two of them.

| What the host calls it  | Id                  | Range                                                                     | Default |
| ----------------------- | ------------------- | ------------------------------------------------------------------------- | ------- |
| `Noise Colour`          | `noise_colour`      | 0 % to 100 %                                                              | 50 %    |
| `Noise Strike Velocity` | `noise_strike`      | -100 % to +100 %                                                          | 0 %     |
| `Noise Delay`           | `noise_delay`       | 0.0 ms to 5.00 s                                                          | 0.0 ms  |
| `Noise Attack`          | `noise_attack`      | 0.2 ms to 5.00 s                                                          | 5.0 ms  |
| `Noise Decay`           | `noise_decay`       | 1.0 ms to 20.00 s                                                         | 600 ms  |
| `Noise Sustain`         | `noise_sustain`     | 0 % to 100 %                                                              | 100 %   |
| `Noise Key-off Swell`   | `noise_swell`       | 0.0 ms to 5.00 s                                                          | 5.0 ms  |
| `Noise Key-off Level`   | `noise_offLevel`    | 0 % to 100 %                                                              | 0 %     |
| `Noise Release`         | `noise_release`     | 1.0 ms to 20.00 s                                                         | 400 ms  |
| `Noise Amp Mod Rate`    | `noise_amRate`      | 0.01 Hz to 30.0 Hz                                                        | 4.00 Hz |
| `Noise Amp Mod Depth`   | `noise_amDepth`     | 0 % to 100 %                                                              | 0 %     |
| `Noise Amp Mod Shape`   | `noise_amShape`     | Sine, Triangle, Sawtooth, Reverse Sawtooth, Square, Sample & Hold, Random | Sine    |
| `Noise Velocity`        | `noise_vel`         | -100 % to +100 %                                                          | +70 %   |
| `Noise Aftertouch`      | `noise_aftertouch`  | -100 % to +100 %                                                          | 0 %     |
| `Noise Mute`            | `noise_mute`        | Off or on                                                                 | Off     |
| `Noise Solo`            | `noise_solo`        | Off or on                                                                 | Off     |
| `Noise Pan`             | `noise_pan`         | L100 to R100                                                              | Centre  |
| `Noise Level`           | `noise_volume`      | -inf dB to 0.0 dB                                                         | -inf dB |
| `Noise String`          | `noise_string`      | 0 % to 100 %                                                              | 0 %     |
| `Noise String Decay`    | `noise_stringDecay` | 50.0 ms to 10.00 s                                                        | 1.50 s  |
| `Noise String Damp`     | `noise_stringDamp`  | 0 % to 100 %                                                              | 40 %    |
| `Noise String Point`    | `noise_stringPoint` | 0 % to 100 %                                                              | 25 %    |

**On every macro**, six controls, each one on all eight macros.

| What the host calls it             | Id                               | Range                                                                                                                                                                                                  | Default                                                |
| ---------------------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------ |
| `Macro 1 to Macro 8`               | `macro1_amount to macro8_amount` | -100.0 % to +100.0 %                                                                                                                                                                                   | 0.0 %                                                  |
| `Macro 1 Row to Macro 8 Row`       | `macro1_row to macro8_row`       | None, Tune, Glide, Pitch mod rate, Pitch mod depth, Drift, Strike, Delay, Attack, Decay, Sustain, Key-off swell, Key-off level, Release, Amp mod rate, Amp mod depth, Velocity, Aftertouch, Pan, Level | None                                                   |
| `Macro 1 Scope to Macro 8 Scope`   | `macro1_scope to macro8_scope`   | All, Odd, Even, Prime/octave, Minor second, Major second, Minor third, Major third, Fourth, Tritone, Fifth, Minor sixth, Major sixth, Minor seventh, Major seventh                                     | All                                                    |
| `Macro 1 Curve to Macro 8 Curve`   | `macro1_curve to macro8_curve`   | Uniform, Taper                                                                                                                                                                                         | Uniform                                                |
| `Macro 1 Anchor to Macro 8 Anchor` | `macro1_anchor to macro8_anchor` | 1 to 32                                                                                                                                                                                                | 1                                                      |
| `Macro 1 Colour to Macro 8 Colour` | `macro1_colour to macro8_colour` | None, Red, Orange, Yellow, Green, Cyan, Blue, Violet, Magenta                                                                                                                                          | Differs by macro, Red on Macro 1 to Magenta on Macro 8 |

## Macros

**A macro is one parameter that moves a whole row.** Macros exist for automation. A Link drag across the tuning knobs moves 32 parameters, and a host records only the last one you touched, so a change across the row that you can make by hand cannot be recorded. A macro gives that change a single control that a host can automate.

The icon of three faders on the top bar, beside the settings cog, opens the macros over the mixer. Each macro is one line: a colour, the row it controls, which channels of that row it reaches, how the movement is shared between them, and the amount. Add a macro uses one of the eight, and the cross at the end of the line frees it again.

**The eight macros are a pool, not a limit you are meant to notice.** A plugin declares its parameters once when it is created, and neither VST3 nor AU can add one later. So making a macro uses one of eight that already exist, and removing it frees it again. The panel shows the ones you have made, and your host's parameter list shows all eight whether you use them or not.

**Scope** is All, Odd, Even, or any one interval: choosing fifths reaches harmonics 3, 6, 12 and 24. **Curve** is Uniform or Taper. A taper has the most effect on a channel you pick, and less with distance on either side of it. Link's third curve, Spread, is not available and cannot be. It moves each strip in a direction based on your drag relative to the strip under the mouse, and a macro has no drag direction and no strip under the mouse.

**A macro offsets the patch's values instead of writing them.** Nothing in your sound is overwritten, the host has one lane to draw instead of thirty-two to record, and the knobs do not move when a macro does. That last part is normal for controls that work this way, but it can be surprising at first, so the controls show both values at once. The ring around a knob lights up to the value the macro has set, in the macro's colour, while the pointer on the cap stays at the patch's value, in the channel's colour. The gap between them is the macro. A fader has no ring, so it shows this in the gutters on either side of its track. Two narrow columns of segments appear there, in the same style as the meter behind the fader, and light from the bottom of the track up to the level the macro has set. The glass of the cap is tinted in the same colour.

With a macro at rest the two are in the same place, so a macro you have assigned but not moved looks the same as no macro.

On a [silenced channel](https://benjamindehli.github.io/overtonium/controls/#silenced) the macro's ring and columns go dark with the rest of the strip, and only the pointer and the fader cap still show the patch's value.

The amount runs from -1 to +1 whatever it controls, so a macro can always push a control either way from the patch's value. It moves each control by a proportion of its own range, not by a number of units. This matters most on a row like decay, which runs from a thousandth of a second to twenty seconds. Adding the same number of seconds would be a twentyfold change at one end and almost no change at the other.

A full sweep reaches the ends of the range, so if the patch value is already near one end, part of the macro's travel does nothing in that direction. This is on purpose. One macro reaches up to 32 controls that may be set anywhere, so no smaller part of the range would suit all of them.

**Right-click a macro's amount to bind a controller to it**, using the same MIDI Learn as the mixer's controls. Only the amount can be bound, because the row, the scope and the curve define the macro rather than move it. Its knob in the Playing bar has the same option, with Edit macro beside it to open MACROS. For a macro not yet made, the knob offers Assign macro instead, which creates the macro and opens MACROS. The macros icon lights whenever any macros exist, because otherwise you cannot see a macro's effect without looking at a ring.

## Playing it from a controller

**Right-click any control and choose MIDI Learn**, then move something on your controller. A violet ring marks the control while it waits, so you can see which control is listening without opening the menu again. Only one control can wait at a time. Right-click the same control afterwards and the menu shows which controller moves it, with options to remove the binding or bind a different one. In the mixer these items are below the LINK settings in the right-click menu. On the top bar, the Playing and Tuning bars and the noise channel, they have a menu of their own, because none of these is part of the series LINK gangs.

Your host almost certainly offers the same thing. The difference is that here you set up the mapping by right-clicking the knob itself, instead of finding `H17 AM Rate` in a list of 850 parameters, and it works the same way in every host.

**Eleven controllers cannot be bound**, because the instrument already uses them and losing them would affect your playing. Controller 1 is the mod wheel, which can drive aftertouch. 64 is the sustain pedal. 74 is the slide axis, the easiest to bind by accident because it moves whenever a finger moves on an MPE keyboard. 120 and 123 stop notes. Controllers 6, 38, 98, 99, 100 and 101 are how a keyboard tells the instrument what kind of keyboard it is. If you arm a knob and then touch the slide to steady a note, the note is steadied and the knob keeps waiting.

**It works the same with MPE on**, which takes extra handling. With MPE on, nearly every message goes to the part of the instrument that reads your keyboard's layout, so bindings are checked before that, not after. Otherwise MIDI Learn would not work on the controllers the instrument is most often played from.

The mapping is saved with your project, along with the window size and the folded sections, and **a preset never changes it**. Which controller moves which knob depends on your hardware setup, not on the sound, so a patch should not change it. A binding stores the parameter by name, so a later version that adds controls cannot quietly point it at something else.

Two things to expect. A control jumps to the controller's current position instead of waiting for the controller to reach it, so a hardware knob turned to the top takes the parameter there the moment you touch it. And undo does not reverse changes made from a controller, for the same reason it does not reverse automation.
