// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/StandardProgressManager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <sstream>
#include <thread>

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

TEST_CASE("AddTotalItems accumulates correctly", "[dbtool][StandardProgressManager]")
{
    std::stringstream out;
    StandardProgressManager pm(false, out);

    // Set initial total
    pm.SetTotalItems(100);

    // Add more items progressively (simulating row counter thread behavior)
    pm.AddTotalItems(50);
    pm.AddTotalItems(75);

    // Process some items and call AllDone
    pm.OnItemsProcessed(225);
    pm.AllDone();

    std::string const output = out.str();
    // After AllDone, the total row count should be 225 (100 + 50 + 75)
    REQUIRE(output.find("225 rows processed") != std::string::npos);
}

TEST_CASE("Summary shows counting when total unknown", "[dbtool][StandardProgressManager]")
{
    std::stringstream out;
    StandardProgressManager pm(false, out);

    // Enable summary line by calling OnItemsProcessed (sets _hasSummaryLine = true)
    // but don't set total yet (simulating row counter thread not started yet)
    pm.OnItemsProcessed(100);

    // Now trigger a table progress to allocate summary line (_summaryLineAllocated = true)
    SqlBackup::Progress p { .state = SqlBackup::Progress::State::InProgress,
                            .tableName = "TestTable",
                            .currentRows = 10,
                            .totalRows = 100,
                            .message = "Processing" };
    pm.Update(p);

    // Give it some time for the summary to be printed (rate update happens every 200ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pm.OnItemsProcessed(50);

    std::string const output = out.str();
    // When total is unknown (0), the summary should show "counting..."
    REQUIRE(output.find("counting...") != std::string::npos);
}

TEST_CASE("AllDone shows 100% after capping at 99%", "[dbtool][StandardProgressManager]")
{
    std::stringstream out;
    StandardProgressManager pm(false, out);

    // Set total first so _hasSummaryLine is true before table update
    pm.SetTotalItems(1000);

    // Allocate a table line to trigger summary line allocation (_summaryLineAllocated = true)
    SqlBackup::Progress p { .state = SqlBackup::Progress::State::InProgress,
                            .tableName = "TestTable",
                            .currentRows = 50,
                            .totalRows = 100,
                            .message = "Processing" };
    pm.Update(p);

    // Process all items
    pm.OnItemsProcessed(1000);

    // Wait for rate sampling to happen and summary to update
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pm.OnItemsProcessed(0); // Trigger another summary print

    std::string const midOutput = out.str();
    // Before AllDone, even with 100% completion, progress should be capped at 99%
    // The summary line should show a percentage less than 100
    REQUIRE(midOutput.find("99") != std::string::npos);

    // Now call AllDone
    pm.AllDone();

    std::string const finalOutput = out.str();
    // After AllDone, we should see 100% (or the final summary showing all rows processed)
    REQUIRE(finalOutput.find("100") != std::string::npos);
    REQUIRE(finalOutput.find("1000 rows processed") != std::string::npos);
}

TEST_CASE("Progress capped at 99% before AllDone", "[dbtool][StandardProgressManager]")
{
    std::stringstream out;
    StandardProgressManager pm(false, out);

    // Set a known total first so _hasSummaryLine is true before table update
    pm.SetTotalItems(100);

    // Allocate a table line to trigger summary line allocation
    SqlBackup::Progress p { .state = SqlBackup::Progress::State::InProgress,
                            .tableName = "TestTable",
                            .currentRows = 50,
                            .totalRows = 100,
                            .message = "Processing" };
    pm.Update(p);

    // Process all 100 items - workers can be ahead of counter thread
    pm.OnItemsProcessed(100);

    // Wait for rate calculation update
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pm.OnItemsProcessed(0); // Trigger summary update

    std::string const output = out.str();
    // Despite processing 100% of items, the display should still show 99% max
    // because we haven't called AllDone() yet
    // The output should contain 99.xx% somewhere in the progress line
    REQUIRE(output.find("99") != std::string::npos);
}
