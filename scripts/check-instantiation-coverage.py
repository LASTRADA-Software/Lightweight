#!/usr/bin/env python3
"""Ratchet on the number of *instrumented* lines per library header.

Why this exists
---------------
gcov/lcov emit coverage records per template *instantiation*, not per template *definition*. A
member of a class template — or a member function template of a plain class, which is most of
`DataMapper` — that nothing instantiates produces no `.gcno` entry at all. Its lines are therefore
absent from the tracefile rather than present with a zero hit count.

Because lcov computes `hit/found` over the lines it knows about, adding an untested template member
does not *lower* the coverage percentage the way adding an untested plain function does. It leaves
the percentage untouched (or nudges it up) while quietly shrinking the denominator. The blind spot
is invisible in the headline number, which is exactly how it went unnoticed long enough to
accumulate the gaps that motivated `src/tests/DataMapper/InstantiationCoverageTests.cpp`.

What this checks
----------------
The `LF:` (lines found) total per source file is a proxy for "how much of this header got
instantiated". It is stable under refactoring that preserves instantiation, and it *drops* when a
template stops being instantiated. So: record a baseline, and fail when a file's LF falls below it
by more than a tolerance.

This catches the failure mode the percentage cannot: a new template member that no test
instantiates never enters LF, so LF stays flat while the API grew. It does not catch that directly,
but it does catch the far more common regression — someone deletes or narrows a forced
instantiation and a previously-visible chunk of a header silently leaves the report.

Usage
-----
    # Fail if any tracked file's instrumented-line count regressed:
    check-instantiation-coverage.py --tracefile coverage/sqlite3.info

    # Accept the current numbers as the new baseline (run after intentionally
    # adding instantiations, and commit the updated baseline):
    check-instantiation-coverage.py --tracefile coverage/sqlite3.info --update-baseline

Several tracefiles may be passed; the per-file maximum LF across them is used, matching how Codecov
merges the per-database uploads into one report.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

# Only these paths are ratcheted. The library headers are what the instantiation problem applies
# to; tests, tools and examples are excluded from the Codecov report anyway (see codecov.yml).
TRACKED_PREFIX = "src/Lightweight/"

# Allowed shrinkage before failing, as a fraction of the baseline. Small drops happen for legitimate
# reasons — deleting a dead overload, merging two functions — and should not block a PR. A real
# de-instantiation removes a whole member or class at once and lands well outside this.
DEFAULT_TOLERANCE = 0.05

DEFAULT_BASELINE = Path(__file__).parent.parent / ".instantiation-coverage-baseline.json"


def parse_tracefile(path: Path) -> dict[str, int]:
    """Return {normalized source path: lines-found} for one lcov tracefile.

    lcov emits one record per source file, delimited by `SF:<path>` ... `end_of_record`, with an
    `LF:<n>` summary line inside. A file can legitimately appear more than once (one record per
    object that contributed to it); the counts are per-record totals of the same underlying lines,
    so the maximum — not the sum — is the meaningful figure.
    """
    found: dict[str, int] = {}
    current: str | None = None

    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("SF:"):
            current = normalize(line[3:])
        elif line.startswith("LF:") and current is not None:
            try:
                count = int(line[3:])
            except ValueError:
                continue
            # Same file may appear via several objects; keep the largest record.
            found[current] = max(found.get(current, 0), count)
        elif line == "end_of_record":
            current = None

    return found


def normalize(source_path: str) -> str:
    """Reduce an absolute tracefile path to a repo-relative one with forward slashes.

    lcov writes absolute paths that differ between CI runners and local machines, so the baseline
    has to key on the repo-relative tail.
    """
    unified = source_path.replace("\\", "/")
    marker = "/" + TRACKED_PREFIX
    index = unified.find(marker)
    if index != -1:
        return unified[index + 1 :]
    if unified.startswith(TRACKED_PREFIX):
        return unified
    return unified


def collect(tracefiles: list[Path]) -> dict[str, int]:
    """Merge several tracefiles, keeping the highest LF seen per file.

    The per-database tracefiles differ: a code path guarded for one DBMS is instantiated in every
    build but only *executed* in some. LF is about instantiation, so the max across databases is the
    honest figure and matches how Codecov merges the flagged uploads.
    """
    merged: dict[str, int] = defaultdict(int)
    for tracefile in tracefiles:
        for source, lines_found in parse_tracefile(tracefile).items():
            if not source.startswith(TRACKED_PREFIX):
                continue
            merged[source] = max(merged[source], lines_found)
    return dict(merged)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tracefile", action="append", required=True, type=Path,
                        help="lcov tracefile to read; repeat for several databases")
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE,
                        help=f"baseline JSON to compare against (default: {DEFAULT_BASELINE.name})")
    parser.add_argument("--update-baseline", action="store_true",
                        help="overwrite the baseline with the current numbers instead of checking")
    parser.add_argument("--tolerance", type=float, default=DEFAULT_TOLERANCE,
                        help=f"fractional shrinkage tolerated before failing (default: {DEFAULT_TOLERANCE})")
    args = parser.parse_args()

    missing = [str(t) for t in args.tracefile if not t.is_file()]
    if missing:
        print(f"error: tracefile(s) not found: {', '.join(missing)}", file=sys.stderr)
        return 2

    current = collect(args.tracefile)
    if not current:
        print(f"error: no records for '{TRACKED_PREFIX}' in the given tracefile(s). "
              f"Wrong tracefile, or the lcov --remove filter stripped the library?", file=sys.stderr)
        return 2

    if args.update_baseline:
        payload = {
            "_comment": "Instrumented-line counts per library header; see scripts/check-instantiation-coverage.py. "
                        "Regenerate with --update-baseline after intentionally adding instantiations.",
            "files": dict(sorted(current.items())),
        }
        args.baseline.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        total = sum(current.values())
        print(f"Wrote baseline for {len(current)} files ({total} instrumented lines) to {args.baseline}")
        return 0

    if not args.baseline.is_file():
        # Not an error: the baseline has to be produced by a real coverage run, which only CI does.
        # Report what a baseline *would* contain so the first CI run is self-documenting, and let
        # the caller decide (the workflow step is non-blocking until the numbers are confirmed).
        total = sum(current.values())
        print(f"No baseline at '{args.baseline}' yet.")
        print(f"Current run instruments {total} lines across {len(current)} library files.")
        print("\nTop files by instrumented lines:")
        for source, lines_found in sorted(current.items(), key=lambda kv: -kv[1])[:15]:
            print(f"  {lines_found:>6}  {source}")
        print("\nTo start enforcing, commit a baseline generated from this tracefile:")
        print("  python3 scripts/check-instantiation-coverage.py \\")
        print("      --tracefile <file.info> --update-baseline")
        return 0

    baseline = json.loads(args.baseline.read_text(encoding="utf-8")).get("files", {})

    regressions: list[tuple[str, int, int]] = []
    for source, expected in sorted(baseline.items()):
        actual = current.get(source, 0)
        if actual < expected * (1.0 - args.tolerance):
            regressions.append((source, expected, actual))

    added = sorted(set(current) - set(baseline))

    if regressions:
        print("Instrumented-line count regressed - a template likely stopped being instantiated.\n")
        for source, expected, actual in regressions:
            delta = actual - expected
            print(f"  {source}")
            print(f"      baseline {expected} -> current {actual}  ({delta:+d} lines)")
        print("\nA drop here means code that used to be compiled into the coverage report no longer is.")
        print("Coverage *percentage* will not have caught this: uninstantiated templates leave the")
        print("denominator entirely rather than counting as uncovered.\n")
        print("If the drop is intentional (dead code removed), refresh the baseline:")
        print("  python3 scripts/check-instantiation-coverage.py \\")
        print("      --tracefile <file.info> --update-baseline")
        return 1

    print(f"OK: {len(baseline)} tracked files, no instrumented-line regressions "
          f"(tolerance {args.tolerance:.0%}).")
    if added:
        print(f"\n{len(added)} file(s) newly instrumented - run --update-baseline to track them:")
        for source in added[:10]:
            print(f"  + {source} ({current[source]} lines)")
        if len(added) > 10:
            print(f"  ... and {len(added) - 10} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
