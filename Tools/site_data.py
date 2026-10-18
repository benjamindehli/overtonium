"""Everything the docs site says more than once, said once.

Tools/build_site.py reads this and writes the parts of docs/ that repeat from
page to page: each page's <head>, the header and its contents rail, the trail
above the title, the links at the foot of the page, the footer, the two card
lists and the questions on the front page. The prose in between stays in the
HTML and is edited there.

Any string here can name a count the source knows, and the generator fills it
in rather than trusting whoever last typed it:

    {presets}       the factory presets in words, "thirty-five"
    {parameters}    the host parameters in figures, "850"
    {preset_names}  every factory preset, in the order the menu lists them

The version is read from CMakeLists.txt. RELEASED is the day that version went
out, in UTC, and the release workflow rewrites the line on the tag. Nothing
else here changes with a release.
"""

RELEASED = "2026-10-16"

BASE = "https://benjamindehli.github.io/overtonium/"

# The 404 page is served at whatever address was asked for, so a relative link
# on it resolves against that address rather than against the site. Its links
# are absolute under this prefix instead.
ROOT_PATH = "/overtonium/"

THEME_COLOUR = "#0b0d10"

# The prose is British English, which is what a screen reader should speak it
# in and what a search engine should file it under.
LANGUAGE = "en-GB"

AUTHOR = {
    "@type": "Person",
    "@id": "https://musicbrainz.org/artist/56639e59-2bb5-40bd-9d5a-97d964298b6f",
    "name": "Benjamin Dehli",
    "url": "https://github.com/benjamindehli",
    "sameAs": ["https://www.wikidata.org/wiki/Q141757746"],
}

PUBLISHER = {
    "@type": "Organization",
    "@id": "https://www.dehlimusikk.no/",
    "name": "Dehli Musikk",
    "url": "https://www.dehlimusikk.no/",
    "sameAs": ["https://www.wikidata.org/wiki/Q141757809"],
}

# One description of the instrument, for every page that carries one. The
# structured-data job insists that blocks sharing an @id agree, and two copies
# typed by hand did not.
PRODUCT = {
    "@context": "https://schema.org",
    "@type": "SoftwareApplication",
    "@id": "https://www.dehlimusikk.no/products/overtonium/#product",
    "name": "Overtonium",
    "url": BASE,
    "applicationCategory": "MultimediaApplication",
    "applicationSubCategory": "Audio plugin",
    "operatingSystem": "macOS, Windows, Linux",
    "description": "An additive synthesizer laid out like a 32-channel mixer. Every channel is one sine partial locked to a harmonic of the played note, with its own tuning, envelope, modulation and pan.",
    "image": BASE + "overtonium.png",
    "screenshot": BASE + "overtonium.png",
    "isAccessibleForFree": True,
    "license": "https://www.gnu.org/licenses/agpl-3.0.html",
    "sameAs": [
        "https://www.dehlimusikk.no/products/overtonium/",
        "https://www.dehlimusikk.no/en/products/overtonium/",
        "https://www.kvraudio.com/product/overtonium-by-dehli-musikk",
        "https://github.com/benjamindehli/overtonium",
        "https://www.youtube.com/watch?v=L1oYdPxGlGA",
        "https://www.wikidata.org/wiki/Q141757590",
    ],
    "codeRepository": "https://github.com/benjamindehli/overtonium",
    "downloadUrl": BASE + "install/#downloads",
    "installUrl": BASE + "install/#downloads",
    "programmingLanguage": "C++",
    "releaseNotes": BASE + "releases/",
    "softwareHelp": {"@type": "CreativeWork", "url": BASE + "controls/"},
    # Filled in by the generator: the version from CMakeLists, the day from
    # RELEASED.
    "softwareVersion": None,
    "datePublished": None,
    "offers": {"@type": "Offer", "price": 0, "priceCurrency": "USD"},
    "author": AUTHOR,
    "publisher": PUBLISHER,
    "featureList": [
        "32 sine partials plus a noise channel",
        "A Karplus-Strong string in the noise channel, tuned to the note, that the noise plucks or blows into",
        "Continuous blend between equal temperament and just intonation",
        "Nine keyboard temperaments on any root, one of them adaptive",
        "Inharmonic stretch and keyboard tracking",
        "Per-partial envelope, tremolo, pitch modulation and pan",
        "MPE, with pitch bend and pressure per note",
        "A resonator of 88 sympathetic strings that resonate with what you play and respond to the sustain pedal",
        "An echo and a reverb with three machines each, and a lo-fi converter",
        "Eight macros, each one host parameter that moves a whole row of channels",
        "MIDI Learn on every control",
        "An output stage with five clip shapes, including a lookahead limiter",
        "Glide on every channel, with each partial reaching a new note over its own time",
    ],
}

