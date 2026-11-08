"""Checks that the clips on the site are what the plugin plays now.

The clips in docs/audio are rendered by overtonium_render_docs_audio in the
Linux build job and committed. A change to the audio path changes what they
should hold, and nothing else notices when they are not rendered again:
thirteen of them sat a step out of date for two weeks that way. The build job
renders them afresh and this compares the two sets.

The comparison is exact, so the committed clips have to come from that job.
Another compiler, or another version of the same one, rounds some samples the
other way, by a step of 16-bit audio, without anything being wrong. With
--changed-to, the clips that differ are copied there, FLAC and Ogg, for the job
to keep as an artifact to be committed.

What is compared is the MD5 of the unencoded audio that every FLAC file stores
in its own header. The encoded bytes can change without the sound changing,
for example across a JUCE upgrade, and the Ogg files differ on every render
because the container carries a random stream serial.

    audio_clips.py <committed directory> <fresh directory> [--changed-to DIR]
    audio_clips.py --self-test
"""

import shutil
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

    args = sys.argv[1:]
    changed_to = None

    if "--changed-to" in args:
        at = args.index("--changed-to")

        if at + 1 >= len(args):
            sys.exit(__doc__)

        changed_to = Path(args[at + 1])
        del args[at:at + 2]

    if len(args) != 2:
        sys.exit(__doc__)

    committed, fresh = args
    problems, checked = compare(committed, fresh)

    for problem in problems:
        print(f"::error::{problem}")

    if not problems:
        print(f"all {checked} clips match what the plugin plays")
        return

    if changed_to is not None:
        changed_to.mkdir(parents=True, exist_ok=True)

        for flac in sorted(Path(fresh).glob("*.flac")):
            old = Path(committed) / flac.name

            if old.exists() and audio_md5(old) == audio_md5(flac):
                continue

            for clip in (flac, flac.with_suffix(".ogg")):
                if clip.exists():
                    shutil.copy2(clip, changed_to / clip.name)

        print("The clips that changed are in the clips-changed artifact of "
              "this run. Unpack it into docs/audio and commit them. "
              "CONTRIBUTING.md has the step.")

    sys.exit(1)


if __name__ == "__main__":
    main()
