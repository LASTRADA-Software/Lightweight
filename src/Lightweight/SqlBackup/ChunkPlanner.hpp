// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlSchema.hpp"
#include "../SqlServerType.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Lightweight::SqlBackup::detail
{

/// How a chunk's row window is expressed in SQL.
enum class ChunkStrategy : uint8_t
{
    /// OFFSET/FETCH window: rows [offset, offset+limit). Requires a total ORDER BY.
    Offset,
    /// Primary-key range window: pk >= lo AND pk <= hi. Single numeric PK only.
    PrimaryKeyRange,
};

/// Shared per-table aggregation point for chunks processed concurrently by multiple workers.
/// Owned by the ChunkPlan (stable addresses); chunks point into it.
struct TableBackupState
{
    /// Chunks of this table not yet completed; the worker that drops this to 0 reports Finished.
    std::atomic<size_t> remainingChunks { 0 };
    /// Table-cumulative rows processed across all workers (drives currentRows in progress updates).
    std::atomic<size_t> processedRows { 0 };
    /// First-chunk-fires-Started latch.
    std::atomic<bool> started { false };
    /// Set when any chunk of this table failed; suppresses the Finished progress event.
    std::atomic<bool> failed { false };
    /// PK-range: plan-time estimate (pkMax - pkMin + 1). OFFSET: set by the worker after COUNT(*).
    std::atomic<size_t> totalRows { 0 };
};

/// One unit of backup work: a bounded row-range of a single table. Multiple chunks of the
/// same table can be processed concurrently. The chunk index is assigned at plan time so
/// produced chunk filenames are unique without inter-worker coordination.
struct Chunk
{
    /// The table this chunk reads from (pointer into the caller-owned table list; must outlive the chunk).
    SqlSchema::Table const* table = nullptr;
    /// How the window is expressed.
    ChunkStrategy strategy = ChunkStrategy::Offset;
    /// Stable per-table window index (0-based), assigned at plan time, used to derive a unique
    /// chunk filename. PK-range tables get one chunk per window; OFFSET tables have a single
    /// seed chunk with index 0.
    uint32_t windowIndex = 0;
    /// OFFSET-strategy resume cursor (rows to skip). Valid when strategy == Offset.
    size_t offset = 0;
    /// PrimaryKeyRange window bounds [lo, hi]. Valid when strategy == PrimaryKeyRange.
    int64_t lo = 0;
    int64_t hi = 0;
    /// Primary-key column name for PrimaryKeyRange strategy; empty otherwise.
    std::string pkColumn;
    /// Informational: true if the table has at least one LOB column (varchar(max)/text/binary).
    /// NOTE: dispatch routes on `arrayFetchable` (which already excludes LOB tables), not on this
    /// field directly. Kept for observability / potential future use; not consulted by the fetch path.
    bool hasLob = false;
    /// True if every column of the table is in the "array-fetch-safe" type set (integer family,
    /// Real, Bool, Varchar/Char, Decimal, NVarchar/NChar) AND the table has no LOB column. Such
    /// chunks use the bulk RowArrayCursor read path (one SQLFetchScroll per block instead of one
    /// SQLGetData per cell). Tables containing temporal (Date/Time/DateTime/Timestamp), Guid, or
    /// binary/LOB columns are NOT array-fetchable yet and keep the proven single-row path (their
    /// array representation is pending P6 follow-up tasks).
    bool arrayFetchable = false;
    /// Shared progress/completion state of this chunk's table (owned by the ChunkPlan). Never
    /// null for chunks produced by PlanChunks — the workers dereference it unconditionally.
    TableBackupState* state = nullptr;
};

/// Queries the inclusive [MIN(pk), MAX(pk)] bounds of @p table's @p pkColumn at plan time,
/// or std::nullopt if the table is empty. Injected so PlanChunks stays database-free in tests.
using PkBoundsFunction =
    std::function<std::optional<std::pair<int64_t, int64_t>>(SqlSchema::Table const& table, std::string const& pkColumn)>;

/// The planned chunk work-list plus the per-table shared state the chunks point into.
struct ChunkPlan
{
    /// Flat chunk work-list, in table order then window order. Single-numeric-PK tables contribute
    /// one PrimaryKeyRange chunk per window; all other tables contribute one OFFSET seed chunk.
    std::vector<Chunk> chunks;
    /// One state per table that has chunks; deque for stable addresses (chunks hold pointers,
    /// and std::deque moves without relocating elements).
    std::deque<TableBackupState> tableStates;
    /// Single-numeric-PK tables with no rows: no chunks; caller reports them Finished(0).
    std::vector<SqlSchema::Table const*> emptyTables;
};

/// Plans the chunk work-list for a set of tables. Tables with a single numeric primary key are
/// split into one PrimaryKeyRange chunk per key window (bounds via @p pkBounds, window width via
/// CappedWindowWidth) so multiple workers can process one table concurrently; all other tables get
/// a single OFFSET seed chunk. The returned plan owns the per-table states the chunks point into
/// and must outlive the workers.
///
/// @param tables The tables to back up (must outlive the returned plan — chunks hold pointers).
/// @param rowsPerChunk Target rows per chunk window.
/// @param pkBounds Plan-time MIN/MAX query for a table's primary-key column.
/// @param serverType The DBMS being backed up (gates per-DBMS array-fetch admissions, see
///                   TableIsArrayFetchable).
/// @return The chunk plan (work-list + per-table states + empty-table list).
[[nodiscard]] LIGHTWEIGHT_API ChunkPlan PlanChunks(std::vector<SqlSchema::Table> const& tables,
                                                   size_t rowsPerChunk,
                                                   PkBoundsFunction const& pkBounds,
                                                   SqlServerType serverType);

/// Returns true if @p table has a column whose type cannot be fixed-stride array-bound
/// (varchar(max)/text/nvarchar(max)/binary/varbinary/image LOBs). Such tables use the
/// single-row fallback fetch path.
[[nodiscard]] LIGHTWEIGHT_API bool TableHasLobColumn(SqlSchema::Table const& table);

/// Returns true if @p table can be read through the bulk RowArrayCursor path while staying
/// byte-identical to the trusted single-row decode path.
///
/// A table is array-fetchable iff it has no LOB column AND every column type is in the
/// "safe" set whose array-cursor representation matches the single-row path exactly:
///   - integer family: Integer / Bigint / Smallint / Tinyint  (read as int64),
///   - Real                                                    (read as double),
///   - Bool                                                    (read as 0/1 int64 -> bool),
///   - Varchar / Char                                          (read as text),
///   - Decimal                                                 (already string / MSSQL-CONVERTed),
///   - NVarchar / NChar                                        (P6: SQL_C_WCHAR -> ToUtf8, the same
///                                                              conversion as the single-row
///                                                              u16string read),
///   - Time, on PostgreSQL/MSSQL only                          (P6: both paths read TIME as driver
///                                                              text via SQL_C_CHAR, preserving
///                                                              fractional seconds; SQLite's
///                                                              single-row path uses the native
///                                                              SqlTime struct instead, so Time
///                                                              stays single-row there),
///   - Date / DateTime / Timestamp, on PostgreSQL/MSSQL only  (native SQL_DATE_STRUCT /
///                                                              SQL_TIMESTAMP_STRUCT array binds,
///                                                              formatted via the same std::format
///                                                              as the single-row SqlDate /
///                                                              SqlDateTime reads; SQLite describes
///                                                              temporal columns as text, so the
///                                                              native-struct decode would mismatch
///                                                              the bound type — they stay
///                                                              single-row there).
///
/// Guid, Text, and binary/LOB columns are still EXCLUDED: Guid array decode is pending a P6
/// follow-up task, and Text/binary are LOBs that cannot be fixed-stride array-bound at all.
/// Tables with any such column keep the proven single-row path.
///
/// @param table The table to classify.
/// @param serverType The DBMS being backed up (gates the per-DBMS admissions above).
[[nodiscard]] LIGHTWEIGHT_API bool TableIsArrayFetchable(SqlSchema::Table const& table, SqlServerType serverType);

/// Returns the name of the table's sole primary-key column if it is a single numeric (integer-family)
/// PK suitable for range partitioning; std::nullopt otherwise (no PK, composite PK, or non-numeric PK).
[[nodiscard]] LIGHTWEIGHT_API std::optional<std::string> SingleNumericPrimaryKey(SqlSchema::Table const& table);

/// Computes the inclusive primary-key window list [lo, hi] covering [pkMin, pkMax] with at most
/// @p rowsPerChunk keys per window. Windows are contiguous and disjoint (next lo = previous hi + 1),
/// so every key in [pkMin, pkMax] is covered exactly once. Returns empty if pkMin > pkMax.
/// Guards against int64 overflow when pkMax is near INT64_MAX.
///
/// @param pkMin Inclusive lower bound of the key range to cover.
/// @param pkMax Inclusive upper bound of the key range to cover.
/// @param rowsPerChunk Maximum number of keys per window (clamped to at least 1).
/// @return The ordered list of closed [lo, hi] windows; empty if pkMin > pkMax.
[[nodiscard]] LIGHTWEIGHT_API std::vector<std::pair<int64_t, int64_t>> PlanPrimaryKeyWindows(int64_t pkMin,
                                                                                             int64_t pkMax,
                                                                                             int64_t rowsPerChunk);

/// Upper bound on parallel windows a single table is split into at plan time. Caps plan-time
/// memory for sparse key spaces (e.g. snowflake-style int64 IDs) where span/rowsPerChunk would
/// explode; sparse tables get proportionally wider windows instead.
constexpr int64_t MaxWindowsPerTable = 1024;

/// Computes the per-window key width for splitting [pkMin, pkMax] into at most
/// @p maxWindowsPerTable windows of at least @p rowsPerChunk keys each:
/// windowCount = clamp(ceil(span / rowsPerChunk), 1, maxWindowsPerTable); width = ceil(span / windowCount).
/// Wrap-safe over the full int64 range (span arithmetic is done in uint64).
///
/// @param pkMin Inclusive lower bound of the key range.
/// @param pkMax Inclusive upper bound of the key range.
/// @param rowsPerChunk Target keys per window (clamped to at least 1).
/// @param maxWindowsPerTable Maximum number of windows (clamped to at least 1).
/// @return The window width in keys; @p rowsPerChunk if pkMin > pkMax.
[[nodiscard]] LIGHTWEIGHT_API int64_t CappedWindowWidth(int64_t pkMin,
                                                        int64_t pkMax,
                                                        int64_t rowsPerChunk,
                                                        int64_t maxWindowsPerTable);

} // namespace Lightweight::SqlBackup::detail
