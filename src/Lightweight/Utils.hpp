// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <reflection-cpp/reflection.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

// libc++ exposes the locale-aware strtod_l / newlocale family via <xlocale.h>; glibc declares them in
// <stdlib.h>/<locale.h>. We only need them on the fallback path below (no float std::from_chars).
#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
    #include <clocale>
    #if defined(__APPLE__)
        #include <xlocale.h>
    #else
        #include <locale.h>
    #endif
#endif

#include <sql.h>

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
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

    /// Parses a floating-point value from the character range @c [first, last) in a locale-independent way.
    ///
    /// This is the single, shared replacement for @c std::from_chars on floating-point types, which is
    /// unavailable on libc++ before macOS 26. Unlike a bare @c std::strtod it neither depends on the active
    /// @c LC_NUMERIC locale nor silently accepts trailing garbage, so it round-trips the @c '.' decimal point
    /// the library always emits regardless of the host locale.
    ///
    /// @tparam T A floating-point type (@c float, @c double, or @c long double).
    /// @param first Pointer to the first character of the numeric text.
    /// @param last Pointer one past the last character of the numeric text.
    /// @return The parsed value, or @c std::nullopt if the text is not a complete, in-range number.
    template <typename T>
        requires std::is_floating_point_v<T>
    [[nodiscard]] inline std::optional<T> ParseFloat(char const* first, char const* last) noexcept
    {
        if (first == last)
            return std::nullopt;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        // std::from_chars is locale-independent, allocation-free, and reports both partial parses (ptr) and
        // range errors (ec) — the preferred path wherever the float overloads exist.
        T value {};
        auto const [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc {} || ptr != last)
            return std::nullopt;
        return value;
#else
        // libc++ fallback: strtod_l with a persistent "C" locale gives locale-independent parsing; from_chars
        // float overloads are unavailable here. Copy into a NUL-terminated buffer (the range need not be) and
        // require the parse to consume all of it (mirrors the from_chars ptr==last check). On-stack for the
        // common short numeric text; only the rare over-long token allocates.
        static ::locale_t const cLocale = ::newlocale(LC_NUMERIC_MASK, "C", static_cast<::locale_t>(nullptr));
        auto const length = static_cast<std::size_t>(last - first);
        std::array<char, 64> stackBuffer {};
        std::string heapBuffer;
        char const* text = nullptr;
        if (length < stackBuffer.size())
        {
            std::ranges::copy(first, last, stackBuffer.begin());
            stackBuffer[length] = '\0';
            text = stackBuffer.data();
        }
        else
        {
            heapBuffer.assign(first, last);
            text = heapBuffer.c_str();
        }

        char* parseEnd = nullptr;
        errno = 0;
        T value {};
        if constexpr (std::is_same_v<T, float>)
            value = ::strtof_l(text, &parseEnd, cLocale);
        else if constexpr (std::is_same_v<T, long double>)
            value = ::strtold_l(text, &parseEnd, cLocale);
        else
            value = static_cast<T>(::strtod_l(text, &parseEnd, cLocale));

        if (errno == ERANGE || parseEnd != text + length)
            return std::nullopt;
        return value;
#endif
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
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
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
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
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

    template <typename FieldType>
    constexpr auto ColumnNameOverride = []() consteval {
        if constexpr (requires { FieldType::ColumnNameOverride; })
            return FieldType::ColumnNameOverride;
        else
            return std::string_view {};
    }();
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    template <auto reflection>
    struct FieldNameOfImpl
    {
        using R = typename[:std::meta::type_of(reflection):];
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
#endif // LIGHTWEIGHT_CXX26_REFLECTION

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

/// @brief Holds the SQL tabl ename for the given record type.
///
/// @ingroup DataMapper
template <typename Record>
constexpr std::string_view RecordTableName = detail::RecordTableNameImpl<Record>::Value;

template <template <typename...> class S, class T>
concept IsSpecializationOf = detail::is_specialization_of<S, T>::value;

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)

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
    auto members = nonstatic_data_members_of(parent_of(Member), std::meta::access_context::current());
    if (auto it = std::ranges::find(members, Member); it != members.end())
    {
        index = std::distance(members.begin(), it);
        return static_cast<size_t>(index);
    }
    return -1;
}();
#else // not LIGHTWEIGHT_CXX26_REFLECTION

