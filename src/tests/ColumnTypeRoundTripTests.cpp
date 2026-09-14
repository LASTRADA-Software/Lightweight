// SPDX-License-Identifier: Apache-2.0

// One executable invariant for every `SqlColumnTypeDefinition` alternative, on every supported
// DBMS. A column type declared through the migration builder travels a chain of four independent
// pieces of per-DBMS logic before it reaches user code:
//
//     declared SqlColumnTypeDefinition
//       -> DDL type name        (SqlQueryFormatter, per DBMS)
//       -> database catalog
//       -> recovered definition (SqlSchema reader, per DBMS)
//       -> generated C++ type   (CxxModelPrinter)
//       -> a value written and read straight back
//
// Two defects shipped through that chain and survived a green CI matrix for weeks:
//
//   - #586: a PostgreSQL `DOUBLE PRECISION` column came back out of the catalog as
//     `Real{24}`, so every generated record narrowed it from 8 bytes to 4.
//   - #587: `Timestamp{}` on MS SQL Server emitted `TIMESTAMP` — a synonym for `rowversion`, a
//     server-generated binary counter — which read back as `binary(8)` and rejected every write.
//
// Neither was caught, for two separate reasons this file exists to remove.
//
// First, coverage was not exhaustive: `Ddl2CppColumnTypeTests.cpp`'s shared table covers 16 of the
// 19 alternatives, and `Timestamp` — the type #587 broke — was one of the three sitting outside it
// in a hand-written case. `AllAlternativesAreCovered` below makes an uncovered alternative fail
// the suite instead of going unnoticed.
//
// Second, the assertions sat downstream of the defect. Both breaks were in the *recovered
// definition*; the generated C++ type was merely the symptom. So this file asserts the recovered
// definition directly, and #586 would have read `Real{53} came back as Real{24}` rather than
// `expected double, got float`.
//
// Where a backend legitimately stores something other than what was asked for, it is recorded as a
// `Deviation` carrying a mandatory reason. A deviation describes a *backend difference*. It is not
// a place to record a defect as expected behaviour — that is precisely how #586 stayed green, its
// PostgreSQL result pinned to the known-wrong `float`. Anything that looks like a defect belongs
// in an issue, not here.

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace Lightweight;
using Lightweight::Tools::CxxModelPrinter;

