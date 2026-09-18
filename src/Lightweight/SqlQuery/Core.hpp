// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../Utils.hpp"

#include <algorithm>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>

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
constexpr inline auto SqlWildcard = SqlWildcardType {};

/// @brief Name of table in a SQL query, where the table's name is aliased.
struct AliasedTableName
{
    /// The table name.
    std::string_view tableName;
    /// The alias for the table.
    std::string_view alias;

    /// Three-way comparison operator.
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

    /// @brief Writes a literal value into a SQL fragment.
    ///
    /// Shared by the WHERE and the ON clause so a value is spelled the same way wherever it lands.
    ///
    /// @param value     The value to write. Column names, NULL, wildcards and raw conditions are
    ///                  written as themselves; everything else is formatted, and quoted when its
    ///                  type needs quoting.
    /// @param target    The fragment to append to.
    /// @param formatter Supplies the dialect's spelling of a boolean literal.
    template <typename LiteralType, typename TargetType>
    void AppendLiteralValueInto(LiteralType const& value, TargetType& target, SqlQueryFormatter const& formatter);

} // namespace detail

/// @brief Helper function to create a SqlQualifiedTableColumnName from string_view
///
/// @param column The column name, which must be qualified with a table name.
/// Example QualifiedColumnName<"Table.Column"> will create a SqlQualifiedTableColumnName with
/// tableName = "Table" and columnName = "Column".
template <Reflection::StringLiteral columnLiteral>
constexpr SqlQualifiedTableColumnName QualifiedColumnName = []() consteval {
#if !defined(_MSC_VER)
    // enforce that we do not have symbols \ [ ] " '
    static_assert(
        !std::ranges::any_of(columnLiteral,
                             [](char c) consteval { return c == '\\' || c == '[' || c == ']' || c == '"' || c == '\''; }),
        "QualifiedColumnName should not contain symbols \\ [ ] \" '");
#endif

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

    /// How many of @c inputBindings were contributed by ON clauses.
    ///
    /// A JOIN precedes the WHERE clause in the statement, so a value bound by an ON clause is
    /// inserted ahead of the WHERE values rather than appended. Tracking the count is what keeps
    /// the vector in statement order however the caller interleaves joins and WHERE terms.
    std::size_t joinBindingCount = 0;
};

/// @brief Query builder for building JOIN conditions.
///
/// An ON clause can say what a WHERE clause can: compare the joined table's column against another
/// table's column or against a value, test it for null, and group terms in parentheses. The
/// distinction matters for an outer join, where moving a term from the ON clause to the WHERE
/// clause turns the join into an inner one.
///
/// @ingroup QueryBuilder
class SqlJoinConditionBuilder
{
  public:
    /// Constructs a new SqlJoinConditionBuilder.
    ///
    /// @param referenceTable  The table being joined; the left operand of every condition.
    /// @param searchCondition The query's search condition, whose @c tableJoins is written into and
    ///                        whose bindings vector, when present, receives the compared values.
    /// @param formatter       Supplies the dialect's spelling of a boolean literal.
    explicit SqlJoinConditionBuilder(std::string_view referenceTable,
                                     SqlSearchCondition* searchCondition,
                                     SqlQueryFormatter const* formatter) noexcept:
        _referenceTable { referenceTable },
        _searchCondition { *searchCondition },
        _formatter { *formatter }
    {
    }

