#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Utils.hpp>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <print>

// TODO: have an OdbcConnectionString API to help compose/decompose connection settings
// TODO: move SanitizePwd function into that API, like `string OdbcConnectionString::PrettyPrintSanitized()`

// TODO: get inspired by .NET's Dapper, and EF Core APIs

namespace
{

constexpr auto finally(auto&& cleanupRoutine) noexcept // NOLINT(readability-identifier-naming)
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
                       [](Varchar const& type) -> std::string {
                           if (type.size > 0 && type.size <= SqlOptimalMaxColumnSize)
                               return std::format("SqlAnsiString<{}>", type.size);
                           return std::format("SqlDynamicAnsiString<{}>", 255); // put something else ?
                       },
                   },
                   column.type));
}

std::string MakeVariableName(SqlSchema::FullyQualifiedTableName const& table)
{
    auto name = std::format("{}", table.table);
    name.at(0) = static_cast<char>(std::tolower(name.at(0)));
    return name;
}

// "user_id" into "UserId"
// "task_list_entry" into "TaskListEntry"
// "person" into "Person"
// and so on
std::string FormatTableName(std::string_view name)
{
    std::string result;
    result.reserve(name.size());

    bool makeUpper = true;

    for (auto const& c: name)
    {
        if (c == '_')
        {
            makeUpper = true;
            continue;
        }
        if (makeUpper)
        {
            result += static_cast<char>(std::toupper(c));
            makeUpper = false;
        }
        else
        {
            result += static_cast<char>(std::tolower(c));
        }
    }

    return result;
}

class CxxModelPrinter
{
  private:
    mutable std::vector<std::string> _forwardDeclarations;
    std::stringstream _definitions;
    struct Config
    {
        bool makeAliases = false;
        bool generateExample = false;
        FormatType formatType = FormatType::camelCase;
    } _config;

  public:
    auto& Config() noexcept
    {
        return _config;
    }

    std::string str(std::string_view modelNamespace) const // NOLINT(readability-identifier-naming)
    {
        std::ranges::sort(_forwardDeclarations);

        std::stringstream output;
        output << "// SPDX-License-Identifier: Apache-2.0\n";
        output << "#pragma once\n";
        output << "#include <Lightweight/DataMapper/DataMapper.hpp>\n";
        output << "\n";
        output << "using namespace std::string_view_literals;\n";
        output << "\n";

        if (!modelNamespace.empty())
            output << std::format("namespace {}\n{{\n\n", modelNamespace);
        for (auto const& name: _forwardDeclarations)
            output << std::format("struct {};\n", name);
        output << "\n";
        output << _definitions.str();
        if (!modelNamespace.empty())
            output << std::format("}} // end namespace {}\n", modelNamespace);

        return output.str();
    }

