# LUP SQL Migration File Format

This document describes the SQL file format used by LUP (Lastrada UPdate) migration files.

## Overview

LUP migration files are SQL scripts with specific conventions for statement delimitation, comments, and directives. The `lup2dbtool` utility parses these files and generates C++ migration code using the Lightweight Migration API.

## File Naming Convention

### Initial Schema Files
- Pattern: `init_m_MAJOR_MINOR_PATCH.sql`
- Example: `init_m_2_1_5.sql` - Creates base database structure for version 2.1.5
- Contains `CREATE TABLE` statements
- Ends with `INSERT INTO DB_PROPERTIES VALUES (4, 215, 'DB Version')`

### Update Migration Files
- Pattern: `upd_m_MAJOR_MINOR_PATCH.sql`
- Examples:
  - `upd_m_6_08_08.sql` - Single version update
  - `upd_m_2_1_6__2_1_9.sql` - Version range update
- Contains `ALTER TABLE`, `INSERT`, `UPDATE` statements
- Ends with `UPDATE DB_PROPERTIES SET VALUE=... WHERE NR=4`

## Format Specification

### Statement Delimiter
- **Newline-based** - Each SQL statement is on its own line (or multiple lines for complex statements)
- **No semicolons** - Statements are NOT terminated by semicolons

### Comment Prefix
- `--` (SQL standard single-line comments)

### File Encoding
- **Source:** Windows-1252 (legacy files)
- **Output:** UTF-8 (converted by lup2dbtool)

## Supported Directives

LUP files use special comments as directives:

| Directive | Example | Purpose |
|-----------|---------|---------|
| Base Version | `--[Based on Lup Version 6.8.7]` | Declares prerequisite version |
| Version Marker | `--/* LUP-Version: 6_08_08 */` | Marks target version |
| Print Message | `--print 'LUP-DB Update 6_08_08'` | Migration title/description |
| Regular Comment | `-- Any text` | Preserved as C++ comments |

### Base Version Directive
```sql
--[Based on Lup Version 6.8.7]
```
Indicates that this migration requires version 6.8.7 to be already applied.

### Version Marker
```sql
--/* LUP-Version: 6_08_08 */
```
Marks the target version of this migration.

### Print Message
```sql
--print 'LUP-DB Update 6_08_08'
```
Provides a human-readable title for the migration.

## Supported SQL Statements

### CREATE TABLE
```sql
CREATE TABLE table_name (
    column_name type [NULL | NOT NULL] [PRIMARY KEY],
    FOREIGN KEY (column) REFERENCES other_table(column)
)
```

### ALTER TABLE ADD COLUMN
```sql
ALTER TABLE table_name ADD COLUMN column_name type [NULL | NOT NULL]
```

### ALTER TABLE ADD FOREIGN KEY
```sql
ALTER TABLE table_name ADD FOREIGN KEY (column) REFERENCES other_table(column)
```

### ALTER TABLE ADD COMPOSITE FOREIGN KEY
```sql
ALTER TABLE table_name ADD FOREIGN KEY (col1, col2) REFERENCES other_table(ref1, ref2)
```

### ALTER TABLE DROP FOREIGN KEY
```sql
ALTER TABLE table_name DROP FOREIGN KEY (column) REFERENCES other_table(column)
```

### DROP TABLE
```sql
DROP TABLE table_name
```

### INSERT
```sql
INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...)
```

### UPDATE
```sql
UPDATE table_name SET column = value WHERE condition
```

### DELETE
```sql
DELETE FROM table_name WHERE condition
```

### CREATE INDEX
```sql
CREATE INDEX index_name ON table_name (column1, column2, ...)
CREATE UNIQUE INDEX index_name ON table_name (column1, column2, ...)
```

## Data Types

