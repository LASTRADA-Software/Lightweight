// SPDX-License-Identifier: Apache-2.0

#pragma once

// See SqlOdbcPrelude.hpp's header comment for why this replaces a direct <Windows.h> include.
#include "../Api.hpp"
#include "../SqlError.hpp"
#include "../SqlOdbcPrelude.hpp"
#include "../SqlServerType.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

namespace Lightweight
{

/// @defgroup DataTypes Data Types
///
/// @brief Special purpose data types for SQL data binding.

/// @brief A step a binder plans to run before every fetch into the output column it binds.
///
/// Plain data rather than a type-erased callable, so planning one never allocates, not even when a
/// column is bound anew for every row. @ref run receives the step itself (so it can update
/// @ref boundData and @ref boundSize) and the statement handle the column is bound on.
struct SqlOutputColumnPreFetch
{
    /// Runs the step. Returns @c SQL_SUCCESS, or the failing ODBC return code (the fetch then fails with it).
    SQLRETURN (*run)(SqlOutputColumnPreFetch& step, SQLHSTMT stmt) = nullptr;
    /// The bound object the step works on.
    void* target = nullptr;
    /// The column's length/indicator buffer.
    SQLLEN* indicator = nullptr;
    /// The storage the column is currently bound to.
    void const* boundData = nullptr;
    /// The element count the column is currently bound with.
    std::size_t boundSize = 0;
    /// The 1-based column index.
    SQLUSMALLINT column = 0;
    /// The C type the column is bound as.
    SQLSMALLINT cType = 0;
};

/// @brief Which declared length of a result column sizes a buffer bound to it.
enum class SqlOutputColumnLength : std::uint8_t
{
    Octets,     //!< @c SQL_DESC_OCTET_LENGTH: for a buffer of bytes (narrow text, binary)
    Characters, //!< @c SQL_DESC_LENGTH: for a buffer of characters (UTF-16 text)
};

namespace detail
{
    /// @brief Elements a growable container is bound with at least, where the driver can read a bound
    /// column again (see @ref SqlDataBinderCallback::MinimumOutputColumnSize).
    inline constexpr std::size_t DefaultBoundColumnSize = 255;

    /// @brief Throws SQLSTATE 22003 for a value of column @p column that the variable it is read into cannot
    /// hold, as a driver that checks the range reports it.
    /// @param column The 1-based column index.
    /// @param value The value as read.
    /// @param lowest The smallest value the variable accepts.
    /// @param highest The largest value the variable accepts.
    template <typename Value, typename Bound>
    [[noreturn]] void ThrowNumericValueOutOfRange(SQLUSMALLINT column, Value value, Bound lowest, Bound highest)
    {
        throw SqlException(SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = "22003",
            .message = std::format(
                "Numeric value out of range: column {} holds {}, outside [{}, {}].", column, value, lowest, highest),
        });
    }

    /// @brief Whether @p serverType's driver reports a cell its column type cannot convert as NULL; forwards to
    /// @c SqlQueryFormatter::ReportsUnconvertibleCellsAsNull.
    /// @param serverType The backend.
    /// @return The formatter's answer; @c false for a backend without a formatter.
    [[nodiscard]] LIGHTWEIGHT_API bool ReportsUnconvertibleCellsAsNull(SqlServerType serverType) noexcept;
} // namespace detail

/// Callback interface for SqlDataBinder to allow post-processing of output columns.
///
/// This is needed because the SQLBindCol() function does not allow to specify a callback function to be called
/// after the data has been fetched from the database. This is needed to trim strings to the correct size, for
/// example.
class LIGHTWEIGHT_API SqlDataBinderCallback
{
  public:
    /// Default constructor.
    SqlDataBinderCallback() = default;
    /// Default move constructor.
    SqlDataBinderCallback(SqlDataBinderCallback&&) = default;
    /// Default copy constructor.
    SqlDataBinderCallback(SqlDataBinderCallback const&) = default;
    /// Default move assignment operator.
    SqlDataBinderCallback& operator=(SqlDataBinderCallback&&) = default;
    /// Default copy assignment operator.
    SqlDataBinderCallback& operator=(SqlDataBinderCallback const&) = default;

    virtual ~SqlDataBinderCallback() = default;

    /// Plans a callback to be called after the statement has been executed.
    ///
    /// @see SqlDataBinder::PostExecute()
    virtual void PlanPostExecuteCallback(std::function<void()>&&) = 0;

    /// Plans a callback to be called after every row fetched into the output column being bound.
    ///
    /// The callback stays registered as long as the column binding: it runs after each fetch until the
    /// column is bound again or its binding is dropped (a new query is prepared or executed directly).
    ///
    /// @see SqlDataBinder::PostProcessOutputColumn()
    virtual void PlanPostProcessOutputColumn(std::function<void()>&&) = 0;

