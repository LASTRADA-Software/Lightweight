// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlDataDiff.hpp>
#include <Lightweight/SqlSchemaDiff.hpp>

#include <iosfwd>
#include <vector>

namespace Lightweight::Tools
{

/// @brief Display options for `dbtool diff` output.
struct DiffRenderOptions
{
    /// When false, ANSI color escapes are suppressed entirely.
    bool useColor = true;

    /// Maximum table width in terminal columns. 0 means "auto-detect from `COLUMNS`,
    /// fall back to 120".
    int maxWidth = 0;
};

/// @brief Pretty-prints a schema diff and (optionally) a list of per-table data diffs.
///
/// Renders one bordered table section for the schema diff and one section per table
/// data diff. Cells representing additions are colored green, removals red, and
/// modifications yellow. Diff entries are emitted in a stable order so output is
/// deterministic across runs.
///
/// @param out         Output stream (e.g. `std::cout`).
/// @param schemaDiff  Result of `SqlSchema::DiffSchemas`.
/// @param dataDiffs   Optional per-table data diffs (empty when `--schema-only`).
/// @param options     Color / width controls.
void RenderDiff(std::ostream& out,
                SqlSchema::SchemaDiff const& schemaDiff,
                std::vector<SqlSchema::TableDataDiff> const& dataDiffs,
                DiffRenderOptions const& options);

} // namespace Lightweight::Tools
