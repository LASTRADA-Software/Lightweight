// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "../Lightweight/DataBinder/UnicodeConverter.hpp"
#include "../Lightweight/SqlConnectInfo.hpp"
#include "../Lightweight/SqlConnection.hpp"
#include "../Lightweight/SqlDataBinder.hpp"
#include "../Lightweight/SqlLogger.hpp"
#include "../Lightweight/SqlStatement.hpp"
#include "../Lightweight/Utils.hpp"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <ostream>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#if __has_include(<stacktrace>)
    #include <stacktrace>
#endif

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

// NOTE: I've done this preprocessor stuff only to have a single test for UTF-16 (UCS-2) regardless of platform.
using WideChar = std::conditional_t<sizeof(wchar_t) == 2, wchar_t, char16_t>;
using WideString = std::basic_string<WideChar>;
using WideStringView = std::basic_string_view<WideChar>;

#if !defined(_WIN32)
    #define WTEXT(x) (u##x)
#else
    #define WTEXT(x) (L##x)
#endif

#define UNSUPPORTED_DATABASE(stmt, dbType)                                                           \
    if ((stmt).Connection().ServerType() == (dbType))                                                \
    {                                                                                                \
        WARN(std::format("TODO({}): This database is currently unsupported on this test.", dbType)); \
        return;                                                                                      \
    }

namespace std
{

// Add support for std::basic_string<WideChar> and std::basic_string_view<WideChar> to std::ostream,
// so that we can get them pretty-printed in REQUIRE() and CHECK() macros.

template <typename WideStringT>
    requires(detail::OneOf<WideStringT,
                           std::wstring,
                           std::wstring_view,
                           std::u16string,
                           std::u16string_view,
                           std::u32string,
                           std::u32string_view>)
ostream& operator<<(ostream& os, WideStringT const& str)
{
    auto constexpr BitsPerChar = sizeof(typename WideStringT::value_type) * 8;
    auto const u8String = ToUtf8(str);
    return os << "UTF-" << BitsPerChar << '{' << "length: " << str.size() << ", characters: " << '"'
              << string_view((char const*) u8String.data(), u8String.size()) << '"' << '}';
}

inline ostream& operator<<(ostream& os, SqlGuid const& guid)
{
    return os << format("SqlGuid({})", guid);
}

} // namespace std

template <std::size_t Precision, std::size_t Scale>
std::ostream& operator<<(std::ostream& os, SqlNumeric<Precision, Scale> const& value)
{
    return os << std::format("SqlNumeric<{}, {}>({}, {}, {}, {})",
                             Precision,
                             Scale,
                             value.sqlValue.sign,
                             value.sqlValue.precision,
                             value.sqlValue.scale,
                             value.ToUnscaledValue());
}

// Refer to an in-memory SQLite database (and assuming the sqliteodbc driver is installed)
// See:
// - https://www.sqlite.org/inmemorydb.html
// - http://www.ch-werner.de/sqliteodbc/
// - https://github.com/softace/sqliteodbc
//
auto const inline DefaultTestConnectionString = SqlConnectionString {
    .value = std::format("DRIVER={};Database={}",
#if defined(_WIN32) || defined(_WIN64)
                         "SQLite3 ODBC Driver",
#else
                         "SQLite3",
#endif
                         "file::memory:"),
};

class TestSuiteSqlLogger: public SqlLogger::Null
{
  private:
    std::string m_lastPreparedQuery;

    template <typename... Args>
    void WriteInfo(std::format_string<Args...> const& fmt, Args&&... args)
    {
        auto message = std::format(fmt, std::forward<Args>(args)...);
        message = std::format("[{}] {}", "Lightweight", message);
        try
        {
            UNSCOPED_INFO(message);
        }
        catch (...)
        {
            std::println("{}", message);
        }
    }

    template <typename... Args>
    void WriteWarning(std::format_string<Args...> const& fmt, Args&&... args)
    {
        WARN(std::format(fmt, std::forward<Args>(args)...));
    }

  public:
    static TestSuiteSqlLogger& GetLogger() noexcept
    {
        static TestSuiteSqlLogger theLogger;
        return theLogger;
    }

    void OnError(SqlError error, std::source_location sourceLocation) override
    {
        WriteWarning("SQL Error: {}", error);
        WriteDetails(sourceLocation);
    }

