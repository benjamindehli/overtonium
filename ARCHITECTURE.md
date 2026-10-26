# Architecture

Where things live and why they are arranged this way. For how to build and test, see [CONTRIBUTING.md](CONTRIBUTING.md). For what the instrument does and why it does it that way, see [DESIGN.md](DESIGN.md).

## The shape of it

There are three layers, and the boundary between the first two is the important one.

**A DSP core with no JUCE in it.** Everything under `Source/dsp/` compiles from a bare compiler with no framework and no third-party headers. A CI job checks this. It builds the core with `c++ -std=c++17 -Wall -Wextra -Werror` and runs its tests, so an accidental `#include <juce_core/...>` fails the build. This means the part that does the arithmetic can be tested without a plugin host, a display or a build system.

**A JUCE layer** that owns parameters, MIDI, state and the editor. `PluginProcessor` handles notes and rendering, `PluginParameters` declares the 850 parameters and flattens them into a plain snapshot the audio thread can read, and `Presets` holds the thirty-eight factory patches.

**A UI layer** under `Source/UI/`, which draws the mixer and knows nothing about how sound is made.

## Threading

There are two threads, and neither waits for the other.

The audio thread reads a snapshot of the parameters rather than the `AudioProcessorValueTreeState` itself. It allocates nothing. A host may hand over a bigger block than the one it promised in `prepareToPlay`. Growing a buffer to fit would take the allocator's lock on the audio thread, where whatever the message thread is doing can make it wait. So the block is cut into pieces that fit the existing scratch buffers. A test renders the same passage through both paths and compares them sample by sample.

The message thread owns the editor and the state tree. Values the editor displays, such as meters, lamps and voice counts, are published as atomics by the render loop, and the editor polls them. Adaptive's result for the held chord is too big for an atomic, so the engine publishes it through a `TripleBuffer` in `dsp/AdaptiveSnapshot.h`: three copies swapped with one atomic exchange each way, so the audio thread never waits and the editor always reads a whole copy.

Work that arrives over MIDI but belongs to the message thread is handled the same way. A program change is a preset load, which moves hundreds of parameters and reports each one to the host. So the audio thread stores the number in an atomic and a 20 Hz timer on the processor picks it up. Nothing is posted from the audio thread, which would take the message queue's lock.

There is also a third lifetime, and it is the easiest one to get wrong. A plugin is a library the host loads and later unloads, so anything with static lifetime is destroyed at unload rather than when the last instance is removed. By then JUCE has shut its event system down, and on Windows the destructors run under the loader lock. A destructor that needs another thread to finish, or hands something back to a graphics driver whose threads are waiting on that same lock, deadlocks the host instead of only delaying the unload. So the rule is that a static holds plain memory and nothing else: tables, identifiers and lists of names. The test is whether its destructor touches anything outside the heap, not whether it owns a thread. A `juce::Image` can be a texture on a graphics device, which is why the panel's grain tile lives in a `juce::SharedResourcePointer` held by each look and feel and goes with the last window. `juce::PropertiesFile` privately inherits `juce::Timer`, and every `Timer` holds a reference to the single thread JUCE runs timers on, so a settings file kept in a static joins a thread at unload with nothing in the code to show it. The picture caches the controls draw from belong to the controls for the same reason.

## Layout

```
packaging/
  macos/              pkgbuild and productbuild into a .pkg with a chooser
  windows/            Inno Setup script for the installer .exe
Resources/
  logo.png            the overtonium wordmark, compiled into the binary
  dehli-musikk.svg    the maker's logo, vector since it is drawn small
  icon.png            the standalone build's program icon, 1024 square
Source/
  dsp/            JUCE-free DSP core, unit tested standalone
    Harmonics.h     tuning table and blend maths
    Temperament.h   how the keyboard is tuned, the fixed temperaments and
                    Adaptive's chain of fifths
    SineTable.h     interpolated wavetable lookup, and the plain sine
    Character.h     the oscillator characters, as band-limited tables
    Measured.h      what the five recorded characters were measured to be
    Drift.h         seeded PRNG and the smooth random contour
    Lfo.h           the LFO shapes
    Envelope.h      per-partial delay, ADSR and the two-stage key-off
    Velocity.h      what key velocity does to the fader, the attack and the
                    delay
    Params.h        plain-data parameter snapshot
    Exact.h         exact equality, for comparisons meant to be exact
    Voice.*         32 partials, one note
    Halfband.h      the half-band filter the drive oversamples through
    BusDrive.*      the drive each character puts on the summed mix
    TapeEcho.*      the master echo as a tape loop
    BucketEcho.*    the same as a bucket-brigade delay
    DigitalEcho.*   the same as a ping-pong digital delay
    Reverb.*        the master reverb as a feedback delay network
    PlateReverb.*   the same as a modulated plate
    SpringReverb.*  the same as a tray of springs
    KarplusString.* the string the noise channel strikes, and each of the
                    resonator's 88
    SympatheticStrings.*  the resonator: 88 sympathetic strings, before the bus drive
    Wobble.*        a warped-record wobble on the whole instrument
    OutputStage.*   the six machines at the end of the signal chain
    Console.*       the console machine's transformer, compressor and tape stage
    SynthEngine.*   voice pool, allocation, stealing, effects, master stage
    AdaptiveSnapshot.h  Adaptive's result for the held chord, and the
                    triple buffer that hands it to the editor
  PluginParameters.*  APVTS layout, 850 parameters, and the audio-thread snapshot
  Presets.*           factory presets
  MidiLearn.*         which controller moves which control
  PluginProcessor.*   MIDI handling, sample-accurate rendering, state
  PluginEditor.*      window, zoom, LINK, gutter
  UpdateCheck.*       the opt-in check for a new version, in the background,
                      and the settings file its answer is kept in
  UI/                 theme, look and feel, channel and noise strips, top bar,
                      the Playing and Tuning bars, the Settings lists both
                      share, macro panel
    Notice.*          the in-window dialog the update question and a new
                      release are shown in
    PaintCache.h      the last two looks of a control, kept as pictures
Tests/
  dsp_test.cpp            standalone DSP tests and CPU benchmark
  plugin_runtime_test.cpp headless plugin integration tests
  music_utils_chord_types.h  the 125 chord types Adaptive is tested on
Tools/
  render_docs_images.cpp  renders the screenshots on the site
  render_docs_audio.cpp   renders the clips on the site
  preset_to_code.cpp      turns a saved preset into a factory preset's code
  build_site.py           writes the parts every page shares, from site_data.py
  compose_cards.py        composes the social cards
.github/
  workflows/          build and test on every push, release on a tag, and
                      tell IndexNow which pages changed once they are live
  scripts/            the checks those run that are not inline
release-notes/        one file per version, read by the release workflow
docs/                 the project page, one folder per URL
```

