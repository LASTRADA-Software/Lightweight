// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Utils.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

using namespace std::string_view_literals;
using namespace std::string_literals;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

std::ostream& operator<<(std::ostream& os, RecordId const& id)
{
    return os << id.value;
}

template <typename T, auto P1, auto P2>
std::ostream& operator<<(std::ostream& os, Field<std::optional<T>, P1, P2> const& field)
{
    if (field.Value())
        return os << std::format("Field<{}> {{ {}, {} }}",
                                 Reflection::TypeNameOf<T>,
                                 *field.Value(),
                                 field.IsModified() ? "modified" : "not modified");
    else
        return os << "NULL";
}

template <typename T, auto P1, auto P2>
std::ostream& operator<<(std::ostream& os, Field<T, P1, P2> const& field)
{
    return os << std::format("Field<{}> {{ ", Reflection::TypeNameOf<T>) << "value: " << field.Value() << "; "
              << (field.IsModified() ? "modified" : "not modified") << " }";
}

struct NamingTest1
{
    Field<int> normal;
    Field<int, SqlRealName { "c1" }> name;
};

struct NamingTest2
{
    Field<int, PrimaryKey::AutoAssign, SqlRealName { "First_PK" }> pk1;
    Field<int, SqlRealName { "Second_PK" }, PrimaryKey::AutoAssign> pk2;

    static constexpr std::string_view TableName = "NamingTest2_aliased"sv;
};

TEST_CASE_METHOD(SqlTestFixture, "SQL entity naming", "[DataMapper]")
{
    static_assert(RecordTableName<NamingTest1> == "NamingTest1"sv);
    static_assert(RecordTableName<NamingTest2> == "NamingTest2_aliased"sv);

    static_assert(FieldNameAt<0, NamingTest1> == "normal"sv);
    static_assert(FieldNameAt<1, NamingTest1> == "c1"sv);
    static_assert(FieldNameAt<0, NamingTest2> == "First_PK"sv);
    static_assert(FieldNameAt<1, NamingTest2> == "Second_PK"sv);

    static_assert(FieldNameOf<&NamingTest1::normal> == "normal"sv);
    static_assert(FieldNameOf<&NamingTest1::name> == "c1"sv);
    static_assert(FieldNameOf<&NamingTest2::pk1> == "First_PK"sv);
    static_assert(FieldNameOf<&NamingTest2::pk2> == "Second_PK"sv);

    static_assert(FullFieldNameOf<&NamingTest1::normal> == R"("NamingTest1"."normal")");
    static_assert(FullFieldNameOf<&NamingTest1::name> == R"("NamingTest1"."c1")");
    static_assert(FullFieldNameOf<&NamingTest2::pk1> == R"("NamingTest2_aliased"."First_PK")");
    static_assert(FullFieldNameOf<&NamingTest2::pk2> == R"("NamingTest2_aliased"."Second_PK")");
}

TEST_CASE("Field: int", "[DataMapper],[Field]")
{
    Field<int> field;

    field = 42;
    CHECK(field == 42);
    CHECK(field.Value() == 42);
    CHECK(field.IsModified());
}

TEST_CASE("Field: SqlAnsiString", "[DataMapper],[Field]")
{
    Field<SqlAnsiString<25>> field;

    field = "Hello";
    CHECK(field == "Hello");
    CHECK(field.Value() == "Hello");
    CHECK(field.IsModified());

    field.SetModified(false);
    field = "World"sv;
    CHECK(field == "World"sv);
    CHECK(field.Value() == "World"sv);
    CHECK(field.IsModified());

    field.SetModified(false);
    field = "Hello, World"s;
    CHECK(field == "Hello, World"s);
    CHECK(field.Value() == "Hello, World"s);
    CHECK(field.IsModified());
}

namespace Models
{

struct NamingTest1
{
    Field<int> normal;
    Field<int, SqlRealName { "c1" }> name;
};

struct NamingTest2
{
    Field<int, PrimaryKey::AutoAssign, SqlRealName { "First_PK" }> pk1;
    Field<int, SqlRealName { "Second_PK" }, PrimaryKey::AutoAssign> pk2;

    static constexpr std::string_view TableName = "NamingTest2_aliased"sv;
};

} // namespace Models

