// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "DataMapper.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

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
        std::scoped_lock lock(_mutex);
        _idleDataMappers.push_back(std::move(dm));
    }

    /// for bounded wait strategy, return the data mapper to the pool and notify one waiting thread if any
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedWait)
    {
        {
            std::scoped_lock lock(_mutex);
            _idleDataMappers.push_back(std::move(dm));
            --_checkedOut;
        }
        _cv.notify_one();
    }

    /// for bounded overflow strategy, only return to pool if we have capacity, otherwise just destroy the data mapper
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedOverflow)
    {
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

    /// Default destructor, the pool manages the lifecycle of the data mappers, so no special cleanup is needed
    /// bug be aware that any acquired data mappers that are not returned to the pool will be destroyed when the pool is
    /// destroyed, which may lead to resource leaks if not handled properly
    ~Pool() noexcept = default;

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

#if defined(BUILD_TESTS)
    [[nodiscard]] size_t IdleCount() noexcept
    {
        std::scoped_lock lock(_mutex);
        return _idleDataMappers.size();
    }
#endif

  private:
    std::mutex _mutex;
    std::condition_variable _cv;
    std::vector<std::unique_ptr<DataMapper>> _idleDataMappers;
    size_t _checkedOut {};
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
