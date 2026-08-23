# Architecture

Where things live and why they are arranged this way. For how to build and test, see [CONTRIBUTING.md](CONTRIBUTING.md). For what the instrument does, see the [readme](README.md).

## The shape of it

Three layers, and the boundary between the first two is the one that matters.

**A DSP core with no JUCE in it.** Everything under `Source/dsp/` compiles from a bare compiler with no framework and no third-party headers. That is checked rather than asserted: a CI job builds it with `c++ -std=c++17 -Wall -Wextra -Werror` and runs its tests, so an accidental `#include <juce_core/...>` fails the build. The benefit is that the part with the arithmetic in it can be tested without a plugin host, a display or a build system.

**A JUCE layer** that owns parameters, MIDI, state and the editor. `PluginProcessor` handles notes and rendering, `PluginParameters` declares the 710 parameters and flattens them into a plain snapshot the audio thread can read, and `Presets` holds the sixteen factory patches.

**A UI layer** under `Source/UI/`, which draws the mixer and knows nothing about how sound is made.

## Threading

Two threads, and the rule is that neither waits for the other.

The audio thread reads a snapshot of the parameters rather than the `AudioProcessorValueTreeState` itself. It allocates nothing. A host may hand over a bigger block than the one it promised in `prepareToPlay`, and growing a buffer to fit would take the allocator's lock on the audio thread, where whatever the message thread is doing can make it wait, so the block is cut into pieces the existing scratch already holds. A test renders the same passage through both paths and compares them sample by sample.

The message thread owns the editor and the state tree. Values the editor displays, meters, lamps and voice counts, are published as atomics by the render loop and polled, rather than pushed.

## Layout

```
packaging/
  macos/              pkgbuild and productbuild into a .pkg with a chooser
  windows/            Inno Setup script for the installer .exe
Resources/
  logo.png            the overtonium wordmark, compiled into the binary
  dehli-musikk.svg    the maker's mark, vector since it is drawn small
  icon.png            the standalone build's program icon, 1024 square
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
docs/                 the project page, one folder per URL
```

## One layout function

Every column in the mixer is the same list of rows, and all of them come from `layoutRows` in `Source/UI/Theme.cpp`. The caption gutter, the 32 channel strips and the noise strip each call it with their own rectangle and get back a `RowBounds` indexed by the `Row` enum.

That is why the captions on the left always line up with the knobs on the right, and it is why folding a section away is a small change: the rows in a folded section are given zero height in one place and the gutter, the strips, the lamps, the hover highlight and the repaint bands all follow.

It also means rows are a whole-mixer property. Folding a group on one channel and not the next would put every row below it out of step with the only thing naming the knobs.

## Drawing economics

The meters and lamps are the only things that move on their own, so they set the cost of playing a note. Three decisions carry most of that:

- **Nothing repaints itself.** Each meter and lamp hands the band that changed up to the editor, which collects all of them and invalidates a few rectangles once.
- **Merged by row, not by neighbour.** Lamps sit at the same heights on every strip, so merging across a row gives a thin wide band while merging by proximity pairs a lamp at the top of one strip with a meter at the bottom of it. `mergeIntoRows` and `coalesceRegions` in `Theme.cpp` are the two passes, and the comments there carry the measurements.
- **Segmented rather than continuous**, so the display only changes when a level crosses a boundary rather than on every frame in which it moves at all.

`JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` is set for Apple builds in `CMakeLists.txt`. Without it CoreGraphics answers a list of scattered dirty rectangles by redrawing the one rectangle enclosing them, which for this layout is the whole window, and none of the care above shows up.

## Parameters and state

Parameters are declared once in `PluginParameters.cpp` and reached by string id, built from a suffix and a channel index by `oscParamId`. Adding a control means adding it there, giving it a `Row`, and placing that row in a section.

The saved state is the `AudioProcessorValueTreeState` tree plus a few properties written alongside it for things that belong to the session rather than the patch: window size, zoom, LINK settings, which sections are folded, and the current program. Presets deliberately carry less than the state does, so loading a sound never moves your window or your temperament.

The factory presets are generated C++ rather than data files, converted from patches saved on the panel. `factoryCode` in `Presets.cpp` writes that code, diffing against `neutralBase` so a preset only spells out what it changes.

## Formats

`juce_add_plugin` builds VST3 and standalone everywhere, an Audio Unit on macOS and LV2 on Linux. Most of the plugin does not know which it is inside, with one deliberate exception: `getNumPrograms` reports the sixteen factory presets to the Audio Unit alone, because Logic reads its preset menu from there, while a program count above one makes the VST3 wrapper publish an automatable parameter that would rewrite every other parameter when it moves.
