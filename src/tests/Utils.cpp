// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <cstdio>
#include <print>

void TestSuiteSqlLogger::WriteToStderr(std::string_view message)
{
    static std::mutex stderrMutex;
    auto const lock = std::scoped_lock(stderrMutex);
    std::println(stderr, "{}", message);
}

void TestSuiteSqlLogger::WriteRawInfo(std::string_view message) // NOLINT(readability-convert-member-functions-to-static)
{
    if (!SqlTestFixture::running)
        return;

    if (!IsTestThread())
    {
        WriteToStderr(message);
        return;
    }

    try
    {
        UNSCOPED_INFO(message);
    }
    catch (...)
    {
        std::println("{}", message);
    }
}

void TestSuiteSqlLogger::WriteRawWarning(std::string_view message)
{
    if (!IsTestThread())
    {
        WriteToStderr(message);
        return;
    }
    WARN(std::string(message));
}
