// SPDX-License-Identifier: Apache-2.0

#include "ProfileFileEditor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <ranges>
#include <thread>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace Lightweight::Config
{

namespace
{

    /// Bounded retry of the replacing rename (see EditConfigFile): 20 x 100 ms.
    inline constexpr int RenameAttempts = 20;
    inline constexpr auto RenameRetryDelay = std::chrono::milliseconds(100);

    /// Half-open byte range of a scalar in the source text.
    struct Extent
    {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    /// Parses YAML text, converting yaml-cpp exceptions into the error channel.
    std::expected<YAML::Node, std::string> Parse(std::string_view yaml)
    {
        try
        {
            return YAML::Load(std::string { yaml });
        }
        catch (YAML::Exception const& e)
        {
            return std::unexpected(std::format("cannot parse the configuration file: {}", e.what()));
        }
    }

    /// The line ending used by the file: CRLF if it appears anywhere, LF otherwise.
    std::string_view LineEnding(std::string_view yaml) noexcept
    {
        return yaml.contains("\r\n") ? std::string_view { "\r\n" } : std::string_view { "\n" };
    }

    /// Finds a mapping entry by key, returning (key node, value node).
    std::optional<std::pair<YAML::Node, YAML::Node>> FindEntry(YAML::Node const& map, std::string_view key)
    {
        if (!map.IsMap())
            return std::nullopt;
        for (auto const& entry: map)
            if (entry.first.IsScalar() && entry.first.Scalar() == key)
                return std::pair { entry.first, entry.second };
        return std::nullopt;
    }

    /// Mirrors ProfileStore's legacy detection: top-level connection keys and no `profiles` map.
    bool IsLegacyShape(YAML::Node const& root)
    {
        if (!root.IsMap() || FindEntry(root, "profiles"))
            return false;
        constexpr auto LegacyKeys =
            std::array { "PluginsDir", "ConnectionString", "Schema", "pluginsDir", "connectionString", "schema" };
        return std::ranges::any_of(LegacyKeys, [&](char const* key) { return FindEntry(root, key).has_value(); });
    }

    /// Extent of a double-quoted scalar starting at `begin` (which holds the opening quote).
    std::expected<Extent, std::string> DoubleQuotedExtent(std::string_view yaml, std::size_t begin)
    {
        auto index = begin + 1;
        while (index < yaml.size() && yaml[index] != '"')
            index += yaml[index] == '\\' ? 2 : 1;
        if (index >= yaml.size())
            return std::unexpected(std::string { "unterminated double-quoted value" });
        return Extent { .begin = begin, .end = index + 1 };
    }

    /// Extent of a single-quoted scalar starting at `begin`; `''` is an escaped quote.
    std::expected<Extent, std::string> SingleQuotedExtent(std::string_view yaml, std::size_t begin)
    {
        auto index = yaml.find('\'', begin + 1);
        while (index != std::string_view::npos && index + 1 < yaml.size() && yaml[index + 1] == '\'')
            index = yaml.find('\'', index + 2);
        if (index == std::string_view::npos)
            return std::unexpected(std::string { "unterminated single-quoted value" });
        return Extent { .begin = begin, .end = index + 1 };
    }

    /// Whether the plain scalar that started at `begin` ends before position `pos`.
    bool EndsPlainScalar(std::string_view yaml, std::size_t begin, std::size_t pos, bool inFlow) noexcept
    {
        auto const c = yaml[pos];
        auto const afterBlank = pos > begin && (yaml[pos - 1] == ' ' || yaml[pos - 1] == '\t');
        return c == '\n' || c == '\r' || (c == '#' && afterBlank) || (inFlow && (c == ',' || c == '}' || c == ']'));
    }

    /// Extent of a plain scalar: up to the line end, a comment, or (inside {...}) a
    /// flow indicator, without trailing blanks.
    Extent PlainExtent(std::string_view yaml, std::size_t begin, bool inFlow) noexcept
    {
        auto end = begin;
        while (end < yaml.size() && !EndsPlainScalar(yaml, begin, end, inFlow))
            ++end;
        while (end > begin && (yaml[end - 1] == ' ' || yaml[end - 1] == '\t'))
            --end;
        return Extent { .begin = begin, .end = end };
    }

    /// Source extent of the scalar that starts at `begin`. Block scalars are refused
    /// because replacing them would require reflowing the following lines.
    std::expected<Extent, std::string> ScalarExtent(std::string_view yaml, std::size_t begin, bool inFlow)
    {
        if (begin >= yaml.size())
            return std::unexpected(std::string { "value position is outside the file" });

        switch (yaml[begin])
        {
            case '|':
            case '>':
                return std::unexpected(std::string { "block scalars (| or >) cannot be rewritten in place" });
            case '"':
                return DoubleQuotedExtent(yaml, begin);
            case '\'':
                return SingleQuotedExtent(yaml, begin);
            default:
                return PlainExtent(yaml, begin, inFlow);
        }
    }

    /// Replaces the scalar value node `value` with `replacement` (already quoted).
    std::expected<std::string, std::string> ReplaceScalar(std::string_view yaml,
                                                          YAML::Node const& value,
                                                          bool inFlow,
                                                          std::string_view replacement)
    {
        return ScalarExtent(yaml, static_cast<std::size_t>(value.Mark().pos), inFlow).transform([&](Extent extent) {
            auto edited = std::string { yaml };
            edited.replace(extent.begin, extent.end - extent.begin, replacement);
            return edited;
        });
    }

    /// Offset of the first character of the line containing `pos`.
    std::size_t LineStart(std::string_view yaml, std::size_t pos) noexcept
    {
        auto const newline = yaml.rfind('\n', pos == 0 ? 0 : pos - 1);
        return newline == std::string_view::npos || pos == 0 ? 0 : newline + 1;
    }

    /// Offset just past the line containing `pos` (including its line ending).
    std::size_t NextLineStart(std::string_view yaml, std::size_t pos) noexcept
    {
        auto const newline = yaml.find('\n', pos);
        return newline == std::string_view::npos ? yaml.size() : newline + 1;
    }

    /// Indentation width of the line starting at `lineStart`, or nullopt for blank lines.
    std::optional<std::size_t> Indentation(std::string_view yaml, std::size_t lineStart) noexcept
    {
        auto const lineEnd = yaml.find_first_of("\r\n", lineStart);
        auto const line =
            yaml.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);
        auto const firstContent = line.find_first_not_of(" \t");
        if (firstContent == std::string_view::npos)
            return std::nullopt;
        return firstContent;
    }

    /// Renders a profile block: a key line at `keyIndent` and fields at `fieldIndent`.
    std::string RenderProfile(NewProfile const& profile,
                              std::size_t keyIndent,
                              std::size_t fieldIndent,
                              std::string_view eol)
    {
        auto out = std::format("{}{}:{}", std::string(keyIndent, ' '), QuoteYamlScalar(profile.name), eol);
        auto const fields = std::array<std::pair<std::string_view, std::string const*>, 6> { {
            { "connectionString", &profile.connectionString },
            { "dsn", &profile.dsn },
            { "uid", &profile.uid },
            { "schema", &profile.schema },
            { "pluginsDir", &profile.pluginsDir },
            { "password", &profile.password },
        } };
        for (auto const& [key, value]: fields)
            if (!value->empty())
                out += std::format("{}{}: {}{}", std::string(fieldIndent, ' '), key, QuoteYamlScalar(*value), eol);
        return out;
    }

    /// Removes the block of profile key `key` (its line plus all deeper-indented or blank lines
    /// that follow it, without trailing blank lines).
    std::string RemoveProfileBlock(std::string_view yaml, YAML::Node const& key)
    {
        auto const keyPos = static_cast<std::size_t>(key.Mark().pos);
        auto const keyColumn = static_cast<std::size_t>(key.Mark().column);
        auto const blockStart = LineStart(yaml, keyPos);
        auto blockEnd = NextLineStart(yaml, keyPos);
        auto scan = blockEnd;
        while (scan < yaml.size())
        {
            auto const indent = Indentation(yaml, scan);
            if (indent && *indent <= keyColumn)
                break;
            auto const next = NextLineStart(yaml, scan);
            if (indent)
                blockEnd = next; // only content lines extend the block; trailing blanks stay
            scan = next;
        }
        auto edited = std::string { yaml };
        edited.erase(blockStart, blockEnd - blockStart);
        return edited;
    }

    /// Inserts `profile` directly after the `profiles:` key line.
    std::string InsertAfterProfilesKey(std::string_view yaml,
                                       YAML::Node const& profilesKey,
                                       YAML::Node const& profilesValue,
                                       NewProfile const& profile,
                                       std::string_view eol)
    {
        auto const profilesColumn = static_cast<std::size_t>(profilesKey.Mark().column);
        auto childIndent = profilesColumn + 2;
        if (profilesValue.IsMap() && profilesValue.size() > 0)
            childIndent = static_cast<std::size_t>(profilesValue.begin()->first.Mark().column);
        auto const fieldIndent = childIndent + (childIndent - profilesColumn);

        auto edited = std::string { yaml };
        auto insertAt = NextLineStart(yaml, static_cast<std::size_t>(profilesKey.Mark().pos));
        if (insertAt == yaml.size() && !yaml.ends_with('\n'))
        {
            edited += eol;
            insertAt = edited.size();
        }
        edited.insert(insertAt, RenderProfile(profile, childIndent, fieldIndent, eol));
        return edited;
    }

} // namespace

std::string QuoteYamlScalar(std::string_view value)
{
    auto out = std::string { "\"" };
    for (auto const c: value)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out += std::format("\\x{:02X}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                else
                    out += c;
        }
    }
    out += '"';
    return out;
}