## One layout function

Every column in the mixer is the same list of rows, and all of them come from `layoutRows` in `Source/UI/Theme.cpp`. The caption gutter, the 32 channel strips and the noise strip each call it with their own rectangle and get back a `RowBounds` indexed by the `Row` enum.

That is why the captions on the left always line up with the knobs on the right, and it is why folding a section away is a small change: the rows in a folded section are given zero height in one place and the gutter, the strips, the lamps, the hover highlight and the repaint bands all follow.

It also means rows belong to the whole mixer. Folding a group on one channel and not the next would put every row below it out of line with the gutter captions, which are the only labels the knobs have.

## Drawing economics

The meters and lamps are the only things that move on their own, so they set what drawing costs while a note plays. Three decisions keep that cost down:

- **Nothing repaints itself.** Each meter and lamp hands the band that changed up to the editor, which collects all of them and invalidates a few rectangles once.
- **Merged by row, not by neighbour.** Lamps sit at the same heights on every strip, so merging across a row gives a thin wide band. Merging by proximity would pair a lamp at the top of one strip with a meter at the bottom of it. `mergeIntoRows` and `coalesceRegions` in `Theme.cpp` are the two passes, and the comments there carry the measurements.
- **Segmented rather than continuous**, so the display only changes when a level crosses a boundary rather than on every frame in which it moves at all.

Soloing a channel, scrolling the mixer and resizing the window redraw every strip at once, which is a different cost: hundreds of controls that mostly look exactly as they did. Three things keep that down:

- **Controls are copied, not drawn.** Knobs, faders, the shape buttons, the segment readouts and the lamp buttons each keep their last two looks as pictures in a `PaintCache`, at the display's own resolution, keyed by a hash of everything their drawing reads. Two looks, because soloing swaps each control between its lit and its unlit look.
- **The strips are opaque**, so JUCE does not repaint the panel behind each one first.
- **One background for every strip.** The look and feel keeps the strip's gradient and grain as a picture for each size, so 32 strips share it.

`JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS` is set for Apple builds in `CMakeLists.txt`. Without it CoreGraphics answers a list of scattered dirty rectangles by redrawing the one rectangle enclosing them, which for this layout is the whole window, and none of the savings above take effect.

## Parameters and state

Parameters are declared once in `PluginParameters.cpp` and reached by string id, built from a suffix and a channel index by `oscParamId`. Adding a channel control means adding it there, giving it a `Row`, and placing that row in a section. A control on the top bar has no `Row` and goes into a group on the bar instead. Either way, two things then fail until they are updated: the expected parameter count in `Tests/plugin_runtime_test.cpp`, and `docs/parameters.json`, which `overtonium_runtime_test --write-parameters` writes.

The saved state is the `AudioProcessorValueTreeState` tree plus a few properties written alongside it for things that belong to the session rather than the patch: window size, zoom, LINK settings, which sections are folded, which of the Playing and Tuning bars are open, where the parameter section is scrolled to, the MIDI Learn bindings, the name of the loaded preset and the current program. Presets deliberately carry less than the state does, so loading a sound never moves your window or your temperament.

The factory presets are generated C++ rather than data files, converted from patches saved on the panel. `factoryCode` in `Presets.cpp` writes that code, diffing against `neutralBase` so a preset only spells out what it changes.

## Formats

`juce_add_plugin` builds VST3 and standalone everywhere, an Audio Unit on macOS and LV2 on Linux. Most of the plugin does not know which format it is running in, with one deliberate exception. `getNumPrograms` reports the thirty-eight factory presets to the Audio Unit alone, because Logic reads its preset menu from there. A program count above one would make the VST3 wrapper publish an automatable parameter that rewrites every other parameter when it moves.
