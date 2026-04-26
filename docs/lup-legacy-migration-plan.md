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
  *semantics* divergent from 20 years of LUP installs. Good as an opt-in
  escape hatch for columns where we refuse to lose data, not as the
  default fix.
- **Edit the `.sql` files** — out of scope per project constraints.

## Plan

Implement **LUpd-compatible client-side truncation** as an opt-in
compat mode in Lightweight, and regenerate the plugin with
`--force-unicode` so MSSQL uses `NVARCHAR`/`NCHAR` (character-counted,
no UTF-8 byte-budget surprises).

### 1. Regenerate `LupMigrationsPlugin` with `--force-unicode`

Add `--force-unicode` to the `lup2dbtool` invocation in
`src/tools/LupMigrationsPlugin/CMakeLists.txt` (or expose it via a
CMake cache variable gated on the target backend). This converts:

- `VARCHAR(N)` → `NVARCHAR(N)`
- `CHAR(N)` → `NCHAR(N)`

in the generated DSL. On MSSQL, `NVARCHAR(N)` counts characters, not
bytes, so German umlauts stop eating the budget. SQLite and PostgreSQL
treat `NVARCHAR` the same as `VARCHAR` semantically, so this is a
safe no-op for those backends.

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

### 2. Add an opt-in compat mode: `compat: lup-truncate`

Wire a new per-profile compat flag through the existing chain:

- `Lightweight::Config::Profile` gains a `compatFlags` field (string set
  or bitmask) parsed from YAML: `compat: lup-truncate` or
  `compat: [lup-truncate]`.
- `dbtool` + `migrations-gui` propagate the flag into the migration
  runner via the existing profile → `SqlMigration` path.
- At the `SqlStatement` binding layer, when the flag is active and the
  bound parameter is a character type whose value exceeds the
  destination column's declared width, the value is truncated to that
  width *before* `SQLBindParameter`.

Column widths come from a per-table cache populated lazily via
`SQLDescribeCol` or `INFORMATION_SCHEMA.COLUMNS`. The cache is
invalidated when a migration alters the table (trivially: clear on
`ALTER TABLE` / `DROP TABLE` / `CREATE TABLE`).

### 3. Scope of the compat mode

- **Default: off.** Strict behavior (current) is correct for new
  projects; LUP is the exception, not the rule.
- **Activation: profile-local.** Only `lastrada-sql2022` /
  `lastrada-postgres` / `lastrada-sqlite` opt in. Other profiles remain
  strict.
- **Logging: every truncation is logged** with migration timestamp,
  table, column, declared width, original length, and the truncated
  tail. Silent in LUpd — **not silent** here. This is the one place we
  intentionally diverge from LUpd, so that the data loss remains
  auditable.

### 4. Optional escape hatch — column-widening preflight

For columns where truncation would corrupt a reference (not just a
display string), add a small sideloaded preflight plugin that widens
them up front:

```cpp
ALTER TABLE PROBEN_PRUEFUNGEN ALTER COLUMN NAME NVARCHAR(250) NOT NULL;
```

This plugin has no migrations of its own — it is a compat shim loaded
before `LupMigrationsPlugin`. Keep the list short and curated.

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

## Out of scope

- Rewriting the LUP `.sql` files.
- Changing LUpd.
- Generalizing the compat mode to other legacy apps without a
  concrete customer need.
