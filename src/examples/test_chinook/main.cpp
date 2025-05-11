#include "Lightweight/SqlLogger.hpp"
#include "entities/Album.hpp"
#include "entities/Customer.hpp"
#include "entities/Employee.hpp"
#include "entities/Track.hpp"

#include <Lightweight/Lightweight.hpp>

#include <print>

int main()
{

    SqlConnection::SetDefaultConnectionString(SqlConnectionString {
        "DRIVER={ODBC Driver 18 for SQL "
     //     "Server};SERVER=localhost;UID=SA;PWD=Qwerty1.;TrustServerCertificate=yes;DATABASE=LightweightTest" });
   "Server};SERVER=localhost;UID=SA;PWD=BlahThat.;TrustServerCertificate=yes;DATABASE=LightweightTest" });
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
        std::println("EmployeeId: {}, FirstName: {}, LastName: {}",
                     employee.EmployeeId.Value(),
                     toString(employee.FirstName.Value().c_str()),
                     toString(employee.LastName.Value().c_str()));
    }

    // directly iterate over elements
    int numberOfAlbums = 0;
    for (auto const& album: SqlRowIterator<Album>(dm.Connection()))
    {
        std::println("{}", toString(album.Title.Value().c_str()));
        ++numberOfAlbums;
    }
    std::println("Iterated over {} Albums", numberOfAlbums);

    // select album with the title "Mozart Gala: Famous Arias"
    auto album = dm.QuerySingle<Album>().Where(FieldNameOf<&Album::Title>, "=", "Mozart Gala: Famous Arias").Get().value();

    std::println("AlbumId: {}, Title: {}", album.AlbumId.Value(), album.ArtistId.Value());

    // we can use BelongsTo<&Artist::ArtistId, SqlRealName{"ArtistId"}> c_ArtistId member
    // to get access to the artist entry in the database, using dereference operator
    // after configuring the relations
    dm.ConfigureRelationAutoLoading(album);
    std::println("Artist name: {}", toString(album.ArtistId->Name.Value().value().c_str()));

    {
        // get an artist with the name "Sir Georg Solti, Sumi Jo & Wiener Philharmoniker"
        auto artist = dm.QuerySingle<Artist>().Where(FieldNameOf<&Artist::Name>, "=", "Red Hot Chili Peppers").Get().value();
        std::println("ArtistId: {}, Name: {}", artist.ArtistId.Value(), toString(artist.Name.Value().value().c_str()));

        // get albums of the artist
        auto albums = dm.Query<Album>().Where(FieldNameOf<&Album::ArtistId>, "=", artist.ArtistId.Value()).All();
        std::println("got {} albums", albums.size());

        auto albumIds = albums | std::views::transform([](auto const& album) { return album.AlbumId.Value(); });
        // get all tracks from all albums
        auto tracks = dm.Query<Track>().WhereIn(FieldNameOf<&Track::AlbumId>, albumIds).All();
        std::println("got {} tracks", tracks.size());

        // iterate over all tracks and print song names
        for (const auto& track: tracks)
        {
            std::println("TrackId: {}, Name: {}, Bytes: {} , UnitPrice: {}",
                         track.TrackId.Value(),
                         toString(track.Name.Value().c_str()),
                         track.Bytes.Value().value_or(0),
                         track.UnitPrice.Value().ToString());
        }
    }

    {
        // get pair of customer and employee
        auto records = dm.Query<Customer, Employee>().InnerJoin<&Employee::EmployeeId, &Customer::SupportRepId>().All();

        for (auto const& [customer, employee]: records)
        {
            std::println("CustomerId: {}, FirstName: {}, LastName: {}",
                         customer.CustomerId.Value(),
                         toString(customer.FirstName.Value().c_str()),
                         toString(customer.LastName.Value().c_str()));
            std::println("EmployeeId: {}, FirstName: {}, LastName: {}",
                         employee.EmployeeId.Value(),
                         toString(employee.FirstName.Value().c_str()),
                         toString(employee.LastName.Value().c_str()));
        }
    }

    {
        // get one employee
        auto employee = dm.QuerySingle<Employee>(1).value();
        std::println(" {} ", employee.HireDate.Value().value());
        // update hiring date to current date
        employee.HireDate = SqlDateTime::Now();
        dm.Update(employee);
    }
}
