"""Checks the structured data on the site, run by the structured data job.

The project page carries a machine-readable copy of what the prose says: what
the instrument is, what it costs, which version it is. Nothing renders it and no
test parses it, so a block broken by an edit fails silently and is discovered
by a search engine months later, if at all.

The version is checked against CMakeLists rather than rewritten at release
time. It is knowable the moment the version is bumped, unlike the download
links, which name a tag that does not exist yet and are written by the release
workflow for that reason.

    structured_data.py
"""

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
from build_site import version  # noqa: E402

os.chdir(ROOT)

project = version()

failed = []
versions = []
nodes = {}

# CITATION.cff is the other place a version is written down, and the
# one nothing was watching: it sat three releases behind before
# anybody looked. Read as text rather than parsed, so this needs no
# YAML library on the runner.
citation = Path("CITATION.cff").read_text(encoding="utf-8")
stated = re.search(r"^version: *(\S+)", citation, re.M)

if stated is None:
    print("::error::CITATION.cff states no version")
    failed.append("CITATION.cff")
else:
    print(f"CMakeLists says {project}, CITATION.cff says {stated.group(1)}")

    if stated.group(1) != project:
        print(f"::error::CITATION.cff claims {stated.group(1)}, "
              f"CMakeLists says {project}. Bump both together.")
        failed.append("CITATION.cff")

for path in sorted(Path("docs").rglob("*.html")):
    html = path.read_text(encoding="utf-8")

    for i, block in enumerate(re.finditer(
            r'<script type="application/ld\+json">(.*?)</script>',
            html, re.S)):
        try:
            data = json.loads(block.group(1))
        except ValueError as e:
            failed.append(f"{path} block {i} is not valid JSON: {e}")
            continue

        if not isinstance(data, dict):
            continue

        if "softwareVersion" in data:
            versions.append((path, data["softwareVersion"]))

        if "@id" in data:
            nodes.setdefault(data["@id"], []).append((path, data))

for problem in failed:
    print(f"::error::{problem}")

if not versions:
    print("::error::no block declares softwareVersion")
    failed.append("missing version")

for path, version in versions:
    print(f"CMakeLists says {project}, {path} says {version}")

    if version != project:
        print(f"::error::{path} claims {version}, CMakeLists says "
              f"{project}. Bump both together.")
        failed.append(str(path))

# Blocks sharing an @id describe one thing, so anything reading them
# together gets whatever they disagree about. Two pages here already
# claimed a different version and release date for the same product,
# which is how this half earned its place.
for node, claims in sorted(nodes.items()):
    fields = {k for _, block in claims for k in block}

    for field in sorted(fields):
        seen = {}

        for path, block in claims:
            if field in block:
                seen.setdefault(json.dumps(block[field], sort_keys=True),
                                []).append(str(path))

        if len(seen) > 1:
            print(f"::error::{node} disagrees about {field}: " +
                  "; ".join(f"{value} in {' and '.join(where)}"
                            for value, where in sorted(seen.items())))
            failed.append(node)

sys.exit(1 if failed else 0)
