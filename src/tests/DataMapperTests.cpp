// SPDX-License-Identifier: Apache-2.0

#include "Lightweight/DataBinder/SqlGuid.hpp"
#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <deque>
#include <iostream>
#include <ostream>
#include <ranges>
#include <set>
#include <string>
#include <string_view>

using namespace std::string_view_literals;
using namespace std::string_literals;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace std
{

template <typename T>
ostream& operator<<(ostream& os, optional<T> const& opt)
{
    if (opt.has_value())
        return os << *opt;
    else
        return os << "nullopt";
}

} // namespace std

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

template <typename T>
struct TableWithLargeStrings
{
    using value_type = T;

    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlDynamicString<5000, T>> longString;

    static constexpr std::string_view TableName = "TableWithLargeStrings"sv;

    std::weak_ordering operator<=>(TableWithLargeStrings<T> const& other) const = default;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, TableWithLargeStrings<T> const& record)
{
    return os << DataMapper::Inspect(record);
}

// clang-format off
using TypesToTest = std::tuple<
    TableWithLargeStrings<char>,
    TableWithLargeStrings<char8_t>,
    TableWithLargeStrings<char16_t>,
    TableWithLargeStrings<char32_t>,
    TableWithLargeStrings<wchar_t>
>;
// clang-format on

TEMPLATE_LIST_TEST_CASE("SqlDataBinder specializations", "[DataMapper],[Field],[SqlDynamicString]", TypesToTest)
{
    using TestRecord = TestType;

    INFO(Reflection::TypeNameOf<TestRecord>);

    SqlLogger::SetLogger(TestSuiteSqlLogger::GetLogger());

    {
        auto stmt = SqlStatement {};
        SqlTestFixture::DropAllTablesInDatabase(stmt);
    }

    auto dm = DataMapper {};
    dm.CreateTable<TestRecord>();

    auto const expectedRecord = TestRecord {
        .id = SqlGuid::Create(),
        .longString = MakeLargeText<typename TestRecord::value_type>(5000),
    };
    dm.CreateExplicit(expectedRecord);

    // Check single record retrieval
    auto const actualResult = dm.QuerySingle<TestRecord>().Get();
    REQUIRE(actualResult.has_value());
    CHECK(actualResult.value() == expectedRecord);

    // Check multi-record retrieval (if one works, they all do)
    auto const records = dm.Query<TestRecord>().All();
    REQUIRE(records.size() == 1);
    CHECK(records[0] == expectedRecord);
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

    auto const records =
        dm.Query<Person>().Where(FieldNameOf<&Person::is_active>, "=", true).OrderBy(FieldNameOf<&Person::name>).All();

    CHECK(records.size() == 2);
    CHECK(records[0] == expectedPersons[2]);
    CHECK(records[1] == expectedPersons[0]);
}

TEST_CASE_METHOD(SqlTestFixture, "Query.First n", "[DataMapper]")
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

    auto const records = dm.Query<Person>()
                             .Where(FieldNameOf<&Person::age>, ">=", 30)
                             .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::ASCENDING)
                             .First(2);

    CHECK(records.size() == 2);
    CHECK(records[0] == expectedPersons[2]);
    CHECK(records[1] == expectedPersons[3]);
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

    auto const record = dm.Query<Person>().Where(FieldNameOf<&Person::age>, "<=", 24).First();

    REQUIRE(record.has_value());
    CHECK(record.value() == expectedPersons[1]);

    auto const impossible = dm.Query<Person>().Where(FieldNameOf<&Person::age>, "=", -5).First();
    REQUIRE(impossible.has_value() == false);
}

