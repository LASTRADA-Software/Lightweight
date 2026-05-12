// SPDX-License-Identifier: Apache-2.0

#include "SqlLogger.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlScopedLock.hpp"

#include <format>
#include <stdexcept>
#include <utility>

namespace Lightweight
{

SqlScopedLock::SqlScopedLock(SqlConnection& connection, std::string_view lockName, std::chrono::milliseconds timeout):
    _connection(&connection),
    _lockName(lockName),
    _handler(&connection.QueryFormatter().AdvisoryLockOps())
{
    auto result = _handler->TryAcquire(*_connection, _lockName, timeout);
    if (!result)
    {
        // Throw with the handler's pre-formatted explanation so
        // diagnosis isn't lost in translation. Callers that want the
        // typed error use `TryConstruct`.
        throw std::runtime_error(result.error().message);
    }
    _locked = true;
}

SqlScopedLock::SqlScopedLock(SqlConnection& connection,
                             std::string_view lockName,
                             SqlAdvisoryLockHandler const& handler,
                             AlreadyLockedTag /*tag*/) noexcept:
    _connection(&connection),
    _lockName(lockName),
    _handler(&handler),
    _locked(true)
{
}

std::expected<SqlScopedLock, SqlLockError> SqlScopedLock::TryConstruct(SqlConnection& connection,
                                                                       std::string_view lockName,
                                                                       std::chrono::milliseconds timeout)
{
    auto const& handler = connection.QueryFormatter().AdvisoryLockOps();
    return handler.TryAcquire(connection, lockName, timeout).transform([&] {
        return SqlScopedLock { connection, lockName, handler, AlreadyLockedTag {} };
    });
}

SqlScopedLock::~SqlScopedLock()
{
    // Destructors must not propagate. Route release-time errors to the active
    // SqlLogger so they can't disappear silently — a bare `catch (...) {}`
    // would make operational regressions (e.g. a session-scope lock not
    // actually being released) invisible.
    if (_locked && _handler && _connection)
    {
        if (auto result = _handler->Release(*_connection, _lockName); !result)
        {
            try
            {
                SqlLogger::GetLogger().OnWarning(std::format("SqlScopedLock release failed: {}", result.error().message));
            }
            // NOLINTNEXTLINE(bugprone-empty-catch) — destructor must never throw.
            catch (...)
            {
            }
        }
        _locked = false;
    }
}

SqlScopedLock::SqlScopedLock(SqlScopedLock&& other) noexcept:
    _connection(other._connection),
    _lockName(std::move(other._lockName)),
    _handler(other._handler),
    _locked(other._locked)
{
    other._connection = nullptr;
    other._handler = nullptr;
    other._locked = false;
}

SqlScopedLock& SqlScopedLock::operator=(SqlScopedLock&& other) noexcept
{
    if (this != &other)
    {
        // Release any lock currently held by *this before adopting `other`'s state.
        // Mirrors the destructor's logging path so we never silently lose the lock.
        if (_locked && _handler && _connection)
        {
            if (auto result = _handler->Release(*_connection, _lockName); !result)
            {
                try
                {
                    SqlLogger::GetLogger().OnWarning(
                        std::format("SqlScopedLock release failed: {}", result.error().message));
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) — move-assign must not throw.
                catch (...)
                {
                }
            }
        }

        _connection = other._connection;
        _lockName = std::move(other._lockName);
        _handler = other._handler;
        _locked = other._locked;

        other._connection = nullptr;
        other._handler = nullptr;
        other._locked = false;
    }
    return *this;
}

std::expected<void, SqlLockError> SqlScopedLock::Release()
{
    if (!_locked || !_handler || !_connection)
        return {};

    auto result = _handler->Release(*_connection, _lockName);
    _locked = false;
    return result;
}

} // namespace Lightweight
