// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "BasicStringBinder.hpp"
#include "Core.hpp"

#include <cassert>
#include <format>
#include <string>

namespace Lightweight
{

/// SQL dynamic-capacity string that mimicks standard library string.
///
/// The underlying memory is allocated dynamically and the string can grow up to the maximum size of a SQL column.
///
/// @ingroup DataTypes
template <std::size_t N>
class SqlDynamicBinary final
{
    using BaseType = std::vector<uint8_t>;
    BaseType _base;

  public:
    using value_type = uint8_t;
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::VarBinary { N };

    /// The maximum size of the underlying data storage.
    static constexpr std::size_t DynamicCapacity = N;

    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary() noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary(SqlDynamicBinary const&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary& operator=(SqlDynamicBinary const&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary(SqlDynamicBinary&&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary& operator=(SqlDynamicBinary&&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr ~SqlDynamicBinary() noexcept = default;

    /// Constructs a fixed-size string from a string literal.
    template <std::size_t SourceSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlDynamicBinary(value_type const (&text)[SourceSize]):
        _base { text, text + SourceSize }
    {
        static_assert(SourceSize <= N + 1, "RHS string size must not exceed target string's capacity.");
    }

    /// Constructs the object from an initializer list of bytes.
    template <std::size_t SourceSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlDynamicBinary(std::initializer_list<value_type> data):
        _base { data }
    {
        static_assert(SourceSize <= N + 1, "RHS string size must not exceed target string's capacity.");
    }

    /// Constructs a fixed-size string from a string pointer and end pointer.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary(value_type const* s, value_type const* e) noexcept:
        _base { s, e }
    {
    }

    /// Constructs a fixed-size string from a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicBinary(std::basic_string_view<value_type> s) noexcept:
        _base { static_cast<uint8_t const*>(s.data()), static_cast<uint8_t const*>(s.data() + s.size()) }
    {
    }

    /// Retrieves a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<value_type> ToStringView() const noexcept
    {
        return { _base.data(), _base.size() };
    }

    constexpr auto operator<=>(SqlDynamicBinary<N> const&) const noexcept = default;

    /// Retrieves the size of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::size_t size() const noexcept
    {
        return _base.size();
    }

    /// Resizes the underlying data storage to the given size.
    void LIGHTWEIGHT_FORCE_INLINE resize(std::size_t newSize)
    {
        _base.resize(newSize);
    }

    /// Tests if the stored data is empty.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool empty() const noexcept
    {
        return _base.empty();
    }

    /// Retrieves the pointer to the string data.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr decltype(auto) data(this auto&& self) noexcept
    {
        return self._base.data();
    }

    /// Clears the storad data.
    LIGHTWEIGHT_FORCE_INLINE void clear() noexcept
    {
        _base.clear();
    }

  private:
    mutable SQLLEN _indicator = 0;

    friend struct SqlDataBinder<SqlDynamicBinary<N>>;
};

namespace detail
{

    template <typename>
    struct IsSqlDynamicBinaryImpl: std::false_type
    {
    };

    template <size_t T>
    struct IsSqlDynamicBinaryImpl<SqlDynamicBinary<T>>: std::true_type
    {
    };

    template <size_t T>
    struct IsSqlDynamicBinaryImpl<std::optional<SqlDynamicBinary<T>>>: std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsSqlDynamicBinary = detail::IsSqlDynamicBinaryImpl<T>::value;

} // namespace Lightweight

template <std::size_t N>
struct std::formatter<Lightweight::SqlDynamicBinary<N>>: std::formatter<std::string>
{
    auto format(Lightweight::SqlDynamicBinary<N> const& text, format_context& ctx) const
    {
        std::string humanReadableText;
        for (auto byte: text)
        {
            if (byte >= 0x20 && byte <= 0x7E)
                humanReadableText += static_cast<char>(byte);
            else
                humanReadableText += std::format("\\x{:02X}", byte);
        }
        return std::formatter<std::string>::format(humanReadableText.data(), ctx);
    }
};

namespace Lightweight
{

template <std::size_t N>
struct SqlDataBinder<SqlDynamicBinary<N>>
{
    using ValueType = SqlDynamicBinary<N>;

    static constexpr auto ColumnType = SqlColumnTypeDefinitions::VarBinary { N };

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             ValueType const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        value._indicator = static_cast<SQLLEN>(value.size());
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_BINARY,
                                SQL_LONGVARBINARY,
                                value.size(),
                                0,
                                (SQLPOINTER) value.data(),
                                0,
                                &value._indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, ValueType* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        if (result->empty())
            result->resize(255);

        cb.PlanPostProcessOutputColumn([stmt, column, result, indicator]() {
            if (*indicator == SQL_NULL_DATA)
                // The data is NULL.
                result->clear();
            else if (*indicator == SQL_NO_TOTAL)
                // We have a truncation and the server does not know how much data is left.
                result->resize(result->size() - 1);
            else if (*indicator <= static_cast<SQLLEN>(result->size()))
                result->resize(*indicator);
            else
            {
                // We have a truncation and the server knows how much data is left.
                // Extend the buffer and fetch the rest via SQLGetData.

                auto const totalCharsRequired = *indicator;
                result->resize(totalCharsRequired + 1);
                auto const sqlResult =
                    SQLGetData(stmt, column, SQL_C_BINARY, (SQLPOINTER) result->data(), totalCharsRequired + 1, indicator);
                (void) sqlResult;
                assert(SQL_SUCCEEDED(sqlResult));
                assert(*indicator == totalCharsRequired);
                result->resize(totalCharsRequired);
            }
        });

        return SQLBindCol(stmt, column, SQL_C_BINARY, (SQLPOINTER) result->data(), 255, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        ValueType* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        if (result->empty())
            result->resize(255);

        return detail::GetRawColumnArrayData<SQL_C_BINARY>(stmt, column, result, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(ValueType const& value)
    {
        return std::format("SqlBinary<{}>(size={})", N, value.size());
    }
};

} // namespace Lightweight
