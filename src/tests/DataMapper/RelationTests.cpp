// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"
#include "Lightweight/DataMapper/QueryBuilders.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;
using namespace std::string_literals;
using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

static_assert(HasPrimaryKey<User>);
static_assert(HasPrimaryKey<Email>);

static_assert(RecordPrimaryKeyIndex<User> == 0);
static_assert(std::same_as<RecordPrimaryKeyType<User>, SqlGuid>);

static_assert(RecordStorageFieldCount<User> == 2);
static_assert(RecordStorageFieldCount<Email> == 3);

std::ostream& operator<<(std::ostream& os, User const& record)
{
    return os << DataMapper::Inspect(record);
}

std::ostream& operator<<(std::ostream& os, Email const& record)
{
    return os << DataMapper::Inspect(record);
}

// Regression coverage for issue #515: a HasMany must locate its inverse BelongsTo by relationship
// *type*, never by member position. These two records deliberately declare their relation members
// at different indices: MisalignedDepartment::employees is member 2, while
// MisalignedEmployee::department is member 5. Before the fix, the generated WHERE clause took the
// column name from member 2 of the child record ("lastName") and the relation silently returned
// no rows at all.
struct MisalignedEmployee;

struct MisalignedDepartment
{
    static constexpr std::string_view TableName = "MisalignedDepartments";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {}; // 0
    Field<SqlAnsiString<40>> name {};                           // 1
    HasMany<MisalignedEmployee> employees {};                   // 2
};

struct MisalignedEmployee
{
    static constexpr std::string_view TableName = "MisalignedEmployees";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};                                                    // 0
    Field<SqlAnsiString<30>> firstName {};                                                                         // 1
    Field<SqlAnsiString<30>> lastName {};                                                                          // 2
    Field<int> salary {};                                                                                          // 3
    Field<std::optional<int>> age {};                                                                              // 4
    BelongsTo<Member(MisalignedDepartment::id), SqlRealName { "department_id" }, SqlNullable::Null> department {}; // 5
};

// Member 2 of the child record - the index the old positional lookup reused from the owner's
// HasMany - is a plain column. Picking it for the WHERE clause is exactly the reported bug.
static_assert(FieldNameAt<2, MisalignedEmployee> == "lastName"sv);

// The inverse BelongsTo is found by type, at its own index - not at the HasMany's index.
static_assert(InverseBelongsToIndexOf<MisalignedDepartment, MisalignedEmployee> == 5);
static_assert(InverseBelongsToFieldNameOf<MisalignedDepartment, MisalignedEmployee> == "department_id"sv);

// The pre-existing, accidentally index-aligned User/Email pair must keep resolving to the same column.
static_assert(InverseBelongsToIndexOf<User, Email> == 2);
static_assert(InverseBelongsToFieldNameOf<User, Email> == "user_id"sv);

// A record holding two foreign keys into the *same* table. Auto-detection cannot pick between them -
// that is a compile error - so each HasMany names the foreign key column it belongs to.
struct Meeting;

struct Human
{
    static constexpr std::string_view TableName = "Humans";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    HasMany<Meeting, SqlRealName { "organizer_id" }> organizedMeetings {};
    HasMany<Meeting, SqlRealName { "attendee_id" }> attendedMeetings {};
};

struct Meeting
{
    static constexpr std::string_view TableName = "Meetings";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> topic {};
    BelongsTo<Member(Human::id), SqlRealName { "organizer_id" }> organizer {};
    BelongsTo<Member(Human::id), SqlRealName { "attendee_id" }> attendee {};
};

std::ostream& operator<<(std::ostream& os, Meeting const& record)
{
    return os << DataMapper::Inspect(record);
}

// Each HasMany resolves to its own foreign key, not merely to the first one declared.
static_assert(InverseBelongsToIndexOf<Human, Meeting, SqlRealName { "organizer_id" }> == 2);
static_assert(InverseBelongsToIndexOf<Human, Meeting, SqlRealName { "attendee_id" }> == 3);
static_assert(InverseBelongsToFieldNameOf<Human, Meeting, SqlRealName { "attendee_id" }> == "attendee_id"sv);

// A self-referential many-to-many: both foreign keys of the join record point at the same table, so
// both ends of the relationship have to be named.
struct Buddyship;

struct Buddy
{
    static constexpr std::string_view TableName = "Buddies";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    HasManyThrough<Buddy, Buddyship, SqlRealName { "a_id" }, SqlRealName { "b_id" }> buddies {};
};

struct Buddyship
{
    static constexpr std::string_view TableName = "Buddyships";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(Buddy::id), SqlRealName { "a_id" }> a {};
    BelongsTo<Member(Buddy::id), SqlRealName { "b_id" }> b {};
};

std::ostream& operator<<(std::ostream& os, Buddy const& record)
{
    return os << DataMapper::Inspect(record);
}

// A through-relationship whose three records have differently named primary keys at differing member
// indices. Resolving any of them by reusing another record's primary key index - as the through-
// relation loaders used to - names a column of the wrong table.
struct ShopOrder;
struct ShopOrderLine;

struct Shop
{
    static constexpr std::string_view TableName = "Shops";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "shop_key" }> shopKey {}; // 0
    Field<SqlAnsiString<20>> name {};                                                            // 1
    HasOneThrough<ShopOrderLine, ShopOrder> firstOrderLine {};                                   // 2
};

struct ShopOrder
{
    static constexpr std::string_view TableName = "ShopOrders";

    Field<SqlAnsiString<20>> reference {};                                // 0
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> order_key {};    // 1 - PK is NOT at index 0
    BelongsTo<Member(Shop::shopKey), SqlRealName { "shop_key" }> shop {}; // 2
};

struct ShopOrderLine
{
    static constexpr std::string_view TableName = "ShopOrderLines";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "line_key" }> lineKey {}; // 0
    Field<SqlAnsiString<20>> article {};                                                         // 1
    BelongsTo<Member(ShopOrder::order_key), SqlRealName { "order_key" }> order {};               // 2
};

// The three primary keys sit at different indices and carry different names; a positional lookup
// would silently produce "shop_key" where "order_key" is meant, and vice versa.
static_assert(RecordPrimaryKeyIndex<Shop> == 0);
static_assert(RecordPrimaryKeyIndex<ShopOrder> == 1);
static_assert(InverseBelongsToFieldNameOf<Shop, ShopOrder> == "shop_key"sv);
static_assert(InverseBelongsToFieldNameOf<ShopOrder, ShopOrderLine> == "order_key"sv);

namespace
{

/// Installs itself as the active SqlLogger for its lifetime and records every SQL statement seen.
///
/// Copying or moving would leave two objects believing they own the logger slot, so both are deleted
/// rather than suppressing the clang-tidy special-member-function warning.
class ScopedSqlQueryRecorder: public SqlLogger::Null
{
  public:
    ScopedSqlQueryRecorder()
    {
        SqlLogger::SetLogger(*this);
    }

    ScopedSqlQueryRecorder(ScopedSqlQueryRecorder const&) = delete;
    ScopedSqlQueryRecorder(ScopedSqlQueryRecorder&&) = delete;
    ScopedSqlQueryRecorder& operator=(ScopedSqlQueryRecorder const&) = delete;
    ScopedSqlQueryRecorder& operator=(ScopedSqlQueryRecorder&&) = delete;

    ~ScopedSqlQueryRecorder() override
    {
        SqlLogger::SetLogger(_previousLogger);
    }

    void OnPrepare(std::string_view const& query) override
    {
        _queries.emplace_back(query);
    }

    void OnExecuteDirect(std::string_view const& query) override
    {
        _queries.emplace_back(query);
    }

    /// The SQL statements recorded so far, in order.
    [[nodiscard]] std::vector<std::string> const& Queries() const noexcept
    {
        return _queries;
    }

  private:
    SqlLogger& _previousLogger = SqlLogger::GetLogger();
    std::vector<std::string> _queries;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "BelongsTo: assigning a bare foreign-key value marks the field modified",
                 "[DataMapper][relations][BelongsTo]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    auto original = User { .name = "Original" };
    auto target = User { .name = "Target" };
    dm.Create(original);
    dm.Create(target);

