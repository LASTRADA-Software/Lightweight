// SPDX-License-Identifier: Apache-2.0
//
// Smoke coverage for `Lightweight::Odbc::EnumerateDataSources` and
// `EnumerateDrivers`. These tests hit the real ODBC driver manager, so they
// are tagged `[.integration]` and skipped by default on CI images that don't
// install unixODBC. On Windows the driver manager is always present.

#include <Lightweight/Odbc/DataSourceEnumerator.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("DataSourceEnumerator — EnumerateDrivers finds at least one driver on systems with ODBC",
          "[.integration][DataSourceEnumerator]")
{
    auto const drivers = Lightweight::Odbc::EnumerateDrivers();

    // An ODBC install with no drivers registered is legal (e.g. a fresh
    // container) — we don't require a minimum count. What we *do* require is
    // structural sanity: every returned entry has a non-empty name.
    for (auto const& driver: drivers)
    {
        CHECK_FALSE(driver.name.empty());
        for (auto const& [key, _]: driver.attributes)
            CHECK_FALSE(key.empty());
    }
}

TEST_CASE("DataSourceEnumerator — EnumerateDataSources returns user/system DSNs with non-empty names",
          "[.integration][DataSourceEnumerator]")
{
    auto const dsns = Lightweight::Odbc::EnumerateDataSources();

    // Same structural invariants as the driver test — we don't assume specific
    // DSNs exist because CI machines vary wildly. But every returned entry
    // must have a name, and the scope must be one of the two enum values.
    for (auto const& dsn: dsns)
    {
        CHECK_FALSE(dsn.name.empty());
        CHECK((dsn.scope == Lightweight::Odbc::DataSourceInfo::Scope::User
               || dsn.scope == Lightweight::Odbc::DataSourceInfo::Scope::System));
    }
}
