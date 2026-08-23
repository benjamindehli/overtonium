# Overtonium 1.1.0

A 32-partial additive synthesiser laid out like a 32-channel mixer. Every channel is one sine oscillator locked to a harmonic of the played note, and every channel has its own tuning, envelope and modulation.

## What is new in 1.1.0

**Fold the mixer down.** Click a section heading in the caption gutter and that group of rows folds away across every channel at once, taking the window's height with it. Pitch modulation, envelope, key off, amp mod and output each fold. All five is 480 pixels shorter. The tuning at the top and the faders at the bottom always stay, and a folded heading keeps its activity lamp, so a group you cannot see still reports whether it is doing anything. What you had folded is remembered with the session.

**The factory presets appear in Logic's own preset menu.** Logic and GarageBand read presets through the Audio Unit's program interface, which previously offered a single entry. All sixteen are listed now. Other formats are unchanged, deliberately: a program count above one makes the VST3 wrapper publish an automatable parameter that rewrites every other parameter when it moves, which is a poor thing to put in an automation lane.

**A panel you can almost touch.** A fine grain across the strips, the section rules scored as grooves cut into the surface rather than lines drawn on it, the effect groups in the bar recessed into it, and the lamps and meters blooming into the panel around them.

**It can tell you when a new version is out.** Off unless you ask. The first time you open the window the credit line under the wordmark offers to turn it on, once, and nothing reaches the network unless you take the offer. It fetches one small file from the project page and reads a version number out of it. No identifier, no telemetry, nothing about you. The setting lives under Settings, and the security policy in the repository spells out exactly what is sent.

**Layout.** The mixer runs to the window edges instead of floating inside a border, with the breathing room moved inside the channels. In the bar, captions sit closer to the knob they name. The short interval label under each channel number is gone, since the colour of the strip already says which interval class a channel is, and the gutter says MUTE / SOLO rather than M / S.

Nothing about the sound has changed. A patch saved in 1.0.0 loads identically.

## Installing

Take the installer for your platform. The zip beside it holds the same builds loose, for anyone without administrator rights or with plugin folders of their own.

| Platform | Installer              | Formats                      |
| -------- | ---------------------- | ---------------------------- |
| macOS    | `.pkg`, with a chooser | VST3, Audio Unit, Standalone |
| Windows  | `.exe`                 | VST3, Standalone             |
| Linux    | none, use the zip      | VST3, LV2, Standalone        |

The macOS build is a universal binary and runs on both Apple Silicon and Intel. Logic and GarageBand only look for new Audio Units when they start, so restart them after installing.

On Linux the zip's contents go in `~/.vst3` and `~/.lv2`.

## What has actually been tried

All three platforms are built on every push, run through both test suites, and put through pluginval at strictness 8. macOS is the only platform where the plugin has been loaded into a real host, where the Audio Unit and the VST3 also pass `auval`. Nobody has run the Windows or Linux builds in a DAW yet, so treat those as untried and please open an issue with whatever you find.

## Windows will warn you

The Windows installer is not signed, so SmartScreen says the publisher is unknown. Choose "More info" and then "Run anyway". If you would rather not, build from source: the readme says how, and it takes one CMake command.

## macOS

The installer is signed and notarised, so it opens without a warning. It asks which of the three formats you want and installs only those.

## Licence

AGPLv3, which follows from JUCE. The whole source is in this repository.
