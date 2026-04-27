// SPDX-License-Identifier: Apache-2.0

#include "SqlColumnTypeDefinitions.hpp"
#include "SqlSchemaDiff.hpp"

#include <format>
#include <map>
#include <ranges>
#include <set>
#include <variant>

namespace Lightweight::SqlSchema
{

namespace
{

    /// Logical column-type kinds used for cross-engine equivalence comparison.
    ///
    /// Driver reportage is engine-specific in ways that no migration can reconcile: a
    /// migration declaring `NChar(30)` reads back as `Varchar(30)` on SQLite (NCHAR has
    /// TEXT affinity, so the driver returns SQL_VARCHAR) and as `Char(30)` on PostgreSQL
    /// (which honours fixed-width). MSSQL returns `NChar(30)`. We project all string-class
    /// types into a single @ref LogicalKind::String so the diff sees them as equivalent.
    /// Likewise the integer-class types stay distinct (Bigint/Integer/Smallint/Tinyint),
    /// but Tinyint folds into Smallint because PostgreSQL has no TINYINT and silently
    /// promotes it to SMALLINT during migration.
    enum class LogicalKind : std::uint8_t
    {
        Bigint,
        Binary, ///< Fixed-width binary
        Bool,
        Date,
        DateTime,
        Decimal,
        Guid,
        Integer,
        Real,     ///< Floating point — driver-level precision drift (24 vs 53) is collapsed.
        Smallint, ///< Smallint and Tinyint fold together (Postgres lacks TINYINT).
        String,   ///< CHAR/NCHAR/VARCHAR/NVARCHAR/TEXT — engines disagree on fixed/wide.
        Time,
        Timestamp,
        VarBinary,
    };

    struct LogicalType
    {
        LogicalKind kind {};
        /// Declared character/byte size for sized types. 0 means "unbounded" (e.g. TEXT,
        /// or a driver that reports a sentinel huge size for unsized text columns).
        std::size_t size = 0;
        /// Decimal precision (only meaningful for @ref LogicalKind::Decimal).
        std::size_t precision = 0;
        /// Decimal scale (only meaningful for @ref LogicalKind::Decimal).
        std::size_t scale = 0;

        auto operator<=>(LogicalType const&) const = default;
    };

    /// Projects an engine-reported column type into its logical, cross-engine form.
    ///
    /// Char-class and Varchar-class types all collapse into @ref LogicalKind::String
    /// (drivers return inconsistent fixed/variable kinds for the same migration). String
    /// columns whose driver-reported size is 0 or exceeds the typical user-declared
    /// upper bound are treated as unbounded — that captures `TEXT` columns regardless of
    /// whether the driver returns `0` (PostgreSQL on `text` historically) or a sentinel
    /// huge value (SQLite, or the PostgreSQL ODBC `MaxLongVarcharSize` of 8190).
    [[nodiscard]] LogicalType ToLogical(SqlColumnTypeDefinition const& type)
    {
        using namespace SqlColumnTypeDefinitions;

        // 4096 is well above any size a hand-written migration would declare for a
        // bounded-string column, and below the smallest "engine default unbounded" size
        // any tested driver reports. Anything at or above this is intent = unbounded.
        constexpr auto kUnboundedStringThreshold = std::size_t { 4096 };

        auto normalizeStringSize = [](std::size_t size) -> std::size_t {
            return (size == 0 || size >= kUnboundedStringThreshold) ? 0 : size;
        };

        return std::visit(
            [&](auto const& t) -> LogicalType {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, Bigint>)
                    return { .kind = LogicalKind::Bigint };
                else if constexpr (std::is_same_v<T, Binary>)
                    return { .kind = LogicalKind::Binary, .size = t.size };
                else if constexpr (std::is_same_v<T, Bool>)
                    return { .kind = LogicalKind::Bool };
                else if constexpr (std::is_same_v<T, Char>)
                    return { .kind = LogicalKind::String, .size = normalizeStringSize(t.size) };
                else if constexpr (std::is_same_v<T, NChar>)
                    return { .kind = LogicalKind::String, .size = normalizeStringSize(t.size) };
                else if constexpr (std::is_same_v<T, Date>)
                    return { .kind = LogicalKind::Date };
                else if constexpr (std::is_same_v<T, DateTime>)
                    return { .kind = LogicalKind::DateTime };
                else if constexpr (std::is_same_v<T, Decimal>)
                    return { .kind = LogicalKind::Decimal, .precision = t.precision, .scale = t.scale };
                else if constexpr (std::is_same_v<T, Guid>)
                    return { .kind = LogicalKind::Guid };
                else if constexpr (std::is_same_v<T, Integer>)
                    return { .kind = LogicalKind::Integer };
                else if constexpr (std::is_same_v<T, NVarchar>)
                    return { .kind = LogicalKind::String, .size = normalizeStringSize(t.size) };
                else if constexpr (std::is_same_v<T, Varchar>)
                    return { .kind = LogicalKind::String, .size = normalizeStringSize(t.size) };
                else if constexpr (std::is_same_v<T, Text>)
                    return { .kind = LogicalKind::String, .size = normalizeStringSize(t.size) };
                else if constexpr (std::is_same_v<T, Real>)
                    return { .kind = LogicalKind::Real };
                else if constexpr (std::is_same_v<T, Smallint>)
                    return { .kind = LogicalKind::Smallint };
                else if constexpr (std::is_same_v<T, Time>)
                    return { .kind = LogicalKind::Time };
                else if constexpr (std::is_same_v<T, Timestamp>)
                    return { .kind = LogicalKind::Timestamp };
                else if constexpr (std::is_same_v<T, Tinyint>)
                    return { .kind = LogicalKind::Smallint };
                else if constexpr (std::is_same_v<T, VarBinary>)
                    // PostgreSQL maps `VarBinary(N)` to `BYTEA` and loses the size on
                    // round-trip; sqlite preserves it. Treat VarBinary as unsized for
                    // logical equivalence.
                    return { .kind = LogicalKind::VarBinary };
                else
                    static_assert(sizeof(T) == 0, "Unhandled SqlColumnTypeDefinition alternative");
            },
            type);
    }

