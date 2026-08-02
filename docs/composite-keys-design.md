# Composite key support — design

Status: **proposal.** Nothing implemented yet. The core mechanism below has been prototyped
standalone and compiles; the integration has not been written.

Two givens shape everything:

1. **Every database column is represented by one data member.** A composite primary key is therefore
   several members marked `PrimaryKey`; a composite foreign key is several ordinary column members
   plus one relation member that ties them to the parent.
2. **`BelongsTo` is not that relation member.** It is inseparably a *column* (storage,
   `FieldWithStorage`, a binder over one ODBC column index) as well as a navigator. Widening it would
   break the "one member ⇒ one column" invariant; and under given (1) its column half is unnecessary,
   because the columns already have their own members.

## The design: `CompositeForeignKey<Connection<...>, ...>`

The relation is a list of **connections**, each pairing one of this record's columns with the parent
column it references:

```cpp
struct CkParent
{
    static constexpr std::string_view TableName = "CkParent";

    // One member per column. Both marked PrimaryKey - already legal today.
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName{"part_a"}> partA;
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName{"part_b"}> partB;
    Field<std::optional<SqlAnsiString<40>>, SqlRealName{"caption"}> caption;
};

struct CkChild
{
    static constexpr std::string_view TableName = "CkChild";

    Field<int32_t, PrimaryKey::AutoAssign> id;

    // Ordinary column members. They bind, project and round-trip with no involvement
    // from the relation.
    Field<int32_t, SqlRealName{"ref_a"}> refA;
    Field<int32_t, SqlRealName{"ref_b"}> refB;

    // The relation: a list of connections, each "my column -> their column".
    CompositeForeignKey<Connection<&CkChild::refA, &CkParent::partA>,
                        Connection<&CkChild::refB, &CkParent::partB>> parent;
};
```

Scales to any width by adding connections — the 3-column form that dominates the surveyed schema is
just three of them:

```cpp
CompositeForeignKey<Connection<&Leaf::a, &Hub::k1>,
                    Connection<&Leaf::b, &Hub::k2>,
                    Connection<&Leaf::c, &Hub::k3>> hub;
```

## Why the pairing matters

