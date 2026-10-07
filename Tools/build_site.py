#!/usr/bin/env python3
"""Writes the parts of the docs site that repeat from page to page.

    python3 Tools/build_site.py           rewrite docs/ in place
    python3 Tools/build_site.py --check   fail if anything would change

The site is hand-written HTML, and most of it stays that way. What this owns is
everything a page says that another page also says, or that the source already
knows: the <head> with its metadata and structured data, the header with the
section list and the page's own contents rail, the trail above the title, the
onward links, the footer, the two card lists and the questions on the front
page. Each of those is a whole element in the page, found by its tag or its
class and replaced outright, so an edit made inside one by hand is undone by
the next build and caught by --check in CI before that.

It also writes three files whole: sitemap.xml, llms.txt, and llms-full.txt,
which is every page's content as Markdown for a language model to read in one
request rather than seven.

Each page's last-modified day lives in Tools/site_dates.json beside a hash of
the words in its <main>. When the words change the day becomes today, in UTC,
and the sitemap and the page's structured data both carry it. A change to the
head, the rail or the footer is not a change to what the page says and leaves
the day alone. Reading this from git would need the whole history, which a CI
checkout does not have, and would date a page by its last commit whatever that
commit touched.

What it reads, besides Tools/site_data.py: the version from CMakeLists.txt, the
preset count from kNames in Source/Presets.cpp, the parameter count from the
expected total in Tests/plugin_runtime_test.cpp, and each page's own headings
for its rail. Every page is passed through the repository's pinned prettier
afterwards, so what is written is exactly what the format job expects.

Standard library only, and Python 3.9, which is what a Mac has without asking.
"""

import datetime
import hashlib
import html
import json
import re
import subprocess
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
DATES = Path(__file__).resolve().parent / "site_dates.json"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import site_data as data  # noqa: E402

ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
        "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = ["", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"]


def spelt(n):
    if n < 20:
        return ONES[n]
    ten, one = divmod(n, 10)
    return TENS[ten] + (f"-{ONES[one]}" if one else "")


