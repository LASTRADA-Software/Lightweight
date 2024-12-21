// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <variant>

namespace SqlColumnTypeDefinitions
{

// clang-format off
struct Binary { std::size_t size = 255; };
struct Bool {};
struct Char { std::size_t size = 1; };
struct NChar { std::size_t size = 1; };
struct Varchar { std::size_t size = 255; };
struct NVarchar { std::size_t size = 255; };
struct Text { std::size_t size {}; };
struct Smallint {};
struct Integer {};
struct Bigint {};
struct Real {};
struct Decimal { std::size_t precision {}; std::size_t scale {}; };
struct DateTime {};
struct Timestamp {};
struct Date {};
struct Time {};
struct Guid {};
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
                                             SqlColumnTypeDefinitions::Smallint,
                                             SqlColumnTypeDefinitions::Text,
                                             SqlColumnTypeDefinitions::Time,
                                             SqlColumnTypeDefinitions::Timestamp,
                                             SqlColumnTypeDefinitions::Varchar>;
