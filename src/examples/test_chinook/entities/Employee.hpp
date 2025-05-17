// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Employee final
{
    static constexpr std::string_view TableName = "Employee";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"EmployeeId"}> EmployeeId;
    Field<SqlUtf16String<20>, SqlRealName{"LastName"}> LastName;
    Field<SqlUtf16String<20>, SqlRealName{"FirstName"}> FirstName;
    Field<std::optional<SqlUtf16String<30>>, SqlRealName{"Title"}> Title;
    Field<std::optional<int32_t>, SqlRealName{"ReportsTo"}> ReportsTo; // NB: This is also a foreign key
    Field<std::optional<SqlDateTime>, SqlRealName{"BirthDate"}> BirthDate;
    Field<std::optional<SqlDateTime>, SqlRealName{"HireDate"}> HireDate;
    Field<std::optional<SqlUtf16String<70>>, SqlRealName{"Address"}> Address;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"City"}> City;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"State"}> State;
    Field<std::optional<SqlUtf16String<40>>, SqlRealName{"Country"}> Country;
    Field<std::optional<SqlUtf16String<10>>, SqlRealName{"PostalCode"}> PostalCode;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Phone"}> Phone;
    Field<std::optional<SqlUtf16String<24>>, SqlRealName{"Fax"}> Fax;
    Field<std::optional<SqlUtf16String<60>>, SqlRealName{"Email"}> Email;
};

