// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <format>

namespace Lightweight
{

class PostgreSqlFormatter final: public SQLiteQueryFormatter
{
  public:
    using SQLiteQueryFormatter::CreateTable;

    [[nodiscard]] StringList DropTable(std::string_view schemaName,
                                       std::string_view const& tableName,
                                       bool ifExists = false,
                                       bool cascade = false) const override
    {
        std::string sql = ifExists ? std::format("DROP TABLE IF EXISTS {}", FormatTableName(schemaName, tableName))
                                   : std::format("DROP TABLE {}", FormatTableName(schemaName, tableName));
        if (cascade)
            sql += " CASCADE";
        sql += ";";
        return { sql };
    }

    [[nodiscard]] std::string BinaryLiteral(std::span<uint8_t const> data) const override
    {
        std::string result;
        result.reserve((data.size() * 2) + 4);
        result += "'\\x";
        for (uint8_t byte: data)
            result += std::format("{:02X}", byte);
        result += "'";
        return result;
    }

    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // NB: Find a better way to do this on the given table.
        // In our case it works, because we're expected to call this right after an insert.
        // But a race condition may still happen if another client inserts a row at the same time too.
        return std::format("SELECT lastval();");
    }

    [[nodiscard]] std::string_view DateFunction() const noexcept override
    {
        return "CURRENT_DATE";
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;

        sqlQueryString << '"' << column.name << "\" ";

        // Detect PostgreSQL auto-increment columns by checking for nextval() in default value.
        // This handles restore of backed-up tables where SERIAL columns have their default
        // value captured as nextval('"TableName_id_seq"'::regclass).
        bool const isAutoIncrementViaDefault = column.defaultValue.find("nextval(") != std::string::npos;
        bool const isAutoIncrement = column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT || isAutoIncrementViaDefault;

        if (isAutoIncrement)
            sqlQueryString << "SERIAL";
        else
            sqlQueryString << ColumnType(column.type);

        if (column.required)
            sqlQueryString << " NOT NULL";

        // Only add inline PRIMARY KEY for explicitly marked AUTO_INCREMENT columns.
        // For columns detected via nextval() default, the table-level PRIMARY KEY constraint
        // will handle it to avoid "multiple primary keys" error.
        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " PRIMARY KEY";
        else if (column.primaryKey == SqlPrimaryKeyType::NONE && !column.index && column.unique)
            sqlQueryString << " UNIQUE";

        // Don't output default value for auto-increment columns as SERIAL handles it
        if (!column.defaultValue.empty() && !isAutoIncrement)
            sqlQueryString << " DEFAULT " << column.defaultValue;

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
                              [](NVarchar const& type) -> std::string {
                                  if (type.size == 0)
                                      return "TEXT";
                                  return std::format("VARCHAR({})", type.size);
                              },
                              [](Real const&) -> std::string { return "REAL"; },
                              [](Smallint const&) -> std::string { return "SMALLINT"; },
                              [](Text const&) -> std::string { return "TEXT"; },
                              [](Time const&) -> std::string { return "TIME"; },
                              [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                              // NB: PostgreSQL doesn't have a TINYINT type, but it does have a SMALLINT type.
                              [](Tinyint const&) -> std::string { return "SMALLINT"; },
                              [](VarBinary const& /*type*/) -> std::string { return std::format("BYTEA"); },
                              [](Varchar const& type) -> std::string {
                                  if (type.size == 0)
                                      return "TEXT";
                                  return std::format("VARCHAR({})", type.size);
                              },
                          },
                          type);
    }

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
                        return std::format(R"(ALTER TABLE {} ADD COLUMN "{}" {} {};)",
                                           FormatTableName(schemaName, tableName),
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType),
                                           actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName, this](AlterColumn const& actualCommand) -> std::string {
                        return std::format(
                            R"(ALTER TABLE {0} ALTER COLUMN "{1}" TYPE {2}, ALTER COLUMN "{1}" {3} NOT NULL;)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            ColumnType(actualCommand.columnType),
                            actualCommand.nullable == SqlNullable::NotNull ? "SET" : "DROP");
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
                            BuildForeignKeyConstraint(actualCommand.columnName, actualCommand.referencedColumn));
                    },
                    [schemaName, tableName](DropForeignKey const& actualCommand) -> std::string {
                        return std::format(R"(ALTER TABLE {} DROP CONSTRAINT "{}";)",
                                           FormatTableName(schemaName, tableName),
                                           std::format("FK_{}", actualCommand.columnName));
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
                },
                command);
        }

        return { sqlQueryString.str() };
    }

    [[nodiscard]] std::string QueryServerVersion() const override
    {
        return "SELECT version()";
    }
};

} // namespace Lightweight
