// SPDX-License-Identifier: Apache-2.0

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x) ^^x

#else
    /// @brief marco to define a member to the structure, in case of C++26 reflection this
    /// will create reflection, in case of C++20 reflection this will create a member pointer
    #define Member(x) &x
#endif

#include "entities/Table99.hpp"

#include <cstdlib>

int main()
{
    auto dm = Lightweight::DataMapper();
    for (auto& entry: dm.Query<Table99>().All())
    {
        dm->ConfigureRelationAutoLoading(entry);
    }

    return EXIT_SUCCESS;
}
