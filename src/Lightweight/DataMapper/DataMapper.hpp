// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlConnection.hpp"
#include "../SqlDataBinder.hpp"
#include "../SqlStatement.hpp"
#include "BelongsTo.hpp"
#include "CollectDifferences.hpp"
#include "Field.hpp"
#include "HasMany.hpp"
#include "HasManyThrough.hpp"
#include "HasOneThrough.hpp"
#include "Record.hpp"
#include "RecordId.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <concepts>
#include <type_traits>

/// @defgroup DataMapper Data Mapper
///
/// The data mapper is a high level API for mapping records to and from the database
/// using high level C++ syntax.

// Requires that T satisfies to be a field with storage.
template <typename T>
concept FieldWithStorage = requires(T const& field, T& mutableField) {
    { field.Value() } -> std::convertible_to<typename T::ValueType const&>;
    { mutableField.MutableValue() } -> std::convertible_to<typename T::ValueType&>;
    { field.IsModified() } -> std::convertible_to<bool>;
    { mutableField.SetModified(bool {}) } -> std::convertible_to<void>;
};

// Represents the number of fields with storage in a record.
template <typename Record>
constexpr size_t RecordStorageFieldCount =
    Reflection::FoldMembers<Record>(size_t { 0 }, []<size_t I, typename Field>(size_t const accum) constexpr {
        if constexpr (FieldWithStorage<Field>)
            return accum + 1;
        else
            return accum;
    });

template <typename Record>
concept RecordWithStorageFields = (RecordStorageFieldCount<Record> > 0);

namespace detail
{

template <template <typename> class Allocator, template <typename, typename> class Container, typename Object>
auto ToSharedPtrList(Container<Object, Allocator<Object>> container)
{
    using SharedPtrRecord = std::shared_ptr<Object>;
    auto sharedPtrContainer = Container<SharedPtrRecord, Allocator<SharedPtrRecord>> {};
    for (auto& object: container)
        sharedPtrContainer.emplace_back(std::make_shared<Object>(std::move(object)));
    return sharedPtrContainer;
}

} // namespace detail

/// @brief Main API for mapping records to and from the database using high level C++ syntax.
///
/// A DataMapper instances operates on a single SQL connection and provides methods to
/// create, read, update and delete records in the database.
///
/// @see Field, BelongsTo, HasMany, HasManyThrough, HasOneThrough
/// @ingroup DataMapper
class DataMapper
{
  public:
    /// Constructs a new data mapper, using the default connection.
    DataMapper():
        _connection {},
        _stmt { _connection }
    {
    }

    /// Constructs a new data mapper, using the given connection.
    explicit DataMapper(SqlConnection&& connection):
        _connection { std::move(connection) },
        _stmt { _connection }
    {
    }

    /// Constructs a new data mapper, using the given connection string.
    explicit DataMapper(SqlConnectionString connectionString):
        _connection { std::move(connectionString) },
        _stmt { _connection }
    {
    }

    DataMapper(DataMapper const&) = delete;
    DataMapper(DataMapper&&) noexcept = default;
    DataMapper& operator=(DataMapper const&) = delete;
    DataMapper& operator=(DataMapper&&) noexcept = default;
    ~DataMapper() = default;

    /// Returns the connection reference used by this data mapper.
    [[nodiscard]] SqlConnection const& Connection() const noexcept
    {
        return _connection;
    }

    /// Returns the mutable connection reference used by this data mapper.
    [[nodiscard]] SqlConnection& Connection() noexcept
    {
        return _connection;
    }

    /// Constructs a human readable string representation of the given record.
    template <typename Record>
    static std::string Inspect(Record const& record);

    /// Constructs a string list of SQL queries to create the table for the given record type.
    template <typename Record>
    std::vector<std::string> CreateTableString(SqlServerType serverType);

    /// Constructs a string list of SQL queries to create the tables for the given record types.
    template <typename FirstRecord, typename... MoreRecords>
    std::vector<std::string> CreateTablesString(SqlServerType serverType);

    /// Creates the table for the given record type.
    template <typename Record>
    void CreateTable();

    /// Creates the tables for the given record types.
    template <typename FirstRecord, typename... MoreRecords>
    void CreateTables();

    /// @brief Creates a new record in the database.
    ///
    /// The record is inserted into the database and the primary key is set on this record.
    ///
    /// @return The primary key of the newly created record.
    template <typename Record>
    RecordId Create(Record& record);