TEST_CASE_METHOD(SqlTestFixture, "Query.Range", "[DataMapper]")
{
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    // clang-format off
    auto const records = dm.Query<Person>()
                           .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::ASCENDING)
                           .Range(1, 2);
    // clang-format on

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

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.First n", "[DataMapper]")
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

    auto const records = dm.QuerySparse<Person, &Person::name, &Person::age>()
                             .Where(FieldNameOf<&Person::age>, ">=", 30)
                             .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::ASCENDING)
                             .First(2);

    CHECK(records.size() == 2);

    CHECK(records[0].name.Value() == expectedPersons[2].name.Value());
    CHECK(records[0].age.Value() == expectedPersons[2].age.Value());

    CHECK(records[1].name.Value() == expectedPersons[3].name.Value());
    CHECK(records[1].age.Value() == expectedPersons[3].age.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.First", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });

    auto const record = dm.QuerySparse<Person, &Person::name>().Where(FieldNameOf<&Person::age>, "<=", 24).First();

    REQUIRE(record.has_value());
    CHECK(record->name == "Jimmy John");
    CHECK(record->age.Value().has_value() == false); // age is not queried, so it defaults to 0
    CHECK(record->is_active == true);                // is_active is not queried, so it defaults to false

    auto const impossible = dm.QuerySparse<Person, &Person::name>().Where(FieldNameOf<&Person::age>, "=", -5).First();
    REQUIRE(impossible.has_value() == false);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.Range", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm.CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });

    // clang-format off
    auto const records = dm.QuerySparse<Person, &Person::name, &Person::age>()
                           .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::ASCENDING)
                           .Range(1, 2);
    // clang-format on

    CHECK(records.size() == 2);
    CHECK(records[0].name == "Jimbo Jones");
    CHECK(records[0].age == 69);
    CHECK(records[1].name == "Jimmy John");
    CHECK(records[1].age == 24);
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

    auto retrievedPersons = std::vector<Person> {};
    for (auto&& person: SqlRowIterator<Person>(dm.Connection()))
        retrievedPersons.emplace_back(person);

    std::ranges::sort(retrievedPersons, [](Person const& a, Person const& b) { return a.age.Value() < b.age.Value(); });

    int age = 40;
    size_t count = 0;

    for (auto const& person: retrievedPersons)
    {
        CHECK(person.name.Value() == std::format("John-{}", age));
        CHECK(person.age.Value() == age);
        CHECK(person.id.Value());
        ++age;
        ++count;
    }

    CHECK(count == 11);
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
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> name {};

    HasMany<Email> emails {};
};

struct Email
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> address {};
    BelongsTo<&User::id> user {};

    constexpr std::weak_ordering operator<=>(Email const& other) const = default;
};

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

struct Physician;
struct Appointment;
struct Patient;

