// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Lup2DbTool
{

/// @brief Parses a SQL WHERE-clause body structurally and returns a canonical
/// SQL rendering on success, or `std::nullopt` on a form we don't understand.
///
/// The grammar accepted:
/// @code
///   condition  := disjunction
///   disjunction:= conjunction ( OR conjunction )*
///   conjunction:= negation   ( AND negation   )*
///   negation   := NOT negation | primary
///   primary    := '(' condition ')'
///               | EXISTS '(' subquery ')'
///               | identifier IS [NOT] NULL
///               | identifier [NOT] IN '(' subquery ')'
///               | identifier <op> value
///   op         := = | <> | != | < | <= | > | >=
///   identifier := [qualifier '.'] name                (qualifier/name may be quoted/bracketed)
///   value      := number | string | NULL | identifier
///   subquery   := <any text up to the matching ')', paren-balanced and quote-aware>
/// @endcode
///
/// Subquery bodies are kept verbatim: we validate the parenthesis balance and
/// confirm the leading keyword is `SELECT`, but we don't parse inside. That is
/// sufficient for the migration use-case where the subquery is re-emitted as-is
/// into the final `WHERE …` clause executed by the target database.
///
/// The canonical output uses ALL-CAPS keywords, exactly one space around
/// operators, double-quoted identifiers, and re-parenthesized subqueries.
///
/// @param whereBody The text after the `WHERE` keyword (with no leading `WHERE`).
/// @return Canonical rendering of the clause, or `std::nullopt` if parsing fails.
[[nodiscard]] std::optional<std::string> ParseWhereClause(std::string_view whereBody);

} // namespace Lup2DbTool
