// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "Api.hpp"
#include "DataBinder/Core.hpp"
#include "DataBinder/SqlDate.hpp"
#include "DataBinder/SqlDateTime.hpp"
#include "DataBinder/SqlGuid.hpp"
#include "SqlConnection.hpp"
#include "SqlQuery.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlServerType.hpp"
#include "TracyProfiler.hpp"
#include "Utils.hpp"

#include <cstring>
#include <expected>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

namespace Lightweight
{

struct SqlRawColumn;

/// @brief Represents an SQL query object, that provides a ToSql() method.
template <typename QueryObject>
concept SqlQueryObject = requires(QueryObject const& queryObject) {
    { queryObject.ToSql() } -> std::convertible_to<std::string>;
};

class SqlResultCursor;
class SqlVariantRowCursor;
class RowArrayCursor;

/// @brief High level API for (prepared) raw SQL statements
///
/// SQL prepared statement lifecycle:
/// 1. Prepare the statement
/// 2. Optionally bind output columns to local variables
/// 3. Execute the statement (optionally with input parameters)
/// 4. Fetch rows (if any)
/// 5. Repeat steps 3 and 4 as needed
class [[nodiscard]] SqlStatement final: public SqlDataBinderCallback
{
  public:
    /// Construct a new SqlStatement object, using a new connection, and connect to the default database.
    LIGHTWEIGHT_API SqlStatement();

    /// Move constructor.
    LIGHTWEIGHT_API SqlStatement(SqlStatement&& other) noexcept;
    /// Move assignment operator.
    LIGHTWEIGHT_API SqlStatement& operator=(SqlStatement&& other) noexcept;

    SqlStatement(SqlStatement const&) noexcept = delete;
    SqlStatement& operator=(SqlStatement const&) noexcept = delete;

    /// Construct a new SqlStatement object, using the given connection.
    LIGHTWEIGHT_API explicit SqlStatement(SqlConnection& relatedConnection);

    /// Construct a new empty SqlStatement object. No SqlConnection is associated with this statement.
    LIGHTWEIGHT_API explicit SqlStatement(std::nullopt_t /*nullopt*/);

    LIGHTWEIGHT_API ~SqlStatement() noexcept final;

    /// Checks whether the statement's connection is alive and the statement handle is valid.
    [[nodiscard]] LIGHTWEIGHT_API bool IsAlive() const noexcept;

    /// Checks whether the statement has been prepared.
    [[nodiscard]] LIGHTWEIGHT_API bool IsPrepared() const noexcept;

    /// Retrieves the connection associated with this statement.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnection& Connection() noexcept;

    /// Retrieves the connection associated with this statement.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnection const& Connection() const noexcept;

    /// Retrieves the last error information with respect to this SQL statement handle.
    [[nodiscard]] LIGHTWEIGHT_API SqlErrorInfo LastError() const;

    /// Creates a new query builder for the given table, compatible with the SQL server being connected.
    LIGHTWEIGHT_API SqlQueryBuilder Query(std::string_view const& table = {}) const;

    /// Creates a new query builder for the given table with an alias, compatible with the SQL server being connected.
    [[nodiscard]] LIGHTWEIGHT_API SqlQueryBuilder QueryAs(std::string_view const& table,
                                                          std::string_view const& tableAlias) const;

    /// Retrieves the native handle of the statement.
    [[nodiscard]] LIGHTWEIGHT_API SQLHSTMT NativeHandle() const noexcept;

    /// Prepares the statement for execution.
    ///
    /// @note When preparing a new SQL statement the previously executed statement, yielding a result set,
    ///       must have been closed.
    LIGHTWEIGHT_API void Prepare(std::string_view query) &;

    /// Prepares the statement for execution on an rvalue reference and returns the statement.
    LIGHTWEIGHT_API SqlStatement Prepare(std::string_view query) &&;

    /// Prepares the statement for execution.
    ///
    /// @note When preparing a new SQL statement the previously executed statement, yielding a result set,
    ///       must have been closed.
    void Prepare(SqlQueryObject auto const& queryObject) &;

    /// Prepares the statement from a query object on an rvalue reference and returns the statement.
    SqlStatement Prepare(SqlQueryObject auto const& queryObject) &&;

    /// Retrieves the last prepared query string.
    [[nodiscard]] std::string const& PreparedQuery() const noexcept;

    /// Binds an input parameter to the prepared statement at the given column index.
    template <SqlInputParameterBinder Arg>
    void BindInputParameter(SQLSMALLINT columnIndex, Arg const& arg);

    /// Binds an input parameter to the prepared statement at the given column index with a column name hint.
    template <SqlInputParameterBinder Arg, typename ColumnName>
    void BindInputParameter(SQLSMALLINT columnIndex, Arg const& arg, ColumnName&& columnNameHint);

    /// Binds the given arguments to the prepared statement and executes it.
    template <SqlInputParameterBinder... Args>
    [[nodiscard]] SqlResultCursor Execute(Args const&... args);

    /// Binds the given arguments to the prepared statement and executes it.
    [[nodiscard]] LIGHTWEIGHT_API SqlResultCursor ExecuteWithVariants(std::vector<SqlVariant> const& args);

    /// Executes the prepared statement on a batch of data.
    ///
    /// Each parameter represents a column, to be bound as input parameter.
    /// The element types of each column container must be explicitly supported.
    ///
    /// In order to support column value types, their underlying storage must be contiguous.
    /// Also the input range itself must be contiguous.
    /// If any of these conditions are not met, the function will not compile - use ExecuteBatch() instead.
    template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::contiguous_range... MoreColumnBatches>
    [[nodiscard]] SqlResultCursor ExecuteBatchNative(FirstColumnBatch const& firstColumnBatch,
                                                     MoreColumnBatches const&... moreColumnBatches);

    /// Executes the prepared statement on a batch of data.
    ///
    /// Each parameter represents a column, to be bound as input parameter,
    /// and the number of elements in these bound column containers will
    /// mandate how many executions will happen.
    ///
    /// This function will bind and execute each row separately,
    /// which is less efficient than ExecuteBatchNative(), but works non-contiguous input ranges.
    template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::range... MoreColumnBatches>
    [[nodiscard]] SqlResultCursor ExecuteBatchSoft(FirstColumnBatch const& firstColumnBatch,
                                                   MoreColumnBatches const&... moreColumnBatches);

    /// Executes the prepared statement on a batch of data.
    ///
    /// Each parameter represents a column, to be bound as input parameter,
    /// and the number of elements in these bound column containers will
    /// mandate how many executions will happen.
    template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::range... MoreColumnBatches>
    [[nodiscard]] SqlResultCursor ExecuteBatch(FirstColumnBatch const& firstColumnBatch,
                                               MoreColumnBatches const&... moreColumnBatches);

    /// Executes the prepared statement on a batch of SqlRawColumn-prepared data.
    ///
    /// @param columns The columns to bind as input parameters.
    /// @param rowCount The number of rows to execute.
    [[nodiscard]] LIGHTWEIGHT_API SqlResultCursor ExecuteBatch(std::span<SqlRawColumn const> columns, size_t rowCount);

