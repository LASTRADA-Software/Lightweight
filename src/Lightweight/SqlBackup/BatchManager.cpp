// SPDX-License-Identifier: Apache-2.0
#include "../DataBinder/SqlDate.hpp"
#include "../DataBinder/SqlDateTime.hpp"
#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/SqlTime.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "BatchManager.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <variant>

namespace Lightweight::detail
{

using namespace SqlBackup;

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

    void PushNull() override
    {
        data.push_back(T {});
        indicators.push_back(SQL_NULL_DATA);
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
                            T v {};
                            auto const& s = inVec[idx];
                            if (s == "NULL" || s.empty())
                                PushNull();
                            else
                            {
                                std::from_chars(s.data(), s.data() + s.size(), v);
                                data.push_back(v);
                                indicators.push_back(sizeof(T));
                            }
                        }
                        else
                            PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<bool>>)
                    {
                        if (idx < inVec.size())
                        {
                            data.push_back(static_cast<T>(inVec[idx] ? 1 : 0));
                            indicators.push_back(sizeof(T));
                        }
                        else
                            PushNull();
                    }
                    else if constexpr (std::is_same_v<typename VecT::value_type, std::vector<uint8_t>>)
                    {
                        PushNull();
                    }
                    else if constexpr (std::is_convertible_v<typename VecT::value_type, T>)
                    {
                        if (idx < inVec.size())
                        {
                            data.push_back(static_cast<T>(inVec[idx]));
                            indicators.push_back(sizeof(T));
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

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
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
                    if (arg == "NULL")
                        this->PushNull();
                    else
                    {
                        T v {};
                        std::from_chars(arg.data(), arg.data() + arg.size(), v);
                        this->data.push_back(v);
                        this->indicators.push_back(sizeof(T));
                    }
                }
                else if constexpr (std::is_convertible_v<ArgT, T>)
                {
                    this->data.push_back(static_cast<T>(arg));
                    this->indicators.push_back(sizeof(T));
                }
                else
                {
                    this->data.push_back(static_cast<T>(0));
                    this->indicators.push_back(sizeof(T));
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

    /// Parses ISO 8601 datetime string format: YYYY-MM-DDTHH:MM:SS.mmm
    static SQL_TIMESTAMP_STRUCT ParseDateTime(std::string_view s)
    {
        SQL_TIMESTAMP_STRUCT ts {};

        // Expected format: "YYYY-MM-DDTHH:MM:SS.mmm" (length 23)
        // or "YYYY-MM-DD HH:MM:SS" (length 19) or similar variations
        if (s.size() < 19)
            return ts;

        auto parseNum = [&s](size_t pos, size_t len) -> int {
            int val = 0;
            std::from_chars(s.data() + pos, s.data() + pos + len, val);
            return val;
        };

        ts.year = static_cast<SQLSMALLINT>(parseNum(0, 4));
        ts.month = static_cast<SQLUSMALLINT>(parseNum(5, 2));
        ts.day = static_cast<SQLUSMALLINT>(parseNum(8, 2));
        ts.hour = static_cast<SQLUSMALLINT>(parseNum(11, 2));
        ts.minute = static_cast<SQLUSMALLINT>(parseNum(14, 2));
        ts.second = static_cast<SQLUSMALLINT>(parseNum(17, 2));

        // Parse fractional seconds if present (after position 19, following '.')
        if (s.size() > 20 && s[19] == '.')
        {
            // Parse milliseconds and convert to nanoseconds (fraction field is in 100ns units)
            int millis = 0;
            size_t fracLen = std::min(s.size() - 20, size_t { 3 });
            std::from_chars(s.data() + 20, s.data() + 20 + fracLen, millis);
            // Pad to 3 digits if shorter
            while (fracLen < 3)
            {
                millis *= 10;
                fracLen++;
            }
            ts.fraction = static_cast<SQLUINTEGER>(millis) * 1'000'000; // ms to 100ns units
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
                        data.push_back(ParseDateTime(arg));
                        indicators.push_back(sizeof(SQL_TIMESTAMP_STRUCT));
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
                                data.push_back(ParseDateTime(s));
                                indicators.push_back(sizeof(SQL_TIMESTAMP_STRUCT));
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

    /// Parses ISO 8601 date string format: YYYY-MM-DD
    static SQL_DATE_STRUCT ParseDate(std::string_view s)
    {
        SQL_DATE_STRUCT ds {};

        // Expected format: "YYYY-MM-DD" (length 10)
        if (s.size() < 10)
            return ds;

        auto parseNum = [&s](size_t pos, size_t len) -> int {
            int val = 0;
            std::from_chars(s.data() + pos, s.data() + pos + len, val);
            return val;
        };

        ds.year = static_cast<SQLSMALLINT>(parseNum(0, 4));
        ds.month = static_cast<SQLUSMALLINT>(parseNum(5, 2));
        ds.day = static_cast<SQLUSMALLINT>(parseNum(8, 2));

        return ds;
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
                        data.push_back(ParseDate(arg));
                        indicators.push_back(sizeof(SQL_DATE_STRUCT));
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
                                data.push_back(ParseDate(s));
                                indicators.push_back(sizeof(SQL_DATE_STRUCT));
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

    /// Parses time string format: HH:MM:SS or HH:MM:SS.ffffff
    /// Note: Fractional seconds are ignored as SQL_TIME_STRUCT doesn't support them.
    static SQL_TIME_STRUCT ParseTime(std::string_view s)
    {
        SQL_TIME_STRUCT ts {};

        // Expected format: "HH:MM:SS" (length 8) or "HH:MM:SS.ffffff"
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
                        indicators.push_back(sizeof(SQL_TIME_STRUCT));
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
                                indicators.push_back(sizeof(SQL_TIME_STRUCT));
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
                        std::array<char, 16> buf {};
                        auto const len = std::min(arg.size(), size_t { 15 });
                        std::memcpy(buf.data(), arg.data(), len);
                        buf[len] = '\0';
                        data.push_back(buf);
                        indicators.push_back(static_cast<SQLLEN>(len));
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
                                std::array<char, 16> buf {};
                                auto const len = std::min(s.size(), size_t { 15 });
                                std::memcpy(buf.data(), s.data(), len);
                                buf[len] = '\0';
                                data.push_back(buf);
                                indicators.push_back(static_cast<SQLLEN>(len));
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

struct StringBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<char> buffer;
    size_t maxLen;
    std::vector<SQLLEN> indicators;

    StringBatchColumn(SqlRawColumnMetadata meta, size_t maxLen):
        metadata(meta),
        maxLen(std::max(size_t { 1 }, maxLen))
    {
        // Bind as standard C char array.
        metadata.cType = SQL_C_CHAR;
        // Standard VARCHAR SQL type (unless overridden).
        if (metadata.sqlType == 0)
            metadata.sqlType = maxLen > 255 ? SQL_LONGVARCHAR : SQL_VARCHAR;
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
                                PushNull();
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
                    this->PushNull();
                else
                    this->PushString(std::format("{}", arg));
            },
            val);
    }

    void PushString(std::string_view s)
    {
        size_t const bufOffset = buffer.size();
        size_t const copyLen =
            std::min(s.size(), maxLen); // Binary doesn't need null terminator space technically, but strict sizing
        buffer.resize(bufOffset + maxLen);
        std::memcpy(buffer.data() + bufOffset, s.data(), copyLen);
        // We don't need null terminator for SQL_C_BINARY, but zeroing rest is good practice
        std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(bufOffset + copyLen),
                  buffer.begin() + static_cast<std::ptrdiff_t>(bufOffset + maxLen),
                  0);
        indicators.push_back(static_cast<SQLLEN>(copyLen));
    }

    void PushNull() override
    {
        size_t const bufOffset = buffer.size();
        buffer.resize(bufOffset + maxLen);
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        // meta.size = maxLen;
        meta.bufferLength = maxLen;
        return SqlRawColumn { .metadata = meta,
                              .data = std::as_bytes(std::span(buffer)),
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

struct WideStringBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<char16_t> buffer;
    size_t maxLen;
    std::vector<SQLLEN> indicators;

    WideStringBatchColumn(SqlRawColumnMetadata meta, size_t maxLen):
        metadata(meta),
        maxLen(std::max(size_t { 1 }, maxLen))
    {
        // Bind as Unicode char array.
        metadata.cType = SQL_C_WCHAR;
        // Only set sqlType if not already set (preserve SQL_WLONGVARCHAR for MAX types)
        if (metadata.sqlType == 0)
            metadata.sqlType = maxLen > 255 ? SQL_WLONGVARCHAR : SQL_WVARCHAR;
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
                                PushNull();
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
                    this->PushNull();
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
        auto const ws = ToUtf16(std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size()));
        size_t const bufOffset = buffer.size();
        // size in characters (not bytes)
        size_t const copyLen = std::min(ws.size(), maxLen);

        buffer.resize(bufOffset + maxLen);
        std::memcpy(buffer.data() + bufOffset, ws.data(), copyLen * sizeof(char16_t));

        // Zero-fill remaining buffer
        if (copyLen < maxLen)
            std::memset(buffer.data() + bufOffset + copyLen, 0, (maxLen - copyLen) * sizeof(char16_t));

        indicators.push_back(static_cast<SQLLEN>(copyLen * sizeof(char16_t)));
    }

    void PushNull() override
    {
        size_t const bufOffset = buffer.size();
        buffer.resize(bufOffset + maxLen); // Default init to 0
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        // Preserve size=0 for MAX types to avoid HY104 precision errors on MS SQL
        if (metadata.size != 0)
            meta.size = maxLen;
        meta.bufferLength = maxLen * sizeof(char16_t);
        // Preserve sqlType if already set (e.g., SQL_WLONGVARCHAR for MAX types)
        if (metadata.sqlType == 0)
            meta.sqlType = maxLen > 4000 ? SQL_WLONGVARCHAR : SQL_WVARCHAR;

        return SqlRawColumn { .metadata = meta,
                              // Send data as bytes
                              .data = std::as_bytes(std::span(buffer)),
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
    }
};

struct BinaryBatchColumn: BatchColumn
{
    SqlRawColumnMetadata metadata;
    std::vector<uint8_t> buffer;
    size_t maxLen;
    std::vector<SQLLEN> indicators;

    BinaryBatchColumn(SqlRawColumnMetadata meta, size_t maxLen):
        metadata(meta),
        maxLen(std::max(size_t { 1 }, maxLen))
    {
        metadata.cType = SQL_C_BINARY;
        // Only set sqlType if not already set (preserve SQL_LONGVARBINARY for MAX types)
        if (metadata.sqlType == 0)
            metadata.sqlType = maxLen > 255 ? SQL_LONGVARBINARY : SQL_VARBINARY;
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
                    else if constexpr (std::is_same_v<VecT, std::vector<std::vector<uint8_t>>>)
                    {
                        if (idx < inVec.size())
                        {
                            auto const& v = inVec[idx];
                            size_t const bufOffset = buffer.size();
                            buffer.resize(bufOffset + maxLen);
                            size_t const copyLen = std::min(v.size(), maxLen);
                            if (copyLen > 0)
                                std::memcpy(buffer.data() + bufOffset, v.data(), copyLen);
                            if (copyLen < maxLen)
                                std::memset(buffer.data() + bufOffset + copyLen, 0, maxLen - copyLen);
                            indicators.push_back(static_cast<SQLLEN>(copyLen));
                        }
                        else
                            PushNull();
                    }
                    else if constexpr (std::is_same_v<VecT, std::vector<std::string>>)
                    {
                        if (idx < inVec.size())
                        {
                            auto const& s = inVec[idx];
                            if (s.empty())
                            {
                                size_t const bufOffset = buffer.size();
                                buffer.resize(bufOffset + maxLen);
                                std::memset(buffer.data() + bufOffset, 0, maxLen);
                                indicators.push_back(0);
                            }
                            else
                            {
                                size_t const bufOffset = buffer.size();
                                buffer.resize(bufOffset + maxLen);

                                // Decode Hex
                                size_t const hexLen = s.size();
                                size_t const binLen = hexLen / 2;
                                size_t const copyLen = std::min(binLen, maxLen);

                                for (size_t b = 0; b < copyLen; ++b)
                                {
                                    char h1 = s[b * 2];
                                    char h2 = s[(b * 2) + 1];
                                    auto fromHex = [](char c) -> uint8_t {
                                        if (c >= '0' && c <= '9')
                                            return static_cast<uint8_t>(c - '0');
                                        if (c >= 'A' && c <= 'F')
                                            return static_cast<uint8_t>(c - 'A' + 10);
                                        if (c >= 'a' && c <= 'f')
                                            return static_cast<uint8_t>(c - 'a' + 10);
                                        return 0;
                                    };
                                    buffer[bufOffset + b] = static_cast<uint8_t>((fromHex(h1) << 4) | fromHex(h2));
                                }

                                if (copyLen < maxLen)
                                    std::memset(buffer.data() + bufOffset + copyLen, 0, maxLen - copyLen);
                                indicators.push_back(static_cast<SQLLEN>(copyLen));
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

    void Push(BackupValue const& val) override
    {
        std::visit(
            [this](auto&& arg) {
                using ArgT = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<ArgT, std::monostate>)
                {
                    this->PushNull();
                }
                else if constexpr (std::is_same_v<ArgT, std::vector<uint8_t>>)
                {
                    size_t const offset = buffer.size();
                    buffer.resize(offset + maxLen);
                    size_t const copyLen = std::min(arg.size(), maxLen);
                    std::memcpy(buffer.data() + offset, arg.data(), copyLen);
                    if (copyLen < maxLen)
                        std::memset(buffer.data() + offset + copyLen, 0, maxLen - copyLen);
                    indicators.push_back(static_cast<SQLLEN>(copyLen));
                }
                else if constexpr (std::is_same_v<ArgT, std::string>)
                {
                    if (arg.empty())
                    {
                        size_t const offset = buffer.size();
                        buffer.resize(offset + maxLen);
                        std::memset(buffer.data() + offset, 0, maxLen);
                        indicators.push_back(0);
                    }
                    else
                    {
                        size_t const offset = buffer.size();
                        buffer.resize(offset + maxLen);

                        size_t const copyLen = std::min(arg.size(), maxLen);
                        std::memcpy(buffer.data() + offset, arg.data(), copyLen);

                        if (copyLen < maxLen)
                            std::memset(buffer.data() + offset + copyLen, 0, maxLen - copyLen);
                        indicators.push_back(static_cast<SQLLEN>(copyLen));
                    }
                }
                else
                {
                    this->PushNull();
                }
            },
            val);
    }

    void PushNull() override
    {
        buffer.resize(buffer.size() + maxLen, 0);
        indicators.push_back(SQL_NULL_DATA);
    }

    void Clear() override
    {
        buffer.clear();
        buffer.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
    }

    SqlRawColumn ToRaw() override
    {
        SqlRawColumnMetadata meta = metadata;
        // meta.size = maxLen; // Keep original column size (0 for MAX)
        meta.bufferLength = maxLen;
        return SqlRawColumn { .metadata = meta,
                              .data = std::span<std::byte const> { reinterpret_cast<std::byte const*>(buffer.data()),
                                                                   buffer.size() },
                              .indicators = std::span<SQLLEN const> { indicators.data(), indicators.size() } };
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
                            if (s == "NULL" || s.empty())
                                PushNull();
                            else if (auto guid = SqlGuid::TryParse(s); guid.has_value())
                            {
                                data.push_back(*guid);
                                indicators.push_back(sizeof(SqlGuid));
                            }
                            else
                                PushNull();
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
                    if (arg == "NULL" || arg.empty())
                        this->PushNull();
                    else if (auto guid = SqlGuid::TryParse(arg); guid.has_value())
                    {
                        this->data.push_back(*guid);
                        this->indicators.push_back(sizeof(SqlGuid));
                    }
                    else
                        this->PushNull();
                }
                else
                {
                    this->PushNull();
                }
            },
            val);
    }

    void Clear() override
    {
        data.clear();
        data.shrink_to_fit();
        indicators.clear();
        indicators.shrink_to_fit();
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

BatchManager::BatchManager(BatchExecutor executor,
                           std::vector<SqlColumnDeclaration> const& colDecls,
                           size_t capacity,
                           SqlServerType serverType):
    executor(std::move(executor)),
    serverType(serverType)
{
    // logic to create columns based on type
    for (auto const& col: colDecls)
    {
        columns.push_back(CreateColumn(col));
    }

    // Calculate memory-aware capacity
    // Estimate bytes per row based on column definitions
    size_t bytesPerRow = 0;
    for (auto const& col: colDecls)
    {
        bytesPerRow += EstimateColumnBufferSize(col);
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

BatchManager::~BatchManager() = default;

BatchManager::BatchManager(BatchManager&&) noexcept = default;

BatchManager& BatchManager::operator=(BatchManager&&) noexcept = default;

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
        return std::make_unique<NumericBatchColumn<int8_t, SQL_C_TINYINT, SQL_TINYINT>>(meta);
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
        return std::make_unique<StringBatchColumn>(meta, size);
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
        return std::make_unique<WideStringBatchColumn>(meta, size);
    }

    if (std::holds_alternative<SqlColumnTypeDefinitions::VarBinary>(col.type)
        || std::holds_alternative<SqlColumnTypeDefinitions::Binary>(col.type))
    {
        // Cap binary column sizes to prevent memory exhaustion with VARBINARY(MAX) types.
        // VARBINARY(MAX) has size INT_MAX (2147483647), which would allocate 2GB per row.
        // Using 64KB as max - large enough for most data, small enough to not exhaust memory.
        // Data larger than this will be truncated during batch insert.
        size_t constexpr maxBinarySize = 65535;
        size_t constexpr defaultBinarySize = 65535;
        size_t size = defaultBinarySize;
        if (auto const* bin = std::get_if<SqlColumnTypeDefinitions::VarBinary>(&col.type))
            size = (bin->size > 0 && bin->size <= maxBinarySize) ? bin->size : defaultBinarySize;
        else if (auto const* bin2 = std::get_if<SqlColumnTypeDefinitions::Binary>(&col.type))
            size = (bin2->size > 0 && bin2->size <= maxBinarySize) ? bin2->size : defaultBinarySize;

        if (meta.size == 0 || meta.size >= 4000)
        {
            meta.size = 0;
            meta.sqlType = SQL_LONGVARBINARY;
        }

        meta.decimalDigits = 0;
        return std::make_unique<BinaryBatchColumn>(meta, size);
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

    try
    {
        std::vector<SqlRawColumn> rawCols;
        rawCols.reserve(columns.size());
        for (auto& c: columns)
            rawCols.push_back(c->ToRaw());

        executor(rawCols, rowCount);
    }
    catch (...)
    {
        for (auto& c: columns)
            c->Clear();
        rowCount = 0;
        throw;
    }

    for (auto& c: columns)
        c->Clear();
    rowCount = 0;
}

void BatchManager::PushRow(std::vector<BackupValue> const& row)
{
    if (row.size() != columns.size())
        return;
    for (size_t i = 0; i < row.size(); ++i)
        columns[i]->Push(row[i]);
    rowCount++;
    if (rowCount >= capacity)
        Flush();
}

void BatchManager::PushBatch(ColumnBatch const& batch)
{
    if (batch.rowCount == 0)
        return;
    if (batch.columns.size() != columns.size())
        return;

    size_t remaining = batch.rowCount;
    size_t offset = 0;

    while (remaining > 0)
    {
        size_t const space = capacity - rowCount;
        if (space == 0)
        {
            Flush();
            continue;
        }

        size_t const chunk = std::min(remaining, space);
        for (size_t i = 0; i < columns.size(); ++i)
        {
            columns[i]->PushFromBatch(batch.columns[i], batch.nullIndicators[i], offset, chunk);
        }

        rowCount += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    if (rowCount >= capacity)
        Flush();
}

} // namespace Lightweight::detail
