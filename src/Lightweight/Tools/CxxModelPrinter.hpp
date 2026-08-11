// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/DataMapper/Field.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/Utils.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <set>
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

    /// Renders the `// NOTE:` comment block to emit above a generated member for a `DECIMAL`
    /// column whose declared precision is wider than what `SqlDataBinder<SqlNumeric<P, S>>` can
    /// actually deliver.
    ///
    /// The emitted `Light::SqlNumeric<P, S>` carries the column's declared precision, but the
    /// transfer does not always carry the matching number of digits, and nothing at the call site
    /// says so. Rather than silently generating a lossy record, state the limit where a ddl2cpp
    /// consumer reads it — in the generated header itself.
    ///
    /// @param column Column to describe; non-`DECIMAL` columns and narrow ones yield no note.
    /// @return The note, each line already indented and newline-terminated, or an empty string.
    [[nodiscard]] static std::string MakeDecimalPrecisionNote(SqlSchema::Column const& column);

    [[nodiscard]] std::optional<std::string> MapColumnNameOverride(SqlSchema::FullyQualifiedTableName const& tableName,
                                                                   std::string const& columnName) const;

    void ResolveOrderAndPrintTable(std::vector<SqlSchema::Table> const& tables);

    /// @brief One inverse or through relation to emit on a record.
    ///
    /// A `BelongsTo` is derivable from the child table alone, but every relation pointing the other
    /// way needs to know about foreign keys declared on *other* tables. `RelationPlan` is that
    /// schema-wide answer, computed once by `PlanRelations` and consumed per table.
    struct PlannedRelation
    {
        /// Which relation template to emit.
        enum class Kind : uint8_t
        {
            HasMany,        //!< One-to-many: the inverse of a child's BelongsTo.
            HasOne,         //!< One-to-one: as HasMany, but the child's foreign key is uniquely indexed.
            HasManyThrough, //!< Many-to-many across a join table.
            HasOneThrough,  //!< As HasManyThrough, but the far side is uniquely indexed.
        };

        /// Which relation template to emit for this relation.
        Kind kind {};

        /// Table this relation is emitted on.
        std::string ownerTable;

        /// The record the relation yields: the child table for HasMany/HasOne, the far table for the
        /// through relations.
        std::string referencedTable;

        /// Join table, for the through relations only; empty otherwise.
        std::string throughTable;

        /// Foreign key column linking the child (or join record) back to the owner. Emitted as a
        /// `SqlRealName` selector so that several foreign keys into one table stay distinguishable.
        std::string ownerForeignKeyColumn;

        /// Foreign key column linking the join record to the far table; empty for HasMany/HasOne.
        std::string referencedForeignKeyColumn;

        /// Whether a selector must be emitted for @ref ownerForeignKeyColumn. It is required when the
        /// owner is reachable from the same child table through more than one foreign key, and is
        /// harmless otherwise - but emitting it unconditionally would churn every generated header,
        /// so it is tracked.
        bool ownerSelectorRequired {};

        /// As @ref ownerSelectorRequired, for @ref referencedForeignKeyColumn.
        bool referencedSelectorRequired {};

        /// Member name to emit, already sanitized and formatted.
        std::string memberName;
    };

    /// @brief Relations to emit, keyed by the table they belong on.
    using RelationPlan = std::map<std::string, std::vector<PlannedRelation>>;

    /// @brief Works out every inverse and through relation implied by a schema.
    ///
    /// Applies the rules documented in `docs/ddl2cpp-relation-generation.md`: a join table (exactly
    /// two single-column foreign keys to two distinct tables, and no payload columns) becomes a
    /// `HasManyThrough` on both sides; every other single-column foreign key becomes a `HasMany` on
    /// the referenced side; and either degrades to its scalar form when the relevant foreign key is
    /// covered by a single-column unique index. Composite foreign keys are ignored, matching the
    /// existing `BelongsTo` behaviour.
    ///
    /// Pure: it reads the schema and returns a plan, so it is testable without a database.
    ///
    /// @param tables The whole schema. Relations are only planned between tables present here.
    /// @return The relations to emit, keyed by owning table name.
    [[nodiscard]] static RelationPlan PlanRelations(std::vector<SqlSchema::Table> const& tables);

    /// @param table Table to emit.
    /// @param relationPlan Inverse and through relations to emit on it, from @ref PlanRelations.
    ///                     Defaults to none, which yields the BelongsTo-only output.
    void PrintTable(SqlSchema::Table const& table, std::vector<PlannedRelation> const& relationPlan = {});

    void PrintReport();

  private:
    // Writes the CMakeLists.txt that builds the per-record instantiation sources into a library.
    void WriteInstantiationCMakeLists(std::string_view outputDirectory, std::vector<std::string> instantiationSources) const;

    struct TableInfo
    {
        std::stringstream text;
        /// Headers this record depends on, one entry per *distinct* referenced table. A table with
        /// several foreign-key columns pointing at the same target must still be included once, so
        /// this is a set (which also gives the emitted `#include` block a stable, sorted order).
        std::set<std::string> requiredTables;

        /// Records this one names but must *not* include, emitted as forward declarations instead.
        ///
        /// An inverse relation points from parent to child while the child's `BelongsTo` points back,
        /// so including both ways would be a cycle. `HasMany` stores
        /// `std::vector<std::shared_ptr<OtherRecord>>` and the through relations are equally
        /// indirect, so a declaration is sufficient at the point of use - the definition is only
        /// needed where the relation is actually loaded, which is a `.cpp`.
        std::set<std::string> forwardDeclaredTables;
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
    size_t _numberOfRelationsListed = 0;
};

} // namespace Lightweight::Tools
