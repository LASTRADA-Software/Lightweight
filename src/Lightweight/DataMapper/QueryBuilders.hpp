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
    void BindAllOutputColumnsWithOffset(SqlResultCursor& reader, Record& record, SQLSMALLINT startOffset)
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
    /// Executes a SELECT COUNT query and returns the number of records found.
    [[nodiscard]] size_t Count()
    {
        _stmt.ExecuteDirect(_formatter.SelectCount(this->_query.distinct,
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition));
        auto reader = _stmt.GetResultCursor();
        if (reader.FetchRow())
            return reader.GetColumn<size_t>(1);
        return 0;
    }

    /// Executes a SELECT query and returns all records found.
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
    [[nodiscard]] auto All() -> std::vector<ReferencedFieldTypeOf<Field>>
    {
        using value_type = ReferencedFieldTypeOf<Field>;
        auto result = std::vector<value_type> {};

        _stmt.ExecuteDirect(_formatter.SelectAll(this->_query.distinct,
                                                 FullyQualifiedNamesOf<Field>.string_view(),
                                                 RecordTableName<Record>,
                                                 this->_query.searchCondition.tableAlias,
                                                 this->_query.searchCondition.tableJoins,
                                                 this->_query.searchCondition.condition,
                                                 this->_query.orderBy,
                                                 this->_query.groupBy));
        SqlResultCursor reader = _stmt.GetResultCursor();
        auto const outputColumnsBound = detail::CanSafelyBindOutputColumn<value_type>(_stmt.Connection().ServerType());
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
    [[nodiscard]] auto All() -> std::vector<Record>
    {
        auto records = std::vector<Record> {};

        _stmt.ExecuteDirect(_formatter.SelectAll(this->_query.distinct,
                                                 FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                                 RecordTableName<Record>,
                                                 this->_query.searchCondition.tableAlias,
                                                 this->_query.searchCondition.tableJoins,
                                                 this->_query.searchCondition.condition,
                                                 this->_query.orderBy,
                                                 this->_query.groupBy));

        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());
        SqlResultCursor reader = _stmt.GetResultCursor();
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

    /// Executes a SELECT query for the first record found and returns it.
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

    /// @brief Executes the query to get a single scalar value from the first record found.
    ///
    /// @tparam Field The field to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns an optional value of the type of the field, or an empty optional if no record was found.
    template <auto Field>
    [[nodiscard]] auto First() -> std::optional<ReferencedFieldTypeOf<Field>>
    {
        auto constexpr count = 1;
        _stmt.ExecuteDirect(_formatter.SelectFirst(this->_query.distinct,
                                                   FullyQualifiedNamesOf<Field>.string_view(),
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition,
                                                   this->_query.orderBy,
                                                   count));
        if (SqlResultCursor reader = _stmt.GetResultCursor(); reader.FetchRow())
            return reader.template GetColumn<ReferencedFieldTypeOf<Field>>(1);
        return std::nullopt;
    }

    /// @brief Executes a SELECT query for the first record found and returns it with only the specified fields populated.
    ///
    /// @tparam ReferencedFields The fields to select from the record, in the form of &Record::FieldName.
    ///
    /// @returns an optional record with only the specified fields populated, or an empty optional if no record was found.
    template <auto... ReferencedFields>
        requires(sizeof...(ReferencedFields) >= 2)
    [[nodiscard]] auto First() -> std::optional<Record>
    {
        auto optionalRecord = std::optional<Record> {};

        _stmt.ExecuteDirect(_formatter.SelectFirst(this->_query.distinct,
                                                   FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition,
                                                   this->_query.orderBy,
                                                   1));

        auto& record = optionalRecord.emplace();
        SqlResultCursor reader = _stmt.GetResultCursor();
        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());
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

        return optionalRecord;
    }

    /// Executes a SELECT query for the first n records found and returns them.
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

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> First(size_t n)
    {
        auto records = std::vector<Record> {};
        records.reserve(n);
        _stmt.ExecuteDirect(_formatter.SelectFirst(this->_query.distinct,
                                                   FullyQualifiedNamesOf<ReferencedFields...>.string_view(),
                                                   RecordTableName<Record>,
                                                   this->_query.searchCondition.tableAlias,
                                                   this->_query.searchCondition.tableJoins,
                                                   this->_query.searchCondition.condition,
                                                   this->_query.orderBy,
                                                   n));

        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());
        SqlResultCursor reader = _stmt.GetResultCursor();
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

    /// Executes a SELECT query for a range of records and returns them.
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

    template <auto... ReferencedFields>
    [[nodiscard]] std::vector<Record> Range(size_t offset, size_t limit)
    {
        auto records = std::vector<Record> {};
        records.reserve(limit);
        _stmt.ExecuteDirect(_formatter.SelectRange(
            this->_query.distinct,
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

        auto const outputColumnsBound = detail::CanSafelyBindOutputColumns<Record>(_stmt.Connection().ServerType());
        SqlResultCursor reader = _stmt.GetResultCursor();
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
            if (!detail::ReadSingleResult(sqlServerType, reader, record))
            {
                records->pop_back();
                break;
            }
        }
    }

    static void ReadResult(SqlServerType sqlServerType, SqlResultCursor reader, std::optional<Record>* optionalRecord)
    {
        Record& record = optionalRecord->emplace();
        if (!detail::ReadSingleResult(sqlServerType, reader, record))
            optionalRecord->reset();
    }
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
};

} // namespace Lightweight