    /// Executes the prepared statement once per row of a *row-major* batch, preferring native ODBC
    /// row-wise array binding (a single zero-copy @c SQLExecute) and transparently falling back to a
    /// prepare-once + per-row execute when native binding is not possible.
    ///
    /// Unlike the column-major @c ExecuteBatch overloads, the data here is laid out as an array of row
    /// structs (e.g. records). Each @p accessors invocable maps a row to one bound column's value,
    /// returning a reference into the row (so the native path binds the value in place):
    /// @code
    /// stmt.ExecuteBatch(std::span { records }, [](Record const& r) -> auto const& { return r.id.Value(); }, ...);
    /// @endcode
    ///
    /// The native row-wise path is taken when every column value type is row-bindable
    /// (@c SqlNativeRowBindableValue, or @c std::optional of such a non-numeric type), every accessor
    /// returns an lvalue reference, the row stride satisfies the indicator-alignment requirement, and the
    /// driver advertises parameter-array support (@ref SqlConnection::SupportsNativeRowBatch). A
    /// per-row runtime stride check guards against accessors that are not constant-offset subobjects.
    /// Otherwise the soft path is used, which correctly binds every supported type (strings, binary,
    /// variant, @c std::optional of any type, …) one row at a time.
    ///
    /// @param rows Contiguous range of row structs (e.g. @c std::span<Record const>).
    /// @param accessors One invocable per bound column; @c accessor(row) yields that column's value.
    /// @return A result cursor for the executed batch (empty when @p rows is empty).
    template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
        requires(sizeof...(ColumnAccessors) >= 1
                 && (std::invocable<ColumnAccessors const&, std::ranges::range_value_t<Rows> const&> && ...))
    [[nodiscard]] SqlResultCursor ExecuteBatch(Rows const& rows, ColumnAccessors const&... accessors);

    /// Executes the given query directly.
    [[nodiscard]] LIGHTWEIGHT_API SqlResultCursor
    ExecuteDirect(std::string_view const& query, std::source_location location = std::source_location::current());

    /// Executes the given query directly.
    [[nodiscard]] SqlResultCursor ExecuteDirect(SqlQueryObject auto const& query,
                                                std::source_location location = std::source_location::current());

    /// Executes @p query and prepares bulk row-array fetching with up to @p arrayDepth rows per
    /// SQLFetchScroll round-trip.
    ///
    /// This is a fast-path alternative to the per-cell SQLGetData loop used by the regular result
    /// cursor: it binds one contiguous buffer per result column and materializes whole row blocks
    /// per ODBC round-trip. Only fixed-stride column types are supported (integers, floating point,
    /// and bounded character columns). LOB / unbounded columns (varchar(max)/text/varbinary(max))
    /// are rejected by the returned cursor's construction.
    ///
    /// @param query The SQL query to execute.
    /// @param arrayDepth Maximum number of rows materialized per SQLFetchScroll call (must be > 0).
    /// @return A RowArrayCursor bound to this statement's result set.
    [[nodiscard]] LIGHTWEIGHT_API RowArrayCursor ExecuteBatchFetch(std::string_view query, std::size_t arrayDepth);

    /// Executes an SQL migration query, as created b the callback.
    template <typename Callable>
        requires std::invocable<Callable, SqlMigrationQueryBuilder&>
    void MigrateDirect(Callable const& callable, std::source_location location = std::source_location::current());

    /// Executes the given query, assuming that only one result row and column is affected, that one will be
    /// returned.
    template <typename T>
        requires(!std::same_as<T, SqlVariant>)
    [[nodiscard]] std::optional<T> ExecuteDirectScalar(std::string_view const& query,
                                                       std::source_location location = std::source_location::current());

    /// Executes the given query and returns the single result as an SqlVariant.
    template <typename T>
        requires(std::same_as<T, SqlVariant>)
    [[nodiscard]] T ExecuteDirectScalar(std::string_view const& query,
                                        std::source_location location = std::source_location::current());

    /// Executes the given query, assuming that only one result row and column is affected, that one will be
    /// returned.
    template <typename T>
        requires(!std::same_as<T, SqlVariant>)
    [[nodiscard]] std::optional<T> ExecuteDirectScalar(SqlQueryObject auto const& query,
                                                       std::source_location location = std::source_location::current());

    /// Executes the given query object and returns the single result as an SqlVariant.
    template <typename T>
        requires(std::same_as<T, SqlVariant>)
    [[nodiscard]] T ExecuteDirectScalar(SqlQueryObject auto const& query,
                                        std::source_location location = std::source_location::current());

    /// Retrieves the last insert ID of the given table.
    [[nodiscard]] LIGHTWEIGHT_API size_t LastInsertId(std::string_view tableName);

  private:
    friend class SqlResultCursor;
    friend class RowArrayCursor;

    [[nodiscard]] LIGHTWEIGHT_API size_t NumRowsAffected() const;
    [[nodiscard]] LIGHTWEIGHT_API size_t NumColumnsAffected() const;
    [[nodiscard]] LIGHTWEIGHT_API bool FetchRow();
    [[nodiscard]] LIGHTWEIGHT_API std::expected<bool, SqlErrorInfo> TryFetchRow(
        std::source_location location = std::source_location::current()) noexcept;
    void CloseCursor() noexcept;

    /// @brief Binds the given output column variables to the result columns of this statement.
    /// @tparam Args ODBC-bindable output column types.
    /// @param args Pointers to caller-owned storage for each result column, in order.
    template <SqlOutputColumnBinder... Args>
    void BindOutputColumns(Args*... args);

    /// @brief Binds the members of @p records to the result columns of this statement
    /// in declaration order, via reflection.
    /// @tparam Records Aggregate record types whose members map to result columns.
    /// @param records Pointers to caller-owned record instances.
    template <typename... Records>
        requires(((std::is_class_v<Records> && std::is_aggregate_v<Records>) && ...))
    void BindOutputColumnsToRecord(Records*... records);

    /// @brief Binds a single output column variable to the result column at @p columnIndex.
    /// @tparam T An ODBC-bindable output column type.
    /// @param columnIndex 1-based result column index.
    /// @param arg Pointer to caller-owned storage for the column value.
    template <SqlOutputColumnBinder T>
    void BindOutputColumn(SQLUSMALLINT columnIndex, T* arg);

    template <SqlGetColumnNativeType T>
    [[nodiscard]] bool GetColumn(SQLUSMALLINT column, T* result) const;

    template <SqlGetColumnNativeType T>
    [[nodiscard]] T GetColumn(SQLUSMALLINT column) const;

    /// @brief Native row-wise batch execution: binds each column in place over @p rows and submits the
    /// whole batch in a single @c SQLExecute. Precondition: every column is row-bindable.
    template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
    [[nodiscard]] SqlResultCursor ExecuteBatchNativeRowWise(Rows const& rows, ColumnAccessors const&... accessors);

    /// @brief Soft row-major batch execution: binds and executes each row individually. Works for every
    /// supported column type and is the fallback when native row-wise binding does not apply.
    template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
    [[nodiscard]] SqlResultCursor ExecuteBatchSoftRowMajor(Rows const& rows, ColumnAccessors const&... accessors);

    template <SqlGetColumnNativeType T>
    [[nodiscard]] std::optional<T> GetNullableColumn(SQLUSMALLINT column) const;

    template <SqlGetColumnNativeType T>
    [[nodiscard]] T GetColumnOr(SQLUSMALLINT column, T&& defaultValue) const;

    LIGHTWEIGHT_API void RequireSuccess(SQLRETURN error,
                                        std::source_location sourceLocation = std::source_location::current()) const;
    LIGHTWEIGHT_API void PlanPostExecuteCallback(std::function<void()>&& cb) override;
    LIGHTWEIGHT_API void PlanPostProcessOutputColumn(std::function<void()>&& cb) override;
    [[nodiscard]] LIGHTWEIGHT_API SqlServerType ServerType() const noexcept override;
    [[nodiscard]] LIGHTWEIGHT_API std::string const& DriverName() const noexcept override;
    LIGHTWEIGHT_API void ProcessPostExecuteCallbacks();

