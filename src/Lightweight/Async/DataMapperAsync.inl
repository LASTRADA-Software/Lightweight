// SPDX-License-Identifier: Apache-2.0
//
// Out-of-line definitions for DataMapper's asynchronous (coroutine) methods.
// This file is included at the end of DataMapper/DataMapper.hpp when LIGHTWEIGHT_ENABLE_ASYNC is on.
//
// Each method offloads the existing synchronous DataMapper method to the connection's async backend
// (a worker thread, serialized per connection via a strand) and resumes the awaiting coroutine on the
// app's resume scheduler. The whole synchronous operation runs as one closure on the strand, so the
// ODBC connection is only ever touched by one thread at a time.

#pragma once

#include "Backend.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Lightweight
{

template <DataMapperOptions QueryOptions, typename Record>
Async::Task<RecordPrimaryKeyType<Record>> DataMapper::CreateAsync(Record& record)
{
    return Async::RunAsync(_connection.AsyncBackend(),
                           [this, &record]() -> RecordPrimaryKeyType<Record> { return Create<QueryOptions>(record); });
}

template <typename Record, DataMapperOptions QueryOptions, typename... PrimaryKeyTypes>
Async::Task<std::optional<Record>> DataMapper::QuerySingleAsync(PrimaryKeyTypes... primaryKeys)
{
    return Async::RunAsync(_connection.AsyncBackend(),
                           [this, ... primaryKeys = std::move(primaryKeys)]() mutable -> std::optional<Record> {
                               return QuerySingle<Record, QueryOptions>(std::move(primaryKeys)...);
                           });
}

template <typename Record>
Async::Task<void> DataMapper::UpdateAsync(Record& record)
{
    return Async::RunAsync(_connection.AsyncBackend(), [this, &record] { Update(record); });
}

template <typename Record>
Async::Task<std::size_t> DataMapper::DeleteAsync(Record const& record)
{
    return Async::RunAsync(_connection.AsyncBackend(), [this, &record]() -> std::size_t { return Delete(record); });
}

template <typename Record>
Async::Task<void> DataMapper::LoadRelationsAsync(Record& record)
{
    return Async::RunAsync(_connection.AsyncBackend(), [this, &record] { LoadRelations(record); });
}

} // namespace Lightweight