def version():
    found = re.search(r"^project\(Overtonium VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(), re.M)
    if found is None:
        sys.exit("cannot read the version from CMakeLists.txt")
    return found.group(1)


def preset_names():
    table = re.search(r"const char \*const kNames\[\] = \{(.*?)\n\};", (ROOT / "Source/Presets.cpp").read_text(), re.S)
    if table is None:
        sys.exit("cannot find kNames in Source/Presets.cpp")
    return re.findall(r'^    "([^"]+)",$', table.group(1), re.M)


def parameter_count():
    """The total the runtime test holds the real layout to, read as text."""
    sums = re.search(r"const int expected =\s*(.*?);", (ROOT / "Tests/plugin_runtime_test.cpp").read_text(), re.S)
    if sums is None:
        sys.exit("cannot find the expected parameter count in Tests/plugin_runtime_test.cpp")

    constants = {}
    for header in (ROOT / "Source").rglob("*.h"):
        for name, value in re.findall(r"inline constexpr int (\w+) = (\d+);", header.read_text()):
            constants[name] = value

    arithmetic = re.sub(r"(?:\w+::)*(k\w+)", lambda m: constants[m.group(1)], sums.group(1))
    if not re.fullmatch(r"[\d\s+*()]+", arithmetic):
        sys.exit(f'cannot read the parameter count from "{sums.group(1)}"')
    return eval(arithmetic)


COUNTS = {}


def fill(text):
    """A string from site_data with the counts the source knows put in."""
    return text.format_map(COUNTS) if isinstance(text, str) else text


def esc(text):
    return html.escape(fill(text), quote=True)


def plain(markup):
    """The words of a piece of HTML, for a copy that cannot carry markup."""
    markup = re.sub(r"<!--.*?-->", "", markup, flags=re.S)
    return " ".join(html.unescape(re.sub(r"<[^>]+>", "", markup)).split())


# --- When each page last said something new ----------------------------------

MODIFIED = {}


def dated(page, main, dates, changed):
    """The day this page's words last changed, moved to today if they just did."""
    words = hashlib.sha256(plain(main).encode()).hexdigest()[:16]
    known = dates.get(page["path"])
    if known and known["words"] == words:
        return known["modified"]
    today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    dates[page["path"]] = {"words": words, "modified": today}
    changed.append(page["path"] or "the front page")
    return today


def jsonld(block):
    return '<script type="application/ld+json">\n' + json.dumps(block, indent=4, ensure_ascii=False) + "\n</script>"


# Where links on a page point from. The front page is at the root, every other
# page one directory down, and the 404 page anywhere at all.
def prefix(page):
    if page is data.NOT_FOUND:
        return data.ROOT_PATH
    return "" if page["path"] == "" else "../"


# --- Structured data ---------------------------------------------------------


def product():
    block = dict(data.PRODUCT)
    block["softwareVersion"] = version()
    block["datePublished"] = data.RELEASED
    block["featureList"] = [fill(f) for f in block["featureList"]]
    return block


def faq():
    return {
        "@context": "https://schema.org",
        "@type": "FAQPage",
        "mainEntity": [
            {"@type": "Question", "name": fill(q), "acceptedAnswer": {"@type": "Answer", "text": plain(fill(a))}}
            for q, a in data.QUESTIONS
        ],
    }


def website():
    return {
        "@context": "https://schema.org",
        "@type": "WebSite",
        "@id": data.BASE + "#website",
        "url": data.BASE,
        "name": "Overtonium",
        "inLanguage": data.LANGUAGE,
        "publisher": data.PUBLISHER,
    }


def webpage(page):
    """What this page is and how it hangs together with the rest.

    Each block on its own says one thing. These references are what let a
    search engine or an answer engine read the site as one connected
    description: this page belongs to that site, is about that product, sits
    at this place in the trail and is pictured by that card."""
    url = data.BASE + page["path"]
    block = {
        "@context": "https://schema.org",
        "@type": "WebPage",
        "@id": url + "#webpage",
        "url": url,
        "name": fill(page["title"]),
        "description": fill(page["description"]),
        "inLanguage": data.LANGUAGE,
        "isPartOf": {"@id": data.BASE + "#website"},
        "about": {"@id": data.PRODUCT["@id"]},
        "primaryImageOfPage": {"@type": "ImageObject", "url": data.BASE + page["image"], "width": 1200, "height": 630},
        "dateModified": MODIFIED[page["path"]],
    }
    if page["path"]:
        block["breadcrumb"] = {"@id": url + "#breadcrumb"}
    else:
        block["mainEntity"] = {"@id": data.PRODUCT["@id"]}
    return block


def breadcrumbs(page):
    return {
        "@context": "https://schema.org",
        "@type": "BreadcrumbList",
        "@id": data.BASE + page["path"] + "#breadcrumb",
        "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "Overtonium", "item": data.BASE},
            {"@type": "ListItem", "position": 2, "name": page["nav"], "item": data.BASE + page["path"]},
        ],
    }


def video():
    v = data.VIDEO
    minutes, seconds = divmod(v["seconds"], 60)
    watch = f"https://www.youtube.com/watch?v={v['id']}"
    starts = [start for _, start in v["chapters"]]
    ends = starts[1:] + [v["seconds"]]

    return {
        "@context": "https://schema.org",
        "@type": "VideoObject",
        "@id": data.BASE + "#video",
        "name": v["name"],
        "description": fill(v["description"]),
        "thumbnailUrl": v["thumbnail"],
        "uploadDate": v["uploaded"],
        "duration": f"PT{minutes}M{seconds}S",
        "contentUrl": watch,
        "embedUrl": f"https://www.youtube.com/embed/{v['id']}",
        "author": data.AUTHOR,
        "hasPart": [
            {"@type": "Clip", "name": name, "startOffset": start, "endOffset": end, "url": f"{watch}&t={start}s"}
            for (name, start), end in zip(v["chapters"], ends)
        ],
    }


# --- The elements this owns --------------------------------------------------


