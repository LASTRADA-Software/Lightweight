// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../SqlConnection.hpp"
#include "../SqlDataBinder.hpp"
#include "../SqlLogger.hpp"
#include "../SqlRealName.hpp"
#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "BelongsTo.hpp"
#include "CollectDifferences.hpp"
#include "Field.hpp"
#include "HasMany.hpp"
#include "HasManyThrough.hpp"
#include "HasOneThrough.hpp"
#include "QueryBuilders.hpp"
#include "Record.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Lightweight
{

/// @defgroup DataMapper Data Mapper
///
/// @brief The data mapper is a high level API for mapping records to and from the database using high level C++ syntax.

namespace detail
{
    // Converts a container of T to a container of std::shared_ptr<T>.
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
///
/// @code
/// struct Person
/// {
///    Field<SqlGuid, PrimaryKey::AutoAssign> id;
///    Field<SqlAnsiString<30>> name;
///    Field<SqlAnsiString<40>> email;
/// };
///
/// auto dm = DataMapper {};
///
/// // Create a new person record
/// auto person = Person { .id = SqlGuid::Create(), .name = "John Doe", .email = "johnt@doe.com" };
///
/// // Create the record in the database and set the primary key on the record
/// auto const personId = dm.Create(person);
///
/// // Query the person record from the database
/// auto const queriedPerson = dm.Query<Person>(personId)
///                              .Where(FieldNameOf<&Person::id>, "=", personId)
///                              .First();
///
/// if (queriedPerson.has_value())
///     std::println("Queried Person: {}", DataMapper::Inspect(queriedPerson.value()));
///
/// // Update the person record in the database
/// person.email = "alt@doe.com";
/// dm.Update(person);
///
/// // Delete the person record from the database
/// dm.Delete(person);
/// @endcode
class DataMapper
{
  public:
    /// Acquires a thread-local DataMapper instance that is safe for reuse within that thread.
    LIGHTWEIGHT_API static DataMapper& AcquireThreadLocal();

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
    explicit DataMapper(std::optional<SqlConnectionString> connectionString):
        _connection { std::move(connectionString) },
        _stmt { _connection }
    {
    }

    DataMapper(DataMapper const&) = delete;
    DataMapper& operator=(DataMapper const&) = delete;

    DataMapper(DataMapper&& other) noexcept:
        _connection(std::move(other._connection)),
        _stmt(_connection)
    {
        other._stmt = SqlStatement(std::nullopt);
    }

    DataMapper& operator=(DataMapper&& other) noexcept
    {
        if (this != &other)
        {
            _connection = std::move(other._connection);
            _stmt = SqlStatement(_connection);
            other._stmt = SqlStatement(std::nullopt);
        }

        return *this;
    }

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

#if defined(BUILD_TESTS)

    [[nodiscard]] SqlStatement& Statement(this auto&& self) noexcept
    {
        return self._stmt;
    }

#endif

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
    template <DataMapperOptions QueryOptions = {}, typename Record>
    RecordPrimaryKeyType<Record> Create(Record& record);

    /// @brief Creates a new record in the database.
    ///
    /// @note This is a variation of the Create() method and does not update the record's primary key.
    ///
    /// @return The primary key of the newly created record.
    template <typename Record>
    RecordPrimaryKeyType<Record> CreateExplicit(Record const& record);

    /// @brief Creates a copy of an existing record in the database.
    ///
    /// This method is useful for duplicating a database record while assigning a new primary key.
    /// All fields except primary key(s) are copied from the original record.
    /// The primary key is automatically generated (auto-incremented or auto-assigned).
    ///
    /// @param originalRecord The record to copy.
    /// @return The primary key of the newly created record.
    template <DataMapperOptions QueryOptions = {}, typename Record>
    [[nodiscard]] RecordPrimaryKeyType<Record> CreateCopyOf(Record const& originalRecord);

    /// @brief Queries a single record (based on primary key) from the database.
    ///
    /// The primary key(s) are used to identify the record to load.
    /// If the record is not found, std::nullopt is returned.
    template <typename Record, typename... PrimaryKeyTypes>
    std::optional<Record> QuerySingle(PrimaryKeyTypes&&... primaryKeys);

    /// @brief Queries a single record (based on primary key) from the database without auto-loading relations.
    ///
    /// The primary key(s) are used to identify the record to load.
    ///
    /// Main goal of this function is to load record without relationships to
    /// decrease compilation time and work around some limitations of template instantiation
    /// depth on MSVC compiler.
    template <typename Record, typename... PrimaryKeyTypes>
    std::optional<Record> QuerySingleWithoutRelationAutoLoading(PrimaryKeyTypes&&... primaryKeys);

    /// Queries multiple records from the database, based on the given query.
    template <typename Record, typename... InputParameters>
    std::vector<Record> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters);

    /// Queries multiple records from the database, based on the given query.
    ///
    /// @param sqlQueryString The SQL query string to execute.
    /// @param inputParameters The input parameters for the query to be bound before executing.
    /// @return A vector of records of the given type that were found via the query.
    ///
    /// example:
    /// @code
    /// struct Person
    /// {
    ///     int id;
    ///     std::string name;
    ///     std::string email;
    ///     std::string phone;
    ///     std::string address;
    ///     std::string city;
    ///     std::string country;
    /// };
    ///
    /// void example(DataMapper& dm)
    /// {
    ///     auto const sqlQueryString = R"(SELECT * FROM "Person" WHERE "city" = ? AND "country" = ?)";
    ///     auto const records = dm.Query<Person>(sqlQueryString, "Berlin", "Germany");
    ///     for (auto const& record: records)
    ///     {
    ///         std::println("Person: {}", DataMapper::Inspect(record));
    ///     }
    /// }
    /// @endcode
    template <typename Record, typename... InputParameters>
    std::vector<Record> Query(std::string_view sqlQueryString, InputParameters&&... inputParameters);

    /// Queries records from the database, based on the given query and can be used to retrieve only part of the record
    /// by specifying the ElementMask.
    ///
    /// example:
    /// @code
    ///
    /// struct Person
    /// {
    ///    int id;
    ///    std::string name;
    ///    std::string email;
    ///    std::string phone;
    ///    std::string address;
    ///    std::string city;
    ///    std::string country;
    /// };
    ///
    /// auto infos = dm.Query<SqlElements<1,5>(RecordTableName<Person>.Fields({"name"sv, "city"sv}));
    ///
    /// for(auto const& info : infos)
    /// {
    ///    // only info.name and info.city are loaded
    /// }
    /// @endcode
    template <typename ElementMask, typename Record, typename... InputParameters>
    std::vector<Record> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters);

    /// Queries records of different types from the database, based on the given query.
    /// User can constructed query that selects columns from the multiple tables
    /// this function is used to get result of the query
    ///
    /// example:
    /// @code
    ///
    /// struct JointA{};
    /// struct JointB{};
    /// struct JointC{};
    ///
    /// // the following query will construct statement to fetch all elements of JointA and JointC types
    /// auto dm = DataMapper {};
    /// auto const query = dm.FromTable(RecordTableName<JoinTestA>)
    ///                      .Select()
    ///                      .Fields<JointA, JointC>()
    ///                      .InnerJoin<&JointB::a_id, &JointA::id>()
    ///                      .InnerJoin<&JointC::id, &JointB::c_id>()
    ///                      .All();
    /// auto const records = dm.Query<JointA, JointC>(query);
    /// for(const auto [elementA, elementC] : records)
    /// {
    ///   // do something with elementA and elementC
    /// }
    template <typename First, typename Second, typename... Rest, DataMapperOptions QueryOptions = {}>
        requires DataMapperRecord<First> && DataMapperRecord<Second> && DataMapperRecords<Rest...>
    std::vector<std::tuple<First, Second, Rest...>> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery);

    /// Queries records of different types from the database, based on the given query.
    template <typename FirstRecord, typename NextRecord, DataMapperOptions QueryOptions = {}>
        requires DataMapperRecord<FirstRecord> && DataMapperRecord<NextRecord>
    SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, NextRecord>, QueryOptions> Query()
    {
        std::string fields;

        auto const emplaceRecordsFrom = [&fields]<typename Record>() {
            Reflection::EnumerateMembers<Record>([&fields]<size_t I, typename Field>() {
                if (!fields.empty())
                    fields += ", ";
                fields += std::format(R"("{}"."{}")", RecordTableName<Record>, FieldNameAt<I, Record>);
            });
        };

        emplaceRecordsFrom.template operator()<FirstRecord>();
        emplaceRecordsFrom.template operator()<NextRecord>();

        return SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, NextRecord>, QueryOptions>(*this, std::move(fields));
    }

    /// Queries records of given Record type.
    ///
    /// The query builder can be used to further refine the query.
    /// The query builder will execute the query when a method like All(), First(n), etc. is called.
    ///
    /// @returns A query builder for the given Record type.
    ///
    /// @code
    /// auto const records = dm.Query<Person>()
    ///                        .Where(FieldNameOf<&Person::is_active>, "=", true)
    ///                        .All();
    /// @endcode
    template <typename Record, DataMapperOptions QueryOptions = {}>
    SqlAllFieldsQueryBuilder<Record, QueryOptions> Query()
    {
        std::string fields;
        Reflection::EnumerateMembers<Record>([&fields]<size_t I, typename Field>() {
            if (!fields.empty())
                fields += ", ";
            fields += '"';
            fields += RecordTableName<Record>;
            fields += "\".\"";
            fields += FieldNameAt<I, Record>;
            fields += '"';
        });
        return SqlAllFieldsQueryBuilder<Record, QueryOptions>(*this, std::move(fields));
    }

    /// Returns a SqlQueryBuilder using the default query formatter.
    /// This can be used to build custom queries separately from the DataMapper.
    /// and execute them via the DataMapper's Query() methods that SqlSelectQueryBuilder
    ///
    SqlQueryBuilder Query()
    {
        return SqlQueryBuilder(_connection.QueryFormatter());
    }

    /// Updates the record in the database.
    template <typename Record>
    void Update(Record& record);

    /// Deletes the record from the database.
    template <typename Record>
    std::size_t Delete(Record const& record);

    /// Constructs an SQL query builder for the given table name.
    SqlQueryBuilder FromTable(std::string_view tableName)
    {
        return _connection.Query(tableName);
    }

    /// Checks if the record has any modified fields.
    template <typename Record>
    bool IsModified(Record const& record) const noexcept;

    /// Enum to set the modified state of a record.
    enum class ModifiedState : uint8_t
    {
        Modified,
        NotModified
    };

    /// Sets the modified state of the record after receiving from the database.
    /// This marks all fields as not modified.
    template <ModifiedState state, typename Record>
    void SetModifiedState(Record& record) noexcept;

    /// Loads all direct relations to this record.
    template <typename Record>
    void LoadRelations(Record& record);

    /// Configures the auto loading of relations for the given record.
    ///
    /// This means, that no explicit loading of relations is required.
    /// The relations are automatically loaded when accessed.
    template <typename Record>
    void ConfigureRelationAutoLoading(Record& record);

    /// Helper function that allow to execute query directly via data mapper
    /// and get scalar result without need to create SqlStatement manually
    ///
    /// @param sqlQueryString The SQL query string to execute.
    template <typename T>
    [[nodiscard]] std::optional<T> Execute(std::string_view sqlQueryString);

  private:
    /// @brief Queries a single record from the database based on the given query.
    ///
    /// @param selectQuery The SQL select query to execute.
    /// @param args The input parameters for the query.
    ///
    /// @return The record if found, otherwise std::nullopt.
    template <typename Record, typename... Args>
    std::optional<Record> QuerySingle(SqlSelectQueryBuilder selectQuery, Args&&... args);

    template <typename Record, typename ValueType>
    void SetId(Record& record, ValueType&& id);

    template <typename Record, size_t InitialOffset = 1>
    Record& BindOutputColumns(Record& record);

    template <typename Record, size_t InitialOffset = 1>
    Record& BindOutputColumns(Record& record, SqlStatement* stmt);

    template <typename ElementMask, typename Record, size_t InitialOffset = 1>
    Record& BindOutputColumns(Record& record);

    template <typename ElementMask, typename Record, size_t InitialOffset = 1>
    Record& BindOutputColumns(Record& record, SqlStatement* stmt);

    template <typename FieldType>
    std::optional<typename FieldType::ReferencedRecord> LoadBelongsTo(FieldType::ValueType value);

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

    enum class PrimaryKeySource : std::uint8_t
    {
        Record,
        Override,
    };

    template <typename Record>
    std::optional<RecordPrimaryKeyType<Record>> GenerateAutoAssignPrimaryKey(Record const& record);

    template <PrimaryKeySource UsePkOverride, typename Record>
    RecordPrimaryKeyType<Record> CreateInternal(
        Record const& record,
        std::optional<std::conditional_t<std::is_void_v<RecordPrimaryKeyType<Record>>, int, RecordPrimaryKeyType<Record>>>
            pkOverride = std::nullopt);

    SqlConnection _connection;
    SqlStatement _stmt;
};

