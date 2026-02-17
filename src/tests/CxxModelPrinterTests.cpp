// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

        namespace Test
        {

        struct test final
        {
            Light::Field<std::optional<int32_t>> id;
            Light::Field<std::optional<Light::SqlAnsiString<64>>> name;
        };

        } // end namespace Test
    )cpp"));
}
