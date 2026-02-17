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
    /// Default constructor.
    SqlQueryFormatter() = default;
    /// Default move constructor.
    SqlQueryFormatter(SqlQueryFormatter&&) = default;
    /// Default copy constructor.
    SqlQueryFormatter(SqlQueryFormatter const&) = default;
    /// Default move assignment operator.
    SqlQueryFormatter& operator=(SqlQueryFormatter&&) = default;
    /// Default copy assignment operator.
    SqlQueryFormatter& operator=(SqlQueryFormatter const&) = default;
    virtual ~SqlQueryFormatter() = default;

    /// Converts a boolean value to a string literal.
    [[nodiscard]] virtual std::string_view BooleanLiteral(bool value) const noexcept = 0;

    /// Returns the SQL function name used to retrieve the current date.
    [[nodiscard]] virtual std::string_view DateFunction() const noexcept = 0;

    /// Converts a string value to a string literal.
    [[nodiscard]] virtual std::string StringLiteral(std::string_view value) const noexcept = 0;

    /// Converts a character value to a string literal.
    [[nodiscard]] virtual std::string StringLiteral(char value) const noexcept = 0;

    /// Converts a binary value to a hex-encoded string literal.
    [[nodiscard]] virtual std::string BinaryLiteral(std::span<uint8_t const> data) const = 0;

    /// Formats a qualified table name with proper quoting for this database.
    /// @param schema The schema name (can be empty for default schema)
    /// @param table The table name
    /// @return The properly quoted qualified table name (e.g., "schema"."table" or [schema].[table])
    [[nodiscard]] virtual std::string QualifiedTableName(std::string_view schema, std::string_view table) const = 0;

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

    /// Constructs an SQL INSERT query with a schema prefix.
    [[nodiscard]] virtual std::string Insert(std::string_view schema,
                                             std::string_view intoTable,
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

    /// Alias for a list of SQL statement strings.
    using StringList = std::vector<std::string>;

    /// Convert the given column type definition to the SQL type.
    [[nodiscard]] virtual std::string ColumnType(SqlColumnTypeDefinition const& type) const = 0;

    /// Constructs an SQL CREATE TABLE query.
    ///
    /// @param schema The schema name of the table to create.
    /// @param tableName The name of the table to create.
    /// @param columns The columns of the table.
    /// @param foreignKeys The foreign key constraints of the table.
    /// @param ifNotExists If true, generates CREATE TABLE IF NOT EXISTS instead of CREATE TABLE.
    [[nodiscard]] virtual StringList CreateTable(std::string_view schema,
                                                 std::string_view tableName,
                                                 std::vector<SqlColumnDeclaration> const& columns,
                                                 std::vector<SqlCompositeForeignKeyConstraint> const& foreignKeys,
                                                 bool ifNotExists = false) const = 0;

    /// Constructs an SQL ALTER TABLE query.
    [[nodiscard]] virtual StringList AlterTable(std::string_view schema,
                                                std::string_view tableName,
                                                std::vector<SqlAlterTableCommand> const& commands) const = 0;

    /// Constructs an SQL DROP TABLE query.
    ///
    /// @param schema The schema name of the table to drop.
    /// @param tableName The name of the table to drop.
    /// @param ifExists If true, generates DROP TABLE IF EXISTS instead of DROP TABLE.
    /// @param cascade If true, drops all foreign key constraints referencing this table first.
    [[nodiscard]] virtual StringList DropTable(std::string_view schema,
                                               std::string_view const& tableName,
                                               bool ifExists = false,
                                               bool cascade = false) const = 0;

    /// Returns the SQL query to retrieve the full server version string.
    ///
    /// This query returns detailed version information specific to each database:
    /// - SQL Server: Returns result of SELECT @@VERSION (includes build, edition, OS info)
    /// - PostgreSQL: Returns result of SELECT version() (includes build info)
    /// - SQLite: Returns result of SELECT sqlite_version()
    [[nodiscard]] virtual std::string QueryServerVersion() const = 0;

    /// Retrieves the SQL query formatter for SQLite.
    static SqlQueryFormatter const& Sqlite();

    /// Retrieves the SQL query formatter for Microsoft SQL server.
    static SqlQueryFormatter const& SqlServer();

    /// Retrieves the SQL query formatter for PostgreSQL.
    static SqlQueryFormatter const& PostgrSQL();

    /// Retrieves the SQL query formatter for the given SqlServerType.
    static SqlQueryFormatter const* Get(SqlServerType serverType) noexcept;

  protected:
    /// Formats a table name with optional schema prefix.
    static std::string FormatTableName(std::string_view schema, std::string_view table);
};

} // namespace Lightweight
