// File is automatically generated using ddl2cpp.
#pragma once

#include "Employee.hpp"

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
#include <Lightweight/DataMapper/DataMapper.hpp>
#endif


struct Customer final
{
    static constexpr std::string_view TableName = "Customer";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "CustomerId" }> CustomerId;
    Light::Field<Light::SqlDynamicUtf16String<40>, Light::SqlRealName { "FirstName" }> FirstName;
    Light::Field<Light::SqlDynamicUtf16String<20>, Light::SqlRealName { "LastName" }> LastName;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<80>>, Light::SqlRealName { "Company" }> Company;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<70>>, Light::SqlRealName { "Address" }> Address;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "City" }> City;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "State" }> State;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "Country" }> Country;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<10>>, Light::SqlRealName { "PostalCode" }> PostalCode;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<24>>, Light::SqlRealName { "Phone" }> Phone;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<24>>, Light::SqlRealName { "Fax" }> Fax;
    Light::Field<Light::SqlDynamicUtf16String<60>, Light::SqlRealName { "Email" }> Email;
    Light::BelongsTo<&Employee::EmployeeId, Light::SqlRealName { "SupportRepId" }, Light::SqlNullable::Null> SupportRepId;
};

