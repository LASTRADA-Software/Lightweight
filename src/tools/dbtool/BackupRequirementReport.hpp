// SPDX-License-Identifier: Apache-2.0
//
// The messages dbtool prints about a migrating command's backup requirement: the refusal and the dry-run
// line. The sentences they share with dbtool-gui (and the waiver warning) live in BackupRecording.hpp.

#pragma once

#include <Lightweight/SqlMigration.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include <BackupRecording.hpp>

namespace Lightweight::Tools
{

/// The exit code of a migrating command that refuses to run because a required backup is missing.
inline constexpr int BackupRequiredExitCode = 4;

/// The message of a migrating command that refuses to run without a backup.
///
/// Names what demands the backup, why the newest backup does not count (when there is one), the
/// problem that kept this run's backup from being recorded (when there was one), how to take a
/// backup, and, as the last option, how to migrate without one.
///
/// @param requirement The unsatisfied requirement.
/// @param invocation The refused command with its argument, e.g. `migrate` or `apply 20240201000000`.
/// @param recordProblem Why the backup this run took could not be recorded; empty when there was none.
/// @return The message, lines separated by `\n`, without a trailing newline.
[[nodiscard]] std::string FormatBackupRequirementRefusal(SqlMigration::BackupRequirement const& requirement,
                                                         std::string_view invocation,
                                                         std::string_view recordProblem);

/// The line a dry run prints for a required backup, saying what a real run would do, with dbtool's
/// `--backup-to` and `--without-backup` as the overrides.
///
/// @param requirement The requirement; `required` is expected to be true.
/// @param backupOverride How the real run would deal with a missing backup.
/// @return One SQL comment line, without a trailing newline.
[[nodiscard]] std::string FormatBackupRequirementDryRun(SqlMigration::BackupRequirement const& requirement,
                                                        BackupOverride backupOverride);

} // namespace Lightweight::Tools
