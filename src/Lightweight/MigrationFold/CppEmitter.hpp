// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "Folder.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Lightweight::MigrationFold
{

/// @brief Configuration for `EmitCppBaseline`.
struct CppEmitOptions
{
    /// Output `.cpp` file. May be a `<stem>.cpp` — the writer will split into
    /// `<stem>_part01.cpp`, `<stem>_part02.cpp`, ... when needed.
    std::filesystem::path outputPath;
    /// Threshold for splitting the body across multiple `.cpp` files. Zero
    /// disables splitting and emits a single file.
    std::size_t maxLinesPerFile = 5000;
    /// Optional plugin name. When non-empty (and `--emit-cmake` is requested by
    /// the caller) `EmitPluginCmake` is invoked alongside the source emission.
    std::string pluginName;
    /// When true, emit `CMakeLists.txt` + `Plugin.cpp` next to the generated
    /// sources so the directory becomes a drop-in plugin.
    bool emitCmake = false;
};

/// @brief Emits a self-contained baseline migration as one `LIGHTWEIGHT_SQL_MIGRATION`
/// body that reproduces the post-fold schema and data state.
///
/// The emitted code includes:
///   1. A runtime guard inside `Up()` that throws if `schema_migrations` already has
///      rows (the operator must run `dbtool hard-reset` or `mark-applied` first).
///   2. One `plan.CreateTable(...)` call per surviving table, with all columns and
///      composite FKs reproduced from the fold.
///   3. One `plan.CreateIndex(...)` call per surviving index.
///   4. Every data step rendered as the equivalent DSL builder call (Insert / Update /
///      Delete / RawSql), grouped by source migration via header comments.
///   5. `LIGHTWEIGHT_SQL_RELEASE(...)` markers for releases inside the fold range.
///
/// When `options.maxLinesPerFile > 0` and the body would exceed that budget, the body
/// is split into `<stem>_partNN.cpp` companion files using the shared `SplitFileWriter`.
LIGHTWEIGHT_API void EmitCppBaseline(FoldResult const& fold, CppEmitOptions const& options);

} // namespace Lightweight::MigrationFold