// ------------------------------------------------------------------------------------------------

namespace detail
{
    template <typename FieldType>
    constexpr bool CanSafelyBindOutputColumn(SqlServerType sqlServerType) noexcept
    {
        if (sqlServerType != SqlServerType::MICROSOFT_SQL)
            return true;

        // Test if we have some columns that might not be sufficient to store the result (e.g. string truncation),
        // then don't call BindOutputColumn but SQLFetch to get the result, because
        // regrowing previously bound columns is not supported in MS-SQL's ODBC driver, so it seems.
        bool result = true;
        if constexpr (IsField<FieldType>)
        {
            if constexpr (detail::OneOf<typename FieldType::ValueType,
                                        std::string,
                                        std::wstring,
                                        std::u16string,
                                        std::u32string,
                                        SqlBinary>
                          || IsSqlDynamicString<typename FieldType::ValueType>
                          || IsSqlDynamicBinary<typename FieldType::ValueType>)
            {
                // Known types that MAY require growing due to truncation.
                result = false;
            }
        }
        return result;
    }

    template <DataMapperRecord Record>
    constexpr bool CanSafelyBindOutputColumns(SqlServerType sqlServerType) noexcept
    {
        if (sqlServerType != SqlServerType::MICROSOFT_SQL)
            return true;

        bool result = true;
        Reflection::EnumerateMembers<Record>([&result]<size_t I, typename Field>() {
            if constexpr (IsField<Field>)
            {
                if constexpr (detail::OneOf<typename Field::ValueType,
                                            std::string,
                                            std::wstring,
                                            std::u16string,
                                            std::u32string,
                                            SqlBinary>
                              || IsSqlDynamicString<typename Field::ValueType>
                              || IsSqlDynamicBinary<typename Field::ValueType>)
                {
                    // Known types that MAY require growing due to truncation.
                    result = false;
                }
            }
        });
        return result;
    }

    template <typename Record>
    void BindAllOutputColumnsWithOffset(SqlResultCursor& reader, Record& record, SQLUSMALLINT startOffset)
    {
        Reflection::EnumerateMembers(record,
                                     [reader = &reader, i = startOffset]<size_t I, typename Field>(Field& field) mutable {
                                         if constexpr (IsField<Field>)
                                         {
                                             reader->BindOutputColumn(i++, &field.MutableValue());
                                         }
                                         else if constexpr (IsBelongsTo<Field>)
                                         {
                                             reader->BindOutputColumn(i++, &field.MutableValue());
                                         }
                                         else if constexpr (SqlOutputColumnBinder<Field>)
                                         {
                                             reader->BindOutputColumn(i++, &field);
                                         }
                                     });
    }

    template <typename Record>
    void BindAllOutputColumns(SqlResultCursor& reader, Record& record)
    {
        BindAllOutputColumnsWithOffset(reader, record, 1);
    }

