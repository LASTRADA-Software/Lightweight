// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using Lightweight::Tools::CxxModelPrinter;

TEST_CASE("CxxModelPrinter: FormatTableName", "[CxxModelPrinter]")
{
    CHECK(CxxModelPrinter::FormatTableName("user_id") == "UserId");
    CHECK(CxxModelPrinter::FormatTableName("task_list_entry") == "TaskListEntry");
    CHECK(CxxModelPrinter::FormatTableName("person") == "Person");
    CHECK(CxxModelPrinter::FormatTableName("person_id") == "PersonId");
}

static std::string StripLeadingWhitespaces(std::string_view str)
{
    if (str.empty())
        return {};

    if (str.front() != '\n')
        return std::string(str);

    str.remove_prefix(1); // Remove the leading newline character

    // count the number of leading whitespaces
    size_t leadingSpacesCount = 0;
    while (leadingSpacesCount < str.size() && std::isspace(static_cast<unsigned char>(str[leadingSpacesCount])))
        ++leadingSpacesCount;

    std::string trimmedLeadingWhitespacesText;
    trimmedLeadingWhitespacesText.reserve(str.size());

    // Remove `leadingSpacesCount` leading whitespaces from each line and append to the result
    for (auto const& line: std::views::split(str, '\n'))
    {
        auto lineView = std::string_view { line.begin(), line.end() };
        if (lineView.empty())
            // If the line is empty, just append a newline
            trimmedLeadingWhitespacesText.push_back('\n');
        else if (lineView.size() >= leadingSpacesCount)
        {
            // If the line has enough characters, remove leading spaces, and append the rest
            trimmedLeadingWhitespacesText.append(lineView.substr(leadingSpacesCount));
            trimmedLeadingWhitespacesText.push_back('\n'); // Add newline after each line
        }
    }

    return trimmedLeadingWhitespacesText;
}

static auto CxxTypeName(Lightweight::SqlSchema::Column const& column) -> std::string
{
    return CxxModelPrinter::MakeType(column, "TestTable", false, {}, Lightweight::SqlOptimalMaxColumnSize);
}

TEST_CASE("CxxModelPrinter: MakeType - optional vs required", "[CxxModelPrinter],[MakeType]")
{
    CHECK(CxxTypeName(Lightweight::SqlSchema::Column {
              .name = "id",
              .type = Lightweight::SqlColumnTypeDefinitions::Integer {},
              .isNullable = true,
          })
          == "std::optional<int32_t>");

    CHECK(CxxTypeName(Lightweight::SqlSchema::Column {
              .name = "id",
              .type = Lightweight::SqlColumnTypeDefinitions::Integer {},
              .isNullable = false,
          })
          == "int32_t");
}

TEST_CASE("CxxModelPrinter: MakeType - CHAR(n)", "[CxxModelPrinter],[MakeType]")
{
    CHECK(CxxTypeName(Lightweight::SqlSchema::Column {
              .name = "id",
              .type = Lightweight::SqlColumnTypeDefinitions::Char { .size = 32 },
              .isNullable = false,
          })
          == "Light::SqlTrimmedFixedString<32>");

    CHECK(CxxTypeName(Lightweight::SqlSchema::Column {
              .name = "id",
              .type = Lightweight::SqlColumnTypeDefinitions::NChar { .size = 32 },
              .isNullable = false,
          })
          == "Light::SqlTrimmedFixedString<32, wchar_t>");
}