TEST_CASE_METHOD(SqlTestFixture, "SQL entity naming (namespace)", "[DataMapper]")
{
    CHECK(FieldNameAt<0, Models::NamingTest1> == "normal"sv);
    CHECK(FieldNameAt<1, Models::NamingTest1> == "c1"sv);
    CHECK(RecordTableName<Models::NamingTest1> == "NamingTest1"sv);

    CHECK(FieldNameAt<0, Models::NamingTest2> == "First_PK"sv);
    CHECK(FieldNameAt<1, Models::NamingTest2> == "Second_PK"sv);
    CHECK(RecordTableName<Models::NamingTest2> == "NamingTest2_aliased"sv);
}

struct PKTest0
{
    Field<int, PrimaryKey::AutoAssign> pk;
    Field<char> field;
};

static_assert(IsPrimaryKey<Reflection::MemberTypeOf<0, PKTest0>>);
static_assert(!IsPrimaryKey<Reflection::MemberTypeOf<1, PKTest0>>);
static_assert(RecordPrimaryKeyIndex<PKTest0> == 0);
static_assert(std::same_as<RecordPrimaryKeyType<PKTest0>, int>);

struct PKTest1
{
    Field<int> field;
    Field<SqlTrimmedFixedString<10>, PrimaryKey::AutoAssign> pk;
};

static_assert(!IsPrimaryKey<Reflection::MemberTypeOf<0, PKTest1>>);
static_assert(IsPrimaryKey<Reflection::MemberTypeOf<1, PKTest1>>);
static_assert(RecordPrimaryKeyIndex<PKTest1> == 1);
static_assert(std::same_as<RecordPrimaryKeyType<PKTest1>, SqlTrimmedFixedString<10>>);

struct PKTestMulti
{
    Field<int> fieldA;
    Field<char> fieldB;
    Field<SqlTrimmedFixedString<10>, PrimaryKey::AutoAssign> pk1;
    Field<double, PrimaryKey::AutoAssign> pk2;
};

static_assert(RecordPrimaryKeyIndex<PKTestMulti> == 2,
              "Primary key index should be the first primary key field in the record");
static_assert(std::same_as<RecordPrimaryKeyType<PKTestMulti>, SqlTrimmedFixedString<10>>);

TEST_CASE_METHOD(SqlTestFixture, "Primary key access", "[DataMapper]")
{
    PKTest0 pk0;
    RecordPrimaryKeyOf(pk0) = 42;
    CHECK(pk0.pk.Value() == 42);

    PKTest1 pk1;
    RecordPrimaryKeyOf(pk1) = "Hello";
    CHECK(pk1.pk.Value() == "Hello");

    PKTestMulti pkMulti;
    RecordPrimaryKeyOf(pkMulti) = "World";
    CHECK(pkMulti.pk1.Value() == "World");
}

struct Person
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;
    Field<bool> is_active { true };
    Field<std::optional<int>> age;

    std::weak_ordering operator<=>(Person const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, Person const& value)
{
    return os << std::format("Person {{ id: {}, name: {}, is_active: {}, age: {} }}",
                             value.id.Value(),
                             value.name.Value(),
                             value.is_active.Value(),
                             value.age.Value().value_or(-1));
}

// This is a test to only partially query a table row (a few columns)
struct PersonName
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;

    static constexpr std::string_view TableName = RecordTableName<Person>;
};

TEST_CASE_METHOD(SqlTestFixture, "Query.All", "[DataMapper]")
{
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    auto const records = dm.Query<Person>().Where(FieldNameOf<&Person::is_active>, "=", true).All();

    CHECK(records.size() == 2);
    CHECK(records[0] == expectedPersons[0]);
    CHECK(records[1] == expectedPersons[2]);
}

TEST_CASE_METHOD(SqlTestFixture, "Query.First", "[DataMapper]")
{
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    auto const records = dm.Query<Person>().Where(FieldNameOf<&Person::age>, ">=", 30).First(2);

    CHECK(records.size() == 2);
    CHECK(records[0] == expectedPersons[0]);
    CHECK(records[1] == expectedPersons[2]);
}

