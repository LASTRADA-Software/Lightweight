// SPDX-License-Identifier: Apache-2.0

#include "Async/ThreadOffloadBackend.hpp"
#include "DataBinder/UnicodeConverter.hpp"
#include "ImmortalInstance.hpp"
#include "SqlConnection.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlPreparedStatementCache.hpp"
#include "SqlQuery.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlStatement.hpp"
#include "SqlStatistics.hpp"
#include "TracyProfiler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#include <sql.h>

namespace Lightweight
{

using namespace std::chrono_literals;
using namespace std::string_view_literals;

static std::atomic<uint64_t> gNextConnectionId { 1 };
static std::function<void(SqlConnection&)> gPostConnectedHook {};
static std::atomic<SqlStringTruncationMode> gDefaultStringTruncationMode { SqlStringTruncationMode::Truncate };
static std::mutex gConnectionMutex {};
static std::mutex gDefaultSqliteSettingsMutex {};
static SqliteConnectionSettings gDefaultSqliteSettings {};
static std::mutex gDefaultConnectRetrySettingsMutex {};
static SqlRetrySettings gDefaultConnectRetrySettings { .maxRetries = 0 };

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

    /// Tells whether the driver behind @p hDbc receives wide text unchanged.
    ///
    /// The driver echoes, through SQLNativeSqlW and without a server round trip, a statement holding a
    /// CJK character: a driver manager narrowing wide text for an ANSI-only driver cannot carry it. The
    /// Windows driver manager converts wide text for such a driver to the ANSI code page it expects, so
    /// there the wide entry points are always the right ones.
    bool DriverAcceptsWideQueryText([[maybe_unused]] SQLHDBC hDbc) noexcept
    {
#if defined(_WIN32)
        return true;
#else
        constexpr auto probeCharacter = SQLWCHAR { 0x4E2D };
        auto probe = std::array<SQLWCHAR, 10> { 'S', 'E', 'L', 'E', 'C', 'T', ' ', '\'', probeCharacter, '\'' };
        auto echoed = std::array<SQLWCHAR, 32> {};
        SQLINTEGER echoedLength {};
        auto const sqlResult = SQLNativeSqlW(hDbc,
                                             probe.data(),
                                             static_cast<SQLINTEGER>(probe.size()),
                                             echoed.data(),
                                             static_cast<SQLINTEGER>(echoed.size()),
                                             &echoedLength);
        // A driver that cannot echo the text keeps the wide entry points, as before the probe existed.
        if (!SQL_SUCCEEDED(sqlResult) || echoedLength <= 0)
            return true;
        auto const echoedCount = std::min(static_cast<std::size_t>(echoedLength), echoed.size());
        return std::ranges::contains(echoed | std::views::take(echoedCount), probeCharacter);
#endif
    }

    /// The ODBC entry point family a connect call hands its text to.
    enum class OdbcTextEntry : std::uint8_t
    {
        Wide,
        Ansi,
    };

    /// Whether drivers an earlier connection of this process reached accept wide text, by DriverKey().
    struct KnownDriverTextEntries
    {
        std::mutex mutex;
        std::map<std::string, bool> acceptsWideText;
    };

    /// Never destroyed, as connections owned by other statics may still connect during static destruction.
    KnownDriverTextEntries& KnownDrivers() noexcept
    {
        return detail::ImmortalInstance<KnownDriverTextEntries>();
    }

    /// True where the driver manager hands wide text to an ANSI-only driver narrowed one code unit to one
    /// byte. The Windows one converts it to the ANSI code page the driver expects instead.
#if defined(_WIN32)
    constexpr auto DriverManagerNarrowsWideText = false;
#else
    constexpr auto DriverManagerNarrowsWideText = true;
#endif

    /// Names the driver a connect call reaches: its connection string's DRIVER, else its DSN.
    std::string DriverKey(SqlConnectionString const& connectionString)
    {
        auto const attributes = ParseConnectionString(connectionString);
        if (auto const driver = attributes.find("DRIVER"); driver != attributes.end())
            return "DRIVER=" + driver->second;
        if (auto const dataSource = attributes.find("DSN"); dataSource != attributes.end())
            return "DSN=" + dataSource->second;
        return {};
    }