struct Physician
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    HasMany<Appointment> appointments;
    HasManyThrough<Patient, Appointment> patients;

    constexpr std::weak_ordering operator<=>(Physician const& other) const
    {
        if (auto result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = name.Value() <=> other.name.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};

struct Patient
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    Field<SqlAnsiString<30>> comment;
    HasMany<Appointment> appointments;
    HasManyThrough<Physician, Appointment> physicians;

    constexpr std::weak_ordering operator<=>(Patient const& other) const
    {
        if (auto result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = name.Value() <=> other.name.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = comment.Value() <=> other.comment.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};

struct Appointment
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlDateTime> date;
    Field<SqlAnsiString<80>> comment;
    BelongsTo<&Physician::id> physician;
    BelongsTo<&Patient::id> patient;

    constexpr std::weak_ordering operator<=>(Appointment const& other) const
    {
        if (auto const result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = date.Value() <=> other.date.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = comment.Value() <=> other.comment.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = physician.Value() <=> other.physician.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = patient.Value() <=> other.patient.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};

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

TEST_CASE_METHOD(SqlTestFixture, "Query: SELECT into SqlVariantRow", "[DataMapper],[SqlVariantRow]")
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
        dm.Query<SqlVariantRow>(dm.FromTable("TableA").Select().Field("*").LeftOuterJoin("TableB", "c1", "c1").All());

    CHECK(records.size() == 1);
    auto& record = records.at(0);
    CHECK(record.size() == 6);
    CHECK(record[0].TryGetInt().value() == 1);
    CHECK(record[1].TryGetStringView().value() == "a");
    CHECK(record[2].TryGetStringView().value() == "b");
    CHECK(record[3].TryGetInt().value() == 1);
    CHECK(record[4].TryGetStringView().value() == "a");
    CHECK(record[5].TryGetStringView().value() == "c");
}

TEST_CASE_METHOD(SqlTestFixture, "Query: Partial retriaval of the data", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();

    constexpr auto StartAge = 20;
    constexpr auto EndAge = 21;

    for (auto const age: std::views::iota(StartAge, EndAge + 1))
    {
        auto person = Person {};
        person.name = std::format("John-{}", age);
        person.age = age;
        dm.Create(person);
        INFO("Created person: " << person);
    }

    auto records = dm.Query<SqlElements<1, 3>, Person>(dm.FromTable(RecordTableName<Person>)
                                                           .Select()
                                                           .Fields({ "name"sv, "age"sv })
                                                           .OrderBy(FieldNameOf<&Person::age>, SqlResultOrdering::ASCENDING)
                                                           .All());

    for (auto const [i, record]: records | std::views::enumerate)
    {
        auto const age = StartAge + i;
        CAPTURE(age);
        REQUIRE(record.name.Value() == std::format("John-{}", age));
        REQUIRE(record.age.Value() == age);
    }
}

struct SimpleStruct2
{
    std::u8string name;
    int age;
};

static_assert(std::cmp_equal(RecordPrimaryKeyIndex<SimpleStruct2>, static_cast<size_t>(-1)));
static_assert(std::same_as<RecordPrimaryKeyType<SimpleStruct2>, void>);

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

    auto result =
        dm.QuerySingle<SimpleStruct3>(dm.FromTable(RecordTableName<SimpleStruct3>).Select().Fields({ "name"sv, "age"sv }));

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

    auto result =
        dm.QuerySingle<SimpleStruct2>(dm.FromTable(RecordTableName<SimpleStruct3>).Select().Fields({ "name"sv, "age"sv }));

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

static_assert(std::same_as<typename BelongsTo<&AliasedRecord::id>::ReferencedRecord, AliasedRecord>);
static_assert(std::same_as<typename BelongsTo<&AliasedRecord::id>::ValueType, uint64_t>);

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

TEST_CASE_METHOD(SqlTestFixture, "MapFromRecordFields", "[DataMapper]")
{
    auto const person = Person {
        .id = SqlGuid::Create(),
        .name = "John Doe",
        .is_active = true,
        .age = 42,
    };

    auto variantFields = SqlVariantRow {};
    variantFields.resize(RecordStorageFieldCount<Person>);

    MapFromRecordFields(person, variantFields);

    Reflection::EnumerateMembers(person,
                                 [&]<size_t I>(auto const& field) { CHECK(variantFields[I] == SqlVariant(field.Value())); });
}

struct JoinA
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> value_a_first {};
    Field<int> value_a_second {};
    Field<int> value_a_third {};
};

struct JoinB
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<uint64_t> a_id {};
    Field<uint64_t> c_id {};
};

struct JoinC
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> value_c_first {};
    Field<int> value_c_second {};
    Field<int> value_c_third {};
    Field<int> value_c_fourth {};
};

