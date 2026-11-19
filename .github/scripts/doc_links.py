"""Checks that every link to this repository's own files and site resolves.

Walks the site in docs/ (HTML, CSS, the Markdown copies, llms.txt,
llms-full.txt, the sitemap and the JSON files) and the Markdown in the
repository, collects every link, and resolves each one to a file. Exits 1
listing anything that does not exist.

Three kinds of link are checked. Relative links resolve against the file they
are in, and a folder such as ../tuning/ is served by its index.html. Links
starting /overtonium/ resolve against the site, which 404.html relies on.
Full URLs on the published site are checked wherever they appear, which covers
the canonical links, the share cards, the JSON-LD, the sitemap, the llms files,
the readme and the release notes. Other sites are not fetched, so this stays
offline and fast. It catches a renamed page, section, clip or picture, not a
dead link somewhere else on the web.

A #section is checked whenever a link lands on an HTML page. The site's pages
link into each other's sections a great deal, and renaming an id breaks a link
that still reads right. A section that has moved to another page is listed in
MOVED in Tools/site_data.py, and docs/moved.js sends an old link on to it, so
a link to a moved section counts as found when the section exists where it
went. Fragments into Markdown are left alone, because whoever renders the file
makes those anchors.

    python3 .github/scripts/doc_links.py --self-test
    python3 .github/scripts/doc_links.py
"""

import re
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent.parent

SITE_URL = "https://benjamindehli.github.io/overtonium/"
SITE_PREFIX = "/overtonium/"

# Folders with nothing of ours to check: installed tooling, build output and
# local working folders.
SKIP_DIRS = {"node_modules", "build", "build-werror", "temp", "archives", ".git"}

SITE_SUFFIXES = {".html", ".css", ".md", ".txt", ".xml", ".json"}

ATTR = re.compile(r'(?:href|src|poster)="([^"]+)"')
SRCSET = re.compile(r'srcset="([^"]+)"')
CSS_URL = re.compile(r'url\("?([^")]+)"?\)')
# [text](target) and ![alt](target), with an optional "title" after the target.
MD_LINK = re.compile(r'!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')
# A full site URL ends at a space, a quote, a bracket or a backtick. A full
# stop or comma at its end belongs to the sentence it sits in.
SITE_LINK = re.compile(re.escape(SITE_URL) + r"[^\s\"'<>)\]`]*[^\s\"'<>)\]`.,;:]")
# A JSON-LD "@id" names a thing in the structured data. It looks like a URL
# but is not a link, so it is taken out before searching.
JSONLD_ID = re.compile(r'"@id"\s*:\s*"[^"]*"')
# An element's id. The lookbehind keeps this off data-id and the like.
ID = re.compile(r'(?<![-\w])id="([^"]+)"')
# One entry in the MOVED map that Tools/build_site.py writes into moved.js.
MOVED_ENTRY = re.compile(r'^\s*"?([\w-]+)"?:\s*"\.\./([\w-]+)/",?$', re.M)


def is_external(url):
    return url.startswith(("http://", "https://", "//", "mailto:", "data:", "tel:"))


def references(path):
    """Every link in a file that points at something of ours."""
    text = path.read_text(encoding="utf-8")
    found = set()

    if path.suffix in (".md", ".txt"):
        found |= set(MD_LINK.findall(text))
    elif path.suffix in (".html", ".css"):
        found |= set(ATTR.findall(text)) | set(CSS_URL.findall(text))
        for group in SRCSET.findall(text):
            for candidate in group.split(","):
                parts = candidate.split()
                if parts:
                    found.add(parts[0])

    found = {u for u in found if u and not is_external(u)}
    found |= set(SITE_LINK.findall(JSONLD_ID.sub("", text)))
    return found


def inside(path, folder):
    try:
        path.resolve().relative_to(folder.resolve())
        return True
    except ValueError:
        return False


