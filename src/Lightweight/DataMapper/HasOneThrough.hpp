// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "Error.hpp"

#include <compare>
#include <memory>
#include <type_traits>

namespace Lightweight
{

/// @brief Represents a one-to-one relationship through a join table.
///
/// The `OtherField` parameter is the field in the join table that references the other record.
/// The `ThroughField` parameter is the field in the join table that references the current record.
///
/// @ingroup DataMapper
template <typename OtherTable, typename ThroughTable>
class HasOneThrough
{
  public:
    /// The record type of the "through" side of the relationship.
    using ThroughRecord = ThroughTable;

    /// The record type of the "Other" side of the relationship.
    using ReferencedRecord = OtherTable;

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
    LIGHTWEIGHT_FORCE_INLINE void Unload() noexcept { _record = std::nullopt; }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord& operator*() noexcept { RequireLoaded(); return *_record; }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const& operator*() const noexcept { RequireLoaded(); return *_record; }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord* operator->() noexcept { RequireLoaded(); return &_record.get(); }

    /// @brief Retrieves the record in this relationship.
    /// @note On-demand loads the record if it is not already loaded.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr ReferencedRecord const* operator->() const noexcept { RequireLoaded(); return &_record.get(); }
    // clang-format on

    std::weak_ordering operator<=>(HasOneThrough const& other) const noexcept = default;

    struct Loader
    {
        std::function<void()> loadReference {};
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
            _loader.loadReference();

        if (!IsLoaded())
            throw SqlRequireLoadedError { Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>> };
    }

    Loader _loader {};

    // We use shared_ptr to not require ReferencedRecord to be declared before HasOneThrough.
    std::shared_ptr<ReferencedRecord> _record {};
};

namespace detail
{
    template <typename T>
    struct IsHasOneThrough: std::false_type
    {
    };

    template <typename OtherTable, typename ThroughTable>
    struct IsHasOneThrough<HasOneThrough<OtherTable, ThroughTable>>: std::true_type
    {
    };
} // namespace detail

template <typename T>
constexpr bool IsHasOneThrough = detail::IsHasOneThrough<std::remove_cvref_t<T>>::value;

} // namespace Lightweight
