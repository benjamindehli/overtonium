# Accessibility

Overtonium is a synthesizer with six hundred and fifty-nine knobs on it. That is a lot of small targets in a dark window, and the instrument could easily end up usable only by people who see well and point accurately. This document says what the project does about that, what it does not do yet, and how to report a problem when you find one.

There are two separate things here, and they are in different situations. The **plugin** is a window a host draws, and the **site** is static HTML. Most writing about accessibility in open source is about the second kind. The harder problems are in the first.

## What is true today

These are things that are checked, not just intended. Where a number appears, it comes from a measurement.

**Every control has a name.** All 659 sliders have an accessible name, and no two share one. The test suite checks this on every commit. A screen reader hears "Harmonic 19 attack" or "Reverb mix", not "attack" six hundred times, and the noise channel has its own name instead of a number. JUCE skips an element entirely if it has neither a name nor a description. On a mixer this size, that check decides whether the instrument can be used without seeing it.

**The readouts can be read without looking at them.** The tuning and level figures, the converter readouts and the displays in the Playing and Tuning bars are drawn as segment shapes, not written as text, so a screen reader finds nothing in them. Each one therefore has a separate spoken name and value. Replacing a plain label with a segment display has removed the name without any warning before, and the suite checks for that.

**Every control is a host parameter**, 850 parameters in all. This matters more than it sounds. A host's own generic editor is a plain list of named parameters with text fields, and it works with a keyboard and a screen reader in a way the plugin's own window does not. If you cannot use the mixer, the host's generic view is a complete alternative, not a reduced one. Nothing is hidden from it.

**Colour is never the only way something important is shown.** Each channel shows its harmonic's number at the top of the strip as well as its interval colour, and its tuning in cents as a figure. The one exception is listed below.

**Text on the lit caps keeps its contrast in both states.** The legend is a fixed near-black printed on the plastic, and it is not part of what lights up, so a word does not change colour under its own lamp. It measures 4.1 to 1 against the unlit cap and between 4.9 and 11.4 to 1 against the lit ones.

**You can skip past the site's navigation.** Every page starts with a skip link. It is the first thing a keyboard reaches, and the only way past the six section links without tabbing through all of them. It moves focus to the content as well as scrolling to it. A link without `tabindex` only does the scrolling. Every section and subsection also has a name, so a link can point to the part of a page that answers a question instead of the top. Each page lists its own contents under its entry in the one site navigation, not in a second list somewhere else. On a window wide enough for the navigation to sit beside the page, it marks both the page you are on and the section you are reading. It does this with `aria-current`, not with colour alone. In that layout it also folds a section's subsections away until you are in that section. That needs script, so with script blocked the whole list is shown, instead of most of it being hidden with no way to open it. Either way it is an ordinary list of links. Only the marking needs script, and without script every link still goes where it says.

**Pictures on the site declare their size.** Every image has a width and a height, so the page does not jump while it loads, and a check compares the declared numbers with the files. Every picture that shows something has alternative text, and the purely decorative ones have an empty one, so a screen reader skips them.

## Known limitations

These are described as what you would run into, not as standards codes.

**The plugin's own window cannot be used with a keyboard.** Tab does not move between controls and there is no focus indicator, because nothing in the editor takes keyboard focus. The only keys it handles are undo and redo. Every control can be reached from the host's generic parameter view instead, and from MIDI Learn, but that is a way around the problem, not a fix. This is the largest barrier in the project, and it is not close to being solved.

**Which macro controls a knob is shown only by colour.** A knob under a macro lights its ring in that macro's colour, and there is no second cue. On a channel that is muted or silenced by a solo the ring goes dark, so even the colour is gone there. If you cannot tell the eight colours apart, the macro panel lists which rows and channels each macro reaches, and the macro knobs in the Playing bar name the row each one targets. A knob in the mixer will not tell you.

**The Playing bar's three meters give their name but not their value.** PITCH, WHEEL and PRESSURE have a spoken name and no value, so a screen reader can find them but cannot read them. They show what a controller is doing, not a setting, so there is no host parameter to read instead.

**The window does not scale with the operating system's text size.** It has its own zoom, in the Settings menu, which scales the whole window, not only the text.

**The video on the front page relies on YouTube's captions**, which are automatic, not written by hand. The thirty-eight preset clips each have a written description next to them, which describes the sound and is not a transcript.

**Nobody who relies on assistive technology has evaluated any of this.** Everything above is what the code does and what the tests check. It is not a conformance claim, and no audit has been done. If you use a screen reader or a keyboard and try this, your report will be worth more than anything in this file.

## What is being worked towards

[WCAG 2.2 Level AA](https://www.w3.org/TR/WCAG22) is the target for the site. It guides the work, and it is not a claim that the site meets it.

For the plugin there is no matching standard, because WCAG is written for web content and a plugin window is not web content. The goals for the plugin are keyboard navigation of the mixer, a focus indicator that works on a panel this dense, and a second cue for the macro colours.

## If you contribute

**For the plugin.** A new control needs a name before anything else. JUCE reads it from `Component::setTitle`, and the suite fails if a slider has no title or shares one with another. If a control draws its value instead of writing it, as the segment displays do, it needs its own spoken name and value.

**For the site.** Keep the heading order unbroken, give links text that says where they go, put alternative text on images, and declare each image's width and height. Do not let colour be the only thing that carries a meaning. Give a new section an id, because something will want to link to it, and give a new page the same skip link the others have.

**What runs on every commit.** The accessible-names check over the whole mixer, and the image-dimensions check over the site. Neither covers much. Passing them is the minimum, not the goal.

## Reporting a barrier

Open an issue at [github.com/benjamindehli/overtonium/issues](https://github.com/benjamindehli/overtonium/issues). An accessibility report tells the project things about the instrument that no amount of testing here can find.

What helps, as far as you are willing to share:

- What you were trying to do and what happened instead.
- Where: the plugin window, a particular page of the site, or the host's own view of the parameters.
- Your operating system, and your host if it is the plugin.
- Your assistive technology and its version.
- A recording, if you are comfortable making one.

You do not need to say anything about yourself, and you do not need to work out how severe it is or which guideline it falls under. Sorting that out is the maintainer's job, not yours.

## What happens then

This is one person working on it in his own time, and that limits what can be promised.

- You will get a reply that describes the problem as it is understood, so you can say if that is wrong.
- If there is a workaround in the meantime, you will be told what it is.
- If it is not going to be fixed soon, you will be told so instead of being left waiting. A known, documented barrier is more useful than one that waits in a queue with no word.
- Everything reported goes into the known limitations above, whether or not it is fixed, so the next person finds it before running into it.
- You may be asked whether a fix actually helped before the issue is closed.

## Where it has been tried

The plugin runs as VST3, AU and LV2 on macOS, Windows and Linux, and as a standalone application. The site is static HTML and uses no framework.

Neither has been tested with a screen reader by someone who uses one daily. The accessible names are checked by the tests, which proves they exist and are all different. It proves nothing about whether they are useful to listen to.

## Suggestions

If something here is wrong, missing, or promises more than the project delivers, open an issue or a pull request. A statement that claims too much is worse than one that admits a gap.
