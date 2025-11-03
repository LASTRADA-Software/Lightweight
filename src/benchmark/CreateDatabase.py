
import random
import datetime
from sqlalchemy import (
    create_engine, MetaData, Table, Column,
    Integer, Float, Numeric, String, Text, Unicode, UnicodeText,
    Boolean, DateTime, Date, Time, ForeignKey
)
from sqlalchemy.orm import Session
from sqlalchemy.schema import CreateTable

metadata = MetaData()

ALL_TYPES = [
    Integer, Float, Numeric(10, 2), String(50), Text, Unicode(50), UnicodeText,
    Boolean, DateTime, Date, Time
]

num_tables = 100
min_cols = 3
max_cols = 50
max_foreign_keys = 30

tables = {}

for i in range(num_tables):
    table_name = f"table_{i}"
    columns = [Column("id", Integer, primary_key=True)]

    for c in range(random.randint(min_cols, max_cols)):
        col_name = f"col_{c}"
        col_type = random.choice(ALL_TYPES)
        nullable = random.choice([True, False])
        columns.append(Column(col_name, col_type, nullable=nullable))

    if i > 0:
        num_fks = random.randint(1, min(max_foreign_keys, i))
        fk_targets = random.sample(range(i), num_fks)
        for fk_table in fk_targets:
            columns.append(
                Column(f"fk_{fk_table}", Integer,
                       ForeignKey(f"table_{fk_table}.id"),
                       nullable=random.choice([True, False]))
            )

    tables[i] = Table(table_name, metadata, *columns)

with open("schema.sql", "w") as f:
    for table in metadata.sorted_tables:  # ensures foreign keys are in the right order
        ddl = str(CreateTable(table).compile())
        f.write(ddl + ";\n")