namespace
{

// ------------------------------------------------------------------------------ naming the types

/// @param qualified A fully qualified type name.
/// @return @p qualified with every namespace qualifier stripped, e.g. `Varchar` for
///         `Lightweight::SqlColumnTypeDefinitions::Varchar`.
[[nodiscard]] constexpr std::string_view UnqualifiedName(std::string_view qualified) noexcept
{
    auto const lastSeparator = qualified.rfind("::");
    return lastSeparator == std::string_view::npos ? qualified : qualified.substr(lastSeparator + 2);
}

/// @return The unqualified name of every `SqlColumnTypeDefinition` alternative, in variant order.
///
/// Derived from the variant itself rather than hand-listed, so inserting an alternative cannot
/// silently shift the names out of alignment with the indices they describe.
template <std::size_t... I>
[[nodiscard]] constexpr auto MakeAlternativeNames(std::index_sequence<I...> /*indices*/)
{
    return std::array<std::string_view, sizeof...(I)> { UnqualifiedName(
        Reflection::TypeNameOf<std::variant_alternative_t<I, SqlColumnTypeDefinition>>)... };
}

constexpr auto AlternativeNames =
    MakeAlternativeNames(std::make_index_sequence<std::variant_size_v<SqlColumnTypeDefinition>> {});

/// @param type The definition to render.
/// @return A readable rendering including the type's parameters, e.g. `Varchar{30}`, `Decimal{10,2}`.
[[nodiscard]] std::string Describe(SqlColumnTypeDefinition const& type)
{
    return std::visit(
        []<typename T>(T const& alternative) -> std::string {
            auto const name = UnqualifiedName(Reflection::TypeNameOf<T>);
            if constexpr (std::is_same_v<T, SqlColumnTypeDefinitions::Decimal>)
                return std::format("{}{{{},{}}}", name, alternative.precision, alternative.scale);
            else if constexpr (requires { alternative.precision; })
                return std::format("{}{{{}}}", name, alternative.precision);
            else if constexpr (requires { alternative.size; })
                return std::format("{}{{{}}}", name, alternative.size);
            else
                return std::string(name);
        },
        type);
}

/// @param value The value to render.
/// @return @p value unchanged; the overload that lets `CheckExpectation` render either kind.
[[nodiscard]] std::string Describe(std::string const& value)
{
    return value;
}

// ------------------------------------------------------- expectations and per-DBMS deviations

/// What a round-trip result must satisfy, plus a description of what that accepts.
///
/// A predicate rather than a plain value because some results cannot be pinned exactly: the
/// PostgreSQL Unicode driver substitutes its configured `MaxLongVarcharSize` for an unbounded
/// column's unknown length, which is a driver setting rather than a fact about the column.
template <typename T>
struct Requirement
{
    /// Whether an observed value is acceptable.
    std::function<bool(T const&)> matches {};
    /// What @ref matches accepts, rendered into failure messages.
    std::string description {};
};

/// @param expected The only acceptable value.
/// @return A @ref Requirement satisfied solely by @p expected.
template <typename T>
[[nodiscard]] Requirement<T> Exactly(T expected)
{
    return Requirement<T> { .matches = [expected](T const& actual) { return actual == expected; },
                            .description = Describe(expected) };
}

/// @param prefix Required leading text.
/// @return A @ref Requirement satisfied by any string starting with @p prefix.
[[nodiscard]] Requirement<std::string> StartsWith(std::string_view prefix)
{
    return Requirement<std::string> { .matches = [prefix = std::string(prefix)](
                                                     std::string const& actual) { return actual.starts_with(prefix); },
                                      .description = std::format("a type starting with `{}`", prefix) };
}

/// One DBMS' documented departure from an expectation's default.
template <typename T>
struct Deviation
{
    /// The DBMS that differs.
    SqlServerType serverType {};
    /// What that DBMS must produce instead.
    Requirement<T> requirement {};
    /// Why this backend differs. Always a backend fact — never a defect being recorded as expected.
    std::string_view reason {};
};

/// A default requirement together with any per-DBMS departures from it.
template <typename T>
struct Expectation
{
    /// Applies to every DBMS not named in @ref deviations.
    Requirement<T> base {};
    /// Documented per-DBMS departures.
    std::vector<Deviation<T>> deviations {};

    /// @param serverType The DBMS under test.
    /// @return The deviation recorded for @p serverType, or nullptr when the default applies.
    [[nodiscard]] Deviation<T> const* DeviationFor(SqlServerType serverType) const
    {
        auto const found = std::ranges::find_if(deviations, [=](auto const& d) { return d.serverType == serverType; });
        return found != deviations.end() ? &*found : nullptr;
    }