    LIGHTWEIGHT_API SQLLEN* ProvideInputIndicator() override;
    LIGHTWEIGHT_API SQLLEN* ProvideInputIndicators(size_t rowCount) override;
    LIGHTWEIGHT_API std::byte* ProvideBatchStagingBuffer(std::size_t byteCount) override;
    LIGHTWEIGHT_API void ClearBatchIndicators();
    /// Restores single-row, column-bound parameter binding (the ODBC default). @c noexcept so it can run
    /// from a scope guard on the native-batch exception path.
    LIGHTWEIGHT_API void ResetParameterArrayBinding() noexcept;
    /// Throws unless @p result is a success code or @c SQL_NO_DATA (a searched UPDATE/DELETE that matched
    /// no rows). Mirrors @c Execute() so the batch execute paths tolerate zero-row updates.
    LIGHTWEIGHT_API void RequireExecuteSucceededOrNoData(
        SQLRETURN result, std::source_location sourceLocation = std::source_location::current()) const;
    /// Native-batch execute check: tolerates @c SQL_NO_DATA and, on success, verifies the driver
    /// processed all @p expectedCount parameter sets (guards against silent partial array execution).
    LIGHTWEIGHT_API void RequireSuccessfulBatchExecute(
        SQLRETURN result,
        SQLULEN processedCount,
        SQLULEN expectedCount,
        std::source_location sourceLocation = std::source_location::current()) const;
    LIGHTWEIGHT_API void RequireIndicators();
    LIGHTWEIGHT_API SQLLEN* GetIndicatorForColumn(SQLUSMALLINT column) noexcept;

    // private data members
    struct Data;
    std::unique_ptr<Data, void (*)(Data*)> m_data; // The private data of the statement
    SqlConnection* m_connection {};                // Pointer to the connection object
    SQLHSTMT m_hStmt {};                           // The native oDBC statement handle
    std::string m_preparedQuery;                   // The last prepared query
    std::optional<SQLSMALLINT> m_numColumns;       // The number of columns in the result set, if known
    SQLSMALLINT m_expectedParameterCount {};       // The number of parameters expected by the query
};

/// API for reading an SQL query result set.
class [[nodiscard]] SqlResultCursor
{
  public:
    /// Constructs a result cursor for the given SQL statement.
    explicit LIGHTWEIGHT_FORCE_INLINE SqlResultCursor(SqlStatement& stmt) noexcept:
        m_stmt { &stmt }
    {
    }

    SqlResultCursor() = delete;
    SqlResultCursor(SqlResultCursor const&) = delete;
    SqlResultCursor& operator=(SqlResultCursor const&) = delete;

    /// Move constructor.
    constexpr SqlResultCursor(SqlResultCursor&& other) noexcept:
        m_stmt { other.m_stmt }
    {
        other.m_stmt = nullptr;
    }

    /// Move assignment operator.
    constexpr SqlResultCursor& operator=(SqlResultCursor&& other) noexcept
    {
        if (this != &other)
        {
            m_stmt = other.m_stmt;
            other.m_stmt = nullptr;
        }
        return *this;
    }

    LIGHTWEIGHT_FORCE_INLINE ~SqlResultCursor()
    {
        if (m_stmt)
        {
            m_stmt->CloseCursor();
            m_stmt = nullptr;
        }
    }

    /// Retrieves the number of rows affected by the last query.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE size_t NumRowsAffected() const
    {
        return m_stmt->NumRowsAffected();
    }

    /// Retrieves the number of columns affected by the last query.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE size_t NumColumnsAffected() const
    {
        return m_stmt->NumColumnsAffected();
    }

    /// Binds the given arguments to the prepared statement to store the fetched data to.
    ///
    /// The statement must be prepared before calling this function.
    template <SqlOutputColumnBinder... Args>
    LIGHTWEIGHT_FORCE_INLINE void BindOutputColumns(Args*... args)
    {
        m_stmt->BindOutputColumns(args...);
    }

    /// Binds a single output column at the given index to store fetched data.
    template <SqlOutputColumnBinder T>
    LIGHTWEIGHT_FORCE_INLINE void BindOutputColumn(SQLUSMALLINT columnIndex, T* arg)
    {
        m_stmt->BindOutputColumn(columnIndex, arg);
    }

    /// Fetches the next row of the result set.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE bool FetchRow()
    {
        return m_stmt->FetchRow();
    }

    /// Attempts to fetch the next row, returning an error info on failure instead of throwing.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::expected<bool, SqlErrorInfo> TryFetchRow(
        std::source_location location = std::source_location::current()) noexcept
    {
        return m_stmt->TryFetchRow(location);
    }

    /// Binds the given records to the prepared statement to store the fetched data to.
    template <typename... Records>
        requires(((std::is_class_v<Records> && std::is_aggregate_v<Records>) && ...))
    LIGHTWEIGHT_FORCE_INLINE void BindOutputColumnsToRecord(Records*... records)
    {
        m_stmt->BindOutputColumnsToRecord(records...);
    }

    /// Retrieves the value of the column at the given index for the currently selected row.
    ///
    /// Returns true if the value is not NULL, false otherwise.
    template <SqlGetColumnNativeType T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE bool GetColumn(SQLUSMALLINT column, T* result) const
    {
        return m_stmt->GetColumn<T>(column, result);
    }

    /// Retrieves the value of the column at the given index for the currently selected row.
    template <SqlGetColumnNativeType T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE T GetColumn(SQLUSMALLINT column) const
    {
        return m_stmt->GetColumn<T>(column);
    }

    /// Retrieves the value of the column at the given index for the currently selected row.
    ///
    /// If the value is NULL, std::nullopt is returned.
    template <SqlGetColumnNativeType T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<T> GetNullableColumn(SQLUSMALLINT column) const
    {
        return m_stmt->GetNullableColumn<T>(column);
    }

    /// Retrieves the value of the column at the given index for the currently selected row.
    ///
    /// If the value is NULL, the given @p defaultValue is returned.
    template <SqlGetColumnNativeType T>
    [[nodiscard]] T GetColumnOr(SQLUSMALLINT column, T&& defaultValue) const
    {
        return m_stmt->GetColumnOr(column, std::forward<T>(defaultValue));
    }

  private:
    SqlStatement* m_stmt;
};

/// @brief Thrown by RowArrayCursor's constructor when the executed result set cannot be fixed-stride
/// array-bound.
///
/// Raised for an unbounded/LOB or over-wide character column (e.g. a column the driver reports
/// as SQL_LONGVARCHAR with no size, common for SQLite's dynamically-typed columns), or a query that
/// produced no result columns. It is a precondition signal, not a database error, so callers that
/// use bulk array-fetch purely as an optimization should catch it and fall back to the single-row
/// path. Distinct from SqlException so transient-error retry logic does not mistake it for one.
class RowArrayCursorUnsupported: public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

