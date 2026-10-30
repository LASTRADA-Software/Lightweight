// SPDX-License-Identifier: Apache-2.0
#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../TracyProfiler.hpp"
#include "../Utils.hpp"
#include "BatchManager.hpp"
#include "RawColumnSlice.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Lightweight::detail
{

using namespace SqlBackup;

/// Thrown by a batch column for a non-NULL backup value it cannot convert to the column's type,
/// before anything of that value is appended. BatchManager appends NULL instead and reports it.
struct UnconvertibleValueError: std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Quotes the text @p text for an error message: at most 64 bytes, never cut inside a UTF-8
/// sequence, with control characters written as `\xHH`.
/// @param text The text value.
/// @return The quoted text, followed by its size when it was shortened.
inline std::string QuoteText(std::string_view text)
{
    constexpr size_t MaxShownLength = 64;
    constexpr auto IsUtf8Continuation = [](char character) {
        return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
    };
    auto shownLength = std::min(text.size(), MaxShownLength);
    while (shownLength > 0 && shownLength < text.size() && IsUtf8Continuation(text[shownLength]))
        --shownLength;

    std::string quoted = "'";
    for (char const character: text.substr(0, shownLength))
    {
        auto const code = static_cast<unsigned char>(character);
        if (code < 0x20U || code == 0x7FU)
            quoted += std::format("\\x{:02X}", code);
        else
            quoted += character;
    }
    if (shownLength < text.size())
        return std::format("{}...' ({} bytes)", quoted, text.size());
    return quoted + "'";
}

/// Describes @p value for an error message: text quoted (and shortened when long), binary by size.
/// @param value A backup value, or an element of a column batch.
/// @return The description.
template <typename T>
std::string DescribeValue(T const& value)
{
    if constexpr (std::is_convertible_v<T const&, std::string_view>)
        return QuoteText(std::string_view { value });
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        return std::format("a {}-byte binary value", value.size());
    else if constexpr (std::is_same_v<T, std::monostate>)
        return "NULL";
    else
        return std::format("{}", value);
}

/// Throws UnconvertibleValueError for @p value, which is not a valid @p typeName.
/// @param value The value that does not convert.
/// @param typeName The column type, as the error message names it.
template <typename T>
[[noreturn]] void RejectValue(T const& value, std::string_view typeName)
{
    throw UnconvertibleValueError(std::format("{} is not a valid {}", DescribeValue(value), typeName));
}

/// Throws UnconvertibleValueError for the backup value @p value; see RejectValue().
/// @param value The value that does not convert.
/// @param typeName The column type, as the error message names it.
[[noreturn]] void RejectBackupValue(BackupValue const& value, std::string_view typeName)
{
    std::visit([typeName](auto const& alternative) { RejectValue(alternative, typeName); }, value);
    std::unreachable();
}

/// Tests whether the archived text @p text stands for NULL.
/// @param text An archived text value.
/// @return `true` for an empty text and for `NULL`.
constexpr bool IsNullText(std::string_view text) noexcept
{
    return text.empty() || text == "NULL";
}

/// Parses the whole of @p text as a value of the numeric column type @p T.
///
/// SQL_BIT columns also read `true` and `false`, the texts a column archived with mixed value
/// types holds for booleans.
/// @note Floating-point types use the locale-independent, error-reporting Lightweight::detail::ParseFloat
/// helper (std::from_chars' float overloads are unavailable before macOS 26 in libc++); integral types use
/// std::from_chars directly.
/// @param text The text to parse.
/// @return The value, or std::nullopt if @p text is not entirely a number in T's range.
template <typename T, SQLSMALLINT SqlType>
std::optional<T> ParseNumeric(std::string_view text)
{
    if constexpr (SqlType == SQL_BIT)
    {
        if (text == "true")
            return T { 1 };
        if (text == "false")
            return T { 0 };
    }
    if constexpr (std::is_floating_point_v<T>)
        return ParseFloat<T>(text.data(), text.data() + text.size());
    else
    {
        T value {};
        auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc {} || end != text.data() + text.size())
            return std::nullopt;
        return value;
    }
}

/// Returns the name an error message gives a numeric column bound as @p sqlType.
/// @param sqlType The SQL type the column binds as.
/// @return The type name.
constexpr std::string_view NumericTypeName(SQLSMALLINT sqlType) noexcept
{
    switch (sqlType)
    {
        case SQL_BIT:
            return "bool";
        case SQL_TINYINT:
            return "tinyint";
        case SQL_SMALLINT:
            return "smallint";
        case SQL_INTEGER:
            return "integer";
        case SQL_BIGINT:
            return "bigint";
        case SQL_DOUBLE:
            return "real";
        default:
            return "number";
    }
}

/// The place of one numeric field in a fixed-layout date or time text.
struct TextField
{
    /// The offset of the field's first character.
    size_t position;
    /// The number of characters of the field.
    size_t length;
};

/// Parses the numeric @p fields of the fixed-layout text @p text, which every field must fit in.
/// @param text The date or time text.
/// @param fields Where each field lies in @p text.
/// @return The field values in the order of @p fields, or std::nullopt if a field is not entirely
///         decimal digits (a sign is rejected).
template <size_t FieldCount>
std::optional<std::array<unsigned, FieldCount>> ParseFields(std::string_view text,
                                                            std::array<TextField, FieldCount> const& fields)
{
    std::array<unsigned, FieldCount> values {};
    for (auto const index: std::views::iota(size_t { 0 }, FieldCount))
    {
        auto const* const first = text.data() + fields[index].position;
        auto const* const last = first + fields[index].length;
        auto const [end, error] = std::from_chars(first, last, values[index]);
        if (error != std::errc {} || end != last)
            return std::nullopt;
    }
    return values;
}

/// Converts the native backup value @p value to the numeric column type @p T.
/// @param value A bool, integer or floating-point value.
/// @return The value, or std::nullopt if it lies outside T's range (or is not finite).
template <typename T, typename Source>
std::optional<T> NarrowNumeric(Source value)
{
    if constexpr (std::is_same_v<Source, bool>)
        return static_cast<T>(value ? 1 : 0);
    else if constexpr (std::is_floating_point_v<T>)
        return static_cast<T>(value);
    else if constexpr (std::is_floating_point_v<Source>)
    {
        // Casting a double that does not fit T is undefined behaviour, so check the range first.
        if (!std::isfinite(value) || value < static_cast<Source>(std::numeric_limits<T>::min())
            || value >= static_cast<Source>(std::numeric_limits<T>::max()) + Source { 1 })
            return std::nullopt;
        return static_cast<T>(value);
    }
    else
    {
        if (!std::in_range<T>(value))
            return std::nullopt;
        return static_cast<T>(value);
    }
}

/// A column in a batch.
struct BatchColumn
{
    virtual ~BatchColumn() = default;
    BatchColumn() = default;
    BatchColumn(BatchColumn const&) = delete;
    BatchColumn(BatchColumn&&) = delete;
    BatchColumn& operator=(BatchColumn const&) = delete;
    BatchColumn& operator=(BatchColumn&&) = delete;

    /// Push a value to the batch column.
    virtual void Push(BackupValue const& val) = 0;

    /// Push a range of values/nulls from a columnar batch.
    virtual void PushFromBatch(ColumnBatch::ColumnData const& colData,
                               std::vector<bool> const& nulls,
                               size_t offset,
                               size_t count) = 0;

    /// Push a null value to the batch column.
    virtual void PushNull() = 0;

    /// Convert the batch column to a raw column.
    virtual SqlRawColumn ToRaw() = 0;

    /// Clear the batch column.
    virtual void Clear() = 0;

    /// Number of values (NULL included) the column holds that are not flushed yet.
    [[nodiscard]] virtual size_t PendingRows() const = 0;

