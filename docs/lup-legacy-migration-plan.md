# Running LUP migrations via dbtool — compatibility plan

## Problem

Running `dbtool --profile lastrada-sql2022 migrate` applies the first 205
LUP migrations but fails on the 206th (`20000000000475 — LUP Update 4_07_05`):

```
Error: failed to apply migration.
  Migration:    20000000000475 - LUP Update 4_07_05
  Step:         20
  SQL State:    42000, Native error: 2628
  Driver message:
    [Microsoft][ODBC Driver 18 for SQL Server][SQL Server]
    String or binary data would be truncated in table
    'LightweightTest.dbo.PROBEN_PRUEFUNGEN', column 'NAME'.
    Truncated value: 'EK-Min: Dichtebestimmung mit Pyknometer für
                      Körnungen zwischen 0,063 und 31,5 mm (EN 1097-6 Anhang'.
  Failed SQL:
    INSERT INTO "PROBEN_PRUEFUNGEN" ("NR", "NAME")
    VALUES (1008, 'EK-Min: Dichtebestimmung mit Pyknometer für
                   Körnungen zwischen 0,063 und 31,5 mm (EN 1097-6 Anhang A.4)')
```

The `NAME` column was declared `VARCHAR(100)` in
`D:/Lastrada/src/model4_JP/upd_m_2_8_1.sql` and never widened by any later
LUP migration. The INSERT in `upd_m_4_7_5.sql:33` is **103 characters** — it
does not fit in a 100-character column under any collation, and under the
UTF-8 collation of the container it is `≥106` bytes against a 100-byte
budget.

## Constraints

- The LUP `.sql` files cannot be modified. They are shared with LUpd and
  represent two decades of shipped migrations across many customer
  installs.
- Regenerating `.cpp` migrations with different column types changes
  their checksum. The 205 already-applied migrations on this database
  were applied under the current checksums, so any regen must preserve
  them or we break checksum validation.

## Why LUpd worked for 20 years

LUpd’s ODBC layer **silently truncates strings client-side** before
binding them to parameterized INSERT/UPDATE statements. The decades-old
migrations have always contained values that exceed their declared
column widths — nobody ever noticed because the excess was discarded
before it reached the server.

### Evidence

`D:/Lastrada/src/DABase/ODBC/DBaseI.cpp:478` — when building a bind
variant for a column, LUpd seeds `m_nMaxSize` with the column's declared
`Precision()`:

```cpp
ODBC_Variant ODBC_DBDatabaseI::Variant(const DBColumn &dbCol) const
{
    ODBC_DBColumnI *pCol = (ODBC_DBColumnI*)DBAccessor::Impl(dbCol);
    return ODBC_Variant(Map2Variant(pCol->SQLType()),
                        (short)pCol->SQLType(),
                        Map2SQLCType(pCol->SQLType()),
                        pCol->Precision());         // ← declared column width
}
```

`D:/Lastrada/src/DABase/ODBC/Variant.cpp:305-322` — when copying the
application value into the bind buffer, any overflow is clipped to
`m_nMaxSize`:

```cpp
case DBVariant::VarChar:
{
    SQLULEN nS = v.AsString().Length();
    if (nS != m_nSize) {
        if (nS > m_nSize)
            nS = (nS < m_nMaxSize) ? nS : m_nMaxSize;   // cap at column width
        Resize(nS);
    }
    memcpy(m_uData.pBuffer, v.AsString(), m_nSize);     // copies m_nSize bytes
    m_uData.pBuffer[m_nSize] = 0;
    m_nStrLen_or_IndPtr = SQL_NTS;
}
```

So the real mechanism is **client-side silent truncation against the
live destination schema**. There is no `SET ANSI_WARNINGS OFF`, no
special collation, no compatibility-level trick. The server simply
never sees an over-long value.

## Rejected alternatives

- **`SET ANSI_WARNINGS OFF` / trace flag 460** — addresses the symptom
  server-side, but SQL Server’s behavior around truncation has changed
  across versions; the result is fragile and MSSQL-specific, and does
  not reproduce LUpd’s actual mechanism.
- **Regenerate with `--varchar-scale 4`** — changes column types, which
  changes checksums of already-applied migrations. Breaks validation.
  Also does not solve the core "source data exceeds declared width"
  issue.
