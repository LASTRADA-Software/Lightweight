// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlConnection.hpp"
#include "SqlQuery/MigrationPlan.hpp"
#include "SqlServerType.hpp"

#include <string>
#include <string_view>

namespace Lightweight
{

struct SqlQualifiedTableColumnName;

/// API to format SQL queries for different SQL dialects.
class [[nodiscard]] LIGHTWEIGHT_API SqlQueryFormatter
{
  public:
    SqlQueryFormatter() = default;
    SqlQueryFormatter(SqlQueryFormatter&&) = default;
    SqlQueryFormatter(SqlQueryFormatter const&) = default;
    SqlQueryFormatter& operator=(SqlQueryFormatter&&) = default;
    SqlQueryFormatter& operator=(SqlQueryFormatter const&) = default;
    virtual ~SqlQueryFormatter() = default;

    /// Converts a boolean value to a string literal.
    [[nodiscard]] virtual std::string_view BooleanLiteral(bool value) const noexcept = 0;

    // Returns a function to get a date from the database
    [[nodiscard]] virtual std::string_view DateFunction() const noexcept = 0;


    /// Converts a string value to a string literal.
    [[nodiscard]] virtual std::string StringLiteral(std::string_view value) const noexcept = 0;

    /// Converts a character value to a string literal.
    [[nodiscard]] virtual std::string StringLiteral(char value) const noexcept = 0;

    /// Constructs an SQL INSERT query.
    ///
    /// @param intoTable The table to insert into.
    /// @param fields The fields to insert into.
    /// @param values The values to insert.
    ///
    /// The fields and values must be in the same order.
    [[nodiscard]] virtual std::string Insert(std::string_view intoTable,
                                             std::string_view fields,
                                             std::string_view values) const = 0;

    /// Retrieves the last insert ID of the given table.
    [[nodiscard]] virtual std::string QueryLastInsertId(std::string_view tableName) const = 0;

    /// Constructs an SQL SELECT query for all rows.
    [[nodiscard]] virtual std::string SelectAll(bool distinct,
                                                std::string_view fields,
                                                std::string_view fromTable,
                                                std::string_view fromTableAlias,
                                                std::string_view tableJoins,
                                                std::string_view whereCondition,
                                                std::string_view orderBy,
                                                std::string_view groupBy) const = 0;

    /// Constructs an SQL SELECT query for the first row.
    [[nodiscard]] virtual std::string SelectFirst(bool distinct,
                                                  std::string_view fields,
                                                  std::string_view fromTable,
                                                  std::string_view fromTableAlias,
                                                  std::string_view tableJoins,
                                                  std::string_view whereCondition,
                                                  std::string_view orderBy,
                                                  size_t count) const = 0;

    /// Constructs an SQL SELECT query for a range of rows.
    [[nodiscard]] virtual std::string SelectRange(bool distinct,
                                                  std::string_view fields,
                                                  std::string_view fromTable,
                                                  std::string_view fromTableAlias,
                                                  std::string_view tableJoins,
                                                  std::string_view whereCondition,
                                                  std::string_view orderBy,
                                                  std::string_view groupBy,
                                                  std::size_t offset,
                                                  std::size_t limit) const = 0;

    /// Constructs an SQL SELECT query retrieve the count of rows matching the given condition.
    [[nodiscard]] virtual std::string SelectCount(bool distinct,
                                                  std::string_view fromTable,
                                                  std::string_view fromTableAlias,
                                                  std::string_view tableJoins,
                                                  std::string_view whereCondition) const = 0;

    /// Constructs an SQL UPDATE query.
    [[nodiscard]] virtual std::string Update(std::string_view table,
                                             std::string_view tableAlias,
                                             std::string_view setFields,
                                             std::string_view whereCondition) const = 0;

    /// Constructs an SQL DELETE query.
    [[nodiscard]] virtual std::string Delete(std::string_view fromTable,
                                             std::string_view fromTableAlias,
                                             std::string_view tableJoins,
                                             std::string_view whereCondition) const = 0;

    using StringList = std::vector<std::string>;

    /// Convert the given column type definition to the SQL type.
    [[nodiscard]] virtual std::string ColumnType(SqlColumnTypeDefinition const& type) const = 0;

    /// Constructs an SQL CREATE TABLE query.
    [[nodiscard]] virtual StringList CreateTable(std::string_view tableName,
                                                 std::vector<SqlColumnDeclaration> const& columns) const = 0;

    /// Constructs an SQL ALTER TABLE query.
    [[nodiscard]] virtual StringList AlterTable(std::string_view tableName,
                                                std::vector<SqlAlterTableCommand> const& commands) const = 0;

    /// Constructs an SQL DROP TABLE query.
    [[nodiscard]] virtual StringList DropTable(std::string_view const& tableName) const = 0;

    /// Retrieves the SQL query formatter for SQLite.
    static SqlQueryFormatter const& Sqlite();

    /// Retrieves the SQL query formatter for Microsoft SQL server.
    static SqlQueryFormatter const& SqlServer();

    /// Retrieves the SQL query formatter for PostgreSQL.
    static SqlQueryFormatter const& PostgrSQL();

    /// Retrieves the SQL query formatter for Oracle database.
    static SqlQueryFormatter const& OracleSQL();

    /// Retrieves the SQL query formatter for the given SqlServerType.
    static SqlQueryFormatter const* Get(SqlServerType serverType) noexcept;
};

} // namespace Lightweight
