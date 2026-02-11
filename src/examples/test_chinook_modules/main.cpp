// SPDX-License-Identifier: Apache-2.0

#include <format>
#include <optional>
#include <print>
#include <string>
#include <string_view>

import Lightweight;

using std::basic_string_view;
using std::format_string;
using std::optional;
using std::string;
using std::string_view;

using Lightweight::BelongsTo;
using Lightweight::DataMapper;
using Lightweight::Field;
using Lightweight::PrimaryKey;
using Lightweight::SqlConnection;
using Lightweight::SqlConnectionString;
using Lightweight::SqlDateTime;
using Lightweight::SqlDynamicUtf16String;
using Lightweight::SqlNullable;
using Lightweight::SqlNumeric;
using Lightweight::SqlRealName;

struct Artist final
{
    static constexpr string_view TableName = "Artist";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "ArtistId" }> ArtistId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};

struct Album final
{
    static constexpr string_view TableName = "Album";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "AlbumId" }> AlbumId;
    Field<SqlDynamicUtf16String<160>, SqlRealName { "Title" }> Title;
    BelongsTo<&Artist::ArtistId, SqlRealName { "ArtistId" }> ArtistId;
};

struct Employee final
{
    static constexpr string_view TableName = "Employee";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "EmployeeId" }> EmployeeId;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "LastName" }> LastName;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "FirstName" }> FirstName;
    Field<optional<SqlDynamicUtf16String<30>>, SqlRealName { "Title" }> Title;
    Field<optional<int32_t>, SqlRealName { "ReportsTo" }> ReportsTo;
    Field<optional<SqlDateTime>, SqlRealName { "BirthDate" }> BirthDate;
    Field<optional<SqlDateTime>, SqlRealName { "HireDate" }> HireDate;
    Field<optional<SqlDynamicUtf16String<70>>, SqlRealName { "Address" }> Address;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "City" }> City;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "State" }> State;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "Country" }> Country;
    Field<optional<SqlDynamicUtf16String<10>>, SqlRealName { "PostalCode" }> PostalCode;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Phone" }> Phone;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Fax" }> Fax;
    Field<optional<SqlDynamicUtf16String<60>>, SqlRealName { "Email" }> Email;
};

struct Customer final
{
    static constexpr string_view TableName = "Customer";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "CustomerId" }> CustomerId;
    Field<SqlDynamicUtf16String<40>, SqlRealName { "FirstName" }> FirstName;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "LastName" }> LastName;
    Field<optional<SqlDynamicUtf16String<80>>, SqlRealName { "Company" }> Company;
    Field<optional<SqlDynamicUtf16String<70>>, SqlRealName { "Address" }> Address;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "City" }> City;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "State" }> State;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "Country" }> Country;
    Field<optional<SqlDynamicUtf16String<10>>, SqlRealName { "PostalCode" }> PostalCode;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Phone" }> Phone;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Fax" }> Fax;
    Field<SqlDynamicUtf16String<60>, SqlRealName { "Email" }> Email;
    BelongsTo<&Employee::EmployeeId, SqlRealName { "SupportRepId" }> SupportRepId;
};

struct Genre final
{
    static constexpr string_view TableName = "Genre";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "GenreId" }> GenreId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};

struct Invoice final
{
    static constexpr string_view TableName = "Invoice";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "InvoiceId" }> InvoiceId;
    BelongsTo<&Customer::CustomerId, SqlRealName { "CustomerId" }> CustomerId;
    Field<SqlDateTime, SqlRealName { "InvoiceDate" }> InvoiceDate;
    Field<optional<SqlDynamicUtf16String<70>>, SqlRealName { "BillingAddress" }> BillingAddress;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "BillingCity" }> BillingCity;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "BillingState" }> BillingState;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "BillingCountry" }> BillingCountry;
    Field<optional<SqlDynamicUtf16String<10>>, SqlRealName { "BillingPostalCode" }>
        BillingPostalCode;
    Field<SqlNumeric<10, 2>, SqlRealName { "Total" }> Total;
};