std::expected<std::string, std::string> SetProfilePasswordText(std::string_view yaml, PasswordEdit const& edit)
{
    auto const [profileName, newValue] = edit;
    auto const root = Parse(yaml);
    if (!root)
        return std::unexpected(root.error());

    auto const replacement = QuoteYamlScalar(newValue);
    if (IsLegacyShape(*root))
    {
        auto const password = FindEntry(*root, "Password").or_else([&] { return FindEntry(*root, "password"); });
        if (!password || !password->second.IsScalar())
            return std::unexpected(std::format("profile '{}' has no password to replace", profileName));
        return ReplaceScalar(yaml, password->second, root->Style() == YAML::EmitterStyle::Flow, replacement);
    }

    auto const profiles = FindEntry(*root, "profiles");
    auto const profile = profiles ? FindEntry(profiles->second, profileName) : std::nullopt;
    if (!profile || !profile->second.IsMap())
        return std::unexpected(std::format("profile '{}' not found in the configuration file", profileName));

    auto const password = FindEntry(profile->second, "password");
    if (!password || !password->second.IsScalar())
        return std::unexpected(std::format("profile '{}' has no password to replace", profileName));

    return ReplaceScalar(yaml, password->second, profile->second.Style() == YAML::EmitterStyle::Flow, replacement)
        .transform_error([&](std::string const& error) {
            return std::format("cannot rewrite the password of profile '{}': {}", profileName, error);
        });
}

