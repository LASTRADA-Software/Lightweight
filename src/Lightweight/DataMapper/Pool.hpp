// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Async/Executor.hpp"
#include "../Async/Task.hpp"
#include "../SqlLogger.hpp"
#include "DataMapper.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(BUILD_TESTS)
    #include <functional>
#endif

/// @defgroup ConnectionPool Connection Pooling
/// @brief A thread-safe pool of @c DataMapper instances, configured at compile time.
///
/// The growth strategy, initial size and maximum size are supplied as a @c PoolConfig
/// template parameter, so the policy is fixed at the type level rather than at runtime.

namespace Lightweight
{

/// @ingroup ConnectionPool
/// Enum to define growth strategies of the pool
///
enum class GrowthStrategy : uint8_t
{
    /// Pre-create initialSize objects.  Allow the total count
    /// to grow up to maxSize.  Once maxSize objects exist, callers BLOCK
    /// until one is returned.
    BoundedWait,

    /// Pre-create initialSize objects.  The pool stores up to maxSize
    /// objects.  If none are idle, a fresh object is ALWAYS created (no
    /// waiting).  On return: kept if the idle set is below maxSize, otherwise
    /// destroyed.
    BoundedOverflow,

    /// Pre-create initialSize objects.  Grow without limit.  Every returned
    /// object is always kept in the pool.
    UnboundedGrow,
};

/// @ingroup ConnectionPool
/// Whether the pool checks that a connection is still live before handing it to a caller.
enum class ValidateOnBorrow : uint8_t
{
    /// Hand the connection out without checking it.
    No,

    /// Check SqlConnection::IsAlive() first and discard a connection reported dead, transparently
    /// serving the caller from the next idle connection or a freshly created one.
    ///
    /// The check reads the driver-local @c SQL_ATTR_CONNECTION_DEAD attribute, so it costs no round
    /// trip to the server. By the same token it is only as good as the driver's own bookkeeping:
    /// several drivers mark a connection dead only after an operation has already failed, so a
    /// connection whose peer vanished silently (a firewall or NAT dropping the flow without sending
    /// FIN or RST) can still pass this check. Pair it with @ref PoolConfig::maxIdleTimeMs to retire
    /// such connections before they are ever handed out.
    Yes,
};

/// @ingroup ConnectionPool
/// Reason an @ref Pool::Acquire call with a timeout failed to produce a data mapper.
enum class PoolError : uint8_t
{
    /// The timeout elapsed before a data mapper became available.
    Timeout,
};

/// @ingroup ConnectionPool
/// Structure to hold the configuration of the pool, including the initial size, maximum size and growth strategy.
/// Structure is used as a template parameter for the Pool class to configure its behavior at compile time.
///
/// @note The lifetime bounds are expressed as plain millisecond counts rather than
///       @c std::chrono::milliseconds because this structure is used as a non-type template
///       parameter: @c std::chrono::duration keeps its representation private and is therefore not a
///       structural type. Use @ref PoolConfig::MaxIdleTime and @ref PoolConfig::MaxLifetime to read
///       them back as durations.
struct PoolConfig
{
    /// Initial number of data mappers to pre-create and store in the pool, must be less than or equal to maxSize
    size_t initialSize {};
    /// Maximum number of data mappers that can exist in the pool, must be greater than or equal to initialSize
    /// this is used for the  Bounded* strategies to determine when to block or when to stop accepting returned data mappers,
    /// for the UnboundedGrow strategy this is ignored
    size_t maxSize {};
    /// Strategy to determine how the pool should grow when there are no idle data mappers available, default is BoundedWait
    /// which blocks until a data mapper is returned to the pool
    GrowthStrategy growthStrategy { GrowthStrategy::BoundedWait };

    /// Whether a connection is checked for liveness before it is handed to a caller, enabled by default.
    ///
    /// @see ValidateOnBorrow
    ValidateOnBorrow validateOnBorrow { ValidateOnBorrow::Yes };

    /// Maximum time in milliseconds a connection may sit idle in the pool before it is retired
    /// instead of handed out again; 0 (the default) disables the bound.
    ///
    /// Set this below any idle timeout imposed by the network path (a firewall or NAT dropping idle
    /// flows) or by the server, so a connection is never idle long enough to be reaped behind the
    /// pool's back. This removes the failure mode that @ref ValidateOnBorrow can only detect, and
    /// then only when the driver has noticed.
    ///
    /// @note Retirement is lazy: it happens when the pool is next used, so a pool that goes
    ///       completely idle keeps its connections until the next @ref Pool::Acquire. Correctness is
    ///       unaffected — an expired connection is discarded rather than handed out — but the pool
    ///       does not shrink on its own.
    std::chrono::milliseconds::rep maxIdleTimeMs {};

