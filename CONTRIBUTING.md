# Contributing

Bug reports, patches and reports of how the plugin behaves in hosts not yet tried here are all welcome. Windows and Linux in particular are built and tested on every push, but nobody has loaded them into a DAW yet. Anything you find there is new information.

When you open an issue there is a form for each of these, including one for a host report where everything went fine. "It loaded and behaved" is worth filing for a platform nobody has run it on.

By taking part you agree to the [code of conduct](CODE_OF_CONDUCT.md). For how the code is arranged, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Before a pull request

CI runs six jobs on every push and pull request: the build and both test suites on all three platforms, formatting, the sanitizers, the DSP core on its own, the structured data on the site, and the counts in the docs. Each one can be run locally, and this section says how.

**The tests pass.** `ctest --test-dir build --build-config Release` runs both suites.

**Warnings are errors.** This is the default, so a plain `cmake -B build` already gives you the build CI does, with nothing extra to run. A build tree keeps the setting it was configured with, so if one was configured with `-DOVERTONIUM_WARNINGS_AS_ERRORS=OFF`, reconfigure it with `ON` or delete it.

Compilers do not agree on warnings. JUCE turns on more warnings for clang than for gcc, and a newer clang warns about more than an older one. So a clean local build does not guarantee that all three runners will pass.

If your compiler is not one the three runners use, and it warns about something this project has never seen, `-DOVERTONIUM_WARNINGS_AS_ERRORS=OFF` builds the plugin anyway. Please report the warning as well, because nothing here can see it.

**Formatting.** C++ uses clang-format with stock LLVM style and no overrides, so running `clang-format -i` on each file you touched is all you need. Everything else, meaning the docs, the readme and the workflows, uses prettier:

```sh
npm ci
npm run format
```

`npm run format:check` is what CI runs. Prettier is pinned in `package-lock.json` because its output changes between versions, and a check that disagrees with your copy is worse than no check.

CI checks the C++ the same way, and clang-format is pinned for the same reason. Install it from pip, not from your package manager, or your copy will format differently from the CI job:

```sh
pip install clang-format==23.1.1
clang-format --style=LLVM -i $(find Source Tests Tools -name '*.cpp' -o -name '*.h')
```

One region is deliberately exempt. The factory presets inside `apply` in `Presets.cpp` sit between `clang-format off` and `on`, because `factoryCode` writes them and does its own wrapping. If they were reformatted, a preset regenerated from the same patch would no longer match the file it came from. That match is how a preset is checked against what its author actually set.

