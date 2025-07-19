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

} // namespace Lightweight
