"""Everything the docs site says more than once, said once.

Tools/build_site.py reads this and writes the parts of docs/ that repeat from
page to page: each page's <head>, the header and its contents rail, the trail
above the title, the links at the foot of the page, the footer, the two card
lists and the questions on the front page. The prose in between stays in the
HTML and is edited there.

Any string here can name a count the source knows, and the generator fills it
in rather than trusting whoever last typed it:

    {presets}       the factory presets in words, "thirty-three"
    {parameters}    the host parameters in figures, "836"

The version is read from CMakeLists.txt. RELEASED is the day that version went
out, in UTC, and the release workflow rewrites the line on the tag. Nothing
else here changes with a release.
"""

RELEASED = "2026-10-07"

BASE = "https://benjamindehli.github.io/overtonium/"

# The 404 page is served at whatever address was asked for, so a relative link
# on it resolves against that address rather than against the site. Its links
# are absolute under this prefix instead.
ROOT_PATH = "/overtonium/"

THEME_COLOUR = "#0b0d10"

AUTHOR = {
    "@type": "Person",
    "@id": "https://musicbrainz.org/artist/56639e59-2bb5-40bd-9d5a-97d964298b6f",
    "name": "Benjamin Dehli",
}

PUBLISHER = {
    "@type": "Organization",
    "@id": "https://www.dehlimusikk.no/",
    "name": "Dehli Musikk",
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
    "description": "An additive synthesiser laid out like a 32-channel mixer. Every channel is one sine partial locked to a harmonic of the played note, with its own tuning, envelope, modulation and pan.",
    "image": BASE + "overtonium.png",
    "screenshot": BASE + "overtonium.png",
    "isAccessibleForFree": True,
    "license": "https://www.gnu.org/licenses/agpl-3.0.html",
    "sameAs": [
        "https://www.kvraudio.com/product/overtonium-by-dehli-musikk",
        "https://github.com/benjamindehli/overtonium",
        "https://www.youtube.com/watch?v=L1oYdPxGlGA",
    ],
    "codeRepository": "https://github.com/benjamindehli/overtonium",
    "downloadUrl": BASE + "install/#downloads",
    "installUrl": BASE + "install/#downloads",
    "programmingLanguage": "C++",
    # Filled in by the generator: the version from CMakeLists, the day from
    # RELEASED.
    "softwareVersion": None,
    "datePublished": None,
    "offers": {"@type": "Offer", "price": 0, "priceCurrency": "USD"},
    "author": AUTHOR,
    "publisher": PUBLISHER,
    "featureList": [
        "32 sine partials plus a noise channel",
        "Continuous blend between equal temperament and just intonation",
        "Six keyboard temperaments on any root",
        "Inharmonic stretch and keyboard tracking",
        "Per-partial envelope, tremolo, pitch modulation and pan",
        "MPE, with pitch bend and pressure per note",
        "An echo and a reverb that are each one of three machines, and a lo-fi converter",
        "Eight macros, each one host parameter that moves a whole row of channels",
        "MIDI Learn on every control",
        "An output stage with five clip shapes, including a lookahead limiter",
    ],
}

