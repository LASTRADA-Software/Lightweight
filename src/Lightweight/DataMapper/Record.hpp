// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"
#include "BelongsTo.hpp"
#include "Field.hpp"

#include <reflection-cpp/reflection.hpp>

#include <concepts>
#include <limits>
#include <string_view>

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

namespace detail
{

    /// @brief Outcome of scanning a child record for the `BelongsTo` members pointing back to an owner record.
    struct InverseBelongsToLookup
    {
        /// Member index of the first matching `BelongsTo`, or `RecordMemberCount` if there is none.
        size_t index {};

        /// Number of matching `BelongsTo` members found.
        size_t count {};
    };

    /// @brief Scans @p ChildRecord for all `BelongsTo` members whose referenced record is @p OwnerRecord.
    ///
    /// @tparam OwnerRecord The record on the "one" side of a one-to-many relationship.
    /// @tparam ChildRecord The record on the "many" side, holding the foreign key.
    /// @return The index of the first match and how many matches exist.
    template <typename OwnerRecord, typename ChildRecord>
    constexpr InverseBelongsToLookup FindInverseBelongsTo()
    {
        return FoldRecordMembers<ChildRecord>(
            InverseBelongsToLookup { .index = RecordMemberCount<ChildRecord>, .count = 0 },
            []<size_t I, typename MemberType>(InverseBelongsToLookup const accum) constexpr -> InverseBelongsToLookup {
                // The two conditions must nest: `MemberType::ReferencedRecord` does not exist on plain
                // fields, and `&&` inside a single `if constexpr` would still instantiate it.
                if constexpr (IsBelongsTo<MemberType>)
                {
                    if constexpr (std::same_as<typename MemberType::ReferencedRecord, OwnerRecord>)
                        return { .index = accum.count == 0 ? I : accum.index, .count = accum.count + 1 };
                    else
                        return accum;
                }
                else
                    return accum;
            });
    }

    /// @brief Resolves - and validates - the inverse `BelongsTo` member of a one-to-many relationship.
    ///
    /// Instantiating this template fails to compile when @p ChildRecord does not declare exactly one
    /// `BelongsTo` member referencing @p OwnerRecord. The compiler's instantiation backtrace names both
    /// record types.
    ///
    /// @tparam OwnerRecord The record on the "one" side of the relationship (declaring the `HasMany`).
    /// @tparam ChildRecord The record on the "many" side of the relationship (declaring the `BelongsTo`).
    template <typename OwnerRecord, typename ChildRecord>
    struct InverseBelongsToResolver
    {
        /// The raw lookup result for @p OwnerRecord within @p ChildRecord.
        static constexpr InverseBelongsToLookup Lookup = FindInverseBelongsTo<OwnerRecord, ChildRecord>();

        static_assert(Lookup.count != 0,
                      "HasMany<ChildRecord> requires ChildRecord to declare a BelongsTo member referencing the "
                      "owning record's primary key. No such member was found. "
                      "See the instantiation backtrace below for the two record types involved.");

        static_assert(Lookup.count <= 1,
                      "HasMany<ChildRecord> requires ChildRecord to declare exactly one BelongsTo member "
                      "referencing the owning record's primary key, but multiple were found, making the inverse "
                      "relationship ambiguous. "
                      "See the instantiation backtrace below for the two record types involved.");

        /// Member index of the inverse `BelongsTo` inside @p ChildRecord.
        /// Clamped to 0 on failure so that the two static_asserts above are the only diagnostics emitted.
        static constexpr size_t Index = Lookup.count == 1 ? Lookup.index : 0;
    };

} // namespace detail

/// @brief Member index, within @p ChildRecord, of the unique `BelongsTo` member that points back to @p OwnerRecord.
///
/// This is how a `HasMany<ChildRecord>` on @p OwnerRecord locates its foreign key column: by matching the
/// relationship *type*, never by member position. Using it is a hard compile-time error when @p ChildRecord
/// declares no such `BelongsTo`, or more than one (ambiguous inverse).
///
/// @tparam OwnerRecord The record on the "one" side of the relationship (declaring the `HasMany`).
/// @tparam ChildRecord The record on the "many" side of the relationship (declaring the `BelongsTo`).
///
/// @ingroup DataMapper
template <typename OwnerRecord, typename ChildRecord>
constexpr size_t InverseBelongsToIndexOf = detail::InverseBelongsToResolver<OwnerRecord, ChildRecord>::Index;

/// @brief SQL column name of the foreign key that links @p ChildRecord back to @p OwnerRecord.
///
/// @tparam OwnerRecord The record on the "one" side of the relationship (declaring the `HasMany`).
/// @tparam ChildRecord The record on the "many" side of the relationship (declaring the `BelongsTo`).
///
/// @ingroup DataMapper
template <typename OwnerRecord, typename ChildRecord>
constexpr std::string_view InverseBelongsToFieldNameOf =
    FieldNameAt<InverseBelongsToIndexOf<OwnerRecord, ChildRecord>, ChildRecord>;

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