    /// Bytes the column's fixed-stride raw buffer will occupy at ToRaw() time. Used to flush
    /// early when variable-length values (large binaries) would make stride x rows explode.
    [[nodiscard]] virtual size_t ProjectedRawBytes() const
    {
        return 0;
    }
};

/// A typed batch column.
template <typename T, SQLSMALLINT CType, SQLSMALLINT SqlType>
struct TypedBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<T> data;
    std::vector<SQLLEN> indicators;

    explicit TypedBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = CType;
        metadata.sqlType = SqlType;
    }

    /// The column type, as error messages name it.
    static constexpr std::string_view TypeName = NumericTypeName(SqlType);

    /// The values the column accepts, inclusive, when narrower than T's range.
    std::optional<std::pair<int64_t, int64_t>> valueRange;

    void PushNull() override
    {
        data.push_back(T {});
        indicators.push_back(SQL_NULL_DATA);
    }

    /// Tests whether @p value lies in `valueRange`.
    /// @param value The converted value.
    /// @return `true` if no range is set or @p value lies in it.
    [[nodiscard]] bool InValueRange(T value) const noexcept
    {
        if constexpr (std::is_integral_v<T>)
            return !valueRange
                   || (std::cmp_greater_equal(value, valueRange->first) && std::cmp_less_equal(value, valueRange->second));
        else
            return true;
    }

    /// Appends the number @p text, or NULL for a NULL text.
    /// @throws UnconvertibleValueError if @p text is no number of this column's type.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        auto const value = ParseNumeric<T, SqlType>(text);
        if (!value || !InValueRange(*value))
            RejectValue(text, TypeName);
        data.push_back(*value);
        indicators.push_back(sizeof(T));
    }

    /// Appends the native value @p value.
    /// @throws UnconvertibleValueError if @p value lies outside this column type's range.
    template <typename Source>
    void PushNative(Source value)
    {
        auto const narrowed = NarrowNumeric<T>(value);
        if (!narrowed || !InValueRange(*narrowed))
            RejectValue(value, TypeName);
        data.push_back(*narrowed);
        indicators.push_back(sizeof(T));
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else if constexpr (std::is_same_v<VecT, std::vector<std::vector<uint8_t>>>)
                        RejectValue(inVec[idx], TypeName);
                    else
                        PushNative(std::ranges::range_value_t<VecT> { inVec[idx] });
                }
            },
            colData);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(T);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(T) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

/// A numeric batch column.
template <typename T, SQLSMALLINT CType, SQLSMALLINT SqlType>
struct NumericBatchColumn: TypedBatchColumn<T, CType, SqlType>
{
    using Base = TypedBatchColumn<T, CType, SqlType>;
    using Base::Base;

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::monostate>)
                {
                    this->PushNull();
                }
                else if constexpr (std::is_same_v<ArgT, std::string>)
                {
                    this->PushText(arg);
                }
                else if constexpr (std::is_same_v<ArgT, std::vector<uint8_t>>)
                {
                    RejectValue(arg, Base::TypeName);
                }
                else
                {
                    this->PushNative(arg);
                }
            },
            val);
    }
};

/// A batch column for DateTime/Timestamp columns.
/// Parses ISO 8601 datetime strings and binds as SQL_TIMESTAMP_STRUCT.
struct DateTimeBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<SQL_TIMESTAMP_STRUCT> data;
    std::vector<SQLLEN> indicators;

    explicit DateTimeBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = SQL_C_TYPE_TIMESTAMP;
        metadata.sqlType = SQL_TYPE_TIMESTAMP;
        metadata.size = 27;         // Standard precision for datetime
        metadata.decimalDigits = 7; // Fractional seconds precision
    }

    /// Where year, month, day, hour, minute and second lie in a datetime text.
    static constexpr std::array Layout {
        TextField { .position = 0, .length = 4 },  TextField { .position = 5, .length = 2 },
        TextField { .position = 8, .length = 2 },  TextField { .position = 11, .length = 2 },
        TextField { .position = 14, .length = 2 }, TextField { .position = 17, .length = 2 }
    };

    /// Parses ISO 8601 datetime string format: YYYY-MM-DDTHH:MM:SS.mmm
    /// (or `YYYY-MM-DD HH:MM:SS`); fractional digits beyond milliseconds are ignored.
    /// @param s The datetime text.
    /// @return The timestamp, or std::nullopt if a field of @p s is not a number.
    static std::optional<SQL_TIMESTAMP_STRUCT> ParseDateTime(std::string_view s)
    {
        if (s.size() < 19)
            return std::nullopt;

        auto const fields = ParseFields(s, Layout);
        if (!fields)
            return std::nullopt;
        auto const [year, month, day, hour, minute, second] = *fields;

        SQL_TIMESTAMP_STRUCT ts {};
        ts.year = static_cast<SQLSMALLINT>(year);
        ts.month = static_cast<SQLUSMALLINT>(month);
        ts.day = static_cast<SQLUSMALLINT>(day);
        ts.hour = static_cast<SQLUSMALLINT>(hour);
        ts.minute = static_cast<SQLUSMALLINT>(minute);
        ts.second = static_cast<SQLUSMALLINT>(second);

        // Parse fractional seconds if present (after position 19, following '.')
        if (s.size() > 20 && s[19] == '.')
        {
            // Parse milliseconds; SQL_TIMESTAMP_STRUCT::fraction is in nanoseconds
            size_t fracLen = std::min(s.size() - 20, size_t { 3 });
            auto const fraction = ParseFields(s, std::array { TextField { .position = 20, .length = fracLen } });
            if (!fraction)
                return std::nullopt;
            auto millis = (*fraction)[0];
            // Pad to 3 digits if shorter
            while (fracLen < 3)
            {
                millis *= 10;
                fracLen++;
            }
            ts.fraction = static_cast<SQLUINTEGER>(millis) * 1'000'000; // ms to ns
        }

        return ts;
    }

    /// Appends the datetime @p text, or NULL for a NULL text.
    /// @throws UnconvertibleValueError if @p text is no datetime.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        auto const value = ParseDateTime(text);
        if (!value)
            RejectValue(text, "datetime");
        data.push_back(*value);
        indicators.push_back(sizeof(SQL_TIMESTAMP_STRUCT));
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushText(*text);
        else
            RejectBackupValue(val, "datetime");
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "datetime");
                }
            },
            colData);
    }

    void PushNull() override
    {
        data.push_back(SQL_TIMESTAMP_STRUCT {});
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(SQL_TIMESTAMP_STRUCT);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(SQL_TIMESTAMP_STRUCT) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

/// A batch column for Date columns.
/// Parses ISO 8601 date strings and binds as SQL_DATE_STRUCT.
struct DateBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<SQL_DATE_STRUCT> data;
    std::vector<SQLLEN> indicators;

    explicit DateBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = SQL_C_TYPE_DATE;
        metadata.sqlType = SQL_TYPE_DATE;
        metadata.size = 10; // YYYY-MM-DD
    }

    /// Where year, month and day lie in a date text.
    static constexpr std::array Layout { TextField { .position = 0, .length = 4 },
                                         TextField { .position = 5, .length = 2 },
                                         TextField { .position = 8, .length = 2 } };

    /// Parses ISO 8601 date string format: YYYY-MM-DD
    /// @param s The date text.
    /// @return The date, or std::nullopt if a field of @p s is not a number.
    static std::optional<SQL_DATE_STRUCT> ParseDate(std::string_view s)
    {
        if (s.size() < 10)
            return std::nullopt;

        auto const fields = ParseFields(s, Layout);
        if (!fields)
            return std::nullopt;
        auto const [year, month, day] = *fields;

        SQL_DATE_STRUCT ds {};
        ds.year = static_cast<SQLSMALLINT>(year);
        ds.month = static_cast<SQLUSMALLINT>(month);
        ds.day = static_cast<SQLUSMALLINT>(day);
        return ds;
    }

    /// Appends the date @p text, or NULL for a NULL text.
    /// @throws UnconvertibleValueError if @p text is no date.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        auto const value = ParseDate(text);
        if (!value)
            RejectValue(text, "date");
        data.push_back(*value);
        indicators.push_back(sizeof(SQL_DATE_STRUCT));
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushText(*text);
        else
            RejectBackupValue(val, "date");
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "date");
                }
            },
            colData);
    }

    void PushNull() override
    {
        data.push_back(SQL_DATE_STRUCT {});
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(SQL_DATE_STRUCT);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(SQL_DATE_STRUCT) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

/// A batch column for Time columns using standard SQL_TIME_STRUCT.
/// Used for databases that don't support fractional seconds in TIME.
/// Note: SQL_TIME_STRUCT (6 bytes) does not support fractional seconds.
/// This is only used for MySQL and unknown server types (not MSSQL, PostgreSQL, or SQLite).
struct TimeBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<SQL_TIME_STRUCT> data;
    std::vector<SQLLEN> indicators;

    explicit TimeBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = SQL_C_TYPE_TIME;
        metadata.sqlType = SQL_TYPE_TIME;
        metadata.size = 8; // HH:MM:SS
        metadata.decimalDigits = 0;
    }

    /// Where hour, minute and second lie in a time text.
    static constexpr std::array Layout { TextField { .position = 0, .length = 2 },
                                         TextField { .position = 3, .length = 2 },
                                         TextField { .position = 6, .length = 2 } };

    /// Parses time string format: HH:MM:SS or HH:MM:SS.ffffff
    /// Note: Fractional seconds are ignored as SQL_TIME_STRUCT doesn't support them.
    /// @param s The time text.
    /// @return The time, or std::nullopt if a field of @p s is not a number.
    static std::optional<SQL_TIME_STRUCT> ParseTime(std::string_view s)
    {
        if (s.size() < 8)
            return std::nullopt;

        auto const fields = ParseFields(s, Layout);
        if (!fields)
            return std::nullopt;
        auto const [hour, minute, second] = *fields;

        SQL_TIME_STRUCT ts {};
        ts.hour = static_cast<SQLUSMALLINT>(hour);
        ts.minute = static_cast<SQLUSMALLINT>(minute);
        ts.second = static_cast<SQLUSMALLINT>(second);
        return ts;
    }

    /// Appends the time @p text, or NULL for a NULL text.
    /// @throws UnconvertibleValueError if @p text is no time.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        auto const value = ParseTime(text);
        if (!value)
            RejectValue(text, "time");
        data.push_back(*value);
        indicators.push_back(sizeof(SQL_TIME_STRUCT));
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushText(*text);
        else
            RejectBackupValue(val, "time");
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "time");
                }
            },
            colData);
    }

    void PushNull() override
    {
        data.push_back(SQL_TIME_STRUCT {});
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(SQL_TIME_STRUCT);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(SQL_TIME_STRUCT) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

/// A batch column for Time columns that binds as string (SQL_C_CHAR).
/// This preserves fractional seconds for databases like PostgreSQL that support them
/// but where SQL_TIME_STRUCT binding would lose the fractional part.
/// Format: "HH:MM:SS.ffffff" (up to 6 digits of fractional seconds).
struct StringTimeBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<std::array<char, 16>> data; // "HH:MM:SS.ffffff\0"
    std::vector<SQLLEN> indicators;

    explicit StringTimeBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = SQL_C_CHAR;
        metadata.sqlType = SQL_TYPE_TIME;
        metadata.size = 15; // "HH:MM:SS.ffffff"
        metadata.decimalDigits = 6;
    }

    /// Appends the time @p text for the server to parse, or NULL for a NULL text.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        std::array<char, 16> buf {};
        auto const len = std::min(text.size(), size_t { 15 });
        std::memcpy(buf.data(), text.data(), len);
        buf[len] = '\0';
        data.push_back(buf);
        indicators.push_back(static_cast<SQLLEN>(len));
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushText(*text);
        else
            RejectBackupValue(val, "time");
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "time");
                }
            },
            colData);
    }

    void PushNull() override
    {
        data.push_back(std::array<char, 16> {});
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = 16;
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * 16 },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

