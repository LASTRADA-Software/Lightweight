// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "Api.hpp"

#include <cstdint>
#include <format>
#include <source_location>
#include <stdexcept>
#include <system_error>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

namespace Lightweight
{

/// @brief Represents an ODBC SQL error.
///
/// NOTE: This is a simple wrapper around the SQL return codes. It is not meant to be
/// comprehensive, but rather to provide a simple way to convert SQL return codes to
/// std::error_code.
///
/// The code below is DRAFT and may be subject to change.
struct SqlErrorInfo
{
    /// The native ODBC error code.
    SQLINTEGER nativeErrorCode {};
    /// The SQLSTATE diagnostic code (5 characters).
    std::string sqlState = "     "; // 5 characters + null terminator
    /// The human-readable error message.
    std::string message;

    /// Constructs an ODBC error info object from the given ODBC connection handle.
    static SqlErrorInfo FromConnectionHandle(SQLHDBC hDbc)
    {
        return FromHandle(SQL_HANDLE_DBC, hDbc);
    }

    /// Constructs an ODBC error info object from the given ODBC statement handle.
    static SqlErrorInfo FromStatementHandle(SQLHSTMT hStmt)
    {
        return FromHandle(SQL_HANDLE_STMT, hStmt);
    }

    /// Constructs an ODBC error info object from the given ODBC environment handle.
    static SqlErrorInfo FromEnvironmentHandle(SQLHENV hEnv)
    {
        return FromHandle(SQL_HANDLE_ENV, hEnv);
    }

    /// Asserts that the given result is a success code, otherwise throws an exception.
    static void RequireStatementSuccess(SQLRETURN result, SQLHSTMT hStmt, std::string_view message);

  private:
    LIGHTWEIGHT_API static SqlErrorInfo FromHandle(SQLSMALLINT handleType, SQLHANDLE handle);
};

/// @brief Supplies diagnostics for an ODBC handle.
///
/// Exists so error paths can be driven from a unit test without a database. Production code never
/// installs one: with no source configured, @ref SqlErrorInfo reads diagnostics from the driver as
/// usual. A test installs a source that returns a scripted @ref SqlErrorInfo, which makes the
/// classification and propagation logic reachable without provoking a real driver failure.
///
/// @see SetDiagnosticSource
class SqlDiagnosticSource
{
  public:
    SqlDiagnosticSource() = default;
    SqlDiagnosticSource(SqlDiagnosticSource const&) = delete;
    SqlDiagnosticSource& operator=(SqlDiagnosticSource const&) = delete;
    SqlDiagnosticSource(SqlDiagnosticSource&&) = delete;
    SqlDiagnosticSource& operator=(SqlDiagnosticSource&&) = delete;
    virtual ~SqlDiagnosticSource() = default;

    /// @brief Returns the diagnostics for the given handle.
    ///
    /// @param handleType One of @c SQL_HANDLE_ENV, @c SQL_HANDLE_DBC or @c SQL_HANDLE_STMT.
    /// @param handle The handle the diagnostics are requested for. A fake may ignore it.
    /// @return The diagnostics to report for @p handle.
    [[nodiscard]] virtual SqlErrorInfo Diagnose(SQLSMALLINT handleType, SQLHANDLE handle) = 0;
};

/// @brief Overrides the source of ODBC diagnostics process-wide.
///
/// Intended for tests. Ownership is not transferred and remains with the caller, which must keep
/// @p source alive until it is cleared. Pass @c nullptr to restore the real ODBC reader.
///
/// This mirrors how @c SqlLogger::SetLogger installs a logger, and costs nothing on the success
/// path: the override is consulted only once a call has already failed and diagnostics are being
/// retrieved.
///
/// @param source The source to install, or @c nullptr to restore the default.
LIGHTWEIGHT_API void SetDiagnosticSource(SqlDiagnosticSource* source);

/// @brief Returns the currently installed diagnostic source, or @c nullptr if none is installed.
[[nodiscard]] LIGHTWEIGHT_API SqlDiagnosticSource* GetDiagnosticSource() noexcept;

class SqlException: public std::runtime_error
{
  public:
    LIGHTWEIGHT_API explicit SqlException(SqlErrorInfo info,
                                          std::source_location location = std::source_location::current());

