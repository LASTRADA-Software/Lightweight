// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <format>

namespace Lightweight
{

class SqlServerQueryFormatter final: public SQLiteQueryFormatter
{
  public:
    [[nodiscard]] StringList DropTable(std::string_view schemaName,
                                       std::string_view const& tableName,
                                       bool ifExists = false,
                                       bool cascade = false) const override
    {
        StringList result;

        if (cascade)
        {
            // Drop all FK constraints referencing this table first using dynamic SQL
            std::string const schemaFilter = schemaName.empty() ? "dbo" : std::string(schemaName);

            result.emplace_back(std::format(
                R"(DECLARE @sql NVARCHAR(MAX) = N'';
SELECT @sql = @sql + 'ALTER TABLE ' + QUOTENAME(OBJECT_SCHEMA_NAME(fk.parent_object_id)) + '.' + QUOTENAME(OBJECT_NAME(fk.parent_object_id)) + ' DROP CONSTRAINT ' + QUOTENAME(fk.name) + '; '
FROM sys.foreign_keys fk
WHERE OBJECT_NAME(fk.referenced_object_id) = '{}' AND OBJECT_SCHEMA_NAME(fk.referenced_object_id) = '{}';
EXEC sp_executesql @sql;)",
                tableName,
                schemaFilter));
        }

        // Then drop the table
        if (ifExists)
            result.emplace_back(std::format("DROP TABLE IF EXISTS {};", FormatTableName(schemaName, tableName)));
        else
            result.emplace_back(std::format("DROP TABLE {};", FormatTableName(schemaName, tableName)));

        return result;
    }

