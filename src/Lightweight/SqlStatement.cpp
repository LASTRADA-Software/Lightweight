// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlRawColumn.hpp"
#include "DataBinder/UnicodeConverter.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlQuery.hpp"
#include "SqlStatement.hpp"
#include "TracyProfiler.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Lightweight
{

struct SqlStatement::Data
{
    std::optional<SqlConnection> ownedConnection;    // The connection object (if owned)
    std::vector<SQLLEN> indicators;                  // Holds the indicators for the bound output columns
    std::deque<SQLLEN> inputIndicators;              // Holds the indicators for the bound input parameters
    std::deque<std::vector<SQLLEN>> batchIndicators; // Holds the indicators for the bound batch input parameters
    std::deque<std::vector<std::byte>>
        batchStagingBuffers; // Holds temporary scratch buffers for batch input parameter binders
    std::vector<std::function<void()>> postExecuteCallbacks;
    std::vector<std::function<void()>> postProcessOutputColumnCallbacks;

    static Data const NoData;
};

SqlStatement::Data const SqlStatement::Data::NoData {};

static auto MakeUnexpected(SqlErrorInfo info, std::source_location location)
{
    SqlLogger::GetLogger().OnError(info, location);
    return std::unexpected { std::move(info) };
}

namespace
{
    /// Packs an integral ODBC statement-attribute value into the SQLPOINTER ABI slot.
    ///
    /// @c SQLSetStmtAttr takes its value argument as @c SQLPOINTER. For attributes whose value is an
    /// integer (e.g. @c SQL_ATTR_ROW_ARRAY_SIZE, @c SQL_ATTR_ROW_BIND_TYPE), ODBC's documented
    /// convention is to pass the integer in the pointer slot. We copy the bit-pattern with
    /// @c std::memcpy instead of @c reinterpret_cast so there is no integer-to-pointer conversion to
    /// reason about (and clang-tidy's @c performance-no-int-to-ptr has nothing to flag).
    ///
    /// @param value The integral attribute value to convey to ODBC.
    /// @return A @c SQLPOINTER carrying the same bit-pattern as @p value.
    [[nodiscard]] SQLPOINTER OdbcIntAttr(SQLULEN value) noexcept
    {
        static_assert(sizeof(SQLPOINTER) >= sizeof(SQLULEN),
                      "SQLPOINTER must be wide enough to carry an integral ODBC attribute value");
        SQLPOINTER ptr = nullptr;
        std::memcpy(static_cast<void*>(&ptr), &value, sizeof(value));
        return ptr;
    }
} // namespace

void SqlStatement::RequireIndicators()
{
    auto const count = NumColumnsAffected() + 1;
    if (m_data->indicators.size() <= count)
        m_data->indicators.resize(count + 1);
}

SQLLEN* SqlStatement::GetIndicatorForColumn(SQLUSMALLINT column) noexcept
{
    return &m_data->indicators[column];
}

SQLLEN* SqlStatement::ProvideInputIndicator()
{
    return &m_data->inputIndicators.emplace_back(0);
}

SQLLEN* SqlStatement::ProvideInputIndicators(size_t rowCount)
{
    m_data->batchIndicators.emplace_back(rowCount); // Emplaces a vector of rowCount elements.
    return m_data->batchIndicators.back().data();
}

std::byte* SqlStatement::ProvideBatchStagingBuffer(std::size_t byteCount)
{
    // std::vector<std::byte> allocates via operator new, which yields max_align_t-aligned storage.
    m_data->batchStagingBuffers.emplace_back(byteCount);
    return m_data->batchStagingBuffers.back().data();
}

void SqlStatement::ClearBatchIndicators()
{
    m_data->batchIndicators.clear();
    m_data->batchIndicators.shrink_to_fit();
    m_data->batchStagingBuffers.clear();
    m_data->batchStagingBuffers.shrink_to_fit();
}

void SqlStatement::ResetParameterArrayBinding() noexcept
{
    // Restore single-row, column-bound parameter binding (the ODBC default). Invoked from Prepare() and,
    // via a scope guard, after a native row-wise batch — including the exception path, which is why this
    // is noexcept and ignores the return codes (resetting to defaults on a live handle does not
    // meaningfully fail, and a cleanup running during stack unwinding must never throw).
    // clang-format off
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER) 1, 0);
    SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0);
    SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_OFFSET_PTR, nullptr, 0);
    SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMS_PROCESSED_PTR, nullptr, 0);
    // clang-format on
}

