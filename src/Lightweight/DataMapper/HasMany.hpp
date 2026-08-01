// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../DataBinder/Core.hpp"
#include "../DataBinder/SqlNullValue.hpp"
#include "../SqlStatement.hpp"
#include "BelongsTo.hpp"
#include "Field.hpp"
#include "Record.hpp"

#include <reflection-cpp/reflection.hpp>

#include <compare>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace Lightweight
{

/// @brief This HasMany<OtherRecord> represents a simple one-to-many relationship between two records.
///
/// The HasMany<OtherRecord> is a member of the "one" side of the relationship.
///
/// `OtherRecord` must declare a `BelongsTo` member that points back to this "one" side. That member is
/// located by matching the relationship *type*, not by its position in either record, so the two
/// relationship members may be declared at any index. Declaring no such `BelongsTo` is a compile-time
/// error.
///
/// When `OtherRecord` holds more than one foreign key into this record's table - say a meeting that
/// references the same person table as both its organizer and its attendee - the inverse is ambiguous.
/// Name the foreign key column through @p TheInverseSelector to single one out:
///
/// @code
/// struct Meeting;
/// struct Human
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     HasMany<Meeting, SqlRealName { "organizer_id" }> organizedMeetings;
///     HasMany<Meeting, SqlRealName { "attendee_id" }> attendedMeetings;
/// };
/// struct Meeting
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     BelongsTo<&Human::id, SqlRealName { "organizer_id" }> organizer;
///     BelongsTo<&Human::id, SqlRealName { "attendee_id" }> attendee;
/// };
/// @endcode
///
/// @tparam OtherRecord The record type on the "many" side of the relationship.
/// @tparam TheInverseSelector Singles out one of several foreign keys, see @ref RelationSelector.
///
/// @see InverseBelongsToIndexOf, RelationSelector
///
/// @see DataMapper, Field, HasManyThrough
/// @ingroup DataMapper
template <typename OtherRecord, auto TheInverseSelector = AutoDetectRelation>
class HasMany
{
    static_assert(RelationSelector<TheInverseSelector>,
                  "The second template argument of HasMany must be a foreign key column name (a SqlRealName) "
                  "or std::nullopt to resolve the relationship automatically.");

  public:
    /// The record type of the "many" side of the relationship.
    using ReferencedRecord = OtherRecord;

    /// Singles out the foreign key of `OtherRecord` that backs this relationship.
    static constexpr auto InverseSelector = TheInverseSelector;

    /// The list of records on the "many" side of the relationship.
    using ReferencedRecordList = std::vector<std::shared_ptr<OtherRecord>>;

    /// Record type of the "many" side of the relationship.
    using value_type = OtherRecord;

    /// Iterator type for the list of records.
    using iterator = ReferencedRecordList::iterator;

    /// Const iterator type for the list of records.
    using const_iterator = ReferencedRecordList::const_iterator;

    /// Retrieves the list of loaded records.
    [[nodiscard]] ReferencedRecordList const& All() const noexcept;

    /// Retrieves the list of records as mutable reference.
    [[nodiscard]] ReferencedRecordList& All() noexcept;

    /// @brief Iterates over the list of records and calls the given callable for each record.
    ///
    /// @note Use this method if you want to iterate over all records but do not need to store them all in memory, e.g.
    ///       because the full data set wuold be too large.
    template <typename Callable>
    void Each(Callable const& callable);

    /// Emplaces the given list of records.
    ReferencedRecordList& Emplace(ReferencedRecordList&& records) noexcept;

    /// Retrieves the number of records in this 1-to-many relationship.
    [[nodiscard]] std::size_t Count() const noexcept;

    /// Checks if this 1-to-many relationship is empty.
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will throw if the index is out of bounds.
    [[nodiscard]] OtherRecord const& At(std::size_t index) const;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will throw if the index is out of bounds.
    [[nodiscard]] OtherRecord& At(std::size_t index);

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will NOT throw if the index is out of bounds. The behaviour is undefined.
    [[nodiscard]] OtherRecord const& operator[](std::size_t index) const;

    /// @brief Retrieves the record at the given index.
    ///
    /// @param index The index of the record to retrieve.
    /// @note This method will on-demand load the records if they are not already loaded.
    /// @note This method will NOT throw if the index is out of bounds. The behaviour is undefined.
    [[nodiscard]] OtherRecord& operator[](std::size_t index);

    /// Returns an iterator to the beginning of the record list.
    [[nodiscard]] iterator begin() noexcept;
    /// Returns an iterator to the end of the record list.
    [[nodiscard]] iterator end() noexcept;
    /// Returns a const iterator to the beginning of the record list.
    [[nodiscard]] const_iterator begin() const noexcept;
    /// Returns a const iterator to the end of the record list.
    [[nodiscard]] const_iterator end() const noexcept;

    /// Three-way comparison operator.
    constexpr std::weak_ordering operator<=>(HasMany const& other) const noexcept = default;
    /// Equality comparison operator.
    constexpr bool operator==(HasMany const& other) const noexcept = default;
    /// Inequality comparison operator.
    constexpr bool operator!=(HasMany const& other) const noexcept = default;

    struct Loader
    {
        std::function<size_t()> count {};
        std::function<ReferencedRecordList()> all {};
        std::function<void(std::function<void(ReferencedRecord const&)>)> each {};

        std::weak_ordering operator<=>(Loader const& /*other*/) const noexcept
        {
            return std::weak_ordering::equivalent; // Loader is not comparable, so we return equivalent
        }
    };

    /// Used internally to configure on-demand loading of the records.
    void SetAutoLoader(Loader loader) noexcept;

  private:
    void RequireLoaded();

    Loader _loader;
    std::optional<ReferencedRecordList> _records;
    std::optional<size_t> _count;
};

namespace detail
{
    template <typename T>
    struct IsHasManyType: std::false_type
    {
    };

    template <typename OtherRecord, auto InverseSelector>
    struct IsHasManyType<HasMany<OtherRecord, InverseSelector>>: std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsHasMany = detail::IsHasManyType<std::remove_cvref_t<T>>::value;

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE void HasMany<OtherRecord, InverseSelector>::SetAutoLoader(Loader loader) noexcept
{
    _loader = std::move(loader);
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE void HasMany<OtherRecord, InverseSelector>::RequireLoaded()
{
    if (!_records)
        _records = _loader.all();
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::ReferencedRecordList& HasMany<
    OtherRecord,
    InverseSelector>::Emplace(ReferencedRecordList&& records) noexcept
{
    _records = { std::move(records) };
    return *_records;
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::ReferencedRecordList& HasMany<
    OtherRecord,
    InverseSelector>::All() noexcept
{
    RequireLoaded();
    return *_records; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
template <typename Callable>
void HasMany<OtherRecord, InverseSelector>::Each(Callable const& callable)
{
    if (!_records && _loader.each)
    {
        _loader.each(callable);
        return;
    }

    for (auto const& record: All())
        callable(*record);
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::ReferencedRecordList const& HasMany<
    OtherRecord,
    InverseSelector>::All() const noexcept
{
    RequireLoaded();
    return *_records;
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE std::size_t HasMany<OtherRecord, InverseSelector>::Count() const noexcept
{
    if (_records)
        return _records->size();

    if (!_count && _loader.count)
        const_cast<HasMany<OtherRecord, InverseSelector>*>(this)->_count = _loader.count();

    return _count.value_or(0);
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE bool HasMany<OtherRecord, InverseSelector>::IsEmpty() const noexcept
{
    return Count() == 0;
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord, InverseSelector>::At(std::size_t index) const
{
    RequireLoaded();
    return *_records->at(index); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord, InverseSelector>::At(std::size_t index)
{
    RequireLoaded();
    return *_records->at(index); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord, InverseSelector>::operator[](std::size_t index) const
{
    RequireLoaded();
    return *(*_records)[index]; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord, InverseSelector>::operator[](std::size_t index)
{
    RequireLoaded();
    return *(*_records)[index]; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::iterator HasMany<OtherRecord,
                                                                                        InverseSelector>::begin() noexcept
{
    RequireLoaded();
    if (_records)
        return _records->begin();
    else
        return iterator {};
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::iterator HasMany<OtherRecord,
                                                                                        InverseSelector>::end() noexcept
{
    RequireLoaded();
    if (_records)
        return _records->end();
    else
        return iterator {};
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::const_iterator HasMany<OtherRecord,
                                                                                              InverseSelector>::begin()
    const noexcept
{
    RequireLoaded();
    if (_records)
        return _records->begin();
    else
        return const_iterator {};
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::const_iterator HasMany<OtherRecord,
                                                                                              InverseSelector>::end()
    const noexcept
{
    RequireLoaded();
    if (_records)
        return _records->end();
    else
        return const_iterator {};
}

} // namespace Lightweight
