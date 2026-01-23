// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "SqlBackupFormats.hpp"

#include <iosfwd>
#include <memory>

namespace Lightweight::SqlBackup
{

/// Factory for creating chunk writers.
std::unique_ptr<ChunkWriter> CreateMsgPackChunkWriter(size_t limitBytes = 10 * 1024 * 1024);

/// Factory for creating chunk readers.
std::unique_ptr<ChunkReader> CreateMsgPackChunkReader(std::istream& input);

} // namespace Lightweight::SqlBackup
