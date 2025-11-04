// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlConnection.hpp"
#include "../SqlQueryFormatter.hpp"
#include "../SqlStatement.hpp"
#include "../Utils.hpp"
#include "BelongsTo.hpp"
#include "Field.hpp"
#include "Record.hpp"

namespace Lightweight
{

class DataMapper;

/// Main API for mapping records to C++ from the database using high level C++ syntax.
///
/// @ingroup DataMapper
template <typename Record, typename Derived>
class [[nodiscard]] SqlCoreDataMapperQueryBuilder: public SqlBasicSelectQueryBuilder<Derived>
{
  private:
    DataMapper& _dm;
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
    LIGHTWEIGHT_FORCE_INLINE explicit SqlCoreDataMapperQueryBuilder(DataMapper& dm, std::string fields) noexcept;

  public:
    /// Executes a SELECT COUNT query and returns the number of records found.
    [[nodiscard]] size_t Count();

    /// Executes a SELECT query and returns all records found.
    [[nodiscard]] std::vector<Record> All();

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
    [[nodiscard]] auto All() -> std::vector<ReferencedFieldTypeOf<Field>>;

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
    [[nodiscard]] auto All() -> std::vector<Record>;

    /// Executes a SELECT query for the first record found and returns it.
    [[nodiscard]] std::optional<Record> First();

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
    auto First() -> std::optional<ReferencedFieldTypeOf<Field>>;

    /// @brief Executes a SELECT query for the first record found and returns it with only the specified fields populated.
    ///
    /// @tparam ReferencedFields The fields to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns an optional record with only the specified fields populated, or an empty optional if no record was found.
    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto First() -> std::optional<Record>;

    /// Executes a SELECT query for the first n records found and returns them.
    [[nodiscard]] std::vector<Record> First(size_t n);

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> First(size_t n);

    /// Executes a SELECT query for a range of records and returns them.
    [[nodiscard]] std::vector<Record> Range(size_t offset, size_t limit);

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> Range(size_t offset, size_t limit);
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

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record>> { dm, std::move(fields) }
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
template <typename FirstRecord, typename SecondRecord>
class [[nodiscard]] SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>> final:
    public SqlCoreDataMapperQueryBuilder<std::tuple<FirstRecord, SecondRecord>,
                                         SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>>>
{
  private:
    using RecordType = std::tuple<FirstRecord, SecondRecord>;
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType>>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType>> { dm, std::move(fields) }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records);
};

} // namespace Lightweight
