// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlQuery/MigrationPlan.hpp>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Lightweight::Tools
{

/// @brief Knobs controlling how a migration source file is emitted.
///
/// @ingroup SqlMigration
struct MigrationSourceOptions
{
    /// The `YYYYMMDDHHMMSS` timestamp that identifies the migration. Emitted verbatim as
    /// the first argument of `LIGHTWEIGHT_SQL_MIGRATION`, so the caller owns the format;
    /// `FormatMigrationTimestamp` produces the canonical shape.
    std::string timestamp;

    /// Human-readable migration title, emitted as the macro's second argument.
    std::string title;

    /// When true, the file also carries `LIGHTWEIGHT_MIGRATION_PLUGIN()`. Exactly one
    /// translation unit per migration plugin may define it, so this defaults to off:
    /// a generated migration usually joins an existing plugin that already has one.
    bool includePluginEntryPoint = false;

    /// Optional provenance banner emitted as a comment above the migration, e.g. the
    /// connection the diff was taken against. Empty means no banner.
    std::string provenance;
};

/// @brief Emits a compilable `.cpp` migration source file for @p plan.
///
/// The body is the migration DSL spelling of @p plan: `CreateTable`, `AlterTable`,
/// `CreateIndex`, `DropTable` and `RawSql` elements each render as the fluent call that
/// would rebuild them. A column declaration that the fluent shorthands cannot express
/// losslessly (a default value, a composite primary-key position, a GUID primary key)
/// falls back to the `Column(SqlColumnDeclaration { ... })` overload rather than silently
/// dropping the attribute.
///
/// Data plan elements (INSERT / UPDATE / DELETE) are not schema changes and are emitted as
/// a visible `// NOTE:` comment instead of code — autogenerate produces DDL only, and a
/// silently missing data step would be worse than an obvious one.
///
/// An empty @p plan yields a valid migration with an empty body, which is the scaffolding
/// mode: a correctly-timestamped file for the author to fill in.
///
/// @param options Timestamp, title and emission knobs.
/// @param plan The plan elements to spell out, in replay order.
/// @return The full contents of the `.cpp` file, newline-terminated.
[[nodiscard]] std::string PrintMigrationSource(MigrationSourceOptions const& options,
                                                               std::span<SqlMigrationPlanElement const> plan);

/// @brief Formats a time point as a `YYYYMMDDHHMMSS` migration timestamp in UTC.
///
/// UTC rather than local time so that two authors in different zones cannot produce
/// timestamps that order differently from the wall-clock order in which they wrote them.
///
/// @param when The instant to format.
/// @return The 14-digit timestamp.
[[nodiscard]] std::string FormatMigrationTimestamp(std::chrono::system_clock::time_point when);

/// @brief Turns a free-form migration title into a filename-safe `lower_snake` slug.
///
/// Alphanumerics are lower-cased and kept, every other run of characters collapses to a
/// single underscore, and leading/trailing underscores are trimmed. A title with no usable
/// characters yields `"migration"` so the caller always has a filename.
///
/// @param title The human-readable title.
/// @return The slug.
[[nodiscard]] std::string SlugifyMigrationTitle(std::string_view title);

} // namespace Lightweight::Tools
