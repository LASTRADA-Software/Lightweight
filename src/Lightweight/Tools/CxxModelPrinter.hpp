// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/DataMapper/Field.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/Utils.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_set>

namespace Lightweight::Tools
{

using ColumnNameOverrides = std::map<SqlSchema::FullyQualifiedTableColumn, std::string>;

class CxxModelPrinter
{
  public:
    using UnicodeTextColumnOverrides = std::unordered_map<std::string /*table*/, std::unordered_set<std::string /*column*/>>;

    struct Config
    {
        std::vector<std::string> stripSuffixes = { "_id", "_nr" };
        bool makeAliases = false;
        FormatType formatType = FormatType::camelCase;
        PrimaryKey primaryKeyAssignment = PrimaryKey::ServerSideAutoIncrement;
        ColumnNameOverrides columnNameOverrides;
        bool forceUnicodeTextColumns = false;
        UnicodeTextColumnOverrides unicodeTextColumnOverrides;
        bool suppressWarnings = false;
        size_t sqlFixedStringMaxSize = SqlOptimalMaxColumnSize;
    };

    explicit CxxModelPrinter(Config config) noexcept;

    std::string ToString(std::string_view modelNamespace);

    std::string TableIncludes() const;

    std::string AliasTableName(std::string_view name) const;

    [[nodiscard]] std::expected<void, std::string> PrintCumulativeHeaderFile(
        std::filesystem::path const& outputDirectory, std::filesystem::path const& cumulativeHeaderFile);

    void PrintToFiles(std::string_view modelNamespace, std::string_view outputDirectory);

    std::string HeaderFileForTheTable(std::string_view modelNamespace, std::string const& tableName);

    std::string Example(SqlSchema::Table const& table) const;

    auto StripSuffix(std::string name) -> std::string;

    static auto SanitizeName(std::string name) -> std::string;

    static auto FormatTableName(std::string_view name) -> std::string;

    static SqlSchema::ForeignKeyConstraint const& GetForeignKey(
        SqlSchema::Column const& column, std::vector<SqlSchema::ForeignKeyConstraint> const& foreignKeys);

    static std::string MakeType(SqlSchema::Column const& column,
                                std::string const& tableName,
                                bool forceUnicodeTextColumn,
                                UnicodeTextColumnOverrides const& unicodeTextColumnOverrides,
                                size_t sqlFixedStringMaxSize,
                                bool makeAliases = false);

    std::optional<std::string> MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                     std::string const& columnName) const;

    void ResolveOrderAndPrintTable(std::vector<SqlSchema::Table> const& tables);

    void PrintTable(SqlSchema::Table const& table);

    void PrintReport();

  private:
    struct TableInfo
    {
        std::stringstream text;
        std::vector<std::string> requiredTables;
    };

    std::map<std::string, TableInfo> _definitions;
    Config _config;
    std::map<SqlSchema::FullyQualifiedTableName, SqlSchema::ForeignKeyConstraint> _warningOnUnsupportedMultiKeyForeignKey;
    size_t _numberOfColumnsListed = 0;
    size_t _numberOfForeignKeysListed = 0;
};

} // namespace Lightweight::Tools
