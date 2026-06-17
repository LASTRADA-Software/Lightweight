// SPDX-License-Identifier: Apache-2.0

#include "SqlColumnTypeDefinitions.hpp"
#include "SqlError.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlSchema.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

namespace Lightweight::SqlSchema
{

using namespace std::string_literals;
using namespace std::string_view_literals;

using ::Lightweight::detail::OdbcWideArg;

namespace
{
    template <typename A, typename B>
    std::tuple<std::vector<A>, std::vector<B>> Unzip(std::vector<std::pair<A, B>> const& zippedList)
    {
        auto first = std::vector<A>();
        auto second = std::vector<B>();
        first.reserve(zippedList.size());
        second.reserve(zippedList.size());
        for (auto const& [a, b]: zippedList)
        {
            first.emplace_back(a);
            second.emplace_back(b);
        }
        return { std::move(first), std::move(second) };
    }

    /// Represents a table with its schema name.
    struct TableWithSchema
    {
        std::string schema;
        std::string name;
    };

    std::vector<TableWithSchema> AllTables(SqlStatement& stmt, std::string_view database, std::string_view schema)
    {
        auto wDatabase = OdbcWideArg { database };
        auto wSchema = OdbcWideArg { schema };
        auto sqlResult = SQLTablesW(stmt.NativeHandle(),
                                    wDatabase.data(),
                                    wDatabase.length(),
                                    wSchema.data(),
                                    wSchema.length(),
                                    nullptr,
                                    0,
                                    nullptr,
                                    0);
        SqlErrorInfo::RequireStatementSuccess(sqlResult, stmt.NativeHandle(), "SQLTables");

        auto result = std::vector<TableWithSchema>();
        auto cursor = SqlResultCursor(stmt);
        while (cursor.FetchRow())
        {
            auto schemaOpt = cursor.GetNullableColumn<std::string>(2);
            auto nameOpt = cursor.GetNullableColumn<std::string>(3);
            auto typeOpt = cursor.GetNullableColumn<std::string>(4);
            if (!nameOpt)
                continue;

            auto const schemaName = schemaOpt.value_or("");
            auto const& name = *nameOpt;
            auto const type = typeOpt.value_or("");

            if (schemaName == "sys" || schemaName == "INFORMATION_SCHEMA")
                continue;

            if (type == "TABLE" || type == "BASE TABLE")
                result.emplace_back(TableWithSchema { .schema = schemaName, .name = name });
        }

        return result;
    }

    /// SQLite-specific path: the SQLite ODBC driver does not populate FK_NAME (column 12 of
    /// SQLForeignKeys), so two distinct FK constraints between the same pair of tables
    /// collapse into one entry — see the FK_NAME-keyed grouping below. PRAGMA
    /// foreign_key_list returns each constraint with a stable `id` we can use as a
    /// synthetic name. Only used for the "FKs originating from `table`" lookup, which is
    /// what the schema reader needs per table.
    [[nodiscard]] std::vector<ForeignKeyConstraint> AllForeignKeysFromSqlite(SqlStatement& stmt,
                                                                             FullyQualifiedTableName const& table)
    {
        auto query = !table.schema.empty() ? std::format(R"(PRAGMA "{}".foreign_key_list("{}"))", table.schema, table.table)
                                           : std::format(R"(PRAGMA foreign_key_list("{}"))", table.table);

        auto cursor = stmt.ExecuteDirect(query);

        // Group rows by `id` (column 1) — each unique id is one FK constraint.
        struct Row
        {
            int seq {};
            std::string fkColumn;
            std::string pkColumn;
            std::string targetTable;
        };
        auto byId = std::map<int, std::vector<Row>> {};
        auto idOrder = std::vector<int> {};
        while (cursor.FetchRow())
        {
            // 0 id, 1 seq, 2 table, 3 from, 4 to, 5 on_update, 6 on_delete, 7 match
            auto const id = cursor.GetColumn<int>(1);
            auto row = Row {
                .seq = cursor.GetColumn<int>(2),
                .fkColumn = cursor.GetColumn<std::string>(4),
                .pkColumn = cursor.GetColumn<std::string>(5),
                .targetTable = cursor.GetColumn<std::string>(3),
            };
            if (!byId.contains(id))
                idOrder.push_back(id);
            byId[id].push_back(std::move(row));
        }

        auto result = std::vector<ForeignKeyConstraint> {};
        result.reserve(idOrder.size());
        for (auto const id: idOrder)
        {
            auto rows = std::move(byId.at(id));
            std::ranges::sort(rows, [](Row const& a, Row const& b) { return a.seq < b.seq; });
            auto fk = ForeignKeyConstraint {
                .foreignKey = { .table = table, .columns = {} },
                .primaryKey = {
                    .table = FullyQualifiedTableName {
                        .catalog = {},
                        .schema = {},
                        .table = rows.front().targetTable,
                    },
                    .columns = {},
                },
            };
            for (auto& row: rows)
            {
                fk.foreignKey.columns.push_back(std::move(row.fkColumn));
                fk.primaryKey.columns.push_back(std::move(row.pkColumn));
            }
            result.push_back(std::move(fk));
        }
        return result;
    }

    std::vector<ForeignKeyConstraint> AllForeignKeys(SqlStatement& stmt,
                                                     FullyQualifiedTableName const& primaryKey,
                                                     FullyQualifiedTableName const& foreignKey)
    {
        // SQLite ODBC's SQLForeignKeys does not return FK_NAME, so distinct FKs between the
        // same table pair collapse. Use PRAGMA foreign_key_list when querying outbound FKs
        // (the case the table reader uses).
        if (stmt.Connection().ServerType() == SqlServerType::SQLITE && primaryKey.table.empty() && !foreignKey.table.empty())
        {
            return AllForeignKeysFromSqlite(stmt, foreignKey);
        }

        auto wPkCatalog = OdbcWideArg { primaryKey.catalog };
        auto wPkSchema = OdbcWideArg { primaryKey.schema };
        auto wPkTable = OdbcWideArg { primaryKey.table };
        auto wFkCatalog = OdbcWideArg { foreignKey.catalog };
        auto wFkSchema = OdbcWideArg { foreignKey.schema };
        auto wFkTable = OdbcWideArg { foreignKey.table };
        auto sqlResult = SQLForeignKeysW(stmt.NativeHandle(),
                                         wPkCatalog.data(),
                                         wPkCatalog.length(),
                                         wPkSchema.data(),
                                         wPkSchema.length(),
                                         wPkTable.data(),
                                         wPkTable.length(),
                                         wFkCatalog.data(),
                                         wFkCatalog.length(),
                                         wFkSchema.data(),
                                         wFkSchema.length(),
                                         wFkTable.data(),
                                         wFkTable.length());

        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLForeignKeys failed: {}", stmt.LastError()));

        // ODBC SQLForeignKeys() should return at least 14 columns per the spec.
        // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
        // return fewer columns. We need at least 9 columns (KEY_SEQ) to process foreign keys.
        auto cursor = SqlResultCursor(stmt);
        constexpr size_t MinRequiredColumns = 9;
        auto const numColumns = cursor.NumColumnsAffected();
        if (numColumns < MinRequiredColumns)
            return {}; // Driver didn't return expected columns, return empty result

        // Group rows that belong to the same constraint. ODBC SQLForeignKeys returns one row
        // per (constraint, column-position) tuple — composite keys span multiple rows. The
        // grouping key must include the constraint name (FK_NAME, column 12), otherwise two
        // separate FKs that happen to point between the same pair of tables (e.g.
        // `ASPHALT.BINDEMITTEL_NR -> BINDEMITTEL` and `ASPHALT.BINDEMITTEL2_NR -> BINDEMITTEL`)
        // collapse into one and the second overwrites the first at sequence number 1.
        struct ConstraintKey
        {
            std::string fkName;
            FullyQualifiedTableName fkTable;
            FullyQualifiedTableName pkTable;
            bool operator<(ConstraintKey const& other) const noexcept
            {
                return std::tie(fkName, fkTable, pkTable) < std::tie(other.fkName, other.fkTable, other.pkTable);
            }
        };
        using ColumnList = std::vector<std::pair<std::string /*fk*/, std::string /*pk*/>>;
        auto constraints = std::map<ConstraintKey, ColumnList> {};
        // Track insertion order so the result preserves the driver-reported order rather than
        // the lexical order of constraint names.
        auto constraintOrder = std::vector<ConstraintKey> {};
        auto seen = std::set<ConstraintKey> {};
        while (cursor.FetchRow())
        {
            auto primaryKeyTable = FullyQualifiedTableName {
                .catalog = cursor.GetNullableColumn<std::string>(1).value_or(""),
                .schema = cursor.GetNullableColumn<std::string>(2).value_or(""),
                .table = cursor.GetNullableColumn<std::string>(3).value_or(""),
            };
            auto pkColumnName = cursor.GetNullableColumn<std::string>(4).value_or("");
            auto foreignKeyTable = FullyQualifiedTableName {
                .catalog = cursor.GetNullableColumn<std::string>(5).value_or(""),
                .schema = cursor.GetNullableColumn<std::string>(6).value_or(""),
                .table = cursor.GetNullableColumn<std::string>(7).value_or(""),
            };
            auto foreignKeyColumn = cursor.GetNullableColumn<std::string>(8).value_or("");
            auto const sequenceNumber = static_cast<size_t>(cursor.GetNullableColumn<int16_t>(9).value_or(1));
            // FK_NAME is column 12 in the ODBC spec; some drivers may not expose it.
            auto fkName = numColumns >= 12 ? cursor.GetNullableColumn<std::string>(12).value_or("") : std::string {};
            auto key = ConstraintKey {
                .fkName = std::move(fkName),
                .fkTable = std::move(foreignKeyTable),
                .pkTable = std::move(primaryKeyTable),
            };
            if (seen.insert(key).second)
                constraintOrder.push_back(key);
            ColumnList& keyColumns = constraints[key];
            if (sequenceNumber > keyColumns.size())
                keyColumns.resize(sequenceNumber);
            keyColumns[sequenceNumber - 1] = { std::move(foreignKeyColumn), std::move(pkColumnName) };
        }

        auto result = std::vector<ForeignKeyConstraint> {};
        result.reserve(constraintOrder.size());
        for (auto const& key: constraintOrder)
        {
            auto const& columns = constraints.at(key);
            auto const [fromColumns, toColumns] = Unzip(columns);
            result.emplace_back(ForeignKeyConstraint {
                .foreignKey = {
                    .table = key.fkTable,
                    .columns = fromColumns,
                },
                .primaryKey = {
                    .table = key.pkTable,
                    .columns = toColumns,
                },
            });
        }
        return result;
    }

