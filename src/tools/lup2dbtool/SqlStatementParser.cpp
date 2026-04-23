// SPDX-License-Identifier: Apache-2.0

#include "SqlStatementParser.hpp"
#include "StringUtils.hpp"
#include "WhereClauseParser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <string_view>
#include <vector>

namespace Lup2DbTool
{

namespace
{

    /// @brief Set of SQL keywords that terminate a column-type phrase.
    ///
    /// When parsing `FOO long varchar not null`, scanning stops as soon as we see `NOT` so
    /// the type phrase captured is `long varchar` rather than swallowing the constraints.
    bool IsTypeTerminatorKeyword(std::string_view upperWord)
    {
        static constexpr std::array<std::string_view, 10> terminators = {
            "NOT", "NULL", "DEFAULT", "PRIMARY", "FOREIGN", "REFERENCES", "UNIQUE", "CHECK", "CONSTRAINT", "IDENTITY",
        };
        return std::ranges::find(terminators, upperWord) != terminators.end();
    }

    struct TypePhraseToken
    {
        std::string text;
        size_t start {};
    };

    /// @brief Tokenizes @p input into whitespace-separated words, respecting paren depth.
    ///
    /// Parenthesized substrings (e.g. `decimal(10,2)`) stay bound to the surrounding word
    /// so `decimal(10,2)` is returned as a single token rather than being split on the comma.
    std::vector<TypePhraseToken> TokenizeWords(std::string_view input)
    {
        std::vector<TypePhraseToken> tokens;
        std::string current;
        size_t wordStart = 0;
        int parenDepth = 0;
        for (size_t pos = 0; pos < input.size(); ++pos)
        {
            char c = input[pos];
            if (parenDepth == 0 && std::isspace(static_cast<unsigned char>(c)))
            {
                if (!current.empty())
                {
                    tokens.push_back({ .text = std::move(current), .start = wordStart });
                    current.clear();
                }
                continue;
            }
            if (current.empty())
                wordStart = pos;
            if (c == '(')
                ++parenDepth;
            else if (c == ')')
                --parenDepth;
            current += c;
        }
        if (!current.empty())
            tokens.push_back({ .text = std::move(current), .start = wordStart });
        return tokens;
    }

    /// @brief Extracts the full SQL column-type phrase from the start of @p rest.
    ///
    /// Accumulates words into the type phrase until the first terminator keyword
    /// (see IsTypeTerminatorKeyword) or end-of-input. The returned type phrase has
    /// internal whitespace normalized to single spaces.
    ///
    /// @return {typeString, constraintTail}. `constraintTail` is the remainder of
    ///         @p rest starting at the first terminator word, trimmed.
    std::pair<std::string, std::string> ExtractTypePhrase(std::string_view rest)
    {
        auto tokens = TokenizeWords(rest);
        std::string type;
        size_t tailStart = rest.size();

        for (auto const& token: tokens)
        {
            if (!type.empty() && IsTypeTerminatorKeyword(ToUpper(token.text)))
            {
                tailStart = token.start;
                break;
            }
            if (!type.empty())
                type += ' ';
            type += token.text;
        }

        auto tail = tailStart < rest.size() ? Trim(rest.substr(tailStart)) : std::string {};
        return { std::move(type), std::move(tail) };
    }

