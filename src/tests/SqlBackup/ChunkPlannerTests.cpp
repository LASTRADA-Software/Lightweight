// SPDX-License-Identifier: Apache-2.0
#include "../../Lightweight/SqlBackup/ChunkPlanner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Lightweight::SqlBackup::detail;
using namespace Lightweight;

namespace
{
SqlSchema::Table MakeTable(std::string name)
{
    SqlSchema::Table t;
    t.name = std::move(name);
    SqlSchema::Column c;
    c.name = "data";
    c.type = SqlColumnTypeDefinitions::Varchar { .size = 50 };
    t.columns.push_back(c);
    return t;
}

// Fake plan-time bounds: tables named "Empty*" report no rows; everything else [1, 2500].
std::optional<std::pair<int64_t, int64_t>> FakeBounds(SqlSchema::Table const& table, std::string const& /*pk*/)
{
    if (table.name.starts_with("Empty"))
        return std::nullopt;
    return std::pair<int64_t, int64_t> { 1, 2500 };
}
} // namespace

TEST_CASE("PlanChunks: no-PK table keeps a single OFFSET seed chunk with state", "[chunkplanner]")
{
    auto const tables = std::vector<SqlSchema::Table> { MakeTable("Foo") };
    auto const plan = PlanChunks(tables, /*rowsPerChunk=*/1000, FakeBounds, SqlServerType::SQLITE);

    REQUIRE(plan.chunks.size() == 1);
    CHECK(plan.chunks.front().strategy == ChunkStrategy::Offset);
    CHECK(plan.chunks.front().windowIndex == 0);
    CHECK(plan.chunks.front().table == &tables.front());
    CHECK(plan.chunks.front().offset == 0);
    REQUIRE(plan.tableStates.size() == 1);
    CHECK(plan.tableStates.front().remainingChunks.load() == 1);
    CHECK(plan.chunks.front().state == &plan.tableStates.front());
}

TEST_CASE("SingleNumericPrimaryKey accepts one integer PK, rejects composite/non-numeric", "[chunkplanner]")
{
    SqlSchema::Table t;
    t.primaryKeys = { "id" };
    SqlSchema::Column id;
    id.name = "id";
    id.type = SqlColumnTypeDefinitions::Bigint {};
    id.isPrimaryKey = true;
    t.columns.push_back(id);
    REQUIRE(SingleNumericPrimaryKey(t) == std::optional<std::string> { "id" });

    SqlSchema::Table composite;
    composite.primaryKeys = { "a", "b" };
    REQUIRE_FALSE(SingleNumericPrimaryKey(composite).has_value());

    SqlSchema::Table guidPk;
    guidPk.primaryKeys = { "g" };
    SqlSchema::Column g;
    g.name = "g";
    g.type = SqlColumnTypeDefinitions::Guid {};
    g.isPrimaryKey = true;
    guidPk.columns.push_back(g);
    REQUIRE_FALSE(SingleNumericPrimaryKey(guidPk).has_value());
}

TEST_CASE("PlanChunks: PK table is split into one chunk per window with shared state", "[chunkplanner]")
{
    SqlSchema::Table table = MakeTable("Numbered");
    table.primaryKeys = { "id" };
    SqlSchema::Column id;
    id.name = "id";
    id.type = SqlColumnTypeDefinitions::Integer {};
    id.isPrimaryKey = true;
    table.columns.push_back(id);

    auto const tables = std::vector<SqlSchema::Table> { std::move(table) };
    auto const plan = PlanChunks(tables, /*rowsPerChunk=*/1000, FakeBounds, SqlServerType::SQLITE);

    // [1,2500] at rowsPerChunk=1000 -> width ceil(2500/3)=834 -> 3 windows.
    REQUIRE(plan.chunks.size() == 3);
    REQUIRE(plan.tableStates.size() == 1);
    auto const& state = plan.tableStates.front();
    CHECK(state.remainingChunks.load() == 3);
    CHECK(state.totalRows.load() == 2500);
    for (uint32_t i = 0; i < 3; ++i)
    {
        CHECK(plan.chunks[i].strategy == ChunkStrategy::PrimaryKeyRange);
        CHECK(plan.chunks[i].windowIndex == i);
        CHECK(plan.chunks[i].pkColumn == "id");
        CHECK(plan.chunks[i].table == &tables.front());
        CHECK(plan.chunks[i].state == &state);
    }
    // Windows are contiguous and cover [1, 2500] exactly.
    CHECK(plan.chunks[0].lo == 1);
    for (size_t i = 1; i < 3; ++i)
        CHECK(plan.chunks[i].lo == plan.chunks[i - 1].hi + 1);
    CHECK(plan.chunks[2].hi == 2500);
}

