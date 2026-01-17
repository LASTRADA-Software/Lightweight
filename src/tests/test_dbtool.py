#!/usr/bin/env python3
import argparse
import subprocess
import sys
import os

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
    parser.add_argument("--connection-string", required=True, help="Database connection string")
    args = parser.parse_args()

    # Define base command
    base_cmd = [args.dbtool, "--plugins-dir", args.plugins_dir, "--connection-string", args.connection_string]

    # Clean up previous run if any
    # Only for SQLite, we can remove the database file.
    # For others, we assume the environment is clean or we might need explicit drop logic.
    is_sqlite = "sqlite" in args.connection_string.lower()
    db_path = None
    if is_sqlite and "Database=" in args.connection_string:
        for part in args.connection_string.split(";"):
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
