#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a SQLite schema with a deep chain of foreign-key-connected tables.

This is the input for the *compilation-time* benchmark: ddl2cpp turns the
generated schema into C++ entity headers whose relationships force the
DataMapper to recursively instantiate templates. The more tables and the
denser the foreign-key graph, the heavier the recursive instantiation.

The generator is intentionally stdlib-only (no SQLAlchemy) and **seeded**, so
the emitted schema is byte-for-byte reproducible. That reproducibility is the
whole point: to compare compile time before/after a change you must compile the
*same* entity graph both times.

Usage:
    python ./src/benchmark/CreateDatabase.py            # default: 100 tables, seed 42
    python ./src/benchmark/CreateDatabase.py --num-tables 50 --seed 7
"""

import argparse
import random

# Declared column types, rendered exactly as the SQLite dialect spells them so
# ddl2cpp maps each to a stable C++ type. Keep this list aligned with the type
# coverage we want the binder instantiation to exercise.
ALL_TYPES = [
    "INTEGER",
    "FLOAT",
    "NUMERIC(10, 2)",
    "VARCHAR(50)",
    "TEXT",
    "BOOLEAN",
    "DATETIME",
    "DATE",
    "TIME",
]


def build_schema(rng, num_tables, min_cols, max_cols, max_foreign_keys):
    """Return the schema as a single CREATE TABLE string.

    Tables are emitted in dependency order (table_i may only reference
    table_j for j < i), so the script's output is already topologically sorted
    and loads cleanly into SQLite without deferred constraints.
    """
    statements = []
    for i in range(num_tables):
        columns = ["    id INTEGER NOT NULL PRIMARY KEY"]

        for c in range(rng.randint(min_cols, max_cols)):
            col_type = rng.choice(ALL_TYPES)
            nullable = "" if rng.choice([True, False]) else " NOT NULL"
            columns.append(f"    col_{c} {col_type}{nullable}")

        constraints = []
        if i > 0:
            num_fks = rng.randint(1, min(max_foreign_keys, i))
            for fk_table in rng.sample(range(i), num_fks):
                nullable = "" if rng.choice([True, False]) else " NOT NULL"
                columns.append(f"    fk_{fk_table} INTEGER{nullable}")
                constraints.append(
                    f"    FOREIGN KEY(fk_{fk_table}) REFERENCES table_{fk_table} (id)"
                )

        body = ",\n".join(columns + constraints)
        statements.append(f"CREATE TABLE table_{i} (\n{body}\n);")

    return "\n\n".join(statements) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=42,
                        help="RNG seed; fix it so the schema is reproducible (default: 42)")
    parser.add_argument("--num-tables", type=int, default=100,
                        help="number of tables in the chain (default: 100)")
    parser.add_argument("--min-cols", type=int, default=3,
                        help="minimum non-key columns per table (default: 3)")
    parser.add_argument("--max-cols", type=int, default=50,
                        help="maximum non-key columns per table (default: 50)")
    parser.add_argument("--max-foreign-keys", type=int, default=30,
                        help="maximum foreign keys per table (default: 30)")
    parser.add_argument("--output", default="schema.sql",
                        help="output schema file (default: schema.sql)")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    schema = build_schema(rng, args.num_tables, args.min_cols,
                          args.max_cols, args.max_foreign_keys)

    with open(args.output, "w") as f:
        f.write(schema)

    print(f"Wrote {args.output}: {args.num_tables} tables (seed {args.seed}).")


if __name__ == "__main__":
    main()
