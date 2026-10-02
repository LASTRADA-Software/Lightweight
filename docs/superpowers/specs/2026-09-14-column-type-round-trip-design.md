# Column-type round-trip test coverage

**Date:** 2026-09-14
**Status:** Approved, not yet implemented
**Branch:** `test/column-type-round-trip-coverage` (based on `master`)

## Problem

Two defects shipped and survived a green CI matrix for weeks, and they share one shape.

- **#586** (fixed by PR #599): a PostgreSQL `DOUBLE PRECISION` column was read back out of
  the catalog as `Real { .precision = 24 }`, so `CxxModelPrinter` emitted `float`. Every
  generated record silently narrowed the column from 8 bytes to 4.
- **#587** (fixed by PR #598): `Timestamp {}` on MS SQL Server emitted `TIMESTAMP`, a
  deprecated synonym for `rowversion` — a server-generated binary counter, not a point in
  time. The column read back as `binary(8)`, generated `SqlDynamicBinary<8>`, and rejected
  every write.

Both are breaks in the same chain:

```
declared SqlColumnTypeDefinition
  -> DDL type name        (QueryFormatter, per DBMS)
  -> database catalog
  -> recovered definition (SqlSchema reader, per DBMS)
  -> generated C++ type   (CxxModelPrinter)
  -> a value written and read back
```

Both were single-DBMS. Neither was caught, for two distinct reasons:

1. **Coverage was not exhaustive.** `Ddl2CppColumnTypeTests.cpp`'s shared descriptor table
   (`ColumnTypeCases()`) covers 16 of the 19 `SqlColumnTypeDefinition` alternatives. `Text`,
   `Guid` and `Timestamp` sit in separate hand-written `TEST_CASE`s — `Guid` only as a
   primary key, never as an ordinary column — and `Timestamp` is exactly the type #587
   broke. Nothing enforces that every alternative is covered, so an uncovered one is
   invisible.
2. **The assertion sat downstream of the defect.** The table asserted the generated C++
   type. #586's actual break was in the recovered definition; the generated type was a
   symptom. Worse, the PostgreSQL result was recorded as a `dialectException` asserting the
   known-wrong `float`, which made the suite green *because* the bug was present.

## Goal

One executable invariant covering every `SqlColumnTypeDefinition` alternative, on every
supported DBMS, asserting all three links of the chain — so a break anywhere in it fails,
names the alternative and the database, and cannot hide in an uncovered type.

## Non-goals

- Reforming how "known defects" are recorded so they expire (considered and deliberately
  deferred; scope was set to the round trip itself).
- `SqlDataBinder<T>` value round trips and the per-dialect `QueryFormatter` output table.
  Related invariants, separate work.
- Changing any library code. This is test-only.

## Design

### Location

New file `src/tests/ColumnTypeRoundTripTests.cpp`, registered in the explicit source list in
`src/tests/CMakeLists.txt`. Catch2 tag `[ColumnTypeRoundTrip][SqlSchema]`.

`Ddl2CppColumnTypeTests.cpp` is left untouched. PR #598 rewrites that file's descriptor
table, and this work is based on `master`; a separate file keeps both independently
mergeable. Consolidating the two tables is a follow-up once #598 lands.

### Descriptor

One row per alternative:

```cpp
struct TypeRoundTripCase
{
    std::string_view                     columnName;
    SqlColumnTypeDefinition              declaredType;
    Expectation<SqlColumnTypeDefinition> recoveredType;
    Expectation<std::string>             cxxType;
    ColumnSample                         sample;
};
```

### Expectation<T>

A default value plus optional per-`SqlServerType` overrides. An override is either a value
or a predicate, and carries a mandatory `reason` printed on failure.

The predicate form is required: `Text {}` cannot be asserted exactly, because the
PostgreSQL Unicode driver substitutes its configured `MaxLongVarcharSize` for an unbounded
column's unknown length. That number is a driver setting, not a fact about the column, so
only the shape of the generated type is assertable there.

Overrides exist for genuine backend differences — PostgreSQL has no 1-byte integer and
stores `Tinyint` as `SMALLINT`; `float4` keeps its true 4-byte width. They are documented as
*not* being a place to record a defect as expected behaviour, which is how #586 stayed
green.

### Completeness check

Its own `TEST_CASE`. Collect `declaredType.index()` across the table and assert the covered
set equals every index of `SqlColumnTypeDefinition`. Adding an alternative to the variant
turns the suite red until it gets a row.

A `constexpr` array of the 19 alternative names, in variant order, guarded by
`static_assert(std::size(names) == std::variant_size_v<SqlColumnTypeDefinition>)`, turns a
gap into a message naming the missing type instead of a bare index. The `static_assert`
stops that array from drifting out of sync with the variant.

The table holds `std::function` samples, so it cannot be `constexpr` and a pure
compile-time completeness check is not available without restructuring the file. A runtime
check that fails CI was chosen over that restructuring.

### Samples

Every alternative carries one. A type that genuinely cannot round-trip a value uses an
explicit `NoSample("reason")` marker, so an omission is a visible decision rather than an
oversight.

### Failure output

Every assertion `CAPTURE`s the column name and the server type, so a failure identifies the
alternative and the database without reading the table.

## Validation

Beyond the standard matrix (all three databases under `gcc-release`, and `clang-debug` for
warnings, clang-tidy and ASan/UBSan), the test must be shown to earn its place:

1. Revert the #599 `SqlSchema.cpp` float4/float8 fixup locally; confirm the new
   recovered-definition assertion fails on PostgreSQL naming `Real{53}` vs `Real{24}` —
   i.e. that it would have caught #586. Restore.
2. Remove one table row; confirm the completeness check fails naming that alternative.
   Restore.

A test that cannot be shown to fail without the fix it guards is not a guard. This is the
same discipline applied on `fix/596-sqlguid-byte-order`, where a first-draft regression test
passed with the fix removed because both sides of a symmetric conversion cancelled out.

## Risks

- **Conflict with PR #598.** Avoided by using a new file; the two tables overlap in intent
  and should be consolidated once #598 merges.
- **New per-DBMS deviations surfacing.** Asserting the recovered definition for all 19
  alternatives on three databases will likely expose mappings nobody has looked at. Each one
  is a decision: a genuine backend difference gets a documented override; anything that
  looks like a defect gets an issue, not an override.
