// SPDX-License-Identifier: Apache-2.0

#include "StringUtils.hpp"
#include "WhereClauseParser.hpp"

#include <cctype>
#include <format>
#include <string>
#include <string_view>

namespace Lup2DbTool
{

namespace
{

    /// Recursive-descent parser. Each recursive `Parse*` returns the rendered
    /// canonical text for the sub-tree it matched, or `std::nullopt` on failure.
    ///
    /// Errors anywhere bubble up as `std::nullopt` from the outermost call, so the
    /// caller (ParseUpdate) can fall back to reporting the statement as unparseable.
    class WhereParser
    {
      public:
        explicit WhereParser(std::string_view sql) noexcept:
            _sql { sql }
        {
        }

        /// Parse a top-level WHERE body. Must consume all input (modulo trailing
        /// whitespace); otherwise the input has trailing garbage we didn't recognize.
        std::optional<std::string> ParseAll()
        {
            SkipWs();
            auto rendered = ParseOr();
            if (!rendered)
                return std::nullopt;
            SkipWs();
            if (_pos != _sql.size())
                return std::nullopt;
            return rendered;
        }

      private:
        std::string_view _sql;
        size_t _pos = 0;

        // --- character / keyword primitives -----------------------------------

        static bool IsIdStart(char c) noexcept
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        static bool IsIdCont(char c) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        void SkipWs() noexcept
        {
            while (_pos < _sql.size() && std::isspace(static_cast<unsigned char>(_sql[_pos])))
                ++_pos;
        }

        [[nodiscard]] bool Eof() const noexcept
        {
            return _pos >= _sql.size();
        }

        /// Attempts to match `keyword` (case-insensitively) as a whole word at the
        /// current position. Consumes it and returns true on success; leaves the
        /// position untouched otherwise.
        bool TryKeyword(std::string_view keyword)
        {
            SkipWs();
            if (_pos + keyword.size() > _sql.size())
                return false;
            for (size_t i = 0; i < keyword.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(_sql[_pos + i]))
                    != std::tolower(static_cast<unsigned char>(keyword[i])))
                    return false;
            }
            // Word boundary after the keyword.
            auto const after = _pos + keyword.size();
            if (after < _sql.size() && IsIdCont(_sql[after]))
                return false;
            _pos = after;
            return true;
        }

