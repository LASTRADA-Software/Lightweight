# dbtool - Database Management CLI

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

### Pre-built installers

Released artifacts ship `dbtool` and `dbtool-gui` together with every runtime
DLL or shared object they need. They do **not** include an ODBC driver — you
must install the appropriate driver for your database separately (e.g.,
Microsoft ODBC Driver for SQL Server, PostgreSQL ODBC, or SQLite ODBC).

`dbtool-gui` additionally offers **managed backups**: pick a single folder on
the Settings page and the Backups page keeps one `<profile>.zip` per
configured profile there, with one-click "back up all", safe atomic
overwrite, and restore into any profile or an ad-hoc connection string. See
`src/tools/dbtool-gui/README.md` for details; this is a GUI-only convenience
layer over the same `backup`/`restore` primitives described below.

| Platform           | Artifact                          | Install command                                     |
|--------------------|-----------------------------------|------------------------------------------------------|
| Windows (x86_64)   | `Lightweight-<version>-win64.msi` | `msiexec /i Lightweight-<version>-win64.msi`         |
| Debian / Ubuntu    | `lightweight_<version>_amd64.deb` | `sudo dpkg -i lightweight_<version>_amd64.deb`       |
| Fedora / RHEL      | `lightweight-<version>.x86_64.rpm`| `sudo dnf install lightweight-<version>.x86_64.rpm`  |
| Portable (Linux)   | `Lightweight-<version>-Linux.tar.gz` | extract anywhere; run from `bin/`                  |

Windows installs into `C:\Program Files\Lightweight\bin\`. Linux packages
install into `/usr/bin/` with `Lightweight.so.<version>` under `/usr/lib/`.

### Building from source

```bash
cmake --preset clang-release
cmake --build out/build/clang-release --target dbtool
```

The binary will be located at `out/build/clang-release/src/tools/dbtool/dbtool`.

### Producing the installer locally

```bash
# Linux (TGZ + DEB + RPM)
cmake --preset gcc-release
cmake --build --preset gcc-release
cpack --preset gcc-release

# Windows (WiX MSI)
cmake --preset clangcl-release -DLIGHTWEIGHT_BUILD_GUI=ON
cmake --build --preset clangcl-release
cpack --preset clangcl-release
```

The resulting artefacts land in `out/package/<preset>/`.

## Configuration

### Connection String

dbtool requires a database connection string. The first of these that supplies one wins:

1. **Command-line option**: `--connection-string "..."`.
2. **A named profile**: `--profile <NAME>`, from `--config <FILE>` or from the `dbtool.yml` that
   discovery finds (see below).
3. **An explicit config file**: `--config <FILE>` without `--profile` uses that file's `defaultProfile`.
4. **Environment variable**: `SQL_CONNECTION_STRING`, then `ODBC_CONNECTION_STRING`.
5. **A discovered config file**: the `defaultProfile` of the `dbtool.yml` that discovery finds.

The settings of a profile (`schema`, `pluginsDir`, `backupDir`) and the file-wide defaults that
stand in for them (`defaultPluginsDir`, `defaultBackupDir`) apply only when that profile is
selected: named by `--profile`, the default of `--config`, or the discovered default that supplies
the connection. `--connection-string` without `--profile` applies no profile. When the environment
variable overrides a discovered `defaultProfile`, neither that profile's settings nor the file's
`defaultPluginsDir` / `defaultBackupDir` apply, so plugins come from `--plugins-dir` or the current
directory. A discovered profile without a connection of its own (for example a legacy file with
only `PluginsDir`) still applies. A file that sets no `defaultProfile` uses its first profile as
the default.

**Commands that write never use a discovered `defaultProfile`.** Only rule 5 is implicit: a
`dbtool.yml` in a parent directory, next to the executable or in the per-user location can
point at a database you did not mean. So a command that can change the database refuses a
target that only rule 5 supplies, names the target and the file, and exits non-zero; pass
`--profile`, `--config` or `--connection-string`, or set `SQL_CONNECTION_STRING`. These commands
are `migrate`, `migrate-to-release`, `apply`, `rollback`, `rollback-to`, `rollback-to-release`,
`mark-applied`, `rewrite-checksums`, `hard-reset`, `unicode-upgrade-tables`, `restore` and `exec`.
With `--dry-run`, `migrate`, `migrate-to-release`, `rewrite-checksums`, `hard-reset`,
`unicode-upgrade-tables` and `restore` only read, and `status`, `list-pending`, `list-applied`,
`releases` and `backup` always do.

**A plugin post-init hook counts as writing.** Every command that loads migration plugins
(`status`, `list-pending`, `list-applied`, `releases` and every migration command, `--dry-run`
included) runs each plugin's `LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT` hook against the database
first, and a hook may write (for example, one that seeds reference data). So a read-only
command uses a discovered `defaultProfile` only when no loaded plugin exports a post-init hook;
otherwise it refuses, names the plugins, and exits non-zero. The hooks are never skipped
instead, because a bridging hook changes what `status` reports. Choose a target explicitly, or use
a `--plugins-dir` without those plugins. `backup` and `restore` load no plugins.

Against a discovered target dbtool itself creates no missing SQLite database file or folder (the
SQLite ODBC driver may still create an empty file when it connects, as it always did), and it
does not encrypt a plaintext password in that `dbtool.yml` (see
[Profile passwords](#profile-passwords)).

Before it starts, a command that writes prints its target to stderr: server, port, database or
DSN, and where the target came from (never a password):

```
Target: server 127.0.0.1,1444, database inventory (from profile 'inventory' from D:\cfg\dbtool.yml (--config))
Target: database D:\data\b.db (from environment variable SQL_CONNECTION_STRING)
```

`--profile` that no config file defines is an error, also when no `dbtool.yml` is found at all.

### Where dbtool finds `dbtool.yml`

The first match wins:

1. `--config <FILE>` (a missing file is an error);
2. `dbtool.yml` in the current directory or any parent directory — so a project can keep its
   own `dbtool.yml` at its root and every subdirectory picks it up;
3. `dbtool.yml` in the directory of the `dbtool` executable or any parent directory — handy for
   portable installs that ship a config next to the binary;
4. the per-user file: `~/.config/dbtool/dbtool.yml` (Linux/macOS, honouring `$XDG_CONFIG_HOME`)
   or `%APPDATA%\dbtool\dbtool.yml` (Windows). When `LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR` is set
   to an absolute directory, the per-user file is `dbtool.yml` in that directory instead, which
   keeps tests and scripted runs off the user's own file (a relative path resolves against the
   working directory).

`dbtool list-profiles` shows which file was used and why; `--verbose` prints it for every other
command. `dbtool-gui` uses the same lookup unless a profile-store path is set in its Settings.

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
defaultBackupDir: /var/backups/lightweight  # store-wide fallback backup folder
profiles:
  prod:
    schema: dbo
    backupDir: /var/backups/lightweight/prod  # per-profile override of defaultBackupDir
    connectionString: "DRIVER={ODBC Driver 18 for SQL Server};Server=...;Database=prod"
  dev:
    pluginsDir: ./dev-plugins       # per-profile override
    connectionString: "DRIVER=SQLite3;Database=dev.db"
```