    /// @brief Creates a new record in the database.
    ///
    /// @note This is a variation of the Create() method and does not update the record's primary key.
    ///
    /// @return The primary key of the newly created record.
    template <typename Record>
    RecordId CreateExplicit(Record const& record);

    /// @brief Queries a single record from the database based on the given query.
    ///
    /// @param selectQuery The SQL select query to execute.
    /// @param args The input parameters for the query.
    ///
    /// @return The record if found, otherwise std::nullopt.
    template <typename Record, typename... Args>
    std::optional<Record> QuerySingle(SqlSelectQueryBuilder selectQuery, Args&&... args);

    /// @brief Queries a single record (based on primary key) from the database.
    ///
    /// The primary key(s) are used to identify the record to load.
    /// If the record is not found, std::nullopt is returned.
    template <typename Record, typename... PrimaryKeyTypes>
    std::optional<Record> QuerySingle(PrimaryKeyTypes&&... primaryKeys);

    /// @brief Queries a single record by the given column name and value.
    ///
    /// @param columnName The name of the column to search.
    /// @param value The value to search for.
    /// @return The record if found, otherwise std::nullopt.
    template <typename Record, typename ColumnName, typename T>
    std::optional<Record> QuerySingleBy(ColumnName const& columnName, T const& value);

    /// Queries multiple records from the database, based on the given query.
    template <typename Record, typename... InputParameters>
    std::vector<Record> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                              InputParameters&&... inputParameters);

    /// Queries multiple records from the database, based on the given query.
    template <typename Record, typename... InputParameters>
    std::vector<Record> Query(std::string_view sqlQueryString, InputParameters&&... inputParameters);

    /// Checks if the record has any modified fields.
    template <typename Record>
    bool IsModified(Record const& record) const noexcept;

    /// Updates the record in the database.
    template <typename Record>
    void Update(Record& record);

    /// Deletes the record from the database.
    template <typename Record>
    std::size_t Delete(Record const& record);

    /// Counts the total number of records in the database for the given record type.
    template <typename Record>
    std::size_t Count();

    /// Loads all records from the database for the given record type.
    template <typename Record>
    std::vector<Record> All();

    /// Constructs an SQL query builder for the given record type.
    template <typename Record>
    auto Query() -> SqlQueryBuilder
    {
        return _connection.Query(RecordTableName<Record>);
    }

    /// Constructs an SQL query builder for the given table name.
    SqlQueryBuilder FromTable(std::string_view tableName)
    {
        return _connection.Query(tableName);
    }

    /// Clears the modified state of the record.
    template <typename Record>
    void ClearModifiedState(Record& record) noexcept;

    /// Loads all direct relations to this record.
    template <typename Record>
    void LoadRelations(Record& record);

    /// Returns the first primary key field of the record.
    template <typename Record>
    decltype(auto) GetPrimaryKeyField(Record const& record) const;

    /// Configures the auto loading of relations for the given record.
    ///
    /// This means, that no explicit loading of relations is required.
    /// The relations are automatically loaded when accessed.
    template <typename Record>
    void ConfigureRelationAutoLoading(Record& record);

  private:
    template <typename Record, typename ValueType>
    void SetId(Record& record, ValueType&& id);

    template <typename Record>
    void BindOutputColumns(Record& record);

    template <typename Record>
    void BindOutputColumns(Record& record, SqlStatement* stmt);

    template <size_t FieldIndex, auto ReferencedRecordField, typename Record>
    void LoadBelongsTo(Record& record, BelongsTo<ReferencedRecordField>& field);

    template <size_t FieldIndex, typename Record, typename OtherRecord>
    void LoadHasMany(Record& record, HasMany<OtherRecord>& field);

    template <typename ReferencedRecord, typename ThroughRecord, typename Record>
    void LoadHasOneThrough(Record& record, HasOneThrough<ReferencedRecord, ThroughRecord>& field);

    template <typename ReferencedRecord, typename ThroughRecord, typename Record>
    void LoadHasManyThrough(Record& record, HasManyThrough<ReferencedRecord, ThroughRecord>& field);

    template <size_t FieldIndex, typename Record, typename OtherRecord, typename Callable>
    void CallOnHasMany(Record& record, Callable const& callback);

    template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename Callable>
    void CallOnHasManyThrough(Record& record, Callable const& callback);

    SqlConnection _connection;
    SqlStatement _stmt;
};

// ------------------------------------------------------------------------------------------------