    /// Maximum total age in milliseconds of a connection, counted from when it was created, after
    /// which it is retired rather than reused; 0 (the default) disables the bound.
    ///
    /// Unlike @ref validateOnBorrow, this retires connections that are perfectly alive but no longer
    /// appropriate: after a failover or a rolling restart a pooled connection stays bound to the old
    /// node, and nothing else in the pool will ever move it. Setting this shorter than any
    /// connection-age ceiling imposed by the database or the infrastructure also means connections
    /// are retired while idle, which is free, rather than being cut mid-query by something else.
    ///
    /// @note Retirement is lazy, as described for @ref maxIdleTimeMs.
    std::chrono::milliseconds::rep maxLifetimeMs {};

    /// @return @ref maxIdleTimeMs as a duration.
    [[nodiscard]] constexpr std::chrono::milliseconds MaxIdleTime() const noexcept
    {
        return std::chrono::milliseconds { maxIdleTimeMs };
    }

    /// @return @ref maxLifetimeMs as a duration.
    [[nodiscard]] constexpr std::chrono::milliseconds MaxLifetime() const noexcept
    {
        return std::chrono::milliseconds { maxLifetimeMs };
    }
};

/// @ingroup ConnectionPool
/// A thread-safe pool of DataMapper instances with the policy configured by the PoolConfig template parameter.
/// The pool allows acquiring and returning DataMapper instances, and manages the lifecycle of these instances according to
/// the specified growth strategy.
template <PoolConfig Config>
class Pool
{
  private:
    /// Clock the idle-time and lifetime bounds are measured against. Monotonic, so the bounds are
    /// immune to wall-clock adjustments.
    using Clock = std::chrono::steady_clock;

    /// True when this configuration enables at least one time-based bound, and the pool therefore
    /// has to timestamp its connections. When false, no clock is ever read.
    static constexpr bool TracksTime = Config.maxIdleTimeMs > 0 || Config.maxLifetimeMs > 0;

    /// A pooled DataMapper together with the timestamps the health bounds are evaluated against.
    ///
    /// @c createdAt travels with the mapper across checkout and return, so @ref PoolConfig::maxLifetimeMs
    /// measures the connection's total age rather than the time since it was last idled.
    /// @c idleSince is refreshed each time the mapper is stored in the idle set.
    ///
    /// An entry whose @c mapper is null is the empty result of @ref TakeIdleLocked, not a pooled entry.
    struct Entry
    {
        std::unique_ptr<DataMapper> mapper;
        Clock::time_point createdAt {};
        Clock::time_point idleSince {};
    };

  public:
    /// @ingroup ConnectionPool
    /// A wrapper around a DataMapper that returns it to the pool when destroyed
    /// can be created only from the Pool and is move-only to ensure it is always
    /// returned to the pool when it goes out of scope
    class PooledDataMapper
    {
      private:
        friend class Pool;

        explicit PooledDataMapper(Pool& pool, Entry entry) noexcept:
            _entry { std::move(entry) },
            _pool { pool }
        {
        }

      public:
        PooledDataMapper() = delete;
        PooledDataMapper(PooledDataMapper const&) = delete;

        /// Move constructor for the pooled data mapper, the only public
        /// constructor, allows moving the pooled data mapper but not copying it
        PooledDataMapper(PooledDataMapper&& other) noexcept:
            _entry { std::move(other._entry) },
            _pool { other._pool }
        {
        }
        PooledDataMapper& operator=(PooledDataMapper const&) = delete;
        PooledDataMapper& operator=(PooledDataMapper&&) = delete;
        ~PooledDataMapper() noexcept
        {
            if (_entry.mapper)
                ReturnToPool();
        }

        /// Access the underlying data mapper via pointer semantics
        DataMapper* operator->() const noexcept
        {
            return _entry.mapper.get();
        }

        /// Access the underlying data mapper via reference semantics
        /// This is useful for passing the pooled data mapper to functions
        /// that expect a DataMapper reference
        [[nodiscard]] DataMapper& Get() const noexcept
        {
            return *_entry.mapper;
        }

      private:
        void ReturnToPool() noexcept
        {
            _pool.Return(std::move(_entry));
            _entry.mapper = nullptr;
        }

        Entry _entry;
        Pool& _pool;
    };

  private:
    struct WaiterNode; // defined below; referenced by ReturnLocked's signature.

    /// Detaches the async backend from a returned mapper's connection before it is idled or handed
    /// off, so a recycled connection never carries references to executors that may since have been
    /// destroyed (the next @c AcquireAsync re-enables it fresh). Shared by every @c Return overload.
    ///
    /// @warning The caller must not return a mapper that still has an async operation in flight on it:
    /// dropping the backend destroys the strand/executors an outstanding offloaded step references and
    /// races the worker still touching the ODBC handle. Await every async op before returning.
    /// @param dm The mapper whose connection's async backend is dropped.
    static void DropAsyncBackend(DataMapper& dm) noexcept
    {
        dm.Connection().DisableAsync();
    }

    /// @return The current time, or a default-constructed time point when this configuration enables
    ///         no time-based bound and therefore never inspects timestamps.
    [[nodiscard]] Clock::time_point NowIfTracking() const noexcept
    {
        if constexpr (!TracksTime)
            return {};
        else
        {
#if defined(BUILD_TESTS)
            if (_clock)
                return _clock();
#endif
            return Clock::now();
        }
    }