`defaultPluginsDir` also accepts a YAML sequence to fan out plugin discovery
across multiple directories — handy when a vendor ships a shared plugin set
under one prefix and the operator keeps local overrides under another:

```yaml
defaultPluginsDir:
  - ./plugins                       # local / operator-owned plugins
  - /opt/lightweight/plugins        # vendor-shipped baseline
```

When the same plugin filename appears in more than one of those directories,
dbtool keeps the file with the newest modification time and discards the
others — so dropping a fresher build into `./plugins` reliably shadows the
baseline copy without having to rebuild the YAML. Filename comparison is
case-insensitive on Windows and case-sensitive elsewhere; when two
same-named files share the same modification time, the directory listed
earlier in the sequence wins.

Plugins load in a fixed order: directory by directory in the order they are
listed, and within one directory by file name, compared ordinally (character
code by character code, so case-sensitively: `Zeta` sorts before `alpha`). A
plugin that shadows a same-named one from an earlier directory loads in the
place of the one it shadows. The order in which the filesystem lists a
directory plays no part, so the load order is the same on every machine.

Pass `--verbose` (`-v`) to see one stderr line per discarded duplicate, e.g.:

```
plugin 'pricing.dll' from ./plugins/pricing.dll shadowed by /opt/lightweight/plugins/pricing.dll (newer mtime)
```

The effective plugin directory for a given run resolves as: `--plugins-dir`
CLI option → profile's own `pluginsDir` → top-level `defaultPluginsDir`
(possibly a list) → current working directory. The profile and file steps apply only when the
file's profile is selected (see [Connection String](#connection-string)): not with
`--connection-string` alone, and not when `SQL_CONNECTION_STRING` overrides a discovered
`defaultProfile`.

#### Backup folder (`defaultBackupDir` / `backupDir`)

`defaultBackupDir` names a folder backups are written to, and a profile can override it with its
own `backupDir`. This lets a deployment (an installer, say) pre-configure where backups go, so
`dbtool` and `dbtool-gui` use the same folder without every user setting it up. The effective
folder resolves like `pluginsDir`: the profile's own `backupDir` → top-level `defaultBackupDir` →
none (the behaviour described below for `--output`).

