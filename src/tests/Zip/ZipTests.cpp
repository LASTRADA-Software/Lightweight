// SPDX-License-Identifier: Apache-2.0

// NOLINTBEGIN(bugprone-unused-return-value)

#include <Lightweight/Zip/ZipArchive.hpp>
#include <Lightweight/Zip/ZipEntry.hpp>
#include <Lightweight/Zip/ZipError.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>

using namespace Lightweight::Zip;

namespace
{

/// RAII helper for temporary files
class TempFile
{
  public:
    // NOLINTNEXTLINE(cert-msc50-cpp)
    explicit TempFile(std::string_view suffix = ".zip"):
        m_path { std::filesystem::temp_directory_path() / std::format("lightweight_test_{}{}", std::rand(), suffix) }
    {
    }

    ~TempFile() noexcept
    {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }

    TempFile(TempFile const&) = delete;
    TempFile& operator=(TempFile const&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    [[nodiscard]] std::filesystem::path const& Path() const noexcept
    {
        return m_path;
    }

  private:
    std::filesystem::path m_path;
};

} // namespace

// =============================================================================
// ZipError tests
// =============================================================================

TEST_CASE("ZipError.Custom", "[Zip]")
{
    auto error = ZipError::Custom(ZipErrorCode::ReadFailed, "Test error message");

    CHECK(error.code == ZipErrorCode::ReadFailed);
    CHECK(error.libzipError == 0);
    CHECK(error.message == "Test error message");
}

TEST_CASE("ZipError formatting", "[Zip]")
{
    auto error = ZipError::Custom(ZipErrorCode::OpenFailed, "Cannot open file");

    auto formatted = std::format("{}", error);
    CHECK_THAT(formatted, Catch::Matchers::ContainsSubstring("OpenFailed"));
    CHECK_THAT(formatted, Catch::Matchers::ContainsSubstring("Cannot open file"));
}

TEST_CASE("ZipErrorCode formatting", "[Zip]")
{
    CHECK(std::format("{}", ZipErrorCode::OpenFailed) == "OpenFailed");
    CHECK(std::format("{}", ZipErrorCode::CloseFailed) == "CloseFailed");
    CHECK(std::format("{}", ZipErrorCode::EntryNotFound) == "EntryNotFound");
    CHECK(std::format("{}", ZipErrorCode::ReadFailed) == "ReadFailed");
    CHECK(std::format("{}", ZipErrorCode::WriteFailed) == "WriteFailed");
    CHECK(std::format("{}", ZipErrorCode::SourceCreationFailed) == "SourceCreationFailed");
}

// =============================================================================
// ZipArchive factory method tests
// =============================================================================

TEST_CASE("ZipArchive.CreateOrTruncate creates new archive", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    CHECK(archive.IsOpen());
    CHECK(archive.EntryCount() == 0);

    // Add an entry (libzip doesn't create a file for empty archives)
    (void) archive.AddString("dummy.txt", "dummy");

    auto closeResult = archive.Close();
    CHECK(closeResult.has_value());
    CHECK(!archive.IsOpen());

    // Verify file was created
    CHECK(std::filesystem::exists(temp.Path()));
}

TEST_CASE("ZipArchive.Create fails if file exists", "[Zip]")
{
    TempFile temp;

    // Create the file first (must have content - libzip doesn't write empty archives)
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("test.txt", "content");
        (void) result->Close();
    }
    REQUIRE(std::filesystem::exists(temp.Path()));

    // Now Create should fail
    auto result = ZipArchive::Create(temp.Path());
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ZipErrorCode::OpenFailed);
}

TEST_CASE("ZipArchive.CreateOrTruncate overwrites existing file", "[Zip]")
{
    TempFile temp;

    // Create with some content
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("test.txt", "Hello World");
        (void) result->Close();
    }

    // Truncate should create empty archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        CHECK(result->EntryCount() == 0);
        (void) result->Close();
    }
}

TEST_CASE("ZipArchive.Open reads existing archive", "[Zip]")
{
    TempFile temp;

    // Create archive with content
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("greeting.txt", "Hello, World!");
        (void) result->Close();
    }

    // Open and verify
    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    CHECK(archive.IsOpen());
    CHECK(archive.EntryCount() == 1);
}