/// @brief A cursor that fetches result rows in bulk (ODBC row-array binding) for fast column reads.
///
/// Created via @ref SqlStatement::ExecuteBatchFetch. Instead of issuing one SQLGetData per cell,
/// this cursor binds a contiguous buffer per result column and lets the driver materialize whole
/// blocks of rows per SQLFetchScroll round-trip — eliminating per-cell driver round-trips.
///
/// Supported (fixed-stride) column types, decided per column from SQLDescribeCol:
///  - integer SQL types (SQL_BIT, SQL_TINYINT, SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT)
///    are bound as SQL_C_SBIGINT (an int64 buffer);
///  - floating SQL types (SQL_REAL, SQL_FLOAT, SQL_DOUBLE) are bound as SQL_C_DOUBLE;
///  - all other types (char/varchar/decimal/date/time/timestamp/numeric/...) are bound as
///    SQL_C_CHAR with a per-column buffer sized from the reported column size (plus a margin,
///    capped at @ref RowArrayCursor::MaxCharColumnBytes).
///
/// LOB / unbounded columns (the driver reports column size 0 or an absurdly large size) are
/// rejected: constructing the cursor throws std::runtime_error. Such columns must use the
/// single-row SQLGetData fallback instead.
///
/// The cursor is non-copyable and non-movable: it owns the ODBC statement's array-binding state for
/// its entire lifetime. The constructor binds raw pointers into its own members
/// (SQL_ATTR_ROWS_FETCHED_PTR, SQL_ATTR_ROW_STATUS_PTR) and SQLBindCol into its per-column buffers,
/// so the object must not be relocated after construction — a move would leave the statement handle
/// pointing at the moved-from storage (use-after-free). It is constructed in place via
/// @ref SqlStatement::ExecuteBatchFetch (guaranteed copy elision) and used as a local. The bound
/// buffers must outlive the SQLBindCol binding until fetching completes. Cell indices are 1-based to
/// match SqlResultCursor::GetColumn.
class [[nodiscard]] RowArrayCursor
{
  public:
    /// Maximum byte width allocated for a single bound character column (per row). Columns whose
    /// reported size exceeds this are treated as unbounded/LOB and rejected.
    static constexpr std::size_t MaxCharColumnBytes = 8192;

    /// Per-cursor byte budget for the bound column buffers. The effective array depth is
    /// clamp(budget / row-byte-width, MinArrayDepth, requested depth), so wide tables (many or
    /// large character columns) bind fewer rows per round-trip instead of exhausting memory —
    /// the footprint otherwise multiplies across workers x columns x depth on real schemas.
    static constexpr std::size_t MemoryBudgetBytes = 4 * 1024 * 1024;

    /// Lower bound for the budget-adapted array depth, so bulk fetch always makes progress even
    /// on extremely wide rows (never reduced below this unless the caller requested less).
    static constexpr std::size_t MinArrayDepth = 16;

    RowArrayCursor() = delete;
    RowArrayCursor(RowArrayCursor const&) = delete;
    RowArrayCursor& operator=(RowArrayCursor const&) = delete;
    RowArrayCursor(RowArrayCursor&&) = delete;
    RowArrayCursor& operator=(RowArrayCursor&&) = delete;

    /// @brief Constructs the cursor on a statement whose query has already been executed.
    /// Inspects the result columns via SQLDescribeCol, allocates per-column buffers, and binds
    /// them with the row-array statement attributes.
    /// @param stmt The executed statement (must outlive the cursor).
    /// @param arrayDepth Maximum number of rows materialized per FetchArray() (must be > 0). The
    ///                   effective depth may be reduced to fit MemoryBudgetBytes (see ArrayDepth()).
    LIGHTWEIGHT_API RowArrayCursor(SqlStatement& stmt, std::size_t arrayDepth);

    /// @brief Resets the statement's row-array attributes and unbinds the columns so the handle
    /// can be safely reused.
    LIGHTWEIGHT_API ~RowArrayCursor() noexcept;

    /// @brief Fetches the next block of rows into the bound buffers.
    /// @return The number of rows materialized (0 at end of result set).
    [[nodiscard]] LIGHTWEIGHT_API std::size_t FetchArray();

    /// @brief The number of result columns.
    [[nodiscard]] LIGHTWEIGHT_API std::size_t ColumnCount() const noexcept;

    /// @brief The effective maximum number of rows per FetchArray() — the requested depth, possibly
    /// reduced so the bound buffers fit MemoryBudgetBytes (never below MinArrayDepth unless the
    /// caller requested less).
    [[nodiscard]] LIGHTWEIGHT_API std::size_t ArrayDepth() const noexcept;

    /// @brief Reads an integer cell from the last fetched block.
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<std::int64_t> GetI64(std::size_t rowInBatch, SQLUSMALLINT column) const;

    /// @brief Reads a floating-point cell from the last fetched block.
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<double> GetF64(std::size_t rowInBatch, SQLUSMALLINT column) const;

    /// @brief Reads a text cell from the last fetched block, however the driver bound it.
    ///
    /// Narrow-bound cells (SQL_C_CHAR) are returned verbatim — identical bytes to a single-row
    /// SQL_C_CHAR read. Wide-bound cells (the driver reported SQL_WCHAR/SQL_WVARCHAR, e.g. MSSQL
    /// NVARCHAR, or SQLite which reports all text as wide) are converted UTF-16 -> UTF-8; for
    /// valid UTF-8 source data that round-trip is byte-lossless, so the result again matches the
    /// single-row read of the same cell.
    ///
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The UTF-8 value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<std::string> GetString(std::size_t rowInBatch, SQLUSMALLINT column) const;

    /// @brief Reads a DATE cell from the last fetched block. Valid only for Date-bound columns.
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<SqlDate> GetDate(std::size_t rowInBatch, SQLUSMALLINT column) const;

    /// @brief Reads a TIMESTAMP/DATETIME cell from the last fetched block. Valid only for
    /// Timestamp-bound columns.
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<SqlDateTime> GetTimestamp(std::size_t rowInBatch, SQLUSMALLINT column) const;

    /// @brief Reads a GUID cell from the last fetched block. Valid only for Guid-bound columns
    /// (drivers that report SQL_GUID, i.e. MSSQL uniqueidentifier / PostgreSQL uuid).
    /// @param rowInBatch 0-based row offset within the block returned by the last FetchArray().
    /// @param column 1-based result column index.
    /// @return The value, or std::nullopt if the cell is NULL.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<SqlGuid> GetGuid(std::size_t rowInBatch, SQLUSMALLINT column) const;

  private:
    /// How a result column is bound for bulk fetch.
    enum class BoundType : std::uint8_t
    {
        Int64,     //!< bound as SQL_C_SBIGINT into an int64 buffer
        Double,    //!< bound as SQL_C_DOUBLE into a double buffer
        Char,      //!< bound as SQL_C_CHAR into a per-column byte buffer
        WChar,     //!< bound as SQL_C_WCHAR (UTF-16) into a per-column byte buffer
        Date,      //!< bound as SQL_C_TYPE_DATE into a SQL_DATE_STRUCT buffer
        Timestamp, //!< bound as SQL_C_TYPE_TIMESTAMP into a SQL_TIMESTAMP_STRUCT buffer
        Guid,      //!< bound as SQL_C_GUID into a 16-byte GUID buffer
    };

    /// Per-column binding metadata + owning buffers.
    struct BoundColumn
    {
        BoundType type {};              //!< how this column is bound
        std::size_t elementWidth {};    //!< byte stride of one row's value in the buffer
        std::vector<char> buffer;       //!< arrayDepth * elementWidth contiguous bytes
        std::vector<SQLLEN> indicators; //!< arrayDepth length indicators (SQL_NULL_DATA etc.)
    };

    void ResetStatementState() noexcept;

    /// Shared accessor prelude: bounds-checks @p rowInBatch against the last fetched block,
    /// verifies the column is bound as @p expected, and returns the cell's buffer address —
    /// or nullptr when the cell is SQL NULL.
    [[nodiscard]] char const* CheckedCell(std::size_t rowInBatch,
                                          SQLUSMALLINT column,
                                          BoundType expected,
                                          char const* accessorName) const;

