"""Checks that every count written in the docs matches the source, run by the
counts job.

How many factory presets there are is written out in words in many places
across the readme, the design notes, the contributing guide and the site.
Adding a preset changes all of them, and three presets went in over two
releases with one sentence or another left behind every time.

The presets are counted from kNames, read as text rather than by building, and
the host parameters from the expected count in Tests/plugin_runtime_test.cpp,
which that test checks against the real layout. Only words that are numbers are
judged, so a pattern can be as broad as it likes without inventing failures.
Release notes, the releases page and llms-full.txt are left out, since each
describes the release it belongs to.

    doc_counts.py
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
from build_site import ONES as ones, preset_names, spelt  # noqa: E402

os.chdir(ROOT)

presets = len(preset_names())


def without_tags(text):
    """Each tag becomes the newlines it held, or a space if none, so the line
    an error names is the line in the file."""
    return re.sub(r"<[^>]+>", lambda m: "\n" * m.group(0).count("\n") or " ", text)

clips = len(list(Path("docs/audio").glob("*.flac")))

print(f"kNames holds {presets} presets, docs/audio holds {clips} clips")

sums = re.search(r"const int expected =\s*(.*?);",
                 Path("Tests/plugin_runtime_test.cpp").read_text(), re.S)

if sums is None:
    print("::error::cannot find the expected parameter count in "
          "Tests/plugin_runtime_test.cpp")
    sys.exit(1)

constants = {}

for header in Path("Source").rglob("*.h"):
    for name, value in re.findall(
            r"inline constexpr int (\w+) = (\d+);", header.read_text()):
        constants[name] = value

def constant(m):
    if m.group(1) not in constants:
        print(f"::error::cannot find {m.group(1)} in Source")
        sys.exit(1)
    return constants[m.group(1)]

arithmetic = re.sub(r"(?:\w+::)*(k\w+)", constant, sums.group(1))

if not re.fullmatch(r"[\d\s+*()]+", arithmetic):
    print(f"::error::cannot read the parameter count from "
          f"\"{sums.group(1)}\"")
    sys.exit(1)

# Joined onto one line first: clang-format wraps a long sum, and a
# newline in the middle of one is a syntax error to eval.
parameters = eval(" ".join(arithmetic.split()))

print(f"the runtime test expects {parameters} parameters")

numbers = {spelt(n) for n in range(1, 100)}
numbers |= {str(n) for n in range(1, 100)}

# What each count is written in front of. Broad on purpose where the
# noun is unambiguous, since a word that is not a number is ignored
# and a wide pattern therefore cannot invent a failure. Narrow where
# it is not: "clips" means the preset clips in one sentence and every
# audio file on the site in another, and both are right.
checks = [
    (r"([\w-]+)\s+factory patches", presets),
    (r"([\w-]+)\s+factory presets", presets),
    (r"([\w-]+)\s+presets", presets),
    (r"([\w-]+)\s+that ship", presets),
    (r"([\w-]+)\s+clips meant to be browsed", presets),
    (r"across all ([\w-]+) clips", clips),
]

files = sorted(Path(".").glob("*.md"))
files += sorted(p for p in Path("docs").rglob("*.html")
                if "releases" not in p.parts)
# llms-full.txt is left out for the reason the releases page is: it
# carries that page's text, every count in it true of its release.
# The rest of it is the other pages, which are checked above.
files += sorted(p for p in Path("docs").glob("*.txt")
                if p.name != "llms-full.txt")

failed = []

for path in files:
    text = path.read_text(encoding="utf-8")

    # Tags out first, so a count and its noun that have a link
    # between them still read as neighbours. "the thirty-one
    # <a href="presets/">factory presets</a>" was two releases stale
    # on the front page and on the controls page, and this check
    # walked past it twice, because \s+ cannot cross an anchor.
    #
    # Each tag becomes the newlines it held, or a space if none, so
    # the line an error names is the line in the file.
    text = without_tags(text)

    for pattern, expected in checks:
        want = spelt(expected)

        for m in re.finditer(pattern, text):
            said = m.group(1).lower()

            if said not in numbers or said in (want, str(expected)):
                continue

            line = text[:m.start()].count("\n") + 1
            print(f"::error file={path},line={line}::"
                  f"{path}:{line} says \"{m.group(0)}\", and there "
                  f"are {expected} ({want})")
            failed.append(str(path))

def spelt_hundreds(n):
    hundreds, rest = divmod(n, 100)
    return f"{ones[hundreds]} hundred" + (f" and {spelt(rest)}" if rest else "")

said_parameters = [
    (r"\b(\d{3,4})\s+(?:host\s+)?parameters\b", str(parameters)),
    (r"\b([a-z]+ hundred(?: and [a-z-]+)?)\s+(?:host\s+)?parameters\b",
     spelt_hundreds(parameters)),
]

for path in files:
    text = without_tags(path.read_text(encoding="utf-8"))

    for pattern, want in said_parameters:
        for m in re.finditer(pattern, text, re.I):
            if m.group(1).lower() == want:
                continue

            line = text[:m.start()].count("\n") + 1
            print(f"::error file={path},line={line}::"
                  f"{path}:{line} says \"{m.group(0)}\", and there "
                  f"are {parameters} ({want})")
            failed.append(str(path))

if failed:
    sys.exit(1)

print(f"every counted noun agrees, across {len(files)} files")
