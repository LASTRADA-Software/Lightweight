# Statistics and metrics

Lightweight can collect its own execution statistics — how many statements ran, how long they took,
how hard the connection pool is working — and hand them back as a plain struct you can feed to
Prometheus, StatsD, a log line, or a test assertion.

Collection is **opt-in at compile time** and **off by default**, so a normal release build carries
none of it: the instrumentation is not merely a no-op call, it is not compiled at all.

## Enabling it

```sh
cmake -B build -D LIGHTWEIGHT_ENABLE_STATISTICS=ON
```

The option is independent of [Tracy](#feeding-tracy). Turn on either, both, or neither.

`SqlStatistics::IsEnabled()` is a `constexpr bool` reporting which kind of build you are in, so
downstream code can branch at compile time:

```cpp
if constexpr (Lightweight::SqlStatistics::IsEnabled())
    PublishMetrics(Lightweight::SqlStatistics::Instance().Snapshot());
```

The types always exist. Code that reads a snapshot compiles in a build without the option — it just
reads back zeros. You never need `#ifdef` around your own metrics code.

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
than absolute counters.

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

When Lightweight is built with **both** `LIGHTWEIGHT_ENABLE_STATISTICS=ON` and
`LIGHTWEIGHT_ENABLE_TRACY=ON`, every recorded sample is additionally emitted as a Tracy plot:

| Plot | Source |
|------|--------|
| `Sql.Execute.us`, `Sql.ExecuteDirect.us`, `Sql.ExecuteBatch.us`, `Sql.Prepare.us`, `Sql.PoolAcquire.us` | per-operation latency |
| `Sql.Pool.Idle`, `Sql.Pool.CheckedOut` | live pool occupancy |

```sh
cmake -B build -D LIGHTWEIGHT_ENABLE_STATISTICS=ON -D LIGHTWEIGHT_ENABLE_TRACY=ON
```

Connect the Tracy GUI (or capture headlessly with `tracy-capture`) and the plots appear alongside
the `ZoneScoped` regions the library already emits. Tracy's own Statistics window additionally gives
you per-zone min/max/mean/median and a histogram, so the two views complement each other: Tracy for
interactive profiling, `Snapshot()` for production export.

CI exercises exactly this combination — the `tracy_capture` job builds the Chinook example with both
options on, captures a trace headlessly, and asserts the expected zones and plots are present.

## Cost

With the option **off**, the macros expand to nothing; there is no call, no branch, and no object.

With it **on**, an instrumented operation costs one `steady_clock::now()` pair plus a handful of
relaxed atomic increments — negligible next to an ODBC round-trip, but not free, which is why it is
opt-in rather than always compiled.

## See also

- [Logging and tracing](logging.md) — the `SqlLogger` hook interface, which statistics deliberately
  does *not* go through: collection is compiled out entirely rather than dispatched to a no-op logger.
- [Best practices](best-practices.md)
