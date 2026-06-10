// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "SqlBackup.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

typedef struct zip zip_t; // NOLINT(modernize-use-using) - mirrors libzip's own C typedef

namespace Lightweight::SqlBackup::detail
{

/// One backup worker's rotating set of compressed temp archives.
///
/// libzip defers all compression work to zip_close, so the only way to compress chunks DURING
/// the (network-bound) export phase is to actually close archives as we go: chunks are added to a
/// per-worker temp zip, and once it has swallowed @c rotationBytes of uncompressed input the
/// archive is sealed (zip_close runs the compression in the worker thread) and a fresh one is
/// started. The finalize phase later raw-merges the sealed archives' entries into the final
/// backup archive without recompressing (see SqlBackup.cpp).
///
/// Single-threaded use: each worker owns exactly one instance; no internal locking.
class LIGHTWEIGHT_API WorkerChunkArchive
{
  public:
    /// Constructs the archive set (no file is created until the first Add).
    ///
    /// @param directory Existing temp directory the archives are created in.
    /// @param workerId Stable worker index, used in the archive file names.
    /// @param rotationBytes Uncompressed input bytes per archive before it is sealed (clamped to >= 1).
    /// @param method Compression method applied to every entry.
    /// @param level Compression level applied to every entry.
    WorkerChunkArchive(std::filesystem::path directory,
                       unsigned workerId,
                       std::size_t rotationBytes,
                       CompressionMethod method,
                       std::uint32_t level);

    /// Discards a still-open current archive (error path); sealed archives stay on disk.
    ~WorkerChunkArchive() noexcept;

    WorkerChunkArchive(WorkerChunkArchive const&) = delete;
    WorkerChunkArchive& operator=(WorkerChunkArchive const&) = delete;
    WorkerChunkArchive(WorkerChunkArchive&&) = delete;
    WorkerChunkArchive& operator=(WorkerChunkArchive&&) = delete;

    /// Adds @p data under @p entryName, overwriting a same-named entry in the current archive.
    /// Seals and rotates first if the current archive already holds >= rotationBytes of input.
    /// @param entryName The final backup archive entry name (kept verbatim through the merge).
    /// @param data The uncompressed chunk bytes.
    void Add(std::string const& entryName, std::string_view data);

    /// Deletes @p entryName from the current archive if present there; otherwise records a
    /// tombstone so the finalize merge skips the name from earlier sealed archives.
    /// @param entryName The entry name to remove.
    void Remove(std::string const& entryName);

    /// Seals the current archive (zip_close — the compression happens here, in the calling
    /// worker thread). No-op when no entries are pending. Idempotent.
    void Seal();

    /// The sealed archive paths in rotation order (complete after the final Seal()).
    [[nodiscard]] std::vector<std::filesystem::path> const& SealedArchives() const noexcept
    {
        return m_sealed;
    }

    /// Entry names removed after their archive was sealed; the merge must skip these.
    [[nodiscard]] std::set<std::string> const& Tombstones() const noexcept
    {
        return m_tombstones;
    }

  private:
    void OpenNextArchive();

    std::filesystem::path m_directory;
    unsigned m_workerId;
    std::size_t m_rotationBytes;
    CompressionMethod m_method;
    std::uint32_t m_level;

    zip_t* m_current = nullptr;
    std::filesystem::path m_currentPath;
    std::size_t m_currentInputBytes = 0;
    std::set<std::string> m_currentNames; // names present in the (open) current archive
    // Backing store for the current archive's entries: libzip defers reading sources until
    // zip_close, so the chunk bytes must outlive every Add until Seal(). Owned here and handed
    // to libzip as non-owning buffers (freep=0); released once Seal() has closed the archive.
    std::vector<std::string> m_currentBuffers;
    unsigned m_rotationIndex = 0;

    std::vector<std::filesystem::path> m_sealed;
    std::set<std::string> m_tombstones;
};

} // namespace Lightweight::SqlBackup::detail
