// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlQuery/MigrationPlan.hpp"

#include <format>
#include <functional>
#include <string_view>
#include <tuple>
#include <vector>

namespace Lightweight
{

class SqlStatement;

namespace SqlSchema
{

    namespace detail
    {
        // NOLINTNEXTLINE(readability-identifier-naming)
        constexpr std::string_view rtrim(std::string_view value) noexcept
        {
            while (!value.empty() && (std::isspace(value.back()) || value.back() == '\0'))
                value.remove_suffix(1);
            return value;
        }
    } // namespace detail

    struct FullyQualifiedTableName
    {
        std::string catalog;
        std::string schema;
        std::string table;

        bool operator==(FullyQualifiedTableName const& other) const noexcept
        {
            return catalog == other.catalog && schema == other.schema && table == other.table;
        }

        bool operator!=(FullyQualifiedTableName const& other) const noexcept
        {
            return !(*this == other);
        }

        bool operator<(FullyQualifiedTableName const& other) const noexcept
        {
            return std::tie(catalog, schema, table) < std::tie(other.catalog, other.schema, other.table);
        }
    };

    struct FullyQualifiedTableColumn
    {
        FullyQualifiedTableName table;
        std::string column;

        bool operator==(FullyQualifiedTableColumn const& other) const noexcept
        {
            return table == other.table && column == other.column;
        }

        bool operator!=(FullyQualifiedTableColumn const& other) const noexcept
        {
            return !(*this == other);
        }

        bool operator<(FullyQualifiedTableColumn const& other) const noexcept
        {
            return std::tie(table, column) < std::tie(other.table, other.column);
        }
    };

    struct FullyQualifiedTableColumnSequence
    {
        FullyQualifiedTableName table;
        std::vector<std::string> columns;
    };

    inline bool operator<(FullyQualifiedTableColumnSequence const& a, FullyQualifiedTableColumnSequence const& b) noexcept
    {
        return std::tie(a.table, a.columns) < std::tie(b.table, b.columns);
    }

    struct ForeignKeyConstraint
    {
        FullyQualifiedTableColumnSequence foreignKey;
        FullyQualifiedTableColumnSequence primaryKey;
    };

    inline bool operator<(ForeignKeyConstraint const& a, ForeignKeyConstraint const& b) noexcept
    {
        return std::tie(a.foreignKey, a.primaryKey) < std::tie(b.foreignKey, b.primaryKey);
    }

    /// Represents an index definition on a table.
    struct IndexDefinition
    {
        /// The name of the index.
        std::string name;

        /// The columns in the index (in order for composite indexes).
        std::vector<std::string> columns;

        /// Whether the index enforces uniqueness.
        bool isUnique = false;
    };

    using KeyPair = std::pair<FullyQualifiedTableName /*fk table*/, FullyQualifiedTableName /*pk table*/>;

    inline bool operator<(KeyPair const& a, KeyPair const& b)
    {
        return std::tie(a.first, a.second) < std::tie(b.first, b.second);
    }

    /// Holds the definition of a column in a SQL table as read from the database schema.
    struct Column
    {
        /// The name of the column.
        std::string name = {};
        /// The SQL column type definition.
        SqlColumnTypeDefinition type = {};
        /// The dialect-dependent type string.
        std::string dialectDependantTypeString = {};
        /// Whether the column allows NULL values.
        bool isNullable = true;
        /// Whether the column has a UNIQUE constraint.
        bool isUnique = false;
        /// The size of the column (for character/binary types).
        size_t size = 0;
        /// The number of decimal digits (for numeric types).
        unsigned short decimalDigits = 0;
        /// Whether the column auto-increments.
        bool isAutoIncrement = false;
        /// Whether the column is a primary key.
        bool isPrimaryKey = false;
        /// Whether the column is a foreign key.
        bool isForeignKey = false;
        /// The foreign key constraint, if any.
        std::optional<ForeignKeyConstraint> foreignKeyConstraint {};
        /// The default value of the column.
        std::string defaultValue = {};
    };