    std::vector<std::string> AllPrimaryKeys(SqlStatement& stmt, FullyQualifiedTableName const& table)
    {
        std::vector<std::string> keys;
        std::vector<size_t> sequenceNumbers;

        if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
        {
            // SQLite ODBC driver might return incorrect KEY_SEQ. Use PRAGMA table_info instead.
            std::string query;
            if (!table.schema.empty())
                query = std::format(R"(PRAGMA "{}".table_info("{}"))", table.schema, table.table);
            else
                query = std::format("PRAGMA table_info(\"{}\")", table.table);

            auto cursor = stmt.ExecuteDirect(query);

            std::vector<std::pair<int, std::string>> pkCols;
            while (cursor.FetchRow())
            {
                // cid, name, type, notnull, dflt_value, pk
                auto name = cursor.GetColumn<std::string>(2);
                auto pkInfo = cursor.GetColumn<int>(6);
                if (pkInfo > 0)
                    pkCols.emplace_back(pkInfo, name);
            }
            std::ranges::sort(pkCols, [](auto const& a, auto const& b) { return a.first < b.first; });

            std::vector<std::string> sortedKeys;
            sortedKeys.reserve(pkCols.size());
            for (auto const& [index, name]: pkCols)
                sortedKeys.push_back(name);

            return sortedKeys;
        }
        auto wCatalog = OdbcWideArg { table.catalog };
        auto wSchema = OdbcWideArg { table.schema };
        auto wTable = OdbcWideArg { table.table };

        auto sqlResult = SQLPrimaryKeysW(stmt.NativeHandle(),
                                         wCatalog.data(),
                                         wCatalog.length(),
                                         wSchema.data(),
                                         wSchema.length(),
                                         wTable.data(),
                                         wTable.length());
        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLPrimaryKeys failed: {}", stmt.LastError()));

        auto cursor = SqlResultCursor(stmt);
        while (cursor.FetchRow())
        {
            keys.emplace_back(cursor.GetNullableColumn<std::string>(4).value_or(""));
            sequenceNumbers.emplace_back(cursor.GetNullableColumn<int16_t>(5).value_or(0));
        }

        std::vector<std::string> sortedKeys;
        sortedKeys.resize(keys.size());
        for (size_t i = 0; i < keys.size(); ++i)
        {
            if (sequenceNumbers[i] > 0 && sequenceNumbers[i] <= sortedKeys.size())
                sortedKeys.at(sequenceNumbers[i] - 1) = keys[i];
        }

        // Clean up empty entries if sequenceNumbers were non-contiguous or invalid (defensive)
        std::erase_if(sortedKeys, [](auto const& s) { return s.empty(); });

        return sortedKeys;
    }

    std::vector<std::string> AllUniqueColumns(SqlStatement& stmt, FullyQualifiedTableName const& table)
    {
        auto wCatalog = OdbcWideArg { table.catalog };
        auto wSchema = OdbcWideArg { table.schema };
        auto wTable = OdbcWideArg { table.table };

        auto sqlResult = SQLStatisticsW(stmt.NativeHandle(),
                                        wCatalog.data(),
                                        wCatalog.length(),
                                        wSchema.data(),
                                        wSchema.length(),
                                        wTable.data(),
                                        wTable.length(),
                                        SQL_INDEX_UNIQUE,
                                        SQL_ENSURE);

        if (!SQL_SUCCEEDED(sqlResult))
            return {}; // Ignore errors or throw? Safest to ignore for now as some drivers might not support it fully.

        // ODBC SQLStatistics() should return at least 13 columns per the spec.
        // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
        // return fewer columns. We need at least 9 columns (COLUMN_NAME) to process unique columns.
        auto cursor = SqlResultCursor(stmt);
        constexpr size_t MinRequiredColumns = 9;
        auto const numColumns = cursor.NumColumnsAffected();
        if (numColumns < MinRequiredColumns)
            return {}; // Driver didn't return expected columns, return empty result

        std::map<std::string, std::vector<std::string>> uniqueIndices;

        while (cursor.FetchRow())
        {
            // Col 6: INDEX_NAME
            // Col 9: COLUMN_NAME
            auto indexName = cursor.GetNullableColumn<std::string>(6).value_or("");
            auto columnName = cursor.GetNullableColumn<std::string>(9).value_or("");
            if (!indexName.empty() && !columnName.empty())
                uniqueIndices[indexName].push_back(columnName);
        }

        std::vector<std::string> uniqueColumns;
        for (auto const& [name, cols]: uniqueIndices)
        {
            if (cols.size() == 1)
                uniqueColumns.push_back(cols[0]);
        }
        return uniqueColumns;
    }

    /// Retrieves all non-primary key indexes for a table using ODBC SQLStatistics.
    ///
    /// @param stmt The SQL statement to use for reading.
    /// @param table The fully qualified table name.
    /// @param primaryKeys The list of primary key columns (used to filter out PK indexes).
    /// @return A vector of index definitions, excluding primary key indexes.
    /// MSSQL-specific path: SQLStatistics on the Microsoft ODBC driver returns no index
    /// rows for non-clustered, non-unique indexes in our environment, even with
    /// SQL_INDEX_ALL — leaving the schema reader thinking MSSQL has none. Query
    /// `sys.indexes` directly so the introspection lines up with what migrations created.
    [[nodiscard]] std::vector<IndexDefinition> AllIndexesMssql(SqlStatement& stmt,
                                                               FullyQualifiedTableName const& table,
                                                               std::vector<std::string> const& primaryKeys)
    {
        auto sql = std::string {};
        auto const schemaFilter =
            !table.schema.empty() ? std::format("AND SCHEMA_NAME(t.schema_id) = '{}'", table.schema) : std::string {};
        sql = std::format(R"(SELECT i.name AS index_name,
                                    i.is_unique AS is_unique,
                                    c.name AS column_name,
                                    ic.key_ordinal AS key_ordinal
                             FROM sys.indexes i
                             INNER JOIN sys.tables t ON i.object_id = t.object_id
                             INNER JOIN sys.index_columns ic ON ic.object_id = i.object_id AND ic.index_id = i.index_id
                             INNER JOIN sys.columns c ON c.object_id = ic.object_id AND c.column_id = ic.column_id
                             WHERE t.name = '{}' {}
                               AND i.is_primary_key = 0
                               AND i.is_unique_constraint = 0
                               AND i.type > 0
                               AND ic.is_included_column = 0
                             ORDER BY i.name, ic.key_ordinal)",
                          table.table,
                          schemaFilter);

        auto cursor = stmt.ExecuteDirect(sql);
        struct Info
        {
            bool isUnique = false;
            std::vector<std::pair<int, std::string>> columns;
        };
        auto byName = std::map<std::string, Info> {};
        auto order = std::vector<std::string> {};
        while (cursor.FetchRow())
        {
            auto name = cursor.GetColumn<std::string>(1);
            auto isUnique = cursor.GetColumn<bool>(2);
            auto column = cursor.GetColumn<std::string>(3);
            auto ordinal = cursor.GetColumn<int>(4);
            if (!byName.contains(name))
                order.push_back(name);
            auto& info = byName[name];
            info.isUnique = isUnique;
            info.columns.emplace_back(ordinal, std::move(column));
        }

        auto result = std::vector<IndexDefinition> {};
        result.reserve(order.size());
        for (auto const& name: order)
        {
            auto info = std::move(byName.at(name));
            std::ranges::sort(info.columns, [](auto const& a, auto const& b) { return a.first < b.first; });
            auto cols = std::vector<std::string> {};
            cols.reserve(info.columns.size());
            for (auto& [_, col]: info.columns)
                cols.push_back(std::move(col));

            // Skip indexes whose columns exactly match the table's primary key — those are
            // implicit PK indexes that the cross-engine reader treats separately.
            bool const matchesPk =
                cols.size() == primaryKeys.size() && std::ranges::equal(cols, primaryKeys, [](auto const& a, auto const& b) {
                    return std::ranges::equal(a, b, [](char c1, char c2) {
                        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
                    });
                });
            if (matchesPk)
                continue;

            result.push_back(IndexDefinition {
                .name = name,
                .columns = std::move(cols),
                .isUnique = info.isUnique,
            });
        }
        return result;
    }

