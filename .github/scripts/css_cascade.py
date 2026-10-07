"""Finds a rule inside a media query that a later rule quietly outranks.

A media query adds nothing to specificity. So `.sections { display: block }`
written inside `@media (min-width: 1240px)` and `.sections { display: flex }`
written four hundred lines further down are the same weight, and the one later
in the file wins at every window size. The query looks like it is doing
something and does nothing.

That is the worst shape a bug can have in a stylesheet: invisible in the
source, because the rule you are reading is right there and says what you
meant, and visible only in a browser, at one particular window width, on
whichever pages happen to use that selector.

It happened on 7 October 2026. A sidebar laid the six section links out as a
column at wide widths, and `.sections` was restyled at the foot of the file by
rules written long before, so the links stayed a horizontal row squeezed into a
196px column and the wordmark overflowed it, on every page. Two dead rules, no
error anywhere, and nothing in the repository could have caught it.

The fix in that case was to move the media blocks to the end of the file, which
is why they carry a comment saying they have to stay there. This checks that
nobody moves them back.

Only properties set in both places are reported. Two rules for the same
selector are perfectly normal when they say different things.

What this does not catch: the same fault committed with a *different* selector
rather than a later one. `header.nav-only .sections { margin-top: 0 }` beats
`.sections { margin-top: 30px }` inside a media query on specificity alone,
wherever either is written, and that is indistinguishable from the ordinary
case of a more specific rule meaning to win. It bit on the same day as the
fault above, on the same stylesheet. The only defence there is reading, and
knowing that a rule written for a narrow masthead may have been written before
there was a wide one.
"""

import io
import re
import sys
from pathlib import Path


def rules(css):
    """Every declaration block, as (selector, line, media depth, properties).

    A hand-rolled walk rather than a parser, because the alternative is a
    dependency on the runner for a file this regular. It understands nesting
    only as far as media queries, which is as far as this stylesheet goes.
    """
    out = []
    depth = 0
    line = 1
    i = 0
    at_media = re.compile(r"@media[^{]*\{")
    block = re.compile(r"([^{}@]+)\{([^{}]*)\}")

    while i < len(css):
        m = at_media.match(css, i)
        if m:
            depth += 1
            line += css.count("\n", i, m.end())
            i = m.end()
            continue

        if css[i] == "}":
            depth = max(0, depth - 1)
            i += 1
            continue

        m = block.match(css, i)
        if m:
            raw = m.group(1)
            selector = " ".join(re.sub(r"/\*.*?\*/", "", raw, flags=re.S).split())
            props = {p.split(":")[0].strip() for p in m.group(2).split(";") if ":" in p}

            # The selector pattern takes the whitespace in front of the rule
            # with it, newlines included, so the line the rule is really on is
            # the one its first printing character sits on rather than the one
            # the match began on. Counting after the fact put every rule on the
            # line of the rule before it, which made a later rule look earlier
            # and hid exactly the overrides this is here to find.
            at = line + css.count("\n", i, i + len(raw) - len(raw.lstrip()))

            for one in (s.strip() for s in selector.split(",")):
                if one and props:
                    out.append((one, at, depth, props))

            line += css.count("\n", i, m.end())
            i = m.end()
            continue

        if css[i] == "\n":
            line += 1
        i += 1

    return out


def check(css, name):
    found = rules(css)
    bad = set()

    for selector, line, depth, props in found:
        if depth == 0:
            continue

        for other, later, other_depth, other_props in found:
            if other_depth or other != selector or later <= line:
                continue

            clash = props & other_props
            if clash:
                bad.add(
                    f"  {selector}\n"
                    f"      set at line {line}, inside a media query\n"
                    f"      beaten at line {later}, which is not in one\n"
                    f"      for: {', '.join(sorted(clash))}"
                )

    for line in sorted(bad):
        print(f"::error file={name}::a media query here does nothing")
        print(line)

    return len(bad)


SELF_TEST = [
    (
        "a media rule beaten by a later plain one",
        "@media (min-width: 900px) { .a { display: block; } }\n.a { display: flex; }\n",
        1,
    ),
    (
        "the same pair the other way round, which is fine",
        ".a { display: flex; }\n@media (min-width: 900px) { .a { display: block; } }\n",
        0,
    ),
    (
        "two rules that say different things, which is normal",
        "@media (min-width: 900px) { .a { display: block; } }\n.a { color: red; }\n",
        0,
    ),
    (
        "a more specific later rule, which is allowed to win and means to",
        "@media (min-width: 900px) { .a { display: block; } }\n.b .a { display: flex; }\n",
        0,
    ),
    (
        "one of several selectors in a list",
        "@media (min-width: 900px) { .a, .b { display: block; } }\n.b { display: flex; }\n",
        1,
    ),
    (
        "both inside media queries, which the window decides between",
        "@media (min-width: 900px) { .a { display: block; } }\n"
        "@media (max-width: 899px) { .a { display: flex; } }\n",
        0,
    ),
]


def self_test():
    """Runs the real check over each case, rather than a second copy of its
    reasoning. A self test that reimplements what it is testing agrees with
    itself and proves nothing, which is how the first version of this passed
    while finding none of the two faults it was written for."""
    wrong = 0

    for name, css, want in SELF_TEST:
        buffer = io.StringIO()
        stdout = sys.stdout
        sys.stdout = buffer
        try:
            got = check(css, "<self test>")
        finally:
            sys.stdout = stdout

        if got != want:
            print(f"  WRONG  {name}: wanted {want}, got {got}")
            wrong += 1

    print(f"{len(SELF_TEST)} self tests, {wrong} wrong")
    return wrong


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(1 if self_test() else 0)

    where = Path(sys.argv[1] if len(sys.argv) > 1 else "docs")
    sheets = sorted(where.rglob("*.css"))
    total = sum(check(p.read_text(encoding="utf-8"), p.as_posix()) for p in sheets)

    print(f"{len(sheets)} stylesheet(s) checked, {total} dead media rule(s)")
    sys.exit(1 if total else 0)