template <typename Record>
std::string DataMapper::Inspect(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    std::string str;
    Reflection::CallOnMembers(record, [&str]<typename Name, typename Value>(Name const& name, Value const& value) {
        if (!str.empty())
            str += '\n';

        if constexpr (FieldWithStorage<Value>)
            str += std::format("{} {} := {}", Reflection::TypeName<Value>, name, value.Value());
        else if constexpr (!IsHasMany<Value> && !IsHasManyThrough<Value> && !IsHasOneThrough<Value>
                           && !IsBelongsTo<Value>)
            str += std::format("{} {} := {}", Reflection::TypeName<Value>, name, value);
    });
    return "{" + std::move(str) + "}";
}

namespace detail
{
template <std::size_t I, typename Record>
struct BelongsToNameImpl
{
    static constexpr auto baseName = Reflection::MemberNameOf<I, Record>;
    static constexpr auto storage = []() -> std::array<char, baseName.size() + 3>
    {
        std::array<char, baseName.size() + 3> storage;
        std::copy_n(baseName.begin(), baseName.size(), storage.begin());
        std::copy_n("_id", 3, storage.begin() + baseName.size());
        return storage;
    }
    ();
    static constexpr auto name = std::string_view(storage.data(), storage.size());
};

template <typename FieldType>
constexpr auto ColumnNameOverride = []() consteval {
    if constexpr (requires { FieldType::ColumnNameOverride; })
        return FieldType::ColumnNameOverride;
    else
        return std::string_view {};
}();

template <std::size_t I, typename Record>
consteval std::string_view FieldNameOf()
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    using FieldType = Reflection::MemberTypeOf<I, Record>;

    if constexpr (!std::string_view(ColumnNameOverride<FieldType>).empty())
    {
        return FieldType::ColumnNameOverride;
    }
    else if constexpr (IsBelongsTo<FieldType>())
    {
        return BelongsToNameImpl<I, Record>::name;
    }
    else
        return Reflection::MemberNameOf<I, Record>;
}
} // namespace detail

/// @brief Returns the SQL field name of the given field index in the record.
///
/// @ingroup DataMapper
template <std::size_t I, typename Record>
constexpr inline std::string_view FieldNameOf = detail::FieldNameOf<I, Record>();

template <typename Record>
std::vector<std::string> DataMapper::CreateTableString(SqlServerType serverType)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto migration = SqlQueryBuilder(*SqlQueryFormatter::Get(serverType)).Migration();
    auto createTable = migration.CreateTable(RecordTableName<Record>);

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
        {
            if constexpr (IsAutoIncrementPrimaryKey<FieldType>)
                createTable.PrimaryKeyWithAutoIncrement(std::string(FieldNameOf<I, Record>),
                                                        SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
            else if constexpr (FieldType::IsPrimaryKey)
                createTable.PrimaryKey(std::string(FieldNameOf<I, Record>),
                                       SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
            else if constexpr (IsBelongsTo<FieldType>)
            {
                constexpr size_t referencedFieldIndex = []() constexpr -> size_t {
                    auto index = size_t(-1);
                    Reflection::EnumerateMembers<typename FieldType::ReferencedRecord>(
                        [&index]<size_t J, typename ReferencedFieldType>() constexpr -> void {
                            if constexpr (IsField<ReferencedFieldType>)
                                if constexpr (ReferencedFieldType::IsPrimaryKey)
                                    index = J;
                        });
                    return index;
                }();
                createTable.ForeignKey(
                    std::string(FieldNameOf<I, Record>),
                    SqlColumnTypeDefinitionOf<typename FieldType::ValueType>,
                    SqlForeignKeyReferenceDefinition {
                        .tableName = std::string { RecordTableName<typename FieldType::ReferencedRecord> },
                        .columnName =
                            std::string { FieldNameOf<referencedFieldIndex, typename FieldType::ReferencedRecord> } });
            }
            else if constexpr (FieldType::IsMandatory)
                createTable.RequiredColumn(std::string(FieldNameOf<I, Record>),
                                           SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
            else
                createTable.Column(std::string(FieldNameOf<I, Record>),
                                   SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
        }
    });

    return migration.GetPlan().ToSql();
}

template <typename FirstRecord, typename... MoreRecords>
std::vector<std::string> DataMapper::CreateTablesString(SqlServerType serverType)
{
    std::vector<std::string> output;
    auto const append = [&output](auto const& sql) {
        output.insert(output.end(), sql.begin(), sql.end());
    };
    append(CreateTableString<FirstRecord>(serverType));
    (append(CreateTableString<MoreRecords>(serverType)), ...);
    return output;
}

template <typename Record>
void DataMapper::CreateTable()
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto const sqlQueryStrings = CreateTableString<Record>(_connection.ServerType());
    for (auto const& sqlQueryString: sqlQueryStrings)
        _stmt.ExecuteDirect(sqlQueryString);
}

