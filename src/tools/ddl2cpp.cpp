#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTraits.hpp>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <print>

// TODO: have an OdbcConnectionString API to help compose/decompose connection settings
// TODO: move SanitizePwd function into that API, like `string OdbcConnectionString::PrettyPrintSanitized()`

// TODO: get inspired by .NET's Dapper, and EF Core APIs

namespace
{

constexpr auto finally(auto&& cleanupRoutine) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    struct Finally
    {
        std::remove_cvref_t<decltype(cleanupRoutine)> cleanup;
        ~Finally()
        {
            cleanup();
        }
    };
    return Finally { std::forward<decltype(cleanupRoutine)>(cleanupRoutine) };
}

std::string MakeType(SqlSchema::Column const& column)
{
    auto optional = [&](auto const& type) {
        if (column.isNullable)
            return std::format("std::optional<{}>", type);
        return std::string { type };
    };

    using namespace SqlColumnTypeDefinitions;
    return optional(
        std::visit(detail::overloaded {
                       [](Bigint const&) -> std::string { return "int64_t"; },
                       [](Binary const& type) -> std::string { return std::format("SqlBinary<{}>", type.size); },
                       [](Bool const&) -> std::string { return "bool"; },
                       [](Char const& type) -> std::string {
                           if (type.size == 1)
                               return "char";
                           return std::format("SqlAnsiString<{}>", type.size);
                       },
                       [](Date const&) -> std::string { return "SqlDate"; },
                       [](DateTime const&) -> std::string { return "SqlDateTime"; },
                       [](Decimal const& type) -> std::string {
                           return std::format("SqlNumeric<{}, {}>", type.precision, type.scale);
                       },
                       [](Guid const&) -> std::string { return "SqlGuid"; },
                       [](Integer const&) -> std::string { return "int32_t"; },
                       [](NChar const& type) -> std::string {
                           if (type.size == 1)
                               return "char16_t";
                           return std::format("SqlUtf16String<{}>", type.size);
                       },
                       [](NVarchar const& type) -> std::string { return std::format("SqlUtf16String<{}>", type.size); },
                       [](Real const&) -> std::string { return "float"; },
                       [](Smallint const&) -> std::string { return "int16_t"; },
                       [](Text const& type) -> std::string { return std::format("SqlAnsiString<{}>", type.size); },
                       [](Time const&) -> std::string { return "SqlTime"; },
                       [](Timestamp const&) -> std::string { return "SqlDateTime"; },
                       [](Tinyint const&) -> std::string { return "uint8_t"; },
                       // TODO distinguish between BINARY and VARBINARY
                       // (https://github.com/LASTRADA-Software/Lightweight/issues/182)
                       [](VarBinary const& type) -> std::string { return std::format("SqlBinary<{}>", type.size); },
                       [](Varchar const& type) -> std::string { return std::format("SqlAnsiString<{}>", type.size); },
                   },
                   column.type));
}

std::string MakeVariableName(SqlSchema::FullyQualifiedTableName const& table)
{
    auto name = std::format("{}", table.table);
    // NOLINTNEXTLINE(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
    name.at(0) = std::tolower(name.at(0));
    return name;
}

constexpr bool isVowel(char c) noexcept
{
    switch (c)
    {
        case 'a':
        case 'e':
        case 'i':
        case 'o':
        case 'u':
            return true;
        default:
            return false;
    }
}

class CxxModelPrinter
{
  private:
    mutable std::vector<std::string> m_forwwardDeclarations;
    std::stringstream m_definitions;

