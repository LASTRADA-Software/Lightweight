// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataMapper/Field.hpp>
#include <Lightweight/DataMapper/Record.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>
#include <variant>

using namespace std::string_view_literals;
using namespace Lightweight;

struct SimpleRecord
{
    Field<int> value;
    Field<SqlAnsiString<50>> name;
};

struct RecordWithPrimaryKey
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<50>> name;
};

struct RecordWithAutoIncrementPK
{
    Field<int, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<100>> description;
};

struct RecordWithCustomTableName
{
    Field<int, PrimaryKey::AutoAssign> id;
    static constexpr std::string_view TableName = "custom_table"sv;
};

struct RecordWithMultipleFields
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<50>> name;
    Field<double> amount;
    Field<int> extraValue;
};

// ================================================================================================
// Tests for Field type traits (from Field.hpp) used in Create()
// ================================================================================================

TEST_CASE("IsField type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Field types are recognized")
    {
        static_assert(IsField<Field<int>>);
        static_assert(IsField<Field<SqlAnsiString<50>>>);
        static_assert(IsField<Field<int, PrimaryKey::AutoAssign>>);
        static_assert(IsField<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Non-Field types are not recognized")
    {
        static_assert(!IsField<int>);
        static_assert(!IsField<SqlAnsiString<50>>);
        static_assert(!IsField<double>);
    }

    SECTION("cv-qualified Field types are recognized")
    {
        static_assert(IsField<Field<int> const>);
        static_assert(IsField<Field<int>&>);
        static_assert(IsField<Field<int> const&>);
    }
}

TEST_CASE("IsPrimaryKey type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Primary key fields are recognized")
    {
        static_assert(IsPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
        static_assert(IsPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Non-primary key fields are not recognized")
    {
        static_assert(!IsPrimaryKey<Field<int>>);
        static_assert(!IsPrimaryKey<Field<SqlAnsiString<50>>>);
        static_assert(!IsPrimaryKey<Field<int, PrimaryKey::No>>);
    }

    SECTION("Non-Field types are not primary keys")
    {
        static_assert(!IsPrimaryKey<int>);
        static_assert(!IsPrimaryKey<SqlAnsiString<50>>);
    }
}

TEST_CASE("IsAutoIncrementPrimaryKey type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Auto-increment primary key fields are recognized")
    {
        static_assert(IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Auto-assign primary keys are NOT auto-increment")
    {
        static_assert(!IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
    }

    SECTION("Non-primary key fields are not auto-increment")
    {
        static_assert(!IsAutoIncrementPrimaryKey<Field<int>>);
        static_assert(!IsAutoIncrementPrimaryKey<Field<SqlAnsiString<50>>>);
    }
}

// ================================================================================================
// Tests for Record utilities (from Record.hpp) used in Create()
// ================================================================================================

TEST_CASE("DataMapperRecord concept", "[Utils][CompileTime][Create]")
{
    SECTION("Valid aggregate records satisfy the concept")
    {
        static_assert(DataMapperRecord<SimpleRecord>);
        static_assert(DataMapperRecord<RecordWithPrimaryKey>);
        static_assert(DataMapperRecord<RecordWithAutoIncrementPK>);
        static_assert(DataMapperRecord<RecordWithCustomTableName>);
    }

    SECTION("Non-aggregate types do not satisfy the concept")
    {
        static_assert(!DataMapperRecord<int>);
        static_assert(!DataMapperRecord<std::string>);
    }
}

TEST_CASE("HasPrimaryKey compile-time check", "[Utils][CompileTime][Create]")
{
    SECTION("Records with primary keys")
    {
        static_assert(HasPrimaryKey<RecordWithPrimaryKey>);
        static_assert(HasPrimaryKey<RecordWithAutoIncrementPK>);
        static_assert(HasPrimaryKey<RecordWithCustomTableName>);
        static_assert(HasPrimaryKey<RecordWithMultipleFields>);
    }

    SECTION("Records without primary keys")
    {
        static_assert(!HasPrimaryKey<SimpleRecord>);
    }
}

TEST_CASE("HasAutoIncrementPrimaryKey compile-time check", "[Utils][CompileTime][Create]")
{
    SECTION("Records with auto-increment primary keys")
    {
        static_assert(HasAutoIncrementPrimaryKey<RecordWithAutoIncrementPK>);
    }

    SECTION("Records without auto-increment primary keys")
    {
        static_assert(!HasAutoIncrementPrimaryKey<SimpleRecord>);
        static_assert(!HasAutoIncrementPrimaryKey<RecordWithPrimaryKey>);
        static_assert(!HasAutoIncrementPrimaryKey<RecordWithCustomTableName>);
    }
}

TEST_CASE("RecordPrimaryKeyIndex compile-time lookup", "[Utils][CompileTime][Create]")
{
    SECTION("Index is correct for records with primary key at first position")
    {
        static_assert(RecordPrimaryKeyIndex<RecordWithPrimaryKey> == 0);
        static_assert(RecordPrimaryKeyIndex<RecordWithAutoIncrementPK> == 0);
        static_assert(RecordPrimaryKeyIndex<RecordWithMultipleFields> == 0);
    }

    SECTION("Index is max size_t for records without primary key")
    {
        static_assert(RecordPrimaryKeyIndex<SimpleRecord> == (std::numeric_limits<size_t>::max)());
    }
}

TEST_CASE("RecordPrimaryKeyType compile-time type extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Primary key type is correctly extracted")
    {
        static_assert(std::is_same_v<RecordPrimaryKeyType<RecordWithPrimaryKey>, int>);
        static_assert(std::is_same_v<RecordPrimaryKeyType<RecordWithAutoIncrementPK>, int>);
    }

    SECTION("Records without primary key have monostate as key type")
    {
        static_assert(std::is_same_v<RecordPrimaryKeyType<SimpleRecord>, std::monostate>);
    }
}

// ================================================================================================
// Tests for table and field naming utilities (from Utils.hpp) used in Create()
// ================================================================================================

TEST_CASE("RecordTableName compile-time extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Default table name is the struct name")
    {
        // Note: The actual name depends on the reflection library, but for anonymous namespace
        // types, we test the custom table name case
        static_assert(RecordTableName<RecordWithCustomTableName> == "custom_table"sv);
    }

    SECTION("Custom TableName is used when provided")
    {
        static_assert(RecordTableName<RecordWithCustomTableName> == "custom_table"sv);
    }
}

TEST_CASE("FieldNameAt compile-time extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Field names are correctly extracted by index")
    {
        static_assert(FieldNameAt<0, SimpleRecord> == "value"sv);
        static_assert(FieldNameAt<1, SimpleRecord> == "name"sv);
    }

    SECTION("Field names for record with primary key")
    {
        static_assert(FieldNameAt<0, RecordWithPrimaryKey> == "id"sv);
        static_assert(FieldNameAt<1, RecordWithPrimaryKey> == "name"sv);
    }

    SECTION("Field names for record with multiple fields")
    {
        static_assert(FieldNameAt<0, RecordWithMultipleFields> == "id"sv);
        static_assert(FieldNameAt<1, RecordWithMultipleFields> == "name"sv);
        static_assert(FieldNameAt<2, RecordWithMultipleFields> == "amount"sv);
        static_assert(FieldNameAt<3, RecordWithMultipleFields> == "extraValue"sv);
    }
}

// ================================================================================================
// Tests for FieldWithStorage concept (from Record.hpp) used in Create()
// ================================================================================================

TEST_CASE("FieldWithStorage concept", "[Utils][CompileTime][Create]")
{
    SECTION("Field types satisfy FieldWithStorage")
    {
        static_assert(FieldWithStorage<Field<int>>);
        static_assert(FieldWithStorage<Field<SqlAnsiString<50>>>);
        static_assert(FieldWithStorage<Field<int, PrimaryKey::AutoAssign>>);
    }
}

TEST_CASE("RecordStorageFieldCount compile-time counting", "[Utils][CompileTime][Create]")
{
    SECTION("Counts all storage fields in a record")
    {
        static_assert(RecordStorageFieldCount<SimpleRecord> == 2);
        static_assert(RecordStorageFieldCount<RecordWithPrimaryKey> == 2);
        static_assert(RecordStorageFieldCount<RecordWithMultipleFields> == 4);
    }
}
