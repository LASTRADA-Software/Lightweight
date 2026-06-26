#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify that documentation code snippets stay in sync with tested source.

Markdown files tag a fenced code block with an HTML comment:

    <!-- snippet: my-id -->
    ```cpp
    ... code ...
    ```

and a source file carries the *same* code between Doxygen-style region markers:

    //! [my-id]
    ... code ...
    //! [my-id]

This script checks, for every tagged block in the doc(s), that a region with the
same id exists in the source and that the two are identical after normalisation
(common leading indentation removed, trailing whitespace stripped, blank edge
lines dropped). Any drift exits non-zero with a unified diff, so CI fails when an
example is edited in one place but not the other.

Usage:
    check-doc-snippets.py [--doc DOC]... [--source SRC]...

With no arguments it checks the default pairing for this repository:
    docs/sql-to-lightweight.md  <->  src/tests/DocExampleTests.cpp
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def rel(path: Path) -> str:
    """Repo-relative path for display, falling back to the raw path."""
    try:
        return str(path.resolve().relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


DEFAULT_DOCS = ["docs/sql-to-lightweight.md"]
DEFAULT_SOURCES = ["src/tests/DocExampleTests.cpp"]

# `<!-- snippet: id -->` immediately followed by a ```cpp ... ``` fence.
DOC_SNIPPET_RE = re.compile(
    r"<!--\s*snippet:\s*(?P<id>[\w./-]+)\s*-->\s*\n```[a-zA-Z0-9+]*\n(?P<body>.*?)\n```",
    re.DOTALL,
)
# `//! [id]` ... `//! [id]` region markers.
SOURCE_MARKER_RE = re.compile(r"//!\s*\[(?P<id>[\w./-]+)\]\s*$")


def normalize(text: str) -> list[str]:
    """Drop blank edge lines, strip the common indentation, rstrip each line."""
    lines = [line.rstrip() for line in text.split("\n")]
    while lines and lines[0] == "":
        lines.pop(0)
    while lines and lines[-1] == "":
        lines.pop()
    indents = [len(line) - len(line.lstrip()) for line in lines if line]
    common = min(indents) if indents else 0
    return [line[common:] if line else "" for line in lines]


def extract_doc_snippets(path: Path) -> dict[str, list[str]]:
    snippets: dict[str, list[str]] = {}
    text = path.read_text(encoding="utf-8")
    for match in DOC_SNIPPET_RE.finditer(text):
        sid = match.group("id")
        if sid in snippets:
            raise SystemExit(f"{path}: duplicate doc snippet id '{sid}'")
        snippets[sid] = normalize(match.group("body"))
    return snippets


def extract_source_regions(path: Path) -> dict[str, list[str]]:
    regions: dict[str, list[str]] = {}
    open_id: str | None = None
    buffer: list[str] = []
    for line in path.read_text(encoding="utf-8").split("\n"):
        marker = SOURCE_MARKER_RE.search(line)
        if marker:
            sid = marker.group("id")
            if open_id is None:
                open_id, buffer = sid, []
            elif open_id == sid:
                if sid in regions:
                    raise SystemExit(f"{path}: duplicate source region id '{sid}'")
                regions[sid] = normalize("\n".join(buffer))
                open_id = None
            else:
                raise SystemExit(
                    f"{path}: region '{open_id}' not closed before '{sid}' opens"
                )
            continue
        if open_id is not None:
            buffer.append(line)
    if open_id is not None:
        raise SystemExit(f"{path}: region '{open_id}' is never closed")
    return regions


def fix_docs(docs: list[Path], regions: dict[str, tuple[Path, list[str]]]) -> int:
    """Rewrite each doc snippet body from its source region. Returns count changed."""
    changed = 0
    for doc_path in docs:
        text = doc_path.read_text(encoding="utf-8")

        def replace(match: re.Match[str]) -> str:
            sid = match.group("id")
            if sid not in regions:
                return match.group(0)
            body = "\n".join(regions[sid][1])
            return f"<!-- snippet: {sid} -->\n```cpp\n{body}\n```"

        new_text = DOC_SNIPPET_RE.sub(replace, text)
        if new_text != text:
            doc_path.write_text(new_text, encoding="utf-8")
            changed += 1
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--doc", action="append", default=[], metavar="PATH")
    parser.add_argument("--source", action="append", default=[], metavar="PATH")
    parser.add_argument(
        "--fix",
        action="store_true",
        help="rewrite doc snippet bodies from their tested source regions",
    )
    args = parser.parse_args()

    docs = [REPO_ROOT / p for p in (args.doc or DEFAULT_DOCS)]
    sources = [REPO_ROOT / p for p in (args.source or DEFAULT_SOURCES)]

    if args.fix:
        regions: dict[str, tuple[Path, list[str]]] = {}
        for path in sources:
            for sid, body in extract_source_regions(path).items():
                regions[sid] = (path, body)
        changed = fix_docs(docs, regions)
        print(f"Updated {changed} doc file(s) from source regions.")
        # Fall through to a verification pass below.

    doc_snippets: dict[str, tuple[Path, list[str]]] = {}
    for path in docs:
        for sid, body in extract_doc_snippets(path).items():
            doc_snippets[sid] = (path, body)

    source_regions: dict[str, tuple[Path, list[str]]] = {}
    for path in sources:
        for sid, body in extract_source_regions(path).items():
            source_regions[sid] = (path, body)

    if not doc_snippets:
        print("error: no '<!-- snippet: ... -->' blocks found in docs", file=sys.stderr)
        return 1

    failures = 0
    for sid in sorted(doc_snippets):
        doc_path, doc_body = doc_snippets[sid]
        if sid not in source_regions:
            print(
                f"MISSING: snippet '{sid}' in {rel(doc_path)} has no "
                f"//! [{sid}] region in any source file",
                file=sys.stderr,
            )
            failures += 1
            continue
        src_path, src_body = source_regions[sid]
        if doc_body != src_body:
            failures += 1
            diff = difflib.unified_diff(
                src_body,
                doc_body,
                fromfile=f"{rel(src_path)} //! [{sid}]",
                tofile=f"{rel(doc_path)} snippet:{sid}",
                lineterm="",
            )
            print(f"DRIFT: snippet '{sid}' differs:", file=sys.stderr)
            print("\n".join(diff), file=sys.stderr)
            print("", file=sys.stderr)

    unused = sorted(set(source_regions) - set(doc_snippets))
    for sid in unused:
        print(
            f"note: source region '{sid}' in "
            f"{rel(source_regions[sid][0])} is not referenced by any doc snippet",
            file=sys.stderr,
        )

    checked = len(doc_snippets)
    if failures:
        print(f"\n{failures} of {checked} doc snippet(s) out of sync.", file=sys.stderr)
        return 1
    print(f"OK: {checked} doc snippet(s) match their tested source regions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