/// @brief Returns the name of the field referenced by the given pointer-to-member.
///
/// This also supports custom column name overrides.
template <auto ReferencedField>
constexpr inline std::string_view FieldNameOf = detail::FieldNameOfImpl<decltype(ReferencedField), ReferencedField>::value;

template <auto Member>
constexpr size_t MemberIndexOf = Reflection::MemberIndexOf<Member>;

template <typename T>
using MemberClassType = detail::MemberClassTypeHelper<T>::type;

#endif // LIGHTWEIGHT_CXX26_REFLECTION

/// @brief SqlQualifiedTableColumnName represents a column name qualified with a table name.
///
/// This is the single structural representation of a `table.column` reference used
/// throughout the query builder API. The builder is responsible for quoting; do not
/// pre-quote the values stored here.
///
/// @ingroup QueryBuilder
struct SqlQualifiedTableColumnName
{
    /// The table name.
    std::string_view tableName;
    /// The column name.
    std::string_view columnName;

    /// Three-way comparison operator.
    constexpr std::weak_ordering operator<=>(SqlQualifiedTableColumnName const&) const noexcept = default;
};

/// @brief Holds the fully qualified column reference (table + column) for the given field.
/// @tparam ReferencedField A pointer-to-member identifying the field.
///
/// @code
/// constexpr auto ref = FullyQualifiedNameOf<&Person::id>;
/// static_assert(ref.tableName == "Person");
/// static_assert(ref.columnName == "id");
/// @endcode
///
/// The result is an `SqlQualifiedTableColumnName` accepted by every column-name
/// entry point in the builder (`Field`, `Fields`, `Where`, `OrderBy`, `GroupBy`,
/// `Aggregate::*`, joins). The builder applies the quoting.
///
/// @ingroup DataMapper
template <auto ReferencedField>
constexpr inline auto FullyQualifiedNameOf = SqlQualifiedTableColumnName {
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    .tableName = RecordTableName<typename[:std::meta::parent_of(ReferencedField):]>,
#else
    .tableName = RecordTableName<MemberClassType<decltype(ReferencedField)>>,
#endif
    .columnName = FieldNameOf<ReferencedField>,
};

namespace detail
{
    template <auto ReferencedField>
    struct FullyQualifiedQuotedNameOfImpl
    {
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
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

    template <auto ReferencedField>
    constexpr inline auto FullyQualifiedQuotedNameOf = FullyQualifiedQuotedNameOfImpl<ReferencedField>::value;

    template <auto... ReferencedFields>
    struct FullyQualifiedNamesOfImpl
    {
        static constexpr auto StorageSize =
            1 + (2 * (sizeof...(ReferencedFields) - 1)) + (0 + ... + FullyQualifiedQuotedNameOf<ReferencedFields>.size());

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
                    std::ranges::copy(FullyQualifiedQuotedNameOf<ReferencedFields>, result.begin() + offset);
                    offset += FullyQualifiedQuotedNameOf<ReferencedFields>.size();
                }(),
                ...);
            result.back() = '\0';
            return result;
        }();

        static constexpr auto value = std::string_view(Storage.data(), Storage.size() - 1);
    };

    /// Pre-quoted, comma-joined fully qualified field names of the given fields.
    /// Internal helper used by DataMapper to embed column lists into SQL text directly.
    template <auto... ReferencedFields>
    constexpr inline auto FullyQualifiedNamesOf = FullyQualifiedNamesOfImpl<ReferencedFields...>::value;

} // namespace detail

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