template <typename FirstRecord, typename... MoreRecords>
void DataMapper::CreateTables()
{
    CreateTable<FirstRecord>();
    (CreateTable<MoreRecords>(), ...);
}

template <typename T>
constexpr bool HasAutoIncrementPrimaryKey =
    Reflection::FoldMembers<T>(false, []<size_t I, typename Field>(bool const accum) {
        if constexpr (FieldWithStorage<Field> && IsAutoIncrementPrimaryKey<Field>)
            return true;
        else
            return accum;
    });

template <typename Record>
RecordId DataMapper::CreateExplicit(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Insert(nullptr);

    Reflection::EnumerateMembers(record, [&query]<auto I, typename FieldType>(FieldType const& /*field*/) {
        if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
            query.Set(FieldNameOf<I, Record>, SqlWildcard);
    });

    _stmt.Prepare(query);

    Reflection::CallOnMembers(
        record,
        [this, i = SQLSMALLINT { 1 }]<typename Name, typename FieldType>(Name const& name,
                                                                         FieldType const& field) mutable {
            if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
                _stmt.BindInputParameter(i++, field, name);
        });

    _stmt.Execute();

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        return { _stmt.LastInsertId(RecordTableName<Record>) };
    else
        return {};
}

template <typename Record>
RecordId DataMapper::Create(Record& record)
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    // If the primary key is not an auto-increment field and the primary key is not set, we need to set it.
    CallOnPrimaryKey(record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType& primaryKeyField) {
        if constexpr (PrimaryKeyType::IsAutoAssignPrimaryKey)
        {
            if (!primaryKeyField.IsModified())
            {
                using ValueType = typename PrimaryKeyType::ValueType;
                if constexpr (std::same_as<ValueType, SqlGuid>)
                {
                    primaryKeyField = SqlGuid::Create();
                }
                else if constexpr (requires { ValueType {} + 1; })
                {
                    auto maxId = SqlStatement { _connection }.ExecuteDirectScalar<ValueType>(
                        std::format(R"sql(SELECT MAX("{}") FROM "{}")sql",
                                    FieldNameOf<PrimaryKeyIndex, Record>,
                                    RecordTableName<Record>));
                    primaryKeyField = maxId.value_or(ValueType {}) + 1;
                }
            }
        }
    });

    auto const id = CreateExplicit(record);

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        SetId(record, id.value);

    ClearModifiedState(record);
    ConfigureRelationAutoLoading(record);

    return id;
}

template <typename Record>
bool DataMapper::IsModified(Record const& record) const noexcept
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    bool modified = false;

    Reflection::CallOnMembers(record, [&modified](auto const& /*name*/, auto const& field) {
        if constexpr (requires { field.IsModified(); })
        {
            modified = modified || field.IsModified();
        }
    });

    return modified;
}

template <typename Record>
void DataMapper::Update(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Update();

    Reflection::CallOnMembers(record,
                              [&query]<typename Name, typename FieldType>(Name const& name, FieldType const& field) {
                                  if (field.IsModified())
                                      query.Set(name, SqlWildcard);
                                  if constexpr (FieldType::IsPrimaryKey)
                                      if (!field.IsModified())
                                          std::ignore = query.Where(name, SqlWildcard);
                              });

    _stmt.Prepare(query);

    // Bind the SET clause
    SQLSMALLINT i = 1;
    Reflection::CallOnMembers(
        record, [this, &i]<typename Name, typename FieldType>(Name const& name, FieldType const& field) mutable {
            if (field.IsModified())
                _stmt.BindInputParameter(i++, field.Value(), name);
        });

    // Bind the WHERE clause
    Reflection::CallOnMembers(
        record, [this, &i]<typename Name, typename FieldType>(Name const& name, FieldType const& field) mutable {
            if constexpr (FieldType::IsPrimaryKey)
                if (!field.IsModified())
                    _stmt.BindInputParameter(i++, field.Value(), name);
        });

    _stmt.Execute();

    ClearModifiedState(record);
}

