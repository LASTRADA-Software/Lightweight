// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlQuery/Core.hpp>

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
    auto dm = DataMapper {};

    auto expectedPersons = std::array {
        Person { .id = SqlGuid::Create(), .name = "John Doe", .is_active = true, .age = 42 },
        Person { .id = SqlGuid::Create(), .name = "Jimmy John", .is_active = false, .age = 24 }, // 1
        Person { .id = SqlGuid::Create(), .name = "Jane Doe", .is_active = true, .age = 36 },
        Person { .id = SqlGuid::Create(), .name = "Jimbo Jones", .is_active = false, .age = 69 }, // 0
    };

    dm.CreateTable<Person>();
    for (auto& person: expectedPersons)
        dm.Create(person);

    SECTION("Query.Count")
    {
        auto const count = dm.Query<Person>().Where(FieldNameOf<&Person::is_active>, "=", true).Count();
        CHECK(count == 2);

        auto const countAll = dm.Query<Person>().Count();
        CHECK(countAll == 4);
    }

    SECTION("Query.All")
    {
        auto const records =
            dm.Query<Person>().Where(FieldNameOf<&Person::is_active>, "=", true).OrderBy(FieldNameOf<&Person::name>).All();

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[2]);
        CHECK(records[1] == expectedPersons[0]);
    }

    SECTION("Query.First n")
    {
        auto const records = dm.Query<Person>()
                                 .Where(FieldNameOf<&Person::age>, ">=", 30)
                                 .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::ASCENDING)
                                 .First(2);

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[2]);
        CHECK(records[1] == expectedPersons[3]);
    }

    SECTION("Query.First")
    {
        auto const record = dm.Query<Person>().Where(FieldNameOf<&Person::age>, "<=", 24).First();

        REQUIRE(record.has_value());
        CHECK(record.value() == expectedPersons[1]);

        auto const impossible = dm.Query<Person>().Where(FieldNameOf<&Person::age>, "=", -5).First();
        REQUIRE(impossible.has_value() == false);
    }

    SECTION("Query.Range")
    {
        // clang-format off
        auto const records = dm.Query<Person>()
                               .OrderBy(FieldNameOf<&Person::name>, SqlResultOrdering::DESCENDING)
                               .Range(1, 2);
        // clang-format on

        CHECK(records.size() == 2);
        CHECK(records[0] == expectedPersons[1]);
        CHECK(records[1] == expectedPersons[3]);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "QuerySingle", "[DataMapper]")
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

    SECTION("Get()")
    {
        auto const record = dm.QuerySingle<Person>().Where(FullFieldNameOf<&Person::age>, "=", 36).Get();
        CHECK(record.has_value());
        CHECK(record.value() == expectedPersons[2]);
    }

    SECTION("Get() with non-existing record")
    {
        auto const record = dm.QuerySingle<Person>().Where(FullFieldNameOf<&Person::age>, "=", -5).Get();
        CHECK(record.has_value() == false);
    }

    SECTION("Count()")
    {
        auto const count = dm.QuerySingle<Person>().Where(FieldNameOf<&Person::age>, "=", 24).Count();
        CHECK(count == 1);
    }

    SECTION("Single<T>()")
    {
        auto const result =
            dm.QuerySingle<Person>().Where(FieldNameOf<&Person::name>, "=", "Jimbo Jones").Scalar<&Person::age>();
        CHECK(result.has_value());
        CHECK(result.value() == 69);
    }
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

struct UserView
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
};

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
    auto dm = DataMapper {};
    SqlStatement(dm.Connection()).MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        using namespace SqlColumnTypeDefinitions;
        migration.CreateTable(RecordTableName<OptionalFields>).Column("id", Guid {}).RequiredColumn("a", Integer {});
    });

    dm.CreateExplicit(OptionalFields { .id = SqlGuid::Create(), .a = 42 });

    auto const result = dm.Query<OptionalFields>().OrderBy(FieldNameOf<&OptionalFields::a>).First();
    REQUIRE(result.has_value());
    REQUIRE(result.value().a.has_value());
    REQUIRE(result.value().a.value() == 42);
}

// NOLINTEND(bugprone-unchecked-optional-access)
