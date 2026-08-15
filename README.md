# Overtonium

A 32-partial additive synthesiser laid out like a 32-channel mixer. Every channel is one sine oscillator locked to a harmonic of the played note, and every channel has its own tuning, envelope and modulation.

The control worth reaching for first is TUNE. It sweeps each partial continuously between equal temperament and just intonation. At the just end the partial sits at an exact integer ratio with the fundamental and the whole stack fuses into a single timbre. At the equal end each partial snaps to the nearest 12-TET semitone and the same stack smears into a chord. The factory presets *Just Saw* and *Equal Saw* are identical except for that one control, and they sound nothing alike.

- **Formats:** VST3, AU (macOS), Standalone, LV2 (Linux)
- **Platforms:** macOS (universal), Windows, Linux
- **Framework:** JUCE 8
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

There is a second target, `overtonium_runtime_test`, which builds the plugin sources as a console app and exercises parameter wiring, MIDI handling, the factory presets, user preset round-tripping, bus layouts and state round-tripping. It never opens an editor, so it runs on a machine with no display. Both targets are registered with CTest, so `ctest` from the build directory runs them together.

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

**Saving your own.** The preset menu writes to `Dehli Musikk/Overtonium/Presets` under your user application data folder, one small `.ovtpreset` file each, and lists whatever it finds there under the factory list. The menu reads the folder every time it opens, so a preset saved a moment ago is in it and one deleted in Finder is not.

A preset holds parameter values and nothing else. Not the window size, the zoom or the LINK settings, since loading a sound should not move your window or change your tools. Values are stored plain rather than normalised, so a preset survives a parameter's range being widened later, and anything a file does not mention keeps its default rather than being reset, so a preset saved by an older build loads into a newer one without silently zeroing whatever was added in between. The tests cover both of those directly.

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

Strips are colour-coded by interval class, which keeps the structure of the series visible while you scroll. The twelve classes sit in chromatic order along a narrow band running from blue at the octave, through magenta and red in the middle, to yellow at the major seventh. Octaves are therefore blue, fifths rose, major thirds magenta, sevenths amber and yellow.

The band is deliberately narrow, a crop from the middle of a full blue to yellow sweep, so 32 channels read as one family rather than as a rainbow. Saturation and value fall towards the warm end because yellow reads far brighter than blue at the same nominal value, which keeps the sevenths from visually swamping the octaves. Nothing in the band enters green or cyan, which is where the accent used by the global controls lives, so chrome never reads as one of the channels.

## Controls

Each of the 32 strips has, top to bottom:

| Control | Range | Notes |
|---|---|---|
| TUNE | equal to just | Readout shows the resulting cent offset |
| PITCH MOD rate and depth | 0.01 to 30 Hz, 0 to 200 cents | Per-partial vibrato |
| DRIFT | 0 to 25 cents | Smooth random pitch wander. See below |
| ENVELOPE delay, A, D, S | 0 to 5 s, 0.5 ms to 5 s, 1 ms to 20 s, 0 to 100% | Exponential decay |
| KEY OFF swell, level, release | 0 to 5 s, 0 to 100%, 1 ms to 20 s | A second envelope for letting go. See below |
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

The envelope's delay stage holds a partial silent before its attack begins. Staggering it across the series makes the spectrum unfold rather than arrive all at once, which is how *Slow Pad* and *Shimmer* now open up. It is latched in samples at note-on, so moving the knob cannot retime a note already waiting, and releasing a key before the delay elapses cancels that partial rather than letting it burst in afterwards.

Aftertouch works the same way but **adds** to the fader instead of scaling it, and it ignores velocity entirely. That means a strip with its fader all the way down is silent until you lean on the key, and then it fades in under your finger, while a negative amount fades an open strip back out again. Put a few upper partials on positive aftertouch and the note grows brighter the harder you press, without touching the partials you left alone. Both channel pressure and polyphonic aftertouch are accepted, and whichever is higher wins. Pressure is smoothed over about 15 ms, so seven-bit MIDI does not step the gain.