    /// Adds an AND join condition.
    SqlJoinConditionBuilder& On(std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
    {
        return Operator(joinColumnName, onOtherColumn, "AND");
    }

    /// Adds an OR join condition.
    SqlJoinConditionBuilder& OrOn(std::string_view joinColumnName, SqlQualifiedTableColumnName onOtherColumn)
    {
        return Operator(joinColumnName, onOtherColumn, "OR");
    }

    /// Adds a join condition with a custom operator.
    SqlJoinConditionBuilder& Operator(std::string_view joinColumnName,
                                      SqlQualifiedTableColumnName onOtherColumn,
                                      std::string_view op)
    {
        AppendJunctor(op);
        AppendReferenceColumn(joinColumnName);
        Condition() += " = ";
        detail::AppendLiteralValueInto(onOtherColumn, Condition(), _formatter);
        return *this;
    }

    /// Adds an AND join condition testing the joined column against @p value for equality.
    ///
    /// The value is bound when the query was given a bindings vector, and written into the
    /// statement text otherwise -- the same rule the WHERE clause follows. A query whose ON clause
    /// binds must be run through @c SqlStatement::ExecuteWithVariants, and the vector and anything
    /// a stored string_view points at have to outlive the execution.
    template <typename T>
    SqlJoinConditionBuilder& OnValue(std::string_view joinColumnName, T const& value)
    {
        return ValueOperator(joinColumnName, "=", value, "AND");
    }

    /// Adds an AND join condition testing the joined column against @p value with @p binaryOp.
    template <typename T>
    SqlJoinConditionBuilder& OnValue(std::string_view joinColumnName, std::string_view binaryOp, T const& value)
    {
        return ValueOperator(joinColumnName, binaryOp, value, "AND");
    }

    /// Adds an OR join condition testing the joined column against @p value for equality.
    template <typename T>
    SqlJoinConditionBuilder& OrOnValue(std::string_view joinColumnName, T const& value)
    {
        return ValueOperator(joinColumnName, "=", value, "OR");
    }

    /// Adds an OR join condition testing the joined column against @p value with @p binaryOp.
    template <typename T>
    SqlJoinConditionBuilder& OrOnValue(std::string_view joinColumnName, std::string_view binaryOp, T const& value)
    {
        return ValueOperator(joinColumnName, binaryOp, value, "OR");
    }

    /// Adds an AND join condition testing the joined column for NULL.
    SqlJoinConditionBuilder& OnNull(std::string_view joinColumnName)
    {
        return NullTest(joinColumnName, NullSense::Null, "AND");
    }

    /// Adds an AND join condition testing the joined column for NOT NULL.
    SqlJoinConditionBuilder& OnNotNull(std::string_view joinColumnName)
    {
        return NullTest(joinColumnName, NullSense::NotNull, "AND");
    }

    /// Adds an OR join condition testing the joined column for NULL.
    SqlJoinConditionBuilder& OrOnNull(std::string_view joinColumnName)
    {
        return NullTest(joinColumnName, NullSense::Null, "OR");
    }

    /// Adds an OR join condition testing the joined column for NOT NULL.
    SqlJoinConditionBuilder& OrOnNotNull(std::string_view joinColumnName)
    {
        return NullTest(joinColumnName, NullSense::NotNull, "OR");
    }

    /// Adds an AND group of join conditions, in parentheses.
    ///
    /// The parentheses are what keep `a AND (b OR c)` from becoming `a AND b OR c`, which selects
    /// strictly more.
    template <typename Callable>
        requires std::invocable<Callable, SqlJoinConditionBuilder&>
    SqlJoinConditionBuilder& OnGroup(Callable const& build)
    {
        return Group(build, "AND");
    }

    /// Adds an OR group of join conditions, in parentheses.
    template <typename Callable>
        requires std::invocable<Callable, SqlJoinConditionBuilder&>
    SqlJoinConditionBuilder& OrOnGroup(Callable const& build)
    {
        return Group(build, "OR");
    }

  private:
    [[nodiscard]] std::string& Condition() noexcept
    {
        return _searchCondition.tableJoins;
    }

    void AppendJunctor(std::string_view op)
    {
        if (_firstCall)
            _firstCall = false;
        else
            Condition() += std::format(" {} ", op);
    }

    void AppendReferenceColumn(std::string_view joinColumnName)
    {
        Condition() += '"';
        Condition() += _referenceTable;
        Condition() += "\".\"";
        Condition() += joinColumnName;
        Condition() += '"';
    }

    template <typename T>
    SqlJoinConditionBuilder& ValueOperator(std::string_view joinColumnName,
                                           std::string_view binaryOp,
                                           T const& value,
                                           std::string_view op)
    {
        AppendJunctor(op);
        AppendReferenceColumn(joinColumnName);
        Condition() += std::format(" {} ", binaryOp);

        if (_searchCondition.inputBindings != nullptr)
        {
            Condition() += '?';
            // Ahead of the WHERE values, because the JOIN clause precedes the WHERE clause.
            _searchCondition.inputBindings->insert(_searchCondition.inputBindings->begin()
                                                       + static_cast<std::ptrdiff_t>(_searchCondition.joinBindingCount),
                                                   SqlVariant { value });
            ++_searchCondition.joinBindingCount;
        }
        else
            detail::AppendLiteralValueInto(value, Condition(), _formatter);

        return *this;
    }

    enum class NullSense : std::uint8_t
    {
        Null,
        NotNull,
    };

    SqlJoinConditionBuilder& NullTest(std::string_view joinColumnName, NullSense sense, std::string_view op)
    {
        AppendJunctor(op);
        AppendReferenceColumn(joinColumnName);
        Condition() += sense == NullSense::Null ? " IS NULL" : " IS NOT NULL";
        return *this;
    }

    template <typename Callable>
    SqlJoinConditionBuilder& Group(Callable const& build, std::string_view op)
    {
        // Remember the state before the junctor so an empty group can roll the junctor back too --
        // otherwise a dangling " AND " is left where the group would have been.
        auto const sizeBeforeJunctor = Condition().size();
        bool const wasFirstCall = _firstCall;

        AppendJunctor(op);

        auto const sizeBeforeParen = Condition().size();
        Condition() += '(';

        SqlJoinConditionBuilder nested { _referenceTable, &_searchCondition, &_formatter };
        build(nested);

        // An empty group would render as "()", which no dialect accepts. Drop the junctor and the
        // opening parenthesis, and restore the first-call flag so the next condition is not junctored.
        if (Condition().size() == sizeBeforeParen + 1)
        {
            Condition().resize(sizeBeforeJunctor);
            _firstCall = wasFirstCall;
            return *this;
        }

        Condition() += ')';
        return *this;
    }

    std::string_view _referenceTable;
    SqlSearchCondition& _searchCondition;
    SqlQueryFormatter const& _formatter;
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

    /// @brief Starts a conditional WHERE chain driven by a `std::optional<T>` value.
    ///
    /// The returned sub-builder exposes two `ThenWhere` overloads:
    /// `ThenWhere(column)` appends `WHERE column = *value`, and
    /// `ThenWhere(column, binaryOp)` appends `WHERE column <binaryOp> *value`
    /// (e.g. `">="`, `"<"`, `"!="`). Both overloads emit nothing when @p value
    /// is empty and return the underlying builder for further chaining.
    ///
    /// The optional is captured by reference and must outlive the chain.
    ///
    /// @tparam T The contained value type held by the optional.
    /// @param value The optional whose presence gates the conditional WHERE.
    /// @return A sub-builder exposing `ThenWhere(column)` and `ThenWhere(column, binaryOp)`.
    ///
    /// Example:
    /// @code
    /// std::optional<int> val { 42 };
    /// builder.If(val).ThenWhere(FullyQualifiedNameOf<&Table::value>);
    /// // Appends: WHERE "Table"."value" = 42
    ///
    /// std::optional<SqlDateTime> since = ...;
    /// builder.If(since).ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, ">=");
    /// // Appends: WHERE "Events"."createdAt" >= '2026-05-18T12:30:45.000'  (when since holds a value)
    /// @endcode
    template <typename T>
    [[nodiscard]] auto If(std::optional<T> const& value) noexcept;

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
    /// @endcode
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

    /// Appends a column name to the WHERE condition.
    template <typename ColumnName>
        requires(std::same_as<ColumnName, SqlQualifiedTableColumnName> || std::convertible_to<ColumnName, std::string_view>
                 || std::convertible_to<ColumnName, std::string>)
    void AppendColumnName(ColumnName const& columnName);

    /// Appends a literal value to the WHERE condition.
    template <typename LiteralType>
    void AppendLiteralValue(LiteralType const& value);

    /// Populates a literal value into the target string.
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

        /// @brief The name of each projected column, in result-column order, as spelled by the caller.
        ///
        /// Populated by the projection methods of @c SqlSelectQueryBuilder so that result columns can be
        /// addressed by name without asking the driver for result-set metadata (which cannot report table
        /// names portably). An entry is empty for a projection that carries no caller-given name, such as
        /// an un-aliased aggregate; the empty slot keeps the remaining entries aligned with their columns.
        std::vector<std::string> projectedFieldNames;

        /// @brief Whether the projection contains a wildcard, whose column count is unknown at build time.
        ///
        /// A wildcard makes every position after it unpredictable, so named column access is unavailable
        /// for the whole query. Kept separate from an empty @c projectedFieldNames so the diagnostic can
        /// name the actual cause.
        bool projectionHasWildcard = false;

        std::string orderBy;
        std::string groupBy;

        size_t offset = 0;
        size_t limit = (std::numeric_limits<size_t>::max)();

        [[nodiscard]] LIGHTWEIGHT_API std::string ToSql() const;

        /// @brief The projected column names, in result-column order, for named column access.
        /// @return One entry per result column, empty for unnamed projections; an empty span when the
        ///         query carries no usable mapping (e.g. the projection contains a wildcard).
        [[nodiscard]] std::span<std::string const> ProjectedFieldNames() const noexcept
        {
            return projectedFieldNames;
        }

        /// @copydoc projectionHasWildcard
        [[nodiscard]] bool ProjectionHasWildcard() const noexcept
        {
            return projectionHasWildcard;
        }
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

    /// Constructs or extends a GROUP BY clause with a qualified column name.
    Derived& GroupBy(SqlQualifiedTableColumnName const& columnName);

    using ComposedQuery = detail::ComposedQuery;

  protected:
    // mutable so const finalizers / projection-helpers can delegate to the
    // non-const implementations via const_cast (idiomatic builder pattern —
    // the observable const-state is the produced SQL, not the accumulator).
    mutable ComposedQuery _query {}; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
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
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlBasicSelectQueryBuilder<Derived>::GroupBy(
    SqlQualifiedTableColumnName const& columnName)
{
    if (_query.groupBy.empty())
        _query.groupBy += "\n GROUP BY ";
    else
        _query.groupBy += ", ";

    _query.groupBy += '"';
    _query.groupBy += columnName.tableName;
    _query.groupBy += "\".\"";
    _query.groupBy += columnName.columnName;
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

/// Constructs or extends a WHERE clause to test for equality.
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

/// Constructs or extends a WHERE/OR clause to test for equality.
template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::OrWhere(ColumnName const& columnName,
                                                                                 T const& value)
{
    return Or().Where(columnName, value);
}

/// Constructs or extends a WHERE/OR clause to test for a group of values.
template <typename Derived>
template <typename Callable>
    requires std::invocable<Callable, SqlWhereClauseBuilder<Derived>&>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::OrWhere(Callable const& callable)
{
    return Or().Where(callable);
}

/// Constructs or extends a WHERE/AND clause to test for a group of values.
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

/// Constructs or extends a WHERE IN clause with an input range.
template <typename Derived>
template <typename ColumnName, std::ranges::input_range InputRange>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 InputRange const& values)
{
    // An empty IN-set means "match nothing"; emitting no condition would mean "match everything".
    // `1 = 0` rather than `FALSE` because SQL Server has no boolean literal.
    //
    // std::ranges::empty rather than values.empty(): the latter requires a member function, which
    // excludes ranges such as built-in arrays that this overload otherwise handles fine.
    if (std::ranges::empty(values))
        return WhereRaw("1 = 0");
    return Where(columnName, "IN", PopulateSqlSetExpression(values));
}

/// Constructs or extends a WHERE IN clause with an initializer list.
template <typename Derived>
template <typename ColumnName, typename T>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 std::initializer_list<T> const& values)
{
    // See the range overload above: an empty IN-set must not silently drop the condition.
    if (values.begin() == values.end())
        return WhereRaw("1 = 0");
    return Where(columnName, "IN", PopulateSqlSetExpression(values));
}

/// Constructs or extends a WHERE IN clause with a sub-select query.
template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereIn(ColumnName const& columnName,
                                                                                 SubSelectQuery const& subSelectQuery)
{
    return Where(columnName, "IN", detail::RawSqlCondition { "(" + subSelectQuery.ToSql() + ")" });
}

/// Constructs or extends a WHERE clause to test for a value being not null.
template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNotNull(ColumnName const& columnName)
{
    return Where(columnName, "IS NOT", detail::RawSqlCondition { "NULL" });
}

/// Constructs or extends a WHERE clause to test for a value being null.
template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereNull(ColumnName const& columnName)
{
    return Where(columnName, "IS", detail::RawSqlCondition { "NULL" });
}

/// Constructs or extends a WHERE clause to test for inequality.
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

/// Constructs or extends a WHERE clause to test for a value being true.
template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereTrue(ColumnName const& columnName)
{
    return Where(columnName, "=", true);
}

/// Constructs or extends a WHERE clause to test for a value being false.
template <typename Derived>
template <typename ColumnName>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::WhereFalse(ColumnName const& columnName)
{
    return Where(columnName, "=", false);
}

/// @brief Largest IN-set that WhereIn passes as bound parameters.
///
/// Beyond this the set is written into the SQL text as literals, which is what WhereIn did before
/// it learned to bind. Drivers cap how many parameters one statement may carry -- MS SQL Server
/// refuses more than 2100 with "07002 COUNT field incorrect" -- and a caller filtering on a few
/// thousand keys would otherwise build a statement no driver accepts. The margin below that cap
/// leaves room for the parameters the rest of the statement contributes.
///
/// @ingroup QueryBuilder
constexpr inline std::size_t SqlMaxBoundSetSize = 2000;

template <typename T>
struct WhereConditionLiteralType
{
    constexpr static bool needsQuotes = !std::is_integral_v<T> && !std::is_floating_point_v<T> && !std::same_as<T, bool>
                                        && !std::same_as<T, SqlWildcardType>;
};

/// Constructs or extends a WHERE clause with a string literal value.
template <typename Derived>
template <typename ColumnName, std::size_t N>
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               char const (&value)[N])
{
    return Where(columnName, binaryOp, std::string_view { value, N - 1 });
}

