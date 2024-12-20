// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlDataBinder.hpp"
#include "../SqlQueryFormatter.hpp"

#include <concepts>
#include <ranges>

/// @defgroup QueryBuilder Query Builder
///
/// The query builder is a high level API for building SQL queries using high level C++ syntax.

/// @brief SqlWildcardType is a placeholder for an explicit wildcard input parameter in a SQL query.
///
/// Use this in the SqlQueryBuilder::Where method to insert a '?' placeholder for a wildcard.
///
/// @ingroup QueryBuilder
struct SqlWildcardType
{
};

/// @brief SqlWildcard is a placeholder for an explicit wildcard input parameter in a SQL query.
static constexpr inline auto SqlWildcard = SqlWildcardType {};

namespace detail
{

struct RawSqlCondition
{
    std::string condition;
};

} // namespace detail

/// @brief SqlQualifiedTableColumnName represents a column name qualified with a table name.
/// @ingroup QueryBuilder
struct SqlQualifiedTableColumnName
{
    std::string_view tableName;
    std::string_view columnName;
};

namespace detail
{

template <typename ColumnName>
std::string MakeSqlColumnName(ColumnName const& columnName)
{
    using namespace std::string_view_literals;
    std::string output;

    if constexpr (std::is_same_v<ColumnName, SqlQualifiedTableColumnName>)
    {
        output.reserve(columnName.tableName.size() + columnName.columnName.size() + 5);
        output += '"';
        output += columnName.tableName;
        output += R"(".")"sv;
        output += columnName.columnName;
        output += '"';
    }
    else
    {
        output += '"';
        output += columnName;
        output += '"';
    }
    return output;
}

} // namespace detail

struct [[nodiscard]] SqlSearchCondition
{
    std::string tableName;
    std::string tableAlias;
    std::string tableJoins;
    std::string condition;
    std::vector<SqlVariant>* inputBindings = nullptr;
};

/// @brief Query builder for building JOIN conditions.
/// @ingroup QueryBuilder
class SqlJoinConditionBuilder
{
  public:
    explicit SqlJoinConditionBuilder(std::string_view referenceTable, std::string* condition) noexcept:
        _referenceTable { referenceTable },
        _condition { *condition }
    {
    }