    // when we iterate over all columns using element mask
    // indexes of the mask corresponds to the indexe of the field
    // inside the structure, not inside the SQL result set
    template <typename ElementMask, typename Record>
    void GetAllColumns(SqlResultCursor& reader, Record& record, SQLUSMALLINT indexFromQuery = 0)
    {
        Reflection::EnumerateMembers<ElementMask>(
            record, [reader = &reader, &indexFromQuery]<size_t I, typename Field>(Field& field) mutable {
                ++indexFromQuery;
                if constexpr (IsField<Field>)
                {
                    if constexpr (Field::IsOptional)
                        field.MutableValue() =
                            reader->GetNullableColumn<typename Field::ValueType::value_type>(indexFromQuery);
                    else
                        field.MutableValue() = reader->GetColumn<typename Field::ValueType>(indexFromQuery);
                }
                else if constexpr (SqlGetColumnNativeType<Field>)
                {
                    if constexpr (IsOptionalBelongsTo<Field>)
                        field = reader->GetNullableColumn<typename Field::BaseType>(indexFromQuery);
                    else
                        field = reader->GetColumn<Field>(indexFromQuery);
                }
            });
    }

    template <typename Record>
    void GetAllColumns(SqlResultCursor& reader, Record& record, SQLUSMALLINT indexFromQuery = 0)
    {
        return GetAllColumns<std::make_integer_sequence<size_t, Reflection::CountMembers<Record>>, Record>(
            reader, record, indexFromQuery);
    }

    template <typename FirstRecord, typename SecondRecord>
    // TODO we need to remove this at some points and provide generic bindings for tuples
    void GetAllColumns(SqlResultCursor& reader, std::tuple<FirstRecord, SecondRecord>& record)
    {
        auto& [firstRecord, secondRecord] = record;

        Reflection::EnumerateMembers(firstRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                if constexpr (Field::IsOptional)
                    field.MutableValue() = reader->GetNullableColumn<typename Field::ValueType::value_type>(I + 1);
                else
                    field.MutableValue() = reader->GetColumn<typename Field::ValueType>(I + 1);
            }
            else if constexpr (SqlGetColumnNativeType<Field>)
            {
                if constexpr (Field::IsOptional)
                    field = reader->GetNullableColumn<typename Field::BaseType>(I + 1);
                else
                    field = reader->GetColumn<Field>(I + 1);
            }
        });

        Reflection::EnumerateMembers(secondRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                if constexpr (Field::IsOptional)
                    field.MutableValue() = reader->GetNullableColumn<typename Field::ValueType::value_type>(
                        Reflection::CountMembers<FirstRecord> + I + 1);
                else
                    field.MutableValue() =
                        reader->GetColumn<typename Field::ValueType>(Reflection::CountMembers<FirstRecord> + I + 1);
            }
            else if constexpr (SqlGetColumnNativeType<Field>)
            {
                if constexpr (Field::IsOptional)
                    field =
                        reader->GetNullableColumn<typename Field::BaseType>(Reflection::CountMembers<FirstRecord> + I + 1);
                else
                    field = reader->GetColumn<Field>(Reflection::CountMembers<FirstRecord> + I + 1);
            }
        });
    }

    template <typename Record>
    bool ReadSingleResult(SqlServerType sqlServerType, SqlResultCursor& reader, Record& record)
    {
        auto const outputColumnsBound = CanSafelyBindOutputColumns<Record>(sqlServerType);

        if (outputColumnsBound)
            BindAllOutputColumns(reader, record);

        if (!reader.FetchRow())
            return false;

        if (!outputColumnsBound)
            GetAllColumns(reader, record);

        return true;
    }
} // namespace detail

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
inline SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::SqlCoreDataMapperQueryBuilder(
    DataMapper& dm, std::string fields) noexcept:
    _dm { dm },
    _formatter { dm.Connection().QueryFormatter() },
    _fields { std::move(fields) }
{
    this->_query.searchCondition.inputBindings = &_boundInputs;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
size_t SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::Count()
{
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectCount(this->_query.distinct,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition));
    stmt.ExecuteWithVariants(_boundInputs);
    auto reader = stmt.GetResultCursor();
    if (reader.FetchRow())
        return reader.GetColumn<size_t>(1);
    return 0;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
bool SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::Exist()
{
    auto stmt = SqlStatement { _dm.Connection() };

    auto const query = _formatter.SelectFirst(this->_query.distinct,
                                              _fields,
                                              RecordTableName<Record>,
                                              this->_query.searchCondition.tableAlias,
                                              this->_query.searchCondition.tableJoins,
                                              this->_query.searchCondition.condition,
                                              this->_query.orderBy,
                                              1);

    stmt.Prepare(query);
    stmt.ExecuteWithVariants(_boundInputs);
    if (SqlResultCursor reader = stmt.GetResultCursor(); reader.FetchRow())
        return true;
    return false;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
void SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::Delete()
{
    auto stmt = SqlStatement { _dm.Connection() };

    auto const query = _formatter.Delete(RecordTableName<Record>,
                                         this->_query.searchCondition.tableAlias,
                                         this->_query.searchCondition.tableJoins,
                                         this->_query.searchCondition.condition);

    stmt.Prepare(query);
    stmt.Prepare(query);
    stmt.ExecuteWithVariants(_boundInputs);
    stmt.CloseCursor();
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::All()
{

    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectAll(this->_query.distinct,
                                      _fields,
                                      RecordTableName<Record>,
                                      this->_query.searchCondition.tableAlias,
                                      this->_query.searchCondition.tableJoins,
                                      this->_query.searchCondition.condition,
                                      this->_query.orderBy,
                                      this->_query.groupBy));
    stmt.ExecuteWithVariants(_boundInputs);
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.GetResultCursor(), &records);
    if constexpr (DataMapperRecord<Record>)
    {
        // This can be called when record type is not plain aggregate type
        // but more complex tuple, like std::tuple<A, B>
        // for now we do not unwrap this type and just skip auto-loading configuration
        if constexpr (QueryOptions.loadRelations)
        {
            for (auto& record: records)
            {
                _dm.ConfigureRelationAutoLoading(record);
            }
        }
    }
    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    requires(is_aggregate_type(parent_of(Field)))
#else
    requires std::is_member_object_pointer_v<decltype(Field)>
#endif
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::All() -> std::vector<ReferencedFieldTypeOf<Field>>
{
    using value_type = ReferencedFieldTypeOf<Field>;
    auto result = std::vector<value_type> {};

    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectAll(this->_query.distinct,
                                      FullyQualifiedNamesOf<Field>.string_view(),
                                      RecordTableName<Record>,
                                      this->_query.searchCondition.tableAlias,
                                      this->_query.searchCondition.tableJoins,
                                      this->_query.searchCondition.condition,
                                      this->_query.orderBy,
                                      this->_query.groupBy));
    stmt.ExecuteWithVariants(_boundInputs);
    SqlResultCursor reader = stmt.GetResultCursor();
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumn<value_type>(stmt.Connection().ServerType());
    while (true)
    {
        auto& value = result.emplace_back();
        if (outputColumnsBound)
            reader.BindOutputColumn(1, &value);

        if (!reader.FetchRow())
        {
            result.pop_back();
            break;
        }

        if (!outputColumnsBound)
            value = reader.GetColumn<value_type>(1);
    }

    return result;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
    requires(sizeof...(ReferencedFields) >= 2)
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::All() -> std::vector<Record>
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };

    stmt.Prepare(_formatter.SelectAll(this->_query.distinct,
                                      FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                      RecordTableName<Record>,
                                      this->_query.searchCondition.tableAlias,
                                      this->_query.searchCondition.tableJoins,
                                      this->_query.searchCondition.condition,
                                      this->_query.orderBy,
                                      this->_query.groupBy));
    stmt.ExecuteWithVariants(_boundInputs);

    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
    SqlResultCursor reader = stmt.GetResultCursor();
    while (true)
    {
        auto& record = records.emplace_back();
        if (outputColumnsBound)
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
            reader.BindOutputColumns(&(record.[:ReferencedFields:])...);
#else
            reader.BindOutputColumns(&(record.*ReferencedFields)...);
#endif
        if (!reader.FetchRow())
        {
            records.pop_back();
            break;
        }
        if (!outputColumnsBound)
        {
            using ElementMask = std::integer_sequence<size_t, MemberIndexOf<ReferencedFields>...>;
            detail::GetAllColumns<ElementMask>(reader, record);
        }
    }

    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::optional<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::First()
{
    std::optional<Record> record {};
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        _fields,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        1));
    stmt.ExecuteWithVariants(_boundInputs);
    Derived::ReadResult(stmt.Connection().ServerType(), stmt.GetResultCursor(), &record);
    if constexpr (QueryOptions.loadRelations)
    {
        if (record)
            _dm.ConfigureRelationAutoLoading(record.value());
    }
    return record;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    requires(is_aggregate_type(parent_of(Field)))
