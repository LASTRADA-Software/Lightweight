// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataMapper/Field.hpp>
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
#include <unordered_set>
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string MakeType(
    SqlSchema::Column const& column,
    std::string const& tableName,
    bool forceUnicodeTextColumn,
    std::unordered_map<std::string /*table*/, std::unordered_set<std::string /*column*/>> const& unicodeTextColumnOverrides,
    size_t sqlFixedStringMaxSize)
{
    auto const optional = [&]<typename T>(T&& type) {
        if (column.isNullable)
            return std::format("std::optional<{}>", type);
        return std::string { std::forward<T>(type) };
    };

    auto const shouldForceUnicodeTextColumn = [=] {
        return forceUnicodeTextColumn
               || (unicodeTextColumnOverrides.contains(tableName)
                   && unicodeTextColumnOverrides.at(tableName).contains(column.name));
    };

    using namespace SqlColumnTypeDefinitions;
    return optional(std::visit(
        detail::overloaded {
            [](Bigint const&) -> std::string { return "int64_t"; },
            [](Binary const& type) -> std::string { return std::format("Light::SqlBinary", type.size); },
            [](Bool const&) -> std::string { return "bool"; },
            [&](Char const& type) -> std::string {
                if (type.size == 1)
                {
                    if (shouldForceUnicodeTextColumn())
                        return "wchar_t";
                    else
                        return "char";
                }
                else if (type.size <= sqlFixedStringMaxSize)
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlAnsiString<{}>", type.size);
                }
                else
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlDynamicWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
                }
            },
            [](Date const&) -> std::string { return "Light::SqlDate"; },
            [](DateTime const&) -> std::string { return "Light::SqlDateTime"; },
            [](Decimal const& type) -> std::string {
                return std::format("Light::SqlNumeric<{}, {}>", type.scale, type.precision);
            },
            [](Guid const&) -> std::string { return "Light::SqlGuid"; },
            [](Integer const&) -> std::string { return "int32_t"; },
            [=](NChar const& type) -> std::string {
                if (type.size == 1)
                    return "char16_t";
                else if (type.size <= sqlFixedStringMaxSize)
                    return std::format("Light::SqlUtf16String<{}>", type.size);
                else
                    return std::format("Light::SqlDynamicUtf16String<{}>", type.size);
            },
            [](NVarchar const& type) -> std::string { return std::format("Light::SqlDynamicUtf16String<{}>", type.size); },
            [](Real const&) -> std::string { return "float"; },
            [](Smallint const&) -> std::string { return "int16_t"; },
            [=](Text const& type) -> std::string {
                if (shouldForceUnicodeTextColumn())
                    return std::format("Light::SqlDynamicWideString<{}>", type.size);
                else
                    return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
            },
            [](Time const&) -> std::string { return "Light::SqlTime"; },
            [](Timestamp const&) -> std::string { return "Light::SqlDateTime"; },
            [](Tinyint const&) -> std::string { return "uint8_t"; },
            [](VarBinary const& type) -> std::string { return std::format("Light::SqlDynamicBinary<{}>", type.size); },
            [&](Varchar const& type) -> std::string {
                if (type.size > 0 && type.size <= sqlFixedStringMaxSize)
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlAnsiString<{}>", type.size);
                }
                else
                {
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlDynamicWideString<{}>", type.size);
                    else
                        return std::format("Light::SqlDynamicAnsiString<{}>", type.size);
                }
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
  public:
    struct Config
    {
        std::vector<std::string> stripSuffixes = { "_id", "_nr" };
        bool makeAliases = false;
        FormatType formatType = FormatType::camelCase;
        PrimaryKey primaryKeyAssignment = PrimaryKey::ServerSideAutoIncrement;
        ColumnNameOverrides columnNameOverrides;
        bool forceUnicodeTextColumns = false;
        std::unordered_map<std::string /*table*/, std::unordered_set<std::string /*column*/>> unicodeTextColumnOverrides;
        bool suppressWarnings = false;
        size_t sqlFixedStringMaxSize = SqlOptimalMaxColumnSize;
    };

    explicit CxxModelPrinter(Config config) noexcept:
        _config(std::move(std::move(config)))
    {
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

    std::string tableIncludes() const
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

    [[nodiscard]] std::expected<void, std::string> PrintCumulativeHeaderFile(
        std::filesystem::path const& outputDirectory, std::filesystem::path const& cumulativeHeaderFile)
    {
        auto const headerFilePath = outputDirectory / cumulativeHeaderFile;

        auto file = std::ofstream(headerFilePath.string());
        if (!file)
            return std::unexpected(std::format("Failed to create file {}.", headerFilePath.string()));

        file << "// File is automatically generated using ddl2cpp.\n"
             << "#pragma once\n"
             << "\n";

        auto includes = std::vector<std::string> {};
        includes.reserve(_definitions.size());
        for (auto const& [tableName, definition]: _definitions)
            includes.emplace_back(std::format("{}.hpp", aliasTableName(tableName)));

        std::ranges::sort(includes);

        for (auto const& include: includes)
            file << std::format("#include \"{}\"\n", include);

        return {};
    }

    void printToFiles(std::string_view modelNamespace, std::string_view outputDirectory)
    {
        for (auto const& [tableName, definition]: _definitions)
        {
            auto const fileName = std::format("{}/{}.hpp", outputDirectory, aliasTableName(tableName));
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
        output << "// File is automatically generated using ddl2cpp.\n";
        output << "#pragma once\n";
        output << "\n";

        auto requiredTables = _definitions[tableName].requiredTables;
        std::ranges::sort(requiredTables);
        for (auto const& requiredTable: requiredTables)
            output << std::format("#include \"{}.hpp\"\n", requiredTable);
        if (!std::empty(requiredTables))
            output << '\n';

        output << "#include <Lightweight/DataMapper/DataMapper.hpp>\n";
        output << "\n";

        if (!modelNamespace.empty())
            output << std::format("namespace {}\n{{\n", modelNamespace);

        output << "\n";
        output << _definitions[tableName].text.str();
        if (!modelNamespace.empty())
            output << std::format("}} // end namespace {}\n", modelNamespace);

        return output.str();
    }

    std::string example(SqlSchema::Table const& table) const
    {
        std::stringstream exampleEntries;

        auto const tableName = aliasTableName(table.name);
        exampleEntries << std::format("auto entries{} = dm.Query<{}>().First(10);\n", tableName, tableName);

        exampleEntries << std::format("for (auto const& entry: entries{})\n", tableName);
        exampleEntries << "{\n";

        exampleEntries << std::format("    std::println(\"{{}}\", Lightweight::DataMapper::Inspect(entry));\n");

        exampleEntries << "}\n";

        exampleEntries << "\n";

        return exampleEntries.str();
    }

    static auto ToLower(std::string value) -> std::string
    {
        std::ranges::transform(value, value.begin(), [](auto c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    auto StripSuffix(std::string name) -> std::string
    {
        std::string const lowerName = ToLower(name);
        for (auto const& suffix: _config.stripSuffixes)
        {
            if (lowerName.ends_with(suffix))
                return name.substr(0, name.length() - suffix.length());
        }
        return name;
    }

    static auto SanitizeName(std::string name) -> std::string
    {
        static constexpr auto cxxKeywords =
            std::array { "alignas"sv,
                         "alignof"sv,
                         "asm"sv,
                         "auto"sv,
                         "bool"sv,
                         "break"sv,
                         "case"sv,
                         "catch"sv,
                         "char"sv,
                         "char16_t"sv,
                         "char32_t"sv,
                         "char8_t"sv,
                         "class"sv,
                         "co_await"sv,
                         "co_return"sv,
                         "co_yield"sv,
                         "concept"sv,
                         "const"sv,
                         "const_cast"sv,
                         "consteval"sv,
                         "constexpr"sv,
                         "constinit"sv,
                         "continue"sv,
                         "decltype"sv,
                         "default"sv,
                         "delete"sv,
                         "do"sv,
                         "double"sv,
                         "dynamic_cast"sv,
                         "else"sv,
                         "enum"sv,
                         "explicit"sv,
                         "export"sv, // For modules
                         "extern"sv,
                         "false"sv,
                         "float"sv,
                         "for"sv,
                         "friend"sv,
                         "goto"sv,
                         "if"sv,
                         "import"sv, // For modules
                         "inline"sv,
                         "int"sv,
                         "long"sv,
                         "module"sv, // For modules
                         "mutable"sv,
                         "namespace"sv,
                         "new"sv,
                         "noexcept"sv,
                         "nullptr"sv,
                         "operator"sv,
                         "private"sv,
                         "protected"sv,
                         "public"sv,
                         "register"sv, // Deprecated in C++11, reserved in C++17, removed in C++23 but still reserved.
                         "reinterpret_cast"sv,
                         "requires"sv,
                         "return"sv,
                         "short"sv,
                         "signed"sv,
                         "sizeof"sv,
                         "static"sv,
                         "static_assert"sv,
                         "static_cast"sv,
                         "struct"sv,
                         "switch"sv,
                         "template"sv,
                         "this"sv,
                         "thread_local"sv,
                         "throw"sv,
                         "true"sv,
                         "try"sv,
                         "typedef"sv,
                         "typeid"sv,
                         "typename"sv,
                         "union"sv,
                         "unsigned"sv,
                         "using"sv,
                         "virtual"sv,
                         "void"sv,
                         "volatile"sv,
                         "wchar_t"sv,
                         "while"sv };

        if (std::ranges::contains(cxxKeywords, name))
            name += "_";

        return name;
    }

    static SqlSchema::ForeignKeyConstraint const& GetForeignKey(
        SqlSchema::Column const& column, std::vector<SqlSchema::ForeignKeyConstraint> const& foreignKeys)
    {
        auto it = std::ranges::find_if(foreignKeys, [&](SqlSchema::ForeignKeyConstraint const& foreignKey) {
            return std::ranges::contains(foreignKey.foreignKey.columns, column.name);
        });
        if (it != foreignKeys.end())
            return *it;

        throw std::runtime_error(
            std::format("Foreign key not found for {} in table {}",
                        column.name,
                        column.foreignKeyConstraint->foreignKey.table)); // NOLINT(bugprone-unchecked-optional-access)
    }

    std::optional<std::string> MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                     std::string const& columnName) const
    {
        using namespace SqlSchema;
        auto const it =
            _config.columnNameOverrides.find(FullyQualifiedTableColumn { .table = tableName, .column = columnName });
        if (it != _config.columnNameOverrides.end())
            return it->second;
        return std::nullopt;
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
                return std::format(", Light::SqlRealName {{ \"{}\" }}", name);
            return std::string {};
        };

        auto aliasNameOrNullopt = [&](std::string_view name) {
            if (_config.makeAliases)
                return std::format(", Light::SqlRealName {{ \"{}\" }}", name);
            return std::string { ", std::nullopt" };
        };

        auto const primaryKeyPart = [this]() {
            if (_config.primaryKeyAssignment == PrimaryKey::ServerSideAutoIncrement)
                return ", Light::PrimaryKey::ServerSideAutoIncrement"sv;
            else if (_config.primaryKeyAssignment == PrimaryKey::AutoAssign)
                return ", Light::PrimaryKey::AutoAssign"sv;
            else
                return ""sv;
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
                return std::format("    static constexpr std::string_view TableName = \"{}\";\n\n", name);
            }
            return std::string {};
        };

        auto const selfReferencing = [&](auto const& column) {
            if (column.isForeignKey)
            {
                auto const& foreignKey = GetForeignKey(column, table.foreignKeys);
                return foreignKey.primaryKey.table == foreignKey.foreignKey.table;
            }
            return false;
        };

        definition.text << std::format("struct {} final\n", aliasTableName(table.name));
        definition.text << std::format("{{\n");
        definition.text << aliasRealTableName(table.name);

        UniqueNameBuilder uniqueMemberNameBuilder;

        auto const tableName = SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = table.name };

        for (auto const& column: table.columns)
        {
            std::string type = MakeType(column,
                                        table.name,
                                        _config.forceUnicodeTextColumns,
                                        _config.unicodeTextColumnOverrides,
                                        _config.sqlFixedStringMaxSize);
            auto const memberName =
                MapColumnNameOverride(tableName, column.name) // NOLINT(bugprone-unchecked-optional-access)
                    .or_else([&] { return std::optional { SanitizeName(FormatName(column.name, _config.formatType)) }; })
                    .value();

            ++_numberOfColumnsListed;

            if (column.isForeignKey && !column.isPrimaryKey && !selfReferencing(column))
            {
                auto const& foreignKey = GetForeignKey(column, table.foreignKeys);
                if (foreignKey.primaryKey.columns.size() == 1)
                {
                    auto foreignTableName =
                        aliasTableName(foreignKey.primaryKey.table.table); // NOLINT(bugprone-unchecked-optional-access)
                    auto const relationName =
                        MapColumnNameOverride(tableName, column.name) // NOLINT(bugprone-unchecked-optional-access)
                            .or_else([&] {
                                return std::optional { SanitizeName(
                                    FormatName(StripSuffix(foreignKey.foreignKey.columns.at(0)), _config.formatType)) };
                            })
                            .value();
                    definition.text << std::format(
                        "    Light::BelongsTo<&{}{}{}> {};\n",
                        [&] {
                            return std::format("{}::{}",
                                               foreignTableName,
                                               FormatName(foreignKey.primaryKey.columns.at(0), _config.formatType));
                        }(),
                        aliasNameOrNullopt(foreignKey.foreignKey.columns.at(0)),
                        [&] {
                            if (column.isNullable)
                                return ", Light::SqlNullable::Null"sv;
                            else
                                return ""sv;
                        }(),
                        uniqueMemberNameBuilder.DeclareName(relationName));
                    definition.requiredTables.emplace_back(std::move(foreignTableName));
                    ++_numberOfForeignKeysListed;
                    continue;
                }
                _warningOnUnsupportedMultiKeyForeignKey[foreignKey.foreignKey.table] = foreignKey;
            }

            if (column.isPrimaryKey)
            {
                definition.text << std::format("    Light::Field<{}{}{}> {};",
                                               type,
                                               primaryKeyPart(),
                                               aliasName(column.name),
                                               uniqueMemberNameBuilder.DeclareName(memberName));
                if (column.isForeignKey)
                    definition.text << " // NB: This is also a foreign key";
                definition.text << "\n";
                continue;
            }

            // Fallback: Handle the column as a regular field.
            definition.text << std::format(
                "    Light::Field<{}{}> {};", type, aliasName(column.name), uniqueMemberNameBuilder.DeclareName(memberName));
            if (column.isForeignKey)
                definition.text << std::format(" // NB: This is also a foreign key");
            definition.text << '\n';
        }

        for (SqlSchema::ForeignKeyConstraint const& foreignKey: table.externalForeignKeys)
        {
            // TODO: How to figure out if this is a HasOne or HasMany relation.
            (void) foreignKey; // TODO
        }

        definition.text << "};\n\n";
    }

    void PrintReport()
    {
        std::println();
        std::println("Summary");
        std::println("=======");
        std::println();
        std::println("Tables created          : {}", _definitions.size());
        std::println("Columns listed          : {}", _numberOfColumnsListed);
        std::println("Foreign keys considered : {}", _numberOfForeignKeysListed);
        std::println("Foreign keys ignored    : {}", _warningOnUnsupportedMultiKeyForeignKey.size());

        if (!_warningOnUnsupportedMultiKeyForeignKey.empty() && !_config.suppressWarnings)
        {
            std::println();
            std::println(
                "Warning: The database has {} following foreign keys having multiple columns, which is not supported.",
                _warningOnUnsupportedMultiKeyForeignKey.size());
            for (auto const& fk: _warningOnUnsupportedMultiKeyForeignKey)
                std::println("  {} -> {}", fk.second.foreignKey, fk.second.primaryKey);
        }
    }

  private:
    struct TableInfo
    {
        std::stringstream text;
        std::vector<std::string> requiredTables;
    };

    std::unordered_map<std::string_view, TableInfo> _definitions;
    Config _config;
    std::map<SqlSchema::FullyQualifiedTableName, SqlSchema::ForeignKeyConstraint> _warningOnUnsupportedMultiKeyForeignKey;
    size_t _numberOfColumnsListed = 0;
    size_t _numberOfForeignKeysListed = 0;
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
    std::unordered_map<std::string /*table*/, std::unordered_set<std::string /*column*/>> unicodeTextColumnOverrides;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ResolveOrderAndPrintTable(CxxModelPrinter& cxxModelPrinter, std::vector<SqlSchema::Table> const& tables)
{
    std::unordered_map<size_t, std::optional<int>> numberOfForeignKeys;
    for (auto const& [index, table]: std::views::enumerate(tables))
        numberOfForeignKeys[index] = static_cast<int>(table.foreignKeys.size());

    auto const updateForeignKeyCountAfterPrinted = [&](auto const& tablePrinted) {
        for (auto const [index, table]: std::views::enumerate(tables))
        {
            if (table.name == tablePrinted.name)
                numberOfForeignKeys[index] = std::nullopt;

            for (auto const& foreignKey: table.foreignKeys)
            {
                if ((foreignKey.primaryKey.table.table == tablePrinted.name) && numberOfForeignKeys[index].has_value())
                    numberOfForeignKeys[index] = numberOfForeignKeys[index].value() - 1;
            }
        }
    };

    size_t numberOfPrintedTables = 0;

    auto const printTable = [&](size_t index, auto const& table) {
        cxxModelPrinter.PrintTable(table);
        numberOfPrintedTables++;
        updateForeignKeyCountAfterPrinted(table);
        numberOfForeignKeys[index] = std::nullopt;
    };

    while (numberOfPrintedTables < tables.size())
    {
        for (auto const [index, table]: std::views::enumerate(tables))
        {
            if (!numberOfForeignKeys[index]) // NOLINT(bugprone-unchecked-optional-access)
                continue;
            if (numberOfForeignKeys[index].value() == 0) //  NOLINT(bugprone-unchecked-optional-access)
            {
                printTable(index, table);
            }
            else
            {
                // check all other tables and see if we have some with 0 foreign keys
                // if we do NOT have them, we need to print this table anyway since
                // there is some circular dependency that we cannot resolve
                bool found = false;
                for (auto const [otherIndex, otherTable]: std::views::enumerate(tables))
                {
                    if (numberOfForeignKeys[otherIndex] == 0)
                        found = true;
                }
                // we need to print this table so that we do not print it again
                if (!found)
                    printTable(index, table);
            }
        }
    }
    cxxModelPrinter.PrintReport();
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
                     CxxModelPrinter const& cxxModelPrinter,
                     std::vector<SqlSchema::Table> const& tables)
{
    auto const normalizedOutputDir = config.outputDirectory.back() == '/' || config.outputDirectory.back() == '\\'
                                         ? std::string(config.outputDirectory)
                                         : std::string(config.outputDirectory) + '/';
    auto const sourceFileName = normalizedOutputDir + "example.cpp";
    auto file = std::ofstream(sourceFileName); // NOLINT(bugprone-suspicious-stringview-data-usage)

    file << cxxModelPrinter.tableIncludes();
    file << "#include <cstdlib>\n";
    file << "\n";
    file << "int main()\n";
    file << "{\n";
    file << std::format("Lightweight::SqlConnection::SetDefaultConnectionString(SqlConnectionString {{ \"{}\" }});\n",
                        std::string(config.connectionString));
    file << "\n";
    file << "auto dm = Lightweight::DataMapper{};";
    file << "\n";
    for (auto const& table: tables)
    {
        file << cxxModelPrinter.example(table);
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

    std::vector<SqlSchema::Table> const tables = TimedExecution("Reading all tables", [&] {
        return SqlSchema::ReadAllTables(
            config.database, config.schema, [](std::string_view tableName, size_t current, size_t total) {
                std::print("\r\033[K {:>3}% [{}/{}] Reading table schema {}",
                           static_cast<int>((current * 100) / total),
                           current,
                           total,
                           tableName);
                if (current == total)
                    std::println();
            });
    });

    auto cxxModelPrinter = CxxModelPrinter { CxxModelPrinter::Config {
        .makeAliases = config.makeAliases,
        .formatType = config.formatType,
        .primaryKeyAssignment = config.primaryKeyAssignment,
        .columnNameOverrides = config.columnNameOverrides,
        .forceUnicodeTextColumns = config.forceUnicodeTextColumns,
        .unicodeTextColumnOverrides = config.unicodeTextColumnOverrides,
        .suppressWarnings = config.suppressWarnings,
        .sqlFixedStringMaxSize = config.sqlFixedStringMaxSize,
    } };

    TimedExecution("Resolving Order and print tables", [&] { ResolveOrderAndPrintTable(cxxModelPrinter, tables); });

    if (config.outputDirectory.empty() || config.outputDirectory == "-")
        std::println("{}", cxxModelPrinter.str(config.modelNamespace));
    else
        TimedExecution(std::format("Writing to directory {}", config.outputDirectory),
                       [&] { cxxModelPrinter.printToFiles(config.modelNamespace, config.outputDirectory); });

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
