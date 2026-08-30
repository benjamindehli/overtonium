# Contributing

Bug reports, patches and reports of how it behaves in hosts nobody here has tried are all welcome. Windows and Linux in particular are built and tested on every push but have never been loaded into a DAW by anyone, so anything you find there is new information.

There is a form for each of those when you open an issue, including one for a host report that went fine. "It loaded and behaved" is worth filing on a platform nobody has run it on.

By taking part you agree to the [code of conduct](CODE_OF_CONDUCT.md). For how the code is arranged, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Before a pull request

Three things run in CI and all three are quick to check locally.

**The tests pass.** `ctest --test-dir build --build-config Release` runs both suites.

**Warnings are errors.** CI configures with `-DOVERTONIUM_WARNINGS_AS_ERRORS=ON`, which is off by default so that building from source on an untried compiler still produces a plugin. To see what CI will see, configure a separate tree rather than changing your working one:

```sh
cmake -B build-werror -DCMAKE_BUILD_TYPE=Release -DOVERTONIUM_WARNINGS_AS_ERRORS=ON
cmake --build build-werror --config Release
```

Compilers disagree about this. JUCE asks clang for a longer list than it asks gcc, and a newer clang asks for more than an older one, so a clean local build is not a promise that all three runners will agree.

**Formatting.** C++ is clang-format with stock LLVM style, no overrides, so `clang-format -i` on a file you touched is the whole of it. Everything else, meaning the docs, the readme and the workflows, is prettier:

```sh
npm ci
npm run format
```

`npm run format:check` is what CI runs. Prettier is pinned in `package-lock.json` because its output moves between versions, and a check that disagrees with your copy is worse than no check.

