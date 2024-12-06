// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"

#include <reflection-cpp/reflection.hpp>

enum class SqlResultOrdering : uint8_t
{
    ASCENDING,
    DESCENDING
};

enum class SqlQueryBuilderMode : uint8_t
{
    Fluent,
    Varying
};

class [[nodiscard]] SqlSelectQueryBuilder final: public detail::SqlWhereClauseBuilder<SqlSelectQueryBuilder>
{
  public:
    enum class SelectType : std::uint8_t
    {
        Undefined,
        Count,
        All,
        First,
        Range
    };

    struct ComposedQuery
    {
        SelectType selectType = SelectType::Undefined;
        SqlQueryFormatter const* formatter = nullptr;

        bool distinct = false;
        SqlSearchCondition searchCondition {};

        std::string fields;

        std::string orderBy;
        std::string groupBy;

        size_t offset = 0;
        size_t limit = (std::numeric_limits<size_t>::max)();

        [[nodiscard]] LIGHTWEIGHT_API std::string ToSql() const;
    };

    explicit SqlSelectQueryBuilder(SqlQueryFormatter const& formatter,
                                   std::string table,
                                   std::string tableAlias) noexcept:
        detail::SqlWhereClauseBuilder<SqlSelectQueryBuilder> {},
        m_formatter { formatter }
    {
        m_query.formatter = &formatter;
        m_query.searchCondition.tableName = std::move(table);
        m_query.searchCondition.tableAlias = std::move(tableAlias);
        m_query.fields.reserve(256);
    }

    // Sets the builder mode to Varying, allowing varying final query types.
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& Varying() noexcept
    {
        m_mode = SqlQueryBuilderMode::Varying;
        return *this;
    }

    // Adds a DISTINCT clause to the SELECT query.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Distinct() noexcept;

    // Adds a sequence of columns to the SELECT clause.
    template <typename... MoreFields>
    SqlSelectQueryBuilder& Fields(std::string_view const& firstField, MoreFields&&... moreFields);

    // Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(std::string_view const& fieldName);

    // Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(SqlQualifiedTableColumnName const& fieldName);

    // Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames);

    // Adds a sequence of columns from the given table to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

    template <typename Record>
    SqlSelectQueryBuilder& Fields();

    // Adds a single column with an alias to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& FieldAs(std::string_view const& fieldName, std::string_view const& alias);

    // Adds a single column with an alias to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& FieldAs(SqlQualifiedTableColumnName const& fieldName,
                                                   std::string_view const& alias);

    // Constructs or extends a ORDER BY clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& OrderBy(SqlQualifiedTableColumnName const& columnName,
                                                   SqlResultOrdering ordering = SqlResultOrdering::ASCENDING);

    // Constructs or extends a ORDER BY clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& OrderBy(std::string_view columnName,
                                                   SqlResultOrdering ordering = SqlResultOrdering::ASCENDING);

    // Constructs or extends a GROUP BY clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& GroupBy(std::string_view columnName);

    template <typename Callable>
    SqlSelectQueryBuilder& Build(Callable const& callable);

    // Finalizes building the query as SELECT COUNT(*) ... query.
    LIGHTWEIGHT_API ComposedQuery Count();

    // Finalizes building the query as SELECT field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery All();

    // Finalizes building the query as SELECT TOP n field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery First(size_t count = 1);

    // Finalizes building the query as SELECT field names FROM ... query with a range.
    LIGHTWEIGHT_API ComposedQuery Range(std::size_t offset, std::size_t limit);

    LIGHTWEIGHT_FORCE_INLINE SqlSearchCondition& SearchCondition() noexcept
    {
        return m_query.searchCondition;
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE SqlQueryFormatter const& Formatter() const noexcept
    {
        return m_formatter;
    }

  private:
    SqlQueryFormatter const& m_formatter;
    ComposedQuery m_query;
    SqlQueryBuilderMode m_mode = SqlQueryBuilderMode::Fluent;
};

template <typename... MoreFields>
SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::string_view const& firstField, MoreFields&&... moreFields)
{
    using namespace std::string_view_literals;

    std::ostringstream fragment;

    if (!m_query.fields.empty())
        fragment << ", "sv;

    fragment << '"' << firstField << '"';

    if constexpr (sizeof...(MoreFields) > 0)
        ((fragment << R"(, ")"sv << std::forward<MoreFields>(moreFields) << '"') << ...);

    m_query.fields += fragment.str();
    return *this;
}

template <typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& SqlSelectQueryBuilder::Build(Callable const& callable)
{
    callable(*this);
    return *this;
}

template <typename Record>
inline LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields()
{
    std::vector<std::string_view> fieldsNames;
    Reflection::EnumerateMembers<Record>([&]<size_t FieldIndex, typename FieldType>() {
            fieldsNames.push_back(std::string_view{Reflection::MemberNameOf<FieldIndex, Record>});
    });
    return Fields(fieldsNames);
}
