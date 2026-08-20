// SPDX-License-Identifier: Apache-2.0

// This translation unit deliberately exercises the *deprecated* spelling of a through-relationship,
// which names the join record bare instead of wrapping it in Through<>. The deprecation warning is
// raised where Lightweight's own header names the deprecated helper, not at the declaration below,
// so it cannot be silenced around the individual uses - it has to be turned off for the whole file,
// before the library headers are pulled in. Nothing else belongs in here.
#if defined(_MSC_VER)
    #pragma warning(disable : 4996)
#else
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

using namespace Lightweight;

struct Author;
struct Book;
struct Authorship;

struct Author
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<32>> name {};

    // The deprecated spelling, kept compiling for one release.
    HasManyThrough<Book, Authorship> books {};
};

struct Book
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<32>> title {};

    // The spelling that replaces it.
    HasManyThrough<Author, Through<Authorship>> authors {};
};

struct Authorship
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(Author::id)> author {};
    BelongsTo<Member(Book::id)> book {};
};

// The marker itself.
static_assert(IsThrough<Through<Authorship>>);
static_assert(!IsThrough<Authorship>);
static_assert(std::same_as<Through<Authorship>::RecordType, Authorship>);
static_assert(std::same_as<ThroughRecordOf<Through<Authorship>>, Authorship>);
static_assert(std::same_as<ThroughRecordOf<Authorship>, Authorship>);

// Both spellings name the same join record, whichever relationship they are used in.
static_assert(std::same_as<HasManyThrough<Book, Authorship>::ThroughRecord, Authorship>);
static_assert(std::same_as<HasManyThrough<Book, Through<Authorship>>::ThroughRecord, Authorship>);
static_assert(std::same_as<HasOneThrough<Book, Authorship>::ThroughRecord, Authorship>);
static_assert(std::same_as<HasOneThrough<Book, Through<Authorship>>::ThroughRecord, Authorship>);

// The marker changes nothing else about the relationship.
static_assert(std::same_as<HasManyThrough<Book, Through<Authorship>>::ReferencedRecord, Book>);
static_assert(IsHasManyThrough<HasManyThrough<Book, Through<Authorship>>>);
static_assert(IsHasOneThrough<HasOneThrough<Book, Through<Authorship>>>);
static_assert(HasManyThrough<Book, Through<Authorship>, SqlRealName { "author_id" }>::OwnerSelector
              == SqlRealName { "author_id" });

namespace
{
// Local helper: `REQUIRE` the optional has a value, returning it.
// The explicit `if`-with-throw wrapper is what clang-tidy's
// `bugprone-unchecked-optional-access` analysis recognizes as a check -
// Catch2's `REQUIRE` is a macro it cannot reason about.
template <typename Record>
Record RequireValue(std::optional<Record> record)
{
    REQUIRE(record.has_value());
    if (!record.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return std::move(*record);
}
} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Through: both spellings load the same relationship", "[DataMapper][relations]")
{
    auto dm = DataMapper();

    dm.CreateTables<Author, Book, Authorship>();

    auto knuth = Author { .name = "Knuth" };
    dm.Create(knuth);

    auto dijkstra = Author { .name = "Dijkstra" };
    dm.Create(dijkstra);

    auto taocp = Book { .title = "TAOCP" };
    dm.Create(taocp);

    auto structured = Book { .title = "Structured Programming" };
    dm.Create(structured);

    auto const attribute = [&](Author const& author, Book const& book) {
        auto authorship = Authorship { .author = author, .book = book };
        dm.Create(authorship);
    };

    // Knuth wrote both books, Dijkstra only the second one.
    attribute(knuth, taocp);
    attribute(knuth, structured);
    attribute(dijkstra, structured);

    SECTION("deprecated bare spelling")
    {
        REQUIRE(knuth.books.Count() == 2);

        auto titles = std::set<std::string> {};
        knuth.books.Each([&](Book const& book) { titles.emplace(book.title.Value()); });
        CHECK(titles == std::set<std::string> { "Structured Programming", "TAOCP" });
    }

    SECTION("Through<> spelling")
    {
        REQUIRE(structured.authors.Count() == 2);

        auto names = std::set<std::string> {};
        structured.authors.Each([&](Author const& author) { names.emplace(author.name.Value()); });
        CHECK(names == std::set<std::string> { "Dijkstra", "Knuth" });
    }

    SECTION("the two spellings agree on the same data, when auto-loaded from a queried record")
    {
        auto queriedDijkstra = RequireValue(dm.QuerySingle<Author>(dijkstra.id.Value()));
        dm.ConfigureRelationAutoLoading(queriedDijkstra);

        auto queriedTaocp = RequireValue(dm.QuerySingle<Book>(taocp.id.Value()));
        dm.ConfigureRelationAutoLoading(queriedTaocp);

        REQUIRE(queriedDijkstra.books.Count() == 1);
        REQUIRE(queriedTaocp.authors.Count() == 1);
        CHECK(queriedDijkstra.books.At(0).title.Value() == "Structured Programming");
        CHECK(queriedTaocp.authors.At(0).name.Value() == "Knuth");
    }
}
