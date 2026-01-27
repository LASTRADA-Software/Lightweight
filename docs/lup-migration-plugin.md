# LUP Migration Plugin

This document describes how to use the LUP Migration Plugin to migrate legacy LUP SQL databases to the Lightweight migration system.

## Overview

The LUP Migration Plugin provides:
1. **lup2dbtool** - A standalone tool that parses LUP SQL files and generates C++ migration code
2. **LupMigrationsPlugin** - A dbtool plugin containing generated migrations and transition utilities
3. **TransitionGlue** - Utilities to migrate from legacy migration tracking to `schema_migrations`

## lup2dbtool

### Purpose

`lup2dbtool` parses legacy LUP SQL migration files and generates idempotent C++ code using the Lightweight Migration API.

### Usage

```bash
# Single output file (all migrations in one file)
lup2dbtool --input-dir /path/to/legacy_sql_migrations_dir \
           --output /path/to/GeneratedMigrations.cpp

# Multiple output files (one file per migration)
lup2dbtool --input-dir /path/to/legacy_sql_migrations_dir \
           --output "/path/to/lup_{version}.cpp"

# Specify input encoding (default: windows-1252)
lup2dbtool --input-dir /path/to/legacy_sql_migrations_dir \
           --input-encoding utf-8 \
           --output /path/to/GeneratedMigrations.cpp
```

### Command-Line Arguments

| Argument | Description | Default |
|----------|-------------|---------|
| `--input-dir <DIR>` | Directory containing SQL migration files | (required) |
| `--output <FILE>` | Output file path (supports pattern substitution) | (required) |
| `--input-encoding <ENC>` | Input file encoding: `windows-1252` or `utf-8` | `windows-1252` |
| `--help` | Show usage information | |

### Output File Patterns

The `--output` argument supports pattern substitution for generating multiple files:

| Variable | Description | Example |
|----------|-------------|---------|
| `{major}` | Major version number | 6 |
| `{minor}` | Minor version number (zero-padded) | 08 |
| `{patch}` | Patch version number (zero-padded) | 08 |
| `{version}` | Full version string | 6_08_08 |

**Behavior:**
- If `--output` contains substitution variables: generates one file per migration
- If `--output` contains no variables: generates single file with all migrations

### File Discovery

lup2dbtool scans the input directory for:
- `init_m_*.sql` - Initial schema creation (e.g., `init_m_2_1_5.sql`)
- `upd_m_*.sql` - Schema updates (e.g., `upd_m_6_08_08.sql`)

Files are sorted by version number to ensure correct migration order.

### Idempotency

lup2dbtool guarantees:
- Same input files produce same output C++ code
- Running twice produces identical files (byte-for-byte)
- No timestamps or random values in generated code

## LupMigrationsPlugin

### Building

1. Generate migrations using lup2dbtool:
```bash
./out/build/linux-clang-debug/src/tools/lup2dbtool/lup2dbtool \
    --input-dir /path/to/legacy_sql_migrations_dir \
    --output ./src/tools/LupMigrationsPlugin/generated/GeneratedMigrations.cpp
```

2. Build the plugin:
```bash
cmake --build ./out/build/linux-clang-debug --target LupMigrationsPlugin
```

3. The plugin is output to `out/build/linux-clang-debug/plugins/LupMigrationsPlugin.so`

### Using with dbtool

```bash
./out/build/linux-clang-debug/src/tools/dbtool/dbtool migrate \
    --plugins-dir ./out/build/linux-clang-debug/plugins \
    --connection-string "DRIVER=SQLite3;Database=lup.db"
```

## TransitionGlue

The `TransitionGlue` class handles the transition from the legacy `LASTRADA_PROPERTIES` version tracking to the modern `schema_migrations` table.

### API

```cpp
namespace Lup {

class TransitionGlue {
public:
    /// Query current LUP version from LASTRADA_PROPERTIES table
    static std::optional<int64_t> GetCurrentLupVersion(SqlConnection& connection);

    /// Mark all migrations up to given version as applied in schema_migrations
    static size_t MarkMigrationsAsApplied(MigrationManager& manager, int64_t maxVersionInteger);

    /// Initialize transition - call once on first dbtool run
    static bool Initialize(MigrationManager& manager, SqlConnection& connection);

    /// Convert LUP version integer to migration timestamp
    static uint64_t VersionToTimestamp(int64_t versionInteger);
};

}
```

### Usage

Before running migrations on an existing LUP database, call `TransitionGlue::Initialize()`:

