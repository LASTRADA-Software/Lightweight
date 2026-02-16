// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../DataMapper/BelongsTo.hpp"
#include "../DataMapper/Field.hpp"
#include "../DataMapper/Record.hpp"
#include "../Utils.hpp"
#include "Core.hpp"
#include "MigrationPlan.hpp"

#include <reflection-cpp/reflection.hpp>

namespace Lightweight
{

/// @brief Index type for simplified CreateIndex API.
/// @ingroup QueryBuilder
enum class IndexType : std::uint8_t
{
    NonUnique, ///< Regular (non-unique) index
    Unique     ///< Unique index
};

/// @brief Query builder for building CREATE TABLE queries.
///
/// @see SqlQueryBuilder
/// @ingroup QueryBuilder
class [[nodiscard]] SqlCreateTableQueryBuilder final
{
  public:
    /// Constructs a CREATE TABLE query builder.
    explicit SqlCreateTableQueryBuilder(SqlCreateTablePlan& plan):
        _plan { plan }
    {
    }

    /// Adds a new column to the table.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& Column(SqlColumnDeclaration column);

    /// Creates a new nullable column.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& Column(std::string columnName, SqlColumnTypeDefinition columnType);

    /// Creates a new column that is non-nullable.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& RequiredColumn(std::string columnName, SqlColumnTypeDefinition columnType);

    /// Adds the created_at and updated_at columns to the table.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& Timestamps();

    /// Creates a new primary key column.
    /// Primary keys are always required, unique, have an index, and are non-nullable.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& PrimaryKey(std::string columnName, SqlColumnTypeDefinition columnType);

    /// Creates a new primary key column with auto-increment.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& PrimaryKeyWithAutoIncrement(
        std::string columnName, SqlColumnTypeDefinition columnType = SqlColumnTypeDefinitions::Bigint {});

    /// Creates a new nullable foreign key column.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& ForeignKey(std::string columnName,
                                                           SqlColumnTypeDefinition columnType,
                                                           SqlForeignKeyReferenceDefinition foreignKey);

    /// Creates a new non-nullable foreign key column.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& RequiredForeignKey(std::string columnName,
                                                                   SqlColumnTypeDefinition columnType,
                                                                   SqlForeignKeyReferenceDefinition foreignKey);

    /// Adds a composite foreign key constraint.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& ForeignKey(std::vector<std::string> columns,
                                                           std::string referencedTableName,
                                                           std::vector<std::string> referencedColumns);

    /// Enables the UNIQUE constraint on the last declared column.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& Unique();

    /// Enables the INDEX constraint on the last declared column.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& Index();

    /// Enables the UNIQUE and INDEX constraint on the last declared column and makes it an index.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder& UniqueIndex();

  private:
    SqlCreateTablePlan& _plan;
};

/// @brief Query builder for building ALTER TABLE queries.
///
/// @see SqlQueryBuilder
/// @ingroup QueryBuilder
class [[nodiscard]] SqlAlterTableQueryBuilder final
{
  public:
    /// Constructs an ALTER TABLE query builder.
    explicit SqlAlterTableQueryBuilder(SqlAlterTablePlan& plan):
        _plan { plan }
    {
    }

    /// Renames the table.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& RenameTo(std::string_view newTableName);

    /// Adds a new column to the table that is non-nullable.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddColumn(std::string columnName, SqlColumnTypeDefinition columnType);

    /// Adds a new column to the table that is non-nullable.
    ///
    /// @tparam MemberPointer The pointer to the member field in the record.
    ///
    /// @see AddColumn(std::string, SqlColumnTypeDefinition)
    template <auto MemberPointer>
    SqlAlterTableQueryBuilder& AddColumn()
    {
        return AddColumn(std::string(FieldNameOf<MemberPointer>),
                         SqlColumnTypeDefinitionOf<ReferencedFieldTypeOf<MemberPointer>>);
    }

