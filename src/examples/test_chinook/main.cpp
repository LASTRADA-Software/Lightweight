#include "entities/Album.hpp"
#include "entities/Employee.hpp"

#include <Lightweight/Lightweight.hpp>

#include <print>

int main()
{

    SqlConnection::SetDefaultConnectionString(SqlConnectionString {
        "DRIVER={ODBC Driver 18 for SQL "
        "Server};SERVER=localhost;UID=SA;PWD=QWERT1.qwerty;TrustServerCertificate=yes;DATABASE=LightweightExample" });
    auto dm = DataMapper();

    // helper function to create std::string from string_view<char16_t>
    auto const toString = [](std::basic_string_view<char16_t> str) {
        std::string result;
        result.reserve(str.size());
        std::ranges::copy(str, std::back_inserter(result));
        return result;
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
    for (auto const& album: SqlRowIterator<Album>(dm.Connection()))
    {
        std::println("AlbumId: {}, Title: {}", album.AlbumId.Value(), toString(album.Title.Value().c_str()));
    }

    // select album with the title "Mozart Gala: Famous Arias"
    auto album = dm.QuerySingle<Album>().Where(FieldNameOf<&Album::Title>, "=", "Mozart Gala: Famous Arias").Get().value();

    std::println("AlbumId: {}, Title: {}", album.AlbumId.Value(), album.ArtistId.Value());

    // we can use BelongsTo<&Artist::ArtistId, SqlRealName{"ArtistId"}> c_ArtistId member
    // to get access to the artist entry in the database, using dereference operator
    // after configuring the relations
    dm.ConfigureRelationAutoLoading(album);
    std::println("Artist name: {}", toString(album.c_ArtistId->Name.Value().value().c_str()));
}
