// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlQuery/Core.hpp>
#include <Lightweight/Utils.hpp>

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

TEST_CASE_METHOD(SqlTestFixture, "Query", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 }, // 1
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 }, // 0
    };

    dm->CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm->Create(person);

    SECTION("Count()")
    {
        auto const count = dm->Query<Person>().Where(FieldNameOf<Member(Person::is_active)>, "=", true).Count();
        CHECK(count == 2);

        auto const countAll = dm->Query<Person>().Count();
        CHECK(countAll == 4);
    }

    SECTION("All()")
    {
        auto const records = dm->Query<Person>()
                                 .Where(FieldNameOf<Member(Person::is_active)>, "=", true)
                                 .OrderBy(FieldNameOf<Member(Person::name)>)
                                 .All();

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[2]);
        CHECK(records[1] == expectedPersons[0]);
    }

    SECTION("All<&Person::age>()")
    {
        // clang-format off
        auto const ages = dm->Query<Person>()
                            .OrderBy(FieldNameOf<Member(Person::age)>, SqlResultOrdering::ASCENDING)
                            .All<Member(Person::age)>();
        // clang-format on

        CHECK(ages.size() == 4);
        CHECK(ages.at(0).value() == 24);
        CHECK(ages.at(1).value() == 36);
        CHECK(ages.at(2).value() == 42);
        CHECK(ages.at(3).value() == 69);
    }

    SECTION("First(n)")
    {
        auto const records = dm->Query<Person>()
                                 .Where(FieldNameOf<Member(Person::age)>, ">=", 30)
                                 .OrderBy(FieldNameOf<Member(Person::name)>, SqlResultOrdering::ASCENDING)
                                 .First(2);

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[2]);
        CHECK(records[1] == expectedPersons[3]);
    }

    SECTION("First()")
    {
        auto const record = dm->Query<Person>().Where(FieldNameOf<Member(Person::age)>, "<=", 24).First();

        REQUIRE(record.has_value());
        CHECK(record.value() == expectedPersons[1]);

        auto const impossible = dm->Query<Person>().Where(FieldNameOf<Member(Person::age)>, "=", -5).First();
        REQUIRE(impossible.has_value() == false);
    }

    SECTION("Range()")
    {
        // clang-format off
        auto const records = dm->Query<Person>()
                               .OrderBy(FieldNameOf<Member(Person::name)>, SqlResultOrdering::DESCENDING)
                               .Range(1, 2);
        // clang-format on

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[1]);
        CHECK(records[1] == expectedPersons[3]);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Query into First()", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm->CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm->Create(person);

    SECTION("Get()")
    {
        auto const record = dm->Query<Person>().Where(FieldNameOf<Member(Person::age)>, "=", 36).First();
        CHECK(record.has_value());
        CHECK(record.value() == expectedPersons[2]);
    }

    SECTION("Get() with non-existing record")
    {
        auto const record = dm->Query<Person>().Where(FieldNameOf<Member(Person::age)>, "=", -5).First();
        CHECK(record.has_value() == false);
    }

    SECTION("Count()")
    {
        auto const count = dm->Query<Person>().Where(FieldNameOf<Member(Person::age)>, "=", 24).Count();
        CHECK(count == 1);
    }

    SECTION("Single<T>()")
    {
        auto const result =
            dm->Query<Person>().Where(FieldNameOf<Member(Person::name)>, "=", "Jimbo Jones").First<Member(Person::age)>();
        CHECK(result.has_value());
        CHECK(result.value() == 69);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.All", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    dm->CreateTable<Person>();
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });

    auto const records = dm->Query<Person>()
                             .Where(FieldNameOf<Member(Person::is_active)>, "=", true)
                             .All<Member(Person::name), Member(Person::age)>();

    CHECK(records.size() == 1);
    CHECK(records[0].name == "John Doe");
    CHECK(records[0].age == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.First n", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 },
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 },
    };

    dm->CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm->Create(person);

    auto const records = dm->Query<Person>()
                             .Where(FieldNameOf<Member(Person::age)>, ">=", 30)
                             .OrderBy(FieldNameOf<Member(Person::name)>, SqlResultOrdering::ASCENDING)
                             .First<Member(Person::name), Member(Person::age)>(2);

    CHECK(records.size() == 2);

    CHECK(records[0].name.Value() == expectedPersons[2].name.Value());
    CHECK(records[0].age.Value() == expectedPersons[2].age.Value());

    CHECK(records[1].name.Value() == expectedPersons[3].name.Value());
    CHECK(records[1].age.Value() == expectedPersons[3].age.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.First", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    dm->CreateTable<Person>();
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });

    auto const record = dm->Query<Person>()
                            .Where(FieldNameOf<Member(Person::age)>, "<=", 24)
                            .First<Member(Person::name), Member(Person::is_active)>();

    REQUIRE(record.has_value());
    CHECK(record->name == "Jimmy John");             // name is queried
    CHECK(record->age.Value().has_value() == false); // age is not queried
    CHECK(record->is_active == false);               // is_active is queried

    auto const impossible = dm->Query<Person>()
                                .Where(FieldNameOf<Member(Person::age)>, "=", -5)
                                .First<Member(Person::name), Member(Person::is_active)>();
    REQUIRE(impossible.has_value() == false);
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySparse.Range", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    dm->CreateTable<Person>();
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 });
    dm->CreateExplicit(Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 });

    // clang-format off
    auto const records = dm->Query<Person>()
                           .OrderBy(FieldNameOf<Member(Person::name)>, SqlResultOrdering::ASCENDING)
                           .Range<Member(Person::name), Member(Person::age)>(1, 2);
    // clang-format on

    CHECK(records.size() == 2);
    CHECK(records[0].name == "Jimbo Jones");
    CHECK(records[0].age == 69);
    CHECK(records[1].name == "Jimmy John");
    CHECK(records[1].age == 24);
}

