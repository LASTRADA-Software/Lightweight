// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlQueryFormatter.hpp"
#include "../Utils.hpp"
#include "Core.hpp"

#include <reflection-cpp/reflection.hpp>

#include <span>
#include <utility>

namespace Lightweight
{

enum class SqlQueryBuilderMode : uint8_t
{
    Fluent,
    Varying
};

struct [[nodiscard]] SqlFieldExpression final
{
    std::string expression;
};

namespace Aggregate
{

    inline SqlFieldExpression Count(std::string_view field = "*") noexcept
    {
        if (field == "*")
            return SqlFieldExpression { .expression = "COUNT(*)" };
        return SqlFieldExpression { .expression = std::format("COUNT(\"{}\")", field) };
    }

    inline SqlFieldExpression Count(SqlQualifiedTableColumnName const& field) noexcept
    {
        if (field.columnName == "*")
            return SqlFieldExpression { .expression = std::format(R"(COUNT("{}".*))", field.tableName) };
        return SqlFieldExpression { .expression = std::format(R"(COUNT("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Sum(std::string_view field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("SUM(\"{}\")", field) };
    }

    inline SqlFieldExpression Sum(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(SUM("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Avg(std::string_view field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("AVG(\"{}\")", field) };
    }

    inline SqlFieldExpression Avg(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(AVG("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Min(std::string_view field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("MIN(\"{}\")", field) };
    }

    inline SqlFieldExpression Min(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(MIN("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Max(std::string_view field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("MAX(\"{}\")", field) };
    }

    inline SqlFieldExpression Max(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(MAX("{}"."{}"))", field.tableName, field.columnName) };
    }

} // namespace Aggregate

/// @brief Query builder for building SELECT ... queries.
///
/// Not constructed directly by user code; obtained by adding a projection
/// (`Field` / `Fields` / `FieldAs` / `Build`) to a @ref SqlSelectQueryStarter — the
/// type returned by `SqlQueryBuilder::Select()`. The starter intentionally hides
/// the finalizers @ref All, @ref First, and @ref Range so that an empty
/// `Select().All()` (which would emit malformed `SELECT  FROM "T"` SQL) fails at
/// compile time; the gate is in the starter, not here.
///
/// @see SqlQueryBuilder
/// @see SqlSelectQueryStarter
class [[nodiscard]] SqlSelectQueryBuilder: public SqlBasicSelectQueryBuilder<SqlSelectQueryBuilder>
{
  public:
    /// The select query type alias.
    using SelectType = detail::SelectType;

    /// Constructs a SELECT query builder.
    explicit SqlSelectQueryBuilder(SqlQueryFormatter const& formatter, std::string table, std::string tableAlias) noexcept:
        SqlBasicSelectQueryBuilder<SqlSelectQueryBuilder> {},
        _formatter { formatter }
    {
        _query.formatter = &formatter;
        _query.searchCondition.tableName = std::move(table);
        _query.searchCondition.tableAlias = std::move(tableAlias);
        _query.fields.reserve(256);
    }

    /// Sets the builder mode to Varying, allowing varying final query types.
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& Varying() noexcept
    {
        _mode = SqlQueryBuilderMode::Varying;
        return *this;
    }

    /// Adds a sequence of columns to the SELECT clause.
    template <typename... MoreFields>
    SqlSelectQueryBuilder& Fields(std::string_view const& firstField, MoreFields&&... moreFields);

    /// Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(std::string_view const& fieldName);

    /// @copydoc Field(std::string_view const&)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Field(std::string_view const& fieldName) const;

    /// Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(SqlQualifiedTableColumnName const& fieldName);

    /// @copydoc Field(SqlQualifiedTableColumnName const&)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Field(SqlQualifiedTableColumnName const& fieldName) const;

    /// Adds an aggregate function call to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(SqlFieldExpression const& fieldExpression);

    /// @copydoc Field(SqlFieldExpression const&)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Field(SqlFieldExpression const& fieldExpression) const;

    /// Aliases the last added field (a column or an aggregate call) in the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& As(std::string_view alias);

    /// @copydoc As
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& As(std::string_view alias) const;

    /// Adds a sequence of columns to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames);

    /// @copydoc Fields(std::vector<std::string_view> const&)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Fields(std::vector<std::string_view> const& fieldNames) const;

    /// Adds a sequence of columns from the given table to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

    /// @copydoc Fields(std::vector<std::string_view> const&, std::string_view)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Fields(std::vector<std::string_view> const& fieldNames,
                                                                      std::string_view tableName) const;

    /// Adds a sequence of columns from the given table to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

    /// @copydoc Fields(std::initializer_list<std::string_view> const&, std::string_view)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Fields(
        std::initializer_list<std::string_view> const& fieldNames, std::string_view tableName) const;

    /// Adds a sequence of qualified table column names to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::span<SqlQualifiedTableColumnName const> fieldNames);

    /// @copydoc Fields(std::span<SqlQualifiedTableColumnName const>)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Fields(
        std::span<SqlQualifiedTableColumnName const> fieldNames) const;

    /// Adds a sequence of qualified table column names to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::initializer_list<SqlQualifiedTableColumnName const> fieldNames);

    /// @copydoc Fields(std::initializer_list<SqlQualifiedTableColumnName const>)
    /// Const overload: delegates via const_cast — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API SqlSelectQueryBuilder const& Fields(
        std::initializer_list<SqlQualifiedTableColumnName const> fieldNames) const;

    /// Adds a sequence of columns from the given tables to the SELECT clause.
    template <typename FirstRecord, typename... MoreRecords>
    SqlSelectQueryBuilder& Fields();

    /// Adds a single column with an alias to the SELECT clause.
    [[deprecated("Use Field(...).As(\"alias\") instead.")]]
    LIGHTWEIGHT_API SqlSelectQueryBuilder& FieldAs(std::string_view const& fieldName, std::string_view const& alias);

    /// Adds a single column with an alias to the SELECT clause.
    [[deprecated("Use Field(...).As(\"alias\") instead.")]]
    LIGHTWEIGHT_API SqlSelectQueryBuilder& FieldAs(SqlQualifiedTableColumnName const& fieldName,
                                                   std::string_view const& alias);

    /// Builds the query using a callable.
    template <typename Callable>
    SqlSelectQueryBuilder& Build(Callable const& callable);

    /// Finalizes building the query as SELECT COUNT(*) ... query.
    LIGHTWEIGHT_API ComposedQuery Count();

    /// @copydoc Count
    /// Const overload: doesn't mutate @c *this — returns a snapshot ComposedQuery
    /// so the finalizer can be reached through a `const SqlSelectQueryBuilder`.
    [[nodiscard]] LIGHTWEIGHT_API ComposedQuery Count() const;

    /// Finalizes building the query as SELECT field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery All();

    /// @copydoc All
    /// Const overload: doesn't mutate @c *this — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API ComposedQuery All() const;

    /// Finalizes building the query as SELECT TOP n field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery First(size_t count = 1);

    /// @copydoc First
    /// Const overload: doesn't mutate @c *this — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API ComposedQuery First(size_t count = 1) const;

    /// Finalizes building the query as SELECT field names FROM ... query with a range.
    LIGHTWEIGHT_API ComposedQuery Range(std::size_t offset, std::size_t limit);

    /// @copydoc Range
    /// Const overload: doesn't mutate @c *this — see @ref Count() const.
    [[nodiscard]] LIGHTWEIGHT_API ComposedQuery Range(std::size_t offset, std::size_t limit) const;

    // clang-format off
    /// Returns the search condition for the query.
    LIGHTWEIGHT_FORCE_INLINE SqlSearchCondition& SearchCondition() noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        // clang-format on
        return _query.searchCondition;
    }

