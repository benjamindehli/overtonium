## What this changes

<!-- What it does, and why it is worth doing. A sentence is often enough. -->

## How you know it works

<!--
What you ran, or what you listened to. A change to the DSP wants a test that
fails without it. A change to the interface wants saying which host or platform
you looked at it in, since CI can build all three and has ears on none.
-->

## Before you open it

Three things run in CI and all three are quick to check first.
[CONTRIBUTING.md](https://github.com/benjamindehli/overtonium/blob/main/CONTRIBUTING.md) has the commands.

- [ ] `ctest --test-dir build --build-config Release` passes, both suites
- [ ] It builds clean with `-DOVERTONIUM_WARNINGS_AS_ERRORS=ON`, which is a
      separate tree rather than your working one
- [ ] `clang-format -i` on the C++ you touched, and `npm run format` for
      everything else

Not a checklist to satisfy. If one of them fails and you want a second pair of
eyes on why, open it anyway and say so.

## Anything you are unsure about

<!--
A decision you went back and forth on, a measurement that surprised you, a
place you think the reasoning is thin. This is the useful part of a pull
request and the easiest one to leave out.
-->
