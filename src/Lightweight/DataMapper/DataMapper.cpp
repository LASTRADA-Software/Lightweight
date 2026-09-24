// SPDX-License-Identifier: Apache-2.0

#include "DataMapper.hpp"
#include "Pool.hpp"

#include <mutex>
#include <utility>
#include <vector>

namespace Lightweight
{

DataMapper& DataMapper::AcquireThreadLocal()
{
    thread_local auto instance = DataMapper { SqlConnection::DefaultConnectionString() };
    return instance;
}

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
            return GlobalDataMapperPool().RelationLoadSource()->Borrow();
        }

        /// @return The one instance, shared by every such mapper.
        [[nodiscard]] static std::shared_ptr<detail::RelationLoadSource> const& Instance()
        {
            static auto const instance =
                std::shared_ptr<detail::RelationLoadSource> { std::make_shared<GlobalPoolLoadSource>() };
            return instance;
        }
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
        [[nodiscard]] std::unique_ptr<DataMapper> TakeIdle()
        {
            auto const lock = std::scoped_lock { _mutex };
            _idle.reserve(_idle.size() + _outstanding + 1);
            ++_outstanding;
            if (_idle.empty())
                return nullptr;
            auto mapper = std::move(_idle.back());
            _idle.pop_back();
            return mapper;
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

std::shared_ptr<detail::RelationLoadSource> const& DataMapper::RelationLoadSourceForLoaders()
{
    if (!_relationLoadSource)
    {
        if (_connection.ConnectionString() == SqlConnection::DefaultConnectionString())
            _relationLoadSource = GlobalPoolLoadSource::Instance();
        else
            _relationLoadSource = std::make_shared<ConnectionStringLoadSource>(_connection.ConnectionString());
    }
    return _relationLoadSource;
}

} // namespace Lightweight
