"""Finds lambdas that read a local constant without capturing it.

MSVC refuses these, and nothing else does. A `constexpr` local read inside a
lambda is not an odr-use, so the standard asks for no capture and gcc and clang
compile it happily. MSVC raises C3493 and will not, which means the fault
reaches the Windows job and nowhere earlier: it is the one compiler in the
matrix that sees it, it runs last, and it takes twenty minutes to say so.

It has cost two round trips already. The first was 23 September 2026, the
second five days later in eleven lambdas written since. This is here so the
third is caught on push instead.

Reading that output is the other half of it. One lambda that will not compile
makes every call to it an `<error type>`, so MSVC reports C2064, C2110, C2737
and C3536 all over the file and stops at a hundred errors. The lines it names
are mostly consequences. This names causes.

Four things it deliberately does not flag, each of which it did on an early run
over this repository:

  A static class member. `Character.h` keeps its constants that way precisely
  so its lambdas can read them, since a static member has no storage to
  capture.

  A constant at namespace scope, which is most of them. Those need no capture
  either, and telling one from a local means knowing whether the braces around
  it belong to a function or to a namespace.

  A constant somewhere the lambda cannot see, such as a local of a different
  function further up the file. Only declarations inside a block that encloses
  the lambda, and written before it, are in scope.

  A name that belongs to something else: `v.first` is not a local called
  `first`, and a lambda declaring its own is reading its own.

    lambda_captures.py [paths...]

Defaults to Source and Tests. Exits non-zero if it finds anything, and prints
one line per lambda saying what it reads and would have to capture.
"""

import re
import sys
from pathlib import Path

# A capture list opening with & or = takes whatever it needs, so only the ones
# naming their captures can be short of one.
LAMBDA = re.compile(r"\[([^\]\[]*)\]\s*(?:\([^)]*\))?\s*(?:mutable\s*)?"
                    r"(?:->\s*[\w:<>,&*\s]+?\s*)?\{")

CONSTEXPR = re.compile(r"(?<!\w)constexpr\s+(?:[\w:]+\s+)+(\w+)\s*=")

# What the braces belong to, read off the statement that opens them.
TYPE_OR_NAMESPACE = re.compile(r"\b(?:namespace|class|struct|union|enum)\b")

WORD = re.compile(r"\b[A-Za-z_]\w*\b")


def stripped(text: str) -> str:
    """The same text with comments and literals blanked out.

    Offsets and newlines are kept, so line numbers still work. Without this a
    brace is judged by whatever happens to precede it, and `} // namespace` at
    the end of one block makes the next function look like a namespace, which
    is how this check first came to pass everything it was given.
    """
    out = list(text)
    i, n = 0, len(text)

    while i < n:
        two = text[i:i + 2]

        if two == "//":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif two == "/*":
            while i < n and text[i:i + 2] != "*/":
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for _ in range(2):
                if i < n:
                    out[i] = " "
                    i += 1
        elif text[i] in "\"'":
            quote = text[i]
            out[i] = " "
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n and text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
        else:
            i += 1

    return "".join(out)


def pairs(text: str) -> list[tuple[int, int, bool]]:
    """Every brace pair, and whether it belongs to a namespace or a type."""
    out = []
    stack = []

    for i, c in enumerate(text):
        if c == "{":
            stack.append(i)
        elif c == "}" and stack:
            opened = stack.pop()

            # The statement that opened it, back to the previous one.
            start = max(text.rfind(";", 0, opened), text.rfind("{", 0, opened),
                        text.rfind("}", 0, opened)) + 1

            out.append((opened, i,
                        bool(TYPE_OR_NAMESPACE.search(text[start:opened]))))

    return out


def scan(path: Path) -> list[str]:
    # Analysed with comments and literals blanked, so a word in a comment
    # cannot be mistaken for code. Offsets are unchanged, so lines still line
    # up with the file as written.
    text = stripped(path.read_text(encoding="utf-8"))
    blocks = pairs(text)

    def innermost(at: int):
        best = None
        for opened, closed, is_type in blocks:
            if opened < at < closed:
                if best is None or opened > best[0]:
                    best = (opened, closed, is_type)
        return best

    # Local constants: declared inside braces that belong to neither a
    # namespace nor a type, and not static.
    locals_ = []
    for m in CONSTEXPR.finditer(text):
        line_start = text.rfind("\n", 0, m.start()) + 1

        if "static" in text[line_start:m.start()]:
            continue

        block = innermost(m.start())

        if block is not None and not block[2]:
            locals_.append((m.start(), m.group(1)))

    problems = []

    for m in LAMBDA.finditer(text):
        capture = m.group(1)

        if "&" in capture or "=" in capture:
            continue

        captured = set(WORD.findall(capture))

        opens = m.end() - 1
        block = next((b for b in blocks if b[0] == opens), None)

        if block is None:
            continue

        body = text[opens:block[1] + 1]

        # Which blocks enclose the lambda, so a constant in a sibling scope is
        # not mistaken for one it can see.
        enclosing = [b for b in blocks if b[0] < opens < b[1]]

        missing = set()

        for where, name in locals_:
            if where >= opens or name in captured:
                continue

            if not any(o < where < c for o, c, _ in enclosing):
                continue

            # Reached through nothing, or it is somebody's member.
            if not re.search(r"(?<![\w.:>])" + re.escape(name) + r"\b", body):
                continue

            # Declared again inside the lambda, so that is what it reads.
            if re.search(r"\b\w[\w:<>,*&\s]*\b" + re.escape(name) +
                         r"\s*[=;,)]", body[1:]):
                continue

            missing.add(name)

        if missing:
            line = text[:m.start()].count("\n") + 1
            problems.append(
                f"{path}:{line}: lambda reads {', '.join(sorted(missing))} "
                f"without capturing {'it' if len(missing) == 1 else 'them'}")

    return problems


def main() -> int:
    roots = [Path(a) for a in sys.argv[1:]] or [Path("Source"), Path("Tests")]

    files = []
    for root in roots:
        files += sorted(root.rglob("*.cpp")) + sorted(root.rglob("*.h"))

    problems = []
    for path in files:
        problems += scan(path)

    for p in problems:
        print(f"::error::{p}")

    print(f"{len(files)} files scanned, {len(problems)} lambda(s) MSVC would "
          f"refuse")

    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
