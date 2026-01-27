#!/usr/bin/env python3
import argparse
import subprocess
import sys
import os
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


def run_command(cmd, check=True):
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        if check:
            sys.exit(result.returncode)
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

    print("SUCCESS")


if __name__ == "__main__":
    main()