def head(page):
    url = data.BASE + page["path"]
    title = esc(page["title"])
    description = esc(page["description"])
    social = esc(page["social"] or page["description"])
    image = data.BASE + page["image"]
    p = prefix(page)

    blocks = {
        "website": website,
        "webpage": lambda: webpage(page),
        "product": product,
        "faq": faq,
        "breadcrumbs": lambda: breadcrumbs(page),
        "video": video,
    }
    names = [name for name in blocks if name == "webpage" or name in page["jsonld"]]

    return "\n".join(
        [
            "<head>",
            '<meta charset="utf-8" />',
            '<meta name="viewport" content="width=device-width, initial-scale=1" />',
            f"<title>{title}</title>",
            f'<meta name="description" content="{description}" />',
            f'<meta name="author" content="{esc(data.AUTHOR["name"])}" />',
            # Lets a result show the card at full width, the whole of a
            # description and a preview of the video, rather than whatever a
            # crawler would pick by default.
            '<meta name="robots" content="max-image-preview:large, max-snippet:-1, max-video-preview:-1" />',
            f'<link rel="canonical" href="{url}" />',
            f'<meta name="theme-color" content="{data.THEME_COLOUR}" />',
            "",
            '<meta property="og:type" content="website" />',
            '<meta property="og:site_name" content="Overtonium" />',
            f'<meta property="og:locale" content="{data.LANGUAGE.replace("-", "_")}" />',
            f'<meta property="og:url" content="{url}" />',
            f'<meta property="og:title" content="{title}" />',
            f'<meta property="og:description" content="{social}" />',
            f'<meta property="og:image" content="{image}" />',
            '<meta property="og:image:type" content="image/jpeg" />',
            '<meta property="og:image:width" content="1200" />',
            '<meta property="og:image:height" content="630" />',
            f'<meta property="og:image:alt" content="{esc(page["image_alt"])}" />',
            "",
            '<meta name="twitter:card" content="summary_large_image" />',
            f'<meta name="twitter:title" content="{title}" />',
            f'<meta name="twitter:description" content="{social}" />',
            f'<meta name="twitter:image" content="{image}" />',
            f'<meta name="twitter:image:alt" content="{esc(page["image_alt"])}" />',
            "",
            f'<link rel="icon" href="{p}dehli-musikk.svg" />',
            f'<link rel="apple-touch-icon" href="{p}apple-touch-icon.png" />',
            f'<link rel="stylesheet" href="{p}style.css" />',
            *[f'<script src="{p}{script}" defer></script>' for script in page["scripts"]],
            *[jsonld(blocks[name]()) for name in names],
            "</head>",
        ]
    )


def head_not_found():
    page = data.NOT_FOUND
    title = esc(page["title"])
    description = esc(page["description"])
    image = data.BASE + page["image"]
    p = data.ROOT_PATH

    return "\n".join(
        [
            "<head>",
            '<meta charset="utf-8" />',
            '<meta name="viewport" content="width=device-width, initial-scale=1" />',
            f"<title>{title}</title>",
            f'<meta name="description" content="{description}" />',
            '<meta name="robots" content="noindex" />',
            f'<meta name="theme-color" content="{data.THEME_COLOUR}" />',
            "",
            '<meta property="og:type" content="website" />',
            '<meta property="og:site_name" content="Overtonium" />',
            f'<meta property="og:url" content="{data.BASE}" />',
            f'<meta property="og:title" content="{title}" />',
            f'<meta property="og:description" content="{description}" />',
            f'<meta property="og:image" content="{image}" />',
            "",
            '<meta name="twitter:card" content="summary_large_image" />',
            f'<meta name="twitter:title" content="{title}" />',
            f'<meta name="twitter:description" content="{description}" />',
            f'<meta name="twitter:image" content="{image}" />',
            "",
            f'<link rel="icon" href="{p}dehli-musikk.svg" />',
            f'<link rel="apple-touch-icon" href="{p}apple-touch-icon.png" />',
            f'<link rel="stylesheet" href="{p}style.css" />',
            "</head>",
        ]
    )