    /// Plans a step to run before every row is fetched into the output column being bound.
    ///
    /// A binder that binds straight into a growable container uses this to restore the bound buffer
    /// that the previous row's post-processing shrank to that row's length, and to re-bind the column
    /// should the container's storage have moved. Steps of one column run in reverse order of
    /// planning, so a wrapping binder (e.g. @c std::optional) prepares its storage before the binder
    /// it wraps. Same lifetime as @ref PlanPostProcessOutputColumn.
    ///
    /// @param step The step; its @c run function reports a failure through its return code, which
    ///             then fails the fetch.
    virtual void PlanPreFetchOutputColumn(SqlOutputColumnPreFetch const& step) = 0;

    /// The size a growable container bound to output column @p column has at least, without terminator.
    ///
    /// A value longer than the bound buffer is read again with @c SQLGetData. Where the driver can do
    /// that for a bound column (@c SQL_GD_BOUND) this is @c detail::DefaultBoundColumnSize. Where it
    /// cannot (MS SQL Server), it is the column's declared length, so every value the column can hold
    /// fits, asked of the driver once per result set and column; a column too long for that (a MAX
    /// column) gets @c detail::DefaultBoundColumnSize, and a longer value fails the fetch.
    ///
    /// @param column The 1-based column index.
    /// @param length Which declared length counts the container's elements.
    /// @return The minimum number of elements to bind.
    [[nodiscard]] virtual std::size_t MinimumOutputColumnSize(SQLUSMALLINT column, SqlOutputColumnLength length)
    {
        (void) column;
        (void) length;
        return detail::DefaultBoundColumnSize;
    }

    /// Provides a pointer to a single indicator for a single input parameter.
    ///
    /// @note The caller is responsible for filling the indicator with the length of the data or
    /// SQL_NULL_DATA.
    /// @note The indicator must remain valid until the statement is executed.
    ///
    /// @return A pointer to the indicator.
    virtual SQLLEN* ProvideInputIndicator() = 0;

    /// Provides a pointer to a contiguous array of indicators for a batch of input parameters.
    ///
    /// @note The caller is responsible for filling the indicators with the lengths of the data or
    /// SQL_NULL_DATA.
    /// @note The indicators must remain valid until the statement is executed.
    ///
    /// @param rowCount The number of rows in the batch.
    /// @return A pointer to the first element of the indicator array.
    virtual SQLLEN* ProvideInputIndicators(size_t rowCount) = 0;

    /// Provides a pointer to a contiguous, suitably aligned temporary byte buffer that remains valid
    /// until the statement is executed.
    ///
    /// This is used by batch input parameter binders that need scratch storage whose lifetime must
    /// outlive the bind call but not the execution — for example a row-strided NULL/length indicator
    /// array used by native row-wise array binding of @c std::optional columns.
    ///
    /// @note The buffer contents are unspecified; the caller is responsible for initializing it.
    /// @note The returned storage is aligned to at least @c alignof(std::max_align_t).
    /// @note Row-wise callers request @c ~rowStride*rowCount bytes to hold only @c rowCount @c SQLLEN
    /// indicators. This over-allocation is intrinsic to ODBC row-wise binding, which strides the
    /// @c StrLen_or_IndPtr array by @c SQL_ATTR_PARAM_BIND_TYPE (the row stride) — there is no separate
    /// indicator stride — so a tightly packed indicator array would require descriptor-level binding.
    ///
    /// @param byteCount The number of bytes to provide.
    /// @return A pointer to the first byte of the buffer.
    virtual std::byte* ProvideBatchStagingBuffer(std::size_t byteCount) = 0;

    /// @return The server type of the database.
    [[nodiscard]] virtual SqlServerType ServerType() const noexcept = 0;

    /// @return The driver name of the database.
    [[nodiscard]] virtual std::string const& DriverName() const noexcept = 0;

    /// @return How the narrow (@c char) string binders exchange text with the driver.
    [[nodiscard]] virtual detail::NarrowTextBinding NarrowTextBindingMode() const noexcept
    {
        return detail::NarrowTextBindingOf(ServerType());
    }

    /// Resolves the SQL type the server declares for an input parameter of the prepared statement.
    ///
    /// A binder that has no value to derive the type from - a NULL - uses this to bind with a type
    /// the target column accepts. Unlike a bare @c SQLDescribeParam() on the statement handle, it
    /// answers regardless of how many parameters are already bound.
    ///
    /// @param column The 1-based parameter index.
    /// @return The declared SQL type, or @c std::nullopt when it cannot be determined.
    [[nodiscard]] virtual std::optional<SQLSMALLINT> DescribeInputParameterType(SQLUSMALLINT column) noexcept
    {
        (void) column;
        return std::nullopt;
    }