        [[nodiscard]] bool PeekKeyword(std::string_view keyword) const noexcept
        {
            size_t p = _pos;
            while (p < _sql.size() && std::isspace(static_cast<unsigned char>(_sql[p])))
                ++p;
            if (p + keyword.size() > _sql.size())
                return false;
            for (size_t i = 0; i < keyword.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(_sql[p + i]))
                    != std::tolower(static_cast<unsigned char>(keyword[i])))
                    return false;
            }
            auto const after = p + keyword.size();
            if (after < _sql.size() && IsIdCont(_sql[after]))
                return false;
            return true;
        }

        bool TryChar(char c) noexcept
        {
            SkipWs();
            if (_pos < _sql.size() && _sql[_pos] == c)
            {
                ++_pos;
                return true;
            }
            return false;
        }

        // --- structural parsers ------------------------------------------------

        std::optional<std::string> ParseOr()
        {
            auto lhs = ParseAnd();
            if (!lhs)
                return std::nullopt;
            while (TryKeyword("OR"))
            {
                auto rhs = ParseAnd();
                if (!rhs)
                    return std::nullopt;
                lhs = std::format("{} OR {}", *lhs, *rhs);
            }
            return lhs;
        }

        std::optional<std::string> ParseAnd()
        {
            auto lhs = ParseNot();
            if (!lhs)
                return std::nullopt;
            while (TryKeyword("AND"))
            {
                auto rhs = ParseNot();
                if (!rhs)
                    return std::nullopt;
                lhs = std::format("{} AND {}", *lhs, *rhs);
            }
            return lhs;
        }

        std::optional<std::string> ParseNot()
        {
            if (TryKeyword("NOT"))
            {
                auto inner = ParseNot();
                if (!inner)
                    return std::nullopt;
                return std::format("NOT {}", *inner);
            }
            return ParsePrimary();
        }

        std::optional<std::string> ParsePrimary()
        {
            SkipWs();
            if (Eof())
                return std::nullopt;

            // Parenthesized condition.
            if (_sql[_pos] == '(')
            {
                ++_pos;
                auto inner = ParseOr();
                if (!inner)
                    return std::nullopt;
                if (!TryChar(')'))
                    return std::nullopt;
                return std::format("({})", *inner);
            }

            // EXISTS (subquery)
            if (TryKeyword("EXISTS"))
            {
                auto const sub = ParseSubquery();
                if (!sub)
                    return std::nullopt;
                return std::format("EXISTS ({})", *sub);
            }

            // identifier-led predicate: IS NULL / IN (…) / op value
            auto const lhs = ParseIdentifier();
            if (!lhs)
                return std::nullopt;

            if (TryKeyword("IS"))
            {
                bool const negated = TryKeyword("NOT");
                if (!TryKeyword("NULL"))
                    return std::nullopt;
                return negated ? std::format("{} IS NOT NULL", *lhs) : std::format("{} IS NULL", *lhs);
            }

            bool const notIn = TryKeyword("NOT");
            if (TryKeyword("IN"))
            {
                auto const sub = ParseSubquery();
                if (!sub)
                    return std::nullopt;
                return notIn ? std::format("{} NOT IN ({})", *lhs, *sub) : std::format("{} IN ({})", *lhs, *sub);
            }
            if (notIn)
                return std::nullopt; // `col NOT <something-else>` unsupported

            auto const op = ParseComparisonOp();
            if (!op)
                return std::nullopt;
            auto const rhs = ParseValue();
            if (!rhs)
                return std::nullopt;
            return std::format("{} {} {}", *lhs, *op, *rhs);
        }

        // --- leaf parsers ------------------------------------------------------

        /// Parses a SQL identifier (optionally `qualifier.name`). Supports bare,
        /// `"quoted"`, and `[bracketed]` forms; re-emits everything with double
        /// quotes for dialect portability and uppercases the name to stay consistent
        /// with the rest of the lup2dbtool emitter (which canonicalises every SQL
        /// identifier to UPPERCASE so PostgreSQL — which is case-sensitive on quoted
        /// names — finds the columns regardless of the source SQL's casing).
        std::optional<std::string> ParseIdentifier()
        {
            SkipWs();
            auto const first = ParseIdentifierSegment();
            if (!first)
                return std::nullopt;
            if (_pos < _sql.size() && _sql[_pos] == '.')
            {
                ++_pos;
                auto const second = ParseIdentifierSegment();
                if (!second)
                    return std::nullopt;
                return std::format(R"("{}"."{}")", UpperCopy(*first), UpperCopy(*second));
            }
            return std::format(R"("{}")", UpperCopy(*first));
        }

        static std::string UpperCopy(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (char const c: s)
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            return out;
        }

        std::optional<std::string> ParseIdentifierSegment()
        {
            if (Eof())
                return std::nullopt;
            char const c = _sql[_pos];
            if (c == '"')
                return ParseQuotedDelim('"');
            if (c == '[')
                return ParseQuotedDelim(']'); // open '[' -> close ']'
            if (!IsIdStart(c))
                return std::nullopt;
            size_t start = _pos;
            while (_pos < _sql.size() && IsIdCont(_sql[_pos]))
                ++_pos;
            return std::string { _sql.substr(start, _pos - start) };
        }

        std::optional<std::string> ParseQuotedDelim(char closeDelim)
        {
            char const openDelim = (closeDelim == ']') ? '[' : closeDelim;
            if (Eof() || _sql[_pos] != openDelim)
                return std::nullopt;
            ++_pos;
            size_t start = _pos;
            while (_pos < _sql.size() && _sql[_pos] != closeDelim)
                ++_pos;
            if (_pos >= _sql.size())
                return std::nullopt;
            auto body = std::string { _sql.substr(start, _pos - start) };
            ++_pos; // consume closer
            return body;
        }

        std::optional<std::string> ParseComparisonOp()
        {
            SkipWs();
            if (Eof())
                return std::nullopt;
            if (_sql[_pos] == '=')
            {
                ++_pos;
                return std::string { "=" };
            }
            if (_sql[_pos] == '<')
            {
                ++_pos;
                if (_pos < _sql.size() && _sql[_pos] == '=')
                {
                    ++_pos;
                    return std::string { "<=" };
                }
                if (_pos < _sql.size() && _sql[_pos] == '>')
                {
                    ++_pos;
                    return std::string { "<>" };
                }
                return std::string { "<" };
            }
            if (_sql[_pos] == '>')
            {
                ++_pos;
                if (_pos < _sql.size() && _sql[_pos] == '=')
                {
                    ++_pos;
                    return std::string { ">=" };
                }
                return std::string { ">" };
            }
            if (_sql[_pos] == '!' && _pos + 1 < _sql.size() && _sql[_pos + 1] == '=')
            {
                _pos += 2;
                return std::string { "<>" }; // canonicalize `!=` to `<>`
            }
            return std::nullopt;
        }

        /// Value on the right-hand side of a comparison: numeric/string literal,
        /// NULL, or another identifier (column reference). The `= null` idiom is
        /// preserved verbatim even though it's semantically never true — LUP SQL
        /// uses it as a guard (and the ODBC layer hands the literal through).
        std::optional<std::string> ParseValue()
        {
            SkipWs();
            if (Eof())
                return std::nullopt;

            if (TryKeyword("NULL"))
                return std::string { "NULL" };

            char const c = _sql[_pos];
            if (c == '\'')
                return ParseStringLiteral();
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+')
                return ParseNumericLiteral();
            if (IsIdStart(c) || c == '"' || c == '[')
                return ParseIdentifier();
            return std::nullopt;
        }

        std::optional<std::string> ParseStringLiteral()
        {
            if (Eof() || _sql[_pos] != '\'')
                return std::nullopt;
            size_t start = _pos;
            ++_pos;
            while (_pos < _sql.size())
            {
                if (_sql[_pos] != '\'')
                {
                    ++_pos;
                    continue;
                }
                if (_pos + 1 < _sql.size() && _sql[_pos + 1] == '\'')
                {
                    _pos += 2;
                    continue;
                }
                ++_pos;
                return std::string { _sql.substr(start, _pos - start) };
            }
            return std::nullopt; // unterminated
        }

        std::optional<std::string> ParseNumericLiteral()
        {
            SkipWs();
            size_t start = _pos;
            if (!Eof() && (_sql[_pos] == '-' || _sql[_pos] == '+'))
                ++_pos;
            bool any = false;
            while (_pos < _sql.size() && std::isdigit(static_cast<unsigned char>(_sql[_pos])))
            {
                ++_pos;
                any = true;
            }
            if (_pos < _sql.size() && _sql[_pos] == '.')
            {
                ++_pos;
                while (_pos < _sql.size() && std::isdigit(static_cast<unsigned char>(_sql[_pos])))
                {
                    ++_pos;
                    any = true;
                }
            }
            if (!any)
                return std::nullopt;
            return std::string { _sql.substr(start, _pos - start) };
        }

        /// Captures a balanced `( … )` group whose body starts with a SELECT
        /// keyword (the only subquery flavour LUP uses). We keep the body verbatim
        /// after light normalization — enough for structural validation.
        std::optional<std::string> ParseSubquery()
        {
            if (!TryChar('('))
                return std::nullopt;
            SkipWs();
            if (!PeekKeyword("SELECT"))
                return std::nullopt;

            size_t start = _pos;
            int depth = 1;
            bool inString = false;
            while (_pos < _sql.size() && depth > 0)
            {
                char const c = _sql[_pos];
                if (inString)
                {
                    if (c == '\'')
                    {
                        if (_pos + 1 < _sql.size() && _sql[_pos + 1] == '\'')
                            ++_pos;
                        else
                            inString = false;
                    }
                    ++_pos;
                    continue;
                }
                if (c == '\'')
                    inString = true;
                else if (c == '(')
                    ++depth;
                else if (c == ')')
                {
                    if (--depth == 0)
                        break;
                }
                ++_pos;
            }
            if (depth != 0)
                return std::nullopt;
            auto body = std::string { _sql.substr(start, _pos - start) };
            ++_pos; // consume the matching ')'
            // Canonicalise identifiers inside the subquery (uppercase + double-quote
            // every bareword) so PostgreSQL can resolve column/table names regardless
            // of how the source SQL spelt them, then collapse whitespace for a stable
            // canonical form.
            return Canonicalize(CanonicalizeIdentifiersInSql(body));
        }

        /// Trims the body and collapses runs of whitespace to a single space.
        static std::string Canonicalize(std::string s)
        {
            std::string out;
            out.reserve(s.size());
            bool inWs = true;
            bool inString = false;
            for (size_t i = 0; i < s.size(); ++i)
            {
                char const c = s[i];
                if (inString)
                {
                    out += c;
                    if (c == '\'')
                    {
                        if (i + 1 < s.size() && s[i + 1] == '\'')
                        {
                            out += s[i + 1];
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
                    out += c;
                    inWs = false;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)))
                {
                    if (!inWs)
                    {
                        out += ' ';
                        inWs = true;
                    }
                    continue;
                }
                out += c;
                inWs = false;
            }
            while (!out.empty() && out.back() == ' ')
                out.pop_back();
            return out;
        }
    };

} // namespace

std::optional<std::string> ParseWhereClause(std::string_view whereBody)
{
    WhereParser parser { whereBody };
    return parser.ParseAll();
}

} // namespace Lup2DbTool
