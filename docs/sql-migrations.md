# SQL Migrations

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

The migration is automatically registered with the MigrationManager when the program starts, and
unregistered again when it is destroyed. Registration is tied to the object's lifetime, so a
migration that does not live for the whole program -- one with automatic storage duration, say --
leaves no dangling pointer behind in the manager it registered with.

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

A plugin can also export two optional hooks, described in
[dbtool.md](dbtool.md#optional-post-init-hook):

| Macro | Runs |
|-------|------|
| `LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)` | before every dbtool command, read-only ones included; failures are logged only |
| `LIGHTWEIGHT_MIGRATION_PLUGIN_POSTMIGRATE(connection, manager)` | after a successful `migrate` / `migrate-to-release` / `apply`, under the migration lock; a failure fails the command |

The post-init hook may also call `manager.AddPreMigrateStep(...)` to register work that runs before
the first migration; see [Pre-migrate steps](#pre-migrate-steps).

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

> **SQLite note:** SQLite has no native `ALTER TABLE … ALTER COLUMN` or `… ADD/DROP CONSTRAINT`, so
> `.AlterColumn(...)`, `.AddForeignKey(...)`, and `.DropForeignKey(...)` are applied by rebuilding the
> table. That rebuild runs only when the migration is applied through `MigrationManager`
> (`ApplyPendingMigrations()`); the generated `ToSql()` text for these operations is a
> `-- LIGHTWEIGHT_SQLITE_GUARD:` sentinel comment that does nothing if executed directly. Applying such
> a migration via `SqlStatement::MigrateDirect` throws rather than silently skipping the change.

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

#### Rows that may already exist (`IfNotExists`)

A database may already contain a reference row that a later migration inserts, for example one
applied earlier by a hotfix. A plain `Insert` then fails with a duplicate key and stops the whole
migration. `IfNotExists` names the key columns that identify the row and makes the insert skip it
when a row with the same key values exists:

```cpp
plan.Insert("Categories")
    .Set("Id", 200)
    .Set("Name", "Books")
    .IfNotExists({ "Id" });
```

It runs as one statement on every backend, with the table schema-qualified the same way as a plain
insert. Run through `MigrationManager` (and so through `dbtool`), every value is bound as a
parameter:

```sql
INSERT INTO "Categories" ("Id", "Name") SELECT ?, ?
WHERE NOT EXISTS (SELECT 1 FROM "Categories" WHERE "Id" = ?)
```

- Checksums and previews (`--dry-run`, `ToSql`) show the same statement with the values as
  literals, rendered exactly as a plain `Insert` renders them, so a checksum does not depend on how
  the values reach the database. Adding `IfNotExists` changes a migration's checksum; a plain
  `Insert` renders, checksums and runs exactly as before. `ToBoundSql` returns the bound form.
- `SqlStatement::MigrateDirect` and other callers that execute `ToSql()` output run the literal
  form instead; it skips existing rows the same way.
- UTF-8 text is bound as UTF-16, so it is stored exactly as its literal would store it, in wide and
  narrow columns alike. Integers and NULL (into any column type) store identically, too.
- On SQL Server, a migration with an `IfNotExists` insert first runs `SET NOCOUNT OFF`, since the
  skip is read from the affected-row count; the setting stays in effect for the session afterwards.
- A GUID, date, time or timestamp is bound as the text its literal quotes, so the server parses
  the same text either way. Bound natively, a time or timestamp would keep the fraction the literal
  drops (a timestamp literal has milliseconds), and SQLite would store a timestamp in a different
  text form, which a key comparison then never matches.
  On SQL Server, a date sent as text into a DATETIME or SMALLDATETIME column under a session
  language with DMY date order (e.g. `SET LANGUAGE British`) is read as `yyyy-dd-mm`; a plain
  `Insert` literal is read the same way, and a DATE or DATETIME2 column is not affected.
- `SqlDynamicNumeric` (also at high scale such as DECIMAL(28,12)), binary values and `bool` are
  bound natively and store exactly as their literals.
- A key column of any of these types finds a row a plain `Insert` stored.
- A `double` is bound as its binary value. In FLOAT/REAL columns and in DECIMAL columns of low scale
  (e.g. `40.3399`, `0.5`, `-12.25` into DECIMAL(10,4)) it stores exactly as the literal of a plain
  `Insert`. Known divergences, where the literal is parsed as a decimal instead:
  - a DECIMAL column with more fractional digits than the double holds exactly: `123456.789012`
    into DECIMAL(28,12) stores `123456.789011999994` bound and `123456.789012000000` as a literal;
  - SQLite parses some literals 1-2 ULP away from the correctly rounded double (e.g.
    `0.30000000000000004`, `1e-7`), which the bound value carries;
  - a double into a text column: SQLite stores `3.0` bound as `"3.0"`, as a literal as `"3"`.

  Do not key `IfNotExists` on such a double: in these cases the bound key misses a row a plain
  `Insert` stored, and the insert then adds a duplicate. Use `SqlNumeric` / `SqlDynamicNumeric`
  for exact decimal values.
- An existing row is left as it is, **even when its non-key columns differ** from the values in
  the migration. `IfNotExists` only prevents the duplicate; it never updates.
- Several key columns are combined with `AND`: `.IfNotExists({ "Kind", "Id" })`.
- Rendering the plan throws `std::invalid_argument` when a key column is not one of the `Set`
  columns, or when its value is NULL (`=` never matches NULL, so the row would be inserted again on
  every run). `IfNotExists({})` throws right away.
- Each skipped row is counted (`MigrationManager::SkippedExistingRowCount()`) and reported through
  the manager's log sink as `Skipped existing row: <timestamp> <table> (<key>=<value>, ...)`. See
  [dbtool](dbtool.md#skipped-rows) for how `dbtool` prints it.

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

#### Partial (filtered) indexes

Pass a `SqlCreateIndexOptions` to index only the rows that match a predicate. SQLite and
PostgreSQL call this a *partial* index, SQL Server a *filtered* index; all three accept the
same `WHERE` clause after the column list.

A typical use is deduplicating on an optional key: rows that have a key must be unique, rows
without one may repeat. An unconditional unique index would reject the second row with an
empty key.

```cpp
plan.CreateIndex("idx_queue_key", "queue", {"dedup_key"},
                 {.type = IndexType::Unique, .whereExpression = R"("dedup_key" <> '')"});
// CREATE UNIQUE INDEX "idx_queue_key" ON "queue" ("dedup_key") WHERE "dedup_key" <> ''
```

`whereExpression` is the predicate body without the leading `WHERE`. It is emitted verbatim,
so quote identifiers and literals yourself. To stay portable, keep to what SQL Server accepts
in a filter: comparisons of a column with a constant, `IS [NOT] NULL` and `IN (...)` lists,
joined with `AND` (no `OR`, functions or computed expressions).

> **SQL Server:** a filtered index requires `ANSI_WARNINGS ON` both to be created and for every
> `INSERT`, `UPDATE` and `DELETE` on its table; otherwise SQL Server fails the statement with
> error 1934. Lightweight's default string-truncation mode (`SqlStringTruncationMode::Truncate`)
> turns `ANSI_WARNINGS` off, so connections that apply or write through a filtered index must
> use `SqlStringTruncationMode::Error`, set per connection with
> `SqlConnection::SetStringTruncationMode` or for new connections with
> `SqlConnection::SetDefaultStringTruncationMode`. SQLite and PostgreSQL have no such requirement.

> **Limitation:** the backup tool (`dbtool backup` / `SqlBackup`) reads indexes from the live
> schema and does not capture index predicates yet. It backs a partial index up as an index
> over all rows, and a single-column partial *unique* index can also mark its column unique.
> Restoring data that relies on the predicate (such as two rows with an empty `dedup_key`)
> therefore fails for such tables.

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

### Custom Default Schema

When the target database expects unqualified DDL to land in a non-default
schema (e.g. `lasa` instead of `dbo` / `public`), tell the manager about it
*before* opening the first connection:

```cpp
auto& manager = MigrationManager::GetInstance();
manager.SetDefaultSchema("lasa"); // empty disables
manager.CreateMigrationHistory();
manager.ApplyPendingMigrations();
```

`SetDefaultSchema` installs a post-connect hook that emits the dialect-
specific "make this the session default" statement for every new connection:

- **PostgreSQL** — `SET search_path TO "lasa", public`. Both
  `schema_migrations` and unqualified DDL inside migrations land in `lasa`.
- **SQL Server** — no portable session-level switch exists; the hook is a
  no-op. Configure the connecting login's `DEFAULT_SCHEMA` server-side
  (`ALTER USER … WITH DEFAULT_SCHEMA = lasa`). Migrations that need to write
  to a specific schema regardless of the login default should use the
  `WithSchema(...)` builder.
- **SQLite** — no schema concept; the hook is a no-op.

On PostgreSQL a `CreateTable` built with `WithSchema(...)` names the schema for the table and its
indexes (earlier releases created them in the `search_path` schema). So the schema must exist, and
a database migrated by an earlier release reports checksum mismatches for those migrations until
`dbtool rewrite-checksums` rewrites them. The table's inline foreign keys still name the referenced
table without a schema; they resolve through the `search_path`, so the referenced table must lie
in a schema on the `search_path`.

Schema names are validated against `[A-Za-z0-9_]` and rejected via
`std::invalid_argument` otherwise. Passing `""` clears any previously
installed hook.

`dbtool` exposes this via the `--schema` flag and `dbtool-gui` exposes it as
a "Schema" input on both the Profile and the direct-ODBC connection tabs.

### Progress reporting

Long-running library calls report their work through `Lightweight::ProgressTask`, an interface the
host (a CLI, a GUI) implements and passes in; the library only calls it. Tasks nest: a task opens
sub-tasks with `BeginSubTask`, advances with `Advance`, and destroying a task marks it finished. A
total of 0 makes a task indeterminate (the host shows that it runs, not how far it got). Long loops
poll `IsCancellationRequested()` at safe points. `UnicodeUpgradeTables` takes an `ExecutionMode`
and a `ProgressTask*` (null reports nothing); `RunPreMigrateSteps` takes a `ProgressTask&`.
`NullProgressTask` reports nothing and is never cancelled, for hosts that pass none.
`UnicodeUpgradeTables` reports planning, an overall task weighted by the planned tables' row counts,
a task per unit and per table. It checks for cancellation after each committed unit except the last and
then returns with `UnicodeUpgradeResult::wasCancelled` set (`committedUnitCount` says how many units it
committed); a later call continues with the next unit. `dbtool` draws the task tree with its `--progress`
styles. A task that fails calls `Fail(reason)` before it ends, so the host shows it as failed (a row that
stays visible), not as done.

### Pre-migrate steps

A plugin can register work that runs before the first pending migration, such as widening columns
the migrations then rely on. Register it from the post-init hook:

```cpp
MigrationManager::AddPreMigrateStep(std::string title,
                                    PreMigrateStepOptions options,
                                    PreMigrateStep step);
// PreMigrateStep = std::function<void(SqlConnection&, MigrationManager&, ProgressTask&, ExecutionMode)>
```

- **When they run:** `migrate`, `apply` and `migrate-to-release` run the steps in registration order
  whenever they reach their apply phase, also with nothing pending (`apply` once its migration is
  found; `migrate-to-release` after its "already at or past" check). Status and read-only commands
  never run them.
- **Lock and connection:** a step gets the migration connection, on which the command holds the migration
  lock unless it opted out of locking.
- **Dry run:** with `ExecutionMode::DryRun` the step reports its plan and changes nothing. A dry run
  calls the steps, like it previews migrations, without taking the lock.
- **Progress:** each step runs under its own sub-task of the command's `ProgressTask`, titled like the step.
- **Failure:** a step that throws stops the command with `PreMigrateStepError` before any migration runs;
  later steps do not run, and the step's task ends as failed. A step that can live with a problem calls
  `progress.Log(ProgressTask::LogLevel::Warning, ...)` and returns normally; the migrations then continue.
- **Cancellation:** `RunPreMigrateSteps` checks `IsCancellationRequested()` before and after each step and
  returns `PreMigrateStepsOutcome::Cancelled`; the caller then applies no migration.
- **Lifetime:** a step is code of its plugin. Hosts call `RemoveAllPreMigrateSteps()` before running post-init
  hooks again (a reconnect) and before unloading a plugin, so a step is never registered twice nor outlives
  its library.
- **`requiresBackup`:** an optional predicate in `PreMigrateStepOptions`, evaluated on the migration
  connection before the run, says whether this run of the step demands a backup.

```cpp
LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    if (NeedsTextWidening(connection))
        manager.AddPreMigrateStep("Widen text columns",
                                  { .requiresBackup = [](SqlConnection&, MigrationManager& m) {
                                        return !m.UnicodeUpgradeTables(ExecutionMode::DryRun).columns.empty();
                                    } },
                                  [](SqlConnection&, MigrationManager& m, ProgressTask& progress, ExecutionMode mode) {
                                      std::ignore = m.UnicodeUpgradeTables(mode, &progress);
                                  });
}
```

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

### Compatibility Flags

Some databases predate the migration set — they were created by older tooling and differ from
what the migrations declare. A *compat policy* returns, per migration, a set of opt-in flags that
relax strict rendering for exactly those migrations. Migration plugins usually install one; it can
also be set directly:

```cpp
manager.SetCompatPolicy([](SqlMigration::MigrationBase const& migration) {
    return std::set<std::string> { std::string(CompatFlagFkMatchReferencedTextName) };
});
```

| Flag | Effect |
|------|--------|
| `lup-truncate` | String values in `Insert` / `Update` steps are truncated to the destination column's width, with a warning, instead of failing. |
| `fk-match-referenced-text` | A foreign-key column declared by `CreateTable` / `AlterTable` (`AddForeignKeyColumn`) takes the narrow/wide variant of the column it references when the two differ only in that respect (`CHAR(n)`↔`NCHAR(n)`, `VARCHAR(n)`↔`NVARCHAR(n)`). The referenced column's type is read from the live database (or from an earlier migration of the same run), and each change is logged as a warning. SQL Server only — PostgreSQL and SQLite store both kinds the same way. |

Flags only change the SQL sent to the database. Checksums are computed from the migration as
declared, so the same migration has the same checksum on every database.

A policy installed by a plugin is code from that plugin's library. `dbtool` and `dbtool-gui`
clear the policy, the migrations and the releases before they unload plugins
(`Lightweight::Tools::UnloadPlugins`): at exit, after a failed plugin load, and in `dbtool-gui`
whenever it reloads the plugin list, where the reloaded plugins' static initializers install
their policy again. A host that loads plugins itself must do the same, or the manager calls into
an unloaded library: at the next migration it applies, or in its destructor when the process exits.

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

Use `SqlScopedLock` (the generic distributed-lock RAII type) to prevent
concurrent migrations:

```cpp
#include <Lightweight/SqlScopedLock.hpp>

auto& connection = manager.GetDataMapper().Connection();
SqlScopedLock lock { connection, "lightweight_migration", std::chrono::seconds(30) };
manager.ApplyPendingMigrations();
// Lock released automatically at scope exit.
```

`SqlScopedLock` is **not migration-specific** — any caller that needs a
named cross-process token can use it (cron leadership, "only one worker
processes batch X", queue ownership, …). Pick any string for the lock
name; two processes that pass the same string will serialise on it.

For structured error handling — distinguishing timeout from deadlock
from driver error programmatically — use the non-throwing
`SqlScopedLock::TryConstruct(connection, name, timeout)` factory, which
returns `std::expected<SqlScopedLock, SqlLockError>`.

The dialect-specific primitive is selected automatically by the active
`SqlQueryFormatter`:
- **SQL Server**: `sp_getapplock` / `sp_releaseapplock`
- **PostgreSQL**: `pg_advisory_lock` / `pg_advisory_unlock`
- **SQLite**: `_lightweight_locks` table guarded by a unique constraint

On SQLite the bookkeeping table `_lightweight_locks` is treated as
infrastructure — `dbtool hard-reset` drops it alongside
`schema_migrations` rather than mistaking it for user data.

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

- [dbtool.md](dbtool.md) - Command-line tool for managing migrations
- @ref Lightweight::SqlMigrationQueryBuilder - Query builder API reference
- @ref Lightweight::SqlMigration::MigrationManager - Migration manager API reference