**pluginval.** CI loads the built VST3 into [pluginval](https://github.com/Tracktion/pluginval) at strictness 8 on all three platforms. It is a host that misuses the plugin on purpose, and it reaches things our own tests cannot, since they drive the processor directly rather than through a plugin format. To run it yourself, point it at a built or installed plugin:

```sh
pluginval --strictness-level 8 --validate ~/Library/Audio/Plug-Ins/VST3/Overtonium.vst3
```

It randomises its parameter values, so CI pins the seed on a push and leaves it to chance on a weekly scheduled run. A failure prints the seed it used, and passing that back with `--random-seed` reproduces it exactly. That is the only way some of these are reproducible at all.

**The VST3 validator.** Steinberg's own conformance suite, which is a different thing from pluginval: it checks the VST3 contracts rather than misusing the plugin through a host. CI builds it from a pinned revision of the SDK and runs it on Linux, where the build is cheapest. To run it yourself, build the `validator` target out of [the SDK](https://github.com/steinbergmedia/vst3sdk) with `-DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF`, which skips the parts it does not link, and point the result at a built `.vst3`.

## Style

Comments explain why, not what. The repository has a lot of them, and the ones worth having record a measurement, a rejected alternative or a trap: why the lamps merge by row and what the other way cost, why the scratch buffer has a floor, why the program count is what it is on each format. A comment restating the line below it is noise.

Prose in the repository describes the present rather than its own history. Git already holds what changed.

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

Building installs into your own folder under `~/Library`, while the `.pkg` installs system-wide under `/Library`. When both hold an `Overtonium.component`, macOS registers one of them and the host uses whichever that is, which need not be the one you just built. A host can then sit on a build months old while `auval` reports the new one, and every change you make appears to do nothing. If a change to the Audio Unit has no effect, check for a second copy before doubting the change:

```sh
ls -d /Library/Audio/Plug-Ins/Components/Overtonium.component \
      ~/Library/Audio/Plug-Ins/Components/Overtonium.component 2>/dev/null
```

Logic shows the version it loaded beside the plugin name, which is the quickest way to tell whether it is looking at your build.

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
  libxcursor-dev libxcomposite-dev libgl-dev libcurl4-openssl-dev
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

There is a second target, `overtonium_runtime_test`, which builds the plugin sources as a console app and exercises parameter wiring, MIDI handling, the factory presets, user preset round-tripping, bus layouts, undo and state round-tripping. The editors it opens are never put on the desktop, so it runs on a machine with no display. Both targets are registered with CTest, so `ctest` from the build directory runs them together.

The audio thread allocates nothing. A host is allowed to hand over a bigger block than the one it promised in `prepareToPlay`, and growing the scratch buffer to fit would take the allocator's lock on the audio thread, where whatever the message thread is doing can make it wait. The block is cut into pieces the scratch already holds instead, and the scratch is given a floor of 512 frames so a host that promises very little and delivers a lot is not cut into a great many of them. The seam has to be inaudible for that to be worth doing, so the test renders the same passage twice, once to a host that keeps its word and once to a host that promised a sixteenth of what it sent, and compares them sample by sample. They come out identical, sample for sample. The patch it uses has no random modulation in it, because drift is a random walk redrawn once per control block and those blocks fall in different places when a block is cut up, so a patch carrying drift would differ for reasons that have nothing to do with the cutting.

Both run in CI on macOS, Windows and Linux on every push and pull request, building the plugin itself along the way so a compile error in the JUCE half cannot pass unnoticed. A second job compiles the DSP core with the bare-compiler line above, and with `-Werror`, so the claim that it has no build system dependency is checked rather than asserted.

Pass `-DOVERTONIUM_INSTALL_AFTER_BUILD=OFF` to skip copying the built plugins into your user plugin folders, which is what CI does and what you want on any machine with no host to rescan them.

### Releases

Pushing a tag that starts with `v` builds the plugin on all three platforms and attaches the archives to a GitHub release:

```
git tag -a v1.4.0 -m "Overtonium 1.4.0"
git push origin v1.4.0
```

The notes for the release page come from `release-notes/`, one file per version, named after the tag without its `v`. So `v1.2.0` is written up in `release-notes/1.2.0.md`, and it has to exist and be headed `# Overtonium 1.2.0` before the tag is pushed. A file per release rather than one that gets rewritten means the notes for a version stay readable in the tree after the next one lands, and the tag picks its own out by name instead of taking whatever the file happens to say.

The heading is checked as well as the filename, since copying the previous release's notes to a new name and forgetting to edit them is exactly the mistake that would otherwise reach the release page.

Each platform gets an installer where there is a sensible one, and a zip holding every format it can build alongside it, plus the readme and the licence. macOS gets VST3, AU and Standalone as a universal binary covering Apple Silicon and Intel, Linux gets VST3, LV2 and Standalone, and Windows gets VST3 and Standalone. The tests run before anything is packaged, so a tag that does not pass them produces no release.

The same workflow can be started by hand from the Actions tab. That builds and uploads the archives against the run without publishing anything, which is how to exercise the packaging without spending a version number on it.

Each platform also gets an installer. macOS gets a `.pkg` built by `packaging/macos/build-pkg.sh`, with a chooser so VST3, the Audio Unit and the standalone can be taken separately, and Windows gets an `.exe` from `packaging/windows/overtonium.iss`. The zips stay alongside them, for anyone without administrator rights or with plugin folders of their own. Linux gets the zip only, since there is no install location a distribution would agree on.

The tag has to match `project(Overtonium VERSION ...)` in CMakeLists, and the workflow fails the release if it does not. The archive is named from the tag while the version a host displays comes from CMake, so without that check the two can disagree and nothing says so.

**Signing.** The macOS side signs and notarises when the repository has the secrets for it, and falls back to an ad-hoc signature when it does not, so the workflow can be rehearsed before any of them exist. Ad-hoc is what lets a bundle load at all on Apple Silicon, where an unsigned one is refused outright, but it is not notarisation and a download still has to be opened past Gatekeeper.

| Secret                                 | What it is                                                                        |
| -------------------------------------- | --------------------------------------------------------------------------------- |
| `APPLE_CERTIFICATE_P12`                | Developer ID Application certificate and key, as a base64 `.p12`                  |
| `APPLE_CERTIFICATE_PASSWORD`           | the password that `.p12` was exported with                                        |
| `APPLE_INSTALLER_CERTIFICATE_P12`      | Developer ID Installer certificate, base64 `.p12`, for signing the package itself |
| `APPLE_INSTALLER_CERTIFICATE_PASSWORD` | its export password                                                               |
| `APPLE_ID`                             | the Apple ID to submit for notarisation with                                      |
| `APPLE_APP_SPECIFIC_PASSWORD`          | an app-specific password for that Apple ID, not the account password              |
| `APPLE_TEAM_ID`                        | the ten-character team identifier                                                 |

Base64 a certificate with `base64 -i cert.p12 | pbcopy`. The two certificates are different: Application signs the bundles, Installer signs the `.pkg`, and notarisation refuses a package signed with the wrong one.

The Windows installer is not signed, so SmartScreen tells the user the publisher is unknown and they have to choose "More info" and then "Run anyway". A certificate that would remove that is an ongoing cost, and the release notes explain the warning instead.

### The project page

`docs/` is served by GitHub Pages from the `main` branch, one folder per URL so the site has addresses like `/tuning/` rather than `/tuning.html`. There is no generator and no build step: what is in the repository is what gets served. `.nojekyll` stops GitHub running Jekyll over it.

`llms.txt` is a summary of the project for language models, in the shape [llmstxt.org](https://llmstxt.org/) describes: what the instrument is, then links to each page with a line saying what is on it. It duplicates facts that live elsewhere, so it goes stale the same way the shared header does. Worth a glance when a release changes what the plugin can do, and `robots.txt` names it since no crawler is obliged to look for it.

The shared header, nav and footer are repeated in each page and have to be kept in step by hand. That is the price of having no build step, and it was the deliberate choice over a generator whose output would have to be committed anyway.

The screenshots are rendered rather than captured. The plugin has no dependency on a display, so `Tools/render_docs_images.cpp` builds the editor, plays a chord into it, hands the strips the levels the engine measured, and writes the window out with `createComponentSnapshot`. That means the pictures can be regenerated after a layout change instead of going stale, and it works on a machine with no window server.

```
cmake --build build --target overtonium_render_docs
./build/overtonium_render_docs_artefacts/overtonium_render_docs out
```

That writes the window, the six-channel detail and the top bar. Where each crop falls comes from `kGutterWidth`, `kStripWidth` and `TopBar::heightForWidth` rather than from numbers typed in, so a taller bar or a wider strip moves the crops with it instead of slicing one through the middle of a row. A second argument scales the window, and it is a genuine redraw rather than an upscale, which is where artwork needing the window larger than the page shows it comes from.

It writes PNG, because JUCE has no WebP encoder, so producing what the pages actually load is a second step with a lossless encoder:

```
cwebp -lossless out/overtonium.png -o docs/overtonium.webp
```

Through Pillow instead, it is `save(lossless=True, quality=100, method=6)`. The `quality` is not optional: on a lossless save it sets how hard the encoder works rather than how much it throws away, and leaving it at the default of 80 makes the window shot 341 KB where 100 makes it 204 KB, which is larger than the PNG it was supposed to beat.

Regenerating always shows a diff, even with nothing changed. DRIFT is random per voice, so the meters and the lamps land somewhere slightly different every run, which moves about one percent of the pixels. Everything that is a setting rather than a measurement comes out identical, the tuning readouts included.

The target is built by default so that it cannot quietly stop compiling, which is how the program that made the first set of pictures was lost. `-DOVERTONIUM_BUILD_TOOLS=OFF` skips it.

**The audio examples.** The tuning page is about things you hear, so it carries six clips: _Just Saw_ against _Equal Saw_ and the sweep between them, STRETCH taken from a harmonic series out past a piano, and one chord in equal against the same chord in Werckmeister III. The front page carries the first two. They are rendered by the plugin rather than recorded, on the same switch and for the same reason:

```sh
cmake --build build --target overtonium_render_docs_audio
./build/overtonium_render_docs_audio_artefacts/overtonium_render_docs_audio docs/audio
```

A note or a chord, held and let go, with a short fade at both ends so a file cannot start or stop on a step. A clip may sweep a control as it runs, which is what the TUNE and STRETCH ones do: the page claims those knobs move through a range rather than switching between two states, and a pair of fixed clips cannot show that. Mono, because neither patch pans anything and a stereo file would be two copies of one signal, which the renderer asserts rather than assumes. Both come out at the same RMS, so the pair can be played against each other without the louder one seeming better.

Ogg for the browsers that take it and WAV for the rest, which is the only pair JUCE can write and every browser can read. The `<audio>` elements carry `preload="none"`, so a page with audio on it costs what a page of text costs until somebody presses play. Re-rendering gives a byte-identical WAV and a different Ogg every time, since the container carries a random stream serial.

The pages load WebP, encoded losslessly, which is a third smaller than the PNG for a screenshot and pixel-identical. Lossy is worse here rather than better: the panel grain is high-frequency noise, and at quality 95 the window shot comes out larger than the PNG. Some images in `docs/` are never loaded by a browser at all. They are the `og:image` files, and they avoid WebP because social card renderers cannot be relied on to read it. `overtonium.png` and `overtonium-strips.png` are the screenshots the inner pages point at, so regenerating a screenshot means writing the PNG as well as the WebP. `overtonium-card.jpg` is the front page's card, composed at the 1200x630 every renderer expects, and it is a JPEG because the graded background is grainy enough that the PNG of it runs four times the size for no visible gain.

The front page hero is layered rather than flattened. The window shot is a background that `object-fit: cover` crops to the frame, and the wordmark is a separate image with an alpha channel centred over it. Because the stylesheet sizes the mark as a share of the frame, the background can be cropped to any band without the crop ever reaching the name, and the mark can take a larger share of the width on a phone than on a desktop. Flattening the two would give up both.

The stylesheet takes its palette from `Source/UI/Theme.h`, so the page and the instrument stay the same colour. One value deliberately does not. The instrument's dim text is `#6f7a86`, which measures 4.45 against the page background and 4.08 against a panel, either side of the 4.5 that WCAG AA asks for at this size, and it is the colour behind the nav, the fact labels, the captions and the footer. The page lifts it to `#838d97`, which clears the bar on every surface it lands on. Changing it in `Theme.h` instead would drag the instrument's own captions with it and put the hero artwork, which is hand-composed from a render, out of step with the plugin.

Images wider than the column that shows them carry a `srcset`. A phone lays the front page out at 364 CSS pixels, so the window shot goes out at 674 rather than 1348, which is the difference between 63 KB and 205 KB on the connection least able to afford it.
