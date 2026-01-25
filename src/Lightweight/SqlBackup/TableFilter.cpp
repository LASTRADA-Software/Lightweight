// SPDX-License-Identifier: Apache-2.0

#include "TableFilter.hpp"

#include <algorithm>
#include <cctype>

namespace Lightweight::SqlBackup
{

namespace
{
    std::string_view Trim(std::string_view s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        return s;
    }
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TableFilter TableFilter::Parse(std::string_view filterSpec)
{
    TableFilter filter;
    filterSpec = Trim(filterSpec);

    // Empty filter or "*" means match all
    if (filterSpec.empty() || filterSpec == "*")
    {
        filter._matchesAll = true;
        return filter;
    }

    filter._matchesAll = false;

    // Split by comma
    size_t start = 0;
    while (start < filterSpec.size())
    {
        size_t const end = filterSpec.find(',', start);
        std::string_view token =
            (end == std::string_view::npos) ? filterSpec.substr(start) : filterSpec.substr(start, end - start);

        token = Trim(token);

        if (!token.empty())
        {
            Pattern pattern;

            // Check for schema.table notation
            size_t const dotPos = token.find('.');
            if (dotPos != std::string_view::npos)
            {
                std::string_view schemaPattern = token.substr(0, dotPos);
                std::string_view tablePattern = token.substr(dotPos + 1);

                schemaPattern = Trim(schemaPattern);
                tablePattern = Trim(tablePattern);

                // If schema is "*", leave it as nullopt (match any schema)
                if (schemaPattern != "*" && !schemaPattern.empty())
                {
                    pattern.schema = std::string(schemaPattern);
                }

                pattern.table = std::string(tablePattern);
            }
            else
            {
                // No schema specified - match any schema
                pattern.table = std::string(token);
            }

            // Only add non-empty table patterns
            if (!pattern.table.empty())
            {
                // Check if this pattern is "*" which would match all
                if (!pattern.schema.has_value() && pattern.table == "*")
                {
                    filter._matchesAll = true;
                    filter._patterns.clear();
                    return filter;
                }

                filter._patterns.push_back(std::move(pattern));
            }
        }

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    // If no valid patterns were found, match all
    if (filter._patterns.empty())
    {
        filter._matchesAll = true;
    }

    return filter;
}

bool TableFilter::Matches(std::string_view schema, std::string_view tableName) const
{
    if (_matchesAll)
        return true;

    return std::ranges::any_of(_patterns, [&](auto const& pattern) {
        // Check schema match if pattern specifies one
        if (pattern.schema.has_value() && !GlobMatch(*pattern.schema, schema))
            return false;
        // If pattern has no schema, it matches any schema (including empty)

        // Check table match
        return GlobMatch(pattern.table, tableName);
    });
}

bool TableFilter::GlobMatch(std::string_view pattern, std::string_view text)
{
    // Simple recursive glob matching with memoization would be overkill here
    // Use iterative approach with backtracking

    size_t p = 0; // pattern index
    size_t t = 0; // text index
    size_t starP = std::string_view::npos; // position after last '*' in pattern
    size_t starT = 0;                      // position in text when we hit the '*'

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
        {
            // Match single character or '?'
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            // '*' - remember this position and try matching zero characters
            starP = p;
            starT = t;
            ++p;
        }
        else if (starP != std::string_view::npos)
        {
            // Mismatch, but we had a '*' earlier - backtrack
            p = starP + 1;
            ++starT;
            t = starT;
        }
        else
        {
            // Mismatch and no '*' to backtrack to
            return false;
        }
    }

    // Skip trailing '*' in pattern
    while (p < pattern.size() && pattern[p] == '*')
        ++p;

    return p == pattern.size();
}

} // namespace Lightweight::SqlBackup