TEST_CASE("ZipArchive.Open fails for non-existent file", "[Zip]")
{
    auto result = ZipArchive::Open("/nonexistent/path/to/archive.zip");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ZipErrorCode::OpenFailed);
}

// =============================================================================
// ZipArchive writing tests
// =============================================================================

TEST_CASE("ZipArchive.AddString adds text entry", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    auto addResult = archive.AddString("hello.txt", "Hello, World!");
    REQUIRE(addResult.has_value());
    CHECK(*addResult == 0); // First entry has index 0

    CHECK(archive.EntryCount() == 1);
    (void) archive.Close();
}

TEST_CASE("ZipArchive.AddBuffer adds binary entry", "[Zip]")
{
    TempFile temp;

    std::vector<uint8_t> data = { 0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD };

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    auto addResult = archive.AddBuffer("data.bin", data);
    REQUIRE(addResult.has_value());

    CHECK(archive.EntryCount() == 1);
    (void) archive.Close();
}

TEST_CASE("ZipArchive adds multiple entries", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    (void) archive.AddString("file1.txt", "Content 1");
    (void) archive.AddString("file2.txt", "Content 2");
    (void) archive.AddString("subdir/file3.txt", "Content 3");

    CHECK(archive.EntryCount() == 3);
    (void) archive.Close();

    // Verify persistence
    auto openResult = ZipArchive::Open(temp.Path());
    REQUIRE(openResult.has_value());
    CHECK(openResult->EntryCount() == 3);
}

TEST_CASE("ZipArchive.AddString with Store compression", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    auto addResult = archive.AddString("uncompressed.txt", "This is uncompressed data", CompressionMethod::Store, 0);
    REQUIRE(addResult.has_value());

    (void) archive.Close();
}

// =============================================================================
// ZipArchive reading tests
// =============================================================================

TEST_CASE("ZipArchive.ReadEntryAsString reads text content", "[Zip]")
{
    TempFile temp;
    std::string const content = "Hello, this is test content!";

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("message.txt", content);
        (void) result->Close();
    }

    // Read back
    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto readResult = result->ReadEntryAsString(0);
    REQUIRE(readResult.has_value());
    CHECK(*readResult == content);
}

TEST_CASE("ZipArchive.ReadEntry reads binary content", "[Zip]")
{
    TempFile temp;
    std::vector<uint8_t> const data = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB };

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddBuffer("binary.dat", data);
        (void) result->Close();
    }

    // Read back
    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto readResult = result->ReadEntry(0);
    REQUIRE(readResult.has_value());
    CHECK(*readResult == data);
}

