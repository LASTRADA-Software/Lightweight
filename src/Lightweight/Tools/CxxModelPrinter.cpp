// SPDX-License-Identifier: Apache-2.0

#include "CxxModelPrinter.hpp"

#include <array>
#include <fstream>

using namespace std::string_view_literals;

namespace Lightweight::Tools
{

static auto ToLower(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](auto c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

CxxModelPrinter::CxxModelPrinter(Config config) noexcept:
    _config(std::move(std::move(config)))
{
}

auto CxxModelPrinter::StripSuffix(std::string name) -> std::string
{
    std::string const lowerName = ToLower(name);
    for (auto const& suffix: _config.stripSuffixes)
    {
        if (lowerName.ends_with(suffix))
            return name.substr(0, name.length() - suffix.length());
    }
    return name;
}

auto CxxModelPrinter::SanitizeName(std::string name) -> std::string
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

// "user_id" into "UserId"
// "task_list_entry" into "TaskListEntry"
// "person" into "Person"
// and so on
std::string CxxModelPrinter::FormatTableName(std::string_view name)
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

std::string CxxModelPrinter::AliasTableName(std::string_view name) const
{
    if (_config.makeAliases)
        return FormatTableName(name);

    return std::string { name };
}

[[nodiscard]] std::expected<void, std::string> CxxModelPrinter::PrintCumulativeHeaderFile(
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
        includes.emplace_back(std::format("{}.hpp", AliasTableName(tableName)));

    std::ranges::sort(includes);

    for (auto const& include: includes)
        file << std::format("#include \"{}\"\n", include);

    return {};
}

void CxxModelPrinter::PrintToFiles(std::string_view modelNamespace, std::string_view outputDirectory)
{
    for (auto const& [tableName, definition]: _definitions)
    {
        auto const fileName = std::format("{}/{}.hpp", outputDirectory, AliasTableName(tableName));
        auto file = std::ofstream(fileName);
        if (!file)
        {
            std::println("Failed to create file {}.", fileName);
            continue;
        }
        file << HeaderFileForTheTable(modelNamespace, tableName);
    }
}

std::string CxxModelPrinter::HeaderFileForTheTable(std::string_view modelNamespace,
                                                   std::string const& tableName) // NOLINT(readability-identifier-naming)
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

SqlSchema::ForeignKeyConstraint const& CxxModelPrinter::GetForeignKey(
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CxxModelPrinter::MakeType(
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
                    // CHAR(n) seems to be always right-side whitespace trimmed,
                    // so we use SqlTrimmedFixedString for it.
                    if (shouldForceUnicodeTextColumn())
                        return std::format("Light::SqlTrimmedFixedString<{}, wchar_t>", type.size);
                    else
                        return std::format("Light::SqlTrimmedFixedString<{}>", type.size);
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
                // NCHAR(n) seems to be always right-side whitespace trimmed,
                // so we use SqlTrimmedFixedString for it.
                if (type.size == 1)
                    return "char16_t";
                else if (type.size <= sqlFixedStringMaxSize)
                    return std::format("Light::SqlTrimmedFixedString<{}, wchar_t>", type.size);
                else
                    return std::format("Light::SqlDynamicWideString<{}>", type.size);
            },
            [](NVarchar const& type) -> std::string { return std::format("Light::SqlDynamicUtf16String<{}>", type.size); },
            [](Real const& v) -> std::string {
                if (v.precision <= 24)
                    return "float";
                else
                    return "double";
            },
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

std::optional<std::string> CxxModelPrinter::MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                                  std::string const& columnName) const
{
    using namespace SqlSchema;
    auto const it = _config.columnNameOverrides.find(FullyQualifiedTableColumn {
        .table = tableName,
        .column = columnName,
    });
    if (it != _config.columnNameOverrides.end())
        return it->second;
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CxxModelPrinter::ResolveOrderAndPrintTable(std::vector<SqlSchema::Table> const& tables)
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

    auto const printTable = [&, this](size_t index, auto const& table) {
        PrintTable(table);
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
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CxxModelPrinter::PrintTable(SqlSchema::Table const& table)
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
                        return std::format(
                            "{}::{}", foreignTableName, FormatName(foreignKey.primaryKey.columns.at(0), _config.formatType));
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

void CxxModelPrinter::PrintReport()
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
        std::println("Warning: The database has {} following foreign keys having multiple columns, which is not supported.",
                     _warningOnUnsupportedMultiKeyForeignKey.size());
        for (auto const& fk: _warningOnUnsupportedMultiKeyForeignKey)
            std::println("  {} -> {}", fk.second.foreignKey, fk.second.primaryKey);
    }
}

std::string CxxModelPrinter::ToString(std::string_view modelNamespace)
{
    std::string result;
    for (auto const& [tableName, definition]: _definitions)
    {
        result += std::format("// file: {}.hpp\n", tableName);
        result += HeaderFileForTheTable(modelNamespace, tableName);
    }
    return result;
}

std::string CxxModelPrinter::TableIncludes() const
{

    std::string result;
    for (auto const& [tableName, definition]: _definitions)
    {
        result += std::format("#include \"{}.hpp\"\n", tableName);
    }
    return result;
}

std::string CxxModelPrinter::Example(SqlSchema::Table const& table) const
{
    std::stringstream exampleEntries;

    auto const tableName = AliasTableName(table.name);

    exampleEntries << std::format("auto entries{} = dm.Query<{}>().First(10);\n", tableName, tableName);
    exampleEntries << std::format("for (auto const& entry: entries{})\n", tableName);
    exampleEntries << "{\n";
    exampleEntries << std::format("    std::println(\"{{}}\", Lightweight::DataMapper::Inspect(entry));\n");
    exampleEntries << "}\n";
    exampleEntries << "\n";

    return exampleEntries.str();
}

} // namespace Lightweight::Tools
