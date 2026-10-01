// SPDX-License-Identifier: Apache-2.0

// Required fields: a NOT NULL column that neither the data mapper nor the database assigns must be
// given a value when the record is created, and omitting it is a compile error.

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Lightweight;

struct RequiredBook;

struct RequiredAuthor
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name;
    Field<SqlAnsiString<40>> email;
    Field<int> loginCount;
    Field<bool> isActive { true };
    Field<std::optional<int>> age {};

    HasMany<RequiredBook> books {};
};

struct RequiredBook
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> title;
    BelongsTo<Member(RequiredAuthor::id), SqlRealName { "author_id" }> author;
};

// The same shape as ddl2cpp output: a record carrying an explicit descriptor.
struct DescribedRequiredAuthor
{
    static constexpr std::string_view TableName = "RequiredAuthor";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name;
    Field<SqlAnsiString<40>> email;
    Field<int> loginCount;
    Field<bool> isActive { true };
    Field<std::optional<int>> age {};
};

template <>
struct Lightweight::Description<DescribedRequiredAuthor>
{
    static constexpr std::size_t FieldCount = 6;
    using Members = Lightweight::RecordMemberList<&DescribedRequiredAuthor::id,
                                                  &DescribedRequiredAuthor::name,
                                                  &DescribedRequiredAuthor::email,
                                                  &DescribedRequiredAuthor::loginCount,
                                                  &DescribedRequiredAuthor::isActive,
                                                  &DescribedRequiredAuthor::age>;
    static constexpr std::array<std::string_view, 6> FieldNames = { "id", "name", "email", "loginCount", "isActive", "age" };
};

namespace
{

// Each concept names one way of creating a record. They are templates so that an ill-formed
// initialization yields `false` rather than a hard error.
template <typename Author>
concept CreatableWithEveryRequiredField =
    requires { Author { .name = "John", .email = "john@example.com", .loginCount = 0 }; };

template <typename Author>
concept CreatableWithoutEmail = requires { Author { .name = "John", .loginCount = 0 }; };

template <typename Author>
concept CreatableWithoutLoginCount = requires { Author { .name = "John", .email = "john@example.com" }; };

template <typename Book>
concept CreatableWithoutAuthor = requires { Book { .title = "Title" }; };

template <typename Book, typename Author>
concept CreatableWithAuthor = requires(Author const& author) { Book { .title = "Title", .author = author }; };

// A NOT NULL column is required ...
static_assert(Field<int>::IsRequired);
static_assert(Field<SqlAnsiString<30>, SqlRealName { "renamed" }>::IsRequired);
static_assert(!std::default_initializable<Field<int>>);
static_assert(std::constructible_from<Field<int>, int>);

// ... unless it is nullable, or a primary key, which the data mapper or the database assigns.
static_assert(!Field<std::optional<int>>::IsRequired);
static_assert(!Field<int, PrimaryKey::AutoAssign>::IsRequired);
static_assert(!Field<uint64_t, PrimaryKey::ServerSideAutoIncrement>::IsRequired);
static_assert(!Field<int, PrimaryKey::Manual>::IsRequired);
static_assert(std::default_initializable<Field<std::optional<int>>>);
static_assert(std::default_initializable<Field<int, PrimaryKey::AutoAssign>>);
static_assert(std::default_initializable<Field<int, PrimaryKey::Manual, SqlRealName { "renamed" }>>);

// The same holds for the foreign key of a relationship.
static_assert(decltype(RequiredBook::author)::IsRequired);
static_assert(!BelongsTo<Member(RequiredAuthor::id), SqlRealName { "editor_id" }, SqlNullable::Null>::IsRequired);
static_assert(!std::default_initializable<decltype(RequiredBook::author)>);
static_assert(
    std::default_initializable<BelongsTo<Member(RequiredAuthor::id), SqlRealName { "editor_id" }, SqlNullable::Null>>);

// A record cannot be created without its required members ...
static_assert(!std::default_initializable<RequiredAuthor>);
static_assert(!std::default_initializable<RequiredBook>);
static_assert(!std::default_initializable<DescribedRequiredAuthor>);
static_assert(!CreatableWithoutEmail<RequiredAuthor>);
static_assert(!CreatableWithoutLoginCount<RequiredAuthor>);
static_assert(!CreatableWithoutAuthor<RequiredBook>);

// ... while a member with a default member initializer, an optional one, a primary key and a
// relation may all be left out.
static_assert(CreatableWithEveryRequiredField<RequiredAuthor>);
static_assert(CreatableWithEveryRequiredField<DescribedRequiredAuthor>);
static_assert(CreatableWithAuthor<RequiredBook, RequiredAuthor>);

// Reflection sees such records in full.
static_assert(RecordMemberCount<RequiredAuthor> == 7);
static_assert(RecordMemberCount<RequiredBook> == 3);
static_assert(RecordMemberCount<DescribedRequiredAuthor> == 6);
static_assert(DataMapperRecord<RequiredAuthor>);
static_assert(DataMapperRecord<RequiredBook>);

struct RequiredFieldFixture: SqlTestFixture
{
    DataMapper dm;

