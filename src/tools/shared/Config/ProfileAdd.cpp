// SPDX-License-Identifier: Apache-2.0

#include "ProfileAdd.hpp"

#include <Lightweight/SqlConnectInfo.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace Lightweight::Config
{

namespace
{
    /// `directory` made absolute against the working directory, where it was typed: a relative value
    /// in dbtool.yml would resolve against that file's directory instead. Empty stays empty.
    std::string AbsoluteDirectory(std::string const& directory)
    {
        if (directory.empty())
            return {};
        std::error_code ec;
        auto const given = std::filesystem::path { directory };
        auto const absolute = std::filesystem::absolute(given, ec);
        return (ec ? given : absolute).lexically_normal().string();
    }
} // namespace

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
        .pluginsDir = AbsoluteDirectory(request.pluginsDir),
        .password = {},
        .backupDir = AbsoluteDirectory(request.backupDir),
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
