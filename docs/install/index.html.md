Page: Installing Overtonium on macOS, Windows and Linux
URL: https://benjamindehli.github.io/overtonium/install/
Last changed: 2026-10-25

# Installing Overtonium

Installers for macOS and Windows, and a zip for Linux. Every release also has a zip next to the installer with the same builds unpacked, for anyone without administrator rights or who keeps plugins in their own folders.

## Download

Five files. Most people want the installer for their platform.

| File                                                                                                                      | Holds                        | When to use it                                                                                                                                  |
| ------------------------------------------------------------------------------------------------------------------------- | ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| [macOS installer](https://github.com/benjamindehli/overtonium/releases/download/v1.15.0/Overtonium-v1.15.0-macOS.pkg)     | VST3, Audio Unit, Standalone | The usual choice on a Mac. You choose which of the three formats to install                                                                     |
| [macOS zip](https://github.com/benjamindehli/overtonium/releases/download/v1.15.0/Overtonium-v1.15.0-macOS.zip)           | VST3, Audio Unit, Standalone | On a Mac without administrator rights, or if you keep your plugins in a folder of your own                                                      |
| [Windows installer](https://github.com/benjamindehli/overtonium/releases/download/v1.15.0/Overtonium-v1.15.0-Windows.exe) | VST3, Standalone             | The usual choice on Windows. It needs administrator rights, and SmartScreen will say the publisher is unknown                                   |
| [Windows zip](https://github.com/benjamindehli/overtonium/releases/download/v1.15.0/Overtonium-v1.15.0-Windows.zip)       | VST3, Standalone             | On Windows without administrator rights, if you keep your plugins in a folder of your own, or if you would rather not run an unsigned installer |
| [Linux zip](https://github.com/benjamindehli/overtonium/releases/download/v1.15.0/Overtonium-v1.15.0-Linux.zip)           | VST3, LV2, Standalone        | The only download for Linux. There is no installer, because Linux distributions do not agree on where plugins go                                |

Every file above is from **version 1.15.0**, released 25 October 2026. [What changed in it](https://benjamindehli.github.io/overtonium/releases/), and in every version before it, has a page of its own. Older builds and the source are on the [releases page](https://github.com/benjamindehli/overtonium/releases/latest).

## What each platform gets

| Platform | Installer              | Formats                      |
| -------- | ---------------------- | ---------------------------- |
| macOS    | `.pkg`, with a chooser | VST3, Audio Unit, Standalone |
| Windows  | `.exe`                 | VST3, Standalone             |
| Linux    | none, use the zip      | VST3, LV2, Standalone        |

### macOS

The package asks which of the three formats you want and installs only those. It is a universal binary, so one download covers Apple Silicon and Intel, and it is signed and notarised, so it opens without a warning. The minimum is macOS 10.13 on Intel and 11.0 on Apple Silicon, which is the lowest version the toolchain supports for that architecture.

### Windows

The installer is not signed, so SmartScreen says the publisher is unknown. Choose "More info" and then "Run anyway". If you would rather not, [build it from source](https://benjamindehli.github.io/overtonium/install/#building) instead. The VST3 goes to the shared plugin folder, so the installer asks for administrator rights.

### Linux

There is no installer, because Linux distributions do not agree on an install location. The zip holds VST3, LV2 and standalone, and the two plugin folders go in `~/.vst3` and `~/.lv2`.

## When a host does not see it

| Symptom                                   | What to try                                                                                                                                                                                       |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Logic or GarageBand shows no Overtonium   | They only look for new Audio Units when they start. Quit and reopen instead of rescanning                                                                                                         |
| A host lists it but will not load it      | Check you have the right format. Logic uses the Audio Unit and most others use the VST3. It is easy to install only one of the two by accident in the chooser                                     |
| Nothing appears after installing on macOS | Confirm where the files went with `pkgutil --files com.dehlimusikk.overtonium.vst3`. The plugins belong in `/Library/Audio/Plug-Ins` and the app in `/Applications`                               |
| The plugin loads but is silent            | Check the master fader and whether a channel is [soloed](https://benjamindehli.github.io/overtonium/controls/). Solo is per channel and mute overrides solo, so a soloed strip can still be muted |
| Held notes sound but nothing new does     | Overtonium is a synthesizer and needs MIDI. Check the track's input, not the plugin                                                                                                               |

Please report anything else on the [issue tracker](https://github.com/benjamindehli/overtonium/issues), especially on Windows and Linux, for the reason below.

## Telling you about new versions

The check is off unless you turn it on. The first time you open the window, a dialog asks whether to turn it on. Nothing goes out to the network unless you say yes. Either answer is remembered, so you are not asked again. After that, the setting is under Settings.

With it on, the plugin downloads one small file from this site when the window opens and compares the version in it with the build you are running. If there is a newer version, a dialog says so, once for each release, SETTINGS lights up, and the first item in its menu opens the release page. It is a plain request for a static file: no identifier, no telemetry, nothing about your machine. The [security policy](https://github.com/benjamindehli/overtonium/blob/main/SECURITY.md) lists exactly what is sent.

The dialogs are drawn inside the plugin's own window, not opened as separate windows. A plugin cannot tell whether someone is opening it or a host is scanning its plugin folder at startup, and a window that waited for an answer would stop the scan. These dialogs do not wait for anything, and a question only counts as asked once someone has answered it.

## What it costs to run

Polyphony is what matters most, because eight voices of 32 partials is 256 sine oscillators. Measured on one core at 48 kHz with everything running at once: both modulators on every channel, drift, velocity, aftertouch, the meters and the noise channel.

| Voices         | Oscillators | One core  |
| -------------- | ----------- | --------- |
| 1              | 32          | about 1%  |
| 8, the default | 256         | about 7%  |
| 16             | 512         | 14 to 15% |

The effects on the bar add to that, and cost the same at any polyphony, because they process the sum and not each voice. Most of them cost far less than the voices: the room and the plate are around half a percent each and the echoes less. The spring is the exception at about two and a half percent, because its dispersion is a chain of filters per spring and there are three springs. The resonator costs about a tenth of what eight voices do in Piano or Octave. In Open, or in Piano with the sustain pedal down, all eighty-eight of its strings are free to ring, and it costs about half of what eight voices do. These are one machine's figures and not a promise about yours, but the proportions hold: the voices are the main cost, and the resonator with all its strings free is the only effect that comes near them.

**The render rate is the control to reach for** if you need it cheaper. Turning it down really lowers the rate, it is not a filter on top. The whole voice pool renders at the lower rate, and at 8 kHz it costs a fifth as much. You can also hear it, which is why it is on the panel and not in a preferences dialog. Lowering the polyphony also works, in proportion.

What keeps it affordable is that the engine stays a plain bank of oscillators. It uses a 4096-point interpolated sine table instead of a sine call per sample. Modulators and envelope coefficients update once per 32-sample block, and only the oscillator and the envelope run at sample rate. A partial that is muted, faded out or at zero skips the oscillator entirely.

## What has been tested

All three platforms are built and run through the test suite on every push, and the VST3 is checked with pluginval at strictness 8 on each of them. macOS is the only platform where the plugin has been loaded into a real host, and there the Audio Unit and the VST3 also pass `auval`. Nobody has run the Windows or Linux builds in a DAW yet, so treat those as untested.

## Building from source

You need CMake 3.22 or newer and a C++17 compiler. JUCE is downloaded at configure time, so there is nothing else to install on macOS or Windows.

```
git clone https://github.com/benjamindehli/overtonium.git
cd overtonium
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The plugins are copied into your user plugin folders as part of the build. Pass `-DOVERTONIUM_INSTALL_AFTER_BUILD=OFF` to skip that. Linux needs a handful of development packages first, which the [contributing guide](https://github.com/benjamindehli/overtonium/blob/main/CONTRIBUTING.md#linux) lists.

The build treats warnings as errors, the same as the project's own checks. If your compiler is one the project has not been tested with, add `-DOVERTONIUM_WARNINGS_AS_ERRORS=OFF` to the first cmake command and it builds anyway.

Overtonium is free software under the AGPLv3, which follows from JUCE. The whole source, including the tests and the packaging, is in the [repository](https://github.com/benjamindehli/overtonium).