    void OnError(SqlErrorInfo const& errorInfo, std::source_location sourceLocation) override
    {
        WriteWarning("SQL Error: {}", errorInfo);
        WriteDetails(sourceLocation);
    }

    void OnWarning(std::string_view const& message) override
    {
        WriteWarning("{}", message);
        WriteDetails(std::source_location::current());
    }

    void OnExecuteDirect(std::string_view const& query) override
    {
        WriteInfo("ExecuteDirect: {}", query);
    }

    void OnPrepare(std::string_view const& query) override
    {
        m_lastPreparedQuery = query;
    }

    void OnExecute(std::string_view const& query) override
    {
        WriteInfo("Execute: {}", query);
    }

    void OnExecuteBatch() override
    {
        WriteInfo("ExecuteBatch: {}", m_lastPreparedQuery);
    }

    void OnFetchRow() override
    {
        WriteInfo("Fetched row");
    }

    void OnFetchEnd() override
    {
        WriteInfo("Fetch end");
    }

  private:
    void WriteDetails(std::source_location sourceLocation)
    {
        WriteInfo("  Source: {}:{}", sourceLocation.file_name(), sourceLocation.line());
        if (!m_lastPreparedQuery.empty())
            WriteInfo("  Query: {}", m_lastPreparedQuery);
        WriteInfo("  Stack trace:");

#if __has_include(<stacktrace>)
        auto stackTrace = std::stacktrace::current(1, 25);
        for (std::size_t const i: std::views::iota(std::size_t(0), stackTrace.size()))
            WriteInfo("    [{:>2}] {}", i, stackTrace[i]);
#endif
    }
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class ScopedSqlNullLogger: public SqlLogger::Null
{
  private:
    SqlLogger& m_previousLogger = SqlLogger::GetLogger();

  public:
    ScopedSqlNullLogger()
    {
        SqlLogger::SetLogger(*this);
    }

    ~ScopedSqlNullLogger() override
    {
        SqlLogger::SetLogger(m_previousLogger);
    }
};

template <typename Getter, typename Callable>
constexpr void FixedPointIterate(Getter const& getter, Callable const& callable)
{
    auto a = getter();
    for (;;)
    {
        callable(a);
        auto b = getter();
        if (a == b)
            break;
        a = std::move(b);
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class SqlTestFixture
{
  public:
    static inline std::string_view const testDatabaseName = "LightweightTest";
    static inline bool odbcTrace = false;

    using MainProgramArgs = std::tuple<int, char**>;

    static std::variant<MainProgramArgs, int> Initialize(int argc, char** argv)
    {
        SqlLogger::SetLogger(TestSuiteSqlLogger::GetLogger());

        using namespace std::string_view_literals;
        int i = 1;
        for (; i < argc; ++i)
        {
            if (argv[i] == "--trace-sql"sv)
                SqlLogger::SetLogger(SqlLogger::TraceLogger());
            else if (argv[i] == "--trace-odbc"sv)
                odbcTrace = true;
            else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
            {
                std::println("{} [--trace-sql] [--trace-odbc] [[--] [Catch2 flags ...]]", argv[0]);
                return { EXIT_SUCCESS };
            }
            else if (argv[i] == "--"sv)
            {
                ++i;
                break;
            }
            else
                break;
        }

        if (i < argc)
            argv[i - 1] = argv[0];

#if defined(_MSC_VER)
        char* envBuffer = nullptr;
        size_t envBufferLen = 0;
        _dupenv_s(&envBuffer, &envBufferLen, "ODBC_CONNECTION_STRING");
        if (auto const* s = envBuffer; s && *s)
#else
        if (auto const* s = std::getenv("ODBC_CONNECTION_STRING"); s && *s)
#endif

        {
            std::println("Using ODBC connection string: '{}'", SqlConnectionString::SanitizePwd(s));
            SqlConnection::SetDefaultConnectionString(SqlConnectionString { s });
        }
        else
        {
            // Use an in-memory SQLite3 database by default (for testing purposes)
            std::println("Using default ODBC connection string: '{}'", DefaultTestConnectionString.value);
            SqlConnection::SetDefaultConnectionString(DefaultTestConnectionString);
        }

        SqlConnection::SetPostConnectedHook(&SqlTestFixture::PostConnectedHook);

        auto sqlConnection = SqlConnection();
        if (!sqlConnection.IsAlive())
        {
            std::println("Failed to connect to the database: {}", sqlConnection.LastError());
            std::abort();
        }

        std::println("Running test cases against: {} ({}) (identified as: {})",
                     sqlConnection.ServerName(),
                     sqlConnection.ServerVersion(),
                     sqlConnection.ServerType());

        return MainProgramArgs { argc - (i - 1), argv + (i - 1) };
    }

    static void PostConnectedHook(SqlConnection& connection)
    {
        if (odbcTrace)
        {
#if !defined(_WIN32) && !defined(_WIN64)
            SQLHDBC handle = connection.NativeHandle();
            SQLSetConnectAttrA(handle, SQL_ATTR_TRACEFILE, (SQLPOINTER) "/dev/stdout", SQL_NTS);
            SQLSetConnectAttrA(handle, SQL_ATTR_TRACE, (SQLPOINTER) SQL_OPT_TRACE_ON, SQL_IS_UINTEGER);
#endif
        }

        switch (connection.ServerType())
        {
            case SqlServerType::SQLITE: {
                auto stmt = SqlStatement { connection };
                // Enable foreign key constraints for SQLite
                stmt.ExecuteDirect("PRAGMA foreign_keys = ON");
                break;
            }
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::POSTGRESQL:
            case SqlServerType::ORACLE:
            case SqlServerType::MYSQL:
            case SqlServerType::UNKNOWN:
                break;
        }
    }

    SqlTestFixture()
    {
        REQUIRE(SqlConnection().IsAlive());
        DropAllTablesInDatabase();
    }

    virtual ~SqlTestFixture() = default;

    static void DropAllTablesInDatabase()
    {
        auto stmt = SqlStatement {};

        switch (stmt.Connection().ServerType())
        {
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::MYSQL:
                stmt.ExecuteDirect(std::format("USE \"{}\"", testDatabaseName));
                [[fallthrough]];
            case SqlServerType::SQLITE:
            case SqlServerType::UNKNOWN:
                FixedPointIterate([] { return GetAllTableNames(); },
                                  [&stmt](auto const& names) {
                                      for (auto const& name: names)
                                          stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\"", name));
                                  });
                break;
            case SqlServerType::POSTGRESQL:
                if (m_createdTables.empty())
                    m_createdTables = GetAllTableNames();
                for (auto& createdTable: std::views::reverse(m_createdTables))
                    stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\" CASCADE", createdTable));
                break;
            case SqlServerType::ORACLE: {
                // Drop user-created tables
                stmt.ExecuteDirect(R"SQL(
                    SELECT user_tables.table_name FROM user_tables
                    LEFT JOIN sys.user_objects ON user_objects.object_type = 'TABLE' AND user_objects.object_name = user_tables.table_name
                    WHERE user_objects.oracle_maintained != 'Y'
                )SQL");
                std::vector<std::string> tableNames;
                while (stmt.FetchRow())
                    tableNames.emplace_back(stmt.GetColumn<std::string>(1));
                for (auto const& tableName: tableNames)
                    stmt.ExecuteDirect(std::format("DROP TABLE \"{}\"", tableName));
                break;
            }
        }
        m_createdTables.clear();
    }

  private:
    static std::vector<std::string> GetAllTableNames()
    {
        using namespace std::string_view_literals;
        auto result = std::vector<std::string>();
        auto stmt = SqlStatement();
        auto const schemaName = stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL ? "dbo"sv : ""sv;
        auto const sqlResult = SQLTables(stmt.NativeHandle(),
                                         (SQLCHAR*) testDatabaseName.data(),
                                         (SQLSMALLINT) testDatabaseName.size(),
                                         (SQLCHAR*) schemaName.data(),
                                         (SQLSMALLINT) schemaName.size(),
                                         nullptr,
                                         0,
                                         (SQLCHAR*) "TABLE",
                                         SQL_NTS);
        if (SQL_SUCCEEDED(sqlResult))
        {
            while (stmt.FetchRow())
            {
                result.emplace_back(stmt.GetColumn<std::string>(3)); // table name
            }
        }
        return result;
    }

    static inline std::vector<std::string> m_createdTables;
};