template <typename Record>
std::size_t DataMapper::Delete(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Delete();

    Reflection::CallOnMembers(
        record, [&query]<typename Name, typename FieldType>(Name const& name, FieldType const& /*field*/) {
            if constexpr (FieldType::IsPrimaryKey)
                std::ignore = query.Where(name, SqlWildcard);
        });

    _stmt.Prepare(query);

    // Bind the WHERE clause
    Reflection::CallOnMembers(record,
                              [this, i = SQLSMALLINT { 1 }]<typename Name, typename FieldType>(
                                  Name const& name, FieldType const& field) mutable {
                                  if constexpr (FieldType::IsPrimaryKey)
                                      _stmt.BindInputParameter(i++, field.Value(), name);
                              });

    _stmt.Execute();

    return _stmt.NumRowsAffected();
}

template <typename Record>
size_t DataMapper::Count()
{
    _stmt.Prepare(_connection.Query(RecordTableName<Record>).Select().Count().ToSql());
    _stmt.Execute();

    auto result = size_t {};
    _stmt.BindOutputColumns(&result);
    std::ignore = _stmt.FetchRow();
    _stmt.CloseCursor();
    return result;
}

template <typename Record>
std::vector<Record> DataMapper::All()
{
    return Query<Record>(_connection.Query(RecordTableName<Record>).Select().template Fields<Record>().All());
}

template <typename Record, typename... PrimaryKeyTypes>
std::optional<Record> DataMapper::QuerySingle(PrimaryKeyTypes&&... primaryKeys)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto queryBuilder = _connection.Query(RecordTableName<Record>).Select();

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
        {
            queryBuilder.Field(FieldNameOf<I, Record>);

            if constexpr (FieldType::IsPrimaryKey)
                std::ignore = queryBuilder.Where(FieldNameOf<I, Record>, SqlWildcard);
        }
    });

    _stmt.Prepare(queryBuilder.First());
    _stmt.Execute(std::forward<PrimaryKeyTypes>(primaryKeys)...);

    auto resultRecord = Record {};
    BindOutputColumns(resultRecord);

    if (!_stmt.FetchRow())
        return std::nullopt;

    _stmt.CloseCursor();

    ConfigureRelationAutoLoading(resultRecord);

    return { std::move(resultRecord) };
}

template <typename Record, typename... Args>
std::optional<Record> DataMapper::QuerySingle(SqlSelectQueryBuilder selectQuery, Args&&... args)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
            selectQuery.Field(SqlQualifiedTableColumnName { RecordTableName<Record>, FieldNameOf<I, Record> });
    });
    _stmt.Prepare(selectQuery.First().ToSql());
    _stmt.Execute(std::forward<Args>(args)...);

    auto resultRecord = Record {};
    BindOutputColumns(resultRecord);

    if (!_stmt.FetchRow())
        return std::nullopt;

    _stmt.CloseCursor();

    ConfigureRelationAutoLoading(resultRecord);

    return { std::move(resultRecord) };
}

// TODO: Provide Query(QueryBuilder, ...) method variant

template <typename Record, typename... InputParameters>
inline LIGHTWEIGHT_FORCE_INLINE std::vector<Record> DataMapper::Query(
    SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    return Query<Record>(selectQuery.ToSql(), std::forward<InputParameters>(inputParameters)...);
}

template <typename Record, typename... InputParameters>
std::vector<Record> DataMapper::Query(std::string_view sqlQueryString, InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    _stmt.Prepare(sqlQueryString);
    _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

    auto result = std::vector<Record> {};

    auto record = Record {};
    BindOutputColumns(record);
    ConfigureRelationAutoLoading(record);

    while (_stmt.FetchRow())
    {
        result.emplace_back(std::move(record));
        record = Record {};
        BindOutputColumns(record);
        ConfigureRelationAutoLoading(record);
    }

    return result;
}

template <typename Record>
void DataMapper::ClearModifiedState(Record& record) noexcept
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, []<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (requires { field.SetModified(false); })
        {
            field.SetModified(false);
        }
    });
}

template <typename Record, typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE void CallOnPrimaryKey(Record& record, Callable const& callable)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                return callable.template operator()<I, FieldType>(field);
            }
        }
    });
}

template <typename Record, typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE void CallOnPrimaryKey(Callable const& callable)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                return callable.template operator()<I, FieldType>();
            }
        }
    });
}

template <typename Record, typename Callable>
inline LIGHTWEIGHT_FORCE_INLINE void CallOnBelongsTo(Callable const& callable)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (IsBelongsTo<FieldType>)
        {
            return callable.template operator()<I, FieldType>();
        }
    });
}

