#!/usr/bin/env python3
import argparse
import subprocess
import sys
import os
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


def run_command(cmd, check=True):
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
            stdin=subprocess.DEVNULL,
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

    print("SUCCESS")


if __name__ == "__main__":
    main()
