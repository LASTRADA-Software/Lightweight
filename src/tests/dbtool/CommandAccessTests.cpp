// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for which dbtool commands count as writing to the database.

#include "../../tools/dbtool/CommandAccess.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <string_view>

using Lightweight::Tools::CommandLoadsPlugins;
using Lightweight::Tools::CommandWritesToDatabase;

namespace
{

/// A command and whether it writes without and with `--dry-run`.
struct CommandCase
{
    std::string_view command;
    bool writes;
    bool writesWithDryRun;
};

constexpr auto CommandCases = std::array {
    CommandCase { .command = "list-pending", .writes = false, .writesWithDryRun = false },
    CommandCase { .command = "list-applied", .writes = false, .writesWithDryRun = false },
    CommandCase { .command = "status", .writes = false, .writesWithDryRun = false },
    CommandCase { .command = "releases", .writes = false, .writesWithDryRun = false },
    CommandCase { .command = "backup", .writes = false, .writesWithDryRun = false },
    CommandCase { .command = "migrate", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "migrate-to-release", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "rewrite-checksums", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "hard-reset", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "unicode-upgrade-tables", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "restore", .writes = true, .writesWithDryRun = false },
    CommandCase { .command = "apply", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "rollback", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "rollback-to", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "rollback-to-release", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "mark-applied", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "exec", .writes = true, .writesWithDryRun = true },
    CommandCase { .command = "some-future-command", .writes = true, .writesWithDryRun = true },
};

} // namespace

TEST_CASE("CommandWritesToDatabase classifies every dbtool command", "[dbtool][CommandAccess]")
{
    for (auto const& testCase: CommandCases)
    {
        INFO(testCase.command);
        CHECK(CommandWritesToDatabase(testCase.command, /*dryRun=*/false) == testCase.writes);
        CHECK(CommandWritesToDatabase(testCase.command, /*dryRun=*/true) == testCase.writesWithDryRun);
    }
}

TEST_CASE("CommandLoadsPlugins is false only for the commands without the migration manager", "[dbtool][CommandAccess]")
{
    for (std::string_view const command: { "backup", "restore", "exec" })
    {
        INFO(command);
        CHECK_FALSE(CommandLoadsPlugins(command));
    }
    for (std::string_view const command: { "status",
                                           "list-pending",
                                           "list-applied",
                                           "releases",
                                           "migrate",
                                           "migrate-to-release",
                                           "apply",
                                           "rollback",
                                           "rollback-to",
                                           "rollback-to-release",
                                           "mark-applied",
                                           "rewrite-checksums",
                                           "hard-reset",
                                           "unicode-upgrade-tables" })
    {
        INFO(command);
        CHECK(CommandLoadsPlugins(command));
    }
}
