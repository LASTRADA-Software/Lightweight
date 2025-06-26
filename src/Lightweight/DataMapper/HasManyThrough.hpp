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

/// @brief This API represents a many-to-many relationship between two records through a third record.
///
/// @see DataMapper, Field, HasMany
/// @ingroup DataMapper
template <typename ReferencedRecordT, typename ThroughRecordT>
class HasManyThrough
{
  public:
    /// The record type of the "through" side of the relationship.
    using ThroughRecord = ThroughRecordT;

    /// The record type of the "many" side of the relationship.
    using ReferencedRecord = ReferencedRecordT;

    /// The list of records on the "many" side of the relationship.
    using ReferencedRecordList = std::vector<std::shared_ptr<ReferencedRecord>>;

    using value_type = ReferencedRecord;
    using iterator = typename ReferencedRecordList::iterator;
    using const_iterator = typename ReferencedRecordList::const_iterator;

    /// Retrieves the list of loaded records.
    [[nodiscard]] ReferencedRecordList const& All() const noexcept;

    /// Retrieves the list of records as mutable reference.
    [[nodiscard]] ReferencedRecordList& All() noexcept;

    /// Emplaces the given list of records into this relationship.
    ReferencedRecordList& Emplace(ReferencedRecordList&& records) noexcept;

    /// Retrieves the number of records in this relationship.
    [[nodiscard]] std::size_t Count() const;

    /// Checks if this relationship is empty.
    [[nodiscard]] std::size_t IsEmpty() const;

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

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    std::weak_ordering operator<=>(HasManyThrough const& other) const noexcept = default;

    struct Loader
    {
        std::function<size_t()> count;
        std::function<void()> all;
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
            _loader.all();

        if (!_records)
            throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>);
    }

    Loader _loader;

    std::optional<size_t> _count;
    std::optional<ReferencedRecordList> _records;
};

template <typename T>
constexpr bool IsHasManyThrough = IsSpecializationOf<HasManyThrough, T>;

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecordList const& HasManyThrough<ReferencedRecordT,
                                                                                              ThroughRecordT>::All()
    const noexcept
{
    const_cast<HasManyThrough*>(this)->RequireLoaded();

    return _records.value();
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecordList& HasManyThrough<ReferencedRecordT,
                                                                                        ThroughRecordT>::All() noexcept
{
    RequireLoaded();

    return _records.value(); // NOLINT(bugprone-unchecked-optional-access)
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecordList& HasManyThrough<ReferencedRecordT, ThroughRecordT>::
    Emplace(ReferencedRecordList&& records) noexcept
{
    _records = { std::move(records) };
    _count = _records->size();
    return *_records;
}

template <typename ReferencedRecordT, typename ThroughRecordT>
std::size_t HasManyThrough<ReferencedRecordT, ThroughRecordT>::Count() const
{
    if (_records)
        return _records->size();

    if (!_count)
        const_cast<HasManyThrough<ReferencedRecordT, ThroughRecordT>*>(this)->_count = _loader.count();

    return _count.value_or(0);
}

template <typename ReferencedRecordT, typename ThroughRecordT>
std::size_t HasManyThrough<ReferencedRecordT, ThroughRecordT>::IsEmpty() const
{
    return Count() == 0;
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecord const& HasManyThrough<
    ReferencedRecordT,
    ThroughRecordT>::At(std::size_t index) const
{
    return *All().at(index);
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecord& HasManyThrough<ReferencedRecordT, ThroughRecordT>::At(
    std::size_t index)
{
    return *All().at(index);
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecord const& HasManyThrough<
    ReferencedRecordT,
    ThroughRecordT>::operator[](std::size_t index) const
{
    return *All()[index];
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::ReferencedRecord& HasManyThrough<ReferencedRecordT, ThroughRecordT>::
operator[](std::size_t index)
{
    return *All()[index];
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::iterator HasManyThrough<ReferencedRecordT,
                                                                           ThroughRecordT>::begin() noexcept
{
    return All().begin();
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::iterator HasManyThrough<ReferencedRecordT, ThroughRecordT>::end() noexcept
{
    return All().end();
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::const_iterator HasManyThrough<ReferencedRecordT, ThroughRecordT>::begin()
    const noexcept
{
    return All().begin();
}

template <typename ReferencedRecordT, typename ThroughRecordT>
HasManyThrough<ReferencedRecordT, ThroughRecordT>::const_iterator HasManyThrough<ReferencedRecordT, ThroughRecordT>::end()
    const noexcept
{
    return All().end();
}
