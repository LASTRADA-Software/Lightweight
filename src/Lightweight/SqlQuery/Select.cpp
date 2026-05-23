// SPDX-License-Identifier: Apache-2.0

#include "Select.hpp"

namespace Lightweight
{

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(std::string_view const& fieldName)
{
    if (!_query.fields.empty())
        _query.fields += ", ";

    if (fieldName == "*")
        _query.fields += fieldName;
    else
    {
        _query.fields += '"';
        _query.fields += fieldName;
        _query.fields += '"';
        _aliasAllowed = true;
    }

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(SqlQualifiedTableColumnName const& fieldName)
{
    if (!_query.fields.empty())
        _query.fields += ", ";

    _query.fields += '"';
    _query.fields += fieldName.tableName;
    if (fieldName.columnName == "*")
        _query.fields += "\".*";
    else
    {
        _query.fields += "\".\"";
        _query.fields += fieldName.columnName;
        _query.fields += '"';
        _aliasAllowed = true;
    }

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(SqlFieldExpression const& fieldExpression)
{
    if (!_query.fields.empty())
        _query.fields += ", ";

    _query.fields += fieldExpression.expression;
    _aliasAllowed = true;

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::As(std::string_view alias)
{
    assert(_aliasAllowed);

    _aliasAllowed = false;
    _query.fields += " AS \"";
    _query.fields += alias;
    _query.fields += "\"";

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::FieldAs(std::string_view const& fieldName, std::string_view const& alias)
{
    if (!_query.fields.empty())
        _query.fields += ", ";

    _query.fields += '"';
    _query.fields += fieldName;
    _query.fields += "\" AS \"";
    _query.fields += alias;
    _query.fields += '"';

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::FieldAs(SqlQualifiedTableColumnName const& fieldName,
                                                      std::string_view const& alias)
{
    if (!_query.fields.empty())
        _query.fields += ", ";

    _query.fields += '"';
    _query.fields += fieldName.tableName;
    _query.fields += "\".\"";
    _query.fields += fieldName.columnName;
    _query.fields += "\" AS \"";
    _query.fields += alias;
    _query.fields += '"';

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!_query.fields.empty())
            _query.fields += ", ";

        _query.fields += '"';
        _query.fields += fieldName;
        _query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames,
                                                     std::string_view tableName)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!_query.fields.empty())
            _query.fields += ", ";

        _query.fields += '"';
        _query.fields += tableName;
        _query.fields += "\".\"";
        _query.fields += fieldName;
        _query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                     std::string_view tableName)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!_query.fields.empty())
            _query.fields += ", ";

        _query.fields += '"';
        _query.fields += tableName;
        _query.fields += "\".\"";
        _query.fields += fieldName;
        _query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::initializer_list<SqlQualifiedTableColumnName const> fieldNames)
{
    for (auto const& fieldName: fieldNames)
        Field(fieldName);
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::span<SqlQualifiedTableColumnName const> fieldNames)
{
    for (auto const& fieldName: fieldNames)
        Field(fieldName);
    return *this;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Count()
{
    _query.selectType = SelectType::Count;

    if (_mode == SqlQueryBuilderMode::Fluent)
        return std::move(_query);
    else
        return _query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::All()
{
    _query.selectType = SelectType::All;

    if (_mode == SqlQueryBuilderMode::Fluent)
        return std::move(_query);
    else
        return _query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::First(size_t count)
{
    _query.selectType = SelectType::First;
    _query.limit = count;

    if (_mode == SqlQueryBuilderMode::Fluent)
        return std::move(_query);
    else
        return _query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Range(std::size_t offset, std::size_t limit)
{
    _query.selectType = SelectType::Range;
    _query.offset = offset;
    _query.limit = limit;

    if (_mode == SqlQueryBuilderMode::Fluent)
        return std::move(_query);
    else
        return _query;
}

// ---- const overloads ---------------------------------------------------------
// Delegate to the non-const overloads via const_cast. Well-defined here because
// the only state these overloads touch (_query in the base, _aliasAllowed
// here) is marked `mutable` — mutable members are exempt from the const
// constraint, so writes through a const-cast handle have defined behavior even
// for a truly-const object. This lets callers chain through a `const`-bound
// builder reference (e.g. `auto const second = first.Field("x");`).

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Field(std::string_view const& fieldName) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Field(fieldName);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Field(SqlQualifiedTableColumnName const& fieldName) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Field(fieldName);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Field(SqlFieldExpression const& fieldExpression) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Field(fieldExpression);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::As(std::string_view alias) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->As(alias);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Fields(fieldNames);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames,
                                                           std::string_view tableName) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Fields(fieldNames, tableName);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                           std::string_view tableName) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Fields(fieldNames, tableName);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Fields(std::span<SqlQualifiedTableColumnName const> fieldNames) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Fields(fieldNames);
}

SqlSelectQueryBuilder const& SqlSelectQueryBuilder::Fields(
    std::initializer_list<SqlQualifiedTableColumnName const> fieldNames) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Fields(fieldNames);
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Count() const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Count();
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::All() const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->All();
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::First(size_t count) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->First(count);
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Range(std::size_t offset, std::size_t limit) const
{
    return const_cast<SqlSelectQueryBuilder*>(this)->Range(offset, limit);
}

} // namespace Lightweight
