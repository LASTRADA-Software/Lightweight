#!/usr/bin/env python3
"""
Headless process smoke test for dbtool-gui.

Spawns the binary under ``QT_QPA_PLATFORM=offscreen``, lets the Qt event
loop spin for a few seconds, asserts no fatal log lines appeared on stderr
(``[FATAL]`` from ``qInstallMessageHandler``, ``QML_ERROR``, "Failed to
create QML root object"), and then terminates the process cleanly.

This catches the classes of regression an offscreen-only test can see:
QML compile errors, missing resources / icons, plugin-loader crashes,
windeployqt deploy mistakes, AppController constructor exceptions. It does
*not* assert on the in-app startup banner — that text is routed only to
the GUI's log pane (see ``AppController`` constructor), so it's invisible
to a stderr probe.

Companion to ``test_dbtool.py`` — same argparse/timeout style. Run locally
with::

    python src/tests/test_dbtool_gui.py \\
        --dbtool-gui out/build/clangcl-debug/target/dbtool-gui.exe

The CI job sets ``QT_QPA_PLATFORM=offscreen`` at job scope; we set it
unconditionally here too so the test works in both contexts.
"""

from __future__ import annotations

import argparse
import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

# Lines that indicate an unrecoverable failure during startup. The qInstall-
# MessageHandler in main.cpp routes Qt log output to stderr with a `[LEVEL]`
# prefix; the QML engine's `objectCreationFailed` lambda emits the "Failed to
# create QML root object" line directly. If any of these appear we fail
# loudly with the captured stderr so the CI log makes the cause obvious.
FATAL_MARKERS = (
    "[FATAL]",
    "[ERROR]",
    "QML_ERROR",
    "Failed to create QML root object",
    "No root objects were loaded",
)

# How long the binary must stay alive without producing a fatal marker
# before we declare success and terminate it. Three seconds is plenty for
# the QML engine to instantiate Main.qml, AppController to run its
# constructor, and ReloadPlugins() to scan the (typically empty) plugin
# directory — anything that crashes in startup does so within ~1 s.
LIVENESS_PROBE_SECONDS = 3.0


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dbtool-gui",
        required=True,
        type=Path,
        help="Path to the built dbtool-gui executable.",
    )
    parser.add_argument(
        "--liveness-seconds",
        type=float,
        default=LIVENESS_PROBE_SECONDS,
        help="How long the binary must stay alive without emitting a fatal marker before being terminated.",
    )
    parser.add_argument(
        "--extra-arg",
        action="append",
        default=[],
        help="Additional argv entries forwarded to dbtool-gui (repeatable).",
    )
    return parser.parse_args()


def _ensure_executable(path: Path) -> None:
    if not path.exists():
        print(f"Error: dbtool-gui binary not found at {path}", file=sys.stderr)
        sys.exit(1)


def _drain_stderr_until(proc: subprocess.Popen, deadline: float, fatal_markers):
    """Read stderr line-by-line until the deadline elapses, a fatal marker
    appears, or the process dies. Returns (fatal_seen, lines, process_died).

    Uses a background reader thread because `pipe.readline()` blocks
    indefinitely on Windows when no data is available — even after the
    deadline — which would defeat the liveness probe."""
    q: queue.Queue = queue.Queue()
    stop = threading.Event()

    def reader():
        try:
            for raw in iter(proc.stderr.readline, b""):
                if stop.is_set():
                    break
                q.put(raw)
        except (OSError, ValueError):
            pass  # pipe closed during shutdown

    reader_thread = threading.Thread(target=reader, name="stderr-reader", daemon=True)
    reader_thread.start()

    fatal: str | None = None
    lines: list[str] = []
    died = False
    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                died = True
                break
            try:
                raw = q.get(timeout=0.1)
            except queue.Empty:
                continue
            decoded = raw.decode("utf-8", errors="replace").rstrip()
            lines.append(decoded)
            for marker in fatal_markers:
                if marker in decoded:
                    fatal = marker
                    return fatal, lines, died
    finally:
        stop.set()
    return fatal, lines, died


def main() -> int:
    args = _parse_args()
    _ensure_executable(args.dbtool_gui)

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"

    cmd = [
        str(args.dbtool_gui),
        "--theme=light",
        "--enable-backup-restore",
        "--verbose",
        *args.extra_arg,
    ]
    print(f"Launching: {' '.join(cmd)}")

    proc = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )

    try:
        deadline = time.monotonic() + args.liveness_seconds
        fatal, lines, died = _drain_stderr_until(proc, deadline, FATAL_MARKERS)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        # Close the pipes explicitly — on Windows, Popen does not always
        # release them on terminate(), which would keep the Python process
        # alive past the end of `main()` and look like a hang.
        for stream in (proc.stdout, proc.stderr):
            try:
                if stream is not None:
                    stream.close()
            except OSError:
                pass

    print("--- captured stderr ---")
    for line in lines:
        print(line)
    print("--- end stderr ---")

    if fatal is not None:
        print(f"FAIL: fatal marker '{fatal}' appeared during startup.", file=sys.stderr)
        return 1

    if died:
        print(
            f"FAIL: dbtool-gui exited (code {proc.returncode}) before liveness probe finished.",
            file=sys.stderr,
        )
        return 1

    print(
        f"OK: dbtool-gui stayed alive for {args.liveness_seconds}s under "
        "QT_QPA_PLATFORM=offscreen with no fatal log lines."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