- **Widen every offending column via a preflight plugin** — works, but
  requires hand-curating a list of offenders and still leaves the
  *semantics* divergent from 20 years of LUP installs. Rejected: the
  curated list is unbounded in practice and the `compat: lup-truncate`
  mode already handles the data-loss case uniformly.
- **Edit the `.sql` files** — out of scope per project constraints.

## Plan

Implement **LUpd-compatible client-side truncation** as an opt-in
compat mode in Lightweight, and make Unicode textual columns
(`NVARCHAR`/`NCHAR`) the generator default so MSSQL uses
character-counted widths with no UTF-8 byte-budget surprises.

### 1. Make Unicode textual columns the default in `lup2dbtool`

`lup2dbtool` already has a `--force-unicode` flag; it is currently
opt-in. The plan is to flip the default to *on* so `lup2dbtool` always
emits `NVARCHAR`/`NCHAR`:

- `VARCHAR(N)` → `NVARCHAR(N)`
- `CHAR(N)` → `NCHAR(N)`

Concrete edits:

- `src/tools/lup2dbtool/main.cpp:80` — change the default of
  `args.forceUnicode` from `false` to `true`.
- `src/tools/lup2dbtool/CodeGenerator.hpp:52` — mirror the default on
  `CodeGeneratorConfig::forceUnicode`.
- `src/tools/lup2dbtool/main.cpp` argparse — add a `--no-force-unicode`
  opt-*out* for completeness. (Rename or keep `--force-unicode` as a
  no-op accepted-but-ignored alias.)

The mapping itself already lives in
`src/tools/lup2dbtool/CodeGenerator.cpp:614-667`
(`MapParameterizedType` / `MapSimpleType`) and needs no change.

Backend behavior (unchanged, already implemented in the formatters):

- **MSSQL** (`SqlServerFormatter.hpp:166-171`): emits
  `NCHAR(N)` / `NVARCHAR(N)` — character-counted widths, which is the
  primary driver.
- **PostgreSQL** (`PostgreSqlFormatter.hpp:108-112`): downgrades back
  to `CHAR(N)` / `VARCHAR(N)`. Semantic no-op (PG is UTF-8,
  char-counted already).
- **SQLite** (`SQLiteFormatter.hpp:582-583`): passes `NCHAR` /
  `NVARCHAR` through; SQLite's name-based type affinity resolves any
  `CHAR`-containing name to `TEXT` affinity. Semantic no-op.

Because the formatters already handle both forms per backend, flipping
the generator default is the entire code change — no conditional, no
per-backend branching in the generator.

**Checksum impact:** regenerated `.cpp` files produce different
checksums. For already-applied migrations this would fail validation.
Two ways to handle that:

- **(preferred)** have the validation step skip the binary-checksum
  compare for migrations that apply to tables already present in the
  schema with acceptable structure, or record a per-migration
  "equivalence hash" based on logical statements rather than source
  bytes. This is a broader change.
- **(pragmatic short-term)** provide a one-shot `--rewrite-checksums`
  operation for this profile that updates `schema_migrations` rows to
  the new checksums after verifying the migration logically matches.

### 2. Plugin-installed `lup-truncate` compat policy

Compat scope is a property of the legacy code, not of the deployment.
The `LupMigrationsPlugin` itself decides which migrations need
LUpd-compatible truncation, by timestamp:

- `MigrationManager::SetCompatPolicy(fn)` lets a plugin install a
  per-migration callback that returns the active compat flags.
- `MigrationManager::ComposeCompatPolicy(fn)` composes additional
  policies on top — used by dbtool when collecting migrations from
  multiple plugins (each plugin's static-init sets its own policy on
  its own singleton manager; `CollectMigrations` propagates them onto
  the central manager).
- The LUP plugin installs a pure function that returns
  `{lup-truncate}` for every migration with timestamp `< 20'000'000'060'000`
  (the first 6.0.0 migration). Modern migrations get `{}`.

`ApplySingleMigration` / `PreviewMigrationWithContext` re-derive
`context.lupTruncate` from the policy for each migration before
rendering its steps.

The actual truncation lives in `MigrationPlan.cpp`:

- `ToSql(formatter, element, MigrationRenderContext&)` is the
  context-aware overload. It consumes `CreateTable` / `AlterTable` /
  `DropTable` plan elements to maintain a per-(schema, table, column)
  width cache, then visits `SqlInsertDataPlan` / `SqlUpdateDataPlan`
  values and clips any that exceed their column's declared width.
