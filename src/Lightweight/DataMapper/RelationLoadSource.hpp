// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlConnectInfo.hpp"

#include <cstdint>
#include <memory>

namespace Lightweight
{

class DataMapper;

namespace detail
{
    /// Where the lazy relation loaders of a record obtain the data mapper they run their query on.
    ///
    /// Every data mapper carries one and hands it to the loaders it installs, which keep it alive by
    /// shared ownership. @c Borrow() is called from exactly one place: the loader lambdas that
    /// @c DataMapper::ConfigureRelationAutoLoading installs, when a relation is first touched.
    ///
    /// Implementations:
    /// - @c Pool::PoolRelationLoadSource (Pool.hpp) - attached by @c Pool::MakeEntry to every mapper the
    ///   pool creates, via @ref AdoptRelationLoadSource; borrows from that pool.
    /// - @c GlobalPoolLoadSource (DataMapper.cpp) - chosen by @c DataMapper::RelationLoadSourceForLoaders
    ///   for a plain mapper on the default connection string; forwards to @ref GlobalDataMapperPool.
    /// - @c ConnectionStringLoadSource (DataMapper.cpp) - chosen for a plain mapper on any other
    ///   connection string; reconnects with it.
    /// - @c DefaultPinnedLoadSource (DataMapper.cpp) - wraps the first two, see
    ///   @ref PinToDefaultConnectionString. A record may therefore outlive the mapper - or pool lease - it was read
    /// through, and still load its relations from the same database: a pooled mapper's source borrows
    /// from its pool, and a plain mapper's source reconnects with its connection string.
    class RelationLoadSource
    {
      public:
        RelationLoadSource() = default;
        RelationLoadSource(RelationLoadSource const&) = delete;
        RelationLoadSource(RelationLoadSource&&) = delete;
        RelationLoadSource& operator=(RelationLoadSource const&) = delete;
        RelationLoadSource& operator=(RelationLoadSource&&) = delete;
        virtual ~RelationLoadSource() = default;

        /// Borrows a data mapper for one load.
        ///
        /// Held exclusively until the last copy of the returned pointer is released, which gives the
        /// mapper back to where it came from (a pool, or this source), so no two loads ever share its
        /// connection or statement.
        ///
        /// @return The borrowed mapper; never null.
        /// @throws SqlException Connecting failed, when no idle connection was available.
        [[nodiscard]] virtual std::shared_ptr<DataMapper> Borrow() = 0;
    };

    /// Wraps @p inner, a source that follows the default connection string (a pool), so that it only
    /// serves records read while the default was @p connectionString.
    ///
    /// @param inner The default-following source to borrow from.
    /// @param connectionString The connection string the records' mapper was connected with.
    /// @param generation The @ref SqlConnection::DefaultConnectionStringGeneration that string was
    ///                   current under; lets an unchanged default skip the string comparison.
    /// @return A source whose @c Borrow() throws @ref SqlDefaultConnectionChangedError once the default
    ///         no longer is @p connectionString.
    [[nodiscard]] LIGHTWEIGHT_API std::shared_ptr<RelationLoadSource> PinToDefaultConnectionString(
        std::shared_ptr<RelationLoadSource> inner, SqlConnectionString connectionString, std::uint32_t generation);

    /// Makes @p source the relation-load source of @p dataMapper, for the records it reads from now on.
    ///
    /// Used by whatever owns a data mapper on behalf of others - a pool - so that records read through
    /// it load their relations from the same owner.
    ///
    /// @param dataMapper The mapper to configure.
    /// @param source The source its auto-loaders will borrow from; null restores the default choice.
    inline void AdoptRelationLoadSource(DataMapper& dataMapper, std::shared_ptr<RelationLoadSource> source) noexcept;
} // namespace detail

} // namespace Lightweight
