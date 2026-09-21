"""Composes the five social cards from the layered artwork.

The cards are the `og:image` files, so nothing but a link preview ever loads
them and nothing on the site would look wrong if they went stale. They have,
twice. This is here so that regenerating them is one command rather than an
afternoon of measuring the old ones.

It needs the masters, which are not in the repository: `background.png` and
`text.png` are hand-composed artwork rather than anything a build can
produce, and `caption-*.png` are the four captions, lifted off the cards as
they stood in 1.8.0 because they were set in a face this machine has not got.
All of them live beside each other outside the tree. Point ART at them.

Needs Pillow and NumPy. Run it from anywhere:

    python3 Tools/compose_cards.py            # writes the five cards
    python3 Tools/compose_cards.py --check d  # against the set in d
"""

import os
import sys

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(REPO, "temp", "overtonium-4x")

# What every card renderer expects, and the only shape any of them is asked
# for.
W, H = 1200, 630

# The page cards lay this over the background so a caption can be read on it.
# Measured as a linear fit of a page card against the front card, over the
# three quarters of the frame that neither the wordmark nor the caption is
# on: it reproduces one from the other to within the JPEG's own noise, which
# no other model of what was done to them did.
DIM_ALPHA = 0.63
DIM_COLOUR = np.array([15.0, 15.0, 19.0])

# The wordmark canvas, seven tenths of the card on the front one and a little
# under two thirds on the rest. The glyphs stop at 93% of that canvas and the
# remainder is the glow baked into the file, which is why a mark that looks
# centred is placed by its canvas rather than by what you can see of it.
FRONT_MARK_W = 840
PAGE_MARK_W = 744

# Far enough up on a page card to leave the caption somewhere to sit.
FRONT_MARK_Y = 214
PAGE_MARK_Y = 197

# The site's --text, which is what the captions were set in.
CAPTION_COLOUR = np.array([217.0, 223.0, 231.0])

PAGES = ["controls", "tuning", "presets", "install"]


def background(master):
    """The master, full width, centre cropped to the card's shape."""
    im = Image.open(master).convert("RGB")
    h = round(im.height * W / im.width)
    top = (h - H) // 2

    return np.asarray(
        im.resize((W, h), Image.LANCZOS).crop((0, top, W, top + H)),
        dtype=np.float64)


def dim(bg):
    return (1.0 - DIM_ALPHA) * bg + DIM_ALPHA * DIM_COLOUR


def with_mark(bg, width, y):
    mark = Image.open(os.path.join(ART, "text.png")).convert("RGBA")
    height = round(mark.height * width / mark.width)

    m = np.asarray(mark.resize((width, height), Image.LANCZOS),
                   dtype=np.float64) / 255.0

    x = (W - width) // 2
    a = m[:, :, 3:4]

    out = bg.copy()
    under = out[y:y + height, x:x + width]
    out[y:y + height, x:x + width] = under * (1.0 - a) + m[:, :, :3] * 255.0 * a

    return out


def with_caption(bg, matte):
    a = matte[:, :, None]

    return bg * (1.0 - a) + CAPTION_COLOUR * a


def build(master):
    """The five cards, by the name each one is written under."""
    bg = background(master)

    cards = {"overtonium-card.jpg": with_mark(bg, FRONT_MARK_W, FRONT_MARK_Y)}

    for slug in PAGES:
        matte = np.asarray(
            Image.open(os.path.join(ART, "caption-%s.png" % slug)),
            dtype=np.float64) / 255.0

        cards["overtonium-card-%s.jpg" % slug] = with_caption(
            with_mark(dim(bg), PAGE_MARK_W, PAGE_MARK_Y), matte)

    return cards


def save(arr, path):
    """Writes a card with the tables the card it replaces was written with, so
    nothing about the encoding changes but the picture."""
    old = Image.open(path)
    old.load()

    Image.fromarray(np.clip(arr + 0.5, 0, 255).astype(np.uint8)).save(
        path, qtables=old.quantization, subsampling=0, optimize=True)


def difference(arr, path):
    b = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)

    return np.abs(arr - b).mean()


if __name__ == "__main__":
    # Every number above was recovered by composing the cards as they stood
    # from the master they were made from and comparing. All five came back
    # between 2.6 and 2.9 of 255, which is the JPEG's own noise rather than a
    # difference anybody could see. Hand a directory of those cards to
    # --check to run that again after changing any of it.
    if "--check" in sys.argv:
        was = sys.argv[sys.argv.index("--check") + 1]

        for name, arr in build(os.path.join(ART, "background.png")).items():
            print("  %-32s %.2f" % (name, difference(arr, os.path.join(was, name))))

        sys.exit(0)

    for name, arr in build(os.path.join(ART, "background-current-cropped.png")).items():
        save(arr, os.path.join(REPO, "docs", name))
        print("wrote", name)