    [[nodiscard]] std::string BinaryLiteral(std::span<uint8_t const> data) const override
    {
        std::string result;
        result.reserve((data.size() * 2) + 2);
        result += "0x";
        for (uint8_t byte: data)
            result += std::format("{:02X}", byte);
        return result;
    }

    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // TODO: Figure out how to get the last insert id in SQL Server for a given table.
        return std::format("SELECT @@IDENTITY");
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1" : "0";
    }

    [[nodiscard]] std::string_view DateFunction() const noexcept override
    {
        return "GETDATE()";
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
                                          // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                          std::string_view fields,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition,
                                          std::string_view orderBy,
                                          size_t count) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT";
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " TOP " << count;
        sqlQueryString << ' ' << fields;
        sqlQueryString << " FROM \"" << fromTable << '"';
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << '"';
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << orderBy;
        ;
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string SelectRange(bool distinct,
                                          // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                          std::string_view fields,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition,
                                          std::string_view orderBy,
                                          std::string_view groupBy,
                                          std::size_t offset,
                                          std::size_t limit) const override
    {
        assert(!orderBy.empty());
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT " << fields;
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " FROM \"" << fromTable << "\"";
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << "\"";
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << groupBy;
        sqlQueryString << orderBy;
        sqlQueryString << " OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(detail::overloaded {
                              [](Bigint const&) -> std::string { return "BIGINT"; },
                              [](Binary const& type) -> std::string {
                                  if (type.size == 0 || type.size > 8000)
                                      return "VARBINARY(MAX)";
                                  else
                                      return std::format("VARBINARY({})", type.size);
                              },
                              [](Bool const&) -> std::string { return "BIT"; },
                              [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                              [](Date const&) -> std::string { return "DATE"; },
                              [](DateTime const&) -> std::string { return "DATETIME"; },
                              [](Decimal const& type) -> std::string {
                                  return std::format("DECIMAL({}, {})", type.precision, type.scale);
                              },
                              [](Guid const&) -> std::string { return "UNIQUEIDENTIFIER"; },
                              [](Integer const&) -> std::string { return "INTEGER"; },
                              [](NChar const& type) -> std::string { return std::format("NCHAR({})", type.size); },
                              [](NVarchar const& type) -> std::string {
                                  if (type.size == 0 || type.size > SqlOptimalMaxColumnSize)
                                      return "NVARCHAR(MAX)";
                                  else
                                      return std::format("NVARCHAR({})", type.size);
                              },
                              [](Real const&) -> std::string { return "REAL"; },
                              [](Smallint const&) -> std::string { return "SMALLINT"; },
                              [](Text const&) -> std::string { return "VARCHAR(MAX)"; },
                              [](Time const&) -> std::string { return "TIME"; },
                              [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                              [](Tinyint const&) -> std::string { return "TINYINT"; },
                              [](VarBinary const& type) -> std::string {
                                  if (type.size == 0 || type.size > 8000)
                                      return "VARBINARY(MAX)";
                                  else
                                      return std::format("VARBINARY({})", type.size);
                              },
                              [](Varchar const& type) -> std::string {
                                  if (type.size == 0 || type.size > SqlOptimalMaxColumnSize)
                                      return "VARCHAR(MAX)";
                                  else
                                      return std::format("VARCHAR({})", type.size);
                              },
                          },
                          type);
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << '"' << column.name << "\" " << ColumnType(column.type);

        if (column.required)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " IDENTITY(1,1) PRIMARY KEY";
        else if (column.primaryKey == SqlPrimaryKeyType::NONE && !column.index && column.unique)
            sqlQueryString << " UNIQUE";

        if (!column.defaultValue.empty())
            sqlQueryString << " DEFAULT " << column.defaultValue;

        return sqlQueryString.str();
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] StringList CreateTable(std::string_view schema,
                                         std::string_view tableName,
                                         std::vector<SqlColumnDeclaration> const& columns,
                                         std::vector<SqlCompositeForeignKeyConstraint> const& foreignKeys,
                                         bool ifNotExists = false) const override
    {
        std::stringstream ss;

        // SQL Server doesn't have CREATE TABLE IF NOT EXISTS, use conditional block
        if (ifNotExists)
        {
            std::string schemaFilter = schema.empty() ? "dbo" : std::string(schema);
            ss << std::format("IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = '{}' AND schema_id = SCHEMA_ID('{}'))\n",
                              tableName,
                              schemaFilter);
        }

        ss << std::format("CREATE TABLE {} (", FormatTableName(schema, tableName));

        bool first = true;
        for (auto const& column: columns)
        {
            if (!first)
                ss << ",";
            first = false;
            ss << "\n    " << BuildColumnDefinition(column);
        }

        auto const primaryKeys = [&]() -> std::vector<std::string> {
            std::vector<std::pair<uint16_t, std::string>> indexedPrimaryKeys;
            for (auto const& col: columns)
                if (col.primaryKey != SqlPrimaryKeyType::NONE)
                    indexedPrimaryKeys.emplace_back(col.primaryKeyIndex, col.name);
            std::ranges::sort(indexedPrimaryKeys, [](auto const& a, auto const& b) { return a.first < b.first; });

            std::vector<std::string> primaryKeys;
            primaryKeys.reserve(indexedPrimaryKeys.size());
            for (auto const& [index, name]: indexedPrimaryKeys)
                primaryKeys.push_back(name);
            return primaryKeys;
        }();

        if (!primaryKeys.empty())
        {
            // If primary key is AUTO_INCREMENT, it's already defined inline in BuildColumnDefinition.
            // Only add explicit PRIMARY KEY constraint if NOT AUTO_INCREMENT?
            // SQLiteFormatter logic:
            // if (!primaryKeys.empty()) ss << ", PRIMARY KEY (" << Join(primaryKeys, ", ") << ")";
            // But BuildColumnDefinition adds "PRIMARY KEY" for AUTO_INCREMENT!
            // Double primary key definition is invalid.

            // Check if any column is AUTO_INCREMENT
            bool hasIdentity = false;
            for (auto const& col: columns)
                if (col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
                    hasIdentity = true;

            if (!hasIdentity)
            {
                ss << ",\n    PRIMARY KEY (";
                bool firstPk = true;
                for (auto const& pk: primaryKeys)
                {
                    if (!firstPk)
                        ss << ", ";
                    firstPk = false;
                    ss << '"' << pk << '"';
                }
                ss << ")";
            }
        }

        if (!foreignKeys.empty())
        {
            for (auto const& fk: foreignKeys)
            {
                ss << ",\n    CONSTRAINT " << std::format("\"FK_{}_{}\"", tableName, fk.columns[0]) // Basic name generation
                   << " FOREIGN KEY (";

                size_t i = 0;
                for (auto const& col: fk.columns)
                {
                    if (i++ > 0)
                        ss << ", ";
                    ss << '"' << col << '"';
                }

                ss << ") REFERENCES " << FormatTableName(schema, fk.referencedTableName) << " (";

                i = 0;
                for (auto const& col: fk.referencedColumns)
                {
                    if (i++ > 0)
                        ss << ", ";
                    ss << '"' << col << '"';
                }
                ss << ")";
            }
        }

        // Add single-column foreign keys that were defined inline in SQLite but need to be table-constraints here
        // or just appended if we didn't add them in BuildColumnDefinition (which we didn't).
        for (auto const& column: columns)
        {
            if (column.foreignKey)
            {
                ss << ",\n    " << BuildForeignKeyConstraint(tableName, column.name, *column.foreignKey);
            }
        }

        ss << "\n);";

        StringList result;
        result.emplace_back(ss.str());

        // Create Indexes
        for (SqlColumnDeclaration const& column: columns)
        {
            if (column.index && column.primaryKey == SqlPrimaryKeyType::NONE)
            {
                // primary keys are always indexed
                if (column.unique)
                {
                    if (schema.empty())
                        result.emplace_back(std::format(R"(CREATE UNIQUE INDEX "{}_{}_index" ON "{}"("{}");)",
                                                        tableName,
                                                        column.name,
                                                        tableName,
                                                        column.name));
                    else
                        result.emplace_back(std::format(R"(CREATE UNIQUE INDEX "{}_{}_index" ON "{}"."{}"("{}");)",
                                                        tableName,
                                                        column.name,
                                                        schema,
                                                        tableName,
                                                        column.name));
                }
                else
                {
                    if (schema.empty())
                        result.emplace_back(std::format(
                            R"(CREATE INDEX "{}_{}_index" ON "{}"("{}");)", tableName, column.name, tableName, column.name));
                    else
                        result.emplace_back(std::format(R"(CREATE INDEX "{}_{}_index" ON "{}"."{}"("{}");)",
                                                        tableName,
                                                        column.name,
                                                        schema,
                                                        tableName,
                                                        column.name));
                }
            }
        }

        return result;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] StringList AlterTable(std::string_view schemaName,
                                        std::string_view tableName,
                                        std::vector<SqlAlterTableCommand> const& commands) const override
    {
        std::stringstream sqlQueryString;

        int currentCommand = 0;
        for (SqlAlterTableCommand const& command: commands)
        {
            if (currentCommand > 0)
                sqlQueryString << '\n';
            ++currentCommand;

            using namespace SqlAlterTableCommands;
            sqlQueryString << std::visit(
                detail::overloaded {
                    [schemaName, tableName](RenameTable const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} RENAME TO "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.newTableName);
                    },
                    [schemaName, tableName, this](AddColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} ADD "{}" {} {};)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName, this](AlterColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} ALTER COLUMN "{}" {} {};)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName](RenameColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} RENAME COLUMN "{}" TO "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.oldColumnName,
                                           actualCommand.newColumnName);
                    },
                    [schemaName, tableName](DropColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} DROP COLUMN "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName);
                    },
                    [schemaName, tableName](AddIndex const& actualCommand) -> std::string {
                        using namespace std::string_view_literals;
                        auto const uniqueStr = actualCommand.unique ? "UNIQUE "sv : ""sv;
                        if (schemaName.empty())
                            return std::format(R"(CREATE {2}INDEX "{0}_{1}_index" ON "{0}"("{1}");)",
                                               tableName,
                                               actualCommand.columnName,
                                               uniqueStr);
                        else
                            return std::format(R"(CREATE {3}INDEX "{0}_{1}_{2}_index" ON "{0}"."{1}"("{2}");)",
                                               schemaName,
                                               tableName,
                                               actualCommand.columnName,
                                               uniqueStr);
                    },
                    [schemaName, tableName](DropIndex const& actualCommand) -> std::string {
                        if (schemaName.empty())
                            return std::format(R"(DROP INDEX "{0}_{1}_index";)", tableName, actualCommand.columnName);
                        else
                            return std::format(
                                R"(DROP INDEX "{0}_{1}_{2}_index";)", schemaName, tableName, actualCommand.columnName);
                    },
                    [schemaName, tableName](AddForeignKey const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE {} ADD {};)",
                            FormatTableName(schemaName, tableName),
                            BuildForeignKeyConstraint(tableName, actualCommand.columnName, actualCommand.referencedColumn));
                    },
                    [schemaName, tableName](DropForeignKey const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} DROP CONSTRAINT "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           std::format("FK_{}_{}", tableName, actualCommand.columnName));
                    },
                    [schemaName, tableName](AddCompositeForeignKey const& actualCommand) -> std::string {
                        std::stringstream ss;
                        ss << "ALTER TABLE " << FormatTableName(schemaName, tableName) << " ADD CONSTRAINT "
                           << std::format("\"FK_{}_{}\"", tableName, actualCommand.columns[0]) << " FOREIGN KEY (";

                        size_t i = 0;
                        for (auto const& col: actualCommand.columns)
                        {
                            if (i++ > 0)
                                ss << ", ";
                            ss << '"' << col << '"';
                        }
                        ss << ") REFERENCES " << FormatTableName(schemaName, actualCommand.referencedTableName) << " (";

                        i = 0;
                        for (auto const& col: actualCommand.referencedColumns)
                        {
                            if (i++ > 0)
                                ss << ", ";
                            ss << '"' << col << '"';
                        }
                        ss << ");";
                        return ss.str();
                    },
                    [schemaName, tableName, this](AddColumnIfNotExists const& actualCommand) -> std::string {
                        // SQL Server uses conditional IF NOT EXISTS
                        return std::format(
                            R"(IF NOT EXISTS (SELECT * FROM sys.columns WHERE object_id = OBJECT_ID('{}') AND name = '{}')
ALTER TABLE {} ADD "{}" {} {};)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            ColumnType(actualCommand.columnType),
                            actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName](DropColumnIfExists const& actualCommand) -> std::string {
                        // SQL Server uses conditional IF EXISTS
                        return std::format(
                            R"(IF EXISTS (SELECT * FROM sys.columns WHERE object_id = OBJECT_ID('{}') AND name = '{}')
ALTER TABLE {} DROP COLUMN "{}";)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName);
                    },
                    [schemaName, tableName](DropIndexIfExists const& actualCommand) -> std::string {
                        if (schemaName.empty())
                            return std::format(
                                R"(IF EXISTS (SELECT * FROM sys.indexes WHERE name = '{0}_{1}_index' AND object_id = OBJECT_ID('{0}'))
DROP INDEX "{0}_{1}_index" ON "{0}";)",
                                tableName,
                                actualCommand.columnName);
                        else
                            return std::format(
                                R"(IF EXISTS (SELECT * FROM sys.indexes WHERE name = '{0}_{1}_{2}_index')
DROP INDEX "{0}_{1}_{2}_index" ON "{0}"."{1}";)",
                                schemaName,
                                tableName,
                                actualCommand.columnName);
                    },
                },
                command);
        }

        return { sqlQueryString.str() };
    }

    [[nodiscard]] std::string QueryServerVersion() const override
    {
        return "SELECT @@VERSION";
    }
};

} // namespace Lightweight
