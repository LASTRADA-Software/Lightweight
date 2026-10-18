// SPDX-License-Identifier: Apache-2.0

#include "BackupRequirementReport.hpp"

#include <format>
#include <iterator>

namespace Lightweight::Tools
{

std::string FormatBackupRequirementRefusal(SqlMigration::BackupRequirement const& requirement,
                                           std::string_view invocation,
                                           std::string_view recordProblem)
{
    auto message = DescribeBackupRequirement(requirement) + ".\n";
    if (requirement.lastBackup)
        std::format_to(std::back_inserter(message), "{}\n", DescribeStaleBackup(*requirement.lastBackup));
    if (!recordProblem.empty())
        std::format_to(std::back_inserter(message), "{}\n", recordProblem);
    std::format_to(std::back_inserter(message),
                   "Run: dbtool backup --output <file>   or   dbtool {0} --backup-to <file>\n"
                   "Or, to migrate without a backup (recorded as waived): dbtool {0} --without-backup",
                   invocation);
    return message;
}

std::string FormatBackupRequirementDryRun(SqlMigration::BackupRequirement const& requirement, BackupOverride backupOverride)
{
    return FormatBackupRequirementDryRun(
        requirement, backupOverride, BackupOverrideNames { .backupFirst = "--backup-to", .waive = "--without-backup" });
}

} // namespace Lightweight::Tools
