// SPDX-License-Identifier: Apache-2.0
#include "../../Lightweight/SqlBackup/WorkerChunkArchive.hpp"

#include <catch2/catch_test_macros.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>

using namespace Lightweight::SqlBackup;
using namespace Lightweight::SqlBackup::detail;

namespace
{

// RAII temp directory for one test.
struct TempDir
{
    std::filesystem::path path;

    TempDir():
        path { std::filesystem::temp_directory_path() / "worker_chunk_archive_test" }
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Reads every entry of a sealed archive: name -> (decompressed bytes, compression method).
std::map<std::string, std::pair<std::string, zip_int32_t>> ReadArchive(std::filesystem::path const& archive)
{
    std::map<std::string, std::pair<std::string, zip_int32_t>> entries;
    int err = 0;
    zip_t* zip = zip_open(archive.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(zip != nullptr);
    auto const count = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < count; ++i)
    {
        zip_stat_t stat;
        REQUIRE(zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &stat) == 0);
        std::string data(stat.size, '\0');
        zip_file_t* file = zip_fopen_index(zip, static_cast<zip_uint64_t>(i), 0);
        REQUIRE(file != nullptr);
        REQUIRE(zip_fread(file, data.data(), stat.size) == static_cast<zip_int64_t>(stat.size));
        zip_fclose(file);
        entries[stat.name] = { std::move(data), static_cast<zip_int32_t>(stat.comp_method) };
    }
    zip_close(zip);
    return entries;
}

} // namespace

TEST_CASE("WorkerChunkArchive round-trips many small entries without buffer-relocation corruption", "[workerarchive]")
{
    // Regression: Add() hands libzip a NON-owning pointer into the backing buffer and libzip only
    // reads it at Seal()/zip_close. The backing store must keep every element's address stable
    // across subsequent Adds. With a std::vector<std::string> backing store, growth reallocates and
    // moves the elements; for small-string-optimized entries that relocates the character data
    // itself, dangling libzip's pointer and silently corrupting the small chunks. This adds far more
    // small (SSO-sized), distinct entries than any initial capacity so the backing store must grow
    // many times — every entry must still round-trip byte-for-byte.
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 0, /*rotationBytes=*/100 * 1024 * 1024, CompressionMethod::Deflate, 1 };

    constexpr int Count = 512;
    std::map<std::string, std::string> expected;
    for (int i = 0; i < Count; ++i)
    {
        auto const name = std::format("data/t/{:04}_00.msgpack", i);
        auto const payload = std::format("v{}", i); // tiny, SSO-sized, and distinct per entry
        expected.emplace(name, payload);
        archive.Add(name, payload);
    }
    archive.Seal();

    REQUIRE(archive.SealedArchives().size() == 1);
    auto const entries = ReadArchive(archive.SealedArchives().front());
    REQUIRE(entries.size() == expected.size());
    for (auto const& [name, payload]: expected)
        CHECK(entries.at(name).first == payload);
}

TEST_CASE("WorkerChunkArchive seals compressed entries that round-trip", "[workerarchive]")
{
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 0, 1024 * 1024, CompressionMethod::Deflate, 6 };

    std::string const payload(50'000, 'x'); // highly compressible
    archive.Add("data/t/0001_00.msgpack", payload);
    archive.Add("data/t/0002_00.msgpack", "small");
    archive.Seal();

    REQUIRE(archive.SealedArchives().size() == 1);
    auto const entries = ReadArchive(archive.SealedArchives().front());
    REQUIRE(entries.size() == 2);
    CHECK(entries.at("data/t/0001_00.msgpack").first == payload);
    CHECK(entries.at("data/t/0001_00.msgpack").second == ZIP_CM_DEFLATE);
    CHECK(entries.at("data/t/0002_00.msgpack").first == "small");
    // The sealed file is actually compressed (50k of 'x' shrinks drastically).
    CHECK(std::filesystem::file_size(archive.SealedArchives().front()) < 5'000);
}

TEST_CASE("WorkerChunkArchive rotates after the input-byte threshold", "[workerarchive]")
{
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 3, /*rotationBytes=*/100, CompressionMethod::Deflate, 1 };

    archive.Add("a", std::string(80, 'a')); // archive 0: 80 bytes (< 100)
    archive.Add("b", std::string(80, 'b')); // still archive 0 (rotation happens on the NEXT Add)
    archive.Add("c", "ccc");                // 160 >= 100 -> seal archive 0, start archive 1
    archive.Seal();

    REQUIRE(archive.SealedArchives().size() == 2);
    auto const first = ReadArchive(archive.SealedArchives()[0]);
    auto const second = ReadArchive(archive.SealedArchives()[1]);
    CHECK(first.size() == 2);
    CHECK(first.contains("a"));
    CHECK(first.contains("b"));
    CHECK(second.size() == 1);
    CHECK(second.contains("c"));
}