    /// Callback interface for handling events while reading a database schema.
    class EventHandler
    {
      public:
        /// Default constructor.
        EventHandler() = default;
        /// Default move constructor.
        EventHandler(EventHandler&&) = default;
        /// Default copy constructor.
        EventHandler(EventHandler const&) = default;
        /// Default move assignment operator.
        EventHandler& operator=(EventHandler&&) = default;
        /// Default copy assignment operator.
        EventHandler& operator=(EventHandler const&) = default;
        virtual ~EventHandler() = default;

        /// Called when the names of all tables are read.
        virtual void OnTables(std::vector<std::string> const& tables) = 0;

        /// Called for each table. Returns true to process this table, false to skip it.
        /// @param schema The schema the table belongs to.
        /// @param table The name of the table.
        virtual bool OnTable(std::string_view schema, std::string_view table) = 0;
        /// Called when the primary keys of a table are read.
        virtual void OnPrimaryKeys(std::string_view table, std::vector<std::string> const& columns) = 0;
        /// Called when a foreign key constraint is read.
        virtual void OnForeignKey(ForeignKeyConstraint const& foreignKeyConstraint) = 0;
        /// Called for each column in a table.
        virtual void OnColumn(Column const& column) = 0;
        /// Called when an external foreign key referencing this table is read.
        virtual void OnExternalForeignKey(ForeignKeyConstraint const& foreignKeyConstraint) = 0;
        /// Called when the indexes of a table are read.
        virtual void OnIndexes(std::vector<IndexDefinition> const& indexes) = 0;
        /// Called when a table's schema reading is complete.
        virtual void OnTableEnd() = 0;
    };

    /// Reads all tables in the given database and schema and calls the event handler for each table.
    ///
    /// @param stmt The SQL statement to use for reading the database schema.
    /// @param database The name of the database to read the schema from.
    /// @param schema The name of the schema to read the schema from.
    /// @param eventHandler The SAX-style event handler to call for each table.
    ///
    /// @note The event handler is called for each table in the database and schema.
    LIGHTWEIGHT_API void ReadAllTables(SqlStatement& stmt,
                                       std::string_view database,
                                       std::string_view schema,
                                       EventHandler& eventHandler);

    /// Holds the definition of a table in a SQL database as read from the database schema.
    struct Table
    {
        // FullyQualifiedTableName name;

        /// The schema the table belongs to.
        std::string schema;

        /// The name of the table.
        std::string name;

        /// The columns of the table.
        std::vector<Column> columns {};

        /// The foreign keys of the table.
        std::vector<ForeignKeyConstraint> foreignKeys {};

        /// The foreign keys of other tables that reference this table.
        std::vector<ForeignKeyConstraint> externalForeignKeys {};

        /// The primary keys of the table.
        std::vector<std::string> primaryKeys {};

        /// The indexes on the table (excluding primary key index).
        std::vector<IndexDefinition> indexes {};
    };

    /// A list of tables.
    using TableList = std::vector<Table>;

    using ReadAllTablesCallback = std::function<void(std::string_view /*tableName*/, size_t /*current*/, size_t /*total*/)>;

    /// Callback invoked when a table's schema is fully read.
    ///
    /// This callback is called for each table as soon as its schema (columns, keys, constraints)
    /// is complete. Useful for streaming tables to consumers without waiting for all tables.
    using TableReadyCallback = std::function<void(Table&&)>;

    /// Predicate to filter tables before reading their full schema.
    ///
    /// @param schema The schema name.
    /// @param tableName The table name.
    /// @return true to include the table (read its full schema), false to skip it.
    ///
    /// When provided, tables that don't match the predicate will have their detailed
    /// schema (columns, keys, constraints) skipped, improving performance when only
    /// a subset of tables is needed.
    using TableFilterPredicate = std::function<bool(std::string_view /*schema*/, std::string_view /*tableName*/)>;

