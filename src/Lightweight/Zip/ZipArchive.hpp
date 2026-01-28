// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "ZipEntry.hpp"
#include "ZipError.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace Lightweight::Zip
{

/// Mode for opening a ZIP archive.
enum class OpenMode : uint8_t
{
    ReadOnly,        ///< Open existing archive for reading only
    Create,          ///< Create new archive (fails if exists)
    CreateOrTruncate ///< Create new archive or truncate existing
};

/// Compression methods supported by ZIP archives.
/// Uses int32_t base to match libzip ZIP_CM_* constants.
// NOLINTNEXTLINE(performance-enum-size)
enum class CompressionMethod : int32_t
{
    Store = ZIP_CM_STORE,     ///< No compression (store only)
    Deflate = ZIP_CM_DEFLATE, ///< Standard deflate compression
#if defined(ZIP_CM_ZSTD)
    Zstd = ZIP_CM_ZSTD, ///< Zstandard compression (if available)
#endif
};

/// Information about a ZIP archive entry.
struct EntryInfo
{
    zip_int64_t index {};           ///< Index of the entry in the archive
    std::string name;               ///< Name of the entry
    zip_uint64_t size {};           ///< Uncompressed size in bytes
    zip_uint64_t compressedSize {}; ///< Compressed size in bytes
};

/// RAII wrapper for a ZIP archive (zip_t*).
///
/// This class provides safe, scoped access to a ZIP archive file. The archive
/// is automatically closed when the ZipArchive object is destroyed.
///
/// ZipArchive objects are non-copyable but movable.
///
/// @code
/// // Writing
/// auto archiveResult = ZipArchive::CreateOrTruncate("backup.zip");
/// if (!archiveResult) {
///     std::cerr << "Failed: " << archiveResult.error().message << "\n";
///     return;
/// }
/// auto& archive = *archiveResult;
/// archive.AddString("metadata.json", jsonStr);
/// archive.AddBuffer("data.bin", binaryData);
/// archive.Close();
///
/// // Reading with functional chaining
/// ZipArchive::Open("backup.zip")
///     .and_then([](ZipArchive& ar) { return ar.ReadEntryAsString(0); })
///     .transform([](std::string const& content) { /* process */ })
///     .or_else([](ZipError const& e) { std::cerr << e.message; });
/// @endcode
class LIGHTWEIGHT_API ZipArchive final
{
  public:
    ZipArchive(ZipArchive const&) = delete;
    ZipArchive& operator=(ZipArchive const&) = delete;

    /// Move constructor. Transfers ownership of the archive handle.
    ZipArchive(ZipArchive&& other) noexcept;

    /// Move assignment operator. Transfers ownership of the archive handle.
    ZipArchive& operator=(ZipArchive&& other) noexcept;

    /// Destructor. Discards the archive if not explicitly closed.
    ~ZipArchive() noexcept;

    // =========================================================================
    // Factory methods
    // =========================================================================

    /// Opens an existing ZIP archive for reading.
    ///
    /// @param path The path to the archive file.
    /// @return The opened archive, or a ZipError on failure.
    [[nodiscard]] static std::expected<ZipArchive, ZipError> Open(std::filesystem::path const& path);

    /// Creates a new ZIP archive.
    ///
    /// @param path The path for the new archive file.
    /// @return The created archive, or a ZipError if the file already exists or creation fails.
    [[nodiscard]] static std::expected<ZipArchive, ZipError> Create(std::filesystem::path const& path);

    /// Creates a new ZIP archive, truncating any existing file.
    ///
    /// @param path The path for the archive file.
    /// @return The created archive, or a ZipError on failure.
    [[nodiscard]] static std::expected<ZipArchive, ZipError> CreateOrTruncate(std::filesystem::path const& path);

    // =========================================================================
    // Properties
    // =========================================================================

    /// Checks if the archive is currently open.
    ///
    /// @return true if the archive is open, false otherwise.
    [[nodiscard]] bool IsOpen() const noexcept;

    /// Returns the number of entries in the archive.
    ///
    /// @return The entry count, or -1 if the archive is not open.
    [[nodiscard]] zip_int64_t EntryCount() const noexcept;

    /// Returns the native libzip handle.
    ///
    /// @return The zip_t* handle, or nullptr if not open.
    [[nodiscard]] zip_t* NativeHandle() const noexcept;

    // =========================================================================
    // Reading
    // =========================================================================

    /// Locates an entry by name.
    ///
    /// @param name The name of the entry to find.
    /// @return The entry index, or std::nullopt if not found.
    [[nodiscard]] std::optional<zip_int64_t> LocateEntry(std::string_view name) const;

    /// Gets information about an entry by index.
    ///
    /// @param index The entry index.
    /// @return Entry information, or a ZipError if the index is invalid.
    [[nodiscard]] std::expected<EntryInfo, ZipError> GetEntryInfo(zip_int64_t index) const;

    /// Opens an entry for streaming read access.
    ///
    /// @param index The entry index.
    /// @return A ZipEntry for reading, or a ZipError on failure.
    [[nodiscard]] std::expected<ZipEntry, ZipError> OpenEntry(zip_int64_t index) const;

    /// Reads an entire entry as binary data.
    ///
    /// @param index The entry index.
    /// @return The entry contents as a byte vector, or a ZipError on failure.
    [[nodiscard]] std::expected<std::vector<uint8_t>, ZipError> ReadEntry(zip_int64_t index) const;

    /// Reads an entire entry as a string.
    ///
    /// @param index The entry index.
    /// @return The entry contents as a string, or a ZipError on failure.
    [[nodiscard]] std::expected<std::string, ZipError> ReadEntryAsString(zip_int64_t index) const;

    // =========================================================================
    // Writing
    // =========================================================================

    /// Adds binary data as a new entry.
    ///
    /// @param name The name for the new entry.
    /// @param data The binary data to add.
    /// @param method The compression method to use.
    /// @param level The compression level (0-9, where 0 is no compression).
    /// @return The index of the new entry, or a ZipError on failure.
    [[nodiscard]] std::expected<zip_int64_t, ZipError> AddBuffer(std::string_view name,
                                                                 std::span<uint8_t const> data,
                                                                 CompressionMethod method = CompressionMethod::Deflate,
                                                                 uint32_t level = 6);

    /// Adds string content as a new entry.
    ///
    /// @param name The name for the new entry.
    /// @param content The string content to add.
    /// @param method The compression method to use.
    /// @param level The compression level (0-9, where 0 is no compression).
    /// @return The index of the new entry, or a ZipError on failure.
    [[nodiscard]] std::expected<zip_int64_t, ZipError> AddString(std::string_view name,
                                                                 std::string_view content,
                                                                 CompressionMethod method = CompressionMethod::Deflate,
                                                                 uint32_t level = 6);

    // =========================================================================
    // Iteration
    // =========================================================================

    /// Iterates over all entries in the archive.
    ///
    /// @param callback Function called for each entry. Return false to stop iteration.
    ///                 Parameters: (index, name, uncompressed_size)
    void ForEachEntry(std::function<bool(zip_int64_t, std::string_view, zip_uint64_t)> const& callback) const;

    /// Gets information about all entries in the archive.
    ///
    /// @return A vector of EntryInfo for all entries.
    [[nodiscard]] std::vector<EntryInfo> GetAllEntries() const;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Closes the archive, writing all changes to disk.
    ///
    /// @return void on success, or a ZipError if closing fails.
    [[nodiscard]] std::expected<void, ZipError> Close();

    /// Discards the archive without writing changes.
    ///
    /// Use this to abandon modifications without saving them.
    void Discard() noexcept;

  private:
    /// Private constructor. Only factory methods can create ZipArchive objects.
    ///
    /// @param zip The libzip archive handle.
    explicit ZipArchive(zip_t* zip) noexcept;

    zip_t* m_zip {};
};

/// Checks if a compression method is supported by the current libzip installation.
///
/// @param method The compression method to check.
/// @return true if the method is supported, false otherwise.
[[nodiscard]] LIGHTWEIGHT_API bool IsCompressionSupported(CompressionMethod method) noexcept;

} // namespace Lightweight::Zip