#if defined(SQL_SS_TIME2)
/// A batch column for Time columns using Microsoft SQL Server's SS_TIME2 extension.
/// This is required for MS SQL Server as it does not support standard SQL_TYPE_TIME binding.
/// SQL_SS_TIME2_STRUCT (12 bytes) supports fractional seconds with microsecond precision.
struct MsTimeBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<SQL_SS_TIME2_STRUCT> data;
    std::vector<SQLLEN> indicators;

    explicit MsTimeBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        // Use SQL_C_BINARY for compatibility with MS SQL Server's TIME type.
        // SQL_C_TYPE_TIME doesn't work properly with SQL_SS_TIME2.
        metadata.cType = SQL_C_BINARY;
        metadata.sqlType = SQL_SS_TIME2;
        metadata.size = sizeof(SQL_SS_TIME2_STRUCT);
        // MSSQL TIME has 100-nanosecond precision (7 fractional digits)
        metadata.decimalDigits = 7;
    }

    /// Parses time string format: HH:MM:SS or HH:MM:SS.fffffff
    /// SQL_SS_TIME2_STRUCT.fraction is in 100-nanosecond intervals.
    static SQL_SS_TIME2_STRUCT ParseTime(std::string_view s)
    {
        SQL_SS_TIME2_STRUCT ts {};

        // Expected format: "HH:MM:SS" (length 8) or "HH:MM:SS.fffffff"
        if (s.size() < 8)
            return ts;

        auto parseNum = [&s](size_t pos, size_t len) -> int {
            int val = 0;
            std::from_chars(s.data() + pos, s.data() + pos + len, val);
            return val;
        };

        ts.hour = static_cast<SQLUSMALLINT>(parseNum(0, 2));
        ts.minute = static_cast<SQLUSMALLINT>(parseNum(3, 2));
        ts.second = static_cast<SQLUSMALLINT>(parseNum(6, 2));

        // Parse fractional seconds if present (after position 8, following '.')
        // SQL_SS_TIME2_STRUCT.fraction is in 100-nanosecond intervals (10^-7 seconds)
        // MSSQL TIME can have up to 7 fractional digits
        if (s.size() > 9 && s[8] == '.')
        {
            // Parse up to 7 digits for 100-nanosecond precision
            size_t fracLen = std::min(s.size() - 9, size_t { 7 });
            int fraction = 0;
            std::from_chars(s.data() + 9, s.data() + 9 + fracLen, fraction);
            // Pad to 7 digits if shorter (to convert to 100-nanosecond units)
            for (size_t pad = fracLen; pad < 7; ++pad)
                fraction *= 10;
            ts.fraction = static_cast<SQLUINTEGER>(fraction);
        }

        return ts;
    }

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::monostate>)
                {
                    this->PushNull();
                }
                else if constexpr (std::is_same_v<ArgT, std::string>)
                {
                    if (arg.empty() || arg == "NULL")
                        this->PushNull();
                    else
                    {
                        data.push_back(ParseTime(arg));
                        indicators.push_back(sizeof(SQL_SS_TIME2_STRUCT));
                    }
                }
                else
                {
                    this->PushNull();
                }
            },
            val);
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            // NOLINTNEXTLINE(readability-function-cognitive-complexity)
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (size_t i = 0; i < count; ++i)
                {
                    size_t idx = offset + i;
                    if (idx < nulls.size() && nulls[idx])
                    {
                        PushNull();
                        continue;
                    }

                    if constexpr (std::is_same_v<VecT, std::monostate>)
                    {
                        PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                    {
                        if (idx < inVec.size())
                        {
                            auto const& s = inVec[idx];
                            if (s.empty() || s == "NULL")
                                PushNull();
                            else
                            {
                                data.push_back(ParseTime(s));
                                indicators.push_back(sizeof(SQL_SS_TIME2_STRUCT));
                            }
                        }
                        else
                            PushNull();
                    }
                    else
                    {
                        PushNull();
                    }
                }
            },
            colData);
    }

    void PushNull() override
    {
        data.push_back(SQL_SS_TIME2_STRUCT {});
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(SQL_SS_TIME2_STRUCT);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(SQL_SS_TIME2_STRUCT) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};
#endif

