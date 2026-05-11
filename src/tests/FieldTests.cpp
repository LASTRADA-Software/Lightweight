// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <compare>
#include <optional>
#include <string>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace Lightweight;

// ================================================================================================
// Construction
// ================================================================================================

TEST_CASE("Field<int>: default-constructed value is the value-initialized T", "[Field]")
{
    Field<int> const f;
    CHECK(f.Value() == 0);
}

TEST_CASE("Field<int>: value constructor", "[Field]")
{
    Field<int> const f { 42 };
    CHECK(f.Value() == 42);
    // The default-initialized _modified flag is true.
    CHECK(f.IsModified());
}

TEST_CASE("Field<int>: copy construction preserves value", "[Field]")
{
    Field<int> const original { 7 };
    Field<int> const copy = original;
    CHECK(copy.Value() == 7);

    Field<int> assigned;
    assigned = original;
    CHECK(assigned.Value() == 7);
}

TEST_CASE("Field<std::string>: move construction transfers payload", "[Field]")
{
    Field<std::string> source;
    source = std::string { "moved" };
    Field<std::string> const moved = std::move(source);
    CHECK(moved.Value() == "moved");
}

TEST_CASE("Field<std::optional<int>>: default-constructed is empty optional", "[Field]")
{
    Field<std::optional<int>> const f;
    CHECK_FALSE(f.Value().has_value());
}

// ================================================================================================
// Assignment from value sets the modified flag
// ================================================================================================

TEST_CASE("Field<int>: assignment marks modified", "[Field]")
{
    Field<int> f;
    f.SetModified(false);
    REQUIRE_FALSE(f.IsModified());

    f = 5;
    CHECK(f.IsModified());
    CHECK(f.Value() == 5);
}

TEST_CASE("Field<std::string>: assignment from string-like values", "[Field]")
{
    Field<std::string> f;
    f.SetModified(false);

    f = "hello"s;
    CHECK(f.Value() == "hello");
    CHECK(f.IsModified());
}

// ================================================================================================
// Comparison: Field-vs-Field and Field-vs-value
// ================================================================================================

TEST_CASE("Field<int>: equality and ordering vs other Fields", "[Field]")
{
    Field<int> const a { 1 };
    Field<int> const b { 1 };
    Field<int> const c { 2 };

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > a);
    CHECK((a <=> b) == std::weak_ordering::equivalent);
    CHECK((a <=> c) == std::weak_ordering::less);
}

TEST_CASE("Field<int>: equality vs convertible value", "[Field]")
{
    Field<int> const f { 99 };
    CHECK(f == 99);
    CHECK(f != 100);
    CHECK_FALSE(f != 99);
    CHECK_FALSE(f == 100);
}

// ================================================================================================
// Modified-state lifecycle
// ================================================================================================

TEST_CASE("Field<int>: SetModified flips the dirty flag", "[Field]")
{
    Field<int> f;
    REQUIRE(f.IsModified()); // default-constructed is "modified"

    f.SetModified(false);
    CHECK_FALSE(f.IsModified());

    f.SetModified(true);
    CHECK(f.IsModified());
}

TEST_CASE("Field<int>: MutableValue does NOT mark the field modified", "[Field]")
{
    Field<int> f { 1 };
    f.SetModified(false);

    f.MutableValue() = 2;
    CHECK(f.Value() == 2);
    // Documented contract: mutating via MutableValue() bypasses the dirty tracking.
    CHECK_FALSE(f.IsModified());
}

// ================================================================================================
// ValueOr — only available when the field is optional
// ================================================================================================

TEST_CASE("Field<std::optional<int>>::ValueOr returns the held value or the default", "[Field]")
{
    Field<std::optional<int>> empty;
    Field<std::optional<int>> filled;
    filled = std::optional<int> { 5 };

    CHECK(empty.ValueOr(99) == 99);
    CHECK(filled.ValueOr(99) == 5);
}

// ================================================================================================
// IsOptional / IsMandatory / IsPrimaryKey traits
// ================================================================================================

TEST_CASE("Field traits: IsOptional / IsMandatory", "[Field]")
{
    STATIC_CHECK_FALSE(Field<int>::IsOptional);
    STATIC_CHECK(Field<int>::IsMandatory);

    STATIC_CHECK(Field<std::optional<int>>::IsOptional);
    STATIC_CHECK_FALSE(Field<std::optional<int>>::IsMandatory);
}

TEST_CASE("Field traits: IsPrimaryKey / IsAutoAssignPrimaryKey / IsAutoIncrementPrimaryKey", "[Field]")
{
    STATIC_CHECK_FALSE(Field<int>::IsPrimaryKey);
    STATIC_CHECK(Field<int, PrimaryKey::AutoAssign>::IsPrimaryKey);
    STATIC_CHECK(Field<int, PrimaryKey::AutoAssign>::IsAutoAssignPrimaryKey);
    STATIC_CHECK_FALSE(Field<int, PrimaryKey::AutoAssign>::IsAutoIncrementPrimaryKey);

    STATIC_CHECK(Field<int, PrimaryKey::ServerSideAutoIncrement>::IsPrimaryKey);
    STATIC_CHECK_FALSE(Field<int, PrimaryKey::ServerSideAutoIncrement>::IsAutoAssignPrimaryKey);
    STATIC_CHECK(Field<int, PrimaryKey::ServerSideAutoIncrement>::IsAutoIncrementPrimaryKey);
}

