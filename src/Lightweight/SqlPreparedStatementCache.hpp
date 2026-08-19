// SPDX-License-Identifier: Apache-2.0

#pragma once

// See SqlOdbcPrelude.hpp's header comment for why this replaces a direct <Windows.h> include.
#include "Api.hpp"
#include "SqlOdbcPrelude.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sql.h>
#include <sqltypes.h>

namespace Lightweight
{

/// @ingroup CoreApi
/// @brief Whether a single @c SqlStatement takes part in its connection's prepared-statement cache.
///
/// Statements opt in by default, which only has an effect once the owning connection was given a
/// non-zero cache capacity (see @c SqlConnection::SetPreparedStatementCacheCapacity). Individual
/// call sites that must not reuse a plan — for instance a statement that straddles a schema change —
/// opt out via @c SqlStatement::SetPreparedStatementCaching.
enum class SqlPreparedStatementCaching : uint8_t
{
    /// Reuse a pooled handle when one matches, and hand the handle back to the pool afterwards.
    Enabled,

    /// Never take a handle from, nor give one to, the connection's cache.
    Disabled,
};

/// @ingroup CoreApi
/// @brief A bounded LRU pool of already-prepared ODBC statement handles, owned by a @c SqlConnection.
///
/// Preparing a statement is a server round-trip on most drivers (MS SQL Server, PostgreSQL). This
/// cache keeps the @c SQLHSTMT handles of recently prepared queries alive, so re-preparing the same
/// SQL text on the same connection skips @c SQLPrepare entirely.
///
/// A handle is *checked out* while a statement uses it: @ref Acquire removes it from the pool and
/// @ref Release puts it back. Two statements preparing the same query at the same time therefore each
/// get their own handle, and both are pooled afterwards (subject to the capacity bound). Eviction is
/// least-recently-released first, which matters because several backends cap the number of live
/// prepared statements per session.
///
/// @note Not thread-safe, mirroring @c SqlConnection: one connection is used by one thread at a time.
/// @note A pooled handle holds a query plan derived from the schema as it was at preparation time. A
///       connection that runs DDL must drop those plans via
///       @c SqlConnection::ClearPreparedStatementCache — Lightweight's own migration paths do it for you.
class SqlPreparedStatementCache final
{
  public:
    /// @brief A pooled statement handle together with the parameter count the driver reported for it.
    struct PreparedHandle
    {
        /// The native ODBC statement handle, prepared for the associated query text.
        SQLHSTMT nativeHandle {};

        /// The number of input parameters @c SQLNumParams reported for that query.
        SQLSMALLINT parameterCount {};
    };

    /// @brief Cumulative counters, primarily for tests and diagnostics.
    struct Statistics
    {
        /// Prepare requests served from the pool, i.e. without a @c SQLPrepare round-trip.
        uint64_t hits {};

        /// Prepare requests that had to issue @c SQLPrepare.
        uint64_t misses {};

        /// Pooled handles freed because the capacity bound was exceeded.
        uint64_t evictions {};
    };

    /// @brief Constructs a cache with the given capacity.
    /// @param capacity Maximum number of idle prepared handles to keep; @c 0 disables the cache.
    LIGHTWEIGHT_API explicit SqlPreparedStatementCache(std::size_t capacity = 0) noexcept;

    /// Frees every pooled statement handle.
    LIGHTWEIGHT_API ~SqlPreparedStatementCache() noexcept;

    SqlPreparedStatementCache(SqlPreparedStatementCache const&) = delete;
    SqlPreparedStatementCache& operator=(SqlPreparedStatementCache const&) = delete;
    SqlPreparedStatementCache(SqlPreparedStatementCache&&) = delete;
    SqlPreparedStatementCache& operator=(SqlPreparedStatementCache&&) = delete;

    /// @return The maximum number of idle prepared handles kept (@c 0 when disabled).
    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return m_capacity;
    }

    /// @brief Sets the capacity, evicting the least recently released handles when shrinking.
    /// @param capacity Maximum number of idle prepared handles to keep; @c 0 disables and clears.
    LIGHTWEIGHT_API void SetCapacity(std::size_t capacity) noexcept;

    /// @return Whether the cache is enabled, i.e. whether its capacity is non-zero.
    [[nodiscard]] bool IsEnabled() const noexcept
    {
        return m_capacity != 0;
    }

    /// @return The number of idle prepared handles currently pooled.
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_entries.size();
    }

    /// @return The cumulative hit/miss/eviction counters.
    [[nodiscard]] Statistics const& Stats() const noexcept
    {
        return m_stats;
    }

    /// Resets the cumulative counters to zero, leaving the pooled handles untouched.
    LIGHTWEIGHT_API void ResetStatistics() noexcept;

    /// @brief Takes an idle handle prepared for @p query out of the pool.
    ///
    /// The caller owns the returned handle until it hands it back via @ref Release (or frees it).
    ///
    /// @param query The exact SQL text the handle must have been prepared with.
    /// @return The pooled handle, or @c std::nullopt when no idle handle matches.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<PreparedHandle> Acquire(std::string_view query) noexcept;

    /// @brief Hands a prepared handle back to the pool as the most recently used entry.
    ///
    /// The caller must have closed the handle's cursor and unbound its columns beforehand. Ownership
    /// of @p handle transfers to the cache; when the capacity bound is exceeded — or the cache is
    /// disabled — the surplus handle is freed right away.
    ///
    /// @param query The SQL text @p handle is prepared for.
    /// @param handle The prepared handle to pool.
    LIGHTWEIGHT_API void Release(std::string_view query, PreparedHandle handle) noexcept;

    /// Frees every pooled handle, e.g. after DDL invalidated the cached query plans.
    LIGHTWEIGHT_API void Clear() noexcept;

  private:
    /// One pooled handle plus the query text it is keyed by. Held in a list so node addresses — and
    /// therefore the @c string_view keys of @c m_index, which point into @c query — stay stable.
    struct Entry
    {
        std::string query;
        PreparedHandle handle;
    };

    using EntryList = std::list<Entry>;

    /// Drops the index entry referring to @p entry (there may be several entries per query text).
    void EraseFromIndex(EntryList::const_iterator entry) noexcept;

    /// Frees the least recently released handles until at most @c m_capacity remain.
    void EvictSurplus() noexcept;

    std::size_t m_capacity;
    EntryList m_entries;                                                    // front = most recently used
    std::unordered_multimap<std::string_view, EntryList::iterator> m_index; // query text -> entry
    Statistics m_stats {};
};

} // namespace Lightweight
