// SPDX-License-Identifier: Apache-2.0

#include "Guarded.hpp"

#include <Lightweight/Profiles/ProfileConnection.hpp>

#include <expected>
#include <string>
#include <string_view>

#include <Secrets/ProfileCipher.hpp>

namespace Lightweight::Profiles
{

bool IsEncryptedPassword(std::string_view value) noexcept
{
    return Secrets::ProfileCipher::IsEncrypted(value);
}

std::expected<std::string, std::string> EncryptPassword(std::string_view plaintext)
{
    return detail::Guarded("cannot encrypt password", [&] { return Secrets::ProfileCipher::Builtin().Encrypt(plaintext); });
}

std::expected<std::string, std::string> DecryptPassword(std::string_view value)
{
    return detail::Guarded("cannot decrypt password", [&] { return Secrets::ProfileCipher::Builtin().Decrypt(value); });
}

} // namespace Lightweight::Profiles