/// Strips the trailing U+0020 characters of @p text.
/// @param text The text value.
/// @return @p text without its trailing spaces; empty if it held nothing else.
constexpr std::string_view TrimTrailingSpaces(std::string_view text) noexcept
{
    auto const last = text.find_last_not_of(' ');
    return last == std::string_view::npos ? std::string_view {} : text.substr(0, last + 1);
}

struct StringBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<std::string> values; // variable-length; fixed-stride buffer built in ToRaw()
    std::vector<char> buffer;        // ToRaw() staging (stride x rows), rebuilt per flush
    std::vector<SQLLEN> indicators;
    size_t maxActual = 0; // largest value in this batch -> the ToRaw() stride
    /// Strips the trailing spaces of every pushed value (a CHAR(n) pad that the target does not keep).
    bool trimTrailingSpaces = false;

    StringBatchColumn(SqlRawColumnMetadata meta, size_t declaredLen):
        metadata(meta)
    {
        // Bind as standard C char array.
        metadata.cType = SQL_C_CHAR;
        // Standard VARCHAR SQL type (unless overridden).
        if (metadata.sqlType == 0)
            metadata.sqlType = declaredLen > 255 ? SQL_LONGVARCHAR : SQL_VARCHAR;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            // NOLINTNEXTLINE(readability-function-cognitive-complexity)
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (size_t i = 0; i < count; ++i)
                {
                    size_t idx = offset + i;
                    if (idx < nulls.size() && nulls[idx])
                    {
                        PushNull();
                        continue;
                    }

                    if constexpr (std::is_same_v<VecT, std::monostate>)
                    {
                        PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                    {
                        if (idx < inVec.size())
                            PushString(inVec[idx]);
                        else
                            PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<bool>>)
                    {
                        if (idx < inVec.size())
                            PushString(inVec[idx] ? "1" : "0");
                        else
                            PushNull();
                    }
                    else
                    {
                        if (idx < inVec.size())
                        {
                            auto const& val = inVec[idx];
                            if constexpr (!std::is_same_v<typename VecT::value_type, std::vector<uint8_t>>)
                                PushString(std::format("{}", val));
                            else
                                RejectValue(val, "text");
                        }
                        else
                            PushNull();
                    }
                }
            },
            colData);
    }

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::monostate>)
                    this->PushNull();
                else if constexpr (std::is_same_v<ArgT, std::string>)
                    this->PushString(arg);
                else if constexpr (std::is_same_v<ArgT, std::vector<uint8_t>>)
                    RejectValue(arg, "text");
                else
                    this->PushString(std::format("{}", arg));
            },
            val);
    }

    void PushString(std::string_view s)
    {
        if (trimTrailingSpaces)
            s = TrimTrailingSpaces(s);
        // Stored variable-length (no truncation; the old fixed stride silently cut long
        // VARCHAR(MAX)/TEXT values); the fixed-stride buffer is laid out at flush time.
        maxActual = std::max(maxActual, s.size());
        indicators.push_back(static_cast<SQLLEN>(s.size()));
        values.emplace_back(s);
    }

    void PushNull() override
    {
        values.emplace_back();
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        values.clear();
        values.shrink_to_fit();
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
        maxActual = 0;
    }

    [[nodiscard]] size_t ProjectedRawBytes() const override
    {
        return std::max(maxActual, size_t { 1 }) * values.size();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        size_t const stride = std::max(maxActual, size_t { 1 });
        buffer.assign(stride * values.size(), 0);
        for (size_t i = 0; i < values.size(); ++i)
            if (!values[i].empty())
                std::memcpy(buffer.data() + (i * stride), values[i].data(), values[i].size());

        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = stride;
        return SqlRawColumn { .metadata = meta,
                              .data = std::as_bytes(std::span(buffer)),
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

struct WideStringBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<std::u16string> values; // variable-length; fixed-stride buffer built in ToRaw()
    std::vector<char16_t> buffer;       // ToRaw() staging (stride x rows), rebuilt per flush
    std::vector<SQLLEN> indicators;
    size_t maxActualChars = 0; // largest value (in UTF-16 units) -> the ToRaw() stride
    /// Strips the trailing spaces of every pushed value (an NCHAR(n) pad that the target does not keep).
    bool trimTrailingSpaces = false;

    WideStringBatchColumn(SqlRawColumnMetadata meta, size_t declaredLen):
        metadata(meta)
    {
        // Bind as Unicode char array.
        metadata.cType = SQL_C_WCHAR;
        // Only set sqlType if not already set (preserve SQL_WLONGVARCHAR for MAX types)
        if (metadata.sqlType == 0)
            metadata.sqlType = declaredLen > 255 ? SQL_WLONGVARCHAR : SQL_WVARCHAR;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            // NOLINTNEXTLINE(readability-function-cognitive-complexity)
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (size_t i = 0; i < count; ++i)
                {
                    size_t idx = offset + i;
                    if (idx < nulls.size() && nulls[idx])
                    {
                        PushNull();
                        continue;
                    }

                    if constexpr (std::is_same_v<VecT, std::monostate>)
                    {
                        PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                    {
                        if (idx < inVec.size())
                        {
                            PushString(inVec[idx]);
                        }
                        else
                            PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<bool>>)
                    {
                        if (idx < inVec.size())
                        {
                            PushString(inVec[idx] ? "1" : "0");
                        }
                        else
                            PushNull();
                    }
                    else
                    {
                        if (idx < inVec.size())
                        {
                            if constexpr (!std::is_same_v<typename VecT::value_type, std::vector<uint8_t>>)
                            {
                                PushString(std::format("{}", inVec[idx]));
                            }
                            else
                            {
                                RejectValue(inVec[idx], "text");
                            }
                        }
                        else
                            PushNull();
                    }
                }
            },
            colData);
    }

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::monostate>)
                {
                    this->PushNull();
                }
                else if constexpr (std::is_same_v<ArgT, std::string>)
                {
                    this->PushString(arg);
                }
                else if constexpr (std::is_same_v<ArgT, std::vector<uint8_t>>)
                {
                    RejectValue(arg, "text");
                }
                else
                {
                    this->PushString(std::format("{}", arg));
                }
            },
            val);
    }

    void PushString(std::string_view s)
    {
        if (trimTrailingSpaces)
            s = TrimTrailingSpaces(s);
        // Stored variable-length (no truncation; the old fixed stride silently cut long
        // NVARCHAR(MAX) values); the fixed-stride buffer is laid out at flush time.
        auto ws = ToUtf16(std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size()));
        maxActualChars = std::max(maxActualChars, ws.size());
        indicators.push_back(static_cast<SQLLEN>(ws.size() * sizeof(char16_t)));
        values.emplace_back(std::move(ws));
    }

    void PushNull() override
    {
        values.emplace_back();
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        values.clear();
        values.shrink_to_fit();
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
        maxActualChars = 0;
    }

    [[nodiscard]] size_t ProjectedRawBytes() const override
    {
        return std::max(maxActualChars, size_t { 1 }) * sizeof(char16_t) * values.size();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        size_t const strideChars = std::max(maxActualChars, size_t { 1 });
        buffer.assign(strideChars * values.size(), char16_t { 0 });
        for (size_t i = 0; i < values.size(); ++i)
            if (!values[i].empty())
                std::memcpy(buffer.data() + (i * strideChars), values[i].data(), values[i].size() * sizeof(char16_t));

        SqlRawColumnMetadata meta = metadata;
        // Preserve size=0 for MAX types to avoid HY104 precision errors on MS SQL
        if (metadata.size != 0)
            meta.size = strideChars;
        meta.bufferLength = strideChars * sizeof(char16_t);

        return SqlRawColumn { .metadata = meta,
                              // Send data as bytes
                              .data = std::as_bytes(std::span(buffer)),
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

struct BinaryBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<std::vector<uint8_t>> values; // variable-length; fixed-stride buffer built in ToRaw()
    std::vector<SQLLEN> indicators;
    std::vector<uint8_t> buffer; // ToRaw() staging (stride x rows), rebuilt per flush
    size_t maxActual = 0;        // largest value in this batch -> the ToRaw() stride

    BinaryBatchColumn(SqlRawColumnMetadata meta, size_t declaredLen):
        metadata(meta)
    {
        metadata.cType = SQL_C_BINARY;
        // Only set sqlType if not already set (preserve SQL_LONGVARBINARY for MAX types)
        if (metadata.sqlType == 0)
            metadata.sqlType = declaredLen > 255 ? SQL_LONGVARBINARY : SQL_VARBINARY;
    }

    void AppendValue(std::vector<uint8_t> v)
    {
        maxActual = std::max(maxActual, v.size());
        indicators.push_back(static_cast<SQLLEN>(v.size()));
        values.push_back(std::move(v));
    }

    /// Decodes the hex text @p s.
    /// @param s Two hex digits per byte, upper or lower case.
    /// @return The bytes, or std::nullopt if @p s has an odd length or a non-hex character.
    static std::optional<std::vector<uint8_t>> DecodeHex(std::string_view s)
    {
        if (s.size() % 2 != 0 || !std::ranges::all_of(s, IsHexDigit))
            return std::nullopt;
        std::vector<uint8_t> out(s.size() / 2);
        for (auto const index: std::views::iota(size_t { 0 }, out.size()))
            out[index] = static_cast<uint8_t>((HexDigitValue(s[index * 2]) << 4) | HexDigitValue(s[(index * 2) + 1]));
        return out;
    }

    /// Appends the bytes the hex text @p text encodes.
    /// @throws UnconvertibleValueError if @p text is no hex text.
    void PushHexText(std::string const& text)
    {
        auto decoded = DecodeHex(text);
        if (!decoded)
            RejectValue(text, "binary");
        AppendValue(std::move(*decoded)); // full hex-decoded value
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::vector<uint8_t>>>)
                        AppendValue(inVec[idx]); // full value: no truncation
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushHexText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "binary");
                }
            },
            colData);
    }

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::vector<uint8_t>>)
                    AppendValue(arg);
                else if constexpr (std::is_same_v<ArgT, std::string>)
                    AppendValue(std::vector<uint8_t>(arg.begin(), arg.end()));
                else if constexpr (std::is_same_v<ArgT, std::monostate>)
                    this->PushNull();
                else
                    RejectValue(arg, "binary");
            },
            val);
    }

    void PushNull() override
    {
        values.emplace_back();
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        values.clear();
        values.shrink_to_fit();
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
        maxActual = 0;
    }

    [[nodiscard]] size_t ProjectedRawBytes() const override
    {
        return std::max(maxActual, size_t { 1 }) * values.size();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        // Build the column-wise fixed-stride buffer with the stride of the LARGEST value in this
        // batch (the old fixed 64 KB stride silently truncated larger LOBs). BatchManager caps the
        // batch by projected bytes, so stride x rows stays bounded even for multi-MB blobs.
        size_t const stride = std::max(maxActual, size_t { 1 });
        buffer.assign(stride * values.size(), 0);
        for (size_t i = 0; i < values.size(); ++i)
            if (!values[i].empty())
                std::memcpy(buffer.data() + (i * stride), values[i].data(), values[i].size());

        SqlRawColumnMetadata meta = metadata;
        // meta.size stays the declared column size (0 for MAX; the SqlRawColumn binder substitutes
        // bufferLength as the ColumnSize for LONG types).
        meta.bufferLength = stride;
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(buffer.data()),
                                                                   buffer.size() },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};