#else
    requires std::is_member_object_pointer_v<decltype(Field)>
#endif
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::First() -> std::optional<ReferencedFieldTypeOf<Field>>
{
    auto constexpr count = 1;
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        FullyQualifiedNamesOf<Field>.string_view(),
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        count));
    stmt.ExecuteWithVariants(_boundInputs);
    if (SqlResultCursor reader = stmt.GetResultCursor(); reader.FetchRow())
        return reader.template GetColumn<ReferencedFieldTypeOf<Field>>(1);
    return std::nullopt;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
    requires(sizeof...(ReferencedFields) >= 2)
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::First() -> std::optional<Record>
{
    auto optionalRecord = std::optional<Record> {};

    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        1));
    stmt.ExecuteWithVariants(_boundInputs);

    auto& record = optionalRecord.emplace();
    SqlResultCursor reader = stmt.GetResultCursor();
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
    if (outputColumnsBound)
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        reader.BindOutputColumns(&(record.[:ReferencedFields:])...);
#else
        reader.BindOutputColumns(&(record.*ReferencedFields)...);
#endif
    if (!reader.FetchRow())
        return std::nullopt;
    if (!outputColumnsBound)
    {
        using ElementMask = std::integer_sequence<size_t, MemberIndexOf<ReferencedFields>...>;
        detail::GetAllColumns<ElementMask>(reader, record);
    }

    if constexpr (QueryOptions.loadRelations)
        _dm.ConfigureRelationAutoLoading(record);

    return optionalRecord;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::First(size_t n)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(n);
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        _fields,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        n));
    stmt.ExecuteWithVariants(_boundInputs);
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.GetResultCursor(), &records);

    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }
    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::Range(size_t offset, size_t limit)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(limit);
    stmt.Prepare(
        _formatter.SelectRange(this->_query.distinct,
                               _fields,
                               RecordTableName<Record>,
                               this->_query.searchCondition.tableAlias,
                               this->_query.searchCondition.tableJoins,
                               this->_query.searchCondition.condition,
                               !this->_query.orderBy.empty()
                                   ? this->_query.orderBy
                                   : std::format(" ORDER BY \"{}\" ASC", FieldNameAt<RecordPrimaryKeyIndex<Record>, Record>),
                               this->_query.groupBy,
                               offset,
                               limit));
    stmt.ExecuteWithVariants(_boundInputs);
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.GetResultCursor(), &records);
    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }
    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::Range(size_t offset, size_t limit)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(limit);
    stmt.Prepare(
        _formatter.SelectRange(this->_query.distinct,
                               FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                               RecordTableName<Record>,
                               this->_query.searchCondition.tableAlias,
                               this->_query.searchCondition.tableJoins,
                               this->_query.searchCondition.condition,
                               !this->_query.orderBy.empty()
                                   ? this->_query.orderBy
                                   : std::format(" ORDER BY \"{}\" ASC", FieldNameAt<RecordPrimaryKeyIndex<Record>, Record>),
                               this->_query.groupBy,
                               offset,
                               limit));
    stmt.ExecuteWithVariants(_boundInputs);

    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
    SqlResultCursor reader = stmt.GetResultCursor();
    while (true)
    {
        auto& record = records.emplace_back();
        if (outputColumnsBound)
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
            reader.BindOutputColumns(&(record.[:ReferencedFields:])...);
#else
            reader.BindOutputColumns(&(record.*ReferencedFields)...);
#endif
        if (!reader.FetchRow())
        {
            records.pop_back();
            break;
        }
        if (!outputColumnsBound)
        {
            using ElementMask = std::integer_sequence<size_t, MemberIndexOf<ReferencedFields>...>;
            detail::GetAllColumns<ElementMask>(reader, record);
        }
    }

    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }

    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
[[nodiscard]] std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::First(size_t n)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(n);
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        n));
    stmt.ExecuteWithVariants(_boundInputs);

    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
    SqlResultCursor reader = stmt.GetResultCursor();
    while (true)
    {
        auto& record = records.emplace_back();
        if (outputColumnsBound)
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
            reader.BindOutputColumns(&(record.[:ReferencedFields:])...);
#else
            reader.BindOutputColumns(&(record.*ReferencedFields)...);
#endif
        if (!reader.FetchRow())
        {
            records.pop_back();
            break;
        }
        if (!outputColumnsBound)
        {
            using ElementMask = std::integer_sequence<size_t, MemberIndexOf<ReferencedFields>...>;
            detail::GetAllColumns<ElementMask>(reader, record);
        }
    }

    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }

    return records;
}

template <typename Record, DataMapperOptions QueryOptions>
void SqlAllFieldsQueryBuilder<Record, QueryOptions>::ReadResults(SqlServerType sqlServerType,
                                                                 SqlResultCursor reader,
                                                                 std::vector<Record>* records)
{
    while (true)
    {
        Record& record = records->emplace_back();
        if (!detail::ReadSingleResult(sqlServerType, reader, record))
        {
            records->pop_back();
            break;
        }
    }
}

template <typename Record, DataMapperOptions QueryOptions>
void SqlAllFieldsQueryBuilder<Record, QueryOptions>::ReadResult(SqlServerType sqlServerType,
                                                                SqlResultCursor reader,
                                                                std::optional<Record>* optionalRecord)
{
    Record& record = optionalRecord->emplace();
    if (!detail::ReadSingleResult(sqlServerType, reader, record))
        optionalRecord->reset();
}

template <typename FirstRecord, typename SecondRecord, DataMapperOptions QueryOptions>
void SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions>::ReadResults(
    SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records)
{
    while (true)
    {
        auto& record = records->emplace_back();
        auto& [firstRecord, secondRecord] = record;

        using FirstRecordType = std::remove_cvref_t<decltype(firstRecord)>;
        using SecondRecordType = std::remove_cvref_t<decltype(secondRecord)>;

        auto const outputColumnsBoundFirst = detail::CanSafelyBindOutputColumns<FirstRecordType>(sqlServerType);
        auto const outputColumnsBoundSecond = detail::CanSafelyBindOutputColumns<SecondRecordType>(sqlServerType);
        auto const canSafelyBindAll = outputColumnsBoundFirst && outputColumnsBoundSecond;

        if (canSafelyBindAll)
        {
            detail::BindAllOutputColumnsWithOffset(reader, firstRecord, 1);
            detail::BindAllOutputColumnsWithOffset(reader, secondRecord, 1 + Reflection::CountMembers<FirstRecord>);
        }

        if (!reader.FetchRow())
        {
            records->pop_back();
            break;
        }

        if (!canSafelyBindAll)
            detail::GetAllColumns(reader, record);
    }
}

