// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/TableFilter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Lightweight::SqlBackup;

TEST_CASE("TableFilter: Default star matches all tables", "[TableFilter]")
{
    auto filter = TableFilter::Parse("*");
    REQUIRE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("dbo", "Products"));
    REQUIRE(filter.Matches("", "any_table_name"));
}

TEST_CASE("TableFilter: Empty filter matches all tables", "[TableFilter]")
{
    auto filter = TableFilter::Parse("");
    REQUIRE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("dbo", "Products"));
}

TEST_CASE("TableFilter: Whitespace-only filter matches all tables", "[TableFilter]")
{
    auto filter = TableFilter::Parse("   ");
    REQUIRE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "Users"));
}

TEST_CASE("TableFilter: Exact table name match", "[TableFilter]")
{
    auto filter = TableFilter::Parse("Users");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 1);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("dbo", "Users"));
    REQUIRE_FALSE(filter.Matches("", "Products"));
    REQUIRE_FALSE(filter.Matches("", "users")); // Case sensitive
}

TEST_CASE("TableFilter: Multiple exact table names", "[TableFilter]")
{
    auto filter = TableFilter::Parse("Users,Products,Orders");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 3);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "Products"));
    REQUIRE(filter.Matches("", "Orders"));
    REQUIRE_FALSE(filter.Matches("", "Customers"));
}

TEST_CASE("TableFilter: Wildcard suffix pattern", "[TableFilter]")
{
    auto filter = TableFilter::Parse("User*");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "User"));
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "UserAccounts"));
    REQUIRE(filter.Matches("", "UserProfile"));
    REQUIRE_FALSE(filter.Matches("", "AppUser"));
    REQUIRE_FALSE(filter.Matches("", "Products"));
}

TEST_CASE("TableFilter: Wildcard prefix pattern", "[TableFilter]")
{
    auto filter = TableFilter::Parse("*_log");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "audit_log"));
    REQUIRE(filter.Matches("", "error_log"));
    REQUIRE(filter.Matches("", "_log"));
    REQUIRE_FALSE(filter.Matches("", "log"));
    REQUIRE_FALSE(filter.Matches("", "log_audit"));
}

TEST_CASE("TableFilter: Wildcard anywhere pattern", "[TableFilter]")
{
    auto filter = TableFilter::Parse("*audit*");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "audit"));
    REQUIRE(filter.Matches("", "audit_log"));
    REQUIRE(filter.Matches("", "user_audit"));
    REQUIRE(filter.Matches("", "user_audit_log"));
    REQUIRE_FALSE(filter.Matches("", "Users"));
}

TEST_CASE("TableFilter: Single char wildcard", "[TableFilter]")
{
    auto filter = TableFilter::Parse("User?");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "User1"));
    REQUIRE(filter.Matches("", "UserA"));
    REQUIRE_FALSE(filter.Matches("", "User"));
    REQUIRE_FALSE(filter.Matches("", "User12"));
    REQUIRE_FALSE(filter.Matches("", "UserAB"));
}

TEST_CASE("TableFilter: Schema.table notation - exact match", "[TableFilter]")
{
    auto filter = TableFilter::Parse("dbo.Users");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("dbo", "Users"));
    REQUIRE_FALSE(filter.Matches("sales", "Users"));
    REQUIRE_FALSE(filter.Matches("", "Users"));
    REQUIRE_FALSE(filter.Matches("dbo", "Products"));
}

TEST_CASE("TableFilter: Schema.* matches all tables in schema", "[TableFilter]")
{
    auto filter = TableFilter::Parse("dbo.*");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("dbo", "Users"));
    REQUIRE(filter.Matches("dbo", "Products"));
    REQUIRE(filter.Matches("dbo", "Orders"));
    REQUIRE_FALSE(filter.Matches("sales", "Users"));
    REQUIRE_FALSE(filter.Matches("", "Users"));
}

TEST_CASE("TableFilter: *.table matches table in any schema", "[TableFilter]")
{
    auto filter = TableFilter::Parse("*.Users");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("dbo", "Users"));
    REQUIRE(filter.Matches("sales", "Users"));
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE_FALSE(filter.Matches("dbo", "Products"));
}

