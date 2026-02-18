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
/// This implemenation of `HasMany<OtherRecord>` must have only one `BelongsTo` member
/// that points back to this "one" side.
///
/// @see DataMapper, Field, HasManyThrough
/// @ingroup DataMapper
template <typename OtherRecord>
class HasMany
{
  public:
    /// The record type of the "many" side of the relationship.
    using ReferencedRecord = OtherRecord;

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

template <typename T>
constexpr bool IsHasMany = IsSpecializationOf<HasMany, T>;

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE void HasMany<OtherRecord>::SetAutoLoader(Loader loader) noexcept
{
    _loader = std::move(loader);
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE void HasMany<OtherRecord>::RequireLoaded()
{
    if (!_records)
        _records = _loader.all();
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::ReferencedRecordList& HasMany<OtherRecord>::Emplace(
    ReferencedRecordList&& records) noexcept
{
    _records = { std::move(records) };
    return *_records;
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::ReferencedRecordList& HasMany<OtherRecord>::All() noexcept
{
    RequireLoaded();
    return *_records; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord>
template <typename Callable>
void HasMany<OtherRecord>::Each(Callable const& callable)
{
    if (!_records && _loader.each)
    {
        _loader.each(callable);
        return;
    }

    for (auto const& record: All())
        callable(*record);
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::ReferencedRecordList const& HasMany<OtherRecord>::All() const noexcept
{
    RequireLoaded();
    return *_records;
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE std::size_t HasMany<OtherRecord>::Count() const noexcept
{
    if (_records)
        return _records->size();

    if (!_count && _loader.count)
        const_cast<HasMany<OtherRecord>*>(this)->_count = _loader.count();

    return _count.value_or(0);
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE bool HasMany<OtherRecord>::IsEmpty() const noexcept
{
    return Count() == 0;
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord>::At(std::size_t index) const
{
    RequireLoaded();
    return *_records->at(index); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord>::At(std::size_t index)
{
    RequireLoaded();
    return *_records->at(index); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord>::operator[](std::size_t index) const
{
    RequireLoaded();
    return *(*_records)[index]; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord>::operator[](std::size_t index)
{
    RequireLoaded();
    return *(*_records)[index]; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::iterator HasMany<OtherRecord>::begin() noexcept
{
    RequireLoaded();
    if (_records)
        return _records->begin();
    else
        return iterator {};
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::iterator HasMany<OtherRecord>::end() noexcept
{
    RequireLoaded();
    if (_records)
        return _records->end();
    else
        return iterator {};
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::const_iterator HasMany<OtherRecord>::begin() const noexcept
{
    RequireLoaded();
    if (_records)
        return _records->begin();
    else
        return const_iterator {};
}

template <typename OtherRecord>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord>::const_iterator HasMany<OtherRecord>::end() const noexcept
{
    RequireLoaded();
    if (_records)
        return _records->end();
    else
        return const_iterator {};
}

} // namespace Lightweight
