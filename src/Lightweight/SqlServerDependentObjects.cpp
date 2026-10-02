// SPDX-License-Identifier: Apache-2.0

#include "SqlServerDependentObjects.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>

namespace Lightweight::detail
{

namespace
{

    /// `[name]` with `]` doubled — T-SQL identifier quoting (QUOTENAME).
    std::string Bracket(std::string_view name)
    {
        auto out = std::string { "[" };
        for (auto const c: name)
        {
            out += c;
            if (c == ']')
                out += ']';
        }
        out += ']';
        return out;
    }

    /// `N'text'` with `'` doubled — a T-SQL Unicode string literal.
    std::string Literal(std::string_view text)
    {
        auto out = std::string { "N'" };
        for (auto const c: text)
        {
            out += c;
            if (c == '\'')
                out += '\'';
        }
        out += '\'';
        return out;
    }

    /// `[schema].[table]`.
    std::string QualifiedTable(std::string_view schema, std::string_view table)
    {
        return std::format("{}.{}", Bracket(schema), Bracket(table));
    }

    /// Comma-joined list of `items`.
    std::string Join(std::vector<std::string> const& items)
    {
        // A plain loop: libc++ has no views::join_with yet, and Clang cannot compile the
        // `| std::ranges::to` pipe against GCC 14's libstdc++.
        auto joined = std::string {};
        for (auto const& item: items)
        {
            if (!joined.empty())
                joined += ", ";
            joined += item;
        }
        return joined;
    }

    /// `WITH affected (object_id, column_id) AS (...)` — the columns about to be retyped,
    /// resolved to catalog ids. Every query below is restricted through this CTE.
    std::string AffectedCte(std::vector<ColumnToRetype> const& columns)
    {
        auto rows = std::vector<std::string> {};
        for (auto const& c: columns)
            rows.push_back(std::format("({}, {}, {})",
                                       c.schema.empty() ? std::string { "SCHEMA_NAME()" } : Literal(c.schema),
                                       Literal(c.table),
                                       Literal(c.column)));
        return std::format("WITH affected (object_id, column_id) AS ("
                           "SELECT c.object_id, c.column_id FROM sys.columns c "
                           "JOIN (VALUES {}) AS a (s, t, col) "
                           "ON c.object_id = OBJECT_ID(QUOTENAME(a.s) + N'.' + QUOTENAME(a.t)) AND c.name = a.col) ",
                           Join(rows));
    }

    /// Reads column `index` as a string (empty when NULL).
    std::string Text(SqlResultCursor& cursor, SQLUSMALLINT index)
    {
        return cursor.GetNullableColumn<std::string>(index).value_or(std::string {});
    }

    /// Reads column `index` as a 0/1 flag.
    bool Flag(SqlResultCursor& cursor, SQLUSMALLINT index)
    {
        return cursor.GetNullableColumn<int>(index).value_or(0) != 0;
    }

    /// Fails before anything is changed if objects depend on the columns that cannot be
    /// scripted safely: schema-bound views/functions, computed columns, non-rowstore or
    /// full-text indexes.
    void RejectUnsupportedDependents(SqlStatement& stmt, std::string const& cte)
    {
        auto blockers = std::vector<std::string> {};
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT o.type_desc, OBJECT_SCHEMA_NAME(d.referencing_id), OBJECT_NAME(d.referencing_id), "
              "COL_NAME(d.referenced_id, d.referenced_minor_id) "
              "FROM sys.sql_expression_dependencies d "
              "JOIN affected a ON a.object_id = d.referenced_id AND a.column_id = d.referenced_minor_id "
              "JOIN sys.objects o ON o.object_id = d.referencing_id WHERE d.referencing_class = 1 AND o.type <> 'C' "
              "UNION ALL SELECT N'INDEX (' + i.type_desc + N')', OBJECT_SCHEMA_NAME(i.object_id), i.name, "
              "COL_NAME(ic.object_id, ic.column_id) FROM sys.indexes i "
              "JOIN sys.index_columns ic ON ic.object_id = i.object_id AND ic.index_id = i.index_id "
              "JOIN affected a ON a.object_id = ic.object_id AND a.column_id = ic.column_id WHERE i.type > 2 "
              "UNION ALL SELECT N'FULLTEXT INDEX', OBJECT_SCHEMA_NAME(f.object_id), OBJECT_NAME(f.object_id), "
              "COL_NAME(f.object_id, f.column_id) FROM sys.fulltext_index_columns f "
              "JOIN affected a ON a.object_id = f.object_id AND a.column_id = f.column_id");
        while (cursor.FetchRow())
        {
            auto const kind = Text(cursor, 1);
            auto const schema = Text(cursor, 2);
            auto const name = Text(cursor, 3);
            auto const column = Text(cursor, 4);
            blockers.push_back(std::format("{} {}.{} (uses {})", kind, schema, name, column));
        }
        if (!blockers.empty())
            throw std::runtime_error(
                std::format("cannot change the column types: these objects depend on the columns and cannot be recreated "
                            "automatically; drop them first and recreate them afterwards: {}",
                            Join(blockers)));
    }