TEST_CASE("Free traits: IsField, IsPrimaryKey<>, IsAutoIncrementPrimaryKey<>", "[Field]")
{
    STATIC_CHECK(IsField<Field<int>>);
    STATIC_CHECK_FALSE(IsField<int>);

    STATIC_CHECK_FALSE(::Lightweight::IsPrimaryKey<Field<int>>);
    STATIC_CHECK(::Lightweight::IsPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
    STATIC_CHECK(::Lightweight::IsPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);

    STATIC_CHECK_FALSE(IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
    STATIC_CHECK(IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
}

// ================================================================================================
// InspectValue — covers each branch of the if-else ladder
// ================================================================================================

TEST_CASE("Field<int>::InspectValue formats numeric values", "[Field]")
{
    Field<int> const f { 42 };
    CHECK(f.InspectValue() == "42");
}

TEST_CASE("Field<std::string>::InspectValue quotes the string", "[Field]")
{
    Field<std::string> f;
    f = "alice"s;
    auto const s = f.InspectValue();
    CHECK(s.contains("alice"));
    CHECK(s.front() == '\'');
    CHECK(s.back() == '\'');
}

TEST_CASE("Field<SqlText>::InspectValue quotes the text payload", "[Field]")
{
    Field<SqlText> f;
    f = SqlText { .value = "hello" };
    auto const s = f.InspectValue();
    CHECK(s.contains("hello"));
    CHECK(s.front() == '\'');
    CHECK(s.back() == '\'');
}

TEST_CASE("Field<SqlDate>::InspectValue is single-quoted", "[Field]")
{
    Field<SqlDate> f;
    f = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 6 } };
    auto const s = f.InspectValue();
    CHECK(s.front() == '\'');
    CHECK(s.back() == '\'');
    CHECK(s.contains("2026"));
}

TEST_CASE("Field<SqlNumeric>::InspectValue uses ToString", "[Field]")
{
    Field<SqlNumeric<10, 2>> f;
    f = SqlNumeric<10, 2> { 12.34 };
    CHECK(f.InspectValue() == "12.34");
}

TEST_CASE("Field<std::optional<int>>::InspectValue: NULL vs value", "[Field]")
{
    Field<std::optional<int>> f;
    CHECK(f.InspectValue() == "NULL");

    f = std::optional<int> { 7 };
    CHECK(f.InspectValue() == "7");
}

// ================================================================================================
// std::formatter<Field<T>>: forwards to the underlying std::formatter<T>
// ================================================================================================

TEST_CASE("std::formatter<Field<int>> forwards to formatter<int>", "[Field]")
{
    Field<int> const f { 42 };
    CHECK(std::format("{}", f) == "42");
    // Format specifiers on the underlying T also propagate through the inheritance.
    CHECK(std::format("{:5}", f) == "   42");
}

TEST_CASE("std::formatter<Field<double>> forwards format specifiers to formatter<double>", "[Field]")
{
    Field<double> const f { 1.5 };
    CHECK(std::format("{:.2f}", f) == "1.50");
    CHECK(std::format("{:6.1f}", f) == "   1.5");
}

TEST_CASE("Field<double>: ordering instantiates without weakening conversion errors", "[Field]")
{
    Field<double> const a { 1.0 };
    Field<double> const b { 2.0 };
    CHECK(a < b);
    CHECK(b > a);
}

TEST_CASE("std::formatter<Field<std::optional<int>>> renders NULL for empty optional", "[Field]")
{
    Field<std::optional<int>> f;
    CHECK(std::format("{}", f) == "NULL");

    f = std::optional<int> { 7 };
    CHECK(std::format("{}", f) == "7");
}

// ================================================================================================
// SqlDataBinder<Field<T>>::Inspect mirrors Field::InspectValue
// ================================================================================================

TEST_CASE("SqlDataBinder<Field<T>>::Inspect mirrors Field::InspectValue", "[Field]")
{
    Field<int> const f { 42 };
    CHECK(SqlDataBinder<Field<int>>::Inspect(f) == f.InspectValue());

    Field<std::string> g;
    g = "abc"s;
    CHECK(SqlDataBinder<Field<std::string>>::Inspect(g) == g.InspectValue());
}

// ================================================================================================
// ColumnNameOverride — string-literal template parameter is preserved
// ================================================================================================

TEST_CASE("Field column-name override is exposed via ColumnNameOverride", "[Field]")
{
    using NoOverride = Field<int>;
    STATIC_CHECK(NoOverride::ColumnNameOverride.empty());
}
