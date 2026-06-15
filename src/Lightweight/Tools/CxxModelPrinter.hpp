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
#include <utility>
#include <vector>

namespace Lightweight::Tools
{

using ColumnNameOverrides = std::map<SqlSchema::ColumnIdentifier, std::string>;

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
        /// When set, emit `extern template` declarations in the headers plus one explicit-
        /// instantiation .cpp per record and a CMakeLists.txt that builds them into a library.
        /// Consuming translation units then link that library instead of re-instantiating the
        /// (expensive) DataMapper relation machinery for every record.
        bool generateInstantiations = false;
        /// Name of the CMake library target emitted alongside the instantiation sources.
        std::string instantiationTargetName = "LightweightEntities";
    };

    explicit CxxModelPrinter(Config config) noexcept;

    std::string ToString(std::string_view modelNamespace);

    [[nodiscard]] std::string TableIncludes() const;

    [[nodiscard]] std::string AliasTableName(std::string_view name) const;

    [[nodiscard]] std::expected<void, std::string> PrintCumulativeHeaderFile(
        std::filesystem::path const& outputDirectory, std::filesystem::path const& cumulativeHeaderFile);

    void PrintToFiles(std::string_view modelNamespace, std::string_view outputDirectory);

    std::string HeaderFileForTheTable(std::string_view modelNamespace, std::string const& tableName);

    [[nodiscard]] std::string Example(SqlSchema::Table const& table) const;

    auto StripSuffix(std::string name) -> std::string;

    static auto SanitizeName(std::string name) -> std::string;

    static auto FormatTableName(std::string_view name) -> std::string;

    static SqlSchema::ForeignKeyConstraint const& GetForeignKey(
        SqlSchema::Column const& column, std::vector<SqlSchema::ForeignKeyConstraint> const& foreignKeys);

    static std::string MakeType(SqlSchema::Column const& column,
                                std::string const& tableName,
                                bool forceUnicodeTextColumn,
                                UnicodeTextColumnOverrides const& unicodeTextColumnOverrides,
                                size_t sqlFixedStringMaxSize);

    [[nodiscard]] std::optional<std::string> MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                                   std::string const& columnName) const;

    void ResolveOrderAndPrintTable(std::vector<SqlSchema::Table> const& tables);

    void PrintTable(SqlSchema::Table const& table);

    void PrintReport();

  private:
    // Writes the CMakeLists.txt that builds the per-record instantiation sources into a library.
    void WriteInstantiationCMakeLists(std::string_view outputDirectory, std::vector<std::string> instantiationSources) const;

    struct TableInfo
    {
        std::stringstream text;
        std::vector<std::string> requiredTables;
        std::string structName;                                   //< C++ struct name (possibly aliased).
        std::vector<std::pair<std::string, std::string>> members; //< (emitted member id, SQL column name), in order.
    };

    // Renders the Description<> specialization for one table (emitted at global scope so it
    // can specialize the Lightweight customization point). Returns empty if there is nothing to emit.
    [[nodiscard]] static std::string RecordDescriptorFor(std::string_view modelNamespace, TableInfo const& info);

    // Renders the `extern template` declaration appended to a record's header so consuming TUs do
    // not implicitly instantiate the heavy relation machinery. Returns empty if nothing to emit.
    [[nodiscard]] static std::string ExternTemplateDeclarationFor(std::string_view modelNamespace, TableInfo const& info);

    // Renders the .cpp that explicitly instantiates a record's relation machinery exactly once.
    [[nodiscard]] static std::string InstantiationSourceFor(std::string_view modelNamespace,
                                                            std::string const& headerFileName,
                                                            TableInfo const& info);

    std::map<std::string, TableInfo> _definitions;
    Config _config;
    std::map<SqlSchema::FullyQualifiedTableName, SqlSchema::ForeignKeyConstraint> _warningOnUnsupportedMultiKeyForeignKey;
    size_t _numberOfColumnsListed = 0;
    size_t _numberOfForeignKeysListed = 0;
};

} // namespace Lightweight::Tools
