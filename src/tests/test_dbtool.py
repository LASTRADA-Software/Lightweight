#!/usr/bin/env python3
import argparse
import atexit
import subprocess
import sys
import json
import os
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False

if os.name == "nt":
    # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, inherited by every
    # dbtool started below: a DLL that fails to load before main must not open a dialog either.
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)


def find_test_env_file():
    """
    Search upward from current working directory for .test-env.yml.
    Stop at directory containing .git (project root boundary) or filesystem root.
    Also checks scripts/tests/.test-env.yml at project root.
    """
    current_dir = Path.cwd()
    project_root = None

    while True:
        test_env_path = current_dir / ".test-env.yml"
        if test_env_path.exists():
            return test_env_path

        # Check if we reached project root (directory containing .git)
        git_path = current_dir / ".git"
        if git_path.exists():
            project_root = current_dir
            break

        parent_dir = current_dir.parent
        if parent_dir == current_dir:
            return None  # Reached filesystem root

        current_dir = parent_dir

    # Check scripts/tests/.test-env.yml at project root
    scripts_test_env_path = project_root / "scripts" / "tests" / ".test-env.yml"
    if scripts_test_env_path.exists():
        return scripts_test_env_path

    return None


def load_connection_string_from_test_env(env_name):
    """
    Load connection string from .test-env.yml for the given environment name.
    Returns the connection string or None if not found.
    """
    if not HAS_YAML:
        print("Error: PyYAML is required for --test-env. Install with: pip install pyyaml", file=sys.stderr)
        sys.exit(1)

    config_path = find_test_env_file()
    if not config_path:
        print(f"Error: .test-env.yml not found (searched from '{Path.cwd()}' to project root)", file=sys.stderr)
        sys.exit(1)

    try:
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)

        connection_strings = config.get('ODBC_CONNECTION_STRING', {})
        if not connection_strings or env_name not in connection_strings:
            print(f"Error: Key '{env_name}' not found in ODBC_CONNECTION_STRING", file=sys.stderr)
            if connection_strings:
                print(f"Available environments: {' '.join(connection_strings.keys())}", file=sys.stderr)
            sys.exit(1)

        conn_str = connection_strings[env_name]
        if not conn_str:
            print(f"Error: Connection string for '{env_name}' is empty", file=sys.stderr)
            sys.exit(1)

        print(f"Using test environment '{env_name}' from: {config_path}")
        return conn_str

    except yaml.YAMLError as e:
        print(f"Error parsing {config_path}: {e}", file=sys.stderr)
        sys.exit(1)


# Upper bound for a single dbtool invocation. Real successful runs are well under
# 10 s — even the slowest (Windows + LocalDB on master) finishes the whole suite in
# ~2 min for ~25 invocations. 180 s gives plenty of slack and surfaces a hang in
# minutes instead of letting it consume the runner-level timeout.
PER_COMMAND_TIMEOUT_SECONDS = 180

# dbtool's exit code for a migrating command refused for a missing backup (Tools::BackupRequiredExitCode);
# 3 is taken by the CRT's abort on Windows.
BACKUP_REQUIRED_EXIT_CODE = 4


