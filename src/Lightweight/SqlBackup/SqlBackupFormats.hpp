// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Lightweight::SqlBackup
{

/// Represents a value in a backup row.
using BackupValue = std::variant<std::monostate, // NULL
                                 bool,
                                 int64_t,
                                 double,
                                 std::string,
                                 std::vector<uint8_t> // Binary
                                 >;

/// Represents a batch of backup data in column-oriented format.
struct ColumnBatch
{
    using ColumnData =
        std::variant<std::monostate, // Placeholder (e.g. for pure NULL columns if we optimize that later, or initialization)
                     std::vector<int64_t>,
                     std::vector<double>,
                     std::vector<std::string>,
                     std::vector<std::vector<uint8_t>>, // Binary
                     std::vector<bool>>;

    size_t rowCount = 0;
    std::vector<ColumnData> columns;
    std::vector<std::vector<bool>> nullIndicators; // Parallel to columns: true if NULL

    /// Clears the internal buffer after a flush.
    void Clear()
    {
        rowCount = 0;
        for (auto& col: columns)
        {
            std::visit(
                [](auto& v) {
                    if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, std::monostate>)
                        v.clear();
                },
                col);
        }
        for (auto& inds: nullIndicators)
            inds.clear();
    }
};

/// Interface for writing backup chunks.
struct ChunkWriter
{
    virtual ~ChunkWriter() = default;

    ChunkWriter(ChunkWriter const&) = delete;
    ChunkWriter(ChunkWriter&&) = delete;
    ChunkWriter& operator=(ChunkWriter const&) = delete;
    ChunkWriter& operator=(ChunkWriter&&) = delete;
    ChunkWriter() = default;

    /// Writes a single row to the chunk (buffers it).
    ///
    /// @param row The row data to write.
    virtual void WriteRow(std::span<BackupValue const> row) = 0;

    /// Flushes any buffered data to the output.
    ///
    /// @return The formatted data chunk.
    virtual std::string Flush() = 0;

    /// Checks if the current chunk should be finalized (e.g. size limit reached).
    ///
    /// @return True if the chunk is full.
    [[nodiscard]] virtual bool IsChunkFull() const = 0;

    /// Clears the internal buffer after a flush.
    virtual void Clear() = 0;
};

/// Interface for reading backup chunks.
struct ChunkReader
{
    virtual ~ChunkReader() = default;

    ChunkReader(ChunkReader const&) = delete;
    ChunkReader(ChunkReader&&) = delete;
    ChunkReader& operator=(ChunkReader const&) = delete;
    ChunkReader& operator=(ChunkReader&&) = delete;
    ChunkReader() = default;

    /// Reads the next batch of data from the chunk.
    ///
    /// @param[out] batch The batch structure to populate.
    /// @return True if data was read, false if end of stream.
    virtual bool ReadBatch(ColumnBatch& batch) = 0;
};

} // namespace Lightweight::SqlBackup