// {{{ ostream support for Lightweight, for debugging purposes
inline std::ostream& operator<<(std::ostream& os, SqlText const& value)
{
    return os << std::format("SqlText({})", value.value);
}

inline std::ostream& operator<<(std::ostream& os, SqlDate const& date)
{
    auto const ymd = date.value();
    return os << std::format("SqlDate {{ {}-{}-{} }}", ymd.year(), ymd.month(), ymd.day());
}

inline std::ostream& operator<<(std::ostream& os, SqlTime const& time)
{
    auto const value = time.value();
    return os << std::format("SqlTime {{ {:02}:{:02}:{:02}.{:06} }}",
                             value.hours().count(),
                             value.minutes().count(),
                             value.seconds().count(),
                             value.subseconds().count());
}

inline std::ostream& operator<<(std::ostream& os, SqlDateTime const& datetime)
{
    auto const value = datetime.value();
    auto const totalDays = std::chrono::floor<std::chrono::days>(value);
    auto const ymd = std::chrono::year_month_day { totalDays };
    auto const hms = std::chrono::hh_mm_ss<std::chrono::nanoseconds> { std::chrono::floor<std::chrono::nanoseconds>(
        value - totalDays) };
    return os << std::format("SqlDateTime {{ {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:09} }}",
                             (int) ymd.year(),
                             (unsigned) ymd.month(),
                             (unsigned) ymd.day(),
                             hms.hours().count(),
                             hms.minutes().count(),
                             hms.seconds().count(),
                             hms.subseconds().count());
}

