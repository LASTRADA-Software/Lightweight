// SPDX-License-Identifier: Apache-2.0

#include "SqlQueryFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <concepts>
#include <format>

using namespace std::string_view_literals;

namespace
{

class BasicSqlQueryFormatter: public SqlQueryFormatter
{
  public:
    [[nodiscard]] std::string Insert(std::string const& intoTable,
                                     std::string const& fields,
                                     std::string const& values) const override
    {
        return std::format(R"(INSERT INTO "{}" ({}) VALUES ({}))", intoTable, fields, values);
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

    [[nodiscard]] std::string CreateTable(std::string_view tableName,
                                          std::vector<SqlColumnDeclaration> const& columns) const override
    {
        std::stringstream sqlQueryString;

        sqlQueryString << "CREATE TABLE \"" << tableName << "\" (";

        size_t currentColumn = 0;
        std::string primaryKeyColumns;
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
        }

        if (!primaryKeyColumns.empty())
            sqlQueryString << ",\n    PRIMARY KEY (" << primaryKeyColumns << ")";

        sqlQueryString << "\n);";

        for (SqlColumnDeclaration const& column: columns)
        {
            if (column.index && column.primaryKey == SqlPrimaryKeyType::NONE)
            {
                // primary keys are always indexed
                if (column.unique)
                    sqlQueryString << std::format("\nCREATE UNIQUE INDEX \"{}_{}_index\" ON \"{}\"(\"{}\");",
                                                  tableName,
                                                  column.name,
                                                  tableName,
                                                  column.name);
                else
                    sqlQueryString << std::format("\nCREATE INDEX \"{}_{}_index\" ON \"{}\"(\"{}\");",
                                                  tableName,
                                                  column.name,
                                                  tableName,
                                                  column.name);
            }
        }

        return sqlQueryString.str();
    }

    [[nodiscard]] std::string AlterTable(std::string_view tableName,
                                         std::vector<SqlAlterTableCommand> const& commands) const override
    {
        std::stringstream sqlQueryString;

        int currentCommand = 0;
        for (SqlAlterTableCommand const& command: commands)
        {
            if (currentCommand > 0)
                sqlQueryString << '\n';
            ++currentCommand;

            sqlQueryString << std::visit(
                [this, tableName](auto const& actualCommand) -> std::string {
                    using Type = std::decay_t<decltype(actualCommand)>;
                    if constexpr (std::same_as<Type, SqlAlterTableCommands::RenameTable>)
                    {
                        return std::format(
                            R"(ALTER TABLE "{}" RENAME TO "{}";)", tableName, actualCommand.newTableName);
                    }
                    else if constexpr (std::same_as<Type, SqlAlterTableCommands::AddColumn>)
                    {
                        return std::format(R"(ALTER TABLE "{}" ADD COLUMN "{}" {};)",
                                           tableName,
                                           actualCommand.columnName,
                                           ColumnType(actualCommand.columnType));
                    }
                    else if constexpr (std::same_as<Type, SqlAlterTableCommands::RenameColumn>)
                    {
                        return std::format(R"(ALTER TABLE "{}" RENAME COLUMN "{}" TO "{}";)",
                                           tableName,
                                           actualCommand.oldColumnName,
                                           actualCommand.newColumnName);
                    }
                    else if constexpr (std::same_as<Type, SqlAlterTableCommands::DropColumn>)
                    {
                        return std::format(
                            R"(ALTER TABLE "{}" DROP COLUMN "{}";)", tableName, actualCommand.columnName);
                    }
                    else if constexpr (std::same_as<Type, SqlAlterTableCommands::AddIndex>)
                    {
                        auto const uniqueStr = actualCommand.unique ? "UNIQUE "sv : ""sv;
                        return std::format(R"(CREATE {2}INDEX "{0}_{1}_index" ON "{0}"("{1}");)",
                                           tableName,
                                           actualCommand.columnName,
                                           uniqueStr);
                    }
                    else if constexpr (std::same_as<Type, SqlAlterTableCommands::DropIndex>)
                    {
                        return std::format(R"(DROP INDEX "{0}_{1}_index";)", tableName, actualCommand.columnName);
                    }
                    else
                    {
                        throw std::runtime_error(
                            std::format("Unknown alter table command: {}", Reflection::TypeName<Type>));
                    }
                },
                command);
        }

        return sqlQueryString.str();
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(
            [](auto const& actualType) -> std::string {
                using Type = std::decay_t<decltype(actualType)>;
                if constexpr (std::same_as<Type, Bigint>)
                    return "BIGINT";
                else if constexpr (std::same_as<Type, Bool>)
                    return "BOOLEAN";
                else if constexpr (std::same_as<Type, Char>)
                    return std::format("CHAR({})", actualType.size);
                else if constexpr (std::same_as<Type, Date>)
                    return "DATE";
                else if constexpr (std::same_as<Type, DateTime>)
                    return "DATETIME";
                else if constexpr (std::same_as<Type, Decimal>)
                    return std::format("DECIMAL({}, {})", actualType.precision, actualType.scale);
                else if constexpr (std::same_as<Type, Guid>)
                    return "GUID";
                else if constexpr (std::same_as<Type, Integer>)
                    return "INTEGER";
                else if constexpr (std::same_as<Type, Real>)
                    return "REAL";
                else if constexpr (std::same_as<Type, Smallint>)
                    return "SMALLINT";
                else if constexpr (std::same_as<Type, Text>)
                    return "TEXT";
                else if constexpr (std::same_as<Type, Time>)
                    return "TIME";
                else if constexpr (std::same_as<Type, Timestamp>)
                    return "TIMESTAMP";
                else if constexpr (std::same_as<Type, Varchar>)
                    return std::format("VARCHAR({})", actualType.size);
                else
                    throw std::runtime_error(std::format("Unknown column type: {}", Reflection::TypeName<Type>));
            },
            type);
    }

    [[nodiscard]] std::string DropTable(std::string_view const& tableName) const override
    {
        return std::format(R"(DROP TABLE "{}";)", tableName);
    }
};

class SqlServerQueryFormatter final: public BasicSqlQueryFormatter
{
  public:
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
            [this, type](auto const& actualType) -> std::string {
                using Type = std::decay_t<decltype(actualType)>;
                if constexpr (std::same_as<Type, Bool>)
                    return "BIT";
                else if constexpr (std::same_as<Type, Guid>)
                    return "UNIQUEIDENTIFIER";
                else if constexpr (std::same_as<Type, Text>)
                    return "VARCHAR(MAX)";
                else
                    return BasicSqlQueryFormatter::ColumnType(type);
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
};

class PostgreSqlFormatter final: public BasicSqlQueryFormatter
{
  public:
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
        return std::visit(
            [this, type](auto const& actualType) -> std::string {
                using Type = std::decay_t<decltype(actualType)>;
                if constexpr (std::same_as<Type, Guid>)
                    return "UUID";
                else if constexpr (std::same_as<Type, DateTime>)
                    return "TIMESTAMP";
                else
                    return BasicSqlQueryFormatter::ColumnType(type);
            },
            type);
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
    return SqlServer(); // So far, Oracle SQL is similar to Microsoft SQL Server.
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
