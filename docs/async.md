# Asynchronous API (C++23 coroutines)

Lightweight exposes a C++23 coroutine API so applications can drive the database without
blocking their main thread — both single-threaded (one app/event-loop thread) and
multi-threaded.

The async methods are added **directly to the classes you already use** (`SqlConnection`,
`DataMapper`, `Pool`), suffixed with `Async`; only `AsyncSqlTransaction` is a separate type.
The supporting runtime (`Task`, `SyncWait`, executors, backends) lives under
`<Lightweight/Async/…>`.

## Why offloading (and not "true" async ODBC)

ODBC's asynchronous execution is **not portable**: the PostgreSQL ODBC driver reports no async
support at all, the SQLite ODBC driver is synchronous, and SQL Server's event-based async is
effectively Windows-only. So Lightweight's portable model **offloads each blocking ODBC call to a
worker thread and resumes your coroutine on a scheduler you choose**. With synchronous drivers
there is no way to avoid blocking *some* thread — but it never has to be your app thread.

> A native event-based fast-path for SQL Server on Windows is planned behind the same API; until
> then every database uses the portable thread-offload backend.

## Concepts

| Type | Role |
|------|------|
| `Async::Task<T>` | A lazy coroutine result. `co_await` it, or drive it with `SyncWait`. |
| `Async::ThreadPoolExecutor` | The **DB worker pool** — blocking ODBC calls run here. |
| `Async::ManualExecutor` | An **app run-loop** you pump yourself; the resume target for the single-threaded model. |
| `Async::InlineExecutor` | Runs work inline on the calling thread (tests / degenerate configs). |
| `Async::SyncWait` / `SyncWaitPumping` | Bridge a coroutine back to non-coroutine code. |

You **inject** the executors (dependency injection); Lightweight never owns them. They must
outlive every coroutine that can resume on them.

## Enabling async on a connection

Call `EnableAsync(dbWorkers, resume)` once. `dbWorkers` is where blocking calls run; `resume` is
where your coroutine continues afterwards.

```cpp
#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>

using namespace Lightweight;

Async::ThreadPoolExecutor dbWorkers { 4 }; // 4 background DB threads
Async::ManualExecutor     appLoop;         // your app/event-loop thread pumps this

DataMapper dm;
dm.Connection().EnableAsync(dbWorkers, appLoop);
```

## DataMapper async methods

Every common operation has an `…Async` counterpart returning a `Task`:

```cpp
Async::Task<void> Handle(DataMapper& dm)
{
    auto user = User { .name = "Alice" };
    co_await dm.CreateAsync(user);                         // INSERT off-thread

    auto loaded = co_await dm.QuerySingleAsync<User>(user.id.Value());
    if (loaded)
        std::println("loaded {}", loaded->name.Value());

    loaded->name = "Alicia";
    co_await dm.UpdateAsync(*loaded);                      // UPDATE off-thread

    auto everyone = co_await dm.QueryAsync<User>("SELECT \"id\", \"name\" FROM \"User\"");
    co_await dm.DeleteAsync(*loaded);
}
```

Available: `CreateAsync`, `QuerySingleAsync`, `QueryAsync`, `UpdateAsync`, `DeleteAsync`,
`LoadRelationsAsync`.

> **Lifetime:** methods taking a record reference (`CreateAsync`, `UpdateAsync`, …) capture it by
> reference. Keep the record alive and do not touch it between the call and the `co_await`.

## Single-threaded vs multi-threaded

The same coroutine code runs in both models — only how you drive it differs.

**Single-threaded (resume on the app thread).** Drive the work by pumping your `ManualExecutor`.
Blocking ODBC runs on `dbWorkers`; your coroutine body always resumes on the pumping thread:

```cpp
auto result = Async::SyncWaitPumping(Handle(dm), appLoop); // pumps appLoop until done
```

In a real event loop you would instead call `appLoop.Run()` (or `Drain()` / `RunOne()`) from your
loop tick, and start coroutines without blocking.

**Multi-threaded.** Use a pool and let coroutines resume on worker threads (resume scheduler =
the pool). Acquire a connection without blocking via `Pool::AcquireAsync`:

```cpp
Async::ThreadPoolExecutor dbWorkers { 8 };
DataMapperPool pool; // or Pool<YourConfig>{}

Async::Task<std::optional<User>> Fetch(DataMapperPool& pool, SqlGuid id)
{
    auto dm = co_await pool.AcquireAsync(dbWorkers, dbWorkers); // suspends, never blocks
    co_return co_await dm->QuerySingleAsync<User>(id);
    // dm returns to the pool when the coroutine ends
}
```

When the pool is exhausted, `AcquireAsync` suspends the coroutine and resumes it once another
caller returns a connection — no thread is parked.

## Transactions

`AsyncSqlTransaction` is the one distinct async type (a transaction is a scoped object):

```cpp
#include <Lightweight/Async/AsyncSqlTransaction.hpp>

Async::Task<void> Transfer(DataMapper& dm)
{
    auto tx = Async::AsyncSqlTransaction { dm.Connection() };
    co_await tx.BeginAsync();
    co_await dm.UpdateAsync(from);
    co_await dm.UpdateAsync(to);
    co_await tx.CommitAsync();      // or co_await tx.RollbackAsync();
}
```

Always `co_await CommitAsync()` / `RollbackAsync()` explicitly. The destructor only performs a
best-effort *synchronous* finalization if the transaction is still open.

## Cancellation

Async-runtime calls accept an optional `Async::CancellationToken`:

```cpp
auto token = Async::CancellationToken::Create();
// ... pass token where supported; token.Request() to cancel.
```

A request before a step is dispatched completes that step with `Async::OperationCancelledError`.

## Errors

Exceptions thrown by the underlying synchronous call (e.g. `SqlException`) are captured on the
worker and **re-thrown at the `co_await` site on the resume thread** — exactly as the synchronous
API would throw.

## Build

The async API is built by default. To compile it (and its `<coroutine>` cost) out entirely,
configure with `-DLIGHTWEIGHT_ENABLE_ASYNC=OFF`.
