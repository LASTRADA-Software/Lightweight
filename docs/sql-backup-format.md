# Lightweight SQL Backup File Format

This document specifies the file format used by the Lightweight `SqlBackup` facility and as used by the `dbtool` command-line tool. The backup file is a standard **ZIP archive** containing a metadata manifest and a collection of data chunks for each table.

## 1. Archive Structure

The backup file is a ZIP archive with the following internal structure:

```text
/
├── metadata.json           # Global manifest and schema definition
└── data/                   # Data directory
    └── <TableName>/        # One folder per table
        ├── 0001.msgpack    # Data chunks (MsgPack)
        ├── 0002.msgpack
        └── ...
```

- **`metadata.json`**: Located at the root. Contains backup metadata and the full database schema.
- **`data/<TableName>/`**: Contains the actual row data for the table, split into multiple chunks associated with the table.
- **Chunks**: Files are named sequentially (e.g., `0001.msgpack`, `0002.msgpack`) and contain a batch of rows in a column-oriented MessagePack format.

## 2. Metadata Manifest (`metadata.json`)

The `metadata.json` file is a JSON object with the following fields:

| Field | Type | Description |
| --- | --- | --- |
| `format_version` | String | The backup format version (e.g., `1.0`). |
| `creation_time` | ISO 8601 String | Timestamp of when the backup was created (e.g., `2024-01-01T12:00:00Z`). |
| `original_connection_string` | String | The connection string used to create the backup. |
| `schema_name` | String | The database schema name (e.g., `dbo` for SQL Server). |
| `server` | Object | Server identification information. |
| `schema` | Array | A list of table definitions. |

### 2.1 Server Identification

The `server` object contains information about the source database server:

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | The DBMS name (e.g., `Microsoft SQL Server`, `PostgreSQL`, `SQLite`). |
| `version` | String | The DBMS version string reported by ODBC (e.g., `16.00.4165`). |
| `driver` | String | The ODBC driver name used for the connection. |
| `full_version` | String | (Optional) Full version string from the database server. For SQL Server this is the result of `SELECT @@VERSION`, for PostgreSQL `SELECT version()`, and for SQLite `SELECT sqlite_version()`. |

### 2.2 Table Definition

Each object in the `schema` array represents a table:

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | The table name. |
| `rows` | Integer | Total number of rows in the table at the time of backup. |
| `columns` | Array | List of column definitions. |
| `foreign_keys` | Array | List of foreign key constraints. |
| `primary_keys` | Array | List of column names that form the primary key. |

### 2.3 Column Definition

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | Column name. |
| `type` | String | SQL type (e.g., `integer`, `text`, `blob`, `real`, `bool`). |
| `is_primary_key` | Boolean | `true` if this column is part of the primary key. |
| `is_nullable` | Boolean | `true` if the column allows NULL values. |
| `is_auto_increment` | Boolean | `true` if the column is auto-incrementing. |
| `is_unique` | Boolean | `true` if the column has a unique constraint. |
| `default_value` | String | (Optional) The default value expression. |
| `size` | Integer | (Optional) Size for `varchar`, `binary`, etc. |
| `precision` | Integer | (Optional) Precision for `decimal`. |
| `scale` | Integer | (Optional) Scale for `decimal`. |

### 2.4 Foreign Key Definition

Each object in the `foreign_keys` array represents a foreign key constraint:

| Field | Type | Description |
| --- | --- | --- |
| `name` | String | The name of the foreign key constraint (may be empty). |
| `columns` | Array | List of column names in the local table. |
| `referenced_table` | String | The name of the referenced (parent) table. |
| `referenced_columns` | Array | List of column names in the referenced table. |

## 3. Data Chunk Format (`.msgpack`)

Each `.msgpack` file in the `data/` directories is a standalone **MessagePack** file encoding a batch of rows in a **column-oriented** layout.

### 3.1 Top-Level Structure

