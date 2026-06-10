# SQL Backup: How It Works

This page explains how the backup engine (`Lightweight::SqlBackup::Backup` / `Restore`)
works: its pipeline, parallelism model, memory and disk profile, fault tolerance, and the
consistency guarantees you can (and cannot) expect. For the on-disk archive layout, see
[sql-backup-format.md](sql-backup-format.md); for the CLI, see @ref dbtool "dbtool.md"
(`dbtool backup` / `dbtool restore`).

## Overview

A backup is a single zip archive containing:

- `metadata.json` — schema description (tables, columns, types, row counts),
- `checksums.json` — SHA-256 per data chunk,
- `data/<table>/NNNN_SS.msgpack` — the table data, split into self-contained msgpack
  chunks (`NNNN` = window index, `SS` = sub-chunk within the window).

Backup and restore work against SQLite3, Microsoft SQL Server, and PostgreSQL through
ODBC; all per-DBMS SQL differences are handled internally.

## Pipeline phases

1. **Schema scan** (single-threaded). All table schemas are read up front; on MS SQL
   Server this uses batched catalog queries.
2. **Chunk planning** (single-threaded). Each table becomes one or more *chunks* — units
   of work for the parallel phase:
   - **Tables with a single numeric (integer-family) primary key** are split into
     disjoint key windows `[lo, hi]`: the planner runs `SELECT MIN(pk), MAX(pk)` (an
     index seek) and divides the span into windows of about `rowsPerChunk` keys,
     capped at 1024 windows per table — sparse key spaces (e.g. snowflake-style IDs)
     get proportionally wider windows instead of millions of empty ones. **Each window
     is an independent chunk, so one big table is read by many workers concurrently.**
   - **All other tables** (no PK, composite PK, non-numeric PK) are read sequentially
     by a single worker as one chunk, ordered with OFFSET-based resumption.
   - Empty PK tables are detected at plan time and skipped.
3. **Parallel data export.** `concurrency` worker threads, each with its own pooled
   database connection, drain a shared chunk queue. Workers fetch rows (bulk array-fetch
   where possible, row-by-row otherwise), encode them to msgpack, hash, and add the
   chunk files to the archive. Chunk filenames are assigned at plan time, so workers
   never coordinate on naming, and chunks may complete in any order.

   **Array-fetch coverage** (one driver round-trip per block of rows instead of one
   `SQLGetData` per cell — the dominant cost on remote servers): integer family, Real,
   Bool, Varchar/Char, Decimal, NVarchar/NChar (UTF-16 bound), Date/DateTime/Timestamp
   (native structs) on all databases; Time and Guid additionally on SQL Server and
   PostgreSQL. Tables containing LOBs (`varchar(max)`, `text`, binary) fall back to the
   row-by-row path. The per-cursor fetch-buffer memory is capped (~4 MB per worker);
   wide tables automatically read fewer rows per round-trip instead of exhausting RAM.
4. **Finalize.** The workers' sealed archives are merged into the final archive as a
   raw copy (no recompression — chunk compression already happened inside the workers,
   overlapped with the network-bound fetch), `metadata.json` and `checksums.json` are
   written, and the archive is closed. The close only streams already-compressed bytes,
   so it takes seconds even for very large backups.

Restore is chunk-order-independent: it enumerates `data/<table>/*.msgpack` entries and
tracks completion by per-table chunk count, so parallel, out-of-order backups restore
exactly like sequential ones.

## Memory and disk profile

RAM usage is **bounded and independent of database size**. Each worker buffers at most
`chunkSizeBytes` (default 10 MB) of encoded rows; flushed chunks go into the worker's
private temp archive under `<output>.zip.tmp/`, which is sealed (compressed in the
worker thread) every `workerArchiveBytes` (default 256 MB) of input. The memory ceiling
is therefore about `concurrency × workerArchiveBytes` plus fetch buffers — and because
the temp archives hold **compressed** data, the extra disk needed during the run is
only about the size of the final archive itself. The temp directory is removed
automatically when the backup finishes (also on failure).

## Fault tolerance

- **Transient errors** (connection loss, deadlocks, timeouts) are retried per chunk with
  reconnect and exponential backoff (`RetrySettings`). A PK-window chunk re-reads just
  its own window; rows already counted are not double-reported, and chunk files are
  rewritten in place (`ZIP_FL_OVERWRITE`), so retries are idempotent.
- **Hard errors** fail the affected table (reported through the progress callback);
  other tables continue.
- **Re-running a backup** to the same output is always safe — every entry is
  overwritten, never appended.
- **Integrity:** every chunk is SHA-256-hashed into `checksums.json` and verified on
  restore.

## Consistency caveats (online backup)

The backup runs online, without a global snapshot or table locks. If the database is
being written to during the run:

- different tables (and different windows of one table) are read at different times,
  so cross-table consistency is not guaranteed;
- rows inserted above `MAX(pk)` after a table was planned are not included;
- a row updated between two windows may appear in its old or new version.

For a strictly consistent backup, run against a quiesced database, a snapshot, or a
restored replica.

## Tuning

| Knob | Default | Effect |
|------|---------|--------|
| `concurrency` (jobs) | caller-defined | Worker threads *and* database connections. The export phase is mostly network/IO-bound, so more jobs ≈ more concurrent result streams. |
| `BackupSettings::rowsPerChunk` | 100 000 | Target keys per PK window. Smaller = finer load balancing, more files; larger = fewer round-trips per table. |
| `BackupSettings::chunkSizeBytes` | 10 MB | Byte threshold per chunk file flush; sets data-file granularity. |
| `BackupSettings::workerArchiveBytes` | 256 MB | Uncompressed input per worker temp archive before it is sealed (compressed). Bounds worker memory at ~`jobs × workerArchiveBytes`; lower it on memory-constrained machines. |
| `BackupSettings::method` / `level` | Deflate / 6 | Compression method and level (applied at archive close). |
| `RetrySettings` | sensible defaults | Max retries and backoff for transient errors. |

## See also

- [sql-backup-format.md](sql-backup-format.md) — archive layout and msgpack chunk format.
- @ref dbtool "dbtool.md" — `dbtool backup` / `dbtool restore` CLI usage.
