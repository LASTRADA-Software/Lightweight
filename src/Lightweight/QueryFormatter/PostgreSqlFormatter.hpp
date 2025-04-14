// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <format>

class PostgreSqlFormatter final: public SQLiteQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // NB: Find a better way to do this on the given table.
        // In our case it works, because we're expected to call this right after an insert.
        // But a race condition may still happen if another client inserts a row at the same time too.
        return std::format("SELECT lastval();");
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;

        sqlQueryString << '"' << column.name << "\" ";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << "SERIAL";
        else
            sqlQueryString << ColumnType(column.type);

        if (column.required)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " PRIMARY KEY";
        else if (column.primaryKey == SqlPrimaryKeyType::NONE && !column.index && column.unique)
            sqlQueryString << " UNIQUE";

        return sqlQueryString.str();
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;

        // PostgreSQL stores all strings as UTF-8
        return std::visit(detail::overloaded {
                              [](Bigint const&) -> std::string { return "BIGINT"; },
                              [](Binary const& type) -> std::string { return std::format("BYTEA", type.size); },
                              [](Bool const&) -> std::string { return "BOOLEAN"; },
                              [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                              [](Date const&) -> std::string { return "DATE"; },
                              [](DateTime const&) -> std::string { return "TIMESTAMP"; },
                              [](Decimal const& type) -> std::string {
                                  return std::format("DECIMAL({}, {})", type.precision, type.scale);
                              },
                              [](Guid const&) -> std::string { return "UUID"; },
                              [](Integer const&) -> std::string { return "INTEGER"; },
                              [](NChar const& type) -> std::string { return std::format("CHAR({})", type.size); },
                              [](NVarchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
                              [](Real const&) -> std::string { return "REAL"; },
                              [](Smallint const&) -> std::string { return "SMALLINT"; },
                              [](Text const&) -> std::string { return "TEXT"; },
                              [](Time const&) -> std::string { return "TIME"; },
                              [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                              // NB: PostgreSQL doesn't have a TINYINT type, but it does have a SMALLINT type.
                              [](Tinyint const&) -> std::string { return "SMALLINT"; },
                              [](VarBinary const& /*type*/) -> std::string { return std::format("BYTEA"); },
                              [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
                          },
                          type);
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
                        return std::format(R"(ALTER TABLE "{}" ADD COLUMN "{}" {} {};)",
                                           tableName,
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [tableName, this](AlterColumn const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE "{0}" ALTER COLUMN "{1}" TYPE {2}, ALTER COLUMN "{1}" {3} NOT NULL;)",
                            tableName,
                            actualCommand.columnName,
                            ColumnType(actualCommand.columnType),
                            actualCommand.nullable == SqlNullable::NotNull ? "SET" : "DROP");
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
