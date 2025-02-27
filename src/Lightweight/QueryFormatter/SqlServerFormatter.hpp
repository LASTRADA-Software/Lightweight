// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <format>

class SqlServerQueryFormatter final: public SQLiteQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // TODO: Figure out how to get the last insert id in SQL Server for a given table.
        return std::format("SELECT @@IDENTITY");
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1" : "0";
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
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
        return std::visit(
            detail::overloaded {
                [](Bigint const&) -> std::string { return "BIGINT"; },
                [](Binary const& type) -> std::string { return std::format("VARBINARY({})", type.size); },
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
                [](VarBinary const& type) -> std::string { return std::format("VARBINARY({})", type.size); },
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

        return sqlQueryString.str();
    }

    [[nodiscard]] StringList AlterTable(std::string_view tableName,
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
                    [tableName](RenameTable const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE "{}" RENAME TO "{}";)", tableName, actualCommand.newTableName);
                    },
                    [tableName, this](AddColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE "{}" ADD "{}" {} {};)",
                                           tableName,
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [tableName, this](AlterColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE "{}" ALTER COLUMN "{}" {} {};)",
                                           tableName,
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [tableName](RenameColumn const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE "{}" RENAME COLUMN "{}" TO "{}";)",
                                           tableName,
                                           actualCommand.oldColumnName,
                                           actualCommand.newColumnName);
                    },
                    [tableName](DropColumn const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE "{}" DROP COLUMN "{}";)", tableName, actualCommand.columnName);
                    },
                    [tableName](AddIndex const& actualCommand) -> std::string {
                        using namespace std::string_view_literals;
                        auto const uniqueStr = actualCommand.unique ? "UNIQUE "sv : ""sv;
                        return std::format(R"(CREATE {2}INDEX "{0}_{1}_index" ON "{0}"("{1}");)",
                                           tableName,
                                           actualCommand.columnName,
                                           uniqueStr);
                    },
                    [tableName](DropIndex const& actualCommand) -> std::string {
                        return std::format(R"(DROP INDEX "{0}_{1}_index";)", tableName, actualCommand.columnName);
                    },
                    [tableName](AddForeignKey const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE "{}" ADD {};)",
                            tableName,
                            BuildForeignKeyConstraint(actualCommand.columnName, actualCommand.referencedColumn));
                    },
                    [tableName](DropForeignKey const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE "{}" DROP CONSTRAINT "{}";)",
                                           tableName,
                                           std::format("FK_{}", actualCommand.columnName));
                    },
                },
                command);
        }

        return { sqlQueryString.str() };
    }
};