    std::vector<IndexDefinition> AllIndexes(SqlStatement& stmt,
                                            FullyQualifiedTableName const& table,
                                            std::vector<std::string> const& primaryKeys)
    {
        if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
            return AllIndexesMssql(stmt, table, primaryKeys);

        try
        {
            // Use a separate statement to avoid issues with pending cursors from previous operations
            auto indexStmt = SqlStatement { stmt.Connection() };

            auto wCatalog = OdbcWideArg { table.catalog };
            auto wSchema = OdbcWideArg { table.schema };
            auto wTable = OdbcWideArg { table.table };

            // Use SQL_INDEX_ALL to retrieve all index types
            auto sqlResult = SQLStatisticsW(indexStmt.NativeHandle(),
                                            wCatalog.data(),
                                            wCatalog.length(),
                                            wSchema.data(),
                                            wSchema.length(),
                                            wTable.data(),
                                            wTable.length(),
                                            SQL_INDEX_ALL,
                                            SQL_ENSURE);

            if (!SQL_SUCCEEDED(sqlResult))
                return {};

            // ODBC SQLStatistics() should return at least 13 columns per the spec.
            // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
            // return fewer columns. We need at least 9 columns (COLUMN_NAME) to process indexes.
            auto indexCursor = SqlResultCursor(indexStmt);
            constexpr size_t MinRequiredColumns = 9;
            auto const numColumns = indexCursor.NumColumnsAffected();
            if (numColumns < MinRequiredColumns)
                return {}; // Driver didn't return expected columns, return empty result

            // Map: index_name -> (columns sorted by ordinal_position, isUnique)
            struct IndexInfo
            {
                std::vector<std::pair<int16_t, std::string>> columnsWithOrdinal; // (ordinal, column_name)
                bool isUnique = false;
            };
            std::map<std::string, IndexInfo> indexMap;

            while (indexCursor.FetchRow())
            {
                // Column mappings from SQLStatistics result set:
                //   4: NON_UNIQUE (0 = unique, 1 = not unique, NULL for statistics rows)
                //   6: INDEX_NAME
                //   7: TYPE (SQL_TABLE_STAT=0, SQL_INDEX_CLUSTERED=1, SQL_INDEX_HASHED=2, SQL_INDEX_OTHER=3)
                //   8: ORDINAL_POSITION (column position in index)
                //   9: COLUMN_NAME

                // Skip statistics rows (TYPE == 0)
                auto typeOpt = indexCursor.GetNullableColumn<int16_t>(7);
                if (!typeOpt || *typeOpt == 0)
                    continue;

                auto indexNameOpt = indexCursor.GetNullableColumn<std::string>(6);
                auto columnNameOpt = indexCursor.GetNullableColumn<std::string>(9);

                if (!indexNameOpt || indexNameOpt->empty() || !columnNameOpt || columnNameOpt->empty())
                    continue;

                auto ordinalOpt = indexCursor.GetNullableColumn<int16_t>(8);
                auto nonUniqueOpt = indexCursor.GetNullableColumn<int16_t>(4);

                IndexInfo& info = indexMap[*indexNameOpt];
                info.columnsWithOrdinal.emplace_back(ordinalOpt.value_or(1), *columnNameOpt);
                // NON_UNIQUE: 0 means unique, 1 means not unique
                info.isUnique = (nonUniqueOpt.value_or(1) == 0);
            }

            // Convert map to vector, sorting columns by ordinal position
            std::vector<IndexDefinition> result;
            result.reserve(indexMap.size());

            for (auto& [indexName, info]: indexMap)
            {
                // Sort columns by ordinal position
                std::ranges::sort(info.columnsWithOrdinal, [](auto const& a, auto const& b) { return a.first < b.first; });

                // Extract column names
                std::vector<std::string> columns;
                columns.reserve(info.columnsWithOrdinal.size());
                for (auto const& [ordinal, col]: info.columnsWithOrdinal)
                    columns.push_back(col);

                // Filter out primary key indexes by comparing columns
                // An index matches the PK if it has the same columns in the same order
                bool isPrimaryKeyIndex = (columns.size() == primaryKeys.size())
                                         && std::ranges::equal(columns, primaryKeys, [](auto const& a, auto const& b) {
                                                // Case-insensitive comparison for some databases
                                                return std::ranges::equal(a, b, [](char c1, char c2) {
                                                    return std::tolower(static_cast<unsigned char>(c1))
                                                           == std::tolower(static_cast<unsigned char>(c2));
                                                });
                                            });

                if (isPrimaryKeyIndex)
                    continue;

                result.push_back(IndexDefinition {
                    .name = indexName,
                    .columns = std::move(columns),
                    .isUnique = info.isUnique,
                });
            }

            return result;
        }
        catch (std::exception const&)
        {
            // Index retrieval is optional - if it fails, return empty list
            return {};
        }
    }

    std::vector<std::string> AllIdentityColumns(SqlStatement& stmt, FullyQualifiedTableName const& table)
    {
        if (stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
        {
            std::string schemaPart;
            if (!table.schema.empty())
            {
                schemaPart = std::format("AND s.name = '{}'", table.schema);
            }

            auto const sql = std::format(R"(SELECT c.name
                                            FROM sys.identity_columns c
                                            INNER JOIN sys.tables t ON c.object_id = t.object_id
                                            INNER JOIN sys.schemas s ON t.schema_id = s.schema_id
                                            WHERE t.name = '{}' {}
                                         )",
                                         table.table,
                                         schemaPart);

            try
            {
                // Verify we are not interrupting an existing fetch if stmt is reused incorrectly,
                // but AllIdentityColumns is called sequentially in ReadAllTables.
                // However, we should use a fresh statement to be safe if stmt is active?
                // ReadAllTables loop reuses stmt for PrimaryKeys etc. It should be fine.
                // Actually AllUniqueColumns etc use stmt.
                auto identityCursor = stmt.ExecuteDirect(sql);
                std::vector<std::string> identityCols;
                while (identityCursor.FetchRow())
                {
                    identityCols.push_back(identityCursor.GetColumn<std::string>(1));
                }
                return identityCols;
            }
            catch (std::exception const&)
            {
                return {};
            }
        }
        return {};
    }

    // ============================================================================================
    // Batched MSSQL schema introspection
    //
    // The legacy per-table path issues ~5 ODBC catalog round-trips per table. For databases with
    // hundreds of tables this dominates the schema-read time. The batched path below answers the
    // entire schema with a handful of whole-database sys.* queries and then drives the SAME
    // EventHandler virtuals in the SAME order as the legacy loop, so downstream Table construction
    // is identical by construction.
    // ============================================================================================

    /// SQL-escapes a literal by doubling single quotes (for safe inlining into a sys.* query).
    [[nodiscard]] std::string EscapeSqlLiteral(std::string_view value)
    {
        auto result = std::string {};
        result.reserve(value.size());
        for (auto const ch: value)
        {
            if (ch == '\'')
                result.push_back('\'');
            result.push_back(ch);
        }
        return result;
    }

    /// One column as read from sys.columns + sys.types (+ default constraint).
    struct MssqlColumnRow
    {
        std::string name;
        std::string sysTypeName;
        int maxLength = 0;
        int precision = 0;
        int scale = 0;
        bool isNullable = true;
        bool isIdentity = false;
        std::string defaultValue;
    };

    /// All batched schema data, keyed by sys.objects.object_id.
    struct MssqlSchemaData
    {
        std::map<int64_t, std::vector<MssqlColumnRow>> columnsByObject;
        std::map<int64_t, std::vector<std::string>> primaryKeysByObject;
        std::map<int64_t, std::vector<ForeignKeyConstraint>> foreignKeysFromByObject;
        std::map<int64_t, std::vector<ForeignKeyConstraint>> externalForeignKeysByObject;
        std::map<int64_t, std::vector<IndexDefinition>> indexesByObject;
        std::map<int64_t, std::set<std::string>> uniqueColumnsByObject;
        // (schema, table) -> object_id, to align the AllTables enumeration order with the maps.
        std::map<std::pair<std::string, std::string>, int64_t> objectIdByName;
    };

    /// Reads (schema, name) -> object_id for all user tables, scoped to @p schema if non-empty.
    void LoadMssqlTableObjectIds(SqlStatement& stmt, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter =
            !schema.empty() ? std::format("WHERE s.name = '{}'", EscapeSqlLiteral(schema)) : std::string {};
        auto const sql = std::format(R"(SELECT t.object_id, s.name, t.name
                                        FROM sys.tables t
                                        INNER JOIN sys.schemas s ON t.schema_id = s.schema_id
                                        {})",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);
        while (cursor.FetchRow())
        {
            auto const objectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto schemaName = cursor.GetNullableColumn<std::string>(2).value_or("");
            auto tableName = cursor.GetNullableColumn<std::string>(3).value_or("");
            data.objectIdByName[{ std::move(schemaName), std::move(tableName) }] = objectId;
        }
    }

