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

/// Structural type for options for DataMapper queries.
/// This allows to configure behavior of the queries at compile time
/// when using query builder directly from the DataMapper
struct DataMapperOptions
{
    /// This is the default behavior since compilation times significantly increase otherwise.
    bool loadRelations { false };
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
    /// Executes a SELECT 1 ... query and returns true if a record exists
    /// We do not provide db specific syntax to check this but reuse the First() implementation
    [[nodiscard]] bool Exist();

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
template <typename Record, DataMapperOptions QueryOptions>
class [[nodiscard]] SqlAllFieldsQueryBuilder final:
    public SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record, QueryOptions>, QueryOptions>
{
  private:
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record, QueryOptions>, QueryOptions>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<Record, SqlAllFieldsQueryBuilder<Record, QueryOptions>, QueryOptions> {
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
template <typename FirstRecord, typename SecondRecord, DataMapperOptions QueryOptions>
class [[nodiscard]] SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions> final:
    public SqlCoreDataMapperQueryBuilder<std::tuple<FirstRecord, SecondRecord>,
                                         SqlAllFieldsQueryBuilder<std::tuple<FirstRecord, SecondRecord>, QueryOptions>,
                                         QueryOptions>
{
  private:
    using RecordType = std::tuple<FirstRecord, SecondRecord>;
    friend class DataMapper;
    friend class SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType, QueryOptions>>;

    LIGHTWEIGHT_FORCE_INLINE explicit SqlAllFieldsQueryBuilder(DataMapper& dm, std::string fields) noexcept:
        SqlCoreDataMapperQueryBuilder<RecordType, SqlAllFieldsQueryBuilder<RecordType, QueryOptions>, QueryOptions> {
            dm, std::move(fields)
        }
    {
    }

    static void ReadResults(SqlServerType sqlServerType, SqlResultCursor reader, std::vector<RecordType>* records);
};

} // namespace Lightweight
