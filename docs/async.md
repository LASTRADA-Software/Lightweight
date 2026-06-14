# Asynchronous API (C++23 coroutines)

Lightweight exposes a C++23 coroutine API so applications can drive the database without
blocking their main thread — both single-threaded (one app/event-loop thread) and
multi-threaded.

The async entry points are added **directly to the classes you already use** (`SqlConnection`,
`DataMapper`, `Pool`), suffixed with `Async`; only `AsyncSqlTransaction` is a separate type.
Queries go through the same fluent builder as the synchronous API — `QueryAsync<Record>()` returns
a builder whose finishers yield a `Task` (see [Querying asynchronously](#querying-asynchronously)).
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

## Querying asynchronously

Asynchronous queries use the **same fluent query builder** as synchronous ones — you just start the
chain with `QueryAsync<Record>()` instead of `Query<Record>()`. Every finisher (`All()`, `First()`,
`First(n)`, `Range()`, `Count()`, `Exist()`, `Delete()`, plus the field-projection variants such as
`All<&User::name>()`) then returns a `Task` of its usual result instead of the result itself:

```cpp
Async::Task<void> Handle(DataMapper& dm)
{
    // Whole rows
    auto everyone = co_await dm.QueryAsync<User>().All();                       // Task<std::vector<User>>

    // Refine with the normal DSL, then pick a finisher
    auto active = co_await dm.QueryAsync<User>()
                            .Where(FieldNameOf<&User::is_active>, "=", true)
                            .OrderBy(FieldNameOf<&User::name>, SqlResultOrdering::ASCENDING)
                            .First(10);                                         // Task<std::vector<User>>

    // Single record by primary key
    auto loaded = co_await dm.QueryAsync<User>()
                            .Where(FieldNameOf<&User::id>, "=", userId)
                            .First();                                          // Task<std::optional<User>>

    auto total = co_await dm.QueryAsync<User>().Count();                        // Task<std::size_t>
}
```

`Query<Record>()` (synchronous) and `QueryAsync<Record>()` (asynchronous) are otherwise identical:
the only difference is the return type of the finisher. There is intentionally **no** `AllAsync()`
next to `All()`.

## Record-level async methods

Operations that act on a record directly — and have no query-builder form — keep a dedicated
`…Async` counterpart returning a `Task`:

```cpp
Async::Task<void> Handle(DataMapper& dm)
{
    auto user = User { .name = "Alice" };
    co_await dm.CreateAsync(user);   // INSERT off-thread, fills the primary key

    auto loaded = co_await dm.QuerySingleAsync<User>(user.id.Value()); // by-primary-key shorthand
    user.name = "Alicia";
    co_await dm.UpdateAsync(user);   // UPDATE off-thread

    co_await dm.LoadRelationsAsync(user);
    co_await dm.DeleteAsync(user);
}
```

Available: `CreateAsync`, `QuerySingleAsync`, `UpdateAsync`, `DeleteAsync`, `LoadRelationsAsync`
(plus the `QueryAsync<Record>()` builder above). `QuerySingleAsync<Record>(keys…)` is the async
shorthand for a primary-key lookup; for any other query use the builder.

> **Lifetime:** the record-level methods capture the record by reference, and the builder finishers
> capture the builder by reference. Keep the operand alive across the `co_await` — the idiomatic way
> is to keep the whole expression in the `co_await` (e.g. `co_await dm.QueryAsync<User>()…All();`),
> and not to touch a referenced record between the call and the `co_await`.

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

// Back-pressure (suspend-on-exhaustion) requires the BoundedWait strategy; the default
// DataMapperPool uses BoundedOverflow, which never suspends (see the note below).
using BoundedPool = Pool<PoolConfig { .initialSize = 4, .maxSize = 8,
                                      .growthStrategy = GrowthStrategy::BoundedWait }>;
BoundedPool pool;

Async::Task<std::optional<User>> Fetch(BoundedPool& pool, SqlGuid id)
{
    auto dm = co_await pool.AcquireAsync(dbWorkers, dbWorkers); // suspends when at capacity, never blocks
    co_return co_await dm->QueryAsync<User>().Where(FieldNameOf<&User::id>, "=", id).First();
    // dm returns to the pool when the coroutine ends
}
```

`AcquireAsync` never blocks the calling thread; its back-pressure behavior depends on the pool's
growth strategy:

- **`BoundedWait`** — when the pool is at `maxSize`, `AcquireAsync` *suspends* the coroutine and
  resumes it once another caller returns a connection. No thread is parked.
- **`BoundedOverflow`** (the **default** `DataMapperPool`) and **`UnboundedGrow`** — never suspend:
  if no idle connection is available a fresh one is created (matching the synchronous `Acquire`).
  Under load this can open an unbounded number of connections, so choose `BoundedWait` when you
  need back-pressure.

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

The async API is a first-class part of the library — always built, no configuration required.