    /// One foreign key, accumulated over its column rows.
    struct ForeignKey
    {
        std::string name, schema, table, referencedSchema, referencedTable, onDelete, onUpdate;
        std::vector<std::string> columns, referencedColumns;
        bool notTrusted = false, disabled = false, notForReplication = false;
    };

    /// SQL Server reports referential actions as e.g. `SET_NULL`; T-SQL spells `SET NULL`.
    std::string ReferentialAction(std::string action)
    {
        std::ranges::replace(action, '_', ' ');
        return action;
    }

    void ScriptForeignKeys(SqlStatement& stmt, std::string const& cte, DependentObjectScript& script)
    {
        auto keys = std::vector<ForeignKey> {};
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT fk.name, OBJECT_SCHEMA_NAME(fk.parent_object_id), OBJECT_NAME(fk.parent_object_id), "
              "OBJECT_SCHEMA_NAME(fk.referenced_object_id), OBJECT_NAME(fk.referenced_object_id), "
              "COL_NAME(fkc.parent_object_id, fkc.parent_column_id), "
              "COL_NAME(fkc.referenced_object_id, fkc.referenced_column_id), "
              "fk.delete_referential_action_desc, fk.update_referential_action_desc, "
              "CAST(fk.is_not_trusted AS INT), CAST(fk.is_disabled AS INT), CAST(fk.is_not_for_replication AS INT) "
              "FROM sys.foreign_keys fk "
              "JOIN sys.foreign_key_columns fkc ON fkc.constraint_object_id = fk.object_id "
              "WHERE EXISTS (SELECT 1 FROM sys.foreign_key_columns x JOIN affected a ON "
              "(a.object_id = x.parent_object_id AND a.column_id = x.parent_column_id) OR "
              "(a.object_id = x.referenced_object_id AND a.column_id = x.referenced_column_id) "
              "WHERE x.constraint_object_id = fk.object_id) "
              "ORDER BY fk.object_id, fkc.constraint_column_id");
        while (cursor.FetchRow())
        {
            auto name = Text(cursor, 1);
            auto schema = Text(cursor, 2);
            auto table = Text(cursor, 3);
            auto referencedSchema = Text(cursor, 4);
            auto referencedTable = Text(cursor, 5);
            auto column = Text(cursor, 6);
            auto referencedColumn = Text(cursor, 7);
            auto onDelete = Text(cursor, 8);
            auto onUpdate = Text(cursor, 9);
            auto const notTrusted = Flag(cursor, 10);
            auto const disabled = Flag(cursor, 11);
            auto const notForReplication = Flag(cursor, 12);
            if (keys.empty() || keys.back().name != name || keys.back().table != table)
                keys.push_back(ForeignKey { .name = std::move(name),
                                            .schema = std::move(schema),
                                            .table = std::move(table),
                                            .referencedSchema = std::move(referencedSchema),
                                            .referencedTable = std::move(referencedTable),
                                            .onDelete = ReferentialAction(std::move(onDelete)),
                                            .onUpdate = ReferentialAction(std::move(onUpdate)),
                                            .columns = {},
                                            .referencedColumns = {},
                                            .notTrusted = notTrusted,
                                            .disabled = disabled,
                                            .notForReplication = notForReplication });
            keys.back().columns.push_back(Bracket(column));
            keys.back().referencedColumns.push_back(Bracket(referencedColumn));
        }