    /// Query 1: columns + types + identity + default constraints, ordered by (object_id, column_id).
    void LoadMssqlColumns(SqlStatement& stmt, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter = !schema.empty()
                                      ? std::format("WHERE SCHEMA_NAME(t.schema_id) = '{}'", EscapeSqlLiteral(schema))
                                      : std::string {};
        auto const sql = std::format(R"(SELECT c.object_id,
                                               c.column_id,
                                               c.name,
                                               ty.name,
                                               CAST(c.max_length AS int),
                                               CAST(c.precision AS int),
                                               CAST(c.scale AS int),
                                               CAST(c.is_nullable AS int),
                                               CAST(c.is_identity AS int),
                                               dc.definition
                                        FROM sys.columns c
                                        INNER JOIN sys.tables t ON c.object_id = t.object_id
                                        INNER JOIN sys.types ty ON c.user_type_id = ty.user_type_id
                                        LEFT JOIN sys.default_constraints dc
                                               ON dc.parent_object_id = c.object_id
                                              AND dc.parent_column_id = c.column_id
                                        {}
                                        ORDER BY c.object_id, c.column_id)",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);
        while (cursor.FetchRow())
        {
            auto const objectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto row = MssqlColumnRow {
                .name = cursor.GetNullableColumn<std::string>(3).value_or(""),
                .sysTypeName = cursor.GetNullableColumn<std::string>(4).value_or(""),
                .maxLength = cursor.GetNullableColumn<int>(5).value_or(0),
                .precision = cursor.GetNullableColumn<int>(6).value_or(0),
                .scale = cursor.GetNullableColumn<int>(7).value_or(0),
                .isNullable = cursor.GetNullableColumn<int>(8).value_or(1) != 0,
                .isIdentity = cursor.GetNullableColumn<int>(9).value_or(0) != 0,
                .defaultValue = cursor.GetNullableColumn<std::string>(10).value_or(""),
            };
            data.columnsByObject[objectId].push_back(std::move(row));
        }
    }

    /// Query 2: primary-key columns, ordered by (object_id, key_ordinal).
    void LoadMssqlPrimaryKeys(SqlStatement& stmt, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter =
            !schema.empty() ? std::format("AND SCHEMA_NAME(t.schema_id) = '{}'", EscapeSqlLiteral(schema)) : std::string {};
        auto const sql = std::format(R"(SELECT i.object_id, c.name, ic.key_ordinal
                                        FROM sys.indexes i
                                        INNER JOIN sys.tables t ON i.object_id = t.object_id
                                        INNER JOIN sys.index_columns ic
                                               ON ic.object_id = i.object_id AND ic.index_id = i.index_id
                                        INNER JOIN sys.columns c
                                               ON c.object_id = ic.object_id AND c.column_id = ic.column_id
                                        WHERE i.is_primary_key = 1 {}
                                        ORDER BY i.object_id, ic.key_ordinal)",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);
        while (cursor.FetchRow())
        {
            auto const objectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto name = cursor.GetNullableColumn<std::string>(2).value_or("");
            if (!name.empty())
                data.primaryKeysByObject[objectId].push_back(std::move(name));
        }
    }

    /// Query 3: foreign keys. Each constraint yields an OnForeignKey entry for the parent (child)
    /// table and an OnExternalForeignKey entry for the referenced (primary) table.
    void LoadMssqlForeignKeys(SqlStatement& stmt, std::string_view database, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter = !schema.empty()
                                      ? std::format("WHERE SCHEMA_NAME(pt.schema_id) = '{}'", EscapeSqlLiteral(schema))
                                      : std::string {};
        // One row per (constraint, column position). Composite keys span multiple rows; group by
        // fk.object_id and order by constraint_column_id to reconstruct column order.
        auto const sql = std::format(R"(SELECT fk.object_id,
                                               pt.object_id,
                                               SCHEMA_NAME(pt.schema_id),
                                               pt.name,
                                               pc.name,
                                               rt.object_id,
                                               SCHEMA_NAME(rt.schema_id),
                                               rt.name,
                                               rc.name,
                                               fkc.constraint_column_id
                                        FROM sys.foreign_keys fk
                                        INNER JOIN sys.foreign_key_columns fkc ON fkc.constraint_object_id = fk.object_id
                                        INNER JOIN sys.tables pt ON fk.parent_object_id = pt.object_id
                                        INNER JOIN sys.tables rt ON fk.referenced_object_id = rt.object_id
                                        INNER JOIN sys.columns pc
                                               ON pc.object_id = fkc.parent_object_id
                                              AND pc.column_id = fkc.parent_column_id
                                        INNER JOIN sys.columns rc
                                               ON rc.object_id = fkc.referenced_object_id
                                              AND rc.column_id = fkc.referenced_column_id
                                        {}
                                        ORDER BY fk.object_id, fkc.constraint_column_id)",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);

        struct FkAccumulator
        {
            int64_t parentObjectId = 0;
            int64_t referencedObjectId = 0;
            FullyQualifiedTableName parentTable;
            FullyQualifiedTableName referencedTable;
            std::vector<std::string> parentColumns;
            std::vector<std::string> referencedColumns;
        };
        // Keyed by the FK constraint's own object_id (unique per constraint). Insertion order is
        // tracked so emission follows fk.object_id / constraint_column_id order, matching the
        // legacy driver-reported ordering as closely as the catalog allows.
        auto byConstraint = std::map<int64_t, FkAccumulator> {};
        auto constraintOrder = std::vector<int64_t> {};
        while (cursor.FetchRow())
        {
            // Read EVERY column once, in strict ascending order. The MS SQL Server ODBC driver
            // retrieves unbound columns via SQLGetData, which forbids reading a lower column index
            // after a higher one — reading col 5 (parent column) after cols 6-8 (referenced table)
            // raised 07009 "Invalid Descriptor Index". Pull all fields into locals first, then group.
            auto const fkObjectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto const parentObjectId = static_cast<int64_t>(cursor.GetNullableColumn<int>(2).value_or(0));
            auto parentSchema = cursor.GetNullableColumn<std::string>(3).value_or("");
            auto parentTableName = cursor.GetNullableColumn<std::string>(4).value_or("");
            auto parentColumn = cursor.GetNullableColumn<std::string>(5).value_or("");
            auto const referencedObjectId = static_cast<int64_t>(cursor.GetNullableColumn<int>(6).value_or(0));
            auto referencedSchema = cursor.GetNullableColumn<std::string>(7).value_or("");
            auto referencedTableName = cursor.GetNullableColumn<std::string>(8).value_or("");
            auto referencedColumn = cursor.GetNullableColumn<std::string>(9).value_or("");

            if (!byConstraint.contains(fkObjectId))
            {
                constraintOrder.push_back(fkObjectId);
                auto& acc = byConstraint[fkObjectId];
                acc.parentObjectId = parentObjectId;
                acc.parentTable = FullyQualifiedTableName {
                    .catalog = std::string(database),
                    .schema = std::move(parentSchema),
                    .table = std::move(parentTableName),
                };
                acc.referencedObjectId = referencedObjectId;
                acc.referencedTable = FullyQualifiedTableName {
                    .catalog = std::string(database),
                    .schema = std::move(referencedSchema),
                    .table = std::move(referencedTableName),
                };
            }
            auto& acc = byConstraint[fkObjectId];
            acc.parentColumns.push_back(std::move(parentColumn));
            acc.referencedColumns.push_back(std::move(referencedColumn));
        }

        for (auto const fkObjectId: constraintOrder)
        {
            auto const& acc = byConstraint.at(fkObjectId);
            auto constraint = ForeignKeyConstraint {
                .foreignKey = { .table = acc.parentTable, .columns = acc.parentColumns },
                .primaryKey = { .table = acc.referencedTable, .columns = acc.referencedColumns },
            };
            // OnForeignKey is emitted on the parent (child) table; OnExternalForeignKey on the
            // referenced (primary) table.
            data.foreignKeysFromByObject[acc.parentObjectId].push_back(constraint);
            data.externalForeignKeysByObject[acc.referencedObjectId].push_back(std::move(constraint));
        }

        // NOTE: foreign-key array order is canonicalized downstream (see CanonicalizeForeignKeys,
        // applied in OnTableEnd of both the production and comparison event handlers), so we do NOT
        // try to reproduce SQLForeignKeys' collation-dependent row order here. FK creation is
        // order-independent on restore; a single canonical order makes backup output deterministic
        // regardless of driver/collation and avoids matching SQL Server's collation in C++.
    }