    /// Creates a fresh entry, stamped with the current time.
    /// @return The new entry; its mapper is never null.
    [[nodiscard]] Entry MakeEntry() const
    {
        auto const now = NowIfTracking();
        return Entry { std::make_unique<DataMapper>(), now, now };
    }

    /// Decides whether an idle connection may still be handed to a caller.
    ///
    /// Applied only to connections coming out of the idle set. A connection handed straight from
    /// @ref Return to a parked waiter deliberately bypasses this: see @ref ReturnLocked.
    ///
    /// @param entry The idle entry under consideration.
    /// @param now The current time, as returned by @ref NowIfTracking.
    /// @return true when the entry is within both configured bounds and, if validation is enabled,
    ///         its connection is still reported alive.
    [[nodiscard]] bool IsUsable(Entry const& entry, Clock::time_point now) const noexcept
    {
        if constexpr (Config.maxLifetimeMs > 0)
        {
            if (now - entry.createdAt >= Config.MaxLifetime())
                return false;
        }
        if constexpr (Config.maxIdleTimeMs > 0)
        {
            if (now - entry.idleSince >= Config.MaxIdleTime())
                return false;
        }
        if constexpr (Config.validateOnBorrow == ValidateOnBorrow::Yes)
        {
            if (!entry.mapper->Connection().IsAlive())
                return false;
        }
        return true;
    }

    /// Pops the most recently idled usable connection, retiring any expired or dead entries it passes.
    ///
    /// Retired entries are moved into @p retired rather than destroyed here, so the ODBC disconnect
    /// they trigger happens after the caller has released @c _mutex instead of blocking every other
    /// thread for the duration of a network teardown.
    ///
    /// @pre @c _mutex is held by the caller.
    /// @param retired Collects the retired entries; must outlive the caller's lock.
    /// @return A usable entry, or an entry with a null mapper when the idle set holds none.
    [[nodiscard]] Entry TakeIdleLocked(std::vector<Entry>& retired)
    {
        auto const now = NowIfTracking();
        while (!_idleDataMappers.empty())
        {
            auto entry = std::move(_idleDataMappers.back());
            _idleDataMappers.pop_back();
            if (IsUsable(entry, now))
                return entry;
            retired.push_back(std::move(entry));
        }
        return {};
    }

    /// @param entry The entry about to be stored in the idle set.
    /// @param now The current time, as returned by @ref NowIfTracking.
    /// @return true when the connection has outlived @ref PoolConfig::maxLifetimeMs and must be
    ///         retired instead of idled. Checking this on return as well as on borrow releases the
    ///         connection as soon as it is no longer wanted, rather than holding it until the next
    ///         acquire. The idle bound is not checked here — the entry is idle for zero time.
    [[nodiscard]] static bool IsPastLifetime([[maybe_unused]] Entry const& entry,
                                             [[maybe_unused]] Clock::time_point now) noexcept
    {
        if constexpr (Config.maxLifetimeMs > 0)
            return now - entry.createdAt >= Config.MaxLifetime();
        else
            return false;
    }

    /// always return the data mapper to the pool for this strategy
    void Return(Entry entry) noexcept
        requires(Config.growthStrategy == GrowthStrategy::UnboundedGrow)
    {
        DropAsyncBackend(*entry.mapper);
        auto const now = NowIfTracking();
        if (IsPastLifetime(entry, now))
            return; // retired here, outside the lock
        entry.idleSince = now;
        SqlLogger::GetLogger().OnConnectionIdle(entry.mapper->Connection());
        std::scoped_lock lock(_mutex);
        _idleDataMappers.push_back(std::move(entry));
    }

    /// for bounded wait strategy, return the data mapper to the pool: hand it to the next FIFO waiter
    /// (sync or async) or idle it.
    void Return(Entry entry) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        DropAsyncBackend(*entry.mapper);
        Entry retired; // declared before the lock so its disconnect runs after the lock is released
        std::shared_ptr<WaiterNode> toResume;
        {
            std::scoped_lock const lock(_mutex);
            toResume = ReturnLocked(std::move(entry), retired);
        }
        // Resume outside the lock to avoid re-entrancy (the resumed coroutine may call back into the pool).
        if (toResume)
            toResume->resume->Resume(toResume->handle);
    }