    /// The SQL type the driver describes for result column @p column (see detail::DescribeResultColumnSqlType()),
    /// asked of the driver once per result set and column, so a binder that picks its read by the column's type
    /// for every row's cell (SqlVariant) asks nothing after the first row.
    /// @param column The 1-based column index.
    /// @return The SQL type, or @c std::nullopt when the callback keeps none or the driver cannot describe the
    ///         column (the binder then asks the driver itself, and reports its error).
    [[nodiscard]] virtual std::optional<SQLSMALLINT> DescribeResultColumnType(SQLUSMALLINT column) const noexcept
    {
        (void) column;
        return std::nullopt;
    }

    /// Records why a binder refused to bind a value, which it then answers with @c SQL_ERROR: the driver
    /// posted no diagnostic for it, so the error the caller raises is this one.
    ///
    /// @param error The SQLSTATE and message to report.
    virtual void ReportInputParameterError(SqlErrorInfo const& error) noexcept
    {
        (void) error;
    }
};

namespace detail
{
    /// Asks the driver for the concise SQL type of result column @p column with @c SQLDescribeColW.
    ///
    /// SQLDescribeCol reports the concise SQL type code (e.g. SQL_TYPE_DATE, SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP). It
    /// is asked rather than SQLColAttribute(SQL_DESC_CONCISE_TYPE), which the SQLite driver answers with SQL_WCHAR
    /// for a BLOB column. The W variant keeps the ODBC call surface on the Unicode-app path the rest of the library
    /// uses. The column name is not needed, but a real buffer with its true length is passed: psqlODBC under
    /// unixODBC fails a null or zero-length name buffer with HY001 ("Could not allocate memory for column name").
    /// On the stack, and a name longer than it is only truncated (01004, a success).
    /// @param stmt The statement handle.
    /// @param column The 1-based column index.
    /// @param columnType Receives the SQL type.
    /// @return The driver's return code.
    inline SQLRETURN DescribeResultColumnSqlType(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT* columnType) noexcept
    {
        SQLULEN columnSize {};
        SQLSMALLINT decimalDigits {};
        SQLSMALLINT nullable {};
        auto columnName = std::array<SQLWCHAR, 64> {};
        SQLSMALLINT columnNameLength {};
        return SQLDescribeColW(stmt,
                               column,
                               columnName.data(),
                               static_cast<SQLSMALLINT>(columnName.size()),
                               &columnNameLength,
                               columnType,
                               &columnSize,
                               &decimalDigits,
                               &nullable);
    }

    /// Asks the driver for the concise SQL type of result column @p column (see the overload above).
    /// @param stmt The statement handle.
    /// @param column The 1-based column index.
    /// @return The SQL type, or @c std::nullopt when the driver cannot describe the column.
    inline std::optional<SQLSMALLINT> DescribeResultColumnSqlType(SQLHSTMT stmt, SQLUSMALLINT column) noexcept
    {
        SQLSMALLINT columnType {};
        if (!SQL_SUCCEEDED(DescribeResultColumnSqlType(stmt, column, &columnType)))
            return std::nullopt;
        return columnType;
    }
} // namespace detail

namespace detail
{
    /// The layout of the parameter array that is being bound.
    struct SqlParameterArrayLayout
    {
        /// The byte distance from one row's value to the next.
        std::size_t rowStride {};
        /// The number of rows.
        std::size_t rowCount {};
    };

