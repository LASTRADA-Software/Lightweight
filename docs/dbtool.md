# dbtool - Database Management CLI {#dbtool}

## Overview

`dbtool` is a command-line utility for managing database migrations and performing backup/restore operations.
It works with any ODBC-compatible database including SQLite, SQL Server, and PostgreSQL.

Key features:
- Apply, rollback, and manage SQL migrations
- Full database backup with compression
- Selective table backup and restore
- Parallel processing for large databases
- Checksum verification for migration integrity

## Installation

Build dbtool as part of the Lightweight project:

```bash
cmake --preset linux-clang-release
cmake --build out/build/linux-clang-release --target dbtool
```

The binary will be located at `out/build/linux-clang-release/src/tools/dbtool/dbtool`.

## Configuration

### Connection String

dbtool requires a database connection string, which can be provided in three ways (in order of precedence):

1. **Command-line option**: `--connection-string "..."`
2. **Environment variable**: `SQL_CONNECTION_STRING` or `ODBC_CONNECTION_STRING`
3. **Configuration file**: `~/.config/dbtool/dbtool.yml` (Linux) or `%APPDATA%\dbtool\dbtool.yml` (Windows)

### Configuration File Format

Legacy single-profile shape (still supported):

```yaml
ConnectionString: "DRIVER=SQLite3;Database=mydb.db"
PluginsDir: ./plugins
Schema: myschema
```

Multi-profile shape:

```yaml
defaultProfile: prod
defaultPluginsDir: ./plugins        # store-wide fallback for any profile
                                    # that omits its own `pluginsDir`
profiles:
  prod:
    schema: dbo
    connectionString: "DRIVER={ODBC Driver 18 for SQL Server};Server=...;Database=prod"
  dev:
    pluginsDir: ./dev-plugins       # per-profile override
    connectionString: "DRIVER=SQLite3;Database=dev.db"
```

The effective plugin directory for a given run resolves as: `--plugins-dir`
CLI option → profile's own `pluginsDir` → top-level `defaultPluginsDir` →
current working directory.

### Database-Specific Connection Strings

**SQLite:**
```
DRIVER=SQLite3;Database=/path/to/database.db
```

**SQL Server:**
```
DRIVER={ODBC Driver 18 for SQL Server};Server=localhost;Database=mydb;UID=sa;PWD=password;TrustServerCertificate=yes
```

**PostgreSQL:**
```
DRIVER={PostgreSQL Unicode};Server=localhost;Port=5432;Database=mydb;Uid=postgres;Pwd=password
```

## Migration Commands

### migrate

Apply all pending migrations:

```bash
dbtool migrate --connection-string "DRIVER=SQLite3;Database=test.db"
```

Use `--dry-run` to preview SQL without executing:

```bash
dbtool migrate --dry-run --connection-string "..."
```

#### Custom default schema (`--schema`)

The `--schema <NAME>` flag pins the connection's default schema for the
migration runner — useful when the target database expects unqualified DDL/DML
to land in a non-default schema (e.g. `lasa` instead of `dbo` / `public`):

```bash
dbtool migrate --schema lasa --connection-string "..."
```

Per-backend behaviour:

| Backend     | What `--schema` does for migrations                                                                                              |
|-------------|-----------------------------------------------------------------------------------------------------------------------------------|
| PostgreSQL  | Emits `SET search_path TO "<schema>", public` on every new connection. The `schema_migrations` history table is created here and unqualified DDL inside migrations lands here too. |
| SQL Server  | **No session-level switch is portable.** Use the login's server-side `DEFAULT_SCHEMA` (`ALTER USER … WITH DEFAULT_SCHEMA = lasa`). `--schema` is still accepted (it qualifies table names in backup/restore archives) but does *not* relocate `schema_migrations`. |
| SQLite      | No-op — SQLite has no schema concept beyond attached databases.                                                                  |

Schema names are validated against `[A-Za-z0-9_]` to keep them safe to
interpolate into `SET search_path`. Anything else is rejected.

The same flag also applies to `apply`, `rollback`, `migrate-to-release`,
`rollback-to-release`, `status`, and the backup/restore commands (where the
schema additionally qualifies table names inside the archive).

### migrate-to-release \<VERSION\>

Apply pending migrations up to (and including) the named release. Forward-only:
if the database is already at or past the target release, the command is a
no-op and prints a hint pointing at `rollback-to-release`. Pair with
`--dry-run` (`-n`) to preview the SQL without touching the database.

```bash
dbtool migrate-to-release 1.0.0
dbtool migrate-to-release 1.0.0 --dry-run
```

The release version must match a `LIGHTWEIGHT_SQL_RELEASE(...)` declaration
shipped by one of the loaded plugins. Use `dbtool releases` to list them.

If a pending migration whose timestamp is `<= release.highestTimestamp`
declares a dependency on a migration whose timestamp is `>` the release
boundary (and is not already applied), `migrate-to-release` refuses to run
rather than applying a partial state that violates the dependency contract.

### list-pending

List migrations waiting to be applied:

```bash
dbtool list-pending --plugins-dir ./plugins
```

### list-applied

List migrations that have been applied:

```bash
dbtool list-applied
```

### status

Show migration status with checksum verification:

```bash
dbtool status
```

Outputs:
- Total registered migrations
- Applied and pending counts
- Checksum mismatches (if any migrations were modified after application)

### apply \<TIMESTAMP\>

Apply a specific migration by timestamp:

```bash
dbtool apply 20260126120000
```

### rollback \<TIMESTAMP\>