- For tables already present in the database (e.g. created by an
  earlier run), a lazy lookup hits `INFORMATION_SCHEMA.COLUMNS` once
  per table and feeds the widths into the cache.
- Widths are tracked with a unit (`Bytes` for `varchar`/`char` columns,
  `Characters` for `nvarchar`/`nchar`) so MSSQL `varchar(100)` (which
  holds 100 *bytes*) doesn't get clipped to 100 *characters* of UTF-8
  with German umlauts overflowing the byte budget.
- Every truncation emits `SqlLogger::OnWarning` naming the operation,
  schema, table, column, original size, declared width, and unit.
  Silent in LUpd — **not silent** here. This is the deliberate
  divergence from LUpd that keeps the data loss auditable.

No surface in `Profile`, `dbtool.yml`, or any CLI flag — the operator
never sees this knob.

## Test plan

- Unit: `MigrationTests` gains cases that exercise the truncation path
  against both VARCHAR (byte-counted) and NVARCHAR (char-counted)
  columns, on SQLite, PostgreSQL, and both SQL Server 2019/2022.
- Integration: re-run `dbtool --profile lastrada-sql2022 migrate`
  end-to-end; assert (a) all 403 migrations succeed, (b) the truncation
  log contains the expected entries for 4_07_05/1008, (c) the
  post-migration row count and checksum of `PROBEN_PRUEFUNGEN` match a
  canonical LUpd-produced snapshot.
- Regression: verify non-LUP profiles still fail hard on truncation
  attempts (compat mode is opt-in only).

## Risk assessment

- **Data loss is silent by nature** — the compat mode *is* the
  truncation. We mitigate by logging every truncation so an operator
  can audit what was lost.
- **Schema-introspection overhead** — adds one `SQLDescribeCol` (or
  equivalent) per previously-unseen column. Amortized by the per-table
  cache; negligible against the cost of running 200+ migrations.
- **Surface area in the binding layer** — net-new code path, gated
  behind an off-by-default flag. Covered by tests in (test plan)
  above; no behavior change when the flag is off.

## Performance impact

- No impact when `compat: lup-truncate` is off — the check is a single
  flag test before the normal bind path.
- When on, an extra metadata lookup on first use of each column, then
  a cached `std::size_t` compare on every bind. Truncation itself is a
  substring copy, O(width). No per-row SQL round-trip.

## As shipped

- **Section 1 (Unicode-default generator):** flipped via two-line
  default change in `lup2dbtool` plus `--no-force-unicode` opt-out.
  `LupMigrationsPlugin` regenerates with `NVARCHAR`/`NCHAR` everywhere.
- **Section 2 (compat policy):** moved to plugin-side ownership rather
  than profile-side. `LupMigrationsPlugin` installs a `CompatPolicy`
  on `MigrationManager` from a static initializer; the threshold is
  `20'000'000'060'000` (first 6.0.0 migration). Truncation lives in
  `MigrationPlan.cpp::ToSql` with byte-vs-character unit awareness.
  Width cache populated from CREATE/ALTER plans plus a lazy
  `INFORMATION_SCHEMA.COLUMNS` fallback for pre-existing tables.
  Logged via `SqlLogger::OnWarning`.
- **Section 3 (checksum rewrite):** new `dbtool rewrite-checksums`
  admin command. Defaults to dry-run; requires explicit `--yes` to
  write. `MigrationManager::RewriteChecksums(dryRun)` implements the
  primitive. Verified end-to-end against the staging MSSQL database
  (rewrote 150 drifted checksums; subsequent migrate proceeds past
  the 4_07_05 truncation boundary that originally blocked the run).

## Out of scope

- Rewriting the LUP `.sql` files.
- Changing LUpd.
- Generalizing the compat mode to other legacy apps without a
  concrete customer need.
- Logical-equivalence checksum hashes (the long-term shape mentioned
  in the plan). The pragmatic `rewrite-checksums` tool is enough for
  the current Unicode-default transition; the broader change is
  deferred until a second compat-driven regen is on the horizon.
- Foreign-key shape mismatches that surface on a partially-migrated
  legacy database after the Unicode regen — these are real schema
  drift, not silently-truncated data, and need a per-database
  remediation plan rather than a generic compat flag.
