Page: Overtonium releases: what changed in each version
URL: https://benjamindehli.github.io/overtonium/releases/
Last changed: 2026-10-17

# Overtonium releases

What changed in each version, newest first. Each one links to its full notes, which say more and carry the installing and upgrading detail as well.

## 1.14.0, 16 October 2026

**A string in the noise channel.** A Karplus-Strong string the noise strikes, tuned to the note and following the wheel, glide, MPE and every temperament. STRING, DECAY, DAMP and POINT sit under COLOUR. A short noise envelope plucks it and a held one blows into it, so a pluck and a pitched breath are settings, not modes.

**The resonator.** Eighty-eight sympathetic strings that ring with what you play, before the bus drive, in their own frame on the top bar. Piano frees the strings of held keys and the sustain pedal frees the rest, Open keeps the pedal down, Octave rings twelve strings from C3 and Series the root's own harmonics. MIX, DECAY and DAMP keep the level about the same across their range. The new preset Space Cowboy uses both, and Metallic Piano now rings its own strings.

**A rearranged top bar and a faster mixer.** The four buttons come first, then the wordmark over the preset screen, with the spacing evened out. The update question is asked inside the window, and SETTINGS stays lit while a new version waits. Silenced channels and switched-off stages turn their lights fully off. Soloing, scrolling and resizing redraw the mixer far faster. A wheel over a knob in the bars no longer scrolls the mixer, Fit all 32 channels gives every row its full height, and JUCE 9.0.4 fixes a possible crash in Windows hosts when a window closes.

**Before upgrading.** Space Cowboy sorts at program 25, so a MIDI program change from 25 up lands one preset later, while a saved session keeps its preset by name. Metallic Piano sounds fuller than it did. There are 850 parameters, the eight new ones on the end.

[The full notes for 1.14.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.14.0)

## 1.13.0, 13 October 2026

**Adaptive, a temperament that retunes as you play.** After Eivind Groven's organs, every pitch class has five versions a comma apart, and the chord you hold is kept on whichever make it purest, settled again every time a key goes down or comes up. A held note the next chord needs elsewhere slides there over the Retune time. A dominant seventh is tuned on the harmonic seventh, 4:5:6:7, and NO DRIFT keeps the keyboard home where a progression would carry it off a comma at a time. Every one of the 125 chord types in music-utils is played through it in all twelve keys on every build.

**Two bars under the top bar.** PLAYING and TUNING open a bar each, both closed until you open them. The Tuning bar shows twelve displays, one for each note, saying how far it sits from equal temperament in cents, in every temperament, and under Adaptive a PURE lamp, 7:4, DRIFT, NO DRIFT and the Retune knob. The Playing bar holds the voice and expression settings, shows the voices in use, the sustain pedal, the pitch wheel, the mod wheel and pressure, and carries the eight macros as knobs captioned with what they are aimed at, which a right-click assigns, opens in MACROS or binds to a controller.

**Displays that behave like real ones, and No stack.** Every segment display is a fixed row of cells that light or stay dark, with no dimmed figures, cleaner letters and a proper sharp sign. One voice per key is renamed No stack, for what it does. Right-clicking a knob on the top bar now offers MIDI Learn, which it never did.

**Before upgrading.** Adaptive is a seventh entry on the temperament list, so temperament automation drawn against six lands a little further along, while a saved session keeps its temperament. A host shows No Stack, Pitch Mod Shared and Amp Mod Shared in place of the old names, with the same ids. There are 842 parameters.

[The full notes for 1.13.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.13.0)

## 1.12.0, 10 October 2026

**Glide on every channel.** Every channel has its own glide time, the last row under PITCH MOD, from Off to five seconds. Because each partial travels on its own time, the series does not have to arrive together: short times on the top land the brightness first and let the pitch follow, and the other way round the note arrives and its colour catches up. The needle on each partial's PITCH MOD rule follows the glide back to centre. Two switches govern every channel at once, as lit letters beside the GLIDE caption: **L** glides always or only legato, and **T** makes the time a fixed rate per octave or a fixed time however far a note goes.