template <typename Record>
std::string DataMapper::Inspect(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    std::string str;
    Reflection::CallOnMembers(record, [&str]<typename Name, typename Value>(Name const& name, Value const& value) {
        if (!str.empty())
            str += '\n';

        if constexpr (FieldWithStorage<Value>)
        {
            if constexpr (Value::IsOptional)
            {
                if (!value.Value().has_value())
                {
                    str += std::format("{} {} := <nullopt>", Reflection::TypeNameOf<Value>, name);
                }
                else
                {
                    str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value.Value().value());
                }
            }
            else if constexpr (IsBelongsTo<Value>)
            {
                str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value.Value());
            }
            else if constexpr (std::same_as<typename Value::ValueType, char>)
            {
            }
            else
            {
                str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value.InspectValue());
            }
        }
        else if constexpr (!IsHasMany<Value> && !IsHasManyThrough<Value> && !IsHasOneThrough<Value> && !IsBelongsTo<Value>)
            str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value);
    });
    return "{\n" + std::move(str) + "\n}";
}

template <typename Record>
std::vector<std::string> DataMapper::CreateTableString(SqlServerType serverType)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto migration = SqlQueryBuilder(*SqlQueryFormatter::Get(serverType)).Migration();
    auto createTable = migration.CreateTable(RecordTableName<Record>);
    detail::PopulateCreateTableBuilder<Record>(createTable);
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

template <typename Record>
std::optional<RecordPrimaryKeyType<Record>> DataMapper::GenerateAutoAssignPrimaryKey(Record const& record)
{
    std::optional<RecordPrimaryKeyType<Record>> result;
    Reflection::EnumerateMembers(
        record, [this, &result]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
            if constexpr (IsField<PrimaryKeyType> && IsPrimaryKey<PrimaryKeyType>
                          && detail::IsAutoAssignPrimaryKeyField<PrimaryKeyType>::value)
            {
                using ValueType = typename PrimaryKeyType::ValueType;
                if constexpr (std::same_as<ValueType, SqlGuid>)
                {
                    if (!primaryKeyField.Value())
                        [&](auto& res) {
                            res.emplace(SqlGuid::Create());
                        }(result);
                }
                else if constexpr (requires { ValueType {} + 1; })
                {
                    if (primaryKeyField.Value() == ValueType {})
                    {
                        auto maxId = SqlStatement { _connection }.ExecuteDirectScalar<ValueType>(
                            std::format(R"sql(SELECT MAX("{}") FROM "{}")sql",
                                        FieldNameAt<PrimaryKeyIndex, Record>,
                                        RecordTableName<Record>));
                        result = maxId.value_or(ValueType {}) + 1;
                    }
                }
            }
        });
    return result;
}

template <DataMapper::PrimaryKeySource UsePkOverride, typename Record>
RecordPrimaryKeyType<Record> DataMapper::CreateInternal(
    Record const& record,
    std::optional<std::conditional_t<std::is_void_v<RecordPrimaryKeyType<Record>>, int, RecordPrimaryKeyType<Record>>>
        pkOverride)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Insert(nullptr);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
            query.Set(FieldNameOf<el>, SqlWildcard);
    }
#else
    Reflection::EnumerateMembers(record, [&query]<auto I, typename FieldType>(FieldType const& /*field*/) {
        if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
    });
#endif

    _stmt.Prepare(query);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    int i = 1;
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
        {
            if constexpr (IsPrimaryKey<FieldType> && UsePkOverride == PrimaryKeySource::Override)
                _stmt.BindInputParameter(i++, *pkOverride, std::meta::identifier_of(el));
            else
                _stmt.BindInputParameter(i++, record.[:el:], std::meta::identifier_of(el));
        }
    }
#else
    Reflection::CallOnMembers(record,
                              [this, &pkOverride, i = SQLSMALLINT { 1 }]<typename Name, typename FieldType>(
                                  Name const& name, FieldType const& field) mutable {
                                  if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
                                  {
                                      if constexpr (IsPrimaryKey<FieldType> && UsePkOverride == PrimaryKeySource::Override)
                                          _stmt.BindInputParameter(i++, *pkOverride, name);
                                      else
                                          _stmt.BindInputParameter(i++, field, name);
                                  }
                              });
#endif
    _stmt.Execute();

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        return { _stmt.LastInsertId(RecordTableName<Record>) };
    else if constexpr (HasPrimaryKey<Record>)
    {
        if constexpr (UsePkOverride == PrimaryKeySource::Override)
            return *pkOverride; // NOLINT(bugprone-unchecked-optional-access)
        else
            return RecordPrimaryKeyOf(record).Value();
    }
}

template <typename Record>
RecordPrimaryKeyType<Record> DataMapper::CreateExplicit(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    return CreateInternal<PrimaryKeySource::Record>(record);
}

template <DataMapperOptions QueryOptions, typename Record>
RecordPrimaryKeyType<Record> DataMapper::CreateCopyOf(Record const& originalRecord)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(HasPrimaryKey<Record>, "CreateCopyOf requires a record type with a primary key");

    auto generatedKey = GenerateAutoAssignPrimaryKey(originalRecord);
    if (generatedKey)
        return CreateInternal<PrimaryKeySource::Override>(originalRecord, generatedKey);

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        return CreateInternal<PrimaryKeySource::Record>(originalRecord);

    return CreateInternal<PrimaryKeySource::Override>(originalRecord, RecordPrimaryKeyType<Record> {});
}

template <DataMapperOptions QueryOptions, typename Record>
RecordPrimaryKeyType<Record> DataMapper::Create(Record& record)
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto generatedKey = GenerateAutoAssignPrimaryKey(record);
    if (generatedKey)
        SetId(record, *generatedKey);

    auto pk = CreateInternal<PrimaryKeySource::Record>(record);

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        SetId(record, pk);

    SetModifiedState<ModifiedState::NotModified>(record);

    if constexpr (QueryOptions.loadRelations)
        ConfigureRelationAutoLoading(record);

    if constexpr (HasPrimaryKey<Record>)
        return GetPrimaryKeyField(record);
}

template <typename Record>
bool DataMapper::IsModified(Record const& record) const noexcept
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    bool modified = false;

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        if constexpr (requires { record.[:el:].IsModified(); })
        {
            modified = modified || record.[:el:].IsModified();
        }
    }
#else
    Reflection::CallOnMembers(record, [&modified](auto const& /*name*/, auto const& field) {
        if constexpr (requires { field.IsModified(); })
        {
            modified = modified || field.IsModified();
        }
    });
#endif

    return modified;
}

template <typename Record>
void DataMapper::Update(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Update();

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (FieldWithStorage<FieldType>)
        {
            if (record.[:el:].IsModified())
                query.Set(FieldNameOf<el>, SqlWildcard);
            if constexpr (IsPrimaryKey<FieldType>)
                std::ignore = query.Where(FieldNameOf<el>, SqlWildcard);
        }
    }
#else
    Reflection::CallOnMembersWithoutName(record, [&query]<size_t I, typename FieldType>(FieldType const& field) {
        if (field.IsModified())
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
        // for some reason compiler do not want to properly deduce FieldType, so here we
        // directly infer the type from the Record type and index
        if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
            std::ignore = query.Where(FieldNameAt<I, Record>, SqlWildcard);
    });
#endif
    _stmt.Prepare(query);

    SQLSMALLINT i = 1;

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        if (record.[:el:].IsModified())
        {
            _stmt.BindInputParameter(i++, record.[:el:].Value(), FieldNameOf<el>);
        }
    }

    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (FieldType::IsPrimaryKey)
        {
            _stmt.BindInputParameter(i++, record.[:el:].Value(), FieldNameOf<el>);
        }
    }
#else
    // Bind the SET clause
    Reflection::CallOnMembersWithoutName(record, [this, &i]<size_t I, typename FieldType>(FieldType const& field) {
        if (field.IsModified())
            _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
    });

    // Bind the WHERE clause
    Reflection::CallOnMembersWithoutName(record, [this, &i]<size_t I, typename FieldType>(FieldType const& field) {
        if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
            _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
    });
#endif

    _stmt.Execute();

    SetModifiedState<ModifiedState::NotModified>(record);
}

template <typename Record>
std::size_t DataMapper::Delete(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Delete();

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (FieldType::IsPrimaryKey)
            std::ignore = query.Where(FieldNameOf<el>, SqlWildcard);
    }