    /// Records what the connection to @p driverKey found, so a later connect decides before connecting.
    /// Best effort: a record that cannot be stored only costs a later connect its decision up front.
    void RememberDriverTextEntry(std::string const& driverKey, bool acceptsWideText) noexcept
    {
        if (driverKey.empty())
            return; // Names no driver, so it cannot tell drivers apart either.
        try
        {
            auto& known = KnownDrivers();
            auto const lock = std::scoped_lock { known.mutex };
            known.acceptsWideText.insert_or_assign(driverKey, acceptsWideText);
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
        }
    }

    /// What an earlier connection of this process found about the driver @p driverKey: whether it
    /// accepts wide text, or nothing when no connection reached it yet.
    [[nodiscard]] std::optional<bool> KnownDriverTextEntry(std::string const& driverKey)
    {
        if (driverKey.empty())
            return std::nullopt;
        auto& known = KnownDrivers();
        auto const lock = std::scoped_lock { known.mutex };
        if (auto const entry = known.acceptsWideText.find(driverKey); entry != known.acceptsWideText.end())
            return entry->second;
        return std::nullopt;
    }

    /// Tells whether any of @p texts holds a byte outside ASCII.
    [[nodiscard]] bool HasNonAsciiText(std::initializer_list<std::string_view> texts) noexcept
    {
        return std::ranges::any_of(texts, [](std::string_view text) {
            return std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
        });
    }

    /// Tells whether a connect that failed with @p sqlState may have failed on its narrowed text. A
    /// server that cannot be reached (class 08) or a timeout (HYT00, HYT01) is not fixed by a retry.
    [[nodiscard]] bool MayHaveFailedOnNarrowedText(std::string_view sqlState) noexcept
    {
        return !sqlState.starts_with("08") && sqlState != "HYT00" && sqlState != "HYT01";
    }

