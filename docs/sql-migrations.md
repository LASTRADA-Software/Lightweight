# SQL Migrations {#sql-migrations}

## Introduction

SQL migrations provide a structured way to evolve your database schema over time.
Each migration represents a discrete change (creating tables, adding columns, etc.)
that can be applied or reverted independently.

Key benefits:
- Version control for database schema
- Reproducible database setup across environments
- Safe rollback capabilities
- Checksum verification to detect unauthorized changes

## Creating Migrations

### Using the LIGHTWEIGHT_SQL_MIGRATION Macro

The simplest way to create a migration:

```cpp
#include <Lightweight/SqlMigration.hpp>

LIGHTWEIGHT_SQL_MIGRATION(20260126120000, "Create users table")
{
    plan.CreateTable("users")
        .PrimaryKeyWithAutoIncrement("id")
        .RequiredColumn("email", Varchar(255))
        .RequiredColumn("name", Varchar(100))
        .Column("phone", Varchar(20))
        .Timestamps();
}
```

The migration is automatically registered with the MigrationManager when the program starts.

### Using the Migration Class

For more control, including rollback support:

```cpp
#include <Lightweight/SqlMigration.hpp>

using namespace Lightweight::SqlMigration;

static Migration createUsersTable(
    MigrationTimestamp { 20260126120000 },
    "Create users table",
    // Up migration
    [](SqlMigrationQueryBuilder& plan) {
        plan.CreateTable("users")
            .PrimaryKeyWithAutoIncrement("id")
            .RequiredColumn("email", Varchar(255))
            .Timestamps();
    },
    // Down migration (optional)
    [](SqlMigrationQueryBuilder& plan) {
        plan.DropTable("users");
    }
);
```

### Timestamp Format

Migration timestamps use the format **YYYYMMDDHHMMSS** (14 digits):

```cpp
MigrationTimestamp { 20260126143052 }  // 2026-01-26 14:30:52
```

Timestamps must be:
- **Unique** across all migrations
- **Monotonically increasing** (newer migrations have higher timestamps)

### Plugin Macro for Shared Libraries

When creating migrations in a shared library plugin, add this macro to exactly one source file:

```cpp
#include <Lightweight/SqlMigration.hpp>

LIGHTWEIGHT_MIGRATION_PLUGIN()

// Your migrations here...
LIGHTWEIGHT_SQL_MIGRATION(20260126120000, "Create users table")
{
    // ...
}
```

The `LIGHTWEIGHT_MIGRATION_PLUGIN()` macro exports the `AcquireMigrationManager()` function
that dbtool uses to load migrations from the plugin.

## Table Operations

### CreateTable

Create a new table with various column types:

```cpp
plan.CreateTable("posts")
    .PrimaryKeyWithAutoIncrement("id", Bigint())
    .RequiredColumn("title", Varchar(200))
    .Column("body", Text())
    .RequiredColumn("published", Bool())
    .RequiredForeignKey("user_id", Bigint(), { .tableName = "users", .columnName = "id" })
    .Timestamps();
```

**Column modifiers** (chain after column declaration):
- `.Unique()` - Add unique constraint
- `.Index()` - Create an index on this column
- `.UniqueIndex()` - Create a unique index

```cpp
plan.CreateTable("users")
    .PrimaryKeyWithAutoIncrement("id")
    .RequiredColumn("email", Varchar(255)).Unique().Index()
    .RequiredColumn("username", Varchar(50)).UniqueIndex();
```

**Conditional creation:**

```cpp
plan.CreateTableIfNotExists("users")
    .PrimaryKeyWithAutoIncrement("id")
    .RequiredColumn("email", Varchar(255));
```

### AlterTable

Modify an existing table:

```cpp
plan.AlterTable("users")
    .AddColumn("phone", Varchar(20))
    .AddNotRequiredColumn("nickname", Varchar(50))
    .RenameColumn("email", "email_address")
    .AddIndex("email_address")
    .AddUniqueIndex("phone")
    .DropColumn("legacy_field");
```

**Available operations:**

