// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnection.hpp>

struct TaskList;
struct TaskListEntry;
struct User;

struct User final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PK"}> PK;
    Field<SqlAnsiString<128>, SqlRealName{"NAME"}> NAME;
    Field<SqlAnsiString<60>, SqlRealName{"MAIL"}> MAIL;
};

struct TaskList final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PK"}> PK;
    BelongsTo<&User::id> user;
};

struct TaskListEntry final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PK"}> PK;
    Field<std::optional<SqlDateTime>, SqlRealName{"TIME"}> TIME;
    Field<SqlAnsiString<255>, SqlRealName{"INFO"}> INFO;
    BelongsTo<&TaskList::id> taskList;
};
