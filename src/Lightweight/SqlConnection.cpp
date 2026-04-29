// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/UnicodeConverter.hpp"
#include "SqlConnection.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlQuery.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <array>
#include <mutex>

#include <sql.h>

namespace Lightweight
{

using namespace std::chrono_literals;
using namespace std::string_view_literals;

static SqlConnectionString gDefaultConnectionString {};
static std::atomic<uint64_t> gNextConnectionId { 1 };
static std::function<void(SqlConnection&)> gPostConnectedHook {};
static std::mutex gConnectionMutex {};

namespace
{

/// @brief Reads a string-typed `SQLGetInfoW` value into a UTF-8 `std::string`. The
/// W variant returns the buffer length in **bytes** (not characters) per the ODBC
/// spec — divide by `sizeof(SQLWCHAR)` to recover the character count.
std::string GetInfoStringW(SQLHDBC hDbc, SQLUSMALLINT infoType)
{
    std::array<SQLWCHAR, 256> buffer {};
    SQLSMALLINT byteLen {};
    auto const sqlResult = SQLGetInfoW(hDbc, infoType, buffer.data(), sizeof(buffer), &byteLen);
    if (!SQL_SUCCEEDED(sqlResult) || byteLen <= 0)
        return {};
    auto const charCount =
        std::min(static_cast<size_t>(byteLen) / sizeof(SQLWCHAR), buffer.size() - 1);
    auto const utf8 =
        ToUtf8(std::u16string_view { reinterpret_cast<char16_t const*>(buffer.data()), charCount });
    return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
}

} // namespace

// =====================================================================================================================

struct SqlConnection::Data
{
    std::chrono::steady_clock::time_point lastUsed; // Last time the connection was used (mostly interesting for
                                                    // idle connections in the connection pool).
    SqlConnectionString connectionString;
};

SqlConnection::SqlConnection():
    m_connectionId { gNextConnectionId++ },
    m_data { new Data() }
{
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv);
    SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER) SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc);

    if (!Connect(DefaultConnectionString()))
        throw SqlException(LastError());
}

SqlConnection::SqlConnection(std::optional<SqlConnectionString> connectInfo):
    m_connectionId { gNextConnectionId++ },
    m_data { new Data() }
{
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv);
    SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER) SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc);

    if (connectInfo.has_value())
        Connect(std::move(*connectInfo));
}

SqlConnection::SqlConnection(SqlConnection&& other) noexcept:
    m_hEnv { other.m_hEnv },
    m_hDbc { other.m_hDbc },
    m_connectionId { other.m_connectionId },
    m_serverType { other.m_serverType },
    m_queryFormatter { other.m_queryFormatter },
    m_driverName { std::move(other.m_driverName) },
    m_data { other.m_data }
{
    other.m_hEnv = {};
    other.m_hDbc = {};
    other.m_data = nullptr;
}

SqlConnection& SqlConnection::operator=(SqlConnection&& other) noexcept
{
    if (this == &other)
        return *this;

    Close();
    delete m_data;

    m_hEnv = other.m_hEnv;
    m_hDbc = other.m_hDbc;
    m_connectionId = other.m_connectionId;
    m_serverType = other.m_serverType;
    m_queryFormatter = other.m_queryFormatter;
    m_driverName = std::move(other.m_driverName);
    m_data = other.m_data;

    other.m_hEnv = {};
    other.m_hDbc = {};
    other.m_data = nullptr;

    return *this;
}

SqlConnection::~SqlConnection() noexcept
{
    Close();
    delete m_data;
}

SqlConnectionString const& SqlConnection::DefaultConnectionString() noexcept
{
    return gDefaultConnectionString;
}

void SqlConnection::SetDefaultConnectionString(SqlConnectionString const& connectionString) noexcept
{
    gDefaultConnectionString = connectionString;
}

void SqlConnection::SetDefaultDataSource(SqlConnectionDataSource const& dataSource) noexcept
{
    gDefaultConnectionString = SqlConnectionString { .value = std::format("DSN={};UID={};PWD={};TIMEOUT={}",
                                                                          dataSource.datasource,
                                                                          dataSource.username,
                                                                          dataSource.password,
                                                                          dataSource.timeout.count()) };
}

SqlConnectionString const& SqlConnection::ConnectionString() const noexcept
{
    return m_data->connectionString;
}

void SqlConnection::SetLastUsed(std::chrono::steady_clock::time_point lastUsed) noexcept
{
    m_data->lastUsed = lastUsed;
}

std::chrono::steady_clock::time_point SqlConnection::LastUsed() const noexcept
{
    return m_data->lastUsed;
}

