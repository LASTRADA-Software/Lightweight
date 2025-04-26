#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Utils.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <print>
#include <sstream>
#include <unordered_map>

#include <yaml-cpp/yaml.h>

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
                       [](Binary const& type) -> std::string { return std::format("SqlBinary", type.size); },
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
                       // TODO distinguish between BINARY and VARBINARY and VARBINARY(MAX) which is for an IMAGE
                       // (https://github.com/LASTRADA-Software/Lightweight/issues/182)
                       [](VarBinary const& type) -> std::string {
                           return std::format("SqlBinary", type.size > 100 ? 100 : type.size);
                       },
                       [](Varchar const& type) -> std::string {
                           if (type.size > 0 && type.size <= SqlOptimalMaxColumnSize)
                               return std::format("SqlAnsiString<{}>", type.size);
                           return std::format("SqlDynamicAnsiString<{}>", 255); // put something else ?
                       },
                   },
                   column.type));
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
    struct TableInfo
    {
        std::stringstream text;
        std::vector<std::string> requiredTables;
    };

  private:
    std::unordered_map<std::string_view, TableInfo> _definitions;
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

    std::string str(std::string_view modelNamespace)
    {
        std::string result;
        for (auto const& [tableName, definition]: _definitions)
        {
            result += std::format("// file: {}.hpp\n", tableName);
            result += headerFileForTheTable(modelNamespace, tableName);
        }
        return result;
    }

    std::string tableIncludes()
    {

        std::string result;
        for (auto const& [tableName, definition]: _definitions)
        {
            result += std::format("#include \"{}.hpp\"\n", tableName);
        }
        return result;
    }

    std::string aliasTableName(std::string_view name) const
    {
        if (_config.makeAliases)
        {
            return FormatTableName(name);
        }
        return std::string { name };
    }

    void printToFiles(std::string_view modelNamespace, std::string_view outputDirectory)
    {

        for (auto const& [tableName, definition]: _definitions)
        {
            const auto fileName = std::format("{}/{}.hpp", outputDirectory, aliasTableName(tableName));
            auto file = std::ofstream(fileName);
            if (!file)
            {
                std::println("Failed to create file {}.", fileName);
                continue;
            }
            file << headerFileForTheTable(modelNamespace, tableName);
        }
    }

    std::string headerFileForTheTable(std::string_view modelNamespace,
                                      std::string_view tableName) // NOLINT(readability-identifier-naming)
    {

        std::stringstream output;
        output << "// File is generated automatically using ddl2cpp \n";
        output << "// SPDX-License-Identifier: Apache-2.0\n";
        output << "#pragma once\n";
        output << "#include <Lightweight/DataMapper/DataMapper.hpp>\n";
        output << "\n";
        output << "using namespace std::string_view_literals;\n";
        output << "\n";

        auto requiredTables = _definitions[tableName].requiredTables;
        std::sort(requiredTables.begin(), requiredTables.end());
        for (auto const& requiredTable: requiredTables)
        {
            output << std::format("#include \"{}.hpp\"\n", requiredTable);
        }

        if (!modelNamespace.empty())
            output << std::format("namespace {}\n{{\n\n", modelNamespace);
        output << "\n";
        output << _definitions[tableName].text.str();
        if (!modelNamespace.empty())
            output << std::format("}} // end namespace {}\n", modelNamespace);

        return output.str();
    }

    std::string example(SqlSchema::Table const& table) const
    {
        std::stringstream exampleEntries;

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
            exampleEntries << std::format("    std::println(\"{{}}\", entry.{}.Value());\n", memberName);
        }

        exampleEntries << "}\n";

        exampleEntries << "\n";

        return exampleEntries.str();
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void PrintTable(SqlSchema::Table const& table)
    {

        auto& definition = _definitions[table.name];
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

        definition.text << std::format("struct {} final\n", aliasTableName(table.name));
        definition.text << std::format("{{\n");
        definition.text << aliasRealTableName(table.name);

        for (auto const& column: table.columns)
        {
            std::string type = MakeType(column);
            const auto memberName = FormatName(column.name, _config.formatType);
            // all foreign keys will be handled in the BelongsTo
            if (column.isPrimaryKey)
            {
                definition.text << std::format("    Field<{}, PrimaryKey::ServerSideAutoIncrement{}> {};\n",
                                               type,
                                               aliasName(column.name),
                                               memberName);
                continue;
            }
            if (column.isForeignKey)
                continue;

            definition.text << std::format("    Field<{}{}> {};\n", type, aliasName(column.name), memberName);
        }

        for (auto const& foreignKey: table.foreignKeys)
        {

            const auto memberName = FormatName(std::string_view { foreignKey.foreignKey.column }, _config.formatType);
            if (foreignKey.primaryKey.columns[0].empty())
            {
                std::println("Foreign key {} has no primary key", memberName);
                continue;
            }

            const auto foreignTableName = aliasTableName(foreignKey.primaryKey.table.table);

            definition.requiredTables.push_back(foreignTableName);
            std::string foreignKeyContraint = std::format(
                "    BelongsTo<&{}> {};\n",
                [&]() {
                    return std::format("{}::{}",
                                       foreignTableName,
                                       FormatName(foreignKey.primaryKey.columns[0], _config.formatType)); // TODO
                }(),
                memberName);

            definition.text << foreignKeyContraint;
        }

        for (SqlSchema::ForeignKeyConstraint const& foreignKey: table.externalForeignKeys)
        {
            (void) foreignKey; // TODO
        }

        std::vector<std::string> fieldNames;
        for (auto const& column: table.columns)
            if (!column.isPrimaryKey && !column.isForeignKey)
                fieldNames.push_back(column.name);

        definition.text << "};\n\n";
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
    std::string connectionString;
    std::string database;
    std::string schema;
    std::string modelNamespace;
    std::string outputDirectory;
    bool createTestTables = false;
    bool makeAliases = false;
    bool generateExample = false;
    FormatType formatType = FormatType::preserve;

    bool showHelpAndExit = false;
};