    /// @param serverType The DBMS under test.
    /// @return The requirement that applies on @p serverType.
    [[nodiscard]] Requirement<T> const& For(SqlServerType serverType) const
    {
        auto const* const deviation = DeviationFor(serverType);
        return deviation != nullptr ? deviation->requirement : base;
    }
};

/// Checks one link of the round trip, reporting the column, the link, and the reason a deviation
/// applies, so a failure identifies the alternative and the database without reading the table.
///
/// @param columnName Column under test.
/// @param link Which link of the chain is being checked, for the failure message.
/// @param expectation What the link must produce.
/// @param actual What it produced.
/// @param serverType The DBMS under test.
template <typename T>
void CheckLink(std::string_view columnName,
               std::string_view link,
               Expectation<T> const& expectation,
               T const& actual,
               SqlServerType serverType)
{
    auto const& requirement = expectation.For(serverType);
    auto const* const deviation = expectation.DeviationFor(serverType);
    auto const because = deviation != nullptr ? std::format(" (deviation: {})", deviation->reason) : std::string {};

    INFO(std::format(
        "column `{}` -> {}: expected {}{}, got {}", columnName, link, requirement.description, because, Describe(actual)));
    CHECK(requirement.matches(actual));
}

// --------------------------------------------------------------------------- the value round trip

/// A value written into the probe column and read straight back out of it.
///
/// The catalog half of the chain is only half the contract: a column that reads back as
/// `SqlDateTime` must also *accept* one. That is the half a SQL Server `rowversion` fails, which is
/// what made #587 more than a curious catalog reading.
struct ColumnSample
{
    /// Binds the sample as an input parameter. Empty when the alternative carries no sample.
    std::function<void(SqlStatement&, SQLSMALLINT)> bind {};
    /// Reads the value back and compares it to what was written.
    std::function<void(SqlResultCursor&, SQLUSMALLINT)> verify {};
};

/// @param value Sample written into the column and expected back unchanged.
/// @return A @ref ColumnSample round-tripping @p value as a `T`.
template <typename T>
[[nodiscard]] ColumnSample SampleOf(T value)
{
    return ColumnSample {
        .bind = [value](SqlStatement& stmt, SQLSMALLINT parameterIndex) { stmt.BindInputParameter(parameterIndex, value); },
        .verify = [value](SqlResultCursor& cursor,
                          SQLUSMALLINT columnIndex) { CHECK(cursor.GetColumn<T>(columnIndex) == value); },
    };
}

// ------------------------------------------------------------------------------- the descriptor

/// A link deliberately left unasserted on one DBMS, because a known and *open* defect means the
/// correct answer is not available there yet.
///
/// Distinct from a @ref Deviation on purpose: a deviation says "this backend legitimately differs"
/// and asserts that difference; a pending fix asserts nothing at all. Recording a defect as an
/// expected value is what kept #586 green for weeks, so this type refuses to do that — it reports
/// the open issue on every run instead, and the assertions switch on again simply by deleting it.
struct PendingFix
{
    /// The DBMS whose result is not yet correct.
    SqlServerType serverType {};
    /// The issue tracking the defect, and where its fix currently lives.
    std::string_view issue {};
    /// What the backend does today, and what it should do instead.
    std::string_view reason {};
};

/// One alternative of `SqlColumnTypeDefinition` and everything the round trip must produce for it.
struct TypeRoundTripCase
{
    /// Column name used in this alternative's probe table.
    std::string_view columnName {};
    /// Type handed to the migration query builder.
    SqlColumnTypeDefinition declaredType {};
    /// Definition the schema reader must recover from the catalog.
    Expectation<SqlColumnTypeDefinition> recoveredType {};
    /// C++ type `ddl2cpp` must generate from that recovered definition.
    Expectation<std::string> cxxType {};
    /// Value written into the column and read back. Required for every alternative.
    ColumnSample sample {};
    /// DBMS results held back by an open defect. Empty for everything that works.
    std::vector<PendingFix> pendingFixes {};

