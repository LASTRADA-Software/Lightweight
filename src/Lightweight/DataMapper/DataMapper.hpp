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
#include "Record.hpp"

#include <reflection-cpp/reflection.hpp>

#include <cassert>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

/// @defgroup DataMapper Data Mapper
///
/// @brief The data mapper is a high level API for mapping records to and from the database using high level C++ syntax.

/// Requires that T satisfies to be a field with storage.
///
/// @ingroup DataMapper
template <typename T>
concept FieldWithStorage = requires(T const& field, T& mutableField) {
    // clang-format off
    { field.Value() } -> std::convertible_to<typename T::ValueType const&>;
    { mutableField.MutableValue() } -> std::convertible_to<typename T::ValueType&>;
    { field.IsModified() } -> std::convertible_to<bool>;
    { mutableField.SetModified(bool {}) } -> std::convertible_to<void>;
    // clang-format on
};

/// Represents the number of fields with storage in a record.
///
/// @ingroup DataMapper
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

template <auto Test, typename T>
constexpr bool CheckFieldProperty = Reflection::FoldMembers<T>(false, []<size_t I, typename Field>(bool const accum) {
    if constexpr (Test.template operator()<Field>())
        return true;
    else
        return accum;
});

} // namespace detail

/// @brief Tests if the given record type does contain a primary key.
///
/// @ingroup DataMapper
template <typename T>
constexpr bool HasPrimaryKey = detail::CheckFieldProperty<[]<typename Field>() { return IsPrimaryKey<Field>; }, T>;

/// @brief Tests if the given record type does contain an auto increment primary key.
///
/// @ingroup DataMapper
template <typename T>
constexpr bool HasAutoIncrementPrimaryKey =
    detail::CheckFieldProperty<[]<typename Field>() { return IsAutoIncrementPrimaryKey<Field>; }, T>;

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

template <typename Record>
constexpr bool CanSafelyBindOutputColumns(SqlServerType sqlServerType, Record const& record)
{
    if (sqlServerType != SqlServerType::MICROSOFT_SQL)
        return true;

    // Test if we have some columns that might not be sufficient to store the result (e.g. string truncation),
    // then don't call BindOutputColumn but SQLFetch to get the result, because
    // regrowing previously bound columns is not supported in MS-SQL's ODBC driver, so it seems.
    bool result = true;
    Reflection::EnumerateMembers(record, [&result]<size_t I, typename Field>(Field& /*field*/) {
        if constexpr (IsField<Field>)
        {
            if constexpr (detail::OneOf<typename Field::ValueType,
                                        std::string,
                                        std::wstring,
                                        std::u16string,
                                        std::u32string,
                                        SqlBinary>
                          || IsSqlDynamicString<typename Field::ValueType>)
            {
                // Known types that MAY require growing due to truncation.
                result = false;
            }
        }
    });
    return result;
}

template <typename Record>
void BindAllOutputColumnsWithOffset(SqlResultCursor& reader, Record& record, SQLSMALLINT startOffset)
{
    Reflection::EnumerateMembers(record, [reader = &reader, i = startOffset]<size_t I, typename Field>(Field& field) mutable {
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

template <typename Record>
void GetAllColumns(SqlResultCursor& reader, Record& record)
{
    Reflection::EnumerateMembers(record, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
        if constexpr (IsField<Field>)
        {
            field.MutableValue() = reader->GetColumn<typename Field::ValueType>(I + 1);
        }
        else if constexpr (SqlGetColumnNativeType<Field>)
        {
            field = reader->GetColumn<Field>(I + 1);
        }
    });
}

template <typename FirstRecord, typename SecondRecord>
void GetAllColumns(SqlResultCursor& reader, std::tuple<FirstRecord, SecondRecord>& record)
{
    auto& [firstRecord, secondRecord] = record;

    Reflection::EnumerateMembers(firstRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
        if constexpr (IsField<Field>)
        {
            field.MutableValue() = reader->GetColumn<typename Field::ValueType>(I + 1);
        }
        else if constexpr (SqlGetColumnNativeType<Field>)
        {
            field = reader->GetColumn<Field>(I + 1);
        }
    });

    Reflection::EnumerateMembers(secondRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
        if constexpr (IsField<Field>)
        {
            field.MutableValue() =
                reader->GetColumn<typename Field::ValueType>(Reflection::CountMembers<FirstRecord> + I + 1);
        }
        else if constexpr (SqlGetColumnNativeType<Field>)
        {
            field = reader->GetColumn<Field>(Reflection::CountMembers<FirstRecord> + I + 1);
        }
    });
}

} // namespace detail

