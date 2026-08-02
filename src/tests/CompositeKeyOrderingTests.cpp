// SPDX-License-Identifier: Apache-2.0
//
// Predicate ordering for composite keys.
//
// `QuerySingle`/`Update`/`Delete` build their `WHERE` clause by enumerating the record's members in
// *declaration* order and appending one predicate per `PrimaryKey` field (`DataMapper.hpp`, the
// `EnumerateRecordMembers` loop). Arguments are then bound positionally, in the order given at the call
// site. So for a composite key the caller's argument order has to match the record's member order, and
// nothing in the type system enforces it.
//
// That matters for `CompositeForeignKey`, whose loader has to supply those arguments from a list of
// `Connection`s. If the connections are written in a different order than the parent declares its key
// fields, a positional bind sends each value to the wrong predicate. When the key columns share a type
// - which is the common case, e.g. two `INT` parts - the result is not an error but a wrong row.
//
// These tests establish the ground truth the loader is designed against, using only what exists today.
// They are the specification for the ordering decision recorded in `docs/composite-keys-design.md`.

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Lightweight;

namespace CompositeKeyOrdering
{

/// A record whose two key columns are deliberately *both* `int32_t`, so a transposed argument pair
/// still compiles and still binds - the failure is a wrong row, not a type error.
struct OrderedKey
{
    static constexpr std::string_view TableName = "CkOrdered";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "key_first" }> keyFirst {};
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "key_second" }> keySecond {};
    Field<std::optional<SqlAnsiString<20>>, SqlRealName { "payload" }> payload {};
};

/// The same table, mapped with the two key members declared in the *opposite* order. Nothing about the
/// database changes; only the C++ declaration order does. Used to prove that declaration order - not
/// column order in the table - is what drives predicate order.
struct ReversedKey
{
    static constexpr std::string_view TableName = "CkOrdered";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "key_second" }> keySecond {};
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "key_first" }> keyFirst {};
    Field<std::optional<SqlAnsiString<20>>, SqlRealName { "payload" }> payload {};
};

/// Creates the shared fixture table and three rows that make every ordering mistake observable.
///
/// The rows are chosen so that (1, 2) and (2, 1) both exist and carry different payloads: a
/// transposed bind therefore returns a row rather than nothing, which is exactly the silent failure
/// being guarded against.
void CreateOrderingFixture(SqlStatement& stmt)
{
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkOrdered" (
                                     "key_first" INT NOT NULL,
                                     "key_second" INT NOT NULL,
                                     "payload" VARCHAR(20) NULL,
                                     PRIMARY KEY ("key_first", "key_second")
                                 ))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkOrdered" VALUES (1, 2, 'one-two'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkOrdered" VALUES (2, 1, 'two-one'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkOrdered" VALUES (3, 3, 'three-three'))");
}

} // namespace CompositeKeyOrdering

using CompositeKeyOrdering::CreateOrderingFixture;
using CompositeKeyOrdering::OrderedKey;
using CompositeKeyOrdering::ReversedKey;

TEST_CASE_METHOD(SqlTestFixture, "Composite key arguments bind in member declaration order", "[CompositeKey][ordering]")
{
    // The contract the loader must honour: argument N goes to the Nth `PrimaryKey` member, counting in
    // declaration order.
    auto stmt = SqlStatement {};
    CreateOrderingFixture(stmt);

    auto dm = DataMapper {};

    // keyFirst is declared first, so the first argument is matched against it.
    auto const oneTwo = dm.QuerySingle<OrderedKey>(1, 2);
    REQUIRE(oneTwo.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    CHECK(oneTwo->keyFirst.Value() == 1);
    CHECK(oneTwo->keySecond.Value() == 2);
    REQUIRE(oneTwo->payload.Value().has_value());
    CHECK(oneTwo->payload.Value().value() == "one-two");
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Transposed composite key arguments silently select a different row",
                 "[CompositeKey][ordering]")
{
    // The failure mode that makes the ordering question a correctness issue rather than a style one.
    // Swapping the two arguments is well-formed, binds cleanly, and returns the *other* row.
    //
    // This is why `CompositeForeignKey`'s loader must not rely on the caller (or the generator)
    // happening to list its connections in the parent's key order.
    auto stmt = SqlStatement {};
    CreateOrderingFixture(stmt);

    auto dm = DataMapper {};

    auto const transposed = dm.QuerySingle<OrderedKey>(2, 1);
    REQUIRE(transposed.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    // Not "no such row" - a real, wrong row.
    REQUIRE(transposed->payload.Value().has_value());
    CHECK(transposed->payload.Value().value() == "two-one");
    CHECK(transposed->keyFirst.Value() == 2);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Predicate order follows C++ member order, not table column order",
                 "[CompositeKey][ordering]")
{
    // `ReversedKey` maps the same table with its key members declared the other way round. If
    // predicate order were driven by the table, both records would need the same argument order; it is
    // driven by the C++ declaration, so they need opposite orders to reach the same row.
    //
    // Consequence for the design: the loader cannot derive argument order from the schema. It has to
    // derive it from the parent record's member order - which is what the `Into` member pointers in
    // each `Connection` give access to.
    auto stmt = SqlStatement {};
    CreateOrderingFixture(stmt);

    auto dm = DataMapper {};

    // Reaching the row (key_first=1, key_second=2) through ReversedKey means passing key_second first.
    auto const viaReversed = dm.QuerySingle<ReversedKey>(2, 1);
    REQUIRE(viaReversed.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    CHECK(viaReversed->keyFirst.Value() == 1);
    CHECK(viaReversed->keySecond.Value() == 2);
    REQUIRE(viaReversed->payload.Value().has_value());
    CHECK(viaReversed->payload.Value().value() == "one-two");
    // NOLINTEND(bugprone-unchecked-optional-access)

    // ...and the same values in the other order reach the other row, confirming the asymmetry.
    auto const viaReversedSwapped = dm.QuerySingle<ReversedKey>(1, 2);
    REQUIRE(viaReversedSwapped.has_value());
    REQUIRE(viaReversedSwapped->payload.Value().has_value());        // NOLINT(bugprone-unchecked-optional-access)
    CHECK(viaReversedSwapped->payload.Value().value() == "two-one"); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture, "A composite key with equal parts is order-insensitive", "[CompositeKey][ordering]")
{
    // The degenerate case worth pinning: when both key values are equal, ordering cannot be observed.
    // Any test that only used such a row would pass regardless of a transposition bug - which is why
    // the fixture above deliberately contains (1, 2) and (2, 1).
    auto stmt = SqlStatement {};
    CreateOrderingFixture(stmt);

    auto dm = DataMapper {};

    auto const threeThree = dm.QuerySingle<OrderedKey>(3, 3);
    REQUIRE(threeThree.has_value());
    REQUIRE(threeThree->payload.Value().has_value());            // NOLINT(bugprone-unchecked-optional-access)
    CHECK(threeThree->payload.Value().value() == "three-three"); // NOLINT(bugprone-unchecked-optional-access)
}