    // clang-format off
    /// Returns the SQL query formatter.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE SqlQueryFormatter const& Formatter() const noexcept // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        // clang-format on
        return _formatter;
    }

  private:
    SqlQueryFormatter const& _formatter;
    SqlQueryBuilderMode _mode = SqlQueryBuilderMode::Fluent;
    // mutable: see the note on _query in SqlBasicSelectQueryBuilder — the alias
    // flag is part of the projection accumulator, so it follows _query's policy.
    // Marked mutable so the const-qualified Field/Fields/As overloads above can
    // delegate to their non-const counterparts via const_cast.
    mutable bool _aliasAllowed = false;
};

template <typename... MoreFields>
SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::string_view const& firstField, MoreFields&&... moreFields)
{
    using namespace std::string_view_literals;

    std::ostringstream fragment;

    if (!_query.fields.empty())
        fragment << ", "sv;

    fragment << '"' << firstField << '"';

    if constexpr (sizeof...(MoreFields) > 0)
        ((fragment << R"(, ")"sv << std::forward<MoreFields>(moreFields) << '"') << ...);

    _query.fields += fragment.str();
    return *this;
}

/// Builds the query using a callable.
template <typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& SqlSelectQueryBuilder::Build(Callable const& callable)
{
    callable(*this);
    return *this;
}

/// Adds fields from one or more record types to the SELECT clause.
template <typename FirstRecord, typename... MoreRecords>
inline LIGHTWEIGHT_FORCE_INLINE SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields()
{
    if constexpr (sizeof...(MoreRecords) == 0)
    {
        Reflection::EnumerateMembers<FirstRecord>(
            [&]<size_t FieldIndex, typename FieldType>() { Field(FieldNameAt<FieldIndex, FirstRecord>); });
    }
    else
    {
        Reflection::EnumerateMembers<FirstRecord>([&]<size_t FieldIndex, typename FieldType>() {
            Field(SqlQualifiedTableColumnName {
                .tableName = RecordTableName<FirstRecord>,
                .columnName = FieldNameAt<FieldIndex, FirstRecord>,
            });
        });

        (Reflection::EnumerateMembers<MoreRecords>([&]<size_t FieldIndex, typename FieldType>() {
             Field(SqlQualifiedTableColumnName {
                 .tableName = RecordTableName<MoreRecords>,
                 .columnName = FieldNameAt<FieldIndex, MoreRecords>,
             });
         }),
         ...);
    }
    return *this;
}

namespace detail
{
    /// Always-false helper that depends on a template parameter so that a
    /// @c static_assert in a function-template body only fires on instantiation
    /// (and stays portable across compilers that have not implemented P2593).
    template <typename...>
    inline constexpr bool dependent_false = false;
} // namespace detail

/// @brief Compile-time gate for SELECT query construction.
///
/// Returned by `SqlQueryBuilder::Select()`. The starter inherits @em publicly
/// from `SqlSelectQueryBuilder` (so every projection / WHERE / JOIN method
/// resolves directly through inheritance), but defines its own @c All,
/// @c First, and @c Range as deducing-this member templates whose bodies are
/// ill-formed via @c static_assert. Those locals shadow the corresponding base
/// methods at name lookup, so:
///
///   - `Select().All()`, `Select().First()`, `Select().Range(...)` — and the
///     two-step variant `auto q = Select(); q.All();` — fail at compile time
///     with a diagnostic asking the user to add a projection first. Without a
///     projection these would emit malformed `SELECT  FROM "T"` SQL.
///   - `Select().Field(...).All()` compiles: `Field(...)` is inherited and
///     returns `SqlSelectQueryBuilder&`, so the subsequent `All()` resolves
///     against the base type (no shadowing) and reaches the real finalizer.
///   - @c Count is intentionally not overridden — `SELECT COUNT(*) FROM ...`
///     is well-formed without an explicit column list, so the inherited
///     @c Count remains directly callable.
///   - @c Distinct and @c Varying are overridden via deducing this so they
///     return @c Self&& and the starter identity survives them. That keeps
///     the gate intact for `Select().Distinct().All()` while still allowing
///     `Select().Distinct().Fields(...).All()`.
///
/// Methods that conceptually follow the column list (@c Where, @c OrderBy,
/// @c GroupBy, @c InnerJoin, @c LeftOuterJoin, etc.) are reachable on the
/// starter through public inheritance and return `SqlSelectQueryBuilder&`,
/// which technically lets `Select().WhereNotNull("x").All()` compile (a known
/// gate leak the type primarily defends against the empty-`Select().All()`
/// typo, which is far more common).
///
/// For the imperative loop pattern, capture the first projection as a builder
/// reference and continue from there:
///
/// @code
/// auto starter = qb.Select();
/// auto& query  = starter.Field(columns[0].name);
/// for (size_t i = 1; i < columns.size(); ++i)
///     query.Field(columns[i].name);
/// return query.All();
/// @endcode
class [[nodiscard]] SqlSelectQueryStarter final: public SqlSelectQueryBuilder
{
  public:
    /// Constructs a SELECT query starter.
    ///
    /// Intentionally not @c explicit so the factory in @ref SqlQueryBuilder::Select
    /// can return via braced init.
    SqlSelectQueryStarter(SqlQueryFormatter const& formatter, std::string table, std::string tableAlias) noexcept:
        SqlSelectQueryBuilder { formatter, std::move(table), std::move(tableAlias) }
    {
    }

    /// Empty-projection guard for the @c All finalizer.
    ///
    /// Instantiating this template emits a @c static_assert telling the caller
    /// to add a projection first. Calling `All()` on a `SqlSelectQueryBuilder&`
    /// returned by `Field(...)` / `Fields(...)` bypasses the shadow and reaches
    /// the real `SqlSelectQueryBuilder::All()`.
    template <typename Self>
    auto All(this Self const& self) -> ComposedQuery
    {
        (void) self;
        static_assert(detail::dependent_false<Self>,
                      "SELECT requires a projection. Call .Field(...) or .Fields(...) "
                      "before .All() — an empty projection produces malformed "
                      "`SELECT  FROM \"T\"` SQL.");
        std::unreachable();
    }

    /// Empty-projection guard for the @c First finalizer — see @ref All.
    template <typename Self>
    auto First(this Self const& self, size_t count = 1) -> ComposedQuery
    {
        (void) self;
        (void) count;
        static_assert(detail::dependent_false<Self>,
                      "SELECT requires a projection. Call .Field(...) or .Fields(...) "
                      "before .First().");
        std::unreachable();
    }

    /// Empty-projection guard for the @c Range finalizer — see @ref All.
    template <typename Self>
    auto Range(this Self const& self, std::size_t offset, std::size_t limit) -> ComposedQuery
    {
        (void) self;
        (void) offset;
        (void) limit;
        static_assert(detail::dependent_false<Self>,
                      "SELECT requires a projection. Call .Field(...) or .Fields(...) "
                      "before .Range().");
        std::unreachable();
    }

    /// State-preserving override: returns @c Self&&, so the starter identity
    /// survives the call and the @c All / @c First / @c Range guards above
    /// keep their gate against `Select().Distinct().All()`.
    template <typename Self>
    LIGHTWEIGHT_FORCE_INLINE auto&& Distinct(this Self&& self) noexcept
    {
        self.SqlSelectQueryBuilder::Distinct();
        return std::forward<Self>(self);
    }

    /// State-preserving override; see @ref Distinct.
    template <typename Self>
    LIGHTWEIGHT_FORCE_INLINE auto&& Varying(this Self&& self) noexcept
    {
        self.SqlSelectQueryBuilder::Varying();
        return std::forward<Self>(self);
    }
};

} // namespace Lightweight
