// SPDX-License-Identifier: Apache-2.0

#include "Pool.hpp"

namespace Lightweight
{

DataMapperPool& GlobalDataMapperPool()
{
    static DataMapperPool pool;
    return pool;
}

} // namespace Lightweight
