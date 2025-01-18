// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlQueryFormatter.hpp"
#include "SQLiteFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <format>

class OracleSqlQueryFormatter final: public SQLiteQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view tableName) const override
    {
        return std::format("SELECT \"{}_SEQ\".CURRVAL FROM DUAL;", tableName);
    }

    [[nodiscard]] std::string_view BooleanLiteral(bool literalValue) const noexcept override
    {
        return literalValue ? "1" : "0";
    }

    [[nodiscard]] std::string SelectFirst(bool distinct,
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
                                          size_t count) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT";
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " TOP " << count;
        sqlQueryString << ' ' << fields;
        sqlQueryString << " FROM \"" << fromTable << '"';
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << '"';
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << orderBy;
        ;
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string SelectRange(bool distinct,
                                          std::string const& fields,
                                          std::string const& fromTable,
                                          std::string const& fromTableAlias,
                                          std::string const& tableJoins,
                                          std::string const& whereCondition,
                                          std::string const& orderBy,
                                          std::string const& groupBy,
                                          std::size_t offset,
                                          std::size_t limit) const override
    {
        assert(!orderBy.empty());
        std::stringstream sqlQueryString;
        sqlQueryString << "SELECT " << fields;
        if (distinct)
            sqlQueryString << " DISTINCT";
        sqlQueryString << " FROM \"" << fromTable << "\"";
        if (!fromTableAlias.empty())
            sqlQueryString << " AS \"" << fromTableAlias << "\"";
        sqlQueryString << tableJoins;
        sqlQueryString << whereCondition;
        sqlQueryString << groupBy;
        sqlQueryString << orderBy;
        sqlQueryString << " OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";
        return sqlQueryString.str();
    }

    [[nodiscard]] std::string ColumnType(SqlColumnTypeDefinition const& type) const override
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(
            detail::overloaded {
                [](Bigint const&) -> std::string { return "NUMBER(21, 0)"; },
                [](Binary const&) -> std::string { return "BLOB"; },
                [](Bool const&) -> std::string { return "BIT"; },
                [](Char const& type) -> std::string { return std::format("CHAR({})", type.size); },
                [](Date const&) -> std::string { return "DATE"; },
                [](DateTime const&) -> std::string { return "TIMESTAMP"; },
                [](Decimal const& type) -> std::string {
                    return std::format("DECIMAL({}, {})", type.precision, type.scale);
                },
                [](Guid const&) -> std::string { return "RAW(16)"; },
                [](Integer const&) -> std::string { return "INTEGER"; },
                [](NChar const& type) -> std::string { return std::format("NCHAR({})", type.size); },
                [](NVarchar const& type) -> std::string { return std::format("NVARCHAR2({})", type.size); },
                [](Real const&) -> std::string { return "REAL"; },
                [](Smallint const&) -> std::string { return "SMALLINT"; },
                [](Text const& type) -> std::string {
                    if (type.size <= 4000)
                        return std::format("VARCHAR2({})", type.size);
                    else
                        return "CLOB";
                },
                [](Time const&) -> std::string { return "TIMESTAMP"; },
                [](Timestamp const&) -> std::string { return "TIMESTAMP"; },
                [](Tinyint const&) -> std::string { return "TINYINT"; },
                [](VarBinary const& type) -> std::string { return std::format("VARBINARY({})", type.size); },
                [](Varchar const& type) -> std::string { return std::format("VARCHAR({})", type.size); },
            },
            type);
    }

    [[nodiscard]] std::string BuildColumnDefinition(SqlColumnDeclaration const& column) const override
    {
        std::stringstream sqlQueryString;
        sqlQueryString << '"' << column.name << "\" " << ColumnType(column.type);

        if (column.required && column.primaryKey != SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " NOT NULL";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            sqlQueryString << " GENERATED ALWAYS AS IDENTITY";
        else if (column.unique && !column.index)
            sqlQueryString << " UNIQUE";

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
        {
            sqlQueryString << ",\n    PRIMARY KEY (\"" << column.name << "\")";
        }
        return sqlQueryString.str();
    }
};
