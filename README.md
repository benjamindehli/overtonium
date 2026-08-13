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

They cover the tuning table, blend endpoints, sine table accuracy, the anti-aliasing guard, envelope behaviour, mute and solo, click-free parameter changes, voice allocation and modulation, and they finish by printing a CPU benchmark.

There is a second target, `overtonium_runtime_test`, which builds the plugin sources as a console app and exercises parameter wiring, MIDI handling, the factory presets, bus layouts and state round-tripping. It never opens an editor, so it runs on a machine with no display. Both targets are registered with CTest, so `ctest` from the build directory runs them together.

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

Strips are colour-coded by interval class, which keeps the structure of the series visible while you scroll: octaves gold, fifths cyan, major thirds green, sevenths orange, and so on.

## Controls

Each of the 32 strips has, top to bottom:

| Control | Range | Notes |
|---|---|---|
| TUNE | equal to just | Readout shows the resulting cent offset |
| PITCH MOD rate and depth | 0.01 to 30 Hz, 0 to 200 cents | Per-partial vibrato |
| DRIFT | 0 to 25 cents | Smooth random pitch wander. See below |
| ENVELOPE A D S R | 0.5 ms to 5 s, 1 ms to 20 s, 0 to 100%, 1 ms to 20 s | Exponential decay and release |
| AMP MOD rate and depth | 0.01 to 30 Hz, 0 to 100% | Per-partial tremolo |
| VELOCITY | 0 to 100% | How much key velocity scales this partial |
| AFTERTOUCH | 0 to 100% | How much key pressure adds to this partial |
| M and S | | Mute wins over solo |
| LEVEL | -inf to 0 dB | Square-law fader |

Velocity being per partial is what lets a soft note be a different timbre rather than just a quieter one. Set the fundamental to 0% and the upper partials to 100% and the tone opens up as you play harder, which is roughly what a struck string or bar does. *Struck Bell* and *Odd Harmonics* ship with that curve already dialled in.

Aftertouch works the same way but **adds** to the fader instead of scaling it, and it ignores velocity entirely. That means a strip with its fader all the way down is silent until you lean on the key, and then it fades in under your finger. Put a few upper partials on aftertouch and the note grows brighter the harder you press, without touching the partials you left alone. Both channel pressure and polyphonic aftertouch are accepted, and whichever is higher wins. Pressure is smoothed over about 15 ms, so seven-bit MIDI does not step the gain.

Global controls in the top bar:

- **MASTER**, **SPREAD** and **BEND**, the three values that are genuinely single and so are ordinary absolute knobs
- **PRESET**, **POLY** (1 to 16 voices) and **ZOOM**
- **LINK** gangs the strips, so dragging one channel's knob moves that knob on all 32. The master channel below does the same job more directly.
- **PHASE** resets partial phase on each note for a coherent, percussive attack
- **CLIP** soft-clips the output. Worth leaving on when you push 32 faders up

Double-clicking any knob restores its default. Hovering a harmonic number shows its interval and exact cent deviation.

### The master channel

Between the row labels and the scrolling mixer sits a master channel marked ALL. It carries the same controls in the same rows as the 32 partial strips, so every knob in the instrument has a global counterpart sitting directly opposite its own row. It does not scroll, so it stays in view however far along the series you have gone.

Every control on it is relative. It applies an offset to wherever each strip already sits rather than dragging everything to one shared value, so a spectrum you have shaped by hand keeps its shape. LINK in the top bar does the same thing from any individual channel.

The controls are endless. They have no absolute position, they show how far you have turned them during the current drag, and they spring back to centre when you let go, so the next drag starts fresh from wherever the strips now are. A full turn in either direction still covers the entire range, so "everything to just intonation" or "everything to equal" remains one drag away.

The offset is always measured from the values captured when the drag started, so returning a control to where you began restores the strips exactly, even if some of them hit an end stop along the way.

Its **M** mutes or unmutes all 32 channels. Its **S** lights whenever anything is soloed and clicking it clears every solo, since soloing everything would be the same as soloing nothing.

### Drift