struct UserView
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
};

TEST_CASE_METHOD(SqlTestFixture, "iterate over database", "[SqlRowIterator]")
{
    auto dm = DataMapper::Create();
    dm->CreateTable<Person>();

    for (int i = 40; i <= 50; ++i)
    {
        auto person = Person {};
        person.name = std::format("John-{}", i);
        person.age = i;
        dm->Create(person);
    }

    auto retrievedPersons = std::vector<Person> {};
    for (auto&& person: SqlRowIterator<Person>(dm->Connection()))
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

// Simple struct, used for testing SELECT'ing into it
struct SimpleStruct
{
    uint64_t pkFromA {};
    uint64_t pkFromB {};
    SqlAnsiString<30> c1FromA;
    SqlAnsiString<30> c2FromA;
    SqlAnsiString<30> c1FromB;
    SqlAnsiString<30> c2FromB;
};

TEST_CASE_METHOD(SqlTestFixture, "Query: SELECT into simple struct", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    SqlStatement(dm->Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
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

    SqlStatement(dm->Connection()).ExecuteDirect(dm->FromTable("TableA").Insert().Set("c1", "a").Set("c2", "b"));
    SqlStatement(dm->Connection()).ExecuteDirect(dm->FromTable("TableB").Insert().Set("c1", "a").Set("c2", "c"));

    auto records =
        dm->Query<SimpleStruct>(dm->FromTable("TableA")
                                    .Select()
                                    .Field(QualifiedColumnName<"TableA.pk">)
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
    auto dm = DataMapper::Create();

    SqlStatement(dm->Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
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

    SqlStatement(dm->Connection()).ExecuteDirect(dm->FromTable("TableA").Insert().Set("c1", "a").Set("c2", "b"));
    SqlStatement(dm->Connection()).ExecuteDirect(dm->FromTable("TableB").Insert().Set("c1", "a").Set("c2", "c"));

    auto records =
        dm->Query<SqlVariantRow>(dm->FromTable("TableA").Select().Field("*").LeftOuterJoin("TableB", "c1", "c1").All());

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

TEST_CASE_METHOD(SqlTestFixture, "Strings with null", "[String]")
{
    auto dm = DataMapper::Create();
    dm->CreateTable<Person>();

    constexpr size_t SizeOfStringWithNull = 9;
    std::string nameWithNull("John Doe\0", SizeOfStringWithNull);
    CHECK(nameWithNull.size() == SizeOfStringWithNull);
    auto person = Person { .id = SqlGuid::Create(), .name = nameWithNull, .is_active = true, .age = std::nullopt };

    CHECK(person.name.Value() == nameWithNull);
    CHECK(person.name.Value().size() == SizeOfStringWithNull);
    CHECK(std::format("{}", person.name.Value()).size() == SizeOfStringWithNull - 1);
    CHECK(person.name.Value().ToString() == nameWithNull);
    CHECK(person.name.Value().ToStringView() == std::string_view(nameWithNull));

    dm->Create(person);

    auto retrievedPerson = dm->QuerySingle<Person>(person.id).value();
    CHECK(retrievedPerson.id == person.id);
    CHECK(retrievedPerson.name.Value() == "John Doe");
    CHECK(retrievedPerson.name.Value().size() == SizeOfStringWithNull - 1);

    auto retrieveByName = dm->Query<Person>().Where(FieldNameOf<Member(Person::name)>, "=", person.name.Value()).First();
    REQUIRE(retrieveByName.has_value());
    CHECK(retrieveByName.value().id == person.id);
    CHECK(retrieveByName.value().name.Value() == std::string("John Doe"));
    CHECK(retrieveByName.value().name.Value().ToString() == std::string("John Doe"));
    CHECK(retrieveByName.value().name.Value().ToStringView() == std::string_view("John Doe"));
    CHECK(retrieveByName.value().name.Value().size() == SizeOfStringWithNull - 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Query: Partial retriaval of the data", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    dm->CreateTable<Person>();

    constexpr auto StartAge = 20;
    constexpr auto EndAge = 21;

    for (auto const age: std::views::iota(StartAge, EndAge + 1))
    {
        auto person = Person {};
        person.name = std::format("John-{}", age);
        person.age = age;
        dm->Create(person);
        INFO("Created person: " << person);
    }

    auto records =
        dm->Query<SqlElements<1, 3>, Person>(dm->FromTable(RecordTableName<Person>)
                                                 .Select()
                                                 .Fields({ "name"sv, "age"sv })
                                                 .OrderBy(FieldNameOf<Member(Person::age)>, SqlResultOrdering::ASCENDING)
                                                 .All());

#if !defined(__cpp_lib_ranges_enumerate)
    int i { -1 };
    for (auto const& record: records)
    {
        ++i;
#else
    for (auto const [i, record]: records | std::views::enumerate)
    {
#endif
        auto const age = StartAge + i;
        CAPTURE(age);
        REQUIRE(record.name.Value() == std::format("John-{}", age));
        REQUIRE(record.age.Value() == age);
    }
}

struct SimpleStruct2
{
    std::u8string name;
    int age = 0;
};

static_assert(std::cmp_equal(RecordPrimaryKeyIndex<SimpleStruct2>, static_cast<size_t>(-1)));
static_assert(std::same_as<RecordPrimaryKeyType<SimpleStruct2>, void>);

std::ostream& operator<<(std::ostream& os, SimpleStruct2 const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "Query First: into simple struct", "[DataMapper]")
{
    auto dm = DataMapper::Create();

    SqlStatement(dm->Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable(RecordTableName<SimpleStruct2>).Column("name", NVarchar { 30 }).Column("age", Integer {});
    });

    dm->CreateExplicit(SimpleStruct2 { .name = u8"John", .age = 42 });

    auto result = dm->Query<SimpleStruct2>().First();

    REQUIRE(result.has_value());
    auto const& record = result.value();
    CAPTURE(record);
    CHECK(record.name == u8"John");
    CHECK(record.age == 42);
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
    auto dm = DataMapper::Create();
    dm->CreateTable<JoinA>();
    dm->CreateTable<JoinB>();
    dm->CreateTable<JoinC>();

    // fill with some data
    for (int const i: std::views::iota(1, 100))
    {
        auto a = JoinA { .value_a_first = i, .value_a_second = 10 + i, .value_a_third = 100 + i };
        auto b = JoinB { .a_id = 49 + i, .c_id = i };
        auto c =
            JoinC { .value_c_first = i, .value_c_second = 10 + i, .value_c_third = 100 + i, .value_c_fourth = 1000 + i };
        dm->Create(a);
        dm->Create(b);
        dm->Create(c);
    }

    auto const records = dm->Query<JoinA, JoinC>()
                             .InnerJoin<Member(JoinB::a_id), Member(JoinA::id)>()
                             .InnerJoin<Member(JoinC::id), Member(JoinB::c_id)>()
                             .All();

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

struct OptionalFields
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    std::optional<int> a;
};

template <>
constexpr bool detail::CanSafelyBindOutputColumns<OptionalFields>(SqlServerType /*sqlServerType*/) noexcept
{
    // Force disabling the output column binding for this type
    return false;
}

TEST_CASE_METHOD(SqlTestFixture, "Retrieve optional value without output-binding", "[DataMapper]")
{
    auto dm = DataMapper::Create();
    SqlStatement(dm->Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable(RecordTableName<OptionalFields>).Column("id", Guid {}).RequiredColumn("a", Integer {});
    });

    dm->CreateExplicit(OptionalFields { .id = SqlGuid::Create(), .a = 42 });

    auto const result = dm->Query<OptionalFields>().OrderBy(FieldNameOf<Member(OptionalFields::a)>).First();
    REQUIRE(result.has_value());
    REQUIRE(result.value().a.has_value());
    REQUIRE(result.value().a.value() == 42);
}

struct CustomBindingA
{
    static constexpr std::string_view TableName = "A";
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> number {};
    Field<SqlAnsiString<20>> name {};
    Field<SqlDynamicWideString<1000>> description {};
};

struct CustomBindingB
{
    static constexpr std::string_view TableName = "B";
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> title {};
    Field<SqlDateTime> date_time {};
    Field<uint64_t> a_id {};
    Field<uint64_t> c_id {};
};

struct CustomBindingC
{
    static constexpr std::string_view TableName = "C";
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<double> value {};
    Field<SqlAnsiString<20>> comment {};
};

TEST_CASE_METHOD(SqlTestFixture, "GetMultileTypesAsVectorOfTuples", "[DataMapper]")
{
    auto dm = DataMapper::Create();
    dm->CreateTable<CustomBindingA>();
    dm->CreateTable<CustomBindingB>();
    dm->CreateTable<CustomBindingC>();
    // fill with some data
    for (int const i: std::views::iota(1, 10))
    {
        auto a = CustomBindingA {
            .number = i,
            .name = std::format("Name-{}", i),
            .description = ToStdWideString(std::format("Description-{}", i)),
        };
        dm->Create(a);

        auto c = CustomBindingC {
            .value = i * 1.5,
            .comment = std::format("Comment-{}", i),
        };
        dm->Create(c);

        auto b = CustomBindingB {
            .title = std::format("Title-{}", i),
            .date_time = SqlDateTime::Now(),
            .a_id = a.id.Value(),
            .c_id = c.id.Value(),
        };
        dm->Create(b);
    }

    auto stmt = SqlStatement(dm->Connection());
    auto query = dm->FromTable(RecordTableName<CustomBindingA>)
                     .Select()
                     .Fields<CustomBindingA, CustomBindingB>()
                     .Field(QualifiedColumnName<"C.id">)
                     .Field(QualifiedColumnName<"C.comment">)
                     .InnerJoin<Member(CustomBindingB::a_id), Member(CustomBindingA::id)>()
                     .InnerJoin<Member(CustomBindingC::id), Member(CustomBindingB::c_id)>()
                     .OrderBy(QualifiedColumnName<"A.id">)
                     .All();

    struct PartOfC
    {
        uint64_t id {};
        SqlAnsiString<20> comment {};
    };

    auto const records = dm->QueryToTuple<CustomBindingA, CustomBindingB, PartOfC>(query);
    CHECK(!records.empty());

    for (auto const& [a, b, c]: records)
    {
        CHECK(a.id.Value());
        CHECK(b.id.Value());
        CHECK(c.id != 0);

        CHECK(a.name.Value().ToString().starts_with("Name-"));
        CHECK(a.description.Value().ToStringView().starts_with(L"Description-"));

        CHECK(b.date_time.Value().day() == SqlDateTime::Now().day());

        CHECK(c.comment.ToString().starts_with("Comment-"));
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
