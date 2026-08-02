// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"
#include "BelongsTo.hpp"
#include "Field.hpp"

#include <reflection-cpp/reflection.hpp>

#include <concepts>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>

namespace Lightweight
{

/// @brief Represents a sequence of indexes that can be used alongside Query() to retrieve only part of the record.
///
/// @ingroup DataMapper
template <size_t... Ints>
using SqlElements = std::integer_sequence<size_t, Ints...>;

namespace detail
{
    // Helper trait to detect specializations of SqlElements
    template <typename T>
    struct IsSqlElements: std::false_type
    {
    };

    template <size_t... Ints>
    struct IsSqlElements<SqlElements<Ints...>>: std::true_type
    {
    };
} // namespace detail

// @brief Helper concept to check if a type is not a specialization of SqlElements
template <typename T>
concept NotSqlElements = !detail::IsSqlElements<T>::value;

/// @brief Represents a record type that can be used with the DataMapper.
///
/// The record type must be an aggregate type.
///
/// @see DataMapper, Field, BelongsTo, HasMany, HasManyThrough, HasOneThrough
/// @ingroup DataMapper
template <typename Record>
concept DataMapperRecord = std::is_aggregate_v<Record> && NotSqlElements<Record>;

template <typename... Records>
concept DataMapperRecords = (DataMapperRecord<Records> && ...);

namespace detail
{

    template <std::size_t I, typename Record>
    constexpr std::optional<size_t> FindPrimaryKeyIndex()
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
        if constexpr (I < RecordMemberCount<Record>)
        {
            if constexpr (IsPrimaryKey<RecordMemberTypeOf<I, Record>>)
                return { I };
            else
                return FindPrimaryKeyIndex<I + 1, Record>();
        }
        return std::nullopt;
    }

} // namespace detail

/// Declare RecordPrimaryKeyIndex<Record> to retrieve the primary key index of the given record.
template <typename Record>
constexpr size_t RecordPrimaryKeyIndex =
    detail::FindPrimaryKeyIndex<0, Record>().value_or((std::numeric_limits<size_t>::max)());

/// Retrieves a reference to the given record's primary key.
template <typename Record>
decltype(auto) RecordPrimaryKeyOf(Record&& record)
{
    // static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    // static_assert(RecordPrimaryKeyIndex<Record> != static_cast<size_t>(-1), "Record must have a primary key");
    return GetRecordMemberAt<RecordPrimaryKeyIndex<std::remove_cvref_t<Record>>>(std::forward<Record>(record));
}

namespace details
{

    template <typename Record>
    struct RecordPrimaryKeyTypeHelper
    {
        using type = std::monostate;
    };

    template <typename Record>
        requires(RecordPrimaryKeyIndex<Record> < RecordMemberCount<Record>)
    struct RecordPrimaryKeyTypeHelper<Record>
    {
        using type = RecordMemberTypeOf<RecordPrimaryKeyIndex<Record>, Record>::ValueType;
    };

} // namespace details

/// Reflects the primary key type of the given record.
template <typename Record>
using RecordPrimaryKeyType = details::RecordPrimaryKeyTypeHelper<Record>::type;

/// @brief Selector value meaning "resolve the relationship automatically; the match must be unique".
///
/// This is the default for every relationship selector. Pass a `SqlRealName` instead to name the
/// foreign key column explicitly, which is required when a record holds more than one foreign key
/// into the same table.
///
/// @ingroup DataMapper
inline constexpr std::nullopt_t AutoDetectRelation = std::nullopt;