    // Splits a string by top-level commas while respecting parenthesis nesting
    // and SQL single-quoted string literals. Inside a '...' string, commas and
    // parentheses are treated as ordinary characters. SQL's standard `''` escape
    // for a single quote within a string is preserved.
    //
    // Motivation: LUP source VALUES lists routinely contain German decimal
    // notation like `'32,5 L'` — a naive comma split breaks these into two
    // "values", producing downstream parse corruption and out-of-bounds
    // positional inserts (see `ParseInsert`).
    std::vector<std::string> SplitByComma(std::string_view str)
    {
        std::vector<std::string> result;
        std::string current;
        int parenDepth = 0;
        bool inString = false;

        for (size_t i = 0; i < str.size(); ++i)
        {
            char const c = str[i];

            if (inString)
            {
                current += c;
                if (c == '\'')
                {
                    // '' is an escaped quote inside a string literal.
                    if (i + 1 < str.size() && str[i + 1] == '\'')
                    {
                        current += str[i + 1];
                        ++i;
                    }
                    else
                    {
                        inString = false;
                    }
                }
                continue;
            }

            if (c == '\'')
            {
                inString = true;
                current += c;
                continue;
            }
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

        // Extract the full type phrase (handles multi-word types like `long varchar`,
        // `double precision`, `decimal(10,2)` with parens preserved).
        auto [typePhrase, constraintTail] = ExtractTypePhrase(rest);
        if (typePhrase.empty())
            return std::nullopt;
        column.type = std::move(typePhrase);

        // Check for NOT NULL
        column.isNullable = !upperRest.contains("NOT NULL");

        // Check for PRIMARY KEY
        column.isPrimaryKey = upperRest.contains("PRIMARY KEY");

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
        if (upper.contains("FOREIGN KEY"))
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
        // Pattern: ALTER TABLE tablename ADD [COLUMN] columnname <rest>
        // The type phrase is extracted separately so that multi-word types like `long varchar`
        // are captured in full.
        std::regex alterPattern(
            R"(ALTER\s+TABLE\s+["\[\]]?(\w+)["\]\]]?\s+ADD\s+(?:COLUMN\s+)?["\[\]]?(\w+)["\]\]]?\s+(.*))",
            std::regex::icase);

        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, alterPattern))
            return std::nullopt;

        AlterTableAddColumnStmt stmt;
        stmt.tableName = match[1].str();
        stmt.column.name = match[2].str();

        auto [typePhrase, constraintTail] = ExtractTypePhrase(Trim(match[3].str()));
        if (typePhrase.empty())
            return std::nullopt;
        stmt.column.type = std::move(typePhrase);

        auto upperConstraints = ToUpper(constraintTail);
        stmt.column.isNullable = !upperConstraints.contains("NOT NULL");
        stmt.column.isPrimaryKey = upperConstraints.contains("PRIMARY KEY");

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

    /// Returns the position just past the matching `)` for the `(` at `openIdx`,
    /// treating single-quoted string literals as opaque (parens inside them are
    /// preserved as data). Returns `npos` on malformed input.
    [[nodiscard]] size_t FindMatchingCloseParen(std::string_view s, size_t openIdx)
    {
        if (openIdx >= s.size() || s[openIdx] != '(')
            return std::string_view::npos;
        int depth = 1;
        bool inString = false;
        for (size_t i = openIdx + 1; i < s.size(); ++i)
        {
            if (inString)
            {
                if (s[i] == '\'')
                {
                    if (i + 1 < s.size() && s[i + 1] == '\'')
                        ++i; // escaped single quote
                    else
                        inString = false;
                }
                continue;
            }
            if (s[i] == '\'')
                inString = true;
            else if (s[i] == '(')
                ++depth;
            else if (s[i] == ')')
            {
                if (--depth == 0)
                    return i;
            }
        }
        return std::string_view::npos;
    }

    std::optional<InsertStmt> ParseInsert(std::string_view sql)
    {
        // Header: `INSERT\s+INTO\s+["\[]?(\w+)["\]]?` — capture the table name.
        // Use regex only for the easy prefix; quote-aware code takes over at the paren
        // groups because the prior regex-based `[^)]*` capture broke on `)` inside
        // string literals like `'… (MM-LP/BV)'`.
        std::regex headerPattern(R"(INSERT\s+INTO\s+["\[\]]?(\w+)["\]\]]?)", std::regex::icase);
        std::string sqlStr(sql);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, headerPattern))
            return std::nullopt;

        auto cursor = static_cast<size_t>(match.position(0) + match.length(0));

