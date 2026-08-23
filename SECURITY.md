# Security policy

## Supported versions

| Version | Supported |
| ------- | --------- |
| 1.0.x   | yes       |
| < 1.0   | no        |

Fixes go into the next release rather than into patches for older ones. This is a one-person project and there is no long-term support branch to promise.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting, on the [Security tab](https://github.com/benjamindehli/overtonium/security) of the repository. That opens a report only the maintainer can see, so nothing is public while it is being looked at.

Please do not open a normal issue for something exploitable, since issues are public the moment they are filed.

Expect an acknowledgement within a week. A one-person project cannot promise faster than that, and saying so beats a number nobody meets.

## What is worth reporting

Overtonium is an audio plugin. It has no web browser compiled in, stores no credentials and talks to no service, so most of what a security policy usually covers does not apply. What is left is the untrusted input it reads, and the one request it can make:

- **Preset files.** A `.overtonium` preset is XML read from disk, and presets get passed between people. Anything that turns a malformed or hostile preset into a crash, an out-of-bounds access or code execution is worth reporting.
- **Host state blobs.** The state a host hands back through `setStateInformation` is untrusted in the same way, since it travels inside project files.
- **MIDI input**, including MPE, which arrives from hardware and from other software.
- **The update check.** When switched on, and only then, the plugin fetches `https://benjamindehli.github.io/overtonium/latest.json` in the background and reads a version string out of it. It sends nothing beyond the request itself, and the response is length-capped and parsed as JSON. A way to make a hostile response do more than fail quietly is worth reporting.
- **The installers.** The macOS package is signed and notarised. The Windows installer is deliberately unsigned, which is a known and documented gap rather than a finding: SmartScreen warns about it and the release notes say why.

A crash on its own is a bug rather than a vulnerability, and an ordinary issue is the right place for it. Report it privately if you can see a way to make it do something worse than stop.

## The update check

Off until asked. The first time an editor opens, the plugin asks whether it may look, and nothing leaves the machine before that is answered yes. The answer is saved with the session and the setting is under Settings.

It is a GET for one static file on the project's own GitHub Pages site. No identifier, no version, no telemetry: the request carries nothing the plugin chose to put in it, and the server it reaches is the same one serving the documentation.

## What this policy does not cover

Vulnerabilities in JUCE itself belong to [the JUCE project](https://github.com/juce-framework/JUCE), and vulnerabilities in a host belong to whoever makes the host. Tell us anyway if the interaction with Overtonium is the interesting part.
