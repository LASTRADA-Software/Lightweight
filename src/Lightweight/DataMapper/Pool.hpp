// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "DataMapper.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
    #include "../Async/Executor.hpp"
    #include "../Async/Task.hpp"

    #include <cassert>
    #include <coroutine>
    #include <deque>
#endif

namespace Lightweight
{

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

/// Structure to hold the configuration of the pool, including the initial size, maximum size and growth strategy.
/// Structure is used as a template parameter for the Pool class to configure its behavior at compile time.
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
};

/// A thread-safe pool of DataMapper instances with the policy configured by the PoolConfig template parameter.
/// The pool allows acquiring and returning DataMapper instances, and manages the lifecycle of these instances according to
/// the specified growth strategy.
template <PoolConfig Config>
class Pool
{
  public:
    /// A wrapper around a DataMapper that returns it to the pool when destroyed
    /// can be created only from the Pool and is move-only to ensure it is always
    /// returned to the pool when it goes out of scope
    class PooledDataMapper
    {
      private:
        friend class Pool;

        explicit PooledDataMapper(Pool& pool, std::unique_ptr<DataMapper> dm) noexcept:
            _dm { std::move(dm) },
            _pool { pool }
        {
        }

      public:
        PooledDataMapper() = delete;
        PooledDataMapper(PooledDataMapper const&) = delete;

        /// Move constructor for the pooled data mapper, the only public
        /// constructor, allows moving the pooled data mapper but not copying it
        PooledDataMapper(PooledDataMapper&& other) noexcept:
            _dm { std::move(other._dm) },
            _pool { other._pool }
        {
        }
        PooledDataMapper& operator=(PooledDataMapper const&) = delete;
        PooledDataMapper& operator=(PooledDataMapper&&) = delete;
        ~PooledDataMapper() noexcept
        {
            if (_dm)
                ReturnToPool();
        }

        /// Access the underlying data mapper via pointer semantics
        DataMapper* operator->() const noexcept
        {
            return _dm.get();
        }

        /// Access the underlying data mapper via reference semantics
        /// This is useful for passing the pooled data mapper to functions
        /// that expect a DataMapper reference
        [[nodiscard]] DataMapper& Get() const noexcept
        {
            return *_dm;
        }

      private:
        void ReturnToPool() noexcept
        {
            _pool.Return(std::move(_dm));
            _dm = nullptr;
        }

        std::unique_ptr<DataMapper> _dm;
        Pool& _pool;
    };

  private:
    /// always return the data mapper to the pool for this strategy
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::UnboundedGrow)
    {
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
        // Drop any async backend so a recycled connection never carries references to executors
        // that may have been destroyed; the next AcquireAsync re-enables it fresh.
        dm->Connection().DisableAsync();
#endif
        std::scoped_lock lock(_mutex);
        _idleDataMappers.push_back(std::move(dm));
    }

    /// for bounded wait strategy, return the data mapper to the pool. A suspended async waiter (if any)
    /// is handed the mapper directly and resumed; otherwise a blocked synchronous waiter is notified.
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
        // Drop any async backend before the mapper is idled or handed to a waiter, so a recycled
        // connection never carries references to (possibly destroyed) executors; a waiter's
        // AcquireAsync re-enables it fresh.
        dm->Connection().DisableAsync();
        AsyncWaiter waiter;
        bool haveAsyncWaiter = false;
#endif
        {
            std::scoped_lock lock(_mutex);
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
            if (!_asyncWaiters.empty())
            {
                waiter = _asyncWaiters.front();
                _asyncWaiters.pop_front();
                *waiter.slot = std::move(dm); // hand off ownership; _checkedOut stays (transferred to the waiter)
                haveAsyncWaiter = true;
            }
            else
#endif
            {
                _idleDataMappers.push_back(std::move(dm));
                --_checkedOut;
            }
        }
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
        if (haveAsyncWaiter)
        {
            waiter.resume->Resume(waiter.handle); // resume outside the lock to avoid re-entrancy
            return;
        }