    RequiredAuthor john { .name = "John", .email = "john@example.com", .loginCount = 3 };
    RequiredAuthor jane { .name = "Jane", .email = "jane@example.com", .loginCount = 7, .age = 42 };

    RequiredFieldFixture()
    {
        dm.CreateTables<RequiredAuthor, RequiredBook>();
        dm.Create(john);
        dm.Create(jane);
    }
};

} // namespace

TEST_CASE_METHOD(RequiredFieldFixture, "RequiredField: create and query", "[DataMapper][RequiredField]")
{
    SECTION("QuerySingle")
    {
        auto const loaded = dm.QuerySingle<RequiredAuthor>(john.id);
        REQUIRE(loaded.has_value());
        if (loaded.has_value())
        {
            CHECK(loaded->name == "John");
            CHECK(loaded->email == "john@example.com");
            CHECK(loaded->loginCount == 3);
            CHECK(loaded->isActive == true);
            CHECK(!loaded->age.Value().has_value());
        }
    }

    SECTION("All")
    {
        auto const authors = dm.Query<RequiredAuthor>().OrderBy(FieldNameOf<Member(RequiredAuthor::id)>).All();
        REQUIRE(authors.size() == 2);
        CHECK(authors[0].name == "John");
        CHECK(authors[1].name == "Jane");
        CHECK(authors[1].loginCount == 7);
        CHECK(authors[1].age == 42);
    }

    SECTION("First")
    {
        auto const author = dm.Query<RequiredAuthor>().Where(FieldNameOf<Member(RequiredAuthor::name)>, "=", "Jane").First();
        REQUIRE(author.has_value());
        if (author.has_value())
            CHECK(author->email == "jane@example.com");
    }

    SECTION("First with selected fields")
    {
        auto const author = dm.Query<RequiredAuthor>()
                                .Where(FieldNameOf<Member(RequiredAuthor::name)>, "=", "Jane")
                                .First<Member(RequiredAuthor::name), Member(RequiredAuthor::loginCount)>();
        REQUIRE(author.has_value());
        if (author.has_value())
        {
            CHECK(author->name == "Jane");
            CHECK(author->loginCount == 7);
        }
    }

    SECTION("row iterator")
    {
        auto names = std::vector<std::string> {};
        for (auto&& author: SqlRowIterator<RequiredAuthor>(dm.Connection()))
            names.emplace_back(author.name.Value().str());
        std::ranges::sort(names);
        CHECK(names == std::vector<std::string> { "Jane", "John" });
    }

    SECTION("update")
    {
        john.loginCount = 4;
        dm.Update(john);
        auto const loaded = dm.QuerySingle<RequiredAuthor>(john.id);
        REQUIRE(loaded.has_value());
        if (loaded.has_value())
            CHECK(loaded->loginCount == 4);
    }

    SECTION("record with a descriptor")
    {
        auto alice = DescribedRequiredAuthor { .name = "Alice", .email = "alice@example.com", .loginCount = 1 };
        dm.Create(alice);

        auto const loaded = dm.QuerySingle<DescribedRequiredAuthor>(alice.id);
        REQUIRE(loaded.has_value());
        if (loaded.has_value())
        {
            CHECK(loaded->name == "Alice");
            CHECK(loaded->email == "alice@example.com");
            CHECK(loaded->loginCount == 1);
        }

        auto const all = dm.Query<DescribedRequiredAuthor>().All();
        CHECK(all.size() == 3);
    }
}