/// @brief Constrains what may be used to single out one of several foreign keys into the same table.
///
/// Two forms are accepted:
/// - `std::nullopt` (`AutoDetectRelation`) - resolve automatically; ambiguity is a compile error.
/// - a `SqlRealName` (or anything convertible to `std::string_view`) - the SQL column name of the
///   foreign key to use.
///
/// A relationship selector must stay inert at *declaration* time: the two records of a relationship
/// reference each other, so neither is complete where the other is declared. That rules out
/// pointer-to-member selectors and is why the foreign key is named by its column.
///
/// @ingroup DataMapper
template <auto Selector>
concept RelationSelector =
    std::same_as<std::remove_cvref_t<decltype(Selector)>, std::nullopt_t> || requires { std::string_view { Selector }; };

namespace detail
{

    /// @brief Tests whether member @p I of @p Record is singled out by @p Selector.
    ///
    /// @tparam Selector The relationship selector, see the RelationSelector concept.
    /// @tparam I Member index within @p Record.
    /// @tparam Record The record holding the foreign key.
    /// @return `true` when the selector accepts the member.
    template <auto Selector, size_t I, typename Record>
    constexpr bool RelationSelectorMatches()
    {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(Selector)>, std::nullopt_t>)
            return true;
        else
            return Lightweight::FieldNameAt<I, Record> == std::string_view { Selector };
    }

    /// @brief Outcome of scanning a child record for the `BelongsTo` members pointing back to an owner record.
    struct InverseBelongsToLookup
    {
        /// Member index of the first matching `BelongsTo`, or `RecordMemberCount` if there is none.
        size_t index {};

        /// Number of matching `BelongsTo` members found.
        size_t count {};

        /// Number of `BelongsTo` members referencing the owner record, before the selector was applied.
        size_t candidates {};
    };

    /// @brief Scans @p ChildRecord for the `BelongsTo` members whose referenced record is @p OwnerRecord.
    ///
    /// @tparam OwnerRecord The record on the "one" side of a one-to-many relationship.
    /// @tparam ChildRecord The record on the "many" side, holding the foreign key.
    /// @tparam Selector Singles out one of several foreign keys, see the RelationSelector concept.
    /// @return The index of the first match, how many matches exist, and how many candidates the
    ///         selector had to choose from.
    template <typename OwnerRecord, typename ChildRecord, auto Selector = AutoDetectRelation>
    constexpr InverseBelongsToLookup FindInverseBelongsTo()
    {
        return FoldRecordMembers<ChildRecord>(
            InverseBelongsToLookup { .index = RecordMemberCount<ChildRecord>, .count = 0, .candidates = 0 },
            []<size_t I, typename MemberType>(InverseBelongsToLookup const accum) constexpr -> InverseBelongsToLookup {
                // The two conditions must nest: `MemberType::ReferencedRecord` does not exist on plain
                // fields, and `&&` inside a single `if constexpr` would still instantiate it.
                if constexpr (IsBelongsTo<MemberType>)
                {
                    if constexpr (std::same_as<typename MemberType::ReferencedRecord, OwnerRecord>)
                    {
                        if constexpr (RelationSelectorMatches<Selector, I, ChildRecord>())
                            return { .index = accum.count == 0 ? I : accum.index,
                                     .count = accum.count + 1,
                                     .candidates = accum.candidates + 1 };
                        else
                            return { .index = accum.index, .count = accum.count, .candidates = accum.candidates + 1 };
                    }
                    else
                        return accum;
                }
                else
                    return accum;
            });
    }

    /// @brief Resolves - and validates - the inverse `BelongsTo` member of a relationship.
    ///
    /// Instantiating this template fails to compile unless @p ChildRecord declares exactly one
    /// `BelongsTo` member that references @p OwnerRecord and is accepted by @p Selector. The compiler's
    /// instantiation backtrace names the record types involved.
    ///
    /// @tparam OwnerRecord The record being referenced (the "one" side of the relationship).
    /// @tparam ChildRecord The record holding the foreign key (the "many" side).
    /// @tparam Selector Singles out one of several foreign keys, see the RelationSelector concept.
    template <typename OwnerRecord, typename ChildRecord, auto Selector = AutoDetectRelation>
        requires RelationSelector<Selector>
    struct InverseBelongsToResolver
    {
        /// The raw lookup result for @p OwnerRecord within @p ChildRecord.
        static constexpr InverseBelongsToLookup Lookup = FindInverseBelongsTo<OwnerRecord, ChildRecord, Selector>();

        static_assert(Lookup.candidates != 0,
                      "This relationship requires the referencing record to declare a BelongsTo member pointing at "
                      "the referenced record's primary key. No such member was found. "
                      "See the instantiation backtrace below for the two record types involved.");

        static_assert(Lookup.candidates == 0 || Lookup.count != 0,
                      "No BelongsTo member matches the foreign key column named by this relationship. The referencing "
                      "record does declare a BelongsTo pointing at the referenced record, but none of them uses that "
                      "column name - check the SqlRealName spelling on both sides. "
                      "See the instantiation backtrace below for the two record types involved.");

        static_assert(Lookup.count <= 1,
                      "This relationship is ambiguous: the referencing record declares more than one BelongsTo member "
                      "pointing at the referenced record's primary key. Name the foreign key column to disambiguate, "
                      "e.g. HasMany<Child, SqlRealName { \"owner_id\" }>. "
                      "See the instantiation backtrace below for the two record types involved.");

        /// Member index of the inverse `BelongsTo` inside @p ChildRecord.
        /// Clamped to 0 on failure so that the static_asserts above are the only diagnostics emitted.
        static constexpr size_t Index = Lookup.count == 1 ? Lookup.index : 0;
    };

} // namespace detail

