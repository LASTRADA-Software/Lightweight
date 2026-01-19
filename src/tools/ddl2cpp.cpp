// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataMapper/Field.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>
#include <Lightweight/Utils.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>
#include <utility>

#include <yaml-cpp/yaml.h>

// TODO: have an OdbcConnectionString API to help compose/decompose connection settings
// TODO: move SanitizePwd function into that API, like `string OdbcConnectionString::PrettyPrintSanitized()`

// TODO: get inspired by .NET's Dapper, and EF Core APIs

using namespace std::string_view_literals;
using namespace Lightweight;

using ColumnNameOverrides = std::map<SqlSchema::FullyQualifiedTableColumn, std::string>;

namespace
{

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
        case SqlServerType::MYSQL:
        case SqlServerType::UNKNOWN:
            break;
    }
}

struct Configuration
{
    std::string connectionString;
    std::string database;
    std::string schema;
    std::string modelNamespace;
    std::string outputDirectory;
    std::string cumulativeHeaderFile;
    ColumnNameOverrides columnNameOverrides;
    size_t sqlFixedStringMaxSize = SqlOptimalMaxColumnSize;
    bool forceUnicodeTextColumns = false;
    Lightweight::Tools::CxxModelPrinter::UnicodeTextColumnOverrides unicodeTextColumnOverrides;
    PrimaryKey primaryKeyAssignment = PrimaryKey::ServerSideAutoIncrement;
    bool createTestTables = false;
    bool makeAliases = false;
    bool suppressWarnings = false;
    bool generateExample = false;
    FormatType formatType = FormatType::preserve;

    bool showHelpAndExit = false;
};

constexpr std::string_view ToString(FormatType value) noexcept
{
    switch (value)
    {
        case FormatType::preserve:
            return "preserve";
        case FormatType::camelCase:
            return "CamelCase";
        case FormatType::snakeCase:
            return "snake_case";
    }
    return "unknown";
}

void PrintInfo(Configuration const& config)
{
    auto c = SqlConnection();
    assert(c.IsAlive());
    std::println("Output directory name : {}", config.outputDirectory);
    std::println("Naming convention     : {}", ToString(config.formatType));
    std::println("Model namespace       : {}", config.modelNamespace);
    std::println("Connected to          : {}", c.DatabaseName());
    std::println("Server name           : {}", c.ServerName());
    std::println("Server version        : {}", c.ServerVersion());
    std::println("User name             : {}", c.UserName());
    std::println();
}