TEST_CASE("TableFilter: Schema wildcard pattern", "[TableFilter]")
{
    auto filter = TableFilter::Parse("sales*.Users");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.Matches("sales", "Users"));
    REQUIRE(filter.Matches("sales_2024", "Users"));
    REQUIRE(filter.Matches("salesforce", "Users"));
    REQUIRE_FALSE(filter.Matches("dbo", "Users"));
    REQUIRE_FALSE(filter.Matches("sales", "Products"));
}

TEST_CASE("TableFilter: Mixed patterns with schema and without", "[TableFilter]")
{
    auto filter = TableFilter::Parse("dbo.Users,Products,sales.*");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 3);

    // dbo.Users - only matches Users in dbo schema
    REQUIRE(filter.Matches("dbo", "Users"));
    // sales.Users matches the sales.* pattern
    REQUIRE(filter.Matches("sales", "Users"));

    // Products - matches in any schema
    REQUIRE(filter.Matches("", "Products"));
    REQUIRE(filter.Matches("dbo", "Products"));
    REQUIRE(filter.Matches("sales", "Products")); // matches both Products pattern and sales.* pattern

    // sales.* - matches any table in sales schema
    REQUIRE(filter.Matches("sales", "Orders"));
    REQUIRE(filter.Matches("sales", "Customers"));
    REQUIRE_FALSE(filter.Matches("dbo", "Orders"));
}

TEST_CASE("TableFilter: Whitespace handling around patterns", "[TableFilter]")
{
    auto filter = TableFilter::Parse("  Users  ,  Products  ");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 2);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "Products"));
}

TEST_CASE("TableFilter: Trailing comma is ignored", "[TableFilter]")
{
    auto filter = TableFilter::Parse("Users,Products,");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 2);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "Products"));
}

TEST_CASE("TableFilter: Leading comma is ignored", "[TableFilter]")
{
    auto filter = TableFilter::Parse(",Users,Products");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 2);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "Products"));
}

TEST_CASE("TableFilter: Double comma is ignored", "[TableFilter]")
{
    auto filter = TableFilter::Parse("Users,,Products");
    REQUIRE_FALSE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 2);
    REQUIRE(filter.Matches("", "Users"));
    REQUIRE(filter.Matches("", "Products"));
}

TEST_CASE("TableFilter: Single star pattern in list results in matchAll", "[TableFilter]")
{
    auto filter = TableFilter::Parse("Users,*,Products");
    REQUIRE(filter.MatchesAll());
    REQUIRE(filter.PatternCount() == 0); // patterns cleared when * is encountered
}

TEST_CASE("TableFilter: GlobMatch edge cases", "[TableFilter]")
{
    // Test that the glob matching handles various edge cases
    auto filter = TableFilter::Parse("***test***");
    REQUIRE(filter.Matches("", "test"));
    REQUIRE(filter.Matches("", "aaatestbbb"));
    REQUIRE(filter.Matches("", "testsomething"));
    REQUIRE(filter.Matches("", "somethingtestmore"));

    auto filter2 = TableFilter::Parse("a*b*c");
    REQUIRE(filter2.Matches("", "abc"));
    REQUIRE(filter2.Matches("", "aXbc"));
    REQUIRE(filter2.Matches("", "abXc"));
    REQUIRE(filter2.Matches("", "aXbXc"));
    REQUIRE(filter2.Matches("", "aXXXbYYYc"));
    REQUIRE_FALSE(filter2.Matches("", "ab"));
    REQUIRE_FALSE(filter2.Matches("", "bc"));
    REQUIRE_FALSE(filter2.Matches("", "ac"));

    auto filter3 = TableFilter::Parse("?");
    REQUIRE(filter3.Matches("", "a"));
    REQUIRE(filter3.Matches("", "X"));
    REQUIRE_FALSE(filter3.Matches("", ""));
    REQUIRE_FALSE(filter3.Matches("", "ab"));
}

TEST_CASE("TableFilter: Empty table after schema dot", "[TableFilter]")
{
    // "dbo." with no table name should be skipped
    auto filter = TableFilter::Parse("dbo.");
    REQUIRE(filter.MatchesAll()); // No valid patterns -> matchAll
}

TEST_CASE("TableFilter: Dot only", "[TableFilter]")
{
    auto filter = TableFilter::Parse(".");
    REQUIRE(filter.MatchesAll()); // Schema is empty, table is empty -> no valid pattern
}