template <std::size_t N, typename T, SqlFixedStringMode Mode>
inline std::ostream& operator<<(std::ostream& os, SqlFixedString<N, T, Mode> const& value)
{
    if constexpr (Mode == SqlFixedStringMode::FIXED_SIZE)
        return os << std::format("SqlFixedString<{}> {{ size: {}, data: '{}' }}", N, value.size(), value.data());
    else if constexpr (Mode == SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED)
        return os << std::format("SqlTrimmedFixedString<{}> {{ '{}' }}", N, value.data());
    else if constexpr (Mode == SqlFixedStringMode::VARIABLE_SIZE)
    {
        if constexpr (std::same_as<T, char>)
            return os << std::format("SqlVariableString<{}> {{ size: {}, '{}' }}", N, value.size(), value.data());
        else
        {
            auto u8String = ToUtf8(std::basic_string_view<T>(value.data(), value.size()));
            return os << std::format("SqlVariableString<{}, {}> {{ size: {}, '{}' }}",
                                     N,
                                     Reflection::TypeName<T>,
                                     value.size(),
                                     (char const*) u8String.c_str());
        }
    }
    else
        return os << std::format("SqlFixedString<{}> {{ size: {}, data: '{}' }}", N, value.size(), value.data());
}

// }}}

inline void CreateEmployeesTable(SqlStatement& stmt, std::source_location location = std::source_location::current())
{
    stmt.MigrateDirect(
        [](SqlMigrationQueryBuilder& migration) {
            migration.CreateTable("Employees")
                .PrimaryKeyWithAutoIncrement("EmployeeID")
                .RequiredColumn("FirstName", SqlColumnTypeDefinitions::Varchar { 50 })
                .Column("LastName", SqlColumnTypeDefinitions::Varchar { 50 })
                .RequiredColumn("Salary", SqlColumnTypeDefinitions::Integer {});
        },
        location);
}

inline void CreateLargeTable(SqlStatement& stmt)
{
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        auto table = migration.CreateTable("LargeTable");
        for (char c = 'A'; c <= 'Z'; ++c)
        {
            table.Column(std::string(1, c), SqlColumnTypeDefinitions::Varchar { 50 });
        }
    });
}

inline void FillEmployeesTable(SqlStatement& stmt)
{
    stmt.Prepare(stmt.Query("Employees")
                     .Insert()
                     .Set("FirstName", SqlWildcard)
                     .Set("LastName", SqlWildcard)
                     .Set("Salary", SqlWildcard));
    stmt.Execute("Alice", "Smith", 50'000);
    stmt.Execute("Bob", "Johnson", 60'000);
    stmt.Execute("Charlie", "Brown", 70'000);
}