| Method | Description |
|--------|-------------|
| `.AddColumn(name, type)` | Add a non-nullable column |
| `.AddNotRequiredColumn(name, type)` | Add a nullable column |
| `.RenameColumn(old, new)` | Rename a column |
| `.DropColumn(name)` | Remove a column |
| `.AlterColumn(name, type, nullable)` | Change column type or nullability |
| `.AddIndex(column)` | Create an index |
| `.AddUniqueIndex(column)` | Create a unique index |
| `.DropIndex(column)` | Remove an index |
| `.AddForeignKey(column, ref)` | Add foreign key to existing column |
| `.AddForeignKeyColumn(name, type, ref)` | Add new column with foreign key |
| `.DropForeignKey(column)` | Remove foreign key constraint |
| `.RenameTo(newName)` | Rename the table |

**Conditional operations:**

```cpp
plan.AlterTable("users")
    .AddColumnIfNotExists("phone", Varchar(20))
    .DropColumnIfExists("obsolete_field")
    .DropIndexIfExists("old_index");
```

### DropTable

Remove a table:

```cpp
plan.DropTable("obsolete_table");
```

**Conditional drop:**

```cpp
plan.DropTableIfExists("maybe_exists");
```

**Cascade drop** (removes foreign key constraints):

```cpp
plan.DropTableCascade("table_with_dependencies");
```

## Data Manipulation

### Insert

Insert data during migrations:

```cpp
plan.Insert("settings")
    .Set("key", "app_version")
    .Set("value", "1.0.0");
```

### Update

Update existing data:

```cpp
plan.Update("settings")
    .Set("value", "2.0.0")
    .Where("key", "=", "app_version");
```

### Delete

Remove data:

```cpp
plan.Delete("settings")
    .Where("key", "=", "deprecated_setting");
```

### CreateIndex

Create standalone indexes:

```cpp
plan.CreateIndex("idx_users_email", "users", {"email"});
plan.CreateUniqueIndex("idx_users_phone", "users", {"phone"});
```

Composite indexes:

```cpp
plan.CreateIndex("idx_posts_user_date", "posts", {"user_id", "created_at"});
```

## Raw SQL

For database-specific features or complex operations:

```cpp
plan.RawSql("CREATE EXTENSION IF NOT EXISTS pgcrypto");
plan.RawSql("ALTER TABLE users ADD CONSTRAINT check_age CHECK (age >= 0)");
```

## SQL Column Types

| C++ Type | SQL Type | Notes |
|----------|----------|-------|
| `Integer()` | INTEGER | 32-bit integer |
| `Smallint()` | SMALLINT | 16-bit integer |
| `Bigint()` | BIGINT | 64-bit integer |
| `Tinyint()` | TINYINT | 8-bit integer |
| `Real()` | REAL/FLOAT | Floating point |
| `Bool()` | BOOLEAN/BIT | Boolean |
| `Char(n)` | CHAR(n) | Fixed-length string |
| `Varchar(n)` | VARCHAR(n) | Variable-length string |
| `NChar(n)` | NCHAR(n) | Fixed-length Unicode string |
| `NVarchar(n)` | NVARCHAR(n) | Variable-length Unicode string |
| `Text()` | TEXT | Large text |
| `DateTime()` | DATETIME/TIMESTAMP | Date and time |
| `Date()` | DATE | Date only |
| `Time()` | TIME | Time only |
| `Guid()` | UNIQUEIDENTIFIER/UUID | UUID/GUID |
| `Decimal(p, s)` | DECIMAL(p, s) | Fixed-point number |
| `Binary(n)` | BINARY(n) | Fixed-length binary |
| `VarBinary(n)` | VARBINARY(n) | Variable-length binary |

Usage:

```cpp
using namespace Lightweight::SqlColumnTypeDefinitions;

plan.CreateTable("example")
    .PrimaryKeyWithAutoIncrement("id", Bigint())
    .RequiredColumn("name", Varchar(100))
    .Column("price", Decimal { .precision = 10, .scale = 2 })
    .Column("created_at", DateTime());
```

## Migration Manager API

### Applying Migrations Programmatically

```cpp
#include <Lightweight/SqlMigration.hpp>

using namespace Lightweight::SqlMigration;

// Get the singleton instance
auto& manager = MigrationManager::GetInstance();

// Create the schema_migrations table if it doesn't exist
manager.CreateMigrationHistory();

// Apply all pending migrations
size_t applied = manager.ApplyPendingMigrations(
    [](MigrationBase const& m, size_t current, size_t total) {
        std::println("[{}/{}] Applying {} - {}",
            current + 1, total, m.GetTimestamp().value, m.GetTitle());
    }
);

std::println("Applied {} migrations", applied);
```

