#!/usr/bin/env python3
"""
Docker database setup script for Lightweight tests.

Sets up Docker containers for MS SQL 2025/2022/2019/2017, and PostgreSQL.
Idempotent: safe to run multiple times.

Usage:
    python docker-databases.py --start                Start all databases
    python docker-databases.py --start --wait         Start and wait for readiness
    python docker-databases.py --start mssql2022      Start only MS SQL 2022
    python docker-databases.py --stop                 Stop all databases
    python docker-databases.py --status               Show container status
    python docker-databases.py --remove               Remove all containers
"""

import argparse
import subprocess
import sys
import time
from dataclasses import dataclass
from enum import Enum
from typing import Callable

# Database password for all containers (must meet MS SQL complexity requirements)
# MS SQL requires: 8+ chars, uppercase, lowercase, digit, special char
DB_PASSWORD = "Lightweight!Test42"


class ContainerState(Enum):
    """State of a Docker container."""
    NOT_EXISTS = "not exists"
    STOPPED = "stopped"
    RUNNING = "running"


@dataclass
class DatabaseConfig:
    """Configuration for a database container."""
    name: str
    container_name: str
    image: str
    port: int
    internal_port: int
    environment: dict[str, str]
    health_check_cmd: list[str]
    test_database: str


# Database configurations
DATABASES: list[DatabaseConfig] = [
    DatabaseConfig(
        name="mssql2025",
        container_name="sql2025",
        image="mcr.microsoft.com/mssql/server:2025-latest",
        port=1435,
        internal_port=1433,
        environment={
            "ACCEPT_EULA": "Y",
            "MSSQL_SA_PASSWORD": DB_PASSWORD,
        },
        health_check_cmd=[
            "/opt/mssql-tools18/bin/sqlcmd",
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
            "-C",
            "-Q", "SELECT 1",
        ],
        test_database="LightweightTest",
    ),
    DatabaseConfig(
        name="mssql2022",
        container_name="sql2022",
        image="mcr.microsoft.com/mssql/server:2022-latest",
        port=1433,
        internal_port=1433,
        environment={
            "ACCEPT_EULA": "Y",
            "MSSQL_SA_PASSWORD": DB_PASSWORD,
        },
        health_check_cmd=[
            "/opt/mssql-tools18/bin/sqlcmd",
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
            "-C",
            "-Q", "SELECT 1",
        ],
        test_database="LightweightTest",
    ),
    DatabaseConfig(
        name="mssql2019",
        container_name="sql2019",
        image="mcr.microsoft.com/mssql/server:2019-latest",
        port=1434,
        internal_port=1433,
        environment={
            "ACCEPT_EULA": "Y",
            "MSSQL_SA_PASSWORD": DB_PASSWORD,
        },
        health_check_cmd=[
            "/opt/mssql-tools/bin/sqlcmd",
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
            "-Q", "SELECT 1",
        ],
        test_database="LightweightTest",
    ),
    DatabaseConfig(
        name="mssql2017",
        container_name="sql2017",
        image="mcr.microsoft.com/mssql/server:2017-latest",
        port=1432,
        internal_port=1433,
        environment={
            "ACCEPT_EULA": "Y",
            "MSSQL_SA_PASSWORD": DB_PASSWORD,
        },
        health_check_cmd=[
            "/opt/mssql-tools/bin/sqlcmd",
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
            "-Q", "SELECT 1",
        ],
        test_database="LightweightTest",
    ),
    DatabaseConfig(
        name="postgres",
        container_name="postgres-16.4",
        image="postgres:16.4",
        port=5432,
        internal_port=5432,
        environment={
            "POSTGRES_PASSWORD": DB_PASSWORD,
        },
        health_check_cmd=[
            "pg_isready",
            "-U", "postgres",
        ],
        test_database="test",
    ),
]