    /// Tables are paired by name only — schema labels are engine-specific (`dbo`, `public`,
    /// `""`) and would otherwise prevent a SQLite/Postgres/MSSQL diff from ever matching.
    using TableKey = std::string;

    [[nodiscard]] TableKey KeyOf(Table const& t)
    {
        return t.name;
    }

    /// Indexes a TableList by name for O(log n) pairing.
    [[nodiscard]] std::map<TableKey, Table const*, std::less<>> IndexTables(TableList const& list)
    {
        auto index = std::map<TableKey, Table const*, std::less<>> {};
        for (auto const& t: list)
            index.emplace(KeyOf(t), &t);
        return index;
    }

    /// Indexes a column vector by name.
    [[nodiscard]] std::map<std::string, Column const*, std::less<>> IndexColumns(std::vector<Column> const& columns)
    {
        auto index = std::map<std::string, Column const*, std::less<>> {};
        for (auto const& c: columns)
            index.emplace(c.name, &c);
        return index;
    }

    /// Returns the list of field names that differ between two columns, comparing on the
    /// **logical** type and constraint flags only.
    ///
    /// Cross-engine drivers report a column's standalone `size`, `decimalDigits`, and
    /// `defaultValue` inconsistently — Postgres sends `INTEGER` with `size=10`, SQLite with
    /// `size=8`, MSSQL with `size=10`; defaults round-trip with engine-specific quoting
    /// (`'NULL'`, `nextval(...)`, `''`). Those fields are intentionally not compared:
    /// what the migration declared is encoded in @ref Column::type, and the
    /// @ref ToLogical projection collapses the engine-specific variants back to a single
    /// logical kind.
    ///
    /// Per-column `isForeignKey` is also intentionally skipped: it is a derived flag whose
    /// reliability depends on the driver returning column names in the same case as the
    /// table reader saw them. Cross-engine FK identity is instead asserted at the table
    /// level via @ref DiffForeignKeys, which compares the actual constraint shape.
    [[nodiscard]] std::vector<std::string> DiffColumnFields(Column const& a, Column const& b)
    {
        auto fields = std::vector<std::string> {};
        if (ToLogical(a.type) != ToLogical(b.type))
            fields.emplace_back("type");
        if (a.isNullable != b.isNullable)
            fields.emplace_back("nullable");
        if (a.isAutoIncrement != b.isAutoIncrement)
            fields.emplace_back("autoIncrement");
        if (a.isPrimaryKey != b.isPrimaryKey)
            fields.emplace_back("primaryKey");
        if (a.isUnique != b.isUnique)
            fields.emplace_back("unique");
        return fields;
    }