def _capture_diagnostics_on_hang(cmd, partial_stdout, partial_stderr):
    """Emit best-effort diagnostics when a dbtool invocation hits the per-command
    timeout. Output goes straight to stderr so the GitHub Actions log captures it
    even if `cmd` is still mid-flight."""
    print(f"\n!!! dbtool invocation exceeded {PER_COMMAND_TIMEOUT_SECONDS}s — "
          "capturing diagnostics", file=sys.stderr, flush=True)
    print(f"Hung command: {' '.join(cmd)}", file=sys.stderr, flush=True)
    if partial_stdout:
        print("--- partial STDOUT ---", file=sys.stderr, flush=True)
        print(partial_stdout, file=sys.stderr, flush=True)
    if partial_stderr:
        print("--- partial STDERR ---", file=sys.stderr, flush=True)
        print(partial_stderr, file=sys.stderr, flush=True)

    if os.name == "nt":
        try:
            print("--- tasklist (dbtool.exe) ---", file=sys.stderr, flush=True)
            tl = subprocess.run(
                ["tasklist", "/v", "/fi", "imagename eq dbtool.exe"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=10,
            )
            print(tl.stdout, file=sys.stderr, flush=True)
        except Exception as e:  # noqa: BLE001 — diagnostics path, swallow
            print(f"(tasklist failed: {e})", file=sys.stderr, flush=True)

    # Best-effort SQL Server side: if the connection string targets LocalDB,
    # dump active requests + locks via sqlcmd. We deliberately don't fail if
    # sqlcmd is missing — this is diagnostic-only.
    conn_str = next((c for c in cmd if "Driver=" in c or "DRIVER=" in c), "")
    if "Server=(LocalDB)" in conn_str or "SERVER=(LOCALDB)" in conn_str.upper():
        try:
            print("--- sys.dm_exec_requests / locks (LocalDB) ---",
                  file=sys.stderr, flush=True)
            diag_sql = (
                "SET NOCOUNT ON; "
                "SELECT session_id, blocking_session_id, wait_type, wait_resource, "
                "       command, status, last_wait_type, "
                "       CAST(text AS nvarchar(2000)) AS sql_text "
                "FROM sys.dm_exec_requests r "
                "OUTER APPLY sys.dm_exec_sql_text(r.sql_handle); "
                "SELECT request_session_id, resource_type, resource_associated_entity_id, "
                "       request_mode, request_status FROM sys.dm_tran_locks; "
                "EXEC sp_who2;"
            )
            sc = subprocess.run(
                ["sqlcmd", "-S", "(LocalDB)\\MSSQLLocalDB", "-Q", diag_sql],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=20,
            )
            print(sc.stdout, file=sys.stderr, flush=True)
        except Exception as e:  # noqa: BLE001 — diagnostics path, swallow
            print(f"(sqlcmd diag failed: {e})", file=sys.stderr, flush=True)


def run_command(cmd, check=True, input=None, cwd=None, env=None):
    print(f"Running: {' '.join(cmd)}", flush=True)
    # dbtool emits UTF-8 (table names, progress glyphs, etc.) regardless of platform.
    # Without an explicit encoding, Python on Windows defaults to the console code
    # page (cp1252) and crashes the reader thread with UnicodeDecodeError as soon as
    # any non-ASCII byte appears, masking the real exit status.
    #
    # `stdin=DEVNULL`: nothing this test runs should read stdin. Inheriting the
    #   parent's stdin (in CI, bash's stdin) means a future code path that
    #   accidentally calls `getline(std::cin, …)` would hang the whole CI step
    #   for hours instead of failing fast. Cheap defense in depth.
    # `timeout=…`: bound a single invocation. On hang we capture LocalDB
    #   `sys.dm_exec_requests` + `tasklist` so the next CI failure has actionable
    #   data instead of "step ran for 6 hours".
    try:
        result = subprocess.run(
            cmd,
            stdin=None if input is not None else subprocess.DEVNULL,
            input=input,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=PER_COMMAND_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as e:
        _capture_diagnostics_on_hang(cmd, e.stdout or "", e.stderr or "")
        print(f"dbtool invocation timed out after {PER_COMMAND_TIMEOUT_SECONDS}s",
              file=sys.stderr, flush=True)
        sys.exit(1)

    if result.returncode != 0:
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        if check:
            sys.exit(result.returncode)
    # Plugin loading is silent on success. Any stderr line about plugin migration
    # retrieval indicates a regression (e.g. duplicate release registration).
    if "Error retrieving migrations from plugin" in result.stderr:
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        print("Plugin reported a migration-retrieval error — see stderr above")
        sys.exit(1)
    return result


def split_password(connection_string):
    """Removes PWD/Password from an ODBC connection string, returning (rest, password)."""
    kept, password = [], ""
    for part in connection_string.split(";"):
        key, sep, value = part.partition("=")
        if sep and key.strip().lower() in ("pwd", "password"):
            password = value.strip()
            if password.startswith("{") and password.endswith("}"):
                password = password[1:-1].replace("}}", "}")
        elif part.strip():
            kept.append(part)
    return ";".join(kept), password


def yaml_quote(value):
    """Double-quoted YAML scalar."""
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def test_post_migrate_hook(base_cmd, dbtool, plugins_dir, connection_string):
    """The PostMigrateRecorderPlugin fixture appends one line per post-migrate hook call
    to LIGHTWEIGHT_TEST_POST_MIGRATE_LOG. The hook must run exactly once after every
    successful migrate / migrate-to-release / apply (also when nothing was pending),
    while the migration lock is held, and never for dry runs, read-only or failed commands."""
    print("--- 14. post-migrate hook runs only after successful migrating commands ---")
    run_command(base_cmd + ["hard-reset", "--yes"])

    with tempfile.TemporaryDirectory() as tmpdir:
        log_path = os.path.join(tmpdir, "post-migrate.log")

        def run_recorded(command, check=True, fail_hook=False, cmd_prefix=base_cmd):
            if os.path.exists(log_path):
                os.remove(log_path)
            env = dict(os.environ, LIGHTWEIGHT_TEST_POST_MIGRATE_LOG=log_path)
            if fail_hook:
                env["LIGHTWEIGHT_TEST_POST_MIGRATE_FAIL"] = "1"
            result = run_command(cmd_prefix + command, check=check, env=env)
            calls = []
            if os.path.exists(log_path):
                with open(log_path, encoding="utf-8") as f:
                    calls = [line.strip() for line in f if line.strip()]
            return result, calls

        def expect_pending(name):
            output = run_command(base_cmd + ["list-pending"]).stdout
            if name not in output:
                print(f"expected '{name}' to be pending:\n{output}")
                sys.exit(1)

        # (command, expected hook calls, expected success, check afterwards). An expected
        # call is the exact line the fixture writes: pending count after the run, lock state.
        cases = [
            (["status"], [], True, None),
            (["list-pending"], [], True, None),
            (["list-applied"], [], True, None),
            (["releases"], [], True, None),
            (["migrate", "--dry-run"], [], True, None),
            (["migrate-to-release", "1.0.0", "--dry-run"], [], True, None),
            (["migrate-to-release", "does-not-exist"], [], False, None),
            (["migrate-to-release", "1.0.0"], ["post-migrate pending=1 lock=held"], True, None),
            (["migrate"], ["post-migrate pending=0 lock=held"], True, None),
            (["migrate"], ["post-migrate pending=0 lock=held"], True, None),
            # Negative control for the lock probe: without the lock the hook sees it free.
            (["migrate", "--no-lock"], ["post-migrate pending=0 lock=free"], True, None),
            (["migrate-to-release", "1.0.0"], ["post-migrate pending=0 lock=held"], True, None),
            (["rollback", "20230201000000"], [], True, None),
            # `apply` has no dry run: it must refuse instead of applying.
            (["apply", "20230201000000", "--dry-run"], [], False,
             lambda: expect_pending("Second Plugin Migration")),
            (["apply", "20230201000000"], ["post-migrate pending=0 lock=held"], True, None),
            (["apply", "99990101000000"], [], False, None),
            (["rollback-to-release", "1.0.0"], [], True, None),
        ]
        for command, expected_calls, expected_success, check_after in cases:
            result, calls = run_recorded(command, check=expected_success)
            if (result.returncode == 0) != expected_success:
                print(f"dbtool {' '.join(command)}: unexpected exit code {result.returncode}")
                sys.exit(1)
            if calls != expected_calls:
                print(f"dbtool {' '.join(command)}: post-migrate hook calls {calls}, expected {expected_calls}")
                sys.exit(1)
            if check_after:
                check_after()

        # A failing hook fails the command, after the migrations themselves were applied.
        result, calls = run_recorded(["migrate"], check=False, fail_hook=True)
        if result.returncode == 0:
            print("migrate with a failing post-migrate hook unexpectedly succeeded")
            sys.exit(1)
        if calls != ["post-migrate pending=0 lock=held"]:
            print(f"failing post-migrate hook calls {calls}, expected exactly one after the migration")
            sys.exit(1)
        # The hook's own message must arrive through the plugin's error sink, not as an escaped exception.
        reported = [line for line in result.stderr.splitlines()
                    if line.startswith("Plugin post-migrate failed (")
                    and "post-migrate failure requested by LIGHTWEIGHT_TEST_POST_MIGRATE_FAIL" in line]
        if not reported:
            print(f"failing post-migrate hook message was not reported through the sink:\n{result.stderr}")
            sys.exit(1)
        output = run_command(base_cmd + ["list-applied"]).stdout
        if "Second Plugin Migration" not in output:
            print(f"migration applied before the failing hook was not kept:\n{output}")
            sys.exit(1)

        run_command(base_cmd + ["rollback", "20230201000000"])
        result, calls = run_recorded(["mark-applied", "20230201000000"])
        if calls:
            print(f"mark-applied unexpectedly ran the post-migrate hook: {calls}")
            sys.exit(1)

        # A migration that fails while running never reaches the hook. The failing fixture
        # lacks the `Plugin` suffix, so it joins a scratch plugins directory under a new name.
        plugin_extensions = (".dll", ".so", ".dylib")
        fixture = next((name for name in os.listdir(plugins_dir)
                        if name.startswith("FailingMigrationFixture.") and name.endswith(plugin_extensions)), None)
        if fixture is None:
            print(f"FailingMigrationFixture not found under {plugins_dir}")
            sys.exit(1)
        failing_dir = os.path.join(tmpdir, "failing-plugins")
        os.makedirs(failing_dir)
        for name in os.listdir(plugins_dir):
            stem, extension = os.path.splitext(name)
            if extension in plugin_extensions and stem.lower().endswith("plugin"):
                shutil.copy2(os.path.join(plugins_dir, name), os.path.join(failing_dir, name))
        shutil.copy2(os.path.join(plugins_dir, fixture),
                     os.path.join(failing_dir, "FailingMigrationPlugin" + os.path.splitext(fixture)[1]))
        failing_cmd = [dbtool, "--plugins-dir", failing_dir, "--connection-string", connection_string]
        result, calls = run_recorded(["migrate"], check=False, cmd_prefix=failing_cmd)
        if result.returncode == 0:
            print("migrate with a failing migration unexpectedly succeeded")
            sys.exit(1)
        if "fixture migration failed on purpose" not in result.stderr + result.stdout:
            print(f"migrate did not fail on the fixture migration:\n{result.stdout}\n{result.stderr}")
            sys.exit(1)
        if calls:
            print(f"a failed migration unexpectedly ran the post-migrate hook: {calls}")
            sys.exit(1)


def test_pre_migrate_steps(base_cmd, dbtool, plugins_dir, connection_string):
    """PostMigrateRecorderPlugin registers "Recorder step" when LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG is set.
    migrate, migrate-to-release and apply run it under the migration lock before their first migration
    (also with nothing pending), dry runs call it with dryRun=1 and without the lock, read-only
    commands never call it, and a step that throws stops the command before any migration."""
    print("--- 15. pre-migrate steps run before the first migration, under the lock ---")
    run_command(base_cmd + ["hard-reset", "--yes"])

    with tempfile.TemporaryDirectory() as tmpdir:
        log_path = os.path.join(tmpdir, "steps.log")

        def run_logged(command, expected_exit=0, extra_env=None, cmd_prefix=base_cmd):
            """Runs dbtool with the step log; `expected_exit` None accepts any exit code."""
            if os.path.exists(log_path):
                os.remove(log_path)
            env = dict(os.environ, LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG=log_path, LIGHTWEIGHT_TEST_POST_MIGRATE_LOG=log_path)
            env.update(extra_env or {})
            result = run_command(cmd_prefix + command, check=False, env=env)
            if expected_exit is not None and result.returncode != expected_exit:
                print(f"dbtool {' '.join(command)} exited {result.returncode}, expected {expected_exit}:\n"
                      f"{result.stdout}\n{result.stderr}")
                sys.exit(1)
            lines = []
            if os.path.exists(log_path):
                with open(log_path, encoding="utf-8") as f:
                    lines = [line.strip() for line in f if line.strip()]
            return result, lines

        cases = [
            (["status"], []),
            (["list-pending"], []),
            (["releases"], []),
            (["migrate", "--dry-run"], ["pre-migrate dryRun=1 pending=3 lock=free"]),
            (["migrate-to-release", "1.0.0"],
             ["pre-migrate dryRun=0 pending=3 lock=held", "post-migrate pending=1 lock=held"]),
            (["migrate"], ["pre-migrate dryRun=0 pending=1 lock=held", "post-migrate pending=0 lock=held"]),
            (["migrate"], ["pre-migrate dryRun=0 pending=0 lock=held", "post-migrate pending=0 lock=held"]),
            (["rollback", "20230201000000"], []),
            (["apply", "20230201000000"], ["pre-migrate dryRun=0 pending=1 lock=held", "post-migrate pending=0 lock=held"]),
        ]
        for command, expected in cases:
            _, lines = run_logged(command)
            if lines != expected:
                print(f"dbtool {' '.join(command)}: step/hook log {lines}, expected {expected}")
                sys.exit(1)

        # A step that throws stops the command before its first migration.
        run_command(base_cmd + ["hard-reset", "--yes"])
        result, lines = run_logged(["migrate"], expected_exit=None, extra_env={"LIGHTWEIGHT_TEST_PRE_MIGRATE_FAIL": "1"})
        if result.returncode == 0:
            print("migrate with a failing pre-migrate step unexpectedly succeeded")
            sys.exit(1)
        if 'Pre-migrate step "Recorder step" failed: pre-migrate failure requested' not in result.stderr:
            print(f"the failing step was not reported:\n{result.stderr}")
            sys.exit(1)
        if "failed Recorder step" not in result.stdout or "done Recorder step" in result.stdout:
            print(f"the failing step did not end as failed:\n{result.stdout}")
            sys.exit(1)
        if lines != ["pre-migrate dryRun=0 pending=3 lock=held"]:
            print(f"a failed step still reached the migrations or the post-migrate hook: {lines}")
            sys.exit(1)
        if "Initial Migration" not in run_command(base_cmd + ["list-pending"]).stdout:
            print("a migration was applied although the pre-migrate step failed")
            sys.exit(1)

        # With a step, the step and the migrations render as tasks.
        result, _ = run_logged(["migrate", "--progress", "logline"])
        for fragment in ("begin Recorder step", "done Recorder step", "begin Migrations"):
            if fragment not in result.stdout:
                print(f"migrate --progress logline lacks '{fragment}':\n{result.stdout}")
                sys.exit(1)

        # A migration that throws ends the "Migrations" task as failed, not as done.
        plugin_extensions = (".dll", ".so", ".dylib")
        failing = next((name for name in os.listdir(plugins_dir)
                        if name.startswith("FailingMigrationFixture.") and name.endswith(plugin_extensions)), None)
        if failing is None:
            print(f"FailingMigrationFixture not found under {plugins_dir}")
            sys.exit(1)
        failing_dir = os.path.join(tmpdir, "failing-plugins")
        os.makedirs(failing_dir)
        for name in os.listdir(plugins_dir):
            stem, extension = os.path.splitext(name)
            if extension in plugin_extensions and stem.lower().endswith("plugin"):
                shutil.copy2(os.path.join(plugins_dir, name), os.path.join(failing_dir, name))
        shutil.copy2(os.path.join(plugins_dir, failing),
                     os.path.join(failing_dir, "FailingMigrationPlugin" + os.path.splitext(failing)[1]))
        failing_cmd = [dbtool, "--plugins-dir", failing_dir, "--connection-string", connection_string]
        try:
            run_command(base_cmd + ["hard-reset", "--yes"])
            result, _ = run_logged(["migrate", "--progress", "logline"], expected_exit=None, cmd_prefix=failing_cmd)
            if result.returncode == 0:
                print("migrate with a failing migration unexpectedly succeeded")
                sys.exit(1)
            if "failed Migrations" not in result.stdout or "done Migrations" in result.stdout:
                print(f"the failing migration did not end the Migrations task as failed:\n{result.stdout}")
                sys.exit(1)
        finally:
            # The failing fixture cannot be reset through its own plugins; the plain ones drop everything.
            run_command(base_cmd + ["hard-reset", "--yes"], check=False)
        run_command(base_cmd + ["migrate"])

        # A dry run draws its tasks to stderr, so stdout stays the plan.
        result, _ = run_logged(["migrate", "--dry-run"])
        if "begin " in result.stdout or "done " in result.stdout:
            print(f"migrate --dry-run wrote task lines to stdout:\n{result.stdout}")
            sys.exit(1)
        if "begin Recorder step" not in result.stderr:
            print(f"migrate --dry-run did not render the step on stderr:\n{result.stderr}")
            sys.exit(1)

        # The default animated style falls back to log lines when stdout is not a terminal.
        run_command(base_cmd + ["hard-reset", "--yes"])
        result, _ = run_logged(["migrate"])
        if "\x1b" in result.stdout:
            print(f"migrate wrote escape sequences to a piped stdout:\n{result.stdout!r}")
            sys.exit(1)
        if "begin Recorder step" not in result.stdout:
            print(f"migrate on a piped stdout did not fall back to log lines:\n{result.stdout}")
            sys.exit(1)

        # Without steps, apply prints only its summary line, no per-migration line.
        run_command(base_cmd + ["hard-reset", "--yes"])
        run_command(base_cmd + ["migrate"])
        run_command(base_cmd + ["rollback", "20230201000000"])
        applied_plain = run_command(base_cmd + ["apply", "20230201000000"])
        if "[1/1]" in applied_plain.stdout or "Applied migration 20230201000000" not in applied_plain.stdout:
            print(f"apply without steps printed an unexpected output:\n{applied_plain.stdout}")
            sys.exit(1)

        # With a step registered, --quiet still prints the manager's own messages.

        def fixture_named(prefix):
            return next((name for name in os.listdir(plugins_dir)
                         if name.startswith(prefix) and name.endswith(plugin_extensions)), None)

        recorder = fixture_named("PostMigrateRecorderPlugin.")
        seed = fixture_named("SeedRowFixture.")
        if recorder is None or seed is None:
            print(f"PostMigrateRecorderPlugin or SeedRowFixture not found under {plugins_dir}")
            sys.exit(1)
        seed_dir = os.path.join(tmpdir, "seed-plugins")
        os.makedirs(seed_dir)
        shutil.copy2(os.path.join(plugins_dir, recorder), os.path.join(seed_dir, recorder))
        shutil.copy2(os.path.join(plugins_dir, seed),
                     os.path.join(seed_dir, "SeedRowPlugin" + os.path.splitext(seed)[1]))
        seed_cmd = [dbtool, "--plugins-dir", seed_dir, "--connection-string", connection_string]
        try:
            run_command(seed_cmd + ["hard-reset", "--yes"])
            run_logged(["migrate"], cmd_prefix=seed_cmd)
            run_command(seed_cmd + ["rollback", "20240301000001"])
            run_command(seed_cmd + ["exec", """INSERT INTO "Categories" ("Id", "Name") VALUES (200, 'pre-existing')"""])
            result, _ = run_logged(["migrate", "--quiet"], cmd_prefix=seed_cmd)
            if "Skipped existing row:" not in result.stdout:
                print(f"migrate --quiet with a pre-migrate step lost the manager's message:\n{result.stdout}")
                sys.exit(1)
        finally:
            run_command(seed_cmd + ["hard-reset", "--yes"], check=False)


def with_sqlite_database(connection_string, database_path):
    """The SQLite connection string with its Database= attribute replaced by `database_path`."""
    parts = [part for part in connection_string.split(";")
             if part.strip() and not part.strip().lower().startswith("database=")]
    return ";".join(parts + [f"Database={database_path}"])


def file_fingerprint(path):
    """Bytes and modification time of a file, to prove that a command left it untouched."""
    with open(path, "rb") as f:
        return f.read(), os.stat(path).st_mtime_ns


SHARED_PROFILE_YAML = "profiles:\n  shared:\n    connectionString: \"DRIVER=SQLite3;Database=shared.db\"\n"


def write_text(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def make_layered_tree(tmpdir):
    """The shared-profiles deployment: <tmp>/acme/dbtool.yml (root: true, profile `shared`),
    <tmp>/acme/app-a/dbtool.yml (defaultPluginsDir: ./plugins) and a per-user file with profile
    `personal`. Returns the paths and an environment that runs dbtool as if installed in app-a."""
    tmpdir = os.path.realpath(tmpdir)
    acme = os.path.join(tmpdir, "acme")
    app_dir = os.path.join(acme, "app-a")
    user_dir = os.path.join(tmpdir, "user")
    os.makedirs(app_dir)
    os.makedirs(user_dir)
    tree = {
        "app_dir": app_dir,
        "root": os.path.join(acme, "dbtool.yml"),
        "app": os.path.join(app_dir, "dbtool.yml"),
        "user": os.path.join(user_dir, "dbtool.yml"),
        "env": dict(os.environ, LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR=user_dir,
                    LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR=app_dir),
    }
    write_text(tree["root"], "root: true\n" + SHARED_PROFILE_YAML)
    write_text(tree["app"], "defaultPluginsDir: ./plugins\n")
    write_text(tree["user"], "profiles:\n  personal:\n    connectionString: \"DRIVER=SQLite3;Database=personal.db\"\n")
    return tree


def test_layered_writes(dbtool, plugins_dir, connection_string):
    """Profile writes without --config land in the file that owns the profile: a new profile in
    the root file (or the per-user file when there is no root), an encrypted password in the
    profile's own file. SQLite only."""
    print("--- 12i. writes go to the owning dbtool.yml ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        tree = make_layered_tree(tmpdir)
        target = with_sqlite_database(connection_string, os.path.join(os.path.realpath(tmpdir), "layered.db"))

        def add_profile(name, *extra):
            return run_command([dbtool, "add-profile", "--name", name, "--connection-string", target,
                                "--no-password"] + list(extra), check=False, env=tree["env"])

        app_before = file_fingerprint(tree["app"])
        added = add_profile("n1")
        if added.returncode != 0 or "n1" not in read_text(tree["root"]) or "n1" in read_text(tree["user"]):
            print(f"add-profile did not write the new profile into the root file:\n{added.stderr}")
            sys.exit(1)
        if file_fingerprint(tree["app"]) != app_before:
            print("add-profile changed the app-a dbtool.yml")
            sys.exit(1)

        # --force replaces a profile in the non-root layer that owns it.
        write_text(tree["app"], "defaultPluginsDir: ./plugins\nprofiles:\n  local:\n"
                                "    connectionString: \"DRIVER=SQLite3;Database=old.db\"\n")
        root_before = file_fingerprint(tree["root"])
        replaced = add_profile("local", "--force")
        if replaced.returncode != 0 or "old.db" in read_text(tree["app"]) or "layered.db" not in read_text(tree["app"]):
            print(f"add-profile --force did not replace the profile in the app-a file:\n{replaced.stderr}")
            sys.exit(1)
        if file_fingerprint(tree["root"]) != root_before:
            print("add-profile --force of an app-a profile changed the root file")
            sys.exit(1)

        duplicate = add_profile("shared")
        if duplicate.returncode == 0 or tree["root"] not in duplicate.stderr:
            print(f"add-profile of an existing profile did not refuse, naming its file:\n{duplicate.stderr}")
            sys.exit(1)

        write_text(tree["app"], "defaultProfile: x\ndefaultPluginsDir: ./plugins\n")
        shadowed = add_profile("n3", "--set-default")
        if shadowed.returncode != 0 or "Warning:" not in shadowed.stderr or tree["app"] not in shadowed.stderr:
            print(f"add-profile --set-default did not warn that app-a's defaultProfile wins:\n{shadowed.stderr}")
            sys.exit(1)

        secret = "e2e-dummy-password"
        plaintext_root = ("root: true\n" + SHARED_PROFILE_YAML
                          + f"  pw:\n    connectionString: {yaml_quote(target)}\n    password: {yaml_quote(secret)}\n")
        write_text(tree["root"], plaintext_root)
        status = ["--plugins-dir", plugins_dir, "--profile", "pw", "status"]
        run_command([dbtool] + status, env=tree["env"])
        text = read_text(tree["root"])
        if "enc:" not in text or secret in text:
            print(f"the plaintext password was not encrypted in the root file:\n{text}")
            sys.exit(1)

        if os.name != "nt" and os.geteuid() != 0:
            write_text(tree["root"], plaintext_root)
            os.chmod(tree["root"], 0o444)
            try:
                unwritable = run_command([dbtool] + status, check=False, env=tree["env"])
            finally:
                os.chmod(tree["root"], 0o644)
            if unwritable.returncode != 0 or "Warning: could not encrypt" not in unwritable.stderr \
                    or tree["root"] not in unwritable.stderr:
                print(f"a read-only root file did not merely warn:\n{unwritable.stderr}")
                sys.exit(1)

        write_text(tree["root"], SHARED_PROFILE_YAML)
        added = add_profile("n2")
        if added.returncode != 0 or "n2" not in read_text(tree["user"]) or "n2" in read_text(tree["root"]):
            print(f"add-profile without a root file did not write into the per-user file:\n{added.stderr}")
            sys.exit(1)


def test_target_precedence(dbtool, plugins_dir, connection_string):
    """A dbtool.yml found by discovery whose defaultProfile names database A, and
    SQL_CONNECTION_STRING naming database B: the environment wins; without it, commands
    that write refuse the discovered target and leave A untouched, while read-only ones
    may still use it unless a loaded plugin has a post-init hook. SQLite only. Discovery starts
    at LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR, which stands in for the executable's directory."""
    print("--- 16. a discovered defaultProfile never receives a write ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        # The long form: dbtool prints discovered paths canonically, without 8.3 short names.
        tmpdir = os.path.realpath(tmpdir)
        project = os.path.join(tmpdir, "project")
        nested = os.path.join(project, "sub", "dir")
        os.makedirs(nested)
        database_a = os.path.join(tmpdir, "a.db")
        database_b = os.path.join(tmpdir, "b.db")
        database_c = os.path.join(tmpdir, "c.db")
        target_a = with_sqlite_database(connection_string, database_a)
        target_b = with_sqlite_database(connection_string, database_b)
        target_c = with_sqlite_database(connection_string, database_c)
        cfg_path = os.path.join(project, "dbtool.yml")
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write("defaultProfile: a\nprofiles:\n"
                    f"  a:\n    connectionString: {yaml_quote(target_a)}\n"
                    f"  c:\n    connectionString: {yaml_quote(target_c)}\n")

        # The shared plugins directory holds PostMigrateRecorderPlugin, whose post-init hook
        # makes every plugin-loading command refuse a discovered target; this copy has no hook.
        hook_free_plugins = os.path.join(tmpdir, "hook-free-plugins")
        os.makedirs(hook_free_plugins)
        for name in os.listdir(plugins_dir):
            stem, extension = os.path.splitext(name)
            if extension in (".dll", ".so", ".dylib") and stem.startswith("DummyMigration"):
                shutil.copy2(os.path.join(plugins_dir, name), os.path.join(hook_free_plugins, name))

        env_none = {k: v for k, v in os.environ.items()
                    if k not in ("SQL_CONNECTION_STRING", "ODBC_CONNECTION_STRING")}
        env_b = dict(env_none, SQL_CONNECTION_STRING=target_b)
        plugins = ["--plugins-dir", hook_free_plugins]

        def dbtool_in_tree(command, env, check=True, search=nested, plugin_args=plugins, input=None):
            return run_command([dbtool] + plugin_args + command, check=check, input=input,
                               env=dict(env, LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR=search))

        def expect_refusal(result, command, *fragments):
            missing = [f for f in ("refusing to run", cfg_path) + fragments if f not in result.stderr]
            if result.returncode == 0 or missing:
                print(f"dbtool {' '.join(command)} did not refuse the discovered target (missing {missing}):\n"
                      f"{result.stderr}")
                sys.exit(1)

        # A: migrated, then one migration rolled back, so its state differs from B's.
        run_command([dbtool] + plugins + ["--connection-string", target_a, "migrate"])
        run_command([dbtool] + plugins + ["--connection-string", target_a, "rollback", "20230102000000"])
        archive = os.path.join(tmpdir, "a.zip")
        run_command([dbtool] + plugins + ["--connection-string", target_a, "backup", "--output", archive])
        status_a = run_command([dbtool] + plugins + ["--connection-string", target_a, "status"]).stdout

        # The environment beats the discovered defaultProfile, and the writing command names its target.
        migrated = dbtool_in_tree(["migrate"], env_b)
        expected_target = f"Target: database {database_b} (from environment variable SQL_CONNECTION_STRING)"
        if expected_target not in migrated.stderr:
            print(f"migrate did not print its target '{expected_target}':\n{migrated.stderr}")
            sys.exit(1)
        applied_b = run_command([dbtool] + plugins + ["--connection-string", target_b, "list-applied"]).stdout
        if "Add Email Column" not in applied_b:
            print(f"migrate with SQL_CONNECTION_STRING did not migrate database B:\n{applied_b}")
            sys.exit(1)

        restored = dbtool_in_tree(["restore", "--input", archive], env_b)
        if expected_target not in restored.stderr:
            print(f"restore did not print its target '{expected_target}':\n{restored.stderr}")
            sys.exit(1)
        applied_b = run_command([dbtool] + plugins + ["--connection-string", target_b, "list-applied"]).stdout
        if "Add Email Column" in applied_b:
            print(f"restore with SQL_CONNECTION_STRING did not restore A's archive into database B:\n{applied_b}")
            sys.exit(1)

        # An explicit --config without --profile uses its defaultProfile even when the environment is set.
        from_config = run_command([dbtool] + plugins + ["--config", cfg_path, "status"], env=env_b).stdout
        if from_config != status_a:
            print(f"status --config did not read database A:\n{from_config}\nexpected:\n{status_a}")
            sys.exit(1)

        # A named profile is explicit, from --config or from the discovered file, and its origin is printed.
        for command, origin in ((["--config", cfg_path, "--profile", "c", "migrate"], "--config"),
                                (["--profile", "c", "migrate"], "discovered via executable directory")):
            named = dbtool_in_tree(command, env_b)
            expected = f"Target: database {database_c} (from profile 'c' from {cfg_path} ({origin}))"
            if expected not in named.stderr:
                print(f"dbtool {' '.join(command)} did not print '{expected}':\n{named.stderr}")
                sys.exit(1)

        # Without the environment, every writing command refuses the discovered target.
        before = file_fingerprint(database_a)
        for command in (["restore", "--input", archive], ["migrate"], ["rollback", "20230101000000"],
                        ["exec", "DELETE FROM schema_migrations"], ["hard-reset", "--yes"]):
            expect_refusal(dbtool_in_tree(command, env_none, check=False), command, "default profile 'a'")

        # A post-init hook may write, so with the recorder plugin even read-only commands and dry runs
        # refuse, before any hook runs; with an explicit target the hook runs.
        post_init_log = os.path.join(tmpdir, "post-init.log")
        env_hook = dict(env_none, LIGHTWEIGHT_TEST_POST_INIT_LOG=post_init_log)
        all_plugins = ["--plugins-dir", plugins_dir]
        for command in (["status"], ["list-applied"], ["migrate", "--dry-run"]):
            refused = dbtool_in_tree(command, env_hook, check=False, plugin_args=all_plugins)
            expect_refusal(refused, command, "PostMigrateRecorderPlugin", "post-init hook")
            if os.path.exists(post_init_log):
                print(f"dbtool {' '.join(command)} ran a post-init hook against the discovered target")
                sys.exit(1)
        if file_fingerprint(database_a) != before:
            print("a refused command changed database A")
            sys.exit(1)
        run_command([dbtool] + all_plugins + ["--config", cfg_path, "status"], env=env_hook)
        with open(post_init_log, encoding="utf-8") as f:
            if len([line for line in f if line.startswith("post-init")]) != 1:
                print("status with an explicit --config did not run the post-init hook once")
                sys.exit(1)

        # A dry run writes nothing, so without hooks it may use the discovered target.
        dbtool_in_tree(["migrate", "--dry-run"], env_none)

        # Read-only commands without hooks may still use it.
        status = dbtool_in_tree(["status"], env_none).stdout
        if status != status_a:
            print(f"status did not read database A through the discovered profile:\n{status}\nexpected:\n{status_a}")
            sys.exit(1)

        # A file without defaultProfile falls back to its first profile, and the refusal says so.
        fallback = os.path.join(tmpdir, "fallback")
        os.makedirs(fallback)
        with open(os.path.join(fallback, "dbtool.yml"), "w", encoding="utf-8") as f:
            f.write(f"profiles:\n  first:\n    connectionString: {yaml_quote(target_a)}\n")
        refused = dbtool_in_tree(["migrate"], env_none, check=False, search=fallback)
        if refused.returncode == 0 or "the first profile; the file sets no defaultProfile" not in refused.stderr:
            print(f"the refusal did not name the first-profile fallback:\n{refused.stderr}")
            sys.exit(1)

        # The plugin refusal comes before the password is asked for: stdin stays unread.
        prompting = os.path.join(tmpdir, "prompting")
        os.makedirs(prompting)
        with open(os.path.join(prompting, "dbtool.yml"), "w", encoding="utf-8") as f:
            f.write("defaultProfile: s\nprofiles:\n"
                    f"  s:\n    connectionString: {yaml_quote(target_a)}\n    secretRef: \"stdin:\"\n")
        refused = dbtool_in_tree(["status"], env_none, check=False, search=prompting, plugin_args=all_plugins,
                                 input="")
        if refused.returncode == 0 or "post-init hook" not in refused.stderr:
            print(f"status did not refuse the hook plugins before asking for the password:\n{refused.stderr}")
            sys.exit(1)

        # No target at all is not a discovered one: the run ends with the usual error, not a refusal.
        bare_cfg = os.path.join(tmpdir, "bare.yml")
        with open(bare_cfg, "w", encoding="utf-8") as f:
            f.write("profiles:\n  bare:\n    schema: \"main\"\n")
        untargeted = run_command([dbtool] + all_plugins + ["--config", bare_cfg, "--profile", "bare", "status"],
                                 check=False, env=env_none)
        if (untargeted.returncode == 0 or "No connection string provided" not in untargeted.stderr
                or "refusing to run" in untargeted.stderr):
            print(f"status without any target did not report the missing connection string:\n{untargeted.stderr}")
            sys.exit(1)
        if file_fingerprint(database_a) != before:
            print("a refused command changed database A")
            sys.exit(1)


def test_exit_code_after_compat_policy_plugin(dbtool, plugins_dir, connection_string):
    """A plugin that installs a compat policy at load time, as an application's migration
    plugin may, must not crash dbtool on exit: the process returns the command's own exit code,
    also when a later plugin fails to load."""
    print("--- 17. dbtool exits with the command's exit code after loading a compat-policy plugin ---")
    plugin_extensions = (".dll", ".so", ".dylib")
    fixture = next((name for name in os.listdir(plugins_dir)
                    if name.startswith("CompatPolicyFixture.") and name.endswith(plugin_extensions)), None)
    if fixture is None:
        print(f"CompatPolicyFixture not found under {plugins_dir}")
        sys.exit(1)
    extension = os.path.splitext(fixture)[1]

    def make_plugins_dir(parent, name, broken):
        directory = os.path.join(parent, name)
        os.makedirs(directory)
        shutil.copy2(os.path.join(plugins_dir, fixture), os.path.join(directory, "CompatPolicyPlugin" + extension))
        # An application may ship its own Lightweight.dll next to its plugins; mirror that layout.
        lightweight_dll = os.path.join(os.path.dirname(dbtool), "Lightweight.dll")
        if os.path.exists(lightweight_dll):
            shutil.copy2(lightweight_dll, directory)
        if broken:
            # Sorts after the policy plugin, so it fails to load once that one is loaded.
            with open(os.path.join(directory, "ZzzBrokenPlugin" + extension), "wb") as f:
                f.write(b"not a shared library")
        return [dbtool, "--plugins-dir", directory, "--connection-string", connection_string]

    with tempfile.TemporaryDirectory() as tmpdir:
        policy_cmd = make_plugins_dir(tmpdir, "policy", broken=False)
        broken_cmd = make_plugins_dir(tmpdir, "broken", broken=True)

        # (command, expected exit code, text expected in stdout + stderr)
        cases = [
            (policy_cmd + ["list-pending"], 0, "Compat Fixture Create Table"),
            (policy_cmd + ["status"], 0, "Registered migrations"),
            (policy_cmd + ["apply", "99990101000000"], 1, "Migration not found"),
            (policy_cmd + ["rollback", "99990101000000"], 1, "Migration not found"),
            (policy_cmd + ["mark-applied", "99990101000000"], 1, "Migration not found"),
            (broken_cmd + ["status"], 1, "Failed to load plugins"),
        ]
        for command, expected_exit_code, expected_text in cases:
            result = run_command(command, check=False)
            output = result.stdout + result.stderr
            if result.returncode != expected_exit_code:
                print(f"{' '.join(command)} exited {result.returncode}, expected {expected_exit_code}")
                sys.exit(1)
            if expected_text not in output or "Unexpected error" in output:
                print(f"{' '.join(command)}: expected '{expected_text}' and no 'Unexpected error':\n{output}")
                sys.exit(1)


def test_multi_schema_backup(dbtool, plugins_dir, connection_string, is_sqlite):
    """backup --schema a,b / --all-schemas write a multi-schema archive that restore recreates schema by schema."""
    base_cmd = [dbtool, "--plugins-dir", plugins_dir, "--connection-string", connection_string]

    print("--- 13a. multi-schema backup: option checks ---")
    both = run_command(base_cmd + ["backup", "--all-schemas", "--schema", "x", "--output", "unused.zip"], check=False)
    if both.returncode == 0 or "exclude each other" not in both.stderr:
        print(f"backup accepted --all-schemas together with --schema:\n{both.stderr}")
        sys.exit(1)
    listed = run_command(base_cmd + ["restore", "--input", "unused.zip", "--schema", "a,b"], check=False)
    if listed.returncode == 0 or "single --schema" not in listed.stderr:
        print(f"restore accepted a list of schemas:\n{listed.stderr}")
        sys.exit(1)

    ignored = run_command(base_cmd + ["status", "--all-schemas"], check=False)
    if "--all-schemas applies to backup only" not in ignored.stderr:
        print(f"status did not warn that it ignores --all-schemas:\n{ignored.stderr}")
        sys.exit(1)
    # --backup-to backs up like `backup`, so a migrating command with it honors --all-schemas.
    honored = run_command(base_cmd + ["migrate", "--dry-run", "--all-schemas", "--backup-to", "unused.zip"], check=False)
    if "--all-schemas applies to backup only" in honored.stderr:
        print(f"migrate --backup-to claimed to ignore --all-schemas:\n{honored.stderr}")
        sys.exit(1)

    if is_sqlite:
        print("--- 13b. --all-schemas on SQLite writes a single-schema archive ---")
        with tempfile.TemporaryDirectory() as tmpdir:
            archive = os.path.join(tmpdir, "all.zip")
            run_command(base_cmd + ["backup", "--all-schemas", "--output", archive])
            with zipfile.ZipFile(archive) as zf:
                metadata = json.loads(zf.read("metadata.json"))
            if metadata["format_version"] != "1.0" or "schema_names" in metadata:
                print(f"--all-schemas on SQLite did not write a single-schema archive: {metadata['format_version']}")
                sys.exit(1)
        return

    schemas = ("l7cli_core", "l7cli_extra")
    parent = f'"{schemas[0]}"."parent"'
    child = f'"{schemas[1]}"."child"'

    def drop_fixture():
        for table in (child, parent):
            run_command(base_cmd + ["exec", f"DROP TABLE IF EXISTS {table}"])
        for schema in reversed(schemas):
            run_command(base_cmd + ["exec", f'DROP SCHEMA IF EXISTS "{schema}"'])

    def row_count(table):
        """Returns the row count of table; a missing table fails the test."""
        return int(run_command(base_cmd + ["exec", f"SELECT COUNT(*) FROM {table}"]).stdout.strip())

    def orphan_rejected(expected_child_rows):
        """Checks that child exists with expected_child_rows rows, then that its foreign key rejects an orphan."""
        rows = row_count(child)
        if rows != expected_child_rows:
            print(f"{child} has {rows} rows, expected {expected_child_rows}")
            sys.exit(1)
        orphan = run_command(base_cmd + ["exec", f'INSERT INTO {child} ("id", "parent_id") VALUES (99, 42)'],
                             check=False)
        return orphan.returncode != 0

    def write_profile(cfg_path, profile_schema):
        """Writes a dbtool.yml whose profile 'multi' targets the test database with profile_schema."""
        profile_cs, profile_pwd = split_password(connection_string)
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write("profiles:\n"
                    "  multi:\n"
                    f"    connectionString: {yaml_quote(profile_cs)}\n"
                    f"    pluginsDir: {yaml_quote(plugins_dir)}\n"
                    f"    schema: {yaml_quote(profile_schema)}\n"
                    + ("    secretRef: \"env:LW_MULTI_SCHEMA_TEST_PASSWORD\"\n" if profile_pwd else ""))
        return dict(os.environ, LW_MULTI_SCHEMA_TEST_PASSWORD=profile_pwd)

    print("--- 13c. backup --schema core,extra writes a multi-schema archive ---")
    drop_fixture()
    for schema in schemas:
        run_command(base_cmd + ["exec", f'CREATE SCHEMA "{schema}"'])
    run_command(base_cmd + ["exec", f'CREATE TABLE {parent} ("id" INTEGER NOT NULL PRIMARY KEY, "name" VARCHAR(20))'])
    run_command(base_cmd + ["exec", f'CREATE TABLE {child} ("id" INTEGER NOT NULL PRIMARY KEY, "parent_id" INTEGER, '
                                    f'CONSTRAINT "fk_l7cli" FOREIGN KEY ("parent_id") REFERENCES {parent} ("id"))'])
    run_command(base_cmd + ["exec", f"INSERT INTO {parent} (\"id\", \"name\") VALUES (1, 'one'), (2, 'two')"])
    run_command(base_cmd + ["exec", f'INSERT INTO {child} ("id", "parent_id") VALUES (10, 1), (11, 2)'])

    with tempfile.TemporaryDirectory() as tmpdir:
        archive = os.path.join(tmpdir, "multi.zip")
        run_command(base_cmd + ["backup", "--schema", ",".join(schemas), "--output", archive])
        with zipfile.ZipFile(archive) as zf:
            metadata = json.loads(zf.read("metadata.json"))
            names = zf.namelist()
        if metadata["format_version"] != "2.0" or metadata.get("schema_names") != list(schemas):
            print(f"backup --schema a,b did not write a multi-schema archive: {metadata.get('schema_names')}")
            sys.exit(1)
        if not any(name.startswith(f"data/{schemas[1]}/child/") for name in names):
            print(f"multi-schema archive lacks the schema folder of its chunks: {names}")
            sys.exit(1)

        print("--- 13d. backup --all-schemas covers both schemas ---")
        all_archive = os.path.join(tmpdir, "all.zip")
        run_command(base_cmd + ["backup", "--all-schemas", "--schema-only", "--output", all_archive])
        with zipfile.ZipFile(all_archive) as zf:
            all_schemas = json.loads(zf.read("metadata.json")).get("schema_names", [])
        if not set(schemas) <= set(all_schemas):
            print(f"backup --all-schemas missed a schema: {all_schemas}")
            sys.exit(1)

        print("--- 13e. restore of a multi-schema archive refuses --schema ---")
        drop_fixture()
        refused = run_command(base_cmd + ["restore", "--input", archive, "--schema", "elsewhere"], check=False)
        if refused.returncode == 0 or "--schema cannot be used" not in refused.stderr:
            print(f"restore of a multi-schema archive accepted --schema:\n{refused.stderr}")
            sys.exit(1)

        print("--- 13f. restore recreates both schemas, the data and the cross-schema foreign key ---")
        run_command(base_cmd + ["restore", "--input", archive])
        after = os.path.join(tmpdir, "after.zip")
        run_command(base_cmd + ["backup", "--schema", ",".join(schemas), "--output", after])
        run_command(base_cmd + ["backup-diff", "--left", archive, "--right", after])
        if not orphan_rejected(2):
            print("the restored cross-schema foreign key does not reject an orphan row")
            sys.exit(1)

        print("--- 13f1. backup-diff --ignore-table takes a bare name in a multi-schema archive ---")
        run_command(base_cmd + ["exec", f'INSERT INTO {child} ("id", "parent_id") VALUES (12, 1)'])
        drifted = os.path.join(tmpdir, "drifted.zip")
        run_command(base_cmd + ["backup", "--schema", ",".join(schemas), "--output", drifted])
        differing = run_command(base_cmd + ["backup-diff", "--left", archive, "--right", drifted], check=False)
        if differing.returncode == 0:
            print("backup-diff did not report the drifted child table")
            sys.exit(1)
        run_command(base_cmd + ["backup-diff", "--left", archive, "--right", drifted, "--ignore-table", "child"])
        run_command(base_cmd + ["backup-diff", "--left", archive, "--right", drifted,
                                "--ignore-table", f"{schemas[1]}/child"])
        run_command(base_cmd + ["exec", f'DELETE FROM {child} WHERE "id" = 12'])

        print("--- 13g. a schema-only restore of a multi-schema archive recreates the foreign key ---")
        drop_fixture()
        run_command(base_cmd + ["restore", "--input", archive, "--schema-only"])
        if row_count(parent) != 0 or not orphan_rejected(0):
            print("schema-only restore did not recreate empty tables with the foreign key")
            sys.exit(1)

        print("--- 13h. a profile's schema that the archive holds is not applied ---")
        drop_fixture()
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        # SQL Server matches schema names case-insensitively, PostgreSQL exactly.
        is_postgres = "postgres" in connection_string.lower()
        env = write_profile(cfg_path, schemas[0] if is_postgres else schemas[0].upper())
        run_command([dbtool, "--config", cfg_path, "--profile", "multi", "restore", "--input", archive], env=env)
        if row_count(parent) != 2 or not orphan_rejected(2):
            print("restore through a profile with the archive's schema did not recreate the cross-schema foreign key")
            sys.exit(1)

        print("--- 13i. a profile's schema that the archive does not hold is refused ---")
        run_command(base_cmd + ["exec", f"INSERT INTO {parent} (\"id\", \"name\") VALUES (3, 'live')"])
        env = dict(write_profile(cfg_path, "restore_check"), DBTOOL_TRACE="1")
        refused = run_command([dbtool, "--config", cfg_path, "--profile", "multi", "restore", "--input", archive],
                              env=env, check=False)
        # Positive control for 13j: with a profile schema the probe runs and leaves its breadcrumb.
        if "probing the target's identifier case" not in refused.stderr:
            print(f"restore with a profile schema did not probe its target:\n{refused.stderr}")
            sys.exit(1)
        if refused.returncode == 0 or "the profile's schema 'restore_check' cannot be used" not in refused.stderr:
            print(f"restore accepted a profile schema the archive does not hold:\n{refused.stderr}")
            sys.exit(1)
        if row_count(parent) != 3:
            print("a refused restore changed the live tables")
            sys.exit(1)

        print("--- 13j. without a profile schema, a dry-run restore does not probe the target ---")
        traced = run_command(base_cmd + ["restore", "--dry-run", "--input", archive],
                             env=dict(os.environ, DBTOOL_TRACE="1"))
        if "probing the target's identifier case" in traced.stderr:
            print(f"restore --dry-run probed its target:\n{traced.stderr}")
            sys.exit(1)

    drop_fixture()


def test_skipped_seed_rows(base_cmd, dbtool, plugins_dir, connection_string):
    """An `Insert(...).IfNotExists(...)` seed row the database already holds is skipped, not an
    error: migrate and apply report one `Skipped existing row:` line per row plus the count in
    their summary line, exit 0, and leave the existing row as it was. Rows that are inserted,
    the first statement of the migration included, are never reported (dbtool holds its
    migration lock while it migrates)."""
    print("--- 18. IfNotExists inserts skip and report existing rows ---")
    run_command(base_cmd + ["hard-reset", "--yes"])

    plugin_extensions = (".dll", ".so", ".dylib")
    fixture = next((name for name in os.listdir(plugins_dir)
                    if name.startswith("SeedRowFixture.") and name.endswith(plugin_extensions)), None)
    if fixture is None:
        print(f"SeedRowFixture not found under {plugins_dir}")
        sys.exit(1)

    with tempfile.TemporaryDirectory() as tmpdir:
        shutil.copy2(os.path.join(plugins_dir, fixture),
                     os.path.join(tmpdir, "SeedRowPlugin" + os.path.splitext(fixture)[1]))
        seed_cmd = [dbtool, "--plugins-dir", tmpdir, "--connection-string", connection_string]
        seed_migration = "20240301000001"
        skipped_line = f"Skipped existing row: {seed_migration} Categories (Id=200)"

        def expect(output, text, what):
            if text not in output:
                print(f"{what}: expected '{text}' in:\n{output}")
                sys.exit(1)

        def expect_skips(output, count, what):
            if output.count("Skipped existing row:") != count:
                print(f"{what}: expected {count} skipped row(s) in:\n{output}")
                sys.exit(1)

        def stored_name(category_id):
            return run_command(seed_cmd + ["exec",
                                           f'SELECT "Name" FROM "Categories" WHERE "Id" = {category_id}']).stdout

        def pre_seed():
            # A row with the seed's key but different values is already there when the seed runs.
            run_command(seed_cmd + ["exec", """INSERT INTO "Categories" ("Id", "Name") VALUES (200, 'pre-existing')"""])

        try:
            # A fresh database: every row is inserted, none is reported.
            migrated = run_command(seed_cmd + ["migrate"]).stdout
            expect_skips(migrated, 0, "fresh migrate")
            expect(migrated, "Successfully applied 2 migration(s).", "fresh migrate")
            expect(stored_name(100), "Books", "the first seed row")
            expect(stored_name(201), "Toys", "the last seed row")

            # The first statement still inserts; only the pre-seeded row is skipped.
            run_command(seed_cmd + ["rollback", seed_migration])
            pre_seed()
            migrated = run_command(seed_cmd + ["migrate"]).stdout
            expect(migrated, skipped_line, "migrate")
            expect_skips(migrated, 1, "migrate")
            expect(migrated, "Successfully applied 1 migration(s), skipped 1 existing row(s).", "migrate")
            expect(stored_name(100), "Books", "the first seed row")
            expect(stored_name(200), "pre-existing", "the existing row")
            expect(stored_name(201), "Toys", "the last seed row")

            # apply reports the same way.
            rolled_back = run_command(seed_cmd + ["rollback", seed_migration]).stdout
            expect(rolled_back, f"Rolled back migration {seed_migration}.", "rollback")
            pre_seed()
            applied = run_command(seed_cmd + ["apply", seed_migration]).stdout
            expect(applied, skipped_line, "apply")
            expect_skips(applied, 1, "apply")
            expect(applied, f"Applied migration {seed_migration}, skipped 1 existing row(s).", "apply")

            # A dry run shows the conditional statement and reports nothing as skipped.
            run_command(seed_cmd + ["rollback", seed_migration])
            dry_run = run_command(seed_cmd + ["migrate", "--dry-run"]).stdout
            expect(dry_run, "WHERE NOT EXISTS (SELECT 1 FROM", "migrate --dry-run")
            expect_skips(dry_run, 0, "migrate --dry-run")
        finally:
            run_command(seed_cmd + ["hard-reset", "--yes"], check=False)


def expect_exit(result, returncode, *fragments):
    """Fails the run unless result exited with returncode and its output holds every fragment."""
    text = result.stdout + result.stderr
    missing = [f for f in fragments if f not in text]
    if result.returncode != returncode or missing:
        print(f"exit {result.returncode} (expected {returncode}), missing {missing}:\n{text}")
        sys.exit(1)


def test_restore_failure_report(dbtool, plugins_dir, connection_string):
    """A restore whose archive holds rows the database rejects keeps going, prints a "Restore failures"
    block naming the table, its failed rows and the reason, and exits non-zero."""
    print("--- 19. restore reports failed rows and exits non-zero ---")
    base_cmd = [dbtool, "--plugins-dir", plugins_dir, "--connection-string", connection_string]
    table = "l7cli_dup"

    def drop():
        run_command(base_cmd + ["exec", f'DROP TABLE IF EXISTS "{table}"'], check=False)

    drop()
    try:
        run_command(base_cmd + ["exec", f'CREATE TABLE "{table}" ("id" INTEGER NOT NULL, "name" VARCHAR(20))'])
        run_command(base_cmd + ["exec", f"INSERT INTO \"{table}\" (\"id\", \"name\") VALUES (1, 'a'), (1, 'b'), (2, 'c')"])
        with tempfile.TemporaryDirectory() as tmpdir:
            plain = os.path.join(tmpdir, "plain.zip")
            duplicated = os.path.join(tmpdir, "duplicated.zip")
            run_command(base_cmd + ["backup", "--output", plain, "--filter-tables", table])

            # The archive claims "id" is the primary key although it holds id 1 twice: the restored
            # table rejects the second row, every other row is restored.
            with zipfile.ZipFile(plain) as source, zipfile.ZipFile(duplicated, "w") as target:
                for item in source.infolist():
                    data = source.read(item.filename)
                    if item.filename == "metadata.json":
                        metadata = json.loads(data)
                        archived = metadata["schema"][0]
                        archived["columns"][0]["is_primary_key"] = True
                        archived["primary_keys"] = ["id"]
                        data = json.dumps(metadata).encode()
                    target.writestr(item.filename, data)

            result = run_command(base_cmd + ["restore", "--input", duplicated], check=False)
            expect_exit(result, 1, "Restore failures:", f"{table}: 1 rows failed", "Total: 1 rows failed in 1 tables")
            restored = int(run_command(base_cmd + ["exec", f'SELECT COUNT(*) FROM "{table}"']).stdout.strip())
            if restored != 2:
                print(f"expected the 2 rows that do not collide to be restored, found {restored}")
                sys.exit(1)

            # A clean archive prints no failure block and exits 0.
            clean = run_command(base_cmd + ["restore", "--input", plain])
            if "Restore failures:" in clean.stdout:
                print(f"a clean restore printed a failure block:\n{clean.stdout}")
                sys.exit(1)
    finally:
        drop()


def test_backup_requirement(dbtool, plugins_dir, connection_string):
    """A release that requires a backup makes migrate refuse (exit 3) until a backup taken after the
    last migration exists; backup, restore and migrate --backup-to record one; a dry run reports and
    does not refuse; only the work the command runs counts; --without-backup waives it and writes a
    waived row; a step that requires a backup counts too; two backups in a row compare equal; a restore
    of an archive that fails verification warns and records nothing."""
    print("--- 16. the backup requirement ---")
    plugin_extensions = (".dll", ".so", ".dylib")
    fixture = next((n for n in os.listdir(plugins_dir)
                    if n.startswith("BackupRequiredReleaseFixture.") and n.endswith(plugin_extensions)), None)
    if fixture is None:
        print(f"BackupRequiredReleaseFixture not found under {plugins_dir}")
        sys.exit(1)

    with tempfile.TemporaryDirectory() as tmpdir:
        scratch = os.path.join(tmpdir, "plugins")
        os.makedirs(scratch)
        for name in os.listdir(plugins_dir):
            stem, extension = os.path.splitext(name)
            if extension in plugin_extensions and stem.lower().endswith("plugin"):
                shutil.copy2(os.path.join(plugins_dir, name), os.path.join(scratch, name))
        shutil.copy2(os.path.join(plugins_dir, fixture),
                     os.path.join(scratch, "BackupRequiredReleasePlugin" + os.path.splitext(fixture)[1]))
        cmd = [dbtool, "--plugins-dir", scratch, "--connection-string", connection_string]

        def archive(name):
            return os.path.join(tmpdir, name)

        run_command(cmd + ["hard-reset", "--yes"])
        expect_exit(run_command(cmd + ["migrate", "--dry-run"], check=False), 0,
               "-- Backup required: release 3.0.0 requires a backup")
        refused = run_command(cmd + ["migrate"], check=False)
        expect_exit(refused, BACKUP_REQUIRED_EXIT_CODE, "release 3.0.0 requires a backup of this database taken after its last migration.",
               "dbtool migrate --backup-to <file>")
        if "Initial Migration" not in run_command(cmd + ["list-pending"]).stdout:
            print("a refused migrate applied migrations")
            sys.exit(1)

        # Scope: only the work the command runs counts.
        # apply of a migration whose release requires nothing is not held back while 3.0.0 is pending ...
        expect_exit(run_command(cmd + ["apply", "20230201000000"], check=False), 0)
        run_command(cmd + ["rollback", "20230201000000"])
        # ... nor is migrate-to-release up to a release that requires nothing ...
        expect_exit(run_command(cmd + ["migrate-to-release", "1.0.0"], check=False), 0)
        # ... nor a step whose predicate says no (the recorder step without LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP).
        quiet_log = os.path.join(tmpdir, "quiet-step.log")
        quiet_env = dict(os.environ, LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG=quiet_log)
        expect_exit(run_command(cmd + ["migrate-to-release", "2.0.0"], check=False, env=quiet_env), 0)
        if not os.path.exists(quiet_log):
            print("the step whose predicate says no did not run")
            sys.exit(1)
        run_command(cmd + ["rollback", "20230201000000"])

        # apply and migrate-to-release refuse the same way and name their argument in the suggestions.
        expect_exit(run_command(cmd + ["apply", "20240201000000"], check=False), BACKUP_REQUIRED_EXIT_CODE,
               "dbtool apply 20240201000000 --backup-to <file>", "dbtool apply 20240201000000 --without-backup")
        expect_exit(run_command(cmd + ["migrate-to-release", "3.0.0"], check=False), BACKUP_REQUIRED_EXIT_CODE,
               "dbtool migrate-to-release 3.0.0 --backup-to <file>")
        if "Backup Required Fixture Migration" in run_command(cmd + ["list-applied"]).stdout:
            print("a refused apply or migrate-to-release applied the fixture migration")
            sys.exit(1)

        # The refusal names --without-backup as the last option; combined with --backup-to it is an error;
        # a dry run with it writes nothing.
        expect_exit(run_command(cmd + ["migrate"], check=False), BACKUP_REQUIRED_EXIT_CODE, "dbtool migrate --without-backup")
        expect_exit(run_command(cmd + ["migrate", "--without-backup", "--backup-to", archive("x.zip")], check=False), 1,
               "--without-backup")
        rows_before = run_command(cmd + ["exec", 'SELECT COUNT(*) FROM "schema_backups"'], check=False).stdout
        expect_exit(run_command(cmd + ["migrate", "--dry-run", "--without-backup"], check=False), 0,
                    "a real run migrates without one and records the waiver")
        expect_exit(run_command(cmd + ["migrate", "--dry-run", "--backup-to", archive("y.zip")], check=False), 0,
                    "a real run backs up first")
        if run_command(cmd + ["exec", 'SELECT COUNT(*) FROM "schema_backups"'], check=False).stdout != rows_before:
            print("a dry run with --without-backup wrote a row")
            sys.exit(1)

        # A backup taken now covers the database; migrate then applies the release.
        expect_exit(run_command(cmd + ["backup", "--output", archive("a.zip")], check=False), 0)
        expect_exit(run_command(cmd + ["migrate"], check=False), 0)
        if "Backup Required Fixture Migration" not in run_command(cmd + ["list-applied"]).stdout:
            print("migrate did not apply the release after the backup")
            sys.exit(1)
        # Already past the release: migrate-to-release has nothing to do and takes no backup.
        expect_exit(run_command(cmd + ["migrate-to-release", "1.0.0", "--backup-to", archive("z.zip")], check=False), 0,
                    "No backup was taken")
        if os.path.exists(archive("z.zip")):
            print("migrate-to-release past its release still wrote a backup")
            sys.exit(1)

        # Rolled back, the last migration changed: a.zip no longer counts.
        run_command(cmd + ["rollback", "20240201000000"])
        expect_exit(run_command(cmd + ["migrate"], check=False), BACKUP_REQUIRED_EXIT_CODE, "The newest backup (a.zip,",
               "does not match the current migration history.")

        # --backup-to backs up, records and migrates in one call.
        expect_exit(run_command(cmd + ["migrate", "--backup-to", archive("b.zip")], check=False), 0)
        if not os.path.exists(archive("b.zip")):
            print("migrate --backup-to wrote no archive")
            sys.exit(1)

        # A restore records the archive it restored: the database equals b.zip again. (Rolling back only
        # the fixture would already match b.zip's state, so two rollbacks make the newest record stale.)
        run_command(cmd + ["rollback", "20240201000000"])
        run_command(cmd + ["rollback", "20230201000000"])
        expect_exit(run_command(cmd + ["migrate"], check=False), BACKUP_REQUIRED_EXIT_CODE)
        run_command(cmd + ["restore", "--input", archive("b.zip")])
        expect_exit(run_command(cmd + ["migrate"], check=False), 0)
        kinds = run_command(cmd + ["exec", 'SELECT "kind" FROM "schema_backups" ORDER BY "id"']).stdout
        if "restore" not in kinds:
            print(f"restore wrote no record:\n{kinds}")
            sys.exit(1)

        # Two backups in a row compare equal: the record the first wrote is not in the second.
        run_command(cmd + ["backup", "--output", archive("c.zip")])
        run_command(cmd + ["backup", "--output", archive("d.zip")])
        expect_exit(run_command(cmd + ["backup-diff", "--left", archive("c.zip"), "--right", archive("d.zip")], check=False), 0)

        # A step that requires a backup counts like a release: with nothing pending, d.zip covers the
        # database and migrate runs the step; once the state changed, both reasons are named and the
        # refused command runs no step.
        log_path = os.path.join(tmpdir, "steps.log")
        env = dict(os.environ, LIGHTWEIGHT_TEST_PRE_MIGRATE_LOG=log_path, LIGHTWEIGHT_TEST_PRE_MIGRATE_REQUIRES_BACKUP="1")
        expect_exit(run_command(cmd + ["migrate"], check=False, env=env), 0)
        os.remove(log_path)
        run_command(cmd + ["rollback", "20240201000000"])
        expect_exit(run_command(cmd + ["migrate"], check=False, env=env), BACKUP_REQUIRED_EXIT_CODE,
               'release 3.0.0 and step "Recorder step" require a backup')
        if os.path.exists(log_path):
            print("a refused migrate ran its pre-migrate step")
            sys.exit(1)

        # --without-backup migrates, warns and writes a waived row; the waiver satisfies nothing later.
        waived = run_command(cmd + ["migrate", "--without-backup"], check=False)
        expect_exit(waived, 0, "Migrating without a backup although release 3.0.0 requires one")
        kinds = run_command(cmd + ["exec", 'SELECT "kind" FROM "schema_backups" ORDER BY "id"']).stdout
        if "waived" not in kinds:
            print(f"--without-backup wrote no waived row:\n{kinds}")
            sys.exit(1)
        run_command(cmd + ["rollback", "20240201000000"])
        expect_exit(run_command(cmd + ["migrate"], check=False), BACKUP_REQUIRED_EXIT_CODE)
        run_command(cmd + ["migrate", "--backup-to", archive("f.zip")])

        # An archive that fails verification (here: its checksums.json removed) still restores, but the
        # restore warns, exits 0 and is not recorded.
        run_command(cmd + ["backup", "--output", archive("g.zip")])
        with zipfile.ZipFile(archive("g.zip")) as source, zipfile.ZipFile(archive("unverified.zip"), "w") as target:
            for item in source.infolist():
                if item.filename != "checksums.json":
                    target.writestr(item, source.read(item.filename))
        count_records = ["exec", 'SELECT COUNT(*) FROM "schema_backups"']
        records_before = run_command(cmd + count_records).stdout
        expect_exit(run_command(cmd + ["restore", "--input", archive("unverified.zip")], check=False), 0,
               "Warning: The archive failed verification",
               "the restore is not recorded and cannot satisfy a migration backup requirement.")
        if run_command(cmd + count_records).stdout != records_before:
            print("the restore of an archive that failed verification was recorded")
            sys.exit(1)
        run_command(cmd + ["hard-reset", "--yes"])


def test_unwritable_backup_records(dbtool, plugins_dir, connection_string):
    """A schema_backups the user cannot write (SQLite: a view of that name): backup and migrate --backup-to
    succeed and warn that the backup cannot satisfy a requirement."""
    print("--- 17. a schema_backups that cannot be written ---")
    cmd = [dbtool, "--plugins-dir", plugins_dir, "--connection-string", connection_string]

    with tempfile.TemporaryDirectory() as tmpdir:
        run_command(cmd + ["hard-reset", "--yes"])
        run_command(cmd + ["exec", 'CREATE VIEW "schema_backups" AS SELECT 1 AS "id"'])
        try:
            expect_exit(run_command(cmd + ["backup", "--output", os.path.join(tmpdir, "e.zip")], check=False), 0,
                   "Warning: The backup could not be recorded in schema_backups")
            # Nothing requires a backup here, so the run goes on, and still says the backup does not count.
            expect_exit(run_command(cmd + ["migrate", "--backup-to", os.path.join(tmpdir, "h.zip")], check=False), 0,
                   "Warning: The backup could not be recorded in schema_backups")
        finally:
            run_command(cmd + ["exec", 'DROP VIEW "schema_backups"'], check=False)
        run_command(cmd + ["hard-reset", "--yes"])


def main():
    parser = argparse.ArgumentParser(description="Test dbtool integration")
    parser.add_argument("--dbtool", required=True, help="Path to dbtool executable")
    parser.add_argument("--plugins-dir", required=True, help="Path to plugins directory")
    parser.add_argument("--connection-string", help="Database connection string")
    parser.add_argument("--test-env", help="Use connection string from .test-env.yml (e.g., sqlite3, mssql, postgres)")
    args, _ = parser.parse_known_args()
    # Several tests run dbtool from another working directory, where a relative path would no
    # longer find it or its plugins.
    args.dbtool = os.path.abspath(args.dbtool)
    args.plugins_dir = os.path.abspath(args.plugins_dir)

    # dbtool merges every dbtool.yml above its executable with the per-user file, so a developer's
    # own ~/.config/dbtool/dbtool.yml, or a stray dbtool.yml above the build tree, would leak into
    # every run. Both default to empty directories; a case overrides them through its own `env`.
    isolation = tempfile.TemporaryDirectory()
    atexit.register(isolation.cleanup)
    for variable, folder in (("LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR", "user"),
                             ("LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR", "search")):
        os.environ[variable] = os.path.join(isolation.name, folder)
        os.makedirs(os.environ[variable])

    # Determine connection string: --test-env takes precedence over --connection-string
    if args.test_env:
        connection_string = load_connection_string_from_test_env(args.test_env)
    elif args.connection_string:
        connection_string = args.connection_string
    else:
        print("Error: Either --connection-string or --test-env is required", file=sys.stderr)
        sys.exit(1)

    # Define base command
    base_cmd = [args.dbtool, "--plugins-dir", args.plugins_dir, "--connection-string", connection_string]

    # Clean up previous run if any
    # Only for SQLite, we can remove the database file.
    # For others, we assume the environment is clean or we might need explicit drop logic.
    is_sqlite = "sqlite" in connection_string.lower()
    db_path = None
    if is_sqlite and "Database=" in connection_string:
        for part in connection_string.split(";"):
            if part.strip().startswith("Database="):
                db_path = part.split("=", 1)[1].strip()
                break

    if db_path and os.path.exists(db_path):
        try:
            os.remove(db_path)
            print(f"Removed existing test database: {db_path}")
        except OSError as e:
            print(f"Warning: Failed to remove test database {db_path}: {e}")

    print("--- 1. List Pending ---")
    output = run_command(base_cmd + ["list-pending"]).stdout
    if "Initial Migration" not in output:
        print("Failed to list pending migrations")
        sys.exit(1)

    print("--- 2. Migrate ---")
    run_command(base_cmd + ["migrate"])

    print("--- 3. List Applied ---")
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Initial Migration" not in output or "Add Email Column" not in output:
        print("Failed to list applied migrations (Plugin 1)")
        sys.exit(1)
    if "Second Plugin Migration" not in output:
        print("Failed to list applied migrations (Plugin 2)")
        sys.exit(1)

    print("--- 4. Rollback ---")
    run_command(base_cmd + ["rollback", "20230102000000"])

    print("--- 5. Verify Rollback ---")
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Initial Migration" not in output or "Add Email Column" in output:
        print("Rollback verification failed")
        sys.exit(1)

    print("--- 6. Re-Apply ---")
    run_command(base_cmd + ["apply", "20230102000000"])

    print("--- 7. Final Verification ---")
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Add Email Column" not in output:
        print("Final verification failed")
        sys.exit(1)

    print("--- 7a. Releases ---")
    releases_output = run_command(base_cmd + ["releases"]).stdout
    if "1.0.0" not in releases_output or "2.0.0" not in releases_output:
        print(f"Releases command did not list expected versions:\n{releases_output}")
        sys.exit(1)

    print("--- 7b. Rollback-to-release ---")
    run_command(base_cmd + ["rollback-to-release", "1.0.0"])
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Second Plugin Migration" in output:
        print(f"rollback-to-release did not revert migrations past 1.0.0:\n{output}")
        sys.exit(1)
    if "Add Email Column" not in output:
        print(f"rollback-to-release 1.0.0 incorrectly reverted a migration inside the release:\n{output}")
        sys.exit(1)

    print("--- 7b1. migrate-to-release dry-run ---")
    dry_result = run_command(base_cmd + ["migrate-to-release", "2.0.0", "--dry-run"])
    if "Second Plugin Migration" not in dry_result.stdout:
        print(f"migrate-to-release --dry-run did not preview the plugin 2 migration:\n{dry_result.stdout}")
        sys.exit(1)
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Second Plugin Migration" in output:
        print(f"migrate-to-release --dry-run unexpectedly applied the plugin 2 migration:\n{output}")
        sys.exit(1)

    print("--- 7b2. migrate-to-release with unknown release ---")
    bad_result = run_command(base_cmd + ["migrate-to-release", "does-not-exist"], check=False)
    if bad_result.returncode == 0:
        print("migrate-to-release with unknown version unexpectedly succeeded")
        sys.exit(1)
    if "not declared" not in bad_result.stderr:
        print(f"migrate-to-release error message did not mention 'not declared':\n{bad_result.stderr}")
        sys.exit(1)

    print("--- 7b3. migrate-to-release when already at release ---")
    same_result = run_command(base_cmd + ["migrate-to-release", "1.0.0"])
    if "already at or past" not in same_result.stdout:
        print(f"migrate-to-release at boundary did not report already-at-target:\n{same_result.stdout}")
        sys.exit(1)
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Second Plugin Migration" in output:
        print(f"migrate-to-release at boundary should not advance state:\n{output}")
        sys.exit(1)

    print("--- 7b4. migrate-to-release happy path ---")
    run_command(base_cmd + ["migrate-to-release", "2.0.0"])
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Second Plugin Migration" not in output:
        print(f"migrate-to-release 2.0.0 did not advance to plugin 2 migration:\n{output}")
        sys.exit(1)

    print("--- 7c. Re-apply after release rollback ---")
    run_command(base_cmd + ["migrate"])
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Second Plugin Migration" not in output:
        print(f"Re-migrate after rollback-to-release did not re-apply plugin 2 migration:\n{output}")
        sys.exit(1)

    print("--- 8. Schema-only Backup ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        schema_zip = os.path.join(tmpdir, "schema.zip")
        run_command(base_cmd + ["backup", "--schema-only", "--output", schema_zip])
        if not os.path.exists(schema_zip):
            print("Schema-only backup did not produce an output file")
            sys.exit(1)

        with zipfile.ZipFile(schema_zip) as zf:
            names = zf.namelist()

        if "metadata.json" not in names:
            print(f"Schema-only backup missing metadata.json (entries: {names})")
            sys.exit(1)
        if "checksums.json" in names:
            print(f"Schema-only backup unexpectedly contains checksums.json (entries: {names})")
            sys.exit(1)
        data_entries = [n for n in names if n.startswith("data/")]
        if data_entries:
            print(f"Schema-only backup unexpectedly contains data entries: {data_entries}")
            sys.exit(1)

        print("--- 9. Schema-only Dry-run Backup ---")
        dry_output = run_command(base_cmd + ["backup", "--schema-only", "--dry-run",
                                             "--output", os.path.join(tmpdir, "discard.zip")]).stdout
        if "schema only" not in dry_output.lower():
            print(f"Dry-run output did not mention schema-only backup:\n{dry_output}")
            sys.exit(1)

    # Hard-reset must drop the advisory-lock bookkeeping table on backends that
    # use one (SQLite). Regression guard against the lock table being mistaken
    # for user data and ending up in the preserved-tables list. The acquire that
    # hard-reset itself performs proves the table can be (re)created cleanly
    # afterwards — and that releasing a lock whose table got dropped under the
    # holder doesn't error out (idempotent-Release contract).
    print("--- 10. Hard-reset clears bookkeeping tables ---")
    hard_reset_output = run_command(base_cmd + ["hard-reset", "--yes"]).stdout
    if "preserved" in hard_reset_output.lower() and "lightweight_locks" in hard_reset_output.lower():
        print(f"hard-reset reported _lightweight_locks as preserved (should be dropped):\n{hard_reset_output}")
        sys.exit(1)
    # Re-running migrate after hard-reset must work (the lock table can be recreated).
    run_command(base_cmd + ["migrate"])
    output = run_command(base_cmd + ["list-applied"]).stdout
    if "Initial Migration" not in output:
        print(f"Re-migrate after hard-reset did not re-apply migrations:\n{output}")
        sys.exit(1)

    # `--schema <NAME>` is honored by migrate on PostgreSQL via post-connect
    # `SET search_path`. On SQL Server / SQLite the flag is parsed but the
    # migration runner relies on the login's server-side DEFAULT_SCHEMA (or
    # there's no schema concept at all), so we only assert the live effect on
    # Postgres. The flag must still parse cleanly on every backend.
    print("--- 11. --schema flag parses on every backend ---")
    schema_help = run_command([args.dbtool, "--help"]).stdout
    if "--schema" not in schema_help:
        print(f"--schema flag missing from --help:\n{schema_help}")
        sys.exit(1)

    is_postgres = "postgres" in connection_string.lower() or "postgresql" in connection_string.lower()
    if is_postgres:
        print("--- 12. --schema lasa lands schema_migrations in lasa (Postgres) ---")
        # Tear down, then create a fresh `lasa_test` schema and migrate into it.
        # We use a dedicated schema name so this test does not stomp on any
        # `lasa` the operator may already have.
        run_command(base_cmd + ["hard-reset", "--yes"])
        run_command(base_cmd + ["exec", 'DROP SCHEMA IF EXISTS "lasa_test" CASCADE'])
        run_command(base_cmd + ["exec", 'CREATE SCHEMA "lasa_test"'])

        schema_cmd = base_cmd + ["--schema", "lasa_test"]
        run_command(schema_cmd + ["migrate"])

        # Confirm the migration history table lives in lasa_test, not public.
        in_lasa = run_command(base_cmd + [
            "exec",
            "SELECT 1 FROM information_schema.tables "
            "WHERE table_schema='lasa_test' AND table_name='schema_migrations'"
        ]).stdout
        if "1" not in in_lasa:
            print(f"schema_migrations did not land in lasa_test schema:\n{in_lasa}")
            sys.exit(1)

        # And clean up after ourselves.
        run_command(base_cmd + ["exec", 'DROP SCHEMA IF EXISTS "lasa_test" CASCADE'])
    else:
        print("--- 12. --schema effect check skipped: only Postgres has session-level default schema ---")

    print("--- 12a. list-profiles enumerates the config file ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(
                "defaultProfile: prod\n"
                "profiles:\n"
                "  prod:\n"
                "    schema: dbo\n"
                # Brace-quoted, as ToConnectionString() renders a password carrying a `;`: the
                # redaction must mask the whole value rather than stop at the embedded `;`.
                "    connectionString: \"DRIVER=SQLite3;Database=prod.db;PWD={se;cret};UID=me\"\n"
                "  dev:\n"
                "    connectionString: \"DRIVER=SQLite3;Database=dev.db\"\n"
            )

        # list-profiles does not open a database, so the --plugins-dir + --connection-string
        # injected by `base_cmd` are irrelevant here. Build the invocation from scratch so
        # the test mirrors the real "I want to see what profiles I have" UX.
        list_result = run_command([args.dbtool, "--config", cfg_path, "list-profiles"])
        out = list_result.stdout
        if "prod" not in out or "dev" not in out:
            print(f"list-profiles output missing expected profile names:\n{out}")
            sys.exit(1)
        # Exactly one row should carry the default marker.
        default_lines = [line for line in out.splitlines() if line.startswith("prod") and "*" in line]
        if len(default_lines) != 1:
            print(f"list-profiles did not mark exactly one profile as default:\n{out}")
            sys.exit(1)
        if "cret" in out:
            print(f"list-profiles leaked password value in output:\n{out}")
            sys.exit(1)
        if "PWD=***" not in out:
            print(f"list-profiles did not redact PWD= value (expected PWD=***):\n{out}")
            sys.exit(1)

        # A missing --config file must be a hard error.
        missing_cfg = os.path.join(tmpdir, "does-not-exist.yml")
        missing_result = run_command([args.dbtool, "--config", missing_cfg, "list-profiles"],
                                     check=False)
        if missing_result.returncode == 0:
            print("list-profiles with missing --config unexpectedly succeeded")
            sys.exit(1)

    print("--- 12b. defaultPluginsDir as list deduplicates same-named plugins by mtime ---")
    # Verifies the multi-directory `defaultPluginsDir` form:
    #   - Two directories that contain the *same* plugin filename must
    #     resolve to a single load (filename-based dedup).
    #   - When the user passes --verbose, the shadowed copy is reported
    #     on stderr so the choice is debuggable.
    # The original `args.plugins_dir` (single directory) is the source of
    # the real plugin DLLs; we copy each file into two fresh directories
    # and bump one copy's mtime forward so the "newer wins" branch is
    # exercised deterministically.
    import shutil
    import time
    plugin_exts = (".dll", ".so", ".dylib")
    src_plugins = [p for p in os.listdir(args.plugins_dir)
                   if p.lower().endswith(plugin_exts)
                   and os.path.isfile(os.path.join(args.plugins_dir, p))]
    if not src_plugins:
        print(f"No plugin files found under {args.plugins_dir} — cannot run multi-dir test")
        sys.exit(1)

    with tempfile.TemporaryDirectory() as multi_tmpdir:
        dir_old = os.path.join(multi_tmpdir, "old")
        dir_new = os.path.join(multi_tmpdir, "new")
        os.makedirs(dir_old)
        os.makedirs(dir_new)
        for name in src_plugins:
            shutil.copy2(os.path.join(args.plugins_dir, name), os.path.join(dir_old, name))
            shutil.copy2(os.path.join(args.plugins_dir, name), os.path.join(dir_new, name))
        # Push the "new" copies' mtime forward so they unambiguously win
        # the newest-mtime tiebreak, regardless of filesystem mtime resolution.
        future = time.time() + 120
        for name in src_plugins:
            os.utime(os.path.join(dir_new, name), (future, future))

        cfg_path = os.path.join(multi_tmpdir, "dbtool.yml")
        # Single-quoted YAML strings treat backslashes literally — required
        # because Windows paths and connection strings (e.g. the LocalDB
        # `(LocalDB)\MSSQLLocalDB` instance) contain backslashes that a
        # double-quoted scalar would parse as escape sequences.
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(
                "defaultProfile: prod\n"
                "defaultPluginsDir:\n"
                f"  - '{dir_old}'\n"
                f"  - '{dir_new}'\n"
                "profiles:\n"
                "  prod:\n"
                f"    connectionString: '{connection_string}'\n"
            )

        # `list-pending` is a read-only query that exits 0 regardless of
        # whether anything is pending — what we actually want to assert is
        # that loading the same plugin filename from two directories
        # produces *no* duplicate-plugin failure and that the verbose
        # output names the older copy as shadowed.
        multi_result = run_command(
            [args.dbtool, "--config", cfg_path, "--verbose", "list-pending"],
            check=False,
        )
        if multi_result.returncode != 0:
            print(f"multi-dir list-pending failed:\n{multi_result.stdout}\n---\n{multi_result.stderr}")
            sys.exit(1)
        # The verbose log must attribute the shadow to the older directory.
        if "shadowed by" not in multi_result.stderr:
            print(f"--verbose did not report shadowed plugin on stderr:\n{multi_result.stderr}")
            sys.exit(1)
        # The winner must be the newer copy; dbtool prints the load path.
        if dir_new not in multi_result.stdout:
            print(f"newer plugin copy was not the one loaded:\n{multi_result.stdout}")
            sys.exit(1)

    print("--- 12c. layered dbtool.yml discovery ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        tree = make_layered_tree(tmpdir)
        listed = run_command([args.dbtool, "list-profiles"], env=tree["env"]).stdout
        missing = [f for f in ("shared", tree["app"], tree["root"], "(root)") if f not in listed]
        if missing or "personal" in listed or listed.index(tree["app"]) > listed.index(tree["root"]):
            print(f"list-profiles did not merge the layers nearest first, stopping at root (missing {missing}):\n"
                  f"{listed}")
            sys.exit(1)
        root_line = next(line for line in listed.splitlines() if line.strip().startswith(tree["root"]))
        if "(root)" not in root_line:
            print(f"list-profiles did not mark {tree['root']} as the root:\n{listed}")
            sys.exit(1)
        # app-a's relative defaultPluginsDir resolves against app-a, not the working directory.
        if os.path.join(tree["app_dir"], "plugins") not in listed:
            print(f"the shared profile does not use app-a's plugins directory:\n{listed}")
            sys.exit(1)

        write_text(tree["root"], SHARED_PROFILE_YAML)
        listed = run_command([args.dbtool, "list-profiles"], env=tree["env"]).stdout
        if "personal" not in listed or tree["user"] not in listed \
                or listed.index(tree["user"]) < listed.index(tree["root"]):
            print(f"without root: true, the user file was not listed last:\n{listed}")
            sys.exit(1)

        # The working directory is no longer searched.
        cwd_dir = os.path.join(tmpdir, "cwd")
        os.makedirs(cwd_dir)
        write_text(os.path.join(cwd_dir, "dbtool.yml"),
                   "profiles:\n  cwdonly:\n    connectionString: \"DRIVER=SQLite3;Database=cwd.db\"\n")
        listed = run_command([args.dbtool, "list-profiles"], cwd=cwd_dir, env=tree["env"]).stdout
        if "cwdonly" in listed:
            print(f"list-profiles still read the dbtool.yml in the working directory:\n{listed}")
            sys.exit(1)

    print("--- 12d. add-profile stores an encrypted password ---")
    profile_cs, profile_pwd = split_password(connection_string)
    secret = profile_pwd or "e2e-dummy-password"
    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_path = os.path.join(tmpdir, "nested", "dbtool.yml")
        add_cmd = [args.dbtool, "--config", cfg_path, "add-profile", "--name", "e2e",
                   "--connection-string", profile_cs, "--plugins-dir", args.plugins_dir]
        run_command(add_cmd, input=secret + "\n")
        with open(cfg_path, encoding="utf-8") as f:
            stored = f.read()
        if "enc:" not in stored or secret in stored:
            print(f"add-profile did not store an encrypted password:\n{stored}")
            sys.exit(1)
        listed = run_command([args.dbtool, "--config", cfg_path, "list-profiles"]).stdout
        if "encrypted" not in listed:
            print(f"list-profiles does not report the password as encrypted:\n{listed}")
            sys.exit(1)
        # The encrypted profile must actually connect.
        run_command([args.dbtool, "--config", cfg_path, "--profile", "e2e", "list-applied"])

        duplicate = run_command(add_cmd, input=secret + "\n", check=False)
        if duplicate.returncode == 0 or "already exists" not in duplicate.stderr:
            print(f"add-profile accepted a duplicate name:\n{duplicate.stderr}")
            sys.exit(1)
        run_command(add_cmd + ["--force", "--set-default"], input=secret + "\n")
        with open(cfg_path, encoding="utf-8") as f:
            if f.read().count("\"e2e\":") != 1:
                print("add-profile --force did not replace the existing profile")
                sys.exit(1)

        on_argv = run_command(add_cmd + ["--force", "--password", "hunter2"], input="", check=False)
        if on_argv.returncode == 0 or "never accepted on the command line" not in on_argv.stderr:
            print(f"add-profile accepted a password on the command line:\n{on_argv.stderr}")
            sys.exit(1)

    print("--- 12d2. add-profile stores relative directories as absolute paths ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        # The file lives in another directory than the one the directories were typed in, where a
        # relative value would resolve against the file instead.
        cfg_path = os.path.join(tmpdir, "nested", "dbtool.yml")
        run_command([args.dbtool, "--config", cfg_path, "add-profile", "--name", "rel", "--no-password",
                     "--connection-string", profile_cs, "--plugins-dir", "rel-plugins",
                     "--backup-dir", "rel-backups"], cwd=tmpdir)
        with open(cfg_path, encoding="utf-8") as f:
            stored = f.read()
        for key, name in (("pluginsDir", "rel-plugins"), ("backupDir", "rel-backups")):
            match = re.search(rf'^\s*{key}:\s*"?(.*?)"?\s*$', stored, re.MULTILINE)
            value = match.group(1).replace("\\\\", "\\") if match else ""
            expected = os.path.join(tmpdir, name)
            if not os.path.isabs(value) or os.path.realpath(value) != os.path.realpath(expected):
                print(f"add-profile stored {key} as {value!r} instead of {expected!r}:\n{stored}")
                sys.exit(1)

    if is_sqlite:
        test_layered_writes(args.dbtool, args.plugins_dir, connection_string)

    print("--- 12h. backupDir / defaultBackupDir decide where backup writes ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        folder = os.path.join(tmpdir, "bk", "prod")  # does not exist yet: backup must create it
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        add_cmd = [args.dbtool, "--config", cfg_path, "add-profile", "--name", "e2e",
                   "--connection-string", profile_cs, "--plugins-dir", args.plugins_dir,
                   "--backup-dir", folder]
        run_command(add_cmd, input=secret + "\n")
        with open(cfg_path, encoding="utf-8") as f:
            if "backupDir" not in f.read():
                print("add-profile --backup-dir did not write backupDir")
                sys.exit(1)
        listed = run_command([args.dbtool, "--config", cfg_path, "list-profiles"]).stdout
        if "BACKUPDIR" not in listed or os.path.basename(folder) not in listed:
            print(f"list-profiles does not show the backup folder:\n{listed}")
            sys.exit(1)

        backup = [args.dbtool, "--config", cfg_path, "--profile", "e2e", "backup", "--schema-only"]
        # A bare file name goes into the profile's backupDir, which is created on demand.
        run_command(backup + ["--output", "named.zip"])
        if not os.path.isfile(os.path.join(folder, "named.zip")):
            print(f"backup --output named.zip did not write into backupDir ({os.listdir(tmpdir)})")
            sys.exit(1)
        # No --output at all: a generated <profile>-<timestamp>.zip in the folder.
        run_command(backup)
        generated = [n for n in os.listdir(folder) if n.startswith("e2e-") and n.endswith(".zip")]
        if len(generated) != 1:
            print(f"backup without --output did not generate one archive in backupDir: {os.listdir(folder)}")
            sys.exit(1)
        # A path with a directory part is used exactly as given.
        explicit = os.path.join(tmpdir, "explicit.zip")
        run_command(backup + ["--output", explicit])
        if not os.path.isfile(explicit):
            print("backup with an explicit path did not write it as given")
            sys.exit(1)

    with tempfile.TemporaryDirectory() as tmpdir:
        shared = os.path.join(tmpdir, "shared")
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        run_command([args.dbtool, "--config", cfg_path, "add-profile", "--name", "e2e",
                     "--connection-string", profile_cs, "--plugins-dir", args.plugins_dir],
                    input=secret + "\n")
        with open(cfg_path, encoding="utf-8") as f:
            text = f.read()
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write("defaultBackupDir: " + json.dumps(shared) + "\n" + text)
        run_command([args.dbtool, "--config", cfg_path, "--profile", "e2e", "backup", "--schema-only",
                     "--output", "fallback.zip"])
        if not os.path.isfile(os.path.join(shared, "fallback.zip")):
            print("backup did not fall back to defaultBackupDir")
            sys.exit(1)

    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        run_command([args.dbtool, "--config", cfg_path, "add-profile", "--name", "e2e",
                     "--connection-string", profile_cs, "--plugins-dir", args.plugins_dir],
                    input=secret + "\n")
        # Nothing configured and no --output: still an error, as before.
        missing = run_command([args.dbtool, "--config", cfg_path, "--profile", "e2e", "backup", "--schema-only"],
                              check=False)
        if missing.returncode == 0 or "--output" not in missing.stderr:
            print(f"backup without --output and without a backup folder should fail:\n{missing.stderr}")
            sys.exit(1)

    print("--- 12e. a working plaintext password is encrypted in place ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_path = os.path.join(tmpdir, "dbtool.yml")
        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write("# hand-written, keep this comment\n"
                    "profiles:\n"
                    "  plain:\n"
                    f"    connectionString: {yaml_quote(profile_cs)}\n"
                    f"    pluginsDir: {yaml_quote(args.plugins_dir)}\n"
                    f"    password: {yaml_quote(secret)}  # will be encrypted\n")
        upgraded = run_command([args.dbtool, "--config", cfg_path, "--profile", "plain", "list-applied"])
        with open(cfg_path, encoding="utf-8") as f:
            text = f.read()
        if "enc:" not in text or secret in text or "keep this comment" not in text \
                or "will be encrypted" not in text:
            print(f"plaintext password was not encrypted in place:\n{text}")
            sys.exit(1)
        if "Encrypted the plaintext password" not in upgraded.stderr:
            print(f"dbtool did not report the password upgrade:\n{upgraded.stderr}")
            sys.exit(1)
        # Second run uses the encrypted value.
        run_command([args.dbtool, "--config", cfg_path, "--profile", "plain", "list-applied"])

    if profile_pwd:
        print("--- 12f. a wrong plaintext password is left untouched ---")
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg_path = os.path.join(tmpdir, "dbtool.yml")
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write("profiles:\n"
                        "  wrong:\n"
                        f"    connectionString: {yaml_quote(profile_cs)}\n"
                        f"    pluginsDir: {yaml_quote(args.plugins_dir)}\n"
                        "    password: definitely-not-the-password\n")
            failed = run_command([args.dbtool, "--config", cfg_path, "--profile", "wrong", "list-applied"],
                                 check=False)
            with open(cfg_path, encoding="utf-8") as f:
                text = f.read()
            if failed.returncode == 0 or "definitely-not-the-password" not in text or "enc:" in text:
                print(f"a wrong plaintext password was rewritten or accepted:\n{text}")
                sys.exit(1)
    else:
        print("--- 12f. skipped: the test connection string has no password ---")

    print("--- 12g. help works even when a discovered dbtool.yml is broken ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        with open(os.path.join(tmpdir, "dbtool.yml"), "w", encoding="utf-8") as f:
            f.write("profiles: [this is: not valid\n")
        for help_cmd in (["help"], ["--help"], ["show-examples"]):
            helped = run_command([args.dbtool] + help_cmd, check=False,
                                 env=dict(os.environ, LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR=tmpdir))
            if helped.returncode != 0:
                print(f"dbtool {' '.join(help_cmd)} failed because of an unrelated broken dbtool.yml:\n{helped.stderr}")
                sys.exit(1)

    print("--- 13. status fails fast when no migration plugin is loaded ---")
    with tempfile.TemporaryDirectory() as empty_plugins_dir:
        empty_cmd = [args.dbtool, "--plugins-dir", empty_plugins_dir,
                     "--connection-string", connection_string, "status"]
        no_plugin_result = run_command(empty_cmd, check=False)
        if no_plugin_result.returncode == 0:
            print("status with empty plugins dir unexpectedly succeeded")
            sys.exit(1)
        if "No migrations registered" not in no_plugin_result.stderr:
            print(f"status error message did not mention 'No migrations registered':\n{no_plugin_result.stderr}")
            sys.exit(1)

    test_multi_schema_backup(args.dbtool, args.plugins_dir, connection_string, is_sqlite)

    test_post_migrate_hook(base_cmd, args.dbtool, args.plugins_dir, connection_string)

    test_pre_migrate_steps(base_cmd, args.dbtool, args.plugins_dir, connection_string)

    if is_sqlite:
        test_target_precedence(args.dbtool, args.plugins_dir, connection_string)

    test_exit_code_after_compat_policy_plugin(args.dbtool, args.plugins_dir, connection_string)

    test_skipped_seed_rows(base_cmd, args.dbtool, args.plugins_dir, connection_string)

    test_backup_requirement(args.dbtool, args.plugins_dir, connection_string)

    test_restore_failure_report(args.dbtool, args.plugins_dir, connection_string)

    if is_sqlite:
        test_unwritable_backup_records(args.dbtool, args.plugins_dir, connection_string)

    print("SUCCESS")


if __name__ == "__main__":
    main()