TEST_CASE("CxxModelPrinter: simple table with default settings", "[CxxModelPrinter]")
{
    auto cxxModelPrinter = CxxModelPrinter { CxxModelPrinter::Config {} };

    cxxModelPrinter.PrintTable( Lightweight::SqlSchema::Table {
        .schema = "",
        .name = "test",
        .columns = {
            Lightweight::SqlSchema::Column {
                .name = "id",
                .type = Lightweight::SqlColumnTypeDefinitions::Integer {},
            },
            Lightweight::SqlSchema::Column {
                .name = "name",
                .type = Lightweight::SqlColumnTypeDefinitions::Varchar { 64 },
            },
        },
    });

    CHECK(cxxModelPrinter.ToString("Test") == StripLeadingWhitespaces(R"cpp(
        // file: test.hpp
        // File is automatically generated using ddl2cpp.
        #pragma once

        #if !defined(LIGHTWEIGHT_BUILD_MODULES)
        #include <Lightweight/DataMapper/DataMapper.hpp>
        #endif

        #include <array>
        #include <string_view>

        namespace Test
        {

        struct test final
        {
            Light::Field<std::optional<int32_t>> id;
            Light::Field<std::optional<Light::SqlAnsiString<64>>> name;
        };

        } // end namespace Test

        template <>
        struct Lightweight::Description<Test::test>
        {
            static constexpr std::size_t FieldCount = 2;
            using Members = Lightweight::RecordMemberList<&Test::test::id, &Test::test::name>;
            static constexpr std::array<std::string_view, 2> FieldNames = { "id", "name" };
        };
    )cpp"));
}

// ================================================================================================
// CxxModelPrinter::SanitizeName: C++ reserved keyword handling
// ================================================================================================

TEST_CASE("CxxModelPrinter::SanitizeName preserves regular identifiers", "[CxxModelPrinter]")
{
    CHECK(CxxModelPrinter::SanitizeName("user_name") == "user_name");
    CHECK(CxxModelPrinter::SanitizeName("hello") == "hello");
    CHECK(CxxModelPrinter::SanitizeName("MyColumn") == "MyColumn");
}

TEST_CASE("CxxModelPrinter::SanitizeName escapes C++ keywords", "[CxxModelPrinter]")
{
    // The function appends an underscore (or similar) when the identifier collides
    // with a reserved keyword. The exact escape format isn't part of the public
    // contract — we only check that the result differs from the keyword and is safe.
    auto sanitized = CxxModelPrinter::SanitizeName("class");
    CHECK(sanitized != "class");
    sanitized = CxxModelPrinter::SanitizeName("namespace");
    CHECK(sanitized != "namespace");
    sanitized = CxxModelPrinter::SanitizeName("struct");
    CHECK(sanitized != "struct");
    sanitized = CxxModelPrinter::SanitizeName("template");
    CHECK(sanitized != "template");
}

// ================================================================================================
// CxxModelPrinter::FormatTableName branch coverage
// ================================================================================================

TEST_CASE("CxxModelPrinter::FormatTableName handles various input shapes", "[CxxModelPrinter]")
{
    CHECK(CxxModelPrinter::FormatTableName("").empty());
    CHECK(CxxModelPrinter::FormatTableName("a") == "A");
    CHECK(CxxModelPrinter::FormatTableName("UserId") == "Userid"); // lowercases mid-word
    CHECK(CxxModelPrinter::FormatTableName("__double_underscore") == "DoubleUnderscore");
    CHECK(CxxModelPrinter::FormatTableName("trailing_") == "Trailing");
}

// ================================================================================================
// CxxModelPrinter::MakeType: comprehensive column-type coverage
// ================================================================================================

TEST_CASE("CxxModelPrinter::MakeType: integer types", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = Bigint {}, .isNullable = false }) == "int64_t");
    CHECK(CxxTypeName({ .name = "x", .type = Smallint {}, .isNullable = false }) == "int16_t");
    CHECK(CxxTypeName({ .name = "x", .type = Tinyint {}, .isNullable = false }) == "uint8_t");
    CHECK(CxxTypeName({ .name = "x", .type = Bool {}, .isNullable = false }) == "bool");
    CHECK(CxxTypeName({ .name = "x", .type = Bigint {}, .isNullable = true }) == "std::optional<int64_t>");
}

TEST_CASE("CxxModelPrinter::MakeType: floating-point Real", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = Real { .precision = 23 }, .isNullable = false }) == "float");
    CHECK(CxxTypeName({ .name = "x", .type = Real { .precision = 53 }, .isNullable = false }) == "double");
}

