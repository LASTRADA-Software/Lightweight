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
struct RecordTableName
{
    static constexpr std::string_view Value = []() {
        if constexpr (requires { Record::TableName; })
            return Record::TableName;
        else
            return []() {
                // TODO: Build plural
                auto const typeName = Reflection::TypeName<Record>;
                if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                    return typeName.substr(i + 1);
                return typeName;
            }();
    }();
};

} // namespace detail

/// @brief Holds the SQL tabl ename for the given record type.
///
/// @ingroup DataMapper
template <typename Record>
constexpr std::string_view RecordTableName = detail::RecordTableName<Record>::Value;

template <template <typename...> class S, class T>
concept IsSpecializationOf = detail::is_specialization_of<S, T>::value;

template <typename T>
using MemberClassType = typename detail::MemberClassTypeHelper<T>::type;
