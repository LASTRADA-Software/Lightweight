// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <format>

class SQLiteQueryFormatter: public SqlQueryFormatter
{
  public:
    [[nodiscard]] std::string Insert(std::string_view intoTable,
                                     std::string_view fields,
                                     std::string_view values) const override
    {
        return std::format(R"(INSERT INTO "{}" ({}) VALUES ({}))", intoTable, fields, values);
    }

    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // This is SQLite syntax. We might want to provide aspecialized SQLite class instead.
        return "SELECT LAST_INSERT_ROWID()";
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "TRUE" : "FALSE";
    }

    [[nodiscard]] std::string StringLiteral(std::string_view value) const noexcept override
    {
        // TODO: Implement escaping of special characters.
        return std::format("'{}'", value);
    }

    [[nodiscard]] std::string StringLiteral(char value) const noexcept override
    {
        // TODO: Implement escaping of special characters.
        return std::format("'{}'", value);
    }

    [[nodiscard]] std::string SelectCount(bool distinct,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition) const override
    {
        if (fromTableAlias.empty())
            return std::format(R"(SELECT{} COUNT(*) FROM "{}"{}{})",
                               distinct ? " DISTINCT" : "",
                               fromTable,
                               tableJoins,
                               whereCondition);
        else
            return std::format(R"(SELECT{} COUNT(*) FROM "{}" AS "{}"{}{})",
                               distinct ? " DISTINCT" : "",
                               fromTable,
                               fromTableAlias,
                               tableJoins,
                               whereCondition);
    }

    [[nodiscard]] std::string SelectAll(bool distinct,
                                        std::string_view fields,
                                        std::string_view fromTable,
                                        std::string_view fromTableAlias,
                                        std::string_view tableJoins,
                                        std::string_view whereCondition,
                                        std::string_view orderBy,
                                        std::string_view groupBy) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT ";
        if (distinct)
            sqlQueryString << "DISTINCT ";
        sqlQueryString << fields;
        sqlQueryString << " FROM \"" << fromTable << '"';
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << '"';
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << groupBy;
        sqlQueryString << orderBy;

        return sqlQueryString.str();
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
        sqlQueryString << "SELECT " << fields;
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " FROM \"" << fromTable << "\"";
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << "\"";
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << orderBy;
        sqlQueryString << " LIMIT " << count;
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
        sqlQueryString << " LIMIT " << limit << " OFFSET " << offset;
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string Update(std::string_view table,
                                     std::string_view tableAlias,
                                     std::string_view setFields,
                                     std::string_view whereCondition) const override
    {
        if (tableAlias.empty())
            return std::format(R"(UPDATE "{}" SET {}{})", table, setFields, whereCondition);
        else
            return std::format(R"(UPDATE "{}" AS "{}" SET {}{})", table, tableAlias, setFields, whereCondition);
    }

    [[nodiscard]] std::string Delete(std::string_view fromTable,
                                     std::string_view fromTableAlias,
                                     std::string_view tableJoins,
                                     std::string_view whereCondition) const override
    {
        if (fromTableAlias.empty())
            return std::format(R"(DELETE FROM "{}"{}{})", fromTable, tableJoins, whereCondition);
        else
            return std::format(
                R"(DELETE FROM "{}" AS "{}"{}{})", fromTable, fromTableAlias, tableJoins, whereCondition);
    }

    [[nodiscard]] virtual std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const
    {
        std::stringstream sqlQueryString;

        sqlQueryString << '"' << column.name << "\" ";

        if (column.primaryKey != SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << ColumnType(column.type);
        else
            sqlQueryString << ColumnType(SqlColumnTypeDefinitions::Integer {});

        if (column.required)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " PRIMARY KEY AUTOINCREMENT";
        else if (column.unique && !column.index)
            sqlQueryString << " UNIQUE";

        return sqlQueryString.str();
    }

    [[nodiscard]] static std::string BuildForeignKeyConstraint(std::string_view columnName,
                                                               SqlForeignKeyReferenceDefinition const& referencedColumn)
    {
        return std::format(R"(CONSTRAINT {} FOREIGN KEY ("{}") REFERENCES "{}"("{}"))",
                           std::format("FK_{}", columnName),
                           columnName,
                           referencedColumn.tableName,
                           referencedColumn.columnName);
    }

    [[nodiscard]] StringList CreateTable(std::string_view tableName,
                                         std::vector<SqlColumnDeclaration> const& columns) const override
    {
        auto sqlQueries = StringList {};

        sqlQueries.emplace_back([&]() {
            std::stringstream sqlQueryString;
            sqlQueryString << "CREATE TABLE \"" << tableName << "\" (";
            size_t currentColumn = 0;
            std::string primaryKeyColumns;
            std::string foreignKeyConstraints;
            for (SqlColumnDeclaration const& column: columns)
            {
                if (currentColumn > 0)
                    sqlQueryString << ",";
                ++currentColumn;
                sqlQueryString << "\n    ";
                sqlQueryString << BuildColumnDefinition(column);
                if (column.primaryKey == SqlPrimaryKeyType::MANUAL)
                {
                    if (!primaryKeyColumns.empty())
                        primaryKeyColumns += ", ";
                    primaryKeyColumns += '"';
                    primaryKeyColumns += column.name;
                    primaryKeyColumns += '"';
                }
                if (column.foreignKey)
                {
                    foreignKeyConstraints += ",\n    ";
                    foreignKeyConstraints += BuildForeignKeyConstraint(column.name, *column.foreignKey);
                }
            }
            if (!primaryKeyColumns.empty())
                sqlQueryString << ",\n    PRIMARY KEY (" << primaryKeyColumns << ")";

            sqlQueryString << foreignKeyConstraints;

            sqlQueryString << "\n);";
            return sqlQueryString.str();
        }());

        for (SqlColumnDeclaration const& column: columns)
        {
            if (column.index && column.primaryKey == SqlPrimaryKeyType::NONE)
            {
                // primary keys are always indexed
                if (column.unique)
                    sqlQueries.emplace_back(std::format(R"(CREATE UNIQUE INDEX "{}_{}_index" ON "{}"("{}");)",
                                                        tableName,
                                                        column.name,
                                                        tableName,
                                                        column.name));
                else
                    sqlQueries.emplace_back(std::format(R"(CREATE INDEX "{}_{}_index" ON "{}"("{}");)",
                                                        tableName,
                                                        column.name,
                                                        tableName,
                                                        column.name));
            }
        }

        return sqlQueries;
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

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(
            detail::overloaded {
                [](Bigint const&) -> std::string { return "BIGINT"; },
                [](Binary const&) -> std::string { return "BLOB"; },
                [](Bool const&) -> std::string { return "BOOLEAN"; },
                [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                [](Date const&) -> std::string { return "DATE"; },
                [](DateTime const&) -> std::string { return "DATETIME"; },
                [](Decimal const& type) -> std::string {
                    return std::format("DECIMAL({}, {})", type.precision, type.scale);
                },
                [](Guid const&) -> std::string { return "GUID"; },
                [](Integer const&) -> std::string { return "INTEGER"; },
                [](NChar const& type) -> std::string { return std::format("NCHAR({})", type.size); },
                [](NVarchar const& type) -> std::string { return std::format("NVARCHAR({})", type.size); },
                [](Real const&) -> std::string { return "REAL"; },
                [](Smallint const&) -> std::string { return "SMALLINT"; },
                [](Text const&) -> std::string { return "TEXT"; },
                [](Time const&) -> std::string { return "TIME"; },
                [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                [](Tinyint const&) -> std::string { return "TINYINT"; },
                [](VarBinary const& type) -> std::string { return std::format("VARBINARY({})", type.size); },
                [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
            },
            type);
    }

    [[nodiscard]] StringList DropTable(std::string_view const& tableName) const override
    {
        return { std::format(R"(DROP TABLE "{}";)", tableName) };
    }
};