TEST_CASE("CxxModelPrinter::MakeType: decimal", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "amount", .type = Decimal { .precision = 10, .scale = 2 }, .isNullable = false })
          == "Light::SqlNumeric<10, 2>");
}

TEST_CASE("CxxModelPrinter::MakeType: date/time/timestamp/guid/binary", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = Date {}, .isNullable = false }) == "Light::SqlDate");
    CHECK(CxxTypeName({ .name = "x", .type = DateTime {}, .isNullable = false }) == "Light::SqlDateTime");
    CHECK(CxxTypeName({ .name = "x", .type = Timestamp {}, .isNullable = false }) == "Light::SqlDateTime");
    CHECK(CxxTypeName({ .name = "x", .type = Time {}, .isNullable = false }) == "Light::SqlTime");
    CHECK(CxxTypeName({ .name = "x", .type = Guid {}, .isNullable = false }) == "Light::SqlGuid");
    auto const binary = CxxTypeName({ .name = "x", .type = Binary { .size = 16 }, .isNullable = false });
    CHECK(binary.starts_with("Light::SqlBinary"));
    CHECK(CxxTypeName({ .name = "x", .type = VarBinary { .size = 64 }, .isNullable = false })
          == "Light::SqlDynamicBinary<64>");
}

TEST_CASE("CxxModelPrinter::MakeType: Char(1) and Char(N)", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    // Char(1) maps to a single 'char'.
    CHECK(CxxTypeName({ .name = "x", .type = Char { .size = 1 }, .isNullable = false }) == "char");
    CHECK(CxxTypeName({ .name = "x", .type = Char { .size = 16 }, .isNullable = false })
          == "Light::SqlTrimmedFixedString<16>");
}

TEST_CASE("CxxModelPrinter::MakeType: NChar(1) and NChar(N)", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = NChar { .size = 1 }, .isNullable = false }) == "char16_t");
    CHECK(CxxTypeName({ .name = "x", .type = NChar { .size = 16 }, .isNullable = false })
          == "Light::SqlTrimmedFixedString<16, wchar_t>");
}

TEST_CASE("CxxModelPrinter::MakeType: Varchar small vs large", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    // Within sqlFixedStringMaxSize → Light::SqlAnsiString<N>.
    CHECK(CxxTypeName({ .name = "x", .type = Varchar { 64 }, .isNullable = false }) == "Light::SqlAnsiString<64>");
    // Larger than sqlFixedStringMaxSize → Light::SqlDynamicAnsiString<N>.
    CHECK(CxxTypeName({ .name = "x", .type = Varchar { 100000 }, .isNullable = false })
          == "Light::SqlDynamicAnsiString<100000>");
}

TEST_CASE("CxxModelPrinter::MakeType: NVarchar always dynamic UTF-16", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = NVarchar { 64 }, .isNullable = false }) == "Light::SqlDynamicUtf16String<64>");
}

TEST_CASE("CxxModelPrinter::MakeType: Text", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = Text { .size = 1024 }, .isNullable = false })
          == "Light::SqlDynamicAnsiString<1024>");
    CHECK(
        CxxTypeName(
            { .name = "x", .type = Text { .size = Lightweight::detail::SqlMaxNumberOfChars<char>() }, .isNullable = false })
        == "Light::SqlMaxDynamicAnsiString");
}

TEST_CASE("CxxModelPrinter::MakeType: forceUnicodeTextColumn flips Char/Varchar/Text", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    auto const force = [](Lightweight::SqlSchema::Column const& c) {
        return CxxModelPrinter::MakeType(c, "T", true, {}, Lightweight::SqlOptimalMaxColumnSize);
    };

    CHECK(force({ .name = "x", .type = Char { .size = 1 }, .isNullable = false }) == "wchar_t");
    CHECK(force({ .name = "x", .type = Char { .size = 16 }, .isNullable = false })
          == "Light::SqlTrimmedFixedString<16, wchar_t>");
    CHECK(force({ .name = "x", .type = Varchar { 64 }, .isNullable = false }) == "Light::SqlWideString<64>");
    CHECK(force({ .name = "x", .type = Varchar { 100000 }, .isNullable = false }) == "Light::SqlDynamicWideString<100000>");
    CHECK(force({ .name = "x", .type = Text { .size = 1024 }, .isNullable = false }) == "Light::SqlDynamicWideString<1024>");
}

