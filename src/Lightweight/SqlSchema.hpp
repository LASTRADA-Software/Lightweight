// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlQuery/MigrationPlan.hpp"

#include <format>
#include <string_view>
#include <tuple>
#include <vector>

class SqlStatement;

namespace SqlSchema
{

namespace detail
{
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

struct ForeignKeyConstraint
{
    FullyQualifiedTableColumn foreignKey;
    FullyQualifiedTableColumnSequence primaryKey;
};

/// Holds the definition of a column in a SQL table as read from the database schema.
struct Column
{
    std::string name = {};
    SqlColumnTypeDefinition type = {};
    std::string dialectDependantTypeString = {};
    bool isNullable = true;
    bool isUnique = false;
    size_t size = 0;
    unsigned short decimalDigits = 0;
    bool isAutoIncrement = false;
    bool isPrimaryKey = false;
    bool isForeignKey = false;
    std::optional<ForeignKeyConstraint> foreignKeyConstraint {};
    std::string defaultValue = {};
};

/// Callback interface for handling events while reading a database schema.
class EventHandler
{
  public:
    EventHandler() = default;
    EventHandler(EventHandler&&) = default;
    EventHandler(EventHandler const&) = default;
    EventHandler& operator=(EventHandler&&) = default;
    EventHandler& operator=(EventHandler const&) = default;
    virtual ~EventHandler() = default;

    virtual bool OnTable(std::string_view table) = 0;
    virtual void OnPrimaryKeys(std::string_view table, std::vector<std::string> const& columns) = 0;
    virtual void OnForeignKey(ForeignKeyConstraint const& foreignKeyConstraint) = 0;
    virtual void OnColumn(Column const& column) = 0;
    virtual void OnExternalForeignKey(ForeignKeyConstraint const& foreignKeyConstraint) = 0;
    virtual void OnTableEnd() = 0;
};

/// Reads all tables in the given database and schema and calls the event handler for each table.
LIGHTWEIGHT_API void ReadAllTables(std::string_view database, std::string_view schema, EventHandler& eventHandler);

/// Holds the definition of a table in a SQL database as read from the database schema.
struct Table
{
    // FullyQualifiedTableName name;

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
};

/// A list of tables.
using TableList = std::vector<Table>;

/// Retrieves all tables in the given @p database and @p schema.
LIGHTWEIGHT_API TableList ReadAllTables(std::string_view database, std::string_view schema = {});

/// Retrieves all tables in the given database and schema that have a foreign key to the given table.
LIGHTWEIGHT_API std::vector<ForeignKeyConstraint> AllForeignKeysTo(SqlStatement& stmt,
                                                                   FullyQualifiedTableName const& table);

/// Retrieves all tables in the given database and schema that have a foreign key from the given table.
LIGHTWEIGHT_API std::vector<ForeignKeyConstraint> AllForeignKeysFrom(SqlStatement& stmt,
                                                                     FullyQualifiedTableName const& table);

} // namespace SqlSchema

template <>
struct LIGHTWEIGHT_API std::formatter<SqlSchema::FullyQualifiedTableName>: std::formatter<std::string>
{
    auto format(SqlSchema::FullyQualifiedTableName const& value, format_context& ctx) const -> format_context::iterator
    {
        string output = std::string(SqlSchema::detail::rtrim(value.schema));
        if (!output.empty())
            output += '.';
        auto const trimmedSchema = SqlSchema::detail::rtrim(value.catalog);
        output += trimmedSchema;
        if (!output.empty() && !trimmedSchema.empty())
            output += '.';
        output += SqlSchema::detail::rtrim(value.table);
        return formatter<string>::format(output, ctx);
    }
};

template <>
struct LIGHTWEIGHT_API std::formatter<SqlSchema::FullyQualifiedTableColumn>: std::formatter<std::string>
{
    auto format(SqlSchema::FullyQualifiedTableColumn const& value, format_context& ctx) const
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
struct LIGHTWEIGHT_API std::formatter<SqlSchema::FullyQualifiedTableColumnSequence>: std::formatter<std::string>
{
    auto format(SqlSchema::FullyQualifiedTableColumnSequence const& value, format_context& ctx) const
        -> format_context::iterator
    {
        auto const resolvedTableName = std::format("{}", value.table);
        string output;

        for (auto const& column: value.columns)
        {
            if (!output.empty())
                output += ", ";
            output += resolvedTableName;
            if (!output.empty() && !resolvedTableName.empty())
                output += '.';
            output += column;
        }

        return formatter<string>::format(output, ctx);
    }
};
