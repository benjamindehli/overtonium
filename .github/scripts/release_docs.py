"""Checks that a version has the notes and the site section it needs.

Called twice, from two workflows, which is the point of it being a file. The
release workflow runs it on a tag, where failing means a release that was going
out with last release's notes attached or a site that never mentions it. The
build workflow runs it on every push against whatever CMakeLists currently
says, so the same omission is found days earlier, while it is still a commit
rather than a tag that has to be deleted and pushed again.

That second half exists because it happened. 1.7.0 was tagged without a section
on the releases page, the release failed, and the tag had to be taken down and
put back. Nothing before the tag had any reason to look.

    release_docs.py <version>
"""

import re
import sys
from pathlib import Path

NOTES = "release-notes/{}.md"
PAGE = "docs/releases/index.html"


def check(version: str) -> list[str]:
    problems = []

    # One file per release, so a version's notes cannot be overwritten by the
    # next one and a tag picks its own out by name.
    notes = Path(NOTES.format(version))

    if not notes.is_file():
        problems.append(f"{notes} does not exist")
        listing = sorted(p.name for p in Path("release-notes").glob("*.md"))
        problems.append("release-notes/ holds: " + ", ".join(listing))
    else:
        # The first heading rather than the first one that happens to match, so
        # a version named further down cannot stand in for the title itself
        # being right. The filename alone is not enough: copying last release's
        # notes to a new name is exactly the mistake this catches.
        first = re.search(r"^# .*$", notes.read_text(encoding="utf-8"), re.M)
        headed = first.group(0) if first else ""
        want = f"# Overtonium {version}"

        if headed != want:
            problems.append(f"{notes} is headed {headed!r}, wanted {want!r}")

    # The site's own account of what changed. The notes file is written for
    # somebody standing on the release page and this is written for somebody
    # reading the site, so one cannot be generated from the other, and a
    # heading is the one part of it a machine can insist on.
    page = Path(PAGE)
    heading = f"<h2>{version},"

    if heading not in page.read_text(encoding="utf-8"):
        problems.append(f"{page} has no section headed '{version}, <date>'")
        seen = re.findall(r"<h2>([^<]*)</h2>",
                          page.read_text(encoding="utf-8"))[:3]
        problems.append("it currently opens with: " + ", ".join(seen))

    return problems


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: release_docs.py <version>")
        return 1

    version = sys.argv[1]
    problems = check(version)

    for p in problems:
        print(f"::error::{p}")

    if problems:
        return 1

    print(f"{version} has its notes and its section on the releases page")
    return 0


if __name__ == "__main__":
    sys.exit(main())
