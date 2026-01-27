// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "SqlBackupFormats.hpp"

#include <iosfwd>
#include <memory>

namespace Lightweight::SqlBackup
{

/// Factory for creating chunk writers.
LIGHTWEIGHT_API std::unique_ptr<ChunkWriter> CreateMsgPackChunkWriter(size_t limitBytes = 10 * 1024 * 1024);

/// Factory for creating chunk readers.
LIGHTWEIGHT_API std::unique_ptr<ChunkReader> CreateMsgPackChunkReader(std::istream& input);

/// Factory for creating chunk readers from an existing buffer (zero-copy).
///
/// This variant avoids copying the buffer into the reader, reducing memory usage
/// during restore operations. The caller must ensure the buffer remains valid
/// for the lifetime of the reader.
///
/// @param buffer The buffer containing MsgPack data to read from.
/// @return A unique pointer to the chunk reader.
LIGHTWEIGHT_API std::unique_ptr<ChunkReader> CreateMsgPackChunkReaderFromBuffer(std::span<uint8_t const> buffer);

} // namespace Lightweight::SqlBackup