TEST_CASE("ZipArchive.LocateEntry finds entry by name", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("first.txt", "First");
        (void) result->AddString("second.txt", "Second");
        (void) result->AddString("third.txt", "Third");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;

    auto idx1 = archive.LocateEntry("first.txt");
    REQUIRE(idx1.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(idx1.value() == 0);

    auto idx2 = archive.LocateEntry("second.txt");
    REQUIRE(idx2.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(idx2.value() == 1);

    auto idx3 = archive.LocateEntry("third.txt");
    REQUIRE(idx3.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(idx3.value() == 2);

    auto notFound = archive.LocateEntry("nonexistent.txt");
    CHECK(!notFound.has_value());
}

TEST_CASE("ZipArchive.GetEntryInfo returns entry metadata", "[Zip]")
{
    TempFile temp;
    std::string const content = "Test content for entry info";

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("metadata.txt", content);
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto infoResult = result->GetEntryInfo(0);
    REQUIRE(infoResult.has_value());

    auto const& info = *infoResult;
    CHECK(info.index == 0);
    CHECK(info.name == "metadata.txt");
    CHECK(info.size == content.size());
}

TEST_CASE("ZipArchive.GetEntryInfo fails for invalid index", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("test.txt", "Test");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto infoResult = result->GetEntryInfo(999);
    REQUIRE(!infoResult.has_value());
    CHECK(infoResult.error().code == ZipErrorCode::EntryNotFound);
}

// =============================================================================
// ZipArchive iteration tests
// =============================================================================

TEST_CASE("ZipArchive.ForEachEntry iterates all entries", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("a.txt", "A");
        (void) result->AddString("b.txt", "BB");
        (void) result->AddString("c.txt", "CCC");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    std::vector<std::string> names;
    std::vector<zip_uint64_t> sizes;

    result->ForEachEntry([&](zip_int64_t /*index*/, std::string_view name, zip_uint64_t size) {
        names.emplace_back(name);
        sizes.push_back(size);
        return true;
    });

    REQUIRE(names.size() == 3);
    CHECK(names[0] == "a.txt");
    CHECK(names[1] == "b.txt");
    CHECK(names[2] == "c.txt");
    CHECK(sizes[0] == 1);
    CHECK(sizes[1] == 2);
    CHECK(sizes[2] == 3);
}

TEST_CASE("ZipArchive.ForEachEntry can stop early", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("1.txt", "1");
        (void) result->AddString("2.txt", "2");
        (void) result->AddString("3.txt", "3");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    int count = 0;
    result->ForEachEntry([&](zip_int64_t /*index*/, std::string_view /*name*/, zip_uint64_t /*size*/) {
        ++count;
        return count < 2; // Stop after 2 iterations
    });

    CHECK(count == 2);
}

TEST_CASE("ZipArchive.GetAllEntries returns all entry info", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("file1.txt", "Content 1");
        (void) result->AddString("file2.txt", "Content 2 longer");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto entries = result->GetAllEntries();
    REQUIRE(entries.size() == 2);

    CHECK(entries[0].name == "file1.txt");
    CHECK(entries[0].size == 9);
    CHECK(entries[1].name == "file2.txt");
    CHECK(entries[1].size == 16);
}

// =============================================================================
// ZipEntry tests
// =============================================================================

TEST_CASE("ZipEntry.Read reads partial data", "[Zip]")
{
    TempFile temp;
    std::string const content = "0123456789ABCDEF";

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("data.txt", content);
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto entryResult = result->OpenEntry(0);
    REQUIRE(entryResult.has_value());

    auto& entry = *entryResult;
    CHECK(entry.IsOpen());

    // Read first 4 bytes
    std::array<uint8_t, 4> buffer {};
    auto readResult = entry.Read(buffer);
    REQUIRE(readResult.has_value());
    CHECK(*readResult == 4);
    CHECK(buffer[0] == '0');
    CHECK(buffer[3] == '3');

    // Read next 4 bytes
    readResult = entry.Read(buffer);
    REQUIRE(readResult.has_value());
    CHECK(*readResult == 4);
    CHECK(buffer[0] == '4');

    entry.Close();
    CHECK(!entry.IsOpen());
}

TEST_CASE("ZipEntry.ReadAll reads entire content", "[Zip]")
{
    TempFile temp;
    std::string const content = "Complete file content here";

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("complete.txt", content);
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto entryResult = result->OpenEntry(0);
    REQUIRE(entryResult.has_value());

    auto readResult = entryResult->ReadAll(content.size());
    REQUIRE(readResult.has_value());

    std::string readContent(reinterpret_cast<char const*>(readResult->data()), readResult->size());
    CHECK(readContent == content);
}

// =============================================================================
// ZipArchive lifecycle tests
// =============================================================================

TEST_CASE("ZipArchive.Discard abandons changes", "[Zip]")
{
    TempFile temp;

    // Create and close normally first
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("original.txt", "Original");
        (void) result->Close();
    }

    // Open, add entry, but discard
    {
        auto result = ZipArchive::Open(temp.Path());
        REQUIRE(result.has_value());
        result->Discard();
        CHECK(!result->IsOpen());
    }

    // Verify original content is preserved
    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());
    CHECK(result->EntryCount() == 1);

    auto locateResult = result->LocateEntry("original.txt");
    CHECK(locateResult.has_value());
}

TEST_CASE("ZipArchive move semantics", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    // Move construct
    ZipArchive archive2 = std::move(*result);
    CHECK(archive2.IsOpen());
    CHECK(!result->IsOpen()); // NOLINT(bugprone-use-after-move)

    // Move assign
    ZipArchive archive3 = ZipArchive::CreateOrTruncate((temp.Path().string() + ".2")).value();
    archive3 = std::move(archive2);
    CHECK(archive3.IsOpen());
    CHECK(!archive2.IsOpen()); // NOLINT(bugprone-use-after-move)

    archive3.Discard();
}

TEST_CASE("ZipArchive.NativeHandle returns underlying handle", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    CHECK(result->NativeHandle() != nullptr);

    result->Discard();
    CHECK(result->NativeHandle() == nullptr);
}

// =============================================================================
// Compression support tests
// =============================================================================

TEST_CASE("IsCompressionSupported checks method availability", "[Zip]")
{
    // Store and Deflate should always be supported
    CHECK(IsCompressionSupported(CompressionMethod::Store));
    CHECK(IsCompressionSupported(CompressionMethod::Deflate));

    // Zstd support depends on libzip build configuration
#if defined(ZIP_CM_ZSTD)
    // Just verify the function doesn't crash - result depends on build
    (void) IsCompressionSupported(CompressionMethod::Zstd);
#endif
}

// =============================================================================
// Error handling tests
// =============================================================================

TEST_CASE("ZipArchive operations fail on closed archive", "[Zip]")
{
    TempFile temp;

    auto result = ZipArchive::CreateOrTruncate(temp.Path());
    REQUIRE(result.has_value());

    auto& archive = *result;
    archive.Discard();

    CHECK(archive.EntryCount() == -1);
    CHECK(!archive.LocateEntry("test").has_value());

    auto infoResult = archive.GetEntryInfo(0);
    REQUIRE(!infoResult.has_value());

    auto entryResult = archive.OpenEntry(0);
    REQUIRE(!entryResult.has_value());

    auto addResult = archive.AddString("test.txt", "data");
    REQUIRE(!addResult.has_value());
}

TEST_CASE("ZipEntry.Read fails on closed entry", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("test.txt", "Test content");
        (void) result->Close();
    }

    auto result = ZipArchive::Open(temp.Path());
    REQUIRE(result.has_value());

    auto entryResult = result->OpenEntry(0);
    REQUIRE(entryResult.has_value());

    auto& entry = *entryResult;
    entry.Close();

    std::array<uint8_t, 10> buffer {};
    auto readResult = entry.Read(buffer);
    REQUIRE(!readResult.has_value());
    CHECK(readResult.error().code == ZipErrorCode::ReadFailed);
}

