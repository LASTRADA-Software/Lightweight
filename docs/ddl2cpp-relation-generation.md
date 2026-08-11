# Relation generation in ddl2cpp

## Status

`ddl2cpp` generates `Light::BelongsTo` on the child side of a single-column foreign key, and — as of
the relation-generation work on this branch — the inverse and through relations too: `HasMany`,
`HasManyThrough` and `HasOneThrough`. See `src/tests/CxxModelRelationTests.cpp` for the rule-by-rule
coverage.

This page records what a real production schema needs, which shapes are deliberately *not* collapsed,
and what remains genuinely not representable.

> **Depends on the relation selectors from PR #528.** Generating inverse relations for a real schema
> is impossible without them: this reference schema has table pairs joined by up to **55** foreign
> keys from the same child table, and `HasMany<Child>` without a selector is a compile error as soon
> as more than one foreign key links the pair. On `master` today `HasMany` takes only
> `<typename OtherRecord>`; the `SqlRealName` selector overloads of `HasMany`, `HasManyThrough` and
> `HasOneThrough` live on `fix/hasmany-inverse-by-type`. This work therefore branches from there
> rather than from `master`, and must not merge ahead of it.

There is also **no `HasOne`** type in the library — only `HasOneThrough`. A one-to-one relation that
is *not* across a join table has no representation, so the planner records it as `Kind::HasOne` and
the emitter falls back to `HasMany`, with a note in the generated header saying why. Adding a real
`HasOne` is out of scope here.

Composite foreign keys remain ungenerated, but are no longer inexpressible: see
`docs/composite-keys-design.md` for `CompositeForeignKey` / `Connection`. Teaching the generator to
emit them is the natural follow-up.

## Reference schema

The numbers below come from a large production schema (MS SQL Server 2022, single schema), the
biggest this generator is pointed at in practice. Table and column names are not reproduced; only
the structural shapes and their counts, which is what the generation rules are derived from.

| Metric | Count |
|--------|-------|
| Tables | 686 |
| Columns | 10,830 |
| Foreign keys | 1,860 |
| — single-column | 1,770 |
| — composite (multi-column) | 90 |
| Primary keys, composite | 170 |
| Tables with no primary key | 0 |
| Self-referential foreign keys | 0 |
| Foreign keys onto a non-primary-key column | 0 |

### Column types in use

`float` (4060), `int` (3632), `varchar` (1302), `datetime` (629), `char` (605), `text` (434),
`tinyint` (90), `money` (31), `smallint` (19), `real` (12), `image` (7), `bigint` (4),
`nvarchar` (3), `varbinary` (2).

No computed columns, no alias/CLR/`sysname` types, so the alias-type resolution added for
`sysname` is not exercised by this schema (it remains needed for others).

## What this schema needs that is not generated

### 1. `HasMany` — the inverse of every single-column foreign key

1,770 single-column foreign keys each imply a `HasMany` on the referenced side. None is emitted.

Many of these are ambiguous and require the `SqlRealName` selector that `HasMany` already
supports: there are dozens of table pairs joined by more than one foreign key from the same child
table, the worst carrying **55** foreign keys between a single pair. Auto-detection cannot pick
between them — `InverseBelongsToIndexOf` makes that a compile error — so the generator must emit
the selector rather than a bare `HasMany<Child>`.

### 2. `HasManyThrough` — many-to-many across a join table

159 tables look like join tables (exactly two single-column foreign keys to two distinct tables).
Of those, **84 also have a composite primary key**, which is the classic many-to-many marker.

Concrete examples, both two-column tables:

```
project_user (project_id -> project, user_id -> user)
tenant_customer (customer_id -> customer, tenant_id -> tenant)
```

(Shapes reproduced with neutral names: two columns, each a single-column foreign key to a distinct
table, composite primary key over exactly those two.)

Each should yield a `HasManyThrough` on both referenced tables. Because both foreign keys of the
join record point at *different* tables here, the selectors are only needed when a join table
points twice at the same target.

### 3. `HasOneThrough` / one-to-one

24 single-column foreign keys sit under a single-column unique index, which makes the relation
one-to-one rather than one-to-many. Those should be a scalar relation, not a `HasMany`.

## What is not representable, and why

These are limits of the relation model, not gaps in the generator. They are reported rather than
silently mismodelled.

| Construct | Count here | Reason |
|-----------|-----------:|--------|
| Composite foreign keys | 90 | `BelongsTo` names a single referenced field (`&Record::member`); there is no multi-column form. `ddl2cpp` already counts and warns about these. |
| Composite primary keys | 170 | `BelongsTo` requires the referenced field to be a primary key, and a relation is expressed through exactly one such field. Affected tables still generate as plain records; only relations *into* them are skipped. |
| `text` / `image` columns | 441 | Deprecated LOB types. They map as text/binary, but `SQLGetData` semantics differ from `varchar(max)`/`varbinary(max)`; see `docs/data-binder.md`. |

A foreign key onto a non-primary-key unique column is also not representable, and does not occur
in this schema.

## Generation rules

The rules the generator follows, in the order it applies them. Only single-column foreign keys are
considered throughout; composite ones are counted and skipped as before.

1. **`BelongsTo`** on the child side of each foreign key — unchanged.
2. **`HasManyThrough`** on each of the two referenced tables, when a table qualifies as a join
   table: exactly two single-column foreign keys, pointing at two *distinct* tables, and no
   non-key columns beyond those two foreign keys and its own key columns.
3. **`HasOneThrough`** in place of `HasManyThrough`, on a given owner's side, when the join table's
   foreign key back to *that owner* is covered by a single-column unique index — meaning that owner
   can appear in at most one join row, hence reach at most one record on the other side. The other
   side keeps whatever `HasManyThrough`/`HasOneThrough` its own foreign key's uniqueness implies,
   independently.
4. **`HasMany`** on the referenced side of every remaining foreign key, i.e. one that is not part
   of a join table already covered by rule 2 or 3.
5. **Scalar rather than collection** when the child's foreign key is itself covered by a
   single-column unique index: the relation is one-to-one.

A selector (`SqlRealName { "<column>" }`) is emitted whenever the referenced table is reachable
from the same child table through more than one foreign key, which is what makes the ambiguous
pairs above compile.

Because a relation member has to name the *other* record type, and both records must be complete
at that point, the generator emits relations only where its existing dependency ordering already
guarantees a declaration order — the same constraint that governs `BelongsTo` today, and the reason
a self-referential foreign key falls back to a plain field.