The file contains a single MessagePack **Array** (size `N`), where `N` is the number of columns in the table.

```text
[ Column0, Column1, ..., ColumnN ]
```

### 3.2 Column Object

Each element in the top-level array is a MessagePack **Map** representing one column's data for the current batch. The map has three keys:

| Key | Description | Type |
| --- | --- | --- |
| `"t"` | **Type** | String identifier for the column data type. |
| `"d"` | **Data** | The actual values (encoding depends on type). |
| `"n"` | **Nulls** | Array of Booleans indicating NULL values. |

### 3.3 Data Types and Encoding

The `"t"` field determines how the `"d"` field is encoded.

| Type Identifier (`"t"`) | SQL Types | Data Encoding (`"d"`) |
| --- | --- | --- |
| `"nil"` | - | **Null**: Present if the column contains only NULL values in this chunk. |
| `"i64"` | `integer`, `bigint`, `smallint`, `tinyint` | **Packed Binary**: A single byte array containing contiguous big-endian 64-bit integers. |
| `"f64"` | `real`, `double` | **Packed Binary**: A single byte array containing contiguous big-endian 64-bit floating point numbers (IEEE 754). |
| `"str"` | `text`, `varchar`, `char`, etc. | **String Array**: A standard MessagePack Array of Strings. |
| `"bool"` | `bool` | **Bool Array**: A standard MessagePack Array of Booleans. |
| `"bin"` | `binary`, `varbinary`, `image` | **Binary Array**: A standard MessagePack Array of Binary blobs. |

### 3.4 Null Handling (`"n"`)

The `"n"` field provides a parallel array of booleans matching the length of the data array.

- `true`: The value at this index is **NULL**. The corresponding value in `"d"` is undefined/default (e.g., 0, empty string).
- `false`: The value at this index is valid.

### 3.5 Packed Binary Format

For performance, numeric types (`i64`, `f64`) are stored as a single binary blob rather than a MessagePack array of individual integers/floats.

- **Structure**: `[Value1][Value2]...[ValueN]`
- **Endianness**: **Big-Endian**.
- **Example**: 3 integers would be stored as a `3 * 8 = 24` byte blob.

## 4. File Extension

- **Extension**: `.msgpack`
- The file names are sequential 4-digit numbers starting from `0001` (e.g., `0001.msgpack`, `0120.msgpack`).

## 5. Compression

The backup file supports various compression methods for ZIP entries. Compression is configured using the `--compression` and `--compression-level` options in `dbtool`.

### 5.1 Supported Compression Methods

| Method | Description | Compatibility |
|--------|-------------|---------------|
| `none` / `store` | No compression (ZIP_CM_STORE) | Universal - all ZIP tools |
| `deflate` | Deflate compression (ZIP_CM_DEFLATE) | Universal - default, most compatible |
| `bzip2` | Bzip2 compression (ZIP_CM_BZIP2) | Good - most modern tools |
| `lzma` | LZMA compression (ZIP_CM_LZMA) | Limited - requires LZMA support |
| `zstd` | Zstandard compression (ZIP_CM_ZSTD) | Limited - requires Zstd support |
| `xz` | XZ compression (ZIP_CM_XZ) | Limited - requires XZ support |

### 5.2 Compression Levels

The compression level is specified as an integer from 0 to 9:

- **0**: No compression (fastest)
- **1-3**: Fast compression with lower ratio
- **4-6**: Balanced compression (default: 6)
- **7-9**: Best compression ratio (slowest)

### 5.3 Runtime Availability

Not all compression methods may be available depending on how libzip was compiled. Use `dbtool` to check which methods are supported on your system:

- Methods that are not supported will result in an error message listing available alternatives.
- The `deflate` method is typically always available and is the recommended default.

### 5.4 Restoration Compatibility

Backup files can be restored regardless of which compression method was used, as long as the restoring system's libzip supports the compression method used during backup. For maximum portability, use `deflate` compression.