    /// Adds a new column to the table that is nullable.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddNotRequiredColumn(std::string columnName,
                                                                    SqlColumnTypeDefinition columnType);

    /// Adds a new column to the table that is nullable.
    ///
    /// @tparam MemberPointer The pointer to the member field in the record.
    ///
    /// @see AddNotRequiredColumn(std::string, SqlColumnTypeDefinition)
    template <auto MemberPointer>
    SqlAlterTableQueryBuilder& AddNotRequiredColumn()
    {
        return AddNotRequiredColumn(std::string(FieldNameOf<MemberPointer>),
                                    SqlColumnTypeDefinitionOf<ReferencedFieldTypeOf<MemberPointer>>);
    }

    /// @brief Alters the column to have a new non-nullable type.
    ///
    /// @param columnName The name of the column to alter.
    /// @param columnType The new type of the column.
    /// @param nullable The new nullable state of the column.
    ///
    /// @return The current query builder for chaining.
    ///
    /// @see SqlColumnTypeDefinition
    ///
    /// @code
    /// auto stmt = SqlStatement();
    /// auto sqlMigration = stmt.Migration()
    ///                         .AlterTable("Table")
    ///                         .AlterColumn("column", Integer {}, SqlNullable::NotNull)
    ///                         .GetPlan().ToSql();
    /// for (auto const& sql: sqlMigration)
    ///     stmt.ExecuteDirect(sql);
    /// @endcode
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AlterColumn(std::string columnName,
                                                           SqlColumnTypeDefinition columnType,
                                                           SqlNullable nullable);

    /// Renames a column.
    /// @param oldColumnName The old column name.
    /// @param newColumnName The new column name.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& RenameColumn(std::string_view oldColumnName, std::string_view newColumnName);

    /// Drops a column from the table.
    /// @param columnName The name of the column to drop.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& DropColumn(std::string_view columnName);

    /// Adds a new column to the table only if it does not already exist.
    ///
    /// @note Database support varies:
    /// - PostgreSQL: Native IF NOT EXISTS support
    /// - SQL Server: Uses conditional IF NOT EXISTS query
    /// - SQLite: Limited support (may require raw SQL)
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddColumnIfNotExists(std::string columnName,
                                                                    SqlColumnTypeDefinition columnType);

    /// Adds a new nullable column to the table only if it does not already exist.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddNotRequiredColumnIfNotExists(std::string columnName,
                                                                               SqlColumnTypeDefinition columnType);

    /// Drops a column from the table only if it exists.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& DropColumnIfExists(std::string_view columnName);

    /// Add an index to the table for the specified column.
    /// @param columnName The name of the column to index.
    ///
    /// @code
    /// SqlQueryBuilder q;
    /// q.Migration().AlterTable("Table").AddIndex("column");
    /// // Will execute CREATE INDEX "Table_column_index" ON "Table" ("column");
    /// @endcode
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddIndex(std::string_view columnName);

    /// Add an index to the table for the specified column that is unique.
    /// @param columnName The name of the column to index.
    ///
    /// @code
    /// SqlQueryBuilder q;
    /// q.Migration().AlterTable("Table").AddUniqueIndex("column");
    /// // Will execute CREATE UNIQUE INDEX "Table_column_index" ON "Table" ("column");
    /// @endcode
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddUniqueIndex(std::string_view columnName);

    /// Drop an index from the table for the specified column.
    /// @param columnName The name of the column to drop the index from.
    ///
    /// @code
    /// SqlQueryBuilder q;
    /// q.Migration().AlterTable("Table").DropIndex("column");
    /// // Will execute DROP INDEX "Table_column_index";
    /// @endcode
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& DropIndex(std::string_view columnName);

    /// Drop an index from the table only if it exists.
    /// @param columnName The name of the column to drop the index from.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& DropIndexIfExists(std::string_view columnName);

    /// Adds a foreign key column @p columnName to @p referencedColumn to an existing column.
    ///
    /// @param columnName The name of the column to add.
    /// @param referencedColumn The column to reference.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddForeignKey(std::string columnName,
                                                             SqlForeignKeyReferenceDefinition referencedColumn);

    /// Adds a foreign key column @p columnName of type @p columnType to @p referencedColumn.
    ///
    /// @param columnName The name of the column to add.
    /// @param columnType The type of the column to add.
    /// @param referencedColumn The column to reference.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddForeignKeyColumn(std::string columnName,
                                                                   SqlColumnTypeDefinition columnType,
                                                                   SqlForeignKeyReferenceDefinition referencedColumn);

    /// Adds a nullable foreign key column @p columnName of type @p columnType to @p referencedColumn.
    ///
    /// @param columnName The name of the column to add.
    /// @param columnType The type of the column to add.
    /// @param referencedColumn The column to reference.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddNotRequiredForeignKeyColumn(
        std::string columnName, SqlColumnTypeDefinition columnType, SqlForeignKeyReferenceDefinition referencedColumn);

    /// Drops a foreign key for the column @p columnName from the table.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& DropForeignKey(std::string columnName);

    /// Adds a composite foreign key constraint.
    ///
    /// @param columns The columns in the current table.
    /// @param referencedTableName The referenced table name.
    /// @param referencedColumns The referenced columns in the referenced table.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder& AddCompositeForeignKey(std::vector<std::string> columns,
                                                                      std::string referencedTableName,
                                                                      std::vector<std::string> referencedColumns);

  private:
    SqlAlterTablePlan& _plan;
};

