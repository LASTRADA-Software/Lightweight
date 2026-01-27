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
//   cmake -DLUPMIGRATION_SQL_DIR=/path/to/model4_JP ..
//
// The generated migrations are compiled as a separate source file and
// linked into this plugin. Use TransitionGlue::Initialize() to migrate
// from LASTRADA_PROPERTIES to schema_migrations before running migrations
// for the first time on an existing database.

#include <Lightweight/SqlMigration.hpp>

// Note: Generated migrations (GeneratedMigrations.cpp) are compiled separately
// and linked into this plugin via CMake. The migrations auto-register with
// the MigrationManager through static initialization.

// Export the migration manager for dbtool plugin loading
LIGHTWEIGHT_MIGRATION_PLUGIN()
