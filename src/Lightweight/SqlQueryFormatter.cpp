// SPDX-License-Identifier: Apache-2.0

#include "SqlQueryFormatter.hpp"
#include "Utils.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <concepts>
#include <format>
#include <type_traits>

using namespace std::string_view_literals;

namespace
{

class BasicSqlQueryFormatter;
class PostgreSqlFormatter;
class SqlServerQueryFormatter;
class OracleSqlQueryFormatter;

class BasicSqlQueryFormatter: public SqlQueryFormatter
{
  public:
    [[nodiscard]] std::string Insert(std::string const& intoTable,
                                     std::string const& fields,
                                     std::string const& values) const override
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
        return literalValue ? "TRUE"sv : "FALSE"sv;
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
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition) const override
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
                                        std::string const& fields,
                                        std::string const& fromTable,
                                        std::string const& fromTableAlias,
                                        std::string const& tableJoins,
                                        std::string const& whereCondition,
                                        std::string const& orderBy,
                                        std::string const& groupBy) const override
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
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
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
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
                                          std::string const& groupBy,
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

    [[nodiscard]] std::string Update(std::string const& table,
                                     std::string const& tableAlias,
                                     std::string const& setFields,
                                     std::string const& whereCondition) const override
    {
        if (tableAlias.empty())
            return std::format(R"(UPDATE "{}" SET {}{})", table, setFields, whereCondition);
        else
            return std::format(R"(UPDATE "{}" AS "{}" SET {}{})", table, tableAlias, setFields, whereCondition);
    }

    [[nodiscard]] std::string Delete(std::string const& fromTable,
                                     std::string const& fromTableAlias,
                                     std::string const& tableJoins,
                                     std::string const& whereCondition) const override
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

    [[nodiscard]] static std::string BuildForeignKeyConstraint(std::string const& columnName,
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
                [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
            },
            type);
    }

    [[nodiscard]] StringList DropTable(std::string_view const& tableName) const override
    {
        return { std::format(R"(DROP TABLE "{}";)", tableName) };
    }
};

class SqlServerQueryFormatter final: public BasicSqlQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view /*tableName*/) const override
    {
        // TODO: Figure out how to get the last insert id in SQL Server for a given table.
        return std::format("SELECT @@IDENTITY");
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1"sv : "0"sv;
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
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
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
                                          std::string const& groupBy,
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
                [](NVarchar const& type) -> std::string { return std::format("NVARCHAR({})", type.size); },
                [](Real const&) -> std::string { return "REAL"; },
                [](Smallint const&) -> std::string { return "SMALLINT"; },
                [](Text const&) -> std::string { return "VARCHAR(MAX)"; },
                [](Time const&) -> std::string { return "TIME"; },
                [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
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

        if (column.unique && !column.index)
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

class OracleSqlQueryFormatter final: public BasicSqlQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view tableName) const override
    {
        return std::format("SELECT \"{}_SEQ\".CURRVAL FROM DUAL;", tableName);
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1"sv : "0"sv;
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
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
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
                                          std::string const& groupBy,
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
                [](Bigint const&) -> std::string { return "NUMBER(19, 0)"; },
                [](Binary const& type) -> std::string {
                    if (type.size)
                        return std::format("BLOB({})", type.size);
                    else
                        return "BLOB";
                },
                [](Bool const&) -> std::string { return "BIT"; },
                [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                [](Date const&) -> std::string { return "DATE"; },
                [](DateTime const&) -> std::string { return "TIMESTAMP"; },
                [](Decimal const& type) -> std::string {
                    return std::format("DECIMAL({}, {})", type.precision, type.scale);
                },
                [](Guid const&) -> std::string { return "RAW(16)"; },
                [](Integer const&) -> std::string { return "INTEGER"; },
                [](NChar const& type) -> std::string { return std::format("NCHAR({})", type.size); },
                [](NVarchar const& type) -> std::string { return std::format("NVARCHAR2({})", type.size); },
                [](Real const&) -> std::string { return "REAL"; },
                [](Smallint const&) -> std::string { return "SMALLINT"; },
                [](Text const& type) -> std::string {
                    if (type.size <= 4000)
                        return std::format("VARCHAR2({})", type.size);
                    else
                        return "CLOB";
                },
                [](Time const&) -> std::string { return "TIMESTAMP"; },
                [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
            },
            type);
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << '"' << column.name << "\" " << ColumnType(column.type);

        if (column.required && column.primaryKey != SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " GENERATED ALWAYS AS IDENTITY";
        else if (column.unique && !column.index)
            sqlQueryString << " UNIQUE";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
        {
            sqlQueryString << ",\n    PRIMARY KEY (\"" << column.name << "\")";
        }
        return sqlQueryString.str();
    }
};

class PostgreSqlFormatter final: public BasicSqlQueryFormatter
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

        if (column.unique && !column.index)
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

} // namespace

SqlQueryFormatter const& SqlQueryFormatter::Sqlite()
{
    static const BasicSqlQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::SqlServer()
{
    static const SqlServerQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::PostgrSQL()
{
    static const PostgreSqlFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::OracleSQL()
{
    static const OracleSqlQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const* SqlQueryFormatter::Get(SqlServerType serverType) noexcept
{
    switch (serverType)
    {
        case SqlServerType::SQLITE:
            return &Sqlite();
        case SqlServerType::MICROSOFT_SQL:
            return &SqlServer();
        case SqlServerType::POSTGRESQL:
            return &PostgrSQL();
        case SqlServerType::ORACLE:
            return &OracleSQL();
        case SqlServerType::MYSQL: // TODO
        case SqlServerType::UNKNOWN:
            break;
    }
    return nullptr;
}
