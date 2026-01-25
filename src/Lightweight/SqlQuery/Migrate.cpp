// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "Migrate.hpp"

namespace Lightweight
{

SqlMigrationPlan const& SqlMigrationQueryBuilder::GetPlan() const&
{
    return _migrationPlan;
}

SqlMigrationPlan SqlMigrationQueryBuilder::GetPlan() &&
{
    return std::move(_migrationPlan);
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::DropTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlDropTablePlan {
        .schemaName = {},
        .tableName = tableName,
        .ifExists = false,
    });
    return *this;
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::DropTableIfExists(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlDropTablePlan {
        .schemaName = {},
        .tableName = tableName,
        .ifExists = true,
    });
    return *this;
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::DropTableCascade(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlDropTablePlan {
        .schemaName = {},
        .tableName = tableName,
        .ifExists = true,
        .cascade = true,
    });
    return *this;
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::WithSchema(std::string schemaName)
{
    _schemaName = std::move(schemaName);
    return *this;
}

SqlCreateTableQueryBuilder SqlMigrationQueryBuilder::CreateTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlCreateTablePlan {
        .schemaName = _schemaName,
        .tableName = std::string(tableName),
        .columns = {},
        .foreignKeys = {},
        .ifNotExists = false,
    });
    return SqlCreateTableQueryBuilder { std::get<SqlCreateTablePlan>(_migrationPlan.steps.back()) };
}

SqlCreateTableQueryBuilder SqlMigrationQueryBuilder::CreateTableIfNotExists(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlCreateTablePlan {
        .schemaName = _schemaName,
        .tableName = std::string(tableName),
        .columns = {},
        .foreignKeys = {},
        .ifNotExists = true,
    });
    return SqlCreateTableQueryBuilder { std::get<SqlCreateTablePlan>(_migrationPlan.steps.back()) };
}

SqlAlterTableQueryBuilder SqlMigrationQueryBuilder::AlterTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlAlterTablePlan {
        .schemaName = _schemaName,
        .tableName = tableName,
        .commands = {},
    });
    return SqlAlterTableQueryBuilder { std::get<SqlAlterTablePlan>(_migrationPlan.steps.back()) };
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::RenameTo(std::string_view newTableName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::RenameTable {
        .newTableName = newTableName,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddColumn(std::string columnName, SqlColumnTypeDefinition columnType)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddColumn {
        .columnName = std::move(columnName),
        .columnType = columnType,
        .nullable = SqlNullable::NotNull,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddNotRequiredColumn(std::string columnName,
                                                                           SqlColumnTypeDefinition columnType)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddColumn {
        .columnName = std::move(columnName),
        .columnType = columnType,
        .nullable = SqlNullable::Null,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AlterColumn(std::string columnName,
                                                                  SqlColumnTypeDefinition columnType,
                                                                  SqlNullable nullable)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AlterColumn {
        .columnName = std::move(columnName),
        .columnType = columnType,
        .nullable = nullable,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::RenameColumn(std::string_view oldColumnName,
                                                                   std::string_view newColumnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::RenameColumn {
        .oldColumnName = oldColumnName,
        .newColumnName = newColumnName,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::DropColumn(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::DropColumn {
        .columnName = columnName,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddIndex(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddIndex {
        .columnName = columnName,
        .unique = false,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddUniqueIndex(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddIndex {
        .columnName = columnName,
        .unique = true,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::DropIndex(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::DropIndex {
        .columnName = columnName,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddColumnIfNotExists(std::string columnName,
                                                                           SqlColumnTypeDefinition columnType)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddColumnIfNotExists {
        .columnName = std::move(columnName),
        .columnType = columnType,
        .nullable = SqlNullable::NotNull,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddNotRequiredColumnIfNotExists(std::string columnName,
                                                                                      SqlColumnTypeDefinition columnType)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddColumnIfNotExists {
        .columnName = std::move(columnName),
        .columnType = columnType,
        .nullable = SqlNullable::Null,
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::DropColumnIfExists(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::DropColumnIfExists {
        .columnName = std::string(columnName),
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::DropIndexIfExists(std::string_view columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::DropIndexIfExists {
        .columnName = std::string(columnName),
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddForeignKey(std::string columnName,
                                                                    SqlForeignKeyReferenceDefinition referencedColumn)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddForeignKey {
        .columnName = std::move(columnName),
        .referencedColumn = std::move(referencedColumn),
    });
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddForeignKeyColumn(std::string columnName,
                                                                          SqlColumnTypeDefinition columnType,
                                                                          SqlForeignKeyReferenceDefinition referencedColumn)
{
    return AddColumn(columnName, columnType).AddForeignKey(std::move(columnName), std::move(referencedColumn));
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddNotRequiredForeignKeyColumn(
    std::string columnName, SqlColumnTypeDefinition columnType, SqlForeignKeyReferenceDefinition referencedColumn)
{
    AddNotRequiredColumn(columnName, columnType);
    AddForeignKey(std::move(columnName), std::move(referencedColumn));
    return *this;
}

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::DropForeignKey(std::string columnName)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::DropForeignKey {
        .columnName = std::move(columnName),
    });
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Column(SqlColumnDeclaration column)
{
    _plan.columns.emplace_back(std::move(column));
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Column(std::string columnName, SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .defaultValue = {},
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::RequiredColumn(std::string columnName,
                                                                       SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .required = true,
        .defaultValue = {},
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Timestamps()
{
    RequiredColumn("created_at", SqlColumnTypeDefinitions::DateTime {}).Index();
    RequiredColumn("updated_at", SqlColumnTypeDefinitions::DateTime {}).Index();
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::PrimaryKey(std::string columnName,
                                                                   SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .primaryKey = SqlPrimaryKeyType::MANUAL,
        .required = true,
        .unique = true,
        .defaultValue = {},
        .index = true,
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::PrimaryKeyWithAutoIncrement(std::string columnName,
                                                                                    SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .primaryKey = SqlPrimaryKeyType::AUTO_INCREMENT,
        .required = true,
        .unique = true,
        .defaultValue = {},
        .index = true,
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::ForeignKey(std::string columnName,
                                                                   SqlColumnTypeDefinition columnType,
                                                                   SqlForeignKeyReferenceDefinition foreignKey)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .primaryKey = SqlPrimaryKeyType::NONE,
        .foreignKey = std::move(foreignKey),
        .required = false,
        .defaultValue = {},
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::RequiredForeignKey(std::string columnName,
                                                                           SqlColumnTypeDefinition columnType,
                                                                           SqlForeignKeyReferenceDefinition foreignKey)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .primaryKey = SqlPrimaryKeyType::NONE,
        .foreignKey = std::move(foreignKey),
        .required = true,
        .defaultValue = {},
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::ForeignKey(std::vector<std::string> columns,
                                                                   std::string referencedTableName,
                                                                   std::vector<std::string> referencedColumns)
{
    _plan.foreignKeys.emplace_back(SqlCompositeForeignKeyConstraint {
        .columns = std::move(columns),
        .referencedTableName = std::move(referencedTableName),
        .referencedColumns = std::move(referencedColumns),
    });
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Unique()
{
    _plan.columns.back().unique = true;
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Index()
{
    _plan.columns.back().index = true;
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::UniqueIndex()
{
    _plan.columns.back().index = true;
    _plan.columns.back().unique = true;
    return *this;
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::RawSql(std::string_view sql)
{
    _migrationPlan.steps.emplace_back(SqlRawSqlPlan {
        .sql = sql,
    });
    return *this;
}

} // namespace Lightweight