void SqlConnection::SetPostConnectedHook(std::function<void(SqlConnection&)> hook)
{
    gPostConnectedHook = std::move(hook);
}

void SqlConnection::ResetPostConnectedHook()
{
    gPostConnectedHook = {};
}

bool SqlConnection::Connect(SqlConnectionDataSource const& info) noexcept
{
    EnsureHandlesAllocated();

    if (m_hDbc)
        SQLDisconnect(m_hDbc);

    // Convert the three input strings to UTF-16 once, *outside* the scoped lock,
    // so the W-variant `SQLConnectW` call below sees properly-decoded Unicode and
    // marks this DBC handle as a Unicode application — which in turn flips the
    // psqlODBC driver out of its ANSI / cp1252 mode. The try/catch defends the
    // noexcept contract: OdbcWideArg allocates a std::u16string and can throw
    // std::bad_alloc, which would otherwise call std::terminate.
    detail::OdbcWideArg wDataSource { std::string_view {} };
    detail::OdbcWideArg wUsername { std::string_view {} };
    detail::OdbcWideArg wPassword { std::string_view {} };
    try
    {
        wDataSource = detail::OdbcWideArg { info.datasource };
        wUsername = detail::OdbcWideArg { info.username };
        wPassword = detail::OdbcWideArg { info.password };
    }
    catch (...)
    {
        return false;
    }

    SQLRETURN sqlReturn {};
    {
        // Serialize ODBC connection establishment to prevent data races in the ODBC driver
        // and OpenSSL during concurrent TLS handshakes (detected by ThreadSanitizer).
        std::scoped_lock const lock(gConnectionMutex);

        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        sqlReturn = SQLSetConnectAttrW(m_hDbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER) info.timeout.count(), 0);
        if (!SQL_SUCCEEDED(sqlReturn))
        {
            SqlLogger::GetLogger().OnError(LastError());
            return false;
        }

        sqlReturn = SQLConnectW(m_hDbc,
                                wDataSource.data(),
                                wDataSource.length(),
                                wUsername.data(),
                                wUsername.length(),
                                wPassword.data(),
                                wPassword.length());
    }
    if (!SQL_SUCCEEDED(sqlReturn))
    {
        SqlLogger::GetLogger().OnError(LastError());
        return false;
    }

    sqlReturn = SQLSetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(sqlReturn))
    {
        SqlLogger::GetLogger().OnError(LastError());
        return false;
    }

    PostConnect();

    SqlLogger::GetLogger().OnConnectionOpened(*this);

    if (gPostConnectedHook)
        gPostConnectedHook(*this);

    return true;
}

// Connects to the given database with the given connection string.
bool SqlConnection::Connect(SqlConnectionString sqlConnectionString) noexcept
{
    EnsureHandlesAllocated();

    if (m_hDbc)
        SQLDisconnect(m_hDbc);

    m_data->connectionString = std::move(sqlConnectionString);

    // Convert the connection string from UTF-8 to UTF-16 *before* the scoped lock so
    // the W-variant `SQLDriverConnectW` call below puts this DBC handle into Unicode-app
    // mode for the rest of its lifetime. Without this, psqlODBC on Windows drops into
    // ANSI mode and runs every SQL_C_CHAR / SQL_C_WCHAR payload through the system
    // codepage, which mangles UTF-8 bytes ≥ 0x80 and UTF-16 surrogate pairs. The
    // try/catch defends the noexcept contract against std::bad_alloc from the UTF-16
    // allocation.
    detail::OdbcWideArg wConnectionString { std::string_view {} };
    try
    {
        wConnectionString = detail::OdbcWideArg { m_data->connectionString.value };
    }
    catch (...)
    {
        return false;
    }

    SQLRETURN sqlResult {};
    {
        // Serialize ODBC connection establishment to prevent data races in the ODBC driver
        // and OpenSSL during concurrent TLS handshakes (detected by ThreadSanitizer).
        std::scoped_lock const lock(gConnectionMutex);
        sqlResult = SQLDriverConnectW(m_hDbc,
                                      (SQLHWND) nullptr,
                                      wConnectionString.data(),
                                      wConnectionString.length(),
                                      nullptr,
                                      0,
                                      nullptr,
                                      SQL_DRIVER_NOPROMPT);
    }
    if (!SQL_SUCCEEDED(sqlResult))
        return false;

    sqlResult = SQLSetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(sqlResult))
        return false;

    PostConnect();
    SqlLogger::GetLogger().OnConnectionOpened(*this);

    if (gPostConnectedHook)
        gPostConnectedHook(*this);

    return true;
}

