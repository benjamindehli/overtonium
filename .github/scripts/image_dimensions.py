"""Checks that what the pages say an image is matches what it is.

Every `<img>` carries a width and a height so the browser can reserve the
space before the bytes arrive. Get them wrong and the reservation is wrong,
which is the layout shift the attributes exist to prevent, and nothing says
so: the page looks right to whoever is testing it on a fast connection.

They went wrong exactly that way. The scrollbar that arrived with the
scrolling parameters made the window ten pixels wider, the renderer went on
asking for the old size because it was a literal, and the pages went on
declaring a width the picture had not had for two releases.

Reads the formats itself rather than reaching for Pillow, so this costs a
checkout and a second like the checks beside it.

    python3 .github/scripts/image_dimensions.py --self-test
    python3 .github/scripts/image_dimensions.py docs
"""

import os
import re
import struct
import sys


def png_size(data):
    # Signature, then the length and type of the first chunk, which the format
    # requires to be IHDR.
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None

    return struct.unpack(">II", data[16:24])


def jpeg_size(data):
    if data[:2] != b"\xff\xd8":
        return None

    at = 2

    while at + 9 < len(data):
        if data[at] != 0xFF:
            at += 1
            continue

        marker = data[at + 1]

        # The frame headers, which are the ones carrying the dimensions. The
        # gaps are deliberate: C4, C8 and CC are tables and definitions that
        # happen to sit in the same range.
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA,
                      0xCB, 0xCD, 0xCE, 0xCF):
            height, width = struct.unpack(">HH", data[at + 5:at + 9])
            return width, height

        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            at += 2
            continue

        length = struct.unpack(">H", data[at + 2:at + 4])[0]
        at += 2 + length

    return None


def webp_size(data):
    if data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        return None

    kind = data[12:16]

    if kind == b"VP8X":
        # Canvas size, one less than the real one, in three bytes each.
        width = struct.unpack("<I", data[24:27] + b"\x00")[0] + 1
        height = struct.unpack("<I", data[27:30] + b"\x00")[0] + 1
        return width, height

    if kind == b"VP8 ":
        # The keyframe header starts with a three byte tag and a three byte
        # start code, then fourteen bits of each dimension.
        if data[23:26] != b"\x9d\x01\x2a":
            return None

        width, height = struct.unpack("<HH", data[26:30])
        return width & 0x3FFF, height & 0x3FFF

    if kind == b"VP8L":
        if data[20] != 0x2F:
            return None

        bits = struct.unpack("<I", data[21:25])[0]
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1

    return None


def size_of(path):
    with open(path, "rb") as f:
        data = f.read(64 * 1024)

    for reader in (png_size, jpeg_size, webp_size):
        found = reader(data)

        if found is not None:
            return found

    return None


def candidates(tag, page, root):
    """Every file the element could load, from src and from srcset."""
    found = []

    for attribute in ("src", "srcset"):
        m = re.search(r'%s="([^"]*)"' % attribute, tag)

        if m is None:
            continue

        for part in m.group(1).split(","):
            url = part.strip().split(" ")[0]

            if not url or url.startswith("data:") or url.startswith("http"):
                continue

            # A page served from any depth writes its links from the site
            # root, which is the repository's docs directory.
            if url.startswith("/"):
                target = os.path.join(root, url.split("/", 2)[-1])
            else:
                target = os.path.normpath(os.path.join(os.path.dirname(page),
                                                       url))

            found.append((url, target))

    return found


def check(root):
    """@return a list of complaints, empty when everything agrees."""
    wrong = []
    checked = 0

    pages = []
    for where, _, names in os.walk(root):
        for name in sorted(names):
            if name.endswith(".html"):
                pages.append(os.path.join(where, name))

    for page in sorted(pages):
        with open(page, encoding="utf-8") as f:
            text = f.read()

        shown = os.path.relpath(page, root)

        for tag in re.findall(r"<img\b[^>]*>", text, re.S):
            here = candidates(tag, page, root)

            for url, target in here:
                if not os.path.exists(target):
                    wrong.append("%s: %s is not there" % (shown, url))

            w = re.search(r'width="(\d+)"', tag)
            h = re.search(r'height="(\d+)"', tag)

            if not (w and h):
                continue

            said = (int(w.group(1)), int(h.group(1)))

            sizes = []
            for url, target in here:
                if not os.path.exists(target) or target.endswith(".svg"):
                    continue

                real = size_of(target)

                if real is None:
                    wrong.append("%s: cannot read the size of %s"
                                 % (shown, url))
                    continue

                sizes.append((url, real))

            if not sizes:
                continue

            checked += 1

            # The attributes have to be one of the pictures the element can
            # actually load. A srcset may offer several, and declaring the
            # largest is as honest as declaring the one in src.
            if said not in [real for _, real in sizes]:
                wrong.append(
                    "%s: says %dx%d, and the files are %s"
                    % (shown, said[0], said[1],
                       ", ".join("%s %dx%d" % (u, r[0], r[1])
                                 for u, r in sizes)))
                continue

            # And every other one has to be the same shape, or the space
            # reserved is right for one of them and wrong for the rest.
            #
            # Within a percent rather than exactly. Halving 383 gives 191.5
            # and a file cannot be half a pixel tall, so a legitimately
            # resampled srcset entry is a fraction off its original and
            # always will be. A percent is far tighter than any real mistake:
            # a picture of the wrong shape is out by tens of them.
            shape = said[0] / said[1]

            for url, real in sizes:
                if abs(real[0] / real[1] - shape) / shape > 0.01:
                    wrong.append("%s: %s is %dx%d, a different shape from the "
                                 "%dx%d declared"
                                 % (shown, url, real[0], real[1], said[0],
                                    said[1]))

        # The social cards are never loaded by a browser, so nothing on the
        # site looks wrong when one is the wrong shape. The link preview does.
        card = re.search(r'property="og:image"[^>]*content="([^"]+)"', text)

        if card is not None:
            name = card.group(1).rsplit("/", 1)[-1]
            target = os.path.join(root, name)

            if not os.path.exists(target):
                wrong.append("%s: the card %s is not there" % (shown, name))
            else:
                real = size_of(target)
                checked += 1

                if real != (1200, 630):
                    wrong.append("%s: the card %s is %dx%d, and every renderer "
                                 "expects 1200x630"
                                 % (shown, name, real[0], real[1]))

    return wrong, checked