```cpp
#include <LupMigrationsPlugin/TransitionGlue.hpp>
#include <Lightweight/SqlMigration.hpp>

void MigrateExistingDatabase(SqlConnection& connection) {
    auto& manager = SqlMigration::MigrationManager::GetInstance();

    // Initialize transition - marks existing migrations as applied
    Lup::TransitionGlue::Initialize(manager, connection);

    // Now run any pending migrations
    manager.ApplyPendingMigrations();
}
```

### How It Works

1. `GetCurrentLupVersion()` queries `SELECT VALUE FROM LASTRADA_PROPERTIES WHERE NR = 4`
2. The version integer (e.g., 60808) is converted to a migration timestamp (20000000060808)
3. All migrations with timestamps <= that value are marked as applied in `schema_migrations`
4. New migrations (timestamps > current version) will be applied normally

## SQL to Migration API Mapping

| SQL Statement | Migration API |
|---------------|---------------|
| `CREATE TABLE t (...)` | `plan.CreateTable("t").PrimaryKey(...).Column(...)...` |
| `ALTER TABLE t ADD COLUMN c type NULL` | `plan.AlterTable("t").AddNotRequiredColumn("c", type)` |
| `ALTER TABLE t ADD COLUMN c type NOT NULL` | `plan.AlterTable("t").AddColumn("c", type)` |
| `ALTER TABLE t ADD FOREIGN KEY ...` | `plan.AlterTable("t").AddForeignKey(...)` |
| `INSERT INTO t VALUES (...)` | `plan.Insert("t").Set("col", val)...` |
| `UPDATE t SET c=v WHERE ...` | `plan.Update("t").Set("c", v).Where(...)` |
| `DELETE FROM t WHERE ...` | `plan.Delete("t").Where(...)` |

## SQL Type to C++ Type Mapping

| SQL Type | C++ Type |
|----------|----------|
| `INTEGER`, `INT` | `Integer()` |
| `SMALLINT` | `Smallint()` |
| `BIGINT` | `Bigint()` |
| `REAL`, `FLOAT`, `DOUBLE` | `Real()` |
| `CHAR(n)` | `Char(n)` |
| `VARCHAR(n)` | `Varchar(n)` |
| `NVARCHAR(n)` | `NVarchar(n)` |
| `NCHAR(n)` | `NChar(n)` |
| `TEXT`, `LONG VARCHAR` | `Text()` |
| `DATETIME`, `TIMESTAMP` | `DateTime()` |
| `DATE` | `Date()` |
| `TIME` | `Time()` |
| `BOOLEAN`, `BOOL`, `BIT` | `Bool()` |
| `GUID`, `UNIQUEIDENTIFIER` | `Guid()` |
| `DECIMAL(p,s)`, `NUMERIC(p,s)` | `Decimal(p, s)` |
| `VARBINARY(n)` | `VarBinary(n)` |

## Workflow

### Initial Setup (Existing Database)

```bash
# 1. Generate C++ migrations from SQL files
lup2dbtool --input-dir ./legacy_sql_migrations_dir --output ./generated/Migrations.cpp

# 2. Build the plugin
cmake --build ./build --target LupMigrationsPlugin

# 3. Initialize transition (marks existing migrations as applied)
# This is done automatically by TransitionGlue::Initialize()

# 4. Run any new migrations
dbtool --plugins-dir ./build/plugins migrate --connection-string "..."
```

### New Database

```bash
# 1. Generate C++ migrations
lup2dbtool --input-dir ./legacy_sql_migrations_dir --output ./generated/Migrations.cpp

# 2. Build and run all migrations
dbtool --plugins-dir ./build/plugins migrate --connection-string "..."
```

### Adding New Migrations

1. Create a new SQL file: `upd_m_X_YY_ZZ.sql`
2. Regenerate C++ code: `lup2dbtool --input-dir ./legacy_sql_migrations_dir --output ./generated/Migrations.cpp`
3. Rebuild the plugin
4. Run `dbtool migrate`

## Troubleshooting

### Encoding Issues
If you see garbled characters, check the input encoding:
```bash
file -i your_file.sql  # Check actual encoding
lup2dbtool --input-encoding utf-8 ...  # Use utf-8 if needed
```

### Missing Tables
If `TransitionGlue::GetCurrentLupVersion()` returns `nullopt`, the legacy sql migration tracking table doesn't exist. This is expected for new databases.

### Version Mismatch
If migrations run but the version seems wrong, check that all SQL files are in the input directory and properly named.
