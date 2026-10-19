// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin with one migration that always fails while it runs. Its file
// name lacks the `Plugin` suffix, so plugin discovery skips it; test_dbtool.py
// copies it under a `*Plugin` name into a scratch plugins directory to prove
// that a failed migration never reaches the post-migrate hook.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

#include <stdexcept>

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20240101000000, "Failing Fixture Migration")
{
    static_cast<void>(plan);
    throw std::runtime_error("fixture migration failed on purpose");
}