/// Constructs or extends a WHERE clause to test for a binary operation.
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

/// Constructs or extends a WHERE clause with a sub-select query.
template <typename Derived>
template <typename ColumnName, typename SubSelectQuery>
    requires(std::is_invocable_r_v<std::string, decltype(&SubSelectQuery::ToSql), SubSelectQuery const&>)
inline LIGHTWEIGHT_FORCE_INLINE Derived& SqlWhereClauseBuilder<Derived>::Where(ColumnName const& columnName,
                                                                               std::string_view binaryOp,
                                                                               SubSelectQuery const& value)
{
    return Where(columnName, binaryOp, detail::RawSqlCondition { "(" + value.ToSql() + ")" });
}

/// Constructs or extends a WHERE/OR clause with a binary operation.
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
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    return Join(JoinType::INNER,
                RecordTableName<MemberClassType<LeftField>>,
                FieldNameOf<LeftField>,
                SqlQualifiedTableColumnName { RecordTableName<MemberClassType<RightField>>, FieldNameOf<RightField> });
#else
    return Join(
        JoinType::INNER,
        RecordTableName<Reflection::MemberClassType<LeftField>>,
        FieldNameOf<LeftField>,
        SqlQualifiedTableColumnName { RecordTableName<Reflection::MemberClassType<RightField>>, FieldNameOf<RightField> });