#else
    Reflection::CallOnMembersWithoutName(record, [&query]<size_t I, typename FieldType>(FieldType const& /*field*/) {
        if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
            std::ignore = query.Where(FieldNameAt<I, Record>, SqlWildcard);
    });
#endif

    _stmt.Prepare(query);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    SQLSMALLINT i = 1;
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (FieldType::IsPrimaryKey)
        {
            _stmt.BindInputParameter(i++, record.[:el:].Value(), FieldNameOf<el>);
        }
    }
#else
    // Bind the WHERE clause
    Reflection::CallOnMembersWithoutName(
        record, [this, i = SQLSMALLINT { 1 }]<size_t I, typename FieldType>(FieldType const& field) mutable {
            if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
                _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
        });
#endif

    _stmt.Execute();

    return _stmt.NumRowsAffected();
}

template <typename Record, typename... PrimaryKeyTypes>
std::optional<Record> DataMapper::QuerySingleWithoutRelationAutoLoading(PrimaryKeyTypes&&... primaryKeys)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto queryBuilder = _connection.Query(RecordTableName<Record>).Select();

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
        {
            queryBuilder.Field(FieldNameAt<I, Record>);

            if constexpr (FieldType::IsPrimaryKey)
                std::ignore = queryBuilder.Where(FieldNameAt<I, Record>, SqlWildcard);
        }
    });

    _stmt.Prepare(queryBuilder.First());
    _stmt.Execute(std::forward<PrimaryKeyTypes>(primaryKeys)...);

    auto resultRecord = std::optional<Record> { Record {} };
    auto reader = _stmt.GetResultCursor();
    if (!detail::ReadSingleResult(_stmt.Connection().ServerType(), reader, *resultRecord))
        return std::nullopt;

    if (resultRecord)
        SetModifiedState<ModifiedState::NotModified>(resultRecord.value());

    return resultRecord;
}

template <typename Record, typename... PrimaryKeyTypes>
std::optional<Record> DataMapper::QuerySingle(PrimaryKeyTypes&&... primaryKeys)
{
    auto record = QuerySingleWithoutRelationAutoLoading<Record>(std::forward<PrimaryKeyTypes>(primaryKeys)...);
    if (record)
    {
        ConfigureRelationAutoLoading(*record);
    }
    return record;
}

template <typename Record, typename... Args>
std::optional<Record> DataMapper::QuerySingle(SqlSelectQueryBuilder selectQuery, Args&&... args)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
            selectQuery.Field(SqlQualifiedTableColumnName { RecordTableName<Record>, FieldNameAt<I, Record> });
    });
    _stmt.Prepare(selectQuery.First().ToSql());
    _stmt.Execute(std::forward<Args>(args)...);

    auto resultRecord = std::optional<Record> { Record {} };
    auto reader = _stmt.GetResultCursor();
    if (!detail::ReadSingleResult(_stmt.Connection().ServerType(), reader, *resultRecord))
        return std::nullopt;

    if (resultRecord)
        SetModifiedState<ModifiedState::NotModified>(resultRecord.value());

    return resultRecord;
}

// TODO: Provide Query(QueryBuilder, ...) method variant

template <typename Record, typename... InputParameters>
inline LIGHTWEIGHT_FORCE_INLINE std::vector<Record> DataMapper::Query(
    SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record> || std::same_as<Record, SqlVariantRow>, "Record must satisfy DataMapperRecord");

    return Query<Record>(selectQuery.ToSql(), std::forward<InputParameters>(inputParameters)...);
}

template <typename Record, typename... InputParameters>
std::vector<Record> DataMapper::Query(std::string_view sqlQueryString, InputParameters&&... inputParameters)
{
    auto result = std::vector<Record> {};
    if constexpr (std::same_as<Record, SqlVariantRow>)
    {
        _stmt.Prepare(sqlQueryString);
        _stmt.Execute(std::forward<InputParameters>(inputParameters)...);
        size_t const numResultColumns = _stmt.NumColumnsAffected();
        while (_stmt.FetchRow())
        {
            auto& record = result.emplace_back();
            record.reserve(numResultColumns);
            for (auto const i: std::views::iota(1U, numResultColumns + 1))
                record.emplace_back(_stmt.GetColumn<SqlVariant>(static_cast<SQLUSMALLINT>(i)));
        }
    }
    else
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

        bool const canSafelyBindOutputColumns = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());

        _stmt.Prepare(sqlQueryString);
        _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

        auto reader = _stmt.GetResultCursor();

        for (;;)
        {
            auto& record = result.emplace_back();

            if (canSafelyBindOutputColumns)
                BindOutputColumns(record);

            if (!reader.FetchRow())
                break;

            if (!canSafelyBindOutputColumns)
                detail::GetAllColumns(reader, record);
        }

        // Drop the last record, which we failed to fetch (End of result set).
        result.pop_back();

        for (auto& record: result)
        {
            SetModifiedState<ModifiedState::NotModified>(record);
            ConfigureRelationAutoLoading(record);
        }
    }

    return result;
}

template <typename First, typename Second, typename... Rest, DataMapperOptions QueryOptions>
    requires DataMapperRecord<First> && DataMapperRecord<Second> && DataMapperRecords<Rest...>
std::vector<std::tuple<First, Second, Rest...>> DataMapper::Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery)
{
    using value_type = std::tuple<First, Second, Rest...>;
    auto result = std::vector<value_type> {};

    _stmt.Prepare(selectQuery.ToSql());
    _stmt.Execute();
    auto reader = _stmt.GetResultCursor();

    constexpr auto calculateOffset = []<size_t I, typename Tuple>() {
        size_t offset = 1;

        if constexpr (I > 0)
        {
            [&]<size_t... Indices>(std::index_sequence<Indices...>) {
                ((Indices < I ? (offset += Reflection::CountMembers<std::tuple_element_t<Indices, Tuple>>) : 0), ...);
            }(std::make_index_sequence<I> {});
        }
        return offset;
    };

    auto const BindElements = [&](auto& record) {
        Reflection::template_for<0, std::tuple_size_v<value_type>>([&]<auto I>() {
            using TupleElement = std::decay_t<std::tuple_element_t<I, value_type>>;
            auto& element = std::get<I>(record);
            constexpr size_t offset = calculateOffset.template operator()<I, value_type>();
            this->BindOutputColumns<TupleElement, offset>(element);
        });
    };

    auto const GetElements = [&](auto& record) {
        Reflection::template_for<0, std::tuple_size_v<value_type>>([&]<auto I>() {
            auto& element = std::get<I>(record);
            constexpr size_t offset = calculateOffset.template operator()<I, value_type>();
            detail::GetAllColumns(reader, element, offset - 1);
        });
    };

    bool const canSafelyBindOutputColumns = [&]() {
        bool result = true;
        Reflection::template_for<0, std::tuple_size_v<value_type>>([&]<auto I>() {
            using TupleElement = std::decay_t<std::tuple_element_t<I, value_type>>;
            result &= detail::CanSafelyBindOutputColumns<TupleElement>(_stmt.Connection().ServerType());
        });
        return result;
    }();

    for (;;)
    {
        auto& record = result.emplace_back();

        if (canSafelyBindOutputColumns)
            BindElements(record);

        if (!reader.FetchRow())
            break;

        if (!canSafelyBindOutputColumns)
            GetElements(record);
    }

    // Drop the last record, which we failed to fetch (End of result set).
    result.pop_back();

    for (auto& record: result)
    {
        Reflection::template_for<0, std::tuple_size_v<value_type>>([&]<auto I>() {
            auto& element = std::get<I>(record);
            SetModifiedState<ModifiedState::NotModified>(element);
            if constexpr (QueryOptions.loadRelations)
            {
                ConfigureRelationAutoLoading(element);
            }
        });
    }

    return result;
}

