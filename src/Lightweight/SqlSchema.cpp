// SPDX-License-Identifier: Apache-2.0

#include "SqlColumnTypeDefinitions.hpp"
#include "SqlError.hpp"
#include "SqlSchema.hpp"
#include "SqlStatement.hpp"

#include <algorithm>
#include <cassert>
#include <exception>

#include <sql.h>
#include <sqlext.h>
#include <sqlspi.h>
#include <sqltypes.h>

namespace Lightweight::SqlSchema
{

using namespace std::string_literals;
using namespace std::string_view_literals;

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

    std::vector<std::string> AllTables(SqlStatement& stmt, std::string_view database, std::string_view schema)
    {

        auto sqlResult = SQLTables(stmt.NativeHandle(),
                                   (SQLCHAR*) (!database.empty() ? database.data() : nullptr),
                                   (SQLSMALLINT) database.size(),
                                   (SQLCHAR*) (!schema.empty() ? schema.data() : nullptr),
                                   (SQLSMALLINT) schema.size(),
                                   nullptr,
                                   0,
                                   nullptr,
                                   0);
        SqlErrorInfo::RequireStatementSuccess(sqlResult, stmt.NativeHandle(), "SQLTables");

        auto result = std::vector<std::string>();
        while (stmt.FetchRow())
        {
            auto schemaOpt = stmt.GetNullableColumn<std::string>(2);
            auto nameOpt = stmt.GetNullableColumn<std::string>(3);
            auto typeOpt = stmt.GetNullableColumn<std::string>(4);
            if (!nameOpt)
                continue;

            auto const schemaName = schemaOpt.value_or("");
            auto const& name = *nameOpt;
            auto const type = typeOpt.value_or("");

            if (schemaName == "sys" || schemaName == "INFORMATION_SCHEMA")
                continue;

            if (type == "TABLE" || type == "BASE TABLE")
                result.emplace_back(name);
        }

        return result;
    }

