# Connection pooling

`Pool` hands out `DataMapper` instances backed by reusable ODBC connections. Its policy is a
`PoolConfig` supplied as a template parameter, so sizing, growth and health behaviour are fixed at
the type level rather than read from runtime configuration.

```cpp
using namespace Lightweight;

constexpr auto MyPoolConfig = PoolConfig {
    .initialSize = 4,
    .maxSize = 16,
    .growthStrategy = GrowthStrategy::BoundedWait,
};

auto pool = Pool<MyPoolConfig> {};

{
    auto mapper = pool.Acquire();          // returned to the pool when `mapper` goes out of scope
    auto users = mapper->Query<User>().All();
}
```

`DataMapperPool` is the pre-configured alias used by `GlobalDataMapperPool()`; its defaults come from
the `LIGHTWEIGHT_POOL_*` CMake options listed at the bottom of this page.

## Growth strategies

| Strategy | When no connection is idle | On return |
|---|---|---|
| `BoundedWait` | Creates one while below `maxSize`, otherwise **blocks** until one is returned | Handed to the longest-waiting acquirer, else idled |
| `BoundedOverflow` | Always creates a fresh one, ignoring `maxSize` | Kept while fewer than `maxSize` are idle, else closed |
| `UnboundedGrow` | Always creates a fresh one | Always kept |

`BoundedWait` is the only strategy that can make a caller wait, and therefore the only one for which
the acquire timeout below can expire.

## Acquire timeout

`Acquire()` on a `BoundedWait` pool waits indefinitely. If every connection is checked out and one is
never returned — a caller holding a mapper across a long operation, a deadlock, a burst that outruns
`maxSize` — the calling thread parks with no diagnostic and no way out.

Pass a timeout to bound that wait:

```cpp
if (auto mapper = pool.Acquire(std::chrono::milliseconds { 250 }))
    Handle(mapper->Query<User>().All());
else
    // mapper.error() == PoolError::Timeout
    ReportOverload();
```

The result is a `std::expected<PooledDataMapper, PoolError>`, so it composes with the monadic style
used elsewhere in the library. A timed-out acquirer removes itself from the pool's waiter queue, so a
connection returned afterwards goes to the next real waiter rather than being lost.

The overload also exists on `BoundedOverflow` and `UnboundedGrow` pools, where it always succeeds —
those strategies create a connection rather than wait. That lets generic code take a timeout without
knowing which strategy it was instantiated with.

`AcquireAsync` has no timeout overload. It suspends a coroutine rather than occupying a thread, so
the failure this guards against does not arise in the same way.

## Keeping pooled connections healthy

A pooled connection is a long-lived socket, and plenty of things outside the process can invalidate
one while it sits idle: a firewall or NAT dropping the flow, a server restart, a failover, an
administrative disconnect. Three independent controls decide whether a connection is still fit to
hand out.

### Validation on borrow

`validateOnBorrow` (**enabled by default**) checks `SqlConnection::IsAlive()` before a connection
leaves the pool. A connection reported dead is discarded and the caller is transparently served from
the next idle connection or a fresh one, instead of receiving a broken connection and a driver error
on its first statement.

The check reads the driver-local `SQL_ATTR_CONNECTION_DEAD` attribute, so it costs no round trip to
the server. That also bounds what it can detect: several drivers only mark a connection dead once an
operation has already failed, so a connection whose peer vanished silently — a firewall dropping the
flow without sending `FIN` or `RST`, leaving a half-open socket — can still pass. Set
`validateOnBorrow` to `ValidateOnBorrow::No` if you would rather handle failures at the call site
than pay even that cost.

### `maxIdleTimeMs`

Retires a connection that has sat idle longer than the bound, rather than handing it out.

Set this below any idle timeout on the network path or the server, and the connection is never idle
long enough to be reaped behind the pool's back. That removes the failure mode entirely, which is
strictly stronger than detecting it afterwards — and it covers exactly the half-open case that
validation cannot see.

### `maxLifetimeMs`

Retires a connection older than the bound, measured from when it was created — total age, not time
since last use.

This one is not about brokenness. After a failover or a rolling restart, a pooled connection stays
bound to the old node and reports itself perfectly alive; nothing else in the pool will ever move it.
A lifetime bound is what eventually rebalances the pool. Setting it shorter than any connection-age
ceiling imposed by the database or the infrastructure also means connections are retired while idle,
which costs nothing, rather than being cut mid-query at a moment not of your choosing. Long-lived
sessions also accumulate server-side state — temporary objects, cached plans, per-session memory —
that recycling bounds.

Both bounds are expressed in milliseconds and **disabled by default** (`0`). They are plain integer
counts rather than `std::chrono` durations because `PoolConfig` is a non-type template parameter and
`std::chrono::duration` is not a structural type; `PoolConfig::MaxIdleTime()` and
`PoolConfig::MaxLifetime()` read them back as durations.

```cpp
constexpr auto ResilientPoolConfig = PoolConfig {
    .initialSize = 4,
    .maxSize = 16,
    .growthStrategy = GrowthStrategy::BoundedWait,
    .validateOnBorrow = ValidateOnBorrow::Yes,
    .maxIdleTimeMs = 4 * 60 * 1000,   // stay under a 5-minute firewall idle timeout
    .maxLifetimeMs = 30 * 60 * 1000,  // rebalance across nodes every half hour
};
```

### Retirement is lazy

The pool has no background thread. Retirement happens when the pool is next used: expired connections
are dropped as they are taken from or placed into the idle set. A pool that goes completely idle
therefore keeps its sockets open until the next `Acquire`.

This does not affect correctness — an expired connection is discarded rather than handed out, however
long it has been sitting — but it does mean the pool is not a mechanism for releasing connections
during quiet periods.

One case deliberately bypasses both the bounds and the validation check: on a `BoundedWait` pool with
callers already waiting, a returned connection goes straight to the longest-waiting one. Retiring it
there would strand a waiter that can only be woken by a hand-off, and building a replacement inside
that path would mean a connection attempt that can fail on a code path that must not. Such a
connection was in active use moments earlier, and it is checked normally the next time it comes out
of the idle set.

## Compile-time defaults

`DataMapperPool` and `GlobalDataMapperPool()` are configured through CMake:

| Option | Default | Meaning |
|---|---|---|
| `LIGHTWEIGHT_POOL_INITIAL_SIZE` | `4` | Connections pre-created at construction |
| `LIGHTWEIGHT_POOL_MAX_SIZE` | `16` | Upper bound for the `Bounded*` strategies |
| `LIGHTWEIGHT_POOL_GROWTH_STRATEGY` | `BoundedOverflow` | `BoundedWait`, `BoundedOverflow` or `UnboundedGrow` |
| `LIGHTWEIGHT_POOL_VALIDATE_ON_BORROW` | `Yes` | `Yes` or `No` |
| `LIGHTWEIGHT_POOL_MAX_IDLE_TIME_MS` | `0` (disabled) | Idle bound, in milliseconds |
| `LIGHTWEIGHT_POOL_MAX_LIFETIME_MS` | `0` (disabled) | Lifetime bound, in milliseconds |

The lifetime bounds default to disabled because a default recycle window would silently change the
behaviour of every existing deployment, and the right value depends on the infrastructure the
connections traverse.

## See also

- [async.md](async.md) — `Pool::AcquireAsync`, and which strategies can suspend a coroutine
- [best-practices.md](best-practices.md) — when to pool versus when to hold a connection
