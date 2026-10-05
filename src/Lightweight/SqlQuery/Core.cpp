// SPDX-License-Identifier: Apache-2.0

#include "Core.hpp"

std::string Lightweight::detail::ComposedQuery::ToSql() const
{
    // The FROM table's hint belongs right after the table and its alias, which is exactly where the
    // joins begin. Rendering it in front of the joins keeps the formatter's Select* signatures unchanged.
    auto const hintedJoins = searchCondition.tableHint.empty()
                                 ? std::string {}
                                 : std::format("{}{}", searchCondition.tableHint, searchCondition.tableJoins);
    std::string_view const tableJoins = searchCondition.tableHint.empty() ? std::string_view { searchCondition.tableJoins }
                                                                          : std::string_view { hintedJoins };

    switch (selectType)
    {
        case SelectType::All:
            return formatter->SelectAll(distinct,
                                        fields,
                                        searchCondition.tableName,
                                        searchCondition.tableAlias,
                                        tableJoins,
                                        searchCondition.condition,
                                        orderBy,
                                        groupBy);
        case SelectType::First:
            return formatter->SelectFirst(distinct,
                                          fields,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          tableJoins,
                                          searchCondition.condition,
                                          orderBy,
                                          groupBy,
                                          limit);
        case SelectType::Range:
            return formatter->SelectRange(distinct,
                                          fields,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          tableJoins,
                                          searchCondition.condition,
                                          orderBy,
                                          groupBy,
                                          offset,
                                          limit);
        case SelectType::Count:
            return formatter->SelectCount(distinct,
                                          searchCondition.tableName,
                                          searchCondition.tableAlias,
                                          tableJoins,
                                          searchCondition.condition,
                                          groupBy);
        case SelectType::Undefined:
            break;
    }
    return "";
}
