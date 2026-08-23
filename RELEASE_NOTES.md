# Overtonium 1.0.0

A 32-partial additive synthesiser laid out like a 32-channel mixer. Every channel is one sine oscillator locked to a harmonic of the played note, and every channel has its own tuning, envelope and modulation.

The control worth reaching for first is TUNE. It sweeps each partial continuously between equal temperament and just intonation. At the just end the partial sits at an exact integer ratio with the fundamental and the whole stack fuses into a single timbre. At the equal end each partial snaps to the nearest 12-TET semitone and the same stack smears into a chord. The factory presets *Just Saw* and *Equal Saw* are identical except for that one control, and they sound nothing alike.

## What is in it

- 32 sine partials plus a noise channel, each with its own fader, tuning, envelope, tremolo, pitch modulation and pan
- TUNE, a continuous blend between equal temperament and just intonation, per partial
- Six keyboard temperaments on any root: equal, just major, Pythagorean, quarter-comma meantone, Werckmeister III and Young
- Inharmonic stretch and keyboard tracking
- Per-partial drift, a random walk that detunes each partial independently, and wobble, the same idea applied to the whole instrument
- MPE, with pitch bend and pressure per note, alongside ordinary MIDI
- A tape echo with two independent paths, a feedback delay network reverb and a lo-fi converter
- Channel ganging, so a change on one channel can be applied across a selection
- 16 factory presets: Big Saw, Cathedral, Drawbar Organ, Equal Saw, Glass Armonica, Init, Just Saw, Lo-fi, Music Box, Odd Harmonics, Shimmer, Slow Pad, Struck Bell, Tape Choir, Vibraphone and Wurli

## Installing

Take the installer for your platform. The zip beside it holds the same builds loose, for anyone without administrator rights or with plugin folders of their own.

| Platform | Installer | Formats |
|---|---|---|
| macOS | `.pkg`, with a chooser | VST3, Audio Unit, Standalone |
| Windows | `.exe` | VST3, Standalone |
| Linux | none, use the zip | VST3, LV2, Standalone |

The macOS build is a universal binary and runs on both Apple Silicon and Intel. Logic and GarageBand only look for new Audio Units when they start, so restart them after installing.

On Linux the zip's contents go in `~/.vst3` and `~/.lv2`.

## What has actually been tried

All three platforms are built and run through the test suite on every push, and macOS is the only one where the plugin has been loaded into a host. The Audio Unit and the VST3 pass `auval` and pluginval at strictness 8 there. Nobody has run the Windows or Linux builds in a DAW yet, so treat those as untried and please open an issue with whatever you find.

## macOS

The installer is signed and notarised, so it opens without a warning. It asks which of the three formats you want and installs only those.

## Windows will warn you

The Windows installer is not signed, so SmartScreen says the publisher is unknown. Choose "More info" and then "Run anyway". If you would rather not, build from source: the readme says how, and it takes one CMake command.

## Licence

AGPLv3, which follows from JUCE. The whole source is in this repository.
