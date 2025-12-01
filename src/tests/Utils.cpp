// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

void TestSuiteSqlLogger::WriteRawInfo(std::string_view message) // NOLINT(readability-convert-member-functions-to-static)
{
    if (SqlTestFixture::running)
    {
        try
        {
            UNSCOPED_INFO(message);
        }
        catch (...)
        {
            std::println("{}", message);
        }
    }
}
