# Contributing

Bug reports, patches and reports of how it behaves in hosts nobody here has tried are all welcome. Windows and Linux in particular are built and tested on every push but have never been loaded into a DAW by anyone, so anything you find there is new information.

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

There is a second target, `overtonium_runtime_test`, which builds the plugin sources as a console app and exercises parameter wiring, MIDI handling, the factory presets, user preset round-tripping, bus layouts, undo and state round-tripping. It never opens an editor, so it runs on a machine with no display. Both targets are registered with CTest, so `ctest` from the build directory runs them together.

The audio thread allocates nothing. A host is allowed to hand over a bigger block than the one it promised in `prepareToPlay`, and growing the scratch buffer to fit would take the allocator's lock on the audio thread, where whatever the message thread is doing can make it wait. The block is cut into pieces the scratch already holds instead, and the scratch is given a floor of 512 frames so a host that promises very little and delivers a lot is not cut into a great many of them. The seam has to be inaudible for that to be worth doing, so the test renders the same passage twice, once to a host that keeps its word and once to a host that promised a sixteenth of what it sent, and compares them sample by sample. They come out identical, sample for sample. The patch it uses has no random modulation in it, because drift is a random walk redrawn once per control block and those blocks fall in different places when a block is cut up, so a patch carrying drift would differ for reasons that have nothing to do with the cutting.

Both run in CI on macOS, Windows and Linux on every push and pull request, building the plugin itself along the way so a compile error in the JUCE half cannot pass unnoticed. A second job compiles the DSP core with the bare-compiler line above, and with `-Werror`, so the claim that it has no build system dependency is checked rather than asserted.

Pass `-DOVERTONIUM_INSTALL_AFTER_BUILD=OFF` to skip copying the built plugins into your user plugin folders, which is what CI does and what you want on any machine with no host to rescan them.

### Releases

Pushing a tag that starts with `v` builds the plugin on all three platforms and attaches the archives to a GitHub release:

```
git tag -a v1.0.0 -m "Overtonium 1.0.0"
git push origin v1.0.0
```

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

The shared header, nav and footer are repeated in each page and have to be kept in step by hand. That is the price of having no build step, and it was the deliberate choice over a generator whose output would have to be committed anyway.

The screenshots are rendered rather than captured. The plugin has no dependency on a display, so a scratch program builds the editor, plays a chord into it, hands the strips the levels the engine measured, and writes the window out with `createComponentSnapshot`. That means the pictures can be regenerated after a layout change instead of going stale, and it works on a machine with no window server.

The stylesheet takes its palette from `Source/UI/Theme.h`, so the page and the instrument stay the same colour.
