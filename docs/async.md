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

> Every async finisher returns a **lazy** `Task` that does nothing until it is `co_await`-ed — including
> side-effecting ones like `Delete()`. Discarding the returned `Task` (e.g. `dm.QueryAsync<User>()…
> .Delete();` without `co_await`) performs **no** work; the `[[nodiscard]]` attribute warns about it.

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

> **Lifetime (important):** the record-level methods capture the record **by reference**, and the
> builder finishers capture the builder **by reference**. The captured operand is dereferenced on a
> worker thread when the `Task` is awaited — so it must stay alive, and must not be moved or mutated,
> for the **whole duration of the `co_await`** (until the coroutine resumes), not merely until the
> call returns. Awaiting a builder temporary kept in a local
> (`auto t = dm.QueryAsync<User>()…All(); co_await std::move(t);`) destroys the builder too early and
> is a **use-after-free**. The idiomatic, safe form keeps the whole expression inside the `co_await`
> (e.g. `co_await dm.QueryAsync<User>()…All();`).

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

// Configure the pool's executors once. The two arguments are the offload executor (where blocking
// ODBC runs) and the resume scheduler (where coroutines continue) — here both are the worker pool,
// so coroutines resume on a pool thread.
pool.SetAsyncExecutors(dbWorkers, dbWorkers);

Async::Task<std::optional<User>> Fetch(BoundedPool& pool, SqlGuid id)
{
    auto dm = co_await pool.AcquireAsync(); // suspends when at capacity, never blocks
    co_return co_await dm->QueryAsync<User>().Where(FieldNameOf<&User::id>, "=", id).First();
    // dm returns to the pool when the coroutine ends
}
```

If you prefer to pass the executors per call (or to override the configured ones for a single
acquire), the explicit overload `pool.AcquireAsync(offload, resume)` remains available.

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

Always `co_await CommitAsync()` / `RollbackAsync()` explicitly. `CommitAsync` / `RollbackAsync` are a
point of no return and are **not cancellable** (they take no token) — they always run to completion, so
a rollback can never silently degrade into a commit. If the transaction is still open when destroyed,
the destructor finalizes it best-effort (per the configured mode — `COMMIT` by default, matching the
synchronous `SqlTransaction`), routed through the connection's strand and blocking the destroying
thread, so it never races an in-flight async op on the same connection. The connection must stay alive
and async-enabled for the transaction's whole lifetime — do not destroy it or return the owning pooled
`DataMapper` (which disables async) between `BeginAsync` and the finalizing call. A second `BeginAsync`
without an intervening commit/rollback is a programmer error (`std::logic_error`).

> **Destruction blocks on the strand.** Because the open-at-destruction finalization is routed through
> the connection's strand and blocks the destroying thread until it runs, do not let an open
> `AsyncSqlTransaction` be destroyed on a thread the strand needs in order to make progress — e.g. from
> inside another offloaded step on the same connection, or (in the multi-threaded model where the
> resume scheduler *is* the worker pool backing the strand) while resuming on one of those workers. With
> a single-worker pool that self-waits and deadlocks. Prefer explicit `CommitAsync`/`RollbackAsync` so
> the destructor never has to finalize.

## Cancellation

Async-runtime calls (e.g. `BeginAsync`, the offloaded query/record methods) accept an optional
`std::stop_token`. A default-constructed token is non-cancellable, so omit it when you do not need
cancellation. To cancel, hold a `std::stop_source` and pass its token:

```cpp
std::stop_source source;
auto token = source.get_token();
// ... pass token where supported; source.request_stop() to cancel.
```

Cancellation is honored **only before a step is dispatched**: if the token is already requested when the
step is about to run, it completes immediately with `Async::OperationCancelledError` and never occupies
a DB worker. Once a step has begun running, the in-flight blocking ODBC call is **not** interrupted
(there is no `SQLCancel` integration yet), so a late request only affects the next not-yet-dispatched
step. Transaction finalization (`CommitAsync`/`RollbackAsync`) is deliberately exempt and never cancels.

## Errors

Exceptions thrown by the underlying synchronous call (e.g. `SqlException`) are captured on the
worker and **re-thrown at the `co_await` site on the resume thread** — exactly as the synchronous
API would throw.

## Integrating with an external event loop / coroutine runtime

Lightweight's async layer coexists with another coroutine runtime — a GUI framework, an HTTP
server, your own event loop — in the **same executable** without any special arrangement.
`Async::Task` and any other library's task type are distinct, namespaced types, and C++ coroutine
state is per-promise-type, so there is nothing global to clash and no linker conflict. The thing
that actually matters when two runtimes share a process is **thread affinity**: you usually want
Lightweight's database coroutines to resume on *your* loop's thread rather than spin up a second
loop.

That seam is already abstracted behind two small, public, dependency-injected interfaces in
`<Lightweight/Async/Executor.hpp>`:

- `Async::IExecutor` — *where blocking ODBC work runs* (the offload target).
- `Async::IResumeScheduler` — *where a coroutine resumes* after a blocking step completes.

The library only ever holds references to these; it never owns a loop. To resume Lightweight
coroutines on a host event loop, implement `IResumeScheduler` as a thin adapter that posts the
resume onto that loop:

```cpp
// Adapter: resume Lightweight coroutines on the host GUI/event loop.
struct HostLoopScheduler final: Lightweight::Async::IResumeScheduler
{
    HostEventLoop& loop;
    void Resume(std::coroutine_handle<> handle) override
    {
        loop.Post([handle] { handle.resume(); }); // hop onto the host loop's thread
    }
};