std::expected<FormatType, std::string> ToFormatType(std::string_view formatType)
{
    auto lowerString = std::string(formatType);
    std::ranges::transform(lowerString, lowerString.begin(), [](unsigned char c) { return std::tolower(c); });

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
    std::println("  --generate-example      Generate usage example code");
    std::println("                          using generated header and database connection");
    std::println("  --make-aliases          Create aliases for the tables and members");
    std::println("  --naming-convention STR Naming convention for aliases");
    std::println("                          [none, snake_case, camelCase]");
    std::println("  --no-warnings           Suppresses warnings");
    std::println("  --help, -h              Display this information");
    std::println("");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<void, std::string> ParseArguments(int argc, char const* argv[], Configuration& config)
{
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
        else if (argv[i] == "--no-warnings"sv)
        {
            config.suppressWarnings = true;
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

template <typename T>
void TryLoadNode(YAML::Node const& node, T& value)
{
    if (node.IsDefined())
        value = node.as<T>();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<Configuration, std::string> LoadConfigFile(std::filesystem::path const& path)
{
    YAML::Node loadedYaml;

    try
    {
        loadedYaml = YAML::LoadFile(path.string());
    }
    catch (YAML::BadFile const& e)
    {
        return std::unexpected(std::format("Failed to open YAML file: {}", e.what()));
    }
    catch (YAML::ParserException const& e)
    {
        return std::unexpected(std::format("Failed to parse YAML file: {}", e.what()));
    }

    auto config = Configuration {};

    TryLoadNode(loadedYaml["ConnectionString"], config.connectionString);
    TryLoadNode(loadedYaml["CreateTestTables"], config.createTestTables);
    TryLoadNode(loadedYaml["CumulativeHeaderFile"], config.cumulativeHeaderFile);
    TryLoadNode(loadedYaml["Database"], config.database);
    TryLoadNode(loadedYaml["ForceUnicodeTextColumns"], config.forceUnicodeTextColumns);
    TryLoadNode(loadedYaml["GenerateExample"], config.generateExample);
    TryLoadNode(loadedYaml["MakeAliases"], config.makeAliases);
    TryLoadNode(loadedYaml["ModelNamespace"], config.modelNamespace);
    TryLoadNode(loadedYaml["OutputDirectory"], config.outputDirectory);
    TryLoadNode(loadedYaml["Schema"], config.schema);
    TryLoadNode(loadedYaml["SqlFixedStringMaxSize"], config.sqlFixedStringMaxSize);
    TryLoadNode(loadedYaml["SuppressWarnings"], config.suppressWarnings);

    if (auto const unicodeOverridesOn = loadedYaml["ForceUnicodeTextColumnsOn"]; unicodeOverridesOn.IsMap())
    {
        for (auto const& tableNode: unicodeOverridesOn)
        {
            auto const tableName = tableNode.first.as<std::string>();
            auto const tableColumnsNode = tableNode.second;
            auto& unicodeTextColumnOverrides = config.unicodeTextColumnOverrides[tableName];
            for (auto const& columnNode: tableColumnsNode)
                unicodeTextColumnOverrides.emplace(columnNode.as<std::string>());
        }

        if (!config.unicodeTextColumnOverrides.empty())
            for (auto const& [name, columns]: config.unicodeTextColumnOverrides)
                for (auto const& column: columns)
                    std::println("Override {}.{}", name, column);
    }

    if (auto const columnNameOverridesYaml = loadedYaml["ColumnNameOverrides"]; columnNameOverridesYaml.IsMap())
    {
        for (auto const& tableNode: columnNameOverridesYaml)
        {
            auto const tableName = tableNode.first.as<std::string>();
            auto const tableColumnsNode = tableNode.second;
            for (auto const& columnNode: tableColumnsNode)
            {
                auto columnName = columnNode.first.as<std::string>();
                auto columnOverrideName = columnNode.second.as<std::string>();
                auto const tableColumn = SqlSchema::FullyQualifiedTableColumn {
                    .table = SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = tableName },
                    .column = std::move(columnName),
                };
                config.columnNameOverrides[tableColumn] = std::move(columnOverrideName);
            }
        }
    }

    if (loadedYaml["PrimaryKeyAssignment"].IsDefined())
    {
        auto const primaryKey = loadedYaml["PrimaryKeyAssignment"].as<std::string>();
        if (primaryKey == "ServerSide")
            config.primaryKeyAssignment = PrimaryKey::ServerSideAutoIncrement;
        else if (primaryKey == "ClientSide")
            config.primaryKeyAssignment = PrimaryKey::AutoAssign;
        else
            return std::unexpected(std::format("Unknown primary key assignment: {}", primaryKey));
    }

    if (loadedYaml["NamingConvention"].IsDefined())
    {
        auto const formatType = ToFormatType(loadedYaml["NamingConvention"].as<std::string>());
        if (!formatType)
            return std::unexpected(formatType.error());
        config.formatType = formatType.value();
    }

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

std::expected<Configuration, std::string> LoadConfigFromFileAndCLI(std::string_view configFileName,
                                                                   int argc,
                                                                   char const* argv[])
{
    // Walk up the directory tree to find the config file, and load it, if found
    auto config = Configuration {};

    auto const foundPath = FindFileUpwards(configFileName, std::filesystem::current_path());
    if (foundPath.has_value())
    {
        std::println("Using config file {}", foundPath.value().string());
        std::println();
        auto loadedFile = LoadConfigFile(foundPath.value());
        if (!loadedFile)
            return std::unexpected(std::move(loadedFile.error()));

        config = std::move(loadedFile.value());
    }

    // Now, see if we have any command line arguments that override the config file
    auto parsedArguments = ParseArguments(argc, argv, config);
    if (!parsedArguments)
        return std::unexpected(std::move(parsedArguments.error()));

    // Make output directory full path and create it if it doesn't exist
    if (!config.outputDirectory.empty() && config.outputDirectory != "-")
    {
        auto const outputPath = std::filesystem::absolute(config.outputDirectory);
        if (!std::filesystem::exists(outputPath))
            std::filesystem::create_directories(outputPath);
        config.outputDirectory = outputPath.string();
    }

    return config;
}

auto TimedExecution(std::string_view title, auto&& func)
{
    struct ScopeExit
    {
        ScopeExit(ScopeExit const&) = delete;
        ScopeExit(ScopeExit&&) = delete;
        ScopeExit& operator=(ScopeExit const&) = delete;
        ScopeExit& operator=(ScopeExit&&) = delete;
        explicit ScopeExit(std::string_view title):
            title(title)
        {
        }
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

} // end namespace

void GenerateExample(Configuration const& config,
                     Lightweight::Tools::CxxModelPrinter const& cxxModelPrinter,
                     std::vector<SqlSchema::Table> const& tables)
{
    auto const normalizedOutputDir = config.outputDirectory.back() == '/' || config.outputDirectory.back() == '\\'
                                         ? std::string(config.outputDirectory)
                                         : std::string(config.outputDirectory) + '/';
    auto const sourceFileName = normalizedOutputDir + "example.cpp";
    auto file = std::ofstream(sourceFileName); // NOLINT(bugprone-suspicious-stringview-data-usage)

    file << cxxModelPrinter.TableIncludes();
    file << "#include <cstdlib>\n";
    file << "\n";
    file << "int main()\n";
    file << "{\n";
    file << std::format("Lightweight::SqlConnection::SetDefaultConnectionString(SqlConnectionString {{ \"{}\" }});\n",
                        std::string(config.connectionString));
    file << "\n";
    file << "auto dm = Lightweight::DataMapper();";
    file << "\n";
    for (auto const& table: tables)
    {
        file << cxxModelPrinter.Example(table);
    }
    file << "\n";
    file << "return EXIT_SUCCESS;\n";
    file << "}\n";
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

    PrintInfo(config);

    auto stmt = SqlStatement {};

    std::vector<SqlSchema::Table> const tables = TimedExecution("Reading all tables", [&] {
        return SqlSchema::ReadAllTables(
            stmt, config.database, config.schema, [](std::string_view tableName, size_t current, size_t total) {
                std::print("\r\033[K {:>3}% [{}/{}] Reading table schema {}",
                           static_cast<int>((current * 100) / total),
                           current,
                           total,
                           tableName);
                if (current == total)
                    std::println();
            });
    });

    auto cxxModelPrinter = Lightweight::Tools::CxxModelPrinter { Lightweight::Tools::CxxModelPrinter::Config {
        .makeAliases = config.makeAliases,
        .formatType = config.formatType,
        .primaryKeyAssignment = config.primaryKeyAssignment,
        .columnNameOverrides = config.columnNameOverrides,
        .forceUnicodeTextColumns = config.forceUnicodeTextColumns,
        .unicodeTextColumnOverrides = config.unicodeTextColumnOverrides,
        .suppressWarnings = config.suppressWarnings,
        .sqlFixedStringMaxSize = config.sqlFixedStringMaxSize,
    } };

    TimedExecution("Resolving Order and print tables", [&] { cxxModelPrinter.ResolveOrderAndPrintTable(tables); });

    cxxModelPrinter.PrintReport();

    if (config.outputDirectory.empty() || config.outputDirectory == "-")
        std::println("{}", cxxModelPrinter.ToString(config.modelNamespace));
    else
        TimedExecution(std::format("Writing to directory {}", config.outputDirectory),
                       [&] { cxxModelPrinter.PrintToFiles(config.modelNamespace, config.outputDirectory); });

    if (!config.cumulativeHeaderFile.empty())
    {
        auto const success = cxxModelPrinter.PrintCumulativeHeaderFile(config.outputDirectory, config.cumulativeHeaderFile);
        if (!success)
        {
            std::println("Failed to create cumulative header file: {}", success.error());
            return EXIT_FAILURE;
        }
    }

    if (config.generateExample)
        GenerateExample(config, cxxModelPrinter, tables);

    return EXIT_SUCCESS;
}