# The one recording of the instrument being played. Each chapter runs to the
# start of the next, and the last to the end of the video.
VIDEO = {
    "id": "L1oYdPxGlGA",
    "name": "Overtonium - Sound Examples",
    "description": "Eleven of the {presets} factory presets in Overtonium, played on an Expressive E Osmose, with no effects on the output except the plugin's own.",
    "thumbnail": BASE + "overtonium-video.webp",
    # Google wants a time and a time zone with the date. The day is the day the
    # video went up. The time is noon in Norway, since the minute is not known.
    "uploaded": "2026-09-26T12:00:00+02:00",
    "seconds": 674,
    "chapters": [
        ("Synth Ensemble", 0),
        ("Metallic Piano", 124),
        ("Dream Phase", 175),
        ("FM Piano", 244),
        ("Sparkle Pad", 347),
        ("Dire Dire EP", 416),
        ("Music Box", 426),
        ("Stepped", 445),
        ("Wurli", 512),
        ("Lo-fi", 525),
        ("Nylon EP", 603),
    ],
}

# Shown on the front page and read by search engines as an FAQPage. The answer
# is HTML, with links relative to the front page, and the structured copy is
# the same words with the markup taken out, so the two cannot say different
# things.
QUESTIONS = [
    (
        "Which hosts does Overtonium run in?",
        "It comes as a VST3 on macOS, Windows and Linux, an Audio Unit on macOS and an LV2 on Linux, so it loads in any host that supports "
        "those formats. There is also a standalone build.",
    ),
    (
        "Is Overtonium free?",
        "Yes. It is also free software, not only free of charge. It is released under the AGPLv3 because it uses JUCE, and all of the "
        "source is on GitHub.",
    ),
    (
        "What is additive synthesis?",
        "It builds a sound by adding sine waves together, instead of filtering a rich waveform. Every timbre is a stack of partials at "
        "different frequencies and levels, and additive synthesis lets you control those partials directly. Overtonium gives you 32 of "
        "them, one per mixer channel.",
    ),
    (
        "Can Overtonium do microtonal tunings and just intonation?",
        "Yes, both, and they are set separately. TUNE moves each partial between its equal-tempered position and its exact whole-number "
        "ratio. The keyboard has its own tuning: equal, just, Pythagorean, quarter-comma meantone, Werckmeister III, Vallotti, Kirnberger III or Young, on any root, at "
        'any of eleven reference pitches from 415 to 466 Hz. It can also use <a href="tuning/#adaptive">Adaptive</a>, which keeps the chord you hold in '
        "just intonation as you play, and moves a held note when the next chord needs it.",
    ),
    (
        "Does Overtonium support MPE?",
        'Yes. With <a href="playing/#mpe">MPE</a> switched on, pitch bend and pressure work per note, and each of the 33 channels has its '
        "own pressure amount. An ordinary single-channel keyboard works either way.",
    ),
    (
        "Can I play Overtonium from a MIDI controller?",
        'Yes. Right-click any control, choose <a href="playing/#midi-learn">MIDI Learn</a> and move a knob or fader on the controller. The '
        'mapping is saved with your project, not with the preset. A <a href="playing/#macros">macro</a> is one parameter that moves a whole '
        "row, so one controller or one automation lane can move up to 32 controls at once.",
    ),
    (
        "Does Overtonium have portamento or glide?",
        "Yes, on each channel separately. Each partial slides to a new note over its own GLIDE time, so the partials can arrive one at a "
        "time. Two switches apply to every channel. One sets whether it glides always or only when you play legato, and the other whether it "
        'moves at a fixed rate or takes a fixed time. <a href="playing/#glide">Glide</a> on the Playing page has the details.',
    ),
    (
        "How much CPU does Overtonium use?",
        "Eight voices of 32 partials each, with every modulator running, use about 7% of one core, and sixteen voices about 14%. The "
        "effects add little, except the resonator with all its strings free, which costs about half as much as eight voices. Lowering the "
        "render rate saves a lot. "
        "At 8 kHz the whole voice pool costs a fifth as much. "
        '<a href="install/#cost">The full figures</a> are on the install page.',
    ),
]

