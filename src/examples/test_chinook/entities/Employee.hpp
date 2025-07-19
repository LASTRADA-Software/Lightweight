// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

struct Employee final
{
    static constexpr std::string_view TableName = "Employee";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "EmployeeId" }> EmployeeId;
    Light::Field<Light::SqlDynamicUtf16String<20>, Light::SqlRealName { "LastName" }> LastName;
    Light::Field<Light::SqlDynamicUtf16String<20>, Light::SqlRealName { "FirstName" }> FirstName;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<30>>, Light::SqlRealName { "Title" }> Title;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "ReportsTo" }> ReportsTo; // NB: This is also a foreign key
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "BirthDate" }> BirthDate;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "HireDate" }> HireDate;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<70>>, Light::SqlRealName { "Address" }> Address;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "City" }> City;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "State" }> State;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<40>>, Light::SqlRealName { "Country" }> Country;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<10>>, Light::SqlRealName { "PostalCode" }> PostalCode;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<24>>, Light::SqlRealName { "Phone" }> Phone;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<24>>, Light::SqlRealName { "Fax" }> Fax;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<60>>, Light::SqlRealName { "Email" }> Email;
};
