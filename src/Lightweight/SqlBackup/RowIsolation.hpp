// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlError.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <stdexcept>
#include <vector>

namespace Lightweight::detail
{

/// How a failed batch insert is handled.
enum class FailureDisposition : std::uint8_t
{
    /// The failure is caused by individual rows: isolate them and insert every other row.
    IsolateRows,
    /// The failure affects the whole table: give up on it (throws @ref TableRestoreFailure).
    FailTable,
    /// The failure is not about the data (e.g. a lost connection): rethrow it as @ref SqlException.
    Propagate,
};

/// Error returned by a @ref RangeInserter for a range it rolled back.
struct BatchInsertError
{
    /// The diagnostics of the failure.
    SqlErrorInfo error;
    /// Absolute row indices the driver reported as failed (from a parameter status array);
    /// empty if the driver did not tell which rows failed.
    std::vector<std::size_t> reportedFailedRows;
};

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4251) // STL types in DLL interface
    #pragma warning(disable : 4275) // non-DLL-interface base class
#endif

/// Thrown by @ref InsertIsolatingFailures when an error is classified @ref FailureDisposition::FailTable.
struct LIGHTWEIGHT_API TableRestoreFailure: std::runtime_error
{
    /// Constructs the exception; what() names the SQLSTATE and the message of @p error.
    /// @param error The error that failed the table.
    explicit TableRestoreFailure(SqlErrorInfo error);

    /// The error that failed the table.
    SqlErrorInfo error;
};

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

/// Executes and commits rows [firstRow, firstRow + rowCount), or rolls them back and returns the error,
/// leaving the connection ready for the next call. Never called with @c rowCount == 0.
using RangeInserter = std::function<std::expected<void, BatchInsertError>(std::size_t firstRow, std::size_t rowCount)>;

/// Decides how a failed insert is handled.
using FailureClassifier = std::function<FailureDisposition(SqlErrorInfo const&)>;

/// Receives a row that could not be inserted, together with the error of the insert that isolated it.
using RowFailureSink = std::function<void(std::size_t row, SqlErrorInfo const&)>;

/// Receives a row that was excluded up front and therefore not inserted.
using ExcludedRowSink = std::function<void(std::size_t row)>;

/// Receives the resume watermark: rows [0, settledPrefix) are each inserted, excluded or reported failed.
using SettledSink = std::function<void(std::size_t settledPrefix)>;

/// Inserts rows [0, rowCount) except those in @p excluded, isolating failing rows.
///
/// Rows are settled strictly in ascending order. Each maximal run of non-excluded rows is passed to
/// @p tryInsert. A run whose failure is classified @ref FailureDisposition::IsolateRows is split at
/// the rows the driver reported as failed, or bisected when it reported none, until every failing
/// row is isolated and reported via @p onRowFailed; all other rows are still inserted.
///
/// @param rowCount Number of rows.
/// @param excluded Rows not to insert; an index beyond its size counts as not excluded.
/// @param tryInsert Inserts one contiguous range of rows.
/// @param classify Classifies the error of a failed insert.
/// @param onRowFailed Called for each isolated failing row, in ascending order.
/// @param onExcludedRow Called for each excluded row when it is reached, in ascending order.
/// @param onSettled Called with the new settled prefix after every committed run, reported row or
///                  excluded row. The value never decreases; when an exception propagates, the last
///                  value reported equals the gap-free settled prefix at that moment.
/// @throws SqlException for an error classified @ref FailureDisposition::Propagate.
/// @throws TableRestoreFailure for an error classified @ref FailureDisposition::FailTable.
LIGHTWEIGHT_API void InsertIsolatingFailures(std::size_t rowCount,
                                             std::vector<bool> const& excluded,
                                             RangeInserter const& tryInsert,
                                             FailureClassifier const& classify,
                                             RowFailureSink const& onRowFailed,
                                             ExcludedRowSink const& onExcludedRow,
                                             SettledSink const& onSettled);

} // namespace Lightweight::detail
