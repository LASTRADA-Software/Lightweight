// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlConnection.hpp"
#include "Common.hpp"    // ConnectWithRetry
#include "SqlBackup.hpp" // ProgressManager, RetrySettings

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace Lightweight::SqlBackup::detail
{

/// A runtime-sized, thread-safe pool of SqlConnection instances for backup/restore workers.
///
/// BoundedWait semantics: Acquire() blocks until a connection is free. All connections are created
/// up front (sequentially, via ConnectWithRetry) to avoid concurrent-connect driver races and to
/// pay connection setup cost once.
class ConnectionPool
{
  public:
    /// RAII lease: returns its connection to the pool on destruction. Move-only.
    class Lease
    {
      public:
        /// Move constructor; leaves @p other empty.
        LIGHTWEIGHT_API Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&&) = delete;
        Lease(Lease const&) = delete;
        Lease& operator=(Lease const&) = delete;
        LIGHTWEIGHT_API ~Lease();

        /// Access the leased connection.
        [[nodiscard]] SqlConnection& Get() const noexcept
        {
            return *_conn;
        }

      private:
        friend class ConnectionPool;
        Lease(ConnectionPool& pool, std::unique_ptr<SqlConnection> conn) noexcept;
        ConnectionPool* _pool;
        std::unique_ptr<SqlConnection> _conn;
    };

    /// Pre-creates @p size connections to @p connectionString (sequentially, via ConnectWithRetry).
    /// @param connectionString The connection string each pooled connection connects to.
    /// @param size The number of connections to pre-create.
    /// @param retrySettings Retry configuration for transient connection errors.
    /// @param progress Progress manager for reporting connection/retry status.
    /// @throws std::runtime_error if any connection cannot be established.
    LIGHTWEIGHT_API ConnectionPool(SqlConnectionString const& connectionString,
                                   unsigned size,
                                   RetrySettings const& retrySettings,
                                   ProgressManager& progress);

    ConnectionPool(ConnectionPool const&) = delete;
    ConnectionPool& operator=(ConnectionPool const&) = delete;
    ConnectionPool(ConnectionPool&&) = delete;
    ConnectionPool& operator=(ConnectionPool&&) = delete;
    ~ConnectionPool() = default;

    /// Borrows a connection, blocking until one is free. Returned via the Lease's destructor.
    /// @return An RAII lease owning one pooled connection.
    [[nodiscard]] LIGHTWEIGHT_API Lease Acquire();

  private:
    void Return(std::unique_ptr<SqlConnection> conn) noexcept;

    std::mutex _mutex;
    std::condition_variable _cv;
    std::vector<std::unique_ptr<SqlConnection>> _idle;
};

} // namespace Lightweight::SqlBackup::detail