### Status & Verification

```cpp
// Get migration status summary
auto status = manager.GetMigrationStatus();
std::println("Applied: {}, Pending: {}", status.appliedCount, status.pendingCount);

// Verify checksums of applied migrations
auto mismatches = manager.VerifyChecksums();
for (auto const& result : mismatches) {
    if (!result.matches) {
        std::println("Checksum mismatch for {}: stored={}, computed={}",
            result.timestamp.value, result.storedChecksum, result.computedChecksum);
    }
}
```

### Preview (Dry-Run)

Generate SQL without executing:

```cpp
auto statements = manager.PreviewPendingMigrations(
    [](MigrationBase const& m, size_t i, size_t n) {
        std::println("-- Migration: {} - {}", m.GetTimestamp().value, m.GetTitle());
    }
);

for (auto const& sql : statements) {
    std::println("{};", sql);
}
```

### Rollback

Revert migrations:

```cpp
// Revert a single migration
auto const* migration = manager.GetMigration(MigrationTimestamp { 20260126120000 });
if (migration) {
    manager.RevertSingleMigration(*migration);
}

// Revert all migrations after a timestamp
auto result = manager.RevertToMigration(
    MigrationTimestamp { 20260101000000 },
    [](MigrationBase const& m, size_t i, size_t n) {
        std::println("Rolling back {} - {}", m.GetTimestamp().value, m.GetTitle());
    }
);

if (result.failedAt) {
    std::println("Failed at {}: {}", result.failedAt->value, result.errorMessage);
}
```

### Mark as Applied

Mark a migration as applied without executing:

```cpp
auto const* migration = manager.GetMigration(MigrationTimestamp { 20260126120000 });
if (migration) {
    manager.MarkMigrationAsApplied(*migration);
}
```

## Migration Tracking

### schema_migrations Table

Lightweight automatically creates a `schema_migrations` table to track applied migrations:

| Column | Type | Description |
|--------|------|-------------|
| version | BIGINT | Migration timestamp |
| checksum | VARCHAR(64) | SHA-256 checksum of migration SQL |
| applied_at | DATETIME | When the migration was applied |

### Concurrency Control

Use `MigrationLock` to prevent concurrent migrations:

```cpp
#include <Lightweight/SqlMigrationLock.hpp>

using namespace Lightweight::SqlMigration;

auto& connection = manager.GetDataMapper().Connection();
MigrationLock lock(connection, "my_migration_lock", std::chrono::seconds(30));

if (lock.IsLocked()) {
    manager.ApplyPendingMigrations();
}
```

The lock implementation uses database-specific mechanisms:
- **SQL Server**: `sp_getapplock` / `sp_releaseapplock`
- **PostgreSQL**: `pg_advisory_lock` / `pg_advisory_unlock`
- **SQLite**: `BEGIN IMMEDIATE` with `PRAGMA busy_timeout`

## Best Practices

1. **Always write Down()** - Even if rollback is unlikely, having a Down() implementation enables recovery from mistakes.

2. **Test migrations on a copy** - Apply migrations to a test database before production.

3. **Use descriptive titles** - Migration titles should explain the purpose:
   ```cpp
   LIGHTWEIGHT_SQL_MIGRATION(20260126120000, "Add email verification fields to users")
   ```

4. **Keep migrations small** - One logical change per migration is easier to understand and rollback.

5. **Never modify applied migrations** - Instead of editing an existing migration, create a new one. Checksums will detect modifications.

6. **Use conditional operations** - Use `IfNotExists`/`IfExists` variants when idempotency is important.

7. **Backup before migrating** - Use `dbtool backup` before applying migrations to production.

8. **Review dry-run output** - Always preview migrations with `--dry-run` before applying:
   ```bash
   dbtool migrate --dry-run
   ```

## See Also

- @ref dbtool - Command-line tool for managing migrations
- @ref Lightweight::SqlMigrationQueryBuilder - Query builder API reference
- @ref Lightweight::SqlMigration::MigrationManager - Migration manager API reference
