#!/usr/bin/env python3
"""
Cross-platform preparation of the Lightweight test environment.

One-stop entry point that gets a Linux, macOS, or Windows host ready to run
the ``LightweightTest`` binary against one or more database backends. It
supersedes ``.github/prepare-test-run.sh`` (Ubuntu/apt only) for local use.

For each selected backend, the script will:

1. Install (or verify) the host-side ODBC driver(s) the test fixtures in
   ``src/tests/Utils.hpp`` need to connect. Canonical connection strings
   live in ``scripts/tests/.test-env.yml``.
2. For Docker-backed backends (MS SQL Server, PostgreSQL) ensure the Docker
   daemon is reachable, ``docker pull`` the official image, then delegate
   container start and readiness waiting to
   ``scripts/tests/docker-databases.py`` (single source of truth for
   container lifecycle).
3. Print the ``--test-env=<name>`` invocation the user should pass to the
   ``LightweightTest`` binary.

The script is idempotent: re-running is safe and re-uses existing drivers
and containers.

Usage examples::

    python scripts/prepare-test-env.py --backend all
    python scripts/prepare-test-env.py --backend mssql2022
    python scripts/prepare-test-env.py --backend sqlite3 --skip-docker
    python scripts/prepare-test-env.py --backend postgres --dry-run
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))
DOCKER_ORCHESTRATOR = os.path.join(REPO_ROOT, "scripts", "tests", "docker-databases.py")
TEST_ENV_YAML = os.path.join(REPO_ROOT, "scripts", "tests", ".test-env.yml")


@dataclass(frozen=True)
class Backend:
    """Known test backend. ``docker_db`` is None when no container is needed.

    ``odbc_drivers`` is an ordered tuple of driver-name *alternatives*: a
    backend is considered ready if *any* of them is installed (the first
    entry is preferred for generated connection strings). This covers
    cross-platform naming differences such as "SQLite3" on Linux vs
    "SQLite3 ODBC Driver" on Windows.
    """
    name: str
    docker_db: str | None
    odbc_drivers: tuple[str, ...]


BACKENDS: dict[str, Backend] = {
    "sqlite3":   Backend("sqlite3",   None,         ("SQLite3", "SQLite3 ODBC Driver")),
    "mssql2017": Backend("mssql2017", "mssql2017",  ("ODBC Driver 18 for SQL Server",)),
    "mssql2019": Backend("mssql2019", "mssql2019",  ("ODBC Driver 18 for SQL Server",)),
    "mssql2022": Backend("mssql2022", "mssql2022",  ("ODBC Driver 18 for SQL Server",)),
    "postgres":  Backend("postgres",  "postgres",   ("PostgreSQL Unicode", "PostgreSQL ANSI")),
}


# --------------------------------------------------------------------------- #
# Colored output                                                              #
# --------------------------------------------------------------------------- #

class Colors:
    _enabled: bool = sys.stdout.isatty()

    @classmethod
    def init(cls) -> None:
        if sys.platform == "win32":
            try:
                import ctypes
                kernel32 = ctypes.windll.kernel32
                kernel32.SetConsoleMode(
                    kernel32.GetStdHandle(-11),
                    0x0001 | 0x0004,
                )
            except Exception:
                cls._enabled = False
        if not sys.stdout.isatty():
            cls._enabled = False

    @classmethod
    def green(cls, t: str) -> str:  return f"\033[32m{t}\033[0m" if cls._enabled else t
    @classmethod
    def red(cls, t: str) -> str:    return f"\033[31m{t}\033[0m" if cls._enabled else t
    @classmethod
    def yellow(cls, t: str) -> str: return f"\033[33m{t}\033[0m" if cls._enabled else t
    @classmethod
    def cyan(cls, t: str) -> str:   return f"\033[36m{t}\033[0m" if cls._enabled else t
    @classmethod
    def bold(cls, t: str) -> str:   return f"\033[1m{t}\033[0m" if cls._enabled else t


# --------------------------------------------------------------------------- #
# Subprocess helpers                                                          #
# --------------------------------------------------------------------------- #

DRY_RUN = False


def say(msg: str) -> None:
    print(msg, flush=True)


def _quote(s: str) -> str:
    return f'"{s}"' if (" " in s or "\t" in s) else s


def run(cmd: list[str], *, check: bool = True, env: dict[str, str] | None = None) -> int:
    """Run a subprocess, honoring --dry-run. Prints the command before executing."""
    printable = " ".join(_quote(a) for a in cmd)
    say(f"  {Colors.cyan('$')} {printable}")
    if DRY_RUN:
        return 0
    rc = subprocess.call(cmd, env=env if env is not None else os.environ.copy())
    if check and rc != 0:
        raise RuntimeError(f"command failed (rc={rc}): {printable}")
    return rc


# --------------------------------------------------------------------------- #
# Platform detection                                                          #
# --------------------------------------------------------------------------- #

class OS:
    LINUX = "linux"
    MACOS = "macos"
    WINDOWS = "windows"


def detect_os() -> str:
    p = sys.platform
    if p.startswith("linux"):
        return OS.LINUX
    if p == "darwin":
        return OS.MACOS
    if p in ("win32", "cygwin"):
        return OS.WINDOWS
    raise RuntimeError(f"unsupported platform: {p}")


def detect_linux_distro() -> str:
    """Return the distro id (lowercase) from /etc/os-release, or 'unknown'."""
    path = "/etc/os-release"
    if not os.path.isfile(path):
        return "unknown"
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("ID="):
                return line.strip().split("=", 1)[1].strip('"').lower()
    return "unknown"


# --------------------------------------------------------------------------- #
# ODBC driver detection                                                       #
# --------------------------------------------------------------------------- #

def list_installed_drivers_unix() -> set[str]:
    odbcinst = shutil.which("odbcinst")
    if not odbcinst:
        return set()
    try:
        result = subprocess.run([odbcinst, "-q", "-d"], capture_output=True, text=True)
    except Exception:
        return set()
    drivers: set[str] = set()
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            drivers.add(line[1:-1].strip().lower())
    return drivers


def list_installed_drivers_windows() -> set[str]:
    drivers: set[str] = set()
    pattern = re.compile(r"^(.+?)\s+REG_[A-Z_]+\s+.*$")
    for flag in ("/reg:64", "/reg:32"):
        try:
            result = subprocess.run(
                ["reg", "query", r"HKLM\SOFTWARE\ODBC\ODBCINST.INI\ODBC Drivers", flag],
                capture_output=True, text=True,
            )
        except FileNotFoundError:
            return drivers
        if result.returncode != 0:
            continue
        for raw in result.stdout.splitlines():
            line = raw.strip()
            if not line or line.upper().startswith("HKEY_"):
                continue
            m = pattern.match(line)
            if m:
                drivers.add(m.group(1).strip().lower())
    return drivers


def list_installed_drivers() -> set[str]:
    return list_installed_drivers_windows() if detect_os() == OS.WINDOWS else list_installed_drivers_unix()


def driver_installed(name: str) -> bool:
    return name.lower() in list_installed_drivers()


def first_installed_driver(backend: Backend) -> str | None:
    """Return the first driver-name alternative that is installed on the host."""
    installed = list_installed_drivers()
    for name in backend.odbc_drivers:
        if name.lower() in installed:
            return name
    return None


# --------------------------------------------------------------------------- #
# Driver installation                                                         #
# --------------------------------------------------------------------------- #

def install_drivers(backend: Backend) -> None:
    # A backend is satisfied as long as any one of its driver alternatives
    # is present. Cross-platform naming differs (Linux: "SQLite3",
    # Windows: "SQLite3 ODBC Driver") — accept whichever is already there.
    found = first_installed_driver(backend)
    if found is not None:
        say(f"  {Colors.green('OK')} driver already installed: {found}")
        return
    say(f"  Missing any of: {Colors.yellow(', '.join(backend.odbc_drivers))}")

    current = detect_os()
    if current == OS.LINUX:
        _install_drivers_linux(backend)
    elif current == OS.MACOS:
        _install_drivers_macos(backend)
    elif current == OS.WINDOWS:
        _install_drivers_windows(backend)


def _install_drivers_linux(backend: Backend) -> None:
    distro = detect_linux_distro()
    if distro not in ("ubuntu", "debian"):
        say(Colors.yellow(
            f"  Automatic driver install on '{distro}' is not supported.\n"
            f"  Install {', '.join(backend.odbc_drivers)} manually, then re-run."
        ))
        return

    apt = shutil.which("apt-get") or shutil.which("apt")
    if not apt:
        raise RuntimeError("apt-get / apt not found on this Linux host")

    if backend.name == "sqlite3":
        run(["sudo", apt, "install", "-y",
             "libsqlite3-dev", "libsqliteodbc", "sqlite3", "unixodbc-dev"])
        return

    if backend.name.startswith("mssql"):
        # Mirrors .github/prepare-test-run.sh setup_sqlserver().
        run(["bash", "-c",
             "curl -fsSL https://packages.microsoft.com/keys/microsoft.asc "
             "| sudo tee /etc/apt/trusted.gpg.d/microsoft.asc >/dev/null"])
        run(["bash", "-c",
             'sudo add-apt-repository -y '
             '"$(wget -qO- https://packages.microsoft.com/config/ubuntu/20.04/prod.list)"'])
        run(["sudo", apt, "update"])
        run(["sudo", "env", "ACCEPT_EULA=y", "DEBIAN_FRONTEND=noninteractive",
             apt, "install", "-y",
             "unixodbc-dev", "unixodbc", "odbcinst", "mssql-tools18"])
        return

    if backend.name == "postgres":
        run(["sudo", apt, "install", "-y",
             "postgresql-client", "libpq-dev", "odbc-postgresql", "unixodbc-dev"])
        return


def _install_drivers_macos(backend: Backend) -> None:
    brew = shutil.which("brew")
    if not brew:
        say(Colors.yellow(
            "  Homebrew is required for automatic driver install on macOS.\n"
            "  Install it from https://brew.sh/ and re-run."))
        return

    if backend.name == "sqlite3":
        run([brew, "install", "sqliteodbc"])
        return

    if backend.name.startswith("mssql"):
        run([brew, "tap", "microsoft/mssql-release",
             "https://github.com/Microsoft/homebrew-mssql-release"])
        run([brew, "update"])
        env = os.environ.copy()
        env["HOMEBREW_ACCEPT_EULA"] = "Y"
        run([brew, "install", "msodbcsql18", "mssql-tools18"], env=env)
        return

    if backend.name == "postgres":
        run([brew, "install", "psqlodbc", "libpq"])
        return


def _install_drivers_windows(backend: Backend) -> None:
    winget = shutil.which("winget")
    choco = shutil.which("choco")

    if backend.name.startswith("mssql"):
        if winget:
            run([winget, "install", "--id", "Microsoft.msodbcsql.18", "--silent",
                 "--accept-package-agreements", "--accept-source-agreements"], check=False)
            return
        if choco:
            run([choco, "install", "-y", "sqlserver-odbcdriver"], check=False)
            return
        say(Colors.yellow(
            "  Automatic install unavailable (no winget/choco).\n"
            "  Download 'Microsoft ODBC Driver 18 for SQL Server' from:\n"
            "    https://learn.microsoft.com/sql/connect/odbc/download-odbc-driver-for-sql-server"))
        return

    if backend.name == "postgres":
        if winget:
            run([winget, "install", "--id", "PostgreSQL.psqlODBC", "--silent",
                 "--accept-package-agreements", "--accept-source-agreements"], check=False)
            return
        if choco:
            run([choco, "install", "-y", "psqlodbc"], check=False)
            return
        say(Colors.yellow(
            "  Automatic install unavailable (no winget/choco).\n"
            "  Download psqlODBC from:\n"
            "    https://www.postgresql.org/ftp/odbc/versions/msi/"))
        return

    if backend.name == "sqlite3":
        if choco:
            run([choco, "install", "-y", "sqliteodbc"], check=False)
            return
        say(Colors.yellow(
            "  Automatic install unavailable (no choco).\n"
            "  Download the SQLite3 ODBC driver from:\n"
            "    http://www.ch-werner.de/sqliteodbc/"))
        return


# --------------------------------------------------------------------------- #
# Docker orchestration                                                        #
# --------------------------------------------------------------------------- #

def docker_available() -> bool:
    if not shutil.which("docker"):
        return False
    try:
        rc = subprocess.run(["docker", "info"], capture_output=True, text=True).returncode
        return rc == 0
    except Exception:
        return False


def orchestrate(action: str, target: str, *extra: str) -> None:
    """Shell out to scripts/tests/docker-databases.py."""
    if not os.path.isfile(DOCKER_ORCHESTRATOR):
        raise RuntimeError(f"docker orchestrator not found: {DOCKER_ORCHESTRATOR}")
    cmd = [sys.executable, DOCKER_ORCHESTRATOR, action, *extra, target]
    run(cmd)


# --------------------------------------------------------------------------- #
# Per-backend preparation                                                     #
# --------------------------------------------------------------------------- #

def prepare_backend(backend: Backend, *, skip_drivers: bool, skip_docker: bool) -> None:
    say(Colors.bold(f"\n=== {backend.name} ==="))

    if not skip_drivers:
        say("[1/3] Checking ODBC drivers...")
        install_drivers(backend)
    else:
        say("[1/3] Skipping ODBC driver step (--skip-drivers)")

    if backend.docker_db is None:
        say("[2/3] No Docker image needed for this backend")
        say("[3/3] No container to start for this backend")
        return

    if skip_docker:
        say("[2/3] Skipping image pull (--skip-docker)")
        say("[3/3] Skipping container start (--skip-docker)")
        return

    if not docker_available():
        raise RuntimeError(
            "Docker daemon is not reachable. Install Docker and ensure it is running, "
            "or re-run with --skip-docker to prepare non-Docker backends only.")

    say("[2/3] Pulling image via docker-databases.py --pull ...")
    orchestrate("--pull", backend.docker_db)

    say("[3/3] Starting container and waiting for readiness ...")
    orchestrate("--start", backend.docker_db, "--wait")


# --------------------------------------------------------------------------- #
# CLI                                                                         #
# --------------------------------------------------------------------------- #

def main() -> int:
    global DRY_RUN

    Colors.init()

    parser = argparse.ArgumentParser(
        description="Cross-platform setup for Lightweight database-backed tests.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  %(prog)s --backend all\n"
            "  %(prog)s --backend mssql2022\n"
            "  %(prog)s --backend sqlite3 --skip-docker\n"
            "  %(prog)s --backend postgres --dry-run\n"
            f"\nAvailable backends: {', '.join(BACKENDS)}, all\n"
        ),
    )
    parser.add_argument(
        "--backend",
        action="append",
        default=[],
        help="Backend to prepare. May be repeated. Use 'all' for every known backend.",
    )
    parser.add_argument("--skip-drivers", action="store_true",
                        help="Do not install or check host-side ODBC drivers.")
    parser.add_argument("--skip-docker", action="store_true",
                        help="Do not pull images or start containers.")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print commands without executing them.")
    args = parser.parse_args()

    DRY_RUN = args.dry_run
    if not args.backend:
        parser.error("at least one --backend is required")

    selected: list[Backend] = []
    for b in args.backend:
        if b == "all":
            selected.extend(BACKENDS.values())
            continue
        if b not in BACKENDS:
            parser.error(f"unknown backend: {b}. Choose from: {', '.join(BACKENDS)}, all")
        selected.append(BACKENDS[b])

    seen: set[str] = set()
    ordered: list[Backend] = []
    for backend in selected:
        if backend.name in seen:
            continue
        seen.add(backend.name)
        ordered.append(backend)

    say(Colors.bold(f"Platform: {detect_os()}  |  Python: {platform.python_version()}"))
    say(Colors.bold(f"Backends to prepare: {', '.join(b.name for b in ordered)}"))
    if not os.path.isfile(TEST_ENV_YAML):
        say(Colors.yellow(f"Warning: {TEST_ENV_YAML} not found (tests may fall back to SQLite)"))

    failed: list[str] = []
    for backend in ordered:
        try:
            prepare_backend(backend,
                            skip_drivers=args.skip_drivers,
                            skip_docker=args.skip_docker)
        except Exception as e:
            say(f"  {Colors.red('ERROR')} preparing {backend.name}: {e}")
            failed.append(backend.name)

    say("")
    if failed:
        say(Colors.red(f"Failed: {', '.join(failed)}"))
        return 1

    say(Colors.green("All selected backends are ready."))
    say("Run the test binary with, for example:")
    for backend in ordered:
        say(f"  LightweightTest --test-env={backend.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
