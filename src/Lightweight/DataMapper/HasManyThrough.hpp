// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"
#include "Error.hpp"
#include "Record.hpp"

#include <reflection-cpp/reflection.hpp>

#include <compare>
#include <functional>
#include <memory>
#include <vector>

namespace Lightweight
{

/// @brief This API represents a many-to-many relationship between two records through a third record.
///
/// The join record must declare one `BelongsTo` pointing back at the record owning this relationship,
/// and one pointing at the referenced record. Both are located by matching the relationship *type*.
///
/// When the join record cannot be resolved that way - most notably a self-referential many-to-many,
/// where both of its foreign keys point at the *same* table - name the two foreign key columns
/// explicitly:
///
/// @code
/// struct Friendship;
/// struct Human
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     HasManyThrough<Human, Friendship, SqlRealName { "a_id" }, SqlRealName { "b_id" }> friends;
/// };
/// struct Friendship
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     BelongsTo<&Human::id, SqlRealName { "a_id" }> a;
///     BelongsTo<&Human::id, SqlRealName { "b_id" }> b;
/// };
/// @endcode
///
/// @tparam ReferencedRecordT The record type on the "many" side of the relationship.
/// @tparam ThroughRecordT The join record type.
/// @tparam TheOwnerSelector Singles out the join record's foreign key pointing at the *owning* record.
/// @tparam TheReferencedSelector Singles out the join record's foreign key pointing at @p ReferencedRecordT.
///
/// @see DataMapper, Field, HasMany, RelationSelector
/// @ingroup DataMapper
template <typename ReferencedRecordT,
          typename ThroughRecordT,
          auto TheOwnerSelector = AutoDetectRelation,
          auto TheReferencedSelector = AutoDetectRelation>
class HasManyThrough
{
    static_assert(RelationSelector<TheOwnerSelector> && RelationSelector<TheReferencedSelector>,
                  "The selector template arguments of HasManyThrough must be foreign key column names "
                  "(a SqlRealName) or std::nullopt to resolve the relationship automatically.");

  public:
    /// The record type of the "through" side of the relationship.
    using ThroughRecord = ThroughRecordT;

    /// The record type of the "many" side of the relationship.
    using ReferencedRecord = ReferencedRecordT;

    /// Singles out the join record's foreign key pointing at the record owning this relationship.
    static constexpr auto OwnerSelector = TheOwnerSelector;

    /// Singles out the join record's foreign key pointing at @ref ReferencedRecord.
    static constexpr auto ReferencedSelector = TheReferencedSelector;

    /// The list of records on the "many" side of the relationship.
    using ReferencedRecordList = std::vector<std::shared_ptr<ReferencedRecord>>;

    /// Value type for range-based iteration.
    using value_type = ReferencedRecord;
    /// Iterator type for the list of records.
    using iterator = ReferencedRecordList::iterator;
    /// Const iterator type for the list of records.
    using const_iterator = ReferencedRecordList::const_iterator;

    /// Retrieves the list of loaded records.
    [[nodiscard]] ReferencedRecordList const& All() const noexcept;

    /// Retrieves the list of records as mutable reference.
    [[nodiscard]] ReferencedRecordList& All() noexcept;

    /// Emplaces the given list of records into this relationship.
    ReferencedRecordList& Emplace(ReferencedRecordList&& records) noexcept;

    /// Retrieves the number of records in this relationship.
    [[nodiscard]] std::size_t Count() const;

    /// Checks if this relationship is empty.
    [[nodiscard]] bool IsEmpty() const;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will throw if the index is out of bounds.
    [[nodiscard]] ReferencedRecord const& At(std::size_t index) const;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will throw if the index is out of bounds.
    [[nodiscard]] ReferencedRecord& At(std::size_t index);

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will NOT throw if the index is out of bounds. The behaviour is undefined.
    [[nodiscard]] ReferencedRecord const& operator[](std::size_t index) const;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will NOT throw if the index is out of bounds. The behaviour is undefined.
    [[nodiscard]] ReferencedRecord& operator[](std::size_t index);

    /// Returns an iterator to the beginning of the record list.
    [[nodiscard]] iterator begin() noexcept;
    /// Returns an iterator to the end of the record list.
    [[nodiscard]] iterator end() noexcept;
    /// Returns a const iterator to the beginning of the record list.
    [[nodiscard]] const_iterator begin() const noexcept;
    /// Returns a const iterator to the end of the record list.
    [[nodiscard]] const_iterator end() const noexcept;

    /// Default three-way comparison operator.
    std::weak_ordering operator<=>(HasManyThrough const& other) const noexcept = default;

    struct Loader
    {
        std::function<size_t()> count;
        std::function<ReferencedRecordList()> all;
        std::function<void(std::function<void(ReferencedRecord const&)>)> each;
    };

    /// Used internally to configure on-demand loading of the records.
    void SetAutoLoader(Loader loader) noexcept
    {
        _loader = std::move(loader);
    }

    /// Reloads the records from the database.
    void Reload()
    {
        _count = std::nullopt;
        _records = std::nullopt;
        RequireLoaded();
    }

    /// @brief Iterates over all records in this relationship.
    ///
    /// @param callable The callable to invoke for each record.
    /// @note This method will on-demand load the records if they are not already loaded,
    ///       but not hold them all in memory.
    template <typename Callable>
    void Each(Callable const& callable)
    {
        if (!_records && _loader.each)
        {
            _loader.each(callable);
            return;
        }

        for (auto const& record: All())
            callable(*record);
    }

  private:
    void RequireLoaded()
    {
        if (_records)
            return;

        if (_loader.all)
            _records = _loader.all();

        if (!_records)
            throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>);
    }

    Loader _loader;

    std::optional<size_t> _count;
    std::optional<ReferencedRecordList> _records;
};

namespace detail
{
    template <typename T>
    struct IsHasManyThroughType: std::false_type
    {
    };

    template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
    struct IsHasManyThroughType<HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>>:
        std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsHasManyThrough = detail::IsHasManyThroughType<std::remove_cvref_t<T>>::value;

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecordList const&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::All() const noexcept
{
    const_cast<HasManyThrough*>(this)->RequireLoaded();

    return _records.value();
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecordList&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::All() noexcept
{
    RequireLoaded();

    return _records.value(); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecordList& HasManyThrough<
    ReferencedRecordT,
    ThroughRecordT,
    OwnerSelector,
    ReferencedSelector>::Emplace(ReferencedRecordList&& records) noexcept
{
    _records = { std::move(records) };
    _count = _records->size();
    return *_records;
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
std::size_t HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::Count() const
{
    if (_records)
        return _records->size();

    if (!_count)
        const_cast<HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>*>(this)->_count =
            _loader.count();

    return _count.value_or(0);
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
bool HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::IsEmpty() const
{
    return Count() == 0;
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecord const&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::At(std::size_t index) const
{
    return *All().at(index);
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecord&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::At(std::size_t index)
{
    return *All().at(index);
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecord const&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::operator[](std::size_t index) const
{
    return *All()[index];
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::ReferencedRecord&
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::operator[](std::size_t index)
{
    return *All()[index];
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::iterator
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::begin() noexcept
{
    return All().begin();
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::iterator
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::end() noexcept
{
    return All().end();
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::const_iterator
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::begin() const noexcept
{
    return All().begin();
}

template <typename ReferencedRecordT, typename ThroughRecordT, auto OwnerSelector, auto ReferencedSelector>
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::const_iterator
HasManyThrough<ReferencedRecordT, ThroughRecordT, OwnerSelector, ReferencedSelector>::end() const noexcept
{
    return All().end();
}

} // namespace Lightweight
