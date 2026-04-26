// SPDX-License-Identifier: Apache-2.0

#include "SqlColumnTypeDefinitions.hpp"
#include "SqlError.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlSchema.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <exception>

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

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ReadAllTables(SqlStatement& stmt, std::string_view database, std::string_view schema, EventHandler& eventHandler)
{
    ZoneScopedN("SqlSchema::ReadAllTables(EventHandler)");
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
                // std::cerr << "DEBUG: Exception reading column meta for table " << tableName << ": " << e.what() << "\n";
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
                         && column.dialectDependantTypeString.find('(') != std::string::npos)
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
        std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
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
            std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
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
