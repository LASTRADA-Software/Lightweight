// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <reflection-cpp/reflection.hpp>

#include <format>
#include <iostream>

struct CommandlineArgumentDetails
{
    std::string_view key;
    std::optional<std::string_view> value;

    /// Tries to parse a single command line argument, such as: --name=value or simply --name.
    ///
    /// @param arg The command line argument to parse.
    ///
    /// @return The parsed argument details or std::nullopt if the argument was not found.
    static constexpr std::optional<CommandlineArgumentDetails> TryParse(std::string_view arg)
    {
        if (arg.starts_with("--"))
        {
            arg.remove_prefix(2); // Remove the leading "--"
            auto const assignment = arg.find('=');
            if (assignment != std::string_view::npos)
            {
                return CommandlineArgumentDetails {
                    .key = arg.substr(0, assignment),
                    .value = arg.substr(assignment + 1),
                };
            }
            else
            {
                return CommandlineArgumentDetails {
                    .key = arg,
                    .value = std::nullopt,
                };
            }
        }
        return std::nullopt;
    }
};

template <typename Configuration>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool ParseCommandLineArguments(
    Configuration* configuration,
    int argc,
    char const* argv[],
    std::function<void(std::string_view)> const& reportError = [](auto&& message) { std::cerr << message << '\n'; })
{
    auto success = true;
    auto const args = std::span<char const*> { argv, argv + static_cast<std::size_t>(argc) };
    auto argumentConsumedFlag = std::vector<bool>(args.size(), false);

    auto const fail = [&](auto&& message) {
        reportError(message);
        success = false;
    };

    using namespace std::string_view_literals;

    std::vector<size_t> configurationMembersAssigned;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--"sv)
            break;

        if (auto const details = CommandlineArgumentDetails::TryParse(args[i]); details.has_value())
        {
            auto const argumentName = details.value().key;
            auto const argumentValue = details.value().value;
            Reflection::EnumerateMembers(*configuration, [&]<size_t I, typename FieldType>(FieldType& field) {
                if (Reflection::MemberNameOf<I, Configuration> == argumentName)
                {
                    if constexpr (detail::IsStdOptional<FieldType>)
                    {
                        configurationMembersAssigned.push_back(I);
                        if (argumentValue.has_value())
                            field = argumentValue.value();
                        else
                            field = std::nullopt;
                    }
                    else if constexpr (std::is_same_v<FieldType, bool>)
                    {
                        configurationMembersAssigned.push_back(I);
                        field = true;
                    }
                    else if constexpr (std::is_same_v<FieldType, std::string>)
                    {
                        if (argumentValue.has_value())
                        {
                            configurationMembersAssigned.push_back(I);
                            field = argumentValue.value();
                        }
                        else if (i + 1 < static_cast<unsigned>(argc))
                        {
                            configurationMembersAssigned.push_back(I);
                            field = args[i + 1];
                            ++i;
                        }
                        else
                            fail(std::format("Missing value for argument `{}`", argumentName));
                    }
                    else
                    {
                        fail(std::format("Unsupported field type: {}", Reflection::TypeNameOf<FieldType>));
                    }
                }
            });
        }
        else
        {
            fail(std::format("Unknown option: {}", args[i]));
        }
    }

    // Verify that all required members are set
    Reflection::EnumerateMembers(*configuration, [&]<size_t I, typename FieldType>(FieldType& /*field*/) {
        if constexpr (!detail::IsStdOptional<FieldType> && !std::is_same_v<FieldType, bool>)
        {
            if (!std::ranges::contains(configurationMembersAssigned, I))
            {
                fail(std::format("Missing required argument: {}", Reflection::MemberNameOf<I, Configuration>));
            }
        }
    });

    return success;
}
