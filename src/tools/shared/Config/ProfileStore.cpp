// SPDX-License-Identifier: Apache-2.0

#include "ProfileStore.hpp"

#include <Lightweight/SqlConnectInfo.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    // Must precede *any* Windows SDK header, not just <windows.h> itself: <shlobj.h> pulls in
    // windows.h internally, so defining these only in front of the explicit <windows.h> below
    // would already be too late to suppress its min/max macros.
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <shlobj.h>
    #include <windows.h>
#else
    #include <sys/types.h>

    #include <pwd.h>
    #include <unistd.h>
#endif

namespace Lightweight::Config
{

SqlConnectInfo Profile::ToConnectInfo(std::string_view password) const
{
    if (!dsn.empty())
    {
        return SqlConnectionDataSource {
            .datasource = dsn,
            .username = uid,
            .password = std::string(password),
        };
    }
    // For raw connection strings we honour them as-is. If the caller has a
    // resolved password and the connection string does not already contain
    // a PWD field, append it. Otherwise we trust what the user wrote.
    if (password.empty())
        return SqlConnectionString { connectionString };

    // Whole attribute names only (keys come back upper-cased): a `pwd=` buried inside some other
    // value, such as a database path, must not make the resolved secret vanish.
    auto const attributes = ParseConnectionString(SqlConnectionString { connectionString });
    if (attributes.contains("PWD") || attributes.contains("PASSWORD"))
        return SqlConnectionString { connectionString };

    std::string extended = connectionString;
    if (!extended.empty() && extended.back() != ';')
        extended.push_back(';');
    extended += std::format("PWD={}", FormatConnectionStringValue(password));
    return SqlConnectionString { std::move(extended) };
}

std::filesystem::path ProfileStore::DefaultPath()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return std::filesystem::path(path) / "dbtool" / "dbtool.yml";
    return "dbtool.yml";
#else
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "dbtool" / "dbtool.yml";

    char const* home = std::getenv("HOME");
    if (!home)
    {
        if (passwd* pwd = getpwuid(getuid()))
            home = pwd->pw_dir;
    }
    if (home && *home)
        return std::filesystem::path(home) / ".config" / "dbtool" / "dbtool.yml";

    return "dbtool.yml";
#endif
}

namespace
{

    /// Attempts to parse a Profile from a YAML mapping. Unknown keys are ignored.
    Profile ProfileFromYaml(std::string name, YAML::Node const& node)
    {
        Profile p;
        p.name = std::move(name);
        if (auto n = node["pluginsDir"])
            p.pluginsDir = n.as<std::string>();
        else if (auto nLegacy = node["PluginsDir"]) // legacy casing
            p.pluginsDir = nLegacy.as<std::string>();

        if (auto n = node["schema"])
            p.schema = n.as<std::string>();
        else if (auto nLegacy = node["Schema"])
            p.schema = nLegacy.as<std::string>();

        if (auto n = node["dsn"])
            p.dsn = n.as<std::string>();

        if (auto n = node["connectionString"])
            p.connectionString = n.as<std::string>();
        else if (auto nLegacy = node["ConnectionString"])
            p.connectionString = nLegacy.as<std::string>();

        if (auto n = node["uid"])
            p.uid = n.as<std::string>();

        if (auto n = node["secretRef"])
            p.secretRef = n.as<std::string>();

        if (auto n = node["password"])
            p.password = n.as<std::string>();
        else if (auto nLegacy = node["Password"])
            p.password = nLegacy.as<std::string>();

        return p;
    }

    /// Parses `defaultPluginsDir`: a single string or a list of strings.
    std::expected<std::vector<std::filesystem::path>, std::string> ParseDefaultPluginsDir(YAML::Node const& node,
                                                                                          std::filesystem::path const& path)
    {
        auto const error = [&] {
            return std::unexpected(
                std::format("defaultPluginsDir in {} must be a string or a list of strings", path.string()));
        };
        if (node.IsScalar())
            return std::vector<std::filesystem::path> { node.as<std::string>() };
        if (!node.IsSequence())
            return error();

        auto dirs = std::vector<std::filesystem::path> {};
        for (auto const& item: node)
        {
            if (!item.IsScalar())
                return error();
            dirs.emplace_back(item.as<std::string>());
        }
        return dirs;
    }

    /// Writes one profile as a YAML mapping entry; empty fields are omitted.
    void EmitProfile(YAML::Emitter& out, Profile const& p)
    {
        out << YAML::Key << p.name << YAML::Value << YAML::BeginMap;
        auto const emit = [&out](char const* key, std::string const& value) {
            if (!value.empty())
                out << YAML::Key << key << YAML::Value << value;
        };
        emit("pluginsDir", p.pluginsDir.string());
        emit("schema", p.schema);
        emit("dsn", p.dsn);
        emit("connectionString", p.connectionString);
        emit("uid", p.uid);
        emit("secretRef", p.secretRef);
        if (!p.password.empty())
            out << YAML::Key << "password" << YAML::Value << YAML::DoubleQuoted << p.password;
        out << YAML::EndMap;
    }