/// @brief Member index, within @p ChildRecord, of the `BelongsTo` member that points back to @p OwnerRecord.
///
/// This is how `HasMany`, `HasManyThrough` and `HasOneThrough` locate their foreign key column: by matching
/// the relationship *type*, never by member position. Using it is a hard compile-time error when
/// @p ChildRecord declares no such `BelongsTo`, or when it declares more than one and @p Selector does not
/// single out exactly one of them.
///
/// @tparam OwnerRecord The record being referenced (the "one" side of the relationship).
/// @tparam ChildRecord The record holding the foreign key (the "many" side).
/// @tparam Selector Singles out one of several foreign keys, see the RelationSelector concept.
///
/// @ingroup DataMapper
template <typename OwnerRecord, typename ChildRecord, auto Selector = AutoDetectRelation>
constexpr size_t InverseBelongsToIndexOf = detail::InverseBelongsToResolver<OwnerRecord, ChildRecord, Selector>::Index;

/// @brief SQL column name of the foreign key that links @p ChildRecord back to @p OwnerRecord.
///
/// @tparam OwnerRecord The record being referenced (the "one" side of the relationship).
/// @tparam ChildRecord The record holding the foreign key (the "many" side).
/// @tparam Selector Singles out one of several foreign keys, see the RelationSelector concept.
///
/// @ingroup DataMapper
template <typename OwnerRecord, typename ChildRecord, auto Selector = AutoDetectRelation>
constexpr std::string_view InverseBelongsToFieldNameOf =
    FieldNameAt<InverseBelongsToIndexOf<OwnerRecord, ChildRecord, Selector>, ChildRecord>;

/// @brief Maps the fields of the given record to the target that supports the operator[].
template <typename Record, typename TargetMappable>
void MapFromRecordFields(Record&& record, TargetMappable& target)
{
    EnumerateRecordMembers(std::forward<Record>(record), [&]<std::size_t I>(auto const& field) {
        using MemberType = RecordMemberTypeOf<I, Record>;
        static_assert(IsField<MemberType>, "Record member must be a Field<> type");
        static_assert(std::is_assignable_v<decltype(target[I]), decltype(field.Value())>,
                      "Target must support operator[] with the field type");
        target[I] = field.Value();
    });
}

