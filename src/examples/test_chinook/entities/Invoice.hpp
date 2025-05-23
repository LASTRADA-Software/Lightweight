// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include "Customer.hpp"


struct Invoice final
{
    static constexpr std::string_view TableName = "Invoice";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"InvoiceId"}> InvoiceId;
    BelongsTo<&Customer::CustomerId, SqlRealName{"CustomerId"}> CustomerId;
    Field<SqlDateTime, SqlRealName{"InvoiceDate"}> InvoiceDate;
    Field<std::optional<SqlUtf16String<70>>, SqlRealName{"BillingAddress"}> BillingAddress;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingCity"}> BillingCity;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingState"}> BillingState;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"BillingCountry"}> BillingCountry;
    Field<std::optional<SqlUtf16String<10>>, SqlRealName{"BillingPostalCode"}> BillingPostalCode;
    Field<SqlNumeric<10, 2>, SqlRealName{"Total"}> Total;
};