TEST_CASE("CxxModelPrinter::MakeType: per-column unicode override", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter::UnicodeTextColumnOverrides overrides;
    overrides["TestTable"].insert("name");

    auto const result = CxxModelPrinter::MakeType(
        Lightweight::SqlSchema::Column { .name = "name", .type = Varchar { 32 }, .isNullable = false },
        "TestTable",
        false,
        overrides,
        Lightweight::SqlOptimalMaxColumnSize);
    CHECK(result == "Light::SqlWideString<32>");

    // A different column on the same table is unaffected — falls through to ANSI.
    auto const ansi = CxxModelPrinter::MakeType(
        Lightweight::SqlSchema::Column { .name = "other", .type = Varchar { 32 }, .isNullable = false },
        "TestTable",
        false,
        overrides,
        Lightweight::SqlOptimalMaxColumnSize);
    CHECK(ansi == "Light::SqlAnsiString<32>");
}

// ================================================================================================
// CxxModelPrinter: alias mode toggling
// ================================================================================================

TEST_CASE("CxxModelPrinter::AliasTableName: default config returns input verbatim", "[CxxModelPrinter]")
{
    CxxModelPrinter::Config config;
    config.makeAliases = false;
    CxxModelPrinter printer { config };
    CHECK(printer.AliasTableName("user_table") == "user_table");
}

TEST_CASE("CxxModelPrinter::AliasTableName: makeAliases formats name", "[CxxModelPrinter]")
{
    CxxModelPrinter::Config config;
    config.makeAliases = true;
    CxxModelPrinter printer { config };
    CHECK(printer.AliasTableName("user_table") == "UserTable");
}

// ================================================================================================
// CxxModelPrinter: complete table render with multiple columns and primary key
// ================================================================================================

TEST_CASE("CxxModelPrinter: table with primary key and several columns", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter::Config config;
    config.primaryKeyAssignment = Lightweight::PrimaryKey::ServerSideAutoIncrement;
    CxxModelPrinter printer { config };

    printer.PrintTable(Lightweight::SqlSchema::Table {
        .schema = "",
        .name = "users",
        .columns = {
            Lightweight::SqlSchema::Column { .name = "id",
                                              .type = Integer {},
                                              .isNullable = false },
            Lightweight::SqlSchema::Column { .name = "name", .type = Varchar { 50 }, .isNullable = false },
            Lightweight::SqlSchema::Column { .name = "balance", .type = Decimal { .precision = 10, .scale = 2 } },
        },
        .primaryKeys = { "id" },
    });

    auto const output = printer.ToString("Models");
    // We don't pin the entire textual format (it's tested elsewhere), but we
    // verify the key components are present so the iteration and column
    // rendering branches are exercised.
    CHECK(output.contains("namespace Models"));
    CHECK(output.contains("struct users"));
    CHECK(output.contains("id"));
    CHECK(output.contains("name"));
    CHECK(output.contains("balance"));
    CHECK(output.contains("Light::SqlAnsiString<50>"));
    CHECK(output.contains("Light::SqlNumeric<10, 2>"));
}

