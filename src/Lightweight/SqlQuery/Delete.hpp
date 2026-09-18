// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"

#include <string>
#include <vector>

namespace Lightweight
{

/// @brief Query builder for building DELETE FROM ... queries.
///
/// @ingroup QueryBuilder
class LIGHTWEIGHT_API SqlDeleteQueryBuilder final: public SqlWhereClauseBuilder<SqlDeleteQueryBuilder>
{
  public:
    /// Constructs a DELETE query builder.
    ///
    /// @param formatter     Dialect the query is written in.
    /// @param table         Table to delete from.
    /// @param tableAlias    Alias for that table, empty for none.
    /// @param inputBindings Receives the WHERE values as bound parameters instead of having them
    ///                      written into the query text; null keeps them inline. Values are
    ///                      appended, never cleared. Execute the result with
    ///                      SqlStatement::ExecuteWithVariants(), not ExecuteDirect(), which would
    ///                      leave the markers unbound. Both the vector and anything a stored
    ///                      std::string_view / std::u16string_view points at must outlive the
    ///                      execution, because the binder hands the driver that pointer directly.
    /// @note An explicit SqlWildcard emits its marker without recording a value here, and a
    ///       WhereIn() set larger than SqlMaxBoundSetSize falls back to inline literals; either
    ///       makes the marker count differ from the vector size.
    explicit SqlDeleteQueryBuilder(SqlQueryFormatter const& formatter,
                                   std::string table,
                                   std::string tableAlias,
                                   std::vector<SqlVariant>* inputBindings = nullptr) noexcept:
        SqlWhereClauseBuilder<SqlDeleteQueryBuilder> {},
        m_formatter { formatter }
    {
        m_searchCondition.tableName = std::move(table);
        m_searchCondition.tableAlias = std::move(tableAlias);
        m_searchCondition.inputBindings = inputBindings;
    }

    /// Returns the search condition for the query.
    SqlSearchCondition& SearchCondition() noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        return m_searchCondition;
    }

    // clang-format off
    /// Returns the SQL query formatter.
    [[nodiscard]] SqlQueryFormatter const& Formatter() const noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        // clang-format on
        return m_formatter;
    }

    /// Finalizes building the query as DELETE FROM ... query.
    [[nodiscard]] std::string ToSql() const;

  private:
    SqlQueryFormatter const& m_formatter;
    SqlSearchCondition m_searchCondition;
};

inline LIGHTWEIGHT_FORCE_INLINE std::string SqlDeleteQueryBuilder::ToSql() const
{
    return m_formatter.Delete(m_searchCondition.tableName,
                              m_searchCondition.tableAlias,
                              m_searchCondition.tableJoins,
                              m_searchCondition.condition);
}

} // namespace Lightweight