TEST_CASE_METHOD(RequiredFieldFixture, "RequiredField: relations", "[DataMapper][RequiredField]")
{
    auto first = RequiredBook { .title = "First", .author = john };
    auto second = RequiredBook { .title = "Second", .author = john.id.Value() };
    dm.Create(first);
    dm.Create(second);

    SECTION("BelongsTo loads on demand")
    {
        auto loaded = dm.QuerySingle<RequiredBook>(second.id);
        REQUIRE(loaded.has_value());
        if (loaded.has_value())
        {
            CHECK(loaded->title == "Second");
            CHECK(loaded->author->name == "John");
        }
    }

    SECTION("BelongsTo loads eagerly")
    {
        auto books = dm.Query<RequiredBook>()
                         .With<Member(RequiredBook::author)>()
                         .OrderBy(FieldNameOf<Member(RequiredBook::id)>)
                         .All();
        REQUIRE(books.size() == 2);
        REQUIRE(books[0].author.LoadedRecord() != nullptr);
        CHECK(books[0].author.LoadedRecord()->email == "john@example.com");
    }

    SECTION("HasMany")
    {
        auto author = dm.QuerySingle<RequiredAuthor>(john.id);
        REQUIRE(author.has_value());
        if (author.has_value())
        {
            CHECK(author->books.Count().value() == 2);

            auto titles = std::vector<std::string> {};
            for (auto const& book: author->books.All().value().get())
                titles.emplace_back(book->title.Value().str());
            std::ranges::sort(titles);
            CHECK(titles == std::vector<std::string> { "First", "Second" });
        }
    }

    SECTION("joined query")
    {
        auto const query = dm.FromTable(RecordTableName<RequiredBook>)
                               .Select()
                               .Fields<RequiredBook, RequiredAuthor>()
                               .InnerJoin<Member(RequiredAuthor::id), Member(RequiredBook::author)>()
                               .OrderBy(SqlQualifiedTableColumnName {
                                   .tableName = RecordTableName<RequiredBook>,
                                   .columnName = FieldNameOf<Member(RequiredBook::id)>,
                               })
                               .All();
        auto const rows = dm.Query<RequiredBook, RequiredAuthor>(query);
        REQUIRE(rows.size() == 2);
        auto const& [book, author] = rows[0];
        CHECK(book.title == "First");
        CHECK(author.name == "John");
    }

    SECTION("a relationship given at construction is written by Update")
    {
        // A required relationship cannot be assigned after the fact on a fresh record, so the
        // constructors have to mark it modified for Update() to write it.
        auto byKey = RequiredBook { .id = first.id, .title = "First", .author = jane.id.Value() };
        CHECK(byKey.author.IsModified());
        dm.Update(byKey);
        auto const loaded = dm.QuerySingle<RequiredBook>(first.id);
        REQUIRE(loaded.has_value());
        if (loaded.has_value())
            CHECK(loaded->author.Value() == jane.id.Value());

        auto const byRecord = RequiredBook { .id = second.id, .title = "Second", .author = jane };
        CHECK(byRecord.author.IsModified());
    }

    SECTION("EmplaceRecord")
    {
        auto& emplaced = first.author.EmplaceRecord();
        emplaced.name = "Emplaced";
        CHECK(first.author->name == "Emplaced");
    }
}
