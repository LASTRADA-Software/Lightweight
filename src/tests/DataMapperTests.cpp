// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ostream>

std::ostream& operator<<(std::ostream& os, RecordId const& id)
{
    return os << id.value;
}

template <typename T, PrimaryKey IsPrimaryKeyValue>
std::ostream& operator<<(std::ostream& os, Field<std::optional<T>, IsPrimaryKeyValue> const& field)
{
    if (field.Value())
        return os << std::format("Field<{}> {{ {}, {} }}",
                                 Reflection::TypeName<T>,
                                 *field.Value(),
                                 field.IsModified() ? "modified" : "not modified");
    else
        return os << "NULL";
}

template <typename T, PrimaryKey IsPrimaryKeyValue>
std::ostream& operator<<(std::ostream& os, Field<T, IsPrimaryKeyValue> const& field)
{
    return os << std::format("Field<{}> {{ {}, {} }}",
                             Reflection::TypeName<T>,
                             field.Value(),
                             field.IsModified() ? "modified" : "not modified");
}

struct Person
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<SqlTrimmedFixedString<25>> name;
    Field<bool> is_active { true };
    Field<std::optional<int>> age;
};

// This is a test to only partially query a table row (a few columns)
struct PersonName
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<SqlTrimmedFixedString<25>> name;

    static constexpr std::string_view TableName = RecordTableName<Person>;
};

TEST_CASE_METHOD(SqlTestFixture, "CRUD", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create
    auto person = Person {};
    person.name = "John Doe";
    person.is_active = true;

    REQUIRE(person.id == 0);
    dm.Create(person);
    REQUIRE(person.id.Value() != 0);

    // Read (by primary key)
    auto po = dm.QuerySingle<Person>(person.id);
    auto p = po.value();
    CHECK(p.id == person.id);
    CHECK(p.name == person.name);
    CHECK(p.is_active == person.is_active);
    CHECK(p.age.Value().has_value() == false);

    // Update
    person.age = 42;
    person.is_active = false;
    dm.Update(person);

    po = dm.QuerySingle<Person>(person.id);
    p = po.value();
    CHECK(p.id == person.id);
    CHECK(p.name == person.name);
    CHECK(p.is_active == person.is_active);
    CHECK(p.age.Value().value_or(0) == 42);

    // Delete
    auto const numRowsAffected = dm.Delete(person);
    CHECK(numRowsAffected == 1);

    CHECK(!dm.QuerySingle<Person>(person.id));
}

TEST_CASE_METHOD(SqlTestFixture, "partial row retrieval", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto person = Person {};
    person.name = "John Doe";
    person.is_active = true;
    REQUIRE(person.id == 0);
    dm.Create(person);

    auto po = dm.QuerySingle<PersonName>(person.id);
    auto p = po.value();
    CHECK(p.name.Value() == person.name.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "iterate over database", "[SqlRowIterator]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    for (int i = 40; i <= 50; ++i)
    {
        auto person = Person {};
        person.name = "John";
        person.age = i;
        dm.Create(person);
    }

    auto stmt = SqlStatement { dm.Connection() };
    int age = 40;
    int id = 1;
    for (auto&& person: SqlRowIterator<Person>(stmt))
    {
        CHECK(person.name.Value() == "John");
        CHECK(person.age.Value() == age);
        CHECK(person.id.Value() == id);
        ++age;
        ++id;
    }
}

struct RecordWithDefaults
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<std::string> name1 { "John Doe" };
    Field<std::optional<std::string>> name2 { "John Doe" };
    Field<bool> boolean1 { true };
    Field<bool> boolean2 { false };
    Field<std::optional<int>> int1 { 42 };
    Field<std::optional<int>> int2 {};
};

struct RecordWithNoDefaults
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<std::string> name1;
    Field<std::optional<std::string>> name2;
    Field<bool> boolean1;
    Field<bool> boolean2;
    Field<std::optional<int>> int1;
    Field<std::optional<int>> int2;

    static constexpr std::string_view TableName = RecordTableName<RecordWithDefaults>;
};

TEST_CASE_METHOD(SqlTestFixture, "Create table with default values", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<RecordWithDefaults>();

    auto record = RecordWithDefaults {};
    dm.Create(record);

    auto const actual = dm.QuerySingle<RecordWithNoDefaults>(record.id).value();
    CHECK(actual.id == record.id);
    CHECK(actual.name1 == record.name1);
    CHECK(actual.boolean1 == record.boolean1);
    CHECK(actual.boolean2 == record.boolean2);
    CHECK(actual.int1 == record.int1);
    CHECK(actual.int2 == record.int2);
}

// =========================================================================================================

struct User;
struct Email;

struct User
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id {};
    Field<std::string> name {};

    HasMany<Email> emails {};
};

struct Email
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id {};
    Field<std::string> address {};
    BelongsTo<&User::id> user {};

    constexpr std::weak_ordering operator<=>(Email const& other) const = default;
};

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

    auto user = User { .name = "John Doe" };
    dm.Create(user);

    auto email1 = Email { .address = "john@doe.com", .user = user };
    dm.Create(email1);

    dm.CreateExplicit(Email { .address = "john2@doe.com", .user = user });

    auto actualEmail1 = dm.QuerySingle<Email>(email1.id).value();
    CHECK(actualEmail1 == email1);
    dm.ConfigureRelationAutoLoading(actualEmail1);

    REQUIRE(!actualEmail1.user.IsLoaded());
    CHECK(actualEmail1.user->id == user.id);
    REQUIRE(actualEmail1.user.IsLoaded());
    CHECK(actualEmail1.user->name == user.name);

    actualEmail1.user.Unload();
    REQUIRE(!actualEmail1.user.IsLoaded());
}

TEST_CASE_METHOD(SqlTestFixture, "HasMany", "[DataMapper][relations]")
{
    auto dm = DataMapper();
    dm.CreateTables<User, Email>();

    // Create user John with 2 email addresses
    auto johnDoe = User { .name = "John Doe" };
    dm.Create(johnDoe);

    auto email1 = Email { .address = "john@doe.com", .user = johnDoe };
    dm.Create(email1);

    auto email2 = Email { .address = "john2@doe.com", .user = johnDoe };
    dm.Create(email2);

    // Create some other users
    auto const janeDoeID = dm.CreateExplicit(User { .name = "Jane Doe" });
    dm.CreateExplicit(Email { .address = "john3@doe.com", .user = janeDoeID.value });
    auto const jimDoeID = dm.CreateExplicit(User { .name = "Jim Doe" });
    dm.CreateExplicit(Email { .address = "john3@doe.com", .user = jimDoeID.value });

    SECTION("Count")
    {
        REQUIRE(johnDoe.emails.Count() == 2);
    }

    SECTION("At")
    {
        CHECK(johnDoe.emails.At(0) == email1);
        CHECK(johnDoe.emails.At(1) == email2);
    }

    SECTION("Each")
    {
        auto collectedEmails = std::vector<Email> {};
        johnDoe.emails.Each([&](Email const& email) {
            INFO("Email: " << DataMapper::Inspect(email));
            collectedEmails.emplace_back(email);
        });
        CHECK(collectedEmails.size() == 2);
        CHECK(collectedEmails.at(0) == email1);
        CHECK(collectedEmails.at(1) == email2);
    }
}

struct Suppliers;
struct Account;
struct AccountHistory;

struct Suppliers
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id {};
    Field<std::string> name {};

    // TODO: HasOne<Account> account;
    HasOneThrough<AccountHistory, Account> accountHistory {};
};

std::ostream& operator<<(std::ostream& os, Suppliers const& record)
{
    return os << DataMapper::Inspect(record);
}

struct Account
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id {};
    Field<std::string> iban {};
    BelongsTo<&Suppliers::id> supplier {};

    constexpr std::weak_ordering operator<=>(Account const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, Account const& record)
{
    return os << DataMapper::Inspect(record);
}

struct AccountHistory
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id {};
    Field<int> credit_rating {};
    BelongsTo<&Account::id> account {};

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

struct Physician;
struct Appointment;
struct Patient;

struct Physician
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<std::string> name;
    HasMany<Appointment> appointments;
    HasManyThrough<Patient, Appointment> patients;
};

struct Patient
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<std::string> name;
    Field<std::string> comment;
    HasMany<Appointment> appointments;
    HasManyThrough<Physician, Appointment> physicians;
};

struct Appointment
{
    Field<uint64_t, PrimaryKey::AutoIncrement> id;
    Field<SqlDateTime> date;
    Field<std::string> comment;
    BelongsTo<&Physician::id> physician;
    BelongsTo<&Patient::id> patient;
};

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

    auto const queriedCount = physician1.patients.Count();
    REQUIRE(queriedCount == 2);
    CHECK(DataMapper::Inspect(physician1.patients.At(0)) == DataMapper::Inspect(patient1));
    CHECK(DataMapper::Inspect(physician1.patients.At(1)) == DataMapper::Inspect(patient2));

    CHECK(patient1.physicians.Count() == 2);
    CHECK(DataMapper::Inspect(patient1.physicians.At(0)) == DataMapper::Inspect(physician2));
    CHECK(DataMapper::Inspect(patient1.physicians.At(1)) == DataMapper::Inspect(physician1));

    CHECK(patient2.physicians.Count() == 1);
    CHECK(DataMapper::Inspect(patient2.physicians.At(0)) == DataMapper::Inspect(physician1));

    // Test Each() method
    size_t numPatientsIterated = 0;
    std::vector<Patient> retrievedPatients;
    physician2.patients.Each([&](Patient const& patient) {
        REQUIRE(numPatientsIterated == 0);
        ++numPatientsIterated;
        std::println("Patient: {}", DataMapper::Inspect(patient));
        retrievedPatients.emplace_back(patient);

        // Load the relations of the patient
        dm.ConfigureRelationAutoLoading(retrievedPatients.back());
    });

    Patient const& patient = retrievedPatients.at(0);
    CHECK(DataMapper::Inspect(patient) == DataMapper::Inspect(patient1)); // Blooper
    CHECK(patient.comment.Value() == "Prefers morning times");
    CHECK(patient.physicians.Count() == 2);
    CHECK(patient.physicians.At(0).name.Value() == "Granny");
    CHECK(DataMapper::Inspect(patient.physicians.At(0)) == DataMapper::Inspect(physician2));
}

struct TestRecord
{
    Field<uint64_t, PrimaryKey::Manual> id {};
    Field<std::string> comment;

    std::weak_ordering operator<=>(TestRecord const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, TestRecord const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "manual primary key", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestRecord>();

    auto record = TestRecord { .comment = "Hello, World!" };
    dm.Create(record);
    INFO("Created record: " << DataMapper::Inspect(record));
    auto const queriedRecord = dm.QuerySingle<TestRecord>(record.id).value();
    REQUIRE(queriedRecord == record);

    auto record2 = TestRecord { .comment = "Hello, World! 2" };
    dm.Create(record2);
    INFO("Created record: " << DataMapper::Inspect(record2));
    auto const queriedRecord2 = dm.QuerySingle<TestRecord>(record2.id).value();
    REQUIRE(queriedRecord2 == record2);

    REQUIRE(record.id != record2.id);
}
