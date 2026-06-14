// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "BelongsTo.hpp"
#include "Field.hpp"
#include "Record.hpp"

#include <cstdint>

namespace Lightweight
{

class DataMapper;

/// Structural type for options for DataMapper queries.
/// This allows to configure behavior of the queries at compile time
/// when using query builder directly from the DataMapper
struct DataMapperOptions
{
    /// Whether to automatically load relations when querying records.
    bool loadRelations { true };
};

/// Selects whether a query builder's finisher methods execute synchronously or asynchronously.
///
/// In @c Synchronous mode a finisher (e.g. @c All(), @c First(n)) runs immediately and returns its
/// plain result. In @c Asynchronous mode the very same finisher offloads its work to the connection's
/// async backend and returns a @c Async::Task of that result instead, to be @c co_await -ed.
enum class SqlQueryExecutionMode : std::uint8_t
{
    /// The finisher runs on the calling thread and returns its result directly.
    Synchronous,
    /// The finisher returns an @c Async::Task that offloads the work and resumes the awaiting coroutine.
    Asynchronous,
};

/// Main API for mapping records to C++ from the database using high level C++ syntax.
///
/// @ingroup DataMapper
template <typename Record, typename Derived, DataMapperOptions QueryOptions = {}>
class [[nodiscard]] SqlCoreDataMapperQueryBuilder: public SqlBasicSelectQueryBuilder<Derived>
{
  private:
    DataMapper& _dm;
    SqlQueryFormatter const& _formatter;

    std::string _fields;
    std::vector<SqlVariant> _boundInputs;

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
    /// Constructs a query builder with the given data mapper and field list.
    LIGHTWEIGHT_FORCE_INLINE explicit SqlCoreDataMapperQueryBuilder(DataMapper& dm, std::string fields) noexcept;

  public:
    // The public finisher methods below are thin dispatchers: each forwards to its synchronous
    // implementation (the *Impl members) through RunFinisher(), which either calls it directly
    // (Synchronous mode) or offloads it to the connection's async backend and returns an
    // Async::Task (Asynchronous mode). The execution mode is carried by the Derived type
    // (Derived::QueryExecution), so the same fluent builder serves both Query() and QueryAsync().

    /// Executes a SELECT 1 ... query and returns true if a record exists
    /// We do not provide db specific syntax to check this but reuse the First() implementation
    [[nodiscard]] auto Exist()
    {
        return RunFinisher([this] { return ExistImpl(); });
    }

    /// Executes a SELECT COUNT query and returns the number of records found.
    [[nodiscard]] auto Count()
    {
        return RunFinisher([this] { return CountImpl(); });
    }

    /// Executes a SELECT query and returns all records found.
    [[nodiscard]] auto All()
    {
        return RunFinisher([this] { return AllImpl(); });
    }

    /// Executes a DELETE query.
    [[nodiscard]] auto Delete()
    {
        return RunFinisher([this] { return DeleteImpl(); });
    }

    /// @brief Executes a SELECT query and returns all records found for the specified field.
    ///
    /// @tparam Field The field to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns A vector of values of the type of the specified field.
    ///
    /// @code
    /// auto dm = DataMapper {};
    /// auto const ages = dm.Query<Person>()
    ///                     .OrderBy(FieldNameOf<&Person::age>, SqlResultOrdering::ASCENDING)
    ///                     .All<&Person::age>();
    /// @endcode
    template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        requires(is_aggregate_type(parent_of(Field)))
#else
        requires std::is_member_object_pointer_v<decltype(Field)>
#endif
    [[nodiscard]] auto All()
    {
        return RunFinisher([this] { return this->template AllImpl<Field>(); });
    }

    /// @brief Executes a SELECT query and returns all records found for the specified field,
    ///        only having the specified fields queried and populated.
    ///
    /// @tparam ReferencedFields The fields to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns A vector of records with the given fields populated.
    ///
    /// @code
    /// auto dm = DataMapper {};
    /// auto const ages = dm.Query<Person>()
    ///                     .OrderBy(FieldNameOf<&Person::age>, SqlResultOrdering::ASCENDING)
    ///                     .All<&Person::name, &Person::age>();
    /// @endcode
    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto All()
    {
        return RunFinisher([this] { return this->template AllImpl<ReferencedFields...>(); });
    }

    /// Executes a SELECT query for the first record found and returns it.
    [[nodiscard]] auto First()
    {
        return RunFinisher([this] { return FirstImpl(); });
    }

    /// @brief Executes the query to get a single scalar value from the first record found.
    ///
    /// @tparam Field The field to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns an optional value of the type of the field, or an empty optional if no record was found.
    template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        requires(is_aggregate_type(parent_of(Field)))
#else
        requires std::is_member_object_pointer_v<decltype(Field)>
#endif
    [[nodiscard]] auto First()
    {
        return RunFinisher([this] { return this->template FirstImpl<Field>(); });
    }

    /// @brief Executes a SELECT query for the first record found and returns it with only the specified fields populated.
    ///
    /// @tparam ReferencedFields The fields to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns an optional record with only the specified fields populated, or an empty optional if no record was found.
    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto First()
    {
        return RunFinisher([this] { return this->template FirstImpl<ReferencedFields...>(); });
    }

    /// Executes a SELECT query for the first n records found and returns them.
    [[nodiscard]] auto First(size_t n)
    {
        return RunFinisher([this, n] { return FirstImpl(n); });
    }

    /// Executes a SELECT query for the first n records with only the specified fields populated.
    template <auto... ReferencedFields>
    [[nodiscard]] auto First(size_t n)
    {
        return RunFinisher([this, n] { return this->template FirstImpl<ReferencedFields...>(n); });
    }

    /// Executes a SELECT query for a range of records and returns them.
    [[nodiscard]] auto Range(size_t offset, size_t limit)
    {
        return RunFinisher([this, offset, limit] { return RangeImpl(offset, limit); });
    }

    /// Executes a SELECT query for a range of records with only the specified fields populated.
    template <auto... ReferencedFields>
    [[nodiscard]] auto Range(size_t offset, size_t limit)
    {
        return RunFinisher([this, offset, limit] { return this->template RangeImpl<ReferencedFields...>(offset, limit); });
    }

  private:
    /// Dispatches a finisher according to the builder's execution mode.
    ///
    /// In @c Synchronous mode the finisher is invoked directly and its result returned. In
    /// @c Asynchronous mode it is offloaded to the connection's async backend and an
    /// @c Async::Task wrapping its result is returned instead.
    ///
    /// @param finisher A nullary callable running one of the synchronous @c *Impl methods.
    /// @return The finisher's result (Synchronous) or an @c Async::Task of it (Asynchronous).
    ///
    /// @note Defined out-of-line in DataMapper.hpp, where @c DataMapper is a complete type (the
    ///       async branch dereferences it via @c _dm.Connection().AsyncBackend()).
    template <typename Finisher>
    auto RunFinisher(Finisher finisher);

    // Synchronous implementations shared by both execution modes. The public finishers above
    // forward to these; the SQL building and result mapping live here exactly once.

    [[nodiscard]] bool ExistImpl();
    [[nodiscard]] size_t CountImpl();
    [[nodiscard]] std::vector<Record> AllImpl();
    void DeleteImpl();

    template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        requires(is_aggregate_type(parent_of(Field)))
#else
        requires std::is_member_object_pointer_v<decltype(Field)>
#endif
    [[nodiscard]] auto AllImpl() -> std::vector<ReferencedFieldTypeOf<Field>>;

    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto AllImpl() -> std::vector<Record>;

    [[nodiscard]] std::optional<Record> FirstImpl();

    template <auto Field>
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        requires(is_aggregate_type(parent_of(Field)))
#else
        requires std::is_member_object_pointer_v<decltype(Field)>
#endif
    [[nodiscard]] auto FirstImpl() -> std::optional<ReferencedFieldTypeOf<Field>>;

    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto FirstImpl() -> std::optional<Record>;

    [[nodiscard]] std::vector<Record> FirstImpl(size_t n);

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> FirstImpl(size_t n);

    [[nodiscard]] std::vector<Record> RangeImpl(size_t offset, size_t limit);

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> RangeImpl(size_t offset, size_t limit);
};

/// @brief Represents a query builder that retrieves all fields of a record.
///
/// @ingroup DataMapper
///
/// @tparam Execution Whether the finisher methods execute synchronously or asynchronously.
///         @c DataMapper::Query selects @c Synchronous, @c DataMapper::QueryAsync selects
///         @c Asynchronous; the rest of the fluent builder is identical for both.
template <typename Record,
          DataMapperOptions QueryOptions,
          SqlQueryExecutionMode Execution = SqlQueryExecutionMode::Synchronous>
class [[nodiscard]] SqlAllFieldsQueryBuilder final:
    public SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record, QueryOptions, Execution>, QueryOptions>
{
  private:
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<Record,
                                               SqlAllFieldsQueryBuilder<Record, QueryOptions, Execution>,
                                               QueryOptions>;

    /// The execution mode (synchronous/asynchronous) read by the CRTP base to dispatch finishers.
    static constexpr SqlQueryExecutionMode QueryExecution = Execution;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record, QueryOptions, Execution>, QueryOptions> {
            dm, std::move(fields)
        }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<Record>* records);
    static void ReadResult(SqlServerType sqlServerType, SqlResultCursor reader, std::optional<Record>* optionalRecord);
};

/// @brief Specialization of SqlAllFieldsQueryBuilder for the case when we return std::tuple
/// of two records
///
/// @ingroup DataMapper
/// @todo deprecate this in favor of a more generic tuple support
template <typename FirstRecord, typename SecondRecord, DataMapperOptions QueryOptions, SqlQueryExecutionMode Execution>
class [[nodiscard]] SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions, Execution> final:
    public SqlCoreDataMapperQueryBuilder<
        std::tuple<FirstRecord, SecondRecord>,
        SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions, Execution>,
        QueryOptions>
{
  private:
    using RecordType = std::tuple<FirstRecord, SecondRecord>;
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<RecordType,
                                               SqlAllFieldsQueryBuilder<RecordType, QueryOptions, Execution>,
                                               QueryOptions>;

    /// The execution mode (synchronous/asynchronous) read by the CRTP base to dispatch finishers.
    static constexpr SqlQueryExecutionMode QueryExecution = Execution;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<RecordType,
                                      SqlAllFieldsQueryBuilder<RecordType, QueryOptions, Execution>,
                                      QueryOptions> { dm, std::move(fields) }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records);
};

} // namespace Lightweight
