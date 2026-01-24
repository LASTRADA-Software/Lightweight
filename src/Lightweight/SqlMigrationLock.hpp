// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlConnection.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace Lightweight::SqlMigration
{

/// RAII-style migration lock to prevent concurrent migrations.
///
/// This class provides a distributed locking mechanism to ensure that only one process
/// can run migrations at a time. The implementation uses database-specific advisory locks:
/// - SQL Server: sp_getapplock / sp_releaseapplock
/// - PostgreSQL: pg_advisory_lock / pg_advisory_unlock
/// - SQLite: BEGIN IMMEDIATE transaction with PRAGMA busy_timeout
///
/// @code
/// auto& connection = migrationManager.GetDataMapper().Connection();
/// MigrationLock lock(connection);
/// if (lock.IsLocked())
/// {
///     migrationManager.ApplyPendingMigrations();
/// }
/// @endcode
///
/// @ingroup SqlMigration
class MigrationLock
{
  public:
    /// Acquire a migration lock.
    ///
    /// @param connection Database connection to use for locking
    /// @param lockName Name of the lock (default: "lightweight_migration")
    /// @param timeout Maximum time to wait for lock acquisition
    /// @throws SqlException if lock cannot be acquired within timeout
    LIGHTWEIGHT_API explicit MigrationLock(SqlConnection& connection,
                                           std::string_view lockName = "lightweight_migration",
                                           std::chrono::milliseconds timeout = std::chrono::seconds(30));

    /// Releases the lock on destruction.
    LIGHTWEIGHT_API ~MigrationLock();

    MigrationLock(MigrationLock const&) = delete;
    MigrationLock& operator=(MigrationLock const&) = delete;

    /// Move constructor.
    LIGHTWEIGHT_API MigrationLock(MigrationLock&& other) noexcept;

    /// Move assignment operator.
    LIGHTWEIGHT_API MigrationLock& operator=(MigrationLock&& other) noexcept;

    /// Check if lock is held.
    ///
    /// @return true if the lock is currently held by this instance
    [[nodiscard]] bool IsLocked() const noexcept
    {
        return _locked;
    }

    /// Release the lock early.
    ///
    /// This is automatically called in the destructor, but can be called
    /// manually if early release is desired.
    LIGHTWEIGHT_API void Release();

  private:
    SqlConnection* _connection;
    std::string _lockName;
    bool _locked { false };

    void AcquireLock(std::chrono::milliseconds timeout);
    void ReleaseLock();

    void AcquireLockSqlServer(std::chrono::milliseconds timeout);
    void ReleaseLockSqlServer();

    void AcquireLockPostgreSQL(std::chrono::milliseconds timeout);
    void ReleaseLockPostgreSQL();

    void AcquireLockSQLite(std::chrono::milliseconds timeout);
    void ReleaseLockSQLite();
};

} // namespace Lightweight::SqlMigration