Revert a specific migration:

```bash
dbtool rollback 20260126120000
```

### rollback-to \<TIMESTAMP\>

Rollback all migrations applied after the specified timestamp:

```bash
dbtool rollback-to 20260101000000
```

The target migration itself is NOT reverted.

### mark-applied \<TIMESTAMP\>

Mark a migration as applied without executing its SQL:

```bash
dbtool mark-applied 20260126120000
```

Useful for:
- Baseline migrations when setting up an existing database
- Skipping migrations that were applied manually

## Backup & Restore

### backup

Create a compressed backup of the database:

```bash
dbtool backup --output backup.zip --compression zstd --jobs 4
```

**Compression methods:** `none`, `deflate`, `bzip2`, `lzma`, `zstd`, `xz`

**Filter tables with wildcards:**

```bash
# Backup specific tables
dbtool backup --output backup.zip --filter-tables=Users,Products

# Backup tables matching patterns
dbtool backup --output backup.zip --filter-tables="*_log,audit*"

# Backup schema-qualified tables
dbtool backup --output backup.zip --filter-tables=dbo.Users,sales.*
```

### restore

Restore a database from backup:

```bash
dbtool restore --input backup.zip --jobs 4
```

**Restore to a different schema:**

```bash
dbtool restore --input backup.zip --schema new_schema
```

**Restore only specific tables:**

```bash
dbtool restore --input backup.zip --filter-tables=Users,Products
```

## Command-Line Options Reference

| Option | Description | Default |
|--------|-------------|---------|
| `--connection-string <STR>` | ODBC connection string | |
| `--schema <NAME>` | Database schema to use | |
| `--config <FILE>` | Path to configuration file | `~/.config/dbtool/dbtool.yml` |
| `--plugins-dir <DIR>` | Directory to scan for migration plugins | `.` (current directory) |
| `--output <FILE>` | Output file for backup | |
| `--input <FILE>` | Input file for restore | |
| `--filter-tables <PATTERN>` | Table filter (wildcards supported) | `*` (all tables) |
| `--jobs <N>` | Number of concurrent jobs | `1` |
| `--compression <METHOD>` | Compression method for backup | `deflate` |
| `--compression-level <N>` | Compression level (0-9) | `6` |
| `--chunk-size <SIZE>` | Chunk size for backup data | `10M` |
| `--progress <TYPE>` | Progress output: `unicode`, `ascii`, `logline` | `unicode` |
| `--quiet`, `-q` | Suppress progress output | |
| `--dry-run`, `-n` | Preview without executing | |
| `--no-lock` | Skip migration locking | |
| `--schema-only` | For backup/restore: skip data. For `diff`: skip the data diff. | |
| `--no-color` | Disable ANSI colors in `diff` output (auto-disabled when not at a tty) | |
| `--max-rows <N>` | Cap rows scanned per table for `diff` data mode | `0` (unlimited) |
| `--max-retries <N>` | Maximum retry attempts for transient errors | `3` |
| `--help` | Show help message | |

### Size Suffixes

The `--chunk-size` option accepts size suffixes:
- Bytes: `1024` or `1024B`
- Kilobytes: `10K` or `10KB`
- Megabytes: `10M` or `10MB`
- Gigabytes: `1G` or `1GB`

## Plugin System

Migrations can be packaged as shared library plugins. dbtool scans the plugins directory for `.so`, `.dll`, or `.dylib` files.

### Creating a Migration Plugin

1. Write migrations using the `LIGHTWEIGHT_SQL_MIGRATION` macro
2. Add `LIGHTWEIGHT_MIGRATION_PLUGIN()` in exactly one source file
3. Build as a shared library

```cpp
// migrations.cpp
#include <Lightweight/SqlMigration.hpp>

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20260126120000, "Create users table")
{
    plan.CreateTable("users")
        .PrimaryKeyWithAutoIncrement("id")
        .RequiredColumn("email", Varchar(255))
        .Timestamps();
}
```

### Loading Plugins

```bash
dbtool migrate --plugins-dir ./build/plugins
```

## Workflow Examples

### Full Migration Workflow

```bash
# Check current status
dbtool status

# Preview pending migrations
dbtool migrate --dry-run

# Apply migrations
dbtool migrate

# Verify all checksums
dbtool status
```

### Backup Before Migration

```bash
# Create backup
dbtool backup --output pre-migration-backup.zip --compression zstd

# Apply migrations
dbtool migrate

# If something goes wrong, restore
dbtool restore --input pre-migration-backup.zip
```

### Parallel Backup and Restore

For large databases, use multiple jobs:

```bash
# Backup with 4 parallel workers
dbtool backup --output backup.zip --jobs 4

# Restore with 4 parallel workers
dbtool restore --input backup.zip --jobs 4
```

## Troubleshooting

### Connection Errors

- **Login failed**: Verify username and password in connection string
- **Server not found**: Check server address and port, ensure server is running
- **Driver not found**: Install the required ODBC driver
- **Network error**: Check firewall settings and network connectivity

### Checksum Mismatches

If `dbtool status` reports checksum mismatches:
- A migration was modified after it was applied
- This may indicate the database schema is out of sync with the code
- Review the changes and consider creating a new migration instead

### Lock Acquisition Failed

If migration locking fails:
- Another migration may be running
- Use `--no-lock` to skip locking (only if you're certain no other migrations are running)

## See Also

- @ref sql-migrations - Guide to writing SQL migrations in C++
- @ref Lightweight::SqlMigration::MigrationManager - C++ API for managing migrations
