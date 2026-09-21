# Which character each factory preset gets

Working notes for the 1.9.0 release, not part of the repository. Delete it once the decisions are in `Source/Presets.cpp`. It is untracked and `.gitignore` says nothing about it, so it will show up in `git status` until it goes.

Suggestions only. They come from what each circuit does against what each patch is, not from listening, so every one of them is a starting point to overrule.

## The list

Fill in the right-hand column. Anything goes: a name, "keep", "no", a question mark.

| Preset           | Suggested | Why that one                                                              | Decided |
| ---------------- | --------- | ------------------------------------------------------------------------- | ------- |
| 2-bit Fuzz Organ | Pure      | the converter is already the distortion, anything added lands under it    | Valve   |
| Big Saw          | Squashed  | a saw wants edge, and the odd harmonics beat against its own drift        | Slewed  |
| Cathedral        | Squashed  | principal pipes are odd-harmonic, and it hardens as the chorus fills      | Squashed|
| DigiLog          | Valve     | the patch is already "a digital instrument through tired analogue gear"   | Slewed  |
| Dire Dire EP     | Valve     | round electric piano, an octave on every partial is what that is          | Valve   |
| Drawbar Organ    | Bulb      | a tonewheel adds no harmonics either, and the 3 ct spread is the leakage  | Bulb    |
| Dream Phase      | Bulb      | the level breathing behind the squares is free movement on a pad          | Bulb    |
| EP Chimes        | Slewed    | the chimes are high enough to be in the band where it bites               | Slewed  |
| Equal Saw        | **Pure**  | the tuning page plays it against Just Saw, so timbre stays out of it      | Pure    |
| FM Piano         | Pure      | a DX7 is digital and sounds like it on purpose                            | Pure    |
| Glass Armonica   | Bulb      | rubbed glass answers the hand, which nothing else here does               | Bulb    |
| Glockenspiel     | Slewed    | struck bar, everything that matters is above the corner                   | Bulb    |
| Init             | **Pure**  | it is the neutral starting point                                          | Pure    |
| Just Saw         | **Pure**  | same as Equal Saw, it is a tuning reference                               | Pure    |
| Lo-fi            | Pure      | same as the fuzz organ                                                    | Slewed  |
| Metallic Piano   | Folded    | the kink is what thin and metallic is                                     | Pure    |
| Music Box        | Folded    | plucked steel teeth, even harmonics on an already irregular vibrato       | Bulb    |
| Nylon EP         | Bulb      | "round" means adding nothing, and this is the one that adds nothing       | Bulb    |
| Odd Harmonics    | Squashed  | the only character that cannot make an even harmonic, so it stays stopped | Squashed|
| Omni-84          | Squashed  | a 1984 consumer instrument leaning on its rails                           | Pure    |
| Shimmer          | Bulb      | every partial already breathes, this breathes with the pitch as well      | Bulb    |
| Slow Pad         | Bulb      | it has all the time in the world for a lamp to settle                     | Bulb    |
| Space Flute      | Bulb      | a flute's level is the breath, and the breath is already on the noise     | Bulb    |
| Sparkle Pad      | Slewed    | the half of it that matters is short, bright and high                     | Slewed  |
| Stepped          | Bulb      | the pitch steps on squares and the level jumps behind every step          | Bulb    |
| Struck Bell      | Slewed    | the top hardens as you play up, which is what a bell does                 | Slewed  |
| StyloPoly        | Squashed  | a Stylophone is one cheap oscillator against its rails                    | Slewed  |
| Synth Ensemble   | Valve     | brass warmth, and it is already drifting apart                            | Valve   |
| Tape Choir       | Valve     | old machine, and the second harmonic is the half of tape nobody minds     | Valve   |
| Vibraphone       | Folded    | struck metal, and the fundamental is too low for Slewed to touch          | Slewed  |
| Wurli            | Valve     | a 200A is a reed into its own amplifier                                   | Bulb    |

## Two things to know while listening

**Bulb does nothing on a still note.** It only shows itself under vibrato, a bend or a finger moving, which on the Osmose is all the time. A patch judged on a held chord will sound exactly like Pure.

**Slewed does nothing below a kilohertz.** Whether it suits a patch depends on where that patch's partials land rather than on the patch, so the same preset can be right for it two octaves up and pointless two octaves down.

Every character except Pure also brings its rack: a fixed spread of a few cents and a fraction of a decibel across the 32 partials, the same every time. That is audible on a sustained patch with no detuning of its own even where the added harmonics are not.

## Three to leave alone

Init, Just Saw and Equal Saw. The last two are played against each other on the tuning page and the front page to demonstrate tuning, so a timbre change there would be changing the subject.

## What happens to each one that moves

One line in that preset's case in `Source/Presets.cpp`, after `ap.set("track", ...)` and before the lofi and effect lines, which is where the authoring build's generator puts it:

```cpp
ap.set("character", 4.0f);
```

Pure 0, Bulb 1, Squashed 2, Folded 3, Valve 4, Slewed 5.

Then, for each preset that moved:

- its clip on the presets page is re-rendered, since the clip is the preset playing
- the release notes, `DESIGN.md` and the controls page stop saying every factory preset is Pure
- the release notes gain a line saying which presets changed, since a factory preset sounding different is something a session carries

Already checked: a factory preset carrying a character passes both suites. `testPresetsAreReproducible` now dirties the character before reloading, so a preset whose character failed to apply is caught.