        for (auto const& fk: keys)
        {
            auto const table = QualifiedTable(fk.schema, fk.table);
            script.drop.push_back(std::format("ALTER TABLE {} DROP CONSTRAINT {}", table, Bracket(fk.name)));
            script.recreate.push_back(
                std::format("ALTER TABLE {} WITH {} ADD CONSTRAINT {} FOREIGN KEY ({}) REFERENCES {} ({}) "
                            "ON DELETE {} ON UPDATE {}{}",
                            table,
                            fk.notTrusted ? "NOCHECK" : "CHECK",
                            Bracket(fk.name),
                            Join(fk.columns),
                            QualifiedTable(fk.referencedSchema, fk.referencedTable),
                            Join(fk.referencedColumns),
                            fk.onDelete,
                            fk.onUpdate,
                            fk.notForReplication ? " NOT FOR REPLICATION" : ""));
            if (fk.disabled)
                script.recreate.push_back(std::format("ALTER TABLE {} NOCHECK CONSTRAINT {}", table, Bracket(fk.name)));
            script.descriptions.push_back(std::format("FOREIGN KEY {} on {}.{}", fk.name, fk.schema, fk.table));
        }
    }

    /// A key constraint or index, accumulated over its column rows.
    struct KeyOrIndex
    {
        std::string name, schema, table, kind, typeDesc, filter;
        std::vector<std::string> keyColumns, includedColumns;
        bool unique = false;
    };

    /// Primary-key and unique constraints whose index uses an affected column.
    std::vector<KeyOrIndex> ReadKeyConstraints(SqlStatement& stmt, std::string const& cte)
    {
        auto keys = std::vector<KeyOrIndex> {};
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT kc.name, OBJECT_SCHEMA_NAME(kc.parent_object_id), OBJECT_NAME(kc.parent_object_id), kc.type, "
              "i.type_desc, COL_NAME(ic.object_id, ic.column_id), CAST(ic.is_descending_key AS INT) "
              "FROM sys.key_constraints kc "
              "JOIN sys.indexes i ON i.object_id = kc.parent_object_id AND i.index_id = kc.unique_index_id "
              "JOIN sys.index_columns ic ON ic.object_id = i.object_id AND ic.index_id = i.index_id "
              "AND ic.key_ordinal > 0 "
              "WHERE EXISTS (SELECT 1 FROM sys.index_columns x JOIN affected a ON a.object_id = x.object_id "
              "AND a.column_id = x.column_id WHERE x.object_id = i.object_id AND x.index_id = i.index_id) "
              "ORDER BY kc.type, kc.object_id, ic.key_ordinal");
        while (cursor.FetchRow())
        {
            auto name = Text(cursor, 1);
            auto schema = Text(cursor, 2);
            auto table = Text(cursor, 3);
            auto kind = Text(cursor, 4);
            auto typeDesc = Text(cursor, 5);
            auto const column = Text(cursor, 6);
            auto const descending = Flag(cursor, 7);
            if (keys.empty() || keys.back().name != name || keys.back().table != table)
                keys.push_back(KeyOrIndex { .name = std::move(name),
                                            .schema = std::move(schema),
                                            .table = std::move(table),
                                            .kind = std::move(kind),
                                            .typeDesc = std::move(typeDesc),
                                            .filter = {},
                                            .keyColumns = {},
                                            .includedColumns = {},
                                            .unique = true });
            keys.back().keyColumns.push_back(std::format("{} {}", Bracket(column), descending ? "DESC" : "ASC"));
        }
        return keys;
    }

    /// Plain (non-constraint) rowstore indexes that use an affected column as key or included column.
    std::vector<KeyOrIndex> ReadIndexes(SqlStatement& stmt, std::string const& cte)
    {
        auto indexes = std::vector<KeyOrIndex> {};
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT i.name, OBJECT_SCHEMA_NAME(i.object_id), OBJECT_NAME(i.object_id), i.type_desc, "
              "CAST(i.is_unique AS INT), i.filter_definition, COL_NAME(ic.object_id, ic.column_id), "
              "CAST(ic.is_descending_key AS INT), CAST(ic.is_included_column AS INT) "
              "FROM sys.indexes i "
              "JOIN sys.index_columns ic ON ic.object_id = i.object_id AND ic.index_id = i.index_id "
              "WHERE i.type IN (1, 2) AND i.is_primary_key = 0 AND i.is_unique_constraint = 0 "
              "AND OBJECTPROPERTY(i.object_id, 'IsUserTable') = 1 "
              "AND EXISTS (SELECT 1 FROM sys.index_columns x JOIN affected a ON a.object_id = x.object_id "
              "AND a.column_id = x.column_id WHERE x.object_id = i.object_id AND x.index_id = i.index_id) "
              "ORDER BY i.object_id, i.index_id, ic.is_included_column, ic.key_ordinal, ic.index_column_id");
        while (cursor.FetchRow())
        {
            auto name = Text(cursor, 1);
            auto schema = Text(cursor, 2);
            auto table = Text(cursor, 3);
            auto typeDesc = Text(cursor, 4);
            auto const unique = Flag(cursor, 5);
            auto filter = Text(cursor, 6);
            auto const column = Text(cursor, 7);
            auto const descending = Flag(cursor, 8);
            auto const included = Flag(cursor, 9);
            if (indexes.empty() || indexes.back().name != name || indexes.back().table != table)
                indexes.push_back(KeyOrIndex { .name = std::move(name),
                                               .schema = std::move(schema),
                                               .table = std::move(table),
                                               .kind = "INDEX",
                                               .typeDesc = std::move(typeDesc),
                                               .filter = std::move(filter),
                                               .keyColumns = {},
                                               .includedColumns = {},
                                               .unique = unique });
            if (included)
                indexes.back().includedColumns.push_back(Bracket(column));
            else
                indexes.back().keyColumns.push_back(std::format("{} {}", Bracket(column), descending ? "DESC" : "ASC"));
        }
        return indexes;
    }

    void ScriptKeysAndIndexes(SqlStatement& stmt, std::string const& cte, DependentObjectScript& script)
    {
        // Indexes are dropped before the keys (a clustered key rebuilds every other index
        // when it goes) and recreated after them, in reverse.
        auto const indexes = ReadIndexes(stmt, cte);
        auto const keys = ReadKeyConstraints(stmt, cte);

        for (auto const& index: indexes)
            script.drop.push_back(
                std::format("DROP INDEX {} ON {}", Bracket(index.name), QualifiedTable(index.schema, index.table)));
        for (auto const& key: keys)
            script.drop.push_back(
                std::format("ALTER TABLE {} DROP CONSTRAINT {}", QualifiedTable(key.schema, key.table), Bracket(key.name)));

        for (auto const& key: keys)
        {
            script.recreate.push_back(std::format("ALTER TABLE {} ADD CONSTRAINT {} {} {} ({})",
                                                  QualifiedTable(key.schema, key.table),
                                                  Bracket(key.name),
                                                  key.kind == "PK" ? "PRIMARY KEY" : "UNIQUE",
                                                  key.typeDesc,
                                                  Join(key.keyColumns)));
            script.descriptions.push_back(
                std::format("{} {} on {}.{}", key.kind == "PK" ? "PRIMARY KEY" : "UNIQUE", key.name, key.schema, key.table));
        }
        for (auto const& index: indexes)
        {
            script.recreate.push_back(std::format(
                "CREATE {}{} INDEX {} ON {} ({}){}{}",
                index.unique ? "UNIQUE " : "",
                index.typeDesc,
                Bracket(index.name),
                QualifiedTable(index.schema, index.table),
                Join(index.keyColumns),
                index.includedColumns.empty() ? std::string {} : std::format(" INCLUDE ({})", Join(index.includedColumns)),
                index.filter.empty() ? std::string {} : std::format(" WHERE {}", index.filter)));
            script.needsAnsiWarnings = script.needsAnsiWarnings || !index.filter.empty();
            script.descriptions.push_back(std::format("INDEX {} on {}.{}", index.name, index.schema, index.table));
        }
    }

    /// Column statistics (not backing an index). User-created ones are recreated;
    /// auto-created ones are only dropped — SQL Server rebuilds them on demand.
    void ScriptStatistics(SqlStatement& stmt, std::string const& cte, DependentObjectScript& script)
    {
        struct Statistics
        {
            std::string name, schema, table;
            std::vector<std::string> columns;
            bool userCreated = false;
        };
        auto stats = std::vector<Statistics> {};
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT s.name, OBJECT_SCHEMA_NAME(s.object_id), OBJECT_NAME(s.object_id), "
              "COL_NAME(sc.object_id, sc.column_id), CAST(s.user_created AS INT) "
              "FROM sys.stats s JOIN sys.stats_columns sc ON sc.object_id = s.object_id AND sc.stats_id = s.stats_id "
              "WHERE NOT EXISTS (SELECT 1 FROM sys.indexes ix WHERE ix.object_id = s.object_id "
              "AND ix.index_id = s.stats_id) "
              "AND EXISTS (SELECT 1 FROM sys.stats_columns x JOIN affected a ON a.object_id = x.object_id "
              "AND a.column_id = x.column_id WHERE x.object_id = s.object_id AND x.stats_id = s.stats_id) "
              "ORDER BY s.object_id, s.stats_id, sc.stats_column_id");
        while (cursor.FetchRow())
        {
            auto name = Text(cursor, 1);
            auto schema = Text(cursor, 2);
            auto table = Text(cursor, 3);
            auto const column = Text(cursor, 4);
            auto const userCreated = Flag(cursor, 5);
            if (stats.empty() || stats.back().name != name || stats.back().table != table)
                stats.push_back(Statistics { .name = std::move(name),
                                             .schema = std::move(schema),
                                             .table = std::move(table),
                                             .columns = {},
                                             .userCreated = userCreated });
            stats.back().columns.push_back(Bracket(column));
        }

        for (auto const& s: stats)
        {
            auto const table = QualifiedTable(s.schema, s.table);
            script.drop.push_back(std::format("DROP STATISTICS {}.{}", table, Bracket(s.name)));
            if (!s.userCreated)
                continue;
            script.recreate.push_back(
                std::format("CREATE STATISTICS {} ON {} ({})", Bracket(s.name), table, Join(s.columns)));
            script.descriptions.push_back(std::format("STATISTICS {} on {}.{}", s.name, s.schema, s.table));
        }
    }

    void ScriptDefaultConstraints(SqlStatement& stmt, std::string const& cte, DependentObjectScript& script)
    {
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT dc.name, OBJECT_SCHEMA_NAME(dc.parent_object_id), OBJECT_NAME(dc.parent_object_id), "
              "COL_NAME(dc.parent_object_id, dc.parent_column_id), dc.definition "
              "FROM sys.default_constraints dc "
              "JOIN affected a ON a.object_id = dc.parent_object_id AND a.column_id = dc.parent_column_id");
        while (cursor.FetchRow())
        {
            auto const name = Text(cursor, 1);
            auto const schema = Text(cursor, 2);
            auto const table = Text(cursor, 3);
            auto const column = Text(cursor, 4);
            auto const definition = Text(cursor, 5);
            auto const qualified = QualifiedTable(schema, table);
            script.drop.push_back(std::format("ALTER TABLE {} DROP CONSTRAINT {}", qualified, Bracket(name)));
            script.recreate.push_back(std::format("ALTER TABLE {} ADD CONSTRAINT {} DEFAULT {} FOR {}",
                                                  qualified,
                                                  Bracket(name),
                                                  definition,
                                                  Bracket(column)));
            script.descriptions.push_back(std::format("DEFAULT {} on {}.{}", name, schema, table));
        }
    }

    void ScriptCheckConstraints(SqlStatement& stmt, std::string const& cte, DependentObjectScript& script)
    {
        auto cursor = stmt.ExecuteDirect(
            cte
            + "SELECT cc.name, OBJECT_SCHEMA_NAME(cc.parent_object_id), OBJECT_NAME(cc.parent_object_id), "
              "cc.definition, CAST(cc.is_not_trusted AS INT), CAST(cc.is_disabled AS INT), "
              "CAST(cc.is_not_for_replication AS INT) "
              "FROM sys.check_constraints cc "
              "WHERE EXISTS (SELECT 1 FROM affected a WHERE a.object_id = cc.parent_object_id AND "
              "(a.column_id = cc.parent_column_id OR EXISTS (SELECT 1 FROM sys.sql_expression_dependencies d "
              "WHERE d.referencing_id = cc.object_id AND d.referenced_id = a.object_id "
              "AND d.referenced_minor_id = a.column_id)))");
        while (cursor.FetchRow())
        {
            auto const name = Text(cursor, 1);
            auto const schema = Text(cursor, 2);
            auto const table = Text(cursor, 3);
            auto const definition = Text(cursor, 4);
            auto const notTrusted = Flag(cursor, 5);
            auto const disabled = Flag(cursor, 6);
            auto const notForReplication = Flag(cursor, 7);
            auto const qualified = QualifiedTable(schema, table);
            script.drop.push_back(std::format("ALTER TABLE {} DROP CONSTRAINT {}", qualified, Bracket(name)));
            script.recreate.push_back(std::format("ALTER TABLE {} WITH {} ADD CONSTRAINT {} CHECK{} {}",
                                                  qualified,
                                                  notTrusted ? "NOCHECK" : "CHECK",
                                                  Bracket(name),
                                                  notForReplication ? " NOT FOR REPLICATION" : "",
                                                  definition));
            if (disabled)
                script.recreate.push_back(std::format("ALTER TABLE {} NOCHECK CONSTRAINT {}", qualified, Bracket(name)));
            script.descriptions.push_back(std::format("CHECK {} on {}.{}", name, schema, table));
        }
    }

} // namespace