TEST_CASE("PlanChunks: full-range PK span saturates the totalRows estimate without UB", "[chunkplanner]")
{
    SqlSchema::Table table = MakeTable("FullRange");
    table.primaryKeys = { "id" };
    SqlSchema::Column id;
    id.name = "id";
    id.type = SqlColumnTypeDefinitions::Bigint {};
    id.isPrimaryKey = true;
    table.columns.push_back(id);

    auto const tables = std::vector<SqlSchema::Table> { std::move(table) };
    auto const fullRange = [](SqlSchema::Table const&, std::string const&) {
        return std::optional<std::pair<int64_t, int64_t>> { { std::numeric_limits<int64_t>::min(),
                                                              std::numeric_limits<int64_t>::max() } };
    };
    // rowsPerChunk 1 + full span also exercises the window-count wrap guard end to end.
    auto const plan = PlanChunks(tables, 1, fullRange, SqlServerType::SQLITE);

    REQUIRE(plan.tableStates.size() == 1);
    CHECK(plan.tableStates.front().totalRows.load() == std::numeric_limits<size_t>::max());
    CHECK(!plan.chunks.empty());
    CHECK(plan.chunks.size() <= static_cast<size_t>(MaxWindowsPerTable));
}

TEST_CASE("PlanChunks: empty PK table emits no chunks and is listed as empty", "[chunkplanner]")
{
    SqlSchema::Table table = MakeTable("EmptyOne");
    table.primaryKeys = { "id" };
    SqlSchema::Column id;
    id.name = "id";
    id.type = SqlColumnTypeDefinitions::Integer {};
    id.isPrimaryKey = true;
    table.columns.push_back(id);

    auto const tables = std::vector<SqlSchema::Table> { std::move(table) };
    auto const plan = PlanChunks(tables, 1000, FakeBounds, SqlServerType::SQLITE);

    CHECK(plan.chunks.empty());
    REQUIRE(plan.emptyTables.size() == 1);
    CHECK(plan.emptyTables.front() == &tables.front());
}

TEST_CASE("TableHasLobColumn detects LOB types", "[chunkplanner]")
{
    SqlSchema::Table t;
    t.name = "WithLob";
    SqlSchema::Column lob;
    lob.name = "blob";
    lob.type = SqlColumnTypeDefinitions::Varchar { .size = 2147483647 }; // varchar(max) sentinel
    t.columns.push_back(lob);
    REQUIRE(TableHasLobColumn(t));

    SqlSchema::Table t2;
    SqlSchema::Column narrow;
    narrow.name = "n";
    narrow.type = SqlColumnTypeDefinitions::Integer {};
    t2.columns.push_back(narrow);
    REQUIRE_FALSE(TableHasLobColumn(t2));

    SqlSchema::Table withBinary;
    SqlSchema::Column bin;
    bin.name = "bin";
    bin.type = SqlColumnTypeDefinitions::VarBinary { .size = 16 }; // binary is a LOB regardless of size
    withBinary.columns.push_back(bin);
    REQUIRE(TableHasLobColumn(withBinary));

    SqlSchema::Table zeroSize;
    SqlSchema::Column z;
    z.name = "z";
    z.type = SqlColumnTypeDefinitions::Varchar { .size = 0 }; // size==0 is the MAX/LOB sentinel
    zeroSize.columns.push_back(z);
    REQUIRE(TableHasLobColumn(zeroSize));
}

TEST_CASE("TableHasLobColumn classifies declared Varchar widths against the 1 MB LOB threshold", "[chunkplanner]")
{
    using SqlColumnTypeDefinitions::Varchar;
    constexpr std::size_t LobThreshold = 1U << 20; // 1 MB, mirrors ChunkPlanner.cpp

    auto tableWithVarchar = [](std::size_t size) {
        SqlSchema::Table t;
        SqlSchema::Column c;
        c.name = "v";
        c.type = Varchar { .size = size };
        t.columns.push_back(c);
        return t;
    };

    // Anything strictly below the threshold (including the previously-questioned 8 KB / 64 KB range)
    // is a normal bounded column that CAN be fixed-stride array-bound: NOT a LOB.
    CHECK_FALSE(TableHasLobColumn(tableWithVarchar(8 * 1024)));
    CHECK_FALSE(TableHasLobColumn(tableWithVarchar(64 * 1024)));
    CHECK_FALSE(TableHasLobColumn(tableWithVarchar(LobThreshold - 1)));

    // At or above the threshold the declared width is treated as an unbounded/LOB column.
    CHECK(TableHasLobColumn(tableWithVarchar(LobThreshold)));
    CHECK(TableHasLobColumn(tableWithVarchar(LobThreshold + 1)));
}

