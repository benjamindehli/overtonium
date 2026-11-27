"""Checks that the Linux build runs on Ubuntu 22.04 and every newer system.

A Linux program asks for the versions of glibc and libstdc++ that the machine
it was built on had, and a system with older ones refuses to load it. A host
says no more than that the plugin failed to load. The release is built in an
Ubuntu 22.04 container so that it asks for nothing newer than 22.04 has, and
this reads what every ELF file in the build asks for and fails if any of it is
newer, so a change to the build machine cannot raise the floor without anyone
noticing.

The versions a file asks for are in its version references, which objdump -p
prints as lines like "0x09691a75 0x00 03 GLIBC_2.2.5" under the library that
provides them.

    glibc_floor.py <directory>
    glibc_floor.py --self-test
"""

import subprocess
import sys
from pathlib import Path

# What Ubuntu 22.04 provides: glibc 2.35, and libstdc++ from GCC 12.
FLOORS = {"GLIBC": "2.35", "GLIBCXX": "3.4.30", "CXXABI": "1.3.13"}


def as_numbers(version):
    return tuple(int(part) for part in version.split("."))


def required(objdump_text):
    """The newest version of each library in FLOORS that the text asks for."""
    newest = {}

    for line in objdump_text.splitlines():
        words = line.split()

        if len(words) != 4 or not words[0].startswith("0x"):
            continue

        name, _, version = words[3].partition("_")

        if name not in FLOORS or not version[:1].isdigit():
            continue

        if name not in newest or as_numbers(version) > as_numbers(newest[name]):
            newest[name] = version

    return newest


def too_new(newest):
    return [
        f"{name} {version}, newer than {FLOORS[name]}"
        for name, version in sorted(newest.items())
        if as_numbers(version) > as_numbers(FLOORS[name])
    ]


def is_elf(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


def check(directory):
    files = [p for p in sorted(Path(directory).rglob("*")) if p.is_file() and is_elf(p)]

    if not files:
        print(f"no ELF files under {directory}")
        return 1

    failed = 0

    for path in files:
        text = subprocess.run(
            ["objdump", "-p", str(path)], capture_output=True, text=True, check=True
        ).stdout
        newest = required(text)
        asks = ", ".join(f"{k} {v}" for k, v in sorted(newest.items()))
        print(f"{path}: {asks}")

        for problem in too_new(newest):
            print(f"::error::{path} asks for {problem}")
            failed = 1

    return failed


def self_test():
    sample = """
Version References:
  required from libstdc++.so.6:
    0x0297f870 0x00 12 GLIBCXX_3.4.20
    0x08922974 0x00 08 GLIBCXX_3.4
    0x0bafd179 0x00 05 CXXABI_1.3.9
  required from libc.so.6:
    0x09691a75 0x00 04 GLIBC_2.2.5
    0x069691b4 0x00 03 GLIBC_2.34
    0x06969194 0x00 02 GLIBC_2.14
    0x069691b2 0x00 06 GLIBC_PRIVATE
"""
    newest = required(sample)
    assert newest == {"GLIBC": "2.34", "GLIBCXX": "3.4.20", "CXXABI": "1.3.9"}, newest
    assert too_new(newest) == [], too_new(newest)

    # 2.4 is older than 2.35, which a string comparison gets wrong.
    assert required("    0x0 0x00 02 GLIBC_2.4\n    0x0 0x00 03 GLIBC_2.35\n") == {
        "GLIBC": "2.35"
    }

    # What the 1.18.0 Linux build asked for.
    assert too_new({"GLIBC": "2.43", "GLIBCXX": "3.4.32"}) == [
        "GLIBC 2.43, newer than 2.35",
        "GLIBCXX 3.4.32, newer than 3.4.30",
    ]

    print("self-test passed")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())

    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)

    sys.exit(check(sys.argv[1]))