    /// Produces a data mapper for a caller without waiting: reuses a usable idle one, otherwise
    /// creates a fresh one while the pool is below capacity.
    ///
    /// @pre @c _mutex is held by the caller.
    /// @param retired Collects entries retired while scanning the idle set; must outlive the lock.
    /// @return A ready entry, or an entry with a null mapper when the pool is at capacity and the
    ///         caller must park.
    [[nodiscard]] Entry AcquireReadyLocked(std::vector<Entry>& retired)
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        if (auto entry = TakeIdleLocked(retired); entry.mapper)
        {
            ++_checkedOut;
            SqlLogger::GetLogger().OnConnectionReuse(entry.mapper->Connection());
            return entry;
        }
        if (_checkedOut < Config.maxSize)
        {
            // below capacity: create a fresh data mapper. Claim the slot only once the connection
            // actually stands up, so a failing connect does not leak capacity.
            auto fresh = MakeEntry();
            ++_checkedOut;
            return fresh;
        }
        return {};
    }

    /// Hands @p entry to the next FIFO waiter (transferring the checked-out count) or idles it. Serving
    /// @c _waiters in arrival order keeps sync @ref Acquire and async @ref AcquireAsync waiters fair.
    ///
    /// @pre @c _mutex is held by the caller.
    /// @param entry The entry to return; its mapper's async backend must already be disabled.
    /// @param retired Receives the entry when it is retired for having outlived
    ///                @ref PoolConfig::maxLifetimeMs; must outlive the caller's lock.
    /// @return The async waiter node handed the mapper, to be resumed by the caller after releasing
    ///         @c _mutex; @c nullptr if a sync waiter was woken in place, or the entry was idled or
    ///         retired.
    std::shared_ptr<WaiterNode> ReturnLocked(Entry entry, Entry& retired) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        while (!_waiters.empty())
        {
            auto node = _waiters.front();
            _waiters.pop_front();
            // _waiters only ever holds parked nodes (an async awaitable de-registers itself on
            // abandonment; a synchronous waiter is never abandoned), but guard defensively.
            if (node->state != WaiterNode::State::Parked)
                continue;
            // A direct hand-off deliberately skips the health bounds and the liveness check. A waiter
            // is blocked on a predicate only a hand-off satisfies, so retiring the connection here
            // would strand it, and manufacturing a replacement means a DataMapper construction that
            // may throw inside this noexcept path. The connection was in active use moments ago, and
            // it is still checked the next time it comes out of the idle set.
            //
            // Handed directly to a waiter, never idled: a reuse, not an idle transition.
            SqlLogger::GetLogger().OnConnectionReuse(entry.mapper->Connection());
            node->state = WaiterNode::State::Fulfilled;
            node->entry = std::move(entry); // hand off ownership; _checkedOut stays (transferred)
            if (node->kind == WaiterNode::Kind::Async)
                return node;       // resumed by the caller outside the lock
            node->cv.notify_one(); // wake the blocked Acquire(); it consumes node->entry
            return nullptr;
        }
        // No waiter: the connection goes idle, so the lifetime bound applies. Releasing the slot
        // matters either way — a retired connection frees capacity just as an idled one does.
        --_checkedOut;
        auto const now = NowIfTracking();
        if (IsPastLifetime(entry, now))
        {
            retired = std::move(entry); // destroyed by the caller, after _mutex is released
            return nullptr;
        }
        entry.idleSince = now;
        SqlLogger::GetLogger().OnConnectionIdle(entry.mapper->Connection());
        _idleDataMappers.push_back(std::move(entry));
        return nullptr;
    }

    /// for bounded overflow strategy, only return to pool if we have capacity, otherwise just destroy the data mapper
    void Return(Entry entry) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedOverflow)
    {
        DropAsyncBackend(*entry.mapper);
        auto const now = NowIfTracking();
        if (IsPastLifetime(entry, now))
            return; // retired here, outside the lock
        entry.idleSince = now;
        std::scoped_lock lock(_mutex);
        if (_idleDataMappers.size() < Config.maxSize)
        {
            SqlLogger::GetLogger().OnConnectionIdle(entry.mapper->Connection());
            _idleDataMappers.push_back(std::move(entry));
        }
    }

  public:
    /// Default constructor that pre-creates the initial number of data mappers and stores them in the pool
    /// No other constructors are provided, as the pool is configured at compile time via the template parameter
    explicit Pool()
    {
        _idleDataMappers.reserve(Config.initialSize);
        for ([[maybe_unused]] auto const _: std::views::iota(0U, Config.initialSize))
            _idleDataMappers.push_back(MakeEntry());
    }

    /// Destructor. The pool manages the lifecycle of the idle data mappers; be aware that any
    /// acquired data mappers not returned to the pool are destroyed when the pool is destroyed,
    /// which may leak resources if not handled properly.
    ~Pool() noexcept
    {
        // A parked AcquireAsync coroutine (or a thread blocked in Acquire) holds a reference back to this
        // pool, so destroying the pool out from under it is undefined: drive every AcquireAsync task to
        // completion and let every blocked Acquire() return first. The assert catches this in debug; the
        // warning surfaces it in release (where the later access would be a use-after-free).
        if (!_waiters.empty())
            SqlLogger::GetLogger().OnWarning(
                "Pool destroyed while acquirers are still waiting on it (coroutines parked in AcquireAsync "
                "and/or threads blocked in Acquire); the pool must outlive every acquirer (drive each "
                "AcquireAsync task to completion or destroy it first, and never destroy the pool while a "
                "thread is blocked in Acquire). This is undefined behavior.");
        assert(_waiters.empty() && "Pool destroyed while acquirers are still waiting on it");
    }

    Pool(Pool const&) = delete;
    Pool& operator=(Pool const&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    /// Function to acquire a data mapper from the pool, the behavior of this function depends on the growth strategy
    /// this is a specific implementation for the BoundedWait strategy, which blocks until a data mapper is available if the
    /// pool is at maximum capacity
    ///
    /// Prefer the @ref Acquire(std::chrono::milliseconds) overload in production code: this one waits
    /// indefinitely, so an exhausted pool parks the calling thread with no diagnostic.
    PooledDataMapper Acquire()
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        std::vector<Entry> retired; // declared before the lock: disconnects run after it is released
        std::unique_lock lock(_mutex);
        if (auto entry = AcquireReadyLocked(retired); entry.mapper)
            return PooledDataMapper(*this, std::move(entry));

        // Pool exhausted: park as a FIFO waiter (fair with AcquireAsync waiters) and block until a
        // mapper is handed to this node. The hand-off transfers a checked-out slot, so no ++_checkedOut.
        auto node = std::make_shared<WaiterNode>(WaiterNode::Kind::Sync);
        _waiters.push_back(node);
        node->cv.wait(lock, [&node] { return node->state == WaiterNode::State::Fulfilled; });
        return PooledDataMapper(*this, std::move(node->entry));
    }

    /// Acquires a data mapper, giving up if none becomes available within @p timeout.
    ///
    /// Bounds how long an exhausted BoundedWait pool may park the calling thread, so a stuck or
    /// overloaded pool surfaces as an error the caller can act on rather than as an indefinite hang.
    ///
    /// @param timeout How long to wait for a data mapper to be returned. A non-positive value makes
    ///                this a pure try-acquire.
    /// @return The acquired data mapper, or @ref PoolError::Timeout if @p timeout elapsed first.
    [[nodiscard]] std::expected<PooledDataMapper, PoolError> Acquire(std::chrono::milliseconds timeout)
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        std::vector<Entry> retired; // declared before the lock: disconnects run after it is released
        std::unique_lock lock(_mutex);
        if (auto entry = AcquireReadyLocked(retired); entry.mapper)
            return PooledDataMapper(*this, std::move(entry));

        auto node = std::make_shared<WaiterNode>(WaiterNode::Kind::Sync);
        _waiters.push_back(node);
        if (!node->cv.wait_for(lock, timeout, [&node] { return node->state == WaiterNode::State::Fulfilled; }))
        {
            // wait_for evaluates the predicate under the lock and reports its final value, so a false
            // result proves this node is still Parked and no hand-off can be in flight. De-registering
            // it here is therefore race-free: a later Return will never see it.
            node->state = WaiterNode::State::Abandoned;
            std::erase(_waiters, node);
            return std::unexpected { PoolError::Timeout };
        }
        return PooledDataMapper(*this, std::move(node->entry));
    }

    /// Function to acquire a data mapper from the pool, the behavior of this function depends on the growth strategy
    /// this is a specific implementation for the strategies that do not block, which always creates a new data mapper if
    /// the pool is empty, regardless of the maximum capacity
    PooledDataMapper Acquire()
        requires(Config.growthStrategy != GrowthStrategy::BoundedWait)
    {
        std::vector<Entry> retired; // declared before the lock: disconnects run after it is released
        std::scoped_lock lock(_mutex);
        auto entry = TakeIdleLocked(retired);
        if (!entry.mapper)
        {
            // no usable idle data mapper: create a new one and return it
            return PooledDataMapper(*this, MakeEntry());
        }
        SqlLogger::GetLogger().OnConnectionReuse(entry.mapper->Connection());
        return PooledDataMapper(*this, std::move(entry));
    }

    /// Acquires a data mapper, for the strategies that never wait.
    ///
    /// Provided so call sites can be written without knowing the strategy. These strategies create a
    /// fresh data mapper whenever no idle one is available, so the timeout can never elapse and the
    /// result always holds a value.
    ///
    /// @param timeout Ignored; see above.
    /// @return The acquired data mapper.
    [[nodiscard]] std::expected<PooledDataMapper, PoolError> Acquire([[maybe_unused]] std::chrono::milliseconds timeout)
        requires(Config.growthStrategy != GrowthStrategy::BoundedWait)
    {
        return Acquire();
    }

    /// Asynchronously acquires a DataMapper from the pool without blocking the calling thread.
    ///
    /// If the pool is exhausted (BoundedWait at capacity), the awaiting coroutine is suspended and
    /// resumed — via @p resume — when a mapper is returned, rather than parking a thread. The acquired
    /// mapper's connection is wired for async via SqlConnection::EnableAsync(@p dbWorkers, @p resume),
    /// so the caller can immediately co_await its async methods.
    ///
    /// @param dbWorkers The worker pool used to run the acquired mapper's blocking ODBC calls.
    /// @param resume The scheduler used to resume coroutines (typically the app run loop).
    /// @return A Task yielding a pooled DataMapper.
    [[nodiscard]] Async::Task<PooledDataMapper> AcquireAsync(Async::IExecutor& dbWorkers, Async::IResumeScheduler& resume)
    {
        // Forward to a coroutine taking pointers (coroutines must not take reference parameters).
        return AcquireAsyncImpl(&dbWorkers, &resume);
    }

    /// Configures the executors that the no-argument @ref AcquireAsync() overload wires acquired
    /// mappers for, so async consumers of this pool no longer repeat the executors at every call.
    ///
    /// This is opt-in and scoped to the pool: only pools configured this way hand out async-enabled
    /// mappers via the no-arg overload; synchronous @ref Acquire and connections outside the pool are
    /// unaffected. Unlike a process-global default, the executors' lifetime is tied to this pool,
    /// which already must outlive every acquirer.
    ///
    /// @warning @p dbWorkers and @p resume must outlive this pool's async use (the same contract the
    ///          explicit-argument @ref AcquireAsync overload already implies). Only references are
    ///          retained. Intended to be called once during setup, before any concurrent
    ///          @ref AcquireAsync(); it is not synchronized against in-flight acquirers.
    /// @param dbWorkers The worker pool used to run acquired mappers' blocking ODBC calls.
    /// @param resume The scheduler used to resume coroutines (typically the app run loop).
    void SetAsyncExecutors(Async::IExecutor& dbWorkers, Async::IResumeScheduler& resume) noexcept
    {
        _asyncDbWorkers = &dbWorkers;
        _asyncResume = &resume;
    }

    /// Asynchronously acquires a DataMapper using the executors previously set via
    /// @ref SetAsyncExecutors, without blocking the calling thread.
    ///
    /// Equivalent to the explicit-argument @ref AcquireAsync overload with the pool's stored
    /// executors; pass the executors to that overload to override them for a single call.
    ///
    /// @return A Task yielding a pooled DataMapper.
    /// @throws std::logic_error if @ref SetAsyncExecutors has not been called on this pool.
    [[nodiscard]] Async::Task<PooledDataMapper> AcquireAsync()
    {
        if (!_asyncDbWorkers || !_asyncResume)
            throw std::logic_error {
                "Pool::AcquireAsync(): no async executors configured; call Pool::SetAsyncExecutors(...) first "
                "or use the explicit AcquireAsync(dbWorkers, resume) overload."
            };
        return AcquireAsyncImpl(_asyncDbWorkers, _asyncResume);
    }

