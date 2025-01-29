// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/SqlQuery.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlScopedTraceLogger.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTransaction.hpp>

struct Person;

struct Person final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<25>> name;
    Field<bool> is_active;
    Field<std::optional<int32_t>> age;
};

