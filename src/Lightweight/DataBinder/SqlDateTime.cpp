// SPDX-License-Identifier: Apache-2.0

#include "SqlDateTime.hpp"
#include "SqlDate.hpp"
#include "SqlTime.hpp"

namespace Lightweight
{

SqlDate SqlDateTime::date() const noexcept
{
    return SqlDate { year(), month(), day() };
}

SqlTime SqlDateTime::time() const noexcept
{
    return SqlTime { hour(), minute(), second(), 
                    std::chrono::duration_cast<std::chrono::microseconds>(nanosecond()) };
}

} // namespace Lightweight