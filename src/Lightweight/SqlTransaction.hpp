// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "SqlConnection.hpp"
#include "SqlError.hpp"

#include <format>
#include <source_location>
#include <stdexcept>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

namespace Lightweight
{

/// Represents the isolation level of a SQL transaction.
///
/// The isolation level determines the degree of isolation of data between concurrent transactions.
/// The higher the isolation level, the less likely it is that two transactions will interfere with each other.
///
/// @see https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/transaction-isolation-levels
enum class SqlIsolationMode : std::uint8_t
{
    /// The default isolation level of the SQL driver or DBMS
    DriverDefault = 0,

    /// Transactions are not isolated from each other, allowing to concurrently read uncommitted changes.
    ReadUncommitted = SQL_TXN_READ_UNCOMMITTED,

    /// The transaction waits until the locked writes are committed by other transactions.
    ///
    /// The transaction holds a read lock (if it only reads the row) or write lock (if it updates
    /// or deletes the row) on the current row to prevent other transactions from updating or deleting it.
    ReadCommitted = SQL_TXN_READ_COMMITTED,

    /// The transaction waits until the locked writes are committed by other transactions.
    ///
    /// The transaction holds read locks on all rows it returns to the application and write locks
    /// on all rows it inserts, updates, or deletes.
    RepeatableRead = SQL_TXN_REPEATABLE_READ,

    /// The transaction waits until the locked writes are committed by other transactions.
    ///
    /// The transaction holds a read lock (if it only reads rows) or write lock (if it can update
    /// or delete rows) on the range of rows it affects.
    Serializable = SQL_TXN_SERIALIZABLE,
};

/// Represents the mode of a SQL transaction to be applied, if not done so explicitly.
enum class SqlTransactionMode : std::uint8_t
{
    NONE,
    COMMIT,
    ROLLBACK,
};

/// Represents an exception that occurred during a SQL transaction.
///
/// @see SqlTransaction::Commit(), SqlTransaction::Rollback()
class SqlTransactionException: public std::runtime_error
{
  public:
    explicit SqlTransactionException(std::string const& message) noexcept:
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
    SqlTransaction() = delete;
    SqlTransaction(SqlTransaction const&) = delete;
    SqlTransaction& operator=(SqlTransaction const&) = delete;

    SqlTransaction(SqlTransaction&&) = default;
    SqlTransaction& operator=(SqlTransaction&&) = default;

    /// Construct a new SqlTransaction object, and disable the auto-commit mode, so that the transaction can be
    /// controlled manually.
    LIGHTWEIGHT_API explicit SqlTransaction(SqlConnection& connection,
                                            SqlTransactionMode defaultMode = SqlTransactionMode::COMMIT,
                                            SqlIsolationMode isolationMode = SqlIsolationMode::DriverDefault,
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

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlTransactionMode>: std::formatter<std::string_view>
{
    using SqlTransactionMode = Lightweight::SqlTransactionMode;
    auto format(SqlTransactionMode value, format_context& ctx) const -> format_context::iterator
    {
        using namespace std::string_view_literals;
        string_view name;
        switch (value)
        {
            case SqlTransactionMode::COMMIT:
                name = "Commit";
                break;
            case SqlTransactionMode::ROLLBACK:
                name = "Rollback";
                break;
            case SqlTransactionMode::NONE:
                name = "None";
                break;
        }
        return std::formatter<string_view>::format(name, ctx);
    }
};

template <>
struct std::formatter<Lightweight::SqlIsolationMode>: std::formatter<std::string_view>
{
    using SqlIsolationMode = Lightweight::SqlIsolationMode;
    auto format(SqlIsolationMode value, format_context& ctx) const -> format_context::iterator
    {
        using namespace std::string_view_literals;
        string_view name;
        switch (value)
        {
            case SqlIsolationMode::DriverDefault:
                name = "DriverDefault";
                break;
            case SqlIsolationMode::ReadUncommitted:
                name = "ReadUncommitted";
                break;
            case SqlIsolationMode::ReadCommitted:
                name = "ReadCommitted";
                break;
            case SqlIsolationMode::RepeatableRead:
                name = "RepeatableRead";
                break;
            case SqlIsolationMode::Serializable:
                name = "Serializable";
                break;
        }
        return std::formatter<string_view>::format(name, ctx);
    }
};