        // Skip whitespace.
        auto skipWs = [&](size_t pos) {
            while (pos < sqlStr.size() && std::isspace(static_cast<unsigned char>(sqlStr[pos])))
                ++pos;
            return pos;
        };
        cursor = skipWs(cursor);

        // Optional column list: `(cols)` before VALUES.
        std::string columnsStr;
        if (cursor < sqlStr.size() && sqlStr[cursor] == '(')
        {
            auto const close = FindMatchingCloseParen(sqlStr, cursor);
            if (close == std::string::npos)
                return std::nullopt;
            columnsStr = sqlStr.substr(cursor + 1, close - cursor - 1);
            cursor = skipWs(close + 1);
        }

        // `VALUES` keyword (case-insensitive).
        constexpr std::string_view valuesKw { "VALUES" };
        if (cursor + valuesKw.size() > sqlStr.size())
            return std::nullopt;
        auto const headCmp = sqlStr.substr(cursor, valuesKw.size());
        bool matchesValues = true;
        for (size_t i = 0; i < valuesKw.size(); ++i)
        {
            if (std::toupper(static_cast<unsigned char>(headCmp[i])) != valuesKw[i])
            {
                matchesValues = false;
                break;
            }
        }
        if (!matchesValues)
            return std::nullopt;
        cursor = skipWs(cursor + valuesKw.size());

        // VALUES (values).
        if (cursor >= sqlStr.size() || sqlStr[cursor] != '(')
            return std::nullopt;
        auto const valuesClose = FindMatchingCloseParen(sqlStr, cursor);
        if (valuesClose == std::string::npos)
            return std::nullopt;
        std::string valuesStr = sqlStr.substr(cursor + 1, valuesClose - cursor - 1);

        InsertStmt stmt;
        stmt.tableName = match[1].str();

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

    /// True iff `s[pos..pos+len)` matches `keyword` (case-insensitively) as a whole word,
    /// i.e. flanked by non-identifier characters (or string boundaries) on both sides.
    [[nodiscard]] bool MatchesKeywordAt(std::string_view s, size_t pos, std::string_view keyword)
    {
        if (pos + keyword.size() > s.size())
            return false;
        for (size_t j = 0; j < keyword.size(); ++j)
        {
            if (std::tolower(static_cast<unsigned char>(s[pos + j])) != keyword[j])
                return false;
        }
        auto const isIdChar = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        bool const leftOk = (pos == 0) || !isIdChar(s[pos - 1]);
        bool const rightOk = (pos + keyword.size() >= s.size()) || !isIdChar(s[pos + keyword.size()]);
        return leftOk && rightOk;
    }

    /// Scans past a SQL single-quoted string literal (honouring `''` escapes) and
    /// returns the index just past the closing quote. Caller passes the position of
    /// the opening quote. Returns `npos` if the string is unterminated.
    [[nodiscard]] size_t SkipQuotedLiteral(std::string_view s, size_t openQuote)
    {
        for (size_t i = openQuote + 1; i < s.size(); ++i)
        {
            if (s[i] != '\'')
                continue;
            if (i + 1 < s.size() && s[i + 1] == '\'')
            {
                ++i; // consume the escaped pair
                continue;
            }
            return i + 1;
        }
        return std::string_view::npos;
    }

