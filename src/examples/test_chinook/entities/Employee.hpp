// File is automatically generated using ddl2cpp.
#pragma once

struct Employee final
{
    static constexpr string_view TableName = "Employee";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "EmployeeId" }> EmployeeId;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "LastName" }> LastName;
    Field<SqlDynamicUtf16String<20>, SqlRealName { "FirstName" }> FirstName;
    Field<optional<SqlDynamicUtf16String<30>>, SqlRealName { "Title" }> Title;
    Field<optional<int32_t>, SqlRealName { "ReportsTo" }> ReportsTo;
    Field<optional<SqlDateTime>, SqlRealName { "BirthDate" }> BirthDate;
    Field<optional<SqlDateTime>, SqlRealName { "HireDate" }> HireDate;
    Field<optional<SqlDynamicUtf16String<70>>, SqlRealName { "Address" }> Address;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "City" }> City;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "State" }> State;
    Field<optional<SqlDynamicUtf16String<40>>, SqlRealName { "Country" }> Country;
    Field<optional<SqlDynamicUtf16String<10>>, SqlRealName { "PostalCode" }> PostalCode;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Phone" }> Phone;
    Field<optional<SqlDynamicUtf16String<24>>, SqlRealName { "Fax" }> Fax;
    Field<optional<SqlDynamicUtf16String<60>>, SqlRealName { "Email" }> Email;
};
