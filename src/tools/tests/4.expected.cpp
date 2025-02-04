// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Lightweight/DataMapper/DataMapper.hpp>

struct tasklist;
struct tasklistentry;
struct user;

struct user final
{
    static constexpr std::string_view TableName = "User"sv;
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PK" }> PK;
    Field<SqlAnsiString<128>, SqlRealName { "NAME" }> NAME;
    Field<SqlAnsiString<60>, SqlRealName { "MAIL" }> MAIL;
};

struct tasklist final
{
    static constexpr std::string_view TableName = "TaskList"sv;
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PK" }> PK;
    BelongsTo<&User::PK> user;
};

struct tasklistentry final
{
    static constexpr std::string_view TableName = "TaskListEntry"sv;
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PK" }> PK;
    Field<std::optional<SqlDateTime>, SqlRealName { "TIME" }> TIME;
    Field<SqlAnsiString<255>, SqlRealName { "INFO" }> INFO;
    BelongsTo<&TaskList::PK> taskList;
};