/// Main API for mapping records to C++ from the database using high level C++ syntax.
///
/// @ingroup DataMapper
template <typename Record, typename Derived>
class [[nodiscard]] SqlCoreDataMapperQueryBuilder: public SqlBasicSelectQueryBuilder<Derived>
{
  private:
    SqlStatement& _stmt;
    SqlQueryFormatter const& _formatter;

    std::string _fields;

    friend class SqlWhereClauseBuilder<Derived>;

    LIGHTWEIGHT_FORCE_INLINE SqlSearchCondition& SearchCondition() noexcept
    {
        return this->_query.searchCondition;
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE SqlQueryFormatter const& Formatter() const noexcept
    {
        return _formatter;
    }

  protected:
    LIGHTWEIGHT_FORCE_INLINE explicit SqlCoreDataMapperQueryBuilder(SqlStatement& stmt, std::string fields) noexcept:
        _stmt { stmt },
        _formatter { stmt.Connection().QueryFormatter() },
        _fields { std::move(fields) }
    {
    }

  public:
    [[nodiscard]] std::vector<Record> All()
    {
        auto records = std::vector<Record> {};
        _stmt.ExecuteDirect(_formatter.SelectAll(this->_query.distinct,
                                                 _fields,
                                                 RecordTableName<Record>,
                                                 this->_query.searchCondition.tableAlias,
                                                 this->_query.searchCondition.tableJoins,
                                                 this->_query.searchCondition.condition,
                                                 this->_query.orderBy,
                                                 this->_query.groupBy));
        Derived::ReadResults(_stmt.Connection().ServerType(), _stmt.GetResultCursor(), &records);
        return records;
    }

    [[nodiscard]] std::optional<Record> First()
    {
        std::optional<Record> record {};
        _stmt.ExecuteDirect(_formatter.SelectFirst(this->_query.distinct,
                                                   _fields,
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition,
                                                   this->_query.orderBy,
                                                   1));
        Derived::ReadResult(_stmt.Connection().ServerType(), _stmt.GetResultCursor(), &record);
        return record;
    }

    [[nodiscard]] std::vector<Record> First(size_t n)
    {
        auto records = std::vector<Record> {};
        records.reserve(n);
        _stmt.ExecuteDirect(_formatter.SelectFirst(this->_query.distinct,
                                                   _fields,
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition,
                                                   this->_query.orderBy,
                                                   n));
        Derived::ReadResults(_stmt.Connection().ServerType(), _stmt.GetResultCursor(), &records);
        return records;
    }

    [[nodiscard]] std::vector<Record> Range(size_t offset, size_t limit)
    {
        auto records = std::vector<Record> {};
        records.reserve(limit);
        _stmt.ExecuteDirect(_formatter.SelectRange(
            this->_query.distinct,
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
        Derived::ReadResults(_stmt.Connection().ServerType(), _stmt.GetResultCursor(), &records);
        return records;
    }
};

/// @brief Represents a query builder that retrieves only the fields specified.
///
/// @ingroup DataMapper
template <typename Record, auto... ReferencedFields>
class [[nodiscard]] SqlSparseFieldQueryBuilder final:
    public SqlCoreDataMapperQueryBuilder<Record, SqlSparseFieldQueryBuilder<Record, ReferencedFields...>>
{
  private:
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<Record, SqlSparseFieldQueryBuilder<Record, ReferencedFields...>>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlSparseFieldQueryBuilder(SqlStatement& stmt, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<Record, SqlSparseFieldQueryBuilder<Record, ReferencedFields...>> { stmt,
                                                                                                         std::move(fields) }
    {
    }

    // NB: Required by SqlCoreDataMapperQueryBuilder:

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<Record>* records)
    {
        while (true)
        {
            auto& record = records->emplace_back();
            if (!ReadResultImpl(sqlServerType, reader, record))
            {
                records->pop_back();
                break;
            }
        }
    }

    static void ReadResult(SqlServerType sqlServerType, SqlResultCursor reader, std::optional<Record>* optionalRecord)
    {
        auto& record = optionalRecord->emplace();
        if (!ReadResultImpl(sqlServerType, reader, record))
            optionalRecord->reset();
    }

    static bool ReadResultImpl(SqlServerType sqlServerType, SqlResultCursor& reader, Record& record)
    {
        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns(sqlServerType, record);
        if (outputColumnsBound)
            reader.BindOutputColumns(&(record.*ReferencedFields)...);

        if (!reader.FetchRow())
            return false;

        if (!outputColumnsBound)
            detail::GetAllColumns(reader, record);

        return true;
    }
};

/// @brief Represents a query builder that retrieves all fields of a record.
///
/// @ingroup DataMapper
template <typename Record>
class [[nodiscard]] SqlAllFieldsQueryBuilder final:
    public SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record>>
{
  private:
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record>>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(SqlStatement& stmt, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record>> { stmt, std::move(fields) }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<Record>* records)
    {
        while (true)
        {
            Record& record = records->emplace_back();
            if (!ReadResultImpl(sqlServerType, reader, record))
            {
                records->pop_back();
                break;
            }
        }
    }

    static void ReadResult(SqlServerType sqlServerType, SqlResultCursor reader, std::optional<Record>* optionalRecord)
    {
        Record& record = optionalRecord->emplace();
        if (!ReadResultImpl(sqlServerType, reader, record))
            optionalRecord->reset();
    }

    static bool ReadResultImpl(SqlServerType sqlServerType, SqlResultCursor& reader, Record& record)
    {
        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns(sqlServerType, record);
        if (outputColumnsBound)
            detail::BindAllOutputColumns(reader, record);

        if (!reader.FetchRow())
            return false;

        if (!outputColumnsBound)
            detail::GetAllColumns(reader, record);

        return true;
    }
};

/// @brief Specialization of SqlAllFieldsQueryBuilder for the case when we return std::tuple
/// of two records
///
/// @ingroup DataMapper
template <typename FirstRecord, typename SecondRecord>
class [[nodiscard]] SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>> final:
    public SqlCoreDataMapperQueryBuilder<std::tuple<FirstRecord, SecondRecord>,
                                         SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>>>
{
  private:
    using RecordType = std::tuple<FirstRecord, SecondRecord>;
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType>>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(SqlStatement& stmt, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType>> { stmt, std::move(fields) }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records)
    {
        while (true)
        {
            auto& record = records->emplace_back();
            auto& [firstRecord, secondRecord] = record;

            auto const outputColumnsBoundFirst = detail::CanSafelyBindOutputColumns(sqlServerType, firstRecord);
            auto const outputColumnsBoundSecond = detail::CanSafelyBindOutputColumns(sqlServerType, secondRecord);
            if (outputColumnsBoundFirst && outputColumnsBoundSecond)
            {
                detail::BindAllOutputColumnsWithOffset(reader, firstRecord, 1);
                detail::BindAllOutputColumnsWithOffset(reader, secondRecord,  1 + Reflection::CountMembers<FirstRecord>);
            }

            if (!reader.FetchRow())
            {
                records->pop_back();
                break;
            }

            if (!(outputColumnsBoundFirst && outputColumnsBoundSecond))
                detail::GetAllColumns(reader, record);
        }
    }
};

/// @brief Represents a query builder that retrieves only the first record found.
///
/// @see DataMapper::QuerySingle()
///
/// @ingroup DataMapper
template <typename Record>
class [[nodiscard]] SqlQuerySingleBuilder: public SqlWhereClauseBuilder<SqlQuerySingleBuilder<Record>>
{
  private:
    SqlStatement& _stmt;
    SqlQueryFormatter const& _formatter;

    std::string _fields;
    SqlSearchCondition _searchCondition {};

    friend class DataMapper;
    friend class SqlWhereClauseBuilder<SqlQuerySingleBuilder<Record>>;

    LIGHTWEIGHT_FORCE_INLINE SqlSearchCondition& SearchCondition() noexcept
    {
        return _searchCondition;
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE SqlQueryFormatter const& Formatter() const noexcept
    {
        return _formatter;
    }

  protected:
    LIGHTWEIGHT_FORCE_INLINE explicit SqlQuerySingleBuilder(SqlStatement& stmt, std::string fields) noexcept:
        _stmt { stmt },
        _formatter { stmt.Connection().QueryFormatter() },
        _fields { std::move(fields) }
    {
    }

  public:
    /// @brief Executes the query and returns the first record found.
    [[nodiscard]] std::optional<Record> Get()
    {
        auto constexpr count = 1;
        auto constexpr distinct = false;
        auto constexpr orderBy = std::string_view {};
        _stmt.ExecuteDirect(_formatter.SelectFirst(distinct,
                                                   _fields,
                                                   RecordTableName<Record>,
                                                   _searchCondition.tableAlias,
                                                   _searchCondition.tableJoins,
                                                   _searchCondition.condition,
                                                   orderBy,
                                                   count));
        auto reader = _stmt.GetResultCursor();
        auto record = Record {};
        auto canBindOutputColumns = detail::CanSafelyBindOutputColumns(_stmt.Connection().ServerType(), record);
        if (canBindOutputColumns)
            detail::BindAllOutputColumns(reader, record);
        if (!reader.FetchRow())
            return std::nullopt;
        if (!canBindOutputColumns)
            detail::GetAllColumns(reader, record);
        return std::optional { std::move(record) };
    }
};

/// Returns the first primary key field of the record.
///
/// @ingroup DataMapper
template <typename Record>
inline LIGHTWEIGHT_FORCE_INLINE RecordPrimaryKeyType<Record> GetPrimaryKeyField(Record const& record) noexcept
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(HasPrimaryKey<Record>, "Record must have a primary key");

    auto result = RecordPrimaryKeyType<Record> {};
    Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType const& field) {
        if constexpr (IsPrimaryKey<FieldType> && std::same_as<FieldType, RecordPrimaryKeyType<Record>>)
        {
            result = field;
        }
    });
    return result;
}

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
    RecordPrimaryKeyType<Record> Create(Record& record);