    /// Reads the layout of the parameter array the statement is bound with.
    ///
    /// @param stmt The statement whose parameters are being bound.
    /// @param valueSize The size of one value, which is the stride of a column-wise array.
    /// @param columnWiseRowCount The row count of a column-wise (or single-row) binding.
    /// @return The row size and parameter set size of a row-wise binding, otherwise the column-wise layout.
    [[nodiscard]] inline SqlParameterArrayLayout CurrentParameterArrayLayout(SQLHSTMT stmt,
                                                                             std::size_t valueSize,
                                                                             std::size_t columnWiseRowCount) noexcept
    {
        auto bindType = SQLULEN { SQL_PARAM_BIND_BY_COLUMN };
        if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt, SQL_ATTR_PARAM_BIND_TYPE, &bindType, 0, nullptr))
            || bindType == SQL_PARAM_BIND_BY_COLUMN)
            return { .rowStride = valueSize, .rowCount = columnWiseRowCount };

        auto parameterSetSize = SQLULEN { 1 };
        if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt, SQL_ATTR_PARAMSET_SIZE, &parameterSetSize, 0, nullptr)))
            parameterSetSize = 1;
        return { .rowStride = static_cast<std::size_t>(bindType), .rowCount = static_cast<std::size_t>(parameterSetSize) };
    }

    /// @brief Whether a date or timestamp with year @p year can be written so that it reads back: a negative
    /// year cannot (SQLite stores text no reader accepts, SQL Server rejects it, PostgreSQL makes it a BC date
    /// its driver reads back without the sign). Years 0 to 9999 are written as before.
    /// @param year The year to write.
    /// @return @c true if it can be written.
    [[nodiscard]] constexpr bool IsWritableYear(SQLSMALLINT year) noexcept
    {
        return year >= 0;
    }

    /// @brief The error of writing a date or timestamp with year @p year, which IsWritableYear() refuses.
    /// @param year The refused year.
    /// @return SQLSTATE 22008 (datetime field overflow) and a message naming the year.
    [[nodiscard]] inline SqlErrorInfo UnwritableYearError(SQLSMALLINT year)
    {
        return SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = "22008",
            .message = std::format("A date with the year {} cannot be written: no supported database stores a negative "
                                   "year so that it reads back.",
                                   year),
        };
    }

    /// @brief Refuses a date or timestamp of year @p year that cannot be written (see IsWritableYear()),
    /// reporting why to @p cb.
    /// @return @c true if the value was refused: the binder then answers @c SQL_ERROR.
    [[nodiscard]] inline bool RefuseUnwritableYear(SQLSMALLINT year, SqlDataBinderCallback& cb) noexcept
    {
        if (IsWritableYear(year))
            return false;
        try
        {
            cb.ReportInputParameterError(UnwritableYearError(year));
        }
        catch (...)
        {
            cb.ReportInputParameterError(SqlErrorInfo {});
        }
        return true;
    }

    /// @brief Refuses an array of dates or timestamps at @p first, laid out @p stride bytes apart, of which one
    /// cannot be written; rows whose @p indicators slot says NULL are skipped.
    /// @return @c true if a value was refused.
    template <typename Value>
    [[nodiscard]] bool RefuseUnwritableYears(Value const* first,
                                             std::size_t stride,
                                             std::size_t rowCount,
                                             SQLLEN const* indicators,
                                             SqlDataBinderCallback& cb) noexcept
    {
        auto const* const bytes = reinterpret_cast<std::byte const*>(first);
        for (auto const row: std::views::iota(std::size_t { 0 }, rowCount))
        {
            if (indicators != nullptr
                && *reinterpret_cast<SQLLEN const*>(reinterpret_cast<std::byte const*>(indicators) + (row * stride))
                       == SQL_NULL_DATA)
                continue;
            if (RefuseUnwritableYear(reinterpret_cast<Value const*>(bytes + (row * stride))->sqlValue.year, cb))
                return true;
        }
        return false;
    }

    /// @brief The year @p year if a date or timestamp of that year cannot be written (see IsWritableYear()).
    /// @param year The year to write.
    /// @return @p year if it is refused, otherwise @c std::nullopt.
    [[nodiscard]] constexpr std::optional<SQLSMALLINT> UnwritableYearOf(SQLSMALLINT year) noexcept
    {
        if (IsWritableYear(year))
            return std::nullopt;
        return year;
    }
} // namespace detail

template <typename>
struct SqlDataBinder;

/// @brief Whether the binder of @p T can tell, before anything is bound, that a value of @p T holds a date or
/// timestamp whose year cannot be written (see detail::IsWritableYear()): the dates themselves and the
/// wrappers that bind through them. A batch that executes row by row checks every row through it first, so
/// that no row is stored when one would be refused; a type without it costs nothing.
template <typename T>
concept SqlBinderChecksYear = requires(T const& value) {
    { SqlDataBinder<T>::UnwritableYear(value) } -> std::same_as<std::optional<SQLSMALLINT>>;
};

// Default traits for output string parameters
// This needs to be implemented for each string type that should be used as output parameter via
// SqlDataBinder<>. An std::string specialization is provided below. Feel free to add more specializations for
// other string types, such as CString, etc.
template <typename>
struct SqlBasicStringOperations;

// -----------------------------------------------------------------------------------------------

namespace detail
{

    /// @brief Column size to declare for a variable-length parameter of @p length characters or bytes.
    ///
    /// ODBC defines a column size of 0 as invalid; strict drivers reject it with HY104 when an empty
    /// string or binary is bound, so the declared size never drops below 1. The bound data length is unaffected.
    [[nodiscard]] constexpr SQLULEN NonZeroColumnSize(std::size_t length) noexcept
    {
        return std::max<SQLULEN>(static_cast<SQLULEN>(length), 1);
    }

    /// @brief Satisfied when @p T is the same type as at least one of @p Us.
    ///
    /// Mirrors @c Lightweight::detail::OneOf, but lives in the DataBinder layer so the low-level binder
    /// headers can use it without reaching up to the higher-level Utils.hpp.
    template <typename T, typename... Us>
    concept IsAnyOf = (std::same_as<T, Us> || ...);

    /// @brief Buffer operations of a contiguous container exposing @c data(), @c size() and
    /// @c resize() (e.g. @c SqlBinary), shaped like the @c SqlBasicStringOperations traits.
    struct ContainerBufferOperations
    {
        /// @return The container's storage.
        template <typename Container>
        static auto* Data(Container* container) noexcept
        {
            return container->data();
        }

