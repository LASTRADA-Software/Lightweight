// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlSchema.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Lightweight::SqlSchema
{

/// Classifies a schema-level diff entry.
enum class DiffKind : std::uint8_t
{
    OnlyInA, ///< Present only in the left-hand side.
    OnlyInB, ///< Present only in the right-hand side.
    Changed, ///< Present on both sides but with different definitions.
};

/// A diff entry for a single column.
struct ColumnDiff
{
    /// Column name (case sensitivity follows the engines' rules — they're paired by name).
    std::string name;
    /// Column-level classification: only-in-A, only-in-B, or changed.
    DiffKind kind {};

    /// For `DiffKind::Changed`: human-readable list of differing fields
    /// (e.g. `"type"`, `"nullable"`, `"defaultValue"`). Empty otherwise.
    std::vector<std::string> changedFields {};

    /// Pointer into the input `TableList` for the left-hand column. Null when
    /// `kind` is `DiffKind::OnlyInB`.
    Column const* a = nullptr;

    /// Pointer into the input `TableList` for the right-hand column. Null when
    /// `kind` is `DiffKind::OnlyInA`.
    Column const* b = nullptr;
};

/// A diff entry for a single table.
struct TableDiff
{
    /// Table name (schema-unqualified). Tables are paired across the two inputs by name only —
    /// engine-specific schema labels (`dbo`, `public`, `""`) are kept for display but ignored
    /// for identity, so the same logical table compares as equal across SQL dialects.
    std::string name;

    /// Schema name on the left-hand side. Empty when `kind` is `DiffKind::OnlyInB`
    /// or when the left-hand engine reports no schema (e.g. SQLite).
    std::string schemaA;

    /// Schema name on the right-hand side. Empty when `kind` is `DiffKind::OnlyInA`
    /// or when the right-hand engine reports no schema.
    std::string schemaB;

    /// Table-level classification: `DiffKind::OnlyInA` / `DiffKind::OnlyInB`
    /// when one side lacks the table entirely; `DiffKind::Changed` when both
    /// sides have it but column / key / index definitions differ.
    DiffKind kind {};

    /// Per-column diffs. Populated only when `kind` is `DiffKind::Changed`.
    std::vector<ColumnDiff> columns {};

    /// Human-readable PK differences (e.g. `"primary key columns differ"`).
    std::vector<std::string> primaryKeyDiffs {};

    /// Human-readable FK differences.
    std::vector<std::string> foreignKeyDiffs {};

    /// Human-readable index differences.
    std::vector<std::string> indexDiffs {};
};

/// Result of a full schema comparison.
struct SchemaDiff
{
    /// All diff entries, sorted lexicographically by (schema, name).
    std::vector<TableDiff> tables {};

    /// True if no differences were found.
    [[nodiscard]] bool Empty() const noexcept
    {
        return tables.empty();
    }
};

/// Compares two table lists field-by-field and returns a structural diff.
///
/// Pairs tables by **name only** so the same logical table matches across SQL dialects
/// even when engine-specific schema labels differ (`dbo` vs `public` vs `""`). Both
/// schema labels are kept on `TableDiff` for the renderer. Columns are paired by name.
///
/// Column comparison uses the canonical `Column::type` variant (engine-agnostic),
/// not the dialect-dependent type string, so an `INTEGER` column compares equal whether
/// the driver reports `int4`, `int`, or `INTEGER`. Other compared fields: nullability,
/// size, decimal digits, default value, auto-increment, primary-key membership,
/// foreign-key membership, and uniqueness. Foreign-key constraints are compared at the
/// table level (not per column).
///
/// Pure function — no I/O, no logging, no exceptions.
LIGHTWEIGHT_API SchemaDiff DiffSchemas(TableList const& a, TableList const& b);

} // namespace Lightweight::SqlSchema
