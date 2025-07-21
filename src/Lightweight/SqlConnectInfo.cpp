// SPDX-License-Identifier: Apache-2.0

#include "SqlConnectInfo.hpp"

#include <algorithm>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>

namespace Lightweight
{

namespace
{
    constexpr std::string_view DropQuotation(std::string_view value) noexcept
    {
        if (!value.empty() && value.front() == '{' && value.back() == '}')
        {
            value.remove_prefix(1);
            value.remove_suffix(1);
        }
        return value;
    }

    constexpr std::string_view Trim(std::string_view value) noexcept
    {
        while (!value.empty() && std::isspace(value.front()))
            value.remove_prefix(1);

        while (!value.empty() && std::isspace(value.back()))
            value.remove_suffix(1);

        return value;
    }

    std::string ToUpperCaseString(std::string_view input)
    {
        std::string result { input };
        std::ranges::transform(result, result.begin(), [](char c) { return (char) std::toupper(c); });
        return result;
    }

} // end namespace
std::string SqlConnectionString::Sanitized() const
{
    return SanitizePwd(value);
}

std::string SqlConnectionString::SanitizePwd(std::string_view input)
{
    std::regex const pwdRegex {
        R"(PWD=.*?;)",
        std::regex_constants::ECMAScript | std::regex_constants::icase,
    };
    std::stringstream outputString;
    std::regex_replace(std::ostreambuf_iterator<char> { outputString }, input.begin(), input.end(), pwdRegex, "Pwd=***;");
    return outputString.str();
}

SqlConnectionStringMap ParseConnectionString(SqlConnectionString const& connectionString)
{
    auto pairs = connectionString.value | std::views::split(';') | std::views::transform([](auto pair_view) {
                     return std::string_view(&*pair_view.begin(), std::ranges::distance(pair_view));
                 });

    SqlConnectionStringMap result;

    for (auto const& pair: pairs)
    {
        auto separatorPosition = pair.find('=');
        if (separatorPosition != std::string_view::npos)
        {
            auto const key = Trim(pair.substr(0, separatorPosition));
            auto const value = DropQuotation(Trim(pair.substr(separatorPosition + 1)));
            result.insert_or_assign(ToUpperCaseString(key), std::string(value));
        }
    }

    return result;
}

SqlConnectionString BuildConnectionString(SqlConnectionStringMap const& map)
{
    SqlConnectionString result;

    for (auto const& [key, value]: map)
    {
        std::string_view const delimiter = result.value.empty() ? "" : ";";
        result.value += std::format("{}{}={{{}}}", delimiter, key, value);
    }

    return result;
}

SqlConnectionDataSource SqlConnectionDataSource::FromConnectionString(SqlConnectionString const& value)
{
    auto result = SqlConnectionDataSource {};
    auto parsedConnectionStringPairs = ParseConnectionString(value);

    if (auto dsn = parsedConnectionStringPairs.extract("DSN"); !dsn.empty())
        result.datasource = std::move(dsn.mapped());

    if (auto uid = parsedConnectionStringPairs.extract("UID"); !uid.empty())
        result.username = std::move(uid.mapped());

    if (auto pwd = parsedConnectionStringPairs.extract("PWD"); !pwd.empty())
        result.password = std::move(pwd.mapped());

    if (auto timeout = parsedConnectionStringPairs.extract("TIMEOUT"); !timeout.empty())
        result.timeout = std::chrono::seconds(std::stoi(timeout.mapped()));

    return result;
}

} // namespace Lightweight
