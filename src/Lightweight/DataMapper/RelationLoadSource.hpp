// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

namespace Lightweight
{

class DataMapper;

namespace detail
{
    /// Where the lazy relation loaders of a record obtain the data mapper they run their query on.
    ///
    /// Every data mapper carries one and hands it to the loaders it installs, which keep it alive by
    /// shared ownership. A record may therefore outlive the mapper - or pool lease - it was read
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