        /// @return The container's element count.
        template <typename Container>
        static std::size_t Size(Container const* container) noexcept
        {
            return container->size();
        }

        /// Resizes the container to @p size elements.
        template <typename Container>
        static void Resize(Container* container, SQLLEN size)
        {
            container->resize(static_cast<std::size_t>(size));
        }
    };

    /// @brief The pre-fetch step of @ref PlanBoundBufferRestore: grows the container back to the bound size
    /// and re-binds the column if its storage moved.
    /// @tparam Ops @c SqlBasicStringOperations-shaped buffer operations of @p Container.
    /// @tparam UnitBytes Bytes per element of the bound buffer.
    /// @tparam TerminatorUnits Elements the bound buffer has beyond its size for the driver's terminator.
    /// @param step The planned step.
    /// @param stmt The statement handle the column is bound on.
    /// @return @c SQL_SUCCESS, or the result of re-binding the column.
    /// @throws std::bad_alloc When growing the container back allocates (its storage was moved out); the
    ///         fetch reports that as HY001.
    template <typename Ops, typename Container, std::size_t UnitBytes, std::size_t TerminatorUnits>
    SQLRETURN RestoreBoundBuffer(SqlOutputColumnPreFetch& step, SQLHSTMT stmt)
    {
        auto* const result = static_cast<Container*>(step.target);
        auto const size = static_cast<std::size_t>(Ops::Size(result));
        if (static_cast<void const*>(Ops::Data(result)) != step.boundData)
            step.boundSize = (std::max) (step.boundSize, size);
        if (size != step.boundSize)
        {
            // The driver overwrites the regrown part, so where the container can skip initializing it
            // (std::basic_string), it does: a long value grows the bound size, and every later row would
            // otherwise zero-fill up to it. A vector (binary) zero-fills up to the bound size.
            if constexpr (requires { Ops::ResizeForOverwrite(result, step.boundSize); })
                Ops::ResizeForOverwrite(result, step.boundSize);
            else
                Ops::Resize(result, static_cast<SQLLEN>(step.boundSize));
        }
        auto* const data = Ops::Data(result);
        if (static_cast<void const*>(data) == step.boundData)
            return SQL_SUCCESS;
        auto const rc = SQLBindCol(stmt,
                                   step.column,
                                   step.cType,
                                   static_cast<SQLPOINTER>(data),
                                   static_cast<SQLLEN>((step.boundSize + TerminatorUnits) * UnitBytes),
                                   step.indicator);
        // Recorded only once bound: after a failed bind the next fetch must try again, not fetch through the
        // binding of the storage that moved away.
        if (SQL_SUCCEEDED(rc))
            step.boundData = data;
        return rc;
    }

    /// @brief The largest declared column length a growable container is grown to when bound: SQL
    /// Server's largest VARCHAR / VARBINARY short of MAX. A longer or MAX column starts at
    /// @ref DefaultBoundColumnSize.
    inline constexpr std::size_t MaxDeclaredBoundColumnSize = 8000;

    /// @brief The declared length of output column @p column as a bound buffer size: between
    /// @ref DefaultBoundColumnSize and @ref MaxDeclaredBoundColumnSize, and @ref DefaultBoundColumnSize
    /// for a length the driver does not report or one above the maximum (a MAX or LONG column). One
    /// @c SQLColAttribute call.
    ///
    /// @param stmt The statement holding the result set.
    /// @param column The 1-based column index.
    /// @param length Which declared length to ask for.
    /// @return The number of elements to bind, without a terminator.
    inline std::size_t DeclaredBoundColumnSize(SQLHSTMT stmt, SQLUSMALLINT column, SqlOutputColumnLength length) noexcept
    {
        auto const field =
            static_cast<SQLUSMALLINT>(length == SqlOutputColumnLength::Octets ? SQL_DESC_OCTET_LENGTH : SQL_DESC_LENGTH);
        SQLLEN declared {};
        if (!SQL_SUCCEEDED(SQLColAttribute(stmt, column, field, nullptr, 0, nullptr, &declared)) || declared <= 0
            || std::cmp_greater(declared, MaxDeclaredBoundColumnSize))
            return DefaultBoundColumnSize;
        return (std::max) (DefaultBoundColumnSize, static_cast<std::size_t>(declared));
    }

