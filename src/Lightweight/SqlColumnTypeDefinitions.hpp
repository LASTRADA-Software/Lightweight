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
struct Bigint {};
struct Binary { std::size_t size = 255; };
struct Bool {};
struct Char { std::size_t size = 1; };
struct Date {};
struct DateTime {};
struct Decimal { std::size_t precision {}; std::size_t scale {}; };
struct Guid {};
struct Integer {};
struct NChar { std::size_t size = 1; };
struct NVarchar { std::size_t size = 255; };
struct Real { std::size_t precision = {}; };
struct Smallint {};
struct Text { std::size_t size {}; };
struct Time {};
struct Timestamp {};
struct Tinyint {};
struct VarBinary { std::size_t size = 255; };
struct Varchar { std::size_t size = 255; };
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
        case SQL_DECIMAL: return Decimal { .precision = precision, .scale = size };
        case SQL_DOUBLE: return Real { .precision = 53 };
        case SQL_FLOAT: return Real { . precision = precision };
        case SQL_GUID: return Guid {};
        case SQL_INTEGER: return Integer {};
        case SQL_LONGVARBINARY: return VarBinary { size };
        case SQL_LONGVARCHAR: return Varchar { size };
        case SQL_NUMERIC: return Decimal { .precision = precision, .scale = size };
        case SQL_REAL: return Real { .precision = 24 };
        case SQL_SMALLINT: return Smallint {};
        case SQL_TIME: return Time {};
        case SQL_TIMESTAMP: return DateTime {};
        case SQL_TINYINT: return Tinyint {};
        case SQL_TYPE_DATE: return Date {};
        case SQL_TYPE_TIME: return Time {};
        case SQL_TYPE_TIMESTAMP: return DateTime {};
        case SQL_VARBINARY: return Binary { size };
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