  public:
    std::string str(std::string_view modelNamespace) const
    {
        std::ranges::sort(m_forwwardDeclarations);

        std::stringstream output;
        output << "// SPDX-License-Identifier: Apache-2.0\n";
        output << "#pragma once\n";
        output << "#include <Lightweight/DataMapper/DataMapper.hpp>\n";
        output << "#include <Lightweight/SqlConnection.hpp>\n";
        output << "#include <Lightweight/SqlDataBinder.hpp>\n";
        output << "#include <Lightweight/SqlQuery.hpp>\n";
        output << "#include <Lightweight/SqlQueryFormatter.hpp>\n";
        output << "#include <Lightweight/SqlScopedTraceLogger.hpp>\n";
        output << "#include <Lightweight/SqlStatement.hpp>\n";
        output << "#include <Lightweight/SqlTransaction.hpp>\n";
        output << "\n";

        if (!modelNamespace.empty())
            output << std::format("namespace {}\n{{\n\n", modelNamespace);
        for (auto const& name: m_forwwardDeclarations)
            output << std::format("struct {};\n", name);
        output << "\n";
        output << m_definitions.str();
        if (!modelNamespace.empty())
            output << std::format("}} // end namespace {}\n", modelNamespace);

        return output.str();
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void PrintTable(SqlSchema::Table const& table)
    {
        m_forwwardDeclarations.push_back(table.name);

        std::string cxxPrimaryKeys;
        for (auto const& key: table.primaryKeys)
        {
            if (!cxxPrimaryKeys.empty())
                cxxPrimaryKeys += ", ";
            cxxPrimaryKeys += '"' + key + '"';
        }

        m_definitions << std::format("struct {} final\n", table.name);
        m_definitions << std::format("{{\n");

        int columnPosition = 0;
        for (auto const& column: table.columns)
        {
            ++columnPosition;
            std::string type = MakeType(column);
            if (column.isPrimaryKey)
            {
                m_definitions << std::format(
                    "    Field<{}, PrimaryKey::ServerSideAutoIncrement> {};\n", type, column.name);
                continue;
            }
            if (column.isForeignKey)
                continue;
            m_definitions << std::format("    Field<{}> {};\n", type, column.name);
        }

        columnPosition = 0;
        for (auto const& foreignKey: table.foreignKeys)
        {
            ++columnPosition;
            m_definitions << std::format(
                "    BelongsTo<&{}> {};\n",
                [&]() {
                    return std::format("{}::{}", foreignKey.primaryKey.table, "id"); // TODO
                }(),
                MakeVariableName(foreignKey.primaryKey.table));
        }

        for (SqlSchema::ForeignKeyConstraint const& foreignKey: table.externalForeignKeys)
        {
            (void) foreignKey; // TODO
        }

        std::vector<std::string> fieldNames;
        for (auto const& column: table.columns)
            if (!column.isPrimaryKey && !column.isForeignKey)
                fieldNames.push_back(column.name);

        m_definitions << "};\n\n";
    }
};

void CreateTestTables()
{
    auto constexpr createStatement = R"(
        CREATE TABLE User (
            id              {0},
            fullname        VARCHAR(128) NOT NULL,
            email           VARCHAR(60) NOT NULL
        );
        CREATE TABLE TaskList (
            id              {0},
            user_id         INT NOT NULL,
            CONSTRAINT      fk1 FOREIGN KEY (user_id) REFERENCES user(id)
        );
        CREATE TABLE TaskListEntry (
            id              {0},
            tasklist_id     INT NOT NULL,
            completed       DATETIME NULL,
            task            VARCHAR(255) NOT NULL,
            CONSTRAINT      fk1 FOREIGN KEY (tasklist_id) REFERENCES TaskList(id)
        );
    )";
    auto stmt = SqlStatement();
    stmt.ExecuteDirect(std::format(createStatement, stmt.Connection().Traits().PrimaryKeyAutoIncrement));
}

void PostConnectedHook(SqlConnection& connection)
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

void PrintInfo()
{
    auto c = SqlConnection();
    assert(c.IsAlive());
    std::println("Connected to   : {}", c.DatabaseName());
    std::println("Server name    : {}", c.ServerName());
    std::println("Server version : {}", c.ServerVersion());
    std::println("User name      : {}", c.UserName());
    std::println("");
}

} // end namespace

struct Configuration
{
    std::string_view connectionString;
    std::string_view database;
    std::string_view schema;
    std::string_view modelNamespace;
    std::string_view outputFileName;
    bool createTestTables = false;
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::variant<Configuration, int> ParseArguments(int argc, char const* argv[])
{
    using namespace std::string_view_literals;
    auto config = Configuration {};

    int i = 1;

    for (; i < argc; ++i)
    {
        if (argv[i] == "--trace-sql"sv)
            SqlLogger::SetLogger(SqlLogger::TraceLogger());
        else if (argv[i] == "--connection-string"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.connectionString = argv[i];
        }
        else if (argv[i] == "--database"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.database = argv[i];
        }
        else if (argv[i] == "--schema"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.schema = argv[i];
        }
        else if (argv[i] == "--create-test-tables"sv)
            config.createTestTables = true;
        else if (argv[i] == "--model-namespace"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.modelNamespace = argv[i];
        }
        else if (argv[i] == "--output"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.outputFileName = argv[i];
        }
        else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
        {
            std::println("Usage: {} [options] [database] [schema]", argv[0]);
            std::println("Options:");
            std::println("  --trace-sql             Enable SQL tracing");
            std::println("  --connection-string STR ODBC connection string");
            std::println("  --database STR          Database name");
            std::println("  --schema STR            Schema name");
            std::println("  --create-test-tables    Create test tables");
            std::println("  --output STR            Output file name");
            std::println("  --help, -h              Display this information");
            std::println("");
            return { EXIT_SUCCESS };
        }
        else if (argv[i] == "--"sv)
        {
            ++i;
            break;
        }
        else
        {
            std::println("Unknown option: {}", argv[i]);
            return { EXIT_FAILURE };
        }
    }

    if (i < argc)
        argv[i - 1] = argv[0];

    return { config };
}

int main(int argc, char const* argv[])
{
    auto const configOpt = ParseArguments(argc, argv);
    if (auto const* exitCode = std::get_if<int>(&configOpt))
        return *exitCode;
    auto const config = std::get<Configuration>(configOpt);

    SqlConnection::SetDefaultConnectionString(SqlConnectionString { std::string(config.connectionString) });
    SqlConnection::SetPostConnectedHook(&PostConnectedHook);

    if (config.createTestTables)
        CreateTestTables();

    PrintInfo();

    std::vector<SqlSchema::Table> tables = SqlSchema::ReadAllTables(config.database, config.schema);
    CxxModelPrinter printer;

    for (auto const& table: tables)
        printer.PrintTable(table);

    if (config.outputFileName.empty() || config.outputFileName == "-")
        std::println("{}", printer.str(config.modelNamespace));
    else
    {
        auto file = std::ofstream(config.outputFileName.data());
        file << printer.str(config.modelNamespace);
    }

    return EXIT_SUCCESS;
}
