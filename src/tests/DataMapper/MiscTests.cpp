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

#include <string_view>

using namespace std::string_view_literals;
using namespace std::string_literals;
using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

struct WithConstionStringTestRecord
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;

    std::weak_ordering operator<=>(WithConstionStringTestRecord const& other) const = default;
};

TEST_CASE_METHOD(SqlTestFixture, "Constructor with connection string", "[DataMapper]")
{
    auto dm = DataMapper(SqlConnection::DefaultConnectionString());
    dm.CreateTable<WithConstionStringTestRecord>();
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

    auto dm = DataMapper();
    dm.CreateTable<TestRecord>();

    auto const expectedRecord = TestRecord {
        .id = SqlGuid::Create(),
        .longString = MakeLargeText<typename TestRecord::value_type>(5000),
    };
    dm.CreateExplicit(expectedRecord);

    // Check single record retrieval
    auto const actualResult = dm.Query<TestRecord>().First();
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

struct PersonDifferenceView
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<25>> name {};
    Field<bool> is_active { true };
    Field<int> age {};
};

TEST_CASE_METHOD(SqlTestFixture, "Test DifferenceView", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<PersonDifferenceView>();
    auto first = PersonDifferenceView { .name = "Jahn Doe", .age = 10 };
    dm.Create(first);
    auto second = PersonDifferenceView { .name = "John Doe", .age = 19 };
    dm.Create(second);

    auto persons = dm.Query<PersonDifferenceView>().All();
    auto difference = CollectDifferences(persons[0], persons[1]);

    auto differenceCount = 0;
    difference.Iterate([&](auto& lhs, auto& rhs) {
        CHECK(lhs.Value() != rhs.Value());
        ++differenceCount;
    });
    // 3 because of the auto-assigned GUID on top of name and age
    CHECK(differenceCount == 3);
}

struct TestDynamicData
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlDynamicAnsiString<16000>> stringAnsi {};
    Field<SqlDynamicUtf16String<16000>> stringUtf16 {};
    Field<SqlDynamicUtf32String<16000>> stringUtf32 {};
    Field<SqlDynamicWideString<16000>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestDynamicData", "[DataMapper]")
{
    auto dm = DataMapper();
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

        auto const result =
            dm.Query<TestDynamicData>().Where(FieldNameOf<Member(TestDynamicData::id)>, data.id.Value()).First();
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
    checkSize(16000);
}

TEST_CASE_METHOD(SqlTestFixture, "TestQuerySingleDynamicData", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestDynamicData>();
    TestDynamicData data {};
    data.stringAnsi = std::string(10, 'a');
    data.stringUtf32 = std::basic_string<char32_t>(10, U'a');
    data.stringWide = std::basic_string<wchar_t>(10, L'a');
    dm.Create(data);

    auto const checkSize = [&](auto size) {
        INFO(size);
        data.stringWide = std::basic_string<wchar_t>(size, L'a');
        dm.Update(data);
        auto const result = dm.Query<TestDynamicData>()
                                .Where(FieldNameOf<Member(TestDynamicData::id)>, "=", data.id.Value())
                                .First<Member(TestDynamicData::stringWide)>();
        REQUIRE(result.has_value());
        REQUIRE(result.value() == std::basic_string<wchar_t>(size, L'a'));
    };

    checkSize(5);
    checkSize(1000);
    checkSize(2000);
    checkSize(4000);
    checkSize(16000);
}

TEST_CASE_METHOD(SqlTestFixture, "TestQuerySparseDynamicData", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestDynamicData>();
    TestDynamicData data {};
    data.stringAnsi = std::string(10, 'a');
    data.stringUtf32 = std::basic_string<char32_t>(10, U'a');
    data.stringWide = std::basic_string<wchar_t>(10, L'a');
    dm.Create(data);

    auto const checkSize = [&](auto size) {
        INFO(size);
        data.stringUtf16 = std::basic_string<char16_t>(size, u'a');
        data.stringUtf32 = std::basic_string<char32_t>(size, U'a');
        dm.Update(data);
        auto const result = dm.Query<TestDynamicData>()
                                .Where(FieldNameOf<Member(TestDynamicData::id)>, "=", data.id.Value())
                                .First<Member(TestDynamicData::stringUtf16), Member(TestDynamicData::stringUtf32)>();
        REQUIRE(result.has_value());
        REQUIRE(result.value().stringUtf16 == std::basic_string<char16_t>(size, u'a'));
        REQUIRE(result.value().stringUtf32 == std::basic_string<char32_t>(size, U'a'));
    };

    checkSize(5);
    checkSize(1000);
    checkSize(2000);
    checkSize(4000);
    checkSize(16000);
}