DRIFT adds a slow random wander to a partial's pitch. It is aimed at chorusing rather than vibrato, so the range stops at 25 cents and the rates sit between 0.08 and 1.1 Hz, well below anything you would hear as a pitch wobble. Even a few cents across all 32 strips thickens the tone noticeably.

Every partial of every note draws its own rate, log distributed across that range, and its own contour. Nothing locks together, so repeated notes never land on the same detuning and the 32 partials never move as one.

The contour is genuinely smooth rather than stepped. Random points are drawn at the chosen rate and joined with a Catmull-Rom spline, which gives a continuous curve through them. Sample and hold would step between values, and lowpassed noise would wander in amplitude as well as in value. The tests assert the largest step stays under 0.02 of full scale, that two streams are uncorrelated, and that two consecutive notes diverge.

*Slow Pad* uses 7 cents and *Shimmer* 12 cents.

### Stereo spread

SPREAD places the partials in mirrored pairs. Harmonics 1 and 2 stay in the centre, 3 and 4 sit opposite each other, and so on out to 31 and 32 at the edges.

Pairing does two jobs. Neighbouring partials have near-identical levels in any normal spectrum, so putting each pair on opposite sides keeps the image centred whatever shape you dial in. And because every position has a mirror, no partial ends up hard panned with nothing facing it, which is what makes a spread sound like it is leaning rather than widening.

The width grows as a square root rather than as a straight fan, because a spectrum that rolls off keeps nearly all its energy in the first few partials. A linear fan leaves exactly those bunched in the middle and the control does very little you can hear. The tests measure both properties, asserting that channel imbalance stays under 0.2 dB and that the partials actually carrying the energy get placed.

## Notes on CPU

Polyphony is the multiplier that matters, since eight voices means 256 sine oscillators. Measured on one core of an x86 container at 48 kHz with every modulator running:

| Voices | Oscillators | Load |
|---|---|---|
| 1 | 32 | about 1% |
| 8 | 256 | 6 to 8% |
| 16 | 512 | 13 to 16% |

Those figures are with every modulator running at once: both LFOs, drift, velocity and aftertouch.

That leaves enough headroom for the engine to stay a plain bank of oscillators with nothing clever in the signal path. What keeps it cheap:

- A 4096-point interpolated sine table, measured error -129 dB, instead of `std::sin` per sample.
- LFOs, envelope coefficients and gain ramps update once per 32-sample control block. Only the oscillator and envelope run at sample rate.
- Silent partials, whether muted, faded out above Nyquist or sitting at zero, skip the oscillator entirely and only advance their envelope.

Partials fade out as they approach Nyquist. Without that, the 32nd harmonic of a high note would fold back down as aliasing, since it lands near 67 kHz for a C7. Measured alias images sit at -122 dB.

## JUCE licensing

JUCE 8 is dual-licensed under AGPLv3 or a commercial licence. This project leaves `JUCE_DISPLAY_SPLASH_SCREEN` at its default, which is enabled, and that is what the licence requires unless you either hold a paid JUCE licence or release this project under the AGPLv3. Decide which applies to you before you disable it in `CMakeLists.txt`.

## Layout

```
Source/
  dsp/            JUCE-free DSP core, unit tested standalone
    Harmonics.h     tuning table and blend maths
    SineTable.h     interpolated sine lookup
    Drift.h         seeded PRNG and the smooth random contour
    Envelope.h      per-partial ADSR
    Params.h        plain-data parameter snapshot
    Voice.*         32 partials, one note
    SynthEngine.*   voice pool, allocation, stealing, master stage
  PluginParameters.*  APVTS layout, 486 parameters, and the audio-thread snapshot
  Presets.*           factory presets
  PluginProcessor.*   MIDI handling, sample-accurate rendering, state
  PluginEditor.*      window, zoom, relative macros, LINK, gutter
  UI/                 theme, look and feel, channel strip, master strip, top bar
Tests/
  dsp_test.cpp            standalone DSP tests and CPU benchmark
  plugin_runtime_test.cpp headless plugin integration tests
```
