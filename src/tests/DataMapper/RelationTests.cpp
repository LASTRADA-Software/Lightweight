// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <deque>
#include <set>
#include <string_view>

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

// TODO: Get this to work
// std::ostream& operator<<(std::ostream& os, RecordWithStorageFields auto const& record)
// {
//     return os << DataMapper::Inspect(record);
// }

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

    REQUIRE(!actualEmail1.user.IsLoaded());
    CHECK(actualEmail1.user->id == user.id);
    REQUIRE(actualEmail1.user.IsLoaded());
    CHECK(actualEmail1.user->name == user.name);

    actualEmail1.user.Unload();
    REQUIRE(!actualEmail1.user.IsLoaded());

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
                                    CONSTRAINT FK_user_id FOREIGN KEY ("user_id") REFERENCES "User"("id")
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

    auto actualEmail1 = dm.QuerySingleWithoutRelationAutoLoading<Email>(email1.id).value();

    CHECK(actualEmail1.address == email1.address);
    REQUIRE(!actualEmail1.user.IsLoaded());

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
    dm.Create(johnDoe);

    auto email1 = Email { .id = SqlGuid::Create(), .address = "john@doe.com", .user = johnDoe };
    dm.Create(email1);

    auto email2 = Email { .id = SqlGuid::Create(), .address = "john2@doe.com", .user = johnDoe };
    dm.Create(email2);

    // Create some other users
    auto const janeDoeID = dm.CreateExplicit(User { .id = SqlGuid::Create(), .name = "Jane Doe" });
    dm.CreateExplicit(Email { .id = SqlGuid::Create(), .address = "john3@doe.com", .user = janeDoeID });
    auto const jimDoeID = dm.CreateExplicit(User { .id = SqlGuid::Create(), .name = "Jim Doe" });
    dm.CreateExplicit(Email { .id = SqlGuid::Create(), .address = "john3@doe.com", .user = jimDoeID });

    SECTION("Count")
    {
        REQUIRE(johnDoe.emails.Count() == 2);
    }

    SECTION("At")
    {
        auto const& email1Retrieved = johnDoe.emails.At(0);
        auto const& email2Retrieved = johnDoe.emails.At(1);
        auto const emailsSet = std::set<Email> { email1Retrieved, email2Retrieved };
        CHECK(emailsSet.size() == 2);
        CHECK(emailsSet.contains(email1));
        CHECK(emailsSet.contains(email2));
    }

    SECTION("Each")
    {
        auto collectedEmails = std::set<Email> {};
        johnDoe.emails.Each([&](Email const& email) {
            INFO("Email: " << DataMapper::Inspect(email));
            collectedEmails.emplace(email);
        });

        CHECK(collectedEmails.size() == 2);
        CHECK(collectedEmails.contains(email1));
        CHECK(collectedEmails.contains(email2));
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
        REQUIRE(!supplier1.accountHistory.IsLoaded());
        dm.LoadRelations(supplier1);
        REQUIRE(supplier1.accountHistory.IsLoaded());

        CHECK(supplier1.accountHistory.Record() == accountHistory1);
    }

    SECTION("Auto loading")
    {
        dm.ConfigureRelationAutoLoading(supplier1);

        REQUIRE(!supplier1.accountHistory.IsLoaded());
        CHECK(supplier1.accountHistory.Record() == accountHistory1);
        REQUIRE(supplier1.accountHistory.IsLoaded());
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
        auto accountHistory = AccountHistory { .credit_rating = 90 + i, .account = account1 };
        dm.Create(accountHistory);
    }

    SECTION("Query multiple with relation wuthout auto loading")
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

TEST_CASE_METHOD(SqlTestFixture, "HasManyThrough", "[DataMapper][relations]")
{
    auto dm = DataMapper {};

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
                                    CONSTRAINT FK_physician_id FOREIGN KEY ("physician_id") REFERENCES "Physician"("id"),
                                    CONSTRAINT FK_patient_id FOREIGN KEY ("patient_id") REFERENCES "Patient"("id")
                                    );)"));
    }
}

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

static_assert(std::same_as<typename BelongsTo<Member(AliasedRecord::id)>::ReferencedRecord, AliasedRecord>);
static_assert(std::same_as<typename BelongsTo<Member(AliasedRecord::id)>::ValueType, uint64_t>);

std::ostream& operator<<(std::ostream& os, AliasedRecord const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "Table with aliased column names", "[DataMapper]")
{
    auto dm = DataMapper {};

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
                                    CONSTRAINT FK_record_id FOREIGN KEY ("record_id") REFERENCES "TheAliasedRecord"("pk")
                                    );)"));
    }

    SECTION("All")
    {
        auto const records = dm.Query<AliasedRecord>().All();
        CHECK(records.size() == 1);
        CHECK(records.at(0) == record);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
