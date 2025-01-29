// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Lightweight/DataMapper/DataMapper.hpp>

struct Account;
struct AccountHistory;
struct Suppliers;

struct Suppliers final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;
};

struct Account final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> iban;
    Field<std::optional<int64_t>> supplier_id;
};

struct AccountHistory final
{
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<int32_t> credit_rating;
    Field<std::optional<int64_t>> account_id;
};

