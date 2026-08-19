// SPDX-License-Identifier: Apache-2.0

#include "SqlPreparedStatementCache.hpp"

#include <iterator>
#include <ranges>
#include <string>
#include <utility>

#include <sql.h>
#include <sqlext.h>

namespace Lightweight
{

namespace
{
    void FreeHandle(SqlPreparedStatementCache::PreparedHandle const& handle) noexcept
    {
        if (handle.nativeHandle != SQL_NULL_HSTMT)
            SQLFreeHandle(SQL_HANDLE_STMT, handle.nativeHandle);
    }
} // namespace

SqlPreparedStatementCache::SqlPreparedStatementCache(std::size_t capacity) noexcept:
    m_capacity { capacity }
{
}

SqlPreparedStatementCache::~SqlPreparedStatementCache() noexcept
{
    Clear();
}

void SqlPreparedStatementCache::SetCapacity(std::size_t capacity) noexcept
{
    m_capacity = capacity;
    EvictSurplus();
}

void SqlPreparedStatementCache::ResetStatistics() noexcept
{
    m_stats = {};
}

std::optional<SqlPreparedStatementCache::PreparedHandle> SqlPreparedStatementCache::Acquire(std::string_view query) noexcept
{
    auto const indexed = m_index.find(query);
    if (indexed == m_index.end())
    {
        ++m_stats.misses;
        return std::nullopt;
    }

    auto const entry = indexed->second;
    auto const handle = entry->handle;
    m_index.erase(indexed);
    m_entries.erase(entry);
    ++m_stats.hits;
    return handle;
}

void SqlPreparedStatementCache::Release(std::string_view query, PreparedHandle handle) noexcept
{
    if (handle.nativeHandle == SQL_NULL_HSTMT)
        return;

    if (!IsEnabled())
    {
        FreeHandle(handle);
        return;
    }

    m_entries.emplace_front(Entry { .query = std::string(query), .handle = handle });
    m_index.emplace(std::string_view { m_entries.front().query }, m_entries.begin());
    EvictSurplus();
}

void SqlPreparedStatementCache::Clear() noexcept
{
    for (auto const& entry: m_entries)
        FreeHandle(entry.handle);
    m_entries.clear();
    m_index.clear();
}

void SqlPreparedStatementCache::EraseFromIndex(EntryList::const_iterator entry) noexcept
{
    auto const [first, last] = m_index.equal_range(std::string_view { entry->query });
    auto const indexed =
        std::ranges::find(first, last, entry, [](auto const& pair) { return EntryList::const_iterator { pair.second }; });
    if (indexed != last)
        m_index.erase(indexed);
}

void SqlPreparedStatementCache::EvictSurplus() noexcept
{
    while (m_entries.size() > m_capacity)
    {
        auto const victim = std::prev(m_entries.end());
        EraseFromIndex(victim);
        FreeHandle(victim->handle);
        m_entries.erase(victim);
        ++m_stats.evictions;
    }
}

} // namespace Lightweight