#if defined(BUILD_TESTS)
    [[nodiscard]] size_t IdleCount() noexcept
    {
        std::scoped_lock lock(_mutex);
        return _idleDataMappers.size();
    }

    /// @return the number of parked acquirers (blocked Acquire() threads + suspended AcquireAsync
    ///         coroutines); lets tests observe parking/fairness deterministically.
    [[nodiscard]] size_t WaiterCount() noexcept
    {
        std::scoped_lock lock(_mutex);
        return _waiters.size();
    }

    /// Overrides the clock the idle-time and lifetime bounds are measured against, so eviction can be
    /// driven deterministically instead of by sleeping.
    ///
    /// @warning Like @ref SetAsyncExecutors, this is setup, not a runtime knob: it is not
    ///          synchronized against in-flight acquirers, so call it before any concurrent use of the
    ///          pool. Advancing whatever time @p clock reports is the caller's business afterwards.
    /// @param clock Source of the current time; pass @c {} to restore the real clock.
    void SetClock(std::function<Clock::time_point()> clock) noexcept
    {
        _clock = std::move(clock);
    }
#endif

  private:
    /// A parked acquirer awaiting a DataMapper — a suspended @ref AcquireAsync coroutine (@c Kind::Async)
    /// or a blocked synchronous @ref Acquire thread (@c Kind::Sync). Both share one FIFO queue
    /// (@c _waiters), served in arrival order so neither kind starves the other.
    ///
    /// Heap-allocated and shared with the pool. Holding the handed-off mapper and @c state in this node
    /// (not via a pointer into a coroutine frame) lets @ref Return and the awaitable's destructor
    /// coordinate purely through node state under @c _mutex, never touching a possibly-destroyed frame.
    struct WaiterNode
    {
        /// Whether this waiter is a suspended coroutine or a blocked synchronous Acquire() thread.
        enum class Kind : std::uint8_t
        {
            Sync,  ///< A blocked @ref Acquire thread; woken via @c cv.
            Async, ///< A suspended @ref AcquireAsync coroutine; resumed via @c resume / @c handle.
        };

        /// Liveness of the waiter, transitioned only under @c Pool::_mutex.
        enum class State : std::uint8_t
        {
            Parked,    ///< Registered in @c _waiters, awaiting a mapper.
            Fulfilled, ///< Return handed it a mapper (in @c mapper) and woke/scheduled it.
            Abandoned, ///< The awaiting async task was destroyed (or its entry consumed); inert.
        };

        Kind kind;
        State state = State::Parked;
        Entry entry {}; ///< Filled by Return on hand-off; lives outside any frame.

        // Async waiter only:
        std::coroutine_handle<> handle {};
        Async::IResumeScheduler* resume = nullptr;

        // Sync waiter only: the blocked Acquire() waits on this CV (under Pool::_mutex). One waiter per
        // CV, so Return's notify_one wakes exactly the served thread.
        std::condition_variable cv {};

        explicit WaiterNode(Kind nodeKind) noexcept:
            kind { nodeKind }
        {
        }
    };

    /// Awaitable that acquires a DataMapper, suspending only when the pool is at capacity.
    ///
    /// Non-copyable/non-movable: constructed in place in the co_await expression. On suspension it
    /// registers a shared @ref WaiterNode (@c Kind::Async) in pool._waiters; the node carries the
    /// handed-off mapper and liveness state so Return() and this destructor coordinate safely.
    struct AsyncAcquireAwaitable
    {
        Pool& pool;
        Async::IResumeScheduler& resume;
        Entry acquired {};                   ///< Entry obtained without suspending (idle/fresh).
        std::shared_ptr<WaiterNode> node {}; ///< Set only while parked; shared with the pool.
        std::vector<Entry> retired {};       ///< Entries retired while scanning the idle set.

        AsyncAcquireAwaitable(Pool& poolRef, Async::IResumeScheduler& resumeRef) noexcept:
            pool { poolRef },
            resume { resumeRef }
        {
        }

        AsyncAcquireAwaitable(AsyncAcquireAwaitable const&) = delete;
        AsyncAcquireAwaitable& operator=(AsyncAcquireAwaitable const&) = delete;
        AsyncAcquireAwaitable(AsyncAcquireAwaitable&&) = delete;
        AsyncAcquireAwaitable& operator=(AsyncAcquireAwaitable&&) = delete;

        /// Cleans up if the awaiting coroutine is destroyed before it consumes its mapper.
        ///
        /// Under pool._mutex: if still parked, de-registers the node so a later Return() never hands
        /// off to a dead frame. If Return() already handed off a mapper (Fulfilled) that await_resume
        /// never consumed, reclaims it into the pool so the BoundedWait checked-out count is not
        /// leaked (possibly handing it straight to the next waiter, resumed after the lock is released).
        ///
        /// @warning A task that has already been handed a mapper must still be driven to completion:
        /// the resumption Return() scheduled cannot be cancelled, so a coroutine frame with a pending
        /// resumption must not be freed (do not destroy such a task concurrently with, or right after,
        /// the hand-off). Likewise the pool must outlive every task acquired from it.
        ~AsyncAcquireAwaitable()
        {
            if (!node)
                return;
            Entry reclaimed; // declared before the lock so its disconnect runs after the lock is released
            std::shared_ptr<WaiterNode> toResume;
            {
                std::scoped_lock const lock(pool._mutex);
                switch (node->state)
                {
                    case WaiterNode::State::Parked:
                        // Never fulfilled: remove ourselves so Return() won't hand off to a dead frame.
                        // Parking never incremented _checkedOut, so there is nothing to release.
                        node->state = WaiterNode::State::Abandoned;
                        std::erase(pool._waiters, node);
                        break;
                    case WaiterNode::State::Fulfilled:
                        // Handed a mapper but the task is dropped before consuming it: reclaim it,
                        // releasing this acquisition's checked-out count so the pool does not leak.
                        node->state = WaiterNode::State::Abandoned;
                        if constexpr (Config.growthStrategy == GrowthStrategy::BoundedWait)
                        {
                            if (node->entry.mapper)
                                toResume = pool.ReturnLocked(std::move(node->entry), reclaimed);
                        }
                        break;
                    case WaiterNode::State::Abandoned:
                        break;
                }
            }
            if (toResume)
                toResume->resume->Resume(toResume->handle);
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::scoped_lock const lock(pool._mutex);
            // Retired entries land in `retired`, a member of this awaitable, so the disconnects they
            // trigger happen when the awaitable dies rather than under pool._mutex.
            acquired = pool.TakeIdleLocked(retired);
            if (acquired.mapper)
            {
                if constexpr (Config.growthStrategy == GrowthStrategy::BoundedWait)
                    ++pool._checkedOut;
                SqlLogger::GetLogger().OnConnectionReuse(acquired.mapper->Connection());
                return false; // do not suspend — resume immediately
            }
            // Only BoundedWait bounds the pool and parks coroutines on exhaustion. The non-blocking
            // strategies (BoundedOverflow — the default — and UnboundedGrow) always create a fresh
            // mapper here, matching the synchronous Acquire() overloads, which also never suspend.
            if constexpr (Config.growthStrategy == GrowthStrategy::BoundedWait)
            {
                if (pool._checkedOut >= Config.maxSize)
                {
                    node = std::make_shared<WaiterNode>(WaiterNode::Kind::Async);
                    node->handle = handle;
                    node->resume = &resume;
                    pool._waiters.push_back(node);
                    return true; // suspend until a mapper is returned
                }
                ++pool._checkedOut;
            }
            acquired = pool.MakeEntry();
            return false;
        }

        Entry await_resume() noexcept
        {
            // If we suspended, Return() placed the entry in the shared node; take it here (on the
            // resuming thread, with no concurrent access per the destruction contract). That leaves
            // node->entry empty, so the destructor treats the node as already consumed.
            if (node)
                return std::move(node->entry);
            return std::move(acquired);
        }
    };

    Async::Task<PooledDataMapper> AcquireAsyncImpl(Async::IExecutor* dbWorkers, Async::IResumeScheduler* resume)
    {
        auto entry = co_await AsyncAcquireAwaitable { *this, *resume };
        // Wrap in the RAII PooledDataMapper BEFORE the throwing EnableAsync call: if EnableAsync
        // throws (e.g. bad_alloc), ~PooledDataMapper returns the mapper to the pool, decrementing
        // _checkedOut and avoiding a permanent BoundedWait capacity leak.
        auto pooled = PooledDataMapper(*this, std::move(entry));
        pooled->Connection().EnableAsync(*dbWorkers, *resume);
        co_return std::move(pooled);
    }

    std::mutex _mutex;
    std::vector<Entry> _idleDataMappers;
    size_t _checkedOut {};