template <typename ElementMask, typename Record, typename... InputParameters>
std::vector<Record> DataMapper::Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                                      InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    _stmt.Prepare(selectQuery.ToSql());
    _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

    auto records = std::vector<Record> {};

    // TODO: We could optimize this further by only considering ElementMask fields in Record.
    bool const canSafelyBindOutputColumns = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());

    auto reader = _stmt.GetResultCursor();

    for (;;)
    {
        auto& record = records.emplace_back();

        if (canSafelyBindOutputColumns)
            BindOutputColumns<ElementMask>(record);

        if (!reader.FetchRow())
            break;

        if (!canSafelyBindOutputColumns)
            detail::GetAllColumns<ElementMask>(reader, record);
    }

    // Drop the last record, which we failed to fetch (End of result set).
    records.pop_back();

    for (auto& record: records)
    {
        SetModifiedState<ModifiedState::NotModified>(record);
        ConfigureRelationAutoLoading(record);
    }

    return records;
}

template <DataMapper::ModifiedState state, typename Record>
void DataMapper::SetModifiedState(Record& record) noexcept
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, []<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (requires { field.SetModified(false); })
        {
            if constexpr (state == ModifiedState::Modified)
                field.SetModified(true);
            else
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

template <typename FieldType>
std::optional<typename FieldType::ReferencedRecord> DataMapper::LoadBelongsTo(FieldType::ValueType value)
{
    using ReferencedRecord = FieldType::ReferencedRecord;

    std::optional<ReferencedRecord> record { std::nullopt };

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^ReferencedRecord, ctx)))
    {
        using BelongsToFieldType = typename[:std::meta::type_of(el):];
        if constexpr (IsField<BelongsToFieldType>)
            if constexpr (BelongsToFieldType::IsPrimaryKey)
            {
                if (auto result = QuerySingle<ReferencedRecord>(value); result)
                    record = std::move(result);
                else
                    SqlLogger::GetLogger().OnWarning(
                        std::format("Loading BelongsTo failed for {}", RecordTableName<ReferencedRecord>));
            }
    }
#else
    CallOnPrimaryKey<ReferencedRecord>([&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>() {
        if (auto result = QuerySingle<ReferencedRecord>(value); result)
            record = std::move(result);
        else
            SqlLogger::GetLogger().OnWarning(
                std::format("Loading BelongsTo failed for {}", RecordTableName<ReferencedRecord>));
    });
#endif
    return record;
}

template <size_t FieldIndex, typename Record, typename OtherRecord, typename Callable>
void DataMapper::CallOnHasMany(Record& record, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    using FieldType = HasMany<OtherRecord>;
    using ReferencedRecord = FieldType::ReferencedRecord;

    CallOnPrimaryKey(record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
        auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                         .Select()
                         .Build([&](auto& query) {
                             Reflection::EnumerateMembers<ReferencedRecord>(
                                 [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                     if constexpr (FieldWithStorage<ReferencedFieldType>)
                                     {
                                         query.Field(FieldNameAt<ReferencedFieldIndex, ReferencedRecord>);
                                     }
                                 });
                         })
                         .Where(FieldNameAt<FieldIndex, ReferencedRecord>, SqlWildcard);
        callback(query, primaryKeyField);
    });
}

template <size_t FieldIndex, typename Record, typename OtherRecord>
void DataMapper::LoadHasMany(Record& record, HasMany<OtherRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    CallOnHasMany<FieldIndex, Record, OtherRecord>(record, [&](SqlSelectQueryBuilder selectQuery, auto& primaryKeyField) {
        field.Emplace(detail::ToSharedPtrList(Query<OtherRecord>(selectQuery.All(), primaryKeyField.Value())));
    });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record>
void DataMapper::LoadHasOneThrough(Record& record, HasOneThrough<ReferencedRecord, ThroughRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<ThroughRecord>, "ThroughRecord must satisfy DataMapperRecord");

    // Find the PK of Record
    CallOnPrimaryKey(record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
        // Find the BelongsTo of ThroughRecord pointing to the PK of Record
        CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToIndex, typename ThroughBelongsToType>() {
            // Find the PK of ThroughRecord
            CallOnPrimaryKey<ThroughRecord>([&]<size_t ThroughPrimaryKeyIndex, typename ThroughPrimaryKeyType>() {
                // Find the BelongsTo of ReferencedRecord pointing to the PK of ThroughRecord
                CallOnBelongsTo<ReferencedRecord>([&]<size_t ReferencedKeyIndex, typename ReferencedKeyType>() {
                    // Query the ReferencedRecord where:
                    // - the BelongsTo of ReferencedRecord points to the PK of ThroughRecord,
                    // - and the BelongsTo of ThroughRecord points to the PK of Record
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
                                                FieldNameAt<ReferencedFieldIndex, ReferencedRecord> });
                                        }
                                    });
                            })
                            .InnerJoin(RecordTableName<ThroughRecord>,
                                       FieldNameAt<ThroughPrimaryKeyIndex, ThroughRecord>,
                                       FieldNameAt<ReferencedKeyIndex, ReferencedRecord>)
                            .InnerJoin(RecordTableName<Record>,
                                       FieldNameAt<PrimaryKeyIndex, Record>,
                                       SqlQualifiedTableColumnName { RecordTableName<ThroughRecord>,
                                                                     FieldNameAt<ThroughBelongsToIndex, ThroughRecord> })
                            .Where(
                                SqlQualifiedTableColumnName {
                                    RecordTableName<Record>,
                                    FieldNameAt<PrimaryKeyIndex, ThroughRecord>,
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
    CallOnPrimaryKey(record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
        // Find the BelongsTo of ThroughRecord pointing to the PK of Record
        CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToRecordIndex, typename ThroughBelongsToRecordType>() {
            using ThroughBelongsToRecordFieldType = Reflection::MemberTypeOf<ThroughBelongsToRecordIndex, ThroughRecord>;
            if constexpr (std::is_same_v<typename ThroughBelongsToRecordFieldType::ReferencedRecord, Record>)
            {
                // Find the BelongsTo of ThroughRecord pointing to the PK of ReferencedRecord
                CallOnBelongsTo<ThroughRecord>(
                    [&]<size_t ThroughBelongsToReferenceRecordIndex, typename ThroughBelongsToReferenceRecordType>() {
                        using ThroughBelongsToReferenceRecordFieldType =
                            Reflection::MemberTypeOf<ThroughBelongsToReferenceRecordIndex, ThroughRecord>;
                        if constexpr (std::is_same_v<typename ThroughBelongsToReferenceRecordFieldType::ReferencedRecord,
                                                     ReferencedRecord>)
                        {
                            auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                                             .Select()
                                             .Build([&](auto& query) {
                                                 Reflection::EnumerateMembers<ReferencedRecord>(
                                                     [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                                         if constexpr (FieldWithStorage<ReferencedFieldType>)
                                                         {
                                                             query.Field(SqlQualifiedTableColumnName {
                                                                 RecordTableName<ReferencedRecord>,
                                                                 FieldNameAt<ReferencedFieldIndex, ReferencedRecord> });
                                                         }
                                                     });
                                             })
                                             .InnerJoin(RecordTableName<ThroughRecord>,
                                                        FieldNameAt<ThroughBelongsToReferenceRecordIndex, ThroughRecord>,
                                                        SqlQualifiedTableColumnName { RecordTableName<ReferencedRecord>,
                                                                                      FieldNameAt<PrimaryKeyIndex, Record> })
                                             .Where(
                                                 SqlQualifiedTableColumnName {
                                                     RecordTableName<ThroughRecord>,
                                                     FieldNameAt<ThroughBelongsToRecordIndex, ThroughRecord>,
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

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (IsBelongsTo<FieldType>)
        {
            auto& field = record.[:el:];
            field = LoadBelongsTo<FieldType>(field.Value());
        }
        else if constexpr (IsHasMany<FieldType>)
        {
            LoadHasMany<el>(record, record.[:el:]);
        }
        else if constexpr (IsHasOneThrough<FieldType>)
        {
            LoadHasOneThrough(record, record.[:el:]);
        }
        else if constexpr (IsHasManyThrough<FieldType>)
        {
            LoadHasManyThrough(record, record.[:el:]);
        }
    }
#else
    Reflection::EnumerateMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            field = LoadBelongsTo<FieldType>(field.Value());
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
#endif
}

template <typename Record, typename ValueType>
inline LIGHTWEIGHT_FORCE_INLINE void DataMapper::SetId(Record& record, ValueType&& id)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    // static_assert(HasPrimaryKey<Record>);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)

    auto constexpr ctx = std::meta::access_context::current();
    template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
    {
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                record.[:el:] = std::forward<ValueType>(id);
            }
        }
    }
#else
    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
        if constexpr (IsField<FieldType>)
        {
            if constexpr (FieldType::IsPrimaryKey)
            {
                field = std::forward<FieldType>(id);
            }
        }
    });
#endif
}

template <typename Record, size_t InitialOffset>
inline LIGHTWEIGHT_FORCE_INLINE Record& DataMapper::BindOutputColumns(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    BindOutputColumns<Record, InitialOffset>(record, &_stmt);
    return record;
}

template <typename Record, size_t InitialOffset>
Record& DataMapper::BindOutputColumns(Record& record, SqlStatement* stmt)
{
    return BindOutputColumns<std::make_integer_sequence<size_t, Reflection::CountMembers<Record>>, Record, InitialOffset>(
        record, stmt);
}

template <typename ElementMask, typename Record, size_t InitialOffset>
inline LIGHTWEIGHT_FORCE_INLINE Record& DataMapper::BindOutputColumns(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    return BindOutputColumns<ElementMask, Record, InitialOffset>(record, &_stmt);
}

template <typename ElementMask, typename Record, size_t InitialOffset>
Record& DataMapper::BindOutputColumns(Record& record, SqlStatement* stmt)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(!std::is_const_v<Record>);
    assert(stmt != nullptr);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    SQLSMALLINT i = SQLSMALLINT { InitialOffset };
    template for (constexpr auto index: define_static_array(template_arguments_of(^^ElementMask)) | std::views::drop(1))
    {
        constexpr auto el = nonstatic_data_members_of(^^Record, ctx)[[:index:]];
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (IsField<FieldType>)
        {
            stmt->BindOutputColumn(i++, &record.[:el:].MutableValue());
        }
        else if constexpr (SqlOutputColumnBinder<FieldType>)
        {
            stmt->BindOutputColumn(i++, &record.[:el:]);
        }
    }
#else
    Reflection::EnumerateMembers<ElementMask>(
        record, [stmt, i = SQLUSMALLINT { InitialOffset }]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                stmt->BindOutputColumn(i++, &field.MutableValue());
            }
            else if constexpr (SqlOutputColumnBinder<Field>)
            {
                stmt->BindOutputColumn(i++, &field);
            }
        });
#endif

    return record;
}

