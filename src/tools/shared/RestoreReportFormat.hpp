// SPDX-License-Identifier: Apache-2.0
//
// Words a `SqlBackup::RestoreReport` for people; shared by dbtool and dbtool-gui.

#pragma once

#include <Lightweight/SqlBackup/SqlBackup.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Lightweight::Tools
{

/// Failed-row ranges listed per table; the rest are summarized as a count.
inline constexpr std::size_t MaxListedRowRanges = 5;

/// Describes what failed for one table, one line per fact: the failed row ranges (at most
/// MaxListedRowRanges), the failure reasons with their occurrence counts and sample keys, the cells
/// set to NULL per column, the table failure and the unreadable chunks.
/// @param report The table's failures.
/// @return The lines, without a leading table name or indentation; empty if nothing failed.
[[nodiscard]] std::vector<std::string> DescribeTableRestoreFailures(SqlBackup::TableRestoreReport const& report);

/// Renders the whole "Restore failures" block of a restore: per table its summary and details, then
/// the run-level problems and the totals.
/// @param report The restore's report.
/// @return The lines of the block; empty if @p report holds no failures.
[[nodiscard]] std::vector<std::string> DescribeRestoreReport(SqlBackup::RestoreReport const& report);

/// Renders the "Restore failures" block of a restore as one text.
/// @param report The restore's report.
/// @return The lines of DescribeRestoreReport(), joined by newlines; empty if @p report holds no failures.
[[nodiscard]] std::string JoinedReportText(SqlBackup::RestoreReport const& report);

} // namespace Lightweight::Tools
