// SPDX-License-Identifier: Apache-2.0
//
// Which dbtool commands can change a database. A command that writes refuses a target it
// merely discovered (see `Config::AllowsWrites`) and prints its target before it starts.

#pragma once

#include <string_view>

namespace Lightweight::Tools
{

/// Whether `dbtool <command>` can change the database it connects to.
///
/// Fails closed: every command not listed as read-only counts as writing, so a command added
/// later is guarded until it is classified.
///
/// @param command The command name, e.g. "restore".
/// @param dryRun  Whether `--dry-run` was given; it makes the commands that honour it read-only.
/// @return True when the command may write.
[[nodiscard]] bool CommandWritesToDatabase(std::string_view command, bool dryRun) noexcept;

/// Whether `dbtool <command>` loads migration plugins, and so runs their post-init hooks.
/// @param command A command that connects to a database, e.g. "status".
/// @return False for `backup`, `restore` and `exec`, which work without the migration manager.
[[nodiscard]] bool CommandLoadsPlugins(std::string_view command) noexcept;

} // namespace Lightweight::Tools