template <size_t FieldIndex, auto ReferencedRecordField, typename Record>
void DataMapper::LoadBelongsTo(Record& record, BelongsTo<ReferencedRecordField>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    using FieldType = BelongsTo<ReferencedRecordField>;
    using ReferencedRecord = typename FieldType::ReferencedRecord;

    CallOnPrimaryKey(record,
                     [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
                         if (auto result = QuerySingle<ReferencedRecord>(primaryKeyField.Value()); result)
                         {
                             field.EmplaceRecord() = std::move(*result);
                         }
                     });
}

template <size_t FieldIndex, typename Record, typename OtherRecord, typename Callable>
void DataMapper::CallOnHasMany(Record& record, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    using FieldType = HasMany<OtherRecord>;
    using ReferencedRecord = typename FieldType::ReferencedRecord;

    CallOnPrimaryKey(
        record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
            auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                             .Select()
                             .Build([&](auto& query) {
                                 Reflection::EnumerateMembers<ReferencedRecord>(
                                     [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                         if constexpr (FieldWithStorage<ReferencedFieldType>)
                                         {
                                             query.Field(FieldNameOf<ReferencedFieldIndex, ReferencedRecord>);
                                         }
                                     });
                             })
                             .Where(FieldNameOf<FieldIndex, ReferencedRecord>, SqlWildcard);
            callback(query, primaryKeyField);
        });
}

template <size_t FieldIndex, typename Record, typename OtherRecord>
void DataMapper::LoadHasMany(Record& record, HasMany<OtherRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    CallOnHasMany<FieldIndex, Record, OtherRecord>(
        record, [&](SqlSelectQueryBuilder selectQuery, auto& primaryKeyField) {
            field.Emplace(detail::ToSharedPtrList(Query<OtherRecord>(selectQuery.All(), primaryKeyField.Value())));
        });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record>
void DataMapper::LoadHasOneThrough(Record& record, HasOneThrough<ReferencedRecord, ThroughRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<ThroughRecord>, "ThroughRecord must satisfy DataMapperRecord");

    // Find the PK of Record
    CallOnPrimaryKey(
        record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
            // Find the BelongsTo of ThroughRecord pointing to the PK of Record
            CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToIndex, typename ThroughBelongsToType>() {
                // Find the PK of ThroughRecord
                CallOnPrimaryKey<ThroughRecord>([&]<size_t ThroughPrimaryKeyIndex, typename ThroughPrimaryKeyType>() {
                    // Find the BelongsTo of ReferencedRecord pointing to the PK of ThroughRecord
                    CallOnBelongsTo<ReferencedRecord>([&]<size_t ReferencedKeyIndex, typename ReferencedKeyType>() {
                        // Query the ReferencedRecord where:
                        // - the BelongsTo of ReferencedRecord points to the PK of ThroughRecord,
                        // - and the BelongsTo of ThroughRecord points to the PK of Record
                        auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                                         .Select()
                                         .Build([&](auto& query) {
                                             Reflection::EnumerateMembers<ReferencedRecord>(
                                                 [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                                     if constexpr (FieldWithStorage<ReferencedFieldType>)
                                                     {
                                                         query.Field(SqlQualifiedTableColumnName {
                                                             RecordTableName<ReferencedRecord>,
                                                             FieldNameOf<ReferencedFieldIndex, ReferencedRecord> });
                                                     }
                                                 });
                                         })
                                         .InnerJoin(RecordTableName<ThroughRecord>,
                                                    FieldNameOf<ThroughPrimaryKeyIndex, ThroughRecord>,
                                                    FieldNameOf<ReferencedKeyIndex, ReferencedRecord>)
                                         .InnerJoin(RecordTableName<Record>,
                                                    FieldNameOf<PrimaryKeyIndex, Record>,
                                                    SqlQualifiedTableColumnName {
                                                        RecordTableName<ThroughRecord>,
                                                        FieldNameOf<ThroughBelongsToIndex, ThroughRecord> })
                                         .Where(
                                             SqlQualifiedTableColumnName {
                                                 RecordTableName<Record>,
                                                 FieldNameOf<PrimaryKeyIndex, ThroughRecord>,
                                             },
                                             SqlWildcard);
                        if (auto link = QuerySingle<ReferencedRecord>(std::move(query), primaryKeyField.Value()); link)
                        {
                            field.EmplaceRecord(std::make_shared<ReferencedRecord>(std::move(*link)));
                        }
                    });
                });
            });
        });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename Callable>