def check(root):
    """Prints each broken link under root and returns how many there are."""
    docs = root / "docs"
    files = [p for p in sorted(docs.rglob("*")) if p.suffix in SITE_SUFFIXES]
    files += [
        p
        for p in sorted(root.rglob("*.md"))
        if not SKIP_DIRS & set(p.relative_to(root).parts) and not inside(p, docs)
    ]

    moved_js = docs / "moved.js"
    moved = dict(MOVED_ENTRY.findall(moved_js.read_text(encoding="utf-8"))) if moved_js.exists() else {}

    ids = {}

    def ids_of(page):
        if page not in ids:
            ids[page] = set(ID.findall(page.read_text(encoding="utf-8")))
        return ids[page]

    def resolve(source, url):
        if url.startswith(SITE_URL):
            return docs / url[len(SITE_URL) :]
        if url.startswith("/"):
            return docs / url[len(SITE_PREFIX) :] if url.startswith(SITE_PREFIX) else None
        return source.parent / unquote(url)

    broken = 0
    links = 0
    fragments = 0

    for f in files:
        source = f.relative_to(root).as_posix()
        from_site = inside(f, docs)

        for url in sorted(references(f)):
            base, _, fragment = url.partition("#")
            on_site = from_site or url.startswith(SITE_URL)

            if base:
                links += 1
                found = resolve(f, base)

                if found is None:
                    print(f"  {source}: {url} leaves the site")
                    broken += 1
                    continue

                # From the Markdown, a path that climbs out of the repository
                # is one of GitHub's own pages, such as ../../releases.
                if not on_site and not inside(found, root):
                    continue

                target = found.resolve()

                # GitHub shows a folder linked from Markdown as a folder. On
                # the site it is served by its index.html.
                if target.is_dir() and on_site:
                    target = target / "index.html"

                if not target.exists():
                    print(f"  {source}: {url} is not a file")
                    broken += 1
                    continue
            elif f.suffix == ".html":
                target = f
            else:
                continue

            if not fragment or target.suffix != ".html":
                continue

            fragments += 1

            if fragment in ids_of(target):
                continue

            went = moved.get(fragment)
            if went and fragment in ids_of(docs / went / "index.html"):
                continue

            where = "this page" if target == f else target.relative_to(docs.resolve()).as_posix()
            print(f'  {source}: {url} has no id="{fragment}" in {where}')
            broken += 1

    print(f"{links} links and {fragments} sections in {len(files)} files, {broken} broken")
    return broken


# Each case is a small site and how many broken links it holds.
SELF_TEST = [
    (
        "relative links, a folder page, a section and a full URL",
        {
            "docs/index.html": '<a href="tuning/#drift">x</a> <img src="a.webp">'
            f'<link rel="canonical" href="{SITE_URL}">',
            "docs/tuning/index.html": '<h2 id="drift">x</h2> <a href="../">x</a> <a href="#drift">x</a>',
            "docs/a.webp": "",
        },
        0,
    ),
    (
        "a missing picture and a missing page",
        {
            "docs/index.html": '<img src="b.webp"> <a href="effects/">x</a>',
        },
        2,
    ),
    (
        "a renamed section, linked from the site, the readme and both llms files",
        {
            "docs/index.html": '<a href="tuning/#drift">x</a>',
            "docs/tuning/index.html": '<h2 id="pitch-drift">x</h2>',
            "docs/llms.txt": f"[Drift]({SITE_URL}tuning/#drift)",
            "docs/llms-full.txt": f"Read {SITE_URL}tuning/#drift. Or `{SITE_URL}tuning/`.",
            "README.md": f"See [drift]({SITE_URL}tuning/#drift).",
        },
        4,
    ),
    (
        "a moved section counts where it went, and only there",
        {
            "docs/index.html": '<a href="tuning/#glide">x</a> <a href="tuning/#legato">x</a>',
            "docs/tuning/index.html": "<h2>x</h2>",
            "docs/playing/index.html": '<h2 id="glide">x</h2>',
            "docs/moved.js": 'var MOVED = {\n        glide: "../playing/",\n        legato: "../playing/"\n    };',
        },
        1,
    ),
    (
        "a JSON-LD @id is a name, and ../../releases is GitHub's",
        {
            "docs/index.html": f'{{"@id": "{SITE_URL}#product"}}',
            "SECURITY.md": "[releases](../../releases/latest)",
        },
        0,
    ),
    (
        "a readme picture in the repository",
        {
            "README.md": "![x](docs/a.webp) ![y](docs/gone.webp)",
            "docs/a.webp": "",
        },
        1,
    ),
]


def self_test():
    """Runs the real check over each case, so the test cannot agree with a
    second copy of the reasoning and prove nothing."""
    wrong = 0

    for name, tree, want in SELF_TEST:
        with tempfile.TemporaryDirectory() as folder:
            # The check reaches one level above the repository for GitHub's
            # pages, so the fake repository sits one level down.
            root = Path(folder) / "repo"
            for path, text in tree.items():
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text, encoding="utf-8")
            (root / "docs").mkdir(exist_ok=True)

            got = check(root)

        if got != want:
            print(f"  WRONG  {name}: wanted {want}, got {got}")
            wrong += 1

    print(f"{len(SELF_TEST)} self tests, {wrong} wrong")
    return wrong


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(1 if self_test() else 0)

    sys.exit(1 if check(ROOT) else 0)