    /// @brief Fails the fetch of a bound value longer than its buffer that the driver cannot read again.
    ///
    /// The binders' post-processing reads such a value with @c SQLGetData; a driver without
    /// @c SQL_GD_BOUND (MS SQL Server) refuses that for a bound column. Rather than return the value
    /// truncated to the buffer, the fetch fails with this error (SQLSTATE 22001).
    ///
    /// @param stmt The statement the column is bound on (its diagnostic names the driver's reason).
    /// @param column The 1-based column index.
    /// @param totalLength The value's length as the driver reported it, in bytes, or @c SQL_NO_TOTAL.
    [[noreturn]] inline void ThrowBoundValueNotReadable(SQLHSTMT stmt, SQLUSMALLINT column, SQLLEN totalLength)
    {
        auto info = SqlErrorInfo::FromStatementHandle(stmt);
        // 22001 (string data, right truncation) names what happened to the value; the driver's own
        // state for the refused re-read (e.g. 07009 on SQL Server) only says why it could not be avoided.
        info.sqlState = "22001";
        info.nativeErrorCode = 0;
        auto const length =
            totalLength == SQL_NO_TOTAL ? std::string { "length unknown" } : std::format("{} bytes", totalLength);
        info.message = std::format("Column {}: the value ({}) is longer than its bound buffer, and the driver cannot "
                                   "read a bound column again (no SQL_GD_BOUND). Read the column with GetColumn(), or "
                                   "size the bound variable to the longest value before binding it. ({})",
                                   column,
                                   length,
                                   info.message);
        throw SqlException(std::move(info));
    }