    // NOLINTNEXTLINE(readability-identifier-naming)
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE SqlErrorInfo const& info() const noexcept
    {
        return _info;
    }

  private:
    SqlErrorInfo _info;
};

enum class SqlError : std::int16_t
{
    SUCCESS = SQL_SUCCESS,
    SUCCESS_WITH_INFO = SQL_SUCCESS_WITH_INFO,
    NODATA = SQL_NO_DATA,
    FAILURE = SQL_ERROR,
    INVALID_HANDLE = SQL_INVALID_HANDLE,
    STILL_EXECUTING = SQL_STILL_EXECUTING,
    NEED_DATA = SQL_NEED_DATA,
    PARAM_DATA_AVAILABLE = SQL_PARAM_DATA_AVAILABLE,
    NO_DATA_FOUND = SQL_NO_DATA_FOUND,
    UNSUPPORTED_TYPE = 1'000,
    INVALID_ARGUMENT = 1'001,
    TRANSACTION_ERROR = 1'002,
};

struct SqlErrorCategory: std::error_category
{
    // NOLINTNEXTLINE(readability-identifier-naming)
    static SqlErrorCategory const& get() noexcept
    {
        static SqlErrorCategory const category;
        return category;
    }

    [[nodiscard]] char const* name() const noexcept override
    {
        return "Lightweight";
    }

    [[nodiscard]] std::string message(int code) const override
    {
        using namespace std::string_literals;
        switch (static_cast<SqlError>(code))
        {
            case SqlError::SUCCESS:
                return "SQL_SUCCESS"s;
            case SqlError::SUCCESS_WITH_INFO:
                return "SQL_SUCCESS_WITH_INFO"s;
            case SqlError::NODATA:
                return "SQL_NO_DATA"s;
            case SqlError::FAILURE:
                return "SQL_ERROR"s;
            case SqlError::INVALID_HANDLE:
                return "SQL_INVALID_HANDLE"s;
            case SqlError::STILL_EXECUTING:
                return "SQL_STILL_EXECUTING"s;
            case SqlError::NEED_DATA:
                return "SQL_NEED_DATA"s;
            case SqlError::PARAM_DATA_AVAILABLE:
                return "SQL_PARAM_DATA_AVAILABLE"s;
            case SqlError::UNSUPPORTED_TYPE:
                return "SQL_UNSUPPORTED_TYPE"s;
            case SqlError::INVALID_ARGUMENT:
                return "SQL_INVALID_ARGUMENT"s;
            case SqlError::TRANSACTION_ERROR:
                return "SQL_TRANSACTION_ERROR"s;
        }
        return std::format("SQL error code {}", code);
    }
};

} // namespace Lightweight

// Register our enum as an error code so we can constructor error_code from it
template <>
struct std::is_error_code_enum<Lightweight::SqlError>: public std::true_type
{
};

/// Tells the compiler that MyErr pairs with MyCategory
// NOLINTNEXTLINE(readability-identifier-naming)
inline std::error_code make_error_code(Lightweight::SqlError e)
{
    return { static_cast<int>(e), Lightweight::SqlErrorCategory::get() };
}

template <>
struct std::formatter<Lightweight::SqlError>: formatter<std::string>
{
    auto format(Lightweight::SqlError value, format_context& ctx) const -> format_context::iterator
    {
        // Use the shared singleton instead of default-constructing a fresh category for every
        // format call — the singleton is the same instance returned by `make_error_code()`.
        return formatter<std::string>::format(Lightweight::SqlErrorCategory::get().message(static_cast<int>(value)), ctx);
    }
};

template <>
struct std::formatter<Lightweight::SqlErrorInfo>: formatter<std::string>
{
    auto format(Lightweight::SqlErrorInfo const& info, format_context& ctx) const -> format_context::iterator
    {
        return formatter<std::string>::format(std::format("{} ({}) - {}", info.sqlState, info.nativeErrorCode, info.message),
                                              ctx);
    }
};