Earlier drafts spelled the two sides as separate lists (`RecordMemberList<&Child::refA, &Child::refB>`
paired positionally against the parent's). That works, but leaves the pairing *implicit in position* —
so transposing two same-typed columns is silently wrong and uncatchable by the compiler.

With `Connection<from, into>` the pairing is part of the type. A transposition is not a subtle
mis-ordering; it is a different `Connection`, and if the two columns differ in type it does not
compile. This is the decisive advantage over both list-based options.

## What it derives, and what it rejects

Each `Connection<From, Into>` destructures its two member pointers — the codebase already has
`MemberClassTypeHelper<M T::*>` for exactly this — into owner record and field type. So the relation
computes rather than restates:

- `Parent` — the referenced record, from the `Into` pointers. **Never declared by hand.**
- `Child` — the owning record, from the `From` pointers.
- the foreign key values, read through the `From` pointers.

And it `static_assert`s, all three verified against a standalone prototype:

| Error | Caught |
|---|---|
| Connections pointing at *different* parent records | ✅ `"all connections must point at the same record"` |
| Connections starting from different child records | ✅ same mechanism |
| A pair whose field types differ (e.g. `int` wired to `long long`) | ✅ `"pairwise field types must match"` |
| Right-hand member not marked `IsPrimaryKey` | ✅ (checkable, not yet prototyped) |
| Transposing two columns *of the same type* | ❌ — inexpressible to catch; but see below |

The last row is the residual risk, and it is much smaller than with positional lists: a same-typed
transposition is the only remaining silent error, and `ddl2cpp` generating these from the schema means
hand-writing is the exception.

## How it works

### Column binding: nothing changes

`refA` and `refB` are ordinary `Field`s. `RecordColumnCount<CkChild>` is 3 (`id`, `refA`, `refB`) and
the relation contributes 0, because — like `HasMany` and `HasOneThrough` — it has no `Value()` /
`MutableValue()` / `IsModified()`, so it does not satisfy `FieldWithStorage`, therefore not
`RecordColumnMember`. Every projection builder, the output-binding loop and the multi-record column
offset arithmetic skip it automatically, with no special-casing.

This is why no binder, projection or column-count code is touched.

### The relation holds no key storage

Only the loaded target, as `HasOneThrough` does (`std::shared_ptr<Parent>`). The key values are read
through the `From` pointers on demand:

```cpp
// prototyped and working
static auto ValuesOf(Child const& c) { return std::tuple { (c.*Cs::From).Value()... }; }
```

One copy of each value, in the `Field` that owns the column. Nothing to keep in sync.

### Loading reuses machinery that already takes N values

The cheap part. `LoadBelongsTo` today calls `QuerySingle<Parent>(value)`, and `QuerySingle` already
emits one `WHERE` per primary-key field of the target and binds one argument per placeholder
(`DataMapper.hpp:2152`). For a two-key parent it therefore already produces:

```sql
SELECT "part_a", "part_b", "caption" FROM "CkParent" WHERE "part_a" = ? AND "part_b" = ?
```

The composite loader only has to pass every value instead of one — `std::apply` over the tuple above.
**No new SQL generation, no new binding path.**

### Ordering — settled by test

`src/tests/CompositeKeyOrderingTests.cpp` establishes the ground truth:

- `QuerySingle` emits one predicate per `PrimaryKey` member **in C++ member declaration order**, and
  binds arguments positionally.
- Predicate order follows the *C++ declaration*, not the table's column order: the same table mapped
  with its key members declared in the opposite order needs the opposite argument order to reach the
  same row.
- A transposed argument pair is well-formed, binds cleanly, and returns **a different row** — not an
  error. With same-typed key parts (the common case) nothing detects it.

So the loader must not depend on connections being written in the parent's key order. It sorts the
values itself: for each `Connection`, the `Into` pointer identifies the parent member, from which the
member *index* is recovered; sorting the values by that index yields exactly the order
`QuerySingle` will bind them in. Connections may then be listed in any order, and a transposition
becomes impossible rather than merely detectable.

### Navigation

Same surface as `HasOneThrough`, which is the closest existing analogue: `Record()`, `IsLoaded()`,
`Unload()`, `operator->`, `SetAutoLoader()`. So `child.parent->caption` works, and
`ConfigureRelationAutoLoading` gains one branch dispatching on an `IsCompositeForeignKey<T>` trait,
installing a loader that closes over the tuple of key values instead of a single one.

### `ddl2cpp` generation is mechanical

The schema reader already reports both ordered column lists, so the generator emits one `Connection`
per column pair in constraint order. The 90 currently-skipped foreign keys and their inverses become
generatable, and the "Foreign keys ignored" warning for them goes away.

Note this also makes the generator the *primary* author of these declarations, which is what reduces
the same-typed-transposition risk to near zero in practice.

## The inverse

`HasMany` on the parent finds its inverse by locating the child's relation member by type
(`InverseBelongsToIndexOf`), which still works: the composite relation is one member.

Disambiguation has to widen, though. The current selector names a single column
(`HasMany<CkChild, SqlRealName{"ref_a"}>`), and two composite foreign keys from the same child into the
same parent are ambiguous exactly as two single-column ones are. Not hypothetical — the surveyed schema
has a table carrying **three** separate two-column foreign keys into one parent. Options: name the
whole `CompositeForeignKey<...>` type as the selector, or name the child's member pointer directly.
The latter is probably clearer and is a small extension of the existing `RelationSelector` concept.

## The other half: reflected identity

Independent of the relation work. `RecordPrimaryKeyType` resolves through `RecordPrimaryKeyIndex`, a
single member index; `GetPrimaryKeyField()` returns the first match. 25 call sites in `DataMapper.hpp`,
2 in `DataMapperAsync.hpp`.

- **Additive** (recommended first): add `RecordPrimaryKeyTuple<Record>` and `GetPrimaryKeyFields()`
  alongside; only composite-aware code calls them. Nothing existing changes behaviour.
- **Breaking**: make the existing pair tuples when a record has several `PrimaryKey` members. Cleaner,
  but every call site needs auditing and `CreateExplicit` — which *returns*
  `RecordPrimaryKeyType<Record>` — changes shape.

The relation work does not depend on the breaking version, so additive first is the lower-risk order.

## Implementation plan

Ordered so each step is independently testable and nothing half-built is reachable from the public API.

**Step 1 — `Connection` and `CompositeForeignKey`, type level only.**
New header `src/Lightweight/DataMapper/CompositeForeignKey.hpp`.
- `Connection<FromPtr, IntoPtr>`: exposes `From`, `Into`, `FromRecord`, `IntoRecord`, `FromField`,
  `IntoField`, derived via the existing `MemberClassType`.
- `CompositeForeignKey<Connections...>`: derives `Child`/`Parent`, exposes `Count`, and
  `static_assert`s the three rejections (same parent, same child, pairwise field types).
- `IsCompositeForeignKey<T>` trait, mirroring `IsHasOneThrough`.
- No storage yet beyond `std::shared_ptr<Parent>`; no DataMapper involvement.
*Tests:* compile-time only — derivation, `Count`, and that the record's `RecordColumnCount` is
unchanged by adding the member (i.e. it is not a `RecordColumnMember`).

**Step 2 — key extraction in parent-member order.**
- `ValuesOf(Child const&)` returning a tuple read through the `From` pointers.
- `OrderedValuesOf(Child const&)`: the same values permuted into the parent's member declaration
  order, using each `Into` pointer to recover the parent member index. This is the piece the ordering
  tests above exist to justify.
*Tests:* connections declared out of order still produce parent-order values.

**Step 3 — navigation surface.**
`Record()`, `IsLoaded()`, `Unload()`, `operator->`, `SetAutoLoader()` — copied in shape from
`HasOneThrough`, which is the closest existing analogue.
*Tests:* default-constructed reports not-loaded; emplace/unload round-trip.

**Step 4 — auto-loading.**
One branch in `ConfigureRelationAutoLoading` dispatching on `IsCompositeForeignKey`, installing a
loader that `std::apply`s `OrderedValuesOf` into `QuerySingle<Parent>`. No new SQL generation.
*Tests:* against a live DB, two- and three-column parents, including connections written out of order.

**Step 5 — reflected identity, additive.**
`RecordPrimaryKeyTuple<Record>` and `GetPrimaryKeyFields()` alongside the existing single-key pair,
which is left untouched. Only composite-aware code calls the new ones.
*Tests:* tuple shape for multi-key records; unchanged behaviour for single-key ones.

**Step 6 — export and document.**
Add to `Lightweight.cppm`, `CMakeLists.txt` header list, and `docs/usage.md`.

Deferred deliberately, and recorded as such rather than silently skipped:

- **`ddl2cpp` generation.** Mechanical once the spelling is fixed (the schema reader already reports
  both ordered column lists), but it is a separate change on top of a working library API.
- **The inverse (`HasMany` over a composite relation).** Needs the selector to name a column *list*;
  the surveyed schema has a table with three separate two-column foreign keys into one parent, so this
  is required eventually, not optional.
- **`AutoAssign` / `ServerSideAutoIncrement` semantics across several key columns.** Server-side
  auto-increment is meaningless for a multi-column key and wants an explicit `static_assert`
  rejection.
- **C++26 reflection branch.** No binder or column arithmetic changes here, so it should need nothing —
  to be confirmed by building that configuration, not assumed.

## Prototype

The mechanism was checked standalone before proposing it: `Connection` destructuring, `Parent`/`Child`
derivation, `ValuesOf` extraction at 2 and 3 columns, and the three `static_assert` rejections all
behave as described. What is *not* prototyped is the integration — the trait, the
`ConfigureRelationAutoLoading` branch, the loader, and the generator.
