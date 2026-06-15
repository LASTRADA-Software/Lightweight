// File is automatically generated using ddl2cpp.
#pragma once

#include "Invoice.hpp"
#include "Track.hpp"

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

#include <array>
#include <string_view>

struct Invoiceline final
{
    static constexpr std::string_view TableName = "InvoiceLine";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "InvoiceLineId" }> InvoiceLineId;
    Light::BelongsTo<&Invoice::InvoiceId, Light::SqlRealName { "InvoiceId" }> InvoiceId;
    Light::BelongsTo<&Track::TrackId, Light::SqlRealName { "TrackId" }> TrackId;
    Light::Field<Light::SqlNumeric<10, 2>, Light::SqlRealName { "UnitPrice" }> UnitPrice;
    Light::Field<int32_t, Light::SqlRealName { "Quantity" }> Quantity;
};

template <>
struct Lightweight::Description<Invoiceline>
{
    static constexpr std::size_t FieldCount = 5;
    using Members = Lightweight::RecordMemberList<&Invoiceline::InvoiceLineId,
                                                  &Invoiceline::InvoiceId,
                                                  &Invoiceline::TrackId,
                                                  &Invoiceline::UnitPrice,
                                                  &Invoiceline::Quantity>;
    static constexpr std::array<std::string_view, 5> FieldNames = {
        "InvoiceLineId", "InvoiceId", "TrackId", "UnitPrice", "Quantity"
    };
};