/// A batch column for GUID values on targets without a native GUID type, bound as SQL_C_CHAR text.
///
/// Archived GUIDs are `to_string(SqlGuid)` text, which is what SqlDataBinder<SqlGuid> stores on such
/// targets, so the text passes through unchanged: it is neither parsed nor normalized, and thus never
/// a value failure. Any non-text archived value is rejected.
struct GuidTextBatchColumn final: StringBatchColumn
{
    explicit GuidTextBatchColumn(SqlRawColumnMetadata meta):
        StringBatchColumn(meta, 36)
    {
        metadata.sqlType = SQL_VARCHAR;
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushString(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "GUID");
                }
            },
            colData);
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushString(*text);
        else
            RejectBackupValue(val, "GUID");
    }
};

/// A batch column for GUID values using proper SQL_GUID binding.
struct GuidBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<SqlGuid> data;
    std::vector<SQLLEN> indicators;

    explicit GuidBatchColumn(SqlRawColumnMetadata meta):
        metadata(meta)
    {
        metadata.cType = SQL_C_GUID;
        metadata.sqlType = SQL_GUID;
        metadata.size = 16; // GUID is 16 bytes
    }

    void PushNull() override
    {
        data.push_back(SqlGuid {});
        indicators.push_back(SQL_NULL_DATA);
    }

    /// Appends @p guid in the native SQL_C_GUID wire byte order that ToRaw()'s raw array bind
    /// expects, so `data` never holds a value in the wrong order. See SwapGuidWireByteOrder().
    void PushGuid(SqlGuid guid)
    {
        detail::SwapGuidWireByteOrder(guid.data);
        data.push_back(guid);
        indicators.push_back(sizeof(SqlGuid));
    }

    /// Appends the GUID @p text, or NULL for a NULL text. Any well-formed GUID is accepted, whatever
    /// its version nibble, and keeps its text's bytes. The braced form `{...}` is accepted too, as
    /// text-typed GUID columns (SQLite) hold it and archive it verbatim.
    /// @throws UnconvertibleValueError if @p text is no GUID.
    void PushText(std::string_view text)
    {
        if (IsNullText(text))
        {
            PushNull();
            return;
        }
        auto const unbraced =
            text.size() >= 2 && text.front() == '{' && text.back() == '}' ? text.substr(1, text.size() - 2) : text;
        auto const guid = SqlGuid::TryParse(unbraced);
        if (!guid)
            RejectValue(text, "GUID");
        PushGuid(*guid);
    }

    void PushFromBatch(ColumnBatch::ColumnData const& colData,
                       std::vector<bool> const& nulls,
                       size_t offset,
                       size_t count) override
    {
        std::visit(
            [&](auto const& inVec) {
                using VecT = std::decay_t<decltype(inVec)>;
                for (auto const idx: std::views::iota(offset, offset + count))
                {
                    if constexpr (std::is_same_v<VecT, std::monostate>)
                        PushNull();
                    else if ((idx < nulls.size() && nulls[idx]) || idx >= inVec.size())
                        PushNull();
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                        PushText(inVec[idx]);
                    else
                        RejectValue(inVec[idx], "GUID");
                }
            },
            colData);
    }

    void Push(BackupValue const& val) override
    {
        if (std::holds_alternative<std::monostate>(val))
            PushNull();
        else if (auto const* text = std::get_if<std::string>(&val))
            PushText(*text);
        else
            RejectBackupValue(val, "GUID");
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    [[nodiscard]] size_t PendingRows() const override
    {
        return indicators.size();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        meta.bufferLength = sizeof(SqlGuid);
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()),
                                                                   data.size() * sizeof(SqlGuid) },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