    std::string example(SqlSchema::Table const& table) const
    {
        std::stringstream exampleEntries;
        auto aliasTableName = [&](std::string_view name) {
            if (_config.makeAliases)
            {
                return FormatTableName(name);
            }
            return std::string { name };
        };

        const auto tableName = aliasTableName(table.name);
        exampleEntries << std::format("auto entries{} = dm.Query<{}>().All();\n", tableName, tableName);

        exampleEntries << std::format("for (auto const& entry: entries{})\n", tableName);
        exampleEntries << "{\n";

        const auto column = table.columns[0];
        const auto memberName = FormatName(column.name, _config.formatType);
        if (column.isNullable)
        {
            exampleEntries << std::format("    if (entry.{}.Value())\n", memberName);
            exampleEntries << "    {\n";
            exampleEntries << std::format(
                "        std::println(\"{}: {{}}\", entry.{}.Value().value());\n", memberName, memberName);
            exampleEntries << "    }\n";
        }
        else
        {
            exampleEntries << std::format("    std::println(\"{{}}\", entry.{}.Value());\n", column.name);
        }

        exampleEntries << "}\n";

        exampleEntries << "\n";

        return exampleEntries.str();
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void PrintTable(SqlSchema::Table const& table)
    {

        std::string cxxPrimaryKeys;
        for (auto const& key: table.primaryKeys)
        {
            if (!cxxPrimaryKeys.empty())
                cxxPrimaryKeys += ", ";
            cxxPrimaryKeys += '"' + key + '"';
        }

        // corresponds to the column name in the sql table
        auto aliasName = [&](std::string_view name) {
            if (_config.makeAliases)
            {
                return std::format(", SqlRealName{{\"{}\"}}", name);
            }
            return std::string {};
        };

        auto aliasTableName = [&](std::string_view name) {
            if (_config.makeAliases)
            {
                return FormatTableName(name);
            }
            return std::string { name };
        };

        auto aliasRealTableName = [&](std::string_view name) {
            if (_config.makeAliases)
            {
                return std::format("    static constexpr std::string_view TableName = \"{}\"sv;\n", name);
            }
            return std::string {};
        };

        _forwardDeclarations.push_back(aliasTableName(table.name));
        _definitions << std::format("struct {} final\n", aliasTableName(table.name));
        _definitions << std::format("{{\n");
        _definitions << aliasRealTableName(table.name);

        for (auto const& column: table.columns)
        {
            std::string type = MakeType(column);
            if (column.isPrimaryKey)
            {
                _definitions << std::format("    Field<{}, PrimaryKey::ServerSideAutoIncrement{}> {};\n",
                                            type,
                                            aliasName(column.name),
                                            FormatName(column.name, _config.formatType));
                continue;
            }
            if (column.isForeignKey)
                continue;
            _definitions << std::format(
                "    Field<{}{}> {};\n", type, aliasName(column.name), FormatName(column.name, _config.formatType));
        }

        for (auto const& foreignKey: table.foreignKeys)
        {
            _definitions << std::format(
                "    BelongsTo<&{}> {};\n",
                [&]() {
                    return std::format("{}::{}", foreignKey.primaryKey.table, foreignKey.primaryKey.columns[0]); // TODO
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

        _definitions << "};\n\n";
    }
};

std::string_view PrimaryKeyAutoIncrement(SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::SQLITE:
            return "INTEGER PRIMARY KEY AUTOINCREMENT";
        case SqlServerType::MICROSOFT_SQL:
            return "INT PRIMARY KEY IDENTITY(1,1)";
        case SqlServerType::POSTGRESQL:
            return "SERIAL PRIMARY KEY";
        case SqlServerType::ORACLE:
            return "NUMBER GENERATED BY DEFAULT ON NULL AS IDENTITY PRIMARY KEY";
        case SqlServerType::MYSQL:
            return "INT PRIMARY KEY AUTO_INCREMENT";
        case SqlServerType::UNKNOWN:
            return "INT PRIMARY KEY";
    }
    std::unreachable();
}

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
    stmt.ExecuteDirect(std::format(createStatement, PrimaryKeyAutoIncrement(stmt.Connection().ServerType())));
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
    bool makeAliases = false;
    bool generateExample = false;
    FormatType formatType = FormatType::preserve;
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::variant<Configuration, int> ParseArguments(int argc, char const* argv[])
{
    using namespace std::string_view_literals;
    auto config = Configuration {};

    int i = 1;

    for (; i < argc; ++i)
    {
        std::println("ARGS: {}", argv[i]);
        if (argv[i] == "--trace-sql"sv)
            SqlLogger::SetLogger(SqlLogger::TraceLogger());
        else if (argv[i] == "--connection-string"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            config.connectionString = argv[i];
        }
        else if (argv[i] == "--naming-convention"sv)
        {
            if (++i >= argc)
                return { EXIT_FAILURE };
            if (argv[i] == "none"sv)
                config.formatType = FormatType::preserve;
            else if (argv[i] == "snake_case"sv)
                config.formatType = FormatType::snakeCase;
            else if (argv[i] == "CamelCase"sv)
                config.formatType = FormatType::camelCase;
            else
                return { EXIT_FAILURE };
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
            std::println("Output file name: {}", config.outputFileName);
        }
        else if (argv[i] == "--generate-example"sv)
        {
            config.generateExample = true;
        }
        else if (argv[i] == "--make-aliases"sv)
        {
            config.makeAliases = true;
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
            std::println(
                "  --generate-example      Generate usage example code using generated header and database connection");
            std::println("  --make-aliases          Create aliases for the tables and members");
            std::println("  --naming-convention STR Naming convention for aliases");
            std::println("                          [none, snake_case, CamelCase]");
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
    printer.Config().makeAliases = config.makeAliases;
    printer.Config().formatType = config.formatType;
    printer.Config().generateExample = config.generateExample;

    for (auto const& table: tables)
    {
        printer.PrintTable(table);
    }

    if (config.outputFileName.empty() || config.outputFileName == "-")
        std::println("{}", printer.str(config.modelNamespace));
    else
    {
        auto file = std::ofstream(config.outputFileName.data()); // NOLINT(bugprone-suspicious-stringview-data-usage)
        file << printer.str(config.modelNamespace);
        std::println("Wrote to file : {}", config.outputFileName);
    }

    if (config.generateExample)
    {
        const auto sourceFileName =
            std::string(config.outputFileName.substr(0, config.outputFileName.find_last_of('.'))) + ".cpp";
        auto file = std::ofstream(sourceFileName); // NOLINT(bugprone-suspicious-stringview-data-usage)
        file << std::format("#include \"{}\"\n", [&config] {
            return config.outputFileName.substr(config.outputFileName.find_last_of('/') + 1,
                                                config.outputFileName.find_last_of('.'));
        }());
        file << "#include <cstdlib>\n";
        file << "\n";
        file << "int main()\n";
        file << "{\n";
        file << std::format("SqlConnection::SetDefaultConnectionString(SqlConnectionString {{ \"{}\" }});\n",
                            std::string(config.connectionString));
        file << "\n";
        file << "auto dm = DataMapper{};";
        file << "\n";
        for (auto const& table: tables)
        {
            file << printer.example(table);
        }
        file << "\n";
        file << "return EXIT_SUCCESS;\n";
        file << "}\n";
    }

    return EXIT_SUCCESS;
}
