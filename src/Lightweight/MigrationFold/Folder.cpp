// SPDX-License-Identifier: Apache-2.0

#include "Folder.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>

namespace Lightweight::MigrationFold
{

SqlMigration::MigrationTimestamp ResolveUpTo(SqlMigration::MigrationManager const& manager, std::string_view raw)
{
    if (raw.empty())
    {
        auto const& releases = manager.GetAllReleases();
        if (releases.empty())
            throw std::runtime_error("--up-to omitted and no releases are registered");
        return releases.back().highestTimestamp;
    }

    auto const allDigits = std::ranges::all_of(raw, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    if (allDigits)
    {
        std::uint64_t value = 0;
        auto const* const begin = raw.data();
        auto const* const end = begin + raw.size();
        auto const result = std::from_chars(begin, end, value);
        if (result.ec != std::errc {} || result.ptr != end)
            throw std::runtime_error(std::format("Failed to parse --up-to timestamp '{}'", raw));
        return SqlMigration::MigrationTimestamp { value };
    }

    auto const* const release = manager.FindReleaseByVersion(raw);
    if (!release)
        throw std::runtime_error(std::format("Unknown release version: '{}'", raw));
    return release->highestTimestamp;
}

FoldResult Fold(SqlMigration::MigrationManager const& manager,
                SqlQueryFormatter const& formatter,
                std::optional<SqlMigration::MigrationTimestamp> upToInclusive)
{
    return manager.FoldRegisteredMigrations(formatter, upToInclusive);
}

} // namespace Lightweight::MigrationFold
