// File is automatically generated using ddl2cpp.
#pragma once

#include "Invoice.hpp"
#include "Track.hpp"

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
#include <Lightweight/DataMapper/DataMapper.hpp>
#endif


struct Invoiceline final
{
    static constexpr std::string_view TableName = "InvoiceLine";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "InvoiceLineId" }> InvoiceLineId;
    Light::BelongsTo<&Invoice::InvoiceId, Light::SqlRealName { "InvoiceId" }> InvoiceId;
    Light::BelongsTo<&Track::TrackId, Light::SqlRealName { "TrackId" }> TrackId;
    Light::Field<Light::SqlNumeric<10, 2>, Light::SqlRealName { "UnitPrice" }> UnitPrice;
    Light::Field<int32_t, Light::SqlRealName { "Quantity" }> Quantity;
};