    SqlJoinConditionBuilder& On(std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
    {
        return Operator(joinColumnName, onOtherColumn, "AND");
    }

    SqlJoinConditionBuilder& OrOn(std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
    {
        return Operator(joinColumnName, onOtherColumn, "OR");
    }

    SqlJoinConditionBuilder& Operator(std::string_view joinColumnName,
                                      SqlQualifiedTableColumnName onOtherColumn,
                                      std::string_view op)
    {
        if (_firstCall)
            _firstCall = !_firstCall;
        else
            _condition += std::format(" {} ", op);

        _condition += '"';
        _condition += _referenceTable;
        _condition += "\".\"";
        _condition += joinColumnName;
        _condition += "\" = \"";
        _condition += onOtherColumn.tableName;
        _condition += "\".\"";
        _condition += onOtherColumn.columnName;
        _condition += '"';

        return *this;
    }

  private:
    std::string_view _referenceTable;
    std::string& _condition;
    bool _firstCall = true;
};

namespace detail
{

/// Helper CRTP-based class for building WHERE clauses.
///
/// This class is inherited by the SqlSelectQueryBuilder, SqlUpdateQueryBuilder, and SqlDeleteQueryBuilder
///
/// @see SqlQueryBuilder
template <typename Derived>
class [[nodiscard]] SqlWhereClauseBuilder
{
  public:
    /// Indicates, that the next WHERE clause should be AND-ed (default).
    [[nodiscard]] Derived& And() noexcept;

    /// Indicates, that the next WHERE clause should be OR-ed.
    [[nodiscard]] Derived& Or() noexcept;

    /// Indicates, that the next WHERE clause should be negated.
    [[nodiscard]] Derived& Not() noexcept;

    /// Constructs or extends a raw WHERE clause.
    [[nodiscard]] Derived& WhereRaw(std::string_view sqlConditionExpression);

    /// Constructs or extends a WHERE clause to test for a binary operation.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& Where(ColumnName const& columnName, std::string_view binaryOp, T const& value);

    /// Constructs or extends a WHERE clause to test for a binary operation for RHS as sub-select query.
    template <typename ColumnName, typename SubSelectQuery>
        requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
    [[nodiscard]] Derived& Where(ColumnName const& columnName, std::string_view binaryOp, SubSelectQuery const& value);

    /// Constructs or extends a WHERE/OR clause to test for a binary operation.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& OrWhere(ColumnName const& columnName, std::string_view binaryOp, T const& value);

    /// Constructs or extends a WHERE clause to test for a binary operation for RHS as string literal.
    template <typename ColumnName, std::size_t N>
    Derived& Where(ColumnName const& columnName, std::string_view binaryOp, char const (&value)[N]);

    /// Constructs or extends a WHERE clause to test for equality.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& Where(ColumnName const& columnName, T const& value);

    /// Constructs or extends an WHERE/OR clause to test for equality.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& OrWhere(ColumnName const& columnName, T const& value);

    /// Constructs or extends a WHERE/AND clause to test for a group of values.
    template <typename Callable>
        requires std::invocable<Callable, SqlWhereClauseBuilder<Derived>&>
    [[nodiscard]] Derived& Where(Callable const& callable);

    /// Constructs or extends an WHERE/OR clause to test for a group of values.
    template <typename Callable>
        requires std::invocable<Callable, SqlWhereClauseBuilder<Derived>&>
    [[nodiscard]] Derived& OrWhere(Callable const& callable);

    /// Constructs or extends an WHERE/OR clause to test for a value, satisfying std::ranges::input_range.
    template <typename ColumnName, std::ranges::input_range InputRange>
    [[nodiscard]] Derived& WhereIn(ColumnName const& columnName, InputRange const& values);

    /// Constructs or extends an WHERE/OR clause to test for a value, satisfying std::initializer_list.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& WhereIn(ColumnName const& columnName, std::initializer_list<T> const& values);

    /// Constructs or extends an WHERE/OR clause to test for a value, satisfying a sub-select query.
    template <typename ColumnName, typename SubSelectQuery>
        requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
    [[nodiscard]] Derived& WhereIn(ColumnName const& columnName, SubSelectQuery const& subSelectQuery);

    /// Constructs or extends an WHERE/OR clause to test for a value to be NULL.
    template <typename ColumnName>
    [[nodiscard]] Derived& WhereNull(ColumnName const& columnName);

    /// Constructs or extends a WHERE clause to test for a value being not null.
    template <typename ColumnName>
    [[nodiscard]] Derived& WhereNotNull(ColumnName const& columnName);

    /// Constructs or extends a WHERE clause to test for a value being equal to another column.
    template <typename ColumnName, typename T>
    [[nodiscard]] Derived& WhereNotEqual(ColumnName const& columnName, T const& value);

    /// Constructs or extends a WHERE clause to test for a value being true.
    template <typename ColumnName>
    [[nodiscard]] Derived& WhereTrue(ColumnName const& columnName);

    /// Constructs or extends a WHERE clause to test for a value being false.
    template <typename ColumnName>
    [[nodiscard]] Derived& WhereFalse(ColumnName const& columnName);

    /// Construts or extends a WHERE clause to test for a binary operation between two columns.
    template <typename LeftColumn, typename RightColumn>
    [[nodiscard]] Derived& WhereColumn(LeftColumn const& left, std::string_view binaryOp, RightColumn const& right);

    /// Constructs an INNER JOIN clause.
    [[nodiscard]] Derived& InnerJoin(std::string_view joinTable,
                                     std::string_view joinColumnName,
                                     SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an INNER JOIN clause.
    [[nodiscard]] Derived& InnerJoin(std::string_view joinTable,
                                     std::string_view joinColumnName,
                                     std::string_view onMainTableColumn);

    /// Constructs an INNER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& InnerJoin(std::string_view joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an LEFT OUTER JOIN clause.
    [[nodiscard]] Derived& LeftOuterJoin(std::string_view joinTable,
                                         std::string_view joinColumnName,
                                         SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an LEFT OUTER JOIN clause.
    [[nodiscard]] Derived& LeftOuterJoin(std::string_view joinTable,
                                         std::string_view joinColumnName,
                                         std::string_view onMainTableColumn);

    /// Constructs an LEFT OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& LeftOuterJoin(std::string_view joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an RIGHT OUTER JOIN clause.
    [[nodiscard]] Derived& RightOuterJoin(std::string_view joinTable,
                                          std::string_view joinColumnName,
                                          SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an RIGHT OUTER JOIN clause.
    [[nodiscard]] Derived& RightOuterJoin(std::string_view joinTable,
                                          std::string_view joinColumnName,
                                          std::string_view onMainTableColumn);

    /// Constructs an RIGHT OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& RightOuterJoin(std::string_view joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an FULL OUTER JOIN clause.
    [[nodiscard]] Derived& FullOuterJoin(std::string_view joinTable,
                                         std::string_view joinColumnName,
                                         SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an FULL OUTER JOIN clause.
    [[nodiscard]] Derived& FullOuterJoin(std::string_view joinTable,
                                         std::string_view joinColumnName,
                                         std::string_view onMainTableColumn);

    /// Constructs an FULL OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& FullOuterJoin(std::string_view joinTable, OnChainCallable const& onClauseBuilder);

  private:
    SqlSearchCondition& SearchCondition() noexcept;
    [[nodiscard]] SqlQueryFormatter const& Formatter() const noexcept;

    enum class WhereJunctor : uint8_t
    {
        Null,
        Where,
        And,
        Or,
    };

    WhereJunctor m_nextWhereJunctor = WhereJunctor::Where;
    bool m_nextIsNot = false;

    void AppendWhereJunctor();

    template <typename ColumnName>
        requires(std::same_as<ColumnName, SqlQualifiedTableColumnName>
                 || std::convertible_to<ColumnName, std::string_view> || std::convertible_to<ColumnName, std::string>)
    void AppendColumnName(ColumnName const& columnName);

    enum class JoinType : uint8_t
    {
        INNER,
        LEFT,
        RIGHT,
        FULL
    };

    // Constructs a JOIN clause.
    [[nodiscard]] Derived& Join(JoinType joinType,
                                std::string_view joinTable,
                                std::string_view joinColumnName,
                                SqlQualifiedTableColumnName onOtherColumn);

    // Constructs a JOIN clause.
    [[nodiscard]] Derived& Join(JoinType joinType,
                                std::string_view joinTable,
                                std::string_view joinColumnName,
                                std::string_view onMainTableColumn);

    // Constructs a JOIN clause.
    template <typename OnChainCallable>
    [[nodiscard]] Derived& Join(JoinType joinType, std::string_view joinTable, OnChainCallable const& onClauseBuilder);
};

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::And() noexcept
{
    m_nextWhereJunctor = WhereJunctor::And;
    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Or() noexcept
{
    m_nextWhereJunctor = WhereJunctor::Or;
    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Not() noexcept
{
    m_nextIsNot = !m_nextIsNot;
    return static_cast<Derived&>(*this);
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               T const& value)
{
    if constexpr (detail::OneOf<T, SqlNullType, std::nullopt_t>)
    {
        if (m_nextIsNot)
        {
            m_nextIsNot = false;
            return Where(columnName, "IS NOT", value);
        }
        else
            return Where(columnName, "IS", value);
    }
    else
        return Where(columnName, "=", value);
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::OrWhere(ColumnName const& columnName,
                                                                                 T const& value)
{
    return Or().Where(columnName, value);
}

template <typename Derived>
template <typename Callable>
    requires std::invocable<Callable, SqlWhereClauseBuilder<Derived>&>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::OrWhere(Callable const& callable)
{
    return Or().Where(callable);
}

template <typename Derived>
template <typename Callable>
    requires std::invocable<Callable, SqlWhereClauseBuilder<Derived>&>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(Callable const& callable)
{
    auto& condition = SearchCondition().condition;

    auto const originalSize = condition.size();

    AppendWhereJunctor();
    m_nextWhereJunctor = WhereJunctor::Null;
    condition += '(';

    auto const sizeBeforeCallable = condition.size();

    (void) callable(*this);

    if (condition.size() == sizeBeforeCallable)
        condition.resize(originalSize);
    else
        condition += ')';

    return static_cast<Derived&>(*this);
}

inline LIGHTWEIGHT_FORCE_INLINE RawSqlCondition PopulateSqlSetExpression(auto const& values)
{
    using namespace std::string_view_literals;
    std::ostringstream fragment;
    fragment << '(';
    for (auto const&& [index, value]: values | std::views::enumerate)
    {
        if (index > 0)
            fragment << ", "sv;
        fragment << value;
    }
    fragment << ')';
    return RawSqlCondition { fragment.str() };
}

template <typename Derived>
template <typename ColumnName, std::ranges::input_range InputRange>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 InputRange const& values)
{
    return Where(columnName, "IN", detail::PopulateSqlSetExpression(values));
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 std::initializer_list<T> const& values)
{
    return Where(columnName, "IN", detail::PopulateSqlSetExpression(values));
}

template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 SubSelectQuery const& subSelectQuery)
{
    return Where(columnName, "IN", RawSqlCondition { "(" + subSelectQuery.ToSql() + ")" });
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNotNull(ColumnName const& columnName)
{
    return Where(columnName, "IS NOT", "NULL");
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNull(ColumnName const& columnName)
{
    return Where(columnName, "IS", "NULL");
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNotEqual(ColumnName const& columnName,
                                                                                       T const& value)
{
    if constexpr (detail::OneOf<T, SqlNullType, std::nullopt_t>)
        return Where(columnName, "IS NOT", value);
    else
        return Where(columnName, "!=", value);
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereTrue(ColumnName const& columnName)
{
    return Where(columnName, "=", true);
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereFalse(ColumnName const& columnName)
{
    return Where(columnName, "=", false);
}

template <typename Derived>
template <typename LeftColumn, typename RightColumn>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereColumn(LeftColumn const& left,
                                                                                     std::string_view binaryOp,
                                                                                     RightColumn const& right)
{
    AppendWhereJunctor();

    AppendColumnName(left);
    SearchCondition().condition += ' ';
    SearchCondition().condition += binaryOp;
    SearchCondition().condition += ' ';
    AppendColumnName(right);

    return static_cast<Derived&>(*this);
}

template <typename T>
struct WhereConditionLiteralType
{
    constexpr static bool needsQuotes = !std::is_integral_v<T> && !std::is_floating_point_v<T> && !std::same_as<T, bool>
                                        && !std::same_as<T, SqlWildcardType>;
};

template <typename Derived>
template <typename ColumnName, std::size_t N>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               char const (&value)[N])
{
    return Where(columnName, binaryOp, std::string_view { value, N - 1 });
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               T const& value)
{
    auto& searchCondition = SearchCondition();

    AppendWhereJunctor();
    AppendColumnName(columnName);
    searchCondition.condition += ' ';
    searchCondition.condition += binaryOp;
    searchCondition.condition += ' ';

    if constexpr (std::is_same_v<T, SqlQualifiedTableColumnName>)
    {
        searchCondition.condition += '"';
        searchCondition.condition += value.tableName;
        searchCondition.condition += "\".\"";
        searchCondition.condition += value.columnName;
        searchCondition.condition += '"';
    }
    else if constexpr (detail::OneOf<T, SqlNullType, std::nullopt_t>)
    {
        searchCondition.condition += "NULL";
    }
    else if constexpr (std::is_same_v<T, SqlWildcardType>)
    {
        searchCondition.condition += '?';
    }
    else if constexpr (std::is_same_v<T, detail::RawSqlCondition>)
    {
        searchCondition.condition += value.condition;
    }
    else if (searchCondition.inputBindings)
    {
        searchCondition.condition += '?';
        searchCondition.inputBindings->emplace_back(value);
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        searchCondition.condition += Formatter().BooleanLiteral(value);
    }
    else if constexpr (!WhereConditionLiteralType<T>::needsQuotes)
    {
        searchCondition.condition += std::format("{}", value);
    }
    else
    {
        // TODO: Escape single quotes
        searchCondition.condition += '\'';
        searchCondition.condition += std::format("{}", value);
        searchCondition.condition += '\'';
    }

    return static_cast<Derived&>(*this);
}

template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               SubSelectQuery const& value)
{
    return Where(columnName, binaryOp, RawSqlCondition { "(" + value.ToSql() + ")" });
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::OrWhere(ColumnName const& columnName,
                                                                                 std::string_view binaryOp,
                                                                                 T const& value)
{
    return Or().Where(columnName, binaryOp, value);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(
    std::string_view joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::INNER, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(std::string_view joinTable,
                                                                                   std::string_view joinColumnName,
                                                                                   std::string_view onMainTableColumn)
{
    return Join(JoinType::INNER, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(std::string_view joinTable, OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::INNER, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::LEFT, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, std::string_view onMainTableColumn)
{
    return Join(JoinType::LEFT, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(std::string_view joinTable,
                                                       OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::LEFT, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::RIGHT, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, std::string_view onMainTableColumn)
{
    return Join(JoinType::RIGHT, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(std::string_view joinTable,
                                                        OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::RIGHT, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::FULL, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(
    std::string_view joinTable, std::string_view joinColumnName, std::string_view onMainTableColumn)
{
    return Join(JoinType::FULL, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(std::string_view joinTable,
                                                       OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::FULL, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereRaw(
    std::string_view sqlConditionExpression)
{
    AppendWhereJunctor();

    auto& condition = SearchCondition().condition;
    condition += sqlConditionExpression;

    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE SqlSearchCondition& SqlWhereClauseBuilder<Derived>::SearchCondition() noexcept
{
    return static_cast<Derived*>(this)->SearchCondition();
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE SqlQueryFormatter const& SqlWhereClauseBuilder<Derived>::Formatter() const noexcept
{
    return static_cast<Derived const*>(this)->Formatter();
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::AppendWhereJunctor()
{
    using namespace std::string_view_literals;

    auto& condition = SearchCondition().condition;

    switch (m_nextWhereJunctor)
    {
        case WhereJunctor::Null:
            break;
        case WhereJunctor::Where:
            condition += "\n WHERE "sv;
            break;
        case WhereJunctor::And:
            condition += " AND "sv;
            break;
        case WhereJunctor::Or:
            condition += " OR "sv;
            break;
    }

    if (m_nextIsNot)
    {
        condition += "NOT "sv;
        m_nextIsNot = false;
    }

    m_nextWhereJunctor = WhereJunctor::And;
}

template <typename Derived>
template <typename ColumnName>
    requires(std::same_as<ColumnName, SqlQualifiedTableColumnName> || std::convertible_to<ColumnName, std::string_view>
             || std::convertible_to<ColumnName, std::string>)
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::AppendColumnName(ColumnName const& columnName)
{
    SearchCondition().condition += detail::MakeSqlColumnName(columnName);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              std::string_view joinTable,
                                                                              std::string_view joinColumnName,
                                                                              SqlQualifiedTableColumnName onOtherColumn)
{
    static constexpr std::array<std::string_view, 4> JoinTypeStrings = {
        "INNER",
        "LEFT OUTER",
        "RIGHT OUTER",
        "FULL OUTER",
    };

    SearchCondition().tableJoins += std::format("\n"
                                                R"( {0} JOIN "{1}" ON "{1}"."{2}" = "{3}"."{4}")",
                                                JoinTypeStrings[static_cast<std::size_t>(joinType)],
                                                joinTable,
                                                joinColumnName,
                                                onOtherColumn.tableName,
                                                onOtherColumn.columnName);
    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              std::string_view joinTable,
                                                                              std::string_view joinColumnName,
                                                                              std::string_view onMainTableColumn)
{
    return Join(
        joinType,
        joinTable,
        joinColumnName,
        SqlQualifiedTableColumnName { .tableName = SearchCondition().tableName, .columnName = onMainTableColumn });
}

template <typename Derived>
template <typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              std::string_view joinTable,
                                                                              Callable const& onClauseBuilder)
{
    static constexpr std::array<std::string_view, 4> JoinTypeStrings = {
        "INNER",
        "LEFT OUTER",
        "RIGHT OUTER",
        "FULL OUTER",
    };

    size_t const originalSize = SearchCondition().tableJoins.size();
    SearchCondition().tableJoins +=
        std::format("\n {0} JOIN \"{1}\" ON ", JoinTypeStrings[static_cast<std::size_t>(joinType)], joinTable);
    size_t const sizeBefore = SearchCondition().tableJoins.size();
    onClauseBuilder(SqlJoinConditionBuilder { joinTable, &SearchCondition().tableJoins });
    size_t const sizeAfter = SearchCondition().tableJoins.size();
    if (sizeBefore == sizeAfter)
        SearchCondition().tableJoins.resize(originalSize);

    return static_cast<Derived&>(*this);
}

} // namespace detail