    /// Retrieves all tables in the given @p database and @p schema.
    ///
    /// @param stmt The SQL statement to use for reading.
    /// @param database The database name.
    /// @param schema The schema name (optional).
    /// @param callback Progress callback invoked for each table during scanning.
    /// @param tableReadyCallback Callback invoked when each table's schema is complete.
    /// @param tableFilter Optional predicate to filter tables before reading their full schema.
    ///                    If provided, only tables where the predicate returns true will have
    ///                    their columns, keys, and constraints read.
    LIGHTWEIGHT_API TableList ReadAllTables(SqlStatement& stmt,
                                            std::string_view database,
                                            std::string_view schema = {},
                                            ReadAllTablesCallback callback = {},
                                            TableReadyCallback tableReadyCallback = {},
                                            TableFilterPredicate tableFilter = {});

    /// Retrieves all tables in the given database and schema that have a foreign key to the given table.
    LIGHTWEIGHT_API std::vector<ForeignKeyConstraint> AllForeignKeysTo(SqlStatement& stmt,
                                                                       FullyQualifiedTableName const& table);

    /// Retrieves all tables in the given database and schema that have a foreign key from the given table.
    LIGHTWEIGHT_API std::vector<ForeignKeyConstraint> AllForeignKeysFrom(SqlStatement& stmt,
                                                                         FullyQualifiedTableName const& table);

    /// Creats an SQL CREATE TABLE plan for the given table description.
    ///
    /// @param tableDescription The description of the table to create the plan for.
    ///
    /// @return An SQL CREATE TABLE plan for the given table description.
    LIGHTWEIGHT_API SqlCreateTablePlan MakeCreateTablePlan(Table const& tableDescription);

    /// Creates an SQL CREATE TABLE plan for all the given table descriptions.
    ///
    /// @param tableDescriptions The descriptions of the tables to create the plan for.
    ///
    /// @return An SQL CREATE TABLE plan for all the given table descriptions.
    LIGHTWEIGHT_API std::vector<SqlCreateTablePlan> MakeCreateTablePlan(TableList const& tableDescriptions);

} // namespace SqlSchema

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlSchema::FullyQualifiedTableName>: std::formatter<std::string>
{
    auto format(Lightweight::SqlSchema::FullyQualifiedTableName const& value, format_context& ctx) const
        -> format_context::iterator
    {
        string output = std::string(Lightweight::SqlSchema::detail::rtrim(value.schema));
        if (!output.empty())
            output += '.';
        auto const trimmedSchema = Lightweight::SqlSchema::detail::rtrim(value.catalog);
        output += trimmedSchema;
        if (!output.empty() && !trimmedSchema.empty())
            output += '.';
        output += Lightweight::SqlSchema::detail::rtrim(value.table);
        return formatter<string>::format(output, ctx);
    }
};

template <>
struct std::formatter<Lightweight::SqlSchema::FullyQualifiedTableColumn>: std::formatter<std::string>
{
    auto format(Lightweight::SqlSchema::FullyQualifiedTableColumn const& value, format_context& ctx) const
        -> format_context::iterator
    {
        auto const table = std::format("{}", value.table);
        if (table.empty())
            return formatter<string>::format(std::format("{}", value.column), ctx);
        else
            return formatter<string>::format(std::format("{}.{}", value.table, value.column), ctx);
    }
};

template <>
struct std::formatter<Lightweight::SqlSchema::FullyQualifiedTableColumnSequence>: std::formatter<std::string>
{
    auto format(Lightweight::SqlSchema::FullyQualifiedTableColumnSequence const& value, format_context& ctx) const
        -> format_context::iterator
    {
        auto const resolvedTableName = std::format("{}", value.table);
        string output;
        output += resolvedTableName;
        output += '(';

#if !defined(__cpp_lib_ranges_enumerate)
        int i { -1 };
        for (auto const& column: value.columns)
        {
            ++i;
#else
        for (auto const [i, column]: value.columns | std::views::enumerate)
        {
#endif
            if (i != 0)
                output += ", ";
            output += column;
        }
        output += ')';

        return formatter<string>::format(output, ctx);
    }
};