namespace
{
    /// Estimates the buffer size needed per row for a given column type.
    /// This is used to calculate memory-aware batch capacity.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    size_t EstimateColumnBufferSize(SqlColumnDeclaration const& col)
    {
        return std::visit(
            // NOLINTNEXTLINE(readability-function-cognitive-complexity)
            [](auto const& t) -> size_t {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Varchar>)
                    return t.size > 0 ? t.size : 4096;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::NVarchar>)
                    return (t.size > 0 ? t.size : 4096) * sizeof(char16_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Char>)
                    return t.size > 0 ? t.size : 4096;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::NChar>)
                    return (t.size > 0 ? t.size : 4096) * sizeof(char16_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Text>)
                    return t.size > 0 ? t.size : 4096;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Binary>)
                    return t.size > 0 ? t.size : 65535;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::VarBinary>)
                    return t.size > 0 ? t.size : 65535;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Decimal>)
                    return t.precision + 3; // digits + sign + decimal point
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Bigint>)
                    return sizeof(int64_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Integer>)
                    return sizeof(int32_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Smallint>)
                    return sizeof(int16_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Tinyint>)
                    return sizeof(int8_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Real>)
                    return sizeof(double);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Bool>)
                    return sizeof(int8_t);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Guid>)
                    return 16;
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::DateTime>
                                   || std::is_same_v<T, SqlColumnTypeDefinitions::Timestamp>)
                    return sizeof(SQL_TIMESTAMP_STRUCT);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Date>)
                    return sizeof(SQL_DATE_STRUCT);
                else if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Time>)
                    return 16; // String time format "HH:MM:SS.ffffff"
                else
                    return 256; // Default fallback
            },
            col.type);
    }
} // namespace

BatchManager::BatchManager(BatchManagerHooks hooks,
                           std::vector<SqlColumnDeclaration> const& colDecls,
                           size_t capacity,
                           SqlServerType serverType,
                           SqlServerType sourceServerType):
    hooks(std::move(hooks)),
    serverType(serverType),
    sourceServerType(sourceServerType)
{
    if (!this->hooks.classify)
        this->hooks.classify = [](SqlErrorInfo const&) {
            return FailureDisposition::Propagate;
        };

    // logic to create columns based on type
    for (auto const& col: colDecls)
    {
        // A primary key column is NOT NULL even where the declaration does not say so.
        auto const isKey = col.primaryKey != SqlPrimaryKeyType::NONE;
        if (isKey)
            keyColumns.push_back(columns.size());
        columns.push_back(CreateColumn(col));
        columnNames.push_back(col.name);
        columnRequired.push_back(col.required || isKey);
    }

    // Calculate memory-aware capacity
    // Estimate bytes per row based on column definitions. The per-column estimate is capped:
    // unbounded types report their declared size, which for varchar(max)/varbinary(max) is the
    // 2 GB sentinel — uncapped, that drove the capacity to 1 row and the restore into row-by-row
    // inserts. Actual memory safety is enforced at runtime by the projected-bytes budget
    // (ExceedsByteBudget), which flushes early when large values really arrive.
    constexpr size_t MaxColumnEstimate = 64ULL * 1024;
    size_t bytesPerRow = 0;
    for (auto const& col: colDecls)
    {
        bytesPerRow += std::min(EstimateColumnBufferSize(col), MaxColumnEstimate);
        bytesPerRow += sizeof(SQLLEN); // indicator per column
    }
    bytesPerRow = std::max(bytesPerRow, size_t { 1 });

    // Memory budget: 32MB per batch to avoid OOM on large restores
    // This is a conservative limit that works well with --memory-limit 1G
    constexpr size_t memoryBudget = 32 * 1024 * 1024;
    size_t const memoryCapacity = memoryBudget / bytesPerRow;

    // Also limit by parameter count for ODBC compatibility
    size_t const numCols = std::max(size_t { 1 }, columns.size());
    size_t const paramLimit = 25000;
    size_t const paramCapacity = paramLimit / numCols;

    // Use the minimum of all limits
    this->capacity = std::min({ capacity, memoryCapacity, paramCapacity });

    // Ensure at least 1 row capacity
    this->capacity = std::max(this->capacity, size_t { 1 });
}

BatchManager::BatchManager(BatchExecutor executor,
                           std::vector<SqlColumnDeclaration> const& colDecls,
                           size_t capacity,
                           SqlServerType serverType,
                           SqlServerType sourceServerType):
    BatchManager(
        BatchManagerHooks {
            .tryInsert = [executor = std::move(executor)](std::span<SqlRawColumn const> slice,
                                                          std::size_t rowCount) -> std::expected<void, BatchInsertError> {
                executor(std::vector<SqlRawColumn>(slice.begin(), slice.end()), rowCount);
                return {};
            },
            .classify = {},
            .onRowFailed = {},
            .onCellNulled = {},
            .onSettled = {},
        },
        colDecls,
        capacity,
        serverType,
        sourceServerType)
{
}

BatchManager::~BatchManager() = default;

BatchManager::BatchManager(BatchManager&&) noexcept = default;

BatchManager& BatchManager::operator=(BatchManager&&) noexcept = default;

