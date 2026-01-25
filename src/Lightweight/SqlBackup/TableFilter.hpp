// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::SqlBackup
{

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251) // STL types in DLL interface
#endif

/// Filters tables by name patterns with glob-style wildcards.
///
/// Supports:
/// - Exact table names: "Users", "Products"
/// - Wildcard suffix: "User*" (matches UserAccounts, Users, etc.)
/// - Wildcard prefix: "*_log" (matches audit_log, error_log, etc.)
/// - Wildcard anywhere: "*audit*" (matches any table containing "audit")
/// - Single char wildcard: "User?" (matches Users, User1, etc.)
/// - Schema.table notation: "dbo.Users", "sales.*"
/// - Comma-separated patterns: "Users,Products,*_log"
class LIGHTWEIGHT_API TableFilter
{
  public:
    /// Parses a filter specification string.
    ///
    /// @param filterSpec Comma-separated patterns like "table1,table2,foo*,schema.table"
    ///                   Empty string or "*" means match all tables.
    static TableFilter Parse(std::string_view filterSpec);

    /// Checks if a table matches any of the patterns.
    ///
    /// @param schema The schema name (can be empty).
    /// @param tableName The table name.
    /// @return true if the table matches at least one pattern.
    [[nodiscard]] bool Matches(std::string_view schema, std::string_view tableName) const;

    /// Returns true if the filter matches all tables (no filtering applied).
    [[nodiscard]] bool MatchesAll() const noexcept { return _matchesAll; }

    /// Returns the number of patterns in this filter.
    [[nodiscard]] size_t PatternCount() const noexcept { return _patterns.size(); }

  private:
    struct Pattern
    {
        std::optional<std::string> schema; ///< Schema pattern (nullopt = any schema)
        std::string table;                 ///< Table name pattern
    };

    std::vector<Pattern> _patterns;
    bool _matchesAll = true;

    /// Performs glob-style pattern matching.
    ///
    /// @param pattern The pattern with * and ? wildcards.
    /// @param text The text to match against.
    /// @return true if text matches the pattern.
    static bool GlobMatch(std::string_view pattern, std::string_view text);
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace Lightweight::SqlBackup
