// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <reflection-cpp/reflection.hpp>

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Lightweight
{

namespace detail
{
    template <typename MemberPointer>
    struct MemberPointee;

    template <typename Class, typename Member>
    struct MemberPointee<Member Class::*>
    {
        using type = Member;
    };
} // namespace detail

/// @brief Compile-time list of a record's members, stored as pointers-to-member.
///
/// This is the access vehicle used by Description. It is an empty type — the members are
/// carried as non-type template parameters — so it is far cheaper to parse and instantiate than a
/// `std::tuple` of heterogeneous pointer values (no class storage, no CTAD). Member types are
/// recovered by indexing the pointer-to-member pack (see `TypeAt`) and iteration uses element-wise
/// pack expansion, both of which are essentially free at compile time.
///
/// @ingroup DataMapper
template <auto... MemberPointers>
struct RecordMemberList
{
    /// Number of members in the list.
    static constexpr std::size_t Count = sizeof...(MemberPointers);

    /// The (unwrapped) type of the member at index @p I.
    ///
    /// Indexes the pointer-to-member pack via `std::tuple_element_t` rather than the
    /// `__type_pack_element` builtin: the latter is a Clang/GCC extension that MSVC does not
    /// accept as a type (it parses the bare identifier as a function template — error C7568).
    template <std::size_t I>
    using TypeAt = detail::MemberPointee<std::tuple_element_t<I, std::tuple<decltype(MemberPointers)...>>>::type;

    /// Invokes `callable<I>(record.member_I)` for every member, in order.
    ///
    /// The callable is forwarded once into a named reference and then invoked for each member, so a
    /// `mutable` visitor (e.g. one carrying a running column offset) keeps its state across members.
    template <typename Record, typename Callable>
    static constexpr void EnumerateValues(Record& record, Callable&& callable)
    {
        auto&& visitor = std::forward<Callable>(callable);
        // Expand only the index pack and reach each member through `MemberAt<I>`. Expanding the
        // enclosing class-template NTTP pack (`MemberPointers...`) directly inside this nested
        // lambda makes MSVC mis-bind the name (error C7746 "cannot appear in its own initializer");
        // routing the access through the `MemberAt` helper — as `EnumerateMaskedValues` already
        // does — sidesteps the defect with identical semantics and ordering.
        [&]<std::size_t... I>(std::index_sequence<I...> /*indices*/) {
            (visitor.template operator()<I>(MemberAt<I>(record)), ...);
        }(std::make_index_sequence<Count> {});
    }

    /// Invokes `callable<I, MemberType>()` for every member, in order.
    template <typename Callable>
    static constexpr void EnumerateTypes(Callable&& callable)
    {
        auto&& visitor = std::forward<Callable>(callable);
        [&]<std::size_t... I>(std::index_sequence<I...> /*indices*/) {
            (visitor.template operator()<I, TypeAt<I>>(), ...);
        }(std::make_index_sequence<Count> {});
    }

    /// Invokes `callable<I>(record.member_I)` for the members whose indices appear in @p ElementMask
    /// (a `std::integer_sequence<std::size_t, ...>`), in mask order. Backs partial-column queries.
    template <typename ElementMask, typename Record, typename Callable>
    static constexpr void EnumerateMaskedValues(Record& record, Callable&& callable)
    {
        auto&& visitor = std::forward<Callable>(callable);
        [&]<std::size_t... I>(std::integer_sequence<std::size_t, I...> /*mask*/) {
            (visitor.template operator()<I>(MemberAt<I>(record)), ...);
        }(ElementMask {});
    }

    /// Returns a reference to the member at index @p I of @p record.
    template <std::size_t I, typename Record>
    static constexpr decltype(auto) MemberAt(Record& record)
    {
        // std::tie builds the reference tuple only at this call site (rare), so the descriptor
        // itself stays free of any std::tuple instantiation.
        return std::get<I>(std::tie(record.*MemberPointers...));
    }
};

/// @brief Customization point providing pre-computed reflection metadata for a record type.
///
/// The DataMapper reflects over records to discover their fields, field names, and types. By
/// default this is done at compile time via the reflection-cpp library, which is accurate but
/// expensive to instantiate for records with many members and dense relationship graphs (the
/// cost is paid once per record type per translation unit and multiplies across the reachable
/// relationship graph).
///
/// Tools such as `ddl2cpp` know a record's structure ahead of time and can emit an explicit
/// specialization of this template. When a specialization exists the DataMapper reads the
/// pre-baked metadata instead of evaluating reflection, which dramatically reduces compile time.
/// Records without a specialization keep working unchanged via automatic reflection — the same
/// "specialize-or-reflect" approach already used for the optional `static TableName` member.
///
/// A specialization must provide:
///  - `static constexpr std::size_t FieldCount;` — number of members.
///  - `using Members = RecordMemberList<&Record::a, &Record::b, ...>;` — the members, in
///    declaration order.
///  - `static constexpr std::array<std::string_view, FieldCount> FieldNames;` — the resolved SQL
///    column name for each field (i.e. the value `FieldNameAt` would otherwise compute).
///
/// @ingroup DataMapper
template <typename Record>
struct Description;

/// @brief Satisfied when a Description specialization exists for the given record type.
/// @ingroup DataMapper
template <typename Record>
concept HasDescription = requires {
    { Description<std::remove_cvref_t<Record>>::FieldCount } -> std::convertible_to<std::size_t>;
};

namespace detail
{
    // Lazy dispatch for the member type: only the selected branch is instantiated, so the
    // (expensive) reflection alias is never expanded when a descriptor is present.
    template <std::size_t I, typename Record, bool HasDescriptor>
    struct RecordMemberTypeOfDispatch
    {
        using type = Description<Record>::Members::template TypeAt<I>;
    };

    template <std::size_t I, typename Record>
    struct RecordMemberTypeOfDispatch<I, Record, false>
    {
        using type = Reflection::MemberTypeOf<I, Record>;
    };
} // namespace detail

/// @brief Number of members in a record — from the descriptor if present, else via reflection.
/// @ingroup DataMapper
template <typename Record>
constexpr std::size_t RecordMemberCount = []() constexpr {
    if constexpr (HasDescription<Record>)
        return Description<std::remove_cvref_t<Record>>::FieldCount;
    else
        return Reflection::CountMembers<Record>;
}();

/// @brief Type of the member at index @p I — from the descriptor if present, else via reflection.
/// @ingroup DataMapper
template <std::size_t I, typename Record>
using RecordMemberTypeOf = detail::RecordMemberTypeOfDispatch<I, std::remove_cvref_t<Record>, HasDescription<Record>>::type;

/// @brief Returns a reference to the member at index @p I — from the descriptor if present, else via reflection.
/// @ingroup DataMapper
template <std::size_t I, typename Record>
constexpr decltype(auto) GetRecordMemberAt(Record&& record)
{
    if constexpr (HasDescription<Record>)
        return Description<std::remove_cvref_t<Record>>::Members::template MemberAt<I>(record);
    else
        return Reflection::GetMemberAt<I>(std::forward<Record>(record));
}

/// @brief Invokes @p callable as `callable<I>(member)` for each member of @p record.
///
/// Mirrors `Reflection::EnumerateMembers(object, callable)` but reads the descriptor when present,
/// avoiding the aggregate-decomposition (`ToTuple`) instantiation.
/// @ingroup DataMapper
template <typename Record, typename Callable>
constexpr void EnumerateRecordMembers(Record& record, Callable&& callable)
{
    if constexpr (HasDescription<Record>)
        Description<std::remove_cvref_t<Record>>::Members::EnumerateValues(record, std::forward<Callable>(callable));
    else
        Reflection::EnumerateMembers(record, std::forward<Callable>(callable));
}

/// @brief Invokes @p callable as `callable<I>(member)` for each member selected by @p ElementMask.
///
/// Mirrors `Reflection::EnumerateMembers<ElementMask>(object, callable)` (partial-column queries)
/// but reads the descriptor when present. @p ElementMask is a `std::integer_sequence<std::size_t, ...>`.
/// @ingroup DataMapper
template <typename ElementMask, typename Record, typename Callable>
constexpr void EnumerateRecordMembers(Record& record, Callable&& callable)
{
    if constexpr (HasDescription<Record>)
        Description<std::remove_cvref_t<Record>>::Members::template EnumerateMaskedValues<ElementMask>(
            record, std::forward<Callable>(callable));
    else
        Reflection::EnumerateMembers<ElementMask>(record, std::forward<Callable>(callable));
}

/// @brief Invokes @p callable as `callable<I, MemberType>()` for each member of @p Record.
///
/// Mirrors `Reflection::EnumerateMembers<Object>(callable)` but reads the descriptor when present.
/// @ingroup DataMapper
template <typename Record, typename Callable>
constexpr void EnumerateRecordMembers(Callable&& callable)
{
    if constexpr (HasDescription<Record>)
        Description<std::remove_cvref_t<Record>>::Members::EnumerateTypes(std::forward<Callable>(callable));
    else
        Reflection::EnumerateMembers<Record>(std::forward<Callable>(callable));
}

/// @brief Folds over a record's members as `result = callable<I, MemberType>(result)`.
///
/// Mirrors `Reflection::FoldMembers<Object>(initialValue, callable)` but reads the descriptor when present.
/// @ingroup DataMapper
template <typename Record, typename Callable, typename ResultType>
constexpr ResultType FoldRecordMembers(ResultType initialValue, Callable const& callable)
{
    ResultType result = initialValue;
    EnumerateRecordMembers<Record>(
        [&]<std::size_t I, typename MemberType>() { result = callable.template operator()<I, MemberType>(result); });
    return result;
}

} // namespace Lightweight
