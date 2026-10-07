# Accessibility

Overtonium is a synthesiser with six hundred and forty-three knobs on it. That is a lot of small targets in a dark window, and it would be easy for the instrument to be usable only by people who can see it well and point accurately. This document says what the project does about that, what it does not do yet, and how to report one when you hit it.

There are two separate things here and they have different stories. The **plugin** is a window a host draws, and the **site** is static HTML. Most of what is written about accessibility in open source is about the second kind of thing. The first is where the harder problems are.

## What is true today

These are things that are checked rather than intended. Where a number appears it came from a measurement.

**Every control says what it is.** All 643 sliders carry an accessible name, and no two share one, which the test suite checks on every commit. A screen reader hears "Harmonic 19 attack" or "Reverb mix" rather than "attack" six hundred times, and the noise channel names itself rather than borrowing a number. JUCE skips an element with neither a name nor a description entirely, so on a mixer this size that check is the difference between an instrument you can use without seeing it and one you cannot.

**The readouts can be read by something other than an eye.** The tuning and level figures and the converter readouts are drawn as seven-segment shapes rather than written as text, so there is nothing in them for a screen reader to find. Each one carries a spoken name and value separately for that reason. This was a regression once: swapping a plain label for a segment display silently removed the name, and the suite now holds it.

**Every control is a host parameter.** All 836 of them. That matters more than it sounds: a host's own generic editor is a plain list of named parameters with text fields, and it is keyboard operable and screen reader readable in a way the plugin's own window is not. If the mixer is unusable for you, the host's generic view is a complete alternative rather than a reduced one. Nothing is hidden from it.

**Colour is never the only thing saying something important.** Each channel carries its harmonic's number at the head of the strip as well as its interval colour, and its tuning in cents as a figure. The one exception is noted below.

**Text on the lit caps holds its contrast in both states.** The legend is a fixed near-black printed on the plastic rather than part of what lights, so a word does not change colour under its own lamp. It measures 4.1 to 1 against the unlit cap and between 4.9 and 11.4 to 1 against the lit ones.

**The site's navigation can be stepped over.** Every page opens with a skip link, which is the first thing a keyboard reaches and the only way past the six section links without tabbing through all of them. It moves focus to the content rather than only scrolling to it, which is the half of that a link without `tabindex` gets wrong. Every section and subsection also carries a name, so a page can be linked at the part that answers the question rather than at the top, and every page lists its own contents under its own entry in the one site navigation, rather than in a second list somewhere else. On a window wide enough that navigation stands beside the page and marks both the page you are on and the section you are reading, which it says with `aria-current` rather than with colour alone. Beside the page it also folds a section's subsections away until you are in that section, which needs script, so with script blocked the whole list shows rather than most of it being hidden with nothing able to open it. It is an ordinary list of links either way: the marking is the only part that needs script, and without script the list still goes where it says.

**Pictures on the site declare their size.** Every image carries a width and a height so the page does not jump as it loads, and a check compares the declared numbers against the files. All twelve have alternative text.

## Known limitations

Written as what you would run into rather than as standards codes.

**The plugin's own window is not keyboard operable.** Tab does not move between controls and there is no focus indicator, because nothing in the editor takes keyboard focus. The only key it handles is undo. Every control is reachable from the host's generic parameter view instead, and from MIDI Learn, but that is a route around the problem rather than a fix for it. This is the largest barrier in the project and it is not close to being solved.

**Which macro is driving a knob is said only by colour.** A knob under a macro lights its ring in that macro's colour and there is no second cue. If you cannot tell the eight colours apart, the macro panel lists which rows and channels each macro reaches, but the knob itself will not tell you.

**The window does not scale with the operating system's text size.** It has its own zoom, in the Settings menu, which scales the whole window rather than the text alone.

**The video on the front page relies on YouTube's captions**, which are automatic rather than written. The thirty-three preset clips have a written description beside each one, which is a description of the sound rather than a transcript.

**None of this has been evaluated by anyone who relies on assistive technology.** Everything above is what the code does and what the tests check. It is not a conformance claim and no audit has been done. If you use a screen reader or a keyboard and try this, what you report will be worth more than anything in this file.

## What is being worked towards

[WCAG 2.2 Level AA](https://www.w3.org/TR/WCAG22) is the target for the site. It guides the work and is not a claim that the site meets it.

For the plugin there is no equivalent standard that fits, since WCAG is written for web content and a plugin window is not that. The things being worked towards are keyboard traversal of the mixer, a focus indicator that suits a panel this dense, and a second cue for the macro colours.

## If you contribute

**For the plugin.** A new control needs a name before it needs anything else. `Component::setTitle` is what JUCE reads, and the suite will fail if a slider has no title or shares one with another. If a control draws its value rather than writing it, as the segment displays do, it needs a spoken name and value of its own.

**For the site.** Keep the heading order unbroken, give links text that says where they go, put alternative text on images, and declare each image's width and height. Do not let colour be the only thing carrying a meaning. A new section wants an id, since something will want to link to it, and a new page wants the skip link the others have.

**What runs on every commit.** The accessible-names check over the whole mixer, and the image-dimensions check over the site. Neither covers much. Passing them is the floor rather than the bar.

## Reporting a barrier

Open an issue at [github.com/benjamindehli/overtonium/issues](https://github.com/benjamindehli/overtonium/issues). An accessibility report is information about the instrument that no amount of testing here produces.

What helps, as far as you are willing to say:

- What you were trying to do and what happened instead.
- Where: the plugin window, a particular page of the site, or the host's own view of the parameters.
- Your operating system, and your host if it is the plugin.
- Your assistive technology and its version.
- A recording, if you are comfortable making one.

You do not need to say anything about yourself, and you do not need to work out how severe it is or which guideline it falls under. Sorting that out is the maintainer's job rather than yours.

## What happens then

This is one person working on this in his own time, which sets what can honestly be promised.

- You will get a reply stating what the problem is understood to be, so you can say if that reading is wrong.
- If there is a way around it in the meantime, you will be told what it is.
- If it is not going to be fixed soon, you will be told that rather than left waiting. A barrier that is known and written down is more useful than one that is silently queued.
- Anything reported goes into the known limitations above, whether or not it is fixed, so the next person finds it before hitting it.
- You may be asked whether a fix actually helped before the issue is closed.

## Where it has been tried

The plugin runs as VST3, AU and LV2 on macOS, Windows and Linux, and as a standalone application. The site is static HTML and uses no framework.

Neither has been tested with a screen reader by someone who uses one daily. The accessible names are checked programmatically, which proves they exist and are distinct, and proves nothing about whether they are useful to listen to.

## Suggestions

If something here is wrong, missing, or promises more than the project delivers, open an issue or a pull request. A statement that overclaims is worse than one that admits a gap.
