// SPDX-License-Identifier: Apache-2.0

#include "tables.hpp"

#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <print>
#include <regex>

auto const inline DefaultTestConnectionString = SqlConnectionString {
    .value = std::format("DRIVER=SQLite3;Database={}", SqlConnectionString::SanitizePwd("mubi_db.sqlite")),
};

class SqlTestFixture
{
  public:
    static inline std::string_view const testDatabaseName = "LightweightTest";

    using MainProgramArgs = std::tuple<int, char**>;

    static std::variant<MainProgramArgs, int> Initialize(int argc, char** argv)
    {
        using namespace std::string_view_literals;
        int i = 1;
        for (; i < argc; ++i)
        {
            if (argv[i] == "--trace-sql"sv)
                SqlLogger::SetLogger(SqlLogger::TraceLogger());
            else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
            {
                std::println("{} [--trace-sql] [--trace-model] [[--] [Catch2 flags ...]]", argv[0]);
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

        auto sqlConnection = SqlConnection();
        if (!sqlConnection.IsAlive())
        {
            std::println("Failed to connect to the database: {}",
                         SqlErrorInfo::fromConnectionHandle(sqlConnection.NativeHandle()));
            std::abort();
        }

        std::println("Running test cases against: {} ({}) (identified as: {})",
                     sqlConnection.ServerName(),
                     sqlConnection.ServerVersion(),
                     sqlConnection.ServerType());

        SqlConnection::SetPostConnectedHook(&SqlTestFixture::PostConnectedHook);

        return MainProgramArgs { argc - (i - 1), argv + (i - 1) };
    }

    static void PostConnectedHook(SqlConnection& connection)
    {
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
        DropAllTablesInDatabase();
    }

    virtual ~SqlTestFixture() = default;

  private:
    static std::vector<std::string> GetAllTableNames()
    {
        auto result = std::vector<std::string>();
        auto stmt = SqlStatement();
        auto const sqlResult = SQLTables(stmt.NativeHandle(),
                                         (SQLCHAR*) testDatabaseName.data(),
                                         (SQLSMALLINT) testDatabaseName.size(),
                                         nullptr,
                                         0,
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

    void DropAllTablesInDatabase()
    {
        auto stmt = SqlStatement {};

        switch (stmt.Connection().ServerType())
        {
            case SqlServerType::MICROSOFT_SQL:
                stmt.ExecuteDirect(std::format("USE {}", "master"));
                stmt.ExecuteDirect(std::format("DROP DATABASE IF EXISTS \"{}\"", testDatabaseName));
                stmt.ExecuteDirect(std::format("CREATE DATABASE \"{}\"", testDatabaseName));
                stmt.ExecuteDirect(std::format("USE {}", testDatabaseName));
                break;
            case SqlServerType::POSTGRESQL:
                if (m_createdTables.empty())
                    m_createdTables = GetAllTableNames();
                for (auto& createdTable: std::views::reverse(m_createdTables))
                    stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\" CASCADE", createdTable));
                break;
            default:
                for (auto& createdTable: std::views::reverse(m_createdTables))
                    stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\"", createdTable));
                break;
        }
        m_createdTables.clear();
    }

    std::vector<std::string> m_createdTables;
};

void longQuery()
{
    auto stmt = SqlStatement {};
    stmt.ExecuteDirect("SELECT user_id, COUNT(movie_id) FROM \"ratings\" GROUP "
                       "by user_id ORDER BY COUNT(movie_id) DESC LIMIT 10");
    while (stmt.FetchRow())
    {
#if 0
    auto user_id = stmt.GetColumn<int>(1);
    auto count = stmt.GetColumn<int>(2);
     std::println("{}|{}", user_id, count);
#endif
    }
}

void count()
{
    auto stmt = SqlStatement {};

    std::vector<std::pair<std::string, int>> count_elements = { { "lists", 80311 },
                                                                { "ratings", 15520005 },
                                                                { "movies", 226575 } };

    auto count_and_compare = [&](std::string_view table, [[maybe_unused]] size_t expected_count) {
        stmt.ExecuteDirect(std::format("SELECT * FROM \"{}\"", table));
        [[maybe_unused]] auto count = 0;
        while (stmt.FetchRow())
        {
            ++count;
        }
#if 0
        if(count != expected_count)
        {
            std::println("Count mismatch for table '{}': expected {}, got {}", table, expected_count, count);
        }
#endif
    };

    count_and_compare("lists", 80311);
    count_and_compare("movies", 226575);
    // count_and_compare("ratings", 15520005); // TODO fix this
}

void iterate()
{
    auto dm = DataMapper();
    auto stmt = SqlStatement { dm.Connection() };
    [[maybe_unused]] int count = 0;
    for (const auto& movie: SqlRowIterator<movies>(stmt))
    {
        ++count;
    }
}

void run()
{
    auto measureTime = [](auto&& f, std::string_view name, size_t measured) {
        auto start = std::chrono::high_resolution_clock::now();
        f();
        auto end = std::chrono::high_resolution_clock::now();
        std::println("{:10} took {:5} ms from sqlite: {:5} ms",
                     name,
                     std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(),
                     measured);
    };
    measureTime(count, "count", 15);
    measureTime(longQuery, "longQuery", 4018);
    measureTime(iterate, "iterate", 0);
}

int main(int argc, char** argv)
{
    std::println("Hello, World! {}", SqlConnectionString::SanitizePwd("benchmark/mubi_db.sqlite"));
    auto result = SqlTestFixture::Initialize(argc, argv);
    if (auto const* exitCode = std::get_if<int>(&result))
        return *exitCode;
    std::tie(argc, argv) = std::get<SqlTestFixture::MainProgramArgs>(result);

    run();
    return 0;
}
