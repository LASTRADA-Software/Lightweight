// SPDX-License-Identifier: Apache-2.0

 #include "entities/Table99.hpp"

#include <cstdlib>

int main()
{
    auto dm = Lightweight::DataMapper {};
    for (auto& entry: dm.Query<Table99>().All())
    {
        dm.ConfigureRelationAutoLoading(entry);
    }

    return EXIT_SUCCESS;
}
