// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlSchema.hpp"
#include "SqlSchemaDiff.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace Lightweight
{

class SqlConnection;

namespace SqlSchema
{

    /// One row-level data difference between two databases.
    struct RowDiff
    {
        DiffKind kind {};

        /// String form of the row's primary key tuple. Empty for tables without a PK
        /// (in which case @ref TableDataDiff::skipReason is set instead).
        std::vector<std::string> primaryKey {};

        /// For @ref DiffKind::Changed: per-column tuple of (column name, value-on-A, value-on-B)
        /// where the values differ. Empty for OnlyInA / OnlyInB.
        std::vector<std::tuple<std::string, std::string, std::string>> changedCells {};
    };

    /// Result of comparing the rows of one table across two databases.
    struct TableDataDiff
    {
        std::string tableName {};

        /// All row-level diffs found, in primary-key order.
        std::vector<RowDiff> rows {};

        /// Total rows scanned from each side (informational; useful for headers).
        std::size_t aRowCount = 0;
        std::size_t bRowCount = 0;

        /// True when scanning was cut short by the @c maxRows cap.
        bool truncated = false;

        /// When set, the diff was not performed; the value is a human-readable reason
        /// (e.g. `"no primary key"`).
        std::optional<std::string> skipReason {};
    };

    /// Live progress event reported during a data diff. Fired ~2 Hz at most.
    struct DiffProgressEvent
    {
        std::string tableName;
        std::size_t rowsScannedA = 0;
        std::size_t rowsScannedB = 0;
        std::size_t expectedRowsA = 0; ///< 0 if unknown.
        std::size_t expectedRowsB = 0; ///< 0 if unknown.
    };

    /// Callback invoked during a data diff to report progress.
    using DiffProgressCallback = std::function<void(DiffProgressEvent const&)>;

    /// Compares the rows of one table across two databases.
    ///
    /// Each side passes its own @ref Table descriptor: @p tableA describes the table on
    /// connection @p a, @p tableB on connection @p b. Both descriptors are expected to
    /// describe the same logical table — callers should run the schema diff first and
    /// only invoke this for tables that match — but their engine-specific schema labels
    /// (`dbo` vs `public` vs `""`) may differ. Each side's SELECT is qualified with its
    /// own schema label so the same query doesn't fail on the other engine.
    ///
    /// Column order and primary keys are taken from @p tableA: cross-engine schema
    /// equivalence guarantees the column names match, and using one side's order keeps
    /// rows positionally aligned.
    ///
    /// All column values are compared as their ODBC string representation (the same coercion
    /// used by `dbtool exec`). This may produce false positives for floating-point or binary
    /// columns whose textual encoding differs across drivers.
    ///
    /// @param a          Connection to the left-hand database (already connected).
    /// @param b          Connection to the right-hand database (already connected).
    /// @param tableA     Schema of the table on side A; columns and primary keys are
    ///                   read from this descriptor.
    /// @param tableB     Schema of the table on side B; only the schema label is used,
    ///                   to qualify the SELECT issued against connection @p b.
    /// @param maxRows    Maximum number of rows to scan per side. 0 means unlimited.
    /// @param onProgress Optional callback fired ~2 Hz with current scan counters.
    LIGHTWEIGHT_API TableDataDiff DiffTableData(SqlConnection& a,
                                                SqlConnection& b,
                                                Table const& tableA,
                                                Table const& tableB,
                                                std::size_t maxRows = 0,
                                                DiffProgressCallback onProgress = {});

} // namespace SqlSchema

} // namespace Lightweight
