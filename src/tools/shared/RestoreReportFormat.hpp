// SPDX-License-Identifier: Apache-2.0
//
// Words a `SqlBackup::RestoreReport` for people; shared by dbtool and dbtool-gui.

#pragma once

#include <Lightweight/SqlBackup/SqlBackup.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Lightweight::Tools
{

/// Failed-row ranges listed per table; the rest are summarized as a count.
inline constexpr std::size_t MaxListedRowRanges = 5;

/// @param report A table's failures.
/// @return The number of cells of the table that were restored as NULL.
[[nodiscard]] std::uint64_t CountNulledCells(SqlBackup::TableRestoreReport const& report);

/// Describes what failed for one table, one line per fact: the failed row ranges (at most
/// MaxListedRowRanges), the failure reasons with their occurrence counts and sample keys, the cells
/// set to NULL per column, the table failure and the unreadable chunks.
/// @param report The table's failures.
/// @return The lines, without a leading table name or indentation; empty if nothing failed.
[[nodiscard]] std::vector<std::string> DescribeTableRestoreFailures(SqlBackup::TableRestoreReport const& report);

/// One-line summary of a table's failures, e.g. `3 rows failed, 2 cells set to NULL`.
/// @param report The table's failures.
/// @return The summary.
[[nodiscard]] std::string SummarizeTableRestoreFailures(SqlBackup::TableRestoreReport const& report);

/// Renders the whole "Restore failures" block of a restore: per table its summary and details, then
/// the run-level problems and the totals.
/// @param report The restore's report.
/// @return The lines of the block; empty if @p report holds no failures.
[[nodiscard]] std::vector<std::string> DescribeRestoreReport(SqlBackup::RestoreReport const& report);

} // namespace Lightweight::Tools