template <typename Record>
void DataMapper::ConfigureRelationAutoLoading(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto const callback = [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            field.SetAutoLoader(typename FieldType::Loader {
                .loadReference = [value = field.Value()]() -> std::optional<typename FieldType::ReferencedRecord> {
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    return dm.LoadBelongsTo<FieldType>(value);
                },
            });
        }
        else if constexpr (IsHasMany<FieldType>)
        {
            using ReferencedRecord = FieldType::ReferencedRecord;
            HasMany<ReferencedRecord>& hasMany = field;
            hasMany.SetAutoLoader(typename FieldType::Loader {
                .count = [&record]() -> size_t {
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    size_t count = 0;
                    dm.CallOnHasMany<FieldIndex, Record, ReferencedRecord>(
                        record, [&](SqlSelectQueryBuilder selectQuery, auto const& primaryKeyField) {
                            dm._stmt.Prepare(selectQuery.Count());
                            dm._stmt.Execute(primaryKeyField.Value());
                            if (dm._stmt.FetchRow())
                                count = dm._stmt.GetColumn<size_t>(1);
                            dm._stmt.CloseCursor();
                        });
                    return count;
                },
                .all =
                    [&record, &hasMany]() {
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.LoadHasMany<FieldIndex>(record, hasMany);
                    },
                .each =
                    [&record](auto const& each) {
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.CallOnHasMany<FieldIndex, Record, ReferencedRecord>(
                            record, [&](SqlSelectQueryBuilder selectQuery, auto const& primaryKeyField) {
                                auto stmt = SqlStatement { dm._connection };
                                stmt.Prepare(selectQuery.All());
                                stmt.Execute(primaryKeyField.Value());

                                auto referencedRecord = ReferencedRecord {};
                                dm.BindOutputColumns(referencedRecord, &stmt);
                                dm.ConfigureRelationAutoLoading(referencedRecord);

                                while (stmt.FetchRow())
                                {
                                    each(referencedRecord);
                                    dm.BindOutputColumns(referencedRecord, &stmt);
                                }
                            });
                    },
            });
        }
        else if constexpr (IsHasOneThrough<FieldType>)
        {
            using ReferencedRecord = FieldType::ReferencedRecord;
            using ThroughRecord = FieldType::ThroughRecord;
            HasOneThrough<ReferencedRecord, ThroughRecord>& hasOneThrough = field;
            hasOneThrough.SetAutoLoader(typename FieldType::Loader {
                .loadReference =
                    [&record, &hasOneThrough]() {
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.LoadHasOneThrough<ReferencedRecord, ThroughRecord>(record, hasOneThrough);
                    },
            });
        }
        else if constexpr (IsHasManyThrough<FieldType>)
        {
            using ReferencedRecord = FieldType::ReferencedRecord;
            using ThroughRecord = FieldType::ThroughRecord;
            HasManyThrough<ReferencedRecord, ThroughRecord>& hasManyThrough = field;
            hasManyThrough.SetAutoLoader(typename FieldType::Loader {
                .count = [&record]() -> size_t {
                    // Load result for Count()
                    size_t count = 0;
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    dm.CallOnHasManyThrough<ReferencedRecord, ThroughRecord>(
                        record, [&](SqlSelectQueryBuilder& selectQuery, auto& primaryKeyField) {
                            dm._stmt.Prepare(selectQuery.Count());
                            dm._stmt.Execute(primaryKeyField.Value());
                            if (dm._stmt.FetchRow())
                                count = dm._stmt.GetColumn<size_t>(1);
                            dm._stmt.CloseCursor();
                        });
                    return count;
                },
                .all =
                    [&record, &hasManyThrough]() {
                        // Load result for All()
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.LoadHasManyThrough(record, hasManyThrough);
                    },
                .each =
                    [&record](auto const& each) {
                        // Load result for Each()
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.CallOnHasManyThrough<ReferencedRecord, ThroughRecord>(
                            record, [&](SqlSelectQueryBuilder& selectQuery, auto& primaryKeyField) {
                                auto stmt = SqlStatement { dm._connection };
                                stmt.Prepare(selectQuery.All());
                                stmt.Execute(primaryKeyField.Value());

                                auto referencedRecord = ReferencedRecord {};
                                dm.BindOutputColumns(referencedRecord, &stmt);
                                dm.ConfigureRelationAutoLoading(referencedRecord);

                                while (stmt.FetchRow())
                                {
                                    each(referencedRecord);
                                    dm.BindOutputColumns(referencedRecord, &stmt);
                                }
                            });
                    },
            });
        }
    };

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    constexpr auto ctx = std::meta::access_context::current();

    Reflection::template_for<0, nonstatic_data_members_of(^^Record, ctx).size()>([&callback, &record]<auto I>() {
        constexpr auto localctx = std::meta::access_context::current();
        constexpr auto members = define_static_array(nonstatic_data_members_of(^^Record, localctx));
        using FieldType = typename[:std::meta::type_of(members[I]):];
        callback.template operator()<I, FieldType>(record.[:members[I]:]);
    });
#else
    Reflection::EnumerateMembers(record, callback);
#endif
}

template <typename T>
std::optional<T> DataMapper::Execute(std::string_view sqlQueryString)
{
    return _stmt.ExecuteDirectScalar<T>(sqlQueryString);
}

} // namespace Lightweight