#if defined(BUILD_TESTS)
    /// Test-injected clock; the real @c Clock::now is used when unset. @see SetClock
    std::function<Clock::time_point()> _clock {};
#endif
    /// Executors used by the no-argument @ref AcquireAsync() overload; set via @ref SetAsyncExecutors.
    /// Null until configured. Only references are held; they must outlive the pool's async use.
    Async::IExecutor* _asyncDbWorkers = nullptr;
    Async::IResumeScheduler* _asyncResume = nullptr;
    /// FIFO of parked acquirers (sync @ref Acquire threads and async @ref AcquireAsync coroutines) in
    /// arrival order. Each sync waiter owns its CV inside its @ref WaiterNode, so no shared CV is needed.
    std::deque<std::shared_ptr<WaiterNode>> _waiters;
};

// Default pool configuration, configurable via CMake options:
//   LIGHTWEIGHT_POOL_INITIAL_SIZE       (default: 4)
//   LIGHTWEIGHT_POOL_MAX_SIZE           (default: 16)
//   LIGHTWEIGHT_POOL_GROWTH_STRATEGY    (default: BoundedOverflow)
//     Accepted values: BoundedWait, BoundedOverflow, UnboundedGrow
//   LIGHTWEIGHT_POOL_VALIDATE_ON_BORROW (default: Yes)
//     Accepted values: Yes, No
//   LIGHTWEIGHT_POOL_MAX_IDLE_TIME_MS   (default: 0, meaning no bound)
//   LIGHTWEIGHT_POOL_MAX_LIFETIME_MS    (default: 0, meaning no bound)