HostLoopScheduler resumeOnHostLoop { myLoop };
connection.EnableAsync(dbWorkerPool, resumeOnHostLoop);
// or, for a pool:  pool.SetAsyncExecutors(dbWorkerPool, resumeOnHostLoop);
```

Now database coroutines resume on the host thread, the host keeps owning its loop, and no second
pump is needed. If the host already has a worker pool, it can additionally implement `IExecutor`
and be passed as the offload target, so the whole process shares a single pool. Because
`Async::Task` is a plain awaitable, a coroutine in the other runtime can `co_await` a Lightweight
task directly; route its continuation through the same adapter to keep it on the host thread.

## Build

The async API is a first-class part of the library — always built, no configuration required.

The executor internals run on [NVIDIA stdexec](https://github.com/NVIDIA/stdexec), the reference
implementation of `std::execution` (P2300 — the C++26 executors). `ThreadPoolExecutor` is an
`exec::static_thread_pool` with each posted item spawned into an `exec::async_scope` (so its
destructor drains every in-flight item before joining); `StrandExecutor` serializes work over that
pool. stdexec is resolved automatically: `find_package(stdexec)` first (any sufficiently new
system/vcpkg install — the build checks `stdexec_VERSION` against `LIGHTWEIGHT_STDEXEC_MIN_VERSION`,
≥ 0.9.0), falling back to CPM (`NVIDIA/stdexec`, tag `nvhpc-26.05`; the tag and minimum version live
in `LIGHTWEIGHT_STDEXEC_GIT_TAG` / `LIGHTWEIGHT_STDEXEC_MIN_VERSION`). The stdexec headers are
confined to a single translation unit behind a pimpl, so including Lightweight's headers never pulls
stdexec in — and an installed Lightweight links it `PRIVATE`/build-only, so stdexec is not part of
the exported interface at all.

### Interop with `std::execution`

`Async::Task<T>` is a plain coroutine awaitable. You can `co_await` it directly from another
coroutine, drive it with `Lightweight::Async::SyncWait(task)`, or — to flow it into an stdexec
sender pipeline — adapt it with `Async::AsSender(task)`:

```cpp
#include <Lightweight/Async/Sender.hpp>   // opt-in; pulls in <stdexec/execution.hpp>
#include <stdexec/execution.hpp>

// Any Task<T> becomes a sender; compose it with then / let_value / when_all / sync_wait:
auto user = stdexec::sync_wait(
    Async::AsSender(dm.QueryAsync<User>().Where(FieldNameOf<&User::id>, "=", id).First())
    | stdexec::then([](std::optional<User> u) { return u.value_or(User {}); }));
```

`AsSender` maps the Task's outcome onto stdexec's completion channels: a produced value completes
`set_value`, a thrown `OperationCancelledError` completes `set_stopped` (matching the cancellation
contract above), and any other exception completes `set_error` carrying a `std::exception_ptr`. The
Task stays lazy — nothing runs until the resulting sender is started (e.g. by `sync_wait` or an
enclosing sender). Because the sender advertises a stopped channel, `stdexec::sync_wait` returns a
`std::optional` that is disengaged only on cancellation.

`Async::SyncWait(task)` and `stdexec::sync_wait(sender)` remain distinct entry points — use the
former on a bare `Task`, the latter on a sender (including one produced by `AsSender`).

> **Lifetime in sender pipelines.** The record-/builder-lifetime rule above still applies: the
> operand a record-level async method (e.g. `UpdateAsync`) captures must outlive the whole
> `co_await`. When chaining with `let_value`, mutate the stage's argument *in place* (it is kept
> alive for the child sender) and pass a reference to it, rather than a local that dies when the
> `let_value` callback returns. See `src/examples/todo/main.cpp` for a worked example.

stdexec is a *build-time* dependency of Lightweight, not part of its installed/exported interface, and
`Async/Sender.hpp` is the one public header that includes the stdexec headers — it is intentionally
**not** included by `<Lightweight/Lightweight.hpp>` or the C++20 module. To use senders (or this
bridge) in your own code, depend on stdexec directly (`find_package(stdexec)` / link
`STDEXEC::stdexec`) — do not rely on Lightweight to provide its headers transitively.
