// SPDX-License-Identifier: Apache-2.0
#include "ChunkPlanner.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Lightweight::SqlBackup::detail
{

using namespace SqlColumnTypeDefinitions;

namespace
{
    /// True if @p T is the same type as any of the listed @p Candidates. Centralizes the repeated
    /// "is this column type one of {…}" checks the per-column std::visit lambdas below need (there
    /// is no std::is_any_of<T, U...> in the standard library).
    template <typename T, typename... Candidates>
    constexpr bool IsAnyOf = (std::is_same_v<T, Candidates> || ...);
} // namespace

bool TableHasLobColumn(SqlSchema::Table const& table)
{
    // LOB = unbounded variable-length. The MSSQL batched reader represents varchar(max)/text as
    // Varchar with the driver's max COLUMN_SIZE sentinel; treat very large declared sizes as LOB,
    // and binary/varbinary/text variants as LOB regardless of size (they use SqlDynamicBinary today).
    constexpr size_t LobSizeThreshold = 1U << 20; // 1 MB: anything wider is treated as a LOB
    auto const isLob = [](SqlSchema::Column const& column) {
        return std::visit(
            [](auto const& t) -> bool {
                using T = std::decay_t<decltype(t)>;
                if constexpr (IsAnyOf<T, Binary, VarBinary, Text>)
                    return true;
                else if constexpr (IsAnyOf<T, Varchar, NVarchar>)
                    return t.size == 0 || t.size >= LobSizeThreshold;
                else
                    return false;
            },
            column.type);
    };
    return std::ranges::any_of(table.columns, isLob);
}

bool TableIsArrayFetchable(SqlSchema::Table const& table, SqlServerType serverType)
{
    // A table is array-fetchable only if EVERY column is in the "safe" set whose RowArrayCursor
    // representation is byte-identical to the trusted single-row decode path. LOB columns can't be
    // array-bound at all; temporal/guid columns CAN be array-bound but their representation work is
    // still pending, so they are excluded here and routed through the single-row fallback.
    if (table.columns.empty())
        return false;

    // TIME is admitted only where the single-row path already reads it as driver text (SQL_C_CHAR,
    // preserving fractional seconds): PostgreSQL and MSSQL. SQLite's single-row path decodes the
    // native SqlTime struct instead, so the text form would not match — Time stays single-row there.
    bool const timeIsText = serverType == SqlServerType::POSTGRESQL || serverType == SqlServerType::MICROSOFT_SQL;
    // GUID is admitted only where the driver reports SQL_GUID and the single-row path reads raw
    // SQL_C_GUID bytes (MSSQL uniqueidentifier, PostgreSQL uuid). SQLite stores GUIDs as text and
    // its single-row path goes through TryParse — it stays single-row there.
    bool const guidIsNative = serverType == SqlServerType::POSTGRESQL || serverType == SqlServerType::MICROSOFT_SQL;

    auto const isSafe = [&](SqlSchema::Column const& column) {
        return std::visit(
            [&](auto const& t) -> bool {
                using T = std::decay_t<decltype(t)>;
                // Integer family, Real, Bool -> bound as int64/double; Varchar/Char/Decimal ->
                // bound as text matching the single-row path's string decode; NVarchar/NChar (P6)
                // -> bound as SQL_C_WCHAR and converted through the same ToUtf8 as the single-row
                // u16string read; Date/DateTime/Timestamp (P6) -> native SQL_DATE_STRUCT /
                // SQL_TIMESTAMP_STRUCT array binds formatted via the same std::format as the
                // single-row read. Text is intentionally NOT here: TableHasLobColumn classifies
                // every Text column as a LOB (it uses SqlDynamicBinary today), so a Text column
                // always falls through to the LOB guard below regardless — keeping it out of this
                // set avoids implying otherwise.
                if constexpr (std::is_same_v<T, Time>)
                    return timeIsText;
                else if constexpr (std::is_same_v<T, Guid>)
                    return guidIsNative;
                else
                    return IsAnyOf<T,
                                   Integer,
                                   Bigint,
                                   Smallint,
                                   Tinyint,
                                   Real,
                                   Bool,
                                   Varchar,
                                   Char,
                                   Decimal,
                                   NVarchar,
                                   NChar,
                                   Date,
                                   DateTime,
                                   Timestamp>;
            },
            column.type);
    };

    // Every column must be in the "safe" set AND the table must have no LOB column (e.g.
    // varchar(max)/text sentinel sizes), since LOBs cannot be fixed-stride array-bound.
    return std::ranges::all_of(table.columns, isSafe) && !TableHasLobColumn(table);
}

std::optional<std::string> SingleNumericPrimaryKey(SqlSchema::Table const& table)
{
    if (table.primaryKeys.size() != 1)
        return std::nullopt;
    auto const& pkName = table.primaryKeys.front();
    auto const isNumericPrimaryKey = [&](SqlSchema::Column const& column) {
        return column.name == pkName
               && std::visit(
                   [](auto const& t) -> bool {
                       using T = std::decay_t<decltype(t)>;
                       return IsAnyOf<T, Integer, Bigint, Smallint, Tinyint>;
                   },
                   column.type);
    };
    if (std::ranges::any_of(table.columns, isNumericPrimaryKey))
        return std::optional<std::string> { pkName };
    return std::nullopt;
}

std::vector<std::pair<int64_t, int64_t>> PlanPrimaryKeyWindows(int64_t pkMin, int64_t pkMax, int64_t rowsPerChunk)
{
    auto windows = std::vector<std::pair<int64_t, int64_t>> {};
    if (pkMin > pkMax)
        return windows; // Empty range: no windows.

    // Defensive: never a zero-width (or negative) window.
    rowsPerChunk = std::max<int64_t>(rowsPerChunk, 1);

    int64_t lo = pkMin;
    while (true)
    {
        // Closed window [lo, hi]. Clamp the upper bound to pkMax (also avoids int64 overflow when
        // lo + rowsPerChunk - 1 would exceed the type maximum).
        int64_t const hi = lo > pkMax - (rowsPerChunk - 1) ? pkMax : lo + (rowsPerChunk - 1);
        windows.emplace_back(lo, hi);
        if (hi >= pkMax)
            break; // Last window reached the maximum key.
        lo = hi + 1;
    }
    return windows;
}

int64_t CappedWindowWidth(int64_t pkMin, int64_t pkMax, int64_t rowsPerChunk, int64_t maxWindowsPerTable)
{
    rowsPerChunk = std::max<int64_t>(rowsPerChunk, 1);
    maxWindowsPerTable = std::max<int64_t>(maxWindowsPerTable, 1);
    if (pkMin > pkMax)
        return rowsPerChunk;

    // spanMinus1 = pkMax - pkMin, wrap-safe in uint64 even for the full int64 range.
    auto const spanMinus1 = static_cast<uint64_t>(pkMax) - static_cast<uint64_t>(pkMin);
    // ceil(span / rowsPerChunk) without computing span (which could be 2^64): spanMinus1/rpc + 1.
    // The min-clamp runs BEFORE the +1 so the count cannot wrap to 0 when spanMinus1/rpc is
    // UINT64_MAX (full int64 span at rowsPerChunk 1) — a wrapped 0 would divide by zero below.
    auto const windowCount =
        std::min(spanMinus1 / static_cast<uint64_t>(rowsPerChunk), static_cast<uint64_t>(maxWindowsPerTable) - 1) + 1;
    // width = ceil(span / windowCount), same trick.
    auto const width = (spanMinus1 / windowCount) + 1;
    return static_cast<int64_t>(std::min(width, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
}

ChunkPlan PlanChunks(std::vector<SqlSchema::Table> const& tables,
                     size_t rowsPerChunk,
                     PkBoundsFunction const& pkBounds,
                     SqlServerType serverType)
{
    auto plan = ChunkPlan {};
    if (rowsPerChunk == 0)
        rowsPerChunk = 1; // defensive: never a zero-width window
    for (auto const& table: tables)
    {
        // A table with a single numeric primary key is split at plan time: MIN/MAX once (via the
        // injected bounds query), then one PrimaryKeyRange chunk per capped window so multiple
        // workers can process the table concurrently. Every other table gets the OFFSET seed:
        // offset 0, and the worker discovers the end of the table itself.
        // Per-table classification, computed once and shared by every window of the table.
        bool const hasLob = TableHasLobColumn(table);
        bool const arrayFetchable = TableIsArrayFetchable(table, serverType);

        if (auto const pkColumn = SingleNumericPrimaryKey(table))
        {
            auto const bounds = pkBounds(table, *pkColumn);
            if (!bounds)
            {
                plan.emptyTables.push_back(&table); // no rows: no chunks; caller reports Finished(0)
                continue;
            }
            auto const [pkMin, pkMax] = *bounds;
            auto const width = CappedWindowWidth(pkMin, pkMax, static_cast<int64_t>(rowsPerChunk), MaxWindowsPerTable);
            auto const windows = PlanPrimaryKeyWindows(pkMin, pkMax, width);
            auto& state = plan.tableStates.emplace_back();
            state.remainingChunks.store(windows.size());
            // Wrap-safe span estimate (pkMax - pkMin in signed int64 is UB for the full range),
            // saturating at SIZE_MAX for the degenerate full-span case.
            auto const estimateMinus1 = static_cast<uint64_t>(pkMax) - static_cast<uint64_t>(pkMin);
            state.totalRows.store(estimateMinus1 == std::numeric_limits<uint64_t>::max()
                                      ? std::numeric_limits<size_t>::max()
                                      : static_cast<size_t>(estimateMinus1) + 1);
#if !defined(__cpp_lib_ranges_enumerate)
            int64_t windowIndex { -1 };
            for (auto const& window: windows)
            {
                ++windowIndex;
#else
            for (auto const& [windowIndex, window]: windows | std::views::enumerate)
            {
#endif
                auto const& [lo, hi] = window;
                plan.chunks.emplace_back(Chunk {
                    .table = &table,
                    .strategy = ChunkStrategy::PrimaryKeyRange,
                    .windowIndex = static_cast<uint32_t>(windowIndex),
                    .offset = 0,
                    .lo = lo,
                    .hi = hi,
                    .pkColumn = *pkColumn,
                    .hasLob = hasLob,
                    .arrayFetchable = arrayFetchable,
                    .state = &state,
                });
            }
        }
        else
        {
            auto& state = plan.tableStates.emplace_back();
            state.remainingChunks.store(1);
            plan.chunks.emplace_back(Chunk {
                .table = &table,
                .strategy = ChunkStrategy::Offset,
                .windowIndex = 0,
                .offset = 0,
                .lo = 0,
                .hi = 0,
                .pkColumn = {},
                .hasLob = hasLob,
                .arrayFetchable = arrayFetchable,
                .state = &state,
            });
        }
    }
    return plan;
}

} // namespace Lightweight::SqlBackup::detail