void DataMapper::CallOnHasManyThrough(Record& record, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    // Find the PK of Record
    CallOnPrimaryKey(
        record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
            // Find the BelongsTo of ThroughRecord pointing to the PK of Record
            CallOnBelongsTo<ThroughRecord>(
                [&]<size_t ThroughBelongsToRecordIndex, typename ThroughBelongsToRecordType>() {
                    using ThroughBelongsToRecordFieldType =
                        Reflection::MemberTypeOf<ThroughBelongsToRecordIndex, ThroughRecord>;
                    if constexpr (std::is_same_v<typename ThroughBelongsToRecordFieldType::ReferencedRecord, Record>)
                    {
                        // Find the BelongsTo of ThroughRecord pointing to the PK of ReferencedRecord
                        CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToReferenceRecordIndex,
                                                           typename ThroughBelongsToReferenceRecordType>() {
                            using ThroughBelongsToReferenceRecordFieldType =
                                Reflection::MemberTypeOf<ThroughBelongsToReferenceRecordIndex, ThroughRecord>;
                            if constexpr (std::is_same_v<
                                              typename ThroughBelongsToReferenceRecordFieldType::ReferencedRecord,
                                              ReferencedRecord>)
                            {
                                auto query =
                                    _connection.Query(RecordTableName<ReferencedRecord>)
                                        .Select()
                                        .Build([&](auto& query) {
                                            Reflection::EnumerateMembers<ReferencedRecord>(
                                                [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                                    if constexpr (FieldWithStorage<ReferencedFieldType>)
                                                    {
                                                        query.Field(SqlQualifiedTableColumnName {
                                                            RecordTableName<ReferencedRecord>,
                                                            FieldNameOf<ReferencedFieldIndex, ReferencedRecord> });
                                                    }
                                                });
                                        })
                                        .InnerJoin(RecordTableName<ThroughRecord>,
                                                   FieldNameOf<ThroughBelongsToReferenceRecordIndex, ThroughRecord>,
                                                   SqlQualifiedTableColumnName { RecordTableName<ReferencedRecord>,
                                                                                 FieldNameOf<PrimaryKeyIndex, Record> })
                                        .Where(
                                            SqlQualifiedTableColumnName {
                                                RecordTableName<ThroughRecord>,
                                                FieldNameOf<ThroughBelongsToRecordIndex, ThroughRecord>,
                                            },
                                            SqlWildcard);
                                callback(query, primaryKeyField);
                            }
                        });
                    }
                });
        });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record>
void DataMapper::LoadHasManyThrough(Record& record, HasManyThrough<ReferencedRecord, ThroughRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    CallOnHasManyThrough<ReferencedRecord, ThroughRecord>(
        record, [&](SqlSelectQueryBuilder& selectQuery, auto& primaryKeyField) {
            field.Emplace(detail::ToSharedPtrList(Query<ReferencedRecord>(selectQuery.All(), primaryKeyField.Value())));
        });
}

template <typename Record>
void DataMapper::LoadRelations(Record& record)
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            LoadBelongsTo<FieldIndex>(record, field);
        }
        else if constexpr (IsHasMany<FieldType>)
        {
            LoadHasMany<FieldIndex>(record, field);
        }
        else if constexpr (IsHasOneThrough<FieldType>)
        {
            LoadHasOneThrough(record, field);
        }
        else if constexpr (IsHasManyThrough<FieldType>)
        {
            LoadHasManyThrough(record, field);
        }
    });
}

template <typename Record>
inline LIGHTWEIGHT_FORCE_INLINE decltype(auto) DataMapper::GetPrimaryKeyField(Record const& record) const
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                return field;
            }
        }
    });
}

template <typename Record, typename ValueType>
inline LIGHTWEIGHT_FORCE_INLINE void DataMapper::SetId(Record& record, ValueType&& id)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    // static_assert(HasPrimaryKey<Record>);

    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                field = std::forward<FieldType>(id);
            }
        }
    });
}

template <typename Record>
inline LIGHTWEIGHT_FORCE_INLINE void DataMapper::BindOutputColumns(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    BindOutputColumns(record, &_stmt);
}

template <typename Record>
void DataMapper::BindOutputColumns(Record& record, SqlStatement* stmt)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(!std::is_const_v<Record>);
    assert(stmt != nullptr);

    Reflection::EnumerateMembers(record, [stmt, i = SQLSMALLINT { 1 }]<size_t I, typename Field>(Field& field) mutable {
        if constexpr (IsField<Field>)
        {
            stmt->BindOutputColumn(i++, &field.MutableValue());
        }
        else if constexpr (SqlOutputColumnBinder<Field>)
        {
            stmt->BindOutputColumn(i++, &field);
        }
    });
}

