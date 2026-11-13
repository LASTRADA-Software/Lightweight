// SPDX-License-Identifier: Apache-2.0
//
// Regression coverage for issue #556: a record carrying an explicit `Lightweight::Description<>`
// specialization — as emitted by ddl2cpp — must still auto-load its relations.
//
// `RecordMemberCount` prefers `Description<Record>::FieldCount` over reflection whenever a
// specialization exists, so a descriptor that lists only the columns hides the relation members
// from `EnumerateRecordMembers`. `ConfigureRelationAutoLoading` then installs no loader and the
// first access throws `SqlRequireLoadedError`. No test instantiated that combination before, which
// is why CI stayed green while every ddl2cpp-generated model had the defect.

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Lightweight;

struct DescribedArtist;
struct DescribedAlbum;

// Mirrors what ddl2cpp emits: columns first, then the relation members, with an explicit descriptor
// specialization below.
struct DescribedArtist
{
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "ArtistId" }> artistId {};
    Field<SqlAnsiString<64>, SqlRealName { "Name" }> name;

    HasMany<DescribedAlbum> albums {};
};

struct DescribedAlbum
{
    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "AlbumId" }> albumId {};
    Field<SqlAnsiString<64>, SqlRealName { "Title" }> title;
    BelongsTo<Member(DescribedArtist::artistId), SqlRealName { "ArtistId" }> artist;
};

template <>
struct Lightweight::Description<DescribedArtist>
{
    static constexpr std::size_t FieldCount = 3;
    using Members =
        Lightweight::RecordMemberList<&DescribedArtist::artistId, &DescribedArtist::name, &DescribedArtist::albums>;
    static constexpr std::array<std::string_view, 3> FieldNames = { "ArtistId", "Name", "albums" };
};

template <>
struct Lightweight::Description<DescribedAlbum>
{
    static constexpr std::size_t FieldCount = 3;
    using Members = Lightweight::RecordMemberList<&DescribedAlbum::albumId, &DescribedAlbum::title, &DescribedAlbum::artist>;
    static constexpr std::array<std::string_view, 3> FieldNames = { "AlbumId", "Title", "ArtistId" };
};

// The descriptor enumerates every member, but only the members that map onto a result-set column
// contribute to a projection - listing the relation must not widen the record's column count.
static_assert(RecordMemberCount<DescribedArtist> == 3);
static_assert(RecordColumnCount<DescribedArtist> == 2);
static_assert(RecordStorageFieldCount<DescribedArtist> == 2);

// RecordMemberList is what a descriptor stands on, so how it indexes its packs must not show: a member
// keeps its exact type, MemberAt yields the member itself, and constness follows the record.
using DescribedAlbumMembers = Description<DescribedAlbum>::Members;
static_assert(DescribedAlbumMembers::Count == 3);
static_assert(std::same_as<DescribedAlbumMembers::TypeAt<0>, decltype(DescribedAlbum::albumId)>);
static_assert(std::same_as<DescribedAlbumMembers::TypeAt<1>, decltype(DescribedAlbum::title)>);
static_assert(std::same_as<DescribedAlbumMembers::TypeAt<2>, decltype(DescribedAlbum::artist)>);
static_assert(std::same_as<decltype(DescribedAlbumMembers::MemberAt<1>(std::declval<DescribedAlbum&>())),
                           decltype(DescribedAlbum::title)&>);
static_assert(std::same_as<decltype(DescribedAlbumMembers::MemberAt<1>(std::declval<DescribedAlbum const&>())),
                           decltype(DescribedAlbum::title) const&>);

