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

struct TaskList;
struct TaskListEntry;
struct User;

struct User final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<128>> fullname;
    Field<SqlAnsiString<60>> email;
};

struct TaskList final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    BelongsTo<&User::id> user;
};

struct TaskListEntry final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<std::optional<SqlDateTime>> completed;
    Field<SqlAnsiString<255>> task;
    BelongsTo<&TaskList::id> taskList;
};

