// SPDX-License-Identifier: Apache-2.0

#include "SqlSchemaDiff.hpp"

#include <format>
#include <map>
#include <ranges>
#include <set>

namespace Lightweight::SqlSchema
{

namespace
{

    using TableKey = std::pair<std::string, std::string>; // (schema, name)

    [[nodiscard]] TableKey KeyOf(Table const& t)
    {
        return { t.schema, t.name };
    }

    /// Indexes a TableList by `(schema, name)` for O(log n) pairing.
    [[nodiscard]] std::map<TableKey, Table const*> IndexTables(TableList const& list)
    {
        auto index = std::map<TableKey, Table const*> {};
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

    /// Returns the list of field names that differ between two columns.
    [[nodiscard]] std::vector<std::string> DiffColumnFields(Column const& a, Column const& b)
    {
        auto fields = std::vector<std::string> {};
        if (a.dialectDependantTypeString != b.dialectDependantTypeString)
            fields.emplace_back("type");
        if (a.isNullable != b.isNullable)
            fields.emplace_back("nullable");
        if (a.size != b.size)
            fields.emplace_back("size");
        if (a.decimalDigits != b.decimalDigits)
            fields.emplace_back("decimalDigits");
        if (a.defaultValue != b.defaultValue)
            fields.emplace_back("defaultValue");
        if (a.isAutoIncrement != b.isAutoIncrement)
            fields.emplace_back("autoIncrement");
        if (a.isPrimaryKey != b.isPrimaryKey)
            fields.emplace_back("primaryKey");
        if (a.isForeignKey != b.isForeignKey)
            fields.emplace_back("foreignKey");
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

    /// Renders a single foreign-key constraint to a stable, comparable form.
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
        return std::format(
            "{}.{}({}) -> {}.{}({})",
            fk.foreignKey.table.schema,
            fk.foreignKey.table.table,
            join(fk.foreignKey.columns),
            fk.primaryKey.table.schema,
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

SchemaDiff DiffSchemas(TableList const& a, TableList const& b)
{
    auto const indexA = IndexTables(a);
    auto const indexB = IndexTables(b);

    auto allKeys = std::set<TableKey> {};
    for (auto const& t: a)
        allKeys.insert(KeyOf(t));
    for (auto const& t: b)
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
            diff.tables.emplace_back(TableDiff { .schema = key.first, .name = key.second, .kind = DiffKind::OnlyInA });
            continue;
        }
        if (!tableA && tableB)
        {
            diff.tables.emplace_back(TableDiff { .schema = key.first, .name = key.second, .kind = DiffKind::OnlyInB });
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
                .schema = key.first,
                .name = key.second,
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
