// SPDX-License-Identifier: Apache-2.0

#include "ProfileStore.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
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

    std::string const lower = [&] {
        std::string s = connectionString;
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }();
    if (lower.find("pwd=") != std::string::npos || lower.find("password=") != std::string::npos)
        return SqlConnectionString { connectionString };

    std::string extended = connectionString;
    if (!extended.empty() && extended.back() != ';')
        extended.push_back(';');
    extended += std::format("PWD={}", password);
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

        return p;
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
        store.Upsert(std::move(p));
        store.SetDefault("default");
        return store;
    }

    if (auto defaults = root["defaultProfile"])
        store._defaultProfile = defaults.as<std::string>();

    auto profiles = root["profiles"];
    if (!profiles || !profiles.IsMap())
        return store;

    for (auto const& kv: profiles)
    {
        auto const name = kv.first.as<std::string>();
        if (!kv.second.IsMap())
            return std::unexpected(std::format("profile '{}' in {} is not a mapping", name, path.string()));

        Profile p = ProfileFromYaml(name, kv.second);

        // Sanity: dsn and connectionString are mutually exclusive.
        if (!p.dsn.empty() && !p.connectionString.empty())
            return std::unexpected(std::format("profile '{}' sets both 'dsn' and 'connectionString'; pick one", name));

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

    out << YAML::Key << "profiles" << YAML::Value << YAML::BeginMap;
    for (auto const& p: _profiles)
    {
        out << YAML::Key << p.name << YAML::Value << YAML::BeginMap;
        if (!p.pluginsDir.empty())
            out << YAML::Key << "pluginsDir" << YAML::Value << p.pluginsDir.string();
        if (!p.schema.empty())
            out << YAML::Key << "schema" << YAML::Value << p.schema;
        if (!p.dsn.empty())
            out << YAML::Key << "dsn" << YAML::Value << p.dsn;
        if (!p.connectionString.empty())
            out << YAML::Key << "connectionString" << YAML::Value << p.connectionString;
        if (!p.uid.empty())
            out << YAML::Key << "uid" << YAML::Value << p.uid;
        if (!p.secretRef.empty())
            out << YAML::Key << "secretRef" << YAML::Value << p.secretRef;
        out << YAML::EndMap;
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
