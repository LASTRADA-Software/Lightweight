// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "DataMapper.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>

namespace Lightweight
{

// Enum to define growth strategies of the pool
//
//  BoundedWait
//    Pre-create initialSize objects.  Allow the total count
//    to grow up to maxSize.  Once maxSize objects exist, callers BLOCK
//    until one is returned.
//
//  BoundedOverflow
//    Pre-create initialSize objects.  The pool stores up to maxSize
//    objects.  If none are idle, a fresh object is ALWAYS created (no
//    waiting).  On return: kept if the idle set is below maxSize, otherwise
//    destroyed.
//
//  UnboundedGrow
//    Pre-create initialSize objects.  Grow without limit.  Every returned
//    object is always kept in the pool.
//
enum class GrowthStrategy : uint8_t
{
    BoundedWait,
    BoundedOverflow,
    UnboundedGrow,
};

struct PoolConfig
{
    size_t initialSize {};
    size_t maxSize {};
    GrowthStrategy growthStrategy { GrowthStrategy::BoundedWait };
};

template <PoolConfig Config>
class Pool
{
  public:
    class PooledDataMapper
    {
        friend class Pool;

        explicit PooledDataMapper(Pool& pool, std::unique_ptr<DataMapper> dm) noexcept:
            _dm { std::move(dm) },
            _pool { pool }
        {
        }

      public:
        PooledDataMapper() = delete;
        PooledDataMapper(PooledDataMapper const&) = delete;
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

        DataMapper* operator->() noexcept
        {
            return _dm.get();
        }

        DataMapper& get() noexcept
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
    // always return the data mapper to the pool for this strategy
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::UnboundedGrow)
    {
        std::scoped_lock lock(_mutex);
        _idleDataMappers.push_back(std::move(dm));
    }

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

    // found bounded overflow strategy, only return to pool if we have capacity, otherwise just destroy the data mapper
    void Return(std::unique_ptr<DataMapper> dm) noexcept
        requires(Config.growthStrategy == GrowthStrategy::BoundedOverflow)
    {
        std::scoped_lock lock(_mutex);
        if (_idleDataMappers.size() < Config.maxSize)
            _idleDataMappers.push_back(std::move(dm));
    }

  public:
    explicit Pool(PoolConfig config = Config)
    {
        _idleDataMappers.reserve(config.initialSize);
        for (size_t i = 0; i < config.initialSize; ++i)
            _idleDataMappers.push_back(std::make_unique<DataMapper>());
    }

    ~Pool() noexcept = default;

    Pool(Pool const&) = delete;
    Pool& operator=(Pool const&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

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

template <PoolConfig Config>
decltype(auto) GlobalDataMapperPool()
{
    static Pool<Config> pool;
    return pool;
}

} // namespace Lightweight
