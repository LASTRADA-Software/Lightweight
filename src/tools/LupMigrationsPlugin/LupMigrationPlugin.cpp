// SPDX-License-Identifier: Apache-2.0

// This file serves as the entry point for the LUP Migrations Plugin.
// It exports the migration manager for use by dbtool.
//
// Build Configuration:
// Set LUPMIGRATION_SQL_DIR in CMake to point to the directory containing
// LUP SQL migration files (init_m_*.sql, upd_m_*.sql). The build system
// will automatically run lup2dbtool to generate C++ migrations.
//
// Example:
//   cmake -DLUPMIGRATION_SQL_DIR=/path/to/lup-sql ..
//
// The generated migrations are compiled as a separate source file and
// linked into this plugin. Use TransitionGlue::Initialize() to migrate
// from LASTRADA_PROPERTIES to schema_migrations before running migrations
// for the first time on an existing database.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/MigrationPlan.hpp>

#include <set>
#include <string>

// Note: Generated migrations (GeneratedMigrations.cpp) are compiled separately
// and linked into this plugin via CMake. The migrations auto-register with
// the MigrationManager through static initialization.

namespace
{

/// @brief Boundary between "legacy LUP migrations that need LUpd-compatible
/// client-side string truncation" and "modern migrations that don't".
///
/// Every LUP migration with a timestamp strictly less than this value is rendered
/// with `lup-truncate` active; anything at or above this value gets strict,
/// standards-compliant behaviour. The value is the first migration timestamp of
/// the 6.0.0 release — chosen because release 6.0.0 is where we declare modern
/// conventions (no-silent-truncation, Unicode-correct widths) as the norm.
///
/// The threshold is intentionally hard-coded here, not exposed as a knob: compat
/// scope is a property of the legacy *code*, not of the deployment. Operators
/// should not need to configure it.
constexpr uint64_t kLupLegacyCutoffTimestamp = 20'000'000'060'000ULL;

/// @brief Returns the compat flags that apply to a single LUP migration.
///
/// Pure function: no side effects, no I/O, no state. Installed once as the
/// `MigrationManager`'s compat policy at plugin load time.
std::set<std::string> LupCompatPolicy(Lightweight::SqlMigration::MigrationBase const& m)
{
    if (m.GetTimestamp().value < kLupLegacyCutoffTimestamp)
        return { std::string(Lightweight::CompatFlagLupTruncateName) };
    return {};
}

/// @brief RAII-less registrar that installs `LupCompatPolicy` on the singleton
/// `MigrationManager` at shared-library load time. Lives in an anonymous namespace
/// so the symbol stays plugin-local.
struct PolicyInstaller
{
    PolicyInstaller()
    {
        Lightweight::SqlMigration::MigrationManager::GetInstance().SetCompatPolicy(&LupCompatPolicy);
    }
};

[[maybe_unused]] PolicyInstaller const kInstallPolicy {};

} // namespace

// Export the migration manager for dbtool plugin loading
LIGHTWEIGHT_MIGRATION_PLUGIN()