    /// @brief Fails the fetch of a bound value whose re-read the driver reported inconsistently, or that grew
    /// beyond @ref MaxUnknownLengthValueBytes.
    ///
    /// @param column The 1-based column index.
    /// @param sqlState 22001 for a value too long to read, HY000 for a driver report that contradicts itself.
    /// @param reason What went wrong, for the message.
    [[noreturn]] inline void ThrowBoundValueMisread(SQLUSMALLINT column, std::string_view sqlState, std::string_view reason)
    {
        throw SqlException(SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = std::string { sqlState },
            .message = std::format("Column {}: the bound value could not be read in full: {}", column, reason),
        });
    }

    /// @brief The most bytes a bound value of unknown length (@c SQL_NO_TOTAL) is grown to while it is read
    /// again; a longer value fails the fetch with 22001 instead of exhausting memory.
    inline constexpr std::size_t MaxUnknownLengthValueBytes = std::size_t { 1 } << 30;

    /// @brief Reads a bound value again in full that the driver reported longer than its buffer without
    /// saying how long (@c SQL_NO_TOTAL), in chunks through @p getData, doubling @p result as it goes.
    ///
    /// Only a row whose value overflowed its bound buffer gets here. The container keeps its grown size, so
    /// the caller decides what the value's length means for it, and re-binds the column if the container's
    /// storage moved (see @ref PlanBoundBufferRestore), also when this throws.
    ///
    /// @tparam Ops @c SqlBasicStringOperations-shaped buffer operations of @p Container.
    /// @tparam UnitBytes Bytes per element of the buffer.
    /// @tparam TerminatorUnits Elements the bound buffer has beyond its size for the driver's terminator.
    /// @param stmt The statement the column is bound on (for the driver's diagnostic).
    /// @param column The 1-based column index.
    /// @param cType The C data type the column is bound as; a character type takes one element of each
    ///              chunk for its terminator.
    /// @param result The bound container.
    /// @param indicator The column's length/indicator buffer; set to the value's length in bytes.
    /// @param getData Reads the next chunk: @c SQLGetData of the column as @p cType, given the target, its
    ///                length in bytes and the indicator.
    /// @param maxBytes The most bytes the container grows to.
    /// @return The value's length in elements; 0 for a NULL, which leaves @p indicator at @c SQL_NULL_DATA (a
    ///         column unbound after a failed re-bind is read through here, NULL rows included).
    /// @throws SqlException 22001 when the driver cannot read the bound column again (see
    ///         @ref ThrowBoundValueNotReadable) or the value exceeds @p maxBytes; HY000 when the driver reports
    ///         a length that contradicts the call.
    template <typename Ops, std::size_t UnitBytes, std::size_t TerminatorUnits, typename Container, typename GetData>
    std::size_t ReadBoundValueOfUnknownLength(SQLHSTMT stmt,
                                              SQLUSMALLINT column,
                                              SQLSMALLINT cType,
                                              Container* result,
                                              SQLLEN* indicator,
                                              GetData const& getData,
                                              std::size_t maxBytes = MaxUnknownLengthValueBytes)
    {
        auto const terminatorUnits = std::size_t { cType == SQL_C_BINARY ? 0U : 1U };
        if (Ops::Size(result) < DefaultBoundColumnSize)
            Ops::Resize(result, static_cast<SQLLEN>(DefaultBoundColumnSize));
        auto read = std::size_t { 0 };
        auto first = true;
        while (true)
        {
            auto const units = Ops::Size(result) - read + TerminatorUnits;
            auto const rv = getData(
                static_cast<SQLPOINTER>(Ops::Data(result) + read), static_cast<SQLLEN>(units * UnitBytes), indicator);
            if (rv == SQL_NO_DATA)
            {
                // All of it was delivered by the previous chunk. On the first call nothing was: the driver
                // does not read the bound column again.
                if (first)
                    ThrowBoundValueNotReadable(stmt, column, SQL_NO_TOTAL);
                break;
            }
            if (!SQL_SUCCEEDED(rv))
                ThrowBoundValueNotReadable(stmt, column, SQL_NO_TOTAL);
            if (first && *indicator == SQL_NULL_DATA)
                return 0; // a NULL: nothing to read, the indicator says so
            first = false;
            if (rv == SQL_SUCCESS)
            {
                // The last chunk: its indicator is its length, which fits what was offered.
                if (*indicator < 0 || std::cmp_greater(*indicator, (units - terminatorUnits) * UnitBytes))
                    ThrowBoundValueMisread(column, "HY000", std::format("the driver reported {} bytes", *indicator));
                read += static_cast<std::size_t>(*indicator) / UnitBytes;
                break;
            }
            read += units - terminatorUnits; // a full chunk: there is more
            if (Ops::Size(result) * 2 * UnitBytes > maxBytes)
                ThrowBoundValueMisread(column, "22001", std::format("it is longer than {} bytes", maxBytes));
            Ops::Resize(result, static_cast<SQLLEN>(Ops::Size(result) * 2));
        }
        *indicator = static_cast<SQLLEN>(read * UnitBytes);
        return read;
    }

    /// @brief @ref ReadBoundValueOfUnknownLength reading through @c SQLGetData.
    /// @return The value's length in elements.
    template <typename Ops, std::size_t UnitBytes, std::size_t TerminatorUnits, typename Container>
    std::size_t ReadBoundValueOfUnknownLength(
        SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT cType, Container* result, SQLLEN* indicator)
    {
        return ReadBoundValueOfUnknownLength<Ops, UnitBytes, TerminatorUnits>(
            stmt, column, cType, result, indicator, [&](SQLPOINTER target, SQLLEN length, SQLLEN* lengthIndicator) {
                return SQLGetData(stmt, column, cType, target, length, lengthIndicator);
            });
    }

    /// @brief Keeps an output column that is bound straight into a growable container bound for every
    /// fetch of the result set.
    ///
    /// The binder's post-processing shrinks the container to each row's length, so before the next
    /// fetch it is grown back to the bound size. When its storage moved — a truncation re-fetch grew it,
    /// or the caller moved the value out — the column is re-bound to the new storage, at the larger of
    /// the old bound size and the container's current size. Per row this is a size compare and a pointer
    /// compare when nothing changed; growing back within the retained capacity allocates nothing, and
    /// planning the step allocates nothing either (see @ref SqlOutputColumnPreFetch).
    ///
    /// @tparam Ops @c SqlBasicStringOperations-shaped buffer operations of @p Container.
    /// @tparam UnitBytes Bytes per element of the bound buffer.
    /// @tparam TerminatorUnits Elements the bound buffer has beyond its size for the driver's terminator.
    /// @param column The 1-based column index.
    /// @param cType The C data type the column is bound as.
    /// @param result The bound container.
    /// @param indicator The column's length/indicator buffer.
    /// @param cb The binder callback the step is planned on.
    template <typename Ops, std::size_t UnitBytes, std::size_t TerminatorUnits, typename Container>
    void PlanBoundBufferRestore(
        SQLUSMALLINT column, SQLSMALLINT cType, Container* result, SQLLEN* indicator, SqlDataBinderCallback& cb)
    {
        auto step = SqlOutputColumnPreFetch {
            .run = &RestoreBoundBuffer<Ops, Container, UnitBytes, TerminatorUnits>,
            .target = result,
            .boundData = static_cast<void const*>(Ops::Data(result)),
            .boundSize = static_cast<std::size_t>(Ops::Size(result)),
            .column = column,
            .cType = cType,
        };
        step.indicator = indicator; // the driver writes through it: the step re-binds the column with it
        cb.PlanPreFetchOutputColumn(step);
    }

    /// @brief Byte offset of the contained value within a @c std::optional<T>.
    ///
    /// Used by the row-wise batch binders to address the inner value of each row's optional in place. The
    /// offset is 0 on all known standard libraries, but is computed rather than assumed so the address
    /// arithmetic does not bake in that assumption. It is derived from the integer addresses of a probe
    /// optional and its contained value (rather than @c byte* subtraction) to keep the computation defined.
    ///
    /// @tparam T The contained value type.
    /// @return The offset, in bytes, of the contained value within the optional.
    template <typename T>
    [[nodiscard]] inline std::size_t OptionalValueOffset() noexcept
    {
        std::optional<T> const probe { T {} };
        return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(std::addressof(*probe))
                                        - reinterpret_cast<std::uintptr_t>(std::addressof(probe)));
    }

    // clang-format off
template <typename T>
concept HasGetStringAndGetLength = requires(T const& t) {
    { t.GetLength() } -> std::same_as<int>;
    { t.GetString() } -> std::same_as<char const*>;
};

