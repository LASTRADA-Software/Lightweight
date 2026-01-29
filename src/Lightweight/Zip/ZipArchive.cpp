// SPDX-License-Identifier: Apache-2.0

#include "ZipArchive.hpp"

#include <cstring>
#include <utility>

namespace Lightweight::Zip
{

ZipArchive::ZipArchive(zip_t* zip) noexcept:
    m_zip { zip }
{
}

ZipArchive::ZipArchive(ZipArchive&& other) noexcept:
    m_zip { std::exchange(other.m_zip, nullptr) }
{
}

ZipArchive& ZipArchive::operator=(ZipArchive&& other) noexcept
{
    if (this != &other)
    {
        Discard();
        m_zip = std::exchange(other.m_zip, nullptr);
    }
    return *this;
}

ZipArchive::~ZipArchive() noexcept
{
    Discard();
}

// =============================================================================
// Factory methods
// =============================================================================

std::expected<ZipArchive, ZipError> ZipArchive::Open(std::filesystem::path const& path)
{
    int errorCode = 0;
    zip_t* zip = zip_open(path.string().c_str(), ZIP_RDONLY, &errorCode);
    if (!zip)
        return std::unexpected(ZipError::FromOpenError(errorCode, ZipErrorCode::OpenFailed));

    return ZipArchive(zip);
}

std::expected<ZipArchive, ZipError> ZipArchive::Create(std::filesystem::path const& path)
{
    int errorCode = 0;
    zip_t* zip = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_EXCL, &errorCode);
    if (!zip)
        return std::unexpected(ZipError::FromOpenError(errorCode, ZipErrorCode::OpenFailed));

    return ZipArchive(zip);
}

std::expected<ZipArchive, ZipError> ZipArchive::CreateOrTruncate(std::filesystem::path const& path)
{
    int errorCode = 0;
    zip_t* zip = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
    if (!zip)
        return std::unexpected(ZipError::FromOpenError(errorCode, ZipErrorCode::OpenFailed));

    return ZipArchive(zip);
}

// =============================================================================
// Properties
// =============================================================================

bool ZipArchive::IsOpen() const noexcept
{
    return m_zip != nullptr;
}

zip_int64_t ZipArchive::EntryCount() const noexcept
{
    if (!m_zip)
        return -1;
    return zip_get_num_entries(m_zip, 0);
}

zip_t* ZipArchive::NativeHandle() const noexcept
{
    return m_zip;
}

// =============================================================================
// Reading
// =============================================================================

std::optional<zip_int64_t> ZipArchive::LocateEntry(std::string_view name) const
{
    if (!m_zip)
        return std::nullopt;

    // zip_name_locate requires a null-terminated string
    std::string nameStr(name);
    auto const index = zip_name_locate(m_zip, nameStr.c_str(), 0);
    if (index < 0)
        return std::nullopt;

    return index;
}

std::expected<EntryInfo, ZipError> ZipArchive::GetEntryInfo(zip_int64_t index) const
{
    if (!m_zip)
        return std::unexpected(ZipError::Custom(ZipErrorCode::ReadFailed, "Archive is not open"));

    zip_stat_t stat;
    zip_stat_init(&stat);

    if (zip_stat_index(m_zip, static_cast<zip_uint64_t>(index), 0, &stat) != 0)
        return std::unexpected(ZipError::FromArchive(m_zip, ZipErrorCode::EntryNotFound));

    return EntryInfo {
        .index = index,
        .name = stat.name ? stat.name : "",
        .size = stat.size,
        .compressedSize = stat.comp_size,
    };
}

std::expected<ZipEntry, ZipError> ZipArchive::OpenEntry(zip_int64_t index) const
{
    if (!m_zip)
        return std::unexpected(ZipError::Custom(ZipErrorCode::ReadFailed, "Archive is not open"));

    zip_file_t* file = zip_fopen_index(m_zip, static_cast<zip_uint64_t>(index), 0);
    if (!file)
        return std::unexpected(ZipError::FromArchive(m_zip, ZipErrorCode::OpenFailed));

    return ZipEntry(file);
}

std::expected<std::vector<uint8_t>, ZipError> ZipArchive::ReadEntry(zip_int64_t index) const
{
    auto infoResult = GetEntryInfo(index);
    if (!infoResult)
        return std::unexpected(infoResult.error());

    auto entryResult = OpenEntry(index);
    if (!entryResult)
        return std::unexpected(entryResult.error());

    return entryResult->ReadAll(infoResult->size);
}

std::expected<std::string, ZipError> ZipArchive::ReadEntryAsString(zip_int64_t index) const
{
    auto dataResult = ReadEntry(index);
    if (!dataResult)
        return std::unexpected(dataResult.error());

    auto const& data = *dataResult;
    return std::string(reinterpret_cast<char const*>(data.data()), data.size());
}