class Colors:
    """Cross-platform colored terminal output."""
    _enabled: bool = True

    @classmethod
    def init(cls) -> None:
        """Initialize color support, enabling ANSI on Windows if needed."""
        if sys.platform == "win32":
            try:
                import ctypes
                kernel32 = ctypes.windll.kernel32
                # Enable ANSI escape codes on Windows
                kernel32.SetConsoleMode(
                    kernel32.GetStdHandle(-11),  # STD_OUTPUT_HANDLE
                    0x0001 | 0x0004  # ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                )
            except Exception:
                cls._enabled = False
        # Check if we're outputting to a terminal
        if not sys.stdout.isatty():
            cls._enabled = False

    @classmethod
    def green(cls, text: str) -> str:
        return f"\033[32m{text}\033[0m" if cls._enabled else text

    @classmethod
    def red(cls, text: str) -> str:
        return f"\033[31m{text}\033[0m" if cls._enabled else text

    @classmethod
    def yellow(cls, text: str) -> str:
        return f"\033[33m{text}\033[0m" if cls._enabled else text

    @classmethod
    def cyan(cls, text: str) -> str:
        return f"\033[36m{text}\033[0m" if cls._enabled else text

    @classmethod
    def bold(cls, text: str) -> str:
        return f"\033[1m{text}\033[0m" if cls._enabled else text


def check_docker_available() -> bool:
    """Check if Docker is available and running."""
    try:
        result = subprocess.run(
            ["docker", "info"],
            capture_output=True,
            text=True,
        )
        return result.returncode == 0
    except FileNotFoundError:
        return False


def get_container_state(container_name: str) -> ContainerState:
    """Get the current state of a container."""
    result = subprocess.run(
        ["docker", "inspect", "--format", "{{.State.Running}}", container_name],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return ContainerState.NOT_EXISTS

    running = result.stdout.strip().lower()
    if running == "true":
        return ContainerState.RUNNING
    return ContainerState.STOPPED


def ensure_image_exists(image: str) -> bool:
    """Pull image if not present locally. Returns True on success."""
    # Check if image exists locally
    result = subprocess.run(
        ["docker", "image", "inspect", image],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print(f"  Image {Colors.cyan(image)} already exists locally")
        return True

    # Pull the image
    print(f"  Pulling image {Colors.cyan(image)}...")
    result = subprocess.run(
        ["docker", "pull", image],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  {Colors.red('Failed')} to pull image: {result.stderr}")
        return False
    print(f"  {Colors.green('Pulled')} image successfully")
    return True


def start_container(db: DatabaseConfig) -> bool:
    """Start a container idempotently. Returns True on success."""
    state = get_container_state(db.container_name)

    if state == ContainerState.RUNNING:
        print(f"  Container {Colors.cyan(db.container_name)} is {Colors.green('already running')}")
        return True

    if state == ContainerState.STOPPED:
        print(f"  Starting stopped container {Colors.cyan(db.container_name)}...")
        result = subprocess.run(
            ["docker", "start", db.container_name],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"  {Colors.red('Failed')} to start container: {result.stderr}")
            return False
        print(f"  {Colors.green('Started')} container")
        return True

    # Container doesn't exist, need to create it
    if not ensure_image_exists(db.image):
        return False

    print(f"  Creating container {Colors.cyan(db.container_name)}...")
    cmd = [
        "docker", "run", "-d",
        "--name", db.container_name,
        "-p", f"{db.port}:{db.internal_port}",
    ]
    for key, value in db.environment.items():
        cmd.extend(["-e", f"{key}={value}"])
    cmd.append(db.image)

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  {Colors.red('Failed')} to create container: {result.stderr}")
        return False
    print(f"  {Colors.green('Created')} and started container")
    return True


def stop_container(db: DatabaseConfig) -> bool:
    """Stop a container if running. Returns True on success."""
    state = get_container_state(db.container_name)

    if state == ContainerState.NOT_EXISTS:
        print(f"  Container {Colors.cyan(db.container_name)} does {Colors.yellow('not exist')}")
        return True

    if state == ContainerState.STOPPED:
        print(f"  Container {Colors.cyan(db.container_name)} is {Colors.yellow('already stopped')}")
        return True

    print(f"  Stopping container {Colors.cyan(db.container_name)}...")
    result = subprocess.run(
        ["docker", "stop", db.container_name],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  {Colors.red('Failed')} to stop container: {result.stderr}")
        return False
    print(f"  {Colors.green('Stopped')} container")
    return True


def remove_container(db: DatabaseConfig) -> bool:
    """Stop and remove a container. Returns True on success."""
    state = get_container_state(db.container_name)

    if state == ContainerState.NOT_EXISTS:
        print(f"  Container {Colors.cyan(db.container_name)} does {Colors.yellow('not exist')}")
        return True

    if state == ContainerState.RUNNING:
        if not stop_container(db):
            return False

    print(f"  Removing container {Colors.cyan(db.container_name)}...")
    result = subprocess.run(
        ["docker", "rm", db.container_name],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  {Colors.red('Failed')} to remove container: {result.stderr}")
        return False
    print(f"  {Colors.green('Removed')} container")
    return True


def find_sqlcmd() -> str | None:
    """Find sqlcmd executable on the host system."""
    import shutil
    # Check common installation paths
    paths_to_check = [
        "/opt/mssql-tools18/bin/sqlcmd",  # Ubuntu with mssql-tools18
        "/opt/mssql-tools/bin/sqlcmd",    # Ubuntu with older mssql-tools
    ]
    for path in paths_to_check:
        import os
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    # Fall back to PATH lookup
    return shutil.which("sqlcmd")


def verify_external_connection(db: DatabaseConfig) -> bool:
    """Verify external connectivity to database through exposed port.

    This is critical because Docker port forwarding may not be ready immediately
    after the container's internal health check passes.

    Returns True if connection succeeds, False otherwise.
    If host tools are not available, returns True (falls back to internal-only check).
    """
    import shutil

    if "mssql" in db.name:
        sqlcmd_path = find_sqlcmd()
        if not sqlcmd_path:
            print("  Warning: sqlcmd not found on host, skipping external connectivity check")
            return True
        # Use sqlcmd on host to connect through exposed port
        result = subprocess.run(
            [
                sqlcmd_path,
                "-S", f"localhost,{db.port}",
                "-U", "SA",
                "-P", DB_PASSWORD,
                "-C",  # Trust server certificate
                "-Q", "SELECT 1",
                "-l", "5",  # Login timeout 5 seconds
            ],
            capture_output=True,
            text=True,
        )
        return result.returncode == 0
    else:
        # PostgreSQL: use pg_isready on host
        pg_isready_path = shutil.which("pg_isready")
        if not pg_isready_path:
            print("  Warning: pg_isready not found on host, skipping external connectivity check")
            return True
        result = subprocess.run(
            [
                pg_isready_path,
                "-h", "localhost",
                "-p", str(db.port),
                "-U", "postgres",
                "-t", "5",
            ],
            capture_output=True,
            text=True,
        )
        return result.returncode == 0


def wait_for_database(db: DatabaseConfig, timeout: int = 120) -> bool:
    """Wait for database to be ready. Returns True when ready.

    This performs a two-phase check:
    1. Internal readiness: verify database is ready inside the container
    2. External connectivity: verify port forwarding works from host
    """
    print(f"  Waiting for {Colors.cyan(db.name)} to be ready (timeout: {timeout}s)...")
    start_time = time.time()

    # Phase 1: Wait for internal readiness (inside container)
    while time.time() - start_time < timeout:
        result = subprocess.run(
            ["docker", "exec", db.container_name] + db.health_check_cmd,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            internal_elapsed = time.time() - start_time
            print(f"  Internal check passed after {internal_elapsed:.1f}s, verifying external connectivity...")
            break
        time.sleep(1)
    else:
        print(f"  {Colors.red('Timeout')} waiting for database (internal check)")
        return False

    # Phase 2: Wait for external connectivity (through port forwarding)
    # This is critical because Docker port forwarding may not be ready immediately
    while time.time() - start_time < timeout:
        if verify_external_connection(db):
            elapsed = time.time() - start_time
            print(f"  {Colors.green('Ready')} after {elapsed:.1f}s")
            return True
        time.sleep(1)

    print(f"  {Colors.red('Timeout')} waiting for database (external check)")
    return False


def create_test_database(db: DatabaseConfig) -> bool:
    """Create the test database. Returns True on success."""
    if "mssql" in db.name:
        # MS SQL: Create database using sqlcmd
        sqlcmd_path = db.health_check_cmd[0]  # Use same path as health check
        create_db_cmd = [
            "docker", "exec", db.container_name,
            sqlcmd_path,
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
        ]
        # Add -C flag only if present in health check (required for mssql-tools18, not for older versions)
        if "-C" in db.health_check_cmd:
            create_db_cmd.append("-C")
        create_db_cmd.extend([
            "-Q", f"IF NOT EXISTS (SELECT name FROM sys.databases WHERE name = '{db.test_database}') CREATE DATABASE [{db.test_database}]",
        ])
        print(f"  Creating database {Colors.cyan(db.test_database)}...")
        result = subprocess.run(create_db_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  {Colors.red('Failed')} to create database: {result.stderr}")
            return False
        print(f"  {Colors.green('Created')} database (or already exists)")
        return True

    # PostgreSQL: Create database using psql
    # First check if database exists, then create if not
    print(f"  Creating database {Colors.cyan(db.test_database)}...")
    check_cmd = [
        "docker", "exec", db.container_name,
        "psql", "-U", "postgres", "-tAc",
        f"SELECT 1 FROM pg_database WHERE datname = '{db.test_database}'",
    ]
    result = subprocess.run(check_cmd, capture_output=True, text=True)
    if result.stdout.strip() == "1":
        print(f"  {Colors.green('Created')} database (or already exists)")
        return True

    create_db_cmd = [
        "docker", "exec", db.container_name,
        "psql", "-U", "postgres", "-c",
        f"CREATE DATABASE {db.test_database}",
    ]
    result = subprocess.run(create_db_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  {Colors.red('Failed')} to create database: {result.stderr}")
        return False
    print(f"  {Colors.green('Created')} database (or already exists)")
    return True


def load_sql_file(db: DatabaseConfig, sql_file: str) -> bool:
    """Load a SQL file into the database. Returns True on success."""
    import os
    if not os.path.exists(sql_file):
        print(f"{Colors.red('Error')}: SQL file not found: {sql_file}")
        return False

    print(f"Loading {Colors.cyan(sql_file)} into {Colors.cyan(db.name)}...")

    if "mssql" in db.name:
        # MS SQL: Use sqlcmd
        sqlcmd_path = db.health_check_cmd[0]
        cmd = [
            "docker", "exec", "-i", db.container_name,
            sqlcmd_path,
            "-S", "localhost",
            "-U", "SA",
            "-P", DB_PASSWORD,
        ]
        # Add -C flag only if present in health check (required for mssql-tools18, not for older versions)
        if "-C" in db.health_check_cmd:
            cmd.append("-C")
        cmd.extend(["-d", db.test_database])
        # Read and pipe the SQL file
        with open(sql_file, 'r') as f:
            sql_content = f.read()
        result = subprocess.run(cmd, input=sql_content, capture_output=True, text=True)
    else:
        # PostgreSQL: Use psql
        cmd = [
            "docker", "exec", "-i", db.container_name,
            "psql", "-U", "postgres", "-d", db.test_database,
        ]
        with open(sql_file, 'r') as f:
            sql_content = f.read()
        result = subprocess.run(cmd, input=sql_content, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"{Colors.red('Failed')} to load SQL file: {result.stderr}")
        return False

    print(f"{Colors.green('Loaded')} SQL file successfully")
    return True


def show_status(dbs: list[DatabaseConfig]) -> None:
    """Show status of all containers in a table format."""
    print()
    print(f"{'Database':<12} {'Container':<16} {'Port':<8} {'Status':<12}")
    print("-" * 50)

    for db in dbs:
        state = get_container_state(db.container_name)
        if state == ContainerState.RUNNING:
            status = Colors.green("running")
        elif state == ContainerState.STOPPED:
            status = Colors.yellow("stopped")
        else:
            status = Colors.red("not exists")

        print(f"{db.name:<12} {db.container_name:<16} {db.port:<8} {status}")

    print()


def get_databases_by_names(names: list[str] | None) -> list[DatabaseConfig]:
    """Filter databases by names. Returns all if names is None or empty."""
    if not names:
        return DATABASES

    result = []
    valid_names = {db.name for db in DATABASES}
    for name in names:
        if name not in valid_names:
            print(f"Unknown database: {Colors.red(name)}")
            print(f"Available: {', '.join(sorted(valid_names))}")
            sys.exit(1)
        for db in DATABASES:
            if db.name == name:
                result.append(db)
                break
    return result


def do_action(
    dbs: list[DatabaseConfig],
    action: Callable[[DatabaseConfig], bool],
    action_name: str,
    past_tense: str,
) -> int:
    """Perform an action on all specified databases. Returns exit code."""
    success_count = 0
    for db in dbs:
        print(f"{Colors.bold(action_name)} {Colors.cyan(db.name)}:")
        if action(db):
            success_count += 1

    if success_count == len(dbs):
        print(f"\n{Colors.green('All')} {len(dbs)} database(s) {past_tense} successfully")
        return 0
    else:
        print(f"\n{Colors.red('Warning')}: {success_count}/{len(dbs)} database(s) {past_tense} successfully")
        return 1


def main() -> int:
    """Main entry point."""
    Colors.init()

    parser = argparse.ArgumentParser(
        description="Docker database setup for Lightweight tests",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --start                 Start all databases
  %(prog)s --start --wait          Start all and wait for readiness
  %(prog)s --start mssql2022       Start only MS SQL 2022
  %(prog)s --stop                  Stop all databases
  %(prog)s --status                Show container status
  %(prog)s --remove                Remove all containers
""",
    )

    action_group = parser.add_mutually_exclusive_group(required=True)
    action_group.add_argument("--start", action="store_true", help="Start database containers")
    action_group.add_argument("--stop", action="store_true", help="Stop database containers")
    action_group.add_argument("--status", action="store_true", help="Show container status")
    action_group.add_argument("--remove", action="store_true", help="Remove containers")
    action_group.add_argument("--load-sql", metavar="FILE", help="Load SQL file into database (requires DATABASE argument)")

    parser.add_argument("--wait", action="store_true", help="Wait for databases to be ready (with --start)")
    parser.add_argument("--timeout", type=int, default=120, help="Timeout for --wait in seconds (default: 120)")
    parser.add_argument("databases", nargs="*", metavar="DATABASE",
                        help=f"Databases to operate on (default: all). Options: {', '.join(db.name for db in DATABASES)}")

    args = parser.parse_args()

    # Check Docker availability
    if not check_docker_available():
        print(f"{Colors.red('Error')}: Docker is not available or not running")
        print("Please install Docker and ensure the Docker daemon is running")
        return 1

    # Get databases to operate on
    dbs = get_databases_by_names(args.databases if args.databases else None)

    if args.status:
        show_status(dbs)
        return 0

    if args.load_sql:
        if len(dbs) != 1:
            print(f"{Colors.red('Error')}: --load-sql requires exactly one DATABASE argument")
            return 1
        db = dbs[0]
        state = get_container_state(db.container_name)
        if state != ContainerState.RUNNING:
            print(f"{Colors.red('Error')}: Container {db.container_name} is not running")
            return 1
        return 0 if load_sql_file(db, args.load_sql) else 1

    if args.start:
        exit_code = do_action(dbs, start_container, "Starting", "started")
        if exit_code != 0:
            return exit_code

        if args.wait:
            print()
            for db in dbs:
                state = get_container_state(db.container_name)
                if state != ContainerState.RUNNING:
                    continue
                if not wait_for_database(db, args.timeout):
                    return 1
                # Always create test database after container is ready
                if not create_test_database(db):
                    return 1

        return 0

    if args.stop:
        return do_action(dbs, stop_container, "Stopping", "stopped")

    if args.remove:
        return do_action(dbs, remove_container, "Removing", "removed")

    return 0


if __name__ == "__main__":
    sys.exit(main())