    /// Query 4: non-PK indexes. Mirrors AllIndexesMssql but whole-DB: the per-table `WHERE t.name`
    /// filter is removed and t.object_id added so rows can be grouped by (object_id, index_name).
    void LoadMssqlIndexes(SqlStatement& stmt, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter =
            !schema.empty() ? std::format("AND SCHEMA_NAME(t.schema_id) = '{}'", EscapeSqlLiteral(schema)) : std::string {};
        auto const sql = std::format(R"(SELECT t.object_id,
                                               i.name AS index_name,
                                               CAST(i.is_unique AS int) AS is_unique,
                                               c.name AS column_name,
                                               CAST(ic.key_ordinal AS int) AS key_ordinal
                                        FROM sys.indexes i
                                        INNER JOIN sys.tables t ON i.object_id = t.object_id
                                        INNER JOIN sys.index_columns ic
                                               ON ic.object_id = i.object_id AND ic.index_id = i.index_id
                                        INNER JOIN sys.columns c
                                               ON c.object_id = ic.object_id AND c.column_id = ic.column_id
                                        WHERE i.is_primary_key = 0
                                          AND i.is_unique_constraint = 0
                                          AND i.type > 0
                                          AND ic.is_included_column = 0
                                          {}
                                        ORDER BY t.object_id, i.name, ic.key_ordinal)",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);

        struct IndexInfo
        {
            bool isUnique = false;
            std::vector<std::pair<int, std::string>> columns; // (key_ordinal, column_name)
        };
        // Index names are NOT unique across tables, so key by (object_id, index_name).
        auto byKey = std::map<std::pair<int64_t, std::string>, IndexInfo> {};
        auto order = std::vector<std::pair<int64_t, std::string>> {};
        while (cursor.FetchRow())
        {
            auto const objectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto name = cursor.GetNullableColumn<std::string>(2).value_or("");
            auto const isUnique = cursor.GetNullableColumn<int>(3).value_or(0) != 0;
            auto column = cursor.GetNullableColumn<std::string>(4).value_or("");
            auto const ordinal = cursor.GetNullableColumn<int>(5).value_or(0);
            auto const key = std::pair<int64_t, std::string> { objectId, name };
            if (!byKey.contains(key))
                order.push_back(key);
            auto& info = byKey[key];
            info.isUnique = isUnique;
            info.columns.emplace_back(ordinal, std::move(column));
        }

        for (auto const& key: order)
        {
            auto info = std::move(byKey.at(key));
            std::ranges::sort(info.columns, [](auto const& a, auto const& b) { return a.first < b.first; });
            auto cols = std::vector<std::string> {};
            cols.reserve(info.columns.size());
            for (auto& [_, col]: info.columns)
                cols.push_back(std::move(col));

            // Skip indexes whose column set exactly matches the table's primary key (case-insensitive,
            // same count) — those are implicit PK indexes the cross-engine reader treats separately.
            auto const pkIt = data.primaryKeysByObject.find(key.first);
            auto const& primaryKeys = pkIt != data.primaryKeysByObject.end() ? pkIt->second : std::vector<std::string> {};
            bool const matchesPk =
                cols.size() == primaryKeys.size() && std::ranges::equal(cols, primaryKeys, [](auto const& a, auto const& b) {
                    return std::ranges::equal(a, b, [](char c1, char c2) {
                        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
                    });
                });
            if (matchesPk)
                continue;

            data.indexesByObject[key.first].push_back(IndexDefinition {
                .name = key.second,
                .columns = std::move(cols),
                .isUnique = info.isUnique,
            });
        }
    }

    /// Query 5: single-column unique indexes -> per-column isUnique membership. Matches the legacy
    /// AllUniqueColumns rule (only single-column unique indexes contribute).
    void LoadMssqlUniqueColumns(SqlStatement& stmt, std::string_view schema, MssqlSchemaData& data)
    {
        auto const schemaFilter =
            !schema.empty() ? std::format("AND SCHEMA_NAME(t.schema_id) = '{}'", EscapeSqlLiteral(schema)) : std::string {};
        auto const sql = std::format(R"(SELECT t.object_id, i.index_id, c.name
                                        FROM sys.indexes i
                                        INNER JOIN sys.tables t ON i.object_id = t.object_id
                                        INNER JOIN sys.index_columns ic
                                               ON ic.object_id = i.object_id AND ic.index_id = i.index_id
                                        INNER JOIN sys.columns c
                                               ON c.object_id = ic.object_id AND c.column_id = ic.column_id
                                        WHERE i.is_unique = 1 {}
                                        ORDER BY t.object_id, i.index_id)",
                                     schemaFilter);
        auto cursor = stmt.ExecuteDirect(sql);

        // Collect columns per (object_id, index_id); keep only single-column unique indexes.
        struct UniqueIndexAccumulator
        {
            int64_t objectId = 0;
            std::vector<std::string> columns;
        };
        auto byIndex = std::map<std::pair<int64_t, int>, UniqueIndexAccumulator> {};
        while (cursor.FetchRow())
        {
            auto const objectId = static_cast<int64_t>(cursor.GetColumn<int>(1));
            auto const indexId = cursor.GetNullableColumn<int>(2).value_or(0);
            auto column = cursor.GetNullableColumn<std::string>(3).value_or("");
            auto& acc = byIndex[{ objectId, indexId }];
            acc.objectId = objectId;
            if (!column.empty())
                acc.columns.push_back(std::move(column));
        }
        for (auto const& [_, acc]: byIndex)
        {
            if (acc.columns.size() == 1)
                data.uniqueColumnsByObject[acc.objectId].insert(acc.columns.front());
        }
    }

} // namespace

namespace detail
{
    namespace
    {
        // LOB sentinel sizes: the MS SQL Server ODBC driver reports these COLUMN_SIZE values via
        // SQLColumns for max/LOB types, and the legacy reader stores them verbatim. To stay
        // byte-identical we reproduce the same magic sizes for text/varchar(max)/image and for
        // ntext/nvarchar(max). (sys.columns.max_length is just -1 for all of these, so the type
        // name plus the -1 sentinel is what selects the LOB form.)
        constexpr std::size_t LobSizeNonUnicode = 2147483647; // text, varchar(max), image, varbinary(max)
        constexpr std::size_t LobSizeUnicode = 1073741823;    // ntext, nvarchar(max)

        // sys.columns.max_length is in BYTES. max_length == -1 is the MAX/LOB sentinel, which the
        // legacy path represents as size == 0.
        std::size_t MssqlByteSize(int maxLength) noexcept
        {
            return maxLength < 0 ? 0 : static_cast<std::size_t>(maxLength);
        }
        // For the N-types (nchar/nvarchar/ntext) the logical character count is half the byte size.
        std::size_t MssqlCharSize(int maxLength) noexcept
        {
            return maxLength < 0 ? 0 : static_cast<std::size_t>(maxLength) / 2;
        }

        // Integral / boolean. Returns std::nullopt if sysTypeName is not in this category.
        std::optional<SqlColumnTypeDefinition> MssqlIntegralType(std::string_view sysTypeName)
        {
            using namespace SqlColumnTypeDefinitions;
            if (sysTypeName == "int")
                return Integer {};
            if (sysTypeName == "bigint")
                return Bigint {};
            if (sysTypeName == "smallint")
                return Smallint {};
            if (sysTypeName == "tinyint")
                return Tinyint {};
            if (sysTypeName == "bit")
                return Bool {};
            return std::nullopt;
        }

        // Exact / approximate numeric.
        std::optional<SqlColumnTypeDefinition> MssqlNumericType(std::string_view sysTypeName,
                                                                detail::MssqlColumnMetrics metrics)
        {
            using namespace SqlColumnTypeDefinitions;
            // decimal/numeric carry their (precision, scale) from sys.columns; money/smallmoney
            // are fixed-scale decimals where sys.columns reports precision 19/10 and scale 4.
            if (sysTypeName == "decimal" || sysTypeName == "numeric" || sysTypeName == "money"
                || sysTypeName == "smallmoney")
                return Decimal { .precision = static_cast<std::size_t>(metrics.precision < 0 ? 0 : metrics.precision),
                                 .scale = static_cast<std::size_t>(metrics.scale < 0 ? 0 : metrics.scale) };
            // float/real both collapse to Real{53} to match the legacy MSSQL float fixup
            // (SqlSchema.cpp), which hard-codes precision 53 regardless of the declared width.
            if (sysTypeName == "float" || sysTypeName == "real")
                return Real { .precision = 53 };
            return std::nullopt;
        }

        // Character (Unicode and non-Unicode).
        std::optional<SqlColumnTypeDefinition> MssqlCharacterType(std::string_view sysTypeName, int maxLength)
        {
            using namespace SqlColumnTypeDefinitions;
            bool const isMax = maxLength < 0;
            // Non-Unicode (size in bytes == characters).
            if (sysTypeName == "char")
                return Char { .size = MssqlByteSize(maxLength) };
            // varchar(max) (max_length == -1) is a LOB: the driver reports COLUMN_SIZE 2147483647.
            if (sysTypeName == "varchar")
                return Varchar { .size = isMax ? LobSizeNonUnicode : MssqlByteSize(maxLength) };
            // text is always a LOB; legacy maps SQL_LONGVARCHAR -> Varchar{2147483647}.
            if (sysTypeName == "text")
                return Varchar { .size = LobSizeNonUnicode };
            // Unicode (size in characters == bytes / 2).
            if (sysTypeName == "nchar")
                return NChar { .size = MssqlCharSize(maxLength) };
            // nvarchar(max) is a LOB: the driver reports COLUMN_SIZE 1073741823.
            if (sysTypeName == "nvarchar")
                return NVarchar { .size = isMax ? LobSizeUnicode : MssqlCharSize(maxLength) };
            if (sysTypeName == "ntext")
                return NVarchar { .size = LobSizeUnicode };
            return std::nullopt;
        }