    [[nodiscard]] std::vector<ColumnDiff> DiffColumns(std::vector<Column> const& a, std::vector<Column> const& b)
    {
        auto const indexA = IndexColumns(a);
        auto const indexB = IndexColumns(b);

        auto names = std::set<std::string, std::less<>> {};
        for (auto const& c: a)
            names.insert(c.name);
        for (auto const& c: b)
            names.insert(c.name);

        auto diffs = std::vector<ColumnDiff> {};
        for (auto const& name: names)
        {
            auto const itA = indexA.find(name);
            auto const itB = indexB.find(name);
            auto const* colA = itA != indexA.end() ? itA->second : nullptr;
            auto const* colB = itB != indexB.end() ? itB->second : nullptr;

            if (colA && !colB)
                diffs.emplace_back(ColumnDiff { .name = name, .kind = DiffKind::OnlyInA, .a = colA });
            else if (!colA && colB)
                diffs.emplace_back(ColumnDiff { .name = name, .kind = DiffKind::OnlyInB, .b = colB });
            else if (colA && colB)
            {
                auto changed = DiffColumnFields(*colA, *colB);
                if (!changed.empty())
                {
                    diffs.emplace_back(ColumnDiff {
                        .name = name,
                        .kind = DiffKind::Changed,
                        .changedFields = std::move(changed),
                        .a = colA,
                        .b = colB,
                    });
                }
            }
        }
        return diffs;
    }

    [[nodiscard]] std::vector<std::string> DiffPrimaryKeys(std::vector<std::string> const& a,
                                                           std::vector<std::string> const& b)
    {
        if (a == b)
            return {};
        auto out = std::vector<std::string> {};
        // Order matters for composite PKs — report a single line with both sides.
        auto join = [](std::vector<std::string> const& v) {
            auto s = std::string {};
            for (auto const& [i, name]: std::views::enumerate(v))
            {
                if (i != 0)
                    s += ", ";
                s += name;
            }
            return s.empty() ? std::string { "(none)" } : s;
        };
        out.emplace_back(std::format("primary key: [{}] vs [{}]", join(a), join(b)));
        return out;
    }

    /// Renders a single foreign-key constraint to a stable, schema-agnostic comparable form.
    ///
    /// Schema labels are intentionally omitted: the same FK reads as `dbo.X` on MSSQL,
    /// `public.X` on Postgres, and `X` on SQLite, but it is the same constraint logically.
    [[nodiscard]] std::string FormatForeignKey(ForeignKeyConstraint const& fk)
    {
        auto join = [](std::vector<std::string> const& v) {
            auto s = std::string {};
            for (auto const& [i, name]: std::views::enumerate(v))
            {
                if (i != 0)
                    s += ",";
                s += name;
            }
            return s;
        };
        return std::format("{}({}) -> {}({})",
                           fk.foreignKey.table.table,
                           join(fk.foreignKey.columns),
                           fk.primaryKey.table.table,
                           join(fk.primaryKey.columns));
    }

    [[nodiscard]] std::vector<std::string> DiffForeignKeys(std::vector<ForeignKeyConstraint> const& a,
                                                           std::vector<ForeignKeyConstraint> const& b)
    {
        auto setA = std::set<std::string> {};
        auto setB = std::set<std::string> {};
        for (auto const& fk: a)
            setA.insert(FormatForeignKey(fk));
        for (auto const& fk: b)
            setB.insert(FormatForeignKey(fk));

        auto diffs = std::vector<std::string> {};
        for (auto const& s: setA)
            if (!setB.contains(s))
                diffs.emplace_back(std::format("only in A: {}", s));
        for (auto const& s: setB)
            if (!setA.contains(s))
                diffs.emplace_back(std::format("only in B: {}", s));
        return diffs;
    }

    [[nodiscard]] std::string FormatIndex(IndexDefinition const& idx)
    {
        auto cols = std::string {};
        for (auto const& [i, name]: std::views::enumerate(idx.columns))
        {
            if (i != 0)
                cols += ",";
            cols += name;
        }
        return std::format("{}{}({})", idx.isUnique ? "UNIQUE " : "", idx.name, cols);
    }