    auto email = Email { .address = "someone@example.com" };
    email.user = original;
    dm.Create(email);
    REQUIRE_FALSE(email.user.IsModified());

    // Assigning the referenced record's bare primary-key value must mark the field dirty, exactly
    // as Field<T>::operator=(S&&) does. Without a direct-value assignment overload this binds to
    // the converting constructor plus move-assignment, which copies the temporary's _modified
    // (false) over the target.
    email.user = target.id.Value();

    CHECK(email.user.IsModified());
    CHECK(email.user.Value() == target.id.Value());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "BelongsTo: Update() writes a foreign key assigned as a bare value",
                 "[DataMapper][relations][BelongsTo]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    auto userA = User { .name = "A" };
    auto userB = User { .name = "B" };
    dm.Create(userA);
    dm.Create(userB);

    auto email = Email { .address = "a@example.com" };
    email.user = userA;
    dm.Create(email);

    // Re-point the foreign key by bare value, then persist.
    email.user = userB.id.Value();
    dm.Update(email);

    auto const reloaded = dm.QuerySingle<Email>(email.id).value();
    CHECK(reloaded.user.Value() == userB.id.Value());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "HasMany resolves its inverse BelongsTo by type, not by member index",
                 "[DataMapper][relations][HasMany]")
{
    auto dm = DataMapper();
    dm.CreateTables<MisalignedDepartment, MisalignedEmployee>();

    auto engineering = MisalignedDepartment { .name = "Engineering" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(engineering);

    auto sales = MisalignedDepartment { .name = "Sales" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(sales);

    dm.CreateExplicit(MisalignedEmployee {
        .firstName = "Alice", .lastName = "Anders", .salary = 50'000, .age = 30, .department = engineering });
    dm.CreateExplicit(MisalignedEmployee {
        .firstName = "Bob", .lastName = "Brown", .salary = 60'000, .age = 40, .department = engineering });
    dm.CreateExplicit(
        MisalignedEmployee { .firstName = "Carol", .lastName = "Clark", .salary = 55'000, .age = 35, .department = sales });

    SECTION("Count")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(engineering.id).value();
        CHECK(department.employees.Count() == 2);
        CHECK_FALSE(department.employees.IsEmpty());

        auto other = dm.QuerySingle<MisalignedDepartment>(sales.id).value();
        CHECK(other.employees.Count() == 1);
    }

    SECTION("All")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(engineering.id).value();
        auto const& employees = department.employees.All();
        REQUIRE(employees.size() == 2);

        auto const lastNames = std::set<std::string> { std::string(employees[0]->lastName.Value()),
                                                       std::string(employees[1]->lastName.Value()) };
        CHECK(lastNames == std::set<std::string> { "Anders", "Brown" });

        for (auto const& employee: employees)
            CHECK(employee->department.Value().value() == engineering.id.Value());
    }

    SECTION("Each")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(engineering.id).value();
        auto collectedLastNames = std::set<std::string> {};
        department.employees.Each(
            [&](MisalignedEmployee const& employee) { collectedLastNames.emplace(employee.lastName.Value()); });
        CHECK(collectedLastNames == std::set<std::string> { "Anders", "Brown" });
    }

    SECTION("Range-based for loop")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(sales.id).value();
        auto collectedLastNames = std::set<std::string> {};
        for (auto const& employee: department.employees)
            collectedLastNames.emplace(employee->lastName.Value());
        CHECK(collectedLastNames == std::set<std::string> { "Clark" });
    }

    SECTION("LoadRelations")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(engineering.id).value();
        dm.LoadRelations(department);
        CHECK(department.employees.All().size() == 2);
    }

    SECTION("Emitted SQL filters on the foreign key column")
    {
        auto department = dm.QuerySingle<MisalignedDepartment>(engineering.id).value();

        auto recordedQueries = std::vector<std::string> {};
        {
            // Not const: the logger callbacks mutate the recorder through the non-const SqlLogger&
            // installed in its constructor, and modifying a const object is undefined behaviour.
            auto recorder = ScopedSqlQueryRecorder {};
            std::ignore = department.employees.Count();
            recordedQueries = recorder.Queries();
        }

        REQUIRE(!recordedQueries.empty());
        auto const& countQuery = recordedQueries.back();
        INFO("Recorded query: " << countQuery);
        CHECK(countQuery.contains(R"("department_id")"));
        CHECK_FALSE(countQuery.contains(R"("lastName")"));
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "HasMany distinguishes two foreign keys into the same table",
                 "[DataMapper][relations][HasMany]")
{
    auto dm = DataMapper();
    dm.CreateTables<Human, Meeting>();

    auto alice = Human { .name = "Alice" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(alice);

    auto bob = Human { .name = "Bob" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(bob);

    // Alice organizes two meetings and attends one; Bob attends the other two.
    dm.CreateExplicit(Meeting { .topic = "Planning", .organizer = alice, .attendee = bob });
    dm.CreateExplicit(Meeting { .topic = "Retro", .organizer = alice, .attendee = bob });
    dm.CreateExplicit(Meeting { .topic = "One-on-one", .organizer = bob, .attendee = alice });

    SECTION("Each relation counts only its own foreign key")
    {
        auto const aliceLoaded = dm.QuerySingle<Human>(alice.id).value();
        CHECK(aliceLoaded.organizedMeetings.Count() == 2);
        CHECK(aliceLoaded.attendedMeetings.Count() == 1);

        auto const bobLoaded = dm.QuerySingle<Human>(bob.id).value();
        CHECK(bobLoaded.organizedMeetings.Count() == 1);
        CHECK(bobLoaded.attendedMeetings.Count() == 2);
    }

    SECTION("Each relation loads the rows of its own foreign key")
    {
        auto aliceLoaded = dm.QuerySingle<Human>(alice.id).value();

        auto organized = std::set<std::string> {};
        for (auto const& meeting: aliceLoaded.organizedMeetings.All())
            organized.emplace(meeting->topic.Value());
        CHECK(organized == std::set<std::string> { "Planning", "Retro" });

        auto attended = std::set<std::string> {};
        for (auto const& meeting: aliceLoaded.attendedMeetings.All())
            attended.emplace(meeting->topic.Value());
        CHECK(attended == std::set<std::string> { "One-on-one" });
    }

    SECTION("Emitted SQL filters on the named foreign key column")
    {
        auto human = dm.QuerySingle<Human>(alice.id).value();

        auto recordedQueries = std::vector<std::string> {};
        {
            auto recorder = ScopedSqlQueryRecorder {};
            std::ignore = human.attendedMeetings.Count();
            recordedQueries = recorder.Queries();
        }

        REQUIRE(!recordedQueries.empty());
        auto const& countQuery = recordedQueries.back();
        INFO("Recorded query: " << countQuery);
        CHECK(countQuery.contains(R"("attendee_id")"));
        CHECK_FALSE(countQuery.contains(R"("organizer_id")"));
    }

    SECTION("LoadRelations fills both relations independently")
    {
        auto human = dm.QuerySingle<Human>(bob.id).value();
        dm.LoadRelations(human);
        CHECK(human.organizedMeetings.All().size() == 1);
        CHECK(human.attendedMeetings.All().size() == 2);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "HasManyThrough resolves a self-referential join record",
                 "[DataMapper][relations][HasManyThrough]")
{
    auto dm = DataMapper();
    dm.CreateTables<Buddy, Buddyship>();

    auto alice = Buddy { .name = "Alice" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(alice);
    auto bob = Buddy { .name = "Bob" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(bob);
    auto carol = Buddy { .name = "Carol" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(carol);

    // Alice knows Bob and Carol; Bob knows nobody in the "a" role.
    dm.CreateExplicit(Buddyship { .a = alice, .b = bob });
    dm.CreateExplicit(Buddyship { .a = alice, .b = carol });

    SECTION("Count follows the named direction")
    {
        auto const aliceLoaded = dm.QuerySingle<Buddy>(alice.id).value();
        CHECK(aliceLoaded.buddies.Count() == 2);

        auto const bobLoaded = dm.QuerySingle<Buddy>(bob.id).value();
        CHECK(bobLoaded.buddies.Count() == 0);
    }

    SECTION("All returns the other side of the relationship, never the record itself")
    {
        auto aliceLoaded = dm.QuerySingle<Buddy>(alice.id).value();

        auto names = std::set<std::string> {};
        for (auto const& buddy: aliceLoaded.buddies.All())
            names.emplace(buddy->name.Value());

        CHECK(names == std::set<std::string> { "Bob", "Carol" });
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Through-relations resolve each primary key on its own record",
                 "[DataMapper][relations][HasOneThrough]")
{
    auto dm = DataMapper();
    dm.CreateTables<Shop, ShopOrder, ShopOrderLine>();

    auto shop = Shop { .name = "Corner" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(shop);

    auto order = ShopOrder { .reference = "ORD-1", .shop = shop };
    dm.Create<DataMapperOptions { .loadRelations = false }>(order);

    dm.CreateExplicit(ShopOrderLine { .article = "Widget", .order = order });

    auto const shopLoaded = dm.QuerySingle<Shop>(shop.shopKey).value();
    CHECK(shopLoaded.firstOrderLine.Record().article.Value() == "Widget");
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsTo", "[DataMapper][relations]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    auto email1 = Email { .id = SqlGuid::Create(), .address = "john@doe.com", .user = user };
    dm.Create(email1);

    dm.CreateExplicit(Email { .id = SqlGuid::Create(), .address = "john2@doe.com", .user = user });

    auto actualEmail1 = dm.QuerySingle<Email>(email1.id).value();
    CHECK(actualEmail1 == email1);
    dm.ConfigureRelationAutoLoading(actualEmail1);

    CHECK(actualEmail1.user->id == user.id);
    CHECK(actualEmail1.user->name == user.name);

    if (dm.Connection().ServerType() == SqlServerType::SQLITE)
    {
        CHECK(NormalizeText(dm.CreateTableString<User>(dm.Connection().ServerType()))
              == NormalizeText(R"(CREATE TABLE "User" (
                                    "id" GUID NOT NULL,
                                    "name" VARCHAR(30) NOT NULL,
                                    PRIMARY KEY ("id")
                                    );)"));
        CHECK(NormalizeText(dm.CreateTableString<Email>(dm.Connection().ServerType()))
              == NormalizeText(R"(CREATE TABLE "Email" (
                                    "id" GUID NOT NULL,
                                    "address" VARCHAR(30) NOT NULL,
                                    "user_id" GUID,
                                    PRIMARY KEY ("id"),
                                    CONSTRAINT "FK_Email_user_id" FOREIGN KEY ("user_id") REFERENCES "User"("id")
                                    );)"));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsTo do not load", "[DataMapper][relations]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    auto email1 = Email { .id = SqlGuid::Create(), .address = "john@doe.com", .user = user };
    dm.Create(email1);

    auto actualEmail1 = dm.QuerySingle<Email, DataMapperOptions { .loadRelations = false }>(email1.id).value();

    CHECK(actualEmail1.address == email1.address);

    // The following test works locally but seems to fail on GitHub Actions with SIGSEGV
    if (!IsGithubActions())
    {
        REQUIRE_THROWS_AS(actualEmail1.user->name.Value(), SqlRequireLoadedError);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "HasMany", "[DataMapper][relations]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    // Create user John with 2 email addresses
    auto johnDoe = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(johnDoe);

    auto email1 = Email { .id = SqlGuid::Create(), .address = "john@doe.com", .user = johnDoe };
    dm.Create<DataMapperOptions { .loadRelations = false }>(email1);

    auto email2 = Email { .id = SqlGuid::Create(), .address = "john2@doe.com", .user = johnDoe };
    dm.Create<DataMapperOptions { .loadRelations = false }>(email2);

    // Create some other users
    auto const janeDoeID = dm.CreateExplicit(User { .id = SqlGuid::Create(), .name = "Jane Doe" });
    dm.CreateExplicit(Email { .id = SqlGuid::Create(), .address = "john3@doe.com", .user = janeDoeID });
    auto const jimDoeID = dm.CreateExplicit(User { .id = SqlGuid::Create(), .name = "Jim Doe" });
    dm.CreateExplicit(Email { .id = SqlGuid::Create(), .address = "john3@doe.com", .user = jimDoeID });

    SECTION("Load has many")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();

        CHECK(getUser.emails.Count() == 2);
        auto& emails = getUser.emails.All();

        auto const expectedIds = std::set<SqlGuid> { email1.id.Value(), email2.id.Value() };
        auto const actualIds = std::set<SqlGuid> { emails[0]->id.Value(), emails[1]->id.Value() };
        CHECK(actualIds == expectedIds);

        for (auto const& email: emails)
        {
            CHECK(email->user->id.Value() == getUser.id.Value());
            CHECK(email->user->name.Value() == getUser.name.Value());
        }
    }

    SECTION("Count")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        CHECK(getUser.emails.Count() == 2);
    }

    SECTION("IsEmpty - user with emails")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        CHECK_FALSE(getUser.emails.IsEmpty());
    }

    SECTION("IsEmpty - user without emails")
    {
        auto emptyUser = User { .id = SqlGuid::Create(), .name = "No Emails" };
        dm.Create<DataMapperOptions { .loadRelations = false }>(emptyUser);
        auto loadedUser = dm.QuerySingle<User>(emptyUser.id).value();
        CHECK(loadedUser.emails.IsEmpty());
        CHECK(loadedUser.emails.Count() == 0);
    }

    SECTION("At")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 2);
        auto const returnedIds = std::set<SqlGuid> { getUser.emails.At(0).id.Value(), getUser.emails.At(1).id.Value() };
        auto const returnedAddresses = std::set<std::string> { std::string(getUser.emails.At(0).address.Value()),
                                                               std::string(getUser.emails.At(1).address.Value()) };
        CHECK(returnedIds == std::set<SqlGuid> { email1.id.Value(), email2.id.Value() });
        CHECK(returnedAddresses == std::set<std::string> { "john@doe.com", "john2@doe.com" });
    }

    SECTION("operator[]")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 2);
        auto const returnedIds = std::set<SqlGuid> { getUser.emails[0].id.Value(), getUser.emails[1].id.Value() };
        auto const returnedAddresses = std::set<std::string> { std::string(getUser.emails[0].address.Value()),
                                                               std::string(getUser.emails[1].address.Value()) };
        CHECK(returnedIds == std::set<SqlGuid> { email1.id.Value(), email2.id.Value() });
        CHECK(returnedAddresses == std::set<std::string> { "john@doe.com", "john2@doe.com" });
    }

    SECTION("Each")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        auto collectedIds = std::vector<SqlGuid> {};
        getUser.emails.Each([&](Email const& email) { collectedIds.push_back(email.id.Value()); });
        REQUIRE(collectedIds.size() == 2);
        CHECK(std::ranges::find(collectedIds, email1.id.Value()) != collectedIds.end());
        CHECK(std::ranges::find(collectedIds, email2.id.Value()) != collectedIds.end());
    }

    SECTION("Range-based for loop")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        auto collectedIds = std::vector<SqlGuid> {};
        for (auto const& emailPtr: getUser.emails)
            collectedIds.push_back(emailPtr->id.Value());
        REQUIRE(collectedIds.size() == 2);
        CHECK(std::ranges::find(collectedIds, email1.id.Value()) != collectedIds.end());
        CHECK(std::ranges::find(collectedIds, email2.id.Value()) != collectedIds.end());
    }

    // The const accessors call RequireLoaded(), which is non-const. Every one of these overloads
    // used to be uncallable: naming any of them on a const HasMany was a hard compile error, and no
    // test instantiated them, so the breakage went unnoticed. Accessing them through a const
    // reference is the regression guard — if the const_cast in HasMany::All() and friends is
    // dropped again, this section fails to compile.
    SECTION("Const accessors are callable")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        auto const& constEmails = getUser.emails;

        REQUIRE(constEmails.All().size() == 2);
        // At() and operator[] dereference the stored pointer and hand back the record itself,
        // whereas All() exposes the underlying pointer list.
        CHECK(constEmails.At(0).id.Value() == constEmails.All()[0]->id.Value());
        CHECK(constEmails[0].id.Value() == constEmails.All()[0]->id.Value());

        // Iterating the const reference binds HasMany's const begin()/end() overloads, which is
        // the point of this section - a range-based for over `constEmails` resolves to exactly
        // those, so no explicit iterator loop is needed.
        auto collectedIds = std::vector<SqlGuid> {};
        for (auto const& emailPtr: constEmails)
            collectedIds.push_back(emailPtr->id.Value());
        REQUIRE(collectedIds.size() == 2);
        CHECK(std::ranges::find(collectedIds, email1.id.Value()) != collectedIds.end());
        CHECK(std::ranges::find(collectedIds, email2.id.Value()) != collectedIds.end());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "HasMany - Connected data mutations", "[DataMapper][relations][HasMany]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    auto johnDoe = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(johnDoe);

    auto email1 = Email { .id = SqlGuid::Create(), .address = "john@doe.com", .user = johnDoe };
    dm.Create<DataMapperOptions { .loadRelations = false }>(email1);

    auto email2 = Email { .id = SqlGuid::Create(), .address = "john2@doe.com", .user = johnDoe };
    dm.Create<DataMapperOptions { .loadRelations = false }>(email2);

    SECTION("Adding an email is reflected when re-querying the user")
    {
        auto email3 = Email { .id = SqlGuid::Create(), .address = "john3@doe.com", .user = johnDoe };
        dm.Create<DataMapperOptions { .loadRelations = false }>(email3);

        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 3);

        auto collectedIds = std::vector<SqlGuid> {};
        getUser.emails.Each([&](Email const& email) { collectedIds.push_back(email.id.Value()); });
        CHECK(std::ranges::find(collectedIds, email1.id.Value()) != collectedIds.end());
        CHECK(std::ranges::find(collectedIds, email2.id.Value()) != collectedIds.end());
        CHECK(std::ranges::find(collectedIds, email3.id.Value()) != collectedIds.end());
    }

    SECTION("Deleting an email is reflected when re-querying the user")
    {
        dm.Delete(email1);

        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 1);
        CHECK(getUser.emails.At(0).id.Value() == email2.id.Value());
        CHECK(getUser.emails.At(0).address.Value() == email2.address.Value());
    }

    SECTION("Updating an email address is reflected when re-querying the user")
    {
        email1.address = "updated@doe.com";
        dm.Update(email1);

        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 2);

        bool foundUpdated = false;
        bool foundOriginal = false;
        getUser.emails.Each([&](Email const& email) {
            if (email.id.Value() == email1.id.Value())
                foundUpdated = (email.address.Value() == email1.address.Value());
            if (email.id.Value() == email2.id.Value())
                foundOriginal = (email.address.Value() == email2.address.Value());
        });
        CHECK(foundUpdated);
        CHECK(foundOriginal);
    }

    SECTION("Emplace replaces the in-memory collection without querying the database")
    {
        auto getUser = dm.QuerySingle<User>(johnDoe.id).value();
        REQUIRE(getUser.emails.Count() == 2);

        HasMany<Email>::ReferencedRecordList singleItem;
        singleItem.emplace_back(std::make_shared<Email>(email1));
        getUser.emails.Emplace(std::move(singleItem));

        CHECK(getUser.emails.Count() == 1);
        CHECK(getUser.emails.At(0).id.Value() == email1.id.Value());
        CHECK(getUser.emails.At(0).address.Value() == email1.address.Value());
    }
}

struct Suppliers;
struct Account;
struct AccountHistory;

struct Suppliers
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};

    // TODO: HasOne<Account> account;
    HasOneThrough<AccountHistory, Account> accountHistory {};
};

std::ostream& operator<<(std::ostream& os, Suppliers const& record)
{
    return os << DataMapper::Inspect(record);
}

struct Account
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> iban {};
    BelongsTo<Member(Suppliers::id)> supplier {};

