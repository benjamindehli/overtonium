"""Checks that the clips on the site are what the plugin plays now.

The clips in docs/audio are rendered by overtonium_render_docs_audio and
committed. A change to the audio path changes what they should hold, and
nothing else notices when they are not rendered again: thirteen of them sat a
step out of date for two weeks that way. The build job renders them afresh and
this compares the two sets.

What is compared is the MD5 of the unencoded audio that every FLAC file stores
in its own header. The encoded bytes can change without the sound changing,
for example across a JUCE upgrade, and the Ogg files differ on every render
because the container carries a random stream serial.

    audio_clips.py <committed directory> <fresh directory>
    audio_clips.py --self-test
"""

import sys
from pathlib import Path


def audio_md5(path):
    """The MD5 of the decoded samples, from the STREAMINFO block.

    STREAMINFO is always the first metadata block: four bytes of "fLaC", a
    four byte block header, then 34 bytes of which the last 16 are the MD5.
    """
    head = path.read_bytes()[:42]

    if len(head) < 42 or head[:4] != b"fLaC" or head[4] & 0x7F != 0:
        raise ValueError(f"{path} is not a FLAC file with STREAMINFO first")

    return head[26:42].hex()


def compare(committed, fresh):
    problems = []
    old = {p.name: p for p in Path(committed).glob("*.flac")}
    new = {p.name: p for p in Path(fresh).glob("*.flac")}

    for name in sorted(old.keys() - new.keys()):
        problems.append(f"{name} is on the site and the renderer no longer makes it")

    for name in sorted(new.keys() - old.keys()):
        problems.append(f"{name} is rendered and not on the site")

    for name in sorted(old.keys() & new.keys()):
        if audio_md5(old[name]) != audio_md5(new[name]):
            problems.append(f"{name} no longer matches what the plugin plays")

    return problems, len(old.keys() & new.keys())


def self_test():
    import tempfile

    def flac(md5):
        return b"fLaC" + bytes([0x80, 0, 0, 34]) + bytes(18) + md5

    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        (Path(a) / "same.flac").write_bytes(flac(bytes(16)))
        (Path(b) / "same.flac").write_bytes(flac(bytes(16)) + b"different encoding")
        (Path(a) / "moved.flac").write_bytes(flac(bytes(16)))
        (Path(b) / "moved.flac").write_bytes(flac(bytes(15) + b"\x01"))
        (Path(a) / "gone.flac").write_bytes(flac(bytes(16)))
        (Path(b) / "new.flac").write_bytes(flac(bytes(16)))

        problems, _ = compare(a, b)
        joined = "\n".join(problems)

        assert "same.flac" not in joined, joined
        assert "moved.flac no longer matches" in joined, joined
        assert "gone.flac is on the site" in joined, joined
        assert "new.flac is rendered" in joined, joined
        assert len(problems) == 3, joined

    print("self-test passed")


def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return

    if len(sys.argv) != 3:
        sys.exit(__doc__)

    problems, checked = compare(sys.argv[1], sys.argv[2])

    for problem in problems:
        print(f"::error::{problem}")

    if problems:
        print("Render them again with overtonium_render_docs_audio docs/audio "
              "and commit the clips that changed. CONTRIBUTING.md has the step.")
        sys.exit(1)

    print(f"all {checked} clips match what the plugin plays")


if __name__ == "__main__":
    main()