void SqlConnection::PostConnect()
{
    auto const mappings = std::array {
        std::pair { "Microsoft SQL Server"sv, SqlServerType::MICROSOFT_SQL },
        std::pair { "PostgreSQL"sv, SqlServerType::POSTGRESQL },
        std::pair { "SQLite"sv, SqlServerType::SQLITE },
        std::pair { "MySQL"sv, SqlServerType::MYSQL },
    };

    auto const serverName = ServerName();
    for (auto const& [name, type]: mappings)
    {
        if (serverName.contains(name))
        {
            m_serverType = type;
            break;
        }
    }

    m_queryFormatter = SqlQueryFormatter::Get(m_serverType);

    // Get the driver name from the connection handle.
    m_driverName = GetInfoStringW(m_hDbc, SQL_DRIVER_NAME);

    if (m_serverType == SqlServerType::SQLITE)
    {
        // Set a busy timeout to prevent "database is locked" errors during concurrent access.
        // 60 seconds should be sufficient for most operations.
        SqlStatement stmt(*this);
        [[maybe_unused]] auto cursor = stmt.ExecuteDirect("PRAGMA busy_timeout = 60000");

        // We could also enable WAL mode here, but that changes the database file structure.
        // However, for high-concurrency restoration, it is highly recommended.
        // Let's stick to busy_timeout for now as it's purely a runtime behavior change.
    }
}

void SqlConnection::EnsureHandlesAllocated()
{
    if (m_hEnv)
        return; // Handles already allocated

    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv);
    SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER) SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc);
}

SqlErrorInfo SqlConnection::LastError() const
{
    return SqlErrorInfo::FromConnectionHandle(m_hDbc);
}

void SqlConnection::Close() noexcept
{
    if (!m_hDbc)
        return;

    SqlLogger::GetLogger().OnConnectionClosed(*this);

    SQLDisconnect(m_hDbc);
    SQLFreeHandle(SQL_HANDLE_DBC, m_hDbc);
    SQLFreeHandle(SQL_HANDLE_ENV, m_hEnv);

    m_hDbc = {};
    m_hEnv = {};
}

std::string SqlConnection::DatabaseName() const
{
    return GetInfoStringW(m_hDbc, SQL_DATABASE_NAME);
}

std::string SqlConnection::UserName() const
{
    return GetInfoStringW(m_hDbc, SQL_USER_NAME);
}

std::string SqlConnection::ServerName() const
{
    return GetInfoStringW(m_hDbc, SQL_DBMS_NAME);
}

std::string SqlConnection::ServerVersion() const
{
    return GetInfoStringW(m_hDbc, SQL_DBMS_VER);
}

bool SqlConnection::TransactionActive() const noexcept
{
    SQLUINTEGER state {};
    SQLRETURN const sqlResult = SQLGetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, &state, 0, nullptr);
    return sqlResult == SQL_SUCCESS && state == SQL_AUTOCOMMIT_OFF;
}

bool SqlConnection::TransactionsAllowed() const noexcept
{
    SQLUSMALLINT txn {};
    SQLSMALLINT t {};
    SQLRETURN const rv = SQLGetInfoW(m_hDbc, (SQLUSMALLINT) SQL_TXN_CAPABLE, &txn, sizeof(txn), &t);
    return rv == SQL_SUCCESS && txn != SQL_TC_NONE;
}

bool SqlConnection::IsAlive() const noexcept
{
    SQLUINTEGER state {};
    SQLRETURN const sqlResult = SQLGetConnectAttrW(m_hDbc, SQL_ATTR_CONNECTION_DEAD, &state, 0, nullptr);
    return SQL_SUCCEEDED(sqlResult) && state == SQL_CD_FALSE;
}

void SqlConnection::RequireSuccess(SQLRETURN sqlResult, std::source_location sourceLocation) const
{
    if (SQL_SUCCEEDED(sqlResult))
        return;

    auto const errorInfo = LastError();
    SqlLogger::GetLogger().OnError(errorInfo, sourceLocation);
    throw SqlException(errorInfo);
}

SqlQueryBuilder SqlConnection::Query(std::string_view const& table) const
{
    return SqlQueryBuilder(QueryFormatter(), std::string(table));
}

SqlQueryBuilder SqlConnection::QueryAs(std::string_view const& table, std::string_view const& tableAlias) const
{
    return SqlQueryBuilder(QueryFormatter(), std::string(table), std::string(tableAlias));
}

SqlMigrationQueryBuilder SqlConnection::Migration() const
{
    return SqlMigrationQueryBuilder(QueryFormatter());
}

} // namespace Lightweight