    /// @param serverType The DBMS under test.
    /// @return The pending fix recorded for @p serverType, or nullptr when none applies.
    [[nodiscard]] PendingFix const* PendingFixFor(SqlServerType serverType) const
    {
        auto const found = std::ranges::find_if(pendingFixes, [=](auto const& p) { return p.serverType == serverType; });
        return found != pendingFixes.end() ? &*found : nullptr;
    }
};

// --------------------------------------------------------------- shared reasons for deviations

constexpr std::string_view SqliteHasOneTextType =
    "SQLite has a single dynamically typed TEXT storage class: no CHAR padding, no Unicode variant";

constexpr std::string_view PostgresReportsTextAsUnicode =
    "the PostgreSQL Unicode driver reports every character column as its wide ODBC type";

constexpr std::string_view PostgresByteaHasNoLength = "PostgreSQL BYTEA carries no declared length";

constexpr std::string_view NoBackendHonoursFixedWidthBinary =
    "no backend honours a fixed-width Binary{n}: SQLite emits BLOB, SQL Server VARBINARY(n), PostgreSQL BYTEA";

// Which C++ string type SQLite's single TEXT storage class turns into is decided by the ODBC driver
// build rather than by SQLite: the unixODBC drivers on the Linux and macOS legs report text as the
// narrow ODBC types, the Windows build reports the wide counterparts. The stored bytes are
// identical either way — only the reported type differs.
#if defined(_WIN32)
constexpr bool SqliteDriverReportsTextAsUnicode = true;
#else
constexpr bool SqliteDriverReportsTextAsUnicode = false;
#endif

/// @param narrow C++ type generated where the driver reports a character column as narrow text.
/// @param wide C++ type generated where it reports wide text.
/// @return Whichever of the two this platform's SQLite ODBC driver produces.
[[nodiscard]] constexpr std::string_view SqliteText(std::string_view narrow, std::string_view wide)
{
    return SqliteDriverReportsTextAsUnicode ? wide : narrow;
}

/// @param size Declared width the column must come back with.
/// @return A requirement accepting either the narrow or the wide text type of that width.
///
/// Used where the narrow/wide distinction is the ODBC driver build's choice rather than the
/// database's, so the expectation must not hard-code one platform's answer.
[[nodiscard]] Requirement<SqlColumnTypeDefinition> TextOfWidth(std::size_t size)
{
    return Requirement<SqlColumnTypeDefinition> {
        .matches =
            [size](SqlColumnTypeDefinition const& actual) {
                if (auto const* const narrow = std::get_if<SqlColumnTypeDefinitions::Varchar>(&actual))
                    return narrow->size == size;
                if (auto const* const wide = std::get_if<SqlColumnTypeDefinitions::NVarchar>(&actual))
                    return wide->size == size;
                return false;
            },
        .description = std::format("Varchar{{{}}} or NVarchar{{{}}} (driver build decides)", size, size)
    };
}

/// @return A requirement accepting an unbounded text column of any width the driver reports.
///
/// The PostgreSQL Unicode driver substitutes its configured `MaxLongVarcharSize` for an unbounded
/// column's unknown length. That number is a driver setting, not a fact about the column, so only
/// the shape is assertable.
[[nodiscard]] Requirement<SqlColumnTypeDefinition> UnboundedText()
{
    return Requirement<SqlColumnTypeDefinition> {
        .matches =
            [](SqlColumnTypeDefinition const& actual) {
                return std::holds_alternative<SqlColumnTypeDefinitions::Varchar>(actual)
                       || std::holds_alternative<SqlColumnTypeDefinitions::NVarchar>(actual);
            },
        .description = "a Varchar or NVarchar of the driver's reported maximum width"
    };
}

// ------------------------------------------------------------------------------ the sample values

auto const SampleDate = SqlDate { std::chrono::year { 2026 }, std::chrono::August, std::chrono::day { 19 } };
auto const SampleTime = SqlTime { std::chrono::hours { 17 }, std::chrono::minutes { 30 }, std::chrono::seconds { 45 } };
auto const SampleDateTime =
    SqlDateTime { std::chrono::year { 2026 }, std::chrono::August,         std::chrono::day { 19 },
                  std::chrono::hours { 17 },  std::chrono::minutes { 30 }, std::chrono::seconds { 45 } };

// 2.5 is a dyadic rational, so it is exact in both 4- and 8-byte floating point and cannot be
// perturbed by a driver that routes the value through a text intermediate representation.
constexpr auto SampleDouble = 2.5;

// Exactly the declared width of the fixed-width character columns, so CHAR(n) padding — which the
// backends apply inconsistently — never enters the value comparison.
constexpr auto SampleFixedText = std::string_view { "abcdefgh" };

/// The round-trip expectation for every `SqlColumnTypeDefinition` alternative.
///
/// Every alternative of the variant must appear here; `AllAlternativesAreCovered` enforces it.
std::vector<TypeRoundTripCase> const& RoundTripCases()
{
    using namespace SqlColumnTypeDefinitions;
    static std::vector<TypeRoundTripCase> const cases {
        // ------------------------------------------------------------------------ integral types
        { .columnName = "bigintCol",
          .declaredType = Bigint {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Bigint {} }) },
          .cxxType = { .base = Exactly(std::string { "int64_t" }) },
          .sample = SampleOf(std::int64_t { -1234567890123 }) },

