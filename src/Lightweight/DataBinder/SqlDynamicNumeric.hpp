// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Lightweight
{

/// The largest number of decimal digits @ref SqlDynamicNumeric carries exactly.
///
/// Bounded by the signed 64-bit integer the unscaled value is stored in. This matches
/// @ref SqlMaxNumericPrecision, the equivalent ceiling on @ref SqlNumeric, so neither type silently
/// accepts a column the other would reject.
inline constexpr std::uint8_t SqlMaxDynamicNumericPrecision = 19;

namespace detail
{
    /// Removes leading and trailing ASCII whitespace.
    [[nodiscard]] constexpr std::string_view TrimAsciiWhitespace(std::string_view text) noexcept
    {
        auto const isSpace = [](char c) noexcept {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };
        while (!text.empty() && isSpace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && isSpace(text.back()))
            text.remove_suffix(1);
        return text;
    }

    /// Appends one decimal digit to @p value, reporting overflow instead of wrapping.
    ///
    /// @retval false The result would exceed `std::int64_t`; @p value is left unchanged.
    [[nodiscard]] constexpr bool TryAppendDecimalDigit(std::int64_t& value, std::int64_t digit) noexcept
    {
        if (value > (INT64_MAX - digit) / 10)
            return false;
        value = (value * 10) + digit;
        return true;
    }

    /// Multiplies @p value by `10^count`, reporting overflow instead of wrapping.
    ///
    /// @retval false The result would exceed `std::int64_t`; @p value is left partially scaled.
    [[nodiscard]] constexpr bool TryScaleByPowerOfTen(std::int64_t& value, std::uint8_t count) noexcept
    {
        for (std::uint8_t i = 0; i < count; ++i)
        {
            if (value > INT64_MAX / 10)
                return false;
            value *= 10;
        }
        return true;
    }

    /// Accumulated state of @ref ParseUnscaledDecimal while it walks the literal.
    struct DecimalParseState
    {
        std::int64_t unscaled = 0;
        std::uint8_t fractionDigits = 0;
        bool sawDecimalPoint = false;
        bool sawDigit = false;
    };

    /// Folds one character of a decimal literal into @p state.
    ///
    /// @retval false The character is not valid at this position, or the value overflowed.
    [[nodiscard]] constexpr bool TryConsumeDecimalCharacter(DecimalParseState& state,
                                                            char character,
                                                            std::uint8_t targetScale) noexcept
    {
        if (character == '.')
        {
            if (state.sawDecimalPoint)
                return false;
            state.sawDecimalPoint = true;
            return true;
        }

        if (character < '0' || character > '9')
            return false;

        if (state.sawDecimalPoint)
        {
            // Digits past the target scale would change the value, so refuse rather than truncate.
            if (state.fractionDigits == targetScale)
                return false;
            ++state.fractionDigits;
        }

        state.sawDigit = true;
        return TryAppendDecimalDigit(state.unscaled, static_cast<std::int64_t>(character - '0'));
    }

    /// Parses a plain decimal literal into its unscaled integer representation at @p targetScale.
    ///
    /// Accepts an optional sign, an optional integral part and an optional fractional part
    /// (`"-12.340"`, `"+.5"`, `"7"`). Surrounding whitespace is ignored. Scientific notation is
    /// deliberately rejected: ODBC drivers do not emit it for DECIMAL/NUMERIC columns, and accepting
    /// it would mean guessing at a value the database expressed exactly.
    ///
    /// The result is the mathematical value multiplied by `10^targetScale`, so `"1.5"` at scale 4
    /// yields `15000`.
    ///
    /// @param text The decimal literal to parse.
    /// @param targetScale Number of fractional digits the returned value is scaled to.
    /// @retval std::nullopt The text is not a plain decimal literal, carries more fractional digits
    ///                      than @p targetScale, or the scaled result overflows `std::int64_t`.
    [[nodiscard]] constexpr std::optional<std::int64_t> ParseUnscaledDecimal(std::string_view text,
                                                                             std::uint8_t targetScale) noexcept
    {
        text = TrimAsciiWhitespace(text);
        if (text.empty())
            return std::nullopt;

        bool isNegative = false;
        if (text.front() == '+' || text.front() == '-')
        {
            isNegative = text.front() == '-';
            text.remove_prefix(1);
        }

        auto state = DecimalParseState {};
        for (char const character: text)
            if (!TryConsumeDecimalCharacter(state, character, targetScale))
                return std::nullopt;

        if (!state.sawDigit)
            return std::nullopt;

        if (!TryScaleByPowerOfTen(state.unscaled, static_cast<std::uint8_t>(targetScale - state.fractionDigits)))
            return std::nullopt;

        return isNegative ? -state.unscaled : state.unscaled;
    }
} // namespace detail

/// @brief A fixed-point decimal whose precision and scale are known only at run time.
///
/// This is the dynamic counterpart to @ref SqlNumeric. Where `SqlNumeric<Precision, Scale>` fixes both
/// at compile time, a value fetched into a @ref SqlVariant learns its precision and scale from the
/// result-set metadata, so they have to travel with the value.
///
/// The number is held exactly, as the unscaled integer `value * 10^scale`, and never passes through a
/// binary floating-point type. That is the point of the type: `DECIMAL(19, 4)` — what MS SQL Server's
/// `money` maps to — carries more significant digits than a `double` can represent, so reading such a
/// column as `double` loses cents on large amounts.
///
/// @ingroup DataTypes
struct SqlDynamicNumeric
{
    /// The value, scaled by `10^scale`. `12.34` at scale 2 is stored as `1234`.
    std::int64_t unscaledValue = 0;

    /// Total number of decimal digits the source column holds.
    std::uint8_t precision = 0;

    /// Number of digits after the decimal point.
    std::uint8_t scale = 0;

    /// Compares two values by mathematical magnitude, so the same number at different scales compares
    /// equal (`1.50` at scale 2 equals `1.5` at scale 1).
    [[nodiscard]] constexpr bool operator==(SqlDynamicNumeric const& other) const noexcept
    {
        if (scale == other.scale)
            return unscaledValue == other.unscaledValue;

        // Lift the coarser-scaled value up to the finer scale, so 1.5 (scale 1) equals 1.50 (scale 2).
        auto const coarser = scale < other.scale ? *this : other;
        auto const finer = scale < other.scale ? other : *this;

        auto rescaled = coarser.unscaledValue;
        auto const negative = rescaled < 0;
        if (negative)
            rescaled = -rescaled;
        if (!detail::TryScaleByPowerOfTen(rescaled, static_cast<std::uint8_t>(finer.scale - coarser.scale)))
            return false;

        return (negative ? -rescaled : rescaled) == finer.unscaledValue;
    }

    /// Inequality, derived from @ref operator==.
    [[nodiscard]] constexpr bool operator!=(SqlDynamicNumeric const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Converts to a floating-point approximation.
    ///
    /// @note Lossy for values needing more significant digits than a `double` carries; read
    ///       @ref unscaledValue together with @ref scale when exactness matters.
    [[nodiscard]] constexpr double ToDouble() const noexcept
    {
        auto result = static_cast<double>(unscaledValue);
        for (std::uint8_t i = 0; i < scale; ++i)
            result /= 10.0;
        return result;
    }

    /// Renders the exact decimal representation, including the trailing zeros implied by @ref scale.
    [[nodiscard]] std::string ToString() const
    {
        auto const negative = unscaledValue < 0;
        // Negate in the unsigned domain so INT64_MIN does not overflow.
        auto const magnitude =
            negative ? 0ULL - static_cast<std::uint64_t>(unscaledValue) : static_cast<std::uint64_t>(unscaledValue);

        auto digits = std::to_string(magnitude);
        if (scale > 0)
        {
            if (digits.size() <= scale)
                digits.insert(0, std::string(scale - digits.size() + 1, '0'));
            digits.insert(digits.size() - scale, ".");
        }
        return negative ? "-" + digits : digits;
    }

    /// Parses an exact decimal literal, scaling it to @p scale.
    ///
    /// @param text The decimal literal, as an ODBC driver renders a DECIMAL/NUMERIC column.
    /// @param precision Total digit count to record on the result.
    /// @param scale Fractional digit count to scale the value to.
    /// @retval std::nullopt @p text is not a plain decimal literal, or does not fit the scale.
    [[nodiscard]] static constexpr std::optional<SqlDynamicNumeric> FromString(std::string_view text,
                                                                               std::uint8_t precision,
                                                                               std::uint8_t scale) noexcept
    {
        auto const unscaled = detail::ParseUnscaledDecimal(text, scale);
        if (!unscaled)
            return std::nullopt;
        return SqlDynamicNumeric { .unscaledValue = *unscaled, .precision = precision, .scale = scale };
    }

    /// ODBC bind scratch: holds the decimal literal handed to the driver, kept alive for the
    /// lifetime of the statement. Mutable because ODBC binds through a const reference to the value,
    /// the same arrangement `SqlDataBinder<SqlBinary>` uses for its length indicator.
    ///
    /// Sized for the widest decimal ODBC admits: 38 digits, a sign, a decimal point and a NUL.
    /// @note Implementation detail — not part of the value, and ignored by comparison.
    mutable std::array<char, 48> bindBuffer {};

    /// ODBC bind scratch: length indicator for @ref bindBuffer.
    /// @note Implementation detail — not part of the value, and ignored by comparison.
    mutable SQLLEN bindIndicator = 0;
};

/// Binds @ref SqlDynamicNumeric as an exact decimal literal.
///
/// Text is deliberately the wire format in both directions. `SQL_C_NUMERIC` needs the application to
/// publish precision and scale on the descriptor before each call and is honoured inconsistently —
/// the SQL Server driver returns scale-0 values without it and the SQLite driver has no native
/// support at all, which is why `SqlDataBinder<SqlNumeric<P, S>>` routes both backends around it.
/// Every supported driver converts between a decimal column and its literal exactly, with no binary
/// floating-point step, so this path keeps all digits on all backends.
template <>
struct SqlDataBinder<SqlDynamicNumeric>
{
    /// Widest decimal ODBC admits, so no supported backend can overflow the bind buffer.
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Decimal { .precision = 38, .scale = 0 };

    /// Binds the value as an input parameter, rendered as an exact decimal literal.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlDynamicNumeric const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        auto const literal = value.ToString();
        auto const length = std::min(literal.size(), value.bindBuffer.size() - 1);
        std::ranges::copy_n(literal.begin(), static_cast<std::ptrdiff_t>(length), value.bindBuffer.begin());
        value.bindBuffer[length] = '\0';
        value.bindIndicator = static_cast<SQLLEN>(length);

        auto const columnSize = static_cast<SQLULEN>(value.precision != 0 ? value.precision : SqlMaxDynamicNumericPrecision);
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_CHAR,
                                SQL_NUMERIC,
                                columnSize,
                                static_cast<SQLSMALLINT>(static_cast<int>(value.scale)),
                                (SQLPOINTER) value.bindBuffer.data(),
                                static_cast<SQLLEN>(value.bindBuffer.size()),
                                &value.bindIndicator);
    }

    /// Retrieves the column as an exact decimal, preserving every digit the column declares.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        SqlDynamicNumeric* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        std::array<char, 64> text {};
        auto const returnCode =
            SQLGetData(stmt, column, SQL_C_CHAR, text.data(), static_cast<SQLLEN>(text.size()), indicator);
        if (!SQL_SUCCEEDED(returnCode) || *indicator == SQL_NULL_DATA)
            return returnCode;

        // A driver that cannot report precision/scale still returned an exact literal, so fall back
        // to deriving the scale from the text rather than failing the fetch.
        SQLLEN declaredPrecision = 0;
        SQLLEN declaredScale = 0;
        (void) SQLColAttributeW(stmt, column, SQL_DESC_PRECISION, nullptr, SQLSMALLINT { 0 }, nullptr, &declaredPrecision);
        (void) SQLColAttributeW(stmt, column, SQL_DESC_SCALE, nullptr, SQLSMALLINT { 0 }, nullptr, &declaredScale);

        auto const literal = std::string_view(text.data(), static_cast<size_t>(*indicator));
        auto const scale = static_cast<std::uint8_t>(std::max<SQLLEN>(declaredScale, 0));
        auto const precision = static_cast<std::uint8_t>(std::max<SQLLEN>(declaredPrecision, 0));
        auto const parsed = SqlDynamicNumeric::FromString(literal, precision, scale);
        if (!parsed)
            return SQL_ERROR;

        *result = *parsed;
        return returnCode;
    }

    /// Renders the value for SQL trace logs.
    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlDynamicNumeric const& value)
    {
        return value.ToString();
    }
};

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlDynamicNumeric>: std::formatter<std::string>
{
    LIGHTWEIGHT_FORCE_INLINE auto format(Lightweight::SqlDynamicNumeric const& value, format_context& ctx) const
        -> format_context::iterator
    {
        return std::formatter<std::string>::format(value.ToString(), ctx);
    }
};