**ATTACK reaches into where a partial starts.** The PHASE knob is gone. Turned past its shortest time, ATTACK reads in degrees, from a rising zero crossing at 0° to the partial's own peak at 90°, and STRIKE moves along the whole knob, so a hard blow lands on the peak and a light one travels up into a short attack.

**Legato is a switch, and with more than one voice it plays a top line.** With one voice it is the monophonic legato it always was. With more, a melody held from note to note carries on over chords that still retrigger each time their keys come up and go down, and the note a melody carries on from always sounds. A wrong guess about which key is the melody costs an attack and nothing more.

**Shared by all notes, a thirty-fourth preset, and a quieter panel.** The in-phase switch is renamed for what it does and has a switch of its own beside each SHAPE caption. _Glide_ is a pad whose partials glide in up the series, round played softly and bright played hard. The panel is a shade darker with lighter captions, which now clear the 4.5:1 contrast the accessibility standard asks for, and a dark sliver at the top left corner of every screen and lamp is gone.

**Before upgrading.** The Limiter's lookahead is off by default, so new instances report 12 samples of latency at 48 kHz rather than 108, and a saved session keeps its setting. Start phase is gone, so a patch that set one starts each partial at the zero crossing, and an automation lane on it in a host that goes by position now moves glide. ATTACK automation drawn against the old travel lands on different times. _Glide_ sorts after _Glass Armonica_, so a MIDI program change will name the preset above the one it used to, while a saved session carries the name and is unaffected. There are 839 parameters.

[The full notes for 1.12.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.12.0)

## 1.11.1, 7 October 2026

**Four controls read to seven decimal places.** The pitch modulator's rate and depth, the drift and the amplitude modulator's rate had no text of their own, so a host's automation lane, the panel's value popup and a screen reader all said `4.0000005 Hz` for four. They now read as the other controls do, `4.00 Hz`, `30.0 Hz` and `0 ct`. The macro amount reads as the panel shows it, `-100.0 %` to `+100.0 %`, rather than to four places, and a value typed into a host is read as a percentage. Only the text changed: ranges, defaults and stored values are as they were, so sessions, presets and automation play back exactly as before.