| SQL Type | Description |
|----------|-------------|
| `INTEGER` | 32-bit integer |
| `SMALLINT` | 16-bit integer |
| `BIGINT` | 64-bit integer |
| `REAL`, `FLOAT`, `DOUBLE` | Floating point |
| `CHAR(n)` | Fixed-length string |
| `VARCHAR(n)` | Variable-length string |
| `NVARCHAR(n)` | Unicode variable-length string |
| `TEXT`, `LONG VARCHAR` | Long text |
| `DATETIME`, `TIMESTAMP` | Date and time |
| `DATE` | Date only |
| `TIME` | Time only |
| `BOOLEAN`, `BOOL`, `BIT` | Boolean |
| `GUID`, `UNIQUEIDENTIFIER` | UUID/GUID |
| `DECIMAL(p,s)`, `NUMERIC(p,s)` | Decimal with precision and scale |
| `VARBINARY(n)` | Variable-length binary |

## Version Encoding

LUP versions are encoded as integers for storage in the properties table:

| Version | Integer Encoding | Migration Timestamp |
|---------|------------------|---------------------|
| 2.1.6 | 216 | 20000000000216 |
| 3.0.0 | 300 | 20000000000300 |
| 6.0.0 | 60000 | 20000000060000 |
| 6.8.8 | 60808 | 20000000060808 |

Formula: `major * 10000 + minor * 100 + patch`

The migration timestamp adds a `20000000000000` prefix to the version integer.

## Example Migration File

```sql
--[Based on Lup Version 6.8.7]
--print 'LUP-DB Update 6_08_08'

-- Categories
insert into CATEGORIES (NAME, ID, CODE) values ('Free Test', 105, 'FREE')

-- Document types
insert into DOCUMENT_TYPES (ID, NAME) values (43, 'Report Templates')

-- Weather data
alter table WEATHER_DATA add column GRAIN_SIZE NVARCHAR(255) NULL

-- Material data
alter table MATERIAL_DATA add column PHASE_ANGLE double NULL

-- LUP-Version: 6_08_08
update DB_PROPERTIES set VALUE=60808 where NR=4
```

## Generated C++ Output

The above SQL file would generate:

```cpp
LIGHTWEIGHT_SQL_MIGRATION(20000000060808, "LUP-DB Update 6_08_08")
{
    // Categories
    plan.Insert("CATEGORIES")
        .Set("NAME", "Free Test")
        .Set("ID", 105)
        .Set("CODE", "FREE");

    // Document types
    plan.Insert("DOCUMENT_TYPES")
        .Set("ID", 43)
        .Set("NAME", "Report Templates");

    // Weather data
    plan.AlterTable("WEATHER_DATA")
        .AddNotRequiredColumn("GRAIN_SIZE", NVarchar(255));

    // Material data
    plan.AlterTable("MATERIAL_DATA")
        .AddNotRequiredColumn("PHASE_ANGLE", Real());

    // LUP-Version: 6_08_08
    plan.Update("DB_PROPERTIES")
        .Set("VALUE", 60808)
        .Where("NR", "=", 4);
}
```

### CREATE INDEX Mapping

SQL input:
```sql
CREATE INDEX idx_user_email ON Users (email)
CREATE UNIQUE INDEX idx_user_name ON Users (first_name, last_name)
```

Generated C++ output:
```cpp
plan.CreateIndex("idx_user_email", "Users", {"email"});
plan.CreateUniqueIndex("idx_user_name", "Users", {"first_name", "last_name"});
```

### DROP TABLE Mapping

SQL input:
```sql
DROP TABLE temp_data
```

Generated C++ output:
```cpp
plan.DropTable("temp_data");
```

### Composite Foreign Key Mapping

SQL input:
```sql
ALTER TABLE order_items ADD FOREIGN KEY (order_id, product_id) REFERENCES catalog(oid, pid)
```

Generated C++ output:
```cpp
plan.AlterTable("order_items")
    .AddCompositeForeignKey({"order_id", "product_id"}, "catalog", {"oid", "pid"});
```

### DROP FOREIGN KEY Mapping

SQL input:
```sql
ALTER TABLE orders DROP FOREIGN KEY (customer_id) REFERENCES customers(id)
```

Generated C++ output:
```cpp
plan.AlterTable("orders")
    .DropForeignKey("customer_id");
```