TEST_CASE_METHOD(SqlTestFixture, "MapForJointStatement", "[DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<JoinA>();
    dm.CreateTable<JoinB>();
    dm.CreateTable<JoinC>();

    // fill with some data
    for (int const i: std::views::iota(1, 100))
    {
        auto a = JoinA { .value_a_first = i, .value_a_second = 10 + i, .value_a_third = 100 + i };
        auto b = JoinB { .a_id = 49 + i, .c_id = i };
        auto c =
            JoinC { .value_c_first = i, .value_c_second = 10 + i, .value_c_third = 100 + i, .value_c_fourth = 1000 + i };
        dm.Create(a);
        dm.Create(b);
        dm.Create(c);
    }

    auto const records =
        dm.Query<JoinA, JoinC>().InnerJoin<&JoinB::a_id, &JoinA::id>().InnerJoin<&JoinC::id, &JoinB::c_id>().All();

    CHECK(records.size() == 50);
    int i = 1;
    for (auto const& [elementA, elementC]: records)
    {
        CHECK(std::cmp_equal(elementA.id.Value(), 49 + i));
        CHECK(std::cmp_equal(elementC.id.Value(), i));
        CHECK(elementA.value_a_first.Value() == 49 + i);
        CHECK(elementA.value_a_second.Value() == 10 + 49 + i);
        CHECK(elementA.value_a_third.Value() == 100 + 49 + i);
        CHECK(elementC.value_c_first.Value() == i);
        CHECK(elementC.value_c_second.Value() == 10 + i);
        CHECK(elementC.value_c_third.Value() == 100 + i);
        CHECK(elementC.value_c_fourth.Value() == 1000 + i);
        ++i;
    }
}

struct TestDynamicData
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlDynamicAnsiString<4000>> stringAnsi {};
    Field<SqlDynamicUtf16String<4000>> stringUtf16 {};
    Field<SqlDynamicUtf32String<4000>> stringUtf32 {};
    Field<SqlDynamicWideString<4000>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestDynamicData", "[DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<TestDynamicData>();
    TestDynamicData data {};
    dm.Create(data);

    auto const checkSize = [&](auto size) {
        INFO(size);

        data.stringAnsi = std::string(size, 'a');
        data.stringUtf16 = std::basic_string<char16_t>(size, u'a');
        data.stringUtf32 = std::basic_string<char32_t>(size, U'a');
        data.stringWide = std::basic_string<wchar_t>(size, L'a');
        dm.Update(data);

        auto const result = dm.QuerySingle<TestDynamicData>(data.id);
        REQUIRE(result.has_value());
        REQUIRE(result.value().stringAnsi.Value() == std::string(size, 'a'));
        REQUIRE(result.value().stringUtf16.Value() == std::basic_string<char16_t>(size, u'a'));
        REQUIRE(result.value().stringUtf32.Value() == std::basic_string<char32_t>(size, U'a'));
        REQUIRE(result.value().stringWide.Value() == std::basic_string<wchar_t>(size, L'a'));
    };

    checkSize(5);
    checkSize(1000);
    checkSize(2000);
    checkSize(4000);
}

TEST_CASE_METHOD(SqlTestFixture, "TestQuerySparseDynamicData", "[DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<TestDynamicData>();
    TestDynamicData data {};
    data.stringAnsi = std::string(10, 'a');
    data.stringUtf32 = std::basic_string<char32_t>(10, U'a');
    data.stringWide = std::basic_string<wchar_t>(10, L'a');
    dm.Create(data);

    auto const checkSize = [&](auto size) {
        INFO(size);
        data.stringUtf16 = std::basic_string<char16_t>(size, u'a');
        dm.Update(data);
        auto const result = dm.QuerySparse<TestDynamicData, &TestDynamicData::stringUtf16>()
                                .Where(FieldNameOf<&TestDynamicData::id>, "=", data.id.Value())
                                .First();
        REQUIRE(result.has_value());
        REQUIRE(result.value().stringUtf16.Value() == std::basic_string<char16_t>(size, u'a'));
    };

    checkSize(5);
    checkSize(1000);
    checkSize(2000);
    checkSize(4000);
}

