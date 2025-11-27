// SPDX-License-Identifier: Apache-2.0

#include "entities/Album.hpp"
#include "entities/Artist.hpp"
#include "entities/Customer.hpp"
#include "entities/Employee.hpp"
#include "entities/Genre.hpp"
#include "entities/Invoice.hpp"
#include "entities/Invoiceline.hpp"
#include "entities/Mediatype.hpp"
#include "entities/Playlist.hpp"
#include "entities/Playlisttrack.hpp"
#include "entities/Track.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlLogger.hpp>

#include <iterator>
#include <print>

using namespace Lightweight;

static std::string GetEnvironmentVariable(std::string const& name)
{
#if defined(_MSC_VER)
    char* envBuffer = nullptr;
    size_t envBufferLen = 0;
    _dupenv_s(&envBuffer, &envBufferLen, name.data());
    if (envBuffer && *envBuffer)
    {
        std::string result { envBuffer };
        free(envBuffer); // free the allocated memory
        return result;
    }
#else
    if (auto const* s = std::getenv(name.data()); s && *s)
        return std::string { s };
#endif
    return {};
}

template <typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args)
{
    std::println(fmt, std::forward<Args>(args)...);
}

template <typename Entity>
void DumpTable(DataMapper& dm, size_t limit = 1)
{
    auto entries = dm.Query<Entity>().First(limit);
    for (auto const& entry: entries)
    {
        Log("{}", DataMapper::Inspect(entry));
    }
}

int main()
{
    if (auto const odbcConnectionString = GetEnvironmentVariable("ODBC_CONNECTION_STRING"); !odbcConnectionString.empty())
    {
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { odbcConnectionString });
    }
    else
    {
        SqlConnection::SetDefaultConnectionString(SqlConnectionString {
            "DRIVER={ODBC Driver 18 for SQL "
            "Server};SERVER=localhost;UID=SA;PWD=BlahThat.;TrustServerCertificate=yes;DATABASE=LightweightTest" });
    }

    auto dm = DataMapper();

    // helper function to create std::string from string_view<char16_t>
    auto const toString = [](std::basic_string_view<char16_t> str) {
        auto const u8Str = ToUtf8(str);
        return std::string(reinterpret_cast<char const*>(u8Str.data()), u8Str.size());
    };

    // get all employees
    auto const empoyees = dm.Query<Employee>().All();
    for (auto const& employee: empoyees)
    {
        Log("EmployeeId: {}, FirstName: {}, LastName: {}",
            employee.EmployeeId.Value(),
            toString(employee.FirstName.Value().c_str()),
            toString(employee.LastName.Value().c_str()));
    }

    // directly iterate over elements
    int numberOfAlbums = 0;
    for (auto const& album: SqlRowIterator<Album>(dm.Connection()))
    {
        Log("{}", toString(album.Title.Value().c_str()));
        ++numberOfAlbums;
    }
    Log("Iterated over {} Albums", numberOfAlbums);

    // select album with the title "Mozart Gala: Famous Arias"
    auto album = dm.Query<Album, DataMapperOptions { .loadRelations = true }>() // NOLINT(bugprone-unchecked-optional-access)
                     .Where(FieldNameOf<&Album::Title>, "=", "Mozart Gala: Famous Arias")
                     .First()
                     .value();

    Log("AlbumId: {}, Title: {}", album.AlbumId.Value(), album.ArtistId.Value());

    // we can use BelongsTo<&Artist::ArtistId, SqlRealName{"ArtistId"}> c_ArtistId member
    // to get access to the artist entry in the database, using dereference operator
    // after configuring the relations
    Log("Artist name: {}",
        toString(album.ArtistId->Name.Value().value().c_str())); // NOLINT(bugprone-unchecked-optional-access)

    {
        // get an artist with the name "Sir Georg Solti, Sumi Jo & Wiener Philharmoniker"
        auto artist = dm.Query<Artist>() // NOLINT(bugprone-unchecked-optional-access)
                          .Where(FieldNameOf<&Artist::Name>, "=", "Red Hot Chili Peppers")
                          .First()
                          .value();

        Log("ArtistId: {}, Name: {}", // NOLINT(bugprone-unchecked-optional-access)
            artist.ArtistId.Value(),
            toString(artist.Name.Value().value().c_str())); // NOLINT(bugprone-unchecked-optional-access)

        // get albums of the artist
        auto albums = dm.Query<Album>().Where(FieldNameOf<&Album::ArtistId>, "=", artist.ArtistId.Value()).All();

        Log("got {} albums", albums.size());

        auto albumIds = albums | std::views::transform([](auto const& album) { return album.AlbumId.Value(); });
        // get all tracks from all albums
        auto tracks = dm.Query<Track>().WhereIn(FieldNameOf<&Track::AlbumId>, albumIds).All();

        Log("got {} tracks", tracks.size());

        // iterate over all tracks and print song names
        for (auto const& track: tracks)
        {
            Log("TrackId: {}, Name: {}, Bytes: {} , UnitPrice: {}",
                track.TrackId.Value(),
                toString(track.Name.Value().c_str()),
                track.Bytes.ValueOr(0),
                track.UnitPrice.Value().ToString());
        }

        for (auto& track: dm.Query<Track>().All())
        {
            dm.ConfigureRelationAutoLoading(track);
            // BelogsTo relation loading
            Log("Track Name: {}. Media type: {}. Genre: {}. Album id: {}. Artist name: {}",
                toString(track.Name.Value().ToStringView()),
                toString(track.MediaTypeId->Name.ValueOr(u"").ToStringView()),
                toString(track.GenreId->Name.ValueOr(u"").ToStringView()),
                toString(track.AlbumId->Title.Value().ToStringView()),
                toString(track.AlbumId->ArtistId->Name.ValueOr(u"").ToStringView()));
        }
    }

    {
        // get pair of customer and employee
        auto records = dm.Query<Customer, Employee>().InnerJoin<&Employee::EmployeeId, &Customer::SupportRepId>().All();

        for (auto const& [customer, employee]: records)
        {
            Log("CustomerId: {}, FirstName: {}, LastName: {}",
                customer.CustomerId.Value(),
                toString(customer.FirstName.Value().c_str()),
                toString(customer.LastName.Value().c_str()));
            Log("EmployeeId: {}, FirstName: {}, LastName: {}",
                employee.EmployeeId.Value(),
                toString(employee.FirstName.Value().c_str()),
                toString(employee.LastName.Value().c_str()));
        }
    }

    {
        // get one employee
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto employee = dm.Query<Employee>().Where(FieldNameOf<&Employee::EmployeeId>, 1).First().value();
        // NOLINTEND(bugprone-unchecked-optional-access)
        Log(" {} ", employee.HireDate.Value().value()); // NOLINT(bugprone-unchecked-optional-access)
        // update hiring date to current date
        employee.HireDate = SqlDateTime::Now();
        dm.Update(employee);
    }

    // Iterate over all entities in the database and print ther content
    DumpTable<Album>(dm);
    DumpTable<Artist>(dm);
    DumpTable<Customer>(dm);
    DumpTable<Employee>(dm);
    DumpTable<Genre>(dm);
    DumpTable<Invoice>(dm);
    DumpTable<Invoiceline>(dm);
    DumpTable<Mediatype>(dm);
    DumpTable<Playlist>(dm);
    DumpTable<Playlisttrack>(dm);
    DumpTable<Track>(dm);
}
