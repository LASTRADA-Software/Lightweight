// SPDX-License-Identifier: Apache-2.0
#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

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

using KeyPair = std::pair<FullyQualifiedTableName /*fk table*/, FullyQualifiedTableName /*pk table*/>;

bool operator<(KeyPair const& a, KeyPair const& b)
{
    return std::tie(a.first, a.second) < std::tie(b.first, b.second);
}

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

    std::vector<std::string> AllTables(std::string_view database, std::string_view schema)
    {
        auto const tableType = "TABLE"sv;
        (void) database;
        (void) schema;

        auto stmt = SqlStatement();
        auto sqlResult = SQLTables(stmt.NativeHandle(),
                                   (SQLCHAR*) database.data(),
                                   (SQLSMALLINT) database.size(),
                                   (SQLCHAR*) schema.data(),
                                   (SQLSMALLINT) schema.size(),
                                   nullptr /* tables */,
                                   0 /* tables length */,
                                   (SQLCHAR*) tableType.data(),
                                   (SQLSMALLINT) tableType.size());
        SqlErrorInfo::RequireStatementSuccess(sqlResult, stmt.NativeHandle(), "SQLTables");

        auto result = std::vector<std::string>();
        while (stmt.FetchRow())
            result.emplace_back(stmt.GetColumn<std::string>(3));

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
            auto const sequenceNumber = stmt.GetNullableColumn<size_t>(9).value_or(1);
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
            keys.emplace_back(stmt.GetColumn<std::string>(4));
            sequenceNumbers.emplace_back(stmt.GetColumn<size_t>(5));
        }

        std::vector<std::string> sortedKeys;
        sortedKeys.resize(keys.size());
        for (size_t i = 0; i < keys.size(); ++i)
            sortedKeys.at(sequenceNumbers[i] - 1) = keys[i];

        return sortedKeys;
    }

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ReadAllTables(std::string_view database, std::string_view schema, EventHandler& eventHandler)
{
    auto stmt = SqlStatement {};
    auto const tableNames = AllTables(database, schema);

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

        std::vector<ForeignKeyConstraint> const foreignKeys = AllForeignKeysFrom(stmt, fullyQualifiedTableName);
        std::vector<ForeignKeyConstraint> const incomingForeignKeys = AllForeignKeysTo(stmt, fullyQualifiedTableName);

        for (auto const& foreignKey: foreignKeys)
            eventHandler.OnForeignKey(foreignKey);

        for (auto const& foreignKey: incomingForeignKeys)
            eventHandler.OnExternalForeignKey(foreignKey);

        auto columnStmt = SqlStatement();
        auto const sqlResult = SQLColumns(columnStmt.NativeHandle(),
                                          (SQLCHAR*) database.data(),
                                          (SQLSMALLINT) database.size(),
                                          (SQLCHAR*) schema.data(),
                                          (SQLSMALLINT) schema.size(),
                                          (SQLCHAR*) tableName.data(),
                                          (SQLSMALLINT) tableName.size(),
                                          nullptr /* column name */,
                                          0 /* column name length */);
        if (!SQL_SUCCEEDED(sqlResult))
            throw std::runtime_error(std::format("SQLColumns failed: {}", columnStmt.LastError()));

        Column column;

        while (columnStmt.FetchRow())
        {
            column.name = columnStmt.GetColumn<std::string>(4);
            auto const type = columnStmt.GetColumn<int>(5);
            column.dialectDependantTypeString = columnStmt.GetColumn<std::string>(6);
            column.size = columnStmt.GetColumn<int>(7);

            // 8 - bufferLength
            try
            {
                column.decimalDigits = columnStmt.GetColumn<uint16_t>(9);
                // some special handling of weird types
                if (column.dialectDependantTypeString == "money")
                {
                    // 0.123 -> decimalDigits = 3 size = 4
                    // 100.123 -> decimalDigits = 3  size = 6
                    column.size = column.decimalDigits;
                    column.decimalDigits = SQL_MAX_NUMERIC_LEN;
                }
            }
            catch (std::exception&)
            {
                column.decimalDigits = static_cast<uint16_t>(column.size);
            }
            // 10 - NUM_PREC_RADIX
            try
            {
                column.isNullable = columnStmt.GetColumn<bool>(11);
            }
            catch (std::exception&)
            {
                column.isNullable = true;
            }
            // 12 - remarks
            try
            {
                column.defaultValue = columnStmt.GetColumn<std::string>(13);
            }
            catch (std::exception&)
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

            // accumulated properties
            column.isPrimaryKey = std::ranges::contains(primaryKeys, column.name);
            // column.isForeignKey = ...;
            column.isForeignKey = std::ranges::any_of(foreignKeys, [&column](ForeignKeyConstraint const& fk) {
                return std::ranges::contains(fk.foreignKey.columns, column.name);
            });
            if (auto const p = std::ranges::find_if(incomingForeignKeys,
                                                    [&column](ForeignKeyConstraint const& fk) {
                                                        return std::ranges::contains(fk.foreignKey.columns, column.name);
                                                    });
                p != incomingForeignKeys.end())
            {
                column.foreignKeyConstraint = *p;
            }

            eventHandler.OnColumn(column);
        }

        eventHandler.OnTableEnd();
    }
}

std::string ToLowerCase(std::string_view str)
{
    std::string result(str);
    std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

TableList ReadAllTables(std::string_view database, std::string_view schema, ReadAllTablesCallback callback)
{
    TableList tables;
    struct EventHandler: public SqlSchema::EventHandler
    {
        TableList& tables;
        ReadAllTablesCallback callback;
        size_t currentlyProcessedTablesCount = 0;
        size_t totalTableCount = 0;

        EventHandler(TableList& tables, ReadAllTablesCallback callback):
            tables { tables },
            callback { std::move(callback) }
        {
        }

        void OnTables(std::vector<std::string> const& tableNames) override
        {
            totalTableCount = tableNames.size();
        }

        bool OnTable(std::string_view table) override
        {
            ++currentlyProcessedTablesCount;
            if (callback)
                callback(table, currentlyProcessedTablesCount, totalTableCount);
            tables.emplace_back(Table { .name = std::string(table) });
            return true;
        }

        void OnTableEnd() override {}

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
    } eventHandler { tables, std::move(callback) };
    ReadAllTables(database, schema, eventHandler);

    std::map<std::string, std::string> tableNameCaseMap;
    for (auto const& table: tables)
        tableNameCaseMap[ToLowerCase(table.name)] = table.name;

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

} // namespace Lightweight::SqlSchema
