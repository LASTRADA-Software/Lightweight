// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "Api.hpp"
#include "SqlConnectInfo.hpp"
#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "SqlServerType.hpp"

#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

class SqlQueryBuilder;
class SqlMigrationQueryBuilder;
class SqlQueryFormatter;

/// @brief Represents a connection to a SQL database.
class SqlConnection final
{
  public:
    /// @brief Constructs a new SQL connection to the default connection.
    ///
    /// The default connection is set via SetDefaultConnectInfo.
    /// In case the default connection is not set, the connection will fail.
    /// And in case the connection fails, the last error will be set.
    LIGHTWEIGHT_API SqlConnection();

    /// @brief Constructs a new SQL connection to the given connect informaton.
    ///
    /// @param connectInfo The connection information to use. If not provided,
    ///                    no connection will be established.
    LIGHTWEIGHT_API explicit SqlConnection(std::optional<SqlConnectionString> connectInfo);

    LIGHTWEIGHT_API SqlConnection(SqlConnection&& /*other*/) noexcept;
    LIGHTWEIGHT_API SqlConnection& operator=(SqlConnection&& /*other*/) noexcept;
    SqlConnection(SqlConnection const& /*other*/) = delete;
    SqlConnection& operator=(SqlConnection const& /*other*/) = delete;

    /// Destructs this SQL connection object,
    LIGHTWEIGHT_API ~SqlConnection() noexcept;

    /// Retrieves the default connection information.
    LIGHTWEIGHT_API static SqlConnectionString const& DefaultConnectionString() noexcept;

    /// Sets the default connection information.
    ///
    /// @param connectionString The connection information to use.
    LIGHTWEIGHT_API static void SetDefaultConnectionString(SqlConnectionString const& connectionString) noexcept;

    /// Sets the default connection information as SqlConnectionDataSource.
    LIGHTWEIGHT_API static void SetDefaultDataSource(SqlConnectionDataSource const& dataSource) noexcept;

    /// Sets a callback to be called after each connection being established.
    LIGHTWEIGHT_API static void SetPostConnectedHook(std::function<void(SqlConnection&)> hook);

    /// Resets the post connected hook.
    LIGHTWEIGHT_API static void ResetPostConnectedHook();

    /// @brief Retrieves the connection ID.
    ///
    /// This is a unique identifier for the connection, which is useful for debugging purposes.
    /// Note, this ID will not change if the connection is moved nor when it is reused via the connection pool.
    [[nodiscard]] uint64_t ConnectionId() const noexcept
    {
        return m_connectionId;
    }

    /// Closes the connection (attempting to put it back into the connect[[ion pool).
    LIGHTWEIGHT_API void Close() noexcept;

    /// Connects to the given database with the given username and password.
    ///
    /// @retval true if the connection was successful.
    /// @retval false if the connection failed. Use LastError() to retrieve the error information.
    LIGHTWEIGHT_API bool Connect(SqlConnectionDataSource const& info) noexcept;

    /// Connects to the given database with the given username and password.
    ///
    /// @retval true if the connection was successful.
    /// @retval false if the connection failed. Use LastError() to retrieve the error information.
    LIGHTWEIGHT_API bool Connect(SqlConnectionString sqlConnectionString) noexcept;

    /// Retrieves the last error information with respect to this SQL connection handle.
    [[nodiscard]] LIGHTWEIGHT_API SqlErrorInfo LastError() const;

    /// Retrieves the name of the database in use.
    [[nodiscard]] LIGHTWEIGHT_API std::string DatabaseName() const;

    /// Retrieves the name of the user.
    [[nodiscard]] LIGHTWEIGHT_API std::string UserName() const;

    /// Retrieves the name of the server.
    [[nodiscard]] LIGHTWEIGHT_API std::string ServerName() const;

    /// Retrieves the reported server version.
    [[nodiscard]] LIGHTWEIGHT_API std::string ServerVersion() const;

    /// Retrieves the type of the server.
    [[nodiscard]] SqlServerType ServerType() const noexcept;

    /// Retrieves the name of the driver used for this connection.
    [[nodiscard]] std::string const& DriverName() const noexcept;

    /// Retrieves a query formatter suitable for the SQL server being connected.
    [[nodiscard]] SqlQueryFormatter const& QueryFormatter() const noexcept;

    /// Creates a new query builder for the given table, compatible with the current connection.
    ///
    /// @param table The table to query.
    ///              If not provided, the query will be a generic query builder.
    [[nodiscard]] LIGHTWEIGHT_API SqlQueryBuilder Query(std::string_view const& table = {}) const;

    /// Creates a new query builder for the given table with an alias, compatible with the current connection.
    ///
    /// @param table The table to query.
    /// @param tableAlias The alias to use for the table.
    [[nodiscard]] LIGHTWEIGHT_API SqlQueryBuilder QueryAs(std::string_view const& table,
                                                          std::string_view const& tableAlias) const;

    /// Creates a new migration query builder, compatible the current connection.
    [[nodiscard]] LIGHTWEIGHT_API SqlMigrationQueryBuilder Migration() const;

    /// Tests if a transaction is active.
    [[nodiscard]] LIGHTWEIGHT_API bool TransactionActive() const noexcept;

    /// Tests if transactions are allowed.
    [[nodiscard]] LIGHTWEIGHT_API bool TransactionsAllowed() const noexcept;

    /// Tests if the connection is still active.
    [[nodiscard]] LIGHTWEIGHT_API bool IsAlive() const noexcept;

    /// Retrieves the connection information.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnectionString const& ConnectionString() const noexcept;

    /// Retrieves the native handle.
    [[nodiscard]] SQLHDBC NativeHandle() const noexcept
    {
        return m_hDbc;
    }

    /// Retrieves the last time the connection was used.
    [[nodiscard]] LIGHTWEIGHT_API std::chrono::steady_clock::time_point LastUsed() const noexcept;

    /// Sets the last time the connection was used.
    LIGHTWEIGHT_API void SetLastUsed(std::chrono::steady_clock::time_point lastUsed) noexcept;

    /// Checks the result of an SQL operation, and throws an exception if it is not successful.
    LIGHTWEIGHT_API void RequireSuccess(SQLRETURN sqlResult,
                                        std::source_location sourceLocation = std::source_location::current()) const;

  private:
    void PostConnect();

    // Private data members
    SQLHENV m_hEnv {};
    SQLHDBC m_hDbc {};
    uint64_t m_connectionId;
    SqlServerType m_serverType = SqlServerType::UNKNOWN;
    SqlQueryFormatter const* m_queryFormatter {};
    std::string m_driverName;

    struct Data;
    Data* m_data {};
};

inline SqlServerType SqlConnection::ServerType() const noexcept
{
    return m_serverType;
}

inline std::string const& SqlConnection::DriverName() const noexcept
{
    return m_driverName;
}

inline SqlQueryFormatter const& SqlConnection::QueryFormatter() const noexcept
{
    return *m_queryFormatter;
}