void SqlStatement::RequireExecuteSucceededOrNoData(SQLRETURN result, std::source_location sourceLocation) const
{
    // A searched UPDATE/DELETE that matched no rows returns SQL_NO_DATA (per the ODBC spec): the
    // statement executed, it simply changed nothing. Treat it as success, matching Execute().
    if (result != SQL_NO_DATA && result != SQL_SUCCESS && result != SQL_SUCCESS_WITH_INFO)
        throw SqlException(SqlErrorInfo::FromStatementHandle(m_hStmt), sourceLocation);
}

void SqlStatement::RequireSuccessfulBatchExecute(SQLRETURN result,
                                                 SQLULEN processedCount,
                                                 SQLULEN expectedCount,
                                                 std::source_location sourceLocation) const
{
    RequireExecuteSucceededOrNoData(result, sourceLocation);

    // Guard against a driver that silently honours fewer parameter sets than requested. The caller
    // initialises processedCount to expectedCount, so a driver that ignores SQL_ATTR_PARAMS_PROCESSED_PTR
    // never trips this; only one that reports a short count does.
    if (SQL_SUCCEEDED(result) && processedCount != expectedCount)
        throw SqlException(
            SqlErrorInfo {
                .nativeErrorCode = 0,
                .sqlState = "HY000",
                .message = std::format("Native row-wise batch processed {} of {} parameter sets; the ODBC "
                                       "driver did not honour the full parameter array.",
                                       processedCount,
                                       expectedCount),
            },
            sourceLocation);
}

void SqlStatement::PlanPostExecuteCallback(std::function<void()>&& cb)
{
    m_data->postExecuteCallbacks.emplace_back(std::move(cb));
}

void SqlStatement::ProcessPostExecuteCallbacks()
{
    for (auto& cb: m_data->postExecuteCallbacks)
        cb();
    m_data->postExecuteCallbacks.clear();
}

void SqlStatement::PlanPostProcessOutputColumn(std::function<void()>&& cb)
{
    m_data->postProcessOutputColumnCallbacks.emplace_back(std::move(cb));
}

SqlServerType SqlStatement::ServerType() const noexcept
{
    return m_connection->ServerType();
}

std::string const& SqlStatement::DriverName() const noexcept
{
    return m_connection->DriverName();
}