// =============================================================================
// Writing
// =============================================================================

// NOLINTBEGIN(clang-analyzer-unix.Malloc)
std::expected<zip_int64_t, ZipError> ZipArchive::AddBuffer(std::string_view name,
                                                           std::span<uint8_t const> data,
                                                           CompressionMethod method,
                                                           uint32_t level)
{
    if (!m_zip)
        return std::unexpected(ZipError::Custom(ZipErrorCode::WriteFailed, "Archive is not open"));

    // Create a copy of the data that libzip will own and free via freep parameter
    // NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    void* dataCopy = nullptr;
    if (!data.empty())
    {
        dataCopy = malloc(data.size());
        if (!dataCopy)
            return std::unexpected(ZipError::Custom(ZipErrorCode::SourceCreationFailed, "Failed to allocate memory"));
        std::memcpy(dataCopy, data.data(), data.size());
    }

    // Create source from buffer (libzip takes ownership of dataCopy via freep=1)
    zip_source_t* source = zip_source_buffer(m_zip, dataCopy, data.size(), 1);
    if (!source)
    {
        free(dataCopy);
        return std::unexpected(ZipError::FromArchive(m_zip, ZipErrorCode::SourceCreationFailed));
    }
    // NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)

    // At this point libzip owns dataCopy and will free it

    // Add to archive
    std::string nameStr(name);
    auto const entryIndex = zip_file_add(m_zip, nameStr.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (entryIndex < 0)
    {
        zip_source_free(source);
        return std::unexpected(ZipError::FromArchive(m_zip, ZipErrorCode::WriteFailed));
    }

    // Set compression method
    if (zip_set_file_compression(m_zip, static_cast<zip_uint64_t>(entryIndex), static_cast<zip_int32_t>(method), level) != 0)
        return std::unexpected(ZipError::FromArchive(m_zip, ZipErrorCode::WriteFailed));

    return entryIndex;
}
// NOLINTEND(clang-analyzer-unix.Malloc)

std::expected<zip_int64_t, ZipError> ZipArchive::AddString(std::string_view name,
                                                           std::string_view content,
                                                           CompressionMethod method,
                                                           uint32_t level)
{
    return AddBuffer(
        name, std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(content.data()), content.size()), method, level);
}

// =============================================================================
// Iteration
// =============================================================================

void ZipArchive::ForEachEntry(std::function<bool(zip_int64_t, std::string_view, zip_uint64_t)> const& callback) const
{
    if (!m_zip)
        return;

    auto const count = zip_get_num_entries(m_zip, 0);
    for (zip_int64_t i = 0; i < count; ++i)
    {
        zip_stat_t stat;
        zip_stat_init(&stat);

        if (zip_stat_index(m_zip, static_cast<zip_uint64_t>(i), 0, &stat) == 0)
        {
            if (!callback(i, stat.name ? stat.name : "", stat.size))
                break;
        }
    }
}

std::vector<EntryInfo> ZipArchive::GetAllEntries() const
{
    std::vector<EntryInfo> entries;

    if (!m_zip)
        return entries;

    auto const count = zip_get_num_entries(m_zip, 0);
    entries.reserve(static_cast<size_t>(count));

    for (zip_int64_t i = 0; i < count; ++i)
    {
        zip_stat_t stat;
        zip_stat_init(&stat);

        if (zip_stat_index(m_zip, static_cast<zip_uint64_t>(i), 0, &stat) == 0)
        {
            entries.push_back(EntryInfo {
                .index = i,
                .name = stat.name ? stat.name : "",
                .size = stat.size,
                .compressedSize = stat.comp_size,
            });
        }
    }

    return entries;
}

// =============================================================================
// Lifecycle
// =============================================================================

std::expected<void, ZipError> ZipArchive::Close()
{
    if (!m_zip)
        return {};

    if (zip_close(m_zip) != 0)
    {
        auto error = ZipError::FromArchive(m_zip, ZipErrorCode::CloseFailed);
        zip_discard(m_zip);
        m_zip = nullptr;
        return std::unexpected(error);
    }

    m_zip = nullptr;
    return {};
}

void ZipArchive::Discard() noexcept
{
    if (m_zip)
    {
        zip_discard(m_zip);
        m_zip = nullptr;
    }
}

// =============================================================================
// Free functions
// =============================================================================

bool IsCompressionSupported(CompressionMethod method) noexcept
{
    return zip_compression_method_supported(static_cast<zip_int32_t>(method), 1) != 0;
}

} // namespace Lightweight::Zip