#endif
        _cv.notify_one();
    }

    /// for bounded overflow strategy, only return to pool if we have capacity, otherwise just destroy the data mapper
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedOverflow)
    {
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
        // Drop any async backend so a recycled connection never carries references to executors
        // that may have been destroyed; the next AcquireAsync re-enables it fresh.
        dm->Connection().DisableAsync();
#endif
        std::scoped_lock lock(_mutex);
        if (_idleDataMappers.size() < Config.maxSize)
            _idleDataMappers.push_back(std::move(dm));
    }

  public:
    /// Default constructor that pre-creates the initial number of data mappers and stores them in the pool
    /// No other constructors are provided, as the pool is configured at compile time via the template parameter
    explicit Pool()
    {
        _idleDataMappers.reserve(Config.initialSize);
        for ([[maybe_unused]] auto const _: std::views::iota(0U, Config.initialSize))
            _idleDataMappers.push_back(std::make_unique<DataMapper>());
    }

    /// Destructor. The pool manages the lifecycle of the idle data mappers; be aware that any
    /// acquired data mappers not returned to the pool are destroyed when the pool is destroyed,
    /// which may leak resources if not handled properly.
    ~Pool() noexcept
    {
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
        // A coroutine still parked in AcquireAsync when the pool dies would leak its frame: we
        // cannot safely resume or destroy a frame the pool does not own. Drive such tasks to
        // completion before destroying the pool.
        assert(_asyncWaiters.empty() && "Pool destroyed with coroutines still parked in AcquireAsync");
#endif
    }

    Pool(Pool const&) = delete;
    Pool& operator=(Pool const&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    /// Function to acquire a data mapper from the pool, the behavior of this function depends on the growth strategy
    /// this is a specific implementation for the BoundedWait strategy, which blocks until a data mapper is available if the
    /// pool is at maximum capacity
    PooledDataMapper Acquire()
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        std::unique_lock lock(_mutex);
        if (_idleDataMappers.empty())
        {
            if (_checkedOut >= Config.maxSize)
            {
                // wait until a data mapper is returned to the pool
                _cv.wait(lock, [this] { return !_idleDataMappers.empty(); });
            }
            else
            {
                // create a new data mapper and return it
                ++_checkedOut;
                return PooledDataMapper(*this, std::make_unique<DataMapper>());
            }
        }

        // get a data mapper from the pool
        auto dm = std::move(_idleDataMappers.back());
        _idleDataMappers.pop_back();
        ++_checkedOut;
        return PooledDataMapper(*this, std::move(dm));
    }

    /// Function to acquire a data mapper from the pool, the behavior of this function depends on the growth strategy
    /// this is a specific implementation for the strategies that do not block, which always creates a new data mapper if
    /// the pool is empty, regardless of the maximum capacity
    PooledDataMapper Acquire()
        requires(Config.growthStrategy != GrowthStrategy::BoundedWait)
    {
        std::scoped_lock lock(_mutex);
        if (_idleDataMappers.empty())
        {
            // create a new data mapper and return it
            return PooledDataMapper(*this, std::make_unique<DataMapper>());
        }

        // get a data mapper from the pool
        auto dm = std::move(_idleDataMappers.back());
        _idleDataMappers.pop_back();
        return PooledDataMapper(*this, std::move(dm));
    }

#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
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
#endif

#if defined(BUILD_TESTS)
    [[nodiscard]] size_t IdleCount() noexcept
    {
        std::scoped_lock lock(_mutex);
        return _idleDataMappers.size();
    }
