// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../DataBinder/UnicodeConverter.hpp"
#include "../SqlLogger.hpp"
#include "Core.hpp"
#include "MFCStringLike.hpp"
#include "Primitives.hpp"
#include "SqlDate.hpp"
#include "SqlDateTime.hpp"
#include "SqlFixedString.hpp"
#include "SqlGuid.hpp"
#include "SqlNullValue.hpp"
#include "SqlText.hpp"
#include "SqlTime.hpp"
#include "StdString.hpp"
#include "StdStringView.hpp"

#include <format>
#include <print>
#include <variant>

namespace Lightweight
{

namespace detail
{
    template <class... Ts>
    struct overloaded: Ts... // NOLINT(readability-identifier-naming)
    {
        using Ts::operator()...;
    };

    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

} // namespace detail

/// @brief Represents a value that can be any of the supported SQL data types.
///
/// Use this class with care. Always prefer native types when possible, in order to avoid any unnecessary overhead.
///
/// @ingroup DataTypes
struct SqlVariant
{
    /// @brief The inner type of the variant.
    ///
    /// This type is a variant of all the supported SQL data types.
    using InnerType = std::variant<SqlNullType,
                                   SqlGuid,
                                   bool,
                                   int8_t,
                                   short,
                                   unsigned short,
                                   int,
                                   unsigned int,
                                   long long,
                                   unsigned long long,
                                   float,
                                   double,
                                   std::string,
                                   std::string_view,
                                   std::u16string,
                                   std::u16string_view,
                                   SqlText,
                                   SqlDate,
                                   SqlTime,
                                   SqlDateTime>;

    InnerType value;

    /// @brief Default construct a new SqlVariant.
    SqlVariant() = default;
    /// @brief Copy construct a new SqlVariant from another.
    SqlVariant(SqlVariant const&) = default;
    /// @brief Move construct a new SqlVariant from another.
    SqlVariant(SqlVariant&&) noexcept = default;
    /// @brief Copy assign a new SqlVariant from another.
    SqlVariant& operator=(SqlVariant const&) = default;
    /// @brief Move assign a new SqlVariant from another.
    SqlVariant& operator=(SqlVariant&&) noexcept = default;
    /// @brief Destructor for SqlVariant.
    ~SqlVariant() = default;

    /// @brief Copy constructor of a SqlVariant from one of the supported types.
    LIGHTWEIGHT_FORCE_INLINE SqlVariant(InnerType const& other):
        value(other)
    {
    }

    /// @brief Move constructor of a SqlVariant from one of the supported types.
    LIGHTWEIGHT_FORCE_INLINE SqlVariant(InnerType&& other) noexcept:
        value(std::move(other))
    {
    }

    /// @brief Construct a new SqlVariant from a SqlFixedString.
    template <std::size_t N, typename T = char, SqlFixedStringMode Mode>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlVariant(SqlFixedString<N, T, Mode> const& other):
        value { std::string_view { other.data(), other.size() } }
    {
    }

    /// @brief Copy constructor of a SqlVariant from a char array.
    template <std::size_t TextSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlVariant(char const (&text)[TextSize]):
        value { std::string_view { text, TextSize - 1 } }
    {
    }

    /// @brief Copy constructor of a SqlVariant from a char16_t array.
    template <std::size_t TextSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlVariant(char16_t const (&text)[TextSize]):
        value { std::u16string_view { text, TextSize - 1 } }
    {
    }

    /// @brief Copy constructor of a SqlVariant from an optional of one of the supported types.
    template <typename T>
    LIGHTWEIGHT_FORCE_INLINE SqlVariant(std::optional<T> const& other):
        value { other ? InnerType { *other } : InnerType { SqlNullValue } }
    {
    }

    /// @brief Assignment operator of a SqlVariant from one of the supported types.
    LIGHTWEIGHT_FORCE_INLINE SqlVariant& operator=(InnerType const& other)
    {
        value = other;
        return *this;
    }

