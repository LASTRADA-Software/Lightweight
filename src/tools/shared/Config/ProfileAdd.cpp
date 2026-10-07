// SPDX-License-Identifier: Apache-2.0

#include "ProfileAdd.hpp"

#include <Lightweight/SqlConnectInfo.hpp>

#include <utility>

namespace Lightweight::Config
{

InlinePasswordSplit SplitInlinePassword(std::string const& connectionString)
{
    auto attributes = ParseConnectionString(SqlConnectionString { connectionString });
    auto password = std::string {};
    for (auto const* key: { "PWD", "PASSWORD" })
    {
        if (auto const node = attributes.extract(key); !node.empty())
            password = node.mapped();
    }
    if (password.empty())
        return { .connectionString = connectionString, .password = {} };
    return { .connectionString = BuildConnectionString(attributes).value, .password = std::move(password) };
}

std::expected<void, std::string> AddProfileToFile(std::filesystem::path const& path,
                                                  ProfileRequest const& request,
                                                  ReplaceExisting replace,
                                                  MakeDefault makeDefault,
                                                  Secrets::ProfileCipher const& cipher)
{
    if (request.name.empty())
        return std::unexpected("A profile needs a name.");
    if (request.dsn.empty() == request.connectionString.empty()) // both, or neither
        return std::unexpected("A profile needs exactly one of a data source name or a connection string.");

    auto stored = SplitInlinePassword(request.connectionString);
    auto const& password = request.password.empty() ? stored.password : request.password;

    auto profile = NewProfile {
        .name = request.name,
        .connectionString = std::move(stored.connectionString),
        .dsn = request.dsn,
        .uid = request.uid,
        .schema = request.schema,
        .pluginsDir = request.pluginsDir,
        .password = {},
        .backupDir = request.backupDir,
    };
    if (!password.empty())
    {
        auto encrypted = cipher.Encrypt(password);
        if (!encrypted)
            return std::unexpected(std::move(encrypted.error()));
        profile.password = std::move(*encrypted);
    }

    return EditConfigFile(path, [&](std::string_view text) {
        return AddProfileText(text, profile, replace).and_then([&](std::string edited) {
            return makeDefault == MakeDefault::Yes ? SetDefaultProfileText(edited, profile.name)
                                                   : std::expected<std::string, std::string> { std::move(edited) };
        });
    });
}

} // namespace Lightweight::Config
