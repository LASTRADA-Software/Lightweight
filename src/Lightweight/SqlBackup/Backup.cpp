// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/SqlRawColumn.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlQuery.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "../TracyProfiler.hpp"
#include "Backup.hpp"
#include "Common.hpp"
#include "MsgPackChunkFormats.hpp"
#include "Sha256.hpp"
#include "SqlBackupFormats.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <print>
#include <ranges>
#include <stdexcept>
#include <variant>
#include <vector>

// #define DEBUG_BACKUP_WORKER

#ifdef DEBUG_BACKUP_WORKER
    #include <iostream>
#endif

/// Compile-time constant for debug output. Enable by defining DEBUG_BACKUP_WORKER.
constexpr bool DebugBackupWorker =
#ifdef DEBUG_BACKUP_WORKER
    true;
#else
    false;
#endif

using namespace std::string_literals;

namespace Lightweight::SqlBackup::detail
{

/// Appends the SELECT column projection list (no leading "SELECT", no trailing space) to @p sql.
///
/// Decimal columns on MS SQL Server are wrapped in CONVERT(VARCHAR(precision+3), col) because the
/// ODBC driver loses precision reading DECIMAL as string directly. Identifier quoting follows
/// @p mssqlDecimalConvert: the MSSQL-decimal path uses bracket quoting ([col]) and applies the
/// CONVERT wrapper, while the cross-DBMS path uses ANSI double quotes ("col") and no CONVERT. Both
/// callers feed this single helper so the projection bytes (column quoting, comma handling, and the
/// precision + 3 VARCHAR-size constant) never drift between them.
///
/// @param sql The SQL buffer to append the projection list to.
/// @param columns The column definitions.
/// @param mssqlDecimalConvert When true, use bracket quoting and the DECIMAL CONVERT workaround;
///                            when false, use ANSI double-quote quoting and emit columns verbatim.
void AppendColumnProjection(std::string& sql, std::vector<SqlSchema::Column> const& columns, bool mssqlDecimalConvert)
{
    using namespace SqlColumnTypeDefinitions;

    // Identifier delimiters: brackets for the MSSQL DECIMAL path, ANSI quotes elsewhere.
    auto const quoteId = [mssqlDecimalConvert](std::string_view id) {
        return mssqlDecimalConvert ? std::format("[{}]", id) : std::format(R"("{}")", id);
    };

    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i > 0)
            std::format_to(std::back_inserter(sql), ", ");

        auto const& col = columns[i];
        if (mssqlDecimalConvert && std::holds_alternative<Decimal>(col.type))
        {
            // MSSQL ODBC driver loses precision when reading DECIMAL as string directly.
            // Using CONVERT(VARCHAR, ...) forces SQL Server to convert with full precision.
            // The inner CAST normalizes the textual form: a no-op for true DECIMAL columns, but
            // MONEY/SMALLMONEY (introspected as DECIMAL(19,4)) would otherwise CONVERT with only
            // 2 decimal places, making the backed-up text differ from what a restored DECIMAL
            // column produces.
            auto const& dec = std::get<Decimal>(col.type);
            // VARCHAR size should accommodate precision + scale + decimal point + sign
            auto const varcharSize = dec.precision + 3;
            std::format_to(std::back_inserter(sql),
                           "CONVERT(VARCHAR({}), CAST({} AS DECIMAL({}, {})))",
                           varcharSize,
                           quoteId(col.name),
                           dec.precision,
                           dec.scale);
        }
        else
        {
            std::format_to(std::back_inserter(sql), "{}", quoteId(col.name));
        }
    }
}

/// Builds a SELECT query for MSSQL with CONVERT for DECIMAL columns.
///
/// MSSQL ODBC driver loses precision when reading DECIMAL as string directly.
/// Using CONVERT(VARCHAR, ...) forces SQL Server to convert with full precision.
///
/// @param schema The schema name (may be empty).
/// @param tableName The table name.
/// @param columns The column definitions.
/// @param primaryKeys The primary key column names for ORDER BY.
/// @param offset The row offset for pagination.
/// @return The SQL query string.
std::string BuildMssqlDecimalSelectQuery(std::string_view schema,
                                         std::string const& tableName,
                                         std::vector<SqlSchema::Column> const& columns,
                                         std::vector<std::string> const& primaryKeys,
                                         size_t offset)
{
    using namespace SqlColumnTypeDefinitions;

    std::string sql;
    sql.reserve(256);
    std::format_to(std::back_inserter(sql), "SELECT ");

    // This builder is only ever used on the MSSQL DECIMAL path, so always use bracket quoting and
    // the CONVERT workaround.
    AppendColumnProjection(sql, columns, /*mssqlDecimalConvert=*/true);

    if (schema.empty())
        std::format_to(std::back_inserter(sql), " FROM [{}]", tableName);
    else
        std::format_to(std::back_inserter(sql), " FROM [{}].[{}]", schema, tableName);

    // ORDER BY is required for OFFSET
    std::format_to(std::back_inserter(sql), " ORDER BY ");
    if (!primaryKeys.empty())
    {
        for (size_t i = 0; i < primaryKeys.size(); ++i)
        {
            if (i > 0)
                std::format_to(std::back_inserter(sql), ", ");
            std::format_to(std::back_inserter(sql), "[{}] ASC", primaryKeys[i]);
        }
    }
    else
    {
        std::format_to(std::back_inserter(sql), "[{}] ASC", columns[0].name);
    }

    if (offset > 0)
        std::format_to(std::back_inserter(sql), " OFFSET {} ROWS", offset);

    return sql;
}

