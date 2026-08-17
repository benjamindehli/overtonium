# Overtonium

A 32-partial additive synthesiser laid out like a 32-channel mixer. Every channel is one sine oscillator locked to a harmonic of the played note, and every channel has its own tuning, envelope and modulation.

The control worth reaching for first is TUNE. It sweeps each partial continuously between equal temperament and just intonation. At the just end the partial sits at an exact integer ratio with the fundamental and the whole stack fuses into a single timbre. At the equal end each partial snaps to the nearest 12-TET semitone and the same stack smears into a chord. The factory presets *Just Saw* and *Equal Saw* are identical except for that one control, and they sound nothing alike.

[![Build and test](https://github.com/benjamindehli/overtonium/actions/workflows/build.yml/badge.svg)](https://github.com/benjamindehli/overtonium/actions/workflows/build.yml)

- **Formats:** VST3, AU (macOS), Standalone, LV2 (Linux)
- **Platforms:** macOS (universal), Windows, Linux
- **Framework:** JUCE 8
- **Licence:** AGPLv3. See [Licensing](#licensing)
- **By:** Benjamin Dehli for Dehli Musikk. Hosts list it under DehliMusikk (manufacturer code `Dhmk`, plugin code `Ovtn`)

## Building

You need CMake 3.22 or newer and a C++17 compiler. JUCE is downloaded at configure time. Pass `-DJUCE_PATH=/path/to/JUCE` to use a local checkout instead.

### macOS

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Drop `-G Ninja` to use Makefiles instead. Neither needs Xcode.app, the Command Line Tools (`xcode-select --install`) are enough.

If you have full Xcode installed and want a project to debug in, use `cmake -B build-xcode -G Xcode` followed by `cmake --build build-xcode --config Release`. That generator needs Xcode.app specifically. With only the Command Line Tools, CMake misreads the version and fails with `Xcode 1.5 not supported`. Point it at a real install with `sudo xcode-select -s /Applications/Xcode.app` if you hit that.

`COPY_PLUGIN_AFTER_BUILD` is on, so the plugins are installed to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components` as part of the build. Logic and GarageBand only rescan the AU after you quit and relaunch them. If the component does not show up, run `auval -v aumu Ovtn Dhmk` to see why.

The build produces a universal arm64 and x86_64 binary. For a faster local build, add `-DCMAKE_OSX_ARCHITECTURES=arm64`.

### Windows

```sh
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 lands in `build/Overtonium_artefacts/Release/VST3/`.

### Linux

```sh
sudo apt install build-essential cmake pkg-config libasound2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libxcomposite-dev libgl-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Tests

The DSP core has no JUCE dependency, so its tests compile straight from a compiler with no build system and no third-party headers involved:

```sh
c++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp Source/dsp/*.cpp -o dsp_test
./dsp_test
```

Through CMake, which also gets you the JUCE-linked tests:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target overtonium_dsp_test
./build/overtonium_dsp_test
```

They cover the tuning table, blend endpoints, sine table accuracy, the anti-aliasing guard, envelope behaviour, mute and solo, click-free parameter changes, voice allocation, modulation and the two master effects, and they finish by printing a CPU benchmark.

There is a second target, `overtonium_runtime_test`, which builds the plugin sources as a console app and exercises parameter wiring, MIDI handling, the factory presets, user preset round-tripping, bus layouts, undo and state round-tripping. It never opens an editor, so it runs on a machine with no display. Both targets are registered with CTest, so `ctest` from the build directory runs them together.

Both run in CI on macOS, Windows and Linux on every push and pull request, building the plugin itself along the way so a compile error in the JUCE half cannot pass unnoticed. A second job compiles the DSP core with the bare-compiler line above, and with `-Werror`, so the claim that it has no build system dependency is checked rather than asserted.

Pass `-DOVERTONIUM_INSTALL_AFTER_BUILD=OFF` to skip copying the built plugins into your user plugin folders, which is what CI does and what you want on any machine with no host to rescan them.

## Presets

Fifteen factory presets ship with it. The first nine are the original ones. The six after them were built on what came later, and each is there to show one thing:

| Preset | What it is for |
|---|---|
| Harpsichord | the key-off stage: the jack falls back louder than the note was holding |
| Music Box | a comb of six teeth, each ringing for a different length and sitting somewhere different in the field |
| Kalimba | a soft key-off thud below the sustain, and the noise channel as the gourd |
| Cathedral | a principal chorus arriving slowly, in a nine second room |
| Tape Choir | a worn echo and 14 cents of drift, with the partials fanned across the field |
| Glass Armonica | rubbed glass, and a key-off level above the sustain so lifting the finger lets the rim ring on |

Six of them use STRETCH or TRACK, since six of them are modelling something with a body. Picking odd partials was always a stand-in for inharmonicity, and three of these now have the real thing on top of it:

| Preset | Stretch | Tracking | Why |
|---|---|---|---|
| Vibraphone | | 3.0 dB/oct | a short bar carries less above its fundamental than a long one |
| Harpsichord | | 2.5 dB/oct | a short treble string carries less of the pluck's edge |
| Music Box | +300 ct | 3.0 dB/oct | a comb tooth is a bar and does not ring in whole numbers. The 23rd tooth ends up 168 cents sharp |
| Kalimba | +500 ct | 3.0 dB/oct | tines are further out than a comb, not less. The 15th partial lands 137 cents sharp |
| Tape Choir | | 3.5 dB/oct | voices thin out at the top of a range rather than brightening |
| Glass Armonica | +180 ct | 4.0 dB/oct | barely any, but enough that the upper partials beat against the fundamental instead of locking to it |

The organs are deliberately left alone. A drawbar organ is electric and a pipe organ is voiced rank by rank, so neither loses its top as you play up, and pretending otherwise would be modelling the wrong instrument. So are the four synthetic patches, where the raw spectrum is the point.

**Saving your own.** The preset menu writes to `Dehli Musikk/Overtonium/Presets` under your user application data folder, one small `.ovtpreset` file each, and lists whatever it finds there under the factory list. The menu reads the folder every time it opens, so a preset saved a moment ago is in it and one deleted in Finder is not.

A preset holds parameter values and nothing else. Not the window size, the zoom or the LINK settings, since loading a sound should not move your window or change your tools.

Factory presets start from a neutral base rather than from wherever you happened to be, so one always gives the instrument it describes. That covers the globals that are part of the sound: STRETCH, TRACK, the converter and phase reset all go back to neutral unless the preset asks for otherwise. It deliberately leaves master gain, polyphony, bend range, the aftertouch source and the safety clipper where you set them, since those are how you play it and how loud rather than what it sounds like. A test loads every factory preset twice, once from clean and once after deliberately making a mess of everything, and requires the two to be identical.

Values are stored plain rather than normalised, so a preset survives a parameter's range being widened later, and anything a file does not mention keeps its default rather than being reset, so a preset saved by an older build loads into a newer one without silently zeroing whatever was added in between. The tests cover both of those directly.

**Undo.** The parameter tree carries an undo history, which matters most because of LINK: one drag can move the same knob on all 32 channels, and before this the only way back from a drag you did not mean was to reload the preset.

A step is a gesture rather than a value change. Transactions are closed when the tree has been still for a moment rather than by hooking every parameter's gesture callbacks, which a host is free to call from the audio thread and which would mean allocating there. Watching for stillness needs no hooks and gives the same answer: a drag is one step however many values it moved, and letting go for a moment starts the next one.

Cmd-Z and Cmd-Shift-Z work where the host lets them through, which many do not, since a DAW usually keeps those for its own history. Undo and Redo are therefore also at the top of the Settings menu, which always works. Loading a session clears the history, since undoing your way back into someone else's edits is not useful.

**Making a factory one.** Dial in a patch, then pick "Copy as factory preset code". That puts the C++ for it on the clipboard, as a `case` that starts from `neutralBase()` and then sets only what differs from the default. Paste it into `Presets.cpp`, add the name to `kNames`, and tidy it by hand where the shape has a formula rather than 32 separate numbers. Factory presets stay as code rather than as embedded data precisely so they can say `1.0 / n` instead of listing values.

## The tuning system

For harmonic `n`, the just interval above the fundamental is `1200 * log2(n)` cents. Overtonium rounds that to the nearest semitone to get the equal-tempered position, and the remainder is the cent offset that the TUNE knob dials in:

```
semitoneOffset(n, blend) = round(1200*log2(n)/100) + blend * (1200*log2(n) mod 100) / 100
```

At `blend = 1` this is exactly `n` times the fundamental. Nothing is hard-coded, but rounded to whole cents the table comes out as the familiar one:

| n | semis | cents | interval | | n | semis | cents | interval |
|---|-------|-------|----------|-|---|-------|-------|----------|
| 1 | 0 | 0 | prime/octave | | 17 | 49 | +5 | minor second |
| 2 | 12 | 0 | prime/octave | | 18 | 50 | +4 | major second |
| 3 | 19 | +2 | fifth | | 19 | 51 | -2 | minor third |
| 4 | 24 | 0 | prime/octave | | 20 | 52 | -14 | major third |
| 5 | 28 | -14 | major third | | 21 | 53 | -29 | fourth |
| 6 | 31 | +2 | fifth | | 22 | 54 | -49 | tritone |
| 7 | 34 | -31 | minor seventh | | 23 | 54 | +28 | tritone |
| 8 | 36 | 0 | prime/octave | | 24 | 55 | +2 | fifth |
| 9 | 38 | +4 | major second | | 25 | 56 | -27 | minor sixth |
| 10 | 40 | -14 | major third | | 26 | 56 | +41 | minor sixth |
| 11 | 42 | -49 | tritone | | 27 | 57 | +6 | major sixth |
| 12 | 43 | +2 | fifth | | 28 | 58 | -31 | minor seventh |
| 13 | 44 | +41 | minor sixth | | 29 | 58 | +30 | minor seventh |
| 14 | 46 | -31 | minor seventh | | 30 | 59 | -12 | major seventh |
| 15 | 47 | -12 | major seventh | | 31 | 59 | +45 | major seventh |
| 16 | 48 | 0 | prime/octave | | 32 | 60 | 0 | prime/octave |

The test suite asserts this table, so the derivation cannot silently drift from it.

### Stretch

TUNE decides how the series is spelled. STRETCH decides whether it is a series at all.

Nothing real rings at integer multiples of anything. A string with any bending stiffness has partials at `n * f0 * sqrt(1 + B * n^2)`, sharp of the harmonic and increasingly so up the series, and it is that, not the hammer or the soundboard, that makes a piano sound like a piano rather than like a sawtooth. It is also what a tuner is matching when they stretch the octaves: tune the top of the instrument to the theory and it beats against its own overtones.

STRETCH is B, dialled by what it does to the top partial rather than by its own value, which lives between 0.00003 and 0.008 and means nothing to anyone. At +150 cents, which is a real piano:

| Partial | 2 | 8 | 16 | 32 |
|---|---|---|---|---|
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

## Controls

Each of the 32 strips has, top to bottom:

| Control | Range | Notes |
|---|---|---|
| TUNE | equal to just | Readout shows the resulting cent offset |
| PITCH MOD rate and depth | 0.01 to 30 Hz, 0 to 200 cents | Per-partial vibrato |
| DRIFT | 0 to 25 cents | Smooth random pitch wander. See below |
| ENVELOPE delay, A, D, S | 0 to 5 s, 0.2 ms to 5 s, 1 ms to 20 s, 0 to 100% | Exponential decay |
| KEY OFF swell, level, release, lift | 0 to 5 s, 0 to 100%, 1 ms to 20 s, -100 to +100% | A second envelope for letting go. See below |
| AMP MOD rate and depth | 0.01 to 30 Hz, 0 to 100% | Per-partial tremolo |
| VELOCITY | -100 to +100% | How much key velocity scales this partial. Negative inverts it |
| AFTERTOUCH | -100 to +100% | How much key pressure moves this partial. Negative fades it out |
| PAN | hard left to hard right | Where this partial sits in the field. Equal power, so the level holds as it crosses |
| M and S | | Mute wins over solo |
| LEVEL | -inf to 0 dB | Square-law fader, with the meter filling its track |

Velocity being per partial is what lets a soft note be a different timbre rather than just a quieter one. Set the fundamental to 0% and the upper partials to 100% and the tone opens up as you play harder, which is roughly what a struck string or bar does. *Struck Bell* and *Odd Harmonics* ship with that curve already dialled in.

Both controls run either side of zero. A positive amount means harder or heavier is louder. A negative amount inverts that, so the partial is at its loudest when you play softly or lift off the key. The two halves are exact mirrors, so -50% at a given velocity matches +50% at the opposite velocity.

The reason to want the negative half is crossfading. Give one set of partials a positive velocity amount and another set a negative one, and the two timbres trade places across the velocity range instead of one simply fading in. The test suite plays that case and measures the spectral balance swinging by a factor of a hundred between a soft and a hard note.

Letting go of a key runs its own little envelope rather than simply fading out. The KEY OFF rows say where the level goes when the key is released and how long it takes to get there, and only then does the release run from that level down to silence.

Above the sustain that is a release click or a bloom, the sound a damper landing or a hammer returning makes, and it can be louder than the note was while you were holding it. *Drawbar Organ* uses it that way: the upper drawbars jump to 55% for two milliseconds as the contacts break. Below the sustain it is the fast initial drop into a long tail that a piano or a struck bell actually has, which is the half of it that probably gets more use.

A key-off level of zero skips the stage entirely and releases from wherever the level sat, which is the default and is exactly what the envelope did before it had one. So nothing you have already made sounds different, and the knob reads honestly: zero means no key-off stage.

Two details that fall out of it. The swell time is exact rather than the "within 1%" the other stages use, because the release has to start when the knob says it does rather than whenever an exponential happens to arrive. And letting go of a key during the delay now still makes a key-off sound if you have asked for one, which is what a release click does on a real instrument, while a level of zero cancels the partial as before.

LIFT aims release velocity at the key-off level, the same way the velocity row aims key velocity at the fader, and reads the same: zero ignores the gesture, positive means a faster release is louder, negative inverts it. On a harpsichord the jack falls back harder when you snatch the key away and barely speaks when you let it up slowly, and that is one control rather than a second envelope. Measured with the level at 90%, a full-amount partial peaks at 0.378 for a fast release against 0.019 for a slow one, and at zero amount both give 0.378.

It defaults to zero, so no patch made before it existed sounds any different. A controller that ends notes with velocity zero, whether as a real note-off or as the note-on-with-zero many of them send, has said nothing about the gesture, so it gets the middle value rather than the slowest, which would otherwise silence every key-off on such a keyboard.

A third case is the one worth watching for, since it is the shape a music box or a thumb piano has: no sustain at all, so the partial decays to silence while the key is still down, and then a key-off level that brings it back. Reaching zero is not the same as being finished. A partial in that state holds at silence and waits for the key rather than freeing itself, and costs nothing while it waits, since there is no point running an oscillator to produce zeroes.

The envelope's delay stage holds a partial silent before its attack begins. Staggering it across the series makes the spectrum unfold rather than arrive all at once, which is how *Slow Pad* and *Shimmer* now open up. It is latched in samples at note-on, so moving the knob cannot retime a note already waiting, and releasing a key before the delay elapses cancels that partial rather than letting it burst in afterwards.

Aftertouch works the same way but **adds** to the fader instead of scaling it, and it ignores velocity entirely. That means a strip with its fader all the way down is silent until you lean on the key, and then it fades in under your finger, while a negative amount fades an open strip back out again. Put a few upper partials on positive aftertouch and the note grows brighter the harder you press, without touching the partials you left alone. Both channel pressure and polyphonic aftertouch are accepted, and whichever is higher wins. Pressure is smoothed over about 15 ms, so seven-bit MIDI does not step the gain.

The mod wheel can stand in for it, and by default does. Most keyboards have no aftertouch at all, and the wheel is the control your hand already goes to, so CC1 feeds the same destination. Nothing changes for a controller that does send pressure, since a wheel left alone reads zero. Settings has an "Aftertouch from" entry if you would rather have one or the other on its own. Polyphonic aftertouch is not on that list: it is per note rather than per channel, there is nothing ambiguous about where it should go, and it stays routed whatever the setting says.

The top bar holds everything that is not per partial, in signal order from left to right:

| Group | Contains |
|---|---|
| Preset | the preset menu: factory, saved, and somewhere to put the one you are working on |
| Settings | undo, polyphony, bend range, what feeds aftertouch, phase reset, the safety clipper and zoom |
| Link | **LINK**, and what it reaches and how. See below |
| Series | **STRETCH** and **TRACK**, what the series does before anything is done to it. See below |
| Echo | the tape echo. See below |
| Reverb | the reverb. See below |
| Output | **MASTER**, the stereo meter, and the converter readouts under it |

Zoom lives in the Settings menu rather than on the bar, and that is worth eighty-eight pixels. Before it moved, the bar needed 1202 px on its first row at the width the window opens at and had 1164, so it wrapped to two rows on a default-sized window. It now needs 1114 and fits, and the room that frees goes to the output meter, which is what makes the converter readouts wide enough to keep their units.

Echo, Reverb and Output are drawn as boxes, because a box is what says "these belong together" and there is something in each of them to group. The rest are single controls standing on their own: a box around one button says nothing the button was not already saying, and four of them in a row turn the bar into a fence. Buttons, lists and the meter all stand on the line the knob dials stand on, rather than in the middle of their row, since a knob carries its caption underneath and anything centred beside one reads as sagging.

Settings and Link are menus rather than panels. Everything behind Settings is set once and then left, and a short list of whole numbers reads better written out than dialled in on a knob. Phase reset gives a coherent, percussive attack by restarting partial phase on each note, and the safety clipper is worth leaving on when you push 32 faders up, but neither is something you sit and adjust, so neither is worth the width of a button. That is what the two effects are sitting in.

Everything fits across one row above about 1200 px of logical width. Below that the bar reflows onto further rows rather than dropping controls or letting captions collide. It fills each row as far as it will go, so it stays compact and anchored to the title, and the rows below the first run the full width since the title is above them rather than beside them. One row per group is the worst case, and the window will not shrink past 512 px, which is where it stops fitting in three.

The output meter is the one element that flexes. It takes any width left over on its row up to a limit, since uncapped it swallowed a whole row and stopped reading as a meter, and it gives up to 40 px back when a row is slightly too tight. That last part matters: a window a few pixels short of fitting a row narrows the meter instead of wrapping.

The exception to filling greedily is a last row that would come out nearly empty, since one small group alone on a row of its own reads as a mistake. In that case a group is pulled down to join it.

Double-clicking any knob restores its default. Hovering a harmonic number shows its interval and exact cent deviation.

Pointing at a knob picks out its row all the way across the mixer, through every strip and the noise channel, and brightens the one caption in the gutter that names it. Out at harmonic 28 that caption is a long way to the left, and following a band back to it beats counting rows. The two readouts count as part of the control above them, so drifting off the tuning knob onto its cent figure does not put the highlight out.

The faders and the mute and solo buttons are left out of it. Those two rows are unmistakable already, and a wash the height of a whole fader was a lot of paint to say so.

Knobs show their value as a ring of discrete ticks rather than a continuous arc, which suits an instrument that is itself built from 32 discrete partials and reads more like a measurement device than a mixing desk. Faders carry a scale in the same tick language.

The panel is lit from the top left throughout. Knob caps are machined discs with a nearly flat face, the roundness living in the rim that catches the light along its upper edge and falls into shadow underneath, with a soft shadow cast down and to the right, channels have a bright left edge and a shadowed right one so a run of strips reads as raised columns, fader grooves darken at the top where they are cut into the panel, and buttons cast a shadow when raised and lose it when engaged. Pointers and fader caps are translucent glass, so the tick ring and the meter read straight through them.

Cast shadows are built from a few overlapping shapes rather than from a real blur. JUCE has a proper `DropShadow`, but it is a software Gaussian and running one on several hundred controls every repaint would be far too slow.

### One voice per key

The instrument is polyphonic across the keyboard and monophonic within a key. A string, a tine or a bar is one object, and striking it again takes over whatever it was already doing rather than starting a second copy beside the first, so playing the same key twice does the same here.

That includes tails. A key with a long release that is tapped repeatedly used to leave every tap ringing and sum them, which no physical instrument does. Five overlapping taps of one key now peak at 0.412 against 0.424 for a single tap, where before they reached 0.961, which is two coherent copies of the same note.

A tail being taken over gets the same four millisecond fade a stolen voice gets, quick enough to read as instant and slow enough not to click. Measured across a retrigger, the largest jump between neighbouring samples is 0.01453, against 0.01455 for the steepest part of the waveform itself, so the cut adds nothing the signal was not already doing.

A key that is still down, or held by the sustain pedal, is retriggered where it stands instead. That keeps two things a fade would lose: with phase reset off, a legato retrigger carries on from the level the envelope is at rather than restarting from silence, and re-striking a pedalled note takes it back off the pedal.

It also stops tails eating the voice pool, which was the more expensive half of the problem. Every tap held one of the 24 voices for the whole of its release, so one repeatedly tapped key could fill the pool by itself: four keys held and a fifth tapped 25 times used to end with all 24 in use. Past that the allocator has nothing free and takes the oldest voice outright, with no fade and no regard for whether a key is still down on it, which is audible as notes being cut off mid-hold. The same sequence now ends with five voices in use.

**When the pool runs out.** Under the polyphony limit a stolen voice is handed a four millisecond fade and keeps rendering out of the eight surplus voices, so stealing never clicks. Past that surplus there is nothing left to fade into: the new note cannot wait, so a voice has to go this instant, and that is a step in the output whichever one it is. The size of the step is that voice's current level, so the only lever is which voice gets taken.

It used to take the oldest. That is the right rule for stealing under the limit and the wrong one here, because the oldest voice is often a note still being held while a tail three quarters of the way through its release sits beside it costing almost nothing to lose. It now takes the quietest, preferring one already on its way out. With one loud note held among quiet tails and the pool overflowing, the held note used to be cut outright and now survives.

### The noise channel

Pinned on the right, after the series it does not belong to, is a noise channel marked NZ. It has the same envelope, tremolo, velocity, aftertouch, pan, mute, solo, level and meter as a partial, and takes part in solo alongside them. What it does not have is pitch, so the tuning, pitch modulation and drift rows are empty.

COLOUR takes the tuning row instead. It tilts the noise from dark rumble at one end, through flat in the middle, to bright hiss at the other. The middle really is flat rather than merely filtered less: the two halves of a complementary one-pole pair are summed at unity there, which reconstructs the original white noise exactly. The tests measure a high to low band ratio of 0.13 dark, 1.06 flat and 4.35 bright.

Each voice runs its own noise stream, so playing a chord does not layer 8 copies of the identical signal. Two voices measure about sqrt(2) times the energy of one, which is what independent sources give.

LINK gangs the 32 partials only. Its tuning, pitch modulation and drift have no noise counterpart, and having some ganged controls reach the noise channel while others could not would be worse than having none of them do.

### Meters

Each channel meters what that partial is actually putting out, on a decibel scale floored at -48 dB. Since it reflects the final gain, it shows the envelope, tremolo, velocity, aftertouch, the Nyquist fade and mute or solo all at once, so the spectrum can be watched evolving as a note decays.

The meter fills the fader's own track rather than sitting beside it. A fader that also drew its set level would be showing you something the cap already says, so the whole track is given over to output instead. The cap is drawn as translucent glass with bright edges, so the meter reads straight through it, and the knob pointers are drawn the same way so the value arc shows through them.


It reads the loudest instance of a partial across the sounding voices rather than the sum, so it shows the shape of the patch instead of pinning itself the moment you play a chord.

The cost is close to nothing on either side. The audio thread samples a value it has already computed once per 32-sample control block, which measured inside run-to-run noise on the benchmark.

Drawing them cost rather more than that until it was measured properly. Meters are the only thing in the window that changes on its own, so they set the cost of playing a note, and several things were wrong at once. They are worth writing down because almost none of them were about drawing being slow.

The meters are segmented rather than continuous, which is the old spectrum-analyser look and also means the display only changes when the level crosses a segment boundary. A lamp lights when the level reaches it and goes out only once the level has fallen clear of it, so a note sitting on a boundary cannot flicker. On a slow decay 138 frames out of 152 have nothing to redraw at all.

Nothing repaints itself. Each meter hands the band that changed up to the editor, which collects all 33 and invalidates a handful of rectangles once. Merging them into a single rectangle is nearly as bad as leaving them scattered, since the bands sit at different heights and their union is most of the mixer. Six rectangles is the setting that both stays specific and stays cheap: it invalidates 7,000 pixels a frame where one merged rectangle invalidates 54,000.

The meters are opaque, painting their own slice of the channel gradient, so the strip behind them is not redrawn underneath. And they are read fifteen times a second rather than thirty, which is more than a segmented meter can show anyway and halves the number of frames in which anything is invalidated at all.

Together those took a decaying chord from 141% of a core to 1%. And it was still visibly sluggish, which is the interesting part: the cost was never CPU. It was that since around macOS 10.13 CoreGraphics answers a list of scattered dirty rectangles by redrawing the one rectangle that encloses them, and for this layout that is the whole window, every frame, whatever care went into the rectangles. JUCE's own note in `juce_NSViewComponentPeer_mac.mm` says as much, and points at the way out: a Metal-backed layer, which keeps the rectangles apart and puts the compositing on the GPU. `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` turns it on and has no default, so it is set in CMakeLists.txt for Apple builds. That was the one that made the difference on the machine, and none of the work above would have shown up without it.

One thing that looks like it should help and does not: splitting the mixer into halves that update on alternate frames. Each meter still updates at the same rate and each frame covers half the width, so the area per frame halves, but the number of frames doubles. Frames are the more expensive of the two.

### Ganging the channels

**LINK** in the top bar gangs the strips: dragging any knob moves the same knob on the others. It works relatively, applying an offset to wherever each strip already sits rather than dragging everything to one shared value, so a spectrum you have shaped by hand keeps its shape.

The button opens a menu rather than toggling, since what a drag reaches and how it shares itself out matter as much as whether it is on at all, and it lights when the switch inside is engaged. The same menu is on a right-click anywhere in the mixer, which is where you are when you want it. Both settings are latched when a drag begins, so changing one midway cannot half-apply two different rules.

While LINK is on, the pointer over the mixer says which curve is loaded: five bars, level for uniform, rising or falling for the tilts and scattered for spread. A mode you cannot see is a mode you forget you are in, and this one changes what every drag does.

With LINK engaged, pointing at a knob arms every knob a drag from it would move, before you touch anything. How brightly each one lights follows the curve, so under a tilt the end of the series that would take the biggest share is the end that glows most, and a scope that leaves channels out leaves them dark. Changing the scope or the curve redraws it under the pointer, which makes the difference between the four scopes and the four curves something you can see rather than something you have to try.

**LINK SCOPE** picks the channels:

| Scope | Reaches |
|---|---|
| All | every partial |
| Same interval | only the strips sharing the interval class of the one you grab, so you can move just the fifths, or just the octaves |
| Odd harmonics | 1, 3, 5 and so on, the hollow half of the series |
| Even harmonics | 2, 4, 6 and so on |

**LINK CURVE** picks how the drag is shared out:

| Curve | Effect |
|---|---|
| Uniform | every selected strip moves by the same amount |
| Tilt up | partials above the one you grabbed move more, those below move less |
| Tilt down | the same tilt reversed |
| Spread / gather | pushing up scatters the strips apart along random directions, pulling down gathers them onto the strip you are dragging |

The tilts are anchored on the strip you grabbed, which always gets exactly its full share of the drag. That has to be true: the knob under the mouse follows the mouse, so any other weighting would put it visibly out of step with the strips beside it. Tilting is therefore relative to where you reach in, and the weighting is geometric, reaching about 1.8 times at the far end of the series and about 0.6 at the near end when you grab the middle. Neither direction falls to zero, so the quiet end still follows.

Spread draws its scatter directions once when the drag begins, so the pattern holds still while you move rather than boiling. Gathering collapses onto the strip you are dragging rather than onto a fixed average, which keeps the knob in your hand as the thing everything converges towards. Half a drag downwards is enough to arrive.

The offset is always measured from the values captured when the drag started, so returning the knob to where you began restores the strips exactly, even if some of them hit an end stop along the way. That holds for every curve.

### The output meter

The top bar carries a horizontal output meter, split into left and right. It is split because the two channels are identical until something is panned off centre, and a summed meter would hide precisely that. It reads the finished output after master gain and the clipper, so it reports what actually leaves the plugin.

It is segmented like the channel meters, for the same two reasons: it reads at a glance, and it only asks to be redrawn when a lamp changes rather than on every frame in which the level moves at all. Lamps run from teal through amber to orange as they approach full scale, with the colours anchored to their decibel positions rather than stretching with the level, so a lamp keeps its colour whatever the bar is doing. Unlit lamps hold their colour dimmed rather than going dark, which means the top of the scale is visible before you reach it. There is a scale beneath marked at -48, -36, -24, -12, -6 and 0 dB.

Each bar carries a peak hold, since what an output meter is mostly wanted for is what it hit a moment ago. The peak stays alight as a single lamp above the bar for a couple of seconds, then falls with it.

### Panning

Every channel has a PAN, the noise channel included, rather than one width control fanning the series out. A single knob can only ever make one shape. Placing the partials by hand is what lets you put a partial opposite the one a semitone away from it, or the octaves left and the sevenths right, and none of those are shapes a width control could have produced.

The law is equal power, so the number on the knob is the number in the audio and the level holds as a partial crosses the field. The tests measure both, reading the positions back out of the rendered audio rather than taking them on trust: hard left and hard right land within 0.01 of the ends, half left images at half left within 0.02, and a partial swept across the whole field varies in loudness by under 0.1 dB.

A good shape to start from is mirrored pairs, 1 and 2 in the centre widening out to 31 and 32 at the edges, with the sides alternating so the louder of each pair does not always land on the same one. That is what the old width control used to do, and *Slow Pad* and *Shimmer* now write it into their pans, where it can be taken apart by hand. It is worth understanding before you leave it: neighbouring partials have near-identical levels in any normal spectrum, so putting each pair on opposite sides keeps the image centred whatever shape you dial in, and because every position has a mirror, no partial ends up hard panned with nothing facing it.

### The master effects

Two of the groups in the top bar work on the finished mix rather than on any one partial: a tape echo and a reverb. They sit where they are in the signal, after everything per partial and before the output group, and both are ahead of the master fader, so the fader is a true output level and moving it cannot change the wet to dry balance underneath it. Each has a switch that names it, and switching one off empties it rather than leaving a tail to reappear next time it comes on.

**ECHO** is a tape loop rather than a digital delay:

| Control | Does |
|---|---|
| MIX | how much of the output is repeats |
| TIME | distance between the heads, 20 ms to 2 s |
| FDBK | how much of each repeat goes round again, up to 95% |
| AGE | how worn the machine is |

TIME is reached by winding rather than by jumping, so moving the knob slides the repeats in pitch on the way to the new setting, which is the sound a tape delay is mostly wanted for.

AGE is one control for the three things that go together as a tape machine wears: the top end it loses on every pass, how far the motor wanders, and how hard the tape leans over when it is driven. New is clean, bright and steady, old is dark, unsteady and compressed. They were three knobs that were nearly always turned together. The tests measure all three separately: a new machine hands back four times the top end of a worn one after ten passes, holds its pitch to 0.01% where a worn one wanders by over 1%, and passes its repeats through at full level where a worn one compresses them.

The compression is in two stages, and they are not the same thing. The first is character and follows AGE, so a new machine really is untouched. The second is a backstop that is always there, because at 95% feedback a steady tone can otherwise pile up to twenty times what went into it. The tests drive it at maximum feedback with a new machine for twenty seconds and the output stays bounded.

The repeats walk across the image rather than sitting where they were played, which is a fixed crossfeed rather than a control. The useful part of that range was a lean, and everything either side of it was either inaudible or a gimmick. It is worth knowing why it is not simply a crossfeed: swapping two identical signals changes neither, so on a centred source, which is nearly everything this gets fed, crossfeed alone does nothing at all. What makes it audible is feeding the repeats in unevenly and letting the crossfeed carry them over on each pass.

**REVERB** is a feedback delay network: eight delay lines fed back through a Householder matrix, with four allpass stages per side in front of it to scatter a hit into a wash before it reaches the network.

| Control | Does |
|---|---|
| MIX | how much of the output is reverb |
| DECAY | how long the tail takes to fall 60 dB, 0.2 to 20 s |
| DAMP | how quickly the top end dies out of the tail |
| PRE | silence between the note and its reverb, up to 250 ms |
| WIDTH | mono at zero, fully spread at the top |

The room is sized from the decay rather than set separately. A long tail in a small room is a spring rather than a place, and a short one in a hall is a gate, so the two were always turned together anyway.

The matrix is orthogonal, so the network neither gains nor loses energy of its own accord and the decay is entirely the doing of the per-line gains, which is why DECAY can be trusted. The tests measure it: a 1 s setting falls silent in 1.0 s and a 5 s setting in 4.3 s.

The choice of a network rather than a bank of combs is about this instrument in particular. Thirty-two pure sines held indefinitely will find every resonance a fixed network has, and a comb reverb answers them with a metallic pitch. The line lengths are therefore mutually prime and each is slowly modulated at its own rate, so the tail keeps moving underneath a held chord. The tests check for exactly that failure, measuring the loudest bin of the tail against the average across the spectrum.

The input is cut off below 175 Hz for the same reason, and that is fixed rather than offered as a control. A fundamental at full level feeding a long tail floods everything above it, and the reverb becomes a rumble the moment you play low, so it is never wanted open. On an instrument built from 32 partials the interest is above there anyway.

## Notes on CPU

Polyphony is the multiplier that matters, since eight voices means 256 sine oscillators. Measured on one core of an x86 container at 48 kHz with every modulator running:

| Voices | Oscillators | Load |
|---|---|---|
| 1 | 32 | about 1% |
| 8 | 256 | about 7% |
| 16 | 512 | 14 to 15% |

Those figures are with everything running at once: both LFOs, drift, velocity, aftertouch, the meters and the noise channel. The two master effects add well under 1% on top of that, whatever the polyphony, since they work on the sum rather than per voice.

That leaves enough headroom for the engine to stay a plain bank of oscillators with nothing clever in the signal path. What keeps it cheap:

- A 4096-point interpolated sine table, measured error -129 dB, instead of `std::sin` per sample.
- LFOs, envelope coefficients and gain ramps update once per 32-sample control block. Only the oscillator and envelope run at sample rate.
- Silent partials, whether muted, faded out above Nyquist or sitting at zero, skip the oscillator entirely and only advance their envelope.

The attack knob is logarithmic rather than skewed towards a midpoint, so equal turns are equal ratios: the step from 1 ms to 2 ms gets the same travel as the one from 100 ms to 200 ms. That matters here because its range spans four and a half decades, and fitting a power curve through a midpoint across that needs an exponent of about 7.4, which leaves the bottom eighth of the knob flat enough that nudging it does nothing. Measured, that dead travel went from 13.7% to 0.1%, and 30 ms stayed within half a percent of the middle where it was. The trade is that sub-millisecond attacks now occupy 9% of the travel rather than 27%, of which half was unusable anyway. A test asserts the dead travel, so the next wide range somebody adds cannot quietly reintroduce it.

The other time controls still use the older curve and still have a few percent of dead travel each: 7.7% on decay and release, 14.8% on swell. They are lower stakes than the attack, and changing them would move where every existing patch sits on those knobs.

### Where a partial starts

PHASE sets where in its own cycle each partial begins, from 0 to 360 degrees, when phase reset is on. It defaults to zero, which is a rising zero crossing, and zero is the softest onset a partial can possibly have: from there it cannot reach its own peak until a quarter of its period has gone by.

That is longer than the shortest attack available for most of the keyboard:

| Note | Quarter cycle | What sets the onset |
|---|---|---|
| A1, 55 Hz | 4.55 ms | the note's own period |
| A2, 110 Hz | 2.27 ms | the note's own period |
| C4, 262 Hz | 0.96 ms | the note's own period |
| A4, 440 Hz | 0.57 ms | the note's own period |
| A5, 880 Hz | 0.28 ms | the attack knob |

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

| Render rate | Load | Against full rate |
|---|---|---|
| host, 48 kHz | 7.8% | |
| 22.05 kHz | 3.8% | 48% |
| 11.025 kHz | 2.0% | 25% |
| 8 kHz | 1.5% | 19% |

The aliasing is the point, so the Nyquist guard comes off with it. A converter running at 8 kHz does not quietly mute everything above 4 kHz, it wraps it back down, and so does this: the 32nd partial of A3 sits at 7040 Hz and reappears at 960. The original frequency stays faintly audible above it, because holding a sample puts an image either side of the rate, about seven times quieter at that spacing.

Modulation gets coarser along with everything else. The LFOs, drift and gain ramps still land once per 32-sample control block, which at 8 kHz is 4 ms rather than 0.7. Everything is still in the right place in real time, since every coefficient is derived from the rate being rendered at, so a one-second attack is still a second.

**Bit depth** picks from 16 bits down to 2, quantising to 2^(n-1) steps either side of zero. It sits with the rate, ahead of the echo, the reverb and the master fader, so those behave like outboard on a lo-fi source rather than being crushed themselves. It does not clip: that is the safety clipper's job further down, and a bit-depth setting that also distorted would be doing something the panel does not mention.

Both settings are stored with a preset, since at that point they are part of the sound rather than part of the setup.

## Licensing

Overtonium is released under the [AGPLv3](LICENSE).

That follows from what it is built on. The JUCE 8 framework modules are dual-licensed under the AGPLv3 or a paid commercial licence, and a plugin is a single combined work with them, so the combination has to be conveyed under terms the AGPL allows. Taking the AGPL for this project too is the simplest way to be exactly what it says it is. The network clause the AGPL is known for, section 13, only applies to software users interact with remotely over a network, which an audio plugin is not, so in practice it reads as the GPL does.

If you want to build on this and ship something closed-source, that needs a commercial JUCE licence from the JUCE side and a separate arrangement on this side, since the AGPL does not permit it.

There is no splash screen to worry about. JUCE 6 and 7 had one, and disabling it was the thing that required a paid licence. JUCE 8 removed it, and `JUCE_DISPLAY_SPLASH_SCREEN` is now ignored with a compiler warning if you define it.

## Layout

```
Resources/
  logo.png            the overtonium wordmark, compiled into the binary
  dehli-musikk.svg    the maker's mark, vector since it is drawn small
Source/
  dsp/            JUCE-free DSP core, unit tested standalone
    Harmonics.h     tuning table and blend maths
    SineTable.h     interpolated sine lookup
    Drift.h         seeded PRNG and the smooth random contour
    Envelope.h      per-partial delay, ADSR and the two-stage key-off
    Params.h        plain-data parameter snapshot
    Voice.*         32 partials, one note
    TapeEcho.*      the master echo
    Reverb.*        the master reverb, a feedback delay network
    SynthEngine.*   voice pool, allocation, stealing, effects, master stage
  PluginParameters.*  APVTS layout, 710 parameters, and the audio-thread snapshot
  Presets.*           factory presets
  PluginProcessor.*   MIDI handling, sample-accurate rendering, state
  PluginEditor.*      window, zoom, LINK, gutter
  UI/                 theme, look and feel, channel and noise strips, top bar
Tests/
  dsp_test.cpp            standalone DSP tests and CPU benchmark
  plugin_runtime_test.cpp headless plugin integration tests
```
