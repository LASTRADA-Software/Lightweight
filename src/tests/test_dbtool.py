#!/usr/bin/env python3
import argparse
import subprocess
import sys
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False


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


def with_sqlite_database(connection_string, database_path):
    """The SQLite connection string with its Database= attribute replaced by `database_path`."""
    parts = [part for part in connection_string.split(";")
             if part.strip() and not part.strip().lower().startswith("database=")]
    return ";".join(parts + [f"Database={database_path}"])


def file_fingerprint(path):
    """Bytes and modification time of a file, to prove that a command left it untouched."""
    with open(path, "rb") as f:
        return f.read(), os.stat(path).st_mtime_ns


def test_target_precedence(dbtool, plugins_dir, connection_string):
    """A dbtool.yml found by discovery whose defaultProfile names database A, and
    SQL_CONNECTION_STRING naming database B: the environment wins; without it, commands
    that write refuse the discovered target and leave A untouched, while read-only ones
    may still use it unless a loaded plugin has a post-init hook. SQLite only. The tree's
    own dbtool.yml stops the upward walk before any dbtool.yml above the temp directory."""
    print("--- 15. a discovered defaultProfile never receives a write ---")
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

        def dbtool_in_tree(command, env, check=True, cwd=nested, plugin_args=plugins):
            return run_command([dbtool] + plugin_args + command, check=check, cwd=cwd, env=env)

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
                                (["--profile", "c", "migrate"], "discovered via current directory")):
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
        refused = dbtool_in_tree(["migrate"], env_none, check=False, cwd=fallback)
        if refused.returncode == 0 or "the first profile; the file sets no defaultProfile" not in refused.stderr:
            print(f"the refusal did not name the first-profile fallback:\n{refused.stderr}")
            sys.exit(1)

        # The plugin refusal comes before the password is asked for: stdin stays unread.
        prompting = os.path.join(tmpdir, "prompting")
        os.makedirs(prompting)
        with open(os.path.join(prompting, "dbtool.yml"), "w", encoding="utf-8") as f:
            f.write("defaultProfile: s\nprofiles:\n"
                    f"  s:\n    connectionString: {yaml_quote(target_a)}\n    secretRef: \"stdin:\"\n")
        refused = run_command([dbtool] + all_plugins + ["status"], check=False, cwd=prompting, env=env_none,
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


def main():
    parser = argparse.ArgumentParser(description="Test dbtool integration")
    parser.add_argument("--dbtool", required=True, help="Path to dbtool executable")
    parser.add_argument("--plugins-dir", required=True, help="Path to plugins directory")
    parser.add_argument("--connection-string", help="Database connection string")
    parser.add_argument("--test-env", help="Use connection string from .test-env.yml (e.g., sqlite3, mssql, postgres)")
    args, _ = parser.parse_known_args()

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

    print("--- 12c. config discovery walks up from the working directory ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        project = os.path.join(tmpdir, "proj")
        nested = os.path.join(project, "sub", "dir")
        os.makedirs(nested)
        with open(os.path.join(project, "dbtool.yml"), "w", encoding="utf-8") as f:
            f.write("profiles:\n  discovered:\n    connectionString: \"DRIVER=SQLite3;Database=x.db\"\n")
        found = run_command([args.dbtool, "list-profiles"], cwd=nested)
        if "discovered" not in found.stdout or "current directory" not in found.stdout:
            print(f"list-profiles did not use the dbtool.yml above the working directory:\n{found.stdout}")
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
            helped = run_command([args.dbtool] + help_cmd, cwd=tmpdir, check=False)
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

    test_post_migrate_hook(base_cmd, args.dbtool, args.plugins_dir, connection_string)

    if is_sqlite:
        test_target_precedence(args.dbtool, args.plugins_dir, connection_string)

    print("SUCCESS")


if __name__ == "__main__":
    main()