    SqlStatement* m_stmt;
    std::size_t m_arrayDepth;
    std::size_t m_lastFetched = 0;
    std::vector<BoundColumn> m_columns;
    SQLULEN m_rowsFetched = 0;
    std::vector<SQLUSMALLINT> m_rowStatus;
};

struct [[nodiscard]] SqlSentinelIterator
{
};

class [[nodiscard]] SqlVariantRowIterator
{
  public:
    explicit SqlVariantRowIterator(SqlSentinelIterator /*sentinel*/) noexcept:
        _cursor { nullptr }
    {
    }

    explicit SqlVariantRowIterator(SqlResultCursor& cursor) noexcept:
        _numResultColumns { static_cast<SQLUSMALLINT>(cursor.NumColumnsAffected()) },
        _cursor { &cursor }
    {
        _row.reserve(_numResultColumns);
        ++(*this);
    }

    SqlVariantRow& operator*() noexcept
    {
        return _row;
    }

    SqlVariantRow const& operator*() const noexcept
    {
        return _row;
    }

    SqlVariantRowIterator& operator++() noexcept
    {
        _end = !_cursor->FetchRow();
        if (!_end)
        {
            _row.clear();
            for (auto const i: std::views::iota(SQLUSMALLINT(1), SQLUSMALLINT(_numResultColumns + 1)))
                _row.emplace_back(_cursor->GetColumn<SqlVariant>(i));
        }
        return *this;
    }

    bool operator!=(SqlSentinelIterator /*sentinel*/) const noexcept
    {
        return !_end;
    }

    bool operator!=(SqlVariantRowIterator const& /*rhs*/) const noexcept
    {
        return !_end;
    }

  private:
    bool _end = false;
    SQLUSMALLINT _numResultColumns = 0;
    SqlResultCursor* _cursor;
    SqlVariantRow _row;
};

class [[nodiscard]] SqlVariantRowCursor
{
  public:
    explicit SqlVariantRowCursor(SqlResultCursor&& cursor):
        _resultCursor { std::move(cursor) }
    {
    }

    SqlVariantRowIterator begin() noexcept
    {
        return SqlVariantRowIterator { _resultCursor };
    }

    static SqlSentinelIterator end() noexcept
    {
        return SqlSentinelIterator {};
    }

  private:
    SqlResultCursor _resultCursor;
};

/// @brief SQL query result row iterator
///
/// Can be used to iterate over rows of the database and fetch them into a record type.
/// @tparam T The record type to fetch the rows into.
/// @code
///
/// struct MyRecord
/// {
///    Field<SqlGuid, PrimaryKey::AutoAssign> field1;
///    Field<int> field2;
///    Field<double> field3;
/// };
///
/// for (auto const& row : SqlRowIterator<MyRecord>(stmt))
/// {
///    // row is of type MyRecord
///    // row.field1, row.field2, row.field3 are accessible
/// }
/// @endcode
template <typename T>
class SqlRowIterator
{
  public:
    /// Constructs a row iterator using the given SQL connection.
    explicit SqlRowIterator(SqlConnection& conn):
        _connection { &conn }
    {
    }

    class iterator
    {
      public:
        using difference_type = bool;
        using value_type = T;

        iterator& operator++()
        {
            if (_cursor)
            {
                _is_end = !_cursor->FetchRow();
                return *this;
            }
            _is_end = true;
            return *this;
        }

        LIGHTWEIGHT_FORCE_INLINE value_type operator*() noexcept
        {
            auto res = T {};

            EnumerateRecordMembers(res, [this]<size_t I>(auto&& value) {
                auto tmp = _cursor->GetColumn<typename RecordMemberTypeOf<I, value_type>::ValueType>(I + 1);
                value = tmp;
            });

            return res;
        }

        LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(iterator const& other) const noexcept
        {
            return _is_end != other._is_end;
        }

        constexpr iterator(std::default_sentinel_t /*sentinel*/) noexcept:
            _is_end { true },
            _cursor { std::nullopt }
        {
        }

        explicit iterator(SqlConnection& conn):
            _stmt { std::make_unique<SqlStatement>(conn) },
            _cursor { std::nullopt }
        {
        }

        LIGHTWEIGHT_FORCE_INLINE SqlStatement& Statement() noexcept
        {
            return *_stmt;
        }

        void SetCursor(SqlResultCursor cursor) noexcept
        {
            _cursor.emplace(std::move(cursor));
        }

      private:
        bool _is_end = false;
        std::unique_ptr<SqlStatement> _stmt;
        std::optional<SqlResultCursor> _cursor;
    };

    /// Returns an iterator to the first row of the result set.
    iterator begin()
    {
        auto it = iterator { *_connection };
        auto& stmt = it.Statement();
        stmt.Prepare(it.Statement().Query(RecordTableName<T>).Select().template Fields<T>().All());
        it.SetCursor(stmt.Execute());
        ++it;
        return it;
    }

    /// Returns a sentinel iterator representing the end of the result set.
    iterator end() noexcept
    {
        return iterator { std::default_sentinel };
    }

  private:
    SqlConnection* _connection;
};

// {{{ inline implementation
inline LIGHTWEIGHT_FORCE_INLINE bool SqlStatement::IsAlive() const noexcept
{
    return m_connection && m_connection->IsAlive() && m_hStmt != nullptr;
}

inline LIGHTWEIGHT_FORCE_INLINE bool SqlStatement::IsPrepared() const noexcept
{
    return !m_preparedQuery.empty();
}

inline LIGHTWEIGHT_FORCE_INLINE SqlConnection& SqlStatement::Connection() noexcept
{
    return *m_connection;
}

inline LIGHTWEIGHT_FORCE_INLINE SqlConnection const& SqlStatement::Connection() const noexcept
{
    return *m_connection;
}

inline LIGHTWEIGHT_FORCE_INLINE SqlErrorInfo SqlStatement::LastError() const
{
    return SqlErrorInfo::FromStatementHandle(m_hStmt);
}

inline LIGHTWEIGHT_FORCE_INLINE SQLHSTMT SqlStatement::NativeHandle() const noexcept
{
    return m_hStmt;
}

inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::Prepare(SqlQueryObject auto const& queryObject) &
{
    Prepare(queryObject.ToSql());
}

inline LIGHTWEIGHT_FORCE_INLINE SqlStatement SqlStatement::Prepare(SqlQueryObject auto const& queryObject) &&
{
    return Prepare(queryObject.ToSql());
}

inline LIGHTWEIGHT_FORCE_INLINE std::string const& SqlStatement::PreparedQuery() const noexcept
{
    return m_preparedQuery;
}

/// @brief Out-of-line definition of `SqlStatement::BindOutputColumns`.
template <SqlOutputColumnBinder... Args>
inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::BindOutputColumns(Args*... args)
{
    RequireIndicators();

    SQLUSMALLINT i = 0;
    ((++i, RequireSuccess(SqlDataBinder<Args>::OutputColumn(m_hStmt, i, args, GetIndicatorForColumn(i), *this))), ...);
}

template <typename... Records>
    requires(((std::is_class_v<Records> && std::is_aggregate_v<Records>) && ...))
void SqlStatement::BindOutputColumnsToRecord(Records*... records)
{
    RequireIndicators();

    SQLUSMALLINT i = 0;
    ((EnumerateRecordMembers(*records,
                             [this, &i]<size_t I, typename FieldType>(FieldType& value) {
                                 ++i;
                                 RequireSuccess(SqlDataBinder<FieldType>::OutputColumn(
                                     m_hStmt, i, &value, GetIndicatorForColumn(i), *this));
                             })),
     ...);
}