def rail(page, main):
    """The page's contents, read from its own headings.

    A section with an id whose first child is an <h2> is an entry, and an <h3>
    with an id is an entry under the section before it. That is the whole
    rule, so a heading that should not be in the rail is one without an id."""
    found = []
    for m in re.finditer(r'<section\b[^>]*\bid="([^"]+)"[^>]*>\s*<h2\b[^>]*>(.*?)</h2>|<h3\b[^>]*\bid="([^"]+)"[^>]*>(.*?)</h3>', main, re.S):
        if m.group(1):
            label = plain(m.group(2))
            if page.get("rail_before_comma"):
                label = label.split(",")[0]
            found.append((m.group(1), label, []))
        elif found:
            found[-1][2].append((m.group(3), plain(m.group(4))))

    def entry(target, label):
        return f'<li><a href="#{target}">{html.escape(label, quote=False)}</a></li>'

    lines = ['<nav class="contents" aria-label="On this page">', "<ul>"]
    for target, label, children in found:
        if children:
            lines += ["<li>", f'<a href="#{target}">{html.escape(label, quote=False)}</a>', "<ul>"]
            lines += [entry(t, l) for t, l in children]
            lines += ["</ul>", "</li>"]
        else:
            lines.append(entry(target, label))
    lines += ["</ul>", "</nav>"]
    return "\n".join(lines)


def header(page, main):
    current = None if page is data.NOT_FOUND else page["path"]
    root = page is not data.NOT_FOUND and page["path"] == ""

    lines = [
        '<header class="nav-only">' if root else "<header>",
        '<p class="brand">',
        f'<a href="{link(page, "")}"><img class="wordmark" src="{prefix(page)}overtonium-wordmark.webp" width="2464" height="448" alt="Overtonium" /></a>',
        "</p>",
        "",
        '<nav aria-label="Sections">',
        '<ul class="sections">',
    ]
    for other in data.PAGES:
        target = link(page, other["path"])
        if other["path"] == current:
            lines += ["<li>", f'<a href="{target}" aria-current="page">{esc(other["nav"])}</a>', rail(page, main), "</li>"]
        else:
            lines.append(f'<li><a href="{target}">{esc(other["nav"])}</a></li>')
    lines += ["</ul>", "</nav>", "</header>"]
    return "\n".join(lines)


def link(page, target):
    """A link from one page to a path under the site root."""
    if page is data.NOT_FOUND:
        return data.ROOT_PATH + target
    if page["path"] == "":
        return target or "./"
    return "../" + target


def crumbs(page):
    return f'<p class="crumbs"><a href="{link(page, "")}">Overtonium</a> / {esc(page["nav"])}</p>'


def onward(page):
    lines = ['<div class="onward">', '<ul class="links">']
    for i, (target, label) in enumerate(page["onward"]):
        # A link to a part of this same page stays a fragment.
        if target.startswith(page["path"] + "#") and page["path"]:
            target = target[len(page["path"]):]
        else:
            target = link(page, target)
        primary = ' class="primary"' if i == 0 else ""
        lines.append(f'<li><a{primary} href="{target}">{esc(label)}</a></li>')
    lines += ["</ul>", "</div>"]
    return "\n".join(lines)


def cards(page, field):
    lines = ['<ul class="cards">']
    for other in data.PAGES:
        if other[field] is None:
            continue
        lines += [
            "<li>",
            f'<a href="{link(page, other["path"])}"><strong>{esc(other["nav"])}</strong><span>{esc(other[field])}</span></a>',
            "</li>",
        ]
    lines.append("</ul>")
    return "\n".join(lines)


def questions():
    lines = ['<dl class="qa">']
    for q, a in data.QUESTIONS:
        lines += [f"<dt>{esc(q)}</dt>", "<dd>", fill(a), "</dd>"]
    lines.append("</dl>")
    return "\n".join(lines)


def footer(page):
    lines = [
        "<footer>",
        '<div class="maker">',
        f'<img src="{prefix(page)}dehli-musikk.svg" alt="" width="392" height="232" loading="lazy" />',
        '<div>By Benjamin Dehli for <a href="https://www.dehlimusikk.no/">Dehli Musikk</a>. Hosts list it under DehliMusikk.</div>',
        "</div>",
        "",
        '<ul class="elsewhere">',
        *[f'<li><a href="{url}">{esc(label)}</a></li>' for url, label in data.FOOTER_LINKS],
        "</ul>",
        "",
        "<p>",
        'Released under the <a href="https://github.com/benjamindehli/overtonium/blob/main/LICENSE">GNU Affero General Public License v3</a>, '
        'which follows from JUCE. Built with <a href="https://juce.com/">JUCE 9</a>.',
        "</p>",
        "</footer>",
    ]
    return "\n".join(lines)


# --- Putting it in the pages -------------------------------------------------


