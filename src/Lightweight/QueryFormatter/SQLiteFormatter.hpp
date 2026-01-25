// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <format>

namespace Lightweight
{

class SQLiteQueryFormatter: public SqlQueryFormatter
{
  public:
    [[nodiscard]] std::string Insert(std::string_view intoTable,
                                     std::string_view fields,
                                     std::string_view values) const override
    {
        return std::format(R"(INSERT INTO "{}" ({}) VALUES ({}))", intoTable, fields, values);
    }

    [[nodiscard]] std::string Insert(std::string_view schema,
                                     std::string_view intoTable,
                                     std::string_view fields,
                                     std::string_view values) const override
    {
        if (schema.empty())
            return std::format(R"(INSERT INTO "{}" ({}) VALUES ({}))", intoTable, fields, values);
        return std::format(R"(INSERT INTO "{}"."{}" ({}) VALUES ({}))", schema, intoTable, fields, values);
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

    [[nodiscard]] std::string_view DateFunction() const noexcept override
    {
        return "date()";
    }

    [[nodiscard]] std::string StringLiteral(std::string_view value) const noexcept override
    {
        if (value.empty())
            return "''";

        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped += '\'';
        for (char const c: value)
        {
            if (c == '\'')
                escaped += "''";
            else
                escaped += c;
        }
        escaped += '\'';
        return escaped;
    }

    [[nodiscard]] std::string StringLiteral(char value) const noexcept override
    {
        if (value == '\'')
            return "''''";
        return std::format("'{}'", value);
    }

    [[nodiscard]] std::string BinaryLiteral(std::span<uint8_t const> data) const override
    {
        std::string result;
        result.reserve((data.size() * 2) + 3);
        result += "X'";
        for (uint8_t byte: data)
            result += std::format("{:02X}", byte);
        result += "'";
        return result;
    }

    [[nodiscard]] std::string SelectCount(bool distinct,
                                          std::string_view fromTable,
                                          std::string_view fromTableAlias,
                                          std::string_view tableJoins,
                                          std::string_view whereCondition) const override
    {
        if (fromTableAlias.empty())
            return std::format(
                R"(SELECT{} COUNT(*) FROM "{}"{}{})", distinct ? " DISTINCT" : "", fromTable, tableJoins, whereCondition);
        else
            return std::format(R"(SELECT{} COUNT(*) FROM "{}" AS "{}"{}{})",
                               distinct ? " DISTINCT" : "",
                               fromTable,
                               fromTableAlias,
                               tableJoins,
                               whereCondition);
    }

    [[nodiscard]] std::string SelectAll(bool distinct,
                                        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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
            return std::format(R"(DELETE FROM "{}" AS "{}"{}{})", fromTable, fromTableAlias, tableJoins, whereCondition);
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
        else if (column.primaryKey == SqlPrimaryKeyType::NONE && !column.index && column.unique)
            sqlQueryString << " UNIQUE";

        if (!column.defaultValue.empty())
            sqlQueryString << " DEFAULT " << column.defaultValue;

        return sqlQueryString.str();
    }

    [[nodiscard]] static std::string BuildForeignKeyConstraint(std::string_view tableName,
                                                               std::string_view columnName,
                                                               SqlForeignKeyReferenceDefinition const& referencedColumn)
    {
        return std::format(R"(CONSTRAINT {} FOREIGN KEY ("{}") REFERENCES "{}"("{}"))",
                           std::format("FK_{}_{}", tableName, columnName),
                           columnName,
                           referencedColumn.tableName,
                           referencedColumn.columnName);
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] StringList CreateTable(std::string_view schema,
                                         std::string_view tableName,
                                         std::vector<SqlColumnDeclaration> const& columns,
                                         std::vector<SqlCompositeForeignKeyConstraint> const& foreignKeys,
                                         bool ifNotExists = false) const override
    {
        auto sqlQueries = StringList {};

        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        sqlQueries.emplace_back([&]() {
            std::stringstream sqlQueryString;
            sqlQueryString << "CREATE TABLE ";
            if (ifNotExists)
                sqlQueryString << "IF NOT EXISTS ";
            sqlQueryString << "\"";
            if (!schema.empty())
            {
                sqlQueryString << schema;
                sqlQueryString << "\".\"";
            }
            sqlQueryString << tableName;
            sqlQueryString << "\" (";
            std::vector<SqlColumnDeclaration const*> pks;
            size_t currentColumn = 0;
            std::string foreignKeyConstraints;
            for (SqlColumnDeclaration const& column: columns)
            {
                if (currentColumn > 0)
                    sqlQueryString << ",";
                ++currentColumn;
                sqlQueryString << "\n    ";
                sqlQueryString << BuildColumnDefinition(column);

                if (column.primaryKey != SqlPrimaryKeyType::NONE)
                    pks.push_back(&column);

                if (column.foreignKey)
                {
                    foreignKeyConstraints += ",\n    ";
                    foreignKeyConstraints += BuildForeignKeyConstraint(tableName, column.name, *column.foreignKey);
                }
            }

            for (SqlCompositeForeignKeyConstraint const& fk: foreignKeys)
            {
                foreignKeyConstraints += ",\n    FOREIGN KEY (";
                for (size_t i = 0; i < fk.columns.size(); ++i)
                {
                    if (i > 0)
                        foreignKeyConstraints += ", ";
                    foreignKeyConstraints += '"' + fk.columns[i] + '"';
                }
                foreignKeyConstraints += ") REFERENCES \"";
                foreignKeyConstraints += fk.referencedTableName;
                foreignKeyConstraints += "\" (";
                for (size_t i = 0; i < fk.referencedColumns.size(); ++i)
                {
                    if (i > 0)
                        foreignKeyConstraints += ", ";
                    foreignKeyConstraints += '"' + fk.referencedColumns[i] + '"';
                }
                foreignKeyConstraints += ")";
            }

            // Filter out AUTO_INCREMENT from table-level PK constraint if it's the ONLY PK,
            // because SQLite handles it inline. But for composite keys involving auto-inc (if that's even valid/used),
            // or if we just want to be explicit.
            // SQLite restriction: "INTEGER PRIMARY KEY" must be on the column definition for auto-increment.
            // If we have AUTO_INCREMENT, it's already in BuildColumnDefinition.

            std::erase_if(
                pks, [](SqlColumnDeclaration const* col) { return col->primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT; });

            if (!pks.empty())
            {
                std::ranges::sort(pks, [](SqlColumnDeclaration const* a, SqlColumnDeclaration const* b) {
                    // If both have index, use it. If one has 0 (no index), treat it as "after" indexed ones?
                    // Or just rely on stable sort?
                    // Let's assume indices are properly set for composite keys.
                    // 1-based index vs 0. 0 means "unordered".
                    if (a->primaryKeyIndex != 0 && b->primaryKeyIndex != 0)
                        return a->primaryKeyIndex < b->primaryKeyIndex;
                    if (a->primaryKeyIndex != 0)
                        return true; // a comes first
                    if (b->primaryKeyIndex != 0)
                        return false; // b comes first
                    return false;     // keep original order
                });

                sqlQueryString << ",\n    PRIMARY KEY (";
                for (size_t i = 0; i < pks.size(); ++i)
                {
                    if (i > 0)
                        sqlQueryString << ", ";
                    sqlQueryString << '"' << pks[i]->name << '"';
                }
                sqlQueryString << ")";
            }

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
                    sqlQueries.emplace_back(std::format(
                        R"(CREATE INDEX "{}_{}_index" ON "{}"("{}");)", tableName, column.name, tableName, column.name));
            }
        }

        return sqlQueries;
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
                    [tableName](AddCompositeForeignKey const& /*actualCommand*/) -> std::string {
                        // SQLite limitation: ALTER TABLE ADD CONSTRAINT not supported.
                        // We return empty string or comment to satisfy visitor.
                        return std::format(R"(-- AddCompositeForeignKey not supported for {};)", tableName);
                    },
                    [schemaName, tableName, this](AddColumnIfNotExists const& actualCommand) -> std::string {
                        // SQLite doesn't support IF NOT EXISTS for ADD COLUMN.
                        // Generate a comment and the statement; caller should handle errors.
                        return std::format(
                            R"(-- AddColumnIfNotExists: SQLite doesn't support IF NOT EXISTS for columns
ALTER TABLE {} ADD COLUMN "{}" {} {};)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName,
                            ColumnType(actualCommand.columnType),
                            actualCommand.nullable == SqlNullable::NotNull ? "NOT NULL" : "NULL");
                    },
                    [schemaName, tableName](DropColumnIfExists const& actualCommand) -> std::string {
                        // SQLite doesn't support IF EXISTS for DROP COLUMN.
                        return std::format(
                            R"(-- DropColumnIfExists: SQLite doesn't support IF EXISTS for columns
ALTER TABLE {} DROP COLUMN "{}";)",
                            FormatTableName(schemaName, tableName),
                            actualCommand.columnName);
                    },
                    [tableName](DropIndexIfExists const& actualCommand) -> std::string {
                        return std::format(R"(DROP INDEX IF EXISTS "{0}_{1}_index";)", tableName, actualCommand.columnName);
                    },
                },
                command);
        }

        return { sqlQueryString.str() };
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(detail::overloaded {
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

    [[nodiscard]] StringList DropTable(std::string_view schemaName,
                                       std::string_view const& tableName,
                                       bool ifExists = false,
                                       bool cascade = false) const override
    {
        // SQLite doesn't support CASCADE syntax, but if FK constraints are disabled
        // (PRAGMA foreign_keys = OFF), dropping works. The cascade flag is ignored.
        (void) cascade;
        if (ifExists)
            return { std::format(R"(DROP TABLE IF EXISTS {};)", FormatTableName(schemaName, tableName)) };
        else
            return { std::format(R"(DROP TABLE {};)", FormatTableName(schemaName, tableName)) };
    }

    [[nodiscard]] std::string QueryServerVersion() const override
    {
        return "SELECT sqlite_version()";
    }
};

} // namespace Lightweight