    /// Rejects field combinations that make a profile ambiguous.
    std::expected<void, std::string> ValidateProfile(Profile const& p)
    {
        if (!p.dsn.empty() && !p.connectionString.empty())
            return std::unexpected(std::format("profile '{}' sets both 'dsn' and 'connectionString'; pick one", p.name));
        if (!p.password.empty() && !p.secretRef.empty())
            return std::unexpected(std::format("profile '{}' sets both 'password' and 'secretRef'; pick one", p.name));
        return {};
    }

    /// True when the YAML document looks like the old single-profile schema
    /// (top-level PluginsDir/ConnectionString/Schema keys, no `profiles` map).
    bool LooksLikeLegacyShape(YAML::Node const& root)
    {
        if (root["profiles"])
            return false;
        return root["PluginsDir"] || root["ConnectionString"] || root["Schema"] || root["pluginsDir"]
               || root["connectionString"] || root["schema"];
    }

} // namespace

std::expected<ProfileStore, std::string> ProfileStore::LoadOrDefault(std::filesystem::path path)
{
    if (path.empty())
        path = DefaultPath();

    if (!std::filesystem::exists(path))
        return ProfileStore {}; // empty store is valid

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (std::exception const& e)
    {
        return std::unexpected(std::format("failed to parse {}: {}", path.string(), e.what()));
    }

    ProfileStore store;

    if (LooksLikeLegacyShape(root))
    {
        // Translate to a single "default" profile preserving behaviour.
        Profile p = ProfileFromYaml("default", root);
        if (auto valid = ValidateProfile(p); !valid)
            return std::unexpected(std::move(valid.error()));
        store.Upsert(std::move(p));
        store.SetDefault("default");
        return store;
    }

    if (auto defaults = root["defaultProfile"])
        store._defaultProfile = defaults.as<std::string>();

    if (auto n = root["defaultPluginsDir"])
    {
        auto dirs = ParseDefaultPluginsDir(n, path);
        if (!dirs)
            return std::unexpected(std::move(dirs.error()));
        store._defaultPluginsDir = std::move(*dirs);
    }

    auto profiles = root["profiles"];
    if (!profiles || !profiles.IsMap())
        return store;

    for (auto const& kv: profiles)
    {
        auto const name = kv.first.as<std::string>();
        if (!kv.second.IsMap())
            return std::unexpected(std::format("profile '{}' in {} is not a mapping", name, path.string()));

        Profile p = ProfileFromYaml(name, kv.second);

        if (auto valid = ValidateProfile(p); !valid)
            return std::unexpected(std::move(valid.error()));

        store.Upsert(std::move(p));
    }

    return store;
}

std::expected<void, std::string> ProfileStore::Save(std::filesystem::path path) const
{
    if (path.empty())
        path = DefaultPath();

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return std::unexpected(std::format("failed to create {}: {}", path.parent_path().string(), ec.message()));

    YAML::Emitter out;
    out << YAML::BeginMap;
    if (!_defaultProfile.empty())
        out << YAML::Key << "defaultProfile" << YAML::Value << _defaultProfile;
    if (!_defaultPluginsDir.empty())
    {
        out << YAML::Key << "defaultPluginsDir" << YAML::Value;
        if (_defaultPluginsDir.size() == 1)
        {
            // Scalar form keeps the YAML file backward-compatible with older
            // readers and with hand-written single-directory configs.
            out << _defaultPluginsDir.front().string();
        }
        else
        {
            out << YAML::BeginSeq;
            for (auto const& dir: _defaultPluginsDir)
                out << dir.string();
            out << YAML::EndSeq;
        }
    }

    out << YAML::Key << "profiles" << YAML::Value << YAML::BeginMap;
    for (auto const& p: _profiles)
    {
        EmitProfile(out, p);
    }
    out << YAML::EndMap;
    out << YAML::EndMap;

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os)
        return std::unexpected(std::format("failed to open {} for writing", path.string()));
    os << out.c_str();
    if (!os)
        return std::unexpected(std::format("failed to write {}", path.string()));

    return {};
}

Profile const* ProfileStore::Find(std::string_view name) const noexcept
{
    auto it = std::ranges::find_if(_profiles, [&](Profile const& p) { return p.name == name; });
    return it == _profiles.end() ? nullptr : &*it;
}

Profile const* ProfileStore::Default() const noexcept
{
    if (!_defaultProfile.empty())
        if (Profile const* p = Find(_defaultProfile))
            return p;
    return _profiles.empty() ? nullptr : &_profiles.front();
}

void ProfileStore::Upsert(Profile profile)
{
    auto it = std::ranges::find_if(_profiles, [&](Profile const& p) { return p.name == profile.name; });
    if (it == _profiles.end())
        _profiles.push_back(std::move(profile));
    else
        *it = std::move(profile);
}

bool ProfileStore::Remove(std::string_view name)
{
    auto it = std::ranges::find_if(_profiles, [&](Profile const& p) { return p.name == name; });
    if (it == _profiles.end())
        return false;
    _profiles.erase(it);
    if (_defaultProfile == name)
        _defaultProfile.clear();
    return true;
}

} // namespace Lightweight::Config
