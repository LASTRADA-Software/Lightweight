// SPDX-License-Identifier: Apache-2.0

module;

#include <reflection-cpp/reflection.hpp>

#include "Lightweight.hpp"
#include "SqlErrorDetection.hpp"
#include "SqlMigrationLock.hpp"
#include "ThreadSafeQueue.hpp"
#include "DataBinder/SqlRawColumn.hpp"
#include "DataMapper/QueryBuilders.hpp"
#include "QueryFormatter/PostgreSqlFormatter.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "QueryFormatter/SqlServerFormatter.hpp"
#include "SqlBackup/MsgPackChunkFormats.hpp"
#include "SqlBackup/Sha256.hpp"
#include "SqlBackup/SqlBackup.hpp"
#include "SqlBackup/TableFilter.hpp"
#include "Tools/CxxModelPrinter.hpp"
#include "Zip/ZipArchive.hpp"
#include "Zip/ZipError.hpp"

export module Lightweight;

export namespace Lightweight {
    using Lightweight::AliasedTableName;
    using Lightweight::BelongsTo;
    using Lightweight::BuildConnectionString;
    using Lightweight::ConvertWindows1252ToUtf8;
    using Lightweight::DataMapper;
    using Lightweight::DataMapperOptions;
    using Lightweight::DataMapperRecord;
    using Lightweight::DataMapperRecords;
    using Lightweight::DifferenceView;
    using Lightweight::Field;
    using Lightweight::FieldNameAt;
    using Lightweight::FieldNameOf;
    using Lightweight::FieldWithStorage;
    using Lightweight::FormatName;
    using Lightweight::FormatType;
    using Lightweight::FullyQualifiedNameOf;
    using Lightweight::FullyQualifiedNamesOf;
    using Lightweight::GetPrimaryKeyField;
    using Lightweight::HasAutoIncrementPrimaryKey;
    using Lightweight::HasMany;
    using Lightweight::HasManyThrough;
    using Lightweight::HasOneThrough;
    using Lightweight::HasPrimaryKey;
    using Lightweight::IndexType;
    using Lightweight::Int64DataBinderHelper;
    using Lightweight::IsAutoIncrementPrimaryKey;
    using Lightweight::IsBelongsTo;
    using Lightweight::IsField;
    using Lightweight::IsForeignKeyViolation;
    using Lightweight::IsHasMany;
    using Lightweight::IsHasManyThrough;
    using Lightweight::IsHasOneThrough;
    using Lightweight::IsOptionalBelongsTo;
    using Lightweight::IsPrimaryKey;
    using Lightweight::IsSpecializationOf;
    using Lightweight::IsSqlDynamicBinary;
    using Lightweight::IsSqlDynamicString;
    using Lightweight::IsSqlFixedString;
    using Lightweight::IsTableAlreadyExistsError;
    using Lightweight::IsTableNotFoundError;
    using Lightweight::IsTransientError;
    using Lightweight::IsUniqueConstraintViolation;
    using Lightweight::LogIfFailed;
    using Lightweight::MakeColumnTypeFromNative;
    using Lightweight::MapFromRecordFields;
    using Lightweight::MemberClassType;
    using Lightweight::MemberIndexOf;
    using Lightweight::NotSqlElements;
    using Lightweight::ParseConnectionString;
    using Lightweight::PostgreSqlFormatter;
    using Lightweight::PrimaryKey;
    using Lightweight::QualifiedColumnName;
    using Lightweight::RecordPrimaryKeyIndex;
    using Lightweight::RecordPrimaryKeyOf;
    using Lightweight::RecordPrimaryKeyType;
    using Lightweight::RecordStorageFieldCount;
    using Lightweight::RecordTableName;
    using Lightweight::RecordWithStorageFields;
    using Lightweight::ReferencedFieldTypeOf;
    using Lightweight::RequireSuccess;
    using Lightweight::SqlAllFieldsQueryBuilder;
    using Lightweight::SqlAlterTableCommand;
    using Lightweight::SqlAlterTablePlan;
    using Lightweight::SqlAlterTableQueryBuilder;
    using Lightweight::SqlAnsiString;
    using Lightweight::SqlBasicSelectQueryBuilder;
    using Lightweight::SqlBasicStringBinderConcept;
    using Lightweight::SqlBasicStringOperations;
    using Lightweight::SqlBinary;
    using Lightweight::SqlColumnDeclaration;
    using Lightweight::SqlColumnTypeDefinition;
    using Lightweight::SqlColumnTypeDefinitionOf;
    using Lightweight::SqlCompositeForeignKeyConstraint;
    using Lightweight::SqlConnectInfo;
    using Lightweight::SqlConnection;
    using Lightweight::SqlConnectionDataSource;
    using Lightweight::SqlConnectionString;
    using Lightweight::SqlConnectionStringMap;
    using Lightweight::SqlCoreDataMapperQueryBuilder;
    using Lightweight::SqlCreateIndexPlan;
    using Lightweight::SqlCreateTablePlan;
    using Lightweight::SqlCreateTableQueryBuilder;
    using Lightweight::SqlDataBinder;
    using Lightweight::SqlDataBinderCallback;
    using Lightweight::SqlDataBinderSupportsInspect;
    using Lightweight::SqlDate;
    using Lightweight::SqlDateTime;
    using Lightweight::SqlDeleteDataPlan;
    using Lightweight::SqlDeleteQueryBuilder;
    using Lightweight::SqlDropTablePlan;
    using Lightweight::SqlDynamicAnsiString;
    using Lightweight::SqlDynamicBinary;
    using Lightweight::SqlDynamicString;
    using Lightweight::SqlDynamicUtf16String;
    using Lightweight::SqlDynamicUtf32String;
    using Lightweight::SqlDynamicWideString;
    using Lightweight::SqlElements;
    using Lightweight::SqlError;
    using Lightweight::SqlErrorCategory;
    using Lightweight::SqlErrorInfo;
    using Lightweight::SqlException;
    using Lightweight::SqlFieldExpression;
    using Lightweight::SqlFixedString;
    using Lightweight::SqlFixedStringMode;
    using Lightweight::SqlForeignKeyReferenceDefinition;
    using Lightweight::SqlGetColumnNativeType;
    using Lightweight::SqlGuid;
    using Lightweight::SqlInputParameterBatchBinder;
    using Lightweight::SqlInputParameterBinder;
    using Lightweight::SqlInsertDataPlan;
    using Lightweight::SqlInsertQueryBuilder;
    using Lightweight::SqlIsolationMode;
    using Lightweight::SQLiteQueryFormatter;
    using Lightweight::SqlJoinConditionBuilder;
    using Lightweight::SqlLastInsertIdQuery;
    using Lightweight::SqlLogger;
    using Lightweight::SqlMaxDynamicAnsiString;
    using Lightweight::SqlMaxDynamicWideString;
    using Lightweight::SqlMigrationDeleteBuilder;
    using Lightweight::SqlMigrationInsertBuilder;
    using Lightweight::SqlMigrationPlan;
    using Lightweight::SqlMigrationPlanElement;
    using Lightweight::SqlMigrationQueryBuilder;
    using Lightweight::SqlMigrationUpdateBuilder;
    using Lightweight::SqlNullable;
    using Lightweight::SqlNullType;
    using Lightweight::SqlNullValue;
    using Lightweight::SqlNumeric;
    using Lightweight::SqlNumericType;
    using Lightweight::SqlOptimalMaxColumnSize;
    using Lightweight::SqlOutputColumnBinder;
    using Lightweight::SqlPrimaryKeyType;
    using Lightweight::SqlQualifiedTableColumnName;
    using Lightweight::SqlQualifiedTableColumnName;
    using Lightweight::SqlQueryBuilder;
    using Lightweight::SqlQueryBuilderMode;
    using Lightweight::SqlQueryFormatter;
    using Lightweight::SqlQueryObject;
    using Lightweight::SqlRawColumn;
    using Lightweight::SqlRawColumnMetadata;
    using Lightweight::SqlRawColumnNameView;
    using Lightweight::SqlRawSqlPlan;
    using Lightweight::SqlRealName;
    using Lightweight::SqlRequireLoadedError;
    using Lightweight::SqlResultCursor;
    using Lightweight::SqlResultOrdering;
    using Lightweight::SqlRowIterator;
    using Lightweight::SqlScopedTimeLogger;
    using Lightweight::SqlScopedTraceLogger;
    using Lightweight::SqlSearchCondition;
    using Lightweight::SqlSelectQueryBuilder;
    using Lightweight::SqlSentinelIterator;
    using Lightweight::SqlServerQueryFormatter;
    using Lightweight::SqlServerType;
    using Lightweight::SqlSimpleDataBinder;
    using Lightweight::SqlStatement;
    using Lightweight::SqlString;
    using Lightweight::SqlText;
    using Lightweight::SqlTime;
    using Lightweight::SqlTransaction;
    using Lightweight::SqlTransactionException;
    using Lightweight::SqlTransactionMode;
    using Lightweight::SqlTrimmedFixedString;
    using Lightweight::SqlTrimmedWideFixedString;
    using Lightweight::SqlUpdateDataPlan;
    using Lightweight::SqlUpdateQueryBuilder;
    using Lightweight::SqlUtf16String;
    using Lightweight::SqlUtf32String;
    using Lightweight::SqlVariant;
    using Lightweight::SqlVariantRow;
    using Lightweight::SqlVariantRowCursor;
    using Lightweight::SqlVariantRowIterator;
    using Lightweight::SqlWhereClauseBuilder;
    using Lightweight::SqlWideString;
    using Lightweight::SqlWildcardType;
    using Lightweight::TableName;
    using Lightweight::ThreadSafeQueue;
    using Lightweight::ToSql;
    using Lightweight::ToStdWideString;
    using Lightweight::ToUtf16;
    using Lightweight::ToUtf32;
    using Lightweight::ToUtf8;
    using Lightweight::UniqueNameBuilder;
    using Lightweight::Unwrap;

