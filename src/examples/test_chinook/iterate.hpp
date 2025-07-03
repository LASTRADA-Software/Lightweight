#pragma once
#include "entities/Employee.hpp"
#include "entities/Invoiceline.hpp"
#include "entities/Invoice.hpp"
#include "entities/Customer.hpp"
#include "entities/Playlisttrack.hpp"
#include "entities/Album.hpp"
#include "entities/Track.hpp"
#include "entities/Playlist.hpp"
#include "entities/Mediatype.hpp"
#include "entities/Genre.hpp"
#include "entities/Artist.hpp"

void iterate()
{

auto dm = DataMapper {};
auto entriesAlbum = dm.Query<Album>().First(10);
for (auto const& entry: entriesAlbum)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesArtist = dm.Query<Artist>().First(10);
for (auto const& entry: entriesArtist)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesCustomer = dm.Query<Customer>().First(10);
for (auto const& entry: entriesCustomer)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesEmployee = dm.Query<Employee>().First(10);
for (auto const& entry: entriesEmployee)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesGenre = dm.Query<Genre>().First(10);
for (auto const& entry: entriesGenre)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesInvoice = dm.Query<Invoice>().First(10);
for (auto const& entry: entriesInvoice)
{
    std::println("{}", DataMapper::Inspect(entry));
}

SqlLogger::SetLogger(SqlLogger::TraceLogger());
auto entriesInvoiceline = dm.Query<Invoiceline>().First(10);
for (auto const& entry: entriesInvoiceline)
{
    std::println("{}", DataMapper::Inspect(entry));
}

dm = DataMapper {}; // TODO : previous connection in the wrong state
auto entriesMediatype = dm.Query<Mediatype>().First(10);
for (auto const& entry: entriesMediatype)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesPlaylist = dm.Query<Playlist>().First(10);
for (auto const& entry: entriesPlaylist)
{
    std::println("{}", DataMapper::Inspect(entry));
}

auto entriesPlaylisttrack = dm.Query<Playlisttrack>().First(10);
for (auto const& entry: entriesPlaylisttrack)
{
    std::println("{}", DataMapper::Inspect(entry));
}

dm = DataMapper {}; // TODO : previous connection in the wrong state
auto entriesTrack = dm.Query<Track>().First(10);
for (auto const& entry: entriesTrack)
{
    std::println("{}", DataMapper::Inspect(entry));
}
}
