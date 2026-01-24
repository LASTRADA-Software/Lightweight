// SPDX-License-Identifier: Apache-2.0

#include "SqlMigrationLock.hpp"
#include "SqlStatement.hpp"

#include <format>
#include <stdexcept>

namespace Lightweight::SqlMigration
{

MigrationLock::MigrationLock(SqlConnection& connection, std::string_view lockName, std::chrono::milliseconds timeout):
    _connection(&connection),
    _lockName(lockName)
{
    AcquireLock(timeout);
}

MigrationLock::~MigrationLock()
{
    if (_locked)
    {
        try
        {
            ReleaseLock();
        }
        catch (...)
        {
            // Suppress exceptions in destructor
        }
    }
}

MigrationLock::MigrationLock(MigrationLock&& other) noexcept:
    _connection(other._connection),
    _lockName(std::move(other._lockName)),
    _locked(other._locked)
{
    other._connection = nullptr;
    other._locked = false;
}

MigrationLock& MigrationLock::operator=(MigrationLock&& other) noexcept
{
    if (this != &other)
    {
        if (_locked)
        {
            try
            {
                ReleaseLock();
            }
            catch (...)
            {
            }
        }

        _connection = other._connection;
        _lockName = std::move(other._lockName);
        _locked = other._locked;

        other._connection = nullptr;
        other._locked = false;
    }
    return *this;
}

void MigrationLock::Release()
{
    if (_locked)
    {
        ReleaseLock();
        _locked = false;
    }
}

void MigrationLock::AcquireLock(std::chrono::milliseconds timeout)
{
    if (!_connection)
        throw std::runtime_error("MigrationLock: No connection provided");

    switch (_connection->ServerType())
    {
        case SqlServerType::MICROSOFT_SQL:
            AcquireLockSqlServer(timeout);
            break;
        case SqlServerType::POSTGRESQL:
            AcquireLockPostgreSQL(timeout);
            break;
        case SqlServerType::SQLITE:
            AcquireLockSQLite(timeout);
            break;
        default:
            throw std::runtime_error(std::format("MigrationLock: Unsupported database type for locking"));
    }
}

void MigrationLock::ReleaseLock()
{
    if (!_connection)
        return;

    switch (_connection->ServerType())
    {
        case SqlServerType::MICROSOFT_SQL:
            ReleaseLockSqlServer();
            break;
        case SqlServerType::POSTGRESQL:
            ReleaseLockPostgreSQL();
            break;
        case SqlServerType::SQLITE:
            ReleaseLockSQLite();
            break;
        default:
            break;
    }
}

// SQL Server implementation using sp_getapplock / sp_releaseapplock
void MigrationLock::AcquireLockSqlServer(std::chrono::milliseconds timeout)
{
    auto stmt = SqlStatement { *_connection };

    // sp_getapplock returns:
    //  0  = Lock was granted synchronously
    //  1  = Lock was granted after waiting for other incompatible locks to be released
    // -1  = Lock request timed out
    // -2  = Lock request was canceled
    // -3  = Lock request was chosen as a deadlock victim
    // -999 = Parameter validation or other call error
    auto const sql = std::format("DECLARE @result INT; "
                                 "EXEC @result = sp_getapplock @Resource = N'{}', @LockMode = N'Exclusive', "
                                 "@LockTimeout = {}, @LockOwner = N'Session'; "
                                 "SELECT @result;",
                                 _lockName,
                                 timeout.count());

    stmt.ExecuteDirect(sql);

    if (stmt.FetchRow())
    {
        auto const result = stmt.GetColumn<int>(1);
        if (result >= 0)
        {
            _locked = true;
        }
        else if (result == -1)
        {
            throw std::runtime_error(
                std::format("MigrationLock: Timeout acquiring lock '{}' after {} ms", _lockName, timeout.count()));
        }
        else
        {
            throw std::runtime_error(
                std::format("MigrationLock: Failed to acquire lock '{}', error code: {}", _lockName, result));
        }
    }
}

void MigrationLock::ReleaseLockSqlServer()
{
    auto stmt = SqlStatement { *_connection };
    auto const sql = std::format("EXEC sp_releaseapplock @Resource = N'{}', @LockOwner = N'Session';", _lockName);
    stmt.ExecuteDirect(sql);
}

// PostgreSQL implementation using pg_advisory_lock
void MigrationLock::AcquireLockPostgreSQL(std::chrono::milliseconds timeout)
{
    auto stmt = SqlStatement { *_connection };

    // Set lock timeout
    stmt.ExecuteDirect(std::format("SET lock_timeout = '{} ms';", timeout.count()));

    // Use pg_try_advisory_lock with a hash of the lock name
    // pg_advisory_lock uses a 64-bit key, we hash the lock name
    auto const sql = std::format("SELECT pg_advisory_lock(hashtext('{}')::bigint);", _lockName);

    try
    {
        stmt.ExecuteDirect(sql);
        _locked = true;
    }
    catch (SqlException const&)
    {
        throw std::runtime_error(
            std::format("MigrationLock: Failed to acquire lock '{}' within {} ms", _lockName, timeout.count()));
    }
}

void MigrationLock::ReleaseLockPostgreSQL()
{
    auto stmt = SqlStatement { *_connection };
    auto const sql = std::format("SELECT pg_advisory_unlock(hashtext('{}')::bigint);", _lockName);
    stmt.ExecuteDirect(sql);
}

// SQLite implementation using busy_timeout and immediate transaction
void MigrationLock::AcquireLockSQLite(std::chrono::milliseconds timeout)
{
    auto stmt = SqlStatement { *_connection };

    // Set busy timeout
    stmt.ExecuteDirect(std::format("PRAGMA busy_timeout = {};", timeout.count()));

    // BEGIN IMMEDIATE acquires a write lock immediately, blocking others
    try
    {
        stmt.ExecuteDirect("BEGIN IMMEDIATE;");
        _locked = true;
    }
    catch (SqlException const&)
    {
        throw std::runtime_error(
            std::format("MigrationLock: Database is locked, could not acquire lock within {} ms", timeout.count()));
    }
}

void MigrationLock::ReleaseLockSQLite()
{
    // For SQLite, we need to commit or rollback the transaction
    // Since we're just using it for locking, we'll commit (no changes made in lock scope)
    auto stmt = SqlStatement { *_connection };
    try
    {
        stmt.ExecuteDirect("COMMIT;");
    }
    catch (SqlException const&)
    {
        // If commit fails, try rollback
        try
        {
            stmt.ExecuteDirect("ROLLBACK;");
        }
        catch (...)
        {
        }
    }
}

} // namespace Lightweight::SqlMigration
