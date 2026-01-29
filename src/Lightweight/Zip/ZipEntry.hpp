// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "ZipError.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
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

class ZipArchive;

/// RAII wrapper for a ZIP entry file handle (zip_file_t*).
///
/// This class provides safe, scoped access to a single entry within a ZIP archive.
/// The entry is automatically closed when the ZipEntry object is destroyed.
///
/// ZipEntry objects are non-copyable but movable.
class LIGHTWEIGHT_API ZipEntry final
{
  public:
    ZipEntry(ZipEntry const&) = delete;
    ZipEntry& operator=(ZipEntry const&) = delete;

    /// Move constructor. Transfers ownership of the file handle.
    ZipEntry(ZipEntry&& other) noexcept;

    /// Move assignment operator. Transfers ownership of the file handle.
    ZipEntry& operator=(ZipEntry&& other) noexcept;

    /// Destructor. Closes the entry if open.
    ~ZipEntry() noexcept;

    /// Checks if the entry is currently open.
    ///
    /// @return true if the entry is open, false otherwise.
    [[nodiscard]] bool IsOpen() const noexcept;

    /// Reads data from the entry into a buffer.
    ///
    /// @param buffer The buffer to read into.
    /// @return The number of bytes read, or a ZipError on failure.
    [[nodiscard]] std::expected<size_t, ZipError> Read(std::span<uint8_t> buffer);

    /// Reads all data from the entry.
    ///
    /// @param expectedSize The expected size of the entry in bytes.
    /// @return A vector containing all entry data, or a ZipError on failure.
    [[nodiscard]] std::expected<std::vector<uint8_t>, ZipError> ReadAll(size_t expectedSize);

    /// Closes the entry.
    void Close() noexcept;

  private:
    friend class ZipArchive;

    /// Private constructor. Only ZipArchive can create ZipEntry objects.
    ///
    /// @param file The libzip file handle.
    explicit ZipEntry(zip_file_t* file) noexcept;

    zip_file_t* m_file {};
};

} // namespace Lightweight::Zip