        { .columnName = "integerCol",
          .declaredType = Integer {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Integer {} }) },
          .cxxType = { .base = Exactly(std::string { "int32_t" }) },
          .sample = SampleOf(std::int32_t { -1234567 }) },

        { .columnName = "smallintCol",
          .declaredType = Smallint {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Smallint {} }) },
          .cxxType = { .base = Exactly(std::string { "int16_t" }) },
          .sample = SampleOf(std::int16_t { -12345 }) },

        { .columnName = "tinyintCol",
          .declaredType = Tinyint {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Tinyint {} }),
                             .deviations = { { .serverType = SqlServerType::POSTGRESQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { Smallint {} }),
                                               .reason = "PostgreSQL has no 1-byte integer; Tinyint is emitted as "
                                                         "SMALLINT" } } },
          .cxxType = { .base = Exactly(std::string { "uint8_t" }),
                       .deviations = { { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = Exactly(std::string { "int16_t" }),
                                         .reason = "PostgreSQL has no 1-byte integer" } } },
          .sample = SampleOf(std::uint8_t { 42 }) },

        // ---------------------------------------------------------- fixed- and floating-point
        { .columnName = "decimalCol",
          .declaredType = Decimal { .precision = 10, .scale = 2 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Decimal { .precision = 10, .scale = 2 } }) },
          .cxxType = { .base = Exactly(std::string { "Light::SqlNumeric<10, 2>" }) },
          .sample = SampleOf(SqlNumeric<10, 2> { 1234.56 }) },

        { .columnName = "realCol",
          .declaredType = Real { .precision = 53 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Real { .precision = 53 } }) },
          .cxxType = { .base = Exactly(std::string { "double" }) },
          .sample = SampleOf(SampleDouble) },

        // -------------------------------------------------------------------------------- boolean
        { .columnName = "boolCol",
          .declaredType = Bool {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Bool {} }) },
          .cxxType = { .base = Exactly(std::string { "bool" }) },
          .sample = SampleOf(true) },

        // -------------------------------------------------------------------------- date and time
        { .columnName = "dateCol",
          .declaredType = Date {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Date {} }) },
          .cxxType = { .base = Exactly(std::string { "Light::SqlDate" }) },
          .sample = SampleOf(SampleDate) },

        { .columnName = "timeCol",
          .declaredType = Time {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Time {} }) },
          .cxxType = { .base = Exactly(std::string { "Light::SqlTime" }) },
          .sample = SampleOf(SampleTime) },

        { .columnName = "datetimeCol",
          .declaredType = DateTime {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { DateTime {} }) },
          .cxxType = { .base = Exactly(std::string { "Light::SqlDateTime" }) },
          .sample = SampleOf(SampleDateTime) },

        // `Timestamp{}` means a point in time on every backend. On MS SQL Server the literal
        // `TIMESTAMP` keyword is a deprecated synonym for `rowversion` — a server-generated binary
        // counter that also rejects every write — which is what #587 is about.
        { .columnName = "timestampCol",
          .declaredType = Timestamp {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { DateTime {} }) },
          .cxxType = { .base = Exactly(std::string { "Light::SqlDateTime" }) },
          .sample = SampleOf(SampleDateTime),
          .pendingFixes = { { .serverType = SqlServerType::MICROSOFT_SQL,
                              .issue = "#587 (fix pending in PR #598)",
                              .reason = "the SQL Server formatter still emits TIMESTAMP, so the column reads back as "
                                        "VarBinary{8} and rejects writes; it must read back as DateTime" } } },

        // ------------------------------------------------------------------------------ the GUID
        { .columnName = "guidCol",
          .declaredType = Guid {},
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Guid {} }),
                             .deviations = { { .serverType = SqlServerType::SQLITE,
                                               .requirement = TextOfWidth(0),
                                               .reason = "SQLite has no GUID type; the declared GUID is an "
                                                         "unrecognised type name reported back as unsized text" } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlGuid" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string { SqliteText(
                                             "Light::SqlDynamicAnsiString<0>", "Light::SqlDynamicUtf16String<0>") }),
                                         .reason = "SQLite has no GUID type" } } },
          .sample = SampleOf(SqlGuid::UnsafeParse("1E772AED-3E73-4C72-8684-5DFFAA17330E")) },

        // -------------------------------------------------------------------------- non-Unicode text
        { .columnName = "charCol",
          .declaredType = Char { .size = 8 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Char { .size = 8 } }),
                             .deviations = { { .serverType = SqlServerType::SQLITE,
                                               .requirement = TextOfWidth(8),
                                               .reason = SqliteHasOneTextType },
                                             { .serverType = SqlServerType::POSTGRESQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { NChar { .size = 8 } }),
                                               .reason = PostgresReportsTextAsUnicode } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlTrimmedFixedString<8>" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string {
                                             SqliteText("Light::SqlAnsiString<8>", "Light::SqlDynamicUtf16String<8>") }),
                                         .reason = SqliteHasOneTextType },
                                       { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = Exactly(std::string { "Light::SqlTrimmedFixedString<8, wchar_t>" }),
                                         .reason = PostgresReportsTextAsUnicode } } },
          .sample = SampleOf(std::string { SampleFixedText }) },

        { .columnName = "varcharCol",
          .declaredType = Varchar { .size = 30 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Varchar { .size = 30 } }),
                             .deviations = { { .serverType = SqlServerType::SQLITE,
                                               .requirement = TextOfWidth(30),
                                               .reason = SqliteHasOneTextType },
                                             { .serverType = SqlServerType::POSTGRESQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { NVarchar { .size = 30 } }),
                                               .reason = PostgresReportsTextAsUnicode } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlAnsiString<30>" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string {
                                             SqliteText("Light::SqlAnsiString<30>", "Light::SqlDynamicUtf16String<30>") }),
                                         .reason = SqliteHasOneTextType },
                                       { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = Exactly(std::string { "Light::SqlDynamicUtf16String<30>" }),
                                         .reason = PostgresReportsTextAsUnicode } } },
          .sample = SampleOf(std::string { "hello, column" }) },

        // ------------------------------------------------------------------------------ Unicode text
        { .columnName = "ncharCol",
          .declaredType = NChar { .size = 8 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { NChar { .size = 8 } }),
                             .deviations = { { .serverType = SqlServerType::SQLITE,
                                               .requirement = TextOfWidth(8),
                                               .reason = SqliteHasOneTextType } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlTrimmedFixedString<8, wchar_t>" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string {
                                             SqliteText("Light::SqlAnsiString<8>", "Light::SqlDynamicUtf16String<8>") }),
                                         .reason = SqliteHasOneTextType } } },
          .sample = SampleOf(std::string { SampleFixedText }) },

        { .columnName = "nvarcharCol",
          .declaredType = NVarchar { .size = 30 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { NVarchar { .size = 30 } }),
                             .deviations = { { .serverType = SqlServerType::SQLITE,
                                               .requirement = TextOfWidth(30),
                                               .reason = SqliteHasOneTextType } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlDynamicUtf16String<30>" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string {
                                             SqliteText("Light::SqlAnsiString<30>", "Light::SqlDynamicUtf16String<30>") }),
                                         .reason = SqliteHasOneTextType } } },
          .sample = SampleOf(std::string { "hello, column" }) },

        // An unbounded TEXT column: every backend reports a different notion of "no limit".
        { .columnName = "textCol",
          .declaredType = Text {},
          .recoveredType = { .base = UnboundedText() },
          .cxxType = { .base = Exactly(std::string { "Light::SqlMaxDynamicAnsiString" }),
                       .deviations = { { .serverType = SqlServerType::SQLITE,
                                         .requirement = Exactly(std::string { SqliteText(
                                             "Light::SqlDynamicAnsiString<0>", "Light::SqlDynamicUtf16String<0>") }),
                                         .reason = "SQLite reports its TEXT storage class without a length" },
                                       { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = StartsWith("Light::SqlDynamicUtf16String<"),
                                         .reason = "the PostgreSQL driver substitutes its configured "
                                                   "MaxLongVarcharSize for the unknown length" } } },
          .sample = SampleOf(std::string { "an unbounded value" }) },

        // ----------------------------------------------------------------------------------- binary
        { .columnName = "binaryCol",
          .declaredType = Binary { .size = 16 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { Binary { .size = 0 } }),
                             .deviations = { { .serverType = SqlServerType::MICROSOFT_SQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { VarBinary { .size = 16 } }),
                                               .reason = NoBackendHonoursFixedWidthBinary },
                                             { .serverType = SqlServerType::POSTGRESQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { VarBinary { .size = 0 } }),
                                               .reason = PostgresByteaHasNoLength } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlBinary" }),
                       .deviations = { { .serverType = SqlServerType::MICROSOFT_SQL,
                                         .requirement = Exactly(std::string { "Light::SqlDynamicBinary<16>" }),
                                         .reason = NoBackendHonoursFixedWidthBinary },
                                       { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = Exactly(std::string { "Light::SqlDynamicBinary<0>" }),
                                         .reason = PostgresByteaHasNoLength } } },
          .sample = SampleOf(SqlBinary { 0x01, 0x02, 0x03 }) },

        { .columnName = "varbinaryCol",
          .declaredType = VarBinary { .size = 16 },
          .recoveredType = { .base = Exactly(SqlColumnTypeDefinition { VarBinary { .size = 16 } }),
                             .deviations = { { .serverType = SqlServerType::POSTGRESQL,
                                               .requirement = Exactly(SqlColumnTypeDefinition { VarBinary { .size = 0 } }),
                                               .reason = PostgresByteaHasNoLength } } },
          .cxxType = { .base = Exactly(std::string { "Light::SqlDynamicBinary<16>" }),
                       .deviations = { { .serverType = SqlServerType::POSTGRESQL,
                                         .requirement = Exactly(std::string { "Light::SqlDynamicBinary<0>" }),
                                         .reason = PostgresByteaHasNoLength } } },
          .sample = SampleOf(SqlBinary { 0x01, 0x02, 0x03 }) },
    };
    return cases;
}