/// Requires that T satisfies to be a field with storage.
///
/// @ingroup DataMapper
template <typename T>
concept FieldWithStorage = requires(T const& field, T& mutableField) {
    // clang-format off
    { field.Value() } -> std::convertible_to<typename T::ValueType const&>;
    { mutableField.MutableValue() } -> std::convertible_to<typename T::ValueType&>;
    { field.IsModified() } -> std::convertible_to<bool>;
    { mutableField.SetModified(bool {}) } -> std::convertible_to<void>;
    // clang-format on
};

/// @brief Requires that T maps onto a column of its record's table.
///
/// This is satisfied by fields with storage (Field, BelongsTo) as well as by plain record members
/// that are directly bindable as an output column (a record may be a plain struct of bindable
/// members). Relation members (HasMany, HasManyThrough, HasOneThrough, ...) have no column of their
/// own and therefore must be skipped by every column-enumerating code path, such as the projection
/// of a SELECT statement.
///
/// @ingroup DataMapper
template <typename T>
concept RecordColumnMember = FieldWithStorage<T> || SqlOutputColumnBinder<T>;

/// @brief Represents the number of members of a record that map onto a column of a result set.
///
/// This is the width the record occupies in a projection built from the @c RecordColumnMember
/// concept (e.g. @c SqlSelectQueryBuilder::Fields), and therefore the amount by which the index must be
/// advanced to reach the first column of the record that follows it in a multi-record projection.
/// It differs from @c RecordMemberCount exactly by the number of relation members (HasMany,
/// HasManyThrough, HasOneThrough, ...), which have no column of their own.
///
/// @ingroup DataMapper
template <typename Record>
constexpr size_t RecordColumnCount =
    FoldRecordMembers<Record>(size_t { 0 }, []<size_t I, typename Field>(size_t const accum) constexpr {
        if constexpr (RecordColumnMember<Field>)
            return accum + 1;
        else
            return accum;
    });

/// Represents the number of fields with storage in a record.
///
/// @ingroup DataMapper
template <typename Record>
constexpr size_t RecordStorageFieldCount =
    FoldRecordMembers<Record>(size_t { 0 }, []<size_t I, typename Field>(size_t const accum) constexpr {
        if constexpr (FieldWithStorage<Field>)
            return accum + 1;
        else
            return accum;
    });

template <typename Record>
concept RecordWithStorageFields = (RecordStorageFieldCount<Record> > 0);

namespace detail
{

    template <auto Test, typename T>
    constexpr bool CheckFieldProperty = FoldRecordMembers<T>(false, []<size_t I, typename Field>(bool const accum) {
        if constexpr (Test.template operator()<Field>())
            return true;
        else
            return accum;
    });

} // namespace detail

/// @brief Tests if the given record type does contain a primary key.
///
/// @ingroup DataMapper
template <typename T>
constexpr bool HasPrimaryKey = detail::CheckFieldProperty<[]<typename Field>() { return IsPrimaryKey<Field>; }, T>;

/// @brief Tests if the given record type does contain an auto increment primary key.
///
/// @ingroup DataMapper
template <typename T>
constexpr bool HasAutoIncrementPrimaryKey =
    detail::CheckFieldProperty<[]<typename Field>() { return IsAutoIncrementPrimaryKey<Field>; }, T>;

namespace detail
{
    /// Collects the value types of every `PrimaryKey` member of @p Record, in declaration order.
    template <typename Record>
    struct RecordPrimaryKeyTupleHelper
    {
        using type = decltype([]<std::size_t... I>(std::index_sequence<I...>) {
            return std::tuple_cat([]<std::size_t J>() {
                using FieldType = RecordMemberTypeOf<J, Record>;
                // The two conditions must nest rather than share one `if constexpr`: `IsPrimaryKey` is
                // not a member of the relation types (HasMany, CompositeForeignKey, ...), and `&&`
                // inside a single condition would still instantiate the right-hand side for them.
                if constexpr (IsField<FieldType>)
                {
                    if constexpr (FieldType::IsPrimaryKey)
                        return std::tuple<typename FieldType::ValueType> {};
                    else
                        return std::tuple<> {};
                }
                else
                    return std::tuple<> {};
            }.template operator()<I>()...);
        }(std::make_index_sequence<RecordMemberCount<Record>> {}));
    };
} // namespace detail

