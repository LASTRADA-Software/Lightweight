// SPDX-License-Identifier: Apache-2.0

#include "CommandAccess.hpp"

#include <algorithm>
#include <array>

namespace Lightweight::Tools
{

namespace
{

    /// Commands that only read the database. `backup` writes an archive file, not the database.
    constexpr auto ReadOnlyCommands = std::array<std::string_view, 5> {
        "list-pending", "list-applied", "status", "releases", "backup",
    };

    /// Commands whose `--dry-run` previews without writing. The rollback commands, `apply`,
    /// `mark-applied` and `exec` have no dry run.
    constexpr auto DryRunReadOnlyCommands = std::array<std::string_view, 6> {
        "migrate", "migrate-to-release", "rewrite-checksums", "hard-reset", "unicode-upgrade-tables", "restore",
    };

    /// Connecting commands that never build the migration manager.
    constexpr auto CommandsWithoutPlugins = std::array<std::string_view, 3> { "backup", "restore", "exec" };

} // namespace

bool CommandLoadsPlugins(std::string_view command) noexcept
{
    return !std::ranges::contains(CommandsWithoutPlugins, command);
}

bool CommandWritesToDatabase(std::string_view command, bool dryRun) noexcept
{
    if (std::ranges::contains(ReadOnlyCommands, command))
        return false;
    return !(dryRun && std::ranges::contains(DryRunReadOnlyCommands, command));
}

} // namespace Lightweight::Tools