SqlStatement::SqlStatement():
    m_data { new Data {
                 .ownedConnection = SqlConnection(),
                 .indicators = {},
                 .inputIndicators = {},
                 .batchIndicators = {},
                 .batchStagingBuffers = {},
                 .postExecuteCallbacks = {},
                 .postProcessOutputColumnCallbacks = {},
             },

             [](Data* data) {
                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                 delete data;
             } },
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    m_connection { &*m_data->ownedConnection }
{
    if (m_connection->NativeHandle())
        RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

SqlStatement::SqlStatement(SqlStatement&& other) noexcept:
    m_data { std::move(other.m_data) },
    m_connection { other.m_connection },
    m_hStmt { other.m_hStmt },
    m_preparedQuery { std::move(other.m_preparedQuery) },
    m_expectedParameterCount { other.m_expectedParameterCount }
{
    other.m_data.reset();
    other.m_connection = nullptr;
    other.m_hStmt = SQL_NULL_HSTMT;
}

SqlStatement& SqlStatement::operator=(SqlStatement&& other) noexcept
{
    if (this == &other)
        return *this;

    m_data = std::move(other.m_data);
    m_connection = other.m_connection;
    m_hStmt = other.m_hStmt;
    m_preparedQuery = std::move(other.m_preparedQuery);
    m_expectedParameterCount = other.m_expectedParameterCount;

    other.m_data.reset();
    other.m_connection = nullptr;
    other.m_hStmt = SQL_NULL_HSTMT;

    return *this;
}

// Construct a new SqlStatement object, using the given connection.
SqlStatement::SqlStatement(SqlConnection& relatedConnection):
    m_data { new Data(),
             [](Data* data) {
                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                 delete data;
             } },
    m_connection { &relatedConnection }
{
    RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

SqlStatement::SqlStatement(std::nullopt_t /*nullopt*/):
    m_data { const_cast<Data*>(&Data::NoData), [](Data* /*data*/) {} }
{
}

SqlStatement::~SqlStatement() noexcept
{
    SqlLogger::GetLogger().OnFetchEnd();
    SQLFreeHandle(SQL_HANDLE_STMT, m_hStmt);
}

SqlStatement SqlStatement::Prepare(std::string_view query) &&
{
    auto resultStatement = SqlStatement { std::move(*this) };
    resultStatement.Prepare(query);
    return resultStatement;
}

void SqlStatement::Prepare(std::string_view query) &
{
    ZoneScopedN("SqlStatement::Prepare");
    ZoneTextObject(query);
    SqlLogger::GetLogger().OnPrepare(query);

    m_preparedQuery = std::string(query);
    const_cast<SqlStatement*>(this)->m_numColumns.reset();

    m_data->postExecuteCallbacks.clear();
    m_data->postProcessOutputColumnCallbacks.clear();
    m_data->inputIndicators.clear();
    m_data->batchIndicators.clear();
    m_data->batchStagingBuffers.clear();

    // Reset parameter-array binding attributes that a preceding batch execution may have left on the
    // handle. SqlStatement handles are reused (e.g. by DataMapper) across single and batched executes;
    // without this reset a subsequent single Execute() would inherit a stale PARAMSET_SIZE and a
    // dangling row-wise PARAM_BIND_OFFSET_PTR/PARAM_BIND_TYPE.
    ResetParameterArrayBinding();

    // Unbinds the columns, if any
    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_UNBIND));

    // Prepares the statement on the W (Unicode) entry point so the ODBC driver stays
    // on the same Unicode-app track that SQLDriverConnectW set up. Mixing A and W
    // calls on the same handle works in theory (the Driver Manager auto-translates),
    // but psqlODBC has historically treated SQL_C_CHAR parameter binds differently
    // depending on the variant of the most recent statement-text call — so we keep
    // the path uniformly W to side-step that.
    auto wQuery = detail::OdbcWideArg { query };
    RequireSuccess(SQLPrepareW(m_hStmt, wQuery.data(), static_cast<SQLINTEGER>(wQuery.buffer.size())));
    RequireSuccess(SQLNumParams(m_hStmt, &m_expectedParameterCount));
    m_data->indicators.resize(static_cast<size_t>(m_expectedParameterCount) + 1);
}

SqlResultCursor SqlStatement::ExecuteDirect(std::string_view const& query, std::source_location location)
{
    ZoneScopedN("SqlStatement::ExecuteDirect");
    ZoneTextObject(query);
    if (query.empty())
        return SqlResultCursor { *this };

    m_preparedQuery.clear();
    m_numColumns.reset();

    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_UNBIND));

    m_data->inputIndicators.clear();
    m_data->batchIndicators.clear();

    SqlLogger::GetLogger().OnExecuteDirect(query);

    // Execute via the W entry point — see the rationale above SQLPrepareW.
    auto wQuery = detail::OdbcWideArg { query };
    auto const rc = SQLExecDirectW(m_hStmt, wQuery.data(), static_cast<SQLINTEGER>(wQuery.buffer.size()));
    // SQL_NO_DATA from SQLExecDirect signals "searched UPDATE/DELETE affected no rows"
    // (per ODBC spec) — and the SQLite ODBC driver also returns it for INSERT … SELECT
    // that copies zero rows. That is not a failure: the statement executed, it simply
    // produced no row changes. Treat it as success.
    if (rc != SQL_NO_DATA)
        RequireSuccess(rc, location);
    return SqlResultCursor { *this };
}

