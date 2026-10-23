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
import tempfile
import threading
import time
from pathlib import Path

if os.name == "nt":
    # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, inherited by
    # dbtool-gui: a DLL that fails to load before main must not open a dialog either.
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)

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


# Windows exit codes of a process that never reached main, because a DLL was missing or broken.
LOADER_FAILURE_CODES = {
    0xC0000135: "STATUS_DLL_NOT_FOUND",
    0xC0000139: "STATUS_ENTRYPOINT_NOT_FOUND",
    0xC0000142: "STATUS_DLL_INIT_FAILED",
    0xC000007B: "STATUS_INVALID_IMAGE_FORMAT",
}


def _describe_exit_code(returncode: int) -> str:
    """The exit code, plus the loader failure it stands for when it is one."""
    unsigned = returncode & 0xFFFFFFFF
    name = LOADER_FAILURE_CODES.get(unsigned)
    return f"{returncode}, 0x{unsigned:08X} {name}: a DLL failed to load" if name else str(returncode)


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

    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as sandbox:
        return _run_smoke(args, Path(sandbox))


def _run_smoke(args: argparse.Namespace, sandbox: Path) -> int:
    """Runs dbtool-gui isolated from the user's state: config discovery starts in the sandbox,
    whose throwaway dbtool.yml declares `root: true` (so no dbtool.yml next to the binary, further
    up or in the user's profile is loaded) and puts the backup folder in the sandbox, and its
    QSettings and per-user dbtool.yml live under the sandbox instead of the user's registry,
    settings file and profile."""
    sqlite_driver = "{SQLite3 ODBC Driver}" if os.name == "nt" else "SQLite3"
    database = (sandbox / "smoke.db").as_posix()
    backups = (sandbox / "backups").as_posix()
    (sandbox / "dbtool.yml").write_text(
        "root: true\n"
        f'defaultBackupDir: "{backups}"\n'
        "defaultProfile: smoke\n"
        "profiles:\n"
        "  smoke:\n"
        f'    connectionString: "DRIVER={sqlite_driver};Database={database}"\n',
        encoding="utf-8",
    )
    settings_dir = sandbox / "settings"
    settings_dir.mkdir()
    user_config_dir = sandbox / "user-config"
    user_config_dir.mkdir()

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    # A failing smoke run must report on stderr, never wait on an error dialog.
    env["LIGHTWEIGHT_NO_ERROR_DIALOGS"] = "1"
    env["LIGHTWEIGHT_DBTOOL_GUI_SETTINGS_DIR"] = str(settings_dir)
    env["LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR"] = str(user_config_dir)
    env["LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR"] = str(sandbox)

    cmd = [
        str(args.dbtool_gui),
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
        # Exited on its own right at the deadline, before we could terminate it.
        died = died or proc.poll() is not None
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
            f"FAIL: dbtool-gui exited (code {_describe_exit_code(proc.returncode)}) "
            "before liveness probe finished.",
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