std::expected<std::string, std::string> AddProfileText(std::string_view yaml,
                                                       NewProfile const& profile,
                                                       ReplaceExisting replace)
{
    auto const root = Parse(yaml);
    if (!root)
        return std::unexpected(root.error());
    auto const eol = LineEnding(yaml);

    if (IsLegacyShape(*root))
        return std::unexpected(std::string {
            "the configuration file uses the legacy single-profile format; convert it to the 'profiles:' format first" });
    if (!root->IsNull() && !root->IsMap())
        return std::unexpected(std::string { "the configuration file is not a YAML mapping" });
    if (root->IsMap() && root->Style() == YAML::EmitterStyle::Flow)
        return std::unexpected(std::string { "flow-style ({...}) configuration files cannot be edited in place" });

    auto const profiles = FindEntry(*root, "profiles");
    if (!profiles)
    {
        auto edited = std::string { yaml };
        if (!edited.empty() && !edited.ends_with('\n'))
            edited += eol;
        edited += std::format("profiles:{}", eol);
        edited += RenderProfile(profile, 2, 4, eol);
        return edited;
    }

    auto const& [profilesKey, profilesValue] = *profiles;
    if (!profilesValue.IsNull() && !profilesValue.IsMap())
        return std::unexpected(std::string { "'profiles' must be a mapping" });

    if (profilesValue.IsMap() && profilesValue.Style() == YAML::EmitterStyle::Flow)
    {
        if (profilesValue.size() > 0)
            return std::unexpected(std::string { "flow-style ({...}) 'profiles' maps cannot be edited in place" });
        // `profiles: {}` — drop the empty braces and continue as for a bare `profiles:`.
        auto const valuePos = static_cast<std::size_t>(profilesValue.Mark().pos);
        auto const close = yaml.find('}', valuePos);
        auto start = valuePos;
        while (start > 0 && (yaml[start - 1] == ' ' || yaml[start - 1] == '\t'))
            --start;
        auto edited = std::string { yaml };
        edited.erase(start, close + 1 - start);
        return AddProfileText(edited, profile, replace);
    }

    if (auto const existing = FindEntry(profilesValue, profile.name))
    {
        if (replace == ReplaceExisting::No)
            return std::unexpected(std::format("profile '{}' already exists (use --force to replace it)", profile.name));
        return AddProfileText(RemoveProfileBlock(yaml, existing->first), profile, ReplaceExisting::No);
    }

    return InsertAfterProfilesKey(yaml, profilesKey, profilesValue, profile, eol);
}