SqlResultCursor SqlStatement::ExecuteWithVariants(std::vector<SqlVariant> const& args)
{
    ZoneScopedN("SqlStatement::ExecuteWithVariants");
    ZoneTextObject(m_preparedQuery);
    SqlLogger::GetLogger().OnExecute(m_preparedQuery);

    if (!(m_expectedParameterCount == (std::numeric_limits<decltype(m_expectedParameterCount)>::max)() && args.empty())
        && !(static_cast<size_t>(m_expectedParameterCount) == args.size()))
        throw std::invalid_argument { "Invalid argument count" };

#if !defined(__cpp_lib_ranges_enumerate)
    int i { -1 };
    for (auto const& arg: args)
    {
        ++i;
#else
    for (auto const& [i, arg]: args | std::views::enumerate)
    {
#endif
        SqlDataBinder<SqlVariant>::InputParameter(m_hStmt, static_cast<SQLUSMALLINT>(1 + i), arg, *this);
    }

    auto const rc = SQLExecute(m_hStmt);
    if (rc != SQL_NO_DATA)
        RequireSuccess(rc);
    ProcessPostExecuteCallbacks();
    return SqlResultCursor { *this };
}

SqlResultCursor SqlStatement::ExecuteBatch(std::span<SqlRawColumn const> columns, size_t rowCount)
{
    ZoneScopedN("SqlStatement::ExecuteBatch");
    ZoneTextObject(m_preparedQuery);
    ZoneValue(rowCount);
    SqlLogger::GetLogger().OnExecute(m_preparedQuery);

    if (m_expectedParameterCount != static_cast<SQLSMALLINT>(columns.size()))
        throw std::invalid_argument { "Invalid number of columns" };

    static SQLLEN ZeroOffset = 0;
    // clang-format off
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER) rowCount, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_OFFSET_PTR, &ZeroOffset, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_OPERATION_PTR, SQL_PARAM_PROCEED, 0));
    // clang-format on

    SQLUSMALLINT column = 1;
    for (auto const& col: columns)
    {
        RequireSuccess(SqlDataBinder<SqlRawColumn>::InputParameter(m_hStmt, column++, col, *this));
    }

    RequireSuccess(SQLExecute(m_hStmt));
    ProcessPostExecuteCallbacks();
    ClearBatchIndicators();
    return SqlResultCursor { *this };
}

RowArrayCursor SqlStatement::ExecuteBatchFetch(std::string_view query, std::size_t arrayDepth)
{
    ZoneScopedN("SqlStatement::ExecuteBatchFetch");
    ZoneTextObject(query);
    ZoneValue(arrayDepth);

    if (arrayDepth == 0)
        throw std::invalid_argument { "arrayDepth must be greater than zero" };

    m_preparedQuery.clear();
    m_numColumns.reset();

    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_UNBIND));

    m_data->inputIndicators.clear();
    m_data->batchIndicators.clear();

    SqlLogger::GetLogger().OnExecuteDirect(query);

    // Execute via the W entry point — see the rationale above SQLPrepareW in Prepare().
    auto wQuery = detail::OdbcWideArg { query };
    auto const rc = SQLExecDirectW(m_hStmt, wQuery.data(), static_cast<SQLINTEGER>(wQuery.buffer.size()));
    if (rc != SQL_NO_DATA)
        RequireSuccess(rc);

    return RowArrayCursor { *this, arrayDepth };
}

// Retrieves the number of rows affected by the last query.
size_t SqlStatement::NumRowsAffected() const
{
    SQLLEN numRowsAffected {};
    RequireSuccess(SQLRowCount(m_hStmt, &numRowsAffected));
    return static_cast<size_t>(numRowsAffected);
}

// Retrieves the number of columns affected by the last query.
size_t SqlStatement::NumColumnsAffected() const
{
    if (!m_numColumns)
    {
        SQLSMALLINT numColumns {};
        RequireSuccess(SQLNumResultCols(m_hStmt, &numColumns));
        const_cast<SqlStatement*>(this)->m_numColumns = numColumns;
    }

    return static_cast<size_t>(m_numColumns.value()); // NOLINT(bugprone-unchecked-optional-access)
}

// Retrieves the last insert ID of the last query's primary key.
size_t SqlStatement::LastInsertId(std::string_view tableName)
{
    return ExecuteDirectScalar<size_t>(Query(tableName).LastInsertId()).value_or(0);
}

// Fetches the next row of the result set.
bool SqlStatement::FetchRow()
{
    ZoneScopedN("SqlStatement::FetchRow");
    auto result = TryFetchRow();
    if (result.has_value())
        return result.value();

    SqlErrorInfo const errorInfo = std::move(result.error());
    if (errorInfo.sqlState == "07009")
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    else
        throw SqlException(errorInfo);
}

std::expected<bool, SqlErrorInfo> SqlStatement::TryFetchRow(std::source_location location) noexcept
{
    auto const sqlResult = SQLFetch(m_hStmt);
    switch (sqlResult)
    {
        case SQL_NO_DATA:
            SQLCloseCursor(m_hStmt);
            m_data->postProcessOutputColumnCallbacks.clear();
            SqlLogger::GetLogger().OnFetchEnd();
            return false;
        default:
            if (!SQL_SUCCEEDED(sqlResult))
                return MakeUnexpected(LastError(), location);

            // post-process the output columns, if needed
            for (auto const& postProcess: m_data->postProcessOutputColumnCallbacks)
                postProcess();
            m_data->postProcessOutputColumnCallbacks.clear();
            SqlLogger::GetLogger().OnFetchRow();
            return true;
    }
}