#endif

  private:
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
    /// A suspended coroutine waiting for a DataMapper to become available.
    struct AsyncWaiter
    {
        std::coroutine_handle<> handle {};
        Async::IResumeScheduler* resume = nullptr;
        std::unique_ptr<DataMapper>* slot = nullptr;
    };

    /// Awaitable that acquires a DataMapper, suspending only when the pool is at capacity.
    ///
    /// Non-copyable/non-movable: it is constructed in place in the co_await expression and lives in
    /// the coroutine frame. The destructor de-registers a still-parked waiter so that destroying a
    /// suspended AcquireAsync task does not leave a dangling entry in pool._asyncWaiters.
    struct AsyncAcquireAwaitable
    {
        Pool& pool;
        Async::IResumeScheduler& resume;
        std::unique_ptr<DataMapper> acquired {};
        bool parked = false; ///< true while this awaitable's waiter sits in pool._asyncWaiters.

        AsyncAcquireAwaitable(Pool& poolRef, Async::IResumeScheduler& resumeRef) noexcept:
            pool { poolRef },
            resume { resumeRef }
        {
        }

        AsyncAcquireAwaitable(AsyncAcquireAwaitable const&) = delete;
        AsyncAcquireAwaitable& operator=(AsyncAcquireAwaitable const&) = delete;
        AsyncAcquireAwaitable(AsyncAcquireAwaitable&&) = delete;
        AsyncAcquireAwaitable& operator=(AsyncAcquireAwaitable&&) = delete;

        /// De-registers a still-parked waiter if the awaiting coroutine is destroyed before it is
        /// resumed (e.g. the AcquireAsync task is dropped/cancelled). Without this, a later Return()
        /// would write through a dangling slot pointer and resume a destroyed coroutine.
        ///
        /// Race-free for the single-threaded pump model (Return and cancellation run on the same
        /// thread). In the multi-threaded resume model a suspended AcquireAsync task must not be
        /// destroyed from a thread other than the one returning mappers.
        ~AsyncAcquireAwaitable()
        {
            if (!parked)
                return;
            std::scoped_lock const lock(pool._mutex);
            std::erase_if(pool._asyncWaiters, [this](AsyncWaiter const& w) { return w.slot == &acquired; });
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        bool await_suspend(std::coroutine_handle<> handle)
        {
            std::scoped_lock const lock(pool._mutex);
            if (!pool._idleDataMappers.empty())
            {
                acquired = std::move(pool._idleDataMappers.back());
                pool._idleDataMappers.pop_back();
                if constexpr (Config.growthStrategy == GrowthStrategy::BoundedWait)
                    ++pool._checkedOut;
                return false; // do not suspend — resume immediately
            }
            // Only BoundedWait bounds the pool and parks coroutines on exhaustion. The non-blocking
            // strategies (BoundedOverflow — the default — and UnboundedGrow) always create a fresh
            // mapper here, matching the synchronous Acquire() overloads, which also never suspend.
            if constexpr (Config.growthStrategy == GrowthStrategy::BoundedWait)
            {
                if (pool._checkedOut >= Config.maxSize)
                {
                    pool._asyncWaiters.push_back(AsyncWaiter { handle, &resume, &acquired });
                    parked = true;
                    return true; // suspend until a mapper is returned
                }
                ++pool._checkedOut;
            }
            acquired = std::make_unique<DataMapper>();
            return false;
        }

        std::unique_ptr<DataMapper> await_resume() noexcept
        {
            parked = false;
            return std::move(acquired);
        }
    };

    Async::Task<PooledDataMapper> AcquireAsyncImpl(Async::IExecutor* dbWorkers, Async::IResumeScheduler* resume)
    {
        auto dm = co_await AsyncAcquireAwaitable { *this, *resume };
        // Wrap in the RAII PooledDataMapper BEFORE the throwing EnableAsync call: if EnableAsync
        // throws (e.g. bad_alloc), ~PooledDataMapper returns the mapper to the pool, decrementing
        // _checkedOut and avoiding a permanent BoundedWait capacity leak.
        auto pooled = PooledDataMapper(*this, std::move(dm));
        pooled->Connection().EnableAsync(*dbWorkers, *resume);
        co_return std::move(pooled);
    }
#endif

    std::mutex _mutex;
    std::condition_variable _cv;
    std::vector<std::unique_ptr<DataMapper>> _idleDataMappers;
    size_t _checkedOut {};
#if defined(LIGHTWEIGHT_ENABLE_ASYNC)
    std::deque<AsyncWaiter> _asyncWaiters;
#endif
};

// Default pool configuration, configurable via CMake options:
//   LIGHTWEIGHT_POOL_INITIAL_SIZE     (default: 4)
//   LIGHTWEIGHT_POOL_MAX_SIZE         (default: 16)
//   LIGHTWEIGHT_POOL_GROWTH_STRATEGY  (default: BoundedOverflow)
//     Accepted values: BoundedWait, BoundedOverflow, UnboundedGrow

#if !defined(LIGHTWEIGHT_POOL_INITIAL_SIZE)
    #define LIGHTWEIGHT_POOL_INITIAL_SIZE 4
#endif

#if !defined(LIGHTWEIGHT_POOL_MAX_SIZE)
    #define LIGHTWEIGHT_POOL_MAX_SIZE 16
#endif

#if !defined(LIGHTWEIGHT_POOL_GROWTH_STRATEGY)
    #define LIGHTWEIGHT_POOL_GROWTH_STRATEGY BoundedOverflow
#endif

inline constexpr PoolConfig DefaultPoolConfig {
    .initialSize = LIGHTWEIGHT_POOL_INITIAL_SIZE,
    .maxSize = LIGHTWEIGHT_POOL_MAX_SIZE,
    .growthStrategy = GrowthStrategy::LIGHTWEIGHT_POOL_GROWTH_STRATEGY,
};

using DataMapperPool = Pool<DefaultPoolConfig>;

/// Returns the process-wide global DataMapper pool.
///
/// The pool is configured at compile time via the LIGHTWEIGHT_POOL_* defines.
/// Because the singleton lives inside the Lightweight library, it is shared
/// correctly across shared-library boundaries.
LIGHTWEIGHT_API DataMapperPool& GlobalDataMapperPool();

} // namespace Lightweight
