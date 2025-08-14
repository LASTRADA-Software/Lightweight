// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlQueryFormatter.hpp"

#include <concepts>
#include <ranges>
#include <algorithm>

namespace Lightweight
{

/// @defgroup QueryBuilder Query Builder
///
/// @brief The query builder is a high level API for building SQL queries using high level C++ syntax.

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

/// @brief Name of table in a SQL query, where the table's name is aliased.
struct AliasedTableName
{
    std::string_view tableName;
    std::string_view alias;

    std::weak_ordering operator<=>(AliasedTableName const&) const = default;
};

template <typename T>
concept TableName =
    std::convertible_to<T, std::string_view> || std::convertible_to<T, std::string> || std::same_as<T, AliasedTableName>;

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

/// @brief Helper function to create a SqlQualifiedTableColumnName from string_view
///
/// @param column The column name, which must be qualified with a table name.
/// Example QualifiedColumnName<"Table.Column"> will create a SqlQualifiedTableColumnName with
/// tableName = "Table" and columnName = "Column".
template <Reflection::StringLiteral columnLiteral>
constexpr SqlQualifiedTableColumnName QualifiedColumnName = []() consteval {
    // enforce that we do not have symbols \ [ ] " '
    static_assert(!std::ranges::any_of(columnLiteral,
                                       [](char c) { return c == '\\' || c == '[' || c == ']' || c == '"' || c == '\''; }),
                  "QualifiedColumnName should not contain symbols \\ [ ] \" '");

    static_assert(std::ranges::count(columnLiteral, '.') == 1,
                  "QualifiedColumnName requires a column name with a single '.' to separate table and column name");
    constexpr auto column = columnLiteral.sv();
    auto dotPos = column.find('.');
    return SqlQualifiedTableColumnName { .tableName = column.substr(0, dotPos), .columnName = column.substr(dotPos + 1) };
}();

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
        else if constexpr (std::is_same_v<ColumnName, SqlRawColumnNameView>)
        {
            output += columnName.value;
        }
        else if constexpr (std::is_same_v<ColumnName, SqlWildcardType>)
        {
            output += '?';
        }
        else
        {
            output += '"';
            output += columnName;
            output += '"';
        }
        return output;
    }

    template <typename T>
    std::string MakeEscapedSqlString(T const& value)
    {
        std::string escapedValue;
        escapedValue += '\'';

        for (auto const ch: value)
        {
            // In SQL strings, single quotes are escaped by doubling them.
            if (ch == '\'')
                escapedValue += '\'';
            escapedValue += ch;
        }
        escapedValue += '\'';
        return escapedValue;
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

/// Helper CRTP-based class for building WHERE clauses.
///
/// This class is inherited by the SqlSelectQueryBuilder, SqlUpdateQueryBuilder, and SqlDeleteQueryBuilder
///
/// @ingroup QueryBuilder
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
    ///
    /// @param joinTable The table's name to join with. This can be a string, a string_view, or an AliasedTableName.
    /// @param joinColumnName The name of the column in the main table to join on.
    /// @param onOtherColumn The column in the join table to compare against.
    [[nodiscard]] Derived& InnerJoin(TableName auto joinTable,
                                     std::string_view joinColumnName,
                                     SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an INNER JOIN clause.
    [[nodiscard]] Derived& InnerJoin(TableName auto joinTable,
                                     std::string_view joinColumnName,
                                     std::string_view onMainTableColumn);

    /// Constructs an INNER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& InnerJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an `INNER JOIN` clause given two fields from different records
    /// using the field name as join column.
    ///
    /// @tparam LeftField  The field name to join on, such as `JoinTestB::a_id`, which will join on table `JoinTestB` with
    /// the column `a_id` to be compared against right field's column.
    /// @tparam RightField The other column to compare and join against.
    ///
    /// Example:
    /// @code
    /// InnerJoin<&JoinTestB::a_id, &JoinTestA::id>()
    /// // This will generate a INNER JOIN "JoinTestB" ON "InnerTestB"."a_id" = "JoinTestA"."id"
    template <auto LeftField, auto RightField>
    [[nodiscard]] Derived& InnerJoin();

    /// Constructs an LEFT OUTER JOIN clause.
    [[nodiscard]] Derived& LeftOuterJoin(TableName auto joinTable,
                                         std::string_view joinColumnName,
                                         SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an LEFT OUTER JOIN clause.
    [[nodiscard]] Derived& LeftOuterJoin(TableName auto joinTable,
                                         std::string_view joinColumnName,
                                         std::string_view onMainTableColumn);

    /// Constructs an LEFT OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& LeftOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an RIGHT OUTER JOIN clause.
    [[nodiscard]] Derived& RightOuterJoin(TableName auto joinTable,
                                          std::string_view joinColumnName,
                                          SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an RIGHT OUTER JOIN clause.
    [[nodiscard]] Derived& RightOuterJoin(TableName auto joinTable,
                                          std::string_view joinColumnName,
                                          std::string_view onMainTableColumn);

    /// Constructs an RIGHT OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& RightOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder);

    /// Constructs an FULL OUTER JOIN clause.
    [[nodiscard]] Derived& FullOuterJoin(TableName auto joinTable,
                                         std::string_view joinColumnName,
                                         SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs an FULL OUTER JOIN clause.
    [[nodiscard]] Derived& FullOuterJoin(TableName auto joinTable,
                                         std::string_view joinColumnName,
                                         std::string_view onMainTableColumn);

    /// Constructs an FULL OUTER JOIN clause with a custom ON clause.
    template <typename OnChainCallable>
        requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
    [[nodiscard]] Derived& FullOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder);

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
        requires(std::same_as<ColumnName, SqlQualifiedTableColumnName> || std::convertible_to<ColumnName, std::string_view>
                 || std::same_as<ColumnName, SqlRawColumnNameView> || std::convertible_to<ColumnName, std::string>)
    void AppendColumnName(ColumnName const& columnName);

    template <typename LiteralType>
    void AppendLiteralValue(LiteralType const& value);

    template <typename LiteralType, typename TargetType>
    void PopulateLiteralValueInto(LiteralType const& value, TargetType& target);

    template <typename LiteralType>
    detail::RawSqlCondition PopulateSqlSetExpression(LiteralType const& values);

    enum class JoinType : uint8_t
    {
        INNER,
        LEFT,
        RIGHT,
        FULL
    };

    /// Constructs a JOIN clause.
    [[nodiscard]] Derived& Join(JoinType joinType,
                                TableName auto joinTable,
                                std::string_view joinColumnName,
                                SqlQualifiedTableColumnName onOtherColumn);

    /// Constructs a JOIN clause.
    [[nodiscard]] Derived& Join(JoinType joinType,
                                TableName auto joinTable,
                                std::string_view joinColumnName,
                                std::string_view onMainTableColumn);

    /// Constructs a JOIN clause.
    template <typename OnChainCallable>
    [[nodiscard]] Derived& Join(JoinType joinType, TableName auto joinTable, OnChainCallable const& onClauseBuilder);
};

enum class SqlResultOrdering : uint8_t
{
    ASCENDING,
    DESCENDING
};

namespace detail
{
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
} // namespace detail

template <typename Derived>
class [[nodiscard]] SqlBasicSelectQueryBuilder: public SqlWhereClauseBuilder<Derived>
{
  public:
    /// Adds a DISTINCT clause to the SELECT query.
    Derived& Distinct() noexcept;

    /// Constructs or extends a ORDER BY clause.
    Derived& OrderBy(SqlQualifiedTableColumnName const& columnName,
                     SqlResultOrdering ordering = SqlResultOrdering::ASCENDING);

    /// Constructs or extends a ORDER BY clause.
    Derived& OrderBy(std::string_view columnName, SqlResultOrdering ordering = SqlResultOrdering::ASCENDING);

    /// Constructs or extends a GROUP BY clause.
    Derived& GroupBy(std::string_view columnName);

    using ComposedQuery = detail::ComposedQuery;

  protected:
    ComposedQuery _query {}; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlBasicSelectQueryBuilder<Derived>::Distinct() noexcept
{
    _query.distinct = true;
    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlBasicSelectQueryBuilder<Derived>::OrderBy(std::string_view columnName,
                                                                                      SqlResultOrdering ordering)
{
    if (_query.orderBy.empty())
        _query.orderBy += "\n ORDER BY ";
    else
        _query.orderBy += ", ";

    _query.orderBy += '"';
    _query.orderBy += columnName;
    _query.orderBy += '"';

    if (ordering == SqlResultOrdering::DESCENDING)
        _query.orderBy += " DESC";
    else if (ordering == SqlResultOrdering::ASCENDING)
        _query.orderBy += " ASC";

    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlBasicSelectQueryBuilder<Derived>::OrderBy(
    SqlQualifiedTableColumnName const& columnName, SqlResultOrdering ordering)
{
    if (_query.orderBy.empty())
        _query.orderBy += "\n ORDER BY ";
    else
        _query.orderBy += ", ";

    _query.orderBy += '"';
    _query.orderBy += columnName.tableName;
    _query.orderBy += "\".\"";
    _query.orderBy += columnName.columnName;
    _query.orderBy += '"';

    if (ordering == SqlResultOrdering::DESCENDING)
        _query.orderBy += " DESC";
    else if (ordering == SqlResultOrdering::ASCENDING)
        _query.orderBy += " ASC";

    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlBasicSelectQueryBuilder<Derived>::GroupBy(std::string_view columnName)
{
    if (_query.groupBy.empty())
        _query.groupBy += "\n GROUP BY ";
    else
        _query.groupBy += ", ";

    _query.groupBy += '"';
    _query.groupBy += columnName;
    _query.groupBy += '"';

    return static_cast<Derived&>(*this);
}

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
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName, T const& value)
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

template <typename Derived>
template <typename ColumnName, std::ranges::input_range InputRange>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 InputRange const& values)
{
    return Where(columnName, "IN", PopulateSqlSetExpression(values));
}

template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 std::initializer_list<T> const& values)
{
    return Where(columnName, "IN", PopulateSqlSetExpression(values));
}

template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 SubSelectQuery const& subSelectQuery)
{
    return Where(columnName, "IN", detail::RawSqlCondition { "(" + subSelectQuery.ToSql() + ")" });
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNotNull(ColumnName const& columnName)
{
    return Where(columnName, "IS NOT", detail::RawSqlCondition { "NULL" });
}

template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNull(ColumnName const& columnName)
{
    return Where(columnName, "IS", detail::RawSqlCondition { "NULL" });
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
    AppendLiteralValue(value);

    return static_cast<Derived&>(*this);
}

template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               SubSelectQuery const& value)
{
    return Where(columnName, binaryOp, detail::RawSqlCondition { "(" + value.ToSql() + ")" });
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
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(TableName auto joinTable,
                                                                                   std::string_view joinColumnName,
                                                                                   SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::INNER, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(TableName auto joinTable,
                                                                                   std::string_view joinColumnName,
                                                                                   std::string_view onMainTableColumn)
{
    return Join(JoinType::INNER, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <auto LeftField, auto RightField>
Derived& SqlWhereClauseBuilder<Derived>::InnerJoin()
{
    return Join(
        JoinType::INNER,
        RecordTableName<Reflection::MemberClassType<LeftField>>,
        FieldNameOf<LeftField>,
        SqlQualifiedTableColumnName { RecordTableName<Reflection::MemberClassType<RightField>>, FieldNameOf<RightField> });
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::InnerJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::INNER, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(
    TableName auto joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::LEFT, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(TableName auto joinTable,
                                                                                       std::string_view joinColumnName,
                                                                                       std::string_view onMainTableColumn)
{
    return Join(JoinType::LEFT, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::LeftOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::LEFT, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(
    TableName auto joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::RIGHT, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(TableName auto joinTable,
                                                                                        std::string_view joinColumnName,
                                                                                        std::string_view onMainTableColumn)
{
    return Join(JoinType::RIGHT, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::RightOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::RIGHT, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(
    TableName auto joinTable, std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
{
    return Join(JoinType::FULL, joinTable, joinColumnName, onOtherColumn);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(TableName auto joinTable,
                                                                                       std::string_view joinColumnName,
                                                                                       std::string_view onMainTableColumn)
{
    return Join(JoinType::FULL, joinTable, joinColumnName, onMainTableColumn);
}

template <typename Derived>
template <typename OnChainCallable>
    requires std::invocable<OnChainCallable, SqlJoinConditionBuilder>
Derived& SqlWhereClauseBuilder<Derived>::FullOuterJoin(TableName auto joinTable, OnChainCallable const& onClauseBuilder)
{
    return Join(JoinType::FULL, joinTable, onClauseBuilder);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereRaw(std::string_view sqlConditionExpression)
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
             || std::same_as<ColumnName, SqlRawColumnNameView> || std::convertible_to<ColumnName, std::string>)
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::AppendColumnName(ColumnName const& columnName)
{
    SearchCondition().condition += detail::MakeSqlColumnName(columnName);
}

template <typename Derived>
template <typename LiteralType>
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::AppendLiteralValue(LiteralType const& value)
{
    auto& searchCondition = SearchCondition();

    if constexpr (std::is_same_v<LiteralType, SqlQualifiedTableColumnName>
                  || detail::OneOf<LiteralType, SqlNullType, std::nullopt_t> || std::is_same_v<LiteralType, SqlWildcardType>
                  || std::is_same_v<LiteralType, detail::RawSqlCondition>)
    {
        PopulateLiteralValueInto(value, searchCondition.condition);
    }
    else if (searchCondition.inputBindings)
    {
        searchCondition.condition += '?';
        searchCondition.inputBindings->emplace_back(value);
    }
    else if constexpr (std::is_same_v<LiteralType, bool>)
    {
        searchCondition.condition += Formatter().BooleanLiteral(value);
    }
    else if constexpr (!WhereConditionLiteralType<LiteralType>::needsQuotes)
    {
        searchCondition.condition += std::format("{}", value);
    }
    else
    {
        searchCondition.condition += detail::MakeEscapedSqlString(std::format("{}", value));
    }
}

template <typename Derived>
template <typename LiteralType, typename TargetType>
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::PopulateLiteralValueInto(LiteralType const& value,
                                                                                              TargetType& target)
{
    if constexpr (std::is_same_v<LiteralType, SqlQualifiedTableColumnName>)
    {
        target += '"';
        target += value.tableName;
        target += "\".\"";
        target += value.columnName;
        target += '"';
    }
    else if constexpr (detail::OneOf<LiteralType, SqlNullType, std::nullopt_t>)
    {
        target += "NULL";
    }
    else if constexpr (std::is_same_v<LiteralType, SqlWildcardType>)
    {
        target += '?';
    }
    else if constexpr (std::is_same_v<LiteralType, detail::RawSqlCondition>)
    {
        target += value.condition;
    }
    else if constexpr (std::is_same_v<LiteralType, bool>)
    {
        target += Formatter().BooleanLiteral(value);
    }
    else if constexpr (!WhereConditionLiteralType<LiteralType>::needsQuotes)
    {
        target += std::format("{}", value);
    }
    else
    {
        target += detail::MakeEscapedSqlString(std::format("{}", value));
    }
}

template <typename Derived>
template <typename LiteralType>
detail::RawSqlCondition SqlWhereClauseBuilder<Derived>::PopulateSqlSetExpression(LiteralType const& values)
{
    using namespace std::string_view_literals;
    std::ostringstream fragment;
    fragment << '(';
    for (auto const&& [index, value]: values | std::views::enumerate)
    {
        if (index > 0)
            fragment << ", "sv;

        std::string valueString;
        PopulateLiteralValueInto(value, valueString);
        fragment << valueString;
    }
    fragment << ')';
    return detail::RawSqlCondition { fragment.str() };
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              TableName auto joinTable,
                                                                              std::string_view joinColumnName,
                                                                              SqlQualifiedTableColumnName onOtherColumn)
{
    static constexpr std::array<std::string_view, 4> JoinTypeStrings = {
        "INNER",
        "LEFT OUTER",
        "RIGHT OUTER",
        "FULL OUTER",
    };

    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(joinTable)>, AliasedTableName>)
    {
        SearchCondition().tableJoins += std::format("\n"
                                                    R"( {0} JOIN "{1}" AS "{2}" ON "{2}"."{3}" = "{4}"."{5}")",
                                                    JoinTypeStrings[static_cast<std::size_t>(joinType)],
                                                    joinTable.tableName,
                                                    joinTable.alias,
                                                    joinColumnName,
                                                    onOtherColumn.tableName,
                                                    onOtherColumn.columnName);
    }
    else
    {
        SearchCondition().tableJoins += std::format("\n"
                                                    R"( {0} JOIN "{1}" ON "{1}"."{2}" = "{3}"."{4}")",
                                                    JoinTypeStrings[static_cast<std::size_t>(joinType)],
                                                    joinTable,
                                                    joinColumnName,
                                                    onOtherColumn.tableName,
                                                    onOtherColumn.columnName);
    }
    return static_cast<Derived&>(*this);
}

template <typename Derived>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              TableName auto joinTable,
                                                                              std::string_view joinColumnName,
                                                                              std::string_view onMainTableColumn)
{
    return Join(joinType,
                joinTable,
                joinColumnName,
                SqlQualifiedTableColumnName { .tableName = SearchCondition().tableName, .columnName = onMainTableColumn });
}

template <typename Derived>
template <typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Join(JoinType joinType,
                                                                              TableName auto joinTable,
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

} // namespace Lightweight