/// Builds a SELECT query restricted to a primary-key window: WHERE pk >= lo AND pk <= hi, ordered by pk.
///
/// Mirrors the projection logic of BuildSelectQueryWithOffset (including the MSSQL DECIMAL CONVERT
/// workaround) but expresses the row window as a closed primary-key range rather than OFFSET/FETCH.
/// A closed (inclusive) range is used so the final window can be clamped exactly to MAX(pk) without
/// an off-by-one or an unrepresentable exclusive upper bound at INT64_MAX. Identifiers are quoted with
/// ANSI double quotes (matching FormatTableName) for the cross-DBMS path and with MSSQL bracket quoting
/// on the DECIMAL-CONVERT path (matching BuildMssqlDecimalSelectQuery). The bounds are plain integers,
/// so no value escaping is required.
///
/// @param serverType The server type (selects the MSSQL DECIMAL workaround).
/// @param schema The schema name (may be empty).
/// @param tableName The table name.
/// @param columns The column definitions.
/// @param pkColumn The single numeric primary-key column name.
/// @param lo Inclusive lower bound of the window.
/// @param hi Inclusive upper bound of the window.
/// @return The SQL query string.
std::string BuildSelectQueryWithPkRange(SqlServerType serverType,
                                        std::string_view schema,
                                        std::string const& tableName,
                                        std::vector<SqlSchema::Column> const& columns,
                                        std::string const& pkColumn,
                                        int64_t lo,
                                        int64_t hi)
{
    using namespace SqlColumnTypeDefinitions;

    bool const needsMssqlDecimalConvert =
        serverType == SqlServerType::MICROSOFT_SQL
        && std::ranges::any_of(columns, [](auto const& c) { return std::holds_alternative<Decimal>(c.type); });

    // Identifier delimiters: brackets for the MSSQL DECIMAL path, ANSI quotes elsewhere.
    auto const quoteId = [needsMssqlDecimalConvert](std::string_view id) {
        return needsMssqlDecimalConvert ? std::format("[{}]", id) : std::format(R"("{}")", id);
    };

    std::string sql;
    sql.reserve(256);
    std::format_to(std::back_inserter(sql), "SELECT ");

    AppendColumnProjection(sql, columns, needsMssqlDecimalConvert);

    std::string formattedTable;
    if (schema.empty())
        formattedTable = quoteId(tableName);
    else
        formattedTable = std::format("{}.{}", quoteId(schema), quoteId(tableName));

    std::format_to(std::back_inserter(sql),
                   " FROM {0} WHERE {1} >= {2} AND {1} <= {3} ORDER BY {1} ASC",
                   formattedTable,
                   quoteId(pkColumn),
                   lo,
                   hi);

    return sql;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string BuildSelectQueryWithOffset(SqlQueryFormatter const& formatter,
                                       SqlServerType serverType,
                                       std::string_view schema,
                                       std::string const& tableName,
                                       std::vector<SqlSchema::Column> const& columns,
                                       std::vector<std::string> const& primaryKeys,
                                       size_t offset)
{
    using namespace SqlColumnTypeDefinitions;

    // Check if we need special handling for MSSQL DECIMAL columns
    bool const needsMssqlDecimalConvert =
        serverType == SqlServerType::MICROSOFT_SQL
        && std::ranges::any_of(columns, [](auto const& c) { return std::holds_alternative<Decimal>(c.type); });

    if (needsMssqlDecimalConvert)
        return BuildMssqlDecimalSelectQuery(schema, tableName, columns, primaryKeys, offset);

    // Use Query Builder with schema support. The starter returned by Select() does
    // not expose All/First/Range/OrderBy until at least one column is projected;
    // capture the first Field()'s return (a SqlSelectQueryBuilder& aliasing the
    // starter's storage) and continue from there.
    auto starter = schema.empty() ? SqlQueryBuilder(formatter).FromTable(tableName).Select()
                                  : SqlQueryBuilder(formatter).FromSchemaTable(schema, tableName).Select();

    auto& query = starter.Field(columns[0].name);
    for (size_t i = 1; i < columns.size(); ++i)
        query.Field(columns[i].name);

    // ORDER BY is required for:
    // 1. MS SQL Server when using OFFSET
    // 2. Deterministic results for resumption
    if (!primaryKeys.empty())
    {
        for (auto const& pk: primaryKeys)
            query.OrderBy(pk, SqlResultOrdering::ASCENDING);
    }
    else
    {
        // Fallback: order by first column (less reliable but better than no order)
        query.OrderBy(columns[0].name, SqlResultOrdering::ASCENDING);
    }

    if (offset > 0)
        return query.Range(offset, (std::numeric_limits<size_t>::max)()).ToSql();
    else
        return query.All().ToSql();
}

/// Builds the zip entry name of a window sub-chunk: data/<table>/<window+1>_<subChunk>.msgpack.
/// Dots in the table name are replaced with underscores so the name keeps exactly three path
/// segments. Shared by the writing path (flushChunk) and the stale-entry cleanup so the two can
/// never disagree on naming.
static std::string BackupChunkEntryName(std::string_view tableName, uint32_t windowIndex, size_t subChunkId)
{
    std::string sName { tableName };
    std::ranges::replace(sName, '.', '_');
    return std::format("data/{}/{:04}_{:02}.msgpack", sName, windowIndex + 1, subChunkId);
}

void DeleteStaleSubChunks(WorkerChunkArchive& archive,
                          BackupContext& ctx,
                          std::string const& tableName,
                          uint32_t windowIndex,
                          size_t firstStale,
                          size_t end)
{
    for (auto const subId: std::views::iota(firstStale, end))
    {
        auto const entryName = BackupChunkEntryName(tableName, windowIndex, subId);
        archive.Remove(entryName);
        if (ctx.checksums && ctx.checksumMutex)
        {
            auto const checksumLock = std::scoped_lock(*ctx.checksumMutex);
            ctx.checksums->erase(entryName);
        }
    }
}

std::optional<std::pair<int64_t, int64_t>> QueryPkBounds(SqlStatement& stmt,
                                                         SqlSchema::Table const& table,
                                                         std::string const& pkColumn)
{
    // Derive bounds from MIN/MAX(pk) instead of a SELECT COUNT(*) on large tables — one combined
    // round-trip. The pk column is quoted the same way as the table name (ANSI double quotes),
    // which QUOTED_IDENTIFIER-on MSSQL, PostgreSQL, and SQLite all accept.
    ZoneScopedN("Backup::PkBounds");
    auto const formattedTableName = FormatTableName(table.schema, table.name);
    auto const quotedPk = std::format(R"("{}")", pkColumn);
    auto cursor = stmt.ExecuteDirect(std::format("SELECT MIN({0}), MAX({0}) FROM {1}", quotedPk, formattedTableName));
    if (!cursor.FetchRow())
        return std::nullopt;
    auto const minOpt = cursor.GetNullableColumn<int64_t>(1);
    auto const maxOpt = cursor.GetNullableColumn<int64_t>(2);
    if (!minOpt.has_value() || !maxOpt.has_value())
        return std::nullopt; // empty table: MIN/MAX are SQL NULL
    return std::pair { *minOpt, *maxOpt };
}

// Converts a UTF-16 column value to UTF-8 and aliases the result as the opaque std::string byte
// container that BackupValue uses for text on the wire. ToUtf8 yields a proper std::u8string (the
// UTF-8 *value* type); the std::string here is deliberately just those bytes for serialization.
[[nodiscard]] std::string Utf16ToBackupUtf8(std::u16string_view value)
{
    auto const utf8 = ToUtf8(value);
    return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
}

// Decodes one column cell of the current row from the single-row (SqlResultCursor) path into the
// BackupValue variant. Extracted from ProcessChunkBackup's per-row lambda so that decode ladder is
// not inlined into the lambda (keeping the lambda's cognitive complexity bounded). The branch order
// and per-type read strategy are byte-identical to the original inline ladder.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
BackupValue DecodeSingleRowColumn(SqlResultCursor& cursor,
                                  SqlSchema::Column const& colDef,
                                  SQLUSMALLINT columnIndex,
                                  SqlServerType serverType)
{
    using namespace SqlColumnTypeDefinitions;
    auto const i = columnIndex;

    auto const orNull = [&](auto&& valueOpt, auto&& transform) -> BackupValue {
        if (valueOpt)
            return transform(*valueOpt);
        return std::monostate {};
    };

    if (std::holds_alternative<Binary>(colDef.type) || std::holds_alternative<VarBinary>(colDef.type))
    {
        auto valOpt = cursor.GetNullableColumn<SqlDynamicBinary<MaxBinaryLobBufferSize>>(i);
        return orNull(valOpt,
                      [](auto const& v) -> BackupValue { return std::vector<uint8_t>(v.data(), v.data() + v.size()); });
    }
    if (std::holds_alternative<Bool>(colDef.type))
        return orNull(cursor.GetNullableColumn<bool>(i), [](bool v) -> BackupValue { return v; });
    if (std::holds_alternative<Integer>(colDef.type) || std::holds_alternative<Bigint>(colDef.type)
        || std::holds_alternative<Smallint>(colDef.type) || std::holds_alternative<Tinyint>(colDef.type))
        return orNull(cursor.GetNullableColumn<int64_t>(i), [](int64_t v) -> BackupValue { return v; });
    if (std::holds_alternative<Real>(colDef.type))
        return orNull(cursor.GetNullableColumn<double>(i), [](double v) -> BackupValue { return v; });
    if (std::holds_alternative<NVarchar>(colDef.type) || std::holds_alternative<NChar>(colDef.type))
        return orNull(cursor.GetNullableColumn<std::u16string>(i),
                      [](auto const& v) -> BackupValue { return Utf16ToBackupUtf8(v); });
    if (std::holds_alternative<Guid>(colDef.type))
        return orNull(cursor.GetNullableColumn<SqlGuid>(i), [](auto const& v) -> BackupValue { return to_string(v); });
    if (std::holds_alternative<Decimal>(colDef.type))
        // Read Decimal as string to preserve full precision (double would lose digits beyond ~15-17).
        return orNull(cursor.GetNullableColumn<std::string>(i), [](auto const& v) -> BackupValue { return v; });
    if (std::holds_alternative<Date>(colDef.type))
        return orNull(cursor.GetNullableColumn<SqlDate>(i),
                      [](auto const& v) -> BackupValue { return std::format("{}", v); });
    if (std::holds_alternative<Time>(colDef.type))
    {
        // PG/MSSQL: read TIME as driver text to keep fractional seconds (SQL_C_TYPE_TIME loses them);
        // other DBs (e.g. SQLite) use the native SqlTime struct.
        if (serverType == SqlServerType::POSTGRESQL || serverType == SqlServerType::MICROSOFT_SQL)
            return orNull(cursor.GetNullableColumn<std::string>(i), [](auto const& v) -> BackupValue { return v; });
        return orNull(cursor.GetNullableColumn<SqlTime>(i),
                      [](auto const& v) -> BackupValue { return std::format("{}", v); });
    }
    if (std::holds_alternative<DateTime>(colDef.type) || std::holds_alternative<Timestamp>(colDef.type))
        // Native SqlDateTime read avoids MSSQL SQL_TYPE_TIMESTAMP -> SQL_C_CHAR conversion error 22003.
        return orNull(cursor.GetNullableColumn<SqlDateTime>(i),
                      [](auto const& v) -> BackupValue { return std::format("{}", v); });
    if (std::holds_alternative<Varchar>(colDef.type) || std::holds_alternative<Char>(colDef.type)
        || std::holds_alternative<Text>(colDef.type))
    {
        // psqlODBC narrows SQL_C_CHAR to the client codepage (losing non-ASCII); read PG text through
        // the wide (UTF-16 -> UTF-8) path, the same as the NVarchar branch. Other DBs read narrow.
        if (serverType == SqlServerType::POSTGRESQL)
            return orNull(cursor.GetNullableColumn<std::u16string>(i),
                          [](auto const& v) -> BackupValue { return Utf16ToBackupUtf8(v); });
        return orNull(cursor.GetNullableColumn<std::string>(i), [](auto const& v) -> BackupValue { return v; });
    }
    // Fallback for any unhandled type: read as string.
    return orNull(cursor.GetNullableColumn<std::string>(i), [](auto const& v) -> BackupValue { return v; });
}

// Decodes one column cell of row @p rowInBatch from the bulk RowArrayCursor path into BackupValue.
// Extracted from ProcessChunkBackup's batched lambda for the same complexity reason; the alternatives
// produced are byte-identical to the single-row path (see TableIsArrayFetchable's safe set).
BackupValue DecodeBatchedColumn(RowArrayCursor& cursor,
                                SqlSchema::Column const& colDef,
                                std::size_t rowInBatch,
                                SQLUSMALLINT columnIndex)
{
    using namespace SqlColumnTypeDefinitions;
    auto const r = rowInBatch;
    auto const i = columnIndex;

    auto const orNull = [&](auto&& valueOpt, auto&& transform) -> BackupValue {
        if (valueOpt)
            return transform(*valueOpt);
        return std::monostate {};
    };

    if (std::holds_alternative<Bool>(colDef.type))
        // The array cursor binds Bool as int64 (0/1); cast to bool so the variant alternative (and
        // thus the serialized bytes) matches the single-row path.
        return orNull(cursor.GetI64(r, i), [](int64_t v) -> BackupValue { return static_cast<bool>(v); });
    if (std::holds_alternative<Integer>(colDef.type) || std::holds_alternative<Bigint>(colDef.type)
        || std::holds_alternative<Smallint>(colDef.type) || std::holds_alternative<Tinyint>(colDef.type))
        return orNull(cursor.GetI64(r, i), [](int64_t v) -> BackupValue { return v; });
    if (std::holds_alternative<Real>(colDef.type))
        return orNull(cursor.GetF64(r, i), [](double v) -> BackupValue { return v; });
    if (std::holds_alternative<Varchar>(colDef.type) || std::holds_alternative<Char>(colDef.type)
        || std::holds_alternative<Decimal>(colDef.type) || std::holds_alternative<NVarchar>(colDef.type)
        || std::holds_alternative<NChar>(colDef.type) || std::holds_alternative<Time>(colDef.type))
        // Text-family cells: GetString returns UTF-8 regardless of how the driver bound the column,
        // matching the single-row path's std::string / (u16string -> ToUtf8) decode. Time reaches
        // here only on PG/MSSQL (gated by TableIsArrayFetchable), where TIME is read as driver text.
        return orNull(cursor.GetString(r, i), [](auto& v) -> BackupValue { return std::move(v); });
    if (std::holds_alternative<Date>(colDef.type))
        // Native SQL_DATE_STRUCT array bind; same std::format as the single-row SqlDate read.
        return orNull(cursor.GetDate(r, i), [](auto const& v) -> BackupValue { return std::format("{}", v); });
    if (std::holds_alternative<DateTime>(colDef.type) || std::holds_alternative<Timestamp>(colDef.type))
        // Native SQL_TIMESTAMP_STRUCT array bind; same std::format as the single-row SqlDateTime read.
        return orNull(cursor.GetTimestamp(r, i), [](auto const& v) -> BackupValue { return std::format("{}", v); });
    if (std::holds_alternative<Guid>(colDef.type))
        // Raw SQL_C_GUID array bind (MSSQL/PG only); same to_string as the single-row SqlGuid read.
        return orNull(cursor.GetGuid(r, i), [](auto const& v) -> BackupValue { return to_string(v); });

    // Unreachable: TableIsArrayFetchable admits only the safe set. Reaching here means the safe-set
    // and this decode ladder drifted.
    throw std::logic_error(std::format("DecodeBatchedColumn: column '{}' has a type not in the "
                                       "array-fetch safe set; TableIsArrayFetchable / decode ladder drift",
                                       colDef.name));
}

// Decodes a whole fetched row from the single-row cursor into @p row (cleared first), wrapping each
// column decode with the debug-trace + rethrow the inline loop had. Extracted from processQuery so
// that lambda's cognitive complexity stays under the clang-tidy threshold.
void DecodeRowSingle(SqlResultCursor& cursor,
                     SqlSchema::Table const& table,
                     SqlServerType serverType,
                     std::string_view tableName,
                     std::vector<BackupValue>& row)
{
    ZoneScopedN("Backup::DecodeRow");
    row.clear();
    auto const cols = static_cast<SQLUSMALLINT>(table.columns.size());
    for (SQLUSMALLINT i = 1; i <= cols; ++i)
    {
        auto const& colDef = table.columns[i - 1];
        try
        {
            if constexpr (DebugBackupWorker)
                std::println(stderr, "DEBUG: Processing col {} ({}) type index: {}", i, colDef.name, colDef.type.index());
            row.emplace_back(DecodeSingleRowColumn(cursor, colDef, i, serverType));
        }
        catch ([[maybe_unused]] std::exception const& e)
        {
            if constexpr (DebugBackupWorker)
                std::println(stderr,
                             "DEBUG: Exception processing row for table {} col {} ({}): {}",
                             tableName,
                             i,
                             colDef.name,
                             e.what());
            throw;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ProcessChunkBackup(BackupContext& ctx, SqlConnection& conn, detail::Chunk const& chunk, WorkerChunkArchive& archive)
{
    auto const& table = *chunk.table;
    ZoneScopedN("SqlBackup::ProcessChunkBackup");
    ZoneTextObject(table.name);
    std::string const& tableName = table.name;
    auto& state = *chunk.state;

    if (table.columns.empty())
    {
        state.failed.store(true);
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "No columns found for table "s + tableName });
        return;
    }

    SqlStatement stmt { conn };
    auto const& formatter = conn.QueryFormatter();

    auto const formattedTableName = FormatTableName(table.schema, tableName);
    bool const usePkRange = chunk.strategy == detail::ChunkStrategy::PrimaryKeyRange && !chunk.pkColumn.empty();

    if (!usePkRange)
    {
        // OFFSET path: exact total via COUNT(*), discovered by the (single) worker of this table.
        // PK-range totals were estimated (key span) and reported at plan time.
        ZoneScopedN("Backup::CountRows");
        auto const counted = static_cast<size_t>(
            stmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", formattedTableName)).value_or(0));
        state.totalRows.store(counted);
        ctx.progress.AddTotalItems(counted);
    }
    size_t const totalRows = state.totalRows.load();

    // First chunk of the table to start fires the Started event (chunks run on many workers).
    if (!state.started.exchange(true))
        ctx.progress.Update({ .state = Progress::State::Started,
                              .tableName = tableName,
                              .currentRows = 0,
                              .totalRows = totalRows,
                              .message = "Started backup"s });

    if constexpr (DebugBackupWorker)
    {
        std::println(stderr, "DEBUG: ProcessChunkBackup: Starting backup for {} ({} rows)", tableName, totalRows);
        std::println(stderr, "DEBUG: Creating writer...");
    }
    auto writer = CreateMsgPackChunkWriter(ctx.backupSettings.chunkSizeBytes);
    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Writer created.");

    size_t subChunkId = 0;
    // Rows read by THIS chunk in the current attempt; for the OFFSET path this doubles as the
    // resume cursor (seeded from the chunk's offset). Reset to 0 on a PK-range window retry.
    size_t processedRows = chunk.offset;
    // Progress high-water mark across attempts: rows are only reported to the progress manager
    // (and the shared per-table counter) beyond this, so a retried window never double-counts.
    size_t reportedRows = 0;
    unsigned retryCount = 0;

    size_t rowsAtLastFlush = 0;
    // Highest sub-chunk count any attempt of this PK-range window flushed; used to delete stale
    // sub-chunk files if a retry produces fewer (live data drift).
    size_t maxSubChunksFlushed = 0;

    auto flushChunk = [&]() {
        ZoneScopedN("Backup::FlushChunk");
        std::string data = writer->Flush();
        if (data.empty())
            return;

        // Throughput plots (emitted only at flush boundaries, negligible cost).
        TracyPlot("Backup.ChunkBytes", static_cast<int64_t>(data.size()));

        std::string const entryName = BackupChunkEntryName(table.name, chunk.windowIndex, subChunkId);

        // Checksum semantics unchanged: SHA-256 of the UNCOMPRESSED chunk bytes (restore verifies
        // after libzip's transparent decompression).
        std::string checksum;
        {
            ZoneScopedN("Backup::Sha256");
            checksum = Sha256::Hash(data);
        }
        if (ctx.checksums && ctx.checksumMutex)
        {
            auto const checksumLock = std::scoped_lock(*ctx.checksumMutex);
            (*ctx.checksums)[entryName] = checksum;
        }

        // Hand the chunk to the worker's private archive: compression happens in THIS worker
        // thread when the archive's rotation fills (overlapped with the network-bound fetch), and
        // the finalize phase raw-merges the precompressed entries into the final zip. No shared
        // zip lock on the data path anymore.
        archive.Add(entryName, data);

        TracyPlot("Backup.RowsPerChunk", static_cast<int64_t>(processedRows - rowsAtLastFlush));
        rowsAtLastFlush = processedRows;
        writer->Clear();
        subChunkId++;
    };

    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Reserving row...");
    std::vector<BackupValue> row;
    row.reserve(table.columns.size());
    if constexpr (DebugBackupWorker)
        std::println(stderr, "DEBUG: Row reserved. Entering FetchRow loop...");

    auto const columnCount = table.columns.size();

    // Shared fetch-decode-write body for both strategies. The ONLY difference between the OFFSET and
    // PK-range paths is the query string passed here; this loop (cursor + the per-column decode switch +
    // WriteRow + flush) is identical, so the decode ladder is not duplicated. The OFFSET path calls this
    // once per retry attempt; the PK-range path calls it once per primary-key window.
    auto processQuery = [&](std::string const& selectQuery) {
        auto cursor = [&] {
            ZoneScopedN("Backup::ExecuteSelect");
            return stmt.ExecuteDirect(selectQuery);
        }();

        while (true)
        {
            bool hasRow = false;
            {
                ZoneScopedN("Backup::FetchRow");
                hasRow = cursor.FetchRow();
            }
            if (!hasRow)
                break;

            if constexpr (DebugBackupWorker)
                std::println(stderr, "DEBUG: FetchRow returned true for {}", tableName);
            DecodeRowSingle(cursor, table, conn.ServerType(), tableName, row);

            try
            {
                ZoneScopedN("Backup::WriteRow");
                writer->WriteRow(row);
            }
            catch ([[maybe_unused]] std::exception const& e)
            {
                if constexpr (DebugBackupWorker)
                    std::println(stderr, "DEBUG: Exception in WriteRow for table {}: {}", tableName, e.what());
                throw;
            }
            processedRows++;
            if (processedRows > reportedRows)
            {
                // Beyond the high-water mark: report for rate calculation and the shared
                // per-table cumulative counter (a retried window never double-counts).
                reportedRows = processedRows;
                state.processedRows.fetch_add(1, std::memory_order_relaxed);
                ctx.progress.OnItemsProcessed(1);
            }

            if (writer->IsChunkFull())
            {
                flushChunk();
                ctx.progress.Update({ .state = Progress::State::InProgress,
                                      .tableName = tableName,
                                      .currentRows = state.processedRows.load(std::memory_order_relaxed),
                                      .totalRows = totalRows,
                                      .message = "Writing chunk..."s });
            }
        }
    };

    // Bulk array-fetch fetch-decode-write body, used only for "array-fetchable" tables
    // (chunk.arrayFetchable, classified by TableIsArrayFetchable). Instead of one SQLGetData per
    // cell, ExecuteBatchFetch binds a contiguous per-column buffer and the driver materializes whole
    // blocks of rows per round-trip — this delivers the bulk of the SQLGetData reduction for the
    // simple-column tables. The per-column decode mirrors processQuery's switch but is restricted to
    // the safe type set TableIsArrayFetchable allows (integer family + Bool -> int64; Real -> double;
    // Varchar/Char/Decimal -> narrow text). The BackupValue alternative produced for each type
    // is identical to the single-row path: int64_t for the integer family, double for Real, bool for
    // Bool, std::string for the text/decimal types, and std::monostate for SQL NULL.
    //
    // Unlike processQuery, this path has NO transient-error retry: a transient error propagates and
    // fails the table. Re-running the backup is idempotent (every chunk entry is added with
    // ZIP_FL_OVERWRITE), the same simplification adopted for the PK-range single-row path in P3.
    auto processQueryBatched = [&](std::string const& selectQuery) {
        using namespace SqlColumnTypeDefinitions;

        // Array depth: rows materialized per SQLFetchScroll round-trip. 512 balances per-round-trip
        // amortization against the per-column buffer footprint (arrayDepth * MaxCharColumnBytes).
        [[maybe_unused]] constexpr std::size_t ArrayDepth = 512;
        auto const cols = static_cast<SQLUSMALLINT>(columnCount);

        auto cursor = [&] {
            ZoneScopedN("Backup::ExecuteBatchFetch");
            return stmt.ExecuteBatchFetch(selectQuery, ArrayDepth);
        }();

        while (true)
        {
            std::size_t n = 0;
            {
                ZoneScopedN("Backup::FetchArray");
                n = cursor.FetchArray();
            }
            if (n == 0)
                break;

            for (std::size_t r = 0; r < n; ++r)
            {
                row.clear();
                {
                    ZoneScopedN("Backup::DecodeRow");
                    for (SQLUSMALLINT i = 1; i <= cols; ++i)
                        row.emplace_back(DecodeBatchedColumn(cursor, table.columns[i - 1], r, i));
                } // End DecodeRow zone

                {
                    ZoneScopedN("Backup::WriteRow");
                    writer->WriteRow(row);
                }
                processedRows++;
                if (processedRows > reportedRows)
                {
                    // Beyond the high-water mark: report for rate calculation and the shared
                    // per-table cumulative counter (a retried window never double-counts).
                    reportedRows = processedRows;
                    state.processedRows.fetch_add(1, std::memory_order_relaxed);
                    ctx.progress.OnItemsProcessed(1);
                }

                if (writer->IsChunkFull())
                {
                    flushChunk();
                    ctx.progress.Update({ .state = Progress::State::InProgress,
                                          .tableName = tableName,
                                          .currentRows = state.processedRows.load(std::memory_order_relaxed),
                                          .totalRows = totalRows,
                                          .message = "Writing chunk..."s });
                }
            }
        }
    };

    // Runs one chunk's query, preferring the bulk array-fetch path for array-fetchable tables but
    // transparently falling back to the proven single-row path when the cursor cannot be built.
    // The planner classifies a table as array-fetchable from its schema, yet a driver can still
    // describe a column in a way the fixed-stride cursor cannot bind — notably SQLite, whose dynamic
    // typing makes SQLDescribeCol report some columns as unbounded LONGVARCHAR. RowArrayCursor throws
    // RowArrayCursorUnsupported from its constructor, BEFORE any row is fetched or any chunk flushed,
    // so falling back here is safe (no partial output) and keeps the backup correct rather than
    // failing the whole table on a misclassification.
    auto const runChunk = [&](std::string const& selectQuery) {
        if (chunk.arrayFetchable)
        {
            try
            {
                processQueryBatched(selectQuery);
                return;
            }
            catch (RowArrayCursorUnsupported const& e)
            {
                ctx.progress.Update(
                    { .state = Progress::State::Warning,
                      .tableName = tableName,
                      .currentRows = state.processedRows.load(),
                      .totalRows = totalRows,
                      .message = std::format("Table not bulk-fetchable ({}); using single-row path", e.what()) });
            }
        }
        processQuery(selectQuery);
    };

    if (usePkRange)
    {
        // One chunk == one plan-time window [chunk.lo, chunk.hi]; other windows of this table run
        // on other workers concurrently. Transient errors re-run the WHOLE window: filenames repeat
        // across attempts (ZIP_FL_OVERWRITE -> idempotent), `reportedRows` keeps progress from
        // double-counting, and stale sub-chunks of longer earlier attempts are deleted after the
        // final flush below. Hard errors propagate and fail the table.
        while (true)
        {
            try
            {
                std::string selectQuery;
                {
                    ZoneScopedN("Backup::BuildQuery");
                    selectQuery = BuildSelectQueryWithPkRange(
                        conn.ServerType(), table.schema, tableName, table.columns, chunk.pkColumn, chunk.lo, chunk.hi);
                }
                // Array-fetch simple-column tables; everything else (and any table the cursor can't
                // bind) keeps the proven single-row path.
                runChunk(selectQuery);
                break;
            }
            catch (SqlException const& e)
            {
                if (!IsTransientError(e.info()) || retryCount >= ctx.retrySettings.maxRetries)
                    throw;
                ++retryCount;
                maxSubChunksFlushed = std::max(maxSubChunksFlushed, subChunkId);
                writer->Clear();
                subChunkId = 0;
                processedRows = 0;
                rowsAtLastFlush = 0;
                ctx.progress.Update({ .state = Progress::State::Warning,
                                      .tableName = tableName,
                                      .currentRows = state.processedRows.load(),
                                      .totalRows = totalRows,
                                      .message = std::format("Transient error, retry {}/{}, re-reading window {}: {}",
                                                             retryCount,
                                                             ctx.retrySettings.maxRetries,
                                                             chunk.windowIndex,
                                                             e.what()) });
                conn.Close();
                std::this_thread::sleep_for(CalculateRetryDelay(retryCount - 1, ctx.retrySettings));
                if (!ConnectWithRetry(conn, ctx.connectionString, ctx.retrySettings, ctx.progress, tableName))
                {
                    state.failed.store(true);
                    ctx.progress.Update({ .state = Progress::State::Error,
                                          .tableName = tableName,
                                          .currentRows = state.processedRows.load(),
                                          .totalRows = totalRows,
                                          .message = "Failed to reconnect after transient error" });
                    return;
                }
                stmt = SqlStatement { conn };
            }
        }
    }
    else if (chunk.arrayFetchable)
    {
        // OFFSET path, array-fetchable table: single SELECT (offset 0) read in bulk. Like the
        // PK-range batched path, there is NO transient-error retry here — a transient error
        // propagates and fails the table; re-running the backup is idempotent (ZIP_FL_OVERWRITE).
        // The transient-retry/resume machinery below is reserved for the single-row path.
        std::string selectQuery;
        {
            ZoneScopedN("Backup::BuildQuery");
            selectQuery = BuildSelectQueryWithOffset(
                formatter, conn.ServerType(), table.schema, tableName, table.columns, table.primaryKeys, processedRows);
        }
        runChunk(selectQuery);
    }
    else
    {
        // OFFSET path: retry loop with offset-based resumption (unchanged behavior).
        while (retryCount <= ctx.retrySettings.maxRetries)
        {
            try
            {
                // Build query with current offset for resumption
                std::string selectQuery;
                {
                    ZoneScopedN("Backup::BuildQuery");
                    selectQuery = BuildSelectQueryWithOffset(formatter,
                                                             conn.ServerType(),
                                                             table.schema,
                                                             tableName,
                                                             table.columns,
                                                             table.primaryKeys,
                                                             processedRows);
                }
                processQuery(selectQuery);

                // Successfully completed - exit retry loop
                break;
            }
            catch (SqlException const& e)
            {
                if (!IsTransientError(e.info()) || retryCount >= ctx.retrySettings.maxRetries)
                {
                    if constexpr (DebugBackupWorker)
                        std::println(stderr, "DEBUG: Exception in FetchRow loop for table {}: {}", tableName, e.what());
                    throw;
                }

                ++retryCount;
                flushChunk(); // Save completed rows before retry

                ctx.progress.Update({ .state = Progress::State::Warning,
                                      .tableName = tableName,
                                      .currentRows = processedRows,
                                      .totalRows = totalRows,
                                      .message = std::format("Transient error, retry {}/{}, resuming from row {}: {}",
                                                             retryCount,
                                                             ctx.retrySettings.maxRetries,
                                                             processedRows,
                                                             e.what()) });

                // Reconnect and resume from last successful row
                conn.Close();
                std::this_thread::sleep_for(CalculateRetryDelay(retryCount - 1, ctx.retrySettings));

                if (!ConnectWithRetry(conn, ctx.connectionString, ctx.retrySettings, ctx.progress, tableName))
                {
                    state.failed.store(true);
                    ctx.progress.Update({ .state = Progress::State::Error,
                                          .tableName = tableName,
                                          .currentRows = processedRows,
                                          .totalRows = totalRows,
                                          .message = "Failed to reconnect after transient error" });
                    return;
                }
                stmt = SqlStatement { conn };
                // Loop will rebuild query with new offset
            }
            catch (std::exception const& e)
            {
                if constexpr (DebugBackupWorker)
                    std::println(stderr, "DEBUG: Exception in FetchRow loop for table {}: {}", tableName, e.what());
                throw;
            }
            catch (...)
            {
                if constexpr (DebugBackupWorker)
                    std::println(stderr, "DEBUG: Unknown exception in FetchRow loop for table {}", tableName);
                throw;
            }
        }
    }

    try
    {
        flushChunk(); // Final flush
        // A retried PK-range window may have produced fewer sub-chunks than an earlier attempt
        // flushed; remove the leftovers so restore never reads stale rows.
        if (usePkRange && maxSubChunksFlushed > subChunkId)
            DeleteStaleSubChunks(archive, ctx, tableName, chunk.windowIndex, subChunkId, maxSubChunksFlushed);
    }
    catch (std::exception const& e)
    {
        if constexpr (DebugBackupWorker)
            std::println(stderr, "DEBUG: Exception in Final FlushChunk for table {}: {}", tableName, e.what());
        throw;
    }
}

void ChunkWorker(ThreadSafeQueue<detail::Chunk>& chunkQueue,
                 BackupContext ctx,
                 SqlConnection& conn,
                 WorkerChunkArchive& archive)
{
    static std::atomic<int> workerCounter { 0 };
    auto const workerName = std::format("BackupWorker-{}", workerCounter.fetch_add(1));
    TracySetThreadName(workerName.c_str());

    try
    {
        detail::Chunk chunk;
        while (chunkQueue.WaitAndPop(chunk))
        {
            try
            {
                ProcessChunkBackup(ctx, conn, chunk, archive);
            }
            catch (std::exception const& e)
            {
                if (chunk.state)
                    chunk.state->failed.store(true);
                ctx.progress.Update({ .state = Progress::State::Error,
                                      .tableName = chunk.table ? chunk.table->name : "Unknown",
                                      .currentRows = 0,
                                      .totalRows = 0,
                                      .message = std::string("Backup failed: ") + e.what() });
            }

            // The worker that completes the table's last chunk reports it finished (suppressed if
            // any chunk of the table failed — the Error event above already told the user).
            if (chunk.state && chunk.state->remainingChunks.fetch_sub(1) == 1 && !chunk.state->failed.load() && chunk.table)
                ctx.progress.Update({ .state = Progress::State::Finished,
                                      .tableName = chunk.table->name,
                                      .currentRows = chunk.state->processedRows.load(),
                                      .totalRows = chunk.state->totalRows.load(),
                                      .message = "Finished table backup"s });
        }

        // Seal the worker's final (partially filled) archive so its entries reach the merge;
        // the compression of this tail runs here, still inside the worker thread.
        archive.Seal();
    }
    catch (std::exception const& e)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "Unknown",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Worker Fatal: "s + e.what() });
    }
    catch (...)
    {
        ctx.progress.Update({ .state = Progress::State::Error,
                              .tableName = "Unknown",
                              .currentRows = 0,
                              .totalRows = 0,
                              .message = "Worker Fatal: Unknown exception" });
    }
}

} // namespace Lightweight::SqlBackup::detail