def replace(text, opening, closing, new, path, required=True):
    """Swaps one whole element, found by its opening tag, for new markup."""
    starts = [m.start() for m in re.finditer(opening, text)]
    if len(starts) != 1:
        if not starts and not required:
            return text
        sys.exit(f"{path}: expected one element matching {opening}, found {len(starts)}")
    start = starts[0]
    end = text.index(closing, start) + len(closing)
    return text[:start] + new + text[end:]


def main_of(text):
    return text[text.index("<main"):text.index("</main>")]


def build(page, text, path, dates, changed):
    not_found = page is data.NOT_FOUND

    text = replace(text, r"<html\b", ">", f'<html lang="{data.LANGUAGE}">', path)
    text = replace(text, r"<header\b", "</header>", header(page, main_of(text)), path)
    text = replace(text, r"<footer>", "</footer>", footer(page), path)

    if not not_found and page["path"]:
        text = replace(text, r'<p class="crumbs">', "</p>", crumbs(page), path)
    if not not_found and page["onward"]:
        text = replace(text, r'<div class="onward">', "</div>", onward(page), path)
    if not_found:
        text = replace(text, r'<ul class="cards">', "</ul>", cards(page, "card_404"), path)
    elif page["path"] == "":
        text = replace(text, r'<ul class="cards">', "</ul>", cards(page, "card"), path)
        text = replace(text, r'<dl class="qa">', "</dl>", questions(), path)

    # Last, because the head carries the day the words in <main> last changed,
    # and the parts above can change those words.
    if not not_found:
        MODIFIED[page["path"]] = dated(page, main_of(text), dates, changed)
    return replace(text, r"<head>", "</head>", head_not_found() if not_found else head(page), path)


# --- The files written whole -------------------------------------------------


def sitemap():
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    for page in data.PAGES:
        lines += [
            "  <url>",
            f"    <loc>{data.BASE + page['path']}</loc>",
            f"    <lastmod>{MODIFIED[page['path']]}</lastmod>",
            "    <changefreq>monthly</changefreq>",
            f"    <priority>{page['priority']}</priority>",
            "  </url>",
        ]
    lines.append("</urlset>")
    return "\n".join(lines) + "\n"


def llms():
    lines = ["# Overtonium", "", f"> {fill(data.LLMS_SUMMARY)}", ""]
    for paragraph in data.LLMS_INTRO:
        lines += [fill(paragraph), ""]
    lines += ["## Documentation", ""]
    lines += [f"- [{page['nav']}]({data.BASE + page['path']}): {fill(page['llms'])}" for page in data.PAGES]
    lines += ["", "## Source", ""]
    lines += [f"- [{name}]({url}): {fill(text)}" for name, url, text in data.LLMS_SOURCE]
    lines += ["", "## Facts", ""]
    lines += [f"- {name}: {fill(text)}" for name, text in data.LLMS_FACTS]
    lines += ["", "## Optional", ""]
    lines += [f"- [{name}]({url}): {fill(text)}" for name, url, text in data.LLMS_OPTIONAL]
    return "\n".join(lines) + "\n"