FOOTER_LINKS = [
    ("https://github.com/benjamindehli/overtonium", "Source"),
    ("https://github.com/benjamindehli/overtonium/blob/main/DESIGN.md", "Design notes"),
    ("https://github.com/benjamindehli/overtonium/blob/main/ARCHITECTURE.md", "Architecture"),
    ("https://github.com/benjamindehli/overtonium/blob/main/ACCESSIBILITY.md", "Accessibility"),
    ("https://github.com/benjamindehli/overtonium/issues", "Issue tracker"),
    ("https://www.kvraudio.com/product/overtonium-by-dehli-musikk", "KVR listing"),
]

# The pages, in the order the rail lists them, which is also the order the
# previous and next links at the foot of every inner page follow, so the site
# reads front to back. Per page:
#
#   path         where it lives under docs/, "" for the front page
#   nav          its name in the rail and in the trail above its title
#   title        the <title>, and what a shared link is headed with
#   description  what a search result shows, so at most about 155 characters
#   social       what a shared link shows, which has room for more
#   image        the card a shared link carries, 1200 by 630
#   image_alt    what that card shows
#   scripts      deferred, in this order
#   jsonld       the structured blocks besides the page's own, in this order:
#                website, product, faq, breadcrumbs, video. Every page also
#                gets a WebPage block tying it to the site and the product.
#   card         its card under "Where to go next" on the front page
#   card_404     its card on the page-not-found page, which lists them all
#   llms         its line in llms.txt, which may carry Markdown links
#   priority     its priority in the sitemap
#
# A page's contents rail is not here. It is read from the page's own headings,
# so a section that is renamed or added is in the rail by the next build.
PAGES = [
    {
        "path": "",
        "nav": "Overview",
        "title": "Overtonium, a free 32-partial additive synthesizer plugin",
        "description": "A free additive synthesizer laid out like a 32-channel mixer, one sine partial per channel, with TUNE sweeping between equal and just. VST3, AU and LV2.",
        "social": "A free additive synthesizer laid out like a 32-channel mixer. Every channel is one sine partial with its own tuning, envelope, modulation and pan, and TUNE sweeps the whole series between equal temperament and just intonation. VST3, AU and LV2 for macOS, Windows and Linux.",
        "image": "overtonium-card.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window: rows of per-partial knobs above 32 channel faders and lit meters.",
        "scripts": ["video.js", "contents.js"],
        "jsonld": ["website", "product", "faq", "video"],
        "card": None,
        "card_404": "What Overtonium is, what it sounds like, and where it came from.",
        "llms": 'what the instrument is, what TUNE does, a video of [eleven of the {presets} factory presets played on a keyboard](https://www.youtube.com/watch?v=L1oYdPxGlGA), common questions, and the three sample instruments that came before it.',
        "priority": "1.0",
    },
    {
        "path": "tuning/",
        "nav": "Tuning",
        "title": "The Overtonium tuning system: TUNE, stretch and nine temperaments",
        "description": "Hear TUNE blend every partial between equal temperament and just intonation, plus inharmonic stretch, nine temperaments, keyboard tracking and drift.",
        "social": "Audio of TUNE blending every partial between equal temperament and just intonation, STRETCH from harmonic to beyond a piano, a chord in equal temperament against Werckmeister III, keyboard tracking thinning the partials as you play higher, and per-partial drift. There is also a diagram of where each partial falls against the semitone it snaps to.",
        "image": "overtonium-card-tuning.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Tuning.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "card": "TUNE, inharmonic stretch, eight historical temperaments and an adaptive one on any root, and how the spectrum thins out higher up the keyboard.",
        "card_404": "TUNE, inharmonic stretch, nine temperaments and keyboard tracking.",
        "llms": 'TUNE from equal to just, inharmonic stretch, eight historical temperaments and an adaptive one that retunes each key as it is played, on any root, at a reference pitch from a list running 415 to 466 Hz, keyboard tracking, and per-partial drift. Includes audio of each and a diagram of where every partial falls against the semitone it snaps to.',
        "priority": "0.8",
    },
    {
        "path": "controls/",
        "nav": "Controls",
        "title": "Overtonium controls: every knob on a channel strip",
        "description": "Every control on an Overtonium channel strip: tuning, glide, envelope, modulation, velocity, pressure, LINK, the lamps, and the noise channel's string.",
        "social": "A reference for every control on an Overtonium channel strip: the twenty-three on each of the 32 channels, the two-part envelope, per-partial velocity and pressure, LINK for ganging the series, the lamps and meters, the gestures the panel responds to, and the noise channel.",
        "image": "overtonium-card-controls.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Controls.",
        "scripts": ["contents.js", "moved.js"],
        "jsonld": ["breadcrumbs"],
        "card": "Every knob on a channel strip, then the effects on the bar and how to play it from a keyboard, a host or a controller.",
        "card_404": "Every knob on a channel strip.",
        "llms": "every knob on a channel strip, the two-part envelope, per-partial velocity and pressure, LINK for ganging the 32 channels, the lamps and meters, the gestures the panel responds to, and the noise channel.",
        "priority": "0.8",
    },
    {
        "path": "effects/",
        "nav": "Effects",
        "title": "Overtonium effects: character, echo, reverb, converter and CLIP",
        "description": "Overtonium's top bar: six oscillator characters, wobble, a resonator, bus drive, three echoes, three reverbs, a lo-fi converter and five clip shapes.",
        "social": "The bar across the top of Overtonium: six oscillator characters that set which circuit every partial is modelled on, a wobble across the whole series, a resonator of 88 sympathetic strings that resonate with what you play, a bus drive that responds to how hard you play, an echo and a reverb with three machines each, a lo-fi converter, and an output stage with five clip shapes including a lookahead limiter.",
        "image": "overtonium-card-effects.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Effects.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "card": None,
        "card_404": "The bar: character, wobble, resonator, echo, reverb, the converter and CLIP.",
        "llms": "the bar across the top: the six oscillator characters, wobble, the resonator of sympathetic strings with its Piano, Open, Octave and Series modes, bus drive, the echo and the reverb with their three machines each, the converter, the five CLIP shapes on the output and the lookahead limiter, the output meter, and a diagram of the order the signal passes through.",
        "priority": "0.7",
    },
    {
        "path": "playing/",
        "nav": "Playing",
        "title": "Playing Overtonium: MPE, automation, macros and MIDI Learn",
        "description": "Playing Overtonium: MPE, legato, glide on every channel, the Settings menu, automation with all {parameters} parameters, macros and MIDI Learn.",
        "social": "MPE with pitch bend and pressure per note, legato, a glide on every channel with each partial on its own time, every entry in the Settings menu, automating Overtonium from a host with all {parameters} parameters listed by name and id, eight macros that each move a whole row of channels, and MIDI Learn on every control.",
        "image": "overtonium-card-playing.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Playing.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "card": None,
        "card_404": "MPE, legato and glide, the Settings menu, automation, macros and MIDI Learn.",
        "llms": "MPE, legato with one voice or as a top line over a polyphonic accompaniment, glide with a time on every channel and the two switches that set when it glides and what the time means, every entry in the Settings menu with its default, how the {parameters} host parameters are named and why automation keeps working when a release adds more, every one of them listed with its id, range and default, macros, and MIDI Learn.",
        "priority": "0.7",
    },
    {
        "path": "presets/",
        "nav": "Presets",
        "title": "Overtonium presets: the {presets} that ship, and saving your own",
        "description": "All {presets} Overtonium factory presets, each playable on the page, what a preset carries, and where your own are stored on each platform.",
        "social": "The {presets} factory presets in Overtonium, each one playable on the page, what a preset stores and what it leaves out on purpose, how a MIDI program change selects one, and where your own presets are stored on macOS, Windows and Linux.",
        "image": "overtonium-card-presets.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Presets.",
        "scripts": ["video.js", "contents.js"],
        "jsonld": ["breadcrumbs", "video"],
        "card": "The {presets} that ship, what a preset carries, and where your own are kept on each platform.",
        "card_404": "The {presets} that ship, and where your own are kept.",
        "llms": 'the {presets} that ship, each one playable on the page, [eleven of them played on a keyboard](https://www.youtube.com/watch?v=L1oYdPxGlGA) with a chapter link to each, what a preset stores and what it leaves out on purpose, how a MIDI program change selects one, where user presets are kept on each platform, and how folders in that directory become groups in the menu.',
        "priority": "0.6",
    },
    {
        "path": "install/",
        "nav": "Install",
        "title": "Installing Overtonium on macOS, Windows and Linux",
        "description": "Install Overtonium on macOS, Windows or Linux, get a host to see it, check its CPU use, or build it from source.",
        "social": "How to install Overtonium: a signed and notarised universal package for macOS, an installer for Windows, and a zip for Linux, what to do when a host does not see the plugin, how much CPU it uses, and how to build it from source.",
        "image": "overtonium-card-install.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Install.",
        "scripts": ["contents.js"],
        "jsonld": ["product", "breadcrumbs"],
        "card": "Installers for macOS and Windows, a zip for Linux, what to do when a host does not see it, and building from source.",
        "card_404": "Downloads for macOS, Windows and Linux, and what to do when a host cannot see it.",
        "llms": 'which download to take on each platform, what to do when a host does not see the plugin, what it costs in CPU at one, eight and sixteen voices, the opt-in update check, and building from source.',
        "priority": "0.9",
    },
    {
        "path": "releases/",
        "nav": "Releases",
        "title": "Overtonium releases: what changed in each version",
        "description": "Every Overtonium release since 1.0.0, newest first: what each one added, what it fixed, and what to know before upgrading.",
        "social": None,
        "image": "overtonium-card.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window: rows of per-partial knobs above 32 channel faders and lit meters.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "card": None,
        "card_404": "What changed in each version, newest first.",
        "llms": 'what changed in each version since 1.0.0, newest first, with what to know before upgrading and a link to the full notes for each.',
        "priority": "0.7",
        # Each heading is "1.11.0, 7 October 2026", and the rail has room for
        # the version alone.
        "rail_before_comma": True,
    },
]

