// SPDX-License-Identifier: Apache-2.0

#include "DataMapper.hpp"

namespace Lightweight
{

DataMapper& DataMapper::AcquireThreadLocal()
{
    thread_local auto instance = DataMapper { SqlConnection::DefaultConnectionString() };
    return instance;
}

} // namespace Lightweight
