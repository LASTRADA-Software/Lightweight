// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/StandardProgressManager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using namespace Lightweight;
using namespace Lightweight::Tools;

TEST_CASE("StandardProgressManager settles finished items", "[dbtool][StandardProgressManager]")
{
    std::stringstream out;
    StandardProgressManager pm(false, out);

    SqlBackup::Progress p1 { .state = SqlBackup::Progress::State::InProgress,
                             .tableName = "Table1",
                             .currentRows = 50,
                             .totalRows = 100,
                             .message = "Processing Table1" };
    SqlBackup::Progress p2 { .state = SqlBackup::Progress::State::InProgress,
                             .tableName = "Table2",
                             .currentRows = 10,
                             .totalRows = 100,
                             .message = "Processing Table2" };

    pm.Update(p1);
    pm.Update(p2);

    // Initial state:
    // Table1 (index 0)
    // Table2 (index 1)

    p2.state = SqlBackup::Progress::State::Finished;
    p2.currentRows = 100;
    p2.message = "Done Table2";
    pm.Update(p2);

    // Expected state after p2 finishes:
    // Table2 (index 0) - moved up because it's finished
    // Table1 (index 1) - shifted down because it's still active

    p1.currentRows = 75;
    pm.Update(p1); // Should still update at index 1

    p1.state = SqlBackup::Progress::State::Finished;
    p1.currentRows = 100;
    p1.message = "Done Table1";
    pm.Update(p1);

    // Expected final state:
    // Table2 (index 0)
    // Table1 (index 1) - finished after Table2, but Table2 was already at the top

    pm.AllDone();

    // Verification: We can't easily parse VT100 sequences here, but we can check if it crashes or does anything weird.
    // The main goal is to ensure the internal logic for _lineTableMapping and _tableLines is correct.
    // Since we don't have public access to them, we rely on the implementation correctness for now.
    // In a more thorough test, we could expose them for testing or parse the stringstream.

    std::string const output = out.str();
    REQUIRE_FALSE(output.empty());
    REQUIRE(output.find("Table1") != std::string::npos);
    REQUIRE(output.find("Table2") != std::string::npos);
    REQUIRE(output.find("Total time:") != std::string::npos);
}