#endif
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

namespace detail
{

    /// @brief Sub-builder returned by `SqlWhereClauseBuilder::If`.
    ///
    /// Captures the underlying builder and a gating `std::optional`. Calling
    /// `ThenWhere(column)` or `ThenWhere(column, binaryOp)` commits the chain:
    /// when the optional holds a value it appends `WHERE column = *value` (or
    /// `WHERE column <binaryOp> *value` for the explicit-operator overload);
    /// either way it returns the underlying builder so further methods can be
    /// chained.
    template <typename Derived, typename T>
    class [[nodiscard]] ConditionalWhereBuilder
    {
      public:
        /// Constructs the sub-builder, binding to the underlying builder and the gating optional.
        constexpr ConditionalWhereBuilder(Derived& builder, std::optional<T> const& value) noexcept:
            _builder { builder },
            _value { value }
        {
        }

        /// Commits the conditional WHERE for @p column. Appends
        /// `WHERE column = *value` when the gating optional holds a value;
        /// otherwise the builder is left untouched.
        /// @param column The column name (string, `SqlQualifiedTableColumnName`, etc.).
        /// @return Reference to the underlying query builder.
        template <typename ColumnName>
        [[nodiscard]] Derived& ThenWhere(ColumnName const& column) const
        {
            if (_value.has_value())
                return _builder.Where(column, *_value);
            return _builder;
        }