    using Lightweight::is_belongs_to;

    using Lightweight::operator!=;
    using Lightweight::operator<<;
    using Lightweight::operator==;

    namespace Aggregate {
        using Lightweight::Aggregate::Avg;
        using Lightweight::Aggregate::Count;
        using Lightweight::Aggregate::Max;
        using Lightweight::Aggregate::Min;
        using Lightweight::Aggregate::Sum;
    }

    namespace SqlAlterTableColumn {
        using Lightweight::SqlAlterTableCommands::AddColumn;
        using Lightweight::SqlAlterTableCommands::AddColumnIfNotExists;
        using Lightweight::SqlAlterTableCommands::AddCompositeForeignKey;
        using Lightweight::SqlAlterTableCommands::AddForeignKey;
        using Lightweight::SqlAlterTableCommands::AddIndex;
        using Lightweight::SqlAlterTableCommands::AlterColumn;
        using Lightweight::SqlAlterTableCommands::DropColumn;
        using Lightweight::SqlAlterTableCommands::DropColumnIfExists;
        using Lightweight::SqlAlterTableCommands::DropForeignKey;
        using Lightweight::SqlAlterTableCommands::DropIndex;
        using Lightweight::SqlAlterTableCommands::DropIndexIfExists;
        using Lightweight::SqlAlterTableCommands::RenameColumn;
        using Lightweight::SqlAlterTableCommands::RenameTable;
    }