    /// @brief Creates a new record in the database.
    ///
    /// @note This is a variation of the Create() method and does not update the record's primary key.
    ///
    /// @return The primary key of the newly created record.
    template <typename Record>
    RecordPrimaryKeyType<Record> CreateExplicit(Record const& record);

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

    /// @brief Queries a single record (based on primary key) from the database without auto-loading relations.
    ///
    /// The primary key(s) are used to identify the record to load.
    ///
    /// Main goal of this function is to load record without relationships to
    /// decrease compilation time and work around some limitations of template instantiation
    /// depth on MSVC compiler.
    template <typename Record, typename... PrimaryKeyTypes>
    std::optional<Record> QuerySingleWithoutRelationAutoLoading(PrimaryKeyTypes&&... primaryKeys);

    /// @brief Queries a single record from the database.
    ///
    /// @return A query builder for the given Record type that will also allow executing the query.
    ///
    /// @code
    /// auto const record = dm.QuerySingle<Person>(personId)
    ///                       .Where(FieldNameOf<&Person::id>, "=", 42)
    ///                       .Get();
    /// if (record.has_value())
    ///     std::println("Person: {}", DataMapper::Inspect(record.value()));
    /// @endcode
    template <typename Record>
    SqlQuerySingleBuilder<Record> QuerySingle()
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

