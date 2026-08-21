// SPDX-License-Identifier: Apache-2.0

#include "Async/ThreadOffloadBackend.hpp"
#include "DataBinder/UnicodeConverter.hpp"
#include "SqlConnection.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlQuery.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlStatement.hpp"
#include "TracyProfiler.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <stdexcept>

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
        auto const charCount = std::min(static_cast<size_t>(byteLen) / sizeof(SQLWCHAR), buffer.size() - 1);
        auto const utf8 = ToUtf8(std::u16string_view { reinterpret_cast<char16_t const*>(buffer.data()), charCount });
        return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
    }

    // SQL_COPT_SS_ENCRYPT and its SQL_EN_* values are declared in the Microsoft-specific `msodbcsql.h`
    // (formerly `sqlncli.h`), which unixODBC does not ship and which we must not take a dependency on —
    // Lightweight builds against plain unixODBC on Linux/macOS. Mirror the values instead; they are part
    // of the driver's stable ABI.
    // https://learn.microsoft.com/en-us/sql/relational-databases/native-client-odbc-api/sqlsetconnectattr
    constexpr SQLINTEGER SqlCoptSsEncrypt = 1200 + 23; // SQL_COPT_SS_BASE + 23
    constexpr SQLULEN SqlEncryptOff = 0;               // SQL_EN_OFF
    constexpr SQLULEN SqlEncryptOn = 1;                // SQL_EN_ON

    /// Maps a SqlEncryptionMode onto the SQL_COPT_SS_ENCRYPT attribute value to set.
    ///
    /// @param mode The requested encryption mode.
    /// @return The attribute value, or `std::nullopt` for `DriverDefault` (the attribute is then not
    ///         touched at all, leaving the driver's own configuration in charge).
    constexpr std::optional<SQLULEN> ToOdbcEncryptValue(SqlEncryptionMode mode) noexcept
    {
        switch (mode)
        {
            case SqlEncryptionMode::DriverDefault:
                return std::nullopt;
            case SqlEncryptionMode::Disabled:
                return SqlEncryptOff;
            case SqlEncryptionMode::Enabled:
                return SqlEncryptOn;
        }
        // Unreachable: the switch above is exhaustive over the enumerators, and every arm returns.
        // It stays because a switch over a scoped enum without a default label still leaves the
        // function without a return statement as far as -Wreturn-type is concerned. The coverage
        // report flags this line for that reason, not because a test is missing.
        return std::nullopt;
    }

} // namespace

// =====================================================================================================================

struct SqlConnection::Data
{
    std::chrono::steady_clock::time_point lastUsed; // Last time the connection was used (mostly interesting for
                                                    // idle connections in the connection pool).
    SqlConnectionString connectionString;
    std::unique_ptr<Async::IAsyncBackend> asyncBackend;      // Async execution backend (null until EnableAsync()).
    std::size_t defaultPrefetchDepth = PrefetchDepthDefault; // Rows requested per SQLFetchScroll on the
                                                             // transparent per-row prefetch path (<= 1 disables).
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

    // Capture the DBC-handle diagnostic *before* any subsequent ODBC call on
    // this connection — otherwise the next `SQLAllocHandle(STMT, …)` from the
    // enclosing DataMapper's SqlStatement member would clobber it, and the
    // caller would see an empty `(0) -` error instead of the real driver
    // message (auth failed, DSN not found, …).
    if (connectInfo.has_value() && !Connect(std::move(*connectInfo)))
    {
        // Throwing from the ctor body skips the destructor, so manually
        // release the env/DBC handles and the heap-allocated `Data` block
        // we just allocated above. Snapshot the driver diagnostic first,
        // since `Close()` frees the handle it lives on.
        auto error = LastError();
        Close();
        delete m_data;
        m_data = nullptr;
        throw SqlException(std::move(error));
    }
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
    // Delegate rather than re-format: ToConnectionString() is the single place that knows which fields
    // (including the optional `Encrypt=` keyword) have to survive the flattening into a connection string.
    gDefaultConnectionString = dataSource.ToConnectionString();
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

std::size_t SqlConnection::DefaultPrefetchDepth() const noexcept
{
    return m_data->defaultPrefetchDepth;
}

void SqlConnection::SetDefaultPrefetchDepth(std::size_t depth) noexcept
{
    m_data->defaultPrefetchDepth = depth;
}

void SqlConnection::EnableAsync(Async::IExecutor& dbWorkers, Async::IResumeScheduler& resume)
{
    // TODO(async): once the native event backend lands, select it here via a per-connection
    // capability probe (SQLGetInfo) and fall back to the thread-offload backend.
    EnableAsync(std::make_unique<Async::ThreadOffloadBackend>(dbWorkers, resume));
}

void SqlConnection::EnableAsync(std::unique_ptr<Async::IAsyncBackend> backend)
{
    m_data->asyncBackend = std::move(backend);
}

bool SqlConnection::IsAsyncEnabled() const noexcept
{
    return m_data->asyncBackend != nullptr;
}

Async::IAsyncBackend& SqlConnection::AsyncBackend()
{
    if (!m_data->asyncBackend)
        throw std::logic_error {
            "SqlConnection::AsyncBackend(): asynchronous API used before EnableAsync() was called on this connection."
        };
    return *m_data->asyncBackend;
}

void SqlConnection::DisableAsync() noexcept
{
    m_data->asyncBackend.reset();
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
    ZoneScopedN("SqlConnection::Connect(DataSource)");
    EnsureHandlesAllocated();

    m_data->defaultPrefetchDepth = info.defaultPrefetchDepth;

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

        // SQL_COPT_SS_ENCRYPT is a pre-connect attribute, so it has to be set here rather than in
        // PostConnect() — which also means the server type is not known yet and cannot be branched on.
        // Only an explicit opt-in touches the attribute, so non-SQL-Server drivers are unaffected by
        // default. When the caller *did* opt in and the driver rejects the attribute, the connection is
        // failed rather than established: silently downgrading a requested encrypted connection to
        // plaintext would be the wrong failure mode for a security setting.
        //
        // Caveat: the DBC handle is reused across Connect() calls (see SQLDisconnect above), and ODBC
        // offers no way to restore a connection attribute to "driver default". So reconnecting the same
        // SqlConnection with SqlEncryptionMode::DriverDefault after an explicit opt-in keeps the
        // previously applied value. Use a fresh SqlConnection when the encryption request changes.
        if (auto const encryptValue = ToOdbcEncryptValue(info.encryption))
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            sqlReturn = SQLSetConnectAttrW(m_hDbc, SqlCoptSsEncrypt, (SQLPOINTER) *encryptValue, SQL_IS_UINTEGER);
            if (!SQL_SUCCEEDED(sqlReturn))
            {
                // Not reachable from the test suite: this needs a driver manager that rejects
                // SQL_COPT_SS_ENCRYPT at set time. Both unixODBC and the Windows driver manager defer
                // driver-specific connection attributes until a driver is loaded, so every driver in
                // the matrix accepts the call here and surfaces a refusal from SQLConnectW instead.
                SqlLogger::GetLogger().OnError(LastError());
                return false;
            }
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
    ZoneScopedN("SqlConnection::Connect(ConnectionString)");
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
    // allocation (and from the regex allocation in SanitizePwd below).
    detail::OdbcWideArg wConnectionString { std::string_view {} };
    try
    {
        auto const sanitized = SqlConnectionString::SanitizePwd(m_data->connectionString.value);
        ZoneTextObject(sanitized);
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

bool SqlConnection::RequiresTableRebuildForSchemaChange() const noexcept
{
    return QueryFormatter().RequiresTableRebuildForSchemaChange();
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