/// @brief Out-of-line definition of `SqlStatement::BindOutputColumn`.
template <SqlOutputColumnBinder T>
inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::BindOutputColumn(SQLUSMALLINT columnIndex, T* arg)
{
    RequireIndicators();

    RequireSuccess(SqlDataBinder<T>::OutputColumn(m_hStmt, columnIndex, arg, GetIndicatorForColumn(columnIndex), *this));
}

/// @copydoc SqlStatement::BindInputParameter(SQLSMALLINT, Arg const&)
template <SqlInputParameterBinder Arg>
inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::BindInputParameter(SQLSMALLINT columnIndex, Arg const& arg)
{
    // tell Execute() that we don't know the expected count
    m_expectedParameterCount = (std::numeric_limits<decltype(m_expectedParameterCount)>::max)();
    RequireSuccess(SqlDataBinder<Arg>::InputParameter(m_hStmt, static_cast<SQLUSMALLINT>(columnIndex), arg, *this));
}

/// @copydoc SqlStatement::BindInputParameter(SQLSMALLINT, Arg const&, ColumnName&&)
template <SqlInputParameterBinder Arg, typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::BindInputParameter(SQLSMALLINT columnIndex,
                                                                      Arg const& arg,
                                                                      ColumnName&& columnNameHint)
{
    SqlLogger::GetLogger().OnBindInputParameter(std::forward<ColumnName>(columnNameHint), arg);
    BindInputParameter(columnIndex, arg);
}

template <SqlInputParameterBinder... Args>
SqlResultCursor SqlStatement::Execute(Args const&... args)
{
    // Each input parameter must have an address,
    // such that we can call SQLBindParameter() without needing to copy it.
    // The memory region behind the input parameter must exist until the SQLExecute() call.

    ZoneScopedN("SqlStatement::Execute");
    ZoneTextObject(m_preparedQuery);
    SqlLogger::GetLogger().OnExecute(m_preparedQuery);

    if (!(m_expectedParameterCount == (std::numeric_limits<decltype(m_expectedParameterCount)>::max)()
          && sizeof...(args) == 0)
        && !(m_expectedParameterCount == sizeof...(args)))
        throw std::invalid_argument { "Invalid argument count" };

    SQLUSMALLINT i = 0;
    ((++i,
      SqlLogger::GetLogger().OnBindInputParameter({}, args),
      RequireSuccess(SqlDataBinder<Args>::InputParameter(m_hStmt, i, args, *this))),
     ...);

    auto const result = SQLExecute(m_hStmt);

    if (result != SQL_NO_DATA && result != SQL_SUCCESS && result != SQL_SUCCESS_WITH_INFO)
        throw SqlException(SqlErrorInfo::FromStatementHandle(m_hStmt), std::source_location::current());

    ProcessPostExecuteCallbacks();
    return SqlResultCursor { *this };
}

// clang-format off
template <typename T>
concept SqlNativeContiguousValueConcept =
       std::same_as<T, bool>
    || std::same_as<T, char>
    || std::same_as<T, unsigned char>
    || std::same_as<T, wchar_t>
    || std::same_as<T, std::int16_t>
    || std::same_as<T, std::uint16_t>
    || std::same_as<T, std::int32_t>
    || std::same_as<T, std::uint32_t>
    || std::same_as<T, std::int64_t>
    || std::same_as<T, std::uint64_t>
    || std::same_as<T, float>
    || std::same_as<T, double>
    || std::same_as<T, SqlDate>
    || std::same_as<T, SqlTime>
    || std::same_as<T, SqlDateTime>
    || std::same_as<T, SqlFixedString<T::Capacity, typename T::value_type, T::PostRetrieveOperation>>;

template <typename FirstColumnBatch, typename... MoreColumnBatches>
concept SqlNativeBatchable =
        std::ranges::contiguous_range<FirstColumnBatch>
    && (std::ranges::contiguous_range<MoreColumnBatches> && ...)
    &&  SqlNativeContiguousValueConcept<std::ranges::range_value_t<FirstColumnBatch>>
    && (SqlNativeContiguousValueConcept<std::ranges::range_value_t<MoreColumnBatches>> && ...);

// clang-format on

/// @brief A value type that can be bound in a native ODBC row-wise parameter array (fixed-width,
/// inline, indicator-free, bound identically across backends). Backed by the data-driven
/// @c SqlIsNativeRowBindableValue trait that each eligible binder header opts into.
template <typename V>
concept SqlNativeRowBindableValue = SqlIsNativeRowBindableValue<V>;

/// @brief A @c std::optional column that can be bound zero-copy in a native row-wise batch: the
/// contained type is row-bindable and non-numeric (numeric optionals are not bound at a uniform
/// offset/representation across backends and therefore use the soft path).
template <typename V>
concept SqlOptionalRowBindable =
    SqlIsStdOptional<V> && SqlNativeRowBindableValue<typename V::value_type> && !SqlIsNumericValue<typename V::value_type>;

/// @brief A column value type usable on the native row-wise batch path — either a row-bindable fixed
/// value or a row-bindable optional of one.
template <typename V>
concept SqlRowBindableColumn = SqlNativeRowBindableValue<V> || SqlOptionalRowBindable<V>;

/// @brief Whether @p V's binder provides a row-wise batch entry point (@c BatchRowWiseInputParameter).
///
/// Such types (e.g. @c std::optional of a fixed type, or inline fixed-capacity strings) need a
/// temporary row-strided NULL/length indicator buffer, which in turn requires the row stride to keep
/// @c SQLLEN indicator slots aligned. Plain indicator-free fixed values bind via @c InputParameter and
/// do not satisfy this concept.
template <typename V>
concept SqlHasRowWiseBatchBinder =
    requires(SQLHSTMT stmt, SQLUSMALLINT column, V const* elem0, std::size_t n, SqlDataBinderCallback& cb) {
        { SqlDataBinder<V>::BatchRowWiseInputParameter(stmt, column, elem0, n, n, cb) } -> std::same_as<SQLRETURN>;
    };

template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::contiguous_range... MoreColumnBatches>
SqlResultCursor SqlStatement::ExecuteBatchNative(FirstColumnBatch const& firstColumnBatch,
                                                 MoreColumnBatches const&... moreColumnBatches)
{
    static_assert(SqlNativeBatchable<FirstColumnBatch, MoreColumnBatches...>,
                  "Must be a supported native contiguous element type.");

    ZoneScopedN("SqlStatement::ExecuteBatchNative");
    ZoneTextObject(m_preparedQuery);

    if (m_expectedParameterCount != 1 + sizeof...(moreColumnBatches))
        throw std::invalid_argument { "Invalid number of columns" };

    auto const rowCount = std::ranges::size(firstColumnBatch);
    ZoneValue(rowCount);
    if (!((std::size(moreColumnBatches) == rowCount) && ...))
        throw std::invalid_argument { "Uneven number of rows" };

    size_t rowStart = 0;

    // clang-format off
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER) rowCount, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_OFFSET_PTR, &rowStart, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_OPERATION_PTR, SQL_PARAM_PROCEED, 0));
    ClearBatchIndicators();
    RequireSuccess(SqlDataBinder<std::remove_cvref_t<decltype(*std::ranges::data(firstColumnBatch))>>::
                       BatchInputParameter(m_hStmt, 1, std::ranges::data(firstColumnBatch), rowCount, *this));
    SQLUSMALLINT column = 1;
    (RequireSuccess(SqlDataBinder<std::remove_cvref_t<decltype(*std::ranges::data(moreColumnBatches))>>::
                        BatchInputParameter(m_hStmt, ++column, std::ranges::data(moreColumnBatches), rowCount, *this)),
     ...);
    RequireSuccess(SQLExecute(m_hStmt));
    ProcessPostExecuteCallbacks();
    // clang-format on
    return SqlResultCursor { *this };
}