TEST_CASE("TableIsArrayFetchable accepts only the safe simple-column type set", "[chunkplanner]")
{
    using namespace SqlColumnTypeDefinitions;

    auto makeColumn = [](std::string name, SqlColumnTypeDefinition type) {
        SqlSchema::Column c;
        c.name = std::move(name);
        c.type = type;
        return c;
    };

    SECTION("all-safe table {int, varchar(50), decimal} is array-fetchable")
    {
        SqlSchema::Table t;
        t.name = "Simple";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("s", Varchar { .size = 50 }));
        t.columns.push_back(makeColumn("d", Decimal { .precision = 18, .scale = 4 }));
        CHECK(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("integer family + Real + Bool + Char are all safe")
    {
        SqlSchema::Table t;
        t.name = "Mixed";
        t.columns.push_back(makeColumn("a", Bigint {}));
        t.columns.push_back(makeColumn("b", Smallint {}));
        t.columns.push_back(makeColumn("c", Tinyint {}));
        t.columns.push_back(makeColumn("r", Real {}));
        t.columns.push_back(makeColumn("flag", Bool {}));
        t.columns.push_back(makeColumn("ch", Char { .size = 4 }));
        CHECK(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("a Text column is a LOB, so the table is NOT array-fetchable")
    {
        SqlSchema::Table t;
        t.name = "WithText";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("txt", Text { .size = 256 }));
        REQUIRE(TableHasLobColumn(t));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("NVarchar/NChar (unicode) columns are array-fetchable (P6: SQL_C_WCHAR binding)")
    {
        SqlSchema::Table t;
        t.name = "Unicode";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("u", NVarchar { .size = 50 }));
        t.columns.push_back(makeColumn("c", NChar { .size = 8 }));
        CHECK(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("a Time column is array-fetchable on PG/MSSQL (text read) but not on SQLite")
    {
        SqlSchema::Table t;
        t.name = "Timed";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("at", Time {}));
        CHECK(TableIsArrayFetchable(t, SqlServerType::MICROSOFT_SQL));
        CHECK(TableIsArrayFetchable(t, SqlServerType::POSTGRESQL));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("an NVarchar(max) column is a LOB, so the table is NOT array-fetchable")
    {
        SqlSchema::Table t;
        t.name = "UnicodeLob";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("u", NVarchar { .size = 0 })); // size==0 is the MAX/LOB sentinel
        REQUIRE(TableHasLobColumn(t));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("Date/DateTime/Timestamp (temporal) columns are array-fetchable (P6: native structs)")
    {
        SqlSchema::Table t;
        t.name = "Temporal";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("d", Date {}));
        t.columns.push_back(makeColumn("when", DateTime {}));
        t.columns.push_back(makeColumn("at", Timestamp {}));
        CHECK(TableIsArrayFetchable(t, SqlServerType::SQLITE));
        CHECK(TableIsArrayFetchable(t, SqlServerType::MICROSOFT_SQL));
        CHECK(TableIsArrayFetchable(t, SqlServerType::POSTGRESQL));
    }

    SECTION("a Guid column is array-fetchable on MSSQL/PG (SQL_C_GUID) but not on SQLite (text)")
    {
        SqlSchema::Table t;
        t.name = "Guided";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("g", Guid {}));
        CHECK(TableIsArrayFetchable(t, SqlServerType::MICROSOFT_SQL));
        CHECK(TableIsArrayFetchable(t, SqlServerType::POSTGRESQL));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("a LOB column makes the table NOT array-fetchable even with otherwise-safe types")
    {
        SqlSchema::Table t;
        t.name = "WithLob";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("blob", Varchar { .size = 2147483647 })); // varchar(max) sentinel
        REQUIRE(TableHasLobColumn(t));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("a VarBinary column makes the table NOT array-fetchable")
    {
        SqlSchema::Table t;
        t.name = "WithBinary";
        t.columns.push_back(makeColumn("id", Integer {}));
        t.columns.push_back(makeColumn("bin", VarBinary { .size = 16 }));
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }

    SECTION("an empty-column table is not array-fetchable")
    {
        SqlSchema::Table t;
        t.name = "Empty";
        CHECK_FALSE(TableIsArrayFetchable(t, SqlServerType::SQLITE));
    }
}

TEST_CASE("PlanChunks sets arrayFetchable per table classification", "[chunkplanner]")
{
    using namespace SqlColumnTypeDefinitions;

    // Simple table (int PK + varchar) -> PrimaryKeyRange chunk that is array-fetchable.
    SqlSchema::Table simple;
    simple.name = "Simple";
    simple.primaryKeys = { "id" };
    SqlSchema::Column id;
    id.name = "id";
    id.type = Integer {};
    id.isPrimaryKey = true;
    simple.columns.push_back(id);
    SqlSchema::Column s;
    s.name = "s";
    s.type = Varchar { .size = 50 };
    simple.columns.push_back(s);

    // Binary-LOB table (no PK) -> OFFSET chunk that is NOT array-fetchable.
    SqlSchema::Table withBin;
    withBin.name = "WithBin";
    SqlSchema::Column u;
    u.name = "u";
    u.type = VarBinary { .size = 16 };
    withBin.columns.push_back(u);

    auto const tables = std::vector<SqlSchema::Table> { std::move(simple), std::move(withBin) };
    auto const plan = PlanChunks(tables, /*rowsPerChunk=*/1000, FakeBounds, SqlServerType::SQLITE);

    // The PK table yields 3 windows (FakeBounds: [1,2500] at 1000 rows/chunk), all array-fetchable;
    // the binary no-PK table contributes the trailing OFFSET chunk, not array-fetchable.
    REQUIRE(plan.chunks.size() == 4);
    CHECK(plan.chunks[0].strategy == ChunkStrategy::PrimaryKeyRange);
    CHECK(plan.chunks[0].arrayFetchable);
    CHECK_FALSE(plan.chunks[0].hasLob);
    CHECK(plan.chunks.back().strategy == ChunkStrategy::Offset);
    CHECK_FALSE(plan.chunks.back().arrayFetchable);
}

TEST_CASE("CappedWindowWidth derives window width from span with a window cap", "[chunkplanner]")
{
    // Dense identity PK that fits one window: width == span.
    CHECK(CappedWindowWidth(1, 100'000, 100'000, 1024) == 100'000);
    // Dense PK needing 3 windows: width is the even split (ceil(250000/3)).
    CHECK(CappedWindowWidth(1, 250'000, 100'000, 1024) == 83'334);
    // Sparse key space (snowflake-style): width widens so the window count stays <= cap.
    auto const sparseWidth = CappedWindowWidth(0, 1'000'000'000'000'000'000, 100'000, 1024);
    CHECK(PlanPrimaryKeyWindows(0, 1'000'000'000'000'000'000, sparseWidth).size() <= 1024);
    CHECK(PlanPrimaryKeyWindows(0, 1'000'000'000'000'000'000, sparseWidth).size() >= 512);
    // Tiny span near INT64_MAX must not overflow.
    CHECK(CappedWindowWidth(std::numeric_limits<int64_t>::max() - 2, std::numeric_limits<int64_t>::max(), 1000, 1024) == 3);
    // Full int64 range must not overflow and must yield a positive width.
    CHECK(CappedWindowWidth(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 100'000, 1024) > 0);
    // Defensive degenerate inputs.
    CHECK(CappedWindowWidth(10, 5, 1000, 1024) == 1000); // min > max -> rowsPerChunk passthrough
    CHECK(CappedWindowWidth(1, 100, 0, 0) >= 1);         // zero rpc/cap clamped
    // Full int64 span at rowsPerChunk 1: spanMinus1/rpc is UINT64_MAX; the window count must be
    // clamped BEFORE the +1 or it wraps to zero and the width computation divides by zero.
    CHECK(CappedWindowWidth(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 1, 1024) > 0);
}

TEST_CASE("PlanPrimaryKeyWindows covers the range exactly once, no gaps/overlaps", "[chunkplanner]")
{
    using W = std::pair<int64_t, int64_t>;
    // Three windows, last is short.
    REQUIRE(PlanPrimaryKeyWindows(1, 2500, 1000) == std::vector<W> { { 1, 1000 }, { 1001, 2000 }, { 2001, 2500 } });
    // Exact multiple: no spurious trailing empty window.
    REQUIRE(PlanPrimaryKeyWindows(1, 2000, 1000) == std::vector<W> { { 1, 1000 }, { 1001, 2000 } });
    // Single row.
    REQUIRE(PlanPrimaryKeyWindows(5, 5, 1000) == std::vector<W> { { 5, 5 } });
    // Empty (min > max) -> no windows.
    REQUIRE(PlanPrimaryKeyWindows(10, 5, 1000).empty());
    // Overflow guard: pkMax at INT64_MAX must not overflow and must terminate.
    auto const big =
        PlanPrimaryKeyWindows(std::numeric_limits<int64_t>::max() - 2, std::numeric_limits<int64_t>::max(), 1000);
    REQUIRE(big.size() == 1);
    REQUIRE(big.front() == W { std::numeric_limits<int64_t>::max() - 2, std::numeric_limits<int64_t>::max() });
}
