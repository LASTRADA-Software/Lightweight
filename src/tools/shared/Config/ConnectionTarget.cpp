// SPDX-License-Identifier: Apache-2.0

#include "ConnectionTarget.hpp"

#include <array>
#include <format>

namespace Lightweight::Config
{

namespace
{

    /// A connection-string attribute shown in a target description, and its label.
    struct DescribedAttribute
    {
        std::string_view key; ///< Upper-cased, as `ParseConnectionString` stores it.
        std::string_view label;
    };

    /// Attributes that identify a target, in display order; the first present key per label wins.
    constexpr auto DescribedAttributes = std::array {
        DescribedAttribute { .key = "SERVER", .label = "server" },
        DescribedAttribute { .key = "HOST", .label = "server" },
        DescribedAttribute { .key = "ADDRESS", .label = "server" },
        DescribedAttribute { .key = "SERVERNAME", .label = "server" },
        DescribedAttribute { .key = "ADDR", .label = "server" },
        DescribedAttribute { .key = "NETWORK ADDRESS", .label = "server" },
        DescribedAttribute { .key = "PORT", .label = "port" },
        DescribedAttribute { .key = "DATABASE", .label = "database" },
        DescribedAttribute { .key = "DSN", .label = "DSN" },
        DescribedAttribute { .key = "FILEDSN", .label = "DSN file" },
    };

    [[nodiscard]] bool IsConfigured(ConfigSource source) noexcept
    {
        return source != ConfigSource::None;
    }

} // namespace

ConnectionTarget ResolveConnectionTarget(ConnectionTargetInputs const& inputs) noexcept
{
    using enum ConnectionTargetSource;
    auto const configured = IsConfigured(inputs.configSource);
    auto const explicitConfig = inputs.configSource == ConfigSource::Explicit;
    auto const profileSelected = configured && (inputs.explicitProfile || explicitConfig);
    auto const profileConnects = configured && inputs.profileHasConnection;

    if (inputs.commandLineConnectionString)
        return { .source = CommandLine, .appliesProfileSettings = configured && inputs.explicitProfile };

    if (profileConnects && inputs.explicitProfile)
        return { .source = ExplicitProfile, .appliesProfileSettings = true };

    if (profileConnects && explicitConfig)
        return { .source = ExplicitConfig, .appliesProfileSettings = true };

    if (inputs.environmentConnectionString)
        return { .source = Environment, .appliesProfileSettings = profileSelected || (configured && !profileConnects) };

    if (profileConnects)
        return { .source = DiscoveredConfig, .appliesProfileSettings = true };

    return { .source = None, .appliesProfileSettings = configured };
}

bool IsProfileSource(ConnectionTargetSource source) noexcept
{
    using enum ConnectionTargetSource;
    return source == ExplicitProfile || source == ExplicitConfig || source == DiscoveredConfig;
}

bool AllowsWrites(ConnectionTargetSource source) noexcept
{
    return source != ConnectionTargetSource::DiscoveredConfig && source != ConnectionTargetSource::None;
}

std::string FirstProfileFallbackNote(std::string_view declaredDefault)
{
    if (declaredDefault.empty())
        return " (the first profile; the file sets no defaultProfile)";
    return std::format(" (the first profile; the file's defaultProfile '{}' names no profile)", declaredDefault);
}

std::string DescribeConnectionTarget(SqlConnectionString const& connectionString)
{
    auto const attributes = ParseConnectionString(connectionString);
    auto description = std::string {};
    auto lastLabel = std::string_view {};
    for (auto const& [key, label]: DescribedAttributes)
    {
        auto const found = attributes.find(std::string { key });
        if (found == attributes.end() || found->second.empty() || label == lastLabel)
            continue;
        if (!description.empty())
            description += ", ";
        description += std::format("{} {}", label, found->second);
        lastLabel = label;
    }
    return description.empty() ? std::string { "a connection string without server, database or DSN" } : description;
}

} // namespace Lightweight::Config
