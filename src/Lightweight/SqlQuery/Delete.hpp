// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"

#include <string>

namespace Lightweight
{

/// @brief Query builder for building DELETE FROM ... queries.
///
/// @ingroup QueryBuilder
class LIGHTWEIGHT_API SqlDeleteQueryBuilder final: public SqlWhereClauseBuilder<SqlDeleteQueryBuilder>
{
  public:
    explicit SqlDeleteQueryBuilder(SqlQueryFormatter const& formatter, std::string table, std::string tableAlias) noexcept:
        SqlWhereClauseBuilder<SqlDeleteQueryBuilder> {},
        m_formatter { formatter }
    {
        m_searchCondition.tableName = std::move(table);
        m_searchCondition.tableAlias = std::move(tableAlias);
    }

    SqlSearchCondition& SearchCondition() noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        return m_searchCondition;
    }

    // clang-format off
    [[nodiscard]] SqlQueryFormatter const& Formatter() const noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        // clang-format on
        return m_formatter;
    }

    // Finalizes building the query as DELETE FROM ... query.
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
