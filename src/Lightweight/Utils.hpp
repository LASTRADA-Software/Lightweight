// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <reflection-cpp/reflection.hpp>

#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <sql.h>

#if defined(CXX26_REFLECTION)
    #include <experimental/meta>
#endif

namespace Lightweight
{

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
#if defined(CXX26_REFLECTION)
                    return std::meta::identifier_of(^^Record);
#else
                    auto const typeName = Reflection::TypeNameOf<Record>;
                    if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                        return typeName.substr(i + 1);
                    return typeName;
#endif
                }();
        }();
    };

    // specialization for the case when we use tuple as
    // a record, then we use the first element of the tuple
    // to get the table name
    template <typename First, typename Second>
    struct RecordTableNameImpl<std::tuple<First, Second>>
    {
        static constexpr std::string_view Value = []() {
            if constexpr (requires { First::TableName; })
                return First::TableName;
            else
                return []() {
#if defined(CXX26_REFLECTION)
                    return std::meta::identifier_of(^^First);
#else
                    auto const typeName = Reflection::TypeNameOf<First>;
                    if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                        return typeName.substr(i + 1);
                    return typeName;
#endif
                }();
        }();
    };

    template <std::size_t I, typename Record>
    struct BelongsToNameImpl
    {
        static constexpr auto baseName = Reflection::MemberNameOf<I, Record>;
        static constexpr auto storage = []() -> std::array<char, baseName.size() + 4>
        {
            std::array<char, baseName.size() + 4> storage {};
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

#if defined(CXX26_REFLECTION)
    template <auto reflection>
    struct FieldNameOfImpl
    {
        using R = [:std::meta::type_of(reflection):];
        static constexpr std::string_view value = []() constexpr -> std::string_view {
            if constexpr (requires { R::ColumnNameOverride; })
            {
                if constexpr (!R::ColumnNameOverride.empty())
                    return R::ColumnNameOverride;
            }
            return std::meta::identifier_of(reflection);
        }();
    };
#else
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
#endif // CXX26_REFLECTION

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

/// @brief Holds the SQL tabl ename for the given record type.
///
/// @ingroup DataMapper
template <typename Record>
constexpr std::string_view RecordTableName = detail::RecordTableNameImpl<Record>::Value;

template <template <typename...> class S, class T>
concept IsSpecializationOf = detail::is_specialization_of<S, T>::value;

#if defined(CXX26_REFLECTION)

    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x)    ^^x

/// @brief Returns the name of the field referenced by the given pointer-to-member.
///
/// This also supports custom column name overrides.
template <std::meta::info ReflectionOfField>
constexpr inline std::string_view FieldNameOf = detail::FieldNameOfImpl<ReflectionOfField>::value;

template <auto Member>
using MemberClassType = typename[:std::meta::parent_of(Member):];

template <auto Member>
constexpr size_t MemberIndexOf = []() consteval -> size_t {
    int index { -1 };
    auto ctx = std::meta::access_context::current();
    for (auto info: nonstatic_data_members_of(parent_of(Member), ctx))
    {
        ++index;
        if (info == Member)
            return index;
    }
    return -1;
}();
#else // not CXX26_REFLECTION

    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x)    &x

/// @brief Returns the name of the field referenced by the given pointer-to-member.
///
/// This also supports custom column name overrides.
template <auto ReferencedField>
constexpr inline std::string_view FieldNameOf = detail::FieldNameOfImpl<decltype(ReferencedField), ReferencedField>::value;

template <auto Member>
constexpr size_t MemberIndexOf = Reflection::MemberIndexOf<Member>;

template <typename T>
using MemberClassType = typename detail::MemberClassTypeHelper<T>::type;

#endif // CXX26_REFLECTION

namespace detail
{
    template <auto ReferencedField>
    struct FullFieldNameOfImpl
    {
#if defined(CXX26_REFLECTION)
        static constexpr auto ClassName = RecordTableName<typename[:std::meta::parent_of(ReferencedField):]>;
#else
        static constexpr auto ClassName = RecordTableName<MemberClassType<decltype(ReferencedField)>>;
#endif
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

/// Defines the naming convention for use (e.g. for C++ column names or table names in C++ struct names).
enum class FormatType : uint8_t
{
    /// Preserve the original naming convention.
    preserve,

    /// Ensure the name is formatted in snake_case naming convention.
    snakeCase,

    /// Ensure the name is formatted in CamelCase naming convention.
    camelCase,
};

/// @brief Converts a string to a format that is more suitable for C++ code.
LIGHTWEIGHT_API std::string FormatName(std::string const& name, FormatType formatType);

/// @brief Converts a string to a format that is more suitable for C++ code.
LIGHTWEIGHT_API std::string FormatName(std::string_view name, FormatType formatType);

/// Maintains collisions to create unique names
class UniqueNameBuilder
{
  public:
    /// Tests if the given name is already registered.
    [[nodiscard]] LIGHTWEIGHT_API bool IsColliding(std::string const& name) const noexcept;

    /// Tries to declare a name and returns it, otherwise returns std::nullopt.
    [[nodiscard]] LIGHTWEIGHT_API std::optional<std::string> TryDeclareName(std::string name);

    /// Creates a name that is definitely not colliding.
    [[nodiscard]] LIGHTWEIGHT_API std::string DeclareName(std::string name);

  private:
    std::unordered_map<std::string, size_t> _collisionMap;
};

} // namespace Lightweight