struct Mediatype final
{
    static constexpr string_view TableName = "MediaType";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "MediaTypeId" }> MediaTypeId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};

struct Playlist final
{
    static constexpr string_view TableName = "Playlist";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PlaylistId" }> PlaylistId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};

struct Playlisttrack final
{
    static constexpr string_view TableName = "PlaylistTrack";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PlaylistId" }>
        PlaylistId; // NB: This is also a foreign key
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "TrackId" }>
        TrackId; // NB: This is also a foreign key
};

struct Track final
{
    static constexpr string_view TableName = "Track";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "TrackId" }> TrackId;
    Field<SqlDynamicUtf16String<200>, SqlRealName { "Name" }> Name;
    BelongsTo<&Album::AlbumId, SqlRealName { "AlbumId" }, SqlNullable::Null> AlbumId;
    BelongsTo<&Mediatype::MediaTypeId, SqlRealName { "MediaTypeId" }> MediaTypeId;
    BelongsTo<&Genre::GenreId, SqlRealName { "GenreId" }, SqlNullable::Null> GenreId;
    Field<optional<SqlDynamicUtf16String<220>>, SqlRealName { "Composer" }> Composer;
    Field<int32_t, SqlRealName { "Milliseconds" }> Milliseconds;
    Field<optional<int32_t>, SqlRealName { "Bytes" }> Bytes;
    Field<SqlNumeric<10, 2>, SqlRealName { "UnitPrice" }> UnitPrice;
};

struct Invoiceline final
{
    static constexpr string_view TableName = "InvoiceLine";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "InvoiceLineId" }> InvoiceLineId;
    BelongsTo<&Invoice::InvoiceId, SqlRealName { "InvoiceId" }> InvoiceId;
    BelongsTo<&Track::TrackId, SqlRealName { "TrackId" }> TrackId;
    Field<SqlNumeric<10, 2>, SqlRealName { "UnitPrice" }> UnitPrice;
    Field<int32_t, SqlRealName { "Quantity" }> Quantity;
};

static string GetEnvironmentVariable(const string& name)
{
#if defined(_MSC_VER)
    char* envBuffer = nullptr;
    size_t envBufferLen = 0;
    _dupenv_s(&envBuffer, &envBufferLen, name.data());
    if (envBuffer && *envBuffer)
    {
        string result { envBuffer };
        free(envBuffer); // free the allocated memory
        return result;
    }
#else
    if (const auto* s = std::getenv(name.data()); s && *s)
        return string { s };
#endif
    return {};
}

template <typename... Args>
void Log(format_string<Args...> fmt, Args&&... args)
{
    std::println(fmt, std::forward<Args>(args)...);
}

template <typename Entity>
void DumpTable(DataMapper& dm, size_t limit = 1)
{
    const auto entries = dm.Query<Entity>().First(limit);
    for (const auto& entry: entries)
    {
        Log("{}", DataMapper::Inspect(entry));
    }
}

int main()
{
    if (const auto odbcConnectionString = GetEnvironmentVariable("ODBC_CONNECTION_STRING"); !odbcConnectionString.empty())
    {
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { odbcConnectionString });
    }
    else
    {
        SqlConnection::SetDefaultConnectionString(SqlConnectionString {
            "DRIVER={ODBC Driver 18 for SQL "
            "Server};SERVER=localhost;UID=SA;PWD=BlahThat.;TrustServerCertificate=yes;DATABASE=LightweightTest" });
    }

    DataMapper dm;

    // helper function to create std::string from string_view<char16_t>
    const auto toString = [](basic_string_view<char16_t> str) {
        const auto u8Str = Lightweight::ToUtf8(str);
        return string(reinterpret_cast<const char*>(u8Str.data()), u8Str.size());
    };

    // get all employees
    const auto employees = dm.Query<Employee>().All();
    for (const auto& employee: employees)
    {
        Log("EmployeeId: {}, FirstName: {}, LastName: {}",
            employee.EmployeeId.Value(),
            toString(employee.FirstName.Value().c_str()),
            toString(employee.LastName.Value().c_str()));
    }

    Log("Module test completed successfully! Found {} employees.", employees.size());
}
