// SPDX-License-Identifier: Apache-2.0

#include "Core.hpp"

std::string Lightweight::detail::ComposedQuery::ToSql() const
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