    std::vector<ForeignKeyConstraint> AllForeignKeys(SqlStatement& stmt,
                                                     FullyQualifiedTableName const& primaryKey,
                                                     FullyQualifiedTableName const& foreignKey)
    {
        auto* pkCatalog = (SQLCHAR*) (!primaryKey.catalog.empty() ? primaryKey.catalog.c_str() : nullptr);
        auto* pkSchema = (SQLCHAR*) (!primaryKey.schema.empty() ? primaryKey.schema.c_str() : nullptr);
        auto* pkTable = (SQLCHAR*) (!primaryKey.table.empty() ? primaryKey.table.c_str() : nullptr);
        auto* fkCatalog = (SQLCHAR*) (!foreignKey.catalog.empty() ? foreignKey.catalog.c_str() : nullptr);
        auto* fkSchema = (SQLCHAR*) (!foreignKey.schema.empty() ? foreignKey.schema.c_str() : nullptr);
        auto* fkTable = (SQLCHAR*) (!foreignKey.table.empty() ? foreignKey.table.c_str() : nullptr);
        auto sqlResult = SQLForeignKeys(stmt.NativeHandle(),
                                        pkCatalog,
                                        (SQLSMALLINT) primaryKey.catalog.size(),
                                        pkSchema,
                                        (SQLSMALLINT) primaryKey.schema.size(),
                                        pkTable,
                                        (SQLSMALLINT) primaryKey.table.size(),
                                        fkCatalog,
                                        (SQLSMALLINT) foreignKey.catalog.size(),
                                        fkSchema,
                                        (SQLSMALLINT) foreignKey.schema.size(),
                                        fkTable,
                                        (SQLSMALLINT) foreignKey.table.size());

        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLForeignKeys failed: {}", stmt.LastError()));

        // ODBC SQLForeignKeys() should return at least 14 columns per the spec.
        // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
        // return fewer columns. We need at least 9 columns (KEY_SEQ) to process foreign keys.
        constexpr size_t MinRequiredColumns = 9;
        auto const numColumns = stmt.NumColumnsAffected();
        if (numColumns < MinRequiredColumns)
            return {}; // Driver didn't return expected columns, return empty result

        using ColumnList = std::vector<std::pair<std::string /*fk*/, std::string /*pk*/>>;
        auto constraints = std::map<KeyPair, ColumnList>();
        while (stmt.FetchRow())
        {
            auto primaryKeyTable = FullyQualifiedTableName {
                .catalog = stmt.GetNullableColumn<std::string>(1).value_or(""),
                .schema = stmt.GetNullableColumn<std::string>(2).value_or(""),
                .table = stmt.GetNullableColumn<std::string>(3).value_or(""),
            };
            auto pkColumnName = stmt.GetNullableColumn<std::string>(4).value_or("");
            auto foreignKeyTable = FullyQualifiedTableName {
                .catalog = stmt.GetNullableColumn<std::string>(5).value_or(""),
                .schema = stmt.GetNullableColumn<std::string>(6).value_or(""),
                .table = stmt.GetNullableColumn<std::string>(7).value_or(""),
            };
            auto foreignKeyColumn = stmt.GetNullableColumn<std::string>(8).value_or("");
            auto const sequenceNumber = static_cast<size_t>(stmt.GetNullableColumn<int16_t>(9).value_or(1));
            ColumnList& keyColumns = constraints[{ foreignKeyTable, primaryKeyTable }];
            if (sequenceNumber > keyColumns.size())
                keyColumns.resize(sequenceNumber);
            keyColumns[sequenceNumber - 1] = { std::move(foreignKeyColumn), std::move(pkColumnName) };
        }

        auto result = std::vector<ForeignKeyConstraint>();
        for (auto const& [keyPair, columns]: constraints)
        {
            auto const [fromColumns, toColumns] = Unzip(columns);
            result.emplace_back(ForeignKeyConstraint {
                .foreignKey = {
                    .table = keyPair.first,
                    .columns = fromColumns,
                },
                .primaryKey = {
                    .table = keyPair.second,
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

            stmt.ExecuteDirect(query);

            std::vector<std::pair<int, std::string>> pkCols;
            while (stmt.FetchRow())
            {
                // cid, name, type, notnull, dflt_value, pk
                auto name = stmt.GetColumn<std::string>(2);
                auto pkInfo = stmt.GetColumn<int>(6);
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
        auto* pkCatalog = (SQLCHAR*) (!table.catalog.empty() ? table.catalog.c_str() : nullptr);
        auto* pkSchema = (SQLCHAR*) (!table.schema.empty() ? table.schema.c_str() : nullptr);
        auto* pkTable = (SQLCHAR*) (!table.table.empty() ? table.table.c_str() : nullptr);

        auto sqlResult = SQLPrimaryKeys(stmt.NativeHandle(),
                                        pkCatalog,
                                        (SQLSMALLINT) table.catalog.size(),
                                        pkSchema,
                                        (SQLSMALLINT) table.schema.size(),
                                        pkTable,
                                        (SQLSMALLINT) table.table.size());
        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLPrimaryKeys failed: {}", stmt.LastError()));

        while (stmt.FetchRow())
        {
            keys.emplace_back(stmt.GetNullableColumn<std::string>(4).value_or(""));
            sequenceNumbers.emplace_back(stmt.GetNullableColumn<int16_t>(5).value_or(0));
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
        auto* pkCatalog = (SQLCHAR*) (!table.catalog.empty() ? table.catalog.c_str() : nullptr);
        auto* pkSchema = (SQLCHAR*) (!table.schema.empty() ? table.schema.c_str() : nullptr);
        auto* pkTable = (SQLCHAR*) (!table.table.empty() ? table.table.c_str() : nullptr);

        auto sqlResult = SQLStatistics(stmt.NativeHandle(),
                                       pkCatalog,
                                       (SQLSMALLINT) table.catalog.size(),
                                       pkSchema,
                                       (SQLSMALLINT) table.schema.size(),
                                       pkTable,
                                       (SQLSMALLINT) table.table.size(),
                                       SQL_INDEX_UNIQUE,
                                       SQL_ENSURE);

        if (!SQL_SUCCEEDED(sqlResult))
            return {}; // Ignore errors or throw? Safest to ignore for now as some drivers might not support it fully.

        // ODBC SQLStatistics() should return at least 13 columns per the spec.
        // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
        // return fewer columns. We need at least 9 columns (COLUMN_NAME) to process unique columns.
        constexpr size_t MinRequiredColumns = 9;
        auto const numColumns = stmt.NumColumnsAffected();
        if (numColumns < MinRequiredColumns)
            return {}; // Driver didn't return expected columns, return empty result

        std::map<std::string, std::vector<std::string>> uniqueIndices;

        while (stmt.FetchRow())
        {
            // Col 6: INDEX_NAME
            // Col 9: COLUMN_NAME
            auto indexName = stmt.GetNullableColumn<std::string>(6).value_or("");
            auto columnName = stmt.GetNullableColumn<std::string>(9).value_or("");
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
    std::vector<IndexDefinition> AllIndexes(SqlStatement& stmt,
                                            FullyQualifiedTableName const& table,
                                            std::vector<std::string> const& primaryKeys)
    {
        try
        {
            // Use a separate statement to avoid issues with pending cursors from previous operations
            auto indexStmt = SqlStatement { stmt.Connection() };

            auto* pkCatalog = (SQLCHAR*) (!table.catalog.empty() ? table.catalog.c_str() : nullptr);
            auto* pkSchema = (SQLCHAR*) (!table.schema.empty() ? table.schema.c_str() : nullptr);
            auto* pkTable = (SQLCHAR*) (!table.table.empty() ? table.table.c_str() : nullptr);

            // Use SQL_INDEX_ALL to retrieve all index types
            auto sqlResult = SQLStatistics(indexStmt.NativeHandle(),
                                           pkCatalog,
                                           (SQLSMALLINT) table.catalog.size(),
                                           pkSchema,
                                           (SQLSMALLINT) table.schema.size(),
                                           pkTable,
                                           (SQLSMALLINT) table.table.size(),
                                           SQL_INDEX_ALL,
                                           SQL_ENSURE);

            if (!SQL_SUCCEEDED(sqlResult))
                return {};

            // ODBC SQLStatistics() should return at least 13 columns per the spec.
            // However, some drivers (e.g., MS SQL ODBC Driver 18 in certain environments) may
            // return fewer columns. We need at least 9 columns (COLUMN_NAME) to process indexes.
            constexpr size_t MinRequiredColumns = 9;
            auto const numColumns = indexStmt.NumColumnsAffected();
            if (numColumns < MinRequiredColumns)
                return {}; // Driver didn't return expected columns, return empty result

            // Map: index_name -> (columns sorted by ordinal_position, isUnique)
            struct IndexInfo
            {
                std::vector<std::pair<int16_t, std::string>> columnsWithOrdinal; // (ordinal, column_name)
                bool isUnique = false;
            };
            std::map<std::string, IndexInfo> indexMap;

            while (indexStmt.FetchRow())
            {
                // Column mappings from SQLStatistics result set:
                //   4: NON_UNIQUE (0 = unique, 1 = not unique, NULL for statistics rows)
                //   6: INDEX_NAME
                //   7: TYPE (SQL_TABLE_STAT=0, SQL_INDEX_CLUSTERED=1, SQL_INDEX_HASHED=2, SQL_INDEX_OTHER=3)
                //   8: ORDINAL_POSITION (column position in index)
                //   9: COLUMN_NAME

                // Skip statistics rows (TYPE == 0)
                auto typeOpt = indexStmt.GetNullableColumn<int16_t>(7);
                if (!typeOpt || *typeOpt == 0)
                    continue;

                auto indexNameOpt = indexStmt.GetNullableColumn<std::string>(6);
                auto columnNameOpt = indexStmt.GetNullableColumn<std::string>(9);

                if (!indexNameOpt || indexNameOpt->empty() || !columnNameOpt || columnNameOpt->empty())
                    continue;

                auto ordinalOpt = indexStmt.GetNullableColumn<int16_t>(8);
                auto nonUniqueOpt = indexStmt.GetNullableColumn<int16_t>(4);

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
                stmt.ExecuteDirect(sql);
                std::vector<std::string> identityCols;
                while (stmt.FetchRow())
                {
                    identityCols.push_back(stmt.GetColumn<std::string>(1));
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
    auto const tableNames = AllTables(stmt, database, schema);

    eventHandler.OnTables(tableNames);

    for (auto const& tableName: tableNames)
    {
        if (tableName == "sqlite_sequence")
            continue;

        if (!eventHandler.OnTable(tableName))
            continue;

        auto const fullyQualifiedTableName = FullyQualifiedTableName {
            .catalog = std::string(database),
            .schema = std::string(schema),
            .table = std::string(tableName),
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
        auto const sqlResult = SQLColumns(columnStmt.NativeHandle(),
                                          (SQLCHAR*) database.data(),
                                          (SQLSMALLINT) database.size(),
                                          (SQLCHAR*) (!schema.empty() ? schema.data() : nullptr),
                                          (SQLSMALLINT) schema.size(),
                                          (SQLCHAR*) tableName.data(),
                                          (SQLSMALLINT) tableName.size(),
                                          nullptr /* column name */,
                                          0 /* column name length */);
        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLColumns failed: {}", columnStmt.LastError()));

        // ODBC SQLColumns() should return 18 columns per the spec.
        // However, some drivers may return fewer columns. Track the actual column count
        // to avoid accessing non-existent columns which causes ODBC errors.
        auto const numColumns = columnStmt.NumColumnsAffected();

        Column column;

        while (columnStmt.FetchRow())
        {
            // std::cerr << "DEBUG: FetchRow success for " << tableName << "\n";
            int type = 0;
            try
            {
                column.name = columnStmt.GetNullableColumn<std::string>(4).value_or("");
                type = columnStmt.GetColumn<int>(5); // DATA_TYPE
                column.dialectDependantTypeString = columnStmt.GetNullableColumn<std::string>(6).value_or("");
                // COLUMN_SIZE (column 7) can be negative for some drivers (e.g., PostgreSQL returns -4 for BYTEA)
                // to indicate "unknown" size. Treat negative values as 0.
                auto const rawSize = columnStmt.GetColumn<int>(7);
                column.size = rawSize > 0 ? static_cast<size_t>(rawSize) : 0;

                // 8 - bufferLength
                column.decimalDigits = numColumns >= 9 ? columnStmt.GetNullableColumn<uint16_t>(9).value_or(0) : 0;
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
                    column.isNullable = columnStmt.GetColumn<bool>(11);
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
                    column.defaultValue = columnStmt.GetNullableColumn<std::string>(13).value_or("");
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
        bool OnTable(std::string_view table) override
        {
            ++currentlyProcessedTablesCount;
            if (callback)
                callback(table, currentlyProcessedTablesCount, totalTableCount);

            // Apply filter predicate - skip reading detailed schema if table doesn't match
            if (tableFilter && !tableFilter(schema, table))
                return false;

            tables.emplace_back(Table { .name = std::string(table) });
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
    return AllForeignKeys(stmt, table, FullyQualifiedTableName {});
}

std::vector<ForeignKeyConstraint> AllForeignKeysFrom(SqlStatement& stmt, FullyQualifiedTableName const& table)
{
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
