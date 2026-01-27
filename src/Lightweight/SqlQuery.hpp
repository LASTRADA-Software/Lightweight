// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Api.hpp"
#include "SqlQuery/Delete.hpp"
#include "SqlQuery/Insert.hpp"
#include "SqlQuery/Migrate.hpp"
#include "SqlQuery/Select.hpp"
#include "SqlQuery/Update.hpp"

namespace Lightweight
{

struct [[nodiscard]] SqlLastInsertIdQuery
{
    std::string tableName;
    SqlQueryFormatter const* formatter;

    [[nodiscard]] std::string ToSql() const
    {
        return formatter->QueryLastInsertId(tableName);
    }
};

/// @brief API Entry point for building SQL queries.
///
/// @ingroup QueryBuilder
class [[nodiscard]] SqlQueryBuilder final
{
  public:
    /// Constructs a new query builder for the given table.
    explicit SqlQueryBuilder(SqlQueryFormatter const& formatter,
                             std::string&& table = {},
                             std::string&& alias = {}) noexcept;

    /// Constructs a new query builder for the given table.
    LIGHTWEIGHT_API SqlQueryBuilder& FromTable(std::string table);

    /// Constructs a new query builder for the given table.
    LIGHTWEIGHT_API SqlQueryBuilder& FromTable(std::string_view table);

    /// Constructs a new query builder for the given table.
    template <size_t N>
    SqlQueryBuilder& FromTable(char const (&table)[N])
    {
        return FromTable(std::string_view { table, N - 1 });
    }

    /// Constructs a new query builder for the given table with an alias.
    LIGHTWEIGHT_API SqlQueryBuilder& FromTableAs(std::string table, std::string alias);

    /// Constructs a new query builder for the given schema and table.
    /// @param schema The schema name (e.g., "public", "dbo")
    /// @param table The table name
    LIGHTWEIGHT_API SqlQueryBuilder& FromSchemaTable(std::string_view schema, std::string_view table);

    ///  Initiates INSERT query building
    ///
    ///  @param boundInputs Optional vector to store bound inputs.
    ///                     If provided, the inputs will be appended to this vector and can be used
    ///                    to bind the values to the query via SqlStatement::ExecuteWithVariants(...)
    LIGHTWEIGHT_API SqlInsertQueryBuilder Insert(std::vector<SqlVariant>* boundInputs = nullptr) noexcept;

    /// Constructs a query to retrieve the last insert ID for the given table.
    LIGHTWEIGHT_API SqlLastInsertIdQuery LastInsertId();

    /// Initiates SELECT query building.
    LIGHTWEIGHT_API SqlSelectQueryBuilder Select() noexcept;

    /// Initiates UPDATE query building.
    ///
    /// @param boundInputs Optional vector to store bound inputs.
    ///                    If provided, the inputs will be appended to this vector and can be used
    ///                    to bind the values to the query via SqlStatement::ExecuteWithVariants(...)
    LIGHTWEIGHT_API SqlUpdateQueryBuilder Update(std::vector<SqlVariant>* boundInputs = nullptr) noexcept;

    /// Initiates DELETE query building.
    LIGHTWEIGHT_API SqlDeleteQueryBuilder Delete() noexcept;

    /// Initiates query for building database migrations.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder Migration();

  private:
    SqlQueryFormatter const& m_formatter;
    std::string m_schema;
    std::string m_table;
    std::string m_tableAlias;
};

inline LIGHTWEIGHT_FORCE_INLINE SqlQueryBuilder::SqlQueryBuilder(SqlQueryFormatter const& formatter,
                                                                 std::string&& table,
                                                                 std::string&& alias) noexcept:
    m_formatter { formatter },
    m_table { std::move(table) },
    m_tableAlias { std::move(alias) }
{
}

} // namespace Lightweight