    /// Connects @p hDbc through @p connect, which takes the entry point family to use.
    ///
    /// - Text that is all ASCII reaches every driver intact, so it takes the wide entry point.
    /// - Non-ASCII text takes the entry point an earlier connection found the driver to accept.
    /// - For a driver no connection reached yet, nothing can be asked before connecting, so non-ASCII
    ///   text is connected wide first. If that succeeds and the driver turns out ANSI-only, it connects
    ///   again with the UTF-8 bytes. If it fails where the driver manager narrows wide text, the
    ///   narrowed text (a password, a database path) may be why, so unless the failure says the server
    ///   was not reached or timed out, it tries once with the UTF-8 bytes.
    template <typename Connect>
    SQLRETURN ConnectThroughDriverTextEntry(SQLHDBC hDbc,
                                            std::string const& driverKey,
                                            bool hasNonAsciiText,
                                            Connect const& connect)
    {
        if (!hasNonAsciiText)
            return connect(OdbcTextEntry::Wide);
        if (auto const acceptsWideText = KnownDriverTextEntry(driverKey))
            return connect(*acceptsWideText ? OdbcTextEntry::Wide : OdbcTextEntry::Ansi);

        auto const sqlReturn = connect(OdbcTextEntry::Wide);
        if (SQL_SUCCEEDED(sqlReturn))
        {
            if (DriverAcceptsWideQueryText(hDbc))
                return sqlReturn;
            SQLDisconnect(hDbc);
            return connect(OdbcTextEntry::Ansi);
        }
        if (!DriverManagerNarrowsWideText || !MayHaveFailedOnNarrowedText(SqlErrorInfo::FromConnectionHandle(hDbc).sqlState))
            return sqlReturn;
        return connect(OdbcTextEntry::Ansi);
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
    std::size_t requestedPreparedStatementCacheCapacity =
        PreparedStatementCacheCapacityDefault; // Capacity the user asked for; only handed to the cache
                                               // below once the connected backend is known to support
                                               // prepared-handle reuse.
    SqlPreparedStatementCache preparedStatementCache {
        PreparedStatementCacheCapacityDefault
    }; // Pool of already-prepared statement handles (inactive while its capacity is zero).
    SqliteConnectionSettings sqliteSettings;        // Applied on every Connect() to a SQLite database.
    SqlRetrySettings connectRetrySettings;          // How Connect() retries a transient failure.
    SqlRetrySleeper* connectRetrySleeper = nullptr; // Waits between connect attempts; null = ThreadSleeper().
};

SqlConnection::SqlConnection():
    SqlConnection(std::optional { DefaultConnectionString() })
{
}

SqlConnection::SqlConnection(std::optional<SqlConnectionString> connectInfo):
    SqlConnection(std::move(connectInfo), DefaultSqliteSettings())
{
}

SqlConnection::SqlConnection(std::optional<SqlConnectionString> connectInfo, SqliteConnectionSettings sqliteSettings):
    m_connectionId { gNextConnectionId++ },
    m_data { new Data() }
{
    m_data->sqliteSettings = sqliteSettings;
    m_data->connectRetrySettings = DefaultConnectRetrySettings();
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
    m_acceptsWideQueryText { other.m_acceptsWideQueryText },
    m_stringTruncationMode { other.m_stringTruncationMode },
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
    m_acceptsWideQueryText = other.m_acceptsWideQueryText;
    m_stringTruncationMode = other.m_stringTruncationMode;
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

namespace
{
    // The default connection string, guarded because it is read by every default-constructed
    // SqlConnection - pool workers included - while the application may replace it from another thread.
    struct DefaultConnection
    {
        std::mutex mutex;
        SqlConnectionString value;
        std::atomic<std::uint32_t> generation { 0 };
    };

    DefaultConnection& TheDefaultConnection()
    {
        static auto instance = DefaultConnection {};
        return instance;
    }
} // namespace

SqlConnectionString const& SqlConnection::DefaultConnectionString()
{
    // Per-thread snapshot, so a reader never sees the string change under it while another thread
    // replaces the default, and the reference-returning signature stays safe. Refreshed by generation:
    // reading the generation before copying can only leave the snapshot looking older than it is,
    // which merely refreshes it once more on the next call. A fresh snapshot - generation 0, empty
    // string - already matches the default before it was ever set.
    struct Snapshot
    {
        std::uint32_t generation {};
        SqlConnectionString value;
    };
    thread_local auto snapshot = Snapshot {};

    if (auto const generation = DefaultConnectionStringGeneration(); snapshot.generation != generation)
    {
        auto& defaultConnection = TheDefaultConnection();
        auto const lock = std::scoped_lock { defaultConnection.mutex };
        snapshot.value = defaultConnection.value;
        snapshot.generation = generation;
    }
    return snapshot.value;
}

std::uint32_t SqlConnection::DefaultConnectionStringGeneration() noexcept
{
    return TheDefaultConnection().generation.load(std::memory_order_acquire);
}

void SqlConnection::SetDefaultConnectionString(SqlConnectionString const& connectionString)
{
    auto& defaultConnection = TheDefaultConnection();
    {
        auto const lock = std::scoped_lock { defaultConnection.mutex };
        defaultConnection.value = connectionString;
    }
    // Published after the string, so a reader that sees the new generation also sees the new string.
    defaultConnection.generation.fetch_add(1, std::memory_order_release);
}

void SqlConnection::SetDefaultDataSource(SqlConnectionDataSource const& dataSource)
{
    // Delegate rather than re-format: ToConnectionString() is the single place that knows which fields
    // (including the optional `Encrypt=` keyword) have to survive the flattening into a connection string.
    SetDefaultConnectionString(dataSource.ToConnectionString());
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

std::size_t SqlConnection::PreparedStatementCacheCapacity() const noexcept
{
    return m_data->preparedStatementCache.Capacity();
}

void SqlConnection::SetPreparedStatementCacheCapacity(std::size_t capacity) noexcept
{
    m_data->requestedPreparedStatementCacheCapacity = capacity;
    ApplyPreparedStatementCacheCapacity();
}

void SqlConnection::ApplyPreparedStatementCacheCapacity() noexcept
{
    // A backend whose driver is not known to keep prepared handles re-executable never pools them, so
    // the request is honoured as "inactive" rather than silently risking a stale handle.
    m_data->preparedStatementCache.SetCapacity(
        SupportsPreparedStatementReuse() ? m_data->requestedPreparedStatementCacheCapacity : 0);
}

void SqlConnection::ClearPreparedStatementCache() noexcept
{
    m_data->preparedStatementCache.Clear();
}

SqlPreparedStatementCache& SqlConnection::PreparedStatementCache() noexcept
{
    return m_data->preparedStatementCache;
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

SqlStringTruncationMode SqlConnection::DefaultStringTruncationMode() noexcept
{
    return gDefaultStringTruncationMode.load(std::memory_order_relaxed);
}

void SqlConnection::SetDefaultStringTruncationMode(SqlStringTruncationMode mode) noexcept
{
    gDefaultStringTruncationMode.store(mode, std::memory_order_relaxed);
}

void SqlConnection::SetStringTruncationMode(SqlStringTruncationMode mode)
{
    m_stringTruncationMode = mode;
    ApplyStringTruncationMode();
}

void SqlConnection::ApplyStringTruncationMode()
{
    // Only Microsoft SQL Server distinguishes the two through a session setting; on every other
    // backend the mode is carried but has no statement to apply.
    if (m_serverType != SqlServerType::MICROSOFT_SQL)
        return;

    SqlStatement stmt(*this);
    std::ignore = stmt.ExecuteDirect(m_stringTruncationMode == SqlStringTruncationMode::Truncate ? "SET ANSI_WARNINGS OFF"
                                                                                                 : "SET ANSI_WARNINGS ON");
}

SqliteConnectionSettings SqlConnection::DefaultSqliteSettings()
{
    auto const lock = std::scoped_lock { gDefaultSqliteSettingsMutex };
    return gDefaultSqliteSettings;
}

void SqlConnection::SetDefaultSqliteSettings(SqliteConnectionSettings const& settings)
{
    auto const lock = std::scoped_lock { gDefaultSqliteSettingsMutex };
    gDefaultSqliteSettings = settings;
}

SqlRetrySettings SqlConnection::DefaultConnectRetrySettings()
{
    auto const lock = std::scoped_lock { gDefaultConnectRetrySettingsMutex };
    return gDefaultConnectRetrySettings;
}

void SqlConnection::SetDefaultConnectRetrySettings(SqlRetrySettings const& settings)
{
    auto const lock = std::scoped_lock { gDefaultConnectRetrySettingsMutex };
    gDefaultConnectRetrySettings = settings;
}

SqlRetrySettings const& SqlConnection::ConnectRetrySettings() const noexcept
{
    return m_data->connectRetrySettings;
}

void SqlConnection::SetConnectRetrySettings(SqlRetrySettings const& settings, SqlRetrySleeper* sleeper) noexcept
{
    m_data->connectRetrySettings = settings;
    m_data->connectRetrySleeper = sleeper;
}

SqliteConnectionSettings const& SqlConnection::SqliteSettings() const noexcept
{
    return m_data->sqliteSettings;
}

std::expected<void, SqlErrorInfo> SqlConnection::SetSqliteSettings(SqliteConnectionSettings const& settings)
{
    m_data->sqliteSettings = settings;
    if (!m_hDbc || !m_queryFormatter)
        return {};
    return ApplySqliteSettings();
}

std::expected<void, SqlErrorInfo> SqlConnection::ApplySqliteSettings()
{
    // The formatter decides which statements, if any, realise the settings on this backend; only the
    // SQLite dialect emits any, so this is inert everywhere else. MySQL has no formatter at all.
    if (!m_queryFormatter)
        return {};

    auto const statements = m_queryFormatter->SqliteSettingsStatements(m_data->sqliteSettings);
    if (statements.empty())
        return {};

    try
    {
        auto stmt = SqlStatement { *this };
        for (auto const& sql: statements)
            std::ignore = stmt.ExecuteDirect(sql);
    }
    catch (SqlException const& ex)
    {
        return std::unexpected(ex.info());
    }
    return {};
}

bool SqlConnection::Connect(SqlConnectionDataSource const& info) noexcept
{
    ZoneScopedN("SqlConnection::Connect(DataSource)");
    EnsureHandlesAllocated();
    return ConnectWithRetry([this, &info] { return ConnectOnce(info); });
}

std::expected<void, SqlErrorInfo> SqlConnection::ConnectOnce(SqlConnectionDataSource const& info) noexcept
{
    ZoneScopedN("SqlConnection::ConnectOnce(DataSource)");
    m_data->defaultPrefetchDepth = info.defaultPrefetchDepth;
    m_data->requestedPreparedStatementCacheCapacity = info.preparedStatementCacheCapacity;

    // Handles prepared against the previous session die with the disconnect below.
    m_data->preparedStatementCache.Clear();

    if (m_hDbc)
    {
        detail::UnregisterAnsiOnlyHandle(m_hDbc);
        SQLDisconnect(m_hDbc);
    }

    // Prepare the three input strings once, *outside* the scoped lock: as UTF-16, so the
    // W-variant `SQLConnectW` call below marks this DBC handle as a Unicode application - which
    // flips psqlODBC out of its ANSI / cp1252 mode - and as UTF-8 for an ANSI-only driver off
    // Windows (see ConnectThroughDriverTextEntry). The try/catch defends the noexcept contract
    // against std::bad_alloc from these allocations.
    detail::OdbcWideArg wDataSource { std::string_view {} };
    detail::OdbcWideArg wUsername { std::string_view {} };
    detail::OdbcWideArg wPassword { std::string_view {} };
    detail::OdbcNarrowArg narrowDataSource { std::string_view {} };
    detail::OdbcNarrowArg narrowUsername { std::string_view {} };
    detail::OdbcNarrowArg narrowPassword { std::string_view {} };
    auto driverKey = std::string {};
    try
    {
        wDataSource = detail::OdbcWideArg { info.datasource };
        wUsername = detail::OdbcWideArg { info.username };
        wPassword = detail::OdbcWideArg { info.password };
        narrowDataSource = detail::OdbcNarrowArg { info.datasource };
        narrowUsername = detail::OdbcNarrowArg { info.username };
        narrowPassword = detail::OdbcNarrowArg { info.password };
        driverKey = "DSN=" + info.datasource;
    }
    catch (...)
    {
        return std::unexpected(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HY001", .message = "out of memory" });
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
            auto error = LastError();
            SqlLogger::GetLogger().OnError(error);
            return std::unexpected(std::move(error));
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
            auto const encryptOutcome = detail::CheckOdbcConnectionCall(sqlReturn, m_hDbc);
            if (!encryptOutcome)
            {
                // No driver in the test matrix rejects SQL_COPT_SS_ENCRYPT at set time (both unixODBC
                // and the Windows driver manager defer driver-specific connection attributes until a
                // driver is loaded, and surface a refusal from SQLConnectW instead) - but going
                // through the seam above, rather than a bare SQL_SUCCEEDED(sqlReturn), lets a test
                // install a SqlFaultSource that forces this branch anyway (see #585). The reported
                // diagnostic comes from the outcome rather than the handle; see EffectiveError().
                auto error = encryptOutcome.EffectiveError([this] { return LastError(); });
                SqlLogger::GetLogger().OnError(error);
                return std::unexpected(std::move(error));
            }
        }

        sqlReturn = ConnectThroughDriverTextEntry(
            m_hDbc, driverKey, HasNonAsciiText({ info.datasource, info.username, info.password }), [&](OdbcTextEntry entry) {
                if (entry == OdbcTextEntry::Wide)
                    return SQLConnectW(m_hDbc,
                                       wDataSource.data(),
                                       wDataSource.length(),
                                       wUsername.data(),
                                       wUsername.length(),
                                       wPassword.data(),
                                       wPassword.length());
                return SQLConnect(m_hDbc,
                                  narrowDataSource.data(),
                                  narrowDataSource.length(),
                                  narrowUsername.data(),
                                  narrowUsername.length(),
                                  narrowPassword.data(),
                                  narrowPassword.length());
            });
    }
    // Through the fault seam rather than a bare SQL_SUCCEEDED, so a test can make an attempt fail
    // transiently and drive the retry loop in Connect() without a server that drops connections.
    if (auto const outcome = detail::CheckOdbcConnectionCall(sqlReturn, m_hDbc); !outcome)
    {
        auto error = outcome.EffectiveError([this] { return LastError(); });
        SqlLogger::GetLogger().OnError(error);
        return std::unexpected(std::move(error));
    }

    sqlReturn = SQLSetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(sqlReturn))
    {
        auto error = LastError();
        SqlLogger::GetLogger().OnError(error);
        return std::unexpected(std::move(error));
    }

    if (!PostConnect())
        return std::unexpected(LastError());
    RememberDriverTextEntry(driverKey, m_acceptsWideQueryText);

    SqlLogger::GetLogger().OnConnectionOpened(*this);
    // Not covered by the suite: this is the success tail of the DSN overload, and reaching it needs a
    // *registered* ODBC data source that actually connects - neither CI nor a developer checkout has
    // one, so every test connects through the SqlConnectionString overload instead, whose counterpart
    // of this call (below) is exercised.
    LIGHTWEIGHT_STATS_CONNECTION_OPENED();

    if (gPostConnectedHook)
        gPostConnectedHook(*this);

    return {};
}

// Connects to the given database with the given connection string.
bool SqlConnection::Connect(SqlConnectionString sqlConnectionString) noexcept
{
    ZoneScopedN("SqlConnection::Connect(ConnectionString)");
    EnsureHandlesAllocated();
    m_data->connectionString = std::move(sqlConnectionString);
    return ConnectWithRetry([this] { return ConnectOnce(); });
}

std::expected<void, SqlErrorInfo> SqlConnection::ConnectOnce() noexcept
{
    // One zone per attempt, so retries show up individually in a capture.
    ZoneScopedN("SqlConnection::ConnectOnce(ConnectionString)");
    // Handles prepared against the previous session die with the disconnect below.
    m_data->preparedStatementCache.Clear();

    if (m_hDbc)
    {
        detail::UnregisterAnsiOnlyHandle(m_hDbc);
        SQLDisconnect(m_hDbc);
    }

    // Prepare both forms of the connection string *before* the scoped lock. The wide one,
    // through `SQLDriverConnectW`, puts this DBC handle into Unicode-app mode for the rest of
    // its lifetime; without it psqlODBC on Windows drops into ANSI mode and runs every
    // SQL_C_CHAR / SQL_C_WCHAR payload through the system codepage. The UTF-8 one serves an
    // ANSI-only driver off Windows (see ConnectThroughDriverTextEntry). The try/catch defends
    // the noexcept contract against std::bad_alloc from these allocations and SanitizePwd's.
    detail::OdbcWideArg wConnectionString { std::string_view {} };
    detail::OdbcNarrowArg narrowConnectionString { std::string_view {} };
    auto driverKey = std::string {};
    try
    {
        auto const sanitized = SqlConnectionString::SanitizePwd(m_data->connectionString.value);
        ZoneTextObject(sanitized);
        wConnectionString = detail::OdbcWideArg { m_data->connectionString.value };
        narrowConnectionString = detail::OdbcNarrowArg { m_data->connectionString.value };
        driverKey = DriverKey(m_data->connectionString);
    }
    catch (...)
    {
        return std::unexpected(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HY001", .message = "out of memory" });
    }

    SQLRETURN sqlResult {};
    {
        // Serialize ODBC connection establishment to prevent data races in the ODBC driver
        // and OpenSSL during concurrent TLS handshakes (detected by ThreadSanitizer).
        std::scoped_lock const lock(gConnectionMutex);
        sqlResult = ConnectThroughDriverTextEntry(
            m_hDbc, driverKey, HasNonAsciiText({ m_data->connectionString.value }), [&](OdbcTextEntry entry) {
                if (entry == OdbcTextEntry::Wide)
                    return SQLDriverConnectW(m_hDbc,
                                             (SQLHWND) nullptr,
                                             wConnectionString.data(),
                                             wConnectionString.length(),
                                             nullptr,
                                             0,
                                             nullptr,
                                             SQL_DRIVER_NOPROMPT);
                return SQLDriverConnect(m_hDbc,
                                        (SQLHWND) nullptr,
                                        narrowConnectionString.data(),
                                        narrowConnectionString.length(),
                                        nullptr,
                                        0,
                                        nullptr,
                                        SQL_DRIVER_NOPROMPT);
            });
    }
    // Through the fault seam rather than a bare SQL_SUCCEEDED, so a test can make an attempt fail
    // transiently and drive the retry loop in Connect() without a server that drops connections.
    if (auto const outcome = detail::CheckOdbcConnectionCall(sqlResult, m_hDbc); !outcome)
        return std::unexpected(outcome.EffectiveError([this] { return LastError(); }));

    sqlResult = SQLSetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (!SQL_SUCCEEDED(sqlResult))
        return std::unexpected(LastError());

    if (!PostConnect())
        return std::unexpected(LastError());
    RememberDriverTextEntry(driverKey, m_acceptsWideQueryText);
    SqlLogger::GetLogger().OnConnectionOpened(*this);
    LIGHTWEIGHT_STATS_CONNECTION_OPENED();

    if (gPostConnectedHook)
        gPostConnectedHook(*this);

    return {};
}

template <typename Attempt>
bool SqlConnection::ConnectWithRetry(Attempt const& attempt) noexcept
{
    try
    {
        // The server type is only known once connected, so the dialect-agnostic classifier decides
        // which failures are transient: an unreachable or dropped server (class 08), a timeout.
        auto const policy = SqlRetryPolicy { m_data->connectRetrySettings, nullptr, m_data->connectRetrySleeper };
        auto state = SqlRetryState {};
        while (true)
        {
            auto const outcome = attempt();
            if (outcome)
                return true;

            auto const decision = policy.Decide(outcome.error(), state);
            if (!decision)
                return false;

            state.retriesSoFar += 1;
            state.delaySoFar += decision.delay;
            SqlLogger::GetLogger().OnWarning(std::format("Connecting failed ({}); retrying in {} ms ({}/{}).",
                                                         outcome.error(),
                                                         decision.delay.count(),
                                                         state.retriesSoFar,
                                                         policy.Settings().maxRetries));
            (m_data->connectRetrySleeper ? *m_data->connectRetrySleeper : ThreadSleeper()).Sleep(decision.delay);
        }
    }
    catch (...)
    {
        // Logging or sleeping threw (e.g. std::bad_alloc); Connect() is noexcept, so report failure.
        return false;
    }
}

bool SqlConnection::PostConnect()
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

    // Decided before the first statement below is issued, so every statement takes the right entry point.
    m_acceptsWideQueryText = DriverAcceptsWideQueryText(m_hDbc);
    if (m_acceptsWideQueryText)
        detail::UnregisterAnsiOnlyHandle(m_hDbc);
    else
        detail::RegisterAnsiOnlyHandle(m_hDbc, m_hDbc);

    // The server type is only known now, so this is the earliest point at which the backend capability
    // gate on the prepared-statement cache can be evaluated.
    ApplyPreparedStatementCacheCapacity();

    // The server type is likewise required before the truncation mode can be applied, since it decides
    // whether there is a session setting to issue at all.
    m_stringTruncationMode = DefaultStringTruncationMode();
    ApplyStringTruncationMode();

    // SQLite's busy timeout and journal mode (see SqliteConnectionSettings). A requested journal mode
    // that cannot be applied fails the connect rather than leaving the connection in a mode the
    // application did not ask for.
    if (auto const applied = ApplySqliteSettings(); !applied)
    {
        SqlLogger::GetLogger().OnError(applied.error());
        return false;
    }
    return true;
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
    LIGHTWEIGHT_STATS_CONNECTION_CLOSED();

    // Statement handles are children of the DBC handle: free the pooled ones before it goes away.
    if (m_data)
        m_data->preparedStatementCache.Clear();

    detail::UnregisterAnsiOnlyHandle(m_hDbc);
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

bool SqlConnection::RollbackOpenTransaction() noexcept
{
    if (m_hDbc == SQL_NULL_HDBC || !TransactionActive())
        return true;

    return SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, m_hDbc, SQL_ROLLBACK))
           && SQL_SUCCEEDED(
               SQLSetConnectAttrW(m_hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER));
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