    namespace SqlBackup {
        using Lightweight::SqlBackup::Backup;
        using Lightweight::SqlBackup::BackupSettings;
        using Lightweight::SqlBackup::BackupValue;
        using Lightweight::SqlBackup::CalculateRestoreSettings;
        using Lightweight::SqlBackup::ChunkReader;
        using Lightweight::SqlBackup::ChunkWriter;
        using Lightweight::SqlBackup::ColumnBatch;
        using Lightweight::SqlBackup::CompressionMethod;
        using Lightweight::SqlBackup::CompressionMethodName;
        using Lightweight::SqlBackup::CreateMetadata;
        using Lightweight::SqlBackup::CreateMsgPackChunkReader;
        using Lightweight::SqlBackup::CreateMsgPackChunkReaderFromBuffer;
        using Lightweight::SqlBackup::CreateMsgPackChunkWriter;
        using Lightweight::SqlBackup::ErrorTrackingProgressManager;
        using Lightweight::SqlBackup::GetAvailableSystemMemory;
        using Lightweight::SqlBackup::GetSupportedCompressionMethods;
        using Lightweight::SqlBackup::IsCompressionMethodSupported;
        using Lightweight::SqlBackup::NullProgressManager;
        using Lightweight::SqlBackup::ParseSchema;
        using Lightweight::SqlBackup::Progress;
        using Lightweight::SqlBackup::ProgressManager;
        using Lightweight::SqlBackup::Restore;
        using Lightweight::SqlBackup::RestoreSettings;
        using Lightweight::SqlBackup::RetrySettings;
        using Lightweight::SqlBackup::Sha256;
        using Lightweight::SqlBackup::TableFilter;
        using Lightweight::SqlBackup::TableInfo;
    }

