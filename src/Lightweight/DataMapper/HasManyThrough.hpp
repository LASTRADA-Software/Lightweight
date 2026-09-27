// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"
#include "Error.hpp"
#include "Record.hpp"

#include <reflection-cpp/reflection.hpp>

#include <compare>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Lightweight
{

/// @brief This API represents a many-to-many relationship between two records through a third record.
///
/// The join record is named with the @ref Through marker, so that the reader can tell it apart from
/// the referenced record at a glance:
///
/// @code
/// struct Friendship;
/// struct Human
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     HasManyThrough<Human, Through<Friendship>> friends;
/// };
/// @endcode
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
///     HasManyThrough<Human, Through<Friendship>, SqlRealName { "a_id" }, SqlRealName { "b_id" }> friends;
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
/// @tparam ThroughSpec The join record, wrapped as `Through<T>`. Naming the record bare is deprecated.
/// @tparam TheOwnerSelector Singles out the join record's foreign key pointing at the *owning* record.
/// @tparam TheReferencedSelector Singles out the join record's foreign key pointing at @p ReferencedRecordT.
///
/// @see DataMapper, Field, HasMany, Through, RelationSelector
/// @ingroup DataMapper
template <typename ReferencedRecordT,
          typename ThroughSpec,
          auto TheOwnerSelector = AutoDetectRelation,
          auto TheReferencedSelector = AutoDetectRelation>
class HasManyThrough
{
    static_assert(RelationSelector<TheOwnerSelector> && RelationSelector<TheReferencedSelector>,
                  "The selector template arguments of HasManyThrough must be foreign key column names "
                  "(a SqlRealName) or std::nullopt to resolve the relationship automatically.");

    static_assert(!IsThrough<ReferencedRecordT>,
                  "The referenced record of HasManyThrough must not be wrapped in Through<>, "
                  "only the join record is.");

  public:
    /// The record type of the "through" side of the relationship.
    using ThroughRecord = ThroughRecordOf<ThroughSpec>;

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

    /// @brief Retrieves the records, loading them on first access.
    ///
    /// Never throws for unavailable records: the reason comes back as the error instead.
    ///
    /// @return The records; or @ref RelationError::NotConfigured when there is no loader (a hand-built
    ///         record, or one read with `loadRelations = false`), @ref RelationError::Outdated when the
    ///         default connection string changed since the record was read, @ref RelationError::QueryFailed
    ///         when the load query failed.
    [[nodiscard]] RelationResult<std::reference_wrapper<ReferencedRecordList const>> All() const
    {
        return Load().transform([this] { return std::reference_wrapper<ReferencedRecordList const> { *_records }; });
    }

    /// @copydoc All() const
    [[nodiscard]] RelationResult<std::reference_wrapper<ReferencedRecordList>> All()
    {
        return Load().transform([this] { return std::reference_wrapper<ReferencedRecordList> { *_records }; });
    }

    /// Emplaces the given list of records into this relationship.
    ReferencedRecordList& Emplace(ReferencedRecordList&& records) noexcept
    {
        _records = { std::move(records) };
        _count = _records->size();
        _loadError.reset();
        return *_records;
    }

    /// @return The number of records in this relationship - counted by a query, without loading them,
    ///         unless they are loaded already - or why it is unavailable (see @ref All()).
    [[nodiscard]] RelationResult<std::size_t> Count() const
    {
        if (_records)
            return _records->size();
        if (_count)
            return *_count;
        if (_loadError)
            return std::unexpected { *_loadError };
        if (!_loader.count)
            return std::unexpected { RelationError::NotConfigured };

        auto counted = _loader.count();
        if (counted)
            _count = *counted;
        else if (counted.error() != RelationError::QueryFailed)
            _loadError = counted.error();
        return counted;
    }

    /// @return Whether this relationship is empty, or why that is unknown (see @ref All()).
    [[nodiscard]] RelationResult<bool> IsEmpty() const
    {
        return Count().transform([](std::size_t count) { return count == 0; });
    }

    /// @brief Retrieves the record at the given index, loading the records on first access.
    ///
    /// @param index The index of the record to retrieve.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    /// @throws std::out_of_range @p index is out of bounds.
    [[nodiscard]] ReferencedRecord const& At(std::size_t index) const
    {
        return *LoadOrThrow().at(index);
    }

    /// @copydoc At(std::size_t) const
    [[nodiscard]] ReferencedRecord& At(std::size_t index)
    {
        return *LoadOrThrow().at(index);
    }

    /// @brief Retrieves the record at the given index, loading the records on first access.
    ///
    /// @param index The index of the record to retrieve; out of bounds is undefined behaviour.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] ReferencedRecord const& operator[](std::size_t index) const
    {
        return *LoadOrThrow()[index];
    }

    /// @copydoc operator[](std::size_t) const
    [[nodiscard]] ReferencedRecord& operator[](std::size_t index)
    {
        return *LoadOrThrow()[index];
    }

    /// Returns an iterator to the beginning of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] iterator begin()
    {
        return LoadOrThrow().begin();
    }

    /// Returns an iterator to the end of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] iterator end()
    {
        return LoadOrThrow().end();
    }

    /// Returns a const iterator to the beginning of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] const_iterator begin() const
    {
        return std::as_const(LoadOrThrow()).begin();
    }

    /// Returns a const iterator to the end of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] const_iterator end() const
    {
        return std::as_const(LoadOrThrow()).end();
    }

    /// Default three-way comparison operator.
    std::weak_ordering operator<=>(HasManyThrough const& other) const noexcept = default;

    /// Carries the deferred loads, installed by the DataMapper.
    struct Loader
    {
        /// Counts the records without loading them.
        std::function<RelationResult<size_t>()> count;
        /// Loads all records.
        std::function<RelationResult<ReferencedRecordList>()> all;
        /// Streams the records to a callback.
        std::function<RelationResult<void>(std::function<void(ReferencedRecord const&)>)> each;
    };

    /// Used internally to configure on-demand loading of the records.
    void SetAutoLoader(Loader loader) noexcept
    {
        _loader = std::move(loader);
    }

    /// @brief Loads the records from the database again, forgetting what was loaded or known before.
    /// @return Nothing, or why the records are unavailable (see @ref All()).
    RelationResult<void> Reload()
    {
        _count = std::nullopt;
        _records = std::nullopt;
        _loadError.reset();
        return Load();
    }

    /// @brief Calls @p callable for each record, without holding them all in memory when not loaded yet.
    ///
    /// An exception thrown by @p callable propagates unchanged.
    ///
    /// @param callable Called once per record.
    /// @return Nothing, or why the records are unavailable (see @ref All()).
    template <typename Callable>
    RelationResult<void> Each(Callable const& callable)
    {
        if (!_records && !_loadError && _loader.each)
        {
            auto streamed = _loader.each(callable);
            if (!streamed && streamed.error() != RelationError::QueryFailed)
                _loadError = streamed.error();
            return streamed;
        }

        return All().transform([&callable](ReferencedRecordList const& records) {
            for (auto const& record: records)
                callable(*record);
        });
    }

  private:
    /// Loads the records unless they already are, or their unavailability is already known.
    ///
    /// @ref RelationError::Outdated is remembered - it does not change by asking again - until records
    /// are emplaced or reloaded; a failed query is retried on the next access.
    [[nodiscard]] RelationResult<void> Load() const
    {
        if (_records)
            return {};
        if (_loadError)
            return std::unexpected { *_loadError };
        if (!_loader.all)
            return std::unexpected { RelationError::NotConfigured };

        auto loaded = _loader.all();
        if (!loaded)
        {
            if (loaded.error() != RelationError::QueryFailed)
                _loadError = loaded.error();
            return std::unexpected { loaded.error() };
        }
        _records = std::move(*loaded);
        return {};
    }

    /// @return The loaded records, or throws SqlRequireLoadedError.
    [[nodiscard]] ReferencedRecordList& LoadOrThrow() const
    {
        if (auto const loaded = Load(); !loaded)
            throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>, loaded.error());
        return *_records; // NOLINT(bugprone-unchecked-optional-access)
    }

    Loader _loader;

    mutable std::optional<size_t> _count;
    mutable std::optional<ReferencedRecordList> _records;
    mutable std::optional<RelationError> _loadError;
};

namespace detail
{
    template <typename T>
    struct IsHasManyThroughType: std::false_type
    {
    };

    template <typename ReferencedRecordT, typename ThroughSpec, auto OwnerSelector, auto ReferencedSelector>
    struct IsHasManyThroughType<HasManyThrough<ReferencedRecordT, ThroughSpec, OwnerSelector, ReferencedSelector>>:
        std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsHasManyThrough = detail::IsHasManyThroughType<std::remove_cvref_t<T>>::value;

} // namespace Lightweight
