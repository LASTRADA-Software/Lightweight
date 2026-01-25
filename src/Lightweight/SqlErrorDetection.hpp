// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SqlError.hpp"
#include "SqlServerType.hpp"

#include <string_view>

namespace Lightweight
{

/// Detects if an error indicates "table already exists".
///
/// @param error The SQL error info
/// @param serverType The database server type
/// @return true if this is a "table already exists" error
[[nodiscard]] inline bool IsTableAlreadyExistsError(SqlErrorInfo const& error, SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::MICROSOFT_SQL:
            // SQLSTATE 42S01 = Base table or view already exists
            // Native error 2714 = "There is already an object named '...' in the database"
            return error.sqlState == "42S01" || error.nativeErrorCode == 2714;
        case SqlServerType::POSTGRESQL:
            // SQLSTATE 42P07 = duplicate_table
            return error.sqlState == "42P07";
        case SqlServerType::SQLITE:
            // SQLite uses generic SQLSTATE, check message
            return error.message.find("already exists") != std::string::npos;
        case SqlServerType::MYSQL:
            // SQLSTATE 42S01 = Base table or view already exists
            // Native error 1050 = "Table '...' already exists"
            return error.sqlState == "42S01" || error.nativeErrorCode == 1050;
        default:
            // Fallback: check message for common pattern
            return error.message.find("already exists") != std::string::npos;
    }
}

/// Detects if an error indicates "table not found".
///
/// @param error The SQL error info
/// @param serverType The database server type
/// @return true if this is a "table not found" error
[[nodiscard]] inline bool IsTableNotFoundError(SqlErrorInfo const& error, SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::MICROSOFT_SQL:
            // SQLSTATE 42S02 = Base table or view not found
            // Native error 208 = "Invalid object name '...'"
            return error.sqlState == "42S02" || error.nativeErrorCode == 208;
        case SqlServerType::POSTGRESQL:
            // SQLSTATE 42P01 = undefined_table
            return error.sqlState == "42P01";
        case SqlServerType::SQLITE:
            // SQLite uses generic SQLSTATE, check message
            return error.message.find("no such table") != std::string::npos;
        case SqlServerType::MYSQL:
            // SQLSTATE 42S02 = Base table or view not found
            // Native error 1146 = "Table '...' doesn't exist"
            return error.sqlState == "42S02" || error.nativeErrorCode == 1146;
        default:
            // Fallback: check message for common patterns
            return error.message.find("not found") != std::string::npos || error.message.find("no such") != std::string::npos
                   || error.message.find("does not exist") != std::string::npos;
    }
}

/// Detects if an error is transient (retryable).
///
/// Transient errors are temporary conditions that may succeed if retried,
/// such as connection issues, timeouts, or database locks.
///
/// @param error The SQL error info
/// @return true if this is a transient error that may be retried
[[nodiscard]] inline bool IsTransientError(SqlErrorInfo const& error) noexcept
{
    std::string_view state = error.sqlState;

    // Connection errors (SQLSTATE Class 08)
    // 08001 = Unable to connect
    // 08003 = Connection does not exist
    // 08004 = Connection rejected
    // 08006 = Connection failure
    // 08007 = Transaction resolution unknown
    if (state.starts_with("08"))
        return true;

    // Timeout errors
    // HYT00 = Timeout expired
    // HYT01 = Connection timeout expired
    if (state == "HYT00" || state == "HYT01")
        return true;

    // Transaction rollback (SQLSTATE Class 40)
    // 40001 = Serialization failure (deadlock)
    // 40002 = Transaction integrity constraint violation
    // 40003 = Statement completion unknown
    if (state.starts_with("40"))
        return true;

    // SQLite-specific busy/locked conditions
    if (error.message.find("database is locked") != std::string::npos)
        return true;
    if (error.message.find("SQLITE_BUSY") != std::string::npos)
        return true;
    if (error.message.find("SQLITE_LOCKED") != std::string::npos)
        return true;

    // SQL Server specific transient errors
    // Native error codes for transient conditions
    switch (error.nativeErrorCode)
    {
        case 1205: // Deadlock victim (SQL Server)
        case 1222: // Lock request timeout (SQL Server)
        case -2:   // Timeout (SQL Server)
            return true;
        default:
            break;
    }

    return false;
}

/// Detects if an error indicates a unique constraint violation.
///
/// @param error The SQL error info
/// @param serverType The database server type
/// @return true if this is a unique constraint violation error
[[nodiscard]] inline bool IsUniqueConstraintViolation(SqlErrorInfo const& error, SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::MICROSOFT_SQL:
            // SQLSTATE 23000 = Integrity constraint violation
            // Native error 2627 = Violation of UNIQUE KEY constraint
            // Native error 2601 = Cannot insert duplicate key row
            return error.nativeErrorCode == 2627 || error.nativeErrorCode == 2601;
        case SqlServerType::POSTGRESQL:
            // SQLSTATE 23505 = unique_violation
            return error.sqlState == "23505";
        case SqlServerType::SQLITE:
            // SQLite uses generic SQLSTATE, check message
            return error.message.find("UNIQUE constraint failed") != std::string::npos;
        case SqlServerType::MYSQL:
            // Native error 1062 = Duplicate entry for key
            return error.nativeErrorCode == 1062;
        default:
            return error.sqlState == "23000" || error.sqlState == "23505";
    }
}

/// Detects if an error indicates a foreign key constraint violation.
///
/// @param error The SQL error info
/// @param serverType The database server type
/// @return true if this is a foreign key constraint violation error
[[nodiscard]] inline bool IsForeignKeyViolation(SqlErrorInfo const& error, SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::MICROSOFT_SQL:
            // Native error 547 = The INSERT/UPDATE/DELETE statement conflicted with FK constraint
            return error.nativeErrorCode == 547;
        case SqlServerType::POSTGRESQL:
            // SQLSTATE 23503 = foreign_key_violation
            return error.sqlState == "23503";
        case SqlServerType::SQLITE:
            // SQLite uses generic SQLSTATE, check message
            return error.message.find("FOREIGN KEY constraint failed") != std::string::npos;
        case SqlServerType::MYSQL:
            // Native error 1451 = Cannot delete or update a parent row
            // Native error 1452 = Cannot add or update a child row
            return error.nativeErrorCode == 1451 || error.nativeErrorCode == 1452;
        default:
            return error.sqlState == "23503";
    }
}

} // namespace Lightweight
