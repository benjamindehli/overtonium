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

What it reads, besides Tools/site_data.py: the version from CMakeLists.txt, the
preset count from kNames in Source/Presets.cpp, the parameter count from the
expected total in Tests/plugin_runtime_test.cpp, and each page's own headings
for its rail. Every page is passed through the repository's pinned prettier
afterwards, so what is written is exactly what the format job expects.

Standard library only, and Python 3.9, which is what a Mac has without asking.
"""

import html
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"

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


def preset_count():
    table = re.search(r"const char \*const kNames\[\] = \{(.*?)\n\};", (ROOT / "Source/Presets.cpp").read_text(), re.S)
    if table is None:
        sys.exit("cannot find kNames in Source/Presets.cpp")
    return len(re.findall(r'^    "[^"]+",$', table.group(1), re.M))


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
    return " ".join(html.unescape(re.sub(r"<[^>]+>", "", markup)).split())


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


def breadcrumbs(page):
    return {
        "@context": "https://schema.org",
        "@type": "BreadcrumbList",
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

    blocks = {"product": product, "faq": faq, "breadcrumbs": lambda: breadcrumbs(page), "video": video}

    return "\n".join(
        [
            "<head>",
            '<meta charset="utf-8" />',
            '<meta name="viewport" content="width=device-width, initial-scale=1" />',
            f"<title>{title}</title>",
            f'<meta name="description" content="{description}" />',
            f'<link rel="canonical" href="{url}" />',
            f'<meta name="theme-color" content="{data.THEME_COLOUR}" />',
            "",
            '<meta property="og:type" content="website" />',
            '<meta property="og:site_name" content="Overtonium" />',
            f'<meta property="og:url" content="{url}" />',
            f'<meta property="og:title" content="{title}" />',
            f'<meta property="og:description" content="{social}" />',
            f'<meta property="og:image" content="{image}" />',
            '<meta property="og:image:width" content="1200" />',
            '<meta property="og:image:height" content="630" />',
            f'<meta property="og:image:alt" content="{esc(page["image_alt"])}" />',
            "",
            '<meta name="twitter:card" content="summary_large_image" />',
            f'<meta name="twitter:title" content="{title}" />',
            f'<meta name="twitter:description" content="{social}" />',
            f'<meta name="twitter:image" content="{image}" />',
            "",
            f'<link rel="icon" href="{p}dehli-musikk.svg" />',
            f'<link rel="apple-touch-icon" href="{p}apple-touch-icon.png" />',
            f'<link rel="stylesheet" href="{p}style.css" />',
            *[f'<script src="{p}{script}" defer></script>' for script in page["scripts"]],
            *[jsonld(blocks[name]()) for name in page["jsonld"]],
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


def build(page, text, path):
    main = text[text.index("<main"):text.index("</main>")]
    not_found = page is data.NOT_FOUND

    text = replace(text, r"<head>", "</head>", head_not_found() if not_found else head(page), path)
    text = replace(text, r"<header\b", "</header>", header(page, main), path)
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
    return text


def prettier(path, text):
    local = ROOT / "node_modules/.bin/prettier"
    command = [str(local)] if local.exists() else ["npx", "--no-install", "prettier"]
    done = subprocess.run(command + ["--stdin-filepath", str(path)], input=text, capture_output=True, text=True, cwd=ROOT)
    if done.returncode != 0:
        sys.exit(f"prettier refused {path}:\n{done.stderr}\nRun npm ci first if it is not installed.")
    return done.stdout


def main():
    check = "--check" in sys.argv[1:]

    COUNTS["presets"] = spelt(preset_count())
    COUNTS["parameters"] = parameter_count()

    pages = [(page, DOCS / page["path"] / "index.html") for page in data.PAGES]
    pages.append((data.NOT_FOUND, DOCS / data.NOT_FOUND["file"]))

    stale = []
    for page, path in pages:
        before = path.read_text(encoding="utf-8")
        after = prettier(path, build(page, before, path))
        if after != before:
            stale.append(path.relative_to(ROOT))
            if not check:
                path.write_text(after, encoding="utf-8")

    if check and stale:
        for path in stale:
            print(f"::error file={path}::{path} is not what Tools/build_site.py writes. Run it and commit the result.")
        sys.exit(1)

    verb = "would change" if check else "rewrote"
    print(f"{verb} {len(stale)} of {len(pages)} pages" if stale else f"all {len(pages)} pages are current")


if __name__ == "__main__":
    main()
