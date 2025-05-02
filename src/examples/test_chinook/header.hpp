#include <Lightweight/DataMapper/DataMapper.hpp>
struct Employee final
{
    static constexpr std::string_view TableName = "Employee";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"EmployeeId"}> EmployeeId;
    Field<SqlUtf16String<20>, SqlRealName{"LastName"}> LastName;
    Field<SqlUtf16String<20>, SqlRealName{"FirstName"}> FirstName;
    Field<std::optional<SqlUtf16String<30>>, SqlRealName{"Title"}> Title;
    Field<std::optional<int32_t>, SqlRealName{"ReportsTo"}> ReportsTo;
    Field<std::optional<SqlDateTime>, SqlRealName{"BirthDate"}> BirthDate;
    Field<std::optional<SqlDateTime>, SqlRealName{"HireDate"}> HireDate;
    Field<std::optional<SqlUtf16String<70>>, SqlRealName{"Address"}> Address;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"City"}> City;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"State"}> State;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"Country"}> Country;
    Field<std::optional<SqlUtf16String<10>>, SqlRealName{"PostalCode"}> PostalCode;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Phone"}> Phone;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Fax"}> Fax;
    Field<std::optional<SqlUtf16String<60>>, SqlRealName{"Email"}> Email;
    BelongsTo<&Employee::EmployeeId, SqlRealName{"ReportsTo"}> ReportsTo;
};
struct Invoiceline final
{
    static constexpr std::string_view TableName = "InvoiceLine";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"InvoiceLineId"}> InvoiceLineId;
    Field<int32_t, SqlRealName{"InvoiceId"}> InvoiceId;
    Field<int32_t, SqlRealName{"TrackId"}> TrackId;
    Field<SqlNumeric<2, 10>, SqlRealName{"UnitPrice"}> UnitPrice;
    Field<int32_t, SqlRealName{"Quantity"}> Quantity;
    BelongsTo<&Invoice::InvoiceId, SqlRealName{"InvoiceId"}> InvoiceId;
    BelongsTo<&Track::TrackId, SqlRealName{"TrackId"}> TrackId;
};
struct Invoice final
{
    static constexpr std::string_view TableName = "Invoice";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"InvoiceId"}> InvoiceId;
    Field<int32_t, SqlRealName{"CustomerId"}> CustomerId;
    Field<SqlDateTime, SqlRealName{"InvoiceDate"}> InvoiceDate;
    Field<std::optional<SqlUtf16String<70>>, SqlRealName{"BillingAddress"}> BillingAddress;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingCity"}> BillingCity;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingState"}> BillingState;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingCountry"}> BillingCountry;
    Field<std::optional<SqlUtf16String<10>>, SqlRealName{"BillingPostalCode"}> BillingPostalCode;
    Field<SqlNumeric<2, 10>, SqlRealName{"Total"}> Total;
    BelongsTo<&Customer::CustomerId, SqlRealName{"CustomerId"}> CustomerId;
};
struct Customer final
{
    static constexpr std::string_view TableName = "Customer";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"CustomerId"}> CustomerId;
    Field<SqlUtf16String<40>, SqlRealName{"FirstName"}> FirstName;
    Field<SqlUtf16String<20>, SqlRealName{"LastName"}> LastName;
    Field<std::optional<SqlUtf16String<80>>, SqlRealName{"Company"}> Company;
    Field<std::optional<SqlUtf16String<70>>, SqlRealName{"Address"}> Address;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"City"}> City;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"State"}> State;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"Country"}> Country;
    Field<std::optional<SqlUtf16String<10>>, SqlRealName{"PostalCode"}> PostalCode;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Phone"}> Phone;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Fax"}> Fax;
    Field<SqlUtf16String<60>, SqlRealName{"Email"}> Email;
    Field<std::optional<int32_t>, SqlRealName{"SupportRepId"}> SupportRepId;
    BelongsTo<&Employee::EmployeeId, SqlRealName{"SupportRepId"}> SupportRepId;
};
struct Playlisttrack final
{
    static constexpr std::string_view TableName = "PlaylistTrack";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PlaylistId"}> PlaylistId;
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"TrackId"}> TrackId;
    BelongsTo<&Playlist::PlaylistId, SqlRealName{"PlaylistId"}> PlaylistId;
    BelongsTo<&Track::TrackId, SqlRealName{"TrackId"}> TrackId;
};
struct Album final
{
    static constexpr std::string_view TableName = "Album";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"AlbumId"}> AlbumId;
    Field<SqlUtf16String<160>, SqlRealName{"Title"}> Title;
    Field<int32_t, SqlRealName{"ArtistId"}> ArtistId;
    BelongsTo<&Artist::ArtistId, SqlRealName{"ArtistId"}> ArtistId;
};
struct Track final
{
    static constexpr std::string_view TableName = "Track";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"TrackId"}> TrackId;
    Field<SqlUtf16String<200>, SqlRealName{"Name"}> Name;
    Field<std::optional<int32_t>, SqlRealName{"AlbumId"}> AlbumId;
    Field<int32_t, SqlRealName{"MediaTypeId"}> MediaTypeId;
    Field<std::optional<int32_t>, SqlRealName{"GenreId"}> GenreId;
    Field<std::optional<SqlUtf16String<220>>, SqlRealName{"Composer"}> Composer;
    Field<int32_t, SqlRealName{"Milliseconds"}> Milliseconds;
    Field<std::optional<int32_t>, SqlRealName{"Bytes"}> Bytes;
    Field<SqlNumeric<2, 10>, SqlRealName{"UnitPrice"}> UnitPrice;
    BelongsTo<&Album::AlbumId, SqlRealName{"AlbumId"}> AlbumId;
    BelongsTo<&Genre::GenreId, SqlRealName{"GenreId"}> GenreId;
    BelongsTo<&Mediatype::MediaTypeId, SqlRealName{"MediaTypeId"}> MediaTypeId;
};
struct Playlist final
{
    static constexpr std::string_view TableName = "Playlist";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PlaylistId"}> PlaylistId;
    Field<std::optional<SqlUtf16String<120>>, SqlRealName{"Name"}> Name;
};
struct Mediatype final
{
    static constexpr std::string_view TableName = "MediaType";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"MediaTypeId"}> MediaTypeId;
    Field<std::optional<SqlUtf16String<120>>, SqlRealName{"Name"}> Name;
};
struct Genre final
{
    static constexpr std::string_view TableName = "Genre";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"GenreId"}> GenreId;
    Field<std::optional<SqlUtf16String<120>>, SqlRealName{"Name"}> Name;
};
struct Artist final
{
    static constexpr std::string_view TableName = "Artist";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"ArtistId"}> ArtistId;
    Field<std::optional<SqlUtf16String<120>>, SqlRealName{"Name"}> Name;
};