        return SqlQuerySingleBuilder<Record>(_stmt, std::move(fields));
    }

    /// @brief Queries a single record by the given column name and value.
    ///
    /// @param columnName The name of the column to search.
    /// @param value The value to search for.
    /// @return The record if found, otherwise std::nullopt.
    template <typename Record, typename ColumnName, typename T>
    std::optional<Record> QuerySingleBy(ColumnName const& columnName, T const& value);

    /// Queries multiple records from the database, based on the given query.
    template <typename Record, typename... InputParameters>
    std::vector<Record> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters);

    /// Queries multiple records from the database, based on the given query.
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
    /// this function is uset to get result of the
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
    ///             .Select()
    ///             .Fields<JointA, JointC>()
    ///             .InnerJoin<&JointB::a_id, &JointA::id>()
    ///             .InnerJoin<&JointC::id, &JointB::c_id>()
    ///             .All();
    /// auto const records = dm.Query<JointA, JointC>(query);
    /// for(const auto [elementA, elementC] : records)
    /// {
    ///   // do something with elementA and elementC
    /// }
    template <typename FirstRecord, typename NextRecord, typename... InputParameters>
        requires DataMapperRecord<FirstRecord> && DataMapperRecord<NextRecord>
    std::vector<std::tuple<FirstRecord, NextRecord>> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                                                           InputParameters&&... inputParameters);

    /// Similar to previous one but quiery is builded from the object return by the quiery
    template <typename FirstRecord, typename NextRecord>
        requires DataMapperRecord<FirstRecord> && DataMapperRecord<NextRecord>
    SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, NextRecord>> Query()
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

        return SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, NextRecord>>(_stmt, std::move(fields));
    }

    /// Queries records of given Record type.
    ///
    /// @returns A query builder for the given Record type. The query builder can be used to further refine the
    /// query.
    ///          The query builder will execute the query when a method like All(), First(n), etc. is called.
    ///
    /// @code
    /// auto const records = dm.Query<Person>()
    ///                        .Where(FieldNameOf<&Person::is_active>, "=", true)
    ///                        .All();
    /// @endcode
    template <typename Record>
    SqlAllFieldsQueryBuilder<Record> Query()
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

        return SqlAllFieldsQueryBuilder<Record>(_stmt, std::move(fields));
    }

    /// Queries select fields from the given Record type.
    ///
    /// The fields are given in form of &Record::field1, &Record::field2, ...
    ///
    /// @returns A query builder for the given Record type. The query builder can be used to further refine the query.
    ///          The query builder will execute the query when a method like All(), First(n), etc. is called.
    ///
    /// @code
    /// auto const records = dm.QuerySparse<Person, &Person::id, &Person::name, &Person::age>()
    ///                        .Where(FieldNameOf<&Person::is_active>, "=", true)
    ///                        .All();
    /// @endcode
    template <typename Record, auto... ReferencedFields>
    SqlSparseFieldQueryBuilder<Record, ReferencedFields...> QuerySparse()
    {
        auto const appendFieldTo = []<auto ReferencedField>(std::string& fields) {
            using ReferencedRecord = Reflection::MemberClassType<ReferencedField>;
            if (!fields.empty())
                fields += ", ";
            fields += '"';
            fields += RecordTableName<ReferencedRecord>;
            fields += "\".\"";
            fields += FieldNameOf<ReferencedField>;
            fields += '"';
        };
        std::string fields;
        (appendFieldTo.template operator()<ReferencedFields>(fields), ...);

        return SqlSparseFieldQueryBuilder<Record, ReferencedFields...>(_stmt, std::move(fields));
    }

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
    auto BuildQuery() -> SqlQueryBuilder
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

    /// Configures the auto loading of relations for the given record.
    ///
    /// This means, that no explicit loading of relations is required.
    /// The relations are automatically loaded when accessed.
    template <typename Record>
    void ConfigureRelationAutoLoading(Record& record);

  private:
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

    template <auto ReferencedRecordField, auto BelongsToAlias>
    void LoadBelongsTo(BelongsTo<ReferencedRecordField, BelongsToAlias>& field);

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
            str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value.Value());
        else if constexpr (!IsHasMany<Value> && !IsHasManyThrough<Value> && !IsHasOneThrough<Value> && !IsBelongsTo<Value>)
            str += std::format("{} {} := {}", Reflection::TypeNameOf<Value>, name, value);
    });
    return "{" + std::move(str) + "}";
}

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
                createTable.PrimaryKeyWithAutoIncrement(std::string(FieldNameAt<I, Record>),
                                                        SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
            else if constexpr (FieldType::IsPrimaryKey)
                createTable.PrimaryKey(std::string(FieldNameAt<I, Record>),
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
                    std::string(FieldNameAt<I, Record>),
                    SqlColumnTypeDefinitionOf<typename FieldType::ValueType>,
                    SqlForeignKeyReferenceDefinition {
                        .tableName = std::string { RecordTableName<typename FieldType::ReferencedRecord> },
                        .columnName =
                            std::string { FieldNameAt<referencedFieldIndex, typename FieldType::ReferencedRecord> } });
            }
            else if constexpr (FieldType::IsMandatory)
                createTable.RequiredColumn(std::string(FieldNameAt<I, Record>),
                                           SqlColumnTypeDefinitionOf<typename FieldType::ValueType>);
            else
                createTable.Column(std::string(FieldNameAt<I, Record>),
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

template <typename Record>
RecordPrimaryKeyType<Record> DataMapper::CreateExplicit(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    auto query = _connection.Query(RecordTableName<Record>).Insert(nullptr);

    Reflection::EnumerateMembers(record, [&query]<auto I, typename FieldType>(FieldType const& /*field*/) {
        if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
    });

    _stmt.Prepare(query);

    Reflection::CallOnMembers(
        record,
        [this, i = SQLSMALLINT { 1 }]<typename Name, typename FieldType>(Name const& name, FieldType const& field) mutable {
            if constexpr (SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>)
                _stmt.BindInputParameter(i++, field, name);
        });

    _stmt.Execute();

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        return { _stmt.LastInsertId(RecordTableName<Record>) };
    else if constexpr (HasPrimaryKey<Record>)
    {
        RecordPrimaryKeyType<Record> const* primaryKey = nullptr;
        Reflection::EnumerateMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
            if constexpr (IsField<FieldType>)
            {
                if constexpr (FieldType::IsPrimaryKey)
                {
                    primaryKey = &field.Value();
                }
            }
        });
        return *primaryKey;
    }
}

