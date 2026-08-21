# Statistics and metrics

Lightweight can collect its own execution statistics — how many statements ran, how long they took,
how hard the connection pool is working — and hand them back as a plain struct you can feed to
Prometheus, StatsD, a log line, or a test assertion.

Collection is always compiled in and toggled **at runtime**, **off by default** — so a process that
never turns it on pays only one relaxed atomic-bool check per instrumented call site.

## Enabling it

```cpp
#include <Lightweight/SqlStatistics.hpp>

Lightweight::SqlStatistics::Enable();
// ... run your workload ...
auto const stats = Lightweight::SqlStatistics::Instance().Snapshot();

Lightweight::SqlStatistics::Disable(); // stop collecting again; counters stay put
```

`Enable()` / `Disable()` are static, process-wide, and safe to call from any thread at any point in
the process lifetime — start collecting when a diagnostic window opens, stop when it closes, with no
recompile or restart. Toggling never clears counters: `Disable()` followed later by `Enable()` picks
up exactly where collection left off. Call [`Reset()`](#reading-a-snapshot) explicitly when you want a
clean baseline.

`SqlStatistics::IsEnabled()` reports the current runtime state:

```cpp
if (Lightweight::SqlStatistics::IsEnabled())
    PublishMetrics(Lightweight::SqlStatistics::Instance().Snapshot());
```

The types always exist regardless of whether collection has ever been turned on — code that reads a
snapshot compiles and runs unconditionally, reading back zeros until `Enable()` is called.

## Reading a snapshot

```cpp
#include <Lightweight/SqlStatistics.hpp>

using namespace Lightweight;

auto const stats = SqlStatistics::Instance().Snapshot();

auto const& execute = stats[SqlStatisticsOperation::Execute];
std::println("executes:  {} ok, {} failed", execute.succeeded, execute.failed);
std::println("latency:   avg {:.1f}us, p50 {}us, p99 {}us",
             execute.latency.AverageMicroseconds(),
             execute.latency.PercentileMicroseconds(0.50),
             execute.latency.PercentileMicroseconds(0.99));

std::println("pool:      {} acquired, {:.0f}% reused, {} waited",
             stats.pool.acquired, stats.pool.ReuseRate() * 100.0, stats.pool.waited);
std::println("rows:      {} rows in {} block round-trips",
             stats.rowsFetched, stats.blockFetches);
```

`Snapshot()` returns [`SqlStatisticsSnapshot`](@ref Lightweight::SqlStatisticsSnapshot) — an
ordinary value type with no exporter-specific concepts baked in. Copy it, diff two of them to get a
delta, serialize it however your metrics stack wants.

`Reset()` zeroes every counter, which is useful in tests and for exporters that report deltas rather
than absolute counters. It is orthogonal to `Enable()`/`Disable()` — resetting does not change whether
collection is running, and toggling does not reset.

### What is counted

| Field | Meaning |
|-------|---------|
| `operations[Execute]` | `SQLExecute` of a prepared statement |
| `operations[ExecuteDirect]` | `SQLExecDirect` of a one-shot statement |
| `operations[ExecuteBatch]` | batch execution (array-bound or per-row) |
| `operations[Prepare]` | `SQLPrepare` |
| `operations[Fetch]` | *reserved* — the library does not time its fetch paths, so this slot stays at zero; use `rowsFetched` / `blockFetches` for row throughput |
| `operations[PoolAcquire]` | every connection acquisition from a pool |
| `pool` | acquire / release / reuse / discard counts, live occupancy, wait latency |
| `connectionsOpened` / `connectionsClosed` | connection churn |
| `rowsFetched` / `blockFetches` | rows retrieved, and how many round-trips that cost |

`rowsFetched` versus `blockFetches` is the interesting pair: on the block-prefetch path *N* rows
collapse into `ceil(N / depth)` round-trips, so a large ratio between the two is the sign that
prefetching is doing its job.

Each operation records `succeeded`, `failed`, and `retried`. Failure is inferred from an exception
leaving the timed region, so every throwing path is classified without the call site saying so.

> **Note on `retried`:** the counter and its `RecordRetry` API exist and are covered by tests, but
> the library currently has no transparent-retry path of its own, so it stays at zero unless your own
> code records into it. It will start reporting once a retry policy lands.

### Latency histograms

[`SqlLatencyHistogram`](@ref Lightweight::SqlLatencyHistogram) keeps `count`, `min`, `max`, a running
total (so `AverageMicroseconds()` is exact) and 32 power-of-two buckets in microseconds — bucket *i*
covers `[2^(i-1), 2^i)`. That spans sub-microsecond calls to ~35 minutes.

`PercentileMicroseconds(p)` reports the upper bound of the bucket the percentile falls into, clamped
to the largest sample actually observed. It is therefore an **over-estimate bounded by a factor of
two** — the standard trade-off for a power-of-two histogram, and precise enough to catch a
regression without storing every sample. `PercentileMicroseconds(1.0)` is exact, because of the
clamp.

### Consistency and thread-safety

Recording is lock-free relaxed atomics: it never blocks, never allocates, and is safe from any
thread. The price is that a snapshot is **not atomic as a whole** — two related counters can disagree
by a few samples at the edges while other threads keep recording. Treat the numbers as
monotonically-growing observations, not as a transaction. A consistent snapshot would need a lock on
the hot path, which is not a trade worth making for metrics.

## Pools

`Pool` is a class template keyed on a compile-time `PoolConfig`, so a process typically holds several
*distinct* pool types. They all record into the same process-wide collector, which gives you a
combined view. `pool.idle` and `pool.checkedOut` are last-writer-wins, so with more than one pool in
play read them as "the most recent pool transition", and rely on the monotonic counters
(`acquired`, `reused`, `waited`, `released`, `discarded`) for anything you want to trend.

`waitLatency` only takes a sample from acquisitions that genuinely blocked or parked — the fast path
would otherwise swamp the distribution with zeros. `waited` tells you how many those were.

`pool.checkedOut` is only meaningful for `GrowthStrategy::BoundedWait`, which is the only strategy
that maintains a checked-out count (it is what bounds the pool). Under `UnboundedGrow` and
`BoundedOverflow` it stays 0 — derive utilization from `acquired - released` there instead.

## Feeding Tracy

When Lightweight is built with `LIGHTWEIGHT_ENABLE_TRACY=ON` *and* statistics collection is
runtime-enabled (`SqlStatistics::Enable()`), every recorded sample is additionally emitted as a Tracy
plot:

| Plot | Source |
|------|--------|
| `Sql.Execute.us`, `Sql.ExecuteDirect.us`, `Sql.ExecuteBatch.us`, `Sql.Prepare.us`, `Sql.PoolAcquire.us` | per-operation latency |
| `Sql.Pool.Idle`, `Sql.Pool.CheckedOut` | live pool occupancy |

```sh
cmake -B build -D LIGHTWEIGHT_ENABLE_TRACY=ON
```

Statistics never depends on Tracy, in either direction: the collector works identically with Tracy
absent, and disabling collection at runtime silences the Tracy plots too — only the `ZoneScoped`
regions the library emits independently of `SqlStatistics` keep reporting.

Connect the Tracy GUI (or capture headlessly with `tracy-capture`) and the plots appear alongside
the `ZoneScoped` regions the library already emits. Tracy's own Statistics window additionally gives
you per-zone min/max/mean/median and a histogram, so the two views complement each other: Tracy for
interactive profiling, `Snapshot()` for production export.

CI exercises exactly this combination — the `tracy_capture` job builds the Chinook example with Tracy
on (the example itself calls `SqlStatistics::Enable()`), captures a trace headlessly, and asserts the
expected zones and plots are present.

## Cost

Every recording method starts with one relaxed atomic-bool load and returns immediately if collection
is disabled — no further atomics, no branching on the data. That is the entire cost for the
`LIGHTWEIGHT_STATS_ROWS`, `_CONNECTION_OPENED/_CLOSED`, `_POOL_*` macros while disabled.

`LIGHTWEIGHT_STATS_SCOPE` (backing `Execute`, `ExecuteDirect`, `ExecuteBatch`, `Prepare`) is the one
exception: its `SqlStatisticsScope` object still pays a `steady_clock::now()` and an
`uncaught_exceptions()` call at construction, because the constructor cannot know whether collection
will still be enabled by the time its destructor runs — that check happens once, in the destructor's
`RecordOperation` call. This is the honest price of a runtime toggle: those two calls are cheap
relative to an ODBC round-trip, but they are not literally zero the way a compiled-out macro would be.

While enabled, an instrumented operation additionally costs a handful of relaxed atomic increments per
sample — still negligible next to the round-trip itself.

## See also

- [Logging and tracing](logging.md) — the `SqlLogger` hook interface, which statistics deliberately
  does *not* go through: collection is compiled out entirely rather than dispatched to a no-op logger.
- [Best practices](best-practices.md)