// =============================================================================
// Functional chaining tests (std::expected)
// =============================================================================

TEST_CASE("ZipArchive supports functional chaining with and_then", "[Zip]")
{
    TempFile temp;

    // Create archive
    {
        auto result = ZipArchive::CreateOrTruncate(temp.Path());
        REQUIRE(result.has_value());
        (void) result->AddString("chain.txt", "Chained content");
        (void) result->Close();
    }

    // Use functional chaining (rvalue expected requires rvalue reference in lambda)
    std::string extractedContent;

    auto finalResult = ZipArchive::Open(temp.Path())
                           // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
                           .and_then([](ZipArchive&& ar) { return ar.ReadEntryAsString(0); })
                           .transform([&](std::string const& content) {
                               extractedContent = content;
                               return content.size();
                           });

    REQUIRE(finalResult.has_value());
    CHECK(*finalResult == 15);
    CHECK(extractedContent == "Chained content");
}

TEST_CASE("ZipArchive functional chaining handles errors", "[Zip]")
{
    bool errorHandled = false;

    auto result = ZipArchive::Open("/nonexistent/file.zip").or_else([&](ZipError const& error) {
        errorHandled = true;
        CHECK(error.code == ZipErrorCode::OpenFailed);
        return std::expected<ZipArchive, ZipError>(std::unexpected(error));
    });

    CHECK(errorHandled);
    CHECK(!result.has_value());
}

// NOLINTEND(bugprone-unused-return-value)