    /// @brief Assignment operator of a SqlVariant from one of the supported types.
    LIGHTWEIGHT_FORCE_INLINE SqlVariant& operator=(InnerType&& other) noexcept
    {
        value = std::move(other);
        return *this;
    }

    /// @brief Construct from an string-like object that implements an SqlViewHelper<>.
    template <detail::HasSqlViewHelper StringViewLike>
    LIGHTWEIGHT_FORCE_INLINE explicit SqlVariant(StringViewLike const* newValue):
        value { detail::SqlViewHelper<std::remove_cv_t<decltype(*newValue)>>::View(*newValue) }
    {
    }

    /// @brief Assign from an string-like object that implements an SqlViewHelper<>.
    template <detail::HasSqlViewHelper StringViewLike>
    LIGHTWEIGHT_FORCE_INLINE SqlVariant& operator=(StringViewLike const* newValue) noexcept
    {
        value = std::string_view(newValue->GetString(), newValue->GetLength());
        return *this;
    }

    /// @brief Check if the value is NULL.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE bool IsNull() const noexcept
    {
        return std::holds_alternative<SqlNullType>(value);
    }

    /// @brief Check if the value is of the specified type.
    template <typename T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE bool Is() const noexcept
    {
        return std::holds_alternative<T>(value);
    }

    /// @brief Retrieve the value as the specified type.
    template <typename T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE decltype(auto) Get() noexcept
    {
        if constexpr (IsSpecializationOf<std::optional, T>)
        {
            if (IsNull())
                return T { std::nullopt };
            else
                return T { std::get<typename T::value_type>(value) };
        }
        else
            return std::get<T>(value);
    }

    /// @brief Retrieve the value as the specified type, or return the default value if the value is NULL.
    template <typename T>
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE T ValueOr(T&& defaultValue) const noexcept
    {
        if constexpr (std::is_integral_v<T>)
            return TryGetIntegral<T>().value_or(std::forward<T>(defaultValue));

        if (IsNull())
            return std::forward<T>(defaultValue);

        return std::get<T>(value);
    }

    // clang-format off
    /// @brief Retrieve the bool from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<bool> TryGetBool() const noexcept { return TryGetIntegral<bool>(); }
    /// @brief Retrieve the int8_t from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<int8_t> TryGetInt8() const noexcept { return TryGetIntegral<int8_t>(); }
    /// @brief Retrieve the unsigned short from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<short> TryGetShort() const noexcept { return TryGetIntegral<short>(); }
    /// @brief Retrieve the unsigned short from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<unsigned short> TryGetUShort() const noexcept { return TryGetIntegral<unsigned short>(); }
    /// @brief Retrieve the int from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<int> TryGetInt() const noexcept { return TryGetIntegral<int>(); }
    /// @brief Retrieve the unsigned int from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<unsigned int> TryGetUInt() const noexcept { return TryGetIntegral<unsigned int>(); }
    /// @brief Retrieve the long long from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<long long> TryGetLongLong() const noexcept { return TryGetIntegral<long long>(); }
    /// @brief Retrieve the unsigned long long from the variant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<unsigned long long> TryGetULongLong() const noexcept { return TryGetIntegral<unsigned long long>(); }
    // clang-format on

  private:
    /// @brief template that is used to get integral types
    template <typename ResultType>
    [[nodiscard]] std::optional<ResultType> TryGetIntegral() const noexcept
    {
        if (IsNull())
            return std::nullopt;

        // clang-format off
        return std::visit(detail::overloaded {
            []<typename T>(T v) -> ResultType requires(std::is_integral_v<T>) { return static_cast<ResultType>(v); },
            [](auto) -> ResultType { throw std::bad_variant_access(); } // NOLINT(performance-unnecessary-value-param)
        }, value);
        // clang-format on
    }