# The one recording of the instrument being played. Each chapter runs to the
# start of the next, and the last to the end of the video.
VIDEO = {
    "id": "L1oYdPxGlGA",
    "name": "Overtonium - Sound Examples",
    "description": "Eleven of the {presets} factory presets in Overtonium, played on an Expressive E Osmose with nothing on the output but the plugin's own effects.",
    "thumbnail": BASE + "overtonium-video.webp",
    "uploaded": "2026-09-26",
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
        "It builds as a VST3 on macOS, Windows and Linux, an Audio Unit on macOS and an LV2 on Linux, so it loads anywhere those formats do. "
        "There is a standalone build as well.",
    ),
    (
        "Is Overtonium free?",
        "Yes, and free software rather than only free of charge. It is released under the AGPLv3, which follows from JUCE, and the whole "
        "source is on GitHub.",
    ),
    (
        "What is additive synthesis?",
        "Building a sound by adding sine waves together rather than by filtering a rich one down. Every timbre is a stack of partials at "
        "different frequencies and levels, and additive synthesis hands you those partials directly. Overtonium gives you 32 of them, one "
        "per mixer channel.",
    ),
    (
        "Can Overtonium do microtonal tunings and just intonation?",
        "Both, and separately. TUNE sweeps each partial between its equal-tempered position and its exact whole-number ratio. The keyboard "
        "underneath is tuned on its own, to equal, just, Pythagorean, quarter-comma meantone, Werckmeister III or Young, on any root, at any "
        "reference pitch from 415 to 466 Hz.",
    ),
    (
        "Does Overtonium support MPE?",
        'Yes. With <a href="controls/#mpe">MPE</a> switched on, pitch bend and pressure arrive per note, and pressure has its own amount on '
        "every one of the 33 channels. An ordinary single-channel keyboard plays either way.",
    ),
    (
        "Can I play Overtonium from a MIDI controller?",
        'Yes. Right-click any control, choose <a href="controls/#midi-learn">MIDI Learn</a> and move a knob or fader on the controller. The '
        'map is saved with your project rather than with the preset. A <a href="controls/#macros">macro</a> moves a whole row from one '
        "parameter, so one controller or one automation lane can reach up to 32 controls at once.",
    ),
    (
        "How much CPU does Overtonium use?",
        "Eight voices of 32 partials each, with every modulator running, costs about 7% of one core, and sixteen voices about 14%. Turning "
        "the render rate down is a real saving: at 8 kHz the whole voice pool costs a fifth as much. "
        '<a href="install/#cost">The figures in full</a> are on the install page.',
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

# The pages, in the order the rail lists them. Per page:
#
#   path         where it lives under docs/, "" for the front page
#   nav          its name in the rail and in the trail above its title
#   title        the <title>, and what a shared link is headed with
#   description  what a search result shows, so at most about 155 characters
#   social       what a shared link shows, which has room for more
#   image        the card a shared link carries, 1200 by 630
#   image_alt    what that card shows
#   scripts      deferred, in this order
#   jsonld       the structured blocks, in this order: product, faq,
#                breadcrumbs, video
#   onward       the links at the foot of the page, the first one primary
#   card         its card under "Where to go next" on the front page
#   card_404     its card on the page-not-found page, which lists them all
#
# A page's contents rail is not here. It is read from the page's own headings,
# so a section that is renamed or added is in the rail by the next build.
PAGES = [
    {
        "path": "",
        "nav": "Overview",
        "title": "Overtonium, a free 32-partial additive synthesiser plugin",
        "description": "A free additive synthesiser laid out like a 32-channel mixer, one sine partial per channel, with TUNE sweeping between equal and just. VST3, AU and LV2.",
        "social": "A free additive synthesiser laid out like a 32-channel mixer. Every channel is one sine partial with its own tuning, envelope, modulation and pan, and TUNE sweeps the whole series between equal temperament and just intonation. VST3, AU and LV2 for macOS, Windows and Linux.",
        "image": "overtonium-card.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window: rows of per-partial knobs above 32 channel faders and lit meters.",
        "scripts": ["video.js", "contents.js"],
        "jsonld": ["product", "faq"],
        "onward": [],
        "card": None,
        "card_404": "What Overtonium is, what it sounds like, and where it came from.",
    },
    {
        "path": "tuning/",
        "nav": "Tuning",
        "title": "The Overtonium tuning system: TUNE, stretch and six temperaments",
        "description": "Hear TUNE blend every partial between equal temperament and just intonation, along with inharmonic stretch, six temperaments, keyboard tracking and drift.",
        "social": "Hear all of it: TUNE blending every partial between equal temperament and just intonation, STRETCH from harmonic out past a piano, a chord in equal against Werckmeister III, keyboard tracking thinning the series as you play up, and per-partial drift. With a diagram of where each partial falls against the semitone it snaps to.",
        "image": "overtonium-card-tuning.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Tuning.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "onward": [("controls/", "Controls"), ("presets/", "Presets")],
        "card": "TUNE, inharmonic stretch, six historical temperaments on any root, and how the spectrum thins as you play up the keyboard.",
        "card_404": "TUNE, inharmonic stretch, six temperaments and keyboard tracking.",
    },
    {
        "path": "controls/",
        "nav": "Controls",
        "title": "Overtonium controls: every knob on a channel and on the bar",
        "description": "A reference for every Overtonium control: the 32 channel strips, envelopes, LINK, MPE, macros, MIDI Learn, the Settings menu and the effects on the bar.",
        "social": "A reference for every Overtonium control: the twenty-three on each of the 32 channel strips, the two-part envelope, per-partial velocity and pressure, LINK for ganging the series, the lamps and meters, MPE, everything in the Settings menu, MIDI Learn, macros, how the {parameters} parameters are named for a host, and the bus drive, wobble, echo, reverb, converter and output stage on the bar.",
        "image": "overtonium-card-controls.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Controls.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "onward": [("presets/", "Presets"), ("tuning/", "Tuning")],
        "card": "Every knob on a channel and on the bar: which oscillator the partials are, envelopes, modulation, ganging, the lamps and meters, macros, MIDI Learn, and the master effects.",
        "card_404": "Every knob on a channel and on the bar.",
    },
    {
        "path": "presets/",
        "nav": "Presets",
        "title": "Overtonium presets: the {presets} that ship, and saving your own",
        "description": "All {presets} Overtonium factory presets, each playable on the page, what a preset carries, and where your own are stored on each platform.",
        "social": "The {presets} factory presets in Overtonium, each one playable on the page, what a preset carries and deliberately does not, how a MIDI program change picks one, and where your own presets are stored on macOS, Windows and Linux.",
        "image": "overtonium-card-presets.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Presets.",
        "scripts": ["video.js", "contents.js"],
        "jsonld": ["breadcrumbs", "video"],
        "onward": [("install/", "Install"), ("controls/", "Controls")],
        "card": "The {presets} that ship, what a preset carries, and where your own are kept on each platform.",
        "card_404": "The {presets} that ship, and where your own are kept.",
    },
    {
        "path": "install/",
        "nav": "Install",
        "title": "Installing Overtonium on macOS, Windows and Linux",
        "description": "Install Overtonium on macOS, Windows or Linux, get a host to see it, check what it costs to run, or build it from source.",
        "social": "How to install Overtonium: a signed and notarised universal package for macOS, an installer for Windows, and a zip for Linux, what to do when a host does not see the plugin, what it costs to run, and how to build it from source.",
        "image": "overtonium-card-install.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window, captioned Install.",
        "scripts": ["contents.js"],
        "jsonld": ["product", "breadcrumbs"],
        "onward": [("install/#downloads", "Download"), ("controls/", "Controls")],
        "card": "Installers for macOS and Windows, a zip for Linux, what to do when a host does not see it, and building from source.",
        "card_404": "Downloads for macOS, Windows and Linux, and what to do when a host cannot see it.",
    },
    {
        "path": "releases/",
        "nav": "Releases",
        "title": "Overtonium releases: what changed in each version",
        "description": "Every Overtonium release since 1.0.0, newest first: what each one added, what it fixed, and what to know before upgrading. The full notes for each are on GitHub.",
        "social": None,
        "image": "overtonium-card.jpg",
        "image_alt": "The Overtonium wordmark over the plugin window: rows of per-partial knobs above 32 channel faders and lit meters.",
        "scripts": ["contents.js"],
        "jsonld": ["breadcrumbs"],
        "onward": [("install/#downloads", "Download the current release"), ("controls/", "Controls")],
        "card": None,
        "card_404": "What changed in each version, newest first.",
        # Each heading is "1.11.0, 7 October 2026", and the rail has room for
        # the version alone.
        "rail_before_comma": True,
    },
]

NOT_FOUND = {
    "file": "404.html",
    "title": "Page not found, Overtonium",
    "description": "That address does not match anything on the Overtonium site.",
    "image": "overtonium-card.jpg",
}