        /// Commits the conditional WHERE for @p column using an explicit binary
        /// operator. Appends `WHERE column <binaryOp> *value` when the gating
        /// optional holds a value; otherwise the builder is left untouched.
        /// Mirrors the `Where(column, binaryOp, value)` overload — use it for
        /// range-style filters such as `">="`, `"<"`, `"!="`, or `"LIKE"`.
        /// @param column The column name (string, `SqlQualifiedTableColumnName`, etc.).
        /// @param binaryOp The SQL binary operator (e.g. `">="`, `"<"`, `"!="`).
        /// @return Reference to the underlying query builder.
        ///
        /// Example:
        /// @code
        /// std::optional<SqlDateTime> since = ...;
        /// std::optional<SqlDateTime> until = ...;
        /// q.FromTable("Events").Select().Field("id")
        ///     .If(since).ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, ">=")
        ///     .If(until).ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, "<")
        ///     .All();
        /// @endcode
        template <typename ColumnName>
        [[nodiscard]] Derived& ThenWhere(ColumnName const& column, std::string_view binaryOp) const
        {
            if (_value.has_value())
                return _builder.Where(column, binaryOp, *_value);
            return _builder;
        }

      private:
        Derived& _builder;
        std::optional<T> const& _value;
    };

} // namespace detail