namespace detail
{
    template <typename Record>
    void PopulateCreateTableBuilder(SqlCreateTableQueryBuilder& builder)
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        constexpr auto ctx = std::meta::access_context::current();
        template for (constexpr auto el: define_static_array(nonstatic_data_members_of(^^Record, ctx)))
        {
            using FieldType = typename[:std::meta::type_of(el):];
            if constexpr (FieldWithStorage<FieldType>)
            {
                if constexpr (IsAutoIncrementPrimaryKey<FieldType>)
                    builder.PrimaryKeyWithAutoIncrement(
                        std::string(FieldNameOf<el>),
                        detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
                else if constexpr (FieldType::IsPrimaryKey)
                    builder.PrimaryKey(std::string(FieldNameOf<el>),
                                       detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
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
                    builder.ForeignKey(
                        std::string(FieldNameOf<el>),
                        detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value,
                        SqlForeignKeyReferenceDefinition {
                            .tableName = std::string { RecordTableName<typename FieldType::ReferencedRecord> },
                            .columnName = std::string { FieldNameOf<FieldType::ReferencedField> } });
                }
                else if constexpr (FieldType::IsMandatory)
                    builder.RequiredColumn(std::string(FieldNameOf<el>),
                                           detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
                else
                    builder.Column(std::string(FieldNameOf<el>),
                                   detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
            }
        }
#else
        Reflection::EnumerateMembers<Record>([&builder]<size_t I, typename FieldType>() {
            if constexpr (FieldWithStorage<FieldType>)
            {
                if constexpr (IsAutoIncrementPrimaryKey<FieldType>)
                    builder.PrimaryKeyWithAutoIncrement(
                        std::string(FieldNameAt<I, Record>()),
                        detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
                else if constexpr (FieldType::IsPrimaryKey)
                    builder.PrimaryKey(std::string(FieldNameAt<I, Record>()),
                                       detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
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
                    builder.ForeignKey(
                        std::string(FieldNameAt<I, Record>()),
                        detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value,
                        SqlForeignKeyReferenceDefinition {
                            .tableName = std::string { RecordTableName<typename FieldType::ReferencedRecord> },
                            .columnName =
                                std::string { FieldNameAt<referencedFieldIndex, typename FieldType::ReferencedRecord>() } });
                }
                else if constexpr (FieldType::IsMandatory)
                    builder.RequiredColumn(std::string(FieldNameAt<I, Record>()),
                                           detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
                else
                    builder.Column(std::string(FieldNameAt<I, Record>()),
                                   detail::SqlColumnTypeDefinitionOf<typename FieldType::ValueType>::value);
            }
        });
#endif
    }
} // namespace detail

/// @brief Query builder for building INSERT queries in migrations.
///
/// @see SqlMigrationQueryBuilder
/// @ingroup QueryBuilder
class [[nodiscard]] SqlMigrationInsertBuilder final
{
  public:
    /// Constructs a migration INSERT builder.
    explicit SqlMigrationInsertBuilder(SqlInsertDataPlan& plan):
        _plan { plan }
    {
    }

    /// Sets a column value for the INSERT.
    template <typename T>
    SqlMigrationInsertBuilder& Set(std::string columnName, T const& value)
    {
        _plan.columns.emplace_back(std::move(columnName), SqlVariant(value));
        return *this;
    }

  private:
    SqlInsertDataPlan& _plan;
};

/// @brief Query builder for building UPDATE queries in migrations.
///
/// @see SqlMigrationQueryBuilder
/// @ingroup QueryBuilder
class [[nodiscard]] SqlMigrationUpdateBuilder final
{
  public:
    /// Constructs a migration UPDATE builder.
    explicit SqlMigrationUpdateBuilder(SqlUpdateDataPlan& plan):
        _plan { plan }
    {
    }

    /// Sets a column value for the UPDATE.
    template <typename T>
    SqlMigrationUpdateBuilder& Set(std::string columnName, T const& value)
    {
        _plan.setColumns.emplace_back(std::move(columnName), SqlVariant(value));
        return *this;
    }

    /// Adds a WHERE condition to the UPDATE.
    template <typename T>
    SqlMigrationUpdateBuilder& Where(std::string columnName, std::string op, T const& value)
    {
        _plan.whereColumn = std::move(columnName);
        _plan.whereOp = std::move(op);
        _plan.whereValue = SqlVariant(value);
        return *this;
    }

  private:
    SqlUpdateDataPlan& _plan;
};

/// @brief Query builder for building DELETE queries in migrations.
///
/// @see SqlMigrationQueryBuilder
/// @ingroup QueryBuilder
class [[nodiscard]] SqlMigrationDeleteBuilder final
{
  public:
    /// Constructs a migration DELETE builder.
    explicit SqlMigrationDeleteBuilder(SqlDeleteDataPlan& plan):
        _plan { plan }
    {
    }

    /// Adds a WHERE condition to the DELETE.
    template <typename T>
    SqlMigrationDeleteBuilder& Where(std::string columnName, std::string op, T const& value)
    {
        _plan.whereColumn = std::move(columnName);
        _plan.whereOp = std::move(op);
        _plan.whereValue = SqlVariant(value);
        return *this;
    }

  private:
    SqlDeleteDataPlan& _plan;
};

/// @brief Query builder for building SQL migration queries.
/// @ingroup QueryBuilder
class [[nodiscard]] SqlMigrationQueryBuilder final
{
  public:
    /// Constructs a migration query builder.
    explicit SqlMigrationQueryBuilder(SqlQueryFormatter const& formatter):
        _formatter { formatter },
        _migrationPlan { .formatter = formatter }
    {
    }

    /// Sets the schema name for the migration.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& WithSchema(std::string schemaName);

    /// Creates a new database.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& CreateDatabase(std::string_view databaseName);

    /// Drops a database.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& DropDatabase(std::string_view databaseName);

    /// Creates a new table.
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder CreateTable(std::string_view tableName);

    /// Creates a new table only if it does not already exist.
    ///
    /// @note Database support:
    /// - SQLite: Native CREATE TABLE IF NOT EXISTS
    /// - PostgreSQL: Native CREATE TABLE IF NOT EXISTS
    /// - SQL Server: Uses conditional IF NOT EXISTS block
    LIGHTWEIGHT_API SqlCreateTableQueryBuilder CreateTableIfNotExists(std::string_view tableName);

    /// Alters an existing table.
    LIGHTWEIGHT_API SqlAlterTableQueryBuilder AlterTable(std::string_view tableName);

    /// Drops a table.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& DropTable(std::string_view tableName);

    /// Drops a table if it exists.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& DropTableIfExists(std::string_view tableName);

    /// Drops a table and all foreign key constraints referencing it.
    /// On PostgreSQL, uses CASCADE. On MS SQL, drops FK constraints first.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& DropTableCascade(std::string_view tableName);

    /// Creates a new table for the given record type.
    template <typename Record>
    SqlCreateTableQueryBuilder CreateTable()
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");

        auto builder = CreateTable(RecordTableName<Record>);
        detail::PopulateCreateTableBuilder<Record>(builder);
        return builder;
    }

    /// Alters an existing table.
    template <typename Record>
    SqlAlterTableQueryBuilder AlterTable()
    {
        static_assert(DataMapperRecord<Record>, "Record must satisfy DataMapperRecord");
        return AlterTable(RecordTableName<Record>);
    }

    /// Executes raw SQL.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& RawSql(std::string_view sql);

    /// Creates an index on a table.
    ///
    /// @param indexName The name of the index to create.
    /// @param tableName The name of the table to create the index on.
    /// @param columns The columns to include in the index.
    /// @param unique If true, creates a UNIQUE index.
    ///
    /// @code
    /// SqlQueryBuilder q;
    /// q.Migration().CreateIndex("idx_user_email", "users", {"email"});
    /// // Will execute CREATE INDEX "idx_user_email" ON "users" ("email");
    /// @endcode
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& CreateIndex(std::string indexName,
                                                          std::string tableName,
                                                          std::vector<std::string> columns,
                                                          bool unique = false);

    /// Creates a unique index on a table.
    ///
    /// @param indexName The name of the index to create.
    /// @param tableName The name of the table to create the index on.
    /// @param columns The columns to include in the index.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& CreateUniqueIndex(std::string indexName,
                                                                std::string tableName,
                                                                std::vector<std::string> columns);

    /// Creates an index on a table with auto-generated name.
    ///
    /// The index name is automatically generated as `idx_{tableName}_{col1}_{col2}_...`
    ///
    /// @param tableName The name of the table to create the index on.
    /// @param columns The columns to include in the index.
    /// @param type The type of index (NonUnique or Unique).
    ///
    /// @code
    /// SqlQueryBuilder q;
    /// q.Migration().CreateIndex("users", {"email"}, IndexType::Unique);
    /// // Will execute CREATE UNIQUE INDEX "idx_users_email" ON "users" ("email");
    /// @endcode
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& CreateIndex(std::string tableName,
                                                          std::vector<std::string> columns,
                                                          IndexType type = IndexType::NonUnique);

    /// Creates an INSERT statement for the migration.
    LIGHTWEIGHT_API SqlMigrationInsertBuilder Insert(std::string_view tableName);

    /// Creates an UPDATE statement for the migration.
    LIGHTWEIGHT_API SqlMigrationUpdateBuilder Update(std::string_view tableName);

    /// Creates a DELETE statement for the migration.
    LIGHTWEIGHT_API SqlMigrationDeleteBuilder Delete(std::string_view tableName);

    /// Executes SQL interactively via a callback.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& Native(std::function<std::string(SqlConnection&)> callback);

    /// Starts a transaction.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& BeginTransaction();

    /// Commits a transaction.
    LIGHTWEIGHT_API SqlMigrationQueryBuilder& CommitTransaction();

    /// Gets the migration plan.
    [[nodiscard]] LIGHTWEIGHT_API SqlMigrationPlan const& GetPlan() const&;

    /// Gets the migration plan.
    ///
    /// @note This method is destructive and will invalidate the current builder.
    LIGHTWEIGHT_API SqlMigrationPlan GetPlan() &&;

  private:
    SqlQueryFormatter const& _formatter;
    std::string _schemaName;
    SqlMigrationPlan _migrationPlan;
};

} // namespace Lightweight