- [`dbtool backup`](#backup) uses it for a bare file name in `--output`, or for a generated name
  when `--output` is omitted. A path with a directory part is used exactly as given. The folder is
  created when it is missing.
- `dbtool-gui` uses `defaultBackupDir` as its default backup folder, unless a folder was chosen in
  its Settings. It keeps every profile's archive in one folder, so a per-profile `backupDir` is
  not used there.
- As with the other profile defaults, a run that gives `--connection-string` without `--profile`
  applies no profile and therefore no backup folder.
- Relative paths are used as written, i.e. relative to the working directory.

### Profile passwords

A profile can carry its password in `dbtool.yml`. dbtool stores it **encrypted**:

```yaml
profiles:
  prod:
    connectionString: "DRIVER={ODBC Driver 18 for SQL Server};Server=db;Database=prod;UID=deploy"
    password: "enc:v1:q8x0...=="
```

You never need to produce the `enc:` value by hand:

- **`dbtool add-profile`** prompts for the password and writes the profile with it encrypted (see
  [add-profile](#add-profile)).
- **Plaintext is upgraded automatically.** If you write `password: hunter2`, dbtool uses it as is
  and — once a connection with it has succeeded — rewrites that one value in place as `enc:...`.
  The rest of the file (comments, ordering, formatting) is left untouched. A password that fails to
  connect is left alone. A `dbtool.yml` that was only discovered, and whose default profile is
  therefore no target for writing commands, is not rewritten either; name it with `--config` or
  the profile with `--profile` to upgrade it. If the file is read-only, dbtool warns and carries
  on. If the file lives in a git repository, dbtool warns that the old plaintext may remain in its
  history — change the database password in that case.
  dbtool-gui follows the same rule: it encrypts the value as soon as a connection with it succeeds,
  whether that is its interactive connect or the start of a managed backup or restore.

`password` and `secretRef` are mutually exclusive. `secretRef` (`env:`, `file:`, `stdin:`) remains
the choice for secrets that must not be in the file at all. `list-profiles` shows each profile's
`AUTH` as `encrypted`, `plaintext`, `secretRef` or `-`.

#### Security model

Values are encrypted with AES-256-CBC + HMAC-SHA256 (encrypt-then-MAC) using only the operating
system's cryptography (Windows CNG, OpenSSL libcrypto on Linux, CommonCrypto on macOS). The master
key is built into official dbtool / dbtool-gui binaries by CI; nothing on the user's machine is
needed, so the same `dbtool.yml` works on every machine.

| Someone who has… | can read the password? |
|---|---|
| only the `dbtool.yml` (repository, share, ticket, screenshot) | No |
| the dbtool source code | No — the key is not in the repository |
| modified an `enc:` value | No — tampering is detected and the connection is refused |
| an official dbtool / dbtool-gui binary | **Yes** — the key is embedded in it |

In other words, the encryption keeps passwords out of plain sight in configuration files; it is
not a vault. Use `secretRef` when holders of the binary must not be able to recover a password.

Builds made without the CI key (local developer builds, forks, distribution packages) use a public
development key and write `enc:dev:...` values. Official builds refuse those with a clear message;
re-run `dbtool add-profile --force`, or replace the value with the plaintext password so it is
re-encrypted on the next connect.

#### Providing the master key at build time

The key ring is read when CMake configures the project, in exactly one of two ways:

| Input | Typical use |
|---|---|
| `DBTOOL_MASTER_KEYS` environment variable: `id=<64 hex>[,id=<64 hex>...]` | GitHub Actions secret exported to the configure step |
| `-DDBTOOL_MASTER_KEYS_FILE=<path>` (or the env var of the same name): a file with one `id=<64 hex>` per line | GitLab "File" variables; package ports (vcpkg/Conan) that strip the environment but forward CMake options |

Generate a key with `openssl rand -hex 32`. Ids are `[a-z0-9]`, at most 16 characters; `dev` is
reserved. The first entry encrypts and every entry decrypts, so to **rotate**, prepend a new key
(`v2=...,v1=...`), ship that build, then re-encrypt profiles at leisure. Only the file *path* is
stored in `CMakeCache.txt`; the keys only land in a generated header inside the build tree, which
is never installed. Configuring with neither input prints
`dbtool: no master key provided - using the public development key`. A build tree that was once
configured with a key ring refuses to be re-configured without one (so an automatic re-configure
cannot silently produce a development-key build); use a fresh build directory, or
`-U DBTOOL_KEYRING_WAS_RELEASE`, when you really want that.

### Inspecting configured profiles

Use `list-profiles` to enumerate every profile parsed from the configuration
file. The command does not open a database connection, so it works against
any platform. Values of `PWD=` / `Password=` inside a profile's raw
`connectionString` are redacted to `***` in the output:

```bash
$ dbtool list-profiles
Profiles (from /home/me/project/dbtool.yml, found via current directory):

NAME  DEFAULT  CONNECTION                            AUTH       SCHEMA  PLUGINSDIR      BACKUPDIR
prod  *        DRIVER={ODBC Driver 18 for SQL Se...  encrypted  dbo     ./migrations    /var/backups/lightweight/prod
dev            DRIVER=SQLite3;Database=dev.db        -                  ./dev-plugins   /var/backups/lightweight
```

`BACKUPDIR` is the profile's effective backup folder: its own `backupDir`, else `defaultBackupDir`.

Use `--config <FILE>` to inspect a non-default configuration file. When no
config file is found, `list-profiles` prints where it looked and exits successfully.

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

On success, `migrate` prints a status summary (registered, applied, pending,
latest release, checksum verdict) so you can confirm the database's current
state in a single command. If there are no pending migrations, it explicitly
reports that the database is already up to date instead of `"Applied 0 migrations."`.

#### Skipped rows

An insert declared with `Insert(...).IfNotExists({keys})` (see
[SQL Migrations](sql-migrations.md#rows-that-may-already-exist-ifnotexists)) skips its row when
the database already holds a row with the same key values. The migration still succeeds, and
dbtool prints one line per skipped row on stdout, at the default log level:

```text
Skipped existing row: <timestamp> <table> (<key>=<value>[, <key>=<value>...])
```

- `<timestamp>` is the migration's timestamp, `<table>` the table name, prefixed with
  `<schema>.` when the migration sets a schema.
- Keys appear in the order the migration lists them. Numbers are printed bare; any other value is
  single-quoted, with an embedded `'` doubled.
- The existing row is left unchanged, even when its non-key columns differ from the migration's values.
- A key value clipped by truncation (see [Compatibility Flags](sql-migrations.md#compatibility-flags)) is
  printed as it was looked up.
- The lines are printed once the migration has committed. A migration that fails rolls back, and
  its skipped rows are neither printed nor counted.

The command's summary line then adds the count, for example
`Successfully applied 3 migration(s), skipped 2 existing row(s).` The suffix
`, skipped N existing row(s)` is printed only when N > 0, and the same applies to `apply`,
`rollback`, `rollback-to`, `migrate-to-release` and `rollback-to-release`. `--dry-run` executes
nothing, so it shows the conditional `INSERT ... WHERE NOT EXISTS (...)` statement but reports no skips.

```text
[2/3] Applying 20240301000001 Seed categories
Skipped existing row: 20240301000001 Categories (Id=200)
Successfully applied 3 migration(s), skipped 1 existing row(s).
```

#### Custom default schema (`--schema`)

> **PostgreSQL and `WithSchema(...)`:** a `CreateTable` in a migration built with `WithSchema("s")`
> now creates `"s"."T"` and its indexes in that schema, where earlier releases created `"T"` in the
> `search_path` schema. Two consequences on PostgreSQL:
> - A database migrated by an earlier release reports checksum mismatches for those migrations.
>   Once it is verified, run `dbtool rewrite-checksums`.
> - A fresh `migrate` needs the schema to exist before the migration runs.

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

`apply` has no dry run: it rejects `--dry-run` with an error instead of applying.
Use `dbtool migrate --dry-run` to preview pending migrations.

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

### rollback-to-release \<VERSION\>

Rolls back every migration applied after the named release:

```bash
dbtool rollback-to-release 1.0.0
```

The release's own migrations are kept; only what came after is reverted.

### releases

Lists the releases declared by the registered migrations, with each one's migration count and
whether it is fully applied:

```bash
dbtool releases
```

### rewrite-checksums

Rewrites `schema_migrations.checksum` so the stored checksums match what the current code
generates:

```bash
dbtool rewrite-checksums --yes
```

Use this **only** after a regeneration that changed the byte shape of a migration without changing
its meaning — for example a formatting change in generated DDL. If the logic actually changed, the
checksum mismatch is a real warning and rewriting it hides a genuine divergence. Because it edits
migration bookkeeping, it refuses to run without `--yes` — there is no interactive prompt, the
command simply exits. See [Checksum Mismatches](#checksum-mismatches).

### hard-reset

Drops every table the registered migrations own, plus the `schema_migrations` table, and leaves
tables it does not own untouched:

```bash
dbtool hard-reset --yes
dbtool migrate
```

The pairing with `migrate` is the point: `hard-reset` returns the database to "no migrations
applied" so the full set can be replayed from scratch. Which tables count as migration-owned is
computed by folding the registered migration plan, not by guessing from the live schema, so user
tables survive. Like `rewrite-checksums`, it refuses to run without `--yes`.

`--dry-run` prints the three groups it computed — tables to drop, migration-declared but absent, and
user-owned tables it will preserve — without touching anything.

### unicode-upgrade-tables

Rewrites legacy `VARCHAR`/`CHAR` columns to `NVARCHAR`/`NCHAR` where the registered migrations now
declare wide types:

```bash
dbtool unicode-upgrade-tables --dry-run
dbtool unicode-upgrade-tables --yes
```

This exists for databases created before a migration switched a column to a wide type: the
migration history is already marked applied, so nothing would otherwise re-run to widen the
existing columns. Run it with `--dry-run` first to see the planned `ALTER` statements.

On SQL Server a column cannot change between narrow and wide text while anything depends on it.
The command therefore reads every dependent object from the catalog — primary-key, unique,
foreign-key (on either side, composite included), default and check constraints, indexes (with
their included columns and filters) and statistics — drops them, alters the columns and recreates
each object under its original name with its original definition. The dry run lists those
objects. Objects it cannot recreate safely — schema-bound views or functions, computed columns,
columnstore, XML, spatial and full-text indexes — stop the command before anything changes; drop
them, run it, and recreate them yourself.

On PostgreSQL the command likewise reads the foreign keys on the changing columns from the catalog
and recreates each under its own name with its own definition: its columns, the referenced table
with its schema, and its `ON DELETE` / `ON UPDATE` actions. So keys that another tool created
survive the upgrade unchanged. On SQLite each table is rebuilt from its own `CREATE TABLE`
statement, which keeps its keys.

The command works unit by unit, and each unit is one transaction, committed before the next one
starts. A unit is a table, together with every table a foreign key joins to it through a column
that changes, and the objects dropped and recreated for them. So no unit leaves an object dropped
behind it. The dry run prints the number of transactions and lists the units of several tables,
and the run prints a line for each committed unit.

Under the simple recovery model, or while log backups run during the upgrade, the transaction log
then only has to hold one unit, and the largest unit sets its peak: tables that many foreign keys
join can form one large unit. Under the full or bulk-logged recovery model without log backups, the
log keeps everything until the next log backup, as one transaction over the whole database would.

When a unit fails, it is rolled back, and the command stops with an error that names it and says
how many units were committed before it. Those stay committed. Run the command again after fixing
the cause: it reads the live schema anew, so the columns already widened are no longer in the plan
and it continues with the failed unit.

A foreign key on SQL Server needs both of its columns to have the same type. So when the command
widens a column, it also widens every column a foreign key joins to it, on either side and
transitively, even when no migration declares that column — a column left over from an older
schema, say. The dry run lists such a column as `(not declared; joined by a foreign key to
<table>.<column>)`. A joined column must have the same narrow type and size and must not be
declared with another type; otherwise the command names it and stops before anything changes.

The dry run lists each column as `<table>.<column>`. When the columns to upgrade span several
schemas, every table in the list, in the foreign-key notes and in the refusal is written as
`<schema>.<table>` instead.

Declared table and column names match the live ones without regard to case on SQLite and on SQL
Server under a case-insensitive database collation (the default; the collation is not checked),
when no live name matches exactly (within a schema); on PostgreSQL they must match exactly.

In a database whose tables span several schemas, each declared table stands for one live table,
which the command finds like this:
- the schema that `WithSchema` names;
- otherwise the first schema that holds a table of that name, in the order the connection resolves
  unqualified names: on SQL Server the login's `DEFAULT_SCHEMA` (`dbo` for a sysadmin), then `dbo`;
  on PostgreSQL the search path, which `--schema` sets (see the table above). On SQL Server
  `--schema` and the profile's schema do not change this order;
- otherwise the only schema that holds a table of that name, even one the connection does not
  resolve unqualified names through.

Run it with the application's login (on PostgreSQL, the application's `--schema`), so that it widens
the tables the application's unqualified statements reach. Tables of the same name in other schemas
are left alone, except for columns that a foreign key joins to a widened column, which are widened in
their own schema. When several schemas hold a declared name, none of them is on the connection's
resolution order and one of them has a column to widen, the command names the schemas and stops
before anything changes.

Backup and restore cover every schema. This command differs because the migrations declare each
table once.

### exec \<QUERY\>

Executes an SQL query and prints any result set:

```bash
dbtool exec "SELECT COUNT(*) FROM Users"
```

Pass `-` as the argument, or omit it entirely, to read the query from stdin:

```bash
echo "SELECT * FROM Users WHERE id = 1" | dbtool exec -
dbtool exec < query.sql
```

### list-profiles

Lists the profiles defined in the configuration file (see [Configuration](#configuration)):

```bash
dbtool list-profiles
```

### add-profile

Adds a profile to `dbtool.yml` (the file found as described in
[Where dbtool finds `dbtool.yml`](#where-dbtool-finds-dbtoolyml), or `--config`; created if missing)
with its password encrypted. Existing comments and formatting are preserved.

```bash
# Prompts for the password (no echo) and stores it encrypted
dbtool add-profile --name prod --dsn ACME_PROD --uid deploy --set-default

# Scripted: the password is read from stdin
printf '%s\n' "$DB_PASSWORD" | dbtool add-profile --name ci \
    --connection-string "DRIVER={PostgreSQL Unicode};Server=db;Database=app;Uid=ci"
```

| Option | Meaning |
|---|---|
| `--name <NAME>` | Profile name (required) |
| `--connection-string <STR>` / `--dsn <DSN>` | Exactly one is required. A `PWD=` inside the connection string is moved into the encrypted `password` field |
| `--uid <UID>` | User name for a DSN profile |
| `--schema <S>`, `--plugins-dir <DIR>`, `--backup-dir <DIR>` | Stored on the profile (`--backup-dir` becomes its `backupDir`) |
| `--no-password` | Store the profile without a password |
| `--set-default` | Make it the `defaultProfile` |
| `--force` | Replace an existing profile of the same name |

Passwords are never accepted as command-line arguments.

### resolve-secret \<REF\>

Resolves a single secret reference and prints it to stdout, without connecting to any database:

```bash
dbtool resolve-secret env:DB_PASSWORD
dbtool resolve-secret file:/run/secrets/db_password
dbtool resolve-secret stdin:
```

This is a debugging aid for configuration: it lets you confirm that a `env:` / `file:` / `stdin:`
reference in a profile resolves to what you expect, before a connection failure sends you looking
in the wrong place.

## Backup & Restore

### backup

Create a compressed backup of the database:

```bash
dbtool backup --output backup.zip --compression zstd --jobs 4
```

**Where the file goes.** When `dbtool.yml` names a backup folder for the selected profile
([`backupDir` / `defaultBackupDir`](#backup-folder-defaultbackupdir--backupdir)):

```bash
# a bare file name is written into that folder
dbtool backup --profile prod --output nightly.zip

# no --output: writes <profile>-<YYYYMMDD-HHMMSS>.zip (local time) into that folder
dbtool backup --profile prod

# a path with a directory part is used exactly as given
dbtool backup --profile prod --output /srv/exports/once.zip
```

The folder is created if it is missing, and the file that was written is printed. Without a
configured folder `--output` is required and is used as given.

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

**Several schemas in one archive.** A database whose tables span several schemas, with foreign
keys between them, needs all of them in one archive, or the restore cannot recreate the foreign
keys that cross into a schema the archive lacks:

```bash
# the schemas named
dbtool backup --output backup.zip --schema dbo,sales

# every schema that holds a user table
dbtool backup --output backup.zip --all-schemas
```

`--all-schemas` leaves out the system schemas `sys`, `INFORMATION_SCHEMA` and `guest`, and those
starting with `db_` (SQL Server roles) or `pg_` (PostgreSQL catalogs). It cannot be combined with
`--schema`, and it overrides a profile's `schema`. On SQLite, which has no schemas, it writes the
ordinary single-schema archive.

Two or more schemas write a multi-schema archive (format version `2.0`, see
[sql-backup-format.md](sql-backup-format.md)), which records each table's schema and each foreign
key's referenced schema. A single `--schema X`, and no `--schema` at all, write the archive layout
they always wrote (format version `1.0`). One exception: a backup refuses tables whose archive
name or folder would collide (see below), where it used to overwrite one with the other.

> **SQL Server and PostgreSQL without `--schema`:** the backup reads the tables of every schema but
> records none of them, and a restore puts every table into one schema. Tables of the same name in
> different schemas are refused: the backup stops and names them. For a database whose tables span
> several schemas, use `--all-schemas` or `--schema a,b`.

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

**Multi-schema archives.** An archive written with several schemas restores every table into the
schema it comes from:

- Schemas that do not exist yet are created first. Then come the tables, the data, and finally all
  foreign keys, including those that cross schemas.
- `--schema` is refused: remapping several schemas onto others is not supported. A profile's
  `schema` is ignored when it names one of the archive's schemas (compared case-insensitively), so a
  profile with `schema: dbo` restores a `dbo,sales` archive; any other profile schema is refused like
  `--schema`, before anything is touched. dbtool-gui applies the same rule to a profile's schema, and
  passes a schema typed for a custom target on, where it is refused.
- `--schema-only` and `--filter-tables` work as for any archive. A filter pattern can name the
  schema (`--filter-tables=sales.*`).
- On SQLite, which has no schemas, the archive restores only if every table name is unique across
  its schemas, compared case-insensitively as SQLite does; the tables then lose their schema.
  Otherwise the restore is refused and names the tables that occur in more than one schema.

**Restoring into PostgreSQL.** Tables are created in the schema the restore targets: the archive's
`schema_name`, or `--schema`. A single-schema archive from SQL Server records `dbo`, which a
PostgreSQL database usually lacks, so restore it with `--schema public` (or another existing schema).

**Values that do not fit their column fail the table.** A GUID column takes any well-formed
`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` value, whatever its version digit, and restores the same
bytes. A value that does not convert to its column's type (a malformed GUID, number, date or time,
a number outside the column's range, or a binary value in a text column) is never replaced by NULL
or zero. Instead, the table fails with an error that names the table, the column, the row within
the chunk and the value, for example:

```
table 'ORDERS', column 'TOKEN', row 1: 'not-a-guid' is not a valid GUID (chunk data/ORDERS/0001_00.msgpack)
```

The table's remaining chunks are skipped, the other tables are restored, and `dbtool restore`
exits non-zero. The failed table may be left partly restored: each chunk is its own transaction,
so chunks of the table that were restored before the failing one (or, with `--jobs` above 1, at
the same time by other workers) stay, and on SQLite so do the rows committed within the failing
chunk, which commits every 5,000 to 10,000 rows. A database error on insert still stops the worker
that hit it; every table left not fully restored is reported as an error.

### backup-diff

Compare the **data content** of two backup archives to detect silent data corruption — for
example, to prove that a concurrent (multi-threaded) backup contains the same rows as a safe
single-threaded baseline:

```bash
dbtool backup-diff --left backup_st.zip --right backup_mt.zip
```

This is a pure file comparison: it opens **no database connection**.

The comparison is **order-independent**. Two backups of the same database can legitimately emit
rows in a different order and split them into different chunks (different pagination, worker
interleaving), so chunk-level checksums would diverge even when the data is identical. Instead,
`backup-diff` compares the *multiset of rows* per table:

- Tables present in only one archive are reported as a difference.
- For each common table, every row is decoded from its `data/<table>/NNNN.msgpack` chunks
  (`data/<schema>/<table>/...` in a multi-schema archive),
  serialized to a canonical, length-prefixed, type-tagged byte encoding (with an explicit NULL
  marker so e.g. string `"12"` followed by int `3` can never collide with string `"123"`),
  hashed with SHA-256, and counted into a `digest -> count` map. The two maps are then compared.
  Only digests and counts are retained, so memory stays proportional to the number of *distinct*
  rows — archives with millions of rows remain tractable. One table is processed at a time on
  both sides; neither archive is held in memory in full.
- For differing tables, the command reports the per-side row count, the number of rows present
  only on each side, and up to three example differing row digests.

It prints a summary line (`N tables compared, M identical, K differing`) and exits `0` when every
common table is identical and both archives hold the same set of tables, or `1` otherwise.

## Command-Line Options Reference

| Option | Description | Default |
|--------|-------------|---------|
| `--connection-string <STR>` | ODBC connection string | |
| `--schema <NAME>` | Database schema to use. `backup` takes a comma-separated list and then writes a multi-schema archive | |
| `--all-schemas` | For backup: every schema that holds a user table | |
| `--config <FILE>` | Path to configuration file | nearest `dbtool.yml` upward, then the per-user file (see [Configuration](#configuration)) |
| `--plugins-dir <DIR>` | Directory to scan for migration plugins | `.` (current directory) |
| `--output <FILE>` | Output file for backup. A bare file name, or no `--output`, goes into the profile's `backupDir` / `defaultBackupDir` when one is configured | |
| `--input <FILE>` | Input file for restore | |
| `--left <FILE>` | First (baseline) backup archive for `backup-diff` | |
| `--right <FILE>` | Second (candidate) backup archive for `backup-diff` | |
| `--filter-tables <PATTERN>` | Table filter (wildcards supported) | `*` (all tables) |
| `--jobs <N>` | Number of concurrent jobs | `1` |
| `--compression <METHOD>` | Compression method for backup | `deflate` |
| `--compression-level <N>` | Compression level (0-9) | `6` |
| `--chunk-size <SIZE>` | Chunk size for backup data | `10M` |
| `--progress <TYPE>` | Progress output: `unicode`, `ascii`, `logline` | `unicode` |
| `--quiet`, `-q` | Suppress progress output | |
| `--dry-run`, `-n` | Preview without executing; `apply` rejects it | |
| `--no-lock` | Skip migration locking | |
| `--schema-only` | For backup/restore: skip data, transferring schema only | |
| `--memory-limit <SIZE>` | Memory limit for restore (accepts the size suffixes below) | |
| `--batch-size <N>` | Rows per batch for restore | |
| `--ignore-table <NAME>` | For `backup-diff`: report differences in this table but do not fail. Repeatable. In a multi-schema archive a table is `<schema>/<table>` or `<schema>.<table>` (dots in names written as `_`); a bare table name also matches when only one schema holds it, and is refused as ambiguous otherwise. A name that matches no table is reported as a warning | |
| `--profile <NAME>` | Named profile from the configuration file | store default |
| `--up-to <TIMESTAMP>` | Upper bound for migration commands | no bound |
| `--max-retries <N>` | Maximum retry attempts for transient errors | `3` |
| `--verbose`, `-v` | Emit extra informational output (e.g. shadowed plugins) | |
| `--yes`, `-y` | Confirm destructive actions without prompting | |
| `--show-examples` | Print usage examples and exit | |
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

### Optional Post-Init Hook

A plugin may export an optional `LightweightMigrationPluginPostInit` symbol that
`dbtool` calls **once per invocation**, after `schema_migrations` has been created
and a live connection is available. Use this for one-shot bridging work that needs
both a `SqlConnection` and the merged `MigrationManager` — for example, importing
a legacy version-tracking table into `schema_migrations`.

```cpp
#include <Lightweight/SqlMigration.hpp>

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    // Runs after CreateMigrationHistory(). Make it idempotent: dbtool invokes
    // this on every run, including `status`. Return early when there is nothing
    // to do.
    MyPlugin::BridgeLegacyState(manager, connection);
}
```

The hook is **optional** — plugins that don't export the symbol behave exactly as
before. Per-plugin exceptions are logged to stderr but do not abort `dbtool`.

Post-init runs before every command, read-only ones included, and before the
migration lock is taken and any migration runs. It is therefore the wrong place
for work that needs the migrated schema; use the post-migrate hook for that.

### Optional Post-Migrate Hook

A plugin may export an optional `LightweightMigrationPluginPostMigrate` symbol for
work that needs the migrated schema, such as seeding reference data. `dbtool` calls
it once per plugin after a **successful** `migrate`, `migrate-to-release` or `apply`
— also when nothing was pending — on the migration connection, while the migration
lock is still held (with `--no-lock` no lock is taken, so the hook runs unlocked too).
It never runs for `--dry-run`, read-only commands (`status`, `list-*`, `releases`,
...), rollbacks, `mark-applied`, or a migration that failed. `apply` has no dry run
and rejects `--dry-run` instead of applying. `dbtool-gui` calls it under the same
conditions after an apply run, and releases the lock before it reports the run.

```cpp
#include <Lightweight/SqlMigration.hpp>

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTMIGRATE(connection, manager)
{
    // Runs after the migrations committed. Make it idempotent: it runs after every
    // successful migrate, including runs where nothing was pending.
    MyPlugin::SeedReferenceData(connection);
}
```

The hook is **optional** — plugins that don't export the symbol behave exactly as
before. Unlike post-init, a failure is fatal for the command: when the body throws,
the error is reported on stderr (`Plugin post-migrate failed (<plugin>): <message>`)
and `dbtool` exits non-zero. The migrations that ran before the hook stay applied;
the remaining plugins' hooks still run. The macro exports a non-throwing entry point
that catches the exception inside the plugin, so it never crosses the shared-library
boundary.

### Plugin Lifetime

`dbtool` keeps its plugins loaded until it exits; `dbtool-gui` also unloads them when it
reloads the plugin list (another profile or plugins directory). Before either unloads a
plugin, and also when a later plugin fails to load, the migration manager drops what the
plugins gave it: the compat policy (`SetCompatPolicy`, usually installed from a plugin's
static initializer), the migrations and the releases. The process then exits with the
command's own exit code.

Anything else a plugin installs process-wide — a callback on the migration manager or the
connection, a logger — is code from the plugin's library, and the plugin must remove it
itself in a static destructor. Otherwise the process calls into an unloaded library when
it destroys that callback, and crashes on exit.

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

If `dbtool status` reports checksum mismatches, there are two quite different causes — check the
second one first, because it is benign and easy to mistake for the first.

**1. You upgraded Lightweight and the generated SQL changed.** The checksum is computed over the SQL
text that the *formatter* renders for a migration, not over your C++ source. So a library release that
changes emitted DDL re-hashes every already-applied migration that uses the affected construct, even
though nobody touched the migration. Example: the PostgreSQL formatter now emits `BIGSERIAL` instead of
`SERIAL` for a `Bigint` auto-increment key, so every PostgreSQL migration using
`PrimaryKeyWithAutoIncrement` reports a mismatch after upgrading past that change. Nothing is out of
sync and nothing
breaks — mismatches are reported, not enforced. Confirm the mismatching migrations are exactly the ones
touched by the release, then re-baseline:

```bash
dbtool rewrite-checksums          # rewrites schema_migrations.checksum to match current code
```

**2. A migration really was modified after it was applied.** Then the database schema may genuinely be
out of sync with the code. Review the change and create a new migration instead of editing the old one;
do **not** run `rewrite-checksums`, which would erase the evidence.

### Lock Acquisition Failed

If migration locking fails:
- Another migration may be running
- Use `--no-lock` to skip locking (only if you're certain no other migrations are running)

### Crashes and Assertions (Windows)

A failed CRT assertion, an invalid CRT parameter or `abort()` is reported on stderr and ends the
process with exit code 3; a crash ends it with its exception code (e.g. `0xC0000005`). Neither opens
a dialog, so scripts and CI never hang waiting for a click. A DLL that fails to load before `main`
starts is outside the process's control; run `dbtool` from a parent that sets the error mode (ctest
does) to suppress that dialog too.

`dbtool` always behaves this way, and so do the test executables. `dbtool-gui` keeps the standard
Windows dialogs for interactive use; set `LIGHTWEIGHT_NO_ERROR_DIALOGS=1` for a scripted run. A
scripted run can also set `LIGHTWEIGHT_DBTOOL_GUI_SETTINGS_DIR` to keep `dbtool-gui`'s settings in
an INI file under that directory instead of the user's settings. Its default backup folder (used
when none is chosen in Settings and the loaded `dbtool.yml` sets no `defaultBackupDir`) is then the
`backups` folder in that directory instead of the per-user one.

## See Also

- [sql-migrations.md](sql-migrations.md) - Guide to writing SQL migrations in C++
- @ref Lightweight::SqlMigration::MigrationManager - C++ API for managing migrations