void SqlStatement::RequireSuccess(SQLRETURN error, std::source_location sourceLocation) const
{
    Lightweight::RequireSuccess(m_hStmt, error, sourceLocation);
}

SqlQueryBuilder SqlStatement::Query(std::string_view const& table) const
{
    return Connection().Query(table);
}

SqlQueryBuilder SqlStatement::QueryAs(std::string_view const& table, std::string_view const& tableAlias) const
{
    return Connection().QueryAs(table, tableAlias);
}

// {{{ RowArrayCursor

namespace
{
    // Maps the SQL data type reported by SQLDescribeCol onto one of the three fixed-stride bound
    // representations. Integer SQL types collapse to int64, floating types to double, and
    // everything else is read textually via SQL_C_CHAR.
    constexpr bool IsIntegerSqlType(SQLSMALLINT sqlType) noexcept
    {
        switch (sqlType)
        {
            case SQL_BIT:
            case SQL_TINYINT:
            case SQL_SMALLINT:
            case SQL_INTEGER:
            case SQL_BIGINT:
                return true;
            default:
                return false;
        }
    }

    constexpr bool IsFloatingSqlType(SQLSMALLINT sqlType) noexcept
    {
        switch (sqlType)
        {
            case SQL_REAL:
            case SQL_FLOAT:
            case SQL_DOUBLE:
                return true;
            default:
                return false;
        }
    }

    constexpr bool IsWideCharSqlType(SQLSMALLINT sqlType) noexcept
    {
        switch (sqlType)
        {
            case SQL_WCHAR:
            case SQL_WVARCHAR:
            case SQL_WLONGVARCHAR:
                return true;
            default:
                return false;
        }
    }
} // namespace

