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

class SqlAdvisoryLockHandler;
class SqlRetryClassifier;

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
    ///
    /// When @p groupBy is non-empty it is emitted between the WHERE and the ORDER BY clause, so
    /// the row limit given by @p count applies to the grouped result set.
    [[nodiscard]] virtual std::string SelectFirst(bool distinct,
                                                  std::string_view fields,
                                                  std::string_view fromTable,
                                                  std::string_view fromTableAlias,
                                                  std::string_view tableJoins,
                                                  std::string_view whereCondition,
                                                  std::string_view orderBy,
                                                  std::string_view groupBy,
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
    ///
    /// When @p groupBy is non-empty the query counts the rows of each group, i.e. it yields one
    /// row per group rather than a single total.
    [[nodiscard]] virtual std::string SelectCount(bool distinct,
                                                  std::string_view fromTable,
                                                  std::string_view fromTableAlias,
                                                  std::string_view tableJoins,
                                                  std::string_view whereCondition,
                                                  std::string_view groupBy) const = 0;

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

    /// @brief Whether the dialect must rebuild a table to apply an `ALTER TABLE` schema change that it
    /// cannot express in place — adding/dropping a foreign-key constraint, or altering a column's
    /// type/nullability (`ALTER TABLE … ADD/DROP CONSTRAINT` / `… ALTER COLUMN`).
    ///
    /// SQLite returns `true`; every other backend defaults to `false`. The migration executor consults
    /// this (via @ref SqlConnection::RequiresTableRebuildForSchemaChange) to decide whether to take the
    /// table-rebuild path for the `-- LIGHTWEIGHT_SQLITE_GUARD:` sentinels these dialects emit.
    [[nodiscard]] virtual bool RequiresTableRebuildForSchemaChange() const noexcept
    {
        return false;
    }

    /// @brief The per-table hint that asks for a dirty read (READ UNCOMMITTED) of one table in a SELECT.
    ///
    /// Rendered right after a table reference (and its alias) in the FROM clause and in each JOIN of a
    /// query built with @ref SqlSelectQueryBuilder::ReadUncommitted. Only that statement is affected;
    /// the connection's transaction isolation level is left alone.
    ///
    /// Defaults to an empty string, meaning the dialect has no per-table hint and renders the
    /// statement unchanged. SQL Server returns `" WITH (READUNCOMMITTED)"`.
    ///
    /// @return The hint text including its leading space, or an empty string. The text must stay
    ///         valid for as long as the formatter does.
    [[nodiscard]] virtual std::string_view ReadUncommittedTableHint() const noexcept
    {
        return {};
    }

    /// @brief Whether the dialect provides a batched, whole-database schema-introspection
    /// fast path that `SqlSchema::ReadAllTables` can use instead of the per-table ODBC
    /// catalog loop.
    ///
    /// Defaults to `false`, meaning the generic per-table catalog reader is used.
    /// SQL Server returns `true`: it can answer the entire schema with a handful of
    /// `sys.*` queries, collapsing thousands of per-table round-trips into a few.
    /// The batched path must produce byte-identical schema metadata to the legacy path.
    ///
    /// @return `true` if the dialect supports the batched fast path, `false` otherwise.
    [[nodiscard]] virtual bool SupportsBatchedSchemaIntrospection() const noexcept
    {
        return false;
    }

    /// @brief Whether the dialect treats narrow (`CHAR`/`VARCHAR`) and wide
    /// (`NCHAR`/`NVARCHAR`) text as distinct types — distinct enough that a foreign key
    /// between them is rejected and changing a column between them needs its dependent
    /// constraints and indexes dropped first.
    ///
    /// Defaults to `false` (PostgreSQL and SQLite store both the same way). SQL Server
    /// returns `true`.
    ///
    /// @return `true` if narrow and wide text are distinct types in this dialect.
    [[nodiscard]] virtual bool DistinguishesNarrowAndWideText() const noexcept
    {
        return false;
    }

    /// @brief Largest number of values this dialect should be handed in a single `WHERE ... IN (...)`
    /// predicate.
    ///
    /// The batched relation loading behind `Query<Record>().With<&Record::relation>()` resolves a whole
    /// result set through `IN` predicates over the collected keys. Those keys are rendered as literals,
    /// so a large batch would otherwise produce one enormous expression — SQL Server raises
    /// "an expression services limit has been reached", and SQLite has its own parser limits. Splitting
    /// the batch into chunks of this size keeps every generated statement inside what the dialect
    /// accepts; the cost is one extra round-trip per chunk, still a constant number of queries per
    /// relation instead of one per record.
    ///
    /// @return The maximum number of values per `IN` predicate. Defaults to 1000 for every dialect.
    [[nodiscard]] virtual size_t MaxInPredicateValues() const noexcept
    {
        return 1000;
    }

    /// @brief Builds the canonical foreign-key constraint name for a set of columns.
    ///
    /// Produces `FK_<table>_<col1>[_<col2>…]`. A single-column FK collapses to
    /// `FK_<table>_<col>`; a composite FK includes every column so that a composite
    /// FK whose first column matches an existing single-column FK doesn't collide
    /// on the constraint name (which MSSQL enforces as globally unique per DB).
    ///
    /// Consumers include the SQL Server, PostgreSQL and SQLite formatters —
    /// centralising the convention here keeps CREATE/ALTER emission in sync with
    /// DROP CONSTRAINT lookup (e.g. `SQLiteRebuildDropForeignKey`).
    template <typename Range>
    [[nodiscard]] static std::string BuildForeignKeyConstraintName(std::string_view tableName, Range const& columns)
    {
        std::string name { "FK_" };
        name.append(tableName);
        for (auto const& col: columns)
        {
            name.push_back('_');
            name.append(col);
        }
        return name;
    }

    /// @brief Returns the SQL statement to execute after connect to make
    /// `schema` the connection-level default for unqualified DDL/DML.
    ///
    /// Returns an empty string when the DBMS has no session-level concept of a
    /// default schema (SQL Server, SQLite). Callers must skip emission for
    /// empty results. PostgreSQL implements this via `SET search_path TO ...`.
    ///
    /// The `schema` value is interpolated into the statement; callers must
    /// validate it (e.g. whitelist `[A-Za-z0-9_]`) before invoking.
    [[nodiscard]] virtual std::string SetDefaultSchemaStatement(std::string_view schema) const
    {
        (void) schema;
        return {};
    }

    /// @brief Returns the statements a connection executes after connecting to realise @p settings.
    ///
    /// Only the SQLite dialect has these settings, and emits one @c PRAGMA per setting; every other
    /// dialect returns an empty list, which leaves the settings inert there.
    ///
    /// @param settings The connection's SQLite settings.
    /// @return The statements to execute, in order; empty when there is nothing to apply.
    [[nodiscard]] virtual StringList SqliteSettingsStatements(SqliteConnectionSettings const& settings) const
    {
        (void) settings;
        return {};
    }

    /// @brief Returns the dialect-specific handler used by `SqlScopedLock` to
    /// acquire and release named cross-process advisory locks.
    ///
    /// The returned reference is to a process-singleton, valid for the lifetime
    /// of the program. Each backend implements its own primitive — SQL Server
    /// uses `sp_getapplock`, PostgreSQL uses `pg_advisory_lock`, SQLite uses
    /// a lock table — and the implementation lives in the backend's formatter
    /// translation unit, so adding a new dialect only touches that unit and
    /// `SqlScopedLock` itself stays dialect-agnostic.
    [[nodiscard]] virtual SqlAdvisoryLockHandler const& AdvisoryLockOps() const = 0;

    /// Returns the dialect's transient-error classifier, used by @c SqlRetryPolicy to decide
    /// whether a failed statement is worth another attempt.
    ///
    /// Routing the classification through the formatter is what keeps per-DBMS error-code
    /// knowledge out of business logic — the same reason @ref AdvisoryLockOps() exists.
    [[nodiscard]] virtual SqlRetryClassifier const& RetryOps() const noexcept = 0;

    /// @brief Whether the dialect resolves table and column names without regard to case, even
    /// when they are quoted — so a declared `ITEM_NAME` names the live column `Item_Name`.
    ///
    /// Defaults to `false`. SQLite returns `true`. SQL Server returns `true` as well, which assumes
    /// a case-insensitive database collation (the default); it is not probed per database.
    /// PostgreSQL keeps quoted identifiers case-sensitive and returns `false`.
    ///
    /// New hooks are appended at the end of the class: that keeps the slots of the earlier ones for
    /// callers, but a subclass outside this library must still be rebuilt.
    ///
    /// @return `true` if identifiers differing only in ASCII letter case name the same object.
    [[nodiscard]] virtual bool IdentifiersAreCaseInsensitive() const noexcept
    {
        return false;
    }

    /// @brief Whether the dialect has schemas that tables can be created in.
    ///
    /// Defaults to `true`. SQLite returns `false`: it has no schemas, only attached databases.
    ///
    /// @return `true` if a table can be created in a named schema.
    [[nodiscard]] virtual bool SupportsSchemas() const noexcept
    {
        return true;
    }

    /// @brief Returns the statement that creates @p schema unless it already exists.
    ///
    /// Returns an empty string when the dialect has no schemas (see @ref SupportsSchemas()).
    ///
    /// @param schema The schema to create.
    /// @return The statement to execute, or an empty string when there is nothing to execute.
    [[nodiscard]] virtual std::string CreateSchemaIfNotExists(std::string_view schema) const
    {
        (void) schema;
        return {};
    }

    /// @brief Returns the statement that makes the session report affected-row counts again.
    ///
    /// Defaults to an empty string, meaning the dialect always reports them. SQL Server returns
    /// `SET NOCOUNT OFF`: under `NOCOUNT ON` the driver may report 0 rows for a statement that
    /// changed rows. The setting stays in effect for the session after the statement ran.
    ///
    /// @return The statement to execute, or an empty string when there is nothing to execute.
    [[nodiscard]] virtual std::string_view ReportRowCountsStatement() const noexcept
    {
        return {};
    }

    /// @brief A query estimating a table's row count from the catalog's statistics, without reading the
    /// table. It takes two parameters, the schema name and then the table name, and yields one integer.
    ///
    /// @return The query, or empty when the dialect keeps no such statistics; callers then count with
    ///         `SELECT COUNT(*)`, which reads the whole table.
    [[nodiscard]] virtual std::string_view TableRowCountEstimateQuery() const noexcept
    {
        return {};
    }

    /// @brief Whether the dialect has a native GUID/UUID column type (as opposed to storing GUIDs as text).
    ///
    /// Defaults to `false`. SQL Server (`UNIQUEIDENTIFIER`) and PostgreSQL (`UUID`) return `true`.
    ///
    /// @return `true` if GUID columns are stored in a dedicated binary type.
    [[nodiscard]] virtual bool HasNativeGuidType() const noexcept
    {
        return false;
    }

    /// @brief Whether comparing a fixed-width CHAR(n) column pads the shorter operand with spaces.
    ///
    /// Defaults to `true`. SQLite returns `false`: it stores CHAR(n) as plain text and compares it verbatim.
    ///
    /// @return `true` if trailing spaces of CHAR(n) values are insignificant in comparisons.
    [[nodiscard]] virtual bool ComparesFixedCharWithPadSemantics() const noexcept
    {
        return true;
    }

    /// @brief Returns the name under which the dialect stores an unquoted identifier.
    ///
    /// Defaults to returning @p identifier unchanged. PostgreSQL folds unquoted identifiers to
    /// lower case and returns the ASCII-lower-cased name.
    ///
    /// @param identifier The identifier as written without quotes.
    /// @return The identifier as the catalog stores it.
    [[nodiscard]] virtual std::string FoldUnquotedIdentifier(std::string_view identifier) const
    {
        return std::string(identifier);
    }

    /// @brief Whether the driver's parameter status array names exactly the parameter sets that
    /// failed when a batch (parameter array) execution fails.
    ///
    /// Defaults to `false`. SQL Server returns `true`: ODBC Driver 18 executes every parameter set and
    /// marks only the failing ones `SQL_PARAM_ERROR`. PostgreSQL's psqlodbc marks every parameter set
    /// of a failed array `SQL_PARAM_ERROR`, and the SQLite ODBC driver does not fill the array at all,
    /// so their failing rows are found by bisection instead.
    ///
    /// @return `true` if the rows marked `SQL_PARAM_ERROR` may be trusted to be the failing ones.
    [[nodiscard]] virtual bool ParameterStatusIdentifiesFailedRows() const noexcept
    {
        return false;
    }

  protected:
    /// Formats a table name with optional schema prefix.
    static std::string FormatTableName(std::string_view schema, std::string_view table);

    /// Doubles every @p quote in @p text, so @p text can be embedded between two such quotes.
    /// @param text The text to embed.
    /// @param quote The quote character that delimits the embedded text.
    /// @return @p text with each @p quote doubled.
    static std::string DoubleQuoteCharacter(std::string_view text, char quote);
};

} // namespace Lightweight