/// Reads @p tableName back out of the live database catalog.
SqlSchema::Table ReadTable(SqlStatement& stmt, std::string_view tableName)
{
    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const table = std::ranges::find_if(tables, [&](SqlSchema::Table const& t) { return t.name == tableName; });
    REQUIRE(table != tables.end());
    return *table;
}

/// @return The column named @p columnName of @p table.
SqlSchema::Column const& ColumnOf(SqlSchema::Table const& table, std::string_view columnName)
{
    auto const column =
        std::ranges::find_if(table.columns, [&](SqlSchema::Column const& c) { return c.name == columnName; });
    REQUIRE(column != table.columns.end());
    return *column;
}

/// @return The C++ type `ddl2cpp` generates for @p column with its default settings.
std::string GeneratedCxxType(SqlSchema::Column const& column, std::string const& tableName)
{
    return CxxModelPrinter::MakeType(column, tableName, /*forceUnicodeTextColumn=*/false, {}, SqlOptimalMaxColumnSize);
}

/// Writes @p sample into @p columnName and reads it straight back.
///
/// Uses a statement of its own rather than the caller's: psqlODBC carries parameter state across a
/// re-`Prepare` of the same handle, so reusing one statement for nineteen differently typed inserts
/// makes a later bind fail in a way that has nothing to do with the type under test.
void CheckValueRoundTrip(std::string_view tableName, std::string_view columnName, ColumnSample const& sample)
{
    INFO(std::format("column `{}` -> value round trip", columnName));
    auto stmt = SqlStatement {};
    stmt.Prepare(std::format(R"(INSERT INTO "{}" ("{}") VALUES (?))", tableName, columnName));
    sample.bind(stmt, 1);
    (void) stmt.Execute();

    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "{}" FROM "{}")", columnName, tableName));
    REQUIRE(cursor.FetchRow());
    sample.verify(cursor, 1);
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "column types: every alternative survives the declare -> catalog -> C++ -> value round trip",
                 "[ColumnTypeRoundTrip][SqlSchema]")
{
    auto const serverType = SqlStatement {}.Connection().ServerType();

    for (auto const& testCase: RoundTripCases())
    {
        auto stmt = SqlStatement {};
        auto const tableName = std::format("RoundTrip_{}", testCase.columnName);
        // One table per alternative: a failure then names the type under test, and a backend that
        // rejects one declaration cannot take the other eighteen down with it.
        stmt.MigrateDirect([&](SqlMigrationQueryBuilder& migration) {
            // Declared NOT NULL so each expectation reads as the bare C++ type rather than an
            // `std::optional<T>`; nullability is a separate concern, covered elsewhere.
            migration.CreateTable(tableName).RequiredColumn(std::string(testCase.columnName), testCase.declaredType);
        });

        if (auto const* const pending = testCase.PendingFixFor(serverType))
        {
            WARN(std::format("column `{}` is not asserted on this backend — {}: {}",
                             testCase.columnName,
                             pending->issue,
                             pending->reason));
            continue;
        }

        auto const table = ReadTable(stmt, tableName);
        auto const& column = ColumnOf(table, testCase.columnName);

        CheckLink(testCase.columnName, "recovered definition", testCase.recoveredType, column.type, serverType);
        CheckLink(
            testCase.columnName, "generated C++ type", testCase.cxxType, GeneratedCxxType(column, tableName), serverType);

        // Every alternative carries a sample: the catalog half of the chain is only half the
        // contract, and a column that reads back as `SqlDateTime` must also accept one.
        REQUIRE(testCase.sample.bind);
        CheckValueRoundTrip(tableName, testCase.columnName, testCase.sample);
    }
}

TEST_CASE("column types: every SqlColumnTypeDefinition alternative is covered", "[ColumnTypeRoundTrip]")
{
    // The gap that let #587 through: `Timestamp` had no entry in the shared table, so nothing
    // exercised it across backends. Adding an alternative to the variant now fails here until it
    // gets a row above.
    auto covered = std::vector<bool>(std::variant_size_v<SqlColumnTypeDefinition>, false);
    for (auto const& testCase: RoundTripCases())
        covered[testCase.declaredType.index()] = true;

    for (auto const index: std::views::iota(std::size_t { 0 }, covered.size()))
    {
        INFO(std::format("SqlColumnTypeDefinition alternative `{}` has no entry in RoundTripCases()",
                         AlternativeNames[index]));
        CHECK(covered[index]);
    }
}