**pluginval.** CI loads the built VST3 into [pluginval](https://github.com/Tracktion/pluginval) at strictness 8 on all three platforms. pluginval is a host that misuses the plugin on purpose. It reaches things the suites here cannot, because they drive the processor directly, not through a plugin format. To run it yourself, point it at a built or installed plugin:

```sh
pluginval --strictness-level 8 --validate ~/Library/Audio/Plug-Ins/VST3/Overtonium.vst3
```

pluginval picks random parameter values. CI fixes the seed on a push and leaves it random on a weekly scheduled run. A failure prints the seed it used, and passing that seed back with `--random-seed` reproduces it exactly. For some failures that is the only way to reproduce them at all.

**The VST3 validator.** This is Steinberg's own conformance suite, which is different from pluginval. It checks the VST3 contracts instead of misusing the plugin through a host. CI builds it from a pinned revision of the SDK and runs it on Linux, where the build is cheapest. To run it yourself, build the `validator` target from [the SDK](https://github.com/steinbergmedia/vst3sdk) with the flags CI uses, `-DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF`, which skip the parts it does not link. Without the hosting examples switched off, the SDK wants GTK 3 on Linux. Point the result at a built `.vst3`.

**Sanitizers.** Both suites run again on Linux with the address and undefined behaviour sanitizers on. The checks above test whether the plugin does the right thing. This one tests whether it does so without reading past the end of an array or relying on behaviour the standard leaves undefined. The other checks cannot see either problem. A read one byte past the end of a buffer returns a plausible number and passes.

```sh
cmake -B build-san -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOVERTONIUM_SANITIZE=ON
cmake --build build-san --target overtonium_dsp_test overtonium_runtime_test
ctest --test-dir build-san --output-on-failure
```

This is off by default, and best kept in a build tree of its own. The build is instrumented throughout, and the result runs several times slower than a plugin you would want to listen to. The sanitizers are set to report everything instead of stopping at the first problem, so a run can find a fault and still exit with zero. For that reason CI reads the log, and a leak counts the same as an overflow.

**The DSP core on its own.** A separate job compiles `Source/dsp/` and its tests with a bare compiler and `-Werror`, so the claim that the core needs no build system and no JUCE is checked, not just stated. The command is under [Tests](#tests). The same job runs the lambda scan below.

**One check runs for a compiler that is not here.** `.github/scripts/lambda_captures.py` looks for lambdas that read a function-local `constexpr` without capturing it. The standard requires no capture, and gcc and clang agree, but MSVC refuses it with C3493. So the fault only shows up in the Windows job. That job runs last and takes twenty minutes to report. Run the script before pushing:

```
python3 .github/scripts/lambda_captures.py --self-test
python3 .github/scripts/lambda_captures.py Source Tests
```

It tests itself first, because the failure that matters most is passing everything it is given without complaint. Its test cases include a `} // namespace` at the end of a block, which must not make every function after it look like a namespace, and `[&in]`, which captures one name and is not a default capture.

Reading a Windows log takes practice for the same reason. When one lambda will not compile, every call to it becomes an `<error type>`, so MSVC reports C2064, C2110, C2737 and C3536 throughout the file and gives up at a hundred errors. Start from the first error, not the last.

**The structured data.** The site's pages carry JSON-LD, which nothing renders, so a block broken by an edit would fail without anyone seeing it. The job parses every block and checks that the version in it, and the `version:` in `CITATION.cff`, match `project(Overtonium VERSION ...)` in CMakeLists.

**The counts in the docs.** The number of factory presets, published clips and host parameters is written in words across the readme, the design notes, this guide and the site. The job reads the real counts from `kNames` in `Presets.cpp`, the files in `docs/audio/` and the expected total in `Tests/plugin_runtime_test.cpp`, and fails on any number that sits next to one of those nouns and disagrees. So write a count beside the noun it counts. "All thirty-five presets" can be checked, and "all thirty-five" cannot. Release notes and the releases page are left out, since each describes its own release. The same job runs `image_dimensions.py` and `css_cascade.py` over `docs/`, and `release_docs.py` for the version in CMakeLists, which fails until that version has its notes file and its section on the releases page. The structured data and counts checks are written inline in `.github/workflows/build.yml` and run from the repository root as they are.

**A new control needs a name before anything else.** JUCE reads a control's accessible name from `Component::setTitle`, and accessibility clients may skip an element entirely if it has neither a name nor a description. The runtime suite walks the whole mixer and fails if a slider has no title or shares one with another. So "attack" on its own will not pass, and "Harmonic 19 attack" will. A control that draws its value instead of writing it, as the segment readouts do, needs its own spoken name and value. Replacing a plain label with one of those removes a name without any warning. [ACCESSIBILITY.md](ACCESSIBILITY.md) describes what the project does and does not do here.

## Style

Comments explain why, not what. The repository has a lot of them. The useful ones record a measurement, a rejected alternative or a trap: why the lamps merge by row and what the other way cost, why the scratch buffer has a floor, why the program count is what it is on each format. A comment that repeats the line below it is noise.

Prose in the repository describes the present, not its own history. Git already records what changed.

Nothing in `Source/` holds anything in a static except plain data, such as tables and identifiers. A plugin is unloaded by its host, and a static that owns an image, a font, a timer or a thread is destroyed at unload, which on Windows can deadlock the host. Keep caches and shared resources in the editor or the look and feel. [ARCHITECTURE.md](ARCHITECTURE.md#threading) explains why.

## Building

You need CMake 3.22 or newer and a C++17 compiler. JUCE is downloaded at configure time. Pass `-DJUCE_PATH=/path/to/JUCE` to use a local checkout instead.

The build copies the plugins into your user plugin folders. Pass `-DOVERTONIUM_INSTALL_AFTER_BUILD=OFF` to skip that. CI does this, and you want it on any machine with no host to rescan them.

### macOS

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Drop `-G Ninja` to use Makefiles instead. Neither needs Xcode.app. The Command Line Tools (`xcode-select --install`) are enough.

If you have full Xcode installed and want a project to debug in, use `cmake -B build-xcode -G Xcode` followed by `cmake --build build-xcode --config Release`. That generator needs Xcode.app itself. With only the Command Line Tools, CMake reads the version wrong and fails with `Xcode 1.5 not supported`. If you hit that, point it at a real install with `sudo xcode-select -s /Applications/Xcode.app`.

`COPY_PLUGIN_AFTER_BUILD` is on, so the build installs the plugins to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`. Logic and GarageBand only rescan the AU after you quit and relaunch them. If the component does not show up, run `auval -v aumu Ovtn Dhmk` to see why.

Building installs into your own folder under `~/Library`, while the `.pkg` installs system-wide under `/Library`. When both contain an `Overtonium.component`, macOS registers one of them and the host uses that one, which may not be the one you just built. The host can then keep loading a build that is months old while `auval` reports the new one, and none of your changes seem to do anything. If a change to the Audio Unit has no effect, check for a second copy before doubting the change:

```sh
ls -d /Library/Audio/Plug-Ins/Components/Overtonium.component \
      ~/Library/Audio/Plug-Ins/Components/Overtonium.component 2>/dev/null
```

Logic shows the version it loaded next to the plugin name. That is the quickest way to tell whether it is using your build.

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
  libxcursor-dev libxcomposite-dev libgl-dev libcurl4-openssl-dev \
  libxi-dev libegl-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Tests

The DSP core has no JUCE dependency, so its tests compile with a compiler alone, with no build system and no third-party headers:

```sh
c++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp Source/dsp/*.cpp -o dsp_test
./dsp_test
```

Through CMake, which also builds the JUCE-linked tests:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target overtonium_dsp_test
./build/overtonium_dsp_test
```

The tests cover every block in the core: the tuning tables, the temperaments and Adaptive, which is checked against all 125 chord types in `Tests/music_utils_chord_types.h`, the oscillators and characters, envelopes, modulation, glide, voice allocation, the noise channel's string, the resonator, the bus drive, wobble, the three echoes, the three reverbs, the converter and the output stage. They also check that parameter changes do not click, and they end by printing a CPU benchmark.

In the DSP tests, a `SynthEngine` is created with `make_unique` and used through a reference, not declared as a local variable. One engine is about 260 kB. MSVC gives the main thread a 1 MB stack, where macOS and Linux give 8, so a test that holds a few of them overflows it on Windows alone.

There is a second target, `overtonium_runtime_test`. It builds the plugin sources as a console app and tests parameter wiring, MIDI handling, the factory presets, user preset round-tripping, bus layouts, undo and state round-tripping. The editors it opens are never put on the desktop, so it runs on a machine with no display. Both targets are registered with CTest, so running `ctest` from the build directory runs them together.

It also checks that the audio thread allocates nothing, even when a host hands over a bigger block than it promised. [ARCHITECTURE.md](ARCHITECTURE.md#threading) describes how. The test renders the same passage twice, once for a host that keeps its promise and once for a host that promised a sixteenth of what it sent, and compares them sample by sample. They come out identical. The scratch buffer has a floor of 512 frames, so a host that promises very little does not get its block cut into a great many pieces. The patch it uses has no random modulation, because drift is redrawn once per control block, and those blocks fall in different places when a block is cut up.

Both run in CI on macOS, Windows and Linux on every push and pull request. CI builds the plugin itself along the way, so a compile error in the JUCE half cannot go unnoticed.

### Factory presets

Factory presets are code, not embedded data. The cases in `Presets.cpp` say `1.0 / n` and `6.0 / (1 + 0.35 * (n - 1))` where the shape has a formula behind it, and give a table of thirty-two figures where it does not. That makes a preset something you can read and argue with, not a blob. The cost is one extra step between setting up a patch and shipping it, and `Tools/preset_to_code.cpp` is that step:

```sh
cmake --build build --target overtonium_preset_to_code
./build/overtonium_preset_to_code_artefacts/Release/overtonium_preset_to_code "Synth Ensemble.ovtpreset" 31
```

Save the patch from the plugin's own preset menu, run the tool on the file it saved, and paste the result over the case it names. The second argument is the case number. Without it, the output says `case N:` for you to fill in by hand. It writes to stdout so you can diff it against what is already there. That is the quickest way to see what a patch actually changed.

It loads the preset into a processor and calls `presets::factoryCode`, the same function the plugin's own authoring menu entry calls, instead of reimplementing the generation. That menu entry is only built with `-DOVERTONIUM_PRESET_AUTHORING=ON`, which is off by default. A second implementation that agreed most of the time would be worse than none. The runtime test already checks that `factoryCode` reproduces every factory preset it is given, so the tool gets that check without needing one of its own.

There are two things it does not do. It does not edit `Presets.cpp`, because each case has a hand-written comment above it saying what the patch is and why, and a generator writing the file in place would lose them without notice. It also writes no session parameters. The polyphony, the temperament and the reference pitch belong to the player, not to the sound, so `factoryCode` leaves them out, and a test fails the build if one ever appears in a case.

Regenerating an existing preset is how to check that a patch really is what you think. Running the tool on the file and diffing against the committed case shows every row that changed and nothing else.

### Releases

Pushing a tag that starts with `v` builds the plugin on all three platforms and attaches the archives to a GitHub release:

```
git tag -a v1.4.0 -m "Overtonium 1.4.0"
git push origin v1.4.0
```

The notes for the release page come from `release-notes/`, one file per version, named after the tag without its `v`. So `v1.2.0` is described in `release-notes/1.2.0.md`, and that file has to exist and start with the heading `# Overtonium 1.2.0` before the tag is pushed. With one file per release, instead of one file that gets rewritten, the notes for a version stay readable in the tree after the next release. The tag also picks its own file by name, instead of taking whatever the file happens to say.

The heading is checked as well as the filename. Otherwise, copying the previous release's notes to a new name and forgetting to edit them would reach the release page.

Each platform gets an installer where a sensible one exists, and next to it a zip holding every format it can build, plus the readme and the licence. macOS gets VST3, AU and Standalone as a universal binary for Apple Silicon and Intel. Linux gets VST3, LV2 and Standalone, and Windows gets VST3 and Standalone. macOS gets a `.pkg` built by `packaging/macos/build-pkg.sh`, with a chooser so VST3, the Audio Unit and the standalone can be installed separately. Windows gets an `.exe` from `packaging/windows/overtonium.iss`. The zips are still provided, for anyone without administrator rights or with their own plugin folders. Linux gets only the zip, because there is no install location that distributions agree on. The tests run before anything is packaged, so a tag that fails them produces no release.

You can also start the same workflow by hand from the Actions tab. That builds the archives and uploads them to the run without publishing anything. Use it to test the packaging without using up a version number.

The tag has to match `project(Overtonium VERSION ...)` in CMakeLists, and the workflow fails the release if it does not. The archive name comes from the tag, while the version a host displays comes from CMake. Without that check the two could disagree with no warning.

The version also needs a section on `docs/releases/index.html`, with the version and the date in its heading. That page is the site's own record of what changed, written for someone reading the site, not someone on the release page. So it cannot be generated from the notes file and has to be written before the tag is pushed. The check runs before anything is built, so a forgotten entry costs a commit and a second tag, not a published release with a site that is one version behind.

**What the release writes back.** Publishing a release and updating the site happen together, so the workflow commits to `main` after the release is published:

| What                                                      | Why it cannot wait                                              |
| --------------------------------------------------------- | --------------------------------------------------------------- |
| `docs/latest.json`                                        | the plugin's update check reads it and would report the old one |
| the five download links on the install page               | the buttons would hand out the previous build                   |
| the version and date printed under them                   | the page would name a release nobody can download               |
| `RELEASED` in `Tools/site_data.py`, then the site rebuilt | a crawler would carry the old release date into search results  |

Each of those is counted before and after it is rewritten, and the job fails if a substitution matched a different number of times than expected. A `sed` that matches nothing without saying so is the one way this can fail with no visible sign. Anyone who clicks a wrong download link sees it at once, but a `softwareVersion` one release behind is visible only to a crawler, which is why it is checked. The site generator reads the version itself from `CMakeLists.txt`.

**What a new version needs before the tag.** The version in `project(Overtonium VERSION ...)` and in `CITATION.cff`, the notes file in `release-notes/`, the section on `docs/releases/index.html`, and the supported version in `SECURITY.md`. CI fails on the first three as soon as CMakeLists names the new version. The last is not checked, so it is easy to forget.

Before committing, the step rebuilds the site and reformats the install page and the feed with Prettier. A longer version number can push a line past Prettier's wrap width, and the format job checks `main` on every push, including this one.

**Signing.** The macOS side signs and notarises when the repository has the secrets for it. When it does not, it falls back to an ad-hoc signature, so the workflow can be tested before any of the secrets exist. An ad-hoc signature is what lets a bundle load at all on Apple Silicon, which refuses unsigned ones outright. But it is not notarisation, and a download still has to be opened past Gatekeeper.

| Secret                                 | What it is                                                                        |
| -------------------------------------- | --------------------------------------------------------------------------------- |
| `APPLE_CERTIFICATE_P12`                | Developer ID Application certificate and key, as a base64 `.p12`                  |
| `APPLE_CERTIFICATE_PASSWORD`           | the password that `.p12` was exported with                                        |
| `APPLE_INSTALLER_CERTIFICATE_P12`      | Developer ID Installer certificate, base64 `.p12`, for signing the package itself |
| `APPLE_INSTALLER_CERTIFICATE_PASSWORD` | its export password                                                               |
| `APPLE_ID`                             | the Apple ID to submit for notarisation with                                      |
| `APPLE_APP_SPECIFIC_PASSWORD`          | an app-specific password for that Apple ID, not the account password              |
| `APPLE_TEAM_ID`                        | the ten-character team identifier                                                 |

Base64-encode a certificate with `base64 -i cert.p12 | pbcopy`. The two certificates are different. Application signs the bundles, Installer signs the `.pkg`, and notarisation rejects a package signed with the wrong one.

The Windows installer is not signed, so SmartScreen tells the user the publisher is unknown, and they have to choose "More info" and then "Run anyway". A certificate that removes the warning is an ongoing cost, so the release notes explain the warning instead.

### The project page

`docs/` is served by GitHub Pages from the `main` branch, with one folder per URL so the site has addresses like `/tuning/` instead of `/tuning.html`. GitHub serves exactly what is in the repository, with no build step on its side, and `.nojekyll` stops GitHub running Jekyll over it.

**The parts every page shares are generated.** `python3 Tools/build_site.py` rewrites them in place, from `Tools/site_data.py` and from what the source code already contains:

| Generated                                                     | From                                                              |
| ------------------------------------------------------------- | ----------------------------------------------------------------- |
| each page's `<head>`: title, descriptions, cards, JSON-LD     | `PAGES`, `PRODUCT`, `VIDEO` and `QUESTIONS` in the data           |
| the header, the section list and the page's contents rail     | the page list, and the page's own `<h2>` and `<h3>` ids           |
| the trail above the title and the links at the foot           | `nav` and `onward` per page                                       |
| the footer                                                    | `FOOTER_LINKS`                                                    |
| the cards on the front page and on `404.html`                 | `card` and `card_404` per page                                    |
| the questions on the front page and their `FAQPage` copy      | `QUESTIONS`, so the visible and structured answers match          |
| the version, the release day, the preset and parameter counts | `CMakeLists.txt`, `RELEASED`, `kNames` and `docs/parameters.json` |
| `sitemap.xml`, with each page's last-modified day             | `priority` per page, and `Tools/site_dates.json`                  |
| `llms.txt`                                                    | `llms` per page and the `LLMS_` lists                             |
| `llms-full.txt`, every page's content as Markdown             | each page's `<main>`                                              |
| the parameter list on the playing page                        | `docs/parameters.json`                                            |
| each page's clips as `AudioObject`s in its JSON-LD            | the page's `<audio>` elements and the text that describes each    |

Everything between those parts is ordinary HTML that you edit directly. An edit inside a generated part is overwritten by the next build, so change the data file instead. The contents rail lists every `<section id>` that starts with an `<h2>`, with each `<h3 id>` inside it as a sub-entry, so giving a new section an id adds it to the rail. The generator runs the pinned Prettier over each page, so run `npm ci` first. The format job runs it with `--check` and fails if the result differs from what is committed.

**The parameter list is written by the plugin.** `docs/parameters.json` lists every parameter the host sees, one per line, with its id, its name, and its range and default as the plugin displays them. `overtonium_runtime_test --write-parameters` writes it from the real layout. The same test, which CI runs on all three platforms, fails when the file and the layout disagree. So when you add or rename a parameter or change its range, run that, then `Tools/build_site.py`. That folds the file into the table on the playing page, with a row for each control, not one for each of the 32 copies.

**A page's last-modified day changes when its text does.** `Tools/site_dates.json` holds a hash of the text in each page's `<main>` and the day it last changed. When the text changes, the build gives the page today's date, in UTC, and the sitemap and the page's `dateModified` both use it. A change to the head, the rail, the footer or the markup around the text leaves the day alone, because it does not change what the page says. The hash is checked too, so a page edited without rebuilding fails CI instead of keeping an old date. The day comes from this file, not from git, because a CI checkout has no history to read it from.

**Search engines are told when a page changes.** `.github/workflows/indexnow.yml` runs on every push to `main` that changes `docs/sitemap.xml`, which is every push that changes what a page says. It waits until the published sitemap matches the committed one, then sends [IndexNow](https://www.indexnow.org/) every page the sitemap dates today or yesterday, so Bing and the engines that share its index fetch it again straight away. Pages publishes the site through a workflow GitHub runs itself, which nothing can follow, hence the wait. The release workflow's own commit to `main` starts no workflow, so the release workflow calls this one at its end. The key is the one served at the root of `benjamindehli.github.io`, which covers every address on that host. It can also be run by hand from the Actions tab.

`llms.txt` is a summary of the project for language models, in the format [llmstxt.org](https://llmstxt.org/) describes: what the instrument is, then links to each page with a line saying what is on it, then the key facts in one place. `llms-full.txt` is the whole site as Markdown in one file, for a model that would rather read everything than follow links. `robots.txt` names both, because no crawler is required to look for them. The facts in `llms.txt` are prose in the data file, and are worth checking when a release changes what the plugin can do. The counts and the preset list in them are filled in from the source.

The screenshots are rendered, not captured. The plugin does not depend on a display, so `Tools/render_docs_images.cpp` builds the editor, plays a chord into it, gives the strips the levels the engine measured, and writes the window out with `createComponentSnapshot`. That means the pictures can be regenerated after a layout change instead of going out of date, and it works on a machine with no window server.

```
cmake --build build --target overtonium_render_docs
./build/overtonium_render_docs_artefacts/Release/overtonium_render_docs out
```

That writes the window, the six-channel detail and the top bar. Then, with both bars open and C7 held under Adaptive, it writes the Tuning bar, and the Playing bar with the pedal down, the mod wheel and pressure part way up and three macros made. The crop positions come from `kGutterWidth`, `kStripWidth` and `TopBar::heightForWidth`, not from typed-in numbers, so a taller bar or a wider strip moves the crops with it instead of cutting one through the middle of a row. A second argument scales the window. It is a real redraw, not an upscale, and it is what artwork that needs the window larger than the page shows it is made from.

On a machine that has never answered the update question, every window opens with that question showing, because the answer is stored in `Overtonium.settings` next to the presets, not in the tree. The renderer closes that dialog without answering it, so the pictures show the instrument wherever they are rendered, and nothing is written to the settings file.

It writes PNG, because JUCE has no WebP encoder. Producing the files the pages actually load is a second step with a lossless encoder:

```
cwebp -lossless out/overtonium.png -o docs/overtonium.webp
```

With Pillow instead, it is `save(lossless=True, quality=100, method=6)`. The `quality` setting is required. On a lossless save it sets how hard the encoder works, not how much it discards. At the default of 80 the window shot comes out about as large as the PNG it is meant to beat. At 100 it is about 40% smaller.

Regenerating from the same source on the same JUCE version gives byte-identical files. DRIFT is a random walk, but each voice reseeds from a fixed number in `prepare`, so the meters and the lamps land in the same place every run. A diff therefore means something really changed, which is what makes the pictures worth regenerating after a layout change. Upgrading JUCE is the exception. Its rasteriser does not promise the same pixels between versions, so a JUCE update can change a few percent of the pixels by a few values each. That is invisible, but it is still a diff.

The target is built by default so that it cannot stop compiling without anyone noticing, which is how a renderer gets lost. `-DOVERTONIUM_BUILD_TOOLS=OFF` skips it.

**The audio examples.** The tuning page is about things you hear, so every section on it has a clip or a pair of clips: _Just Saw_ against _Equal Saw_ and the sweep between them, STRETCH taken from a harmonic series out past a piano, one chord in equal temperament against the same chord in Werckmeister III, five notes up the keyboard with TRACK off and on, and a held chord with and without DRIFT. The playing page has two pairs of its own: a phrase on the Glide preset with every glide at zero and as the preset sets them, and a melody over slowly swelling chords on Synth Ensemble with Legato off and on. The front page has the first two. They are rendered by the plugin, not recorded, with the same switch and for the same reason:

```sh
cmake --build build --target overtonium_render_docs_audio
./build/overtonium_render_docs_audio_artefacts/Release/overtonium_render_docs_audio docs/audio
```

The presets page also has one clip per factory preset, all thirty-five presets playing the same spread C major chord, its notes coming in one at a time and released together. Using the same gesture on every preset is deliberate. The page is for listening down the list, and a patch that showed itself off in its own way would be advertising, not describing. The file names come from `slugOf`, which lowercases the preset's name and hyphenates it, so adding a preset adds a clip without a second list to keep up to date. There are two things these clips deliberately do not do. The levels are left where the presets set them, because thirty-five clips meant to be browsed have to show truthfully which patches are quiet. Only clips meant to be compared with each other are level-matched. And nothing sweeps. Anything that needs a control moved, or the keyboard played across, already has its own clip on the tuning page.

Each clip is a note, a chord or a run of notes, held and released, with a short fade at both ends so a file cannot start or stop on a step. TRACK needs the run: the rolloff sits at a fixed 1 kHz while the partials climb through it, so one note shows nothing. A clip may sweep a control as it plays, as the TUNE and STRETCH clips do. The page says those knobs move through a range instead of switching between two states, and a pair of fixed clips cannot show that. The clips are mono, because neither patch pans anything and a stereo file would hold two copies of one signal. The renderer asserts this instead of assuming it. Both clips in a pair come out at the same RMS, so they can be compared without the louder one seeming better.

Ogg comes first with FLAC behind it, because that is the only pair JUCE can write that covers every browser. Current versions of Safari play the Ogg, so on every browser anyone has checked, the FLAC is never fetched. It stays as cheap insurance against a browser nobody checked. It is also what the page offers as a download to anyone whose browser plays neither, and that has to be a file worth keeping, not the lossy one. Both are lossless or near enough. FLAC is used instead of WAV because the fallback is the expensive half: across all forty-nine clips the WAVs come to 36.1 MB and the FLACs to 14.1, for the same samples. A repository whose packed history is 13 MB should not triple in size to carry a format that exists for one browser. The `<audio>` elements have `preload="none"`, so a page with audio costs the same as a page of text until somebody presses play. Re-rendering on the same JUCE version gives a byte-identical FLAC and a different Ogg every time, because the Ogg container carries a random stream serial. Across a JUCE upgrade the FLAC changes too, without the sound changing: 9.0.1 builds libflac as C instead of C++ and encodes differently because of it. To tell the two cases apart, compare the MD5 of the unencoded audio that FLAC stores in its own header, which stays the same when only the encoder changed. That holds for the DRIFT clip too: drift is a random walk, but each voice reseeds from a fixed number in `prepare`, so the same command draws the same contour.

The pages load WebP, encoded losslessly, which is a third smaller than the PNG for a screenshot and pixel-identical to it. Lossy WebP is worse here, not better. The panel grain is high-frequency noise, and at quality 95 the window shot comes out larger than the PNG.

One file is an exception, and it is listed here. `overtonium-674.webp` is the narrow half of the window shot's `srcset`, made by resampling the render down to 674 pixels wide. There is no original for it to be identical to, so lossless gains nothing and costs several times as much as quality 80. Quality barely changes the result either, because lossy compression damages the grain at every setting. Between 75 and 90 the mean error only falls from 4.5 to 3.2 of 255 while the file doubles, so it is set to 80. The two hero backgrounds are the other exception, described below. Everything else on the pages is lossless. Some images in `docs/` are never loaded by a browser at all. They are the `og:image` files, and they avoid WebP because social card renderers cannot be relied on to read it. `overtonium.png` and `overtonium-strips.png` are the screenshots the inner pages point to, so regenerating a screenshot means writing the PNG as well as the WebP. `overtonium-card.jpg` is the front page's card, composed at the 1200x630 every renderer expects. It is a JPEG because the window behind the wordmark is grainy enough that a PNG of it is four times the size with no visible gain.

**The cards are composed, not rendered.** `Tools/compose_cards.py` composes all seven: the window shot at full width, centre-cropped to the frame, the wordmark over it at seven tenths of the width, and on the six page cards a dim layer over the background and a caption under the mark. It needs masters that are not in the repository, kept together in one folder: the 4x render described below, cropped to the hero's band, the wordmark, which is hand-made artwork that no build can produce, and the six captions as alpha mattes. The captions are DejaVu Sans at 30 px, centred on 600 with the baseline at 430, so a new one is rendered with Pillow to those numbers and saved beside the others. Every number in that script was measured from the cards it replaced, not chosen, and `--check` composes the old set again from the old background to show it: they come back within 2.9 of 255, which is the JPEG's own noise and nothing anyone could see.

The front page hero is layered, not flattened. The window shot is a background that `object-fit: cover` crops to the frame, and the wordmark is a separate image with an alpha channel, centred over it. The stylesheet sizes the mark as a share of the frame. So the background can be cropped to any band without the crop ever reaching the name, and the mark can take a larger share of the width on a phone than on a desktop. Flattening the two would lose both.

The hero's background is a plain render, and the recipe is written here so that it stays one. `overtonium_render_docs <dir> 4` writes `overtonium@4x.png` at 5400x4040. The bottom 3301 rows of that are the band the hero uses, and resampling the crop with Lanczos to 2360x1445 and 1180x722 gives the two files in the `srcset`. They are lossy WebP at quality 50 and method 6, because lossless would be over 4 MB. After a re-render, measure the contrast under the hero's text against the brightest pixels under each line, since `style.css` promises 7:1 and a brighter render can break it.

A screenshot with a blur and a grade over it, kept outside the repository, cannot be regenerated, so it goes stale as the instrument changes. The stylesheet is what makes a plain render enough. It scales the picture by 1.26, crops it with `object-fit: cover`, lays a radial scrim over it and drifts it. So the softening that was baked into the artwork is applied at display time to whatever the current build looks like. Measured against the brightest patch under each line, a plain render has slightly more contrast than a graded one, not less.

The stylesheet takes its palette from `Source/UI/Theme.h`, so the page and the instrument use the same colours. One value deliberately differs. The instrument's dim text is `#77828d`, which measures 4.97 against the page background, 4.55 against a panel and 4.51 against the alternate. At worst that clears the 4.5 that WCAG AA asks for at this size by a hundredth, and it is the colour of the nav, the fact labels, the captions and the footer. The page raises it to `#838d97`, which clears the bar with a margin on every surface it appears on. Changing it in `Theme.h` instead would change the instrument's own captions too, and put the hero wordmark, which is hand-made, out of step with the plugin.

Images wider than the column that shows them have a `srcset`. A phone lays the front page out at 364 CSS pixels, so the window shot is sent at 674 instead of 1350. That is about a third of the bytes, on the connection least able to afford it.

**Every `<img>` has a width and a height**, so the browser reserves the space before the image arrives, and `.github/scripts/image_dimensions.py` checks that what a page declares matches the file. Getting it wrong causes exactly the layout shift those attributes exist to prevent, and nothing reports it: the page looks right to whoever tests it on a fast connection. A window that grows by a few pixels is enough to put every page out of step with its pictures.

```
python3 .github/scripts/image_dimensions.py --self-test
python3 .github/scripts/image_dimensions.py docs
```

It reads PNG, JPEG and WebP headers itself instead of using Pillow, so it is as cheap as the checks next to it. The attributes have to match one of the pictures the element can actually load, because declaring the largest image in a `srcset` is as correct as declaring the one in `src`. Every other entry has to have the same proportions within a percent. A percent, not exact, because halving 383 gives 191.5 and a file cannot be half a pixel tall, so a resampled entry is always slightly off. It also checks that the social cards are 1200x630, which is the one thing about them nothing on the site would show: no browser loads a card, so a card of the wrong shape only shows up in someone else's link preview. It tests itself first, for the same reason `lambda_captures.py` does.