    /// Finds the top-level `WHERE` keyword at a word boundary, outside single-quoted
    /// strings and outside parenthesised subqueries. Returns `npos` if not found.
    ///
    /// The paren tracking is what lets us correctly parse statements like
    /// `UPDATE t SET col = expr WHERE NOT EXISTS (SELECT … WHERE inner_col = 1)` —
    /// without it, the inner subquery's `WHERE` would be picked up as the outer one,
    /// truncating the SET expression and corrupting the WHERE value.
    [[nodiscard]] size_t FindUnquotedWhereKeyword(std::string_view s)
    {
        int parenDepth = 0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char const c = s[i];
            if (c == '\'')
            {
                auto const past = SkipQuotedLiteral(s, i);
                if (past == std::string_view::npos)
                    return std::string_view::npos;
                i = past - 1;
                continue;
            }
            if (c == '(')
            {
                ++parenDepth;
                continue;
            }
            if (c == ')')
            {
                if (parenDepth > 0)
                    --parenDepth;
                continue;
            }
            if (parenDepth == 0 && MatchesKeywordAt(s, i, "where"))
                return i;
        }
        return std::string_view::npos;
    }

    /// Parse `col1 = expr1, col2 = expr2, …` into ordered (col, value) pairs.
    /// Used by both UPDATE branches; factored out for readability.
    void ParseSetAssignments(std::string_view setStr,
                             std::vector<std::pair<std::string, std::string>>& out)
    {
        auto setPairs = SplitByComma(std::string(setStr));
        for (auto const& pair: setPairs)
        {
            auto eqPos = pair.find('=');
            if (eqPos == std::string::npos)
                continue;
            out.emplace_back(RemoveQuotes(Trim(pair.substr(0, eqPos))),
                             Trim(pair.substr(eqPos + 1)));
        }
    }

    /// Try the `IS [NOT] NULL` shape. Returns the built statement on match.
    std::optional<UpdateStmt> TryParseUpdateIsNull(std::string const& sqlStr)
    {
        std::regex isNullPattern(
            R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+(.+?)\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s+IS(\s+NOT)?\s+NULL)",
            std::regex::icase);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, isNullPattern))
            return std::nullopt;

        UpdateStmt stmt;
        stmt.tableName = match[1].str();
        ParseSetAssignments(match[2].str(), stmt.setColumns);
        stmt.whereColumn = match[3].str();
        stmt.whereOp = match[4].matched ? "IS NOT NULL" : "IS NULL";
        return stmt;
    }

    /// Try the composite WHERE shape: anything with AND/OR/NOT, IN/EXISTS subqueries, etc.
    /// On success, `stmt.whereExpression` holds the canonical rendering.
    std::optional<UpdateStmt> TryParseUpdateComposite(std::string const& sqlStr)
    {
        auto const whereStart = FindUnquotedWhereKeyword(sqlStr);
        if (whereStart == std::string::npos)
            return std::nullopt;

        std::regex setHeadPattern(R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+)",
                                  std::regex::icase);
        std::smatch setHeadMatch;
        if (!std::regex_search(sqlStr, setHeadMatch, setHeadPattern))
            return std::nullopt;
        auto const setStart =
            static_cast<size_t>(setHeadMatch.position(0) + setHeadMatch.length(0));
        if (whereStart <= setStart)
            return std::nullopt;

        auto const setStr = Trim(sqlStr.substr(setStart, whereStart - setStart));
        auto const whereBody = Trim(sqlStr.substr(whereStart + std::string("where").size()));
        auto const rendered = ParseWhereClause(whereBody);
        if (!rendered)
            return std::nullopt;

        UpdateStmt stmt;
        stmt.tableName = setHeadMatch[1].str();
        ParseSetAssignments(setStr, stmt.setColumns);
        stmt.whereExpression = *rendered;
        return stmt;
    }

    /// Try the bare form: `UPDATE t SET col = val, …` — no WHERE clause at all.
    /// Only valid when the SQL literally has no `WHERE` keyword at the top level.
    std::optional<UpdateStmt> TryParseUpdateNoWhere(std::string const& sqlStr)
    {
        if (FindUnquotedWhereKeyword(sqlStr) != std::string::npos)
            return std::nullopt;

        std::regex updateNoWherePattern(R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+(.+))",
                                        std::regex::icase);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, updateNoWherePattern))
            return std::nullopt;

        UpdateStmt stmt;
        stmt.tableName = match[1].str();
        ParseSetAssignments(match[2].str(), stmt.setColumns);
        return stmt;
    }

    /// Detects whether a top-level `WHERE` body lies inside parens or contains
    /// AND/OR/NOT/EXISTS/IN — i.e. is too rich for the simple `(col op val)` regex.
    /// Used to decide whether to route an UPDATE through the composite parser even
    /// when the simple regex would otherwise (mis-)match.
    bool WhereBodyNeedsCompositeParser(std::string_view body)
    {
        for (size_t i = 0; i < body.size(); ++i)
        {
            char const c = body[i];
            if (c == '\'')
            {
                auto const past = SkipQuotedLiteral(body, i);
                if (past == std::string_view::npos)
                    return false;
                i = past - 1;
                continue;
            }
            if (c == '(')
                return true;
            if (MatchesKeywordAt(body, i, "and") || MatchesKeywordAt(body, i, "or")
                || MatchesKeywordAt(body, i, "not") || MatchesKeywordAt(body, i, "exists")
                || MatchesKeywordAt(body, i, "in"))
                return true;
        }
        return false;
    }

    std::optional<UpdateStmt> ParseUpdate(std::string_view sql)
    {
        std::string sqlStr(sql);

        // Always try the IS [NOT] NULL shape first — it's a simple structured form
        // that the happy-path regex below can't express.
        if (auto stmt = TryParseUpdateIsNull(sqlStr))
            return stmt;

        // If the WHERE body looks composite (contains AND/OR/NOT/EXISTS/IN or any
        // parenthesised sub-expression), defer to the composite parser. This is
        // what catches `WHERE NOT EXISTS (SELECT … WHERE …)` patterns whose inner
        // `WHERE` would otherwise confuse the greedy happy-path regex into
        // splitting the SET / WHERE in the wrong place.
        auto const whereStart = FindUnquotedWhereKeyword(sqlStr);
        if (whereStart != std::string::npos)
        {
            auto const body = std::string_view { sqlStr }.substr(whereStart + std::string("where").size());
            if (WhereBodyNeedsCompositeParser(body))
            {
                if (auto stmt = TryParseUpdateComposite(sqlStr))
                    return stmt;
            }
        }

        // Happy path: UPDATE tablename SET col=val, ... WHERE col op val
        std::regex updatePattern(
            R"(UPDATE\s+["\[\]]?(\w+)["\]\]]?\s+SET\s+(.+?)\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s*(=|<>|!=|<|>|<=|>=)\s*(.+))",
            std::regex::icase);

        std::smatch match;
        if (!std::regex_search(sqlStr, match, updatePattern))
        {
            if (auto stmt = TryParseUpdateComposite(sqlStr))
                return stmt;
            return TryParseUpdateNoWhere(sqlStr);
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

    /// Extract the table name from `DELETE FROM <table>`. Returns empty string on no match.
    std::string ParseDeleteTableName(std::string const& sqlStr)
    {
        std::regex headPattern(R"(DELETE\s+FROM\s+["\[\]]?(\w+)["\]\]]?)", std::regex::icase);
        std::smatch match;
        if (std::regex_search(sqlStr, match, headPattern))
            return match[1].str();
        return {};
    }

    /// Simple `col op val` WHERE for DELETE. Rejects when the captured value would
    /// include further WHERE structure (to avoid the greedy `.+` trap that turned
    /// `WHERE idd = 4104 AND idc = 2529` into a single string value).
    std::optional<DeleteStmt> TryParseDeleteSimple(std::string const& sqlStr)
    {
        std::regex deletePattern(
            R"(DELETE\s+FROM\s+["\[\]]?(\w+)["\]\]]?\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s*(=|<>|!=|<|>|<=|>=)\s*(.+))",
            std::regex::icase);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, deletePattern))
            return std::nullopt;

        auto const rawValue = Trim(match[4].str());
        // Reject values that contain composite-WHERE tokens — handled by the
        // structured path below.
        if (FindUnquotedWhereKeyword(rawValue) != std::string::npos) // shouldn't happen post-trim
            return std::nullopt;
        // Detect the common `4104 AND idc = 2529` shape: a bare `AND`/`OR` keyword
        // inside the captured value.
        auto const upperValue = ToUpper(rawValue);
        if (upperValue.find(" AND ") != std::string::npos || upperValue.find(" OR ") != std::string::npos)
            return std::nullopt;

        DeleteStmt stmt;
        stmt.tableName = match[1].str();
        stmt.whereColumn = match[2].str();
        stmt.whereOp = match[3].str();
        stmt.whereValue = rawValue;
        return stmt;
    }

    /// DELETE … WHERE col IS [NOT] NULL
    std::optional<DeleteStmt> TryParseDeleteIsNull(std::string const& sqlStr)
    {
        std::regex pattern(
            R"(DELETE\s+FROM\s+["\[\]]?(\w+)["\]\]]?\s+WHERE\s+["\[\]]?(\w+)["\]\]]?\s+IS(\s+NOT)?\s+NULL)",
            std::regex::icase);
        std::smatch match;
        if (!std::regex_search(sqlStr, match, pattern))
            return std::nullopt;

        DeleteStmt stmt;
        stmt.tableName = match[1].str();
        stmt.whereColumn = match[2].str();
        stmt.whereOp = match[3].matched ? "IS NOT NULL" : "IS NULL";
        return stmt;
    }

    /// DELETE with a composite WHERE clause — hand off to the structured parser.
    std::optional<DeleteStmt> TryParseDeleteComposite(std::string const& sqlStr)
    {
        auto const whereStart = FindUnquotedWhereKeyword(sqlStr);
        if (whereStart == std::string::npos)
            return std::nullopt;
        auto const table = ParseDeleteTableName(sqlStr);
        if (table.empty())
            return std::nullopt;
        auto const whereBody = Trim(sqlStr.substr(whereStart + std::string("where").size()));
        auto const rendered = ParseWhereClause(whereBody);
        if (!rendered)
            return std::nullopt;

        DeleteStmt stmt;
        stmt.tableName = table;
        stmt.whereExpression = *rendered;
        return stmt;
    }

    /// DELETE FROM <table> with no WHERE clause at all.
    std::optional<DeleteStmt> TryParseDeleteNoWhere(std::string const& sqlStr)
    {
        if (FindUnquotedWhereKeyword(sqlStr) != std::string::npos)
            return std::nullopt;
        auto const table = ParseDeleteTableName(sqlStr);
        if (table.empty())
            return std::nullopt;
        DeleteStmt stmt;
        stmt.tableName = table;
        return stmt;
    }

    std::optional<DeleteStmt> ParseDelete(std::string_view sql)
    {
        std::string const sqlStr(sql);

        if (auto stmt = TryParseDeleteSimple(sqlStr))
            return stmt;
        if (auto stmt = TryParseDeleteIsNull(sqlStr))
            return stmt;
        if (auto stmt = TryParseDeleteComposite(sqlStr))
            return stmt;
        return TryParseDeleteNoWhere(sqlStr);
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

        // Parse column list. Strip the trailing `ASC`/`DESC` ordering hint that the
        // LUP source frequently attaches to index columns (e.g. `(LAST_UPDATE DESC)`)
        // — we don't propagate ordering to the migration plan today, and leaving the
        // suffix would turn the column name into `LAST_UPDATE DESC`, which no DB can
        // resolve.
        auto columnsStr = match[4].str();
        auto columns = SplitByComma(columnsStr);
        for (auto const& col: columns)
        {
            auto stripped = RemoveQuotes(col);
            auto const space = stripped.find_last_of(" \t");
            if (space != std::string::npos)
            {
                auto const tail = stripped.substr(space + 1);
                auto const upper = ToUpper(tail);
                if (upper == "ASC" || upper == "DESC")
                    stripped = Trim(stripped.substr(0, space));
            }
            stmt.columns.push_back(stripped);
        }

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
        if (upper.contains("DROP FOREIGN KEY"))
        {
            if (auto stmt = ParseAlterTableDropForeignKey(trimmed))
                return *stmt;
        }
        else if (upper.contains("ADD FOREIGN KEY"))
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
        else if (upper.contains("ADD"))
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