template <typename T>
concept HasGetStringAndLength = requires(T const& t)
{
    { t.Length() } -> std::same_as<int>;
    { t.GetString() } -> std::same_as<char const*>;
};
    // clang-format on

    template <typename>
    struct SqlViewHelper;

    template <typename T>
    concept HasSqlViewHelper = requires(T const& t) {
        { SqlViewHelper<T>::View(t) } -> std::convertible_to<std::string_view>;
    };

    template <typename CharT>
    struct SqlViewHelper<std::basic_string<CharT>>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::basic_string_view<CharT> View(std::basic_string<CharT> const& str) noexcept
        {
            return { str.data(), str.size() };
        }
    };

    template <detail::HasGetStringAndGetLength CStringLike>
    struct SqlViewHelper<CStringLike>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::string_view View(CStringLike const& str) noexcept
        {
            return { str.GetString(), static_cast<size_t>(str.GetLength()) };
        }
    };

    template <detail::HasGetStringAndLength StringLike>
    struct SqlViewHelper<StringLike>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::string_view View(StringLike const& str) noexcept
        {
            return { str.GetString(), static_cast<size_t>(str.Length()) };
        }
    };

} // namespace detail

// -----------------------------------------------------------------------------------------------

template <typename T>
concept SqlInputParameterBinder = requires(SQLHSTMT hStmt, SQLUSMALLINT column, T const& value, SqlDataBinderCallback& cb) {
    { SqlDataBinder<T>::InputParameter(hStmt, column, value, cb) } -> std::same_as<SQLRETURN>;
};

template <typename T>
concept SqlOutputColumnBinder =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback& cb) {
        { SqlDataBinder<T>::OutputColumn(hStmt, column, result, indicator, cb) } -> std::same_as<SQLRETURN>;
    };

template <typename T>
concept SqlInputParameterBatchBinder =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, std::ranges::range_value_t<T>* result, SqlDataBinderCallback& cb) {
        {
            SqlDataBinder<std::ranges::range_value_t<T>>::InputParameter(
                hStmt, column, std::declval<std::ranges::range_value_t<T>>(), cb)
        } -> std::same_as<SQLRETURN>;
    };

/// @brief Opt-in trait marking a value type as bindable in a native ODBC row-wise parameter array.
///
/// A type qualifies when it is fixed-width, stored inline, bound via a plain @c SQLBindParameter with
/// no per-call heap conversion, and identically across all supported backends — so its address can be
/// handed to ODBC and strided by @c SQL_ATTR_PARAM_BIND_TYPE. The primary template is @c false; each
/// eligible binder header specializes it to @c true (primitives, date/time/datetime, numeric).
///
/// @note @c SqlGuid is intentionally NOT marked: on SQLite it is bound via a per-value text conversion,
/// which cannot be expressed as a zero-copy row-wise array — GUID columns use the soft batch path.
template <typename T>
inline constexpr bool SqlIsNativeRowBindableValue = false;

/// @brief Opt-in trait marking a value type as an @c SqlNumeric specialization.
///
/// Numeric values are row-wise bindable, but @c std::optional<SqlNumeric> is not (the contained value
/// is not bound at a uniform offset/representation across backends), so the optional batch path
/// excludes them via this trait.
template <typename T>
inline constexpr bool SqlIsNumericValue = false;

/// @brief Whether @p T is a @c std::optional specialization.
///
/// Defined locally rather than reusing @c Lightweight::IsSpecializationOf (Utils.hpp) or the DataMapper
/// @c IsStdOptional (Field.hpp): this low-level binder header is deliberately kept free of dependencies on
/// those higher-level headers, so it carries its own minimal optional traits.
template <typename T>
inline constexpr bool SqlIsStdOptional = false;

template <typename T>
inline constexpr bool SqlIsStdOptional<std::optional<T>> = true;

template <typename T>
concept SqlGetColumnNativeType =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) {
        { SqlDataBinder<T>::GetColumn(hStmt, column, result, indicator, cb) } -> std::same_as<SQLRETURN>;
    };

template <typename T>
concept SqlDataBinderSupportsInspect = requires(T const& value) {
    { SqlDataBinder<std::remove_cvref_t<T>>::Inspect(value) } -> std::convertible_to<std::string>;
};

// clang-format off
template <typename StringType, typename CharType>
concept SqlBasicStringBinderConcept = requires(StringType* str) {
    { SqlBasicStringOperations<StringType>::Data(str) } -> std::same_as<CharType*>;
    { SqlBasicStringOperations<StringType>::Size(str) } -> std::same_as<SQLULEN>;
    { SqlBasicStringOperations<StringType>::Reserve(str, size_t {}) } -> std::same_as<void>;
    { SqlBasicStringOperations<StringType>::Resize(str, SQLLEN {}) } -> std::same_as<void>;
    { SqlBasicStringOperations<StringType>::Clear(str) } -> std::same_as<void>;
};
// clang-format on

} // namespace Lightweight