class Markdown(HTMLParser):
    """The prose of a page as Markdown, for llms-full.txt.

    Only what a reader would take in: headings, paragraphs, lists, tables,
    questions and answers, captions and the words a diagram is labelled with.
    The trail and the onward links are navigation, and an audio player or a
    video poster has nothing to say as text beyond its name."""

    SKIP = {"audio", "script", "style", "button", "video", "iframe"}
    VOID = {"br", "img", "source", "hr", "input", "meta", "link", "wbr"}

    def __init__(self, url):
        super().__init__(convert_charrefs=True)
        self.url = url
        self.blocks = []
        self.buffers = [[]]
        self.prefix = ""
        self.skipping = 0
        self.depth = 0
        self.links = []
        self.pre = None
        self.table = None
        self.diagram = None
        self.svg_depth = 0
        self.video = None

    def text(self, value):
        self.buffers[-1].append(value)

    # Inline markup collects its own words, so the markers can sit against
    # them: "**Just Saw**" rather than "** Just Saw **", which Markdown does
    # not read as bold at all. Space just inside the element moves outside it.
    def open_inline(self):
        self.buffers.append([])

    def close_inline(self, left, right=None):
        inner = "".join(self.buffers.pop())
        words = " ".join(inner.split())
        before = " " if inner[:1].isspace() else ""
        after = " " if inner[-1:].isspace() else ""
        self.text(before + (left + words + (left if right is None else right) if words else "") + after)

    def flush(self):
        words = " ".join("".join(self.buffers[-1]).split())
        self.buffers[-1] = []
        if words:
            self.blocks.append(self.prefix + words)
        self.prefix = ""

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if self.skipping:
            if tag not in self.VOID:
                self.skipping += 1
            return
        if self.diagram is not None:
            if tag in ("title", "desc"):
                self.diagram.append([])
            self.svg_depth += 1
            return

        classes = (a.get("class") or "").split()

        # A video is a poster and a player until somebody presses it, which
        # as text is a link to where it can be watched.
        if tag == "figure" and "video" in classes:
            self.flush()
            self.video = a.get("data-title") or "Video"
            return
        if tag == "a" and "poster" in classes:
            self.blocks.append(f"[Video: {self.video}]({urljoin(self.url, a['href'])})")
            self.skipping = 1
            return
        if tag == "figcaption" and self.video:
            self.skipping = 1
            return

        hidden = a.get("aria-hidden") == "true" or "crumbs" in classes or "onward" in classes
        if tag in self.SKIP or hidden or (tag == "svg" and a.get("role") != "img"):
            if tag not in self.VOID:
                self.skipping = 1
            return
        if tag == "svg":
            self.flush()
            self.diagram = []
            self.svg_depth = 0
        elif tag in ("h1", "h2", "h3", "h4"):
            self.flush()
            self.prefix = "#" * int(tag[1]) + " "
        elif tag in ("p", "figcaption", "dd", "blockquote"):
            self.flush()
        elif tag == "dt":
            self.flush()
            self.open_inline()
        elif tag in ("ul", "ol"):
            self.flush()
            self.depth += 1
        elif tag == "li":
            self.flush()
            self.prefix = "  " * (self.depth - 1) + "- "
        elif tag == "pre":
            self.flush()
            self.pre = []
        elif tag == "table":
            self.flush()
            self.table = []
        elif tag == "tr" and self.table is not None:
            self.table.append([])
        elif tag in ("td", "th") and self.table is not None:
            self.buffers.append([])
        elif tag in ("code", "strong", "b", "em", "i") and self.pre is None:
            self.open_inline()
        elif tag == "a":
            self.links.append(urljoin(self.url, a["href"]) if a.get("href") else None)
            self.open_inline()
        elif tag == "br":
            self.text(" ")
        elif tag == "img" and a.get("alt"):
            self.text(f"[Image: {a['alt']}]")

    def handle_startendtag(self, tag, attrs):
        # A self-closed element opens and closes at once, so it moves no depth.
        if self.diagram is not None or self.skipping:
            return
        if tag in self.VOID:
            self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if self.skipping:
            self.skipping -= 1
            return
        if self.diagram is not None:
            if self.svg_depth == 0:
                said = [" ".join("".join(part).split()) for part in self.diagram]
                self.blocks.append("[Diagram: " + " ".join(s.rstrip(".") + "." for s in said if s) + "]")
                self.diagram = None
            else:
                self.svg_depth -= 1
            return

        if tag in ("h1", "h2", "h3", "h4", "p", "figcaption", "dd", "li", "blockquote"):
            self.flush()
        elif tag == "dt":
            self.close_inline("**")
            self.flush()
        elif tag in ("ul", "ol"):
            self.flush()
            self.depth -= 1
        elif tag == "figure" and self.video:
            self.video = None
        elif tag == "pre" and self.pre is not None:
            self.blocks.append("```\n" + "".join(self.pre).strip("\n") + "\n```")
            self.pre = None
        elif tag in ("td", "th") and self.table is not None:
            cell = " ".join("".join(self.buffers.pop()).split()).replace("|", "\\|")
            self.table[-1].append(cell)
        elif tag == "table" and self.table is not None:
            rows = [row for row in self.table if row]
            if rows:
                width = max(len(row) for row in rows)
                rows = [row + [""] * (width - len(row)) for row in rows]
                lines = ["| " + " | ".join(rows[0]) + " |", "|" + "---|" * width]
                lines += ["| " + " | ".join(row) + " |" for row in rows[1:]]
                self.blocks.append("\n".join(lines))
            self.table = None
        elif tag == "code" and self.pre is None:
            self.close_inline("`")
        elif tag in ("strong", "b") and self.pre is None:
            self.close_inline("**")
        elif tag in ("em", "i") and self.pre is None:
            self.close_inline("*")
        elif tag == "a" and self.links:
            target = self.links.pop()
            self.close_inline("[", f"]({target})") if target else self.close_inline("")

    def handle_data(self, value):
        if self.skipping:
            return
        if self.diagram is not None:
            if self.diagram:
                self.diagram[-1].append(value)
            return
        if self.pre is not None:
            self.pre.append(value)
        else:
            self.text(value)

    def markdown(self):
        """The blocks, a blank line apart, except that a list is kept tight."""
        self.flush()
        out = []
        for block in self.blocks:
            item = block.lstrip().startswith("- ")
            if out and item and out[-1][1]:
                out.append(("\n", item))
            elif out:
                out.append(("\n\n", item))
            out.append((block, item))
        return "".join(text for text, _ in out)