template <typename Record>
void DataMapper::ConfigureRelationAutoLoading(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            BelongsTo<FieldType::ReferencedField>& belongsTo = field;
            belongsTo.SetAutoLoader(typename FieldType::Loader {
                .loadReference = [this, &record, &belongsTo]() { LoadBelongsTo<FieldIndex>(record, belongsTo); },
            });
        }
        else if constexpr (IsHasMany<FieldType>)
        {
            using ReferencedRecord = typename FieldType::ReferencedRecord;
            HasMany<ReferencedRecord>& hasMany = field;
            hasMany.SetAutoLoader(typename FieldType::Loader {
                .count = [this, &record]() -> size_t {
                    size_t count = 0;
                    CallOnHasMany<FieldIndex, Record, ReferencedRecord>(
                        record, [&](SqlSelectQueryBuilder selectQuery, auto const& primaryKeyField) {
                            _stmt.Prepare(selectQuery.Count());
                            _stmt.Execute(primaryKeyField.Value());
                            if (_stmt.FetchRow())
                                count = _stmt.GetColumn<size_t>(1);
                            _stmt.CloseCursor();
                        });
                    return count;
                },
                .all = [this, &record, &hasMany]() { LoadHasMany<FieldIndex>(record, hasMany); },
                .each =
                    [this, &record](auto const& each) {
                        CallOnHasMany<FieldIndex, Record, ReferencedRecord>(
                            record, [&](SqlSelectQueryBuilder selectQuery, auto const& primaryKeyField) {
                                auto stmt = SqlStatement { _connection };
                                stmt.Prepare(selectQuery.All());
                                stmt.Execute(primaryKeyField.Value());

                                auto referencedRecord = ReferencedRecord {};
                                BindOutputColumns(referencedRecord, &stmt);
                                ConfigureRelationAutoLoading(referencedRecord);

                                while (stmt.FetchRow())
                                {
                                    each(referencedRecord);
                                    BindOutputColumns(referencedRecord, &stmt);
                                }
                            });
                    },
            });
        }
        else if constexpr (IsHasOneThrough<FieldType>)
        {
            using ReferencedRecord = typename FieldType::ReferencedRecord;
            using ThroughRecord = typename FieldType::ThroughRecord;
            HasOneThrough<ReferencedRecord, ThroughRecord>& hasOneThrough = field;
            hasOneThrough.SetAutoLoader(typename FieldType::Loader {
                .loadReference =
                    [this, &record, &hasOneThrough]() {
                        LoadHasOneThrough<ReferencedRecord, ThroughRecord>(record, hasOneThrough);
                    },
            });
        }
        else if constexpr (IsHasManyThrough<FieldType>)
        {
            using ReferencedRecord = typename FieldType::ReferencedRecord;
            using ThroughRecord = typename FieldType::ThroughRecord;
            HasManyThrough<ReferencedRecord, ThroughRecord>& hasManyThrough = field;
            hasManyThrough.SetAutoLoader(typename FieldType::Loader {
                .count = [this, &record]() -> size_t {
                    // Load result for Count()
                    size_t count = 0;
                    CallOnHasManyThrough<ReferencedRecord, ThroughRecord>(
                        record, [&](SqlSelectQueryBuilder& selectQuery, auto& primaryKeyField) {
                            _stmt.Prepare(selectQuery.Count());
                            _stmt.Execute(primaryKeyField.Value());
                            if (_stmt.FetchRow())
                                count = _stmt.GetColumn<size_t>(1);
                            _stmt.CloseCursor();
                        });
                    return count;
                },
                .all =
                    [this, &record, &hasManyThrough]() {
                        // Load result for All()
                        LoadHasManyThrough(record, hasManyThrough);
                    },
                .each =
                    [this, &record](auto const& each) {
                        // Load result for Each()
                        CallOnHasManyThrough<ReferencedRecord, ThroughRecord>(
                            record, [&](SqlSelectQueryBuilder& selectQuery, auto& primaryKeyField) {
                                auto stmt = SqlStatement { _connection };
                                stmt.Prepare(selectQuery.All());
                                stmt.Execute(primaryKeyField.Value());

                                auto referencedRecord = ReferencedRecord {};
                                BindOutputColumns(referencedRecord, &stmt);
                                ConfigureRelationAutoLoading(referencedRecord);

                                while (stmt.FetchRow())
                                {
                                    each(referencedRecord);
                                    BindOutputColumns(referencedRecord, &stmt);
                                }
                            });
                    },
            });
        }
    });
}
