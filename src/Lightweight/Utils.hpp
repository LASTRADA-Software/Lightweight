// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <reflection-cpp/reflection.hpp>

#include <algorithm>
#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <sql.h>

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
                    // TODO: Build plural
                    auto const typeName = Reflection::TypeNameOf<Record>;
                    if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                        return typeName.substr(i + 1);
                    return typeName;
                }();
        }();
    };

    // specialization for the case when we use tuple as
    // a record, then we usethe first element of the tuple
    // to get the table name
    template <typename First, typename Second>
    struct RecordTableNameImpl<std::tuple<First, Second>>
    {
        static constexpr std::string_view Value = []() {
            if constexpr (requires { First::TableName; })
                return First::TableName;
            else
                return []() {
                    // TODO: Build plural
                    auto const typeName = Reflection::TypeNameOf<First>;
                    if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                        return typeName.substr(i + 1);
                    return typeName;
                }();
        }();
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
constexpr inline std::string_view FieldNameOf = detail::FieldNameOfImpl<decltype(ReferencedField), ReferencedField>::value;

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

    [[nodiscard]] constexpr auto begin() const noexcept
    {
        return value.begin();
    }

    [[nodiscard]] constexpr auto end() const noexcept
    {
        return value.end();
    }

    [[nodiscard]] constexpr auto size() const noexcept
    {
        return value.size();
    }

    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return value.empty();
    }

    [[nodiscard]] constexpr auto data() const noexcept
    {
        return value.data();
    }

    [[nodiscard]] constexpr std::string_view string_view() const noexcept
    {
        return value;
    }

    [[nodiscard]] constexpr std::string to_string() const
    {
        return std::string(value);
    }
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

namespace detail
{
    template <auto... ReferencedFields>
    struct QuotedFieldNamesOfImpl
    {
        static constexpr auto StorageSize =
            1 + (2 * (sizeof...(ReferencedFields) - 1)) + (0 + ... + FullFieldNameOf<ReferencedFields>.size());

        static constexpr std::array<char, StorageSize> Storage = []() consteval {
            auto result = std::array<char, StorageSize> {};
            size_t offset = 0;
            (
                [&] {
                    if (offset > 0)
                    {
                        constexpr auto Delimiter = std::string_view(", ");
                        std::ranges::copy(Delimiter, result.begin() + offset);
                        offset += Delimiter.size();
                    }
                    std::ranges::copy(FullFieldNameOf<ReferencedFields>, result.begin() + offset);
                    offset += FullFieldNameOf<ReferencedFields>.size();
                }(),
                ...);
            result.back() = '\0';
            return result;
        }();

        static constexpr auto value = std::string_view(Storage.data(), Storage.size() - 1);
    };

} // namespace detail

/// @brief Holds the quoted fully qualified field names of the given fields.
///
/// @code
/// auto const quotedFieldNames = QuotedFieldNamesOf<&Person::id, &Person::name, &Person::age>;
/// static_assert(quotedFieldNames.value == R"sql("Person"."id", "Person"."name", "Person"."age")sql");
/// @endcode
///
/// @ingroup DataMapper
template <auto... ReferencedFields>
constexpr inline auto QuotedFieldNamesOf = SqlRawColumnNameView {
    .value = detail::QuotedFieldNamesOfImpl<ReferencedFields...>::value,
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