struct TestOptionalDynamicData
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<std::optional<SqlDynamicAnsiString<4000>>> stringAnsi {};
    Field<std::optional<SqlDynamicUtf16String<4000>>> stringUtf16 {};
    Field<std::optional<SqlDynamicUtf32String<4000>>> stringUtf32 {};
    Field<std::optional<SqlDynamicWideString<4000>>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestOptionalDynamicData", "[DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<TestOptionalDynamicData>();
    TestOptionalDynamicData data {};
    dm.Create(data);

    auto const checkSize = [&](auto size) {
        INFO(size);

        if (size / 5 == 0)
            data.stringAnsi = std::string(size, 'a');
        else
            data.stringAnsi = std::nullopt;

        data.stringUtf16 = std::basic_string<char16_t>(size, u'a');
        data.stringUtf32 = std::basic_string<char32_t>(size, U'a');

        if (size / 2 == 0)
            data.stringWide = std::basic_string<wchar_t>(size, L'a');
        else
            data.stringWide = std::nullopt;

        dm.Update(data);

        auto const result = dm.QuerySingle<TestOptionalDynamicData>(data.id);
        REQUIRE(result.has_value());
        if (size / 5 == 0)
            REQUIRE(result.value().stringAnsi.Value().value_or("") == std::string(size, 'a'));
        else
            REQUIRE(!result.value().stringAnsi.Value().has_value());
        REQUIRE(result.value().stringUtf16.Value().value_or(u"") == std::basic_string<char16_t>(size, u'a'));
        REQUIRE(result.value().stringUtf32.Value().value_or(U"") == std::basic_string<char32_t>(size, U'a'));
        if (size / 2 == 0)
            REQUIRE(result.value().stringWide.Value().value_or(L"") == std::basic_string<wchar_t>(size, L'a'));
        else
            REQUIRE(!result.value().stringWide.Value().has_value());
    };

    checkSize(5);
    checkSize(5 * 2);
    checkSize((5 * 2) + 1);
    checkSize(1000);
    checkSize(2000);
    checkSize(4000);
}

struct MessagesStruct
{
    Field<SqlGuid, PrimaryKey::AutoAssign, SqlRealName { "primary_key" }> id;
    Field<SqlDateTime, SqlRealName { "time_stamp" }> timeStamp;
    Field<SqlDynamicWideString<4000>, SqlRealName { "Message" }> message;
};

TEST_CASE_METHOD(SqlTestFixture, "TestMessageStruct", "[DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<MessagesStruct>();

    MessagesStruct message {
        .id = SqlGuid::Create(),
        .timeStamp = SqlDateTime::Now(),
        .message = L"Hello, World!",
    };

    dm.Create(message);
    REQUIRE(message.id.Value());
}

struct MessageStructTo
{
    Field<SqlGuid, PrimaryKey::AutoAssign, SqlRealName { "primary_key" }> id;
    BelongsTo<&MessagesStruct::id, SqlRealName { "log_key" }> log_message {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestMessageStructTo", "[DataMapper]")
{
    auto dm = DataMapper {};

    dm.CreateTable<MessagesStruct>();
    // create a table for MessageStructTo
    // where foreign key can be null
    SqlStatement(dm.Connection()).ExecuteDirect(R"SQL(
                             CREATE TABLE MessageStructTo (
                                 "primary_key" GUID PRIMARY KEY,
                                 "log_key" GUID,
                                 FOREIGN KEY ("log_key") REFERENCES MessagesStruct ("primary_key")
                             ))SQL");

    MessagesStruct message {
        .id = SqlGuid::TryParse("B16BEF38-5839-11F0-D290-74563C35FB03").value(),
        .timeStamp = SqlDateTime::Now(),
        .message = L"Hello, World!",
    };
    dm.Create(message);

    MessageStructTo to1 { .id = SqlGuid::Create(), .log_message = message.id.Value() };
    dm.Create(to1);
    SqlStatement(dm.Connection())
        .ExecuteDirect(
            R"SQL( INSERT INTO MessageStructTo (primary_key, log_key) VALUES ("B16BEF38-5839-11F0-D290-74563C35FB03", NULL) )SQL");

    REQUIRE(dm.QuerySingle<MessageStructTo>(to1.id).value().log_message->id.Value() == message.id.Value());

    auto const guid = SqlGuid::TryParse("B16BEF38-5839-11F0-D290-74563C35FB03").value();
    REQUIRE(dm.QuerySingle<MessageStructTo>(guid).value().id.Value() == guid);
}

// NOLINTEND(bugprone-unchecked-optional-access)
