// SPDX-License-Identifier: Apache-2.0
#include "../TracyProfiler.hpp"
#include "../Zip/ZipError.hpp"
#include "WorkerChunkArchive.hpp"

#include <algorithm>
#include <format>
#include <new>
#include <stdexcept>
#include <utility>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace Lightweight::SqlBackup::detail
{

WorkerChunkArchive::WorkerChunkArchive(std::filesystem::path directory,
                                       unsigned workerId,
                                       std::size_t rotationBytes,
                                       CompressionMethod method,
                                       std::uint32_t level):
    m_directory { std::move(directory) },
    m_workerId { workerId },
    m_rotationBytes { std::max<std::size_t>(rotationBytes, 1) },
    m_method { method },
    m_level { level }
{
}

WorkerChunkArchive::~WorkerChunkArchive() noexcept
{
    if (m_current)
        zip_discard(m_current); // error path: drop the unsealed tail; sealed archives stay valid
}

void WorkerChunkArchive::OpenNextArchive()
{
    m_currentPath = m_directory / std::format("worker-{}-{}.zip", m_workerId, m_rotationIndex++);
    int err = 0;
    m_current = zip_open(m_currentPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!m_current)
        throw std::runtime_error { std::format("Failed to create worker chunk archive {}: {}",
                                               m_currentPath.string(),
                                               Zip::ZipError::FromOpenError(err, Zip::ZipErrorCode::OpenFailed).message) };
    m_currentInputBytes = 0;
    m_currentNames.clear();
}

void WorkerChunkArchive::Add(std::string const& entryName, std::string_view data)
{
    if (m_current && m_currentInputBytes >= m_rotationBytes)
        Seal(); // compression of the filled archive runs here, in the worker thread
    if (!m_current)
        OpenNextArchive();

    // libzip defers reading sources until this archive's zip_close (at most rotationBytes away),
    // so the chunk bytes must outlive every Add until Seal(). We own a copy in m_currentBuffers and
    // hand libzip a non-owning view (freep=0); Seal() releases the buffers once the archive is closed.
    std::string const& persistentData = m_currentBuffers.emplace_back(data);

    zip_source_t* source = zip_source_buffer(m_current, persistentData.data(), persistentData.size(), 0);
    if (!source)
        throw std::bad_alloc();

    zip_int64_t const index = zip_file_add(m_current, entryName.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (index < 0)
    {
        zip_source_free(source);
        throw std::runtime_error { std::format("Failed to add {} to worker chunk archive", entryName) };
    }
    zip_set_file_compression(m_current, static_cast<zip_uint64_t>(index), static_cast<zip_int32_t>(m_method), m_level);

    m_currentInputBytes += data.size();
    m_currentNames.insert(entryName);
    // A re-added name (window retry) supersedes any sealed copy: rotation order makes the merge's
    // last write win, so no tombstone is needed — and a previously recorded one must be lifted.
    m_tombstones.erase(entryName);
}

void WorkerChunkArchive::Remove(std::string const& entryName)
{
    if (m_current && m_currentNames.contains(entryName))
    {
        if (auto const index = zip_name_locate(m_current, entryName.c_str(), 0); index >= 0)
            zip_delete(m_current, static_cast<zip_uint64_t>(index));
        m_currentNames.erase(entryName);
        // Fall through: an earlier SEALED archive of this worker may also hold the name (a window
        // retried across a rotation boundary re-adds entries to the then-current archive), and the
        // merge must skip that stale copy too.
    }
    // Make the merge skip every sealed copy of this name (Add() lifts the tombstone on re-add).
    m_tombstones.insert(entryName);
}

void WorkerChunkArchive::Seal()
{
    if (!m_current)
        return;
    ZoneScopedN("Backup::CompressChunkArchive");
    auto* const archive = std::exchange(m_current, nullptr);
    int const closeResult = zip_close(archive); // reads & compresses the buffers we own
    // The chunk bytes were only needed until zip_close consumed them.
    m_currentBuffers.clear();
    if (closeResult < 0)
    {
        zip_error_t* zerr = zip_get_error(archive);
        std::string const message = zip_error_strerror(zerr);
        zip_discard(archive);
        throw std::runtime_error { std::format(
            "Failed to seal worker chunk archive {}: {}", m_currentPath.string(), message) };
    }
    // libzip does not materialize a file for an archive whose entries were all deleted again;
    // only record archives that actually exist on disk.
    if (std::filesystem::exists(m_currentPath))
        m_sealed.push_back(m_currentPath);
    m_currentNames.clear();
    m_currentInputBytes = 0;
}

} // namespace Lightweight::SqlBackup::detail