def llms_full(pages):
    lines = [
        "# Overtonium, the whole site",
        "",
        f"> {fill(data.LLMS_SUMMARY)}",
        "",
        f"Every page of {data.BASE} as Markdown, in the order the site lists them, so the whole of it can be read in one request. "
        f"The short version, with links into each page, is {data.BASE}llms.txt.",
    ]
    for page, text in pages:
        reader = Markdown(data.BASE + page["path"])
        reader.feed(main_of(text))
        lines += ["", "---", "", f"Page: {fill(page['title'])}", f"URL: {data.BASE + page['path']}", f"Last changed: {MODIFIED[page['path']]}", ""]
        lines.append(reader.markdown())
    return "\n".join(lines) + "\n"


def prettier(path, text):
    local = ROOT / "node_modules/.bin/prettier"
    command = [str(local)] if local.exists() else ["npx", "--no-install", "prettier"]
    done = subprocess.run(command + ["--stdin-filepath", str(path)], input=text, capture_output=True, text=True, cwd=ROOT)
    if done.returncode != 0:
        sys.exit(f"prettier refused {path}:\n{done.stderr}\nRun npm ci first if it is not installed.")
    return done.stdout


def main():
    check = "--check" in sys.argv[1:]

    names = preset_names()
    COUNTS["presets"] = spelt(len(names))
    COUNTS["preset_names"] = ", ".join(names)
    COUNTS["parameters"] = parameter_count()

    dates = json.loads(DATES.read_text(encoding="utf-8")) if DATES.exists() else {}
    changed = []

    pages = [(page, DOCS / page["path"] / "index.html") for page in data.PAGES]
    pages.append((data.NOT_FOUND, DOCS / data.NOT_FOUND["file"]))

    written = {}
    for page, path in pages:
        written[path] = prettier(path, build(page, path.read_text(encoding="utf-8"), path, dates, changed))

    built = [(page, written[path]) for page, path in pages if page is not data.NOT_FOUND]
    written[DOCS / "sitemap.xml"] = sitemap()
    written[DOCS / "llms.txt"] = llms()
    written[DOCS / "llms-full.txt"] = llms_full(built)
    written[DATES] = prettier(DATES, json.dumps(dates, indent=4) + "\n")

    stale = []
    for path, after in written.items():
        before = path.read_text(encoding="utf-8") if path.exists() else None
        if after != before:
            stale.append(path.relative_to(ROOT))
            if not check:
                path.write_text(after, encoding="utf-8")

    if check and stale:
        for path in stale:
            print(f"::error file={path}::{path} is not what Tools/build_site.py writes. Run it and commit the result.")
        if changed:
            print(f"::error::the words changed on {', '.join(changed)}, so the day each was last modified has to move with them")
        sys.exit(1)

    verb = "would change" if check else "rewrote"
    print(f"{verb} {len(stale)} of {len(written)} files" if stale else f"all {len(written)} files are current")
    if changed and not check:
        print(f"dated today: {', '.join(changed)}")


if __name__ == "__main__":
    main()
