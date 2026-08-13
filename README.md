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
| ENVELOPE A D S R | 0.5 ms to 5 s, 1 ms to 20 s, 0 to 100%, 1 ms to 20 s | Exponential decay and release |
| AMP MOD rate and depth | 0.01 to 30 Hz, 0 to 100% | Per-partial tremolo |
| VELOCITY | 0 to 100% | How much key velocity scales this partial |
| M and S | | Mute wins over solo |
| LEVEL | -inf to 0 dB | Square-law fader |

Velocity being per partial is what lets a soft note be a different timbre rather than just a quieter one. Set the fundamental to 0% and the upper partials to 100% and the tone opens up as you play harder, which is roughly what a struck string or bar does. *Struck Bell* and *Odd Harmonics* ship with that curve already dialled in.

Global controls in the top bar:

- **TUNE ALL** and **VEL** are endless relative macros. See below.
- **MASTER**, **SPREAD** (fans partials across the stereo field) and **BEND**
- **PRESET**, **POLY** (1 to 16 voices) and **ZOOM**
- **LINK** gangs the strips, so dragging any knob moves that knob on all 32 channels
- **PHASE** resets partial phase on each note for a coherent, percussive attack
- **CLIP** soft-clips the output. Worth leaving on when you push 32 faders up

Double-clicking any knob restores its default. Hovering a harmonic number shows its interval and exact cent deviation.

### Relative macros

TUNE ALL, VEL and LINK all move 32 values at once, and all three do it relatively. They apply an offset to wherever each strip already sits instead of dragging everything to one shared value, so a spectrum you have shaped by hand keeps its shape.

The two macro knobs are endless. They have no absolute position, they show how far you have turned them during the current drag, and they spring back to centre when you let go, so the next drag starts fresh from wherever the strips now are. A full turn in either direction still covers the entire range, so "everything to just intonation" or "everything to equal" remains one drag away.

The offset is always measured from the values captured when the drag started, so returning the knob to where you began restores the strips exactly, even if some of them hit an end stop along the way.

## Notes on CPU

Polyphony is the multiplier that matters, since eight voices means 256 sine oscillators. Measured on one core of an x86 container at 48 kHz with every modulator running:

| Voices | Oscillators | Load |
|---|---|---|
| 1 | 32 | about 1% |
| 8 | 256 | 6 to 8% |
| 16 | 512 | 13 to 16% |

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
    Envelope.h      per-partial ADSR
    Params.h        plain-data parameter snapshot
    Voice.*         32 partials, one note
    SynthEngine.*   voice pool, allocation, stealing, master stage
  PluginParameters.*  APVTS layout, 422 parameters, and the audio-thread snapshot
  Presets.*           factory presets
  PluginProcessor.*   MIDI handling, sample-accurate rendering, state
  PluginEditor.*      window, zoom, LINK, gutter
  UI/                 theme, look and feel, channel strip, top bar
Tests/
  dsp_test.cpp            standalone DSP tests and CPU benchmark
  plugin_runtime_test.cpp headless plugin integration tests
```
