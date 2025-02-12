// SPDX-License-Identifier: Apache-2.0

#include "Select.hpp"

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Distinct() noexcept
{
    m_query.distinct = true;
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(std::string_view const& fieldName)
{
    if (!m_query.fields.empty())
        m_query.fields += ", ";

    if (fieldName == "*")
        m_query.fields += fieldName;
    else
    {
        m_query.fields += '"';
        m_query.fields += fieldName;
        m_query.fields += '"';
        m_aliasAllowed = true;
    }

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(SqlQualifiedTableColumnName const& fieldName)
{
    if (!m_query.fields.empty())
        m_query.fields += ", ";

    m_query.fields += '"';
    m_query.fields += fieldName.tableName;
    if (fieldName.columnName == "*")
        m_query.fields += "\".*";
    else
    {
        m_query.fields += "\".\"";
        m_query.fields += fieldName.columnName;
        m_query.fields += '"';
        m_aliasAllowed = true;
    }

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Field(SqlFieldExpression const& fieldExpression)
{
    if (!m_query.fields.empty())
        m_query.fields += ", ";

    m_query.fields += fieldExpression.expression;
    m_aliasAllowed = true;

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::As(std::string_view alias)
{
    assert(m_aliasAllowed);

    m_aliasAllowed = false;
    m_query.fields += " AS \"";
    m_query.fields += alias;
    m_query.fields += "\"";

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::FieldAs(std::string_view const& fieldName, std::string_view const& alias)
{
    if (!m_query.fields.empty())
        m_query.fields += ", ";

    m_query.fields += '"';
    m_query.fields += fieldName;
    m_query.fields += "\" AS \"";
    m_query.fields += alias;
    m_query.fields += '"';

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::FieldAs(SqlQualifiedTableColumnName const& fieldName,
                                                      std::string_view const& alias)
{
    if (!m_query.fields.empty())
        m_query.fields += ", ";

    m_query.fields += '"';
    m_query.fields += fieldName.tableName;
    m_query.fields += "\".\"";
    m_query.fields += fieldName.columnName;
    m_query.fields += "\" AS \"";
    m_query.fields += alias;
    m_query.fields += '"';

    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!m_query.fields.empty())
            m_query.fields += ", ";

        m_query.fields += '"';
        m_query.fields += fieldName;
        m_query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::vector<std::string_view> const& fieldNames,
                                                     std::string_view tableName)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!m_query.fields.empty())
            m_query.fields += ", ";

        m_query.fields += '"';
        m_query.fields += tableName;
        m_query.fields += "\".\"";
        m_query.fields += fieldName;
        m_query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::Fields(std::initializer_list<std::string_view> const& fieldNames,
                                                     std::string_view tableName)
{
    for (auto const& fieldName: fieldNames)
    {
        if (!m_query.fields.empty())
            m_query.fields += ", ";

        m_query.fields += '"';
        m_query.fields += tableName;
        m_query.fields += "\".\"";
        m_query.fields += fieldName;
        m_query.fields += '"';
    }
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::OrderBy(std::string_view columnName, SqlResultOrdering ordering)
{
    if (m_query.orderBy.empty())
        m_query.orderBy += "\n ORDER BY ";
    else
        m_query.orderBy += ", ";

    m_query.orderBy += '"';
    m_query.orderBy += columnName;
    m_query.orderBy += '"';

    if (ordering == SqlResultOrdering::DESCENDING)
        m_query.orderBy += " DESC";
    else if (ordering == SqlResultOrdering::ASCENDING)
        m_query.orderBy += " ASC";
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::OrderBy(SqlQualifiedTableColumnName const& columnName,
                                                      SqlResultOrdering ordering)
{
    if (m_query.orderBy.empty())
        m_query.orderBy += "\n ORDER BY ";
    else
        m_query.orderBy += ", ";

    m_query.orderBy += '"';
    m_query.orderBy += columnName.tableName;
    m_query.orderBy += "\".\"";
    m_query.orderBy += columnName.columnName;
    m_query.orderBy += '"';

    if (ordering == SqlResultOrdering::DESCENDING)
        m_query.orderBy += " DESC";
    else if (ordering == SqlResultOrdering::ASCENDING)
        m_query.orderBy += " ASC";
    return *this;
}

SqlSelectQueryBuilder& SqlSelectQueryBuilder::GroupBy(std::string_view columnName)
{
    if (m_query.groupBy.empty())
        m_query.groupBy += "\n GROUP BY ";
    else
        m_query.groupBy += ", ";

    m_query.groupBy += '"';
    m_query.groupBy += columnName;
    m_query.groupBy += '"';

    return *this;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Count()
{
    m_query.selectType = SelectType::Count;

    if (m_mode == SqlQueryBuilderMode::Fluent)
        return std::move(m_query);
    else
        return m_query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::All()
{
    m_query.selectType = SelectType::All;

    if (m_mode == SqlQueryBuilderMode::Fluent)
        return std::move(m_query);
    else
        return m_query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::First(size_t count)
{
    m_query.selectType = SelectType::First;
    m_query.limit = count;

    if (m_mode == SqlQueryBuilderMode::Fluent)
        return std::move(m_query);
    else
        return m_query;
}

SqlSelectQueryBuilder::ComposedQuery SqlSelectQueryBuilder::Range(std::size_t offset, std::size_t limit)
{
    m_query.selectType = SelectType::Range;
    m_query.offset = offset;
    m_query.limit = limit;

    if (m_mode == SqlQueryBuilderMode::Fluent)
        return std::move(m_query);
    else
        return m_query;
}

std::string SqlSelectQueryBuilder::ComposedQuery::ToSql() const
{
    switch (selectType)
    {
        case SelectType::All:
            return formatter->SelectAll(distinct,
                                        fields,
                                        searchCondition.tableName,
                                        searchCondition.tableAlias,
                                        searchCondition.tableJoins,
                                        searchCondition.condition,
                                        orderBy,
                                        groupBy);
        case SelectType::First:
            return formatter->SelectFirst(distinct,
                                          fields,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          searchCondition.tableJoins,
                                          searchCondition.condition,
                                          orderBy,
                                          limit);
        case SelectType::Range:
            return formatter->SelectRange(distinct,
                                          fields,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          searchCondition.tableJoins,
                                          searchCondition.condition,
                                          orderBy,
                                          groupBy,
                                          offset,
                                          limit);
        case SelectType::Count:
            return formatter->SelectCount(distinct,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          searchCondition.tableJoins,
                                          searchCondition.condition);
        case SelectType::Undefined:
            break;
    }
    return "";
}
