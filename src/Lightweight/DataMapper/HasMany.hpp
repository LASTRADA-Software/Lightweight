// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../DataBinder/Core.hpp"
#include "../DataBinder/SqlNullValue.hpp"
#include "../SqlStatement.hpp"
#include "BelongsTo.hpp"
#include "Error.hpp"
#include "Field.hpp"
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
/// references the same person table both as its organizer and as whoever writes the minutes - the
/// inverse is ambiguous. Name the foreign key column through @p TheInverseSelector to single one out:
///
/// @code
/// struct Meeting;
/// struct Human
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     HasMany<Meeting, SqlRealName { "organizer_id" }> organizedMeetings;
///     HasMany<Meeting, SqlRealName { "minute_taker_id" }> minutedMeetings;
/// };
/// struct Meeting
/// {
///     Field<int, PrimaryKey::AutoAssign> id;
///     BelongsTo<&Human::id, SqlRealName { "organizer_id" }> organizer;
///     BelongsTo<&Human::id, SqlRealName { "minute_taker_id" }, SqlNullable::Null> minuteTaker;
/// };
/// @endcode
///
/// A meeting with *many* attendees is a many-to-many instead - see `HasManyThrough`. The worked
/// example in `docs/sql-to-lightweight.md` combines both shapes.
///
/// @tparam OtherRecord The record type on the "many" side of the relationship.
/// @tparam TheInverseSelector Singles out one of several foreign keys, see the RelationSelector concept.
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

    /// @brief Retrieves the records, loading them on first access.
    ///
    /// Never throws for unavailable records: the reason comes back as the error instead.
    ///
    /// @return The records; or @ref RelationError::NotConfigured when there is no loader (a hand-built
    ///         record, or one read with `loadRelations = false`), @ref RelationError::Outdated when the
    ///         default connection string changed since the record was read, @ref RelationError::QueryFailed
    ///         when the load query failed.
    [[nodiscard]] RelationResult<std::reference_wrapper<ReferencedRecordList const>> All() const;

    /// @copydoc All() const
    [[nodiscard]] RelationResult<std::reference_wrapper<ReferencedRecordList>> All();

    /// @brief Calls @p callable for each record, without holding them all in memory when not loaded yet.
    ///
    /// Use this to iterate over all records when the full data set would be too large to keep.
    /// An exception thrown by @p callable propagates unchanged.
    ///
    /// @param callable Called once per record.
    /// @return Nothing, or why the records are unavailable (see @ref All()).
    template <typename Callable>
    RelationResult<void> Each(Callable const& callable);

    /// Emplaces the given list of records.
    ReferencedRecordList& Emplace(ReferencedRecordList&& records) noexcept;

    /// @brief Returns the already-loaded records, or `nullptr` when the relation is not loaded.
    ///
    /// Unlike `All()`, this never runs the on-demand loader: it reports what is present right now,
    /// which is what lets the batched relation loading walk one level deeper (`With<A, B>()`).
    ///
    /// @return Pointer to the loaded list, or `nullptr` if the relation was never loaded.
    [[nodiscard]] ReferencedRecordList* LoadedRecords() noexcept
    {
        return _records ? &*_records : nullptr;
    }

    /// @return The number of records in this 1-to-many relationship - counted by a query, without
    ///         loading them, unless they are loaded already - or why it is unavailable (see @ref All()).
    [[nodiscard]] RelationResult<std::size_t> Count() const;

    /// @return Whether this 1-to-many relationship is empty, or why that is unknown (see @ref All()).
    [[nodiscard]] RelationResult<bool> IsEmpty() const;

    /// @brief Retrieves the record at the given index, loading the records on first access.
    ///
    /// @param index The index of the record to retrieve.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    /// @throws std::out_of_range @p index is out of bounds.
    [[nodiscard]] OtherRecord const& At(std::size_t index) const;

    /// @copydoc At(std::size_t) const
    [[nodiscard]] OtherRecord& At(std::size_t index);

    /// @brief Retrieves the record at the given index, loading the records on first access.
    ///
    /// @param index The index of the record to retrieve; out of bounds is undefined behaviour.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] OtherRecord const& operator[](std::size_t index) const;

    /// @copydoc operator[](std::size_t) const
    [[nodiscard]] OtherRecord& operator[](std::size_t index);

    /// Returns an iterator to the beginning of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] iterator begin();
    /// Returns an iterator to the end of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] iterator end();
    /// Returns a const iterator to the beginning of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] const_iterator begin() const;
    /// Returns a const iterator to the end of the record list, loading the records on first access.
    /// @throws SqlRequireLoadedError The records are unavailable; @ref All() reports why without throwing.
    [[nodiscard]] const_iterator end() const;

    /// Three-way comparison operator.
    constexpr std::weak_ordering operator<=>(HasMany const& other) const noexcept = default;
    /// Equality comparison operator.
    constexpr bool operator==(HasMany const& other) const noexcept = default;
    /// Inequality comparison operator.
    constexpr bool operator!=(HasMany const& other) const noexcept = default;

    /// Carries the deferred loads, installed by the DataMapper.
    struct Loader
    {
        /// Counts the records without loading them.
        std::function<RelationResult<size_t>()> count {};
        /// Loads all records.
        std::function<RelationResult<ReferencedRecordList>()> all {};
        /// Streams the records to a callback.
        std::function<RelationResult<void>(std::function<void(ReferencedRecord const&)>)> each {};

        /// Loaders carry no comparable state of their own, so any two are considered equivalent.
        std::weak_ordering operator<=>(Loader const& /*other*/) const noexcept
        {
            return std::weak_ordering::equivalent; // Loader is not comparable, so we return equivalent
        }
    };

    /// Used internally to configure on-demand loading of the records.
    void SetAutoLoader(Loader loader) noexcept;

  private:
    /// Loads the records unless they already are, or their unavailability is already known.
    ///
    /// @ref RelationError::Outdated is remembered - it does not change by asking again - until records
    /// are emplaced; a failed query is retried on the next access.
    [[nodiscard]] RelationResult<void> Load() const;

    /// @return The loaded records, or throws SqlRequireLoadedError.
    [[nodiscard]] ReferencedRecordList& LoadOrThrow() const;

    Loader _loader;
    mutable std::optional<ReferencedRecordList> _records;
    mutable std::optional<size_t> _count;
    mutable std::optional<RelationError> _loadError;
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
inline LIGHTWEIGHT_FORCE_INLINE RelationResult<void> HasMany<OtherRecord, InverseSelector>::Load() const
{
    if (_records)
        return {};
    if (_loadError)
        return std::unexpected { *_loadError };

    // The loader is only populated by ConfigureRelationAutoLoading(). A hand-constructed record
    // never went through it, so calling the empty std::function would be std::bad_function_call.
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

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::ReferencedRecordList& HasMany<
    OtherRecord,
    InverseSelector>::LoadOrThrow() const
{
    if (auto const loaded = Load(); !loaded)
        throw SqlRequireLoadedError(Reflection::TypeNameOf<std::remove_cvref_t<decltype(*this)>>, loaded.error());
    return *_records; // NOLINT(bugprone-unchecked-optional-access)
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::ReferencedRecordList& HasMany<
    OtherRecord,
    InverseSelector>::Emplace(ReferencedRecordList&& records) noexcept
{
    _records = { std::move(records) };
    _loadError.reset();
    return *_records;
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE auto HasMany<OtherRecord, InverseSelector>::All()
    -> RelationResult<std::reference_wrapper<ReferencedRecordList>>
{
    return Load().transform([this] { return std::reference_wrapper<ReferencedRecordList> { *_records }; });
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE auto HasMany<OtherRecord, InverseSelector>::All() const
    -> RelationResult<std::reference_wrapper<ReferencedRecordList const>>
{
    return Load().transform([this] { return std::reference_wrapper<ReferencedRecordList const> { *_records }; });
}

template <typename OtherRecord, auto InverseSelector>
template <typename Callable>
RelationResult<void> HasMany<OtherRecord, InverseSelector>::Each(Callable const& callable)
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

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE RelationResult<std::size_t> HasMany<OtherRecord, InverseSelector>::Count() const
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

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE RelationResult<bool> HasMany<OtherRecord, InverseSelector>::IsEmpty() const
{
    return Count().transform([](std::size_t count) { return count == 0; });
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord, InverseSelector>::At(std::size_t index) const
{
    return *LoadOrThrow().at(index);
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord, InverseSelector>::At(std::size_t index)
{
    return *LoadOrThrow().at(index);
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord const& HasMany<OtherRecord, InverseSelector>::operator[](std::size_t index) const
{
    return *LoadOrThrow()[index];
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE OtherRecord& HasMany<OtherRecord, InverseSelector>::operator[](std::size_t index)
{
    return *LoadOrThrow()[index];
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::iterator HasMany<OtherRecord,
                                                                                        InverseSelector>::begin()
{
    return LoadOrThrow().begin();
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::iterator HasMany<OtherRecord, InverseSelector>::end()
{
    return LoadOrThrow().end();
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::const_iterator HasMany<OtherRecord,
                                                                                              InverseSelector>::begin() const
{
    return std::as_const(LoadOrThrow()).begin();
}

template <typename OtherRecord, auto InverseSelector>
inline LIGHTWEIGHT_FORCE_INLINE HasMany<OtherRecord, InverseSelector>::const_iterator HasMany<OtherRecord,
                                                                                              InverseSelector>::end() const
{
    return std::as_const(LoadOrThrow()).end();
}

} // namespace Lightweight
