// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <ostream>
#include <ranges>
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

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x) ^^x

#else
    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x) &x
#endif

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

template <>
struct std::formatter<std::u8string>: std::formatter<std::string>
{
    auto format(std::u8string const& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(
            std::format("{}", (char const*) value.c_str()), // NOLINT(readability-redundant-string-cstr)
            ctx);
    }
};

namespace std
{

// Add support for std::basic_string<WideChar> and std::basic_string_view<WideChar> to std::ostream,
// so that we can get them pretty-printed in REQUIRE() and CHECK() macros.

template <typename WideStringT>
    requires(Lightweight::detail::OneOf<WideStringT,
                                        std::wstring,
                                        std::wstring_view,
                                        std::u16string,
                                        std::u16string_view,
                                        std::u32string,
                                        std::u32string_view>)
ostream& operator<<(ostream& os, WideStringT const& str)
{
    auto constexpr BitsPerChar = sizeof(typename WideStringT::value_type) * 8;
    auto const u8String = Lightweight::ToUtf8(str);
    return os << "UTF-" << BitsPerChar << '{' << "length: " << str.size() << ", characters: " << '"'
              << string_view((char const*) u8String.data(), u8String.size()) << '"' << '}';
}

inline ostream& operator<<(ostream& os, Lightweight::SqlGuid const& guid)
{
    return os << format("SqlGuid({})", guid);
}

} // namespace std

namespace std
{
template <std::size_t Precision, std::size_t Scale>
std::ostream& operator<<(std::ostream& os, Lightweight::SqlNumeric<Precision, Scale> const& value)
{
    return os << std::format("SqlNumeric<{}, {}>({}, {}, {}, {})",
                             Precision,
                             Scale,
                             value.sqlValue.sign,
                             value.sqlValue.precision,
                             value.sqlValue.scale,
                             value.ToUnscaledValue());
}
} // namespace std

// Refer to an in-memory SQLite database (and assuming the sqliteodbc driver is installed)
// See:
// - https://www.sqlite.org/inmemorydb.html
// - http://www.ch-werner.de/sqliteodbc/
// - https://github.com/softace/sqliteodbc
//

// clang-format off
auto inline const DefaultTestConnectionString = Lightweight::SqlConnectionString { //NOLINT(bugprone-throwing-static-initialization)
    // clang-format on
    .value = std::format("DRIVER={};Database={}",
#if defined(_WIN32) || defined(_WIN64)
                         "SQLite3 ODBC Driver",
#else
                         "SQLite3",
#endif
                         "test.db"),
};