[The full notes for 1.11.1](https://github.com/benjamindehli/overtonium/releases/tag/v1.11.1)

## 1.11.0, 7 October 2026

**Eight macros, so a relationship across the series can be automated.** A LINK drag moves 32 parameters and a host catches only the last one touched, so a relationship you can edit by hand could not be recorded at all. A macro names a row, a scope and a curve, and gives that relationship one control and the host one lane. It offsets rather than writes, so the patch underneath is untouched, which is why the knobs do not move when a macro does. A driven control says both things at once instead: the ring of ticks lights to where the macro has taken the value in the macro's colour, and the pointer stays at what the patch holds. A fader says it in the gutters either side of its track. Eight is a pool rather than a count, since a plugin declares its parameters once and cannot add one later.

**MIDI Learn.** Right-click a control, choose MIDI Learn, move something on the controller. One parameter per controller number, saved with the session rather than with a patch, because which controller moves which knob describes the desk rather than the sound. Eleven numbers are refused rather than bound, the mod wheel and the MPE slide among them, so arming a knob and then reaching for the slide to steady a note leaves the note steadied and the knob still waiting.

**CLIP is five machines instead of a switch.** _Soft_ is the curve that was already there and came through bit for bit. _Hard_ is a wall at unity. _Asymmetric_ lets one half of the wave lean over first, the way a single-ended stage runs out of supply in one direction, which gives even harmonics where the others give odd. _Limiter_ does not bend the wave at all, it moves the level. _Fold_ turns the wave back on itself and sings where the rest crunch. All five are bounded by full scale, tested at drives up to 400 times at three sample rates. The limiter looks ahead two milliseconds, which is what took the reported latency from 12 samples to 108 at 48 kHz, and Settings can refuse it.

**The parameters scroll.** The rows are small on a 1080p screen and zooming in to read them made the window taller than such a screen has room for. A channel is three bands now: the channel number at the top and the fader at the foot are pinned, and everything between them scrolls behind an opening, so neither of the two things you find your place by is ever what has scrolled away.

**The panel was rebuilt.** Every switch is a lit plastic cap in a moulded well, with a fixed near-black legend printed on the plastic so a word does not change colour under its own lamp. The knobs are discs with an eight-slot collar, and the notches are cut out of it rather than painted on. The preset name is spelled on a fourteen-bar display. The OUTPUT rule carries two lamps, VELOCITY on the left and AFTERTOUCH on the right, so all five rules say something. LINK and DRAW became one tool button rather than two switches that could not both be on, and choosing a scope now lights the channel number of every strip a drag would reach.

**A thirty-third preset, and an accessibility statement.** _6581 Triangle_ is the triangle of a MOS 6581, the SID in a Commodore 64, which is not quite a triangle: the chip draws its ramp as a staircase of 2048 steps, so it carries even harmonics a triangle should not have. `ACCESSIBILITY.md` says what is checked rather than intended, and says plainly that the plugin's own window is not keyboard operable and that nobody who relies on assistive technology has evaluated any of it.

**Fixed.** A partial silenced by a negative AFTERTOUCH amount took every lamp on its channel dark with it, which had been wrong for the envelope and tremolo lamps since those lamps existed. And a note played while the channel was already under pressure thumped or faded in, because the pressure smoother started from nothing at every note-on instead of from where the channel was holding it.

**Before upgrading.** The plugin reports 108 samples of latency at 48 kHz where it reported 12, which is the limiter's window, and Settings can take it back. Loading a preset now moves the master fader and the clipper, since both left the settings a preset cannot reach. _6581 Triangle_ sorts third, so a MIDI program change will name the preset above the one it used to, while a saved session carries the name as well as the number and is unaffected. Fifty parameters were added, 836 in all, all of them on the end, so every automation lane written before this release still points at the control it was pointing at.

[The full notes for 1.11.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.11.0)

## 1.10.1, 30 September 2026

**Removing the plugin could hang the host.** In a host that unloads the plugin binary once no instances are left, taking the last one out could lock the host up, with no way out but to end the process. Reported in FL Studio and in Reaper, while hosts that keep the binary loaded were never affected.

The texture the panels are drawn with is an image, built the first time a window opens and shared by every window after it, and it was being kept for the life of the process. Under Windows that image lives on the graphics card, so letting go of it hands resources back to the driver, and keeping it in a static meant that happened while the host was unloading the binary. Windows holds a lock for the whole of an unload and the driver work cannot run until that lock is free, so neither finishes. The texture now belongs to the windows that use it and goes when the last one closes. Nothing else changed, and nothing about the sound has.

## 1.10.0, 30 September 2026

**The echo is three machines, and so is the reverb.** Both were a switch that turned one thing on, and both are now a button that says which machine is running. _Tape_ is the loop that was already there. _BBD_ is a line of buckets, which gets darker the longer you set it to, because a longer delay can only be had by clocking the buckets more slowly and the filter has to come down with the clock. _Digital_ does not pretend to be anything and is the only ping-pong of the three, crossing every repeat to the other side where the other two never cross at all. AGE is the same knob on all three and asks each for the thing that machine gets worse at: tape wear, a slowing clock and a breathing compander, or falling bits and sample rate.

_Room_ is the reverb that was already there. _Modulated plate_ is a sheet of steel with no geometry to hear, so a hit spreads across all of it at once and the tail is dense from its first instant rather than arriving in stages. It is called modulated because it is: its allpasses wander six times as far as the shape they come from gives them, and without that a long decay repeats its own pattern instead of washing. _Spring_ is a tray of three helical springs, and a helix carries the top of the band faster than the bottom, so a hit comes back as a chirp that starts high and falls and every bounce adds another. DECAY, DAMP and PRE mean the same thing on all three, and the wet levels are matched, so switching machines changes the character of a tail rather than its length or its loudness.

**The character now says how hard the mix is driven.** The thirty-three channels are summed onto a bus that drives, by an amount belonging to the oscillator character. It answers to level rather than to a setting, which is what a table baked into one partial's waveform cannot do: play quietly and it is barely there, lean on the keyboard and it arrives. Bulb asks for the most and Diode the least, Pure sums cleanly and does nothing, and there is no knob, because how hard a summing amplifier is driven is part of what choosing a character means.

**How fast you let a key go sets the level its tail starts from.** On a keyboard that senses a release, the hardest lift doubles the key-off level and the softest halves it. There is no knob and no parameter: it is a property of the gesture the way velocity is, and a keyboard that cannot sense a release changes nothing.

**The master volume is a glass fader lying across the output meter**, over the lamps it sets the level of, instead of a knob in a group of its own. A LINK drag that spreads a move uniformly now spreads it in decibels as well: on the fader row it was uniform in travel, which put six or seven decibels on the loudest channel and thirty on the quietest.

**A thirty-second factory preset, and four others that reach for the new machines.** _60s Organ_ is a combo organ that has not been serviced: the partials are pulled off each other rather than tuned, so the stack beats against itself instead of fusing, and the noise channel is a few milliseconds of key click on every note. It runs into a bucket brigade driven hard enough that the repeats grow rather than fade, and falls into a spring still ringing when the next chord arrives. It is also the first patch to ask for Diode. _Shimmer_ and _Tape Choir_ now ask for the plate rather than the room, and _Cathedral_ and _Struck Bell_ stay on the room. Because the list is alphabetical and 60s Organ sorts second, a MIDI program change now names the preset above the one it used to. A saved session is unaffected, since it carries the name as well as the number.

**LINK came back switched off.** Its scope and its curve were remembered with the session and the switch that decides whether either applies was not, so reopening a window gave you exactly the drag you had set up, turned off. All three are saved together now.

**Unfolding a section made the faders swell.** On a window shrunk until the faders were squeezed, unfolding gave back twice the height it had taken, all of it to the faders. Applying the window's new size limits is itself a resize, so the height was measured after it had already moved and the same rows counted twice.

**Draw the faders.** A drag across the fader area sets every channel it passes over from the pointer's height, so a spectrum goes in with one stroke instead of thirty-two drags. Hold shift for it, or latch it on with the new DRAW switch under the LEVEL caption. Armed, every fader lights in the same accent the switch does, so what a drag would reach is visible before you touch anything. It is not LINK under another name: LINK shares one relative move out across the channels by a rule, and this sets absolute values freehand. Columns crossed between one moment and the next are filled in, the whole stroke is one undo, and the noise channel draws with the rest. While it is armed the pointer becomes a pencil and LINK reads as off, without being off.

**A section folds from any strip, not just from the gutter.** Every section draws a rule across all thirty-three columns, lined up with its heading in the caption gutter on the far left, and clicking one of those rules now folds that section. Reaching the gutter meant leaving the partial you were working on, crossing the mixer and finding your way back, by which time you had lost which column you were in. The pointer shows the same hand over a rule as over a heading.

**The bar says what each group is.** A small caption under the button at the head of the character, echo and reverb groups, because those buttons show a value and nothing said what OP-AMP, TAPE or ROOM was a choice of. With the caption doing that, the button reads OFF when the effect is off, where it used to read ECHO or REVERB so the group could still be found, and the ring of lit ticks around that effect's four knobs goes dark with it. The knobs keep their positions, so a setting can be dialled in before the thing is switched on. The clipper also has a switch on the bar now, CLIP, under the output meter beside the two converter readouts, which are a little larger themselves. It is still in the Settings menu too.

**Fixed.** Right-clicking the gap between two sections opened two LINK menus stacked on each other, and picking an item on the front one left the one behind it unchanged, so the setting looked to have been refused when it had already taken. A scroll over a knob also dragged the mixer sideways whenever the window was narrow enough for the series to need scrolling, since each strip watches everything inside it and passed the wheel on after the knob had taken it. A scroll on a strip's background still moves the series. The plugin would not tell you which version it was: not in the Settings menu, not on the panel, not anywhere, while the bug report template refused a report without one and said to read it off that menu. It is the menu's first heading now. The first repeat of an echo came back clean while every repeat after it was worn, on both the tape and the bucket brigade. Both echoes compressed everything they were handed, because the tape's backstop was set for a bus running near one where a full patch runs closer to two. The digital delay had a tail that never went away at any feedback from a half upwards, which is what a quantiser inside a feedback loop does. And the zoom menu ticked 100% whatever the zoom was.

**Before upgrading.** Presets on a character sound fuller, because the bus they are summed onto now drives, and that is the intended sound of them. The echoes are cleaner and their first repeat is coloured, so a patch leaning on the tape echo's compression will not find it. _Sparkle Pad_ is louder and otherwise unchanged, since it was mixed low on purpose to keep that compression down. Your own patches get the tape and the room, which is what every patch written before this had. Two parameters were added, 786 in all, both on the end, so every automation lane written before this release still points at the control it was pointing at.

[The full notes for 1.10.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.10.0)

## 1.9.0, 23 September 2026

**Which oscillator the partials are.** A new control at the head of the series group, offering six of them. All six are sine oscillators, and what differs is the way each circuit fails to make one. _Pure_ is the sine every partial already was. _Bulb_ is a Wien bridge whose lamp lags, so the level sags behind every move of the pitch and recovers over about half a second. _Rail_ grows into its own supply, _Diode_ is bent into shape by two of them that do not match, and _Valve_ is a triode with an octave on top of every partial. _Op-amp_ is an amplifier that cannot keep up, so the top of the series hardens as it climbs while the bottom stays where it was. Each one is also a rack of thirty-two rather than one oscillator used thirty-two times: a fixed spread of a couple of cents and a fraction of a decibel, the same every time you load the patch. It is part of the patch, so a preset carries it, and twenty-five of the thirty-one factory presets now ask for one: eleven on Bulb, eight on Op-amp, four on Valve and two on Rail. Loading one of those gives what it ships as rather than what it was in 1.8.0, and setting the character back to Pure gives the older sound, except on _Synth Ensemble_, which was reworked further: its velocity amounts now take more of the series, so a soft chord arrives with less above the first few partials, and its reverb sits a third in rather than a quarter. A session is unaffected either way, since it carries the values rather than the preset's name.

**A MIDI program change loads a factory preset**, by its position in the alphabetical Factory list counting from zero, so a keyboard, a foot controller or a clip can change the sound without anyone touching the menu. A number past the last preset is ignored rather than wrapping.

**The converter readouts say what they are counting**, reading `48.0 kHz` and `8 bit` where they used to have room for the figure alone. LINK moved to the top of the caption column on the left to make that room, over the rows it gangs, and with it gone the bar fits on one row at the width the window opens at. Every switch on the panel now lights its word rather than its face, every screen and lamp is set into it with the lip of the opening it sits in, the standalone wears the instrument's own title bar instead of the grey-green one every unstyled JUCE application gets, and Settings has an entry that puts a window dragged narrow back to the width that shows all 32 channels.

**A modulator can be one circuit the whole keyboard hears.** Both belong to the note by default, so a chord struck a note at a time gives you three tremolos breathing out of step. The new entry at the foot of a shape button's menu, one for each modulator, hands the phase to the channel instead, so a note arriving late joins what is already running and a chord breathes as one thing. _Wurli_ ships sharing its tremolo and _StyloPoly_ its vibrato.

**Undo took a step several times a second while you were still turning a knob**, which was most obvious with a scroll wheel. A gesture is one step now, however many values it moved and however long it went on, and a gesture that ends where it began is not a step at all. A host playing an automation lane used to fill the history as well, and no longer leaves any: the history holds what a person did.

**Before upgrading.** Nothing you have made moves: a session opens sounding as it did, and your own presets get Pure and a modulator per note, which is what every patch written before this had. Twenty-five of the factory presets do sound different, since they now ask for a character, and two of those share a modulator across the keyboard as well. Three parameters were added, 784 in all, all three on the end, so every automation lane written before this release still points at the control it was pointing at.

[The full notes for 1.9.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.9.0)

## 1.8.0, 19 September 2026

**Three more factory presets, thirty-one in all.** All three lean on STRIKE, which arrived in 1.7.0 and is what lets one patch play two ways depending on how it is struck. _Glockenspiel_ is a struck bar whose tone sits in the fourth and eleventh partials rather than the fundamental, with nothing sustaining and every partial ringing down for its own length. _Dream Phase_ is a strike that becomes a pad, the short partials the transient and the long ones arriving behind them. _Sparkle Pad_ splits the series in two: the bottom three sustain and arrive slowly, everything above attacks at once and is gone inside half a second.

**A session could open with the wrong preset showing as selected.** Presets sort alphabetically, so adding one moved every preset after it and an index saved last year named a different sound. The sound was never in doubt, since a session stores its values, but a host's program menu would highlight a patch the session was not playing. The saved name decides now, so reordering presets can no longer move a session onto a different one.

**Built on JUCE 9.0.2**, up from 8.0.15, with nothing in Overtonium needing to change for it. Every audio example on this site renders to the same samples before and after. Building from source on Linux now also needs `libxi-dev` and `libegl-dev`.

**Before upgrading.** The factory presets are renumbered, since the three new ones sort into alphabetical place. The sound is stored with the session either way, and with the fix above the name you saved is what the menu goes by. No parameter was added or removed, so automation is unaffected.

[The full notes for 1.8.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.8.0)

## 1.7.0, 12 September 2026

**STRIKE aims key velocity at the front of the envelope.** It sits at the head of the ENVELOPE group on every one of the thirty-three channels, above DELAY and ATTACK, because it is about the blow rather than about the shape. A hard note starts sooner and arrives faster, a soft one waits and then opens slowly. Each knob below it stays the limit of its own row: velocity only ever lengthens the attack and only ever pulls the delay in, so ATTACK goes on meaning the fastest that partial gets and DELAY the latest it ever arrives. The range is wide because the attacks worth stretching are short, so 2 ms under a hard blow is half a second under the softest. _Synth Ensemble_ is rebuilt around it.

**MPE slide no longer lurches** on a controller whose slide axis rests at one end rather than at the centre. It is read as movement now: the first value a note is given becomes that note's nought, and what reaches the sound is how far the axis has travelled from it since. A Seaboard behaves as it always did. An Expressive E Osmose, which spends the first part of the key travel on pressure and only then starts sending slide, no longer jumps darker the instant the axis engages.

**Before upgrading.** LIFT is gone. It aimed release velocity at the key-off level and was the one control here that most keyboards cannot play, so a patch that used it loses those values and an automation lane pointed at one finds nothing. The parameter count is unchanged at 781 and everything else is untouched: every factory preset but _Synth Ensemble_ renders to a byte-identical file before and after. A finger landing forward on a pad no longer starts the note bright, which is what reading slide as movement costs.

[The full notes for 1.7.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.7.0)

## 1.6.1, 11 September 2026

One fix, and nothing else. The waveform selectors added in 1.6.0 kept listening to their parameter fractionally longer than they should have, into the moment they were being taken apart, so a value arriving at exactly that instant landed on a control that was already half gone. Closing a plugin window while automation plays is the everyday version of that, which is why it was worth a release of its own rather than waiting.

[The full notes for 1.6.1](https://github.com/benjamindehli/overtonium/releases/tag/v1.6.1)

## 1.6.0, 10 September 2026

**Both modulators pick a waveform.** Eight shapes on pitch and seven on amplitude, on every one of the thirty-three channels, so the fifth partial can be gated while the fourth breathes. The pitch depth reaches an octave now, which is what the shapes that step rather than sweep are worth having: a square on the pitch is a trill and sample and hold is a run of random notes. The tuning digits and the shape glyph are lit, so a strip has two displays rather than a screen and a label.

Two more factory presets, _Stepped_ and _StyloPoly_, twenty-eight in all. Loading a preset used to tell the host about a thousand parameter changes, most of which were not changes, and now tells it only what moved.

**Before upgrading.** The factory presets are renumbered, since the two new ones sort into alphabetical place rather than onto the end, so a session that stored a preset by number opens on a different one. The sound is stored with the session either way. Automation is unaffected: the 65 new parameters are inserted among the existing ones, and both formats identify a parameter by name rather than by position.

[The full notes for 1.6.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.6.0)

## 1.5.0, 5 September 2026

Seven more factory presets, twenty-six in all, six of them the same electric piano seen from different angles, because that is what a stack of sine waves is good at. The preset menu became two submenus rather than one long list, and folders in your preset folder became groups in the menu.

**Nothing in the Settings menu travels with a preset any more.** Saving one used to write your polyphony, temperament, bend range and MPE into the file, so recalling a sound reached over and changed how the instrument was set up. The factory presets already knew better, and both kinds follow the same rule now.

Four fixes, the audible one being a note held by the sustain pedal clicking when you played it again. Its envelope restarted from silence while the note was still ringing at its sustain level.

[The full notes for 1.5.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.5.0)

## 1.4.0, 30 August 2026

Three more factory presets, nineteen in all. **Legato** arrived at the head of the polyphony list, where pressing a key while another is held moves the note rather than starting it again. **One voice per key** became a switch, so a bell struck over and over can be made to build up rather than take over its own voice. Right-clicking a mute or a solo clears every one of them across the mixer, which beats hunting for a stray solo among sixty-six buttons four pixels apart.

**Before upgrading.** Both lists gained an entry at the top, so polyphony indices shifted by one and a session saved on 8 voices opens on 6. The factory presets are renumbered for the same reason.

[The full notes for 1.4.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.4.0)

## 1.3.0, 28 August 2026

**The per-channel outputs are gone again**, and the release is mostly that removal. Logic offers a fixed menu of multi-output arrangements and cannot complete the larger ones, which arrangements work varies with the version and the processor underneath it, and the one shape that would actually suit a 32-partial mixer, 32 mono outputs, was never on the menu at all. A convenience that half worked in the one host it had been tried in is not part of the instrument.

Also fixed: parameters hold the values they are given. Two conversions between a knob position and a value were done in single precision, and 66 defaults fell where that round trip loses a bit.

[The full notes for 1.3.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.3.0)

## 1.2.0, 26 August 2026

An output per channel, 33 of them alongside the stereo mix, which 1.3.0 then removed. MPE slide gained a destination, off, brightness or tuning. The two LINK tilts became one **taper from wherever you grab**, which can peak under your hand in the middle of the series rather than only at an end. The level readout counts down to -99.9 dB rather than giving up at -66, which was two thirds of the fader's usable bottom end. Every control got a name a screen reader can read, all 635 of them.

**Before upgrading.** Automation written against the old level fader plays back at different levels, because the fader's travel is spaced by decibels now rather than by gain.

[The full notes for 1.2.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.2.0)

## 1.1.0, 24 August 2026

**Folding the mixer down.** Click a section heading in the caption gutter and that group of rows folds away across every channel at once, taking the window's height with it. All five groups is 480 pixels shorter.

The factory presets appeared in Logic's own preset menu, the panel gained its grain, its grooves and its bloom, and the [update check](https://benjamindehli.github.io/overtonium/install/) arrived, off unless you ask for it. Nothing about the sound changed, and a patch saved in 1.0.0 loads identically.

[The full notes for 1.1.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.1.0)

## 1.0.0, 23 August 2026

The first release. 32 sine partials and a noise channel, each with its own fader, tuning, envelope, tremolo, pitch modulation and pan. [TUNE](https://benjamindehli.github.io/overtonium/tuning/), six keyboard temperaments, inharmonic stretch, keyboard tracking, drift and wobble. MPE alongside ordinary MIDI, a tape echo, a feedback delay network reverb, a lo-fi converter, channel ganging, and 16 factory presets.

[The full notes for 1.0.0](https://github.com/benjamindehli/overtonium/releases/tag/v1.0.0)

## Being told about a new one

The plugin can check for itself, and does not unless you ask it to. The [install page](https://benjamindehli.github.io/overtonium/install/) says what that costs, which is one request for a static file and nothing about you. Every release is also on the [releases page](https://github.com/benjamindehli/overtonium/releases), which GitHub publishes as [a feed](https://github.com/benjamindehli/overtonium/releases.atom) for anyone who would rather subscribe than be asked.
