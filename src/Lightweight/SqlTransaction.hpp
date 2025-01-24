// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "SqlConnection.hpp"
#include "SqlError.hpp"

#include <source_location>
#include <stdexcept>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

// Represents the mode of a SQL transaction to be applied, if not done so explicitly.
enum class SqlTransactionMode : std::uint8_t
{
    NONE,
    COMMIT,
    ROLLBACK,
};

class SqlTransactionException: public std::runtime_error
{
  public:
    explicit SqlTransactionException(const std::string& message) noexcept:
        std::runtime_error(message)
    {
    }
};

/// Represents a transaction to a SQL database.
///
/// This class is used to control the transaction manually. It disables the auto-commit mode when constructed,
/// and automatically commits the transaction when destructed if not done so.
///
/// This class is designed with RAII in mind, so that the transaction is automatically committed or rolled back
/// when the object goes out of scope.
///
/// @code
///
/// void doSomeWork(SqlTransaction& transaction)
/// {
///    auto stmt = SqlStatement { transaction.Connection() };
///    stmt.ExecuteDirect("INSERT INTO table (column) VALUES (42)"); // Do some work
/// }
///
/// void main()
/// {
///     auto connection = SqlConnection {};
///     auto transaction = SqlTransaction { connection };
///
///     doSomeWork(transaction);
///
///     transaction.Commit();
/// }
/// @endcode
class SqlTransaction
{
  public:
    LIGHTWEIGHT_API SqlTransaction() = delete;
    LIGHTWEIGHT_API SqlTransaction(SqlTransaction const&) = delete;
    LIGHTWEIGHT_API SqlTransaction& operator=(SqlTransaction const&) = delete;

    LIGHTWEIGHT_API SqlTransaction(SqlTransaction&&) = default;
    LIGHTWEIGHT_API SqlTransaction& operator=(SqlTransaction&&) = default;

    /// Construct a new SqlTransaction object, and disable the auto-commit mode, so that the transaction can be
    /// controlled manually.
    LIGHTWEIGHT_API explicit SqlTransaction(SqlConnection& connection,
                                            SqlTransactionMode defaultMode = SqlTransactionMode::COMMIT,
                                            std::source_location location = std::source_location::current());

    /// Automatically commit the transaction if not done so
    LIGHTWEIGHT_API ~SqlTransaction() noexcept;

    /// Get the connection object associated with this transaction.
    SqlConnection& Connection() noexcept;

    /// Rollback the transaction. Throws an exception if the transaction cannot be rolled back.
    LIGHTWEIGHT_API void Rollback();

    /// Try to rollback the transaction, and return true if successful, falls otherwise
    LIGHTWEIGHT_API bool TryRollback() noexcept;

    /// Commit the transaction. Throws an exception if the transaction cannot be committed.
    LIGHTWEIGHT_API void Commit();

    /// Try to commit the transaction, and return true if successful, falls otherwise
    LIGHTWEIGHT_API bool TryCommit() noexcept;

  private:
    /// Retrieves the native handle.
    [[nodiscard]] SQLHDBC NativeHandle() const noexcept
    {
        return m_connection->NativeHandle();
    }

  private:
    SqlConnection* m_connection {};
    SqlTransactionMode m_defaultMode {};
    std::source_location m_location {};
};

inline SqlConnection& SqlTransaction::Connection() noexcept
{
    return *m_connection;
}