class TestSuiteSqlLogger: public Lightweight::SqlLogger::Null
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

    void OnError(Lightweight::SqlError error, std::source_location sourceLocation) override
    {
        WriteWarning("SQL Error: {}", error);
        WriteDetails(sourceLocation);
    }

    void OnError(Lightweight::SqlErrorInfo const& errorInfo, std::source_location sourceLocation) override
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
class ScopedSqlNullLogger: public Lightweight::SqlLogger::Null
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
    static inline std::string testDatabaseName = "LightweightTest"; // NOLINT(bugprone-throwing-static-initialization)
    static inline bool odbcTrace = false;

    using MainProgramArgs = std::tuple<int, char**>;

    static std::variant<MainProgramArgs, int> Initialize(int argc, char** argv)
    {
        Lightweight::SqlLogger::SetLogger(TestSuiteSqlLogger::GetLogger());

        using namespace std::string_view_literals;
        int i = 1;
        for (; i < argc; ++i)
        {
            if (argv[i] == "--trace-sql"sv)
                Lightweight::SqlLogger::SetLogger(Lightweight::SqlLogger::TraceLogger());
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
            std::println("Using ODBC connection string: '{}'", Lightweight::SqlConnectionString::SanitizePwd(s));
            Lightweight::SqlConnection::SetDefaultConnectionString(Lightweight::SqlConnectionString { s });
        }
        else
        {
            // Use an in-memory SQLite3 database by default (for testing purposes)
            std::println("Using default ODBC connection string: '{}'", DefaultTestConnectionString.value);
            Lightweight::SqlConnection::SetDefaultConnectionString(DefaultTestConnectionString);
        }

        Lightweight::SqlConnection::SetPostConnectedHook(&SqlTestFixture::PostConnectedHook);

        auto sqlConnection = Lightweight::SqlConnection();
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

    static void PostConnectedHook(Lightweight::SqlConnection& connection)
    {
        if (odbcTrace)
        {
            auto const traceFile = []() -> std::string_view {
#if !defined(_WIN32) && !defined(_WIN64)
                return "/dev/stdout";
#else
                return "CONOUT$";
#endif
            }();

            SQLHDBC handle = connection.NativeHandle();
            SQLSetConnectAttrA(handle, SQL_ATTR_TRACEFILE, (SQLPOINTER) traceFile.data(), SQL_NTS);
            SQLSetConnectAttrA(handle, SQL_ATTR_TRACE, (SQLPOINTER) SQL_OPT_TRACE_ON, SQL_IS_UINTEGER);
        }

        using Lightweight::SqlServerType;
        switch (connection.ServerType())
        {
            case SqlServerType::SQLITE: {
                auto stmt = Lightweight::SqlStatement { connection };
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
        auto stmt = Lightweight::SqlStatement();
        REQUIRE(stmt.IsAlive());

        // On Github CI, we use the pre-created database "FREEPDB1" for Oracle
        char dbName[100]; // Buffer to store the database name
        SQLSMALLINT dbNameLen {};
        SQLGetInfo(stmt.Connection().NativeHandle(), SQL_DATABASE_NAME, dbName, sizeof(dbName), &dbNameLen);
        if (dbNameLen > 0)
            testDatabaseName = dbName;
        else if (stmt.Connection().ServerType() == Lightweight::SqlServerType::ORACLE)
            testDatabaseName = "FREEPDB1";

        DropAllTablesInDatabase(stmt);
    }

    virtual ~SqlTestFixture() = default;

    static std::string ToString(std::vector<std::string> const& values, std::string_view separator)
    {
        auto result = std::string {};
        for (auto const& value: values)
        {
            if (!result.empty())
                result += separator;
            result += value;
        }
        return result;
    }

    static void DropTableRecursively(Lightweight::SqlStatement& stmt,
                                     Lightweight::SqlSchema::FullyQualifiedTableName const& table)
    {
        auto const dependantTables = Lightweight::SqlSchema::AllForeignKeysTo(stmt, table);
        for (auto const& dependantTable: dependantTables)
            DropTableRecursively(stmt, dependantTable.foreignKey.table);
        stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\"", table.table));
    }

    static void DropAllTablesInDatabase(Lightweight::SqlStatement& stmt)
    {
        using Lightweight::SqlServerType;
        switch (stmt.Connection().ServerType())
        {
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::MYSQL:
                stmt.ExecuteDirect(std::format("USE \"{}\"", testDatabaseName));
                [[fallthrough]];
            case SqlServerType::SQLITE:
            case SqlServerType::ORACLE:
            case SqlServerType::UNKNOWN: {
                auto const tableNames = GetAllTableNames(stmt);
                for (auto const& tableName: tableNames)
                {
                    if (tableName == "sqlite_sequence")
                        continue;

                    DropTableRecursively(stmt,
                                         Lightweight::SqlSchema::FullyQualifiedTableName {
                                             .catalog = {},
                                             .schema = {},
                                             .table = tableName,
                                         });
                }
                break;
            }
            case SqlServerType::POSTGRESQL:
                if (m_createdTables.empty())
                    m_createdTables = GetAllTableNames(stmt);
                for (auto& createdTable: std::views::reverse(m_createdTables))
                    stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\" CASCADE", createdTable));
                break;
        }
        m_createdTables.clear();
    }

  private:
    static std::vector<std::string> GetAllTableNamesForOracle(Lightweight::SqlStatement& stmt)
    {
        auto result = std::vector<std::string> {};
        stmt.Prepare(R"SQL(SELECT table_name
                           FROM user_tables
                           WHERE table_name NOT LIKE '%$%'
                             AND table_name NOT IN ('SCHEDULER_JOB_ARGS_TBL', 'SCHEDULER_PROGRAM_ARGS_TBL', 'SQLPLUS_PRODUCT_PROFILE')
                           ORDER BY table_name)SQL");
        stmt.Execute();
        while (stmt.FetchRow())
        {
            result.emplace_back(stmt.GetColumn<std::string>(1));
        }
        return result;
    }

    static std::vector<std::string> GetAllTableNames(Lightweight::SqlStatement& stmt)
    {
        if (stmt.Connection().ServerType() == Lightweight::SqlServerType::ORACLE)
            return GetAllTableNamesForOracle(stmt);

        using namespace std::string_literals;
        auto result = std::vector<std::string>();
        auto const schemaName = [&] {
            switch (stmt.Connection().ServerType())
            {
                case Lightweight::SqlServerType::MICROSOFT_SQL:
                    return "dbo"s;
                default:
                    return ""s;
            }
        }();
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
inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlText const& value)
{
    return os << std::format("SqlText({})", value.value);
}

inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlDate const& date)
{
    auto const ymd = date.value();
    return os << std::format("SqlDate {{ {}-{}-{} }}", ymd.year(), ymd.month(), ymd.day());
}

inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlTime const& time)
{
    auto const value = time.value();
    return os << std::format("SqlTime {{ {:02}:{:02}:{:02}.{:06} }}",
                             value.hours().count(),
                             value.minutes().count(),
                             value.seconds().count(),
                             value.subseconds().count());
}

inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlDateTime const& datetime)
{
    auto const value = datetime.value();
    auto const totalDays = std::chrono::floor<std::chrono::days>(value);
    auto const ymd = std::chrono::year_month_day { totalDays };
    auto const hms =
        std::chrono::hh_mm_ss<std::chrono::nanoseconds> { std::chrono::floor<std::chrono::nanoseconds>(value - totalDays) };
    return os << std::format("SqlDateTime {{ {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:09} }}",
                             (int) ymd.year(),
                             (unsigned) ymd.month(),
                             (unsigned) ymd.day(),
                             hms.hours().count(),
                             hms.minutes().count(),
                             hms.seconds().count(),
                             hms.subseconds().count());
}

template <std::size_t N, typename T, Lightweight::SqlFixedStringMode Mode>
inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlFixedString<N, T, Mode> const& value)
{
    if constexpr (Mode == Lightweight::SqlFixedStringMode::FIXED_SIZE)
        return os << std::format("SqlFixedString<{}> {{ size: {}, data: '{}' }}", N, value.size(), value.data());
    else if constexpr (Mode == Lightweight::SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED)
        return os << std::format("SqlTrimmedFixedString<{}> {{ '{}' }}", N, value.data());
    else if constexpr (Mode == Lightweight::SqlFixedStringMode::VARIABLE_SIZE)
    {
        if constexpr (std::same_as<T, char>)
            return os << std::format("SqlVariableString<{}> {{ size: {}, '{}' }}", N, value.size(), value.data());
        else
        {
            auto u8String = ToUtf8(std::basic_string_view<T>(value.data(), value.size()));
            return os << std::format("SqlVariableString<{}, {}> {{ size: {}, '{}' }}",
                                     N,
                                     Reflection::TypeNameOf<T>,
                                     value.size(),
                                     (char const*) u8String.c_str()); // NOLINT(readability-redundant-string-cstr)
        }
    }
    else
        return os << std::format("SqlFixedString<{}> {{ size: {}, data: '{}' }}", N, value.size(), value.data());
}

template <std::size_t N, typename T>
inline std::ostream& operator<<(std::ostream& os, Lightweight::SqlDynamicString<N, T> const& value)
{
    if constexpr (std::same_as<T, char>)
        return os << std::format("SqlDynamicString<{}> {{ size: {}, '{}' }}", N, value.size(), value.data());
    else
    {
        auto u8String = ToUtf8(std::basic_string_view<T>(value.data(), value.size()));
        return os << std::format("SqlDynamicString<{}, {}> {{ size: {}, '{}' }}",
                                 N,
                                 Reflection::TypeNameOf<T>,
                                 value.size(),
                                 (char const*) u8String.c_str()); // NOLINT(readability-redundant-string-cstr)
    }
}

