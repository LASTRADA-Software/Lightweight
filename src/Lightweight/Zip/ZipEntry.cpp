// SPDX-License-Identifier: Apache-2.0

#include "ZipEntry.hpp"

#include <utility>

namespace Lightweight::Zip
{

ZipEntry::ZipEntry(zip_file_t* file) noexcept:
    m_file { file }
{
}

ZipEntry::ZipEntry(ZipEntry&& other) noexcept:
    m_file { std::exchange(other.m_file, nullptr) }
{
}

ZipEntry& ZipEntry::operator=(ZipEntry&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_file = std::exchange(other.m_file, nullptr);
    }
    return *this;
}

ZipEntry::~ZipEntry() noexcept
{
    Close();
}

bool ZipEntry::IsOpen() const noexcept
{
    return m_file != nullptr;
}

std::expected<size_t, ZipError> ZipEntry::Read(std::span<uint8_t> buffer)
{
    if (!m_file)
        return std::unexpected(ZipError::Custom(ZipErrorCode::ReadFailed, "Entry is not open"));

    auto const bytesRead = zip_fread(m_file, buffer.data(), buffer.size());
    if (bytesRead < 0)
    {
        zip_error_t* error = zip_file_get_error(m_file);
        return std::unexpected(ZipError {
            .code = ZipErrorCode::ReadFailed,
            .libzipError = zip_error_code_zip(error),
            .message = zip_error_strerror(error),
        });
    }

    return static_cast<size_t>(bytesRead);
}

std::expected<std::vector<uint8_t>, ZipError> ZipEntry::ReadAll(size_t expectedSize)
{
    if (!m_file)
        return std::unexpected(ZipError::Custom(ZipErrorCode::ReadFailed, "Entry is not open"));

    std::vector<uint8_t> data;
    data.resize(expectedSize);

    auto const bytesRead = zip_fread(m_file, data.data(), expectedSize);
    if (bytesRead < 0)
    {
        zip_error_t* error = zip_file_get_error(m_file);
        return std::unexpected(ZipError {
            .code = ZipErrorCode::ReadFailed,
            .libzipError = zip_error_code_zip(error),
            .message = zip_error_strerror(error),
        });
    }

    if (std::cmp_not_equal(bytesRead, expectedSize))
        data.resize(static_cast<size_t>(bytesRead));

    return data;
}

void ZipEntry::Close() noexcept
{
    if (m_file)
    {
        zip_fclose(m_file);
        m_file = nullptr;
    }
}

} // namespace Lightweight::Zip
