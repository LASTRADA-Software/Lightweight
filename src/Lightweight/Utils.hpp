#pragma once

#include <reflection-cpp/reflection.hpp>

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

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
    static constexpr auto storage = []() -> std::array<char, baseName.size() + 3>
    {
        std::array<char, baseName.size() + 3> storage;
        std::copy_n(baseName.begin(), baseName.size(), storage.begin());
        std::copy_n("_id", 3, storage.begin() + baseName.size());
        return storage;
    }
    ();
    static constexpr auto name = std::string_view(storage.data(), storage.size());
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