/// @brief The tuple of a record's primary key value types, in member declaration order.
///
/// Unlike @ref RecordPrimaryKeyType, which names a single field's type, this covers composite keys:
/// for a record with several members marked `PrimaryKey` it is a tuple of all of them. For a
/// single-key record it is a one-element tuple.
///
/// Added alongside the single-key helpers rather than replacing them, so no existing caller changes
/// behaviour; only composite-aware code reaches for this.
///
/// @ingroup DataMapper
template <typename Record>
using RecordPrimaryKeyTuple = typename detail::RecordPrimaryKeyTupleHelper<Record>::type;

/// @brief Number of members of @p Record marked as a primary key.
///
/// One for an ordinary record, more for a composite key, zero for a keyless record.
///
/// @ingroup DataMapper
template <typename Record>
constexpr std::size_t RecordPrimaryKeyCount = std::tuple_size_v<RecordPrimaryKeyTuple<Record>>;

/// @brief Whether @p Record's identity spans more than one column.
///
/// @ingroup DataMapper
template <typename Record>
constexpr bool HasCompositePrimaryKey = RecordPrimaryKeyCount<Record> > 1;

/// @brief Reads every primary key value of @p record, in member declaration order.
///
/// This is the order a primary key lookup binds its arguments in, so the returned tuple can be applied
/// straight to `QuerySingle`/`Update`/`Delete`.
///
/// @param record Record to read.
/// @return The key values as a tuple.
///
/// @ingroup DataMapper
template <typename Record>
[[nodiscard]] RecordPrimaryKeyTuple<Record> GetPrimaryKeyFields(Record const& record)
{
    auto result = RecordPrimaryKeyTuple<Record> {};
    auto slot = std::size_t { 0 };
    EnumerateRecordMembers(record, [&]<size_t I, typename FieldType>(FieldType const& field) {
        if constexpr (IsField<FieldType>)
            if constexpr (FieldType::IsPrimaryKey)
            {
                // The tuple was built by walking the members in this same order, so the Nth key member
                // corresponds to tuple element N. The tuple is heterogeneous, so the write is matched
                // against the compile-time index and only the assignable slot is emitted.
                [&]<std::size_t... J>(std::index_sequence<J...>) {
                    (
                        [&] {
                            if constexpr (std::assignable_from<std::tuple_element_t<J, RecordPrimaryKeyTuple<Record>>&,
                                                               typename FieldType::ValueType const&>)
                                if (J == slot)
                                    std::get<J>(result) = field.Value();
                        }(),
                        ...);
                }(std::make_index_sequence<RecordPrimaryKeyCount<Record>> {});
                ++slot;
            }
    });
    return result;
}

/// Returns the first primary key field of the record.
///
/// @ingroup DataMapper
template <typename Record>
inline LIGHTWEIGHT_FORCE_INLINE RecordPrimaryKeyType<Record> GetPrimaryKeyField(Record const& record) noexcept
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(HasPrimaryKey<Record>, "Record must have a primary key");

    auto result = RecordPrimaryKeyType<Record> {};
    EnumerateRecordMembers(record, [&]<size_t I, typename FieldType>(FieldType const& field) {
        // std::same_as<typename FieldType::ValueType, RecordPrimaryKeyType<Record>>condition is for the case where there are
        // multiple primary keys, we want to return the first one
        if constexpr (IsField<FieldType>)
            if constexpr (IsPrimaryKey<FieldType>)
                if constexpr (std::same_as<typename FieldType::ValueType, RecordPrimaryKeyType<Record>>)
                    result = field.Value();
    });
    return result;
}

} // namespace Lightweight