RowArrayCursor::RowArrayCursor(SqlStatement& stmt, std::size_t arrayDepth):
    m_stmt { &stmt },
    m_arrayDepth { arrayDepth }
{
    SQLHSTMT const hStmt = m_stmt->NativeHandle();

    SQLSMALLINT numColumns = 0;
    m_stmt->RequireSuccess(SQLNumResultCols(hStmt, &numColumns));
    if (numColumns <= 0)
        throw RowArrayCursorUnsupported { "RowArrayCursor: query produced no result columns" };

    m_columns.reserve(static_cast<std::size_t>(numColumns));

    for (auto const i: std::views::iota(SQLUSMALLINT(1), static_cast<SQLUSMALLINT>(numColumns + 1)))
    {
        SQLSMALLINT sqlType = 0;
        SQLULEN columnSize = 0;
        SQLSMALLINT decimalDigits = 0;
        SQLSMALLINT nullable = 0;
        SQLSMALLINT nameLen = 0;
        SQLWCHAR nameBuffer[256] = {};
        m_stmt->RequireSuccess(SQLDescribeColW(hStmt,
                                               i,
                                               nameBuffer,
                                               static_cast<SQLSMALLINT>(std::size(nameBuffer)),
                                               &nameLen,
                                               &sqlType,
                                               &columnSize,
                                               &decimalDigits,
                                               &nullable));

        BoundColumn boundColumn;
        if (IsIntegerSqlType(sqlType))
        {
            boundColumn.type = BoundType::Int64;
            boundColumn.elementWidth = sizeof(std::int64_t);
        }
        else if (IsFloatingSqlType(sqlType))
        {
            boundColumn.type = BoundType::Double;
            boundColumn.elementWidth = sizeof(double);
        }
        else if (sqlType == SQL_TYPE_DATE || sqlType == SQL_DATE)
        {
            boundColumn.type = BoundType::Date;
            boundColumn.elementWidth = sizeof(SQL_DATE_STRUCT);
        }
        else if (sqlType == SQL_TYPE_TIMESTAMP || sqlType == SQL_TIMESTAMP)
        {
            boundColumn.type = BoundType::Timestamp;
            boundColumn.elementWidth = sizeof(SQL_TIMESTAMP_STRUCT);
        }
        else if (sqlType == SQL_GUID)
        {
            boundColumn.type = BoundType::Guid;
            boundColumn.elementWidth = sizeof(SQLGUID);
        }
        else if (IsWideCharSqlType(sqlType))
        {
            // Wide (UTF-16) character columns: SQLDescribeCol reports the maximum *character*
            // count; SQL_C_WCHAR buffers count bytes at 2 bytes per UTF-16 code unit (plus a NUL).
            // The LOB/oversize rejection mirrors the narrow path's character-count semantics.
            if (columnSize == 0 || columnSize >= MaxCharColumnBytes)
                throw RowArrayCursorUnsupported {
                    "RowArrayCursor: column is unbounded (LOB) or too wide for fixed-stride bulk fetch"
                };
            boundColumn.type = BoundType::WChar;
            boundColumn.elementWidth = (static_cast<std::size_t>(columnSize) + 1) * sizeof(SQLWCHAR);
        }
        else
        {
            // Character (and textual representations of decimal/date/time) columns. SQLDescribeCol
            // reports the maximum *character* count, but we bind as SQL_C_CHAR which counts *bytes*.
            // Multibyte/UTF-8 data (e.g. German umlauts) is wider in bytes than in characters, so a
            // buffer sized by character count would silently truncate. We therefore allocate the
            // UTF-8 worst case of 4 bytes per character (plus a NUL terminator).
            //
            // The LOB/unbounded rejection keeps its original *character*-count threshold semantics:
            // a reported size of 0 means the driver could not bound the column (LOB / unbounded),
            // and a size >= MaxCharColumnBytes is treated as too wide for fixed-stride bulk fetch.
            // A legitimately bounded column such as varchar(255) becomes 255*4+1 = 1021 bytes; a
            // varchar(8000) becomes ~32 KB per row. At high arrayDepth that is a known memory
            // tradeoff (arrayDepth * 32 KB) accepted in exchange for correct multibyte round-trips.
            if (columnSize == 0 || columnSize >= MaxCharColumnBytes)
                throw RowArrayCursorUnsupported {
                    "RowArrayCursor: column is unbounded (LOB) or too wide for fixed-stride bulk fetch"
                };
            boundColumn.type = BoundType::Char;
            boundColumn.elementWidth = (static_cast<std::size_t>(columnSize) * 4) + 1;
        }

        m_columns.emplace_back(std::move(boundColumn));
    }

    // Adapt the depth to the per-cursor memory budget: a wide row (many or large character
    // columns) binds fewer rows per round-trip instead of multiplying its width by the full
    // requested depth (the footprint otherwise grows with workers x columns x depth and has been
    // observed to exhaust physical RAM on wide production schemas).
    {
        auto rowWidth = std::size_t { 0 };
        for (auto const& column: m_columns)
            rowWidth += column.elementWidth + sizeof(SQLLEN); // value stride + length indicator
        auto const budgetDepth = MemoryBudgetBytes / std::max<std::size_t>(rowWidth, 1);
        auto const minDepth = std::min(MinArrayDepth, m_arrayDepth); // never raise above the request
        m_arrayDepth = std::clamp(budgetDepth, minDepth, m_arrayDepth);
    }

    m_rowStatus.resize(m_arrayDepth);
    for (auto& column: m_columns)
    {
        column.buffer.assign(column.elementWidth * m_arrayDepth, char {});
        column.indicators.assign(m_arrayDepth, SQLLEN {});
    }

    // Configure row-array (column-wise) fetching on the statement handle. These attributes take
    // integral values conveyed through the SQLPOINTER slot (see OdbcIntAttr).
    m_stmt->RequireSuccess(SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_BIND_TYPE, OdbcIntAttr(SQL_BIND_BY_COLUMN), 0));
    m_stmt->RequireSuccess(SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, OdbcIntAttr(m_arrayDepth), 0));
    m_stmt->RequireSuccess(SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_STATUS_PTR, m_rowStatus.data(), 0));
    m_stmt->RequireSuccess(SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &m_rowsFetched, 0));

    // Bind each column to its contiguous buffer + indicator array.
    for (auto const i: std::views::iota(std::size_t { 0 }, m_columns.size()))
    {
        auto& column = m_columns[i];
        SQLSMALLINT cType = SQL_C_CHAR;
        SQLLEN bufferLength = 0;
        switch (column.type)
        {
            case BoundType::Int64:
                cType = SQL_C_SBIGINT;
                bufferLength = 0; // fixed-size C type: ODBC ignores buffer length
                break;
            case BoundType::Double:
                cType = SQL_C_DOUBLE;
                bufferLength = 0;
                break;
            case BoundType::Char:
                cType = SQL_C_CHAR;
                bufferLength = static_cast<SQLLEN>(column.elementWidth);
                break;
            case BoundType::WChar:
                cType = SQL_C_WCHAR;
                bufferLength = static_cast<SQLLEN>(column.elementWidth);
                break;
            case BoundType::Date:
                cType = SQL_C_TYPE_DATE;
                bufferLength = 0; // fixed-size C type: ODBC ignores buffer length
                break;
            case BoundType::Timestamp:
                cType = SQL_C_TYPE_TIMESTAMP;
                bufferLength = 0;
                break;
            case BoundType::Guid:
                cType = SQL_C_GUID;
                bufferLength = 0;
                break;
        }
        m_stmt->RequireSuccess(SQLBindCol(
            hStmt, static_cast<SQLUSMALLINT>(i + 1), cType, column.buffer.data(), bufferLength, column.indicators.data()));
    }
}

