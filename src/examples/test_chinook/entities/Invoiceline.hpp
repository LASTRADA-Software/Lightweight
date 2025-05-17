// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include "Invoice.hpp"
#include "Track.hpp"


struct Invoiceline final
{
    static constexpr std::string_view TableName = "InvoiceLine";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"InvoiceLineId"}> InvoiceLineId;
    BelongsTo<&Invoice::InvoiceId, SqlRealName{"InvoiceId"}> InvoiceId;
    BelongsTo<&Track::TrackId, SqlRealName{"TrackId"}> TrackId;
    Field<SqlNumeric<12, 2>, SqlRealName{"UnitPrice"}> UnitPrice;
    Field<int32_t, SqlRealName{"Quantity"}> Quantity;
};