TEST_CASE_METHOD(SqlTestFixture, "Query.Range", "[DataMapper]")
{
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    auto const records = dm.Query<Person>().Range(1, 2);

    CHECK(records.size() == 2);
    CHECK(records[0] == expectedPersons[1]);
    CHECK(records[1] == expectedPersons[2]);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySingle.Get", "[DataMapper]")
{
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    auto const record = dm.QuerySingle<Person>().Where(FullFieldNameOf<&Person::age>, "=", 36).Get();

    CHECK(record.has_value());
    CHECK(record.value() == expectedPersons[2]);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.All", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });

    auto const records =
        dm.QuerySparse<Person, &Person::name, &Person::age>().Where(FieldNameOf<&Person::is_active>, "=", true).All();

    CHECK(records.size() == 1);
    CHECK(records[0].name == "John Doe");
    CHECK(records[0].age == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.First", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });

    auto const records =
        dm.QuerySparse<Person, &Person::name, &Person::age>().Where(FieldNameOf<&Person::age>, ">=", 30).First(2);

    CHECK(records.size() == 2);
    CHECK(records[0].name == "John Doe");
    CHECK(records[0].age == 42);
    CHECK(records[1].name == "Jane Doe");
    CHECK(records[1].age == 36);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.Range", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });

    auto const records = dm.QuerySparse<Person, &Person::name, &Person::age>().Range(1, 2);

    CHECK(records.size() == 2);
    CHECK(records[0].name == "Jimmy John");
    CHECK(records[0].age == 24);
    CHECK(records[1].name == "Jane Doe");
    CHECK(records[1].age == 36);
}

TEST_CASE_METHOD(SqlTestFixture, "Constructor with connection string", "[DataMapper]")
{
    auto dm = DataMapper(SqlConnection::DefaultConnectionString());
    dm.CreateTable<Person>();
}

TEST_CASE_METHOD(SqlTestFixture, "CRUD", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create
    auto person = Person {};
    person.name = "John Doe";
    person.is_active = true;

    REQUIRE(!person.id.Value());
    dm.Create(person);
    REQUIRE(person.id.Value());

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

struct UserView
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
};

TEST_CASE_METHOD(SqlTestFixture, "All", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTables<UserView>();
    dm.CreateExplicit(UserView { .name = "John Doe" });
    dm.CreateExplicit(UserView { .name = "Jane Doe" });
    dm.CreateExplicit(UserView { .name = "Jim Doe" });
    CHECK(dm.All<UserView>().size() == 3);
}

TEST_CASE_METHOD(SqlTestFixture, "Count", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<UserView>();
    CHECK(dm.Count<UserView>() == 0);
    dm.CreateExplicit(UserView { .name = "John Doe" });
    dm.CreateExplicit(UserView { .name = "Jane Doe" });
    dm.CreateExplicit(UserView { .name = "Jim Doe" });
    CHECK(dm.Count<UserView>() == 3);
}

TEST_CASE_METHOD(SqlTestFixture, "partial row retrieval", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto person = Person {};
    person.name = "John Doe";
    person.is_active = true;
    REQUIRE(!person.id.Value());
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
        person.name = std::format("John-{}", i);
        person.age = i;
        dm.Create(person);
    }

    auto stmt = SqlStatement { dm.Connection() };
    int age = 40;
    for (auto&& person: SqlRowIterator<Person>(stmt))
    {
        CHECK(person.name.Value() == std::format("John-{}", age));
        CHECK(person.age.Value() == age);
        CHECK(person.id.Value());
        ++age;
    }
}

struct RecordWithDefaults
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name1 { "John Doe" };
    Field<std::optional<SqlAnsiString<30>>> name2 { "John Doe" };
    Field<bool> boolean1 { true };
    Field<bool> boolean2 { false };
    Field<std::optional<int>> int1 { 42 };
    Field<std::optional<int>> int2 {};
};

struct RecordWithNoDefaults
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name1;
    Field<std::optional<SqlAnsiString<30>>> name2;
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
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};

    HasMany<Email> emails {};
};

struct Email
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> address {};
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

    if (dm.Connection().ServerType() == SqlServerType::SQLITE)
    {

        REQUIRE(NormalizeText(dm.CreateTableString<User>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "User" (
                                    "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                                    "name" VARCHAR(30) NOT NULL
                                    );)"));
        REQUIRE(NormalizeText(dm.CreateTableString<Email>(dm.Connection().ServerType()))
                == NormalizeText(R"(CREATE TABLE "Email" (
                                    "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                                    "address" VARCHAR(30) NOT NULL,
                                    "user_id" BIGINT,
                                    CONSTRAINT FK_user_id FOREIGN KEY ("user_id") REFERENCES "User"("id")
                                    );)"));
    }
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
    BelongsTo<&Suppliers::id> supplier {};

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
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    HasMany<Appointment> appointments;
    HasManyThrough<Patient, Appointment> patients;
};

struct Patient
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    Field<SqlAnsiString<30>> comment;
    HasMany<Appointment> appointments;
    HasManyThrough<Physician, Appointment> physicians;
};

struct Appointment
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlDateTime> date;
    Field<SqlAnsiString<80>> comment;
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
        INFO("Patient: " << DataMapper::Inspect(patient));
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

struct TestRecord
{
    Field<uint64_t, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> comment;

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

// Simple struct, used for testing SELECT'ing into it
struct SimpleStruct
{
    uint64_t pkFromA;
    uint64_t pkFromB;
    SqlAnsiString<30> c1FromA;
    SqlAnsiString<30> c2FromA;
    SqlAnsiString<30> c1FromB;
    SqlAnsiString<30> c2FromB;
};

TEST_CASE_METHOD(SqlTestFixture, "Query: SELECT into simple struct", "[DataMapper]")
{
    auto dm = DataMapper {};

    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable("TableA")
            .PrimaryKeyWithAutoIncrement("pk", Bigint {})
            .Column("c1", Varchar { 30 })
            .Column("c2", Varchar { 30 });
        migration.CreateTable("TableB")
            .PrimaryKeyWithAutoIncrement("pk", Bigint {})
            .Column("c1", Varchar { 30 })
            .Column("c2", Varchar { 30 });
    });

    SqlStatement(dm.Connection()).ExecuteDirect(dm.FromTable("TableA").Insert().Set("c1", "a").Set("c2", "b"));
    SqlStatement(dm.Connection()).ExecuteDirect(dm.FromTable("TableB").Insert().Set("c1", "a").Set("c2", "c"));

    auto records =
        dm.Query<SimpleStruct>(dm.FromTable("TableA")
                                   .Select()
                                   .Field(SqlQualifiedTableColumnName { .tableName = "TableA", .columnName = "pk" })
                                   .Field(SqlQualifiedTableColumnName { .tableName = "TableB", .columnName = "pk" })
                                   .Fields({ "c1"sv, "c2"sv }, "TableA"sv)
                                   .Fields({ "c1"sv, "c2"sv }, "TableB"sv)
                                   .LeftOuterJoin("TableB", "c1", "c1")
                                   .All());

    CHECK(records.size() == 1);
    SimpleStruct& record = records.at(0);
    INFO("Record: " << DataMapper::Inspect(record));
    CHECK(record.pkFromA != 0);
    CHECK(record.pkFromB != 0);
    CHECK(record.c1FromA == "a");
    CHECK(record.c2FromA == "b");
    CHECK(record.c1FromB == "a");
    CHECK(record.c2FromB == "c");
}

TEST_CASE_METHOD(SqlTestFixture, "Query: Partial retriaval of the data", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();

    for (int i = 20; i <= 50; ++i)
    {
        auto person = Person {};
        person.name = std::format("John-{}", i);
        person.age = i;
        dm.Create(person);
    }

    auto result = dm.Query<SqlElements<1, 3>, Person>(
        dm.FromTable(RecordTableName<Person>).Select().Fields({ "name"sv, "age"sv }).All());

    for (int i = 20; i <= 50; ++i)
    {
        CAPTURE(i);
        CHECK(result[i - 20].name.Value() == std::format("John-{}", i));
        CHECK(result[i - 20].age.Value() == i);
    }
}

struct SimpleStruct2
{
    std::u8string name;
    int age;
};

std::ostream& operator<<(std::ostream& os, SimpleStruct2 const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySingle: into simple struct", "[DataMapper]")
{
    auto dm = DataMapper {};

    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable(RecordTableName<SimpleStruct2>).Column("name", NVarchar { 30 }).Column("age", Integer {});
    });

    dm.CreateExplicit(SimpleStruct2 { .name = u8"John", .age = 42 });

    auto result = dm.QuerySingle<SimpleStruct2>().Get();

    REQUIRE(result.has_value());
    auto const& record = result.value();
    CAPTURE(record);
    CHECK(record.name == u8"John");
    CHECK(record.age == 42);
}

struct SimpleStruct3
{
    std::string name;
    int age;
    int notAge;
};

TEST_CASE_METHOD(SqlTestFixture, "QuerySingle: into simple struct with extra element", "[DataMapper]")
{
    auto dm = DataMapper {};

    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        // clang-format off
        migration.CreateTable(RecordTableName<SimpleStruct3>)
                 .Column("name", NVarchar { 30 })
                 .Column("age", Integer {});
        // clang-format on
    });

    auto stmt = SqlStatement(dm.Connection());
    stmt.ExecuteDirect(R"(INSERT INTO "SimpleStruct3" ("name", "age") VALUES ('John', 42))");

    auto result = dm.QuerySingle<SimpleStruct3>(
        dm.FromTable(RecordTableName<SimpleStruct3>).Select().Fields({ "name"sv, "age"sv }));

    REQUIRE(result.has_value());
    auto const& record = result.value();
    CAPTURE(record);
    CHECK(record.name == "John");
    CHECK(record.age == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySingle: into simple struct with less elements", "[DataMapper]")
{
    auto dm = DataMapper {};

    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable(RecordTableName<SimpleStruct3>)
            .Column("name", NVarchar { 30 })
            .Column("age", Integer {})
            .Column("notAge", Integer {});
    });

    dm.CreateExplicit(SimpleStruct3 { .name = "John", .age = 42, .notAge = 0 });

    auto result = dm.QuerySingle<SimpleStruct2>(
        dm.FromTable(RecordTableName<SimpleStruct3>).Select().Fields({ "name"sv, "age"sv }));

    REQUIRE(result.has_value());
    auto const& record = result.value();
    CAPTURE(record);
    CHECK(record.name == u8"John");
    CHECK(record.age == 42);
}

struct MultiPkRecord
{
    Field<SqlAnsiString<32>, PrimaryKey::AutoAssign> firstName;
    Field<SqlAnsiString<32>, PrimaryKey::AutoAssign> lastName;

    constexpr std::weak_ordering operator<=>(MultiPkRecord const& other) const = default;
};

TEST_CASE_METHOD(SqlTestFixture, "Table with multiple primary keys", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<MultiPkRecord>();

    auto record = MultiPkRecord { .firstName = "John", .lastName = "Doe" };
    dm.Create(record);

    {
        auto const _ = ScopedSqlNullLogger {}; // Suppress the error message, as we are testing for it
        CHECK_THROWS_AS(dm.CreateExplicit(MultiPkRecord { .firstName = "John", .lastName = "Doe" }), SqlException);
    }

    auto queriedRecords = dm.Query<MultiPkRecord>().All();

    CHECK(queriedRecords.size() == 1);
    auto const& queriedRecord = queriedRecords.at(0);
    INFO("Queried record: " << DataMapper::Inspect(queriedRecord));
    CHECK(queriedRecord == record);
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
    BelongsTo<&AliasedRecord::id> record;
};

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
        auto records = dm.All<AliasedRecord>();
        CHECK(records.size() == 1);
        CHECK(records.at(0) == record);
    }
}

struct PersonDifferenceView
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<25>> name {};
    Field<bool> is_active { true };
    Field<int> age {};
};

TEST_CASE_METHOD(SqlTestFixture, "Test DifferenceView", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<PersonDifferenceView>();
    auto first = PersonDifferenceView { .name = "Jahn Doe", .age = 10 };
    dm.Create(first);
    auto second = PersonDifferenceView { .name = "John Doe", .age = 19 };
    dm.Create(second);

    auto persons = dm.Query<PersonDifferenceView>().All();
    auto difference = CollectDifferences(persons[0], persons[1]);

    auto differenceCount = 0;
    difference.iterate([&](auto& lhs, auto& rhs) {
        CHECK(lhs.Value() != rhs.Value());
        ++differenceCount;
    });
    // 3 because of the auto-assigned GUID on top of name and age
    CHECK(differenceCount == 3);
}

// NOLINTEND(bugprone-unchecked-optional-access)