/// Starts a conditional WHERE chain gated by a `std::optional` value.
template <typename Derived>
template <typename T>
inline LIGHTWEIGHT_FORCE_INLINE auto SqlWhereClauseBuilder<Derived>::If(std::optional<T> const& value) noexcept
{
    return detail::ConditionalWhereBuilder<Derived, T> { static_cast<Derived&>(*this), value };
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

/// Appends a column name to the WHERE condition.
template <typename Derived>
template <typename ColumnName>
    requires(std::same_as<ColumnName, SqlQualifiedTableColumnName> || std::convertible_to<ColumnName, std::string_view>
             || std::convertible_to<ColumnName, std::string>)
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::AppendColumnName(ColumnName const& columnName)
{
    SearchCondition().condition += detail::MakeSqlColumnName(columnName);
}

/// Appends a literal value to the WHERE condition.
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

namespace detail
{

    template <typename LiteralType, typename TargetType>
    void AppendLiteralValueInto(LiteralType const& value, TargetType& target, SqlQueryFormatter const& formatter)
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
            target += formatter.BooleanLiteral(value);
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

} // namespace detail

/// Populates a literal value into the target string.
template <typename Derived>
template <typename LiteralType, typename TargetType>
inline LIGHTWEIGHT_FORCE_INLINE void SqlWhereClauseBuilder<Derived>::PopulateLiteralValueInto(LiteralType const& value,
                                                                                              TargetType& target)
{
    detail::AppendLiteralValueInto(value, target, Formatter());
}

template <typename Derived>
template <typename LiteralType>
detail::RawSqlCondition SqlWhereClauseBuilder<Derived>::PopulateSqlSetExpression(LiteralType const& values)
{
    using namespace std::string_view_literals;

    using ValueType = std::ranges::range_value_t<LiteralType>;

    // Mirrors the dispatch in AppendLiteralValue: these types have no parameter representation and
    // must stay inline in the SQL text. Everything else becomes a parameter marker whenever the
    // caller supplied a bindings vector, so that the statement stays reusable across differing
    // IN-sets and the driver — not this builder — encodes the value.
    constexpr bool isBindable =
        !(std::is_same_v<ValueType, SqlQualifiedTableColumnName> || detail::OneOf<ValueType, SqlNullType, std::nullopt_t>
          || std::is_same_v<ValueType, SqlWildcardType> || std::is_same_v<ValueType, detail::RawSqlCondition>);

    auto& searchCondition = SearchCondition();

    std::ostringstream fragment;

    // String literals decay to a raw character pointer inside an initializer list (and inside a
    // built-in array), and SqlVariant cannot be constructed from one: std::variant's converting
    // constructor is ambiguous between std::string and std::string_view. Hand such elements over as
    // views, mirroring the dedicated char-array overload of Where().
    auto const asBindable = [](auto const& value) -> decltype(auto) {
        using Decayed = std::decay_t<decltype(value)>;
        if constexpr (detail::OneOf<Decayed, char*, char const*>)
            return std::string_view { value };
        else if constexpr (detail::OneOf<Decayed, char16_t*, char16_t const*>)
            return std::u16string_view { value };
        else
            return (value);
    };

    // A set too large to be passed as parameters goes into the SQL text instead. Only a sized range
    // can be measured without consuming it; an unsized one keeps binding, as it did before.
    bool bindValues = searchCondition.inputBindings != nullptr;
    if constexpr (std::ranges::sized_range<LiteralType>)
        if (std::ranges::size(values) > SqlMaxBoundSetSize)
            bindValues = false;

    auto const appendValue = [&](auto const& value) {
        if constexpr (isBindable)
        {
            if (bindValues)
            {
                fragment << '?';
                searchCondition.inputBindings->emplace_back(asBindable(value));
                return;
            }
        }
        std::string valueString;
        PopulateLiteralValueInto(value, valueString);
        fragment << valueString;
    };

    fragment << '(';
#if !defined(__cpp_lib_ranges_enumerate)
    int index { -1 };
    for (auto const& value: values)
    {
        ++index;
#else
    for (auto const&& [index, value]: values | std::views::enumerate)
    {
#endif
        if (index > 0)
            fragment << ", "sv;

        appendValue(value);
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

/// Constructs a JOIN clause with a custom ON clause builder.
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
    onClauseBuilder(SqlJoinConditionBuilder { joinTable, &SearchCondition(), &Formatter() });
    size_t const sizeAfter = SearchCondition().tableJoins.size();
    if (sizeBefore == sizeAfter)
        SearchCondition().tableJoins.resize(originalSize);

    return static_cast<Derived&>(*this);
}

} // namespace Lightweight