The top bar holds everything that is not per partial, boxed into groups of things that work together, in signal order from left to right:

| Group | Contains |
|---|---|
| Preset | the preset menu: factory, saved, and somewhere to put the one you are working on |
| Settings | polyphony, bend range, phase reset and the safety clipper |
| Link | **LINK**, and what it reaches and how. See below |
| Echo | the tape echo. See below |
| Reverb | the reverb. See below |
| Output | **MASTER** and the stereo meter |
| View | **ZOOM** |

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

### The noise channel

Pinned on the right, after the series it does not belong to, is a noise channel marked NZ. It has the same envelope, tremolo, velocity, aftertouch, pan, mute, solo, level and meter as a partial, and takes part in solo alongside them. What it does not have is pitch, so the tuning, pitch modulation and drift rows are empty.

COLOUR takes the tuning row instead. It tilts the noise from dark rumble at one end, through flat in the middle, to bright hiss at the other. The middle really is flat rather than merely filtered less: the two halves of a complementary one-pole pair are summed at unity there, which reconstructs the original white noise exactly. The tests measure a high to low band ratio of 0.13 dark, 1.06 flat and 4.35 bright.

Each voice runs its own noise stream, so playing a chord does not layer 8 copies of the identical signal. Two voices measure about sqrt(2) times the energy of one, which is what independent sources give.

LINK gangs the 32 partials only. Its tuning, pitch modulation and drift have no noise counterpart, and having some ganged controls reach the noise channel while others could not would be worse than having none of them do.

### Meters

Each channel meters what that partial is actually putting out, on a decibel scale floored at -48 dB. Since it reflects the final gain, it shows the envelope, tremolo, velocity, aftertouch, the Nyquist fade and mute or solo all at once, so the spectrum can be watched evolving as a note decays.

The meter fills the fader's own track rather than sitting beside it. A fader that also drew its set level would be showing you something the cap already says, so the whole track is given over to output instead. The cap is drawn as translucent glass with bright edges, so the meter reads straight through it, and the knob pointers are drawn the same way so the value arc shows through them.


It reads the loudest instance of a partial across the sounding voices rather than the sum, so it shows the shape of the patch instead of pinning itself the moment you play a chord.

The cost is close to nothing on either side. The audio thread samples a value it has already computed once per 32-sample control block, which measured inside run-to-run noise on the benchmark. Each meter is its own component and only repaints when its bar moves a visible amount, so a held or silent patch does no drawing at all.

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

Each bar carries a peak hold that sits for about a second before falling, since what an output meter is mostly wanted for is what it hit a moment ago. The bar runs from teal through amber to orange as it approaches full scale, with the colours anchored to their decibel positions rather than stretching with the level, and there is a scale beneath marked at -48, -36, -24, -12, -6 and 0 dB.

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

Partials fade out as they approach Nyquist. Without that, the 32nd harmonic of a high note would fold back down as aliasing, since it lands near 67 kHz for a C7. Measured alias images sit at -122 dB.

## JUCE licensing

JUCE 8 is dual-licensed under AGPLv3 or a commercial licence. This project leaves `JUCE_DISPLAY_SPLASH_SCREEN` at its default, which is enabled, and that is what the licence requires unless you either hold a paid JUCE licence or release this project under the AGPLv3. Decide which applies to you before you disable it in `CMakeLists.txt`.

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
  PluginParameters.*  APVTS layout, 640 parameters, and the audio-thread snapshot
  Presets.*           factory presets
  PluginProcessor.*   MIDI handling, sample-accurate rendering, state
  PluginEditor.*      window, zoom, LINK, gutter
  UI/                 theme, look and feel, channel and noise strips, top bar
Tests/
  dsp_test.cpp            standalone DSP tests and CPU benchmark
  plugin_runtime_test.cpp headless plugin integration tests
```