std::expected<std::string, std::string> SetDefaultProfileText(std::string_view yaml, std::string_view profileName)
{
    auto const root = Parse(yaml);
    if (!root)
        return std::unexpected(root.error());

    auto const replacement = QuoteYamlScalar(profileName);
    if (auto const current = FindEntry(*root, "defaultProfile"); current && current->second.IsScalar())
        return ReplaceScalar(yaml, current->second, root->Style() == YAML::EmitterStyle::Flow, replacement);

    return std::format("defaultProfile: {}{}{}", replacement, LineEnding(yaml), yaml);
}

std::expected<void, std::string> EditConfigFile(std::filesystem::path const& path, ConfigTextTransform const& transform)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    auto original = std::string {};
    auto const exists = fs::exists(path, ec);
    if (exists)
    {
        if (!fs::is_regular_file(path, ec))
            return std::unexpected(std::format("{} is not a regular file", path.string()));
        if ((fs::status(path, ec).permissions() & fs::perms::owner_write) == fs::perms::none)
            return std::unexpected(std::format("{} is read-only", path.string()));
        auto in = std::ifstream(path, std::ios::binary);
        if (!in)
            return std::unexpected(std::format("cannot read {}", path.string()));
        original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    auto edited = transform(original);
    if (!edited)
        return std::unexpected(std::move(edited.error()));

    if (path.has_parent_path())
    {
        fs::create_directories(path.parent_path(), ec);
        if (ec)
            return std::unexpected(std::format("cannot create {}: {}", path.parent_path().string(), ec.message()));
    }

    auto temp = path;
    temp += std::format(".tmp-{:08x}", std::random_device {}());
    {
        auto out = std::ofstream(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return std::unexpected(std::format("cannot write {}", temp.string()));
        out << *edited;
        if (!out.flush())
        {
            out.close();
            fs::remove(temp, ec);
            return std::unexpected(std::format("cannot write {}", temp.string()));
        }
    }
    if (exists)
        fs::permissions(temp, fs::status(path, ec).permissions(), ec);

    // Virus scanners and search indexers briefly hold freshly written files open
    // without delete sharing, so on Windows a replacing rename can fail with
    // "access denied" for a moment. Retry for a bounded time before giving up.
    for (auto const attempt: std::views::iota(0, RenameAttempts))
    {
        fs::rename(temp, path, ec);
        if (!ec || ec != std::errc::permission_denied || attempt + 1 == RenameAttempts)
            break;
        std::this_thread::sleep_for(RenameRetryDelay);
    }
    if (ec)
    {
        auto const message = ec.message();
        fs::remove(temp, ec);
        return std::unexpected(std::format("cannot replace {}: {}", path.string(), message));
    }
    return {};
}

} // namespace Lightweight::Config
