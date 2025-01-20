// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <variant>

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
struct Real {};
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