void RowArrayCursor::ResetStatementState() noexcept
{
    if (!m_stmt)
        return;

    // Restore the statement handle to a single-row, unbound state so it can be safely reused for
    // another query. Failures here are non-fatal (best-effort cleanup), so we don't go through
    // RequireSuccess which would throw.
    SQLHSTMT const hStmt = m_stmt->NativeHandle();
    SQLFreeStmt(hStmt, SQL_UNBIND);
    SQLFreeStmt(hStmt, SQL_CLOSE);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, OdbcIntAttr(1), 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_BIND_TYPE, OdbcIntAttr(SQL_BIND_BY_COLUMN), 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_STATUS_PTR, nullptr, 0);
    SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, nullptr, 0);
}

RowArrayCursor::~RowArrayCursor() noexcept
{
    ResetStatementState();
}

std::size_t RowArrayCursor::FetchArray()
{
    ZoneScopedN("RowArrayCursor::FetchArray");

    m_rowsFetched = 0;
    auto const rc = SQLFetchScroll(m_stmt->NativeHandle(), SQL_FETCH_NEXT, 0);
    if (rc == SQL_NO_DATA)
    {
        m_lastFetched = 0;
        return 0;
    }
    if (!SQL_SUCCEEDED(rc))
        m_stmt->RequireSuccess(rc);

    // SQL_SUCCESS_WITH_INFO is acceptable: it can flag e.g. truncation of one cell, but the row
    // count in m_rowsFetched is still valid. The bounded-column guard in the constructor keeps
    // fixed-stride columns from truncating in practice.
    m_lastFetched = static_cast<std::size_t>(m_rowsFetched);
    return m_lastFetched;
}

std::size_t RowArrayCursor::ColumnCount() const noexcept
{
    return m_columns.size();
}

std::size_t RowArrayCursor::ArrayDepth() const noexcept
{
    return m_arrayDepth;
}

char const* RowArrayCursor::CheckedCell(std::size_t rowInBatch,
                                        SQLUSMALLINT column,
                                        BoundType expected,
                                        char const* accessorName) const
{
    if (rowInBatch >= m_lastFetched)
        throw std::out_of_range { std::format(
            "RowArrayCursor: rowInBatch {} >= rowsFetched {}", rowInBatch, m_lastFetched) };
    auto const& boundColumn = m_columns.at(column - 1);
    if (boundColumn.type != expected)
        throw std::logic_error { std::format("RowArrayCursor::{} called on a mismatched column binding", accessorName) };
    if (boundColumn.indicators[rowInBatch] == SQL_NULL_DATA)
        return nullptr;
    return boundColumn.buffer.data() + (rowInBatch * boundColumn.elementWidth);
}

std::optional<std::int64_t> RowArrayCursor::GetI64(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    auto const* cell = CheckedCell(rowInBatch, column, BoundType::Int64, "GetI64");
    if (!cell)
        return std::nullopt;
    std::int64_t value = 0;
    std::memcpy(&value, cell, sizeof(value));
    return value;
}

std::optional<double> RowArrayCursor::GetF64(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    auto const* cell = CheckedCell(rowInBatch, column, BoundType::Double, "GetF64");
    if (!cell)
        return std::nullopt;
    double value = 0.0;
    std::memcpy(&value, cell, sizeof(value));
    return value;
}

