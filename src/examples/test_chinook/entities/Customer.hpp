// File is automatically generated using ddl2cpp.
#pragma once

#include "Employee.hpp"

struct Customer final
{
    static constexpr string_view TableName = "Customer";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "CustomerId" }> CustomerId;
    Field<SqlDynamicUtf16String<40>, SqlRealName { "FirstName" }> FirstName;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "LastName" }> LastName;
    Field<optional<SqlDynamicUtf16String<80>>, SqlRealName { "Company" }> Company;
    Field<optional<SqlDynamicUtf16String<70>>, SqlRealName { "Address" }> Address;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "City" }> City;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "State" }> State;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "Country" }> Country;
    Field<optional<SqlDynamicUtf16String<10>>, SqlRealName { "PostalCode" }> PostalCode;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Phone" }> Phone;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Fax" }> Fax;
    Field<SqlDynamicUtf16String<60>, SqlRealName { "Email" }> Email;
    BelongsTo<&Employee::EmployeeId, SqlRealName { "SupportRepId" }> SupportRepId;
};