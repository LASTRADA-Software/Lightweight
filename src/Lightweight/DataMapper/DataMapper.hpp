// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Async/Backend.hpp"
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
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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

    /// Move constructor.
    DataMapper(DataMapper&& other) noexcept:
        _connection(std::move(other._connection)),
        _stmt(_connection)
    {
        other._stmt = SqlStatement(std::nullopt);
    }

    /// Move assignment operator.
    DataMapper& operator=(DataMapper&& other) noexcept
    {
        if (this == &other)
            return *this;

        _connection = std::move(other._connection);
        _stmt = SqlStatement(_connection);
        other._stmt = SqlStatement(std::nullopt);

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
    /// @tparam QueryOptions A specialization of DataMapperOptions that controls query behavior.
    /// @tparam Record       The record type to insert.
    /// @param record        The record to insert. The primary key field is updated in-place after the insert.
    /// @return The primary key of the newly created record.
    template <DataMapperOptions QueryOptions = {}, typename Record>
    RecordPrimaryKeyType<Record> Create(Record& record);

    /// @brief Creates a new record in the database.
    ///
    /// @note This is a variation of the Create() method and does not update the record's primary key.
    ///
    /// @tparam Record The record type to insert.
    /// @param record  The record to insert. Unlike Create(), the primary key field is NOT updated in-place.
    /// @return The primary key of the newly created record.
    template <typename Record>
    RecordPrimaryKeyType<Record> CreateExplicit(Record const& record);

    /// @brief Batch-inserts a span of records with a single prepared statement.
    ///
    /// The INSERT is prepared once and the whole batch is submitted via
    /// SqlStatement::ExecuteBatch(rows, accessors...), which uses native zero-copy row-wise array
    /// binding when every inserted column is row-bindable (primitives, date/time/datetime, numeric, or
    /// std::optional of a fixed non-numeric type) and the driver supports parameter arrays, otherwise a
    /// prepare-once + per-row execute. This is dramatically faster than calling CreateExplicit() in a
    /// loop (which re-prepares per row).
    ///
    /// @note Like CreateExplicit(), this does not write back primary keys, relations, or modified-state
    /// onto the records; callers should treat the inserted records as write-only inputs. Auto-increment
    /// primary keys are not retrieved.
    ///
    /// Accepts any contiguous, sized range of records (e.g. std::vector, std::array, std::span, or a C
    /// array), so `dm.CreateAll(records)` works without an explicit std::span wrapper. Non-contiguous
    /// ranges are rejected at compile time via static_assert (no implicit copy is made).
    ///
    /// @tparam Records A contiguous range whose element type is the record type to insert.
    /// @param records The records to insert. An empty range is a no-op.
    template <std::ranges::range Records>
    void CreateAll(Records const& records);

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
    ///
    /// @tparam Record       The record type to query and materialize.
    /// @tparam QueryOptions A specialization of DataMapperOptions that controls query behavior,
    ///                      such as whether related records should be auto-loaded. For example,
    ///                      set the relation loading option to false to disable auto-loading of
    ///                      relations when reading a single record.
    /// @tparam PrimaryKeyTypes The type(s) of the primary key value(s) used to look up the record.
    /// @param primaryKeys   The primary key value(s) identifying the record to load.
    /// @return              An initialized Record if found; otherwise std::nullopt.
    ///
    /// @code
    /// // Example: disable auto-loading of relations when querying a single record
    /// auto result = dataMapper
    ///     .QuerySingle<MyRecord, DataMapperOptions{ .loadRelations = false }>(primaryKeyValue);
    /// if (result)
    /// {
    ///     // use *result; relations have not been auto-loaded
    /// }
    /// @endcode
    template <typename Record, DataMapperOptions QueryOptions = {}, typename... PrimaryKeyTypes>
    std::optional<Record> QuerySingle(PrimaryKeyTypes&&... primaryKeys);

    /// Queries multiple records from the database, based on the given query.
    ///
    /// @tparam Record          The record type to query and materialize.
    /// @tparam QueryOptions    A specialization of DataMapperOptions that controls query behavior.
    /// @tparam InputParameters The types of the input parameters to bind before executing the query.
    /// @param selectQuery      The composed SQL select query to execute.
    /// @param inputParameters  Zero or more values to bind as positional parameters in the query.
    /// @return A vector of records populated from the query results.
    template <typename Record, DataMapperOptions QueryOptions = {}, typename... InputParameters>
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
    template <typename Record, DataMapperOptions QueryOptions = {}, typename... InputParameters>
    std::vector<Record> Query(std::string_view sqlQueryString, InputParameters&&... inputParameters);

    /// Queries records from the database, based on the given query and can be used to retrieve only part of the record
    /// by specifying the ElementMask.
    ///
    /// @tparam ElementMask     A SqlElements<Idx...> specialization specifying the zero-based field indices to populate.
    /// @tparam Record          The record type to query and materialize.
    /// @tparam QueryOptions    A specialization of DataMapperOptions that controls query behavior.
    /// @tparam InputParameters The types of the input parameters to bind before executing the query.
    /// @param selectQuery      The composed SQL select query to execute. Only the columns listed in the SELECT clause
    ///                         are bound; the remaining fields of Record are left at their default values.
    /// @param inputParameters  Zero or more values to bind as positional parameters in the query.
    /// @return A vector of partially populated records; only fields at the specified indices are filled in.
    ///
    /// @code
    ///
    /// struct Person
    /// {
    ///    Field<int> id;
    ///    Field<std::string> name;   // index 1
    ///    Field<std::string> email;
    ///    Field<std::string> phone;
    ///    Field<std::string> address;
    ///    Field<std::string> city;   // index 5
    ///    Field<std::string> country;
    /// };
    ///
    /// void example(DataMapper& dm)
    /// {
    ///     auto const query = dm.FromTable(RecordTableName<Person>)
    ///                          .Select()
    ///                          .Fields({ "name"sv, "city"sv })
    ///                          .All();
    ///     auto const infos = dm.Query<SqlElements<1, 5>, Person>(query);
    ///     for (auto const& info : infos)
    ///     {
    ///         // only info.name and info.city are populated
    ///     }
    /// }
    /// @endcode
    template <typename ElementMask, typename Record, DataMapperOptions QueryOptions = {}, typename... InputParameters>
    std::vector<Record> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters);

    /// Queries records of different types from the database, based on the given query.
    /// User can constructed query that selects columns from the multiple tables
    /// this function is used to get result of the query
    ///
    /// @tparam First        The first record type to materialize from each result row.
    /// @tparam Second       The second record type to materialize from each result row.
    /// @tparam Rest         Zero or more additional record types to materialize from each result row.
    /// @tparam QueryOptions A specialization of DataMapperOptions that controls query behavior.
    /// @param selectQuery   The composed SQL select query whose column list covers all fields of First, Second, and Rest.
    /// @return A vector of tuples, each containing one instance of every requested record type per result row.
    ///
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
    /// @endcode
    template <typename First, typename Second, typename... Rest, DataMapperOptions QueryOptions = {}>
        requires DataMapperRecord<First> && DataMapperRecord<Second> && DataMapperRecords<Rest...>
    std::vector<std::tuple<First, Second, Rest...>> Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery);

    /// Queries records of given Record type.
    ///
    /// The query builder can be used to further refine the query.
    /// The query builder will execute the query when a method like All(), First(n), etc. is called.
    ///
    /// @tparam Record       The record type to query and materialize.
    /// @tparam QueryOptions A specialization of DataMapperOptions that controls query behavior.
    /// @return A query builder for the given Record type.
    ///
    /// @code
    /// auto const records = dm.Query<Person>()
    ///                        .Where(FieldNameOf<&Person::is_active>, "=", true)
    ///                        .All();
    /// @endcode
    template <typename Record, DataMapperOptions QueryOptions = {}>
    SqlAllFieldsQueryBuilder<Record, QueryOptions> Query()
    {
        return SqlAllFieldsQueryBuilder<Record, QueryOptions>(*this, BuildFullyQualifiedFieldList<Record>());
    }

    /// Asynchronous counterpart of @c Query — returns an async query builder for @p Record.
    ///
    /// The builder offers the exact same fluent DSL (`Where`, `OrderBy`, `GroupBy`, joins, …) as the
    /// synchronous one; its finisher methods (`All()`, `First()`, `First(n)`, `Range()`, `Count()`,
    /// `Exist()`, `Delete()`) return an @c Async::Task instead of the plain result, to be @c co_await -ed.
    /// The connection must have been put into async mode via @c SqlConnection::EnableAsync first.
    ///
    /// @note The returned builder is a temporary; keep the whole chain in the @c co_await full-expression
    ///       (e.g. `co_await dm.QueryAsync<Person>().Where(...).All();`) so it outlives the awaited task.
    ///
    /// @tparam Record       The record type to query and materialize.
    /// @tparam QueryOptions A specialization of DataMapperOptions that controls query behavior.
    /// @return An asynchronous query builder for the given Record type.
    ///
    /// @code
    /// auto const records = co_await dm.QueryAsync<Person>()
    ///                                 .Where(FieldNameOf<&Person::is_active>, "=", true)
    ///                                 .All();
    /// @endcode
    template <typename Record, DataMapperOptions QueryOptions = {}>
    SqlAllFieldsQueryBuilder<Record, QueryOptions, SqlQueryExecutionMode::Asynchronous> QueryAsync()
    {
        return SqlAllFieldsQueryBuilder<Record, QueryOptions, SqlQueryExecutionMode::Asynchronous>(
            *this, BuildFullyQualifiedFieldList<Record>());
    }

    /// Returns a SqlQueryBuilder using the default query formatter.
    ///
    /// This can be used to build custom queries separately from the DataMapper
    /// and execute them via the DataMapper's typed Query() overloads that accept a SqlSelectQueryBuilder.
    ///
    /// @return A SqlQueryBuilder bound to the connection's query formatter.
    SqlQueryBuilder Query()
    {
        return SqlQueryBuilder(_connection.QueryFormatter());
    }

    /// Updates the record in the database.
    ///
    /// Only fields that have been modified since the record was last loaded or saved are written.
    /// Fields that were not changed are excluded from the UPDATE statement.
    ///
    /// @tparam Record The record type to update.
    /// @param record  The record to update. Only its modified fields are written to the database.
    template <typename Record>
    void Update(Record& record);

    /// @brief Batch-updates a span of records with a single prepared statement.
    ///
    /// One UPDATE is prepared that writes **all** storable non-primary-key columns of the record,
    /// matched on the primary key(s) (`UPDATE … SET <all non-PK columns> WHERE <pk> = ?`), and the whole
    /// batch is submitted via SqlStatement::ExecuteBatch(rows, accessors...) — natively row-wise when
    /// possible, otherwise prepare-once + per-row execute.
    ///
    /// @note Unlike Update(), which writes only the modified fields of a single record, this writes a
    /// uniform set of columns for every row, because a single prepared statement must bind the same
    /// columns for the whole batch. Per-row modified-state is therefore not consulted, and is not reset.
    ///
    /// Accepts any contiguous, sized range of records (see CreateAll), so `dm.UpdateAll(records)` works
    /// without an explicit std::span wrapper. Non-contiguous ranges are rejected at compile time.
    ///
    /// @tparam Records A contiguous range whose element type is the record type to update (with a primary key).
    /// @param records The records to update. An empty range is a no-op.
    template <std::ranges::range Records>
    void UpdateAll(Records const& records);

    /// Deletes the record from the database.
    ///
    /// The record is identified by its primary key(s). The row is removed from the backing table.
    ///
    /// @tparam Record The record type to delete.
    /// @param record  The record to delete. Its primary key field(s) identify the row to remove.
    /// @return The number of rows deleted (typically 1 if the record was found, 0 otherwise).
    template <typename Record>
    std::size_t Delete(Record const& record);

    /// Constructs an SQL query builder for the given table name.
    SqlQueryBuilder FromTable(std::string_view tableName)
    {
        return _connection.Query(tableName);
    }

    /// Checks if the record has any modified fields.
    ///
    /// @tparam Record The record type to inspect.
    /// @param record  The record to check.
    /// @return True if at least one field has been modified since the record was last loaded or saved.
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
    ///
    /// @tparam state  The target modified state for all fields (Modified or NotModified).
    /// @tparam Record The record type whose fields are to be updated.
    /// @param record  The record whose field modification flags are set to @p state.
    template <ModifiedState state, typename Record>
    void SetModifiedState(Record& record) noexcept;

    /// Loads all direct relations to this record.
    ///
    /// @tparam Record The record type whose relation fields are to be populated.
    /// @param record  The record whose BelongsTo, HasMany, HasOneThrough, and HasManyThrough fields are loaded.
    template <typename Record>
    void LoadRelations(Record& record);

    /// Configures the auto loading of relations for the given record.
    ///
    /// This means, that no explicit loading of relations is required.
    /// The relations are automatically loaded when accessed.
    ///
    /// @tparam Record The record type to configure auto-loading for.
    /// @param record  The record whose relation fields are set up to load lazily on first access.
    template <typename Record>
    void ConfigureRelationAutoLoading(Record& record);

    /// Helper function that allow to execute query directly via data mapper
    /// and get scalar result without need to create SqlStatement manually
    ///
    /// @tparam T             The scalar type of the expected result value.
    /// @param sqlQueryString The SQL query string to execute.
    /// @return The first column of the first result row cast to T, or std::nullopt if the query returns no rows.
    template <typename T>
    [[nodiscard]] std::optional<T> Execute(std::string_view sqlQueryString);

    // --------------------------------------------------------------------------------------------
    // Asynchronous (C++23 coroutine) API.
    //
    // Each method offloads its synchronous counterpart to the connection's async backend — a
    // worker thread, serialized per connection — and resumes the awaiting coroutine on the app's
    // resume scheduler. Call SqlConnection::EnableAsync(...) on the underlying connection (or use a
    // pool that stamps it) before invoking any of these. Definitions live in
    // Async/DataMapperAsync.hpp (included at the end of this header).
    //
    // Methods taking a Record& / Record const& capture the record BY REFERENCE, and dereference it
    // on a worker thread when the returned Task is awaited. The caller must keep the record alive —
    // and must not mutate or move it — for the entire duration of the co_await (i.e. until the
    // awaiting coroutine resumes), not merely until the call returns. Destroying, moving, or mutating
    // it before the co_await resumes is a use-after-free / data race. The idiomatic, safe form keeps
    // the whole expression in the co_await: `co_await dm.UpdateAsync(record);`.

    /// Asynchronously inserts @p record, updating its primary key in place. @see Create.
    template <DataMapperOptions QueryOptions = {}, typename Record>
    [[nodiscard]] Async::Task<RecordPrimaryKeyType<Record>> CreateAsync(Record& record);

    /// Asynchronously queries a single record by its primary key(s). @see QuerySingle.
    ///
    /// This is the asynchronous shorthand for a primary-key lookup; for anything else use the fluent
    /// builder returned by QueryAsync<Record>() (whose finishers also return an Async::Task). Note there
    /// is deliberately no QueryAsync(string)/QueryAsync(ComposedQuery) — that is what the builder is for.
    template <typename Record, DataMapperOptions QueryOptions = {}, typename... PrimaryKeyTypes>
    [[nodiscard]] Async::Task<std::optional<Record>> QuerySingleAsync(PrimaryKeyTypes... primaryKeys);

    /// Asynchronously updates @p record's modified fields. @see Update.
    template <typename Record>
    [[nodiscard]] Async::Task<void> UpdateAsync(Record& record);

    /// Asynchronously deletes @p record. @see Delete.
    template <typename Record>
    [[nodiscard]] Async::Task<std::size_t> DeleteAsync(Record const& record);

    /// Asynchronously loads @p record's relations. @see LoadRelations.
    template <typename Record>
    [[nodiscard]] Async::Task<void> LoadRelationsAsync(Record& record);

  private:
    /// Builds the comma-separated, fully-qualified (`"Table"."Column"`) field list for @p Record.
    ///
    /// Shared by @c Query and @c QueryAsync so the SELECT projection is produced in exactly one place.
    ///
    /// @tparam Record The record type whose members are enumerated.
    /// @return The field list usable as the projection of a SELECT statement.
    template <typename Record>
    [[nodiscard]] static std::string BuildFullyQualifiedFieldList()
    {
        std::string fields;
        EnumerateRecordMembers<Record>([&fields]<size_t I, typename Field>() {
            if (!fields.empty())
                fields += ", ";
            fields += '"';
            fields += RecordTableName<Record>;
            fields += "\".\"";
            fields += FieldNameAt<I, Record>;
            fields += '"';
        });
        return fields;
    }

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
    Record& BindOutputColumns(Record& record, SqlResultCursor& cursor);

    template <typename ElementMask, typename Record, size_t InitialOffset = 1>
    Record& BindOutputColumns(Record& record, SqlResultCursor& cursor);

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

    template <size_t FieldIndex, typename OtherRecord>
    SqlSelectQueryBuilder BuildHasManySelectQuery();

    template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename Callable>
    void CallOnHasManyThrough(Record& record, Callable const& callback);

    template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename PKValue, typename Callable>
    void CallOnHasManyThroughByPK(PKValue const& pkValue, Callable const& callback);

    template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename PKValue>
    std::shared_ptr<ReferencedRecord> LoadHasOneThroughByPK(PKValue const& pkValue);

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
        EnumerateRecordMembers<Record>([&result]<size_t I, typename Field>() {
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
        EnumerateRecordMembers(record, [reader = &reader, i = startOffset]<size_t I, typename Field>(Field& field) mutable {
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

    /// @brief Requested rows per SQLFetchScroll round-trip for the native row-wise fetch fast path. The
    /// statement clamps this to a memory budget, so it is an upper bound, not a guarantee.
    constexpr std::size_t kDefaultRowArrayFetchDepth = 1024;

    /// @brief Mutable-reference output accessor for member @p I that is a Field/BelongsTo: yields the
    /// field's mutable value so the row-wise fetch path binds the result column in place. The read-side
    /// counterpart of @ref FieldValueAccessor.
    template <std::size_t I>
    struct MutableFieldValueAccessor
    {
        template <typename Record>
        decltype(auto) operator()(Record& record) const
        {
            return GetRecordMemberAt<I>(record).MutableValue();
        }
    };

    /// @brief The mutable value type bound for member @p FieldType on the row-wise fetch path (the type
    /// the result column materializes into).
    template <typename FieldType>
    using RowWiseColumnValueType = std::remove_cvref_t<decltype(std::declval<FieldType&>().MutableValue())>;

    /// @return Whether @p FieldType maps to a result column on the bound-output path (Field, BelongsTo, or
    /// a directly-bindable member) — mirrors the classification in @ref BindAllOutputColumnsWithOffset.
    template <typename FieldType>
    constexpr bool RowWiseIsColumn()
    {
        return IsField<FieldType> || IsBelongsTo<FieldType> || SqlOutputColumnBinder<FieldType>;
    }

    /// @return Whether @p FieldType is acceptable on the row-wise fetch path: either it is not a result
    /// column (a relation member, which is not bound) or it is a column whose value type is
    /// @ref SqlRowWiseFetchableColumn. Directly-bindable non-Field members are conservatively rejected
    /// (their value would need a separate accessor shape) so such records fall back to the per-row path.
    template <typename FieldType>
    constexpr bool RowWiseColumnAcceptable()
    {
        if constexpr (IsField<FieldType> || IsBelongsTo<FieldType>)
            return SqlRowWiseFetchableColumn<RowWiseColumnValueType<FieldType>>;
        else if constexpr (SqlOutputColumnBinder<FieldType>)
            return false;
        else
            return true; // relation / non-column member: not bound, imposes no constraint
    }

    template <typename Record, std::size_t... Is>
    constexpr bool CanRowWiseFetchRecordImpl(std::index_sequence<Is...>)
    {
        // The row-strided indicator slots are addressed at i * sizeof(Record); they must stay SQLLEN
        // aligned, so sizeof(Record) must be a multiple of alignof(SQLLEN) (mirrors the write-side
        // indicatorAlignmentSatisfied precondition).
        return (sizeof(Record) % alignof(SQLLEN) == 0) && (RowWiseColumnAcceptable<RecordMemberTypeOf<Is, Record>>() && ...)
               && (RowWiseIsColumn<RecordMemberTypeOf<Is, Record>>() || ...);
    }

    /// @brief Whether @p Record can be materialized via the native row-wise array-fetch fast path: every
    /// result column is a Field/BelongsTo of a @ref SqlRowWiseFetchableColumn type, there is at least one
    /// column, and the record size keeps the row-strided indicators aligned. Records that fail this fall
    /// back to the per-row @c SQLFetch path, with identical results.
    template <typename Record>
    constexpr bool CanRowWiseFetchRecord()
    {
        return CanRowWiseFetchRecordImpl<Record>(std::make_index_sequence<RecordMemberCount<Record>> {});
    }

    /// Returns a one-element accessor tuple for member @p I when it is a bound result column, else an empty
    /// tuple — flattened via tuple_cat so the accessor pack matches the bound column set and order exactly.
    template <std::size_t I, typename Record>
    auto MakeOutputColumnAccessor()
    {
        using FieldType = RecordMemberTypeOf<I, Record>;
        if constexpr (IsField<FieldType> || IsBelongsTo<FieldType>)
            return std::tuple<MutableFieldValueAccessor<I>> {};
        else
            return std::tuple<> {};
    }

    /// @brief Materializes the whole result set into @p records via @ref SqlStatement::FetchAllRowWise,
    /// building one mutable value accessor per bound result column (same set and order as
    /// @ref BindAllOutputColumnsWithOffset). Precondition: @ref CanRowWiseFetchRecord<Record>().
    template <typename Record>
    void ReadAllRowWise(SqlResultCursor& reader, std::vector<Record>* records)
    {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            std::apply(
                [&](auto const&... accessors) {
                    reader.FetchAllRowWise(*records, kDefaultRowArrayFetchDepth, accessors...);
                },
                std::tuple_cat(MakeOutputColumnAccessor<Is, Record>()...));
        }(std::make_index_sequence<RecordMemberCount<Record>> {});
    }

    /// @return Whether @p FieldType is a result column whose value is a char fixed-capacity string (or a
    /// @c std::optional of one). Such columns are array-bound narrow (SQL_C_CHAR), which only round-trips
    /// byte-exact where @ref SqlConnection::RoundTripsNarrowTextByteExact holds.
    template <typename FieldType>
    constexpr bool ColumnIsNarrowFixedString()
    {
        if constexpr (IsField<FieldType> || IsBelongsTo<FieldType>)
        {
            using V = RowWiseColumnValueType<FieldType>;
            if constexpr (SqlIsStdOptional<V>)
                return IsSqlFixedString<typename V::value_type>;
            else
                return IsSqlFixedString<V>;
        }
        else
            return false;
    }

    template <typename Record, std::size_t... Is>
    constexpr bool RecordHasNarrowFixedStringColumnImpl(std::index_sequence<Is...>)
    {
        return (ColumnIsNarrowFixedString<RecordMemberTypeOf<Is, Record>>() || ...);
    }

    /// @brief Whether @p Record has any char fixed-capacity-string result column. Such records take the
    /// row-wise fetch fast path only on backends that round-trip narrow text byte-exact; elsewhere they
    /// fall back to the per-row (wide) path. See @ref SqlConnection::RoundTripsNarrowTextByteExact.
    template <typename Record>
    constexpr bool RecordHasNarrowFixedStringColumn()
    {
        return RecordHasNarrowFixedStringColumnImpl<Record>(std::make_index_sequence<RecordMemberCount<Record>> {});
    }

    /// @brief Whether @p Record may use the row-wise fetch fast path on @p serverType: it is row-wise
    /// fetchable, the driver supports row-array fetch, and any narrow fixed-string column round-trips
    /// byte-exact there. Single runtime gate composed from connection capabilities + the compile-time
    /// record shape, so business logic never branches on the server type directly.
    template <typename Record>
    bool CanRowWiseFetchOn(SqlServerType serverType)
    {
        if constexpr (!CanRowWiseFetchRecord<Record>())
            return false;
        else
            return SqlConnection::SupportsNativeRowArrayFetch(serverType)
                   && (!RecordHasNarrowFixedStringColumn<Record>()
                       || SqlConnection::RoundTripsNarrowTextByteExact(serverType));
    }

    // --- Two-record tuple (JOIN) fast path ----------------------------------------------------------

    /// @brief Mutable-reference output accessor for member @p I of the @p TupleIndex-th sub-record of a
    /// @c std::tuple result row; yields that field's mutable value so a JOIN result binds in place.
    template <std::size_t TupleIndex, std::size_t I>
    struct MutableTupleFieldAccessor
    {
        template <typename TupleType>
        decltype(auto) operator()(TupleType& row) const
        {
            return GetRecordMemberAt<I>(std::get<TupleIndex>(row)).MutableValue();
        }
    };

    template <typename First, typename Second, std::size_t... Fs, std::size_t... Ss>
    constexpr bool CanRowWiseFetchTupleImpl(std::index_sequence<Fs...>, std::index_sequence<Ss...>)
    {
        return (sizeof(std::tuple<First, Second>) % alignof(SQLLEN) == 0)
               && (RowWiseColumnAcceptable<RecordMemberTypeOf<Fs, First>>() && ...)
               && (RowWiseColumnAcceptable<RecordMemberTypeOf<Ss, Second>>() && ...)
               && ((RowWiseIsColumn<RecordMemberTypeOf<Fs, First>>() || ...)
                   || (RowWiseIsColumn<RecordMemberTypeOf<Ss, Second>>() || ...));
    }

    /// @brief Whether a @c std::tuple<First,Second> JOIN row can be materialized via the row-wise fetch
    /// fast path: both sub-records' columns are row-bindable and the combined row size keeps the
    /// row-strided indicators aligned.
    template <typename First, typename Second>
    constexpr bool CanRowWiseFetchTuple()
    {
        return CanRowWiseFetchTupleImpl<First, Second>(std::make_index_sequence<RecordMemberCount<First>> {},
                                                       std::make_index_sequence<RecordMemberCount<Second>> {});
    }

    /// @brief Whether a @c std::tuple<First,Second> JOIN row may use the row-wise fetch fast path on
    /// @p serverType (row-wise fetchable + driver supports row-array fetch + any narrow fixed-string
    /// column round-trips byte-exact there). The tuple counterpart of @ref CanRowWiseFetchOn.
    template <typename First, typename Second>
    bool CanRowWiseFetchTupleOn(SqlServerType serverType)
    {
        if constexpr (!CanRowWiseFetchTuple<First, Second>())
            return false;
        else
            return SqlConnection::SupportsNativeRowArrayFetch(serverType)
                   && ((!RecordHasNarrowFixedStringColumn<First>() && !RecordHasNarrowFixedStringColumn<Second>())
                       || SqlConnection::RoundTripsNarrowTextByteExact(serverType));
    }

    /// Accessor tuple for member @p I of the @p TupleIndex-th sub-record, or empty for non-columns.
    template <std::size_t TupleIndex, std::size_t I, typename SubRecord>
    auto MakeTupleColumnAccessor()
    {
        using FieldType = RecordMemberTypeOf<I, SubRecord>;
        if constexpr (IsField<FieldType> || IsBelongsTo<FieldType>)
            return std::tuple<MutableTupleFieldAccessor<TupleIndex, I>> {};
        else
            return std::tuple<> {};
    }

    /// @brief Materializes a two-record JOIN result set into @p records via row-wise array fetch. The
    /// accessor pack is First's columns followed by Second's, matching the column order of
    /// @ref BindAllOutputColumnsWithOffset's offset scheme. Precondition: @ref CanRowWiseFetchTuple.
    template <typename First, typename Second>
    void ReadAllRowWiseTuple(SqlResultCursor& reader, std::vector<std::tuple<First, Second>>* records)
    {
        [&]<std::size_t... Fs, std::size_t... Ss>(std::index_sequence<Fs...>, std::index_sequence<Ss...>) {
            std::apply(
                [&](auto const&... accessors) {
                    reader.FetchAllRowWise(*records, kDefaultRowArrayFetchDepth, accessors...);
                },
                std::tuple_cat(MakeTupleColumnAccessor<0, Fs, First>()..., MakeTupleColumnAccessor<1, Ss, Second>()...));
        }(std::make_index_sequence<RecordMemberCount<First>> {}, std::make_index_sequence<RecordMemberCount<Second>> {});
    }

    // when we iterate over all columns using element mask
    // indexes of the mask corresponds to the indexe of the field
    // inside the structure, not inside the SQL result set
    template <typename ElementMask, typename Record>
    void GetAllColumns(SqlResultCursor& reader, Record& record, SQLUSMALLINT indexFromQuery = 0)
    {
        EnumerateRecordMembers<ElementMask>(
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
        return GetAllColumns<std::make_integer_sequence<size_t, RecordMemberCount<Record>>, Record>(
            reader, record, indexFromQuery);
    }

    template <typename FirstRecord, typename SecondRecord>
    // TODO we need to remove this at some points and provide generic bindings for tuples
    void GetAllColumns(SqlResultCursor& reader, std::tuple<FirstRecord, SecondRecord>& record)
    {
        auto& [firstRecord, secondRecord] = record;

        EnumerateRecordMembers(firstRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
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

        EnumerateRecordMembers(secondRecord, [reader = &reader]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                if constexpr (Field::IsOptional)
                    field.MutableValue() = reader->GetNullableColumn<typename Field::ValueType::value_type>(
                        RecordMemberCount<FirstRecord> + I + 1);
                else
                    field.MutableValue() =
                        reader->GetColumn<typename Field::ValueType>(RecordMemberCount<FirstRecord> + I + 1);
            }
            else if constexpr (SqlGetColumnNativeType<Field>)
            {
                if constexpr (Field::IsOptional)
                    field = reader->GetNullableColumn<typename Field::BaseType>(RecordMemberCount<FirstRecord> + I + 1);
                else
                    field = reader->GetColumn<Field>(RecordMemberCount<FirstRecord> + I + 1);
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
template <typename Finisher>
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::RunFinisher(Finisher finisher)
{
    if constexpr (Derived::QueryExecution == SqlQueryExecutionMode::Asynchronous)
        return Async::RunAsync(_dm.Connection().AsyncBackend(), std::move(finisher));
    else
        return finisher();
}

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
size_t SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::CountImpl()
{
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectCount(this->_query.distinct,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition));
    auto reader = stmt.ExecuteWithVariants(_boundInputs);
    if (reader.FetchRow())
        return reader.GetColumn<size_t>(1);
    return 0;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
bool SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::ExistImpl()
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
    if (auto reader = stmt.ExecuteWithVariants(_boundInputs); reader.FetchRow())
        return true;
    return false;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
void SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::DeleteImpl()
{
    auto stmt = SqlStatement { _dm.Connection() };

    auto const query = _formatter.Delete(RecordTableName<Record>,
                                         this->_query.searchCondition.tableAlias,
                                         this->_query.searchCondition.tableJoins,
                                         this->_query.searchCondition.condition);

    stmt.Prepare(query);
    [[maybe_unused]] auto cursor = stmt.ExecuteWithVariants(_boundInputs);
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::AllImpl()
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
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.ExecuteWithVariants(_boundInputs), &records);
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
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::AllImpl() -> std::vector<ReferencedFieldTypeOf<Field>>
{
    using value_type = ReferencedFieldTypeOf<Field>;
    auto result = std::vector<value_type> {};

    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectAll(this->_query.distinct,
                                      detail::FullyQualifiedNamesOf<Field>,
                                      RecordTableName<Record>,
                                      this->_query.searchCondition.tableAlias,
                                      this->_query.searchCondition.tableJoins,
                                      this->_query.searchCondition.condition,
                                      this->_query.orderBy,
                                      this->_query.groupBy));
    auto reader = stmt.ExecuteWithVariants(_boundInputs);
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
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::AllImpl() -> std::vector<Record>
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };

    stmt.Prepare(_formatter.SelectAll(this->_query.distinct,
                                      detail::FullyQualifiedNamesOf<ReferencedFields...>,
                                      RecordTableName<Record>,
                                      this->_query.searchCondition.tableAlias,
                                      this->_query.searchCondition.tableJoins,
                                      this->_query.searchCondition.condition,
                                      this->_query.orderBy,
                                      this->_query.groupBy));

    auto reader = stmt.ExecuteWithVariants(_boundInputs);
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
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
std::optional<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::FirstImpl()
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
    Derived::ReadResult(stmt.Connection().ServerType(), stmt.ExecuteWithVariants(_boundInputs), &record);
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
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::FirstImpl() -> std::optional<ReferencedFieldTypeOf<Field>>
{
    auto constexpr count = 1;
    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        detail::FullyQualifiedNamesOf<Field>,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        count));
    if (auto reader = stmt.ExecuteWithVariants(_boundInputs); reader.FetchRow())
        return reader.template GetColumn<ReferencedFieldTypeOf<Field>>(1);
    return std::nullopt;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
    requires(sizeof...(ReferencedFields) >= 2)
auto SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::FirstImpl() -> std::optional<Record>
{
    auto optionalRecord = std::optional<Record> {};

    auto stmt = SqlStatement { _dm.Connection() };
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        detail::FullyQualifiedNamesOf<ReferencedFields...>,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        1));

    auto& record = optionalRecord.emplace();
    auto reader = stmt.ExecuteWithVariants(_boundInputs);
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
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::FirstImpl(size_t n)
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
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.ExecuteWithVariants(_boundInputs), &records);

    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }
    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::RangeImpl(size_t offset, size_t limit)
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
    Derived::ReadResults(stmt.Connection().ServerType(), stmt.ExecuteWithVariants(_boundInputs), &records);
    if constexpr (QueryOptions.loadRelations)
    {
        for (auto& record: records)
            _dm.ConfigureRelationAutoLoading(record);
    }
    return records;
}

template <typename Record, typename Derived, DataMapperOptions QueryOptions>
template <auto... ReferencedFields>
std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::RangeImpl(size_t offset, size_t limit)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(limit);
    stmt.Prepare(
        _formatter.SelectRange(this->_query.distinct,
                               detail::FullyQualifiedNamesOf<ReferencedFields...>,
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

    auto reader = stmt.ExecuteWithVariants(_boundInputs);
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
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
[[nodiscard]] std::vector<Record> SqlCoreDataMapperQueryBuilder<Record, Derived, QueryOptions>::FirstImpl(size_t n)
{
    auto records = std::vector<Record> {};
    auto stmt = SqlStatement { _dm.Connection() };
    records.reserve(n);
    stmt.Prepare(_formatter.SelectFirst(this->_query.distinct,
                                        detail::FullyQualifiedNamesOf<ReferencedFields...>,
                                        RecordTableName<Record>,
                                        this->_query.searchCondition.tableAlias,
                                        this->_query.searchCondition.tableJoins,
                                        this->_query.searchCondition.condition,
                                        this->_query.orderBy,
                                        n));

    auto reader = stmt.ExecuteWithVariants(_boundInputs);
    auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(stmt.Connection().ServerType());
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

template <typename Record, DataMapperOptions QueryOptions, SqlQueryExecutionMode Execution>
void SqlAllFieldsQueryBuilder<Record, QueryOptions, Execution>::ReadResults(SqlServerType sqlServerType,
                                                                            SqlResultCursor reader,
                                                                            std::vector<Record>* records)
{
    // Fast path: when every result column is a fixed-width row-bindable field and the driver honours
    // native row-array fetching, materialize the whole result set in row blocks (one SQLFetchScroll per
    // block) directly into records, instead of one SQLFetch round-trip per row. Results are identical to
    // the per-row path below; this only collapses ODBC round-trips (the win on high-latency links).
    if constexpr (detail::CanRowWiseFetchRecord<Record>())
    {
        if (detail::CanRowWiseFetchOn<Record>(sqlServerType))
        {
            detail::ReadAllRowWise(reader, records);
            return;
        }
    }

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

template <typename Record, DataMapperOptions QueryOptions, SqlQueryExecutionMode Execution>
void SqlAllFieldsQueryBuilder<Record, QueryOptions, Execution>::ReadResult(SqlServerType sqlServerType,
                                                                           SqlResultCursor reader,
                                                                           std::optional<Record>* optionalRecord)
{
    Record& record = optionalRecord->emplace();
    if (!detail::ReadSingleResult(sqlServerType, reader, record))
        optionalRecord->reset();
}

template <typename FirstRecord, typename SecondRecord, DataMapperOptions QueryOptions, SqlQueryExecutionMode Execution>
void SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions, Execution>::ReadResults(
    SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records)
{
    // Fast path: a JOIN row of two row-bindable records is bound row-wise over the tuple and fetched in
    // blocks (one SQLFetchScroll per block) instead of one SQLFetch per row. Identical results.
    if constexpr (detail::CanRowWiseFetchTuple<FirstRecord, SecondRecord>())
    {
        if (detail::CanRowWiseFetchTupleOn<FirstRecord, SecondRecord>(sqlServerType))
        {
            detail::ReadAllRowWiseTuple<FirstRecord, SecondRecord>(reader, records);
            return;
        }
    }

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
            detail::BindAllOutputColumnsWithOffset(reader, secondRecord, 1 + RecordMemberCount<FirstRecord>);
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

    ZoneScopedN("DataMapper::CreateTable");
    ZoneTextObject(RecordTableName<Record>);

    auto const sqlQueryStrings = CreateTableString<Record>(_connection.ServerType());
    for (auto const& sqlQueryString: sqlQueryStrings) [[maybe_unused]]
        auto cursor = _stmt.ExecuteDirect(sqlQueryString);
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
    EnumerateRecordMembers(
        record, [this, &result]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
            if constexpr (IsField<PrimaryKeyType> && IsPrimaryKey<PrimaryKeyType>
                          && detail::IsAutoAssignPrimaryKeyField<PrimaryKeyType>::value)
            {
                using ValueType = PrimaryKeyType::ValueType;
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
    EnumerateRecordMembers(record, [&query]<auto I, typename FieldType>(FieldType const& /*field*/) {
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
    [[maybe_unused]] auto cursor = _stmt.Execute();

    if constexpr (HasAutoIncrementPrimaryKey<Record>)
        return { _stmt.LastInsertId(RecordTableName<Record>) };
    else if constexpr (HasPrimaryKey<Record>)
    {
        if constexpr (UsePkOverride == PrimaryKeySource::Override)
            return *pkOverride; // NOLINT(bugprone-unchecked-optional-access)
        else
            return RecordPrimaryKeyOf(record).Value();
    }

    return {};
}

template <typename Record>
RecordPrimaryKeyType<Record> DataMapper::CreateExplicit(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    return CreateInternal<PrimaryKeySource::Record>(record);
}

namespace detail
{
    /// @brief Whether member field type @p FieldType is an insertable column for a batched CREATE
    /// (bindable and not an auto-increment primary key). Single source of truth shared by the INSERT
    /// column-list builder and the value-accessor builder, so the bound `?` count and the accessor count
    /// cannot drift apart.
    template <typename FieldType>
    constexpr bool IsBatchInsertColumn = SqlInputParameterBinder<FieldType> && !IsAutoIncrementPrimaryKey<FieldType>;

    /// @brief Whether @p FieldType is a SET column for a batched UPDATE (storable, non-primary-key).
    template <typename FieldType>
    constexpr bool IsBatchUpdateSetColumn = FieldWithStorage<FieldType> && !IsPrimaryKey<FieldType>;

    /// @brief Whether @p FieldType is a WHERE (key) column for a batched UPDATE (a primary key).
    template <typename FieldType>
    constexpr bool IsBatchUpdateWhereColumn = IsPrimaryKey<FieldType>;

    /// @brief Column accessor for batched DataMapper operations: maps a record to the value of its
    /// I-th member field, returning a reference so the native row-wise batch path binds it in place.
    template <std::size_t I>
    struct FieldValueAccessor
    {
        template <typename Record>
        decltype(auto) operator()(Record const& record) const
        {
            return GetRecordMemberAt<I>(record).Value();
        }
    };

    /// Returns a one-element accessor tuple for member I when it is an insertable column (bindable and
    /// not an auto-increment primary key), or an empty tuple otherwise — to be flattened via tuple_cat.
    template <std::size_t I, typename Record>
    auto MakeCreateColumnAccessor()
    {
        using FieldType = RecordMemberTypeOf<I, Record>;
        if constexpr (IsBatchInsertColumn<FieldType>)
            return std::tuple<FieldValueAccessor<I>> {};
        else
            return std::tuple<> {};
    }

    /// Accessor tuple for the SET clause of a batched UPDATE: storable, non-primary-key columns.
    template <std::size_t I, typename Record>
    auto MakeUpdateSetAccessor()
    {
        using FieldType = RecordMemberTypeOf<I, Record>;
        if constexpr (IsBatchUpdateSetColumn<FieldType>)
            return std::tuple<FieldValueAccessor<I>> {};
        else
            return std::tuple<> {};
    }

    /// Accessor tuple for the WHERE clause of a batched UPDATE: primary-key columns.
    template <std::size_t I, typename Record>
    auto MakeUpdateWhereAccessor()
    {
        using FieldType = RecordMemberTypeOf<I, Record>;
        if constexpr (IsBatchUpdateWhereColumn<FieldType>)
            return std::tuple<FieldValueAccessor<I>> {};
        else
            return std::tuple<> {};
    }
} // namespace detail

template <std::ranges::range Records>
void DataMapper::CreateAll(Records const& records)
{
    static_assert(std::ranges::contiguous_range<Records> && std::ranges::sized_range<Records>,
                  "CreateAll requires a contiguous, sized range of records (e.g. std::vector, std::array, "
                  "std::span, or a C array); native row-wise array binding needs the records laid out contiguously.");
    using Record = std::remove_cvref_t<std::ranges::range_value_t<Records>>;
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::CreateAll");
    ZoneTextObject(RecordTableName<Record>);

    if (std::ranges::empty(records))
        return;

    // Build the INSERT once, with the same column set and order as CreateInternal().
    auto query = _connection.Query(RecordTableName<Record>).Insert(nullptr);
    EnumerateRecordMembers<Record>([&query]<auto I, typename FieldType>() {
        if constexpr (detail::IsBatchInsertColumn<FieldType>)
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
    });
    _stmt.Prepare(query);

    // Build one value accessor per bound column (same filter/order) and submit the whole batch.
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        std::apply([&](auto const&... accessors) { std::ignore = _stmt.ExecuteBatch(records, accessors...); },
                   std::tuple_cat(detail::MakeCreateColumnAccessor<Is, Record>()...));
    }(std::make_index_sequence<RecordMemberCount<Record>> {});
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

    ZoneScopedN("DataMapper::Create");
    ZoneTextObject(RecordTableName<Record>);

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

    ZoneScopedN("DataMapper::Update");
    ZoneTextObject(RecordTableName<Record>);

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
    EnumerateRecordMembers(record, [&query]<size_t I, typename FieldType>(FieldType const& field) {
        if (field.IsModified())
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
        // for some reason compiler do not want to properly deduce FieldType, so here we
        // directly infer the type from the Record type and index
        if constexpr (IsPrimaryKey<RecordMemberTypeOf<I, Record>>)
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
    EnumerateRecordMembers(record, [this, &i]<size_t I, typename FieldType>(FieldType const& field) {
        if (field.IsModified())
            _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
    });

    // Bind the WHERE clause
    EnumerateRecordMembers(record, [this, &i]<size_t I, typename FieldType>(FieldType const& field) {
        if constexpr (IsPrimaryKey<RecordMemberTypeOf<I, Record>>)
            _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
    });
#endif

    [[maybe_unused]] auto cursor = _stmt.Execute();

    SetModifiedState<ModifiedState::NotModified>(record);
}

template <std::ranges::range Records>
void DataMapper::UpdateAll(Records const& records)
{
    static_assert(std::ranges::contiguous_range<Records> && std::ranges::sized_range<Records>,
                  "UpdateAll requires a contiguous, sized range of records (e.g. std::vector, std::array, "
                  "std::span, or a C array); native row-wise array binding needs the records laid out contiguously.");
    using Record = std::remove_cvref_t<std::ranges::range_value_t<Records>>;
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(HasPrimaryKey<Record>, "UpdateAll requires a record type with a primary key");

    ZoneScopedN("DataMapper::UpdateAll");
    ZoneTextObject(RecordTableName<Record>);

    if (std::ranges::empty(records))
        return;

    // Build one UPDATE that writes all storable non-primary-key columns, matched on the primary key(s).
    auto query = _connection.Query(RecordTableName<Record>).Update();
    EnumerateRecordMembers<Record>([&query]<auto I, typename FieldType>() {
        if constexpr (detail::IsBatchUpdateSetColumn<FieldType>)
            query.Set(FieldNameAt<I, Record>, SqlWildcard);
    });
    EnumerateRecordMembers<Record>([&query]<auto I, typename FieldType>() {
        if constexpr (detail::IsBatchUpdateWhereColumn<FieldType>)
            std::ignore = query.Where(FieldNameAt<I, Record>, SqlWildcard);
    });
    _stmt.Prepare(query);

    // Accessor order must match the SQL parameter order: SET columns first, then the WHERE key(s).
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        std::apply([&](auto const&... accessors) { std::ignore = _stmt.ExecuteBatch(records, accessors...); },
                   std::tuple_cat(detail::MakeUpdateSetAccessor<Is, Record>()...,
                                  detail::MakeUpdateWhereAccessor<Is, Record>()...));
    }(std::make_index_sequence<RecordMemberCount<Record>> {});
}

template <typename Record>
std::size_t DataMapper::Delete(Record const& record)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::Delete");
    ZoneTextObject(RecordTableName<Record>);

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
    EnumerateRecordMembers(record, [&query]<size_t I, typename FieldType>(FieldType const& /*field*/) {
        if constexpr (IsPrimaryKey<RecordMemberTypeOf<I, Record>>)
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
    EnumerateRecordMembers(record,
                           [this, i = SQLSMALLINT { 1 }]<size_t I, typename FieldType>(FieldType const& field) mutable {
                               if constexpr (IsPrimaryKey<RecordMemberTypeOf<I, Record>>)
                                   _stmt.BindInputParameter(i++, field.Value(), FieldNameAt<I, Record>);
                           });
#endif

    auto cursor = _stmt.Execute();

    return cursor.NumRowsAffected();
}

template <typename Record, DataMapperOptions QueryOptions, typename... PrimaryKeyTypes>
std::optional<Record> DataMapper::QuerySingle(PrimaryKeyTypes&&... primaryKeys)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::QuerySingle(PK)");
    ZoneTextObject(RecordTableName<Record>);

    // Starter doesn't expose finalizers / Where until at least one column is
    // projected. The reflection enumeration below is constexpr-conditional, so
    // which iteration adds the first column isn't known up front — promote on the
    // first storage field via the returned Builder&, then reuse that pointer.
    auto selectStarter = _connection.Query(RecordTableName<Record>).Select();
    SqlSelectQueryBuilder* queryBuilder = nullptr;
    EnumerateRecordMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
        {
            if (queryBuilder == nullptr)
                queryBuilder = &selectStarter.Field(FieldNameAt<I, Record>);
            else
                queryBuilder->Field(FieldNameAt<I, Record>);

            if constexpr (FieldType::IsPrimaryKey)
                std::ignore = queryBuilder->Where(FieldNameAt<I, Record>, SqlWildcard);
        }
    });

    _stmt.Prepare(queryBuilder->First());
    auto reader = _stmt.Execute(std::forward<PrimaryKeyTypes>(primaryKeys)...);

    auto resultRecord = std::optional<Record> { Record {} };
    if (!detail::ReadSingleResult(_stmt.Connection().ServerType(), reader, *resultRecord))
        return std::nullopt;

    if (resultRecord)
        SetModifiedState<ModifiedState::NotModified>(resultRecord.value());

    if constexpr (QueryOptions.loadRelations)
    {
        if (resultRecord)
            ConfigureRelationAutoLoading(*resultRecord);
    }

    return resultRecord;
}

template <typename Record, typename... Args>
std::optional<Record> DataMapper::QuerySingle(SqlSelectQueryBuilder selectQuery, Args&&... args)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::QuerySingle(Builder)");
    ZoneTextObject(RecordTableName<Record>);

    EnumerateRecordMembers<Record>([&]<size_t I, typename FieldType>() {
        if constexpr (FieldWithStorage<FieldType>)
            selectQuery.Field(SqlQualifiedTableColumnName { RecordTableName<Record>, FieldNameAt<I, Record> });
    });
    auto const composedSql = selectQuery.First().ToSql();
    ZoneTextObject(composedSql);
    _stmt.Prepare(composedSql);
    auto reader = _stmt.Execute(std::forward<Args>(args)...);

    auto resultRecord = std::optional<Record> { Record {} };
    if (!detail::ReadSingleResult(_stmt.Connection().ServerType(), reader, *resultRecord))
        return std::nullopt;

    if (resultRecord)
        SetModifiedState<ModifiedState::NotModified>(resultRecord.value());

    return resultRecord;
}

// TODO: Provide Query(QueryBuilder, ...) method variant

/// Queries multiple records from the database using a composed query and optional input parameters.
template <typename Record, DataMapperOptions QueryOptions, typename... InputParameters>
inline LIGHTWEIGHT_FORCE_INLINE std::vector<Record> DataMapper::Query(
    SqlSelectQueryBuilder::ComposedQuery const& selectQuery, InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record> || std::same_as<Record, SqlVariantRow>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::Query(ComposedQuery)");
    return Query<Record, QueryOptions>(selectQuery.ToSql(), std::forward<InputParameters>(inputParameters)...);
}

template <typename Record, DataMapperOptions QueryOptions, typename... InputParameters>
std::vector<Record> DataMapper::Query(std::string_view sqlQueryString, InputParameters&&... inputParameters)
{
    ZoneScopedN("DataMapper::Query(string)");
    ZoneTextObject(sqlQueryString);

    auto result = std::vector<Record> {};
    if constexpr (std::same_as<Record, SqlVariantRow>)
    {
        _stmt.Prepare(sqlQueryString);
        SqlResultCursor cursor = _stmt.Execute(std::forward<InputParameters>(inputParameters)...);
        size_t const numResultColumns = cursor.NumColumnsAffected();
        while (cursor.FetchRow())
        {
            auto& record = result.emplace_back();
            record.reserve(numResultColumns);
            for (auto const i: std::views::iota(1U, numResultColumns + 1))
                record.emplace_back(cursor.GetColumn<SqlVariant>(static_cast<SQLUSMALLINT>(i)));
        }
    }
    else
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

        bool const canSafelyBindOutputColumns = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());

        _stmt.Prepare(sqlQueryString);
        auto reader = _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

        for (;;)
        {
            auto& record = result.emplace_back();

            if (canSafelyBindOutputColumns)
                BindOutputColumns(record, reader);

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
            if constexpr (QueryOptions.loadRelations)
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

    ZoneScopedN("DataMapper::Query(ComposedQuery -> tuple)");
    auto const tupleSql = selectQuery.ToSql();
    ZoneTextObject(tupleSql);
    _stmt.Prepare(tupleSql);
    auto reader = _stmt.Execute();

    constexpr auto calculateOffset = []<size_t I, typename Tuple>() {
        size_t offset = 1;

        if constexpr (I > 0)
        {
            [&]<size_t... Indices>(std::index_sequence<Indices...>) {
                ((Indices < I ? (offset += RecordMemberCount<std::tuple_element_t<Indices, Tuple>>) : 0), ...);
            }(std::make_index_sequence<I> {});
        }
        return offset;
    };

    auto const BindElements = [&](auto& record) {
        Reflection::template_for<0, std::tuple_size_v<value_type>>([&]<auto I>() {
            using TupleElement = std::decay_t<std::tuple_element_t<I, value_type>>;
            auto& element = std::get<I>(record);
            constexpr size_t offset = calculateOffset.template operator()<I, value_type>();
            this->BindOutputColumns<TupleElement, offset>(element, reader);
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

template <typename ElementMask, typename Record, DataMapperOptions QueryOptions, typename... InputParameters>
std::vector<Record> DataMapper::Query(SqlSelectQueryBuilder::ComposedQuery const& selectQuery,
                                      InputParameters&&... inputParameters)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::Query(ComposedQuery, ElementMask)");
    auto const maskedSql = selectQuery.ToSql();
    ZoneTextObject(maskedSql);
    _stmt.Prepare(maskedSql);

    auto records = std::vector<Record> {};

    // TODO: We could optimize this further by only considering ElementMask fields in Record.
    bool const canSafelyBindOutputColumns = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());

    auto reader = _stmt.Execute(std::forward<InputParameters>(inputParameters)...);

    for (;;)
    {
        auto& record = records.emplace_back();

        if (canSafelyBindOutputColumns)
            BindOutputColumns<ElementMask>(record, reader);

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
        if constexpr (QueryOptions.loadRelations)
            ConfigureRelationAutoLoading(record);
    }

    return records;
}

template <DataMapper::ModifiedState state, typename Record>
void DataMapper::SetModifiedState(Record& record) noexcept
{
    static_assert(!std::is_const_v<Record>);
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    EnumerateRecordMembers(record, []<size_t I, typename FieldType>(FieldType& field) {
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

    EnumerateRecordMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
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

    EnumerateRecordMembers<Record>([&]<size_t I, typename FieldType>() {
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

    EnumerateRecordMembers<Record>([&]<size_t I, typename FieldType>() {
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

    ZoneScopedN("DataMapper::LoadBelongsTo");
    ZoneTextObject(RecordTableName<ReferencedRecord>);

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
                             EnumerateRecordMembers<ReferencedRecord>(
                                 [&]<size_t ReferencedFieldIndex, typename ReferencedFieldType>() {
                                     if constexpr (FieldWithStorage<ReferencedFieldType>)
                                     {
                                         query.Field(FieldNameAt<ReferencedFieldIndex, ReferencedRecord>);
                                     }
                                 });
                         })
                         .Where(FieldNameAt<FieldIndex, ReferencedRecord>, SqlWildcard)
                         .OrderBy(FieldNameAt<RecordPrimaryKeyIndex<ReferencedRecord>, ReferencedRecord>);
        callback(query, primaryKeyField);
    });
}

template <size_t FieldIndex, typename OtherRecord>
SqlSelectQueryBuilder DataMapper::BuildHasManySelectQuery()
{
    return _connection.Query(RecordTableName<OtherRecord>)
        .Select()
        .Build([](auto& q) {
            EnumerateRecordMembers<OtherRecord>([&]<size_t I, typename F>() {
                if constexpr (FieldWithStorage<F>)
                    q.Field(FieldNameAt<I, OtherRecord>);
            });
        })
        .Where(FieldNameAt<FieldIndex, OtherRecord>, SqlWildcard)
        .OrderBy(FieldNameAt<RecordPrimaryKeyIndex<OtherRecord>, OtherRecord>);
}

template <size_t FieldIndex, typename Record, typename OtherRecord>
void DataMapper::LoadHasMany(Record& record, HasMany<OtherRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<OtherRecord>, "OtherRecord must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::LoadHasMany");
    ZoneTextObject(RecordTableName<OtherRecord>);

    CallOnHasMany<FieldIndex, Record, OtherRecord>(record, [&](SqlSelectQueryBuilder selectQuery, auto& primaryKeyField) {
        field.Emplace(detail::ToSharedPtrList(Query<OtherRecord>(selectQuery.All(), primaryKeyField.Value())));
    });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record>
void DataMapper::LoadHasOneThrough(Record& record, HasOneThrough<ReferencedRecord, ThroughRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(DataMapperRecord<ThroughRecord>, "ThroughRecord must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::LoadHasOneThrough");
    ZoneTextObject(RecordTableName<ReferencedRecord>);

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
                                EnumerateRecordMembers<ReferencedRecord>(
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

template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename PKValue>
std::shared_ptr<ReferencedRecord> DataMapper::LoadHasOneThroughByPK(PKValue const& pkValue)
{
    static_assert(DataMapperRecord<ThroughRecord>, "ThroughRecord must satisfy DataMapperRecord");

    constexpr size_t PrimaryKeyIndex = RecordPrimaryKeyIndex<Record>;
    std::shared_ptr<ReferencedRecord> result;

    // Find the BelongsTo of ThroughRecord pointing to the PK of Record
    CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToIndex, typename ThroughBelongsToType>() {
        // Find the PK of ThroughRecord
        CallOnPrimaryKey<ThroughRecord>([&]<size_t ThroughPrimaryKeyIndex, typename ThroughPrimaryKeyType>() {
            // Find the BelongsTo of ReferencedRecord pointing to the PK of ThroughRecord
            CallOnBelongsTo<ReferencedRecord>([&]<size_t ReferencedKeyIndex, typename ReferencedKeyType>() {
                auto query =
                    _connection.Query(RecordTableName<ReferencedRecord>)
                        .Select()
                        .Build([&](auto& query) {
                            EnumerateRecordMembers<ReferencedRecord>(
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
                if (auto link = QuerySingle<ReferencedRecord>(std::move(query), pkValue); link)
                    result = std::make_shared<ReferencedRecord>(std::move(*link));
            });
        });
    });

    return result;
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename Callable>
void DataMapper::CallOnHasManyThrough(Record& record, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    // Find the PK of Record
    CallOnPrimaryKey(record, [&]<size_t PrimaryKeyIndex, typename PrimaryKeyType>(PrimaryKeyType const& primaryKeyField) {
        // Find the BelongsTo of ThroughRecord pointing to the PK of Record
        CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToRecordIndex, typename ThroughBelongsToRecordType>() {
            using ThroughBelongsToRecordFieldType = RecordMemberTypeOf<ThroughBelongsToRecordIndex, ThroughRecord>;
            if constexpr (std::is_same_v<typename ThroughBelongsToRecordFieldType::ReferencedRecord, Record>)
            {
                // Find the BelongsTo of ThroughRecord pointing to the PK of ReferencedRecord
                CallOnBelongsTo<ThroughRecord>(
                    [&]<size_t ThroughBelongsToReferenceRecordIndex, typename ThroughBelongsToReferenceRecordType>() {
                        using ThroughBelongsToReferenceRecordFieldType =
                            RecordMemberTypeOf<ThroughBelongsToReferenceRecordIndex, ThroughRecord>;
                        if constexpr (std::is_same_v<typename ThroughBelongsToReferenceRecordFieldType::ReferencedRecord,
                                                     ReferencedRecord>)
                        {
                            auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                                             .Select()
                                             .Build([&](auto& query) {
                                                 EnumerateRecordMembers<ReferencedRecord>(
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

template <typename ReferencedRecord, typename ThroughRecord, typename Record, typename PKValue, typename Callable>
void DataMapper::CallOnHasManyThroughByPK(PKValue const& pkValue, Callable const& callback)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    constexpr size_t PrimaryKeyIndex = RecordPrimaryKeyIndex<Record>;

    // Find the BelongsTo of ThroughRecord pointing to the PK of Record
    CallOnBelongsTo<ThroughRecord>([&]<size_t ThroughBelongsToRecordIndex, typename ThroughBelongsToRecordType>() {
        using ThroughBelongsToRecordFieldType = RecordMemberTypeOf<ThroughBelongsToRecordIndex, ThroughRecord>;
        if constexpr (std::is_same_v<typename ThroughBelongsToRecordFieldType::ReferencedRecord, Record>)
        {
            // Find the BelongsTo of ThroughRecord pointing to the PK of ReferencedRecord
            CallOnBelongsTo<ThroughRecord>(
                [&]<size_t ThroughBelongsToReferenceRecordIndex, typename ThroughBelongsToReferenceRecordType>() {
                    using ThroughBelongsToReferenceRecordFieldType =
                        RecordMemberTypeOf<ThroughBelongsToReferenceRecordIndex, ThroughRecord>;
                    if constexpr (std::is_same_v<typename ThroughBelongsToReferenceRecordFieldType::ReferencedRecord,
                                                 ReferencedRecord>)
                    {
                        auto query = _connection.Query(RecordTableName<ReferencedRecord>)
                                         .Select()
                                         .Build([&](auto& query) {
                                             EnumerateRecordMembers<ReferencedRecord>(
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
                        callback(query, pkValue);
                    }
                });
        }
    });
}

template <typename ReferencedRecord, typename ThroughRecord, typename Record>
void DataMapper::LoadHasManyThrough(Record& record, HasManyThrough<ReferencedRecord, ThroughRecord>& field)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

    ZoneScopedN("DataMapper::LoadHasManyThrough");
    ZoneTextObject(RecordTableName<ReferencedRecord>);

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

    ZoneScopedN("DataMapper::LoadRelations");
    ZoneTextObject(RecordTableName<Record>);

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
    EnumerateRecordMembers(record, [&]<size_t FieldIndex, typename FieldType>(FieldType& field) {
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

/// Sets the primary key field(s) of the given record to the specified id value.
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
    EnumerateRecordMembers(record, [&]<size_t I, typename FieldType>(FieldType& field) {
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

/// Binds all output columns of the record via the given cursor.
template <typename Record, size_t InitialOffset>
inline LIGHTWEIGHT_FORCE_INLINE Record& DataMapper::BindOutputColumns(Record& record, SqlResultCursor& cursor)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    return BindOutputColumns<std::make_integer_sequence<size_t, RecordMemberCount<Record>>, Record, InitialOffset>(record,
                                                                                                                   cursor);
}

template <typename ElementMask, typename Record, size_t InitialOffset>
Record& DataMapper::BindOutputColumns(Record& record, SqlResultCursor& cursor)
{
    static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
    static_assert(!std::is_const_v<Record>);

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    auto constexpr ctx = std::meta::access_context::current();
    SQLSMALLINT i = SQLSMALLINT { InitialOffset };
    template for (constexpr auto index: define_static_array(template_arguments_of(^^ElementMask)) | std::views::drop(1))
    {
        constexpr auto el = nonstatic_data_members_of(^^Record, ctx)[[:index:]];
        using FieldType = typename[:std::meta::type_of(el):];
        if constexpr (IsField<FieldType>)
        {
            cursor.BindOutputColumn(i++, &record.[:el:].MutableValue());
        }
        else if constexpr (SqlOutputColumnBinder<FieldType>)
        {
            cursor.BindOutputColumn(i++, &record.[:el:]);
        }
    }
#else
    EnumerateRecordMembers<ElementMask>(
        record, [&cursor, i = SQLUSMALLINT { InitialOffset }]<size_t I, typename Field>(Field& field) mutable {
            if constexpr (IsField<Field>)
            {
                cursor.BindOutputColumn(i++, &field.MutableValue());
            }
            else if constexpr (SqlOutputColumnBinder<Field>)
            {
                cursor.BindOutputColumn(i++, &field);
            }
        });
#endif

    return record;
}
template <typename Record>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
        if constexpr (IsHasMany<FieldType>)
        {
            if constexpr (HasPrimaryKey<Record>)
            {
                using ReferencedRecord = FieldType::ReferencedRecord;
                HasMany<ReferencedRecord>& hasMany = field;
                // Capture the PK value by value to avoid dangling references if the record is moved.
                auto pkValue = GetPrimaryKeyField(record);
                hasMany.SetAutoLoader(typename FieldType::Loader {
                    .count = [pkValue]() -> size_t {
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        auto selectQuery = dm.BuildHasManySelectQuery<FieldIndex, ReferencedRecord>();
                        dm._stmt.Prepare(selectQuery.Count());
                        SqlResultCursor cursor = dm._stmt.Execute(pkValue);
                        size_t count = 0;
                        if (cursor.FetchRow())
                            count = cursor.GetColumn<size_t>(1);
                        return count;
                    },
                    .all = [pkValue]() -> FieldType::ReferencedRecordList {
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        auto selectQuery = dm.BuildHasManySelectQuery<FieldIndex, ReferencedRecord>();
                        return detail::ToSharedPtrList(dm.Query<ReferencedRecord>(selectQuery.All(), pkValue));
                    },
                    .each =
                        [pkValue](auto const& each) {
                            DataMapper& dm = DataMapper::AcquireThreadLocal();
                            auto selectQuery = dm.BuildHasManySelectQuery<FieldIndex, ReferencedRecord>();
                            auto stmt = SqlStatement { dm._connection };
                            stmt.Prepare(selectQuery.All());
                            auto cursor = stmt.Execute(pkValue);

                            auto referencedRecord = ReferencedRecord {};
                            dm.BindOutputColumns(referencedRecord, cursor);
                            dm.ConfigureRelationAutoLoading(referencedRecord);

                            while (cursor.FetchRow())
                            {
                                each(referencedRecord);
                                dm.BindOutputColumns(referencedRecord, cursor);
                            }
                        },
                });
            }
        }
        if constexpr (IsHasOneThrough<FieldType> && HasPrimaryKey<Record>)
        {
            using ReferencedRecord = FieldType::ReferencedRecord;
            using ThroughRecord = FieldType::ThroughRecord;
            HasOneThrough<ReferencedRecord, ThroughRecord>& hasOneThrough = field;
            // Capture the PK value by value to avoid dangling references if the record is moved.
            auto pkValue = GetPrimaryKeyField(record);
            hasOneThrough.SetAutoLoader(typename FieldType::Loader {
                .loadReference = [pkValue]() -> std::shared_ptr<ReferencedRecord> {
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    return dm.LoadHasOneThroughByPK<ReferencedRecord, ThroughRecord, Record>(pkValue);
                },
            });
        }
        if constexpr (IsHasManyThrough<FieldType> && HasPrimaryKey<Record>)
        {
            using ReferencedRecord = FieldType::ReferencedRecord;
            using ThroughRecord = FieldType::ThroughRecord;
            HasManyThrough<ReferencedRecord, ThroughRecord>& hasManyThrough = field;
            // Capture the PK value by value to avoid dangling references if the record is moved.
            auto pkValue = GetPrimaryKeyField(record);
            hasManyThrough.SetAutoLoader(typename FieldType::Loader {
                .count = [pkValue]() -> size_t {
                    // Load result for Count()
                    size_t count = 0;
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    dm.CallOnHasManyThroughByPK<ReferencedRecord, ThroughRecord, Record>(
                        pkValue, [&](SqlSelectQueryBuilder& selectQuery, auto const& pk) {
                            dm._stmt.Prepare(selectQuery.Count());
                            SqlResultCursor cursor = dm._stmt.Execute(pk);
                            if (cursor.FetchRow())
                                count = cursor.GetColumn<size_t>(1);
                        });
                    return count;
                },
                .all = [pkValue]() -> FieldType::ReferencedRecordList {
                    // Load result for All()
                    DataMapper& dm = DataMapper::AcquireThreadLocal();
                    typename FieldType::ReferencedRecordList result;
                    dm.CallOnHasManyThroughByPK<ReferencedRecord, ThroughRecord, Record>(
                        pkValue, [&](SqlSelectQueryBuilder& selectQuery, auto const& pk) {
                            result = detail::ToSharedPtrList(dm.Query<ReferencedRecord>(selectQuery.All(), pk));
                        });
                    return result;
                },
                .each =
                    [pkValue](auto const& each) {
                        // Load result for Each()
                        DataMapper& dm = DataMapper::AcquireThreadLocal();
                        dm.CallOnHasManyThroughByPK<ReferencedRecord, ThroughRecord, Record>(
                            pkValue, [&](SqlSelectQueryBuilder& selectQuery, auto const& pk) {
                                auto stmt = SqlStatement { dm._connection };
                                stmt.Prepare(selectQuery.All());
                                auto cursor = stmt.Execute(pk);
                                auto referencedRecord = ReferencedRecord {};
                                dm.BindOutputColumns(referencedRecord, cursor);
                                dm.ConfigureRelationAutoLoading(referencedRecord);

                                while (cursor.FetchRow())
                                {
                                    each(referencedRecord);
                                    dm.BindOutputColumns(referencedRecord, cursor);
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
    EnumerateRecordMembers(record, callback);
#endif
}

template <typename T>
std::optional<T> DataMapper::Execute(std::string_view sqlQueryString)
{
    ZoneScopedN("DataMapper::Execute(string)");
    ZoneTextObject(sqlQueryString);
    return _stmt.ExecuteDirectScalar<T>(sqlQueryString);
}

} // namespace Lightweight

#include "../Async/DataMapperAsync.hpp"
