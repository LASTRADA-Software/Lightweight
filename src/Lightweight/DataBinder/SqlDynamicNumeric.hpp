// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlError.hpp"
#include "Core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
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

    /// Powers of ten a signed 64-bit integer holds exactly: `10^0` through `10^18`.
    ///
    /// Indexed by scale, so an exact rescale multiplies once. Repeating a multiplication or division
    /// by ten instead rounds at every step and drifts: 123456789 at scale 9 comes back as
    /// 0.12345678900000004 rather than 0.123456789.
    ///
    /// @note The table deliberately stops one short of @ref SqlMaxDynamicNumericPrecision. `10^19`
    ///       exceeds `INT64_MAX`, so an entry for it could only hold a wrong value — and a saturating
    ///       generator would make it silently alias `10^18`, scaling a DECIMAL(19, 19) by ten.
    ///       @ref DoublePowersOfTen covers that last scale, where the value is representable.
    inline constexpr std::array<std::int64_t, SqlMaxDynamicNumericPrecision> PowersOfTen = [] {
        auto powers = std::array<std::int64_t, SqlMaxDynamicNumericPrecision> {};
        auto value = std::int64_t { 1 };
        for (auto& power: powers)
        {
            power = value;
            // Guards only the step past the final entry, whose result is never stored.
            if (value <= INT64_MAX / 10)
                value *= 10;
        }
        return powers;
    }();

    /// Powers of ten as exact doubles, covering every scale @ref SqlDynamicNumeric admits.
    ///
    /// A double represents `10^n` exactly for n up to 22 — the significand only has to hold `5^n` —
    /// so even the widest scale still converts in a single division.
    inline constexpr std::array<double, SqlMaxDynamicNumericPrecision + 1> DoublePowersOfTen = [] {
        auto powers = std::array<double, SqlMaxDynamicNumericPrecision + 1> {};
        auto value = 1.0;
        for (auto& power: powers)
        {
            power = value;
            value *= 10.0;
        }
        return powers;
    }();

    /// Multiplies @p value by `10^count`, reporting overflow instead of wrapping.
    ///
    /// Handles negative values, so the caller need not strip the sign first.
    ///
    /// @retval false The result would exceed `std::int64_t`; @p value is left unchanged.
    [[nodiscard]] constexpr bool TryScaleByPowerOfTen(std::int64_t& value, std::uint8_t count) noexcept
    {
        // 10^count is beyond the exact table, so it overflows int64 for every value but zero —
        // which needs no scaling at all.
        if (count >= PowersOfTen.size())
            return value == 0;

        auto const factor = PowersOfTen[count];
        // Compare against the bound that matches the sign: INT64_MIN has no positive counterpart,
        // so negating first would itself overflow.
        if (value > 0 && value > INT64_MAX / factor)
            return false;
        if (value < 0 && value < INT64_MIN / factor)
            return false;

        value *= factor;
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

    /// Counts the digits after the decimal point in a plain decimal literal.
    ///
    /// Used to recover the scale when a driver does not report one: PostgreSQL's `numeric` without a
    /// typmod, and expression columns such as `SELECT SUM(amount)`, carry no declared scale, but the
    /// literal the driver returns still states it exactly.
    ///
    /// @return Zero if there is no fractional part, or the text is not a plain decimal literal.
    [[nodiscard]] constexpr std::uint8_t FractionDigitsOf(std::string_view text) noexcept
    {
        // Trim exactly as ParseUnscaledDecimal does. A driver that pads the literal would otherwise
        // make the two disagree about the same string: this would score "12.34  " as zero fractional
        // digits, and the parse would then reject the digits it was never told to expect.
        text = TrimAsciiWhitespace(text);

        auto const decimalPoint = text.find('.');
        if (decimalPoint == std::string_view::npos)
            return 0;

        auto digits = std::size_t { 0 };
        for (char const character: text.substr(decimalPoint + 1))
        {
            if (character < '0' || character > '9')
                return 0;
            ++digits;
        }
        return static_cast<std::uint8_t>(std::min<std::size_t>(digits, SqlMaxDynamicNumericPrecision));
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

        // TryScaleByPowerOfTen handles a negative value directly; negating INT64_MIN to strip the
        // sign first would itself be undefined.
        auto rescaled = coarser.unscaledValue;
        if (!detail::TryScaleByPowerOfTen(rescaled, static_cast<std::uint8_t>(finer.scale - coarser.scale)))
            return false;

        return rescaled == finer.unscaledValue;
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
        // A scale past SqlMaxDynamicNumericPrecision describes no value this type can hold.
        if (scale == 0 || scale >= detail::DoublePowersOfTen.size())
            return static_cast<double>(unscaledValue);

        // One division, not `scale` of them: dividing by ten repeatedly rounds at every step, so
        // 123456789 at scale 9 would come back as 0.12345678900000004. The divisor comes from the
        // double table rather than the int64 one, which stops at 10^18 and so cannot express the
        // divisor for a DECIMAL(19, 19).
        return static_cast<double>(unscaledValue) / detail::DoublePowersOfTen[scale];
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
                                                             SqlDataBinderCallback& cb) noexcept
    {
        // Stage the literal on the statement rather than in the value: the buffer only has to
        // outlive the execute, and carrying it inside the type would put ~130 bytes of ODBC scratch
        // into every SqlVariant — and so into every row of every prefetch block.
        auto const literal = value.ToString();
        auto* const buffer = reinterpret_cast<char*>(cb.ProvideBatchStagingBuffer(literal.size() + 1));
        std::ranges::copy(literal, buffer);
        buffer[literal.size()] = '\0';

        auto* const indicator = cb.ProvideInputIndicator();
        *indicator = static_cast<SQLLEN>(literal.size());

        auto const columnSize = static_cast<SQLULEN>(value.precision != 0 ? value.precision : SqlMaxDynamicNumericPrecision);
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_CHAR,
                                SQL_NUMERIC,
                                columnSize,
                                static_cast<SQLSMALLINT>(value.scale),
                                (SQLPOINTER) buffer,
                                static_cast<SQLLEN>(literal.size() + 1),
                                indicator);
    }

    /// A decimal column's text, as delivered by a single SQLGetData call.
    struct Literal
    {
        /// What SQLGetData reported, or SQL_ERROR when the text did not fit @ref text's buffer.
        SQLRETURN returnCode = SQL_SUCCESS;
        /// The literal, or empty when the column is NULL, the read failed, or the text was truncated.
        std::string_view text;
        /// Whether the column was NULL. Reported here rather than left to the caller's @c indicator,
        /// which @ref ReadLiteral accepts as null — a caller passing none could not otherwise tell a
        /// NULL apart from a failed read.
        bool isNull = false;
    };

    /// Reads the column's decimal literal into @p buffer, issuing exactly one SQLGetData.
    ///
    /// ODBC allows a second SQLGetData on the same column only while a character or binary value is
    /// still being delivered in parts. Once it has arrived in full, a conforming driver answers
    /// SQL_NO_DATA — SQL Server's does. So a caller that wants the value exactly and, failing that,
    /// approximately must derive both from this one read rather than retrieving the column twice.
    ///
    /// @param buffer Storage for the text; @ref Literal::text points into it, so it must outlive the
    ///               returned value. 128 bytes is always enough: ODBC caps DECIMAL precision at 38,
    ///               leaving room for the sign, decimal point, terminator and driver padding.
    [[nodiscard]] static LIGHTWEIGHT_FORCE_INLINE Literal ReadLiteral(SQLHSTMT stmt,
                                                                      SQLUSMALLINT column,
                                                                      SQLLEN* indicator,
                                                                      std::span<char> buffer) noexcept
    {
        // SQLGetData needs somewhere to report the length even when the caller does not want it, and
        // the truncation check below is not optional — it is what stops a clipped literal parsing
        // into a plausible-looking wrong number.
        SQLLEN discardedIndicator = 0;
        auto* const lengthIndicator = indicator ? indicator : &discardedIndicator;

        auto const returnCode =
            SQLGetData(stmt, column, SQL_C_CHAR, buffer.data(), static_cast<SQLLEN>(buffer.size()), lengthIndicator);
        if (!SQL_SUCCEEDED(returnCode))
            return { .returnCode = returnCode, .text = {}, .isNull = false };
        if (*lengthIndicator == SQL_NULL_DATA)
            return { .returnCode = returnCode, .text = {}, .isNull = true };

        // The indicator reports the bytes *available*, not the bytes written: on truncation it
        // exceeds the buffer (alongside SQL_SUCCESS_WITH_INFO, which SQL_SUCCEEDED accepts), and it
        // is SQL_NO_TOTAL when the driver cannot say. Neither is a usable length, so measure the
        // NUL-terminated text the driver actually wrote, and refuse a value that did not fit rather
        // than parsing a truncated literal into a plausible-looking wrong number.
        auto const truncated = *lengthIndicator == SQL_NO_TOTAL
                               || (*lengthIndicator >= 0 && static_cast<std::size_t>(*lengthIndicator) >= buffer.size());
        if (truncated)
            return { .returnCode = SQL_ERROR, .text = {}, .isNull = false };

        // Trim here, once, so every consumer sees the same literal. Drivers pad; the exact parser
        // trims and the double fallback's std::from_chars does not, so an untrimmed view would make
        // the two disagree about the very same bytes.
        auto const terminator = std::ranges::find(buffer, '\0');
        auto const raw = std::string_view(buffer.data(), static_cast<std::size_t>(terminator - buffer.begin()));
        return { .returnCode = returnCode, .text = detail::TrimAsciiWhitespace(raw), .isNull = false };
    }

    /// Converts a decimal literal into an exact value, using the column's declared precision and scale.
    ///
    /// @retval std::nullopt The value needs more digits than the unscaled 64-bit carrier holds; see
    ///                      @ref SqlMaxDynamicNumericPrecision.
    [[nodiscard]] static LIGHTWEIGHT_FORCE_INLINE std::optional<SqlDynamicNumeric> FromColumnLiteral(
        SQLHSTMT stmt, SQLUSMALLINT column, std::string_view literal) noexcept
    {
        SQLLEN declaredPrecision = 0;
        SQLLEN declaredScale = 0;
        (void) SQLColAttributeW(stmt, column, SQL_DESC_PRECISION, nullptr, SQLSMALLINT { 0 }, nullptr, &declaredPrecision);
        (void) SQLColAttributeW(stmt, column, SQL_DESC_SCALE, nullptr, SQLSMALLINT { 0 }, nullptr, &declaredScale);

        // Clamp before narrowing: a driver may report a scale or precision wider than this type
        // admits, and a bare cast to uint8_t would wrap it into a small, plausible-looking value.
        auto const toDigitCount = [](SQLLEN reported) noexcept {
            return static_cast<std::uint8_t>(std::clamp<SQLLEN>(reported, 0, SqlMaxDynamicNumericPrecision));
        };

        // Not every column has a declared scale — PostgreSQL's unconstrained `numeric` and any
        // expression column report none — but the literal always states it. Take whichever is
        // larger so the fractional digits present in the text are never discarded.
        auto const scale = std::max(toDigitCount(declaredScale), detail::FractionDigitsOf(literal));
        auto const precision = toDigitCount(declaredPrecision);

        return SqlDynamicNumeric::FromString(literal, precision, scale);
    }

    /// Retrieves the column as an exact decimal without throwing, for callers that can degrade.
    ///
    /// @retval SQL_ERROR The driver truncated the literal, or the value needs more digits than the
    ///                   unscaled 64-bit carrier holds (see @ref SqlMaxDynamicNumericPrecision).
    ///                   No ODBC diagnostic is posted for this — the driver did not fail, the value
    ///                   simply does not fit — so read the return code rather than the statement's
    ///                   last error. @ref GetColumn turns it into a described exception;
    ///                   @ref SqlVariant reuses @ref ReadLiteral and falls back to `double` instead.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN TryGetColumn(SQLHSTMT stmt,
                                                           SQLUSMALLINT column,
                                                           SqlDynamicNumeric* result,
                                                           SQLLEN* indicator,
                                                           SqlDataBinderCallback const& /*cb*/) noexcept
    {
        std::array<char, 128> buffer {};
        auto const literal = ReadLiteral(stmt, column, indicator, buffer);
        if (literal.text.empty())
        {
            // Leave a defined value behind on NULL or a failed read, so a result reused across a
            // fetch loop cannot report the previous row's number.
            *result = SqlDynamicNumeric {};
            return literal.returnCode;
        }

        auto const parsed = FromColumnLiteral(stmt, column, literal.text);
        if (!parsed)
            return SQL_ERROR;

        *result = *parsed;
        return literal.returnCode;
    }

    /// Retrieves the column as an exact decimal, preserving every digit the column declares.
    ///
    /// @throws SqlException When the value cannot be represented exactly. The driver posts no
    ///         diagnostic in that case — it did not fail — so this synthesizes one rather than
    ///         letting the caller surface an empty, or worse a stale, error from the handle.
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlDynamicNumeric* result, SQLLEN* indicator, SqlDataBinderCallback const& cb)
    {
        auto const returnCode = TryGetColumn(stmt, column, result, indicator, cb);
        if (returnCode != SQL_ERROR)
            return returnCode;

        throw SqlException(SqlErrorInfo {
            .nativeErrorCode = 0,
            .sqlState = "22003", // numeric value out of range
            .message = std::format("Column {} holds a decimal that SqlDynamicNumeric cannot represent exactly: "
                                   "it carries more than {} significant digits, or the driver truncated the "
                                   "literal. Read the column as double or std::string to accept an "
                                   "approximation.",
                                   column,
                                   SqlMaxDynamicNumericPrecision),
        });
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