/// @copydoc SqlStatement::ExecuteBatch
template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::range... MoreColumnBatches>
inline LIGHTWEIGHT_FORCE_INLINE SqlResultCursor SqlStatement::ExecuteBatch(FirstColumnBatch const& firstColumnBatch,
                                                                           MoreColumnBatches const&... moreColumnBatches)
{
    // If the input ranges are contiguous and their element types are contiguous and supported as well,
    // we can use the native batch execution.
    if constexpr (SqlNativeBatchable<FirstColumnBatch, MoreColumnBatches...>)
        return ExecuteBatchNative(firstColumnBatch, moreColumnBatches...);
    else
        return ExecuteBatchSoft(firstColumnBatch, moreColumnBatches...);
}

template <SqlInputParameterBatchBinder FirstColumnBatch, std::ranges::range... MoreColumnBatches>
SqlResultCursor SqlStatement::ExecuteBatchSoft(FirstColumnBatch const& firstColumnBatch,
                                               MoreColumnBatches const&... moreColumnBatches)
{
    ZoneScopedN("SqlStatement::ExecuteBatchSoft");
    ZoneTextObject(m_preparedQuery);

    if (m_expectedParameterCount != 1 + sizeof...(moreColumnBatches))
        throw std::invalid_argument { "Invalid number of columns" };

    auto const rowCount = std::ranges::size(firstColumnBatch);
    ZoneValue(rowCount);
    if (!((std::size(moreColumnBatches) == rowCount) && ...))
        throw std::invalid_argument { "Uneven number of rows" };

    for (auto const rowIndex: std::views::iota(size_t { 0 }, rowCount))
    {
        std::apply(
            [&]<SqlInputParameterBinder... ColumnValues>(ColumnValues const&... columnsInRow) {
                SQLUSMALLINT column = 0;
                ((++column, SqlDataBinder<ColumnValues>::InputParameter(m_hStmt, column, columnsInRow, *this)), ...);
                RequireSuccess(SQLExecute(m_hStmt));
                ProcessPostExecuteCallbacks();
            },
            std::make_tuple(
                std::ref(*std::ranges::next(std::ranges::begin(firstColumnBatch), static_cast<std::ptrdiff_t>(rowIndex))),
                std::ref(
                    *std::ranges::next(std::ranges::begin(moreColumnBatches), static_cast<std::ptrdiff_t>(rowIndex)))...));
    }
    return SqlResultCursor { *this };
}

template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
    requires(sizeof...(ColumnAccessors) >= 1
             && (std::invocable<ColumnAccessors const&, std::ranges::range_value_t<Rows> const&> && ...))
SqlResultCursor SqlStatement::ExecuteBatch(Rows const& rows, ColumnAccessors const&... accessors)
{
    ZoneScopedN("SqlStatement::ExecuteBatch(row-major)");
    ZoneTextObject(m_preparedQuery);

    using RowElem = std::ranges::range_value_t<Rows>;

    auto const rowCount = std::ranges::size(rows);
    if (rowCount == 0)
        return SqlResultCursor { *this };

    if (m_expectedParameterCount != static_cast<SQLSMALLINT>(sizeof...(accessors)))
        throw std::invalid_argument { "Invalid number of columns" };

    // Compile-time eligibility for the native row-wise path: every column must be row-bindable, every
    // accessor must return an lvalue reference (so the bound address is a stable subobject), and — when
    // any column needs a row-strided indicator (optionals, inline fixed-capacity strings) — the row
    // stride must keep SQLLEN indicator slots aligned and non-overlapping.
    constexpr bool allColumnsRowBindable =
        (SqlRowBindableColumn<std::remove_cvref_t<std::invoke_result_t<ColumnAccessors const&, RowElem const&>>> && ...);
    constexpr bool allAccessorsReturnReference =
        (std::is_reference_v<std::invoke_result_t<ColumnAccessors const&, RowElem const&>> && ...);
    constexpr bool anyStridedIndicatorColumn =
        (SqlHasRowWiseBatchBinder<std::remove_cvref_t<std::invoke_result_t<ColumnAccessors const&, RowElem const&>>> || ...);
    constexpr bool indicatorAlignmentSatisfied = (sizeof(RowElem) % alignof(SQLLEN)) == 0;

    if constexpr (allColumnsRowBindable && allAccessorsReturnReference
                  && (!anyStridedIndicatorColumn || indicatorAlignmentSatisfied))
    {
        auto const* rowData = std::ranges::data(rows);

        // Runtime guard: confirm each accessor yields a constant-offset subobject (stride == sizeof row),
        // so binding row 0's address and striding by sizeof(RowElem) addresses every row correctly.
        auto const accessorStrideMatchesRow = [&](auto const& accessor) noexcept -> bool {
            auto const* first = reinterpret_cast<std::byte const*>(std::addressof(accessor(rowData[0])));
            auto const* second = reinterpret_cast<std::byte const*>(std::addressof(accessor(rowData[1])));
            return static_cast<std::size_t>(second - first) == sizeof(RowElem);
        };
        bool const rowStrideOk = rowCount < 2 || (accessorStrideMatchesRow(accessors) && ...);

        if (m_connection->SupportsNativeRowBatch() && rowStrideOk)
            return ExecuteBatchNativeRowWise(rows, accessors...);
    }

    return ExecuteBatchSoftRowMajor(rows, accessors...);
}

template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
SqlResultCursor SqlStatement::ExecuteBatchNativeRowWise(Rows const& rows, ColumnAccessors const&... accessors)
{
    ZoneScopedN("SqlStatement::ExecuteBatchNativeRowWise");
    ZoneTextObject(m_preparedQuery);

    using RowElem = std::ranges::range_value_t<Rows>;
    auto const rowCount = std::ranges::size(rows);
    ZoneValue(rowCount);
    auto const* rowData = std::ranges::data(rows);

    // Optimistic init: a driver that ignores SQL_ATTR_PARAMS_PROCESSED_PTR leaves this == rowCount, so the
    // post-execute completeness check never false-trips on such a driver.
    SQLULEN processedCount = rowCount;

    // Restore single-row binding and release scratch buffers on EVERY exit — success or exception — so a
    // throwing bind/execute can never leave the handle in a stale multi-paramset/row-wise state for a
    // later reuse (e.g. a single Execute() without re-Prepare). Installed before the attributes are set,
    // so a failure mid-setup is unwound too.
    auto const restoreParameterBinding = detail::Finally([this] {
        ResetParameterArrayBinding();
        ClearBatchIndicators();
    });

    // Row-wise array binding: the driver strides every bound value and indicator pointer by sizeof(RowElem).
    // clang-format off
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER) rowCount, 0));
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_TYPE, (SQLPOINTER) sizeof(RowElem), 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_BIND_OFFSET_PTR, nullptr, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAM_OPERATION_PTR, SQL_PARAM_PROCEED, 0));
    RequireSuccess(SQLSetStmtAttr(m_hStmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &processedCount, 0));
    // clang-format on

    SQLUSMALLINT column = 0;
    auto const bindColumn = [&](auto const& accessor) {
        ++column;
        using ValueType = std::remove_cvref_t<decltype(accessor(rowData[0]))>;
        // Types needing a per-row indicator (optionals, inline fixed-capacity strings) provide a
        // row-wise batch binder; indicator-free fixed values bind directly via InputParameter.
        if constexpr (SqlHasRowWiseBatchBinder<ValueType>)
            RequireSuccess(SqlDataBinder<ValueType>::BatchRowWiseInputParameter(
                m_hStmt, column, std::addressof(accessor(rowData[0])), sizeof(RowElem), rowCount, *this));
        else
            RequireSuccess(SqlDataBinder<ValueType>::InputParameter(m_hStmt, column, accessor(rowData[0]), *this));
    };
    (bindColumn(accessors), ...);

    SqlLogger::GetLogger().OnExecuteBatch();
    // Capture the result before reading processedCount: SQLExecute updates it via the bound pointer, and
    // function-argument evaluation order is unspecified.
    auto const executeResult = SQLExecute(m_hStmt);
    RequireSuccessfulBatchExecute(executeResult, processedCount, static_cast<SQLULEN>(rowCount));
    ProcessPostExecuteCallbacks();

    return SqlResultCursor { *this };
}