std::expected<FormatType, std::string> ToFormatType(std::string_view formatType)
{
    auto lowerString = std::string(formatType);
    std::transform(lowerString.begin(), lowerString.end(), lowerString.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (lowerString == "none")
        return FormatType::preserve;

    if (lowerString == "snake_case")
        return FormatType::snakeCase;

    if (lowerString == "camelcase")
        return FormatType::camelCase;

    return std::unexpected(std::format("Unknown naming convention: \"{}\"", formatType));
}

void PrintHelp(std::string_view programName)
{
    std::println("Usage: {} [options] [database] [schema]", programName);
    std::println("Options:");
    std::println("  --trace-sql             Enable SQL tracing");
    std::println("  --connection-string STR ODBC connection string");
    std::println("  --database STR          Database name");
    std::println("  --schema STR            Schema name");
    std::println("  --create-test-tables    Create test tables");
    std::println("  --output STR            Output directory, for every table separate header file will be created");
    std::println("  --generate-example      Generate usage example code using generated header and database connection");
    std::println("  --make-aliases          Create aliases for the tables and members");
    std::println("  --naming-convention STR Naming convention for aliases");
    std::println("                          [none, snake_case, camelCase]");
    std::println("  --help, -h              Display this information");
    std::println("");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<void, std::string> ParseArguments(int argc, char const* argv[], Configuration& config)
{
    using namespace std::string_view_literals;

    int i = 1;

    for (; i < argc; ++i)
    {
        if (argv[i] == "--trace-sql"sv)
            SqlLogger::SetLogger(SqlLogger::TraceLogger());
        else if (argv[i] == "--connection-string"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing connection string");
            config.connectionString = argv[i];
        }
        else if (argv[i] == "--naming-convention"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing naming convention");
            auto namingConvention = ToFormatType(argv[i]);
            if (!namingConvention)
                return std::unexpected(std::move(namingConvention.error()));
            config.formatType = namingConvention.value();
        }
        else if (argv[i] == "--database"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing database name");
            config.database = argv[i];
        }
        else if (argv[i] == "--schema"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing schema name");
            config.schema = argv[i];
        }
        else if (argv[i] == "--create-test-tables"sv)
            config.createTestTables = true;
        else if (argv[i] == "--model-namespace"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing model namespace");
            config.modelNamespace = argv[i];
        }
        else if (argv[i] == "--output"sv)
        {
            if (++i >= argc)
                return std::unexpected("Missing output directory");
            config.outputDirectory = argv[i];
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
            config.showHelpAndExit = true;
            break;
        }
        else if (argv[i] == "--"sv)
        {
            ++i;
            break;
        }
        else
        {
            return std::unexpected(std::format("Unknown option: {}", argv[i]));
        }
    }

    if (i < argc)
        argv[i - 1] = argv[0];

    return {};
}

void ResolveOrderAndPrintTable(auto& printer, const auto& tables)
{
    std::println("Starting to print tables");
    std::unordered_map<size_t, int> numberOfForeignKeys;
    for (auto const& [index, table]: std::views::enumerate(tables))
    {
        std::println("Table: {}, has {} foreign keys", table.name, table.foreignKeys.size());
        numberOfForeignKeys[index] = static_cast<int>(table.foreignKeys.size());
    }

    std::println("Filled number of foreign keys");
    const auto updateForeignKeyCountAfterPrinted = [&](const auto& tablePrinted) {
        for (auto const [index, table]: std::views::enumerate(tables))
        {
            if (table.name == tablePrinted.name)
                numberOfForeignKeys[index] = -1;

            for (auto const& foreignKey: table.foreignKeys)
            {
                if (foreignKey.primaryKey.table.table == tablePrinted.name)
                {
                    numberOfForeignKeys[index]--;
                }
            }
        }
    };

    size_t numberOfPrintedTables = 0;
    std::println("Number of tables: {}", tables.size());
    while (numberOfPrintedTables < tables.size())
    {
        for (auto const& [index, table]: std::views::enumerate(tables))
        {
            if (numberOfForeignKeys[index] == 0)
            {
                numberOfPrintedTables++;
                printer.PrintTable(table);
                updateForeignKeyCountAfterPrinted(table);
                // remove the printed table from the map
            }
        }
        std::println("Number of printed tables: {}", numberOfPrintedTables);
    }
}

template <typename T>
void TryLoadNode(YAML::Node const& node, T& value)
{
    if (node.IsDefined())
        value = node.as<T>();
}

std::expected<Configuration, std::string> LoadConfigFile(std::filesystem::path const& path)
{
    YAML::Node loadedYaml;

    try
    {
        loadedYaml = YAML::LoadFile(path.string());
    }
    catch (const YAML::BadFile& e)
    {
        return std::unexpected(std::format("Failed to open YAML file: {}", e.what()));
    }
    catch (const YAML::ParserException& e)
    {
        return std::unexpected(std::format("Failed to parse YAML file: {}", e.what()));
    }

    auto config = Configuration {};

    TryLoadNode(loadedYaml["ConnectionString"], config.connectionString);
    TryLoadNode(loadedYaml["Database"], config.database);
    TryLoadNode(loadedYaml["Schema"], config.schema);
    TryLoadNode(loadedYaml["ModelNamespace"], config.modelNamespace);
    TryLoadNode(loadedYaml["OutputDirectory"], config.outputDirectory);
    TryLoadNode(loadedYaml["CreateTestTables"], config.createTestTables);
    TryLoadNode(loadedYaml["MakeAliases"], config.makeAliases);
    TryLoadNode(loadedYaml["GenerateExample"], config.generateExample);

    auto const formatType = ToFormatType(loadedYaml["NamingConvention"].as<std::string>());
    if (!formatType)
        return std::unexpected(formatType.error());
    config.formatType = formatType.value();

    return config;
}

std::optional<std::filesystem::path> FindFileUpwards(std::string_view fileName, std::filesystem::path const& startPath)
{
    auto path = startPath;
    while (true)
    {
        auto filePath = path / fileName;
        if (std::filesystem::exists(filePath))
            return filePath;

        if (path == path.root_path())
            break;

        path = path.parent_path();
    }
    return std::nullopt;
}

std::expected<Configuration, std::string> LoadConfigFromFileAndCLI(std::string_view configFileName, int argc, char const* argv[])
{
    // Walk up the directory tree to find the config file, and load it, if found
    auto config = Configuration {};

    auto const foundPath = FindFileUpwards(configFileName, std::filesystem::current_path());
    if (foundPath.has_value())
    {
        std::println("Found config file: {}", foundPath.value().string());
        auto loadedFile = LoadConfigFile(foundPath.value());
        if (!loadedFile)
            return std::unexpected(std::move(loadedFile.error()));

        config = std::move(loadedFile.value());
    }

    // Now, see if we have any command line arguments that override the config file
    auto parsedArguments = ParseArguments(argc, argv, config);
    if (!parsedArguments)
        return std::unexpected(std::move(parsedArguments.error()));

    return std::move(config);
}

auto TimedExecution(std::string_view title, auto&& func)
{
    struct ScopeExit
    {
        std::string_view title;
        std::chrono::high_resolution_clock::time_point const start = std::chrono::high_resolution_clock::now();
        ~ScopeExit()
        {
            auto const end = std::chrono::high_resolution_clock::now();
            auto const duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::println("{} took {:.3} seconds", title, duration / 1000.0);
        }
    } scopeExit { title };
    return func();
}

int main(int argc, char const* argv[])
{
    auto const loadedConfig = LoadConfigFromFileAndCLI("ddl2cpp.yml", argc, argv);
    if (!loadedConfig)
    {
        std::println("Failed to load config: {}", loadedConfig.error());
        return EXIT_FAILURE;
    }

    auto const& config = loadedConfig.value();
    if (config.showHelpAndExit)
    {
        PrintHelp(argv[0]);
        return EXIT_SUCCESS;
    }

    SqlConnection::SetDefaultConnectionString(SqlConnectionString { std::string(config.connectionString) });
    SqlConnection::SetPostConnectedHook(&PostConnectedHook);

    if (config.createTestTables)
        CreateTestTables();

    PrintInfo();
    std::println("Output directory name: {}", config.outputDirectory);

    std::vector<SqlSchema::Table> tables = TimedExecution(
        "Read all tables",
        [&]{ return SqlSchema::ReadAllTables(config.database, config.schema); }
    );
    CxxModelPrinter printer;
    printer.Config().makeAliases = config.makeAliases;
    printer.Config().formatType = config.formatType;
    printer.Config().generateExample = config.generateExample;

    TimedExecution("Resolve Order and print tables", [&] {
        ResolveOrderAndPrintTable(printer, tables);
    });

    if (config.outputDirectory.empty() || config.outputDirectory == "-")
        std::println("{}", printer.str(config.modelNamespace));
    else
    {
        printer.printToFiles(config.modelNamespace, config.outputDirectory);
        std::println("Wrote to directory : {}", config.outputDirectory);
    }

    if (config.generateExample)
    {
        const auto normalizedOutputDir = config.outputDirectory.back() == '/' || config.outputDirectory.back() == '\\'
                                             ? std::string(config.outputDirectory)
                                             : std::string(config.outputDirectory) + '/';
        const auto sourceFileName = normalizedOutputDir + "example.cpp";
        auto file = std::ofstream(sourceFileName); // NOLINT(bugprone-suspicious-stringview-data-usage)

        file << printer.tableIncludes();
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
