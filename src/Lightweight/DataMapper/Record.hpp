// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"
#include "Field.hpp"

#include <reflection-cpp/reflection.hpp>

#include <concepts>
#include <limits>

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

namespace detail
{

    template <std::size_t I, typename Record>
    constexpr std::optional<size_t> FindPrimaryKeyIndex()
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
        if constexpr (I < Reflection::CountMembers<Record>)
        {
            if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
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
    return Reflection::GetMemberAt<RecordPrimaryKeyIndex<std::remove_cvref_t<Record>>>(std::forward<Record>(record));
}

namespace details
{

    template <typename Record>
    struct RecordPrimaryKeyTypeHelper
    {
        using type = void;
    };

    template <typename Record>
        requires(RecordPrimaryKeyIndex<Record> < Reflection::CountMembers<Record>)
    struct RecordPrimaryKeyTypeHelper<Record>
    {
        using type = typename Reflection::MemberTypeOf<RecordPrimaryKeyIndex<Record>, Record>::ValueType;
    };

} // namespace details

/// Reflects the primary key type of the given record.
template <typename Record>
using RecordPrimaryKeyType = typename details::RecordPrimaryKeyTypeHelper<Record>::type;

/// @brief Maps the fields of the given record to the target that supports the operator[].
template <typename Record, typename TargetMappable>
void MapFromRecordFields(Record&& record, TargetMappable& target)
{
    Reflection::EnumerateMembers(std::forward<Record>(record), [&]<std::size_t I>(auto const& field) {
        using MemberType = Reflection::MemberTypeOf<I, Record>;
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

/// Represents the number of fields with storage in a record.
///
/// @ingroup DataMapper
template <typename Record>
constexpr size_t RecordStorageFieldCount =
    Reflection::FoldMembers<Record>(size_t { 0 }, []<size_t I, typename Field>(size_t const accum) constexpr {
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
    constexpr bool CheckFieldProperty = Reflection::FoldMembers<T>(false, []<size_t I, typename Field>(bool const accum) {
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
    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType const& field) {
        if constexpr (IsPrimaryKey<FieldType> && std::same_as<FieldType, RecordPrimaryKeyType<Record>>)
        {
            result = field;
        }
    });
    return result;
}

} // namespace Lightweight
