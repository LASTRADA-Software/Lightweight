// SPDX-License-Identifier: Apache-2.0
#include "ConnectionPool.hpp"

#include <format>
#include <ranges>
#include <stdexcept>

namespace Lightweight::SqlBackup::detail
{

ConnectionPool::Lease::Lease(ConnectionPool& pool, std::unique_ptr<SqlConnection> conn) noexcept:
    _pool { &pool },
    _conn { std::move(conn) }
{
}

ConnectionPool::Lease::Lease(Lease&& other) noexcept:
    _pool { other._pool },
    _conn { std::move(other._conn) }
{
    other._pool = nullptr;
}

ConnectionPool::Lease::~Lease()
{
    if (_conn)
        _pool->Return(std::move(_conn));
}

ConnectionPool::ConnectionPool(SqlConnectionString const& connectionString,
                               unsigned size,
                               RetrySettings const& retrySettings,
                               ProgressManager& progress)
{
    _idle.reserve(size);
    for (auto const i: std::views::iota(0U, size))
    {
        auto conn = std::make_unique<SqlConnection>(std::nullopt);
        if (!ConnectWithRetry(*conn, connectionString, retrySettings, progress, std::format("Worker {}", i + 1)))
            throw std::runtime_error(
                std::format("Failed to create pooled connection {}: {}", i + 1, conn->LastError().message));
        _idle.push_back(std::move(conn));
    }
}

ConnectionPool::Lease ConnectionPool::Acquire()
{
    std::unique_lock lock { _mutex };
    _cv.wait(lock, [this] { return !_idle.empty(); });
    auto conn = std::move(_idle.back());
    _idle.pop_back();
    return Lease { *this, std::move(conn) };
}

void ConnectionPool::Return(std::unique_ptr<SqlConnection> conn) noexcept
{
    {
        std::scoped_lock lock { _mutex };
        _idle.push_back(std::move(conn));
    }
    _cv.notify_one();
}

} // namespace Lightweight::SqlBackup::detail
