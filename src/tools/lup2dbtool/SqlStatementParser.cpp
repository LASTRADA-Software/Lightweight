// SPDX-License-Identifier: Apache-2.0

#include "SqlStatementParser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace Lup2DbTool
{

namespace
{

    std::string ToUpper(std::string_view str)
    {
        std::string result(str);
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    std::string Trim(std::string_view str)
    {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            return {};
        auto end = str.find_last_not_of(" \t\r\n");
        return std::string(str.substr(start, end - start + 1));
    }

    /// @brief Normalizes multiple whitespace characters to single spaces.
    std::string NormalizeWhitespace(std::string_view str)
    {
        std::string result;
        result.reserve(str.size());
        bool lastWasSpace = false;
        for (char c: str)
        {
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                if (!lastWasSpace)
                {
                    result += ' ';
                    lastWasSpace = true;
                }
            }
            else
            {
                result += c;
                lastWasSpace = false;
            }
        }
        return Trim(result);
    }

    std::string RemoveQuotes(std::string_view str)
    {
        auto trimmed = Trim(str);
        if (trimmed.size() >= 2)
        {
            if ((trimmed.front() == '"' && trimmed.back() == '"') || (trimmed.front() == '\'' && trimmed.back() == '\'')
                || (trimmed.front() == '[' && trimmed.back() == ']'))
            {
                return trimmed.substr(1, trimmed.size() - 2);
            }
        }
        return trimmed;
    }

    bool StartsWithIgnoreCase(std::string_view str, std::string_view prefix)
    {
        if (str.size() < prefix.size())
            return false;
        return std::ranges::equal(str.substr(0, prefix.size()), prefix, [](char a, char b) {
            return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
        });
    }

    // Split a string by a delimiter, respecting parentheses
    std::vector<std::string> SplitByComma(std::string_view str)
    {
        std::vector<std::string> result;
        std::string current;
        int parenDepth = 0;

        for (char c: str)
        {
            if (c == '(')
                parenDepth++;
            else if (c == ')')
                parenDepth--;

            if (c == ',' && parenDepth == 0)
            {
                result.push_back(Trim(current));
                current.clear();
            }
            else
            {
                current += c;
            }
        }

        if (!current.empty())
            result.push_back(Trim(current));

        return result;
    }

    std::optional<ColumnDef> ParseColumnDefinition(std::string_view colDef)
    {
        auto trimmed = Trim(colDef);
        if (trimmed.empty())
            return std::nullopt;

        // Skip constraints that are defined separately
        auto upper = ToUpper(trimmed);
        if (StartsWithIgnoreCase(upper, "PRIMARY KEY") || StartsWithIgnoreCase(upper, "FOREIGN KEY")
            || StartsWithIgnoreCase(upper, "CONSTRAINT") || StartsWithIgnoreCase(upper, "UNIQUE")
            || StartsWithIgnoreCase(upper, "CHECK"))
        {
            return std::nullopt;
        }

        ColumnDef column;

        // Find the column name (first word)
        size_t nameEnd = trimmed.find_first_of(" \t");
        if (nameEnd == std::string_view::npos)
            return std::nullopt;

        column.name = RemoveQuotes(trimmed.substr(0, nameEnd));

        // Find the type (rest of the definition before any constraints)
        std::string rest = Trim(trimmed.substr(nameEnd));
        auto upperRest = ToUpper(rest);

        // Extract type (everything before NOT NULL, NULL, PRIMARY KEY, REFERENCES, DEFAULT, etc.)
        std::regex typePattern(R"(^(\w+(?:\s*\([^)]*\))?))");
        std::smatch match;
        if (std::regex_search(rest, match, typePattern))
        {
            column.type = Trim(match[1].str());
        }
        else
        {
            return std::nullopt;
        }

        // Check for NOT NULL
        column.isNullable = upperRest.find("NOT NULL") == std::string::npos;

        // Check for PRIMARY KEY
        column.isPrimaryKey = upperRest.find("PRIMARY KEY") != std::string::npos;

        return column;
    }

    std::optional<ForeignKeyDef> ParseForeignKeyConstraint(std::string_view constraint)
    {
        // Pattern: FOREIGN KEY (column) REFERENCES table(column)
        std::regex fkPattern(
            R"(FOREIGN\s+KEY\s*\(\s*["\[\]]?(\w+)["\]\]]?\s*\)\s*REFERENCES\s+["\[\]]?(\w+)["\]\]]?\s*\(\s*["\[\]]?(\w+)["\]\]]?\s*\))",
            std::regex::icase);

        std::string str(constraint);
        std::smatch match;
        if (std::regex_search(str, match, fkPattern))
        {
            ForeignKeyDef fk;
            fk.columnName = match[1].str();
            fk.referencedTable = match[2].str();
            fk.referencedColumn = match[3].str();
            return fk;
        }

        return std::nullopt;
    }

    void MarkPrimaryKeyColumns(std::string_view colDef, std::vector<ColumnDef>& columns)
    {
        std::regex pkPattern(R"(PRIMARY\s+KEY\s*\(([^)]+)\))", std::regex::icase);
        std::smatch pkMatch;
        std::string colDefStr(colDef);
        if (std::regex_search(colDefStr, pkMatch, pkPattern))
        {
            auto pkCols = SplitByComma(pkMatch[1].str());
            for (auto const& pkCol: pkCols)
            {
                auto colName = RemoveQuotes(pkCol);
                for (auto& col: columns)
                {
                    if (col.name == colName)
                        col.isPrimaryKey = true;
                }
            }
        }
    }

    void ProcessColumnDefinition(std::string_view colDef, CreateTableStmt& stmt)
    {
        auto upper = ToUpper(colDef);

        // Check for FOREIGN KEY constraint
        if (upper.find("FOREIGN KEY") != std::string::npos)
        {
            if (auto fk = ParseForeignKeyConstraint(colDef))
                stmt.foreignKeys.push_back(*fk);
            return;
        }

        // Check for PRIMARY KEY constraint (composite)
        if (StartsWithIgnoreCase(colDef, "PRIMARY KEY"))
        {
            MarkPrimaryKeyColumns(colDef, stmt.columns);
            return;
        }

        // Regular column definition
        if (auto col = ParseColumnDefinition(colDef))
            stmt.columns.push_back(*col);
    }

    std::optional<CreateTableStmt> ParseCreateTable(std::string_view sql)
    {
        // Pattern: CREATE TABLE tablename (columns)
        std::regex createPattern(R"(CREATE\s+TABLE\s+["\[\]]?(\w+)["\]\]]?\s*\((.*)\))", std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, createPattern))
            return std::nullopt;

        CreateTableStmt stmt;
        stmt.tableName = match[1].str();

        std::string columnsStr = match[2].str();
        auto columnDefs = SplitByComma(columnsStr);

        for (auto const& colDef: columnDefs)
            ProcessColumnDefinition(colDef, stmt);

        return stmt;
    }

    std::optional<AlterTableAddColumnStmt> ParseAlterTableAddColumn(std::string_view sql)
    {
        // Pattern: ALTER TABLE tablename ADD [COLUMN] columnname type [constraints]
        std::regex alterPattern(
            R"(ALTER\s+TABLE\s+["\[\]]?(\w+)["\]\]]?\s+ADD\s+(?:COLUMN\s+)?["\[\]]?(\w+)["\]\]]?\s+(\w+(?:\s*\([^)]*\))?)\s*(.*)?)",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, alterPattern))
            return std::nullopt;

        AlterTableAddColumnStmt stmt;
        stmt.tableName = match[1].str();
        stmt.column.name = match[2].str();
        stmt.column.type = match[3].str();

        std::string constraints = match[4].str();
        auto upperConstraints = ToUpper(constraints);
        stmt.column.isNullable = upperConstraints.find("NOT NULL") == std::string::npos;
        stmt.column.isPrimaryKey = upperConstraints.find("PRIMARY KEY") != std::string::npos;

        return stmt;
    }

    std::optional<AlterTableAddCompositeForeignKeyStmt> ParseAlterTableAddCompositeForeignKey(std::string_view sql)
    {
        // Pattern: ALTER TABLE tablename ADD FOREIGN KEY (col1, col2) REFERENCES table(col1, col2)
        std::regex alterPattern(
            R"(ALTER\s+TABLE\s+["\[\]]?(\w+)["\]\]]?\s+ADD\s+FOREIGN\s+KEY\s*\(([^)]+)\)\s*REFERENCES\s+["\[\]]?(\w+)["\]\]]?\s*\(([^)]+)\))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, alterPattern))
            return std::nullopt;

        AlterTableAddCompositeForeignKeyStmt stmt;
        stmt.tableName = match[1].str();
        stmt.referencedTable = match[3].str();

        // Parse column lists
        auto colsStr = match[2].str();
        auto refColsStr = match[4].str();

        auto cols = SplitByComma(colsStr);
        auto refCols = SplitByComma(refColsStr);

        for (auto const& col: cols)
            stmt.columns.push_back(RemoveQuotes(col));
        for (auto const& col: refCols)
            stmt.referencedColumns.push_back(RemoveQuotes(col));

        return stmt;
    }

    std::optional<AlterTableDropForeignKeyStmt> ParseAlterTableDropForeignKey(std::string_view sql)
    {
        // Pattern: ALTER TABLE tablename DROP FOREIGN KEY (column) REFERENCES table(column)
        std::regex alterPattern(
            R"(ALTER\s+TABLE\s+["\[\]]?(\w+)["\]\]]?\s+DROP\s+FOREIGN\s+KEY\s*\(\s*["\[\]]?(\w+)["\]\]]?\s*\)\s*REFERENCES\s+["\[\]]?(\w+)["\]\]]?\s*\(\s*["\[\]]?(\w+)["\]\]]?\s*\))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, alterPattern))
            return std::nullopt;

        AlterTableDropForeignKeyStmt stmt;
        stmt.tableName = match[1].str();
        stmt.columnName = match[2].str();
        stmt.referencedTable = match[3].str();
        stmt.referencedColumn = match[4].str();

        return stmt;
    }

    std::optional<InsertStmt> ParseInsert(std::string_view sql)
    {
        // Pattern: INSERT INTO tablename VALUES (values)
        // or: INSERT INTO tablename (columns) VALUES (values)
        std::regex insertPattern(R"(INSERT\s+INTO\s+["\[\]]?(\w+)["\]\]]?\s*(?:\(([^)]*)\))?\s*VALUES\s*\(([^)]*)\))",
                                 std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, insertPattern))
            return std::nullopt;

        InsertStmt stmt;
        stmt.tableName = match[1].str();

        std::string columnsStr = match[2].str();
        std::string valuesStr = match[3].str();

        auto values = SplitByComma(valuesStr);

        if (!columnsStr.empty())
        {
            // Explicit column names provided
            auto columns = SplitByComma(columnsStr);
            for (size_t i = 0; i < columns.size() && i < values.size(); ++i)
            {
                stmt.columnValues.emplace_back(RemoveQuotes(columns[i]), Trim(values[i]));
            }
        }
        else
        {
            // No column names - use positional indices
            for (size_t i = 0; i < values.size(); ++i)
            {
                stmt.columnValues.emplace_back(std::to_string(i), Trim(values[i]));
            }
        }

        return stmt;
    }

    std::optional<UpdateStmt> ParseUpdate(std::string_view sql)
    {
        // Pattern: UPDATE tablename SET col=val, ... WHERE col op val
        std::regex updatePattern(
            R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+(.+?)\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s*(=|<>|!=|<|>|<=|>=)\s*(.+))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, updatePattern))
        {
            // Try without WHERE clause
            std::regex updateNoWherePattern(R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+(.+))", std::regex::icase);
            if (!std::regex_search(sqlStr, match, updateNoWherePattern))
                return std::nullopt;

            UpdateStmt stmt;
            stmt.tableName = match[1].str();

            // Parse SET clause
            std::string setStr = match[2].str();
            auto setPairs = SplitByComma(setStr);
            for (auto const& pair: setPairs)
            {
                auto eqPos = pair.find('=');
                if (eqPos != std::string::npos)
                {
                    stmt.setColumns.emplace_back(RemoveQuotes(Trim(pair.substr(0, eqPos))), Trim(pair.substr(eqPos + 1)));
                }
            }

            return stmt;
        }

        UpdateStmt stmt;
        stmt.tableName = match[1].str();

        // Parse SET clause
        std::string setStr = match[2].str();
        auto setPairs = SplitByComma(setStr);
        for (auto const& pair: setPairs)
        {
            auto eqPos = pair.find('=');
            if (eqPos != std::string::npos)
            {
                stmt.setColumns.emplace_back(RemoveQuotes(Trim(pair.substr(0, eqPos))), Trim(pair.substr(eqPos + 1)));
            }
        }

        stmt.whereColumn = match[3].str();
        stmt.whereOp = match[4].str();
        stmt.whereValue = Trim(match[5].str());

        return stmt;
    }

    std::optional<DeleteStmt> ParseDelete(std::string_view sql)
    {
        // Pattern: DELETE FROM tablename WHERE col op val
        std::regex deletePattern(
            R"(DELETE\s+FROM\s+["\[\]]?(\w+)["\]\]]?\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s*(=|<>|!=|<|>|<=|>=)\s*(.+))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, deletePattern))
        {
            // Try without WHERE clause
            std::regex deleteNoWherePattern(R"(DELETE\s+FROM\s+["\[\]]?(\w+)["\]\]]?)", std::regex::icase);
            if (!std::regex_search(sqlStr, match, deleteNoWherePattern))
                return std::nullopt;

            DeleteStmt stmt;
            stmt.tableName = match[1].str();
            return stmt;
        }

        DeleteStmt stmt;
        stmt.tableName = match[1].str();
        stmt.whereColumn = match[2].str();
        stmt.whereOp = match[3].str();
        stmt.whereValue = Trim(match[4].str());

        return stmt;
    }

    std::optional<CreateIndexStmt> ParseCreateIndex(std::string_view sql)
    {
        // Pattern: CREATE [UNIQUE] INDEX indexname ON tablename (columns)
        std::regex indexPattern(
            R"(CREATE\s+(UNIQUE\s+)?INDEX\s+["\[\]]?(\w+)["\]\]]?\s+ON\s+["\[\]]?(\w+)["\]\]]?\s*\(([^)]+)\))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, indexPattern))
            return std::nullopt;

        CreateIndexStmt stmt;
        stmt.unique = !match[1].str().empty();
        stmt.indexName = match[2].str();
        stmt.tableName = match[3].str();

        // Parse column list
        auto columnsStr = match[4].str();
        auto columns = SplitByComma(columnsStr);
        for (auto const& col: columns)
            stmt.columns.push_back(RemoveQuotes(col));

        return stmt;
    }

    std::optional<DropTableStmt> ParseDropTable(std::string_view sql)
    {
        // Pattern: DROP TABLE tablename
        std::regex dropPattern(R"(DROP\s+TABLE\s+["\[\]]?(\w+)["\]\]]?)", std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, dropPattern))
            return std::nullopt;

        DropTableStmt stmt;
        stmt.tableName = match[1].str();
        return stmt;
    }

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ParsedStatement ParseSqlStatement(std::string_view sql)
{
    auto trimmed = NormalizeWhitespace(sql);
    if (trimmed.empty())
        return RawSqlStmt { std::string(sql) };

    auto upper = ToUpper(trimmed);

    if (StartsWithIgnoreCase(trimmed, "CREATE TABLE"))
    {
        if (auto stmt = ParseCreateTable(trimmed))
            return *stmt;
    }
    else if (StartsWithIgnoreCase(trimmed, "CREATE INDEX") || StartsWithIgnoreCase(trimmed, "CREATE UNIQUE INDEX"))
    {
        if (auto stmt = ParseCreateIndex(trimmed))
            return *stmt;
    }
    else if (StartsWithIgnoreCase(trimmed, "ALTER TABLE"))
    {
        if (upper.find("DROP FOREIGN KEY") != std::string::npos)
        {
            if (auto stmt = ParseAlterTableDropForeignKey(trimmed))
                return *stmt;
        }
        else if (upper.find("ADD FOREIGN KEY") != std::string::npos)
        {
            // Try composite foreign key first (has multiple columns)
            if (auto stmt = ParseAlterTableAddCompositeForeignKey(trimmed))
            {
                // If it's actually a single column, convert to simple foreign key
                if (stmt->columns.size() == 1 && stmt->referencedColumns.size() == 1)
                {
                    AlterTableAddForeignKeyStmt simple;
                    simple.tableName = stmt->tableName;
                    simple.foreignKey.columnName = stmt->columns[0];
                    simple.foreignKey.referencedTable = stmt->referencedTable;
                    simple.foreignKey.referencedColumn = stmt->referencedColumns[0];
                    return simple;
                }
                return *stmt;
            }
        }
        else if (upper.find("ADD") != std::string::npos)
        {
            if (auto stmt = ParseAlterTableAddColumn(trimmed))
                return *stmt;
        }
    }
    else if (StartsWithIgnoreCase(trimmed, "INSERT INTO"))
    {
        if (auto stmt = ParseInsert(trimmed))
            return *stmt;
    }
    else if (StartsWithIgnoreCase(trimmed, "UPDATE"))
    {
        if (auto stmt = ParseUpdate(trimmed))
            return *stmt;
    }
    else if (StartsWithIgnoreCase(trimmed, "DELETE FROM"))
    {
        if (auto stmt = ParseDelete(trimmed))
            return *stmt;
    }
    else if (StartsWithIgnoreCase(trimmed, "DROP TABLE"))
    {
        if (auto stmt = ParseDropTable(trimmed))
            return *stmt;
    }

    return RawSqlStmt { std::string(sql) };
}

bool IsCreateTable(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "CREATE TABLE");
}

bool IsAlterTable(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "ALTER TABLE");
}

bool IsInsert(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "INSERT INTO");
}

bool IsUpdate(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "UPDATE");
}

bool IsDelete(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "DELETE FROM");
}

bool IsCreateIndex(std::string_view sql)
{
    auto trimmed = Trim(sql);
    return StartsWithIgnoreCase(trimmed, "CREATE INDEX") || StartsWithIgnoreCase(trimmed, "CREATE UNIQUE INDEX");
}

bool IsDropTable(std::string_view sql)
{
    return StartsWithIgnoreCase(Trim(sql), "DROP TABLE");
}

} // namespace Lup2DbTool
