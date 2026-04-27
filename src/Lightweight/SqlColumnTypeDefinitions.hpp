// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include <cstdint>
#include <optional>
#include <variant>

#include <sql.h>
#include <sqlext.h>

// Microsoft SQL Server extension for TIME(7) with fractional seconds.
// This is supported by SQL Server 2008 and later, MariaDB and MySQL ODBC drivers.
// clang-format off
#if !defined(SQL_SS_TIME2)
    #define SQL_SS_TIME2 (-154)

    struct SQL_SS_TIME2_STRUCT
    {
        SQLUSMALLINT hour;
        SQLUSMALLINT minute;
        SQLUSMALLINT second;
        SQLUINTEGER fraction;
    };

    static_assert(
        sizeof(SQL_SS_TIME2_STRUCT) == 12,
        "SQL_SS_TIME2_STRUCT size must be padded 12 bytes, as per ODBC extension spec."
    );
#endif
// clang-format on

namespace Lightweight
{

/// @brief Represents if a column is nullable or not.
enum class SqlNullable : uint8_t
{
    NotNull,
    Null,
};

namespace SqlColumnTypeDefinitions
{

    // clang-format off
struct Bigint { auto operator<=>(Bigint const&) const = default; };
struct Binary { std::size_t size = 255; auto operator<=>(Binary const&) const = default; };
struct Bool { auto operator<=>(Bool const&) const = default; };
struct Char { std::size_t size = 1; auto operator<=>(Char const&) const = default; };
struct Date { auto operator<=>(Date const&) const = default; };
struct DateTime { auto operator<=>(DateTime const&) const = default; };
struct Decimal { std::size_t precision {}; std::size_t scale {}; auto operator<=>(Decimal const&) const = default; };
struct Guid { auto operator<=>(Guid const&) const = default; };
struct Integer { auto operator<=>(Integer const&) const = default; };
struct NChar { std::size_t size = 1; auto operator<=>(NChar const&) const = default; };
struct NVarchar { std::size_t size = 255; auto operator<=>(NVarchar const&) const = default; };
struct Real { std::size_t precision = {}; auto operator<=>(Real const&) const = default; };
struct Smallint { auto operator<=>(Smallint const&) const = default; };
struct Text { std::size_t size {}; auto operator<=>(Text const&) const = default; };
struct Time { auto operator<=>(Time const&) const = default; };
struct Timestamp { auto operator<=>(Timestamp const&) const = default; };
struct Tinyint { auto operator<=>(Tinyint const&) const = default; };
struct VarBinary { std::size_t size = 255; auto operator<=>(VarBinary const&) const = default; };
struct Varchar { std::size_t size = 255; auto operator<=>(Varchar const&) const = default; };
    // clang-format on

} // namespace SqlColumnTypeDefinitions

using SqlColumnTypeDefinition = std::variant<SqlColumnTypeDefinitions::Bigint,
                                             SqlColumnTypeDefinitions::Binary,
                                             SqlColumnTypeDefinitions::Bool,
                                             SqlColumnTypeDefinitions::Char,
                                             SqlColumnTypeDefinitions::Date,
                                             SqlColumnTypeDefinitions::DateTime,
                                             SqlColumnTypeDefinitions::Decimal,
                                             SqlColumnTypeDefinitions::Guid,
                                             SqlColumnTypeDefinitions::Integer,
                                             SqlColumnTypeDefinitions::NChar,
                                             SqlColumnTypeDefinitions::NVarchar,
                                             SqlColumnTypeDefinitions::Real,
                                             SqlColumnTypeDefinitions::Tinyint,
                                             SqlColumnTypeDefinitions::Smallint,
                                             SqlColumnTypeDefinitions::Text,
                                             SqlColumnTypeDefinitions::Time,
                                             SqlColumnTypeDefinitions::Timestamp,
                                             SqlColumnTypeDefinitions::VarBinary,
                                             SqlColumnTypeDefinitions::Varchar>;

/// Maps ODBC data type (with given @p size and @p precision) to SqlColumnTypeDefinition
///
/// @return SqlColumnTypeDefinition if the data type is supported, otherwise std::nullopt
constexpr std::optional<SqlColumnTypeDefinition> MakeColumnTypeFromNative(int value, std::size_t size, std::size_t precision)
{
    // Maps ODBC data types to SqlColumnTypeDefinition
    // See: https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/sql-data-types?view=sql-server-ver16
    using namespace SqlColumnTypeDefinitions;
    // clang-format off
    switch (value)
    {
        case SQL_BIGINT: return Bigint {};
        case SQL_BINARY: return Binary { size };
        case SQL_BIT: return Bool {};
        case SQL_CHAR: return Char { size };
        case SQL_DATE: return Date {};
        case SQL_DECIMAL: return Decimal { .precision = size, .scale = precision };
        case SQL_DOUBLE: return Real { .precision = 53 };
        case SQL_FLOAT: return Real { . precision = precision };
        case SQL_GUID: return Guid {};
        case SQL_INTEGER: return Integer {};
        case SQL_LONGVARBINARY: return VarBinary { size };
        case SQL_LONGVARCHAR: return Varchar { size };
        case SQL_NUMERIC: return Decimal { .precision = size, .scale = precision };
        case SQL_REAL: return Real { .precision = 24 };
        case SQL_SMALLINT: return Smallint {};
        case SQL_TIME: return Time {};
        case SQL_TIMESTAMP: return DateTime {};
        case SQL_TINYINT: return Tinyint {};
        case SQL_TYPE_DATE: return Date {};
        case SQL_TYPE_TIME: return Time {};
        case SQL_SS_TIME2: return Time {}; // Microsoft SQL Server extension
        case SQL_TYPE_TIMESTAMP: return DateTime {};
        case SQL_VARBINARY: return VarBinary { size };
        case SQL_VARCHAR: return Varchar { size };
        case SQL_WCHAR: return NChar { size };
        case SQL_WLONGVARCHAR: return NVarchar { size };
        case SQL_WVARCHAR: return NVarchar { size };
        case SQL_UNKNOWN_TYPE: return std::nullopt;
        default: return std::nullopt;
    }
    // clang-format on
}

} // namespace Lightweight