    namespace SqlColumnTypeDefinitions {
        using Lightweight::SqlColumnTypeDefinitions::Bigint;
        using Lightweight::SqlColumnTypeDefinitions::Binary;
        using Lightweight::SqlColumnTypeDefinitions::Bool;
        using Lightweight::SqlColumnTypeDefinitions::Char;
        using Lightweight::SqlColumnTypeDefinitions::Date;
        using Lightweight::SqlColumnTypeDefinitions::DateTime;
        using Lightweight::SqlColumnTypeDefinitions::Decimal;
        using Lightweight::SqlColumnTypeDefinitions::Guid;
        using Lightweight::SqlColumnTypeDefinitions::Integer;
        using Lightweight::SqlColumnTypeDefinitions::NChar;
        using Lightweight::SqlColumnTypeDefinitions::NVarchar;
        using Lightweight::SqlColumnTypeDefinitions::Real;
        using Lightweight::SqlColumnTypeDefinitions::Smallint;
        using Lightweight::SqlColumnTypeDefinitions::Text;
        using Lightweight::SqlColumnTypeDefinitions::Time;
        using Lightweight::SqlColumnTypeDefinitions::Timestamp;
        using Lightweight::SqlColumnTypeDefinitions::Tinyint;
        using Lightweight::SqlColumnTypeDefinitions::VarBinary;
        using Lightweight::SqlColumnTypeDefinitions::Varchar;
    }

    namespace SqlMigration {
        using Lightweight::SqlMigration::ChecksumVerificationResult;
        using Lightweight::SqlMigration::Migration;
        using Lightweight::SqlMigration::MigrationBase;
        using Lightweight::SqlMigration::MigrationLock;
        using Lightweight::SqlMigration::MigrationManager;
        using Lightweight::SqlMigration::MigrationStatus;
        using Lightweight::SqlMigration::MigrationTimestamp;
        using Lightweight::SqlMigration::RevertResult;
    }

    namespace SqlSchema {
        using Lightweight::SqlSchema::AllForeignKeysTo;
        using Lightweight::SqlSchema::Column;
        using Lightweight::SqlSchema::EventHandler;
        using Lightweight::SqlSchema::ForeignKeyConstraint;
        using Lightweight::SqlSchema::FullyQualifiedTableColumn;
        using Lightweight::SqlSchema::FullyQualifiedTableColumnSequence;
        using Lightweight::SqlSchema::FullyQualifiedTableName;
        using Lightweight::SqlSchema::IndexDefinition;
        using Lightweight::SqlSchema::KeyPair;
        using Lightweight::SqlSchema::MakeCreateTablePlan;
        using Lightweight::SqlSchema::ReadAllTables;
        using Lightweight::SqlSchema::ReadAllTablesCallback;
        using Lightweight::SqlSchema::Table;
        using Lightweight::SqlSchema::TableFilterPredicate;
        using Lightweight::SqlSchema::TableList;
        using Lightweight::SqlSchema::TableReadyCallback;

        using Lightweight::SqlSchema::operator<;
    }

    namespace Tools {
        using Lightweight::Tools::ColumnNameOverrides;
        using Lightweight::Tools::CxxModelPrinter;
    }

    namespace Zip {
        using Lightweight::Zip::CompressionMethod;
        using Lightweight::Zip::EntryInfo;
        using Lightweight::Zip::IsCompressionSupported;
        using Lightweight::Zip::OpenMode;
        using Lightweight::Zip::ZipArchive;
        using Lightweight::Zip::ZipEntry;
        using Lightweight::Zip::ZipError;
        using Lightweight::Zip::ZipErrorCode;
    }
}

export namespace std {
    using ::make_error_code;
    using std::formatter;
    using std::hash;
    using std::is_error_code_enum;
}

export {
    using ::SqlStringInterface;
}

export namespace Light = Lightweight;
