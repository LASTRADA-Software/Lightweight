// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../DataBinder/SqlRawColumn.hpp"

#include <cstddef>
#include <string>

namespace Lightweight::detail
{

/// Returns a view of @p rowCount consecutive rows of @p column, starting at @p firstRow.
///
/// The result shares the buffers of @p column (it owns nothing) and carries a copy of its metadata, so
/// it can be bound and executed like the full column. Every row occupies `metadata.bufferLength` bytes
/// in `data`, which is what makes the sub-range addressable by row.
///
/// @param column The column to slice; it must outlive the result.
/// @param firstRow Zero-based index of the first row of the slice.
/// @param rowCount Number of rows in the slice.
/// @return The slice. Precondition: `firstRow + rowCount` does not exceed the rows of @p column.
[[nodiscard]] LIGHTWEIGHT_API SqlRawColumn SliceRows(SqlRawColumn const& column, std::size_t firstRow, std::size_t rowCount);

/// Renders one cell of @p column as text, for naming a row in a failure report.
///
/// NULL renders as `NULL`; character data as is; wide character data as UTF-8; integer, bit and floating
/// point values in decimal; a GUID in its canonical text; date, time and timestamp values in ISO form;
/// binary data as `0x` followed by upper-case hex, cut after 32 bytes and marked with an ellipsis.
///
/// @param column The column holding the cell.
/// @param row Zero-based row index within @p column.
/// @return The text of the cell.
[[nodiscard]] LIGHTWEIGHT_API std::string DescribeRawCell(SqlRawColumn const& column, std::size_t row);

} // namespace Lightweight::detail