  public:
    /// @brief function to get string_view from SqlVariant or std::nullopt
    [[nodiscard]] std::optional<std::string_view> TryGetStringView() const noexcept
    {
        if (IsNull())
            return std::nullopt;

        // clang-format off
        return std::visit(detail::overloaded {
            [](std::string_view v) { return v; },
            [](std::string const& v) { return std::string_view(v.data(), v.size()); },
            [](SqlText const& v) { return std::string_view(v.value.data(), v.value.size()); },
            [](auto const&) -> std::string_view { throw std::bad_variant_access(); }
        }, value);
        // clang-format on
    }

    [[nodiscard]] std::optional<std::u16string_view> TryGetUtf16StringView() const noexcept
    {
        if (IsNull())
            return std::nullopt;

        // clang-format off
        return std::visit(detail::overloaded {
            [](std::u16string_view v) { return v; },
            [](std::u16string const& v) { return std::u16string_view(v.data(), v.size()); },
            [](auto const&) -> std::u16string_view { throw std::bad_variant_access(); }
        }, value);
        // clang-format on
    }

    /// @brief function to get SqlDate from SqlVariant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<SqlDate> TryGetDate() const
    {
        if (IsNull())
            return std::nullopt;

        if (auto const* date = std::get_if<SqlDate>(&value))
            return *date;

        if (auto const* datetime = std::get_if<SqlDateTime>(&value))
        {
            return SqlDate { std::chrono::year(datetime->sqlValue.year),
                             std::chrono::month(datetime->sqlValue.month),
                             std::chrono::day(datetime->sqlValue.day) };
        }

        throw std::bad_variant_access();
    }

    [[nodiscard]] bool operator==(SqlVariant const& other) const noexcept
    {
        return ToString() == other.ToString();
    }

    [[nodiscard]] bool operator!=(SqlVariant const& other) const noexcept
    {
        return !(*this == other);
    }

    /// @brief function to get SqlTime from SqlVariant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<SqlTime> TryGetTime() const
    {
        if (IsNull())
            return std::nullopt;

        if (auto const* time = std::get_if<SqlTime>(&value))
            return *time;

        if (auto const* datetime = std::get_if<SqlDateTime>(&value))
        {
            return SqlTime { std::chrono::hours(datetime->sqlValue.hour),
                             std::chrono::minutes(datetime->sqlValue.minute),
                             std::chrono::seconds(datetime->sqlValue.second),
                             std::chrono::microseconds(datetime->sqlValue.fraction) };
        }

        throw std::bad_variant_access();
    }

    /// @brief function to get SqlDateTime from SqlVariant or std::nullopt
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<SqlDateTime> TryGetDateTime() const
    {
        if (IsNull())
            return std::nullopt;

        if (auto const* dateTime = std::get_if<SqlDateTime>(&value))
            return *dateTime;

        throw std::bad_variant_access();
    }

    /// @brief Retrieve the GUID from the variant or std::nullopt if the value is NULL.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::optional<SqlGuid> TryGetGuid() const
    {
        if (IsNull())
            return std::nullopt;

        if (auto const* guid = std::get_if<SqlGuid>(&value))
            return *guid;

        throw std::bad_variant_access();
    }

    /// @brief Create string representation of the variant. Can be used for debug purposes
    [[nodiscard]] LIGHTWEIGHT_API std::string ToString() const;
};

/// @brief Represents a row of data from the database using SqlVariant as the column data type.
using SqlVariantRow = std::vector<SqlVariant>;

template <>
struct LIGHTWEIGHT_API SqlDataBinder<SqlVariant>
{
    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    SqlVariant const& variantValue,
                                    SqlDataBinderCallback& cb) noexcept;

    static SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlVariant* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept;

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlVariant const& value) noexcept
    {
        return value.ToString();
    }
};

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlVariant>: formatter<string>
{
    auto format(Lightweight::SqlVariant const& value, format_context& ctx) const -> format_context::iterator
    {
        return std::formatter<string>::format(value.ToString(), ctx);
    }
};
