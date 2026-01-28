// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <format>
#include <string>

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

/// Error codes for ZIP archive operations.
enum class ZipErrorCode : uint8_t
{
    OpenFailed,           ///< Failed to open the archive
    CloseFailed,          ///< Failed to close the archive
    EntryNotFound,        ///< Requested entry was not found
    ReadFailed,           ///< Failed to read from archive or entry
    WriteFailed,          ///< Failed to write to archive
    SourceCreationFailed, ///< Failed to create a ZIP source for adding data
};

/// Represents an error that occurred during a ZIP operation.
struct ZipError
{
    ZipErrorCode code {}; ///< The error code
    int libzipError {};   ///< Original libzip error code (0 if not from libzip)
    std::string message;  ///< Human-readable error message

    /// Creates a ZipError from the current error state of a ZIP archive.
    ///
    /// @param zip The ZIP archive handle to extract the error from.
    /// @param code The error code to assign.
    /// @return A ZipError populated with the libzip error information.
    [[nodiscard]] static ZipError FromArchive(zip_t* zip, ZipErrorCode code)
    {
        zip_error_t* error = zip_get_error(zip);
        return ZipError {
            .code = code,
            .libzipError = zip_error_code_zip(error),
            .message = zip_error_strerror(error),
        };
    }

    /// Creates a ZipError from an open error code.
    ///
    /// @param errorCode The libzip error code from zip_open.
    /// @param code The error code to assign.
    /// @return A ZipError populated with the error information.
    [[nodiscard]] static ZipError FromOpenError(int errorCode, ZipErrorCode code)
    {
        zip_error_t error;
        zip_error_init_with_code(&error, errorCode);
        auto message = std::string(zip_error_strerror(&error));
        zip_error_fini(&error);
        return ZipError {
            .code = code,
            .libzipError = errorCode,
            .message = std::move(message),
        };
    }

    /// Creates a custom ZipError with a user-defined message.
    ///
    /// @param code The error code.
    /// @param msg The error message.
    /// @return A ZipError with the specified code and message.
    [[nodiscard]] static ZipError Custom(ZipErrorCode code, std::string msg)
    {
        return ZipError {
            .code = code,
            .libzipError = 0,
            .message = std::move(msg),
        };
    }
};

} // namespace Lightweight::Zip

template <>
struct std::formatter<Lightweight::Zip::ZipErrorCode>: std::formatter<std::string_view>
{
    auto format(Lightweight::Zip::ZipErrorCode code, format_context& ctx) const -> format_context::iterator
    {
        using Lightweight::Zip::ZipErrorCode;
        std::string_view name;
        switch (code)
        {
            case ZipErrorCode::OpenFailed:
                name = "OpenFailed";
                break;
            case ZipErrorCode::CloseFailed:
                name = "CloseFailed";
                break;
            case ZipErrorCode::EntryNotFound:
                name = "EntryNotFound";
                break;
            case ZipErrorCode::ReadFailed:
                name = "ReadFailed";
                break;
            case ZipErrorCode::WriteFailed:
                name = "WriteFailed";
                break;
            case ZipErrorCode::SourceCreationFailed:
                name = "SourceCreationFailed";
                break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

template <>
struct std::formatter<Lightweight::Zip::ZipError>: std::formatter<std::string>
{
    auto format(Lightweight::Zip::ZipError const& error, format_context& ctx) const -> format_context::iterator
    {
        return std::formatter<std::string>::format(
            std::format("{}: {} (libzip error: {})", error.code, error.message, error.libzipError), ctx);
    }
};