struct TestOptionalDynamicData
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<std::optional<SqlDynamicAnsiString<16000>>> stringAnsi {};
    Field<std::optional<SqlDynamicUtf16String<16000>>> stringUtf16 {};
    Field<std::optional<SqlDynamicUtf32String<16000>>> stringUtf32 {};
    Field<std::optional<SqlDynamicWideString<16000>>> stringWide {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestOptionalDynamicData", "[DataMapper]")
{
    auto dm = DataMapper();
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
    checkSize(97);
    checkSize(1000);
    checkSize(2000);
    checkSize(2001);
    checkSize(4000);
    checkSize(16000);
}

struct MessagesStruct
{
    Field<SqlGuid, PrimaryKey::AutoAssign, SqlRealName { "primary_key" }> id;
    Field<SqlDateTime, SqlRealName { "time_stamp" }> timeStamp;
    Field<SqlDynamicWideString<4000>, SqlRealName { "Message" }> message;
};

TEST_CASE_METHOD(SqlTestFixture, "TestMessageStruct", "[DataMapper]")
{
    auto dm = DataMapper();
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
    BelongsTo<Member(MessagesStruct::id), SqlRealName { "log_key" }, SqlNullable::Null> log_message {};
};

TEST_CASE_METHOD(SqlTestFixture, "TestMessageStructTo", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<MessagesStruct>();

    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable("MessageStructTo")
            .PrimaryKey("primary_key", Guid {})
            .ForeignKey("log_key",
                        Guid {},
                        SqlForeignKeyReferenceDefinition { .tableName = "MessagesStruct", .columnName = "primary_key" });
    });

    // Create a message to test the BelongsTo relation
    auto const message = MessagesStruct {
        .id = SqlGuid::TryParse("B16BEF38-5839-11F0-D290-74563C35FB03").value(),
        .timeStamp = SqlDateTime::Now(),
        .message = L"Hello, World!",
    };
    dm.CreateExplicit(message);

    SECTION("Test BelongsTo with non-NULL relation")
    {
        auto const to = MessageStructTo { .id = SqlGuid::Create(), .log_message = message.id.Value() };
        dm.CreateExplicit(to);
        auto const queriedTo = dm.QuerySingle<MessageStructTo>(to.id).value();
        REQUIRE(queriedTo.id.Value() == to.id.Value());
        REQUIRE(queriedTo.log_message.Value().value() == message.id.Value());
    }

    SECTION("Test BelongsTo with NULL relation")
    {
        MessageStructTo to { .id = SqlGuid::Create(), .log_message = std::nullopt };
        dm.Create(to);
        auto const queriedTo = dm.QuerySingle<MessageStructTo>(to.id).value();
        REQUIRE(queriedTo.id.Value() == to.id.Value());
        REQUIRE(!queriedTo.log_message.Value().has_value());
    }
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
    REQUIRE(po.has_value());
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
    REQUIRE(po.has_value());
    p = po.value();
    CHECK(p.id == person.id);
    CHECK(p.name == person.name);
    CHECK(p.is_active == person.is_active);
    CHECK(p.age.Value().value_or(0) == 42);

    // Delete
    auto const numRowsAffected = dm.Delete(person);
    CHECK(numRowsAffected == 1);

    CHECK(!dm.Query<Person>().Where(FieldNameOf<Member(Person::id)>, person.id.Value()).First().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlGuid", "[DataMapper]")
{
    auto dm = DataMapper();
    auto guid = SqlGuid::Create();

    auto expectedPersons = std::array {
        Person { .id = guid, .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person {},
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    CHECK(expectedPersons[0].id.Value() == guid);
    for (auto& person: expectedPersons)
    {
        REQUIRE(person.id.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Delete", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<Person>();
    for (auto const i: std::views::iota(0, 10))
    {
        dm.CreateExplicit(Person {
            .id = SqlGuid::Create(),
            .name = std::format("Person {}", i),
            .is_active = (i % 2) == 0,
            .age = i,
        });
    }
    CHECK(dm.Query<Person>().Count() == 10);
    dm.Query<Person>().Where(FieldNameOf<Member(Person::age)>, "<", 6).Delete();
    CHECK(dm.Query<Person>().Count() == 4);
    dm.Query<Person>().Delete();
    REQUIRE(dm.Query<Person>().Count() == 0);
    dm.Query<Person>().Delete();
}

TEST_CASE_METHOD(SqlTestFixture, "ExecuteDirect", "[DataMapper]")
{
    auto dm = DataMapper();
    auto stmt = SqlStatement(dm.Connection());

    auto const date =
        stmt.ExecuteDirectScalar<SqlDate>(std::format("SELECT {};", stmt.Connection().QueryFormatter().DateFunction()));
    auto const dateFromDataMapper =
        dm.Execute<SqlDate>(std::format("SELECT {};", stmt.Connection().QueryFormatter().DateFunction()));

    REQUIRE(date.has_value());
    REQUIRE(dateFromDataMapper.has_value());
    CHECK(date.value().value() == dateFromDataMapper.value().value());
}

TEST_CASE_METHOD(SqlTestFixture, "Query builder", "[DataMapper]")
{

    auto dm = DataMapper();

    auto const query = dm.Query().FromTable("That").Select().Distinct().Fields("a", "b").All();

    struct QueryResult
    {
        Field<SqlDateTime> date;
    };

    auto stmt = SqlStatement(dm.Connection());

    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
    {
        stmt.ExecuteDirect(R"SQL(
        CREATE TABLE "That" (
            "a" INT,
            "b" INT,
            "c" INT
        ))SQL");

        stmt.ExecuteDirect(R"SQL(INSERT INTO "That" ("a", "b", "c") VALUES (1, 2, 3))SQL");

        auto const result = dm.Query<QueryResult>(query);
        REQUIRE(result.size() == 1);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