#if !defined(LIGHTWEIGHT_POOL_INITIAL_SIZE)
    #define LIGHTWEIGHT_POOL_INITIAL_SIZE 4
#endif

#if !defined(LIGHTWEIGHT_POOL_MAX_SIZE)
    #define LIGHTWEIGHT_POOL_MAX_SIZE 16
#endif

#if !defined(LIGHTWEIGHT_POOL_GROWTH_STRATEGY)
    #define LIGHTWEIGHT_POOL_GROWTH_STRATEGY BoundedOverflow
#endif

#if !defined(LIGHTWEIGHT_POOL_VALIDATE_ON_BORROW)
    #define LIGHTWEIGHT_POOL_VALIDATE_ON_BORROW Yes
#endif

// The lifetime bounds default to 0 (disabled): a default recycle window would silently change the
// behaviour of every existing deployment, and the right value depends on the infrastructure the
// connections traverse. See PoolConfig for how to choose one.
#if !defined(LIGHTWEIGHT_POOL_MAX_IDLE_TIME_MS)
    #define LIGHTWEIGHT_POOL_MAX_IDLE_TIME_MS 0
#endif

#if !defined(LIGHTWEIGHT_POOL_MAX_LIFETIME_MS)
    #define LIGHTWEIGHT_POOL_MAX_LIFETIME_MS 0
#endif

inline constexpr PoolConfig DefaultPoolConfig {
    .initialSize = LIGHTWEIGHT_POOL_INITIAL_SIZE,
    .maxSize = LIGHTWEIGHT_POOL_MAX_SIZE,
    .growthStrategy = GrowthStrategy::LIGHTWEIGHT_POOL_GROWTH_STRATEGY,
    .validateOnBorrow = ValidateOnBorrow::LIGHTWEIGHT_POOL_VALIDATE_ON_BORROW,
    .maxIdleTimeMs = LIGHTWEIGHT_POOL_MAX_IDLE_TIME_MS,
    .maxLifetimeMs = LIGHTWEIGHT_POOL_MAX_LIFETIME_MS,
};

using DataMapperPool = Pool<DefaultPoolConfig>;

/// Returns the process-wide global DataMapper pool.
///
/// The pool is configured at compile time via the LIGHTWEIGHT_POOL_* defines.
/// Because the singleton lives inside the Lightweight library, it is shared
/// correctly across shared-library boundaries.
LIGHTWEIGHT_API DataMapperPool& GlobalDataMapperPool();

} // namespace Lightweight
