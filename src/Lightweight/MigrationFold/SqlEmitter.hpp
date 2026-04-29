// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "Folder.hpp"

#include <filesystem>

namespace Lightweight
{
class SqlQueryFormatter;
}

namespace Lightweight::MigrationFold
{

/// @brief Configuration for `EmitSqlBaseline`.
struct SqlEmitOptions
{
    /// Output `.sql` file.
    std::filesystem::path outputPath;
    /// Required dialect — the formatter that drives all `ToSql(...)` rendering.
    /// `EmitSqlBaseline` itself never opens a connection; the dialect determines
    /// the emitted SQL flavour.
    SqlQueryFormatter const* formatter = nullptr;
    /// Human-readable dialect label included in the file's header comment so the
    /// artifact is self-describing.
    std::string_view dialectLabel;
};

/// @brief Emits a flat `.sql` baseline that reproduces the post-fold schema and data.
///
/// The emitted file:
///   1. Header comment naming the dialect (`-- Dialect: PostgreSQL`).
///   2. For each table in fold creation order: a `CREATE TABLE` rendered via
///      `ToSql(formatter, ...)`.
///   3. For each surviving index: a `CREATE INDEX` rendered via `ToSql(...)`.
///   4. Every data step rendered via `ToSql(...)` (INSERT / UPDATE / DELETE / RawSql).
///   5. A trailing `INSERT INTO schema_migrations (...) VALUES ...` for every
///      fold-input timestamp so the post-fold DB looks identical to a real apply-all.
///
/// The emitted SQL is dialect-specific to the chosen `formatter`. There is no
/// runtime guard against applying it to a non-empty `schema_migrations` — a flat
/// SQL script is the operator's tool, and the trailing inserts will fail loudly
/// with a primary-key conflict in that situation.
LIGHTWEIGHT_API void EmitSqlBaseline(FoldResult const& fold, SqlEmitOptions const& options);

} // namespace Lightweight::MigrationFold
