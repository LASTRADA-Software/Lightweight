// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "Error.hpp"
#include "Record.hpp"

#include <compare>
#include <memory>
#include <type_traits>

namespace Lightweight
{

/// @brief Represents a one-to-one relationship through a join table.
///
/// The `OtherTable` parameter is the record reached through the join table.
/// The `ThroughTable` parameter is the join table, which references the current record.
///
/// Both foreign keys are located by matching the relationship *type*. When either record holds more
/// than one foreign key into the same table, name the column to single one out - see
/// @ref RelationSelector and the example on @ref HasManyThrough.
///
/// @tparam OtherTable The record type reached through the join table.
/// @tparam ThroughTable The join record type, holding a foreign key back to the owning record.
/// @tparam TheOwnerSelector Singles out the join record's foreign key pointing at the *owning* record.
/// @tparam TheThroughSelector Singles out @p OtherTable's foreign key pointing at @p ThroughTable.
///
/// @see DataMapper, Field, HasManyThrough, RelationSelector
/// @ingroup DataMapper
template <typename OtherTable,
          typename ThroughTable,
          auto TheOwnerSelector = AutoDetectRelation,
          auto TheThroughSelector = AutoDetectRelation>
class HasOneThrough
{
    static_assert(RelationSelector<TheOwnerSelector> && RelationSelector<TheThroughSelector>,
                  "The selector template arguments of HasOneThrough must be foreign key column names "
                  "(a SqlRealName) or std::nullopt to resolve the relationship automatically.");

  public:
    /// The record type of the "through" side of the relationship.
    using ThroughRecord = ThroughTable;

    /// The record type of the "Other" side of the relationship.
    using ReferencedRecord = OtherTable;

    /// Singles out the join record's foreign key pointing at the record owning this relationship.
    static constexpr auto OwnerSelector = TheOwnerSelector;

    /// Singles out @ref ReferencedRecord's foreign key pointing at @ref ThroughRecord.
    static constexpr auto ThroughSelector = TheThroughSelector;

    // clang-format off

    /// Emplaces the given record into this relationship.
    LIGHTWEIGHT_FORCE_INLINE constexpr void EmplaceRecord(std::shared_ptr<ReferencedRecord> record) { _record = std::move(record); }

    /// Retrieves the record in this relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& Record() noexcept { RequireLoaded(); return *_record.get(); }

    /// Retrieves the record in this relationship.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const& Record() const noexcept { RequireLoaded(); return *_record.get(); }

    /// Checks if the record is loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool IsLoaded() const noexcept { return _record.get() != nullptr; }

    /// Unloads the record from memory.
    LIGHTWEIGHT_FORCE_INLINE void Unload() noexcept { _record.reset(); }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& operator*() noexcept { RequireLoaded(); return *_record; }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const& operator*() const noexcept { RequireLoaded(); return *_record; }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord* operator->() noexcept { RequireLoaded(); return _record.get(); }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const* operator->() const noexcept { RequireLoaded(); return _record.get(); }
    // clang-format on

    /// Default three-way comparison operator.
    std::weak_ordering operator<=>(HasOneThrough const& other) const noexcept = default;

    struct Loader
    {
        std::function<std::shared_ptr<ReferencedRecord>()> loadReference {};
    };

    /// Used internally to configure on-demand loading of the record.
    void SetAutoLoader(Loader loader)
    {
        _loader = std::move(loader);
    }

  private:
    void RequireLoaded() const
    {
        if (IsLoaded())
            return;

        if (_loader.loadReference)
            _record = _loader.loadReference();

        if (!IsLoaded())
            throw SqlRequireLoadedError { Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>> };
    }

    Loader _loader {};

    // We use shared_ptr to not require ReferencedRecord to be declared before HasOneThrough.
    mutable std::shared_ptr<ReferencedRecord> _record {};
};

namespace detail
{
    template <typename T>
    struct IsHasOneThrough: std::false_type
    {
    };

    template <typename OtherTable, typename ThroughTable, auto OwnerSelector, auto ThroughSelector>
    struct IsHasOneThrough<HasOneThrough<OtherTable, ThroughTable, OwnerSelector, ThroughSelector>>: std::true_type
    {
    };
} // namespace detail

template <typename T>
constexpr bool IsHasOneThrough = detail::IsHasOneThrough<std::remove_cvref_t<T>>::value;

} // namespace Lightweight