        // Binary.
        std::optional<SqlColumnTypeDefinition> MssqlBinaryType(std::string_view sysTypeName, int maxLength)
        {
            using namespace SqlColumnTypeDefinitions;
            if (sysTypeName == "binary")
                return Binary { .size = MssqlByteSize(maxLength) };
            // varbinary(max) is a LOB: the driver reports COLUMN_SIZE 2147483647.
            if (sysTypeName == "varbinary")
                return VarBinary { .size = maxLength < 0 ? LobSizeNonUnicode : MssqlByteSize(maxLength) };
            if (sysTypeName == "image")
                return VarBinary { .size = LobSizeNonUnicode };
            return std::nullopt;
        }

        // Identifiers / temporal.
        std::optional<SqlColumnTypeDefinition> MssqlIdentifierOrTemporalType(std::string_view sysTypeName, int maxLength)
        {
            using namespace SqlColumnTypeDefinitions;
            if (sysTypeName == "uniqueidentifier")
                return Guid {};
            if (sysTypeName == "date")
                return Date {};
            if (sysTypeName == "time")
                return Time {};
            if (sysTypeName == "datetime" || sysTypeName == "datetime2" || sysTypeName == "smalldatetime"
                || sysTypeName == "datetimeoffset")
                return DateTime {};
            // rowversion is the modern alias for timestamp; both are 8-byte binary stamps.
            if (sysTypeName == "timestamp" || sysTypeName == "rowversion")
                return VarBinary { .size = MssqlByteSize(maxLength) };
            return std::nullopt;
        }
    } // namespace

    SqlColumnTypeDefinition MakeColumnTypeFromMssqlSysType(std::string_view sysTypeName, MssqlColumnMetrics metrics)
    {
        using namespace SqlColumnTypeDefinitions;

        if (auto type = MssqlIntegralType(sysTypeName))
            return *type;
        if (auto type = MssqlNumericType(sysTypeName, metrics))
            return *type;
        if (auto type = MssqlCharacterType(sysTypeName, metrics.maxLength))
            return *type;
        if (auto type = MssqlBinaryType(sysTypeName, metrics.maxLength))
            return *type;
        if (auto type = MssqlIdentifierOrTemporalType(sysTypeName, metrics.maxLength))
            return *type;

        // Unknown / unmapped: fall back to a sized Varchar so callers still get a usable type.
        return Varchar { .size = MssqlByteSize(metrics.maxLength) };
    }

    namespace
    {
        // Builds each Column for one table from its batched sys.columns rows and emits it through the
        // handler, applying the same primary-key / foreign-key / unique flags the legacy per-table
        // path sets. Extracted from ReadAllTablesBatchedMssql to keep that loop's complexity bounded.
        void EmitMssqlColumns(std::vector<MssqlColumnRow> const& columnRows,
                              std::vector<std::string> const& primaryKeys,
                              std::vector<ForeignKeyConstraint> const& foreignKeys,
                              std::set<std::string> const& uniqueColumns,
                              EventHandler& eventHandler)
        {
            auto const matchesColumn = [](Column const& column) {
                return [&column](ForeignKeyConstraint const& fk) {
                    return std::ranges::contains(fk.foreignKey.columns, column.name);
                };
            };

            for (auto const& columnRow: columnRows)
            {
                auto column = Column {};
                column.name = columnRow.name;
                column.dialectDependantTypeString = columnRow.sysTypeName;
                column.type = MakeColumnTypeFromMssqlSysType(
                    columnRow.sysTypeName,
                    { .maxLength = columnRow.maxLength, .precision = columnRow.precision, .scale = columnRow.scale });
                column.isNullable = columnRow.isNullable;
                column.defaultValue = columnRow.defaultValue;
                column.isPrimaryKey = std::ranges::contains(primaryKeys, column.name);
                column.isUnique = uniqueColumns.contains(column.name);
                column.isAutoIncrement = columnRow.isIdentity;
                column.isForeignKey = std::ranges::any_of(foreignKeys, matchesColumn(column));
                if (auto const p = std::ranges::find_if(foreignKeys, matchesColumn(column)); p != foreignKeys.end())
                    column.foreignKeyConstraint = *p;
                eventHandler.OnColumn(column);
            }
        }

        // Emits one table's primary keys, foreign keys, indexes, and columns through the handler,
        // looking each set up by object id in the pre-loaded MssqlSchemaData. Extracted from
        // ReadAllTablesBatchedMssql so that loop stays under the cognitive-complexity threshold.
        // Returns false if the handler vetoed the table via OnTable (caller skips it).
        bool EmitMssqlTable(MssqlSchemaData const& data,
                            TableWithSchema const& tableEntry,
                            std::string_view schema,
                            EventHandler& eventHandler)
        {
            auto const& tableName = tableEntry.name;
            auto const tableSchema = tableEntry.schema.empty() ? std::string(schema) : tableEntry.schema;

            // Harmless on MSSQL, kept to mirror the legacy loop exactly.
            if (tableName == "sqlite_sequence")
                return false;
            if (!eventHandler.OnTable(tableSchema, tableName))
                return false;

            auto const objectIdIt = data.objectIdByName.find({ tableSchema, tableName });
            auto const objectId = objectIdIt != data.objectIdByName.end() ? objectIdIt->second : int64_t { 0 };

            // Looks up @p byObject[objectId], returning a reference to a shared empty fallback when the
            // table has no entry (so callers always get a valid const reference).
            auto const lookupOr = [objectId](auto const& byObject, auto const& fallback) -> auto const& {
                auto const it = byObject.find(objectId);
                return it != byObject.end() ? it->second : fallback;
            };
            static auto const emptyKeys = std::vector<std::string> {};
            static auto const emptyForeignKeys = std::vector<ForeignKeyConstraint> {};
            static auto const emptyIndexes = std::vector<IndexDefinition> {};
            static auto const emptyUnique = std::set<std::string> {};

            auto const& primaryKeys = lookupOr(data.primaryKeysByObject, emptyKeys);
            eventHandler.OnPrimaryKeys(tableName, primaryKeys);

            auto const& foreignKeys = lookupOr(data.foreignKeysFromByObject, emptyForeignKeys);
            for (auto const& foreignKey: foreignKeys)
                eventHandler.OnForeignKey(foreignKey);
            for (auto const& foreignKey: lookupOr(data.externalForeignKeysByObject, emptyForeignKeys))
                eventHandler.OnExternalForeignKey(foreignKey);

            eventHandler.OnIndexes(lookupOr(data.indexesByObject, emptyIndexes));

            auto const& uniqueColumns = lookupOr(data.uniqueColumnsByObject, emptyUnique);
            auto const columnsIt = data.columnsByObject.find(objectId);
            if (columnsIt != data.columnsByObject.end())
                EmitMssqlColumns(columnsIt->second, primaryKeys, foreignKeys, uniqueColumns, eventHandler);

            eventHandler.OnTableEnd();
            return true;
        }
    } // namespace

