// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlQueryFormatter.hpp"
#include "../Utils.hpp"
#include "Core.hpp"

#include <reflection-cpp/reflection.hpp>

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

    inline SqlFieldExpression Count(std::string const& field = "*") noexcept
    {
        return SqlFieldExpression { .expression = std::format("COUNT(\"{}\")", field) };
    }

    inline SqlFieldExpression Count(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(COUNT("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Sum(std::string const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("SUM(\"{}\")", field) };
    }

    inline SqlFieldExpression Sum(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(SUM("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Avg(std::string const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("AVG(\"{}\")", field) };
    }

    inline SqlFieldExpression Avg(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(AVG("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Min(std::string const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format("MIN(\"{}\")", field) };
    }

    inline SqlFieldExpression Min(SqlQualifiedTableColumnName const& field) noexcept
    {
        return SqlFieldExpression { .expression = std::format(R"(MIN("{}"."{}"))", field.tableName, field.columnName) };
    }

    inline SqlFieldExpression Max(std::string const& field) noexcept
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
/// @see SqlQueryBuilder
class [[nodiscard]] SqlSelectQueryBuilder final: public SqlBasicSelectQueryBuilder<SqlSelectQueryBuilder>
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

    /// Adds a single column to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(SqlQualifiedTableColumnName const& fieldName);

    /// Adds an aggregate function call to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Field(SqlFieldExpression const& fieldExpression);

    /// Aliases the last added field (a column or an aggregate call) in the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& As(std::string_view alias);

    /// Adds a sequence of columns to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames);

    /// Adds a sequence of columns from the given table to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::vector<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

    /// Adds a sequence of columns from the given table to the SELECT clause.
    LIGHTWEIGHT_API SqlSelectQueryBuilder& Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                  std::string_view tableName);

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

    /// Finalizes building the query as SELECT field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery All();

    /// Finalizes building the query as SELECT TOP n field names FROM ... query.
    LIGHTWEIGHT_API ComposedQuery First(size_t count = 1);

    /// Finalizes building the query as SELECT field names FROM ... query with a range.
    LIGHTWEIGHT_API ComposedQuery Range(std::size_t offset, std::size_t limit);

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
    bool _aliasAllowed = false;
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

} // namespace Lightweight