[[nodiscard]] inline std::string NormalizeText(std::string_view const& text)
{
    auto result = std::string(text);

    // Remove any newlines and reduce all whitespace to a single space
    result.erase(std::unique(result.begin(), // NOLINT(modernize-use-ranges)
                             result.end(),
                             [](char a, char b) { return std::isspace(a) && std::isspace(b); }),
                 result.end());

    // trim lading and trailing whitespace
    while (!result.empty() && std::isspace(result.front()))
        result.erase(result.begin());

    while (!result.empty() && std::isspace(result.back()))
        result.pop_back();

    return result;
}

[[nodiscard]] inline std::string NormalizeText(std::vector<std::string> const& texts)
{
    auto result = std::string {};
    for (auto const& text: texts)
    {
        if (!result.empty())
            result += '\n';
        result += NormalizeText(text);
    }
    return result;
}

// }}}

inline void CreateEmployeesTable(Lightweight::SqlStatement& stmt,
                                 std::source_location location = std::source_location::current())
{
    stmt.MigrateDirect(
        [](Lightweight::SqlMigrationQueryBuilder& migration) {
            migration.CreateTable("Employees")
                .PrimaryKeyWithAutoIncrement("EmployeeID")
                .RequiredColumn("FirstName", Lightweight::SqlColumnTypeDefinitions::Varchar { 50 })
                .Column("LastName", Lightweight::SqlColumnTypeDefinitions::Varchar { 50 })
                .RequiredColumn("Salary", Lightweight::SqlColumnTypeDefinitions::Integer {});
        },
        location);
}

inline void CreateLargeTable(Lightweight::SqlStatement& stmt)
{
    stmt.MigrateDirect([](Lightweight::SqlMigrationQueryBuilder& migration) {
        auto table = migration.CreateTable("LargeTable");
        for (char c = 'A'; c <= 'Z'; ++c)
        {
            table.Column(std::string(1, c), Lightweight::SqlColumnTypeDefinitions::Varchar { 50 });
        }
    });
}

inline void FillEmployeesTable(Lightweight::SqlStatement& stmt)
{
    stmt.Prepare(stmt.Query("Employees")
                     .Insert()
                     .Set("FirstName", Lightweight::SqlWildcard)
                     .Set("LastName", Lightweight::SqlWildcard)
                     .Set("Salary", Lightweight::SqlWildcard));
    stmt.Execute("Alice", "Smith", 50'000);
    stmt.Execute("Bob", "Johnson", 60'000);
    stmt.Execute("Charlie", "Brown", 70'000);
}

template <typename T = char>
inline auto MakeLargeText(size_t size)
{
    auto text = std::basic_string<T>(size, {});
    std::ranges::generate(text, [i = 0]() mutable { return static_cast<T>('A' + (i++ % 26)); });
    return text;
}

inline bool IsGithubActions()
{
#if defined(_WIN32) || defined(_WIN64)
    char envBuffer[32] {};
    size_t requiredCount = 0;
    return getenv_s(&requiredCount, envBuffer, sizeof(envBuffer), "GITHUB_ACTIONS") == 0
           && std::string_view(envBuffer) == "true" == 0;
#else
    return std::getenv("GITHUB_ACTIONS") != nullptr
           && std::string_view(std::getenv("GITHUB_ACTIONS")) == "true"; // NOLINT(clang-analyzer-core.NonNullParamChecker)
#endif
}

namespace std
{

template <typename T>
ostream& operator<<(ostream& os, optional<T> const& opt)
{
    if (opt.has_value())
        return os << *opt;
    else
        return os << "nullopt";
}

} // namespace std

template <typename T, auto P1, auto P2>
std::ostream& operator<<(std::ostream& os, Lightweight::Field<std::optional<T>, P1, P2> const& field)
{
    if (field.Value())
        return os << std::format("Field<{}> {{ {}, {} }}",
                                 Reflection::TypeNameOf<T>,
                                 *field.Value(),
                                 field.IsModified() ? "modified" : "not modified");
    else
        return os << "NULL";
}

template <typename T, auto P1, auto P2>
std::ostream& operator<<(std::ostream& os, Lightweight::Field<T, P1, P2> const& field)
{
    return os << std::format("Field<{}> {{ ", Reflection::TypeNameOf<T>) << "value: " << field.Value() << "; "
              << (field.IsModified() ? "modified" : "not modified") << " }";
}