TEST_CASE("WorkerChunkArchive overwrites a re-added name within the current archive", "[workerarchive]")
{
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 1, 1024 * 1024, CompressionMethod::Deflate, 1 };

    archive.Add("x", "first");
    archive.Add("x", "second");
    archive.Seal();

    auto const entries = ReadArchive(archive.SealedArchives().front());
    REQUIRE(entries.size() == 1);
    CHECK(entries.at("x").first == "second");
}

TEST_CASE("WorkerChunkArchive Remove deletes from current, tombstones sealed names", "[workerarchive]")
{
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 2, /*rotationBytes=*/10, CompressionMethod::Deflate, 1 };

    archive.Add("sealed-name", "0123456789AB"); // 12 bytes >= 10: next Add rotates
    archive.Add("current-name", "x");           // seals archive 0, opens archive 1
    archive.Remove("current-name");             // present in current -> hard delete AND tombstone
    archive.Remove("sealed-name");              // sealed -> tombstone
    archive.Seal();

    // Both names are tombstoned: a name deleted from the current archive may ALSO have a stale
    // copy in an earlier sealed archive (window retry across a rotation), which the merge must
    // skip; Add() lifts the tombstone when a name is legitimately re-added.
    CHECK(archive.Tombstones() == std::set<std::string> { "current-name", "sealed-name" });
    // Archive 1 lost its only entry again; libzip materializes no file for it, so only the
    // first (non-empty) archive is recorded.
    REQUIRE(archive.SealedArchives().size() == 1);
    CHECK(ReadArchive(archive.SealedArchives()[0]).contains("sealed-name"));

    // Re-adding a tombstoned name lifts the tombstone (latest attempt must survive the merge).
    auto archive2 = WorkerChunkArchive { dir.path / "", 7, 1024, CompressionMethod::Deflate, 1 };
    archive2.Add("y", "1");
    archive2.Seal();
    archive2.Remove("y");
    CHECK(archive2.Tombstones().contains("y"));
    archive2.Add("y", "2");
    CHECK_FALSE(archive2.Tombstones().contains("y"));
    archive2.Seal();
}

TEST_CASE("WorkerChunkArchive: Remove tombstones stale copies in earlier sealed archives", "[workerarchive]")
{
    TempDir const dir;
    // Retry-across-rotation scenario: attempt 1 writes sub-chunks 0 and 1, the archive rotates
    // (sealing both); attempt 2 re-writes 0 and 1 into the new current archive; attempt 3 keeps
    // only sub-chunk 0, so the stale "1" must vanish from BOTH the current archive (hard delete)
    // and the sealed one (tombstone) or the merge would resurrect attempt 1's rows.
    auto archive = WorkerChunkArchive { dir.path, 0, /*rotationBytes=*/8, CompressionMethod::Deflate, 1 };
    archive.Add("w/0", "attempt1-0");
    archive.Add("w/1", "attempt1-1"); // 10 bytes >= 8: next Add seals this archive
    archive.Add("w/0", "attempt2-0"); // rotation: archive 0 sealed (holds both), archive 1 current
    archive.Add("w/1", "attempt2-1");
    archive.Remove("w/1"); // attempt 3 produced fewer sub-chunks
    archive.Seal();

    // Rotation precedes each Add at this tiny threshold: sealed = [{w/0}, {w/1}, {w/0}]; the
    // final current archive lost its only entry again and produced no file.
    REQUIRE(archive.SealedArchives().size() == 3);
    CHECK(ReadArchive(archive.SealedArchives()[1]).contains("w/1")); // stale copy still on disk...
    CHECK(archive.Tombstones().contains("w/1"));                     // ...but the merge will skip it
    CHECK_FALSE(archive.Tombstones().contains("w/0"));
}

TEST_CASE("WorkerChunkArchive Seal is idempotent and lazy", "[workerarchive]")
{
    TempDir const dir;
    auto archive = WorkerChunkArchive { dir.path, 5, 1024, CompressionMethod::Deflate, 1 };

    archive.Seal(); // nothing added: no archive created
    CHECK(archive.SealedArchives().empty());

    archive.Add("a", "1");
    archive.Seal();
    archive.Seal(); // idempotent
    CHECK(archive.SealedArchives().size() == 1);
}