# llms.txt, in the shape llmstxt.org describes: a summary, then the pages,
# then the rest. The page entries are each page's "llms" field above.

LLMS_SUMMARY = 'A free, open source additive synthesizer plugin laid out like a 32-channel mixer. Every channel is one sine oscillator locked to a harmonic of the played note, and every channel has its own tuning, envelope, modulation and place in the stereo field. VST3, Audio Unit, LV2 and standalone, for macOS, Windows and Linux, under the AGPLv3.'

LLMS_INTRO = [
    'The first control to try is TUNE. It moves each partial continuously between equal temperament and just intonation. At the just end a partial sits at an exact whole-number ratio with the fundamental, and the partials blend into one timbre. At the equal end each partial snaps to the nearest semitone, and the same partials sound more like a chord. The factory presets Just Saw and Equal Saw differ only in this control and sound completely different.',
    'Additive synthesizers usually hide their partials behind a spectrum drawing or a few macro controls. Overtonium shows all of them as faders and gives each one the controls of a channel on a mixing desk, so you shape a sound by mixing it.',
    'Written in C++ with JUCE 9 by Benjamin Dehli for Dehli Musikk. Hosts list it under DehliMusikk.',
]

LLMS_SOURCE = [
    ('Repository', 'https://github.com/benjamindehli/overtonium', 'the whole source, AGPLv3.'),
    ('Releases', 'https://github.com/benjamindehli/overtonium/releases', 'installers for macOS and Windows, and a zip for each platform holding the same builds loose.'),
    ('README', 'https://github.com/benjamindehli/overtonium/blob/main/README.md', 'what the instrument is, how to build it, the update check and the licensing.'),
    ('Design notes', 'https://github.com/benjamindehli/overtonium/blob/main/DESIGN.md', 'every control and how the synthesis works, with the reasoning and the measurements behind each decision. Longer than the project page.'),
    ('Architecture', 'https://github.com/benjamindehli/overtonium/blob/main/ARCHITECTURE.md', 'how the code is arranged, and which decisions the rest depends on.'),
    ('Contributing', 'https://github.com/benjamindehli/overtonium/blob/main/CONTRIBUTING.md', 'building, the two test suites, style, and how releases are made.'),
    ('Accessibility', 'https://github.com/benjamindehli/overtonium/blob/main/ACCESSIBILITY.md', 'what has been checked, not only intended, with the measurements, the known barriers, and how to report one. The plugin window is not keyboard operable and nobody who relies on assistive technology has evaluated it.'),
]