    [[nodiscard]] std::vector<std::string> DiffIndexes(std::vector<IndexDefinition> const& a,
                                                       std::vector<IndexDefinition> const& b)
    {
        // Pair by index name; treat any change to columns/uniqueness as a difference.
        auto indexByName = [](std::vector<IndexDefinition> const& v) {
            auto m = std::map<std::string, IndexDefinition const*, std::less<>> {};
            for (auto const& i: v)
                m.emplace(i.name, &i);
            return m;
        };
        auto const mapA = indexByName(a);
        auto const mapB = indexByName(b);

        auto names = std::set<std::string, std::less<>> {};
        for (auto const& i: a)
            names.insert(i.name);
        for (auto const& i: b)
            names.insert(i.name);

        auto diffs = std::vector<std::string> {};
        for (auto const& name: names)
        {
            auto const itA = mapA.find(name);
            auto const itB = mapB.find(name);
            auto const* idxA = itA != mapA.end() ? itA->second : nullptr;
            auto const* idxB = itB != mapB.end() ? itB->second : nullptr;
            if (idxA && !idxB)
                diffs.emplace_back(std::format("only in A: {}", FormatIndex(*idxA)));
            else if (!idxA && idxB)
                diffs.emplace_back(std::format("only in B: {}", FormatIndex(*idxB)));
            else if (idxA && idxB && (idxA->columns != idxB->columns || idxA->isUnique != idxB->isUnique))
                diffs.emplace_back(std::format("changed: {} | {}", FormatIndex(*idxA), FormatIndex(*idxB)));
        }
        return diffs;
    }

} // namespace

namespace
{
    /// Tables internal to the migration runtime that should not show up in user-facing diffs.
    /// `_migration_locks` is created on demand by the SQLite lock implementation
    /// (PostgreSQL / MSSQL use advisory locks instead) — its presence on one side and
    /// absence on the other reflects engine-specific lock plumbing, not migration drift.
    [[nodiscard]] bool IsMigrationInternalTable(std::string_view name) noexcept
    {
        return name == "_migration_locks";
    }
} // namespace

SchemaDiff DiffSchemas(TableList const& a, TableList const& b)
{
    auto const indexA = IndexTables(a);
    auto const indexB = IndexTables(b);

    auto allKeys = std::set<TableKey, std::less<>> {};
    for (auto const& t: a)
        if (!IsMigrationInternalTable(t.name))
            allKeys.insert(KeyOf(t));
    for (auto const& t: b)
        if (!IsMigrationInternalTable(t.name))
            allKeys.insert(KeyOf(t));

    auto diff = SchemaDiff {};
    for (auto const& key: allKeys)
    {
        auto const itA = indexA.find(key);
        auto const itB = indexB.find(key);
        auto const* tableA = itA != indexA.end() ? itA->second : nullptr;
        auto const* tableB = itB != indexB.end() ? itB->second : nullptr;

        if (tableA && !tableB)
        {
            diff.tables.emplace_back(TableDiff {
                .name = key,
                .schemaA = tableA->schema,
                .schemaB = {},
                .kind = DiffKind::OnlyInA,
            });
            continue;
        }
        if (!tableA && tableB)
        {
            diff.tables.emplace_back(TableDiff {
                .name = key,
                .schemaA = {},
                .schemaB = tableB->schema,
                .kind = DiffKind::OnlyInB,
            });
            continue;
        }
        if (!tableA || !tableB)
            continue; // Unreachable: keys came from union of both indexes.

        auto columnDiffs = DiffColumns(tableA->columns, tableB->columns);
        auto pkDiffs = DiffPrimaryKeys(tableA->primaryKeys, tableB->primaryKeys);
        auto fkDiffs = DiffForeignKeys(tableA->foreignKeys, tableB->foreignKeys);
        auto idxDiffs = DiffIndexes(tableA->indexes, tableB->indexes);

        if (!columnDiffs.empty() || !pkDiffs.empty() || !fkDiffs.empty() || !idxDiffs.empty())
        {
            diff.tables.emplace_back(TableDiff {
                .name = key,
                .schemaA = tableA->schema,
                .schemaB = tableB->schema,
                .kind = DiffKind::Changed,
                .columns = std::move(columnDiffs),
                .primaryKeyDiffs = std::move(pkDiffs),
                .foreignKeyDiffs = std::move(fkDiffs),
                .indexDiffs = std::move(idxDiffs),
            });
        }
    }
    return diff;
}

} // namespace Lightweight::SqlSchema