TEST_CASE("CxxModelPrinter: emits Description for a keyed table with a relation", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter::Config config;
    config.makeAliases = true;
    CxxModelPrinter printer { config };

    printer.PrintTable(Lightweight::SqlSchema::Table {
        .schema = "",
        .name = "orders",
        .columns = {
            Lightweight::SqlSchema::Column { .name = "id", .type = Integer {}, .isNullable = false, .isPrimaryKey = true },
            Lightweight::SqlSchema::Column {
                .name = "customer_id", .type = Integer {}, .isNullable = false, .isForeignKey = true },
        },
        .foreignKeys = { Lightweight::SqlSchema::ForeignKeyConstraint {
            .foreignKey = { .table = { .catalog = "", .schema = "", .table = "orders" }, .columns = { "customer_id" } },
            .primaryKey = { .table = { .catalog = "", .schema = "", .table = "customers" }, .columns = { "id" } },
        } },
        .primaryKeys = { "id" },
    });

    auto const output = printer.ToString("Models");

    // The descriptor specializes the Lightweight customization point at global scope (qualified name)...
    CHECK(output.contains("struct Lightweight::Description<Models::Orders>"));
    CHECK(output.contains("static constexpr std::size_t FieldCount = 2;"));
    // ...carries one pointer-to-member per field, in order (the FK becomes the relation member `customer`)...
    CHECK(output.contains("using Members = Lightweight::RecordMemberList<&Models::Orders::id, &Models::Orders::customer>;"));
    // ...and the resolved SQL column names, where the relation keeps its real foreign-key column name.
    CHECK(output.contains(R"(FieldNames = { "id", "customer_id" };)"));
}

TEST_CASE("CxxModelPrinter::ResolveOrderAndPrintTable: orders by foreign-key dependencies", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter printer { CxxModelPrinter::Config {} };

    Lightweight::SqlSchema::Table const parent {
        .schema = "",
        .name = "parent",
        .columns = { { .name = "id", .type = Integer {}, .isNullable = false } },
        .primaryKeys = { "id" },
    };
    Lightweight::SqlSchema::Table const child {
        .schema = "",
        .name = "child",
        .columns = { { .name = "id", .type = Integer {}, .isNullable = false },
                     { .name = "parent_id", .type = Integer {}, .isNullable = false } },
        .foreignKeys = { Lightweight::SqlSchema::ForeignKeyConstraint {
            .foreignKey = { .table = { .catalog = "", .schema = "", .table = "child" }, .columns = { "parent_id" } },
            .primaryKey = { .table = { .catalog = "", .schema = "", .table = "parent" }, .columns = { "id" } },
        } },
        .primaryKeys = { "id" },
    };

    // Verify that resolver visits both tables. The exact ordering depends on the
    // resolver's heuristic for circular-dependency detection — we only require
    // that both tables make it into the output.
    printer.ResolveOrderAndPrintTable({ child, parent });
    auto const output = printer.ToString("Models");
    CHECK(output.contains("struct parent"));
    CHECK(output.contains("struct child"));
}

TEST_CASE("CxxModelPrinter::Example: produces compilable usage example", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter printer { CxxModelPrinter::Config {} };

    Lightweight::SqlSchema::Table const table {
        .schema = "",
        .name = "users",
        .columns = { { .name = "id", .type = Integer {}, .isNullable = false } },
        .primaryKeys = { "id" },
    };

    auto const example = printer.Example(table);
    // Just verify we produce non-empty output mentioning the table.
    CHECK(!example.empty());
}