LLMS_OPTIONAL = [
    ("The whole site", BASE + "llms-full.txt", "every page above as Markdown in one file, for reading all of it in one request."),
    ("Every parameter", BASE + "parameters.json", "all {parameters} host parameters as JSON, one per line: the id automation is stored against, the name a host shows, the range and the default. The plugin writes this file itself."),
    ('Update feed', 'https://benjamindehli.github.io/overtonium/latest.json', "the version the plugin's opt-in check reads."),
    ('Security policy', 'https://github.com/benjamindehli/overtonium/blob/main/SECURITY.md', 'what the update check sends, and how to report a vulnerability.'),
    ('KVR Audio listing', 'https://www.kvraudio.com/product/overtonium-by-dehli-musikk', "the instrument's page in the KVR plugin database, where people can rate it and compare it with other plugins."),
    ('Dehli Musikk', 'https://www.dehlimusikk.no/', 'the other instruments, most of them sample libraries rather than plugins.'),
]

LLMS_FACTS = [
    ('Formats', 'VST3, Audio Unit on macOS, LV2 on Linux, and a standalone application.'),
    ('Platforms', 'macOS as a universal binary for Apple Silicon and Intel, Windows, Linux.'),
    ('Licence', 'GNU Affero General Public License v3, which follows from JUCE.'),
    ('Price', 'free, and free software rather than only free of charge.'),
    ('Partials', "32 sine oscillators plus a noise channel, each with its own tuning, envelope, key-off envelope, tremolo, pitch modulation, drift, glide, velocity and pressure amounts, pan, mute, solo and fader. The noise channel has no pitch, so it has no pitch modulation, drift or glide. In their place it has a Karplus-Strong string tuned to the note, which the noise plucks or blows into, with STRING, DECAY, DAMP and POINT knobs. A glide time is how long that partial takes to reach a new note, so the partials can arrive one at a time. Two switches apply to every channel: whether it glides always or only legato, and whether it moves at a fixed rate or takes a fixed time. Both modulators have a waveform per channel: sine, triangle, sawtooth, reverse sawtooth, square, sample and hold, or a smooth random glide. Pitch also has a unipolar square that only bends upward. Each modulator can be switched from one per note to one shared by the whole keyboard, so a chord moves together instead of each key moving from its own starting point. There are two switches, one per modulator, called Shared by all notes in the shape button's menu and shown beside each SHAPE caption. They are off by default and saved with the patch. Wurli shares its tremolo and StyloPoly its vibrato. On a keyboard that senses how fast a key is released, that speed scales the level each partial's tail starts from, from double at the fastest release to half at the slowest. There is no knob or parameter for it. A keyboard that cannot sense release speed changes nothing. It can report that in two ways, a note-off carrying zero or a note-on with velocity zero, and both are read as no information."),
    ('Tuning', 'a continuous blend from equal temperament to just intonation, inharmonic stretch, keyboard tracking, and nine keyboard temperaments: equal, just, Pythagorean, quarter-comma meantone, Werckmeister III, Vallotti, Kirnberger III and Young, and Adaptive. Adaptive gives every pitch class five versions a comma apart and tunes the held chord with whichever versions make it purest. It does this again each time a key goes down or comes up, and slides a held note to its new version over a Retune time from instant to one second. Every chord is pure in every key, including a chord that changes into the next one note at a time. The same keys are tuned the same however they were played, and the keyboard never drifts more than two commas. When a chord cannot be pure throughout, Adaptive keeps the compromise as small as it can. A minor seventh is the harmonic one, 7:4 above its root, unless that would put another held note out of tune with it, so a dominant seventh is 4:5:6:7.'),
    ('Oscillator character', 'one setting for all 32 partials that chooses which circuit each of them is modelled on. Each is named after the part that gives it its sound. Pure is an exact sine. Bulb is a Wien bridge whose lamp responds slowly, so the level lags behind every pitch change and recovers over about half a second. Rail is a phase-shift oscillator driven into its own supply rail, with a third harmonic at -23 dB and no even harmonics. Diode is a triangle shaped by two mismatched diodes, with a second, third and fourth harmonic near -32 dB. Valve is a triode biased so one half of the wave bends over first, with a second harmonic at -20 dB over a third at -23. It is the only one whose loudest added harmonic is an octave rather than a twelfth. Op-amp is an amplifier that is too slow to keep up. It does nothing below a kilohertz and turns partials above it into triangles, so the top of the series gets harder as it goes up. The characters are built as band-limited tables rather than as waveshaping, so they cost nothing per sample and nothing aliases. Every character except Pure is a rack of 32 units rather than one oscillator. A fixed spread, the same in every session and on every machine, puts each partial a couple of cents and a fraction of a dB off spec, from 1.5 ct and 0.1 dB for Bulb to 3 ct and 0.35 dB for Valve. Partial 1 stays exact, as the one the rest were tuned against. This is separate from DRIFT, which wanders. Each unit is also built to one of three drives, 15% either side of nominal, so no two channels distort by the same amount. There are three rather than one per channel because the drive is built into a band-limited table. The character also sets how the bus behaves where the 33 channels are summed. This is separate from the tables and works on the mix rather than on any one partial. It is an amplitude-reactive drive that is barely there when you play quietly and comes in when you play hard. It is strongest on Bulb and Rail, gentlest on Diode, and absent on Pure. There is no knob for it, because how hard a summing amplifier is driven is part of the character you choose.'),
    ('Effects', "a resonator of 88 sympathetic Karplus-Strong strings between the summed channels and the bus drive, an echo with three machines, a reverb with three machines, a wobble across the whole series, and a converter that reduces render rate and bit depth. Most of the factory presets use the echo, the reverb or both, mostly the tape and the room. 60s Organ uses the bucket brigade and the spring, Shimmer and Tape Choir the plate, Glide the digital echo, and Space Cowboy the digital echo and the plate. The echo is a tape loop with two motors, a bucket brigade, or a digital delay that sends every repeat to the other side. AGE means something different on each: tape wear, a slowing clock and breathing companding, or falling bits and sample rate. The reverb is a feedback delay network sized from its decay, a modulated plate that is dense from the very start, or a tray of springs whose dispersion turns every hit into a chirp that starts high and falls. Decay, damping and pre-delay mean the same on all three, and the wet levels are matched, so switching machines changes the character of a tail but not its length or loudness. The resonator's strings are tuned one to each piano key, A0 to C8. Piano lets the held keys' strings ring, and all of them while the sustain pedal is down. Open lets all of them ring, as if the pedal were always down. Octave rings the twelve from C3, and Series rings the tuning root's first eight harmonics, tuned pure. The strings never bend. Metallic Piano and Space Cowboy use the resonator, and the other factory presets have it off."),
    ('MPE', 'pitch bend and pressure per note, with the slide axis routable to brightness or tuning.'),
    ('Macros', 'up to eight, each one host parameter that moves a whole row, such as every tuning knob or every decay, across all channels, the odd or even ones, or one interval, either uniformly or tapering away from a chosen channel. A macro adds an offset to the patch values instead of replacing them, so the knobs stay where they are and a ring around each one shows where the macro has moved it. Macros let you automate a relationship across 32 channels with one lane.'),
    ('MIDI Learn', 'right-click any control, choose MIDI Learn and move a controller. It works with MPE on, the map is saved with the project rather than the preset, and eleven controllers the instrument already uses, such as the mod wheel, sustain and the MPE slide, cannot be learned.'),
    ('Output stage', 'the master fader drives a CLIP stage of five shapes, Soft, Hard, Asymmetric, Limiter and Fold, or Off. With the Limiter lookahead switched on in Settings, the Limiter looks 2 ms ahead and the plugin reports 108 samples of latency at 48 kHz whichever shape is chosen, so changing preset never changes the latency. Lookahead is off by default. The plugin then reports only the 12 samples of the bus stage, and the Limiter is rougher.'),
    ('Automation', '{parameters} parameters reach the host, 44 global, 23 on each of the 32 partials, 22 on the noise channel and 6 on each of the eight macros. A per-channel parameter has its channel in its name, such as "H7 Tune" or "Noise Level". Each parameter has its own id rather than being identified by its position, so when a release adds parameters, existing automation still controls the same things.'),
    ('Program change', 'a MIDI program change loads a factory preset by its position in the alphabetical Factory list, counting from zero, on any channel and with MPE on. Numbers past the last preset are ignored, and your own presets cannot be loaded this way.'),
    ('Factory presets', "{preset_names}. Twenty-eight of them set an oscillator character, chosen by ear: eleven on Bulb, nine on Op-amp, five on Valve, two on Rail, one on Diode, and seven on Pure, four of those on purpose (Init, Just Saw, Equal Saw, 6581 Triangle, whose grit comes from the chip's arithmetic rather than anything analogue)."),
    ('Audio examples', 'every section of the tuning page has audio, and so do glide, legato and every factory preset: Just Saw against Equal Saw and the TUNE sweep between them, a STRETCH sweep from harmonic out past a piano, one chord in equal against Werckmeister III, a run up the keyboard with tracking off and on, a held chord with and without drift, a phrase with glide off and on, a melody over chords with Legato off and on, and all {presets} presets playing the same spread C major. They are rendered by the plugin rather than recorded, so they always match what it does.'),
    ('Privacy', 'nothing reaches the network unless the update check is switched on, which is off by default and offered once.'),
]

# Sections that have moved to another page. A link to one of them at its old
# address, from a search result, a forum or an old release note, is sent on to
# where it lives now by moved.js, which is written from this and from the ids
# on each new page. Without the script it lands at the top of the old page.
MOVED = {"controls/": ["effects/", "playing/"]}

NOT_FOUND = {
    "file": "404.html",
    "title": "Page not found, Overtonium",
    "description": "That address does not match anything on the Overtonium site.",
    "image": "overtonium-card.jpg",
}
