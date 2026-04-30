// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::CodeGen
{

/// @brief A pre-rendered text block plus its line count, ready to be packed into
/// one or more output files by `EmitChunked`.
///
/// The block is treated as opaque — `EmitChunked` never splits a block in two.
/// Callers compute `lineCount` once and reuse the same value across multiple
/// passes, since counting newlines on every visit would be wasteful for large
/// migrations.
struct CodeBlock
{
    /// Pre-rendered text of the block. Treated as opaque by `EmitChunked` —
    /// never split across files.
    std::string content;

    /// Number of newlines in `content`, computed once by the caller and reused
    /// across packing passes.
    std::size_t lineCount = 0;
};

/// @brief Greedy bin-packing of `blocks` across files of at most `maxLinesPerFile`
/// lines.
///
/// One block always lands wholly in one chunk — even when its line count exceeds
/// the budget — because builder DSL chains cannot be broken across translation
/// units. The returned outer vector has no empty inner vectors. When
/// `maxLinesPerFile == 0`, every block lands in a single chunk.
[[nodiscard]] LIGHTWEIGHT_API std::vector<std::vector<CodeBlock>> GroupBlocksByLineBudget(
    std::vector<CodeBlock> const& blocks, std::size_t maxLinesPerFile);

/// @brief Result of an `EmitChunked` call.
struct WriteResult
{
    /// All files actually written, in apply order. Either `[outputPath]` (single
    /// file) or `[<stem>_part01.<ext>, <stem>_part02.<ext>, ...]`.
    std::vector<std::filesystem::path> writtenFiles;
};

/// @brief Writes one or more output files, splitting `blocks` across `<stem>_partNN.<ext>`
/// siblings when their combined line count exceeds `maxLinesPerFile`. When the
/// total fits, a single `outputPath` is written and split is skipped.
///
/// `fileHeader` and `fileFooter` are emitted at the top/bottom of every produced
/// file (so all chunks remain self-contained translation units when used for
/// `.cpp` outputs). Pass empty strings to skip them.
///
/// @return `WriteResult` listing the paths actually written.
/// @throws `std::runtime_error` if any output file cannot be opened.
[[nodiscard]] LIGHTWEIGHT_API WriteResult EmitChunked(std::filesystem::path const& outputPath,
                                                      std::vector<CodeBlock> const& blocks,
                                                      std::size_t maxLinesPerFile,
                                                      std::string_view fileHeader = {},
                                                      std::string_view fileFooter = {});

/// @brief Writes a `CMakeLists.txt` and a `Plugin.cpp` next to the generated
/// migration sources so the output directory becomes a drop-in plugin.
///
/// Mirrors the layout of `src/tools/LupMigrationsPlugin/` so consumers can
/// `add_subdirectory()` the generated dir and pick up a self-registering plugin
/// without further glue.
LIGHTWEIGHT_API void EmitPluginCmake(std::filesystem::path const& outputDir,
                                     std::string_view pluginName,
                                     std::string_view sourceGlob = "lup_*.cpp");

} // namespace Lightweight::CodeGen
