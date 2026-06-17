// File is automatically generated using ddl2cpp.
#pragma once

#include "Customer.hpp"

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

#include <array>
#include <string_view>

struct Invoice final
{
    static constexpr std::string_view TableName = "Invoice";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "InvoiceId" }> InvoiceId;
    Light::BelongsTo<&Customer::CustomerId, Light::SqlRealName { "CustomerId" }> CustomerId;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "InvoiceDate" }> InvoiceDate;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<70>>, Light::SqlRealName { "BillingAddress" }> BillingAddress;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "BillingCity" }> BillingCity;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "BillingState" }> BillingState;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "BillingCountry" }> BillingCountry;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<10>>, Light::SqlRealName { "BillingPostalCode" }>
        BillingPostalCode;
    Light::Field<Light::SqlNumeric<10, 2>, Light::SqlRealName { "Total" }> Total;
};

template <>
struct Lightweight::Description<Invoice>
{
    static constexpr std::size_t FieldCount = 9;
    using Members = Lightweight::RecordMemberList<&Invoice::InvoiceId,
                                                  &Invoice::CustomerId,
                                                  &Invoice::InvoiceDate,
                                                  &Invoice::BillingAddress,
                                                  &Invoice::BillingCity,
                                                  &Invoice::BillingState,
                                                  &Invoice::BillingCountry,
                                                  &Invoice::BillingPostalCode,
                                                  &Invoice::Total>;
    static constexpr std::array<std::string_view, 9> FieldNames = { "InvoiceId",      "CustomerId",        "InvoiceDate",
                                                                    "BillingAddress", "BillingCity",       "BillingState",
                                                                    "BillingCountry", "BillingPostalCode", "Total" };
};