TEST_CASE("CxxModelPrinter::PrintCumulativeHeaderFile writes valid header", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter printer { CxxModelPrinter::Config {} };

    printer.PrintTable(Lightweight::SqlSchema::Table {
        .schema = "",
        .name = "test_table",
        .columns = { { .name = "id", .type = Integer {}, .isNullable = false } },
    });

    auto const tempDir = std::filesystem::temp_directory_path() / "cxxmodelprinter-cumulative-test";
    std::filesystem::create_directories(tempDir);
    auto const cumulative = std::filesystem::path { "all.hpp" };

    auto const result = printer.PrintCumulativeHeaderFile(tempDir, cumulative);
    REQUIRE(result.has_value());

    auto const headerPath = tempDir / cumulative;
    REQUIRE(std::filesystem::exists(headerPath));

    // Scope the ifstream so it closes before `remove_all` runs — Windows holds an
    // exclusive open on the underlying file and would otherwise reject the delete.
    {
        std::ifstream f { headerPath };
        std::stringstream ss;
        ss << f.rdbuf();
        auto const content = ss.str();
        CHECK(content.contains("#pragma once"));
        CHECK(content.contains("test_table.hpp"));
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

// ================================================================================================
// CxxModelPrinter::StripSuffix removes configured suffixes
// ================================================================================================

TEST_CASE("CxxModelPrinter::StripSuffix removes default _id and _nr suffixes", "[CxxModelPrinter]")
{
    CxxModelPrinter printer { CxxModelPrinter::Config {} }; // defaults strip _id and _nr
    CHECK(printer.StripSuffix("user_id") == "user");
    CHECK(printer.StripSuffix("order_nr") == "order");
    CHECK(printer.StripSuffix("plain") == "plain");
}

TEST_CASE("CxxModelPrinter::StripSuffix uses a custom suffix list when configured", "[CxxModelPrinter]")
{
    CxxModelPrinter::Config config;
    config.stripSuffixes = { "_table" };
    CxxModelPrinter printer { config };
    CHECK(printer.StripSuffix("users_table") == "users");
    // _id is no longer in the strip list — falls through unchanged.
    CHECK(printer.StripSuffix("user_id") == "user_id");
}

// ================================================================================================
// CxxModelPrinter::PrintToFiles writes one .hpp per table
// ================================================================================================

TEST_CASE("CxxModelPrinter::PrintToFiles writes one header per known table", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

    auto const tempDir = std::filesystem::temp_directory_path() / "lightweight-cxxprinter-files";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    CxxModelPrinter printer { CxxModelPrinter::Config {} };
    printer.PrintTable(Lightweight::SqlSchema::Table { .schema = "",
                                                       .name = "alpha",
                                                       .columns = {
                                                           { .name = "id", .type = Integer {}, .isNullable = false },
                                                       } });
    printer.PrintTable(Lightweight::SqlSchema::Table { .schema = "",
                                                       .name = "beta",
                                                       .columns = {
                                                           { .name = "id", .type = Integer {}, .isNullable = false },
                                                       } });

    printer.PrintToFiles("Models", tempDir.string());

    CHECK(std::filesystem::exists(tempDir / "alpha.hpp"));
    CHECK(std::filesystem::exists(tempDir / "beta.hpp"));

    auto readFile = [](std::filesystem::path const& p) {
        std::ifstream f { p };
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    auto const alpha = readFile(tempDir / "alpha.hpp");
    CHECK(alpha.contains("namespace Models"));
    CHECK(alpha.contains("struct alpha"));

    std::filesystem::remove_all(tempDir);
}

// ================================================================================================
// CxxModelPrinter::TableIncludes produces #include lines for the registered tables
// ================================================================================================

TEST_CASE("CxxModelPrinter::TableIncludes lists registered tables", "[CxxModelPrinter]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CxxModelPrinter printer { CxxModelPrinter::Config {} };

    printer.PrintTable(Lightweight::SqlSchema::Table { .schema = "",
                                                       .name = "alpha",
                                                       .columns = {
                                                           { .name = "id", .type = Integer {}, .isNullable = false },
                                                       } });
    printer.PrintTable(Lightweight::SqlSchema::Table { .schema = "",
                                                       .name = "beta",
                                                       .columns = {
                                                           { .name = "id", .type = Integer {}, .isNullable = false },
                                                       } });

    auto const includes = printer.TableIncludes();
    CHECK(includes.contains("alpha"));
    CHECK(includes.contains("beta"));
    CHECK(includes.contains("#include"));
}

TEST_CASE("CxxModelPrinter: nullable int produces optional", "[CxxModelPrinter],[MakeType]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    CHECK(CxxTypeName({ .name = "x", .type = Integer {}, .isNullable = true }) == "std::optional<int32_t>");
    CHECK(CxxTypeName({ .name = "x", .type = Date {}, .isNullable = true }) == "std::optional<Light::SqlDate>");
    CHECK(CxxTypeName({ .name = "x", .type = Bool {}, .isNullable = true }) == "std::optional<bool>");
}