bool BatchManager::TrimsFixedCharPadding() const
{
    auto const* const source = SqlQueryFormatter::Get(sourceServerType);
    auto const* const target = SqlQueryFormatter::Get(serverType);
    return source != nullptr && target != nullptr && source->ComparesFixedCharWithPadSemantics()
           && !target->ComparesFixedCharWithPadSemantics();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::unique_ptr<BatchColumn> BatchManager::CreateColumn(SqlColumnDeclaration const& col) const
{
    SqlRawColumnMetadata meta {};
    std::visit(
        [&](auto const& t) {
            if constexpr (requires { t.precision; })
            {
                meta.size = t.precision;
                if constexpr (requires { t.scale; })
                    meta.decimalDigits = static_cast<SQLSMALLINT>(t.scale);
            }
            else if constexpr (requires { t.size; })
            {
                meta.size = t.size;
            }
        },
        col.type);

    if (std::holds_alternative<SqlColumnTypeDefinitions::Integer>(col.type))
    {
        if (meta.size == 0)
            meta.size = 10;
        return std::make_unique<NumericBatchColumn<int32_t, SQL_C_LONG, SQL_INTEGER>>(meta);
    }
    if (std::holds_alternative<SqlColumnTypeDefinitions::Bigint>(col.type))
    {
        if (meta.size == 0)
            meta.size = 19;
        return std::make_unique<NumericBatchColumn<int64_t, SQL_C_SBIGINT, SQL_BIGINT>>(meta);
    }
    if (std::holds_alternative<SqlColumnTypeDefinitions::Smallint>(col.type))
    {
        if (meta.size == 0)
            meta.size = 5;
        return std::make_unique<NumericBatchColumn<int16_t, SQL_C_SHORT, SQL_SMALLINT>>(meta);
    }
    if (std::holds_alternative<SqlColumnTypeDefinitions::Tinyint>(col.type))
    {
        if (meta.size == 0)
            meta.size = 3;
        // SQL Server's TINYINT is unsigned 0..255. Elsewhere it holds what an int8_t or a uint8_t
        // field wrote, so accept -128..255 and bind it as a short.
        if (serverType == SqlServerType::MICROSOFT_SQL)
            return std::make_unique<NumericBatchColumn<uint8_t, SQL_C_UTINYINT, SQL_TINYINT>>(meta);
        auto column = std::make_unique<NumericBatchColumn<int16_t, SQL_C_SSHORT, SQL_TINYINT>>(meta);
        column->valueRange = std::pair<int64_t, int64_t> { -128, 255 };
        return column;
    }
    if (std::holds_alternative<SqlColumnTypeDefinitions::Real>(col.type))
    {
        meta.size = 15;
        return std::make_unique<NumericBatchColumn<double, SQL_C_DOUBLE, SQL_DOUBLE>>(meta);
    }
    if (std::holds_alternative<SqlColumnTypeDefinitions::Bool>(col.type))
    {
        if (meta.size == 0)
            meta.size = 1;
        return std::make_unique<NumericBatchColumn<int8_t, SQL_C_BIT, SQL_BIT>>(meta);
    }

    if (std::holds_alternative<SqlColumnTypeDefinitions::Guid>(col.type))
    {
        // A nullptr formatter (UNKNOWN, MySQL) has no native GUID type either.
        auto const* const formatter = SqlQueryFormatter::Get(serverType);
        if (formatter == nullptr || !formatter->HasNativeGuidType())
            return std::make_unique<GuidTextBatchColumn>(meta);
        return std::make_unique<GuidBatchColumn>(meta);
    }

    // DateTime and Timestamp columns need special handling to bind as SQL_TYPE_TIMESTAMP
    // instead of strings, which MS SQL Server requires for proper type matching.
    if (std::holds_alternative<SqlColumnTypeDefinitions::DateTime>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Timestamp>(col.type))
    {
        return std::make_unique<DateTimeBatchColumn>(meta);
    }

    // Date columns need special handling to bind as SQL_TYPE_DATE
    if (std::holds_alternative<SqlColumnTypeDefinitions::Date>(col.type))
    {
        return std::make_unique<DateBatchColumn>(meta);
    }

    // Time columns need special handling.
    // Use string binding for databases that support fractional seconds (MSSQL, PostgreSQL).
    // SQL_TIME_STRUCT doesn't support fractional seconds, so we bind as SQL_C_CHAR.
    if (std::holds_alternative<SqlColumnTypeDefinitions::Time>(col.type))
    {
        // MSSQL and PostgreSQL: Use string binding to preserve fractional seconds.
        // SQL_SS_TIME2_STRUCT binding has alignment/precision issues on some systems.
        if (serverType == SqlServerType::MICROSOFT_SQL || serverType == SqlServerType::POSTGRESQL
            || serverType == SqlServerType::SQLITE)
            return std::make_unique<StringTimeBatchColumn>(meta);

        return std::make_unique<TimeBatchColumn>(meta);
    }

    if (std::holds_alternative<SqlColumnTypeDefinitions::Text>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Varchar>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Char>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Decimal>(col.type))
    {
        // Cap string column sizes to prevent memory exhaustion with VARCHAR(MAX)/TEXT types.
        // VARCHAR(MAX) has size INT_MAX (2147483647), which would allocate 2GB per row.
        // Using 64KB as default - large enough for most data, small enough to not exhaust memory.
        // Data larger than this will be truncated during batch insert.
        size_t constexpr maxStringSize = 65535;
        size_t constexpr defaultStringSize = 4096;
        size_t size = 8192;
        if (auto const* vc = std::get_if<SqlColumnTypeDefinitions::Varchar>(&col.type))
            size = (vc->size > 0 && vc->size <= maxStringSize) ? vc->size : defaultStringSize;
        else if (auto const* txt = std::get_if<SqlColumnTypeDefinitions::Text>(&col.type))
        {
            size = (txt->size > 0 && txt->size <= maxStringSize) ? txt->size : defaultStringSize;
            meta.sqlType = SQL_LONGVARCHAR;
        }
        else if (auto const* ch = std::get_if<SqlColumnTypeDefinitions::Char>(&col.type))
            size = (ch->size > 0 && ch->size <= maxStringSize) ? ch->size : defaultStringSize;
        else if (auto const* dec = std::get_if<SqlColumnTypeDefinitions::Decimal>(&col.type))
        {
            // For Decimal columns bound as strings, the size needs to accommodate:
            // precision digits + decimal point + optional sign = precision + 3
            size = dec->precision + 3;
            // Override meta.size since it was set to the decimal precision,
            // but for string binding we need the string length
            meta.size = size;
        }

        if (meta.size == 0 || meta.size >= 4000)
        {
            meta.size = 0;
            meta.sqlType = SQL_LONGVARCHAR;
        }

        meta.decimalDigits = 0;
        auto column = std::make_unique<StringBatchColumn>(meta, size);
        column->trimTrailingSpaces =
            std::holds_alternative<SqlColumnTypeDefinitions::Char>(col.type) && TrimsFixedCharPadding();
        return column;
    }

    if (std::holds_alternative<SqlColumnTypeDefinitions::NVarchar>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::NChar>(col.type))
    {
        // Cap wide string column sizes to prevent memory exhaustion with NVARCHAR(MAX) types.
        // NVARCHAR(MAX) has size INT_MAX, which would allocate 4GB per row (2 bytes per char).
        // Using 32K characters (64KB) as max - large enough for most data, small enough to not exhaust memory.
        size_t constexpr maxWideStringSize = 32767;
        size_t constexpr defaultWideStringSize = 4096;
        size_t size = 8192;
        if (auto const* nvc = std::get_if<SqlColumnTypeDefinitions::NVarchar>(&col.type))
            size = (nvc->size > 0 && nvc->size <= maxWideStringSize) ? nvc->size : defaultWideStringSize;
        else if (auto const* nch = std::get_if<SqlColumnTypeDefinitions::NChar>(&col.type))
            size = (nch->size > 0 && nch->size <= maxWideStringSize) ? nch->size : defaultWideStringSize;

        if (meta.size == 0 || meta.size >= 2000)
        {
            meta.size = 0;
            meta.sqlType = SQL_WLONGVARCHAR;
        }

        meta.decimalDigits = 0;
        auto column = std::make_unique<WideStringBatchColumn>(meta, size);
        column->trimTrailingSpaces =
            std::holds_alternative<SqlColumnTypeDefinitions::NChar>(col.type) && TrimsFixedCharPadding();
        return column;
    }

    if (std::holds_alternative<SqlColumnTypeDefinitions::VarBinary>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Binary>(col.type))
    {
        // Values are stored variable-length and laid out into a fixed-stride buffer at flush
        // time with the stride of the largest value actually present (the old fixed 64 KB stride
        // silently TRUNCATED larger LOBs). BatchManager bounds memory by flushing when the
        // projected stride x rows bytes exceed its budget.
        size_t declaredLen = 0;
        if (auto const* bin = std::get_if<SqlColumnTypeDefinitions::VarBinary>(&col.type))
            declaredLen = bin->size;
        else if (auto const* bin2 = std::get_if<SqlColumnTypeDefinitions::Binary>(&col.type))
            declaredLen = bin2->size;

        if (meta.size == 0 || meta.size >= 4000)
        {
            meta.size = 0;
            meta.sqlType = SQL_LONGVARBINARY;
        }

        meta.decimalDigits = 0;
        return std::make_unique<BinaryBatchColumn>(meta, declaredLen == 0 ? size_t { 65536 } : declaredLen);
    }

    // fallback to varchar
    // TODO: throw exception instead
    meta.decimalDigits = 0;
    return std::make_unique<StringBatchColumn>(meta, 255);
}

void BatchManager::Flush()
{
    if (rowCount == 0)
        return;

    ZoneScopedN("Restore::BatchFlush");
    ZoneValue(rowCount);

    // Rows settled here extend the watermark only if every row before them is settled already.
    auto const base = firstPendingRow;
    auto const extendsWatermark = settledRows == base;

    try
    {
        std::vector<SqlRawColumn> rawCols;
        rawCols.reserve(columns.size());
        {
            ZoneScopedN("Restore::BuildRawColumns");
            for (auto& c: columns)
                rawCols.push_back(c->ToRaw());
        }

        auto const insertRange = [&](size_t firstRow, size_t count) {
            auto const slices =
                rawCols
                | std::views::transform([&](SqlRawColumn const& column) { return SliceRows(column, firstRow, count); })
                | std::ranges::to<std::vector>();
            return hooks.tryInsert(slices, count).transform_error([firstRow](BatchInsertError error) {
                for (auto& row: error.reportedFailedRows)
                    row += firstRow;
                return error;
            });
        };
        // Rows not inserted, so that their nulled cells are not reported.
        auto notInserted = std::vector<bool>(rowCount);
        auto const reportRow = [&](size_t row, std::string_view sqlState, std::string_view message) {
            notInserted[row] = true;
            if (hooks.onRowFailed)
                hooks.onRowFailed(base + row + 1, sqlState, message, DescribeRowKey(rawCols, row));
        };

        // Rows settle in ascending order, so the nulled cells are reported in row order as their
        // rows settle.
        std::ranges::stable_sort(nulledCells, {}, &NulledCell::pendingRow);
        auto nextNulledCell = nulledCells.begin();
        auto const reportNulledCells = [&](size_t settledPrefix) {
            auto const settledEnd = std::ranges::find_if(
                nextNulledCell, nulledCells.end(), [&](NulledCell const& cell) { return cell.pendingRow >= settledPrefix; });
            for (auto const& cell: std::ranges::subrange(nextNulledCell, settledEnd))
                if (!notInserted[cell.pendingRow] && hooks.onCellNulled)
                    hooks.onCellNulled(columnNames[cell.columnIndex], cell.reason);
            nextNulledCell = settledEnd;
        };

        InsertIsolatingFailures(
            rowCount,
            excludedRows,
            insertRange,
            hooks.classify,
            [&](size_t row, SqlErrorInfo const& error) { reportRow(row, error.sqlState, error.message); },
            [&](size_t row) { reportRow(row, {}, exclusionReasons[row].value_or(std::string {})); },
            [&](size_t settledPrefix) {
                reportNulledCells(settledPrefix);
                if (!extendsWatermark)
                    return;
                settledRows = base + settledPrefix;
                if (hooks.onSettled)
                    hooks.onSettled(settledRows);
            });
    }
    catch (...)
    {
        firstPendingRow += rowCount;
        DiscardPendingRows();
        throw;
    }

    firstPendingRow += rowCount;
    DiscardPendingRows();
}

std::size_t BatchManager::SettledRows() const noexcept
{
    return settledRows;
}

void BatchManager::DiscardPendingRows()
{
    for (auto& c: columns)
        c->Clear();
    rowCount = 0;
    excludedRows.clear();
    exclusionReasons.clear();
    nulledCells.clear();
}

void BatchManager::PushIntoColumn(size_t columnIndex,
                                  size_t cellCount,
                                  std::function<void(BatchColumn& column, size_t first, size_t count)> const& push)
{
    auto& column = *columns[columnIndex];
    auto const firstPendingCell = column.PendingRows();
    auto nextCell = size_t { 0 };
    while (nextCell < cellCount)
    {
        try
        {
            push(column, nextCell, cellCount - nextCell);
            return;
        }
        catch (UnconvertibleValueError const& error)
        {
            // The column holds the cells before the rejected one, so this is the rejected cell.
            auto const failedCell = column.PendingRows() - firstPendingCell;
            column.PushNull();
            RecordValueFailure(columnIndex, firstPendingCell + failedCell, error.what());
            nextCell = failedCell + 1;
        }
    }
}

void BatchManager::RecordValueFailure(size_t columnIndex, size_t pendingRow, std::string_view reason)
{
    if (!columnRequired[columnIndex])
    {
        nulledCells.push_back(
            NulledCell { .pendingRow = pendingRow, .columnIndex = columnIndex, .reason = std::string(reason) });
        return;
    }

    if (excludedRows.size() <= pendingRow)
    {
        excludedRows.resize(pendingRow + 1);
        exclusionReasons.resize(pendingRow + 1);
    }
    excludedRows[pendingRow] = true;
    // A row with several bad values is reported with the first.
    if (!exclusionReasons[pendingRow])
        exclusionReasons[pendingRow] = std::format("column '{}': {}", columnNames[columnIndex], reason);
}

std::string BatchManager::DescribeRowKey(std::vector<SqlRawColumn> const& rawColumns, size_t row) const
{
    constexpr size_t MaxKeyLength = 200;

    auto const describe = [&](size_t columnIndex) {
        return DescribeRawCell(rawColumns[columnIndex], row);
    };
    auto const cells = keyColumns.empty() ? std::views::iota(size_t { 0 }, rawColumns.size())
                                                | std::views::transform(describe) | std::ranges::to<std::vector>()
                                          : keyColumns | std::views::transform(describe) | std::ranges::to<std::vector>();
    auto key = cells | std::views::join_with(std::string_view { ", " }) | std::ranges::to<std::string>();
    if (key.size() > MaxKeyLength)
    {
        // Never cut inside a UTF-8 sequence.
        auto length = MaxKeyLength;
        while (length > 0 && (static_cast<unsigned char>(key[length]) & 0xC0U) == 0x80U)
            --length;
        key.resize(length);
    }
    return key;
}

void BatchManager::SkipRows(size_t count)
{
    if (count == 0)
        return;
    Flush();
    auto const extendsWatermark = settledRows == firstPendingRow;
    firstPendingRow += count;
    if (!extendsWatermark)
        return;
    settledRows = firstPendingRow;
    if (hooks.onSettled)
        hooks.onSettled(settledRows);
}

namespace
{
    /// Byte budget for one batch's fixed-stride raw buffers: variable-length columns (large
    /// binaries / long texts) flush early instead of letting stride x rows explode (a 2 MB blob
    /// at a 1000-row capacity would otherwise stage 2 GB at ToRaw time).
    constexpr size_t MaxBatchRawBytes = 64ULL * 1024 * 1024;

    bool ExceedsByteBudget(std::vector<std::unique_ptr<BatchColumn>> const& columns)
    {
        size_t total = 0;
        for (auto const& column: columns)
            total += column->ProjectedRawBytes();
        return total > MaxBatchRawBytes;
    }
} // namespace

void BatchManager::PushRow(std::vector<BackupValue> const& row)
{
    if (row.size() != columns.size())
        return;
    for (auto const columnIndex: std::views::iota(size_t { 0 }, row.size()))
        PushIntoColumn(columnIndex, 1, [&row, columnIndex](BatchColumn& column, size_t /*first*/, size_t /*count*/) {
            column.Push(row[columnIndex]);
        });
    rowCount++;
    if (rowCount >= capacity || ExceedsByteBudget(columns))
        Flush();
}

void BatchManager::PushBatch(ColumnBatch const& batch)
{
    PushBatch(batch, 0);
}

void BatchManager::PushBatch(ColumnBatch const& batch, std::size_t skipRows)
{
    if (batch.rowCount == 0)
        return;
    if (batch.columns.size() != columns.size())
        return;

    auto const skipped = std::min(skipRows, batch.rowCount);
    SkipRows(skipped);

    size_t remaining = batch.rowCount - skipped;
    size_t offset = skipped;

    while (remaining > 0)
    {
        size_t const space = capacity - rowCount;
        if (space == 0)
        {
            Flush();
            continue;
        }

        size_t const chunk = std::min(remaining, space);
        for (auto const columnIndex: std::views::iota(size_t { 0 }, columns.size()))
        {
            PushIntoColumn(columnIndex, chunk, [&](BatchColumn& column, size_t first, size_t count) {
                column.PushFromBatch(batch.columns[columnIndex], batch.nullIndicators[columnIndex], offset + first, count);
            });
        }

        rowCount += chunk;
        offset += chunk;
        remaining -= chunk;

        if (ExceedsByteBudget(columns))
            Flush();
    }
    if (rowCount >= capacity)
        Flush();
}

} // namespace Lightweight::detail