    void ReadAllTablesBatchedMssql(SqlStatement& stmt,
                                   std::string_view database,
                                   std::string_view schema,
                                   EventHandler& eventHandler)
    {
        ZoneScopedN("SqlSchema::ReadAllTablesBatchedMssql");
        if (!schema.empty())
            ZoneTextObject(schema);

        // Enumerate tables in exactly the same order as the legacy path (SQLTables).
        auto const tablesWithSchema = AllTables(stmt, database, schema);

        auto tableNames = std::vector<std::string> {};
        tableNames.reserve(tablesWithSchema.size());
        for (auto const& t: tablesWithSchema)
            tableNames.emplace_back(t.name);
        eventHandler.OnTables(tableNames);

        // Run the whole-database catalog queries once.
        auto data = MssqlSchemaData {};
        LoadMssqlTableObjectIds(stmt, schema, data);
        LoadMssqlColumns(stmt, schema, data);
        LoadMssqlPrimaryKeys(stmt, schema, data);
        LoadMssqlForeignKeys(stmt, database, schema, data);
        LoadMssqlIndexes(stmt, schema, data);
        LoadMssqlUniqueColumns(stmt, schema, data);

        for (auto const& tableEntry: tablesWithSchema)
            EmitMssqlTable(data, tableEntry, schema, eventHandler);
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void ReadAllTablesLegacy(SqlStatement& stmt,
                             std::string_view database,
                             std::string_view schema,
                             EventHandler& eventHandler)
    {
        ZoneScopedN("SqlSchema::ReadAllTablesLegacy");
        if (!schema.empty())
            ZoneTextObject(schema);

        auto const tablesWithSchema = AllTables(stmt, database, schema);

        // Extract just table names for the EventHandler interface
        std::vector<std::string> tableNames;
        tableNames.reserve(tablesWithSchema.size());
        for (auto const& t: tablesWithSchema)
            tableNames.emplace_back(t.name);

        eventHandler.OnTables(tableNames);

        for (auto const& tableEntry: tablesWithSchema)
        {
            auto const& tableName = tableEntry.name;
            // Use the discovered schema, or fall back to the requested schema
            auto const& tableSchema = tableEntry.schema.empty() ? std::string(schema) : tableEntry.schema;

            if (tableName == "sqlite_sequence")
                continue;

            if (!eventHandler.OnTable(tableSchema, tableName))
                continue;

            auto const fullyQualifiedTableName = FullyQualifiedTableName {
                .catalog = std::string(database),
                .schema = tableSchema,
                .table = tableName,
            };

            auto const primaryKeys = AllPrimaryKeys(stmt, fullyQualifiedTableName);
            eventHandler.OnPrimaryKeys(tableName, primaryKeys);

            auto const uniqueColumns = AllUniqueColumns(stmt, fullyQualifiedTableName);
            auto const identityColumns = AllIdentityColumns(stmt, fullyQualifiedTableName);

            std::vector<ForeignKeyConstraint> const foreignKeys = AllForeignKeysFrom(stmt, fullyQualifiedTableName);
            std::vector<ForeignKeyConstraint> const incomingForeignKeys = AllForeignKeysTo(stmt, fullyQualifiedTableName);

            for (auto const& foreignKey: foreignKeys)
                eventHandler.OnForeignKey(foreignKey);

            for (auto const& foreignKey: incomingForeignKeys)
                eventHandler.OnExternalForeignKey(foreignKey);

            auto const indexes = AllIndexes(stmt, fullyQualifiedTableName, primaryKeys);
            eventHandler.OnIndexes(indexes);

            auto columnStmt = SqlStatement { stmt.Connection() };
            auto wDatabase = OdbcWideArg { database };
            auto wTableSchema = OdbcWideArg { tableSchema };
            auto wTableName = OdbcWideArg { tableName };
            auto const sqlResult = SQLColumnsW(columnStmt.NativeHandle(),
                                               wDatabase.data(),
                                               wDatabase.length(),
                                               wTableSchema.data(),
                                               wTableSchema.length(),
                                               wTableName.data(),
                                               wTableName.length(),
                                               nullptr /* column name */,
                                               0 /* column name length */);
            if (!SQL_SUCCEEDED(sqlResult))
                throw std::runtime_error(std::format("SQLColumns failed: {}", columnStmt.LastError()));

            // ODBC SQLColumns() should return 18 columns per the spec.
            // However, some drivers may return fewer columns. Track the actual column count
            // to avoid accessing non-existent columns which causes ODBC errors.
            auto columnCursor = SqlResultCursor(columnStmt);
            auto const numColumns = columnCursor.NumColumnsAffected();

            Column column;

            while (columnCursor.FetchRow())
            {
                // std::cerr << "DEBUG: FetchRow success for " << tableName << "\n";
                int type = 0;
                try
                {
                    column.name = columnCursor.GetNullableColumn<std::string>(4).value_or("");
                    type = columnCursor.GetColumn<int>(5); // DATA_TYPE
                    column.dialectDependantTypeString = columnCursor.GetNullableColumn<std::string>(6).value_or("");
                    // COLUMN_SIZE (column 7) can be negative for some drivers (e.g., PostgreSQL returns -4 for BYTEA)
                    // to indicate "unknown" size. Treat negative values as 0.
                    auto const rawSize = columnCursor.GetColumn<int>(7);
                    column.size = rawSize > 0 ? static_cast<size_t>(rawSize) : 0;

                    // 8 - bufferLength
                    column.decimalDigits = numColumns >= 9 ? columnCursor.GetNullableColumn<uint16_t>(9).value_or(0) : 0;
                }
                catch (std::exception const&)
                {
                    // std::cerr << "DEBUG: Exception reading column meta for table " << tableName << ": " << e.what() <<
                    // "\n";
                    continue;
                }

                // 10 - NUM_PREC_RADIX
                // 11 - NULLABLE
                if (numColumns >= 11)
                {
                    try
                    {
                        column.isNullable = columnCursor.GetColumn<bool>(11);
                    }
                    catch (std::exception&)
                    {
                        column.isNullable = true;
                    }
                }
                else
                {
                    column.isNullable = true;
                }

                // 12 - REMARKS
                // 13 - COLUMN_DEF
                if (numColumns >= 13)
                {
                    try
                    {
                        column.defaultValue = columnCursor.GetNullableColumn<std::string>(13).value_or("");
                    }
                    catch (std::exception&)
                    {
                        column.defaultValue = {};
                    }
                }
                else
                {
                    column.defaultValue = {};
                }

                if (auto cType = MakeColumnTypeFromNative(type, column.size, column.decimalDigits); cType.has_value())
                    column.type = *cType;
                else
                {
                    SqlLogger::GetLogger().OnError(SqlError::UNSUPPORTED_TYPE);
                    throw std::runtime_error(std::format("Unsupported data type: {}", type));
                }

                try
                {
                    // some special handling of weird types
                    if (column.dialectDependantTypeString == "money")
                    {
                        // 0.123 -> decimalDigits = 3 size = 4
                        // 100.123 -> decimalDigits = 3  size = 6
                        column.size = column.decimalDigits;
                        column.decimalDigits = SQL_MAX_NUMERIC_LEN;
                    }
                    else if (column.dialectDependantTypeString == "float" || column.dialectDependantTypeString == "FLOAT"
                             || column.dialectDependantTypeString == "real" || column.dialectDependantTypeString == "REAL")
                    {
                        column.type = SqlColumnTypeDefinitions::Real { .precision = 53 };
                        // column.size = 15; // Try letting it be default (from SQLColumns or 0)
                    }
                    // PostgreSQL ODBC driver reports BOOLEAN as VARCHAR - handle it specially
                    else if (column.dialectDependantTypeString == "bool")
                    {
                        column.type = SqlColumnTypeDefinitions::Bool {};
                    }
                    // SQLite is dynamically typed; the ODBC driver reports columns declared as
                    // `DECIMAL(p, s)` as SQL_VARCHAR (so they fall into the Varchar branch
                    // above). Recover the canonical Decimal by parsing the dialect type string
                    // when it carries `(p, s)`. Drivers that reported the column as SQL_DECIMAL/
                    // SQL_NUMERIC already produced a Decimal — leave those untouched, and don't
                    // collapse a parenless `numeric` to `Decimal(0,0)`.
                    else if ((column.dialectDependantTypeString.starts_with("DECIMAL")
                              || column.dialectDependantTypeString.starts_with("decimal")
                              || column.dialectDependantTypeString.starts_with("NUMERIC")
                              || column.dialectDependantTypeString.starts_with("numeric"))
                             && column.dialectDependantTypeString.contains('('))
                    {
                        auto precision = std::size_t {};
                        auto scale = std::size_t {};
                        auto const open = column.dialectDependantTypeString.find('(');
                        auto const close = column.dialectDependantTypeString.find(')', open);
                        if (close != std::string::npos)
                        {
                            auto const inner =
                                std::string_view { column.dialectDependantTypeString }.substr(open + 1, close - open - 1);
                            auto parseSize = [](std::string_view sv) -> std::size_t {
                                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                                    sv.remove_prefix(1);
                                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
                                    sv.remove_suffix(1);
                                auto value = std::size_t {};
                                auto const result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
                                return result.ec == std::errc {} ? value : 0;
                            };
                            auto const comma = inner.find(',');
                            if (comma != std::string_view::npos)
                            {
                                precision = parseSize(inner.substr(0, comma));
                                scale = parseSize(inner.substr(comma + 1));
                            }
                            else
                            {
                                precision = parseSize(inner);
                            }
                        }
                        column.type = SqlColumnTypeDefinitions::Decimal { .precision = precision, .scale = scale };
                    }
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) - intentionally ignoring column type detection errors
                catch (std::exception&)
                {
                }

                // accumulated properties
                column.isPrimaryKey = std::ranges::contains(primaryKeys, column.name);
                column.isUnique = std::ranges::contains(uniqueColumns, column.name);
                column.isAutoIncrement = std::ranges::contains(identityColumns, column.name);
                // column.isForeignKey = ...;
                column.isForeignKey = std::ranges::any_of(foreignKeys, [&column](ForeignKeyConstraint const& fk) {
                    return std::ranges::contains(fk.foreignKey.columns, column.name);
                });
                if (auto const p = std::ranges::find_if(foreignKeys,
                                                        [&column](ForeignKeyConstraint const& fk) {
                                                            return std::ranges::contains(fk.foreignKey.columns, column.name);
                                                        });
                    p != foreignKeys.end())
                {
                    column.foreignKeyConstraint = *p;
                }

                eventHandler.OnColumn(column);
            }

            eventHandler.OnTableEnd();
        }
    }

    void CanonicalizeForeignKeys(std::vector<ForeignKeyConstraint>& foreignKeys)
    {
        // Order by (foreignKey {table, columns}, primaryKey {table, columns}) — a total,
        // driver-independent ordering. The comparator is passed explicitly because
        // ForeignKeyConstraint provides only operator< (not the full std::totally_ordered set that
        // std::ranges::sort's default projection requires). Each constraint within a table is unique
        // on this key, so the order is deterministic.
        std::ranges::sort(foreignKeys, [](ForeignKeyConstraint const& a, ForeignKeyConstraint const& b) { return a < b; });
    }

} // namespace detail

void ReadAllTables(SqlStatement& stmt, std::string_view database, std::string_view schema, EventHandler& eventHandler)
{
    ZoneScopedN("SqlSchema::ReadAllTables(EventHandler)");
    if (!schema.empty())
        ZoneTextObject(schema);

    // For dialects with a batched whole-database introspection fast path (MS SQL Server),
    // delegate to it. It drives the SAME EventHandler virtuals in the SAME order as the legacy
    // per-table loop, so downstream Table construction is identical. All other dialects
    // (SQLite, PostgreSQL) keep the per-table catalog path unchanged.
    if (stmt.Connection().QueryFormatter().SupportsBatchedSchemaIntrospection())
        detail::ReadAllTablesBatchedMssql(stmt, database, schema, eventHandler);
    else
        detail::ReadAllTablesLegacy(stmt, database, schema, eventHandler);
}

TableList ReadAllTables(SqlStatement& stmt,
                        std::string_view database,
                        std::string_view schema,
                        ReadAllTablesCallback callback,
                        TableReadyCallback tableReadyCallback,
                        TableFilterPredicate tableFilter)
{
    ZoneScopedN("SqlSchema::ReadAllTables");
    if (!schema.empty())
        ZoneTextObject(schema);

    auto ToLowerCase = [](std::string_view str) -> std::string {
        std::string result(str);
        std::ranges::transform(result, result.begin(), [](char c) {
            // Cast to unsigned char before std::tolower — passing a signed `char` with
            // the high bit set is undefined behaviour per the standard.
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        return result;
    };

    TableList tables;
    std::map<std::string, std::string> tableNameCaseMap;

    struct StreamingEventHandler: public SqlSchema::EventHandler
    {
        TableList& tables;
        std::map<std::string, std::string>& tableNameCaseMap;
        ReadAllTablesCallback callback;
        TableReadyCallback tableReadyCallback;
        TableFilterPredicate tableFilter;
        std::string_view schema;
        size_t currentlyProcessedTablesCount = 0;
        size_t totalTableCount = 0;

        StreamingEventHandler(TableList& tables,
                              std::map<std::string, std::string>& caseMap,
                              ReadAllTablesCallback progressCallback,
                              TableReadyCallback readyCallback,
                              TableFilterPredicate filterPredicate,
                              std::string_view schemaName):
            tables { tables },
            tableNameCaseMap { caseMap },
            callback { std::move(progressCallback) },
            tableReadyCallback { std::move(readyCallback) },
            tableFilter { std::move(filterPredicate) },
            schema { schemaName }
        {
        }

        void OnTables(std::vector<std::string> const& tableNames) override
        {
            totalTableCount = tableNames.size();

            // Build case map for ALL tables upfront for FK fixup.
            // This is necessary because filtered tables may still be referenced by foreign keys,
            // and the FK fixup code needs to resolve their original casing.
            for (auto const& name: tableNames)
                tableNameCaseMap[ToLowerCaseInline(name)] = name;
        }

      private:
        static std::string ToLowerCaseInline(std::string_view str)
        {
            std::string result(str);
            std::ranges::transform(result, result.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            return result;
        }

      public:
        bool OnTable(std::string_view tableSchema, std::string_view table) override
        {
            ++currentlyProcessedTablesCount;
            if (callback)
                callback(table, currentlyProcessedTablesCount, totalTableCount);

            // Apply filter predicate - skip reading detailed schema if table doesn't match
            if (tableFilter && !tableFilter(tableSchema, table))
                return false;

            tables.emplace_back(Table { .schema = std::string(tableSchema), .name = std::string(table) });
            return true;
        }

        void OnTableEnd() override
        {
            if (tables.empty())
                return;

            auto& completedTable = tables.back();

            // Normalize FK array order so backup metadata is deterministic regardless of which
            // reader (legacy vs batched) and which driver/collation produced it. FK creation is
            // order-independent on restore.
            detail::CanonicalizeForeignKeys(completedTable.foreignKeys);
            detail::CanonicalizeForeignKeys(completedTable.externalForeignKeys);

            // If a table-ready callback is provided, invoke it with the completed table
            if (tableReadyCallback)
            {
                // Make a copy since we need to keep the table in the list for later processing
                Table tableCopy = completedTable;
                tableReadyCallback(std::move(tableCopy));
            }
        }

        void OnColumn(SqlSchema::Column const& column) override
        {
            tables.back().columns.emplace_back(column);
        }

        void OnPrimaryKeys(std::string_view /*table*/, std::vector<std::string> const& columns) override
        {
            tables.back().primaryKeys = columns;
        }

        void OnForeignKey(SqlSchema::ForeignKeyConstraint const& foreignKeyConstraint) override
        {
            tables.back().foreignKeys.emplace_back(foreignKeyConstraint);
        }

        void OnExternalForeignKey(SqlSchema::ForeignKeyConstraint const& foreignKeyConstraint) override
        {
            tables.back().externalForeignKeys.emplace_back(foreignKeyConstraint);
        }

        void OnIndexes(std::vector<SqlSchema::IndexDefinition> const& indexes) override
        {
            tables.back().indexes = indexes;
        }
    } eventHandler { tables, tableNameCaseMap, std::move(callback), std::move(tableReadyCallback), std::move(tableFilter),
                     schema };
    ReadAllTables(stmt, database, schema, eventHandler);

    // Fixup table names in foreign keys
    // (Because at least Sqlite returns them in lowercase)
    for (auto& table: tables)
    {
        for (auto& key: table.foreignKeys)
        {
            key.primaryKey.table.table = tableNameCaseMap.at(ToLowerCase(key.primaryKey.table.table));
            key.foreignKey.table.table = tableNameCaseMap.at(ToLowerCase(key.foreignKey.table.table));
        }
        for (auto& key: table.externalForeignKeys)
        {
            key.primaryKey.table.table = tableNameCaseMap.at(ToLowerCase(key.primaryKey.table.table));
            key.foreignKey.table.table = tableNameCaseMap.at(ToLowerCase(key.foreignKey.table.table));
        }
    }

    return tables;
}

std::vector<ForeignKeyConstraint> AllForeignKeysTo(SqlStatement& stmt, FullyQualifiedTableName const& table)
{
    ZoneScopedN("SqlSchema::AllForeignKeysTo");
    ZoneTextObject(table.table);
    return AllForeignKeys(stmt, table, FullyQualifiedTableName {});
}

std::vector<ForeignKeyConstraint> AllForeignKeysFrom(SqlStatement& stmt, FullyQualifiedTableName const& table)
{
    ZoneScopedN("SqlSchema::AllForeignKeysFrom");
    ZoneTextObject(table.table);
    return AllForeignKeys(stmt, FullyQualifiedTableName {}, table);
}

SqlCreateTablePlan MakeCreateTablePlan(Table const& tableDescription)
{
    auto plan = SqlCreateTablePlan {};

    plan.tableName = tableDescription.name;

    for (auto const& columnDescription: tableDescription.columns)
    {
        // Extract per-column foreign key reference for single-column foreign keys.
        auto const foreignKeyRef = [&]() -> std::optional<SqlForeignKeyReferenceDefinition> {
            if (!columnDescription.foreignKeyConstraint)
                return std::nullopt;

            auto const& fkConstraint = *columnDescription.foreignKeyConstraint;
            // Only populate per-column foreign key for single-column constraints
            if (fkConstraint.foreignKey.columns.size() == 1 && fkConstraint.primaryKey.columns.size() == 1)
            {
                return SqlForeignKeyReferenceDefinition {
                    .tableName = fkConstraint.primaryKey.table.table,
                    .columnName = fkConstraint.primaryKey.columns.front(),
                };
            }
            return std::nullopt;
        }();

        auto columnDecl = SqlColumnDeclaration {
            .name = columnDescription.name,
            .type = columnDescription.type,
            .primaryKey =
                [&] {
                    if (columnDescription.isAutoIncrement)
                        return SqlPrimaryKeyType::AUTO_INCREMENT;

                    if (columnDescription.isPrimaryKey)
                        return SqlPrimaryKeyType::MANUAL;

                    return SqlPrimaryKeyType::NONE;
                }(),
            .foreignKey = foreignKeyRef,
            .required = !columnDescription.isNullable,
            .unique = columnDescription.isUnique,
            .defaultValue = columnDescription.defaultValue,
            .index = false,
            .primaryKeyIndex = [&]() -> uint16_t {
                auto const it = std::ranges::find(tableDescription.primaryKeys, columnDescription.name);
                if (it != tableDescription.primaryKeys.end())
                    return static_cast<uint16_t>(std::distance(tableDescription.primaryKeys.begin(), it) + 1);
                return 0;
            }(),
        };

        plan.columns.emplace_back(std::move(columnDecl));
    }

    for (auto const& fk: tableDescription.foreignKeys)
    {
        plan.foreignKeys.emplace_back(SqlCompositeForeignKeyConstraint {
            .columns = fk.foreignKey.columns,
            .referencedTableName = fk.primaryKey.table.table,
            .referencedColumns = fk.primaryKey.columns,
        });
    }

    return plan;
}

std::vector<SqlCreateTablePlan> MakeCreateTablePlan(TableList const& tableDescriptions)
{
    auto result = std::vector<SqlCreateTablePlan>();

    for (auto const& tableDescription: tableDescriptions)
        result.emplace_back(MakeCreateTablePlan(tableDescription));

    return result;
}

} // namespace Lightweight::SqlSchema