TEST_CASE("RecordMemberList reaches each member in declaration order", "[DataMapper][Description]")
{
    auto album = DescribedAlbum {};
    auto const& constAlbum = album;

    CHECK(static_cast<void const*>(&DescribedAlbumMembers::MemberAt<0>(album)) == &album.albumId);
    CHECK(static_cast<void const*>(&DescribedAlbumMembers::MemberAt<1>(album)) == &album.title);
    CHECK(static_cast<void const*>(&DescribedAlbumMembers::MemberAt<2>(album)) == &album.artist);
    CHECK(static_cast<void const*>(&DescribedAlbumMembers::MemberAt<2>(constAlbum)) == &album.artist);
    CHECK(static_cast<void const*>(&GetRecordMemberAt<1>(album)) == &album.title);

    auto valueOrder = std::vector<void const*> {};
    EnumerateRecordMembers(album, [&]<std::size_t I>(auto& member) { valueOrder.emplace_back(&member); });
    CHECK(valueOrder == std::vector<void const*> { &album.albumId, &album.title, &album.artist });

    auto typeOrder = std::vector<std::size_t> {};
    EnumerateRecordMembers<DescribedAlbum>([&]<std::size_t I, typename MemberType>() {
        static_assert(std::same_as<MemberType, DescribedAlbumMembers::TypeAt<I>>);
        typeOrder.emplace_back(I);
    });
    CHECK(typeOrder == std::vector<std::size_t> { 0, 1, 2 });

    using Mask = std::integer_sequence<std::size_t, 2, 0>;
    auto maskedOrder = std::vector<void const*> {};
    EnumerateRecordMembers<Mask>(album, [&]<std::size_t I>(auto& member) { maskedOrder.emplace_back(&member); });
    CHECK(maskedOrder == std::vector<void const*> { &album.artist, &album.albumId });
}

TEST_CASE_METHOD(SqlTestFixture, "Description-carrying record auto-loads its HasMany", "[DataMapper][relations][issue556]")
{
    auto dm = DataMapper();
    dm.CreateTables<DescribedArtist, DescribedAlbum>();

    auto artist = DescribedArtist { .name = "AC/DC" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(artist);

    // Create(), not CreateExplicit(): an integer AutoAssign key is computed on insert, so two
    // explicit inserts would both land on the same primary key.
    auto album1 = DescribedAlbum { .title = "Let There Be Rock", .artist = artist };
    dm.Create<DataMapperOptions { .loadRelations = false }>(album1);
    auto album2 = DescribedAlbum { .title = "Powerage", .artist = artist };
    dm.Create<DataMapperOptions { .loadRelations = false }>(album2);

    SECTION("QuerySingle configures the loader")
    {
        auto const queried = dm.QuerySingle<DescribedArtist>(artist.artistId);
        REQUIRE(queried.has_value());
        // Unreachable after REQUIRE, but it is what makes the access below provably checked: the
        // optional-access analysis does not model Catch2's assertion macros.
        if (!queried.has_value())
            return;
        auto const& loaded = *queried;

        auto const albums = loaded.albums.All();
        REQUIRE(albums.has_value());
        auto titles = std::set<std::string> {};
        for (auto const& album: albums->get())
            titles.emplace(album->title.Value());

        CHECK(titles == std::set<std::string> { "Let There Be Rock", "Powerage" });
        CHECK(loaded.albums.Count().value() == 2);
    }

    SECTION("Query with loadRelations configures the loader")
    {
        auto const queried = dm.Query<DescribedArtist, DataMapperOptions { .loadRelations = true }>()
                                 .Where(FieldNameOf<Member(DescribedArtist::name)>, "=", "AC/DC")
                                 .First();
        REQUIRE(queried.has_value());
        if (!queried.has_value())
            return;

        CHECK(queried->albums.Count().value() == 2);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Description-carrying record still projects only its columns",
                 "[DataMapper][relations][issue556]")
{
    // The descriptor now lists a member that carries no storage; CREATE TABLE and the SELECT
    // projection must keep skipping it.
    auto dm = DataMapper();
    dm.CreateTables<DescribedArtist, DescribedAlbum>();

    for (auto const& statement: dm.CreateTableString<DescribedArtist>(dm.Connection().ServerType()))
        CHECK(!statement.contains("albums"));

    auto artist = DescribedArtist { .name = "Aerosmith" };
    dm.Create<DataMapperOptions { .loadRelations = false }>(artist);

    auto const all = dm.Query<DescribedArtist>().All();
    REQUIRE(all.size() == 1);
    CHECK(all.front().name.Value() == "Aerosmith");
}