DependentObjectScript ScriptSqlServerDependentObjects(SqlStatement& stmt, std::vector<ColumnToRetype> const& columns)
{
    auto script = DependentObjectScript {};
    if (columns.empty())
        return script;

    auto const cte = AffectedCte(columns);
    RejectUnsupportedDependents(stmt, cte);

    // Collected separately so the drop order is: foreign keys (they pin the keys they
    // reference), statistics, indexes, keys, defaults, checks — and the recreate order is
    // the reverse dependency order, with foreign keys last.
    auto foreignKeys = DependentObjectScript {};
    auto keysAndIndexes = DependentObjectScript {};
    auto statistics = DependentObjectScript {};
    auto defaults = DependentObjectScript {};
    auto checks = DependentObjectScript {};
    ScriptForeignKeys(stmt, cte, foreignKeys);
    ScriptKeysAndIndexes(stmt, cte, keysAndIndexes);
    ScriptStatistics(stmt, cte, statistics);
    ScriptDefaultConstraints(stmt, cte, defaults);
    ScriptCheckConstraints(stmt, cte, checks);

    for (auto const* part: { &foreignKeys, &statistics, &keysAndIndexes, &defaults, &checks })
        script.drop.insert(script.drop.end(), part->drop.begin(), part->drop.end());
    for (auto const* part: { &checks, &defaults, &keysAndIndexes, &statistics, &foreignKeys })
    {
        script.recreate.insert(script.recreate.end(), part->recreate.begin(), part->recreate.end());
        script.descriptions.insert(script.descriptions.end(), part->descriptions.begin(), part->descriptions.end());
    }
    script.needsAnsiWarnings = keysAndIndexes.needsAnsiWarnings;
    return script;
}

} // namespace Lightweight::detail
