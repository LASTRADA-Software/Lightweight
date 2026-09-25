// SPDX-License-Identifier: Apache-2.0

#include "DataMapper.hpp"
#include "Pool.hpp"

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace Lightweight
{

namespace
{
    /// Relation-load source of a plain mapper connected with the default connection string: loads
    /// borrow from @ref GlobalDataMapperPool. It is reached only when a load actually runs, so merely
    /// reading records neither constructs the pool nor opens its initial connections.
    class GlobalPoolLoadSource final: public detail::RelationLoadSource
    {
      public:
        [[nodiscard]] std::shared_ptr<DataMapper> Borrow() override
        {
            return GlobalDataMapperPool().LoadSourceForRelations()->Borrow();
        }

        /// @return The one instance, shared by every such mapper.
        [[nodiscard]] static std::shared_ptr<detail::RelationLoadSource> const& Instance()
        {
            static auto const instance =
                std::shared_ptr<detail::RelationLoadSource> { std::make_shared<GlobalPoolLoadSource>() };
            return instance;
        }
    };

    /// Serves loads from a default-following source (a pool) only while the default is still the
    /// connection string the records were read with, and fails them once the application switched it.
    class DefaultPinnedLoadSource final: public detail::RelationLoadSource
    {
      public:
        DefaultPinnedLoadSource(std::shared_ptr<detail::RelationLoadSource> inner,
                                SqlConnectionString connectionString,
                                std::uint32_t generation) noexcept:
            _inner { std::move(inner) },
            _connectionString { std::move(connectionString) },
            _generation { generation }
        {
        }

        [[nodiscard]] std::shared_ptr<DataMapper> Borrow() override
        {
            // The generation spares the comparison while the default is untouched; switching away and
            // back again leaves the same string, and the same database, so that still loads.
            if (SqlConnection::DefaultConnectionStringGeneration() != _generation
                && SqlConnection::DefaultConnectionString() != _connectionString)
                throw SqlDefaultConnectionChangedError {};

            auto borrowed = _inner->Borrow();
            // A switch between the check above and the borrow would hand out a connection to the new
            // database; the connection itself says which one it is.
            if (borrowed->Connection().ConnectionString() != _connectionString)
                throw SqlDefaultConnectionChangedError {};
            return borrowed;
        }

      private:
        std::shared_ptr<detail::RelationLoadSource> _inner;
        SqlConnectionString _connectionString;
        std::uint32_t _generation;
    };

    /// Relation-load source of a plain (non-pooled) mapper connected with a connection string other
    /// than the current default: loads reconnect with that string, so a record read from a second
    /// database resolves its relations there too.
    ///
    /// Connections are kept for reuse between loads. An idle mapper does not hold its source (the
    /// records do), which is what keeps the two from owning each other.
    class ConnectionStringLoadSource final:
        public detail::RelationLoadSource,
        public std::enable_shared_from_this<ConnectionStringLoadSource>
    {
      public:
        explicit ConnectionStringLoadSource(SqlConnectionString connectionString):
            _connectionString { std::move(connectionString) }
        {
        }

        [[nodiscard]] std::shared_ptr<DataMapper> Borrow() override
        {
            auto mapper = TakeIdle();
            if (!mapper)
            {
                try
                {
                    mapper = std::make_unique<DataMapper>(_connectionString);
                }
                catch (...)
                {
                    auto const lock = std::scoped_lock { _mutex };
                    --_outstanding;
                    throw;
                }
            }
            detail::AdoptRelationLoadSource(*mapper, shared_from_this());
            // Should allocating the control block fail, the deleter still runs, so the mapper comes back.
            return { mapper.release(), [self = shared_from_this()](DataMapper* borrowed) noexcept {
                        self->GiveBack(std::unique_ptr<DataMapper> { borrowed });
                    } };
        }

      private:
        /// Counts the mapper about to be lent, and reserves room for it to come back: every lent mapper
        /// has an idle slot waiting, so @ref GiveBack - run from a deleter - never has to allocate.
        ///
        /// Idle connections the driver reports dead (the server restarted, the network dropped) are
        /// discarded rather than lent, the way the pool's validate-on-borrow does it; otherwise every
        /// later load through this source would fail on them for as long as its records live.
        [[nodiscard]] std::unique_ptr<DataMapper> TakeIdle()
        {
            auto dead = std::vector<std::unique_ptr<DataMapper>> {}; // disconnected after the lock is released
            auto const lock = std::scoped_lock { _mutex };
            _idle.reserve(_idle.size() + _outstanding + 1);
            ++_outstanding;
            while (!_idle.empty())
            {
                auto mapper = std::move(_idle.back());
                _idle.pop_back();
                if (mapper->Connection().IsAlive())
                    return mapper;
                dead.push_back(std::move(mapper));
            }
            return nullptr;
        }

        void GiveBack(std::unique_ptr<DataMapper> mapper) noexcept
        {
            detail::AdoptRelationLoadSource(*mapper, nullptr); // see the class comment
            auto const lock = std::scoped_lock { _mutex };
            --_outstanding;
            _idle.push_back(std::move(mapper)); // within the capacity TakeIdle() reserved
        }

        SqlConnectionString _connectionString;
        std::mutex _mutex;
        std::vector<std::unique_ptr<DataMapper>> _idle;
        std::size_t _outstanding {};
    };
} // namespace

std::shared_ptr<detail::RelationLoadSource> detail::PinToDefaultConnectionString(std::shared_ptr<RelationLoadSource> inner,
                                                                                 SqlConnectionString connectionString,
                                                                                 std::uint32_t generation)
{
    return std::make_shared<DefaultPinnedLoadSource>(std::move(inner), std::move(connectionString), generation);
}

std::shared_ptr<detail::RelationLoadSource> const& DataMapper::RelationLoadSourceForLoaders()
{
    if (!_relationLoadSource)
    {
        // Read before the string, as everywhere else: a switch in between can only make the pin look
        // stale early (one string comparison per load), never current late.
        auto const generation = SqlConnection::DefaultConnectionStringGeneration();
        if (_connection.ConnectionString() == SqlConnection::DefaultConnectionString())
            _relationLoadSource = detail::PinToDefaultConnectionString(
                GlobalPoolLoadSource::Instance(), _connection.ConnectionString(), generation);
        else
            _relationLoadSource = std::make_shared<ConnectionStringLoadSource>(_connection.ConnectionString());
    }
    return _relationLoadSource;
}

} // namespace Lightweight