template <typename Record>
RecordPrimaryKeyType<Record> DataMapper::Create(Record& record)
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
                                    FieldNameAt<PrimaryKeyIndex, Record>,
                                    RecordTableName<Record>));
                    primaryKeyField = maxId.value_or(ValueType {}) + 1;
                }
            }
        }
    });

    CreateExplicit(record);

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        SetId(record, _stmt.LastInsertId(RecordTableName<Record>));

    ClearModifiedState(record);
    ConfigureRelationAutoLoading(record);

    if constexpr (HasPrimaryKey<Record>)
        return GetPrimaryKeyField(record);
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

    Reflection::CallOnMembersWithoutName(record, [&query]<size_t I, typename FieldType>(FieldType const& field) {
        if (field.IsModified())
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
        // for some reason compiler do not want to properly deduce FieldType, so here we
        // directly infer the type from the Record type and index
        if constexpr (IsPrimaryKey<Reflection::MemberTypeOf<I, Record>>)
            std::ignore = query.Where(FieldNameAt<I, Record>, SqlWildcard);
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

    Reflection::CallOnMembers(record,
                              [&query]<typename Name, typename FieldType>(Name const& name, FieldType const& /*field*/) {
                                  if constexpr (FieldType::IsPrimaryKey)
                                      std::ignore = query.Where(name, SqlWildcard);
                              });

    _stmt.Prepare(query);

    // Bind the WHERE clause
    Reflection::CallOnMembers(
        record,
        [this, i = SQLSMALLINT { 1 }]<typename Name, typename FieldType>(Name const& name, FieldType const& field) mutable {
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

    auto resultRecord = Record {};
#if 0
    BindOutputColumns(resultRecord);

    if (!_stmt.FetchRow())
        return std::nullopt;
#else
    auto reader = _stmt.GetResultCursor();
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns(_stmt.Connection().ServerType(), resultRecord);
    if (outputColumnsBound)
        detail::BindAllOutputColumns(reader, resultRecord);

    if (!reader.FetchRow())
        return std::nullopt;

    if (!outputColumnsBound)
        detail::GetAllColumns(reader, resultRecord);
#endif

    ConfigureRelationAutoLoading(resultRecord);

    return { std::move(resultRecord) };
}

template <typename Record, typename... PrimaryKeyTypes>
std::optional<Record> DataMapper::QuerySingle(PrimaryKeyTypes&&... primaryKeys)
{
    auto record = QuerySingleWithoutRelationAutoLoading<Record>(std::forward<PrimaryKeyTypes>(primaryKeys)...);
    if (record)
        ConfigureRelationAutoLoading(*record);
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

        _stmt.Prepare(sqlQueryString);
        _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

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
    }

    return result;
}

template <typename FirstRecord, typename SecondRecord, typename... InputParameters>
    requires DataMapperRecord<FirstRecord> && DataMapperRecord<SecondRecord>
std::vector<std::tuple<FirstRecord, SecondRecord>> DataMapper::Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                                                                     InputParameters&&... inputParameters)
{
    auto result = std::vector<std::tuple<FirstRecord, SecondRecord>> {};

    _stmt.Prepare(selectQuery.ToSql());
    _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

    auto const ConfigureFetchAndBind = [this](auto& record) {
        auto& [recordFirst, recordSecond] = record;
        // clang-cl gives false possitive error that *this*
        // is not used in the lambda, to avoid the warning,
        // use it here explicitly
        this->BindOutputColumns<FirstRecord, 1>(recordFirst);
        this->BindOutputColumns<SecondRecord, Reflection::CountMembers<FirstRecord> + 1>(recordSecond);
        this->ConfigureRelationAutoLoading(recordFirst);
        this->ConfigureRelationAutoLoading(recordSecond);
    };

    ConfigureFetchAndBind(result.emplace_back());
    while (_stmt.FetchRow())
        ConfigureFetchAndBind(result.emplace_back());

    // remove the last empty record
    if (!result.empty())
        result.pop_back();

    return result;
}

template <typename ElementMask, typename Record, typename... InputParameters>
std::vector<Record> DataMapper::Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                                      InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    _stmt.Prepare(selectQuery.ToSql());
    _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

    auto result = std::vector<Record> {};

    ConfigureRelationAutoLoading(BindOutputColumns<ElementMask>(result.emplace_back()));
    while (_stmt.FetchRow())
        ConfigureRelationAutoLoading(BindOutputColumns<ElementMask>(result.emplace_back()));

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

template <auto ReferencedRecordField, auto BelongsToAlias>
void DataMapper::LoadBelongsTo(BelongsTo<ReferencedRecordField, BelongsToAlias>& field)
{
    using FieldType = BelongsTo<ReferencedRecordField>;
    using ReferencedRecord = typename FieldType::ReferencedRecord;

    CallOnPrimaryKey<ReferencedRecord>([&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>() {
        if (auto result = QuerySingle<ReferencedRecord>(field.Value()); result)
            field.EmplaceRecord() = std::move(*result);
        else
            SqlLogger::GetLogger().OnWarning(
                std::format("Loading BelongsTo failed for {} ({})", RecordTableName<ReferencedRecord>, field.Value()));
    });
}

template <size_t FieldIndex, typename Record, typename OtherRecord, typename Callable>
void DataMapper::CallOnHasMany(Record& record, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    using FieldType = HasMany<OtherRecord>;
    using ReferencedRecord = typename FieldType::ReferencedRecord;

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

    Reflection::EnumerateMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            LoadBelongsTo(field);
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

    Reflection::EnumerateMembers<ElementMask>(
        record, [stmt, i = SQLSMALLINT { InitialOffset }]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                stmt->BindOutputColumn(i++, &field.MutableValue());
            }
            else if constexpr (SqlOutputColumnBinder<Field>)
            {
                stmt->BindOutputColumn(i++, &field);
            }
        });

    return record;
}

template <typename Record>
void DataMapper::ConfigureRelationAutoLoading(Record& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    Reflection::EnumerateMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
        if constexpr (IsBelongsTo<FieldType>)
        {
            field.SetAutoLoader(typename FieldType::Loader {
                .loadReference = [this, &field]() { LoadBelongsTo(field); },
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