def self_test():
    """Checks the checker, which has to be able to fail before it can pass.

    A checker that matches nothing reports nothing wrong, which reads exactly
    like a clean tree. lambda_captures.py was quietly passing everything twice
    for that reason, so the cases below are mostly things that must fail.
    """
    import shutil
    import tempfile

    wrong = 0

    def expect(what, got, want):
        nonlocal wrong
        if got != want:
            print("  self test: %s gave %r, wanted %r" % (what, got, want))
            wrong += 1

    # ---- the format readers, against headers built to known sizes ----------
    png = (b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR"
           + struct.pack(">II", 1350, 1010))
    expect("png", png_size(png), (1350, 1010))

    jpeg = (b"\xff\xd8\xff\xe0" + struct.pack(">H", 12) + b"JFIF\x00" * 2
            + b"\xff\xc0" + struct.pack(">H", 17) + b"\x08"
            + struct.pack(">HH", 630, 1200) + b"\x03" + b"\x00" * 9)
    expect("jpeg", jpeg_size(jpeg), (1200, 630))

    lossless = (b"RIFF" + struct.pack("<I", 0) + b"WEBP" + b"VP8L"
                + struct.pack("<I", 0) + b"\x2f"
                + struct.pack("<I", (2360 - 1) | ((1445 - 1) << 14)))
    expect("webp lossless", webp_size(lossless), (2360, 1445))

    lossy = (b"RIFF" + struct.pack("<I", 0) + b"WEBP" + b"VP8 "
             + struct.pack("<I", 0) + b"\x00" * 3 + b"\x9d\x01\x2a"
             + struct.pack("<HH", 674, 504))
    expect("webp lossy", webp_size(lossy), (674, 504))

    extended = (b"RIFF" + struct.pack("<I", 0) + b"WEBP" + b"VP8X"
                + struct.pack("<I", 10) + b"\x00" * 4
                + struct.pack("<I", 1179)[:3] + struct.pack("<I", 721)[:3])
    expect("webp extended", webp_size(extended), (1180, 722))

    expect("not an image", size_of(os.devnull), None)

    # ---- and the check itself, over a tree built for it --------------------
    root = tempfile.mkdtemp()

    def write(name, width, height):
        with open(os.path.join(root, name), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR"
                    + struct.pack(">II", width, height))

    def page(name, body):
        with open(os.path.join(root, name), "w", encoding="utf-8") as f:
            f.write(body)

    try:
        write("wide.png", 1350, 1010)
        write("narrow.png", 674, 504)
        write("card.png", 1200, 630)
        write("squat.png", 100, 100)

        page("right.html",
             '<img src="wide.png" width="1350" height="1010">')
        expect("a picture that is what it says", check(root)[0], [])

        page("right.html", "")
        page("stale.html",
             '<img src="wide.png" width="1348" height="1010">')
        expect("a stale width", len(check(root)[0]), 1)

        page("stale.html", "")
        page("srcset.html",
             '<img src="narrow.png" srcset="narrow.png 674w, wide.png 1350w" '
             'width="1350" height="1010">')
        expect("the largest of a srcset declared", check(root)[0], [])

        page("srcset.html", "")
        page("shape.html",
             '<img src="wide.png" srcset="wide.png 1350w, squat.png 100w" '
             'width="1350" height="1010">')
        expect("a srcset entry of another shape", len(check(root)[0]), 1)

        page("shape.html", "")
        page("gone.html", '<img src="missing.png" width="10" height="10">')
        expect("a picture that is not there", len(check(root)[0]), 1)

        page("gone.html", "")
        page("card.html",
             '<meta property="og:image" content="https://x/card.png">')
        expect("a card of the right shape", check(root)[0], [])

        page("card.html",
             '<meta property="og:image" content="https://x/squat.png">')
        expect("a card of the wrong shape", len(check(root)[0]), 1)
    finally:
        shutil.rmtree(root, ignore_errors=True)

    print("  %d self tests, %d wrong" % (13, wrong))

    return wrong


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(1 if self_test() else 0)

    where = sys.argv[1] if len(sys.argv) > 1 else "docs"

    complaints, counted = check(os.path.abspath(where))

    for complaint in complaints:
        print("::error::" + complaint)

    print("%d declared sizes checked, %d wrong" % (counted, len(complaints)))

    sys.exit(1 if complaints else 0)