    constexpr std::weak_ordering operator<=>(Account const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, Account const& record)
{
    return os << DataMapper::Inspect(record);
}

struct AccountHistory
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> credit_rating {};
    BelongsTo<Member(Account::id)> account {};

    constexpr std::weak_ordering operator<=>(AccountHistory const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, AccountHistory const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "HasOneThrough", "[DataMapper][relations]")
{
    auto dm = DataMapper();

    dm.CreateTables<Suppliers, Account, AccountHistory>();

    auto supplier1 = Suppliers { .name = "Supplier 1" };
    dm.Create(supplier1);

    auto account1 = Account { .iban = "DE89370400440532013000", .supplier = supplier1 };
    dm.Create(account1);

    auto accountHistory1 = AccountHistory { .credit_rating = 100, .account = account1 };
    dm.Create(accountHistory1);

    SECTION("Explicit loading")
    {
        dm.LoadRelations(supplier1);

        CHECK(supplier1.accountHistory.Record() == accountHistory1);
    }

    SECTION("Auto loading")
    {
        // Use a freshly queried record so the relation starts unloaded and the
        // access below goes through the LoadHasOneThroughByPK auto-loader.
        auto supplier = dm.QuerySingle<Suppliers>(supplier1.id.Value()).value();
        dm.ConfigureRelationAutoLoading(supplier);

        CHECK(supplier.accountHistory.Record() == accountHistory1);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsToChain", "[DataMapper][relations]")
{
    auto dm = DataMapper();

    dm.CreateTables<Suppliers, Account, AccountHistory>();

    auto supplier1 = Suppliers { .name = "Supplier 1" };
    dm.Create(supplier1);

    auto account1 = Account { .iban = "DE89370400440532013000", .supplier = supplier1 };
    dm.Create(account1);

    auto accountHistory1 = AccountHistory { .credit_rating = 100, .account = account1 };
    dm.Create(accountHistory1);

    SECTION("Query single with relation auto loading")
    {
        auto queriedAccountHistory = dm.QuerySingle<AccountHistory>(accountHistory1.id).value();
        REQUIRE(queriedAccountHistory.account.Value() == account1.id.Value());
        REQUIRE(queriedAccountHistory.account->id.Value() == account1.id.Value());
        REQUIRE(queriedAccountHistory.account->supplier->id.Value() == supplier1.id.Value());
        REQUIRE(queriedAccountHistory.account->supplier->name.Value() == supplier1.name.Value());
    }

    SECTION("Query single with relation auto loading and unique_ptr ")
    {
        auto queriedAccountHistory =
            std::make_unique<AccountHistory>(dm.QuerySingle<AccountHistory>(accountHistory1.id).value());
        REQUIRE(queriedAccountHistory->account.Value() == account1.id.Value());
        REQUIRE(queriedAccountHistory->account->id.Value() == account1.id.Value());
        REQUIRE(queriedAccountHistory->account->supplier->id.Value() == supplier1.id.Value());
        REQUIRE(queriedAccountHistory->account->supplier->name.Value() == supplier1.name.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsToChainWithScope", "[DataMapper][relations]")
{
    SECTION("Query with relation auto loading in another scope ")
    {
        {
            auto accountHistory = []() {
                auto scopedDm = DataMapper();
                scopedDm.CreateTables<Suppliers, Account, AccountHistory>();
                auto supplier1 = Suppliers { .name = "Supplier 1" };
                scopedDm.Create(supplier1);
                auto account1 = Account { .iban = "DE89370400440532013000", .supplier = supplier1 };
                scopedDm.Create(account1);
                auto accountHistory1 = AccountHistory { .credit_rating = 100, .account = account1 };
                scopedDm.Create(accountHistory1);
                return scopedDm.QuerySingle<AccountHistory>(accountHistory1.id).value();
            }();
            REQUIRE(accountHistory.account.Value());

            REQUIRE(accountHistory.account->id.Value());
            REQUIRE(accountHistory.account->supplier->id.Value());
            REQUIRE(!accountHistory.account->supplier->name.Value().empty());
        }
    }

    SECTION("Query with relation auto loading in another scope and unique_ptr ")
    {
        {
            auto accountHistory = []() {
                auto scopedDm = DataMapper();
                scopedDm.CreateTables<Suppliers, Account, AccountHistory>();
                auto supplier1 = Suppliers { .name = "Supplier 1" };
                scopedDm.Create(supplier1);
                auto account1 = Account { .iban = "DE89370400440532013000", .supplier = supplier1 };
                scopedDm.Create(account1);
                auto accountHistory1 = AccountHistory { .credit_rating = 100, .account = account1 };
                scopedDm.Create(accountHistory1);
                return std::make_unique<AccountHistory>(scopedDm.QuerySingle<AccountHistory>(accountHistory1.id).value());
            }();
            REQUIRE(accountHistory->account.Value());

            REQUIRE(accountHistory->account->id.Value());
            REQUIRE(accountHistory->account->supplier->id.Value());
            REQUIRE(!accountHistory->account->supplier->name.Value().empty());
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsTo loading of multiple records", "[DataMapper][relations]")
{
    auto dm = DataMapper();

    dm.CreateTables<Suppliers, Account, AccountHistory>();

    auto supplier1 = Suppliers { .name = "Supplier 1" };
    dm.Create(supplier1);

    auto account1 = Account { .iban = "DE89370400440532013001", .supplier = supplier1 };
    dm.Create(account1);
    for (int const i: std::views::iota(0, 10))
    {
        dm.CreateExplicit(AccountHistory { .credit_rating = 90 + i, .account = account1 });
    }

    SECTION("Query multiple without relation auto loading using Query builder directly")
    {
        auto allHistories = dm.Query<AccountHistory>()
                                .Where(FullyQualifiedNameOf<Member(AccountHistory::account)>, "=", account1.id.Value())
                                .All();
        REQUIRE(allHistories.size() == 10);
#if !defined(__cpp_lib_ranges_enumerate)
        int index { -1 };
        for (auto& history: allHistories)
        {
            ++index;
#else
        for (auto const& [index, history]: allHistories | std::views::enumerate)
        {
#endif
            dm.ConfigureRelationAutoLoading(history);
            CAPTURE(index);
            REQUIRE(history.account.Value() == account1.id.Value());
            REQUIRE(history.account->id.Value() == account1.id.Value());
            REQUIRE(history.credit_rating.Value() == 90 + static_cast<int>(index));
        }
    }

    SECTION("Query multiple with relation auto loading using Query builder directly")
    {
        auto allHistories = dm.Query<AccountHistory, DataMapperOptions { .loadRelations = true }>()
                                .Where(FullyQualifiedNameOf<Member(AccountHistory::account)>, "=", account1.id.Value())
                                .All();
        REQUIRE(allHistories.size() == 10);
#if !defined(__cpp_lib_ranges_enumerate)
        int index { -1 };
        for (auto& history: allHistories)
        {
            ++index;
#else
        for (auto const& [index, history]: allHistories | std::views::enumerate)
        {
#endif
            CAPTURE(index);
            REQUIRE(history.account.Value() == account1.id.Value());
            REQUIRE(history.account->id.Value() == account1.id.Value());
            REQUIRE(history.credit_rating.Value() == 90 + static_cast<int>(index));
        }
    }

    SECTION("Query multiple with relation auto loading")
    {
        auto allHistories = dm.Query<AccountHistory>(
            dm.FromTable(RecordTableName<AccountHistory>)
                .Select()
                .Fields<AccountHistory>()
                .Where(FullyQualifiedNameOf<Member(AccountHistory::account)>, "=", account1.id.Value())
                .All());
        REQUIRE(allHistories.size() == 10);
#if !defined(__cpp_lib_ranges_enumerate)
        int index { -1 };
        for (auto& history: allHistories)
        {
            ++index;
#else
        for (auto const& [index, history]: allHistories | std::views::enumerate)
        {
#endif
            CAPTURE(index);
            REQUIRE(history.account.Value() == account1.id.Value());
            REQUIRE(history.account->id.Value() == account1.id.Value());
            REQUIRE(history.credit_rating.Value() == 90 + static_cast<int>(index));
        }
    }
}

template <typename T>
std::set<T> MakeSetFromRange(std::ranges::range auto&& range)
{
#if defined(__cpp_lib_from_range)
    return std::set<T>(std::from_range, std::forward<decltype(range)>(range));
#else
    auto set = std::set<T> {};
    for (auto&& item: range)
        set.emplace(std::forward<decltype(item)>(item));
    return set;
#endif
}

#if (defined(_WIN32) || defined(_WIN64)) && !defined(__clang__)
#else
TEST_CASE_METHOD(SqlTestFixture, "HasManyThrough", "[DataMapper][relations]")
{
    auto dm = DataMapper();

    dm.CreateTables<Physician, Patient, Appointment>();

    Physician physician1;
    physician1.name = "Dr. House";
    dm.Create(physician1);

    Physician physician2;
    physician2.name = "Granny";
    dm.Create(physician2);

    Patient patient1;
    patient1.name = "Blooper";
    patient1.comment = "Prefers morning times";
    dm.Create(patient1);

    Patient patient2;
    patient2.name = "Valentine";
    patient2.comment = "always friendly";
    dm.Create(patient2);

    Appointment patient1Apointment1;
    patient1Apointment1.date = SqlDateTime::Now();
    patient1Apointment1.patient = patient1;
    patient1Apointment1.physician = physician2;
    patient1Apointment1.comment = "Patient is a bit nervous";
    dm.Create(patient1Apointment1);

    Appointment patient1Apointment2;
    patient1Apointment2.date = SqlDateTime::Now();
    patient1Apointment2.patient = patient1;
    patient1Apointment2.physician = physician1;
    patient1Apointment2.comment = "Patient is a bit nervous, again";
    dm.Create(patient1Apointment2);

    Appointment patient2Apointment1;
    patient2Apointment1.date = SqlDateTime::Now();
    patient2Apointment1.patient = patient2;
    patient2Apointment1.physician = physician1;
    patient2Apointment1.comment = "Patient is funny";
    dm.Create(patient2Apointment1);

    {
        auto const queriedCount = physician1.patients.Count();
        REQUIRE(queriedCount == 2);
        auto const physician1Patiens = std::set<Patient> { physician1.patients.At(0), physician1.patients.At(1) };
        CHECK(physician1Patiens.contains(patient1));
        CHECK(physician1Patiens.contains(patient2));
    }

    {
        CHECK(patient1.physicians.Count() == 2);
        auto const patient1Physicians = std::set<Physician> { patient1.physicians.At(0), patient1.physicians.At(1) };
        CHECK(patient1Physicians.contains(physician1));
        CHECK(patient1Physicians.contains(physician2));
    }

    CHECK(patient2.physicians.Count() == 1);
    CHECK(DataMapper::Inspect(patient2.physicians.At(0)) == DataMapper::Inspect(physician1));

    // Test Each() method
    {
        size_t numPatientsIterated = 0;
        std::deque<Patient> retrievedPatients;
        physician2.patients.Each([&](Patient const& patient) {
            REQUIRE(numPatientsIterated == 0);
            ++numPatientsIterated;
            INFO("Patient: " << DataMapper::Inspect(patient));
            retrievedPatients.emplace_back(patient);

            // Load the relations of the patient
            dm.ConfigureRelationAutoLoading(retrievedPatients.back());
        });
        auto const physician2Patients = MakeSetFromRange<Patient>(retrievedPatients);
        CHECK(physician2Patients.size() == 1);
        CHECK(physician2Patients.contains(patient1));

        // Check that the relations of the patient are loaded (on-demand, and correctly)
        Patient& patient = retrievedPatients.at(0);
        REQUIRE(patient.physicians.Count() == 2);
        auto const patient1Physicians = MakeSetFromRange<Physician>(
            patient.physicians.All() | std::views::transform([](std::shared_ptr<Physician>& p) { return std::move(*p); }));
        CHECK(patient1Physicians.size() == 2);
        CHECK(patient1Physicians.contains(physician1));
        CHECK(patient1Physicians.contains(physician2));
    }

    if (dm.Connection().ServerType() == SqlServerType::SQLITE)
    {
        REQUIRE(NormalizeText(dm.CreateTableString<Physician>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "Physician" (
                                    "id" GUID NOT NULL,
                                    "name" VARCHAR(30) NOT NULL,
                                    PRIMARY KEY ("id")
                                    );)"));

        REQUIRE(NormalizeText(dm.CreateTableString<Patient>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "Patient" (
                                    "id" GUID NOT NULL,
                                    "name" VARCHAR(30) NOT NULL,
                                    "comment" VARCHAR(30) NOT NULL,
                                    PRIMARY KEY ("id")
                                    );)"));
        REQUIRE(NormalizeText(dm.CreateTableString<Appointment>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "Appointment" (
                                    "id" GUID NOT NULL,
                                    "date" DATETIME NOT NULL,
                                    "comment" VARCHAR(80) NOT NULL,
                                    "physician_id" GUID,
                                    "patient_id" GUID,
                                    PRIMARY KEY ("id"),
                                    CONSTRAINT "FK_Appointment_physician_id" FOREIGN KEY ("physician_id") REFERENCES "Physician"("id"),
                                    CONSTRAINT "FK_Appointment_patient_id" FOREIGN KEY ("patient_id") REFERENCES "Patient"("id")
                                    );)"));
    }
}

// The accessors below are all thin wrappers over All(), but each is a separate non-template member
// of a class template, so each needs its own call to be emitted and instrumented. The explicit
// instantiation in InstantiationCoverageTests.cpp makes them *compile*; only calling them makes
// them *covered*. Split out from the test above so a failure names the accessor that broke.
TEST_CASE_METHOD(SqlTestFixture, "HasManyThrough: element access and iteration", "[DataMapper][relations][HasManyThrough]")
{
    auto dm = DataMapper();
    dm.CreateTables<Physician, Patient, Appointment>();

    Physician physician;
    physician.name = "Dr. House";
    dm.Create(physician);

    Patient patient1;
    patient1.name = "Blooper";
    patient1.comment = "Prefers morning times";
    dm.Create(patient1);

    Patient patient2;
    patient2.name = "Valentine";
    patient2.comment = "always friendly";
    dm.Create(patient2);

    for (auto* patient: { &patient1, &patient2 })
    {
        Appointment appointment;
        appointment.date = SqlDateTime::Now();
        appointment.patient = *patient;
        appointment.physician = physician;
        appointment.comment = "Checkup";
        dm.Create(appointment);
    }

    // Patient declares operator<=> but no operator==, so std::set is ordered-comparable but not
    // equality-comparable; check membership rather than comparing whole sets.
    auto const containsBoth = [&](std::set<Patient> const& actual) {
        return actual.size() == 2 && actual.contains(patient1) && actual.contains(patient2);
    };

    SECTION("IsEmpty reflects the relationship's contents")
    {
        CHECK_FALSE(physician.patients.IsEmpty());

        // A physician with no appointments at all resolves to an empty relationship.
        Physician lonely;
        lonely.name = "Dr. Nobody";
        dm.Create(lonely);
        dm.ConfigureRelationAutoLoading(lonely);
        CHECK(lonely.patients.IsEmpty());
        CHECK(lonely.patients.Count() == 0);
    }

    SECTION("operator[] retrieves records by index")
    {
        REQUIRE(physician.patients.Count() == 2);
        CHECK(containsBoth(std::set<Patient> { physician.patients[0], physician.patients[1] }));
    }

    SECTION("const overloads of At and operator[] retrieve the same records")
    {
        REQUIRE(physician.patients.Count() == 2);
        auto const& constPatients = physician.patients;
        CHECK(containsBoth(std::set<Patient> { constPatients.At(0), constPatients.At(1) }));
        CHECK(containsBoth(std::set<Patient> { constPatients[0], constPatients[1] }));
    }

    SECTION("At() throws when the index is out of bounds")
    {
        REQUIRE(physician.patients.Count() == 2);
        CHECK_THROWS_AS(physician.patients.At(2), std::out_of_range);
    }

    SECTION("range-based iteration visits every record")
    {
        auto collected = std::set<Patient> {};
        for (auto const& p: physician.patients)
            collected.emplace(*p);
        CHECK(containsBoth(collected));

        // The const begin()/end() pair is a distinct overload from the mutable one above.
        auto const& constPatients = physician.patients;
        CHECK(static_cast<std::size_t>(std::distance(constPatients.begin(), constPatients.end())) == 2);
    }

    SECTION("Reload() re-reads the relationship from the database")
    {
        REQUIRE(physician.patients.Count() == 2);

        Patient patient3;
        patient3.name = "Newcomer";
        patient3.comment = "walk-in";
        dm.Create(patient3);

        Appointment extra;
        extra.date = SqlDateTime::Now();
        extra.patient = patient3;
        extra.physician = physician;
        extra.comment = "Walk-in checkup";
        dm.Create(extra);

        // The cached count and record list still describe the pre-insert state.
        CHECK(physician.patients.Count() == 2);

        physician.patients.Reload();
        CHECK(physician.patients.Count() == 3);
    }

    SECTION("Emplace() replaces the loaded records without touching the database")
    {
        Physician detached;
        auto replacement = decltype(detached.patients)::ReferencedRecordList {};
        replacement.emplace_back(std::make_shared<Patient>(patient1));

        auto& emplaced = detached.patients.Emplace(std::move(replacement));
        CHECK(emplaced.size() == 1);
        CHECK(detached.patients.Count() == 1);
        CHECK_FALSE(detached.patients.IsEmpty());
        CHECK(DataMapper::Inspect(detached.patients.At(0)) == DataMapper::Inspect(patient1));
    }

    SECTION("A HasMany with no auto-loader reports SqlRequireLoadedError rather than terminating")
    {
        // `appointments` is the HasMany; `patients` above is a HasManyThrough, which already
        // guarded this. A hand-constructed record never went through ConfigureRelationAutoLoading,
        // so its loader holds an empty std::function. Calling it would be std::bad_function_call -
        // and because these accessors used to be noexcept, that would have been std::terminate
        // rather than an error the caller could handle. Both overloads must report it.
        Physician detached;
        Physician const& constDetached = detached;

        CHECK_THROWS_AS(detached.appointments.All(), SqlRequireLoadedError);
        CHECK_THROWS_AS(constDetached.appointments.All(), SqlRequireLoadedError);
        CHECK_THROWS_AS(detached.appointments.At(0), SqlRequireLoadedError);
        CHECK_THROWS_AS(detached.appointments.begin(), SqlRequireLoadedError);
        CHECK_THROWS_AS(constDetached.appointments.begin(), SqlRequireLoadedError);
        CHECK_THROWS_AS(constDetached.appointments.end(), SqlRequireLoadedError);

        // Count()/IsEmpty() guard the loader themselves and stay noexcept, so they must not throw.
        CHECK(detached.appointments.Count() == 0);
        CHECK(detached.appointments.IsEmpty());
    }
}

// NOTE: HasMany::RequireLoaded() guarantees _records is engaged on return, or throws trying:
// SqlRequireLoadedError when the relation never went through ConfigureRelationAutoLoading (the
// loader holds an empty std::function), otherwise whatever the loader itself raises. The accessors
// that call it therefore dereference _records unconditionally, with no unreachable fallback branch,
// and are deliberately not noexcept - a throw crossing a noexcept boundary would be std::terminate
// rather than an error the caller can catch.
#endif

struct AliasedRecord
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "pk" }> id {};
    Field<SqlAnsiString<30>, SqlRealName { "c1" }> name;
    Field<SqlAnsiString<30>, SqlRealName { "c2" }> comment;

    static constexpr std::string_view TableName = "TheAliasedRecord";

    constexpr std::weak_ordering operator<=>(AliasedRecord const& other) const = default;
};

struct BelongsToAliasedRecord
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(AliasedRecord::id), SqlRealName { "record_id" }> record;
};

static_assert(std::same_as<BelongsTo<Member(AliasedRecord::id)>::ReferencedRecord, AliasedRecord>);
static_assert(std::same_as<BelongsTo<Member(AliasedRecord::id)>::ValueType, uint64_t>);

std::ostream& operator<<(std::ostream& os, AliasedRecord const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "Table with aliased column names", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<AliasedRecord>();

    auto record = AliasedRecord { .name = "John Doe", .comment = "Hello, World!" };
    dm.Create(record);

    auto const queriedRecord = dm.QuerySingle<AliasedRecord>(record.id).value();
    CHECK(queriedRecord == record);

    auto const queriedRecords2 = dm.Query<AliasedRecord>().All();
    CHECK(queriedRecords2.size() == 1);
    auto const& queriedRecord2 = queriedRecords2.at(0);
    CHECK(queriedRecord2 == record);

    if (dm.Connection().ServerType() == SqlServerType::SQLITE)
    {
        REQUIRE(NormalizeText(dm.CreateTableString<AliasedRecord>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "TheAliasedRecord" (
                                    "pk" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                                    "c1" VARCHAR(30) NOT NULL,
                                    "c2" VARCHAR(30) NOT NULL
                                    );)"));
        REQUIRE(NormalizeText(dm.CreateTableString<BelongsToAliasedRecord>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "BelongsToAliasedRecord" (
                                    "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                                    "record_id" BIGINT,
                                    CONSTRAINT "FK_BelongsToAliasedRecord_record_id" FOREIGN KEY ("record_id") REFERENCES "TheAliasedRecord"("pk")
                                    );)"));
    }

    SECTION("All")
    {
        auto const records = dm.Query<AliasedRecord>().All();
        CHECK(records.size() == 1);
        CHECK(records.at(0) == record);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "BelongsTo Optinal records", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTables<User, NullableForeignKeyUser>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    auto nullableFKUser = NullableForeignKeyUser { .user = user };
    dm.Create(nullableFKUser);
    REQUIRE(nullableFKUser.user.Value().has_value());
    REQUIRE(nullableFKUser.user.Record().transform(Light::Unwrap).value().id == user.id);

    auto nullableFKUserNotSet = NullableForeignKeyUser {};
    dm.Create<Light::DataMapperOptions { .loadRelations = false }>(nullableFKUserNotSet);
    REQUIRE(!nullableFKUserNotSet.user.Value().has_value());
    REQUIRE(!nullableFKUserNotSet.user.Record().transform(Light::Unwrap).value_or(User {}).id.Value());
}

bool CheckFieldInEntityConstCorrectness(auto const& nullableFKUser)
{
    return static_cast<bool>(nullableFKUser.id.Value());
}

bool CheckBelongsToInEntityConstCorrectness(auto const& nullableFKUser)
{
    return nullableFKUser.user.Record().transform(Light::Unwrap).has_value();
}

TEST_CASE_METHOD(SqlTestFixture, "Entity const corectness", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTables<User, NullableForeignKeyUser>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    auto nullableFKUser = NullableForeignKeyUser { .user = user };
    dm.Create(nullableFKUser);

    REQUIRE(CheckFieldInEntityConstCorrectness(nullableFKUser));
    REQUIRE(CheckBelongsToInEntityConstCorrectness(nullableFKUser));
}

// ================================================================================================
// HasOneThrough state management — exercises the EmplaceRecord / Unload / operator-> paths that
// previous regression tests never reached.
// ================================================================================================

TEST_CASE("HasOneThrough: default-constructed reports not-loaded", "[HasOneThrough]")
{
    HasOneThrough<AccountHistory, Account> rel {};
    CHECK_FALSE(rel.IsLoaded());
}

TEST_CASE("HasOneThrough: EmplaceRecord makes IsLoaded true and Unload reverts it", "[HasOneThrough]")
{
    HasOneThrough<AccountHistory, Account> rel {};
    rel.EmplaceRecord(std::make_shared<AccountHistory>(AccountHistory { .credit_rating = 750 }));
    REQUIRE(rel.IsLoaded());
    CHECK(rel.Record().credit_rating.Value() == 750);

    rel.Unload();
    CHECK_FALSE(rel.IsLoaded());
}

TEST_CASE("HasOneThrough: operator-> forwards to the loaded record", "[HasOneThrough]")
{
    HasOneThrough<AccountHistory, Account> rel {};
    rel.EmplaceRecord(std::make_shared<AccountHistory>(AccountHistory { .credit_rating = 600 }));
    REQUIRE(rel.IsLoaded());
    CHECK(rel->credit_rating.Value() == 600);

    AccountHistory* asPtr = rel.operator->();
    REQUIRE(asPtr != nullptr);
    CHECK(asPtr->credit_rating.Value() == 600);

    HasOneThrough<AccountHistory, Account> const& constRel = rel;
    CHECK(constRel->credit_rating.Value() == 600);
}

TEST_CASE("HasOneThrough: operator* returns the loaded record by reference", "[HasOneThrough]")
{
    HasOneThrough<AccountHistory, Account> rel {};
    rel.EmplaceRecord(std::make_shared<AccountHistory>(AccountHistory { .credit_rating = 42 }));
    AccountHistory& deref = *rel;
    CHECK(deref.credit_rating.Value() == 42);
}

// ================================================================================================
// Regression tests for issue #517: members without storage (HasMany and friends) must be skipped
// by *every* column-enumerating code path. DataMapper::Update() used to fail to compile on such a
// record, and the fluent DataMapper::Query<T>() projected the relation member into the SELECT
// list, which the database rejected with "no such column".
// ================================================================================================

/// Records the SQL statements a test triggers, so a query can be asserted on its emitted text.
class ScopedQueryRecordingLogger final: public SqlLogger::Null
{
  public:
    ScopedQueryRecordingLogger()
    {
        SqlLogger::SetLogger(*this);
    }

    ScopedQueryRecordingLogger(ScopedQueryRecordingLogger const&) = delete;
    ScopedQueryRecordingLogger(ScopedQueryRecordingLogger&&) = delete;
    ScopedQueryRecordingLogger& operator=(ScopedQueryRecordingLogger const&) = delete;
    ScopedQueryRecordingLogger& operator=(ScopedQueryRecordingLogger&&) = delete;

    ~ScopedQueryRecordingLogger() override
    {
        SqlLogger::SetLogger(_previousLogger);
    }

    void OnPrepare(std::string_view const& query) override
    {
        _queries.emplace_back(query);
    }

    void OnExecuteDirect(std::string_view const& query) override
    {
        _queries.emplace_back(query);
    }

    /// The SQL statements recorded so far.
    [[nodiscard]] std::vector<std::string> const& Queries() const noexcept
    {
        return _queries;
    }

    /// Tests whether any recorded query contains @p needle.
    [[nodiscard]] bool AnyQueryContains(std::string_view needle) const
    {
        return std::ranges::any_of(_queries, [needle](std::string const& query) { return query.contains(needle); });
    }

  private:
    std::vector<std::string> _queries;
    SqlLogger& _previousLogger = SqlLogger::GetLogger();
};

struct Issue517Child;

struct Issue517Parent
{
    static constexpr std::string_view TableName = "Issue517Parent";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "id" }> id {};
    Field<SqlAnsiString<32>, SqlRealName { "name" }> name {};
    HasMany<Issue517Child> children {};
};

struct Issue517Child
{
    static constexpr std::string_view TableName = "Issue517Child";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "id" }> id {};
    Field<int, SqlRealName { "n" }> n {};
    BelongsTo<Member(Issue517Parent::id), SqlRealName { "parent_id" }> parent {};
};

static_assert(RecordStorageFieldCount<Issue517Parent> == 2);
static_assert(RecordMemberCount<Issue517Parent> == 3);

TEST_CASE_METHOD(SqlTestFixture, "Record with HasMany: Update", "[DataMapper][relations][HasMany][regression]")
{
    auto dm = DataMapper();
    dm.CreateTables<Issue517Parent, Issue517Child>();

    auto parent = Issue517Parent { .name = SqlAnsiString<32> { "acme" } };
    dm.Create(parent);

    dm.CreateExplicit(Issue517Child { .n = 1, .parent = parent.id.Value() });

    auto loaded = dm.QuerySingle<Issue517Parent>(parent.id.Value()).value();
    REQUIRE(loaded.name.Value() == "acme");

    loaded.name = SqlAnsiString<32> { "beta" };

    auto const recordedQueries = [&] {
        auto logger = ScopedQueryRecordingLogger {};
        dm.Update(loaded); // Used to not even compile (issue #517).
        return logger.Queries();
    }();

    // The relation member must not appear in the UPDATE statement.
    CHECK(std::ranges::none_of(
        recordedQueries, [](std::string const& query) { return query.contains("UPDATE") && query.contains("children"); }));

    auto const reloaded = dm.QuerySingle<Issue517Parent>(parent.id.Value()).value();
    CHECK(reloaded.id.Value() == parent.id.Value());
    CHECK(reloaded.name.Value() == "beta");
    CHECK(reloaded.children.Count() == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Record with HasMany: fluent Query", "[DataMapper][relations][HasMany][regression]")
{
    auto dm = DataMapper();
    dm.CreateTables<Issue517Parent, Issue517Child>();

    auto parent = Issue517Parent { .name = SqlAnsiString<32> { "acme" } };
    dm.Create(parent);
    dm.CreateExplicit(Issue517Parent { .name = SqlAnsiString<32> { "globex" } });

    SECTION("All")
    {
        auto logger = ScopedQueryRecordingLogger {};

        // Used to throw "no such column". ORDER BY, because a bare All() leaves the row order
        // unspecified and the ordering is incidental to what this checks.
        auto const records = dm.Query<Issue517Parent>().OrderBy(FieldNameOf<Member(Issue517Parent::id)>).All();

        CHECK_FALSE(logger.AnyQueryContains("children"));
        REQUIRE(records.size() == 2);
        CHECK(records[0].name.Value() == "acme");
        CHECK(records[1].name.Value() == "globex");
    }

    SECTION("First and Count")
    {
        auto const first = dm.Query<Issue517Parent>().Where(FieldNameOf<Member(Issue517Parent::name)>, "=", "acme").First();
        REQUIRE(first.has_value());
        CHECK(first->id.Value() == parent.id.Value());
        CHECK(dm.Query<Issue517Parent>().Count() == 2);
    }

    SECTION("Query builder Fields<>() skips the relation member")
    {
        auto const sql =
            dm.Connection().Query(RecordTableName<Issue517Parent>).Select().Fields<Issue517Parent>().All().ToSql();
        CHECK_FALSE(sql.contains("children"));
        CHECK(sql.contains("\"name\""));
    }
}

// Same as above, but with the relation member in the *middle* of the record, so that skipping it
// must not shift the column indices of the members following it. The dynamic string field forces
// the GetAllColumns() code path on MS SQL Server (bound columns may need to grow there).
struct Issue517MidChild;

struct Issue517MidParent
{
    static constexpr std::string_view TableName = "Issue517MidParent";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    HasMany<Issue517MidChild> children {};
    Field<std::string> name {};
    Field<int> counter {};
};

struct Issue517MidChild
{
    static constexpr std::string_view TableName = "Issue517MidChild";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(Issue517MidParent::id)> parent {};
};

TEST_CASE_METHOD(SqlTestFixture,
                 "Record with HasMany in the middle: Update and fluent Query",
                 "[DataMapper][relations][HasMany][regression]")
{
    auto dm = DataMapper();
    dm.CreateTables<Issue517MidParent, Issue517MidChild>();

    auto parent = Issue517MidParent { .name = "acme", .counter = 1 };
    dm.Create(parent);
    dm.CreateExplicit(Issue517MidChild { .parent = parent.id.Value() });

    auto loaded = dm.QuerySingle<Issue517MidParent>(parent.id.Value()).value();
    CHECK(loaded.name.Value() == "acme");
    CHECK(loaded.counter.Value() == 1);

    loaded.counter = 2;
    dm.Update(loaded);

    auto const records = dm.Query<Issue517MidParent>().All();
    REQUIRE(records.size() == 1);
    CHECK(records[0].id.Value() == parent.id.Value());
    CHECK(records[0].name.Value() == "acme");
    CHECK(records[0].counter.Value() == 2);
    CHECK(records[0].children.Count() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Record with HasMany in the middle: SqlRowIterator",
                 "[DataMapper][relations][HasMany][SqlRowIterator][regression]")
{
    auto dm = DataMapper();
    dm.CreateTables<Issue517MidParent, Issue517MidChild>();

    dm.CreateExplicit(Issue517MidParent { .name = "acme", .counter = 7 });

    // SqlRowIterator::begin() projects via Select().Fields<T>(), so operator*() must read by column
    // position too. Reading by member position used to not even compile here (HasMany has no
    // ValueType) and would otherwise have read `counter` out of the `name` column.
    auto rows = std::vector<Issue517MidParent> {};
    for (auto&& row: SqlRowIterator<Issue517MidParent>(dm.Connection()))
        rows.emplace_back(row);

    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name.Value() == "acme");
    CHECK(rows[0].counter.Value() == 7);
}

// A two-record JOIN projection where the *first* record carries a relation member. Fields<A, B>()
// emits one column per column-mapping member, so the second record starts after A's *columns*; an
// offset computed from RecordMemberCount would place B one column too far to the right and make every
// one of its fields read the neighbouring column (or run off the end of the result set — observed as
// `07009 Invalid Descriptor Index` on MS SQL Server).
struct Issue517JoinChild;

struct Issue517JoinParent
{
    static constexpr std::string_view TableName = "Issue517JoinParent";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    HasMany<Issue517JoinChild> children {};
    Field<SqlAnsiString<32>> name {};
};

struct Issue517JoinChild
{
    static constexpr std::string_view TableName = "Issue517JoinChild";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(Issue517JoinParent::id)> parent {};
    Field<SqlAnsiString<32>> title {};
};

// The projection is 2 (parent) + 3 (child) = 5 columns wide, not 3 + 3.
static_assert(RecordMemberCount<Issue517JoinParent> == 3);
static_assert(RecordColumnCount<Issue517JoinParent> == 2);
static_assert(RecordColumnCount<Issue517JoinChild> == 3);

TEST_CASE_METHOD(SqlTestFixture,
                 "Record with HasMany: two-record JOIN projection offset",
                 "[DataMapper][relations][HasMany][regression]")
{
    auto dm = DataMapper();
    dm.CreateTables<Issue517JoinParent, Issue517JoinChild>();

    auto parent = Issue517JoinParent { .name = SqlAnsiString<32> { "acme" } };
    dm.Create(parent);
    dm.CreateExplicit(Issue517JoinChild { .parent = parent.id.Value(), .title = SqlAnsiString<32> { "widget" } });

    auto const query = dm.FromTable(RecordTableName<Issue517JoinParent>)
                           .Select()
                           .Fields<Issue517JoinParent, Issue517JoinChild>()
                           .InnerJoin<Member(Issue517JoinChild::parent), Member(Issue517JoinParent::id)>()
                           .OrderBy(SqlQualifiedTableColumnName {
                               .tableName = RecordTableName<Issue517JoinChild>,
                               .columnName = FieldNameOf<Member(Issue517JoinChild::id)>,
                           })
                           .All();

    CHECK_FALSE(query.ToSql().contains("children"));

    // Used to read the second record starting one column too far right (relation member counted).
    auto const records = dm.Query<Issue517JoinParent, Issue517JoinChild>(query);

    REQUIRE(records.size() == 1);
    auto const& [loadedParent, loadedChild] = records[0];
    CHECK(loadedParent.id.Value() == parent.id.Value());
    CHECK(loadedParent.name.Value() == "acme");
    CHECK(loadedChild.parent.Value() == parent.id.Value());
    CHECK(loadedChild.title.Value() == "widget");
}

// NOLINTEND(bugprone-unchecked-optional-access)