std::optional<std::string> RowArrayCursor::GetString(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    if (rowInBatch >= m_lastFetched)
        throw std::out_of_range { std::format(
            "RowArrayCursor: rowInBatch {} >= rowsFetched {}", rowInBatch, m_lastFetched) };
    auto const& boundColumn = m_columns.at(column - 1);
    if (boundColumn.type != BoundType::Char && boundColumn.type != BoundType::WChar)
        throw std::logic_error { "RowArrayCursor::GetString called on a non-character column" };

    auto const indicator = boundColumn.indicators[rowInBatch];
    if (indicator == SQL_NULL_DATA)
        return std::nullopt;

    if (boundColumn.type == BoundType::WChar)
    {
        // The indicator carries the value's byte length; the buffer holds UTF-16 code units. A
        // value wider than the bound buffer was truncated by the driver (indicator > usable width,
        // or SQL_NO_TOTAL when the driver can't report the full length). Silently clamping it would
        // write a half-row into the backup, so we fail loudly instead. The constructor sizes the
        // buffer to the column's full declared width, so this cannot fire for a legitimately
        // bounded column — reaching it means the column is wider than SQLDescribeCol reported.
        auto const usableUnits = (boundColumn.elementWidth / sizeof(char16_t)) - 1;
        if (indicator < 0 || std::cmp_greater(indicator, usableUnits * sizeof(char16_t)))
            throw std::runtime_error { std::format(
                "RowArrayCursor::GetString: value in column {} was truncated during bulk fetch "
                "(indicator byte length {}, buffer holds {} code units); aborting to avoid a corrupt backup",
                column,
                indicator,
                usableUnits) };
        auto const units = static_cast<std::size_t>(indicator) / sizeof(char16_t);
        auto const* cell =
            reinterpret_cast<char16_t const*>(boundColumn.buffer.data() + (rowInBatch * boundColumn.elementWidth));
        // GetString hands back UTF-8 bytes in a std::string (an opaque byte container for callers
        // such as the backup serializer); convert UTF-16 -> UTF-8 (std::u8string) then alias the bytes.
        auto const utf8 = ToUtf8(std::u16string_view { cell, units });
        return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
    }

    // The indicator carries the byte length of the value (excluding the NUL terminator). As above,
    // a value wider than the usable buffer width was truncated — fail loudly rather than corrupt
    // the backup with a clamped value.
    auto const usableWidth = boundColumn.elementWidth - 1;
    if (indicator < 0 || std::cmp_greater(indicator, usableWidth))
        throw std::runtime_error { std::format(
            "RowArrayCursor::GetString: value in column {} was truncated during bulk fetch "
            "(indicator byte length {}, buffer holds {} bytes); aborting to avoid a corrupt backup",
            column,
            indicator,
            usableWidth) };
    char const* const cell = boundColumn.buffer.data() + (rowInBatch * boundColumn.elementWidth);
    return std::string { cell, static_cast<std::size_t>(indicator) };
}

std::optional<SqlDate> RowArrayCursor::GetDate(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    auto const* cell = CheckedCell(rowInBatch, column, BoundType::Date, "GetDate");
    if (!cell)
        return std::nullopt;
    SqlDate value;
    std::memcpy(&value.sqlValue, cell, sizeof(value.sqlValue));
    return value;
}

std::optional<SqlDateTime> RowArrayCursor::GetTimestamp(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    auto const* cell = CheckedCell(rowInBatch, column, BoundType::Timestamp, "GetTimestamp");
    if (!cell)
        return std::nullopt;
    SqlDateTime value;
    std::memcpy(&value.sqlValue, cell, sizeof(value.sqlValue));
    return value;
}

std::optional<SqlGuid> RowArrayCursor::GetGuid(std::size_t rowInBatch, SQLUSMALLINT column) const
{
    auto const* cell = CheckedCell(rowInBatch, column, BoundType::Guid, "GetGuid");
    if (!cell)
        return std::nullopt;
    // The cell holds the driver-filled SQLGUID; SqlGuid::data carries the same raw 16 bytes the
    // single-row SQLGetData(SQL_C_GUID) fill produces, so to_string yields identical text.
    SqlGuid value;
    static_assert(sizeof(value.data) == sizeof(SQLGUID));
    std::memcpy(value.data, cell, sizeof(value.data));
    return value;
}

// }}} RowArrayCursor

} // namespace Lightweight