template <std::ranges::contiguous_range Rows, typename... ColumnAccessors>
SqlResultCursor SqlStatement::ExecuteBatchSoftRowMajor(Rows const& rows, ColumnAccessors const&... accessors)
{
    ZoneScopedN("SqlStatement::ExecuteBatchSoftRowMajor");
    ZoneTextObject(m_preparedQuery);

    auto const* rowData = std::ranges::data(rows);
    auto const rowCount = std::ranges::size(rows);
    ZoneValue(rowCount);

    for (auto const rowIndex: std::views::iota(std::size_t { 0 }, rowCount))
    {
        auto const& row = rowData[rowIndex];
        SQLUSMALLINT column = 0;
        ((++column,
          RequireSuccess(SqlDataBinder<std::remove_cvref_t<decltype(accessors(row))>>::InputParameter(
              m_hStmt, column, accessors(row), *this))),
         ...);
        SqlLogger::GetLogger().OnExecute(m_preparedQuery);
        RequireExecuteSucceededOrNoData(SQLExecute(m_hStmt));
        ProcessPostExecuteCallbacks();
    }

    return SqlResultCursor { *this };
}

template <SqlGetColumnNativeType T>
inline bool SqlStatement::GetColumn(SQLUSMALLINT column, T* result) const
{
    SQLLEN indicator {}; // TODO: Handle NULL values if we find out that we need them for our use-cases.
    RequireSuccess(SqlDataBinder<T>::GetColumn(m_hStmt, column, result, &indicator, *this));
    return indicator != SQL_NULL_DATA;
}

namespace detail
{

    template <typename T>
    concept SqlNullableType = (std::same_as<T, SqlVariant> || IsSpecializationOf<std::optional, T>);

} // end namespace detail

template <SqlGetColumnNativeType T>
inline T SqlStatement::GetColumn(SQLUSMALLINT column) const
{
    T result {};
    SQLLEN indicator {};
    {
        // SQLGetData is where the ODBC driver materializes the column value (driver/network I/O).
        // Isolating it lets a profiler separate I/O-bound retrieval from CPU-bound value conversion
        // done by the caller — the key question for deciding what to parallelize.
        ZoneScopedN("SqlStatement::ColumnGetData");
        RequireSuccess(SqlDataBinder<T>::GetColumn(m_hStmt, column, &result, &indicator, *this));
    }
    if constexpr (!detail::SqlNullableType<T>)
        if (indicator == SQL_NULL_DATA)
            throw std::runtime_error { "Column value is NULL" };
    return result;
}

template <SqlGetColumnNativeType T>
inline std::optional<T> SqlStatement::GetNullableColumn(SQLUSMALLINT column) const
{
    T result {};
    SQLLEN indicator {}; // TODO: Handle NULL values if we find out that we need them for our use-cases.
    {
        ZoneScopedN("SqlStatement::ColumnGetData");
        RequireSuccess(SqlDataBinder<T>::GetColumn(m_hStmt, column, &result, &indicator, *this));
    }
    if (indicator == SQL_NULL_DATA)
        return std::nullopt;
    return { std::move(result) };
}

template <SqlGetColumnNativeType T>
T SqlStatement::GetColumnOr(SQLUSMALLINT column, T&& defaultValue) const
{
    return GetNullableColumn<T>(column).value_or(std::forward<T>(defaultValue));
}

inline LIGHTWEIGHT_FORCE_INLINE SqlResultCursor SqlStatement::ExecuteDirect(SqlQueryObject auto const& query,
                                                                            std::source_location location)
{
    return ExecuteDirect(query.ToSql(), location);
}

template <typename Callable>
    requires std::invocable<Callable, SqlMigrationQueryBuilder&>
void SqlStatement::MigrateDirect(Callable const& callable, std::source_location location)
{
    ZoneScopedN("SqlStatement::MigrateDirect");
    auto migration = SqlMigrationQueryBuilder { Connection().QueryFormatter() };
    callable(migration);
    auto const queries = migration.GetPlan().ToSql();
    ZoneValue(queries.size());
    for (auto const& query: queries)
    {
        [[maybe_unused]] auto cursor = ExecuteDirect(query, location);
    }
}

template <typename T>
    requires(!std::same_as<T, SqlVariant>)
inline std::optional<T> SqlStatement::ExecuteDirectScalar(std::string_view const& query, std::source_location location)
{
    auto cursor = ExecuteDirect(query, location);
    RequireSuccess(FetchRow());
    return GetNullableColumn<T>(1);
}

template <typename T>
    requires(std::same_as<T, SqlVariant>)
inline T SqlStatement::ExecuteDirectScalar(std::string_view const& query, std::source_location location)
{
    auto cursor = ExecuteDirect(query, location);
    RequireSuccess(FetchRow());
    if (auto result = GetNullableColumn<T>(1); result.has_value())
        return *result;
    return SqlVariant { SqlNullValue };
}

template <typename T>
    requires(!std::same_as<T, SqlVariant>)
inline std::optional<T> SqlStatement::ExecuteDirectScalar(SqlQueryObject auto const& query, std::source_location location)
{
    return ExecuteDirectScalar<T>(query.ToSql(), location);
}

template <typename T>
    requires(std::same_as<T, SqlVariant>)
inline T SqlStatement::ExecuteDirectScalar(SqlQueryObject auto const& query, std::source_location location)
{
    return ExecuteDirectScalar<T>(query.ToSql(), location);
}

inline LIGHTWEIGHT_FORCE_INLINE void SqlStatement::CloseCursor() noexcept
{
    // SQL Server batches and DML/DDL row-count tokens produce multiple result
    // sets per SQLExecDirect. SQLFreeStmt(SQL_CLOSE) only discards the current
    // cursor — remaining result sets stay pending on the *connection*, and
    // without MARS every subsequent statement on that connection fails with
    // HY000 "Connection is busy with results for another command". Drain via
    // SQLMoreResults until SQL_NO_DATA (or an error), then close.
    //
    // SQLMoreResults is standard ODBC; SQLite and PostgreSQL drivers return
    // SQL_NO_DATA on the first call when nothing is pending, so the cost on
    // single-statement queries is one no-op driver call.
    while (true)
    {
        auto const rc = SQLMoreResults(m_hStmt);
        if (rc == SQL_NO_DATA || !SQL_SUCCEEDED(rc))
            break;
    }
    SQLFreeStmt(m_hStmt, SQL_CLOSE);
    SqlLogger::GetLogger().OnFetchEnd();
}

// }}}

} // namespace Lightweight
