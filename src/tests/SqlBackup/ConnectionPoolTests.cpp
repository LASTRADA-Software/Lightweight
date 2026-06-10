// SPDX-License-Identifier: Apache-2.0
#include "../../Lightweight/SqlBackup/ConnectionPool.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Lightweight::SqlBackup::detail;
using namespace Lightweight::SqlBackup;
using namespace Lightweight;

TEST_CASE("ConnectionPool hands out distinct live connections and recycles", "[connectionpool]")
{
    auto const& connStr = SqlConnection::DefaultConnectionString();
    RetrySettings const retry;
    NullProgressManager progress;

    ConnectionPool pool { connStr, /*size=*/2, retry, progress };
    {
        auto a = pool.Acquire();
        auto b = pool.Acquire();
        REQUIRE(a.Get().IsAlive());
        REQUIRE(b.Get().IsAlive());
        REQUIRE(&a.Get() != &b.Get());
    } // leases released here

    auto c = pool.Acquire(); // must not deadlock; recycled connection
    REQUIRE(c.Get().IsAlive());
}
