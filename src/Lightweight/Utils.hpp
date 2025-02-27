// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <reflection-cpp/reflection.hpp>

#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include <sql.h>

namespace detail
{

template <typename T, typename... Comps>
concept OneOf = (std::same_as<T, Comps> || ...);

template <typename T>
constexpr auto AlwaysFalse = std::false_type::value;

constexpr auto Finally(auto&& cleanupRoutine) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    struct Finally
    {
        std::remove_cvref_t<decltype(cleanupRoutine)> cleanup;
        ~Finally()
        {
            cleanup();
        }
    };
    return Finally { std::forward<decltype(cleanupRoutine)>(cleanupRoutine) };
}

// is_specialization_of<> is inspired by:
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2098r1.pdf

template <template <typename...> class T, typename U>
struct is_specialization_of: std::false_type
{
};

template <template <typename...> class T, typename... Us>
struct is_specialization_of<T, T<Us...>>: std::true_type
{
};

template <typename T>
struct MemberClassTypeHelper;

template <typename M, typename T>
struct MemberClassTypeHelper<M T::*>
{
    using type = std::remove_cvref_t<T>;
};

template <typename Record>
struct RecordTableNameImpl
{
    static constexpr std::string_view Value = []() {
        if constexpr (requires { Record::TableName; })
            return Record::TableName;
        else
            return []() {
                // TODO: Build plural
                auto const typeName = Reflection::TypeNameOf<Record>;
                if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                    return typeName.substr(i + 1);
                return typeName;
            }();
    }();
};

template <std::size_t I, typename Record>
struct BelongsToNameImpl
{
    static constexpr auto baseName = Reflection::MemberNameOf<I, Record>;
    static constexpr auto storage = []() -> std::array<char, baseName.size() + 4>
    {
        std::array<char, baseName.size() + 4> storage;
        std::copy_n(baseName.begin(), baseName.size(), storage.begin());
        std::copy_n("_id", 3, storage.begin() + baseName.size());
        storage.back() = '\0';
        return storage;
    }
    ();
    static constexpr auto name = std::string_view(storage.data(), storage.size() - 1);
};

template <typename FieldType>
constexpr auto ColumnNameOverride = []() consteval {
    if constexpr (requires { FieldType::ColumnNameOverride; })
        return FieldType::ColumnNameOverride;
    else
        return std::string_view {};
}();

template <typename ReferencedFieldType, auto F>
struct FieldNameOfImpl;

template <typename T, auto F, typename R>
struct FieldNameOfImpl<R T::*, F>
{
    static constexpr std::string_view value = []() constexpr -> std::string_view {
        if constexpr (requires { R::ColumnNameOverride; })
        {
            if constexpr (!R::ColumnNameOverride.empty())
                return R::ColumnNameOverride;
        }
        return Reflection::NameOf<F>;
    }();
};

template <std::size_t I, typename Record>
consteval std::string_view FieldNameAt()
{
    using FieldType = Reflection::MemberTypeOf<I, Record>;

    if constexpr (!std::string_view(ColumnNameOverride<FieldType>).empty())
    {
        return FieldType::ColumnNameOverride;
    }
    else if constexpr (requires { FieldType::ReferencedField; }) // check isBelongsTo
    {
        return detail::BelongsToNameImpl<I, Record>::name;
    }
    else
        return Reflection::MemberNameOf<I, Record>;
}

} // namespace detail

/// @brief Returns the SQL field name of the given field index in the record.
///
/// @ingroup DataMapper
template <std::size_t I, typename Record>
constexpr inline std::string_view FieldNameAt = detail::FieldNameAt<I, Record>();

/// @brief Returns the name of the field referenced by the given pointer-to-member.
///
/// This also supports custom column name overrides.
template <auto ReferencedField>
constexpr inline std::string_view FieldNameOf =
    detail::FieldNameOfImpl<decltype(ReferencedField), ReferencedField>::value;

/// @brief Holds the SQL tabl ename for the given record type.
///
/// @ingroup DataMapper
template <typename Record>
constexpr std::string_view RecordTableName = detail::RecordTableNameImpl<Record>::Value;

template <template <typename...> class S, class T>
concept IsSpecializationOf = detail::is_specialization_of<S, T>::value;

template <typename T>
using MemberClassType = typename detail::MemberClassTypeHelper<T>::type;

namespace detail
{
template <auto ReferencedField>
struct FullFieldNameOfImpl
{
    static constexpr auto ClassName = RecordTableName<MemberClassType<decltype(ReferencedField)>>;
    static constexpr auto FieldName = FieldNameOf<ReferencedField>;
    static constexpr auto StorageSize = ClassName.size() + FieldName.size() + 6;

    // Holds the full field name in the format "ClassName"."FieldName"
    static constexpr auto Storage = []() constexpr -> std::array<char, StorageSize> {
        // clang-format off
        auto storage = std::array<char, StorageSize> {};
        std::ranges::copy("\"",      storage.begin());
        std::ranges::copy(ClassName, storage.begin() + 1);
        std::ranges::copy("\".\"",   storage.begin() + 1 + ClassName.size());
        std::ranges::copy(FieldName, storage.begin() + 1 + ClassName.size() + 3);
        std::ranges::copy("\"",      storage.begin() + 1 + ClassName.size() + 3 + FieldName.size());
        storage.back() = '\0';
        // clang-format on
        return storage;
    }();
    static constexpr auto value = std::string_view(Storage.data(), Storage.size() - 1);
};
} // namespace detail

struct SqlRawColumnNameView
{
    std::string_view value;

    std::weak_ordering operator<=>(SqlRawColumnNameView const& other) const = default;
};

constexpr bool operator==(SqlRawColumnNameView const& lhs, std::string_view rhs) noexcept
{
    return lhs.value == rhs;
}

constexpr bool operator!=(SqlRawColumnNameView const& lhs, std::string_view rhs) noexcept
{
    return lhs.value != rhs;
}

template <auto ReferencedField>
constexpr inline auto FullFieldNameOf = SqlRawColumnNameView {
    .value = detail::FullFieldNameOfImpl<ReferencedField>::value,
};

LIGHTWEIGHT_API void LogIfFailed(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation);

LIGHTWEIGHT_API void RequireSuccess(SQLHSTMT hStmt,
                                    SQLRETURN error,
                                    std::source_location sourceLocation = std::source_location::current());
