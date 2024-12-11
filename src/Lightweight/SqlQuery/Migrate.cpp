// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "Migrate.hpp"

SqlMigrationPlan SqlMigrationQueryBuilder::GetPlan()
{
    return _migrationPlan;
}

SqlMigrationQueryBuilder& SqlMigrationQueryBuilder::DropTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlDropTablePlan {
        .tableName = tableName,
    });
    return *this;
}

SqlCreateTableQueryBuilder SqlMigrationQueryBuilder::CreateTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlCreateTablePlan {
        .tableName = tableName,
        .columns = {},
    });
    return SqlCreateTableQueryBuilder { std::get<SqlCreateTablePlan>(_migrationPlan.steps.back()) };
}

SqlAlterTableQueryBuilder SqlMigrationQueryBuilder::AlterTable(std::string_view tableName)
{
    _migrationPlan.steps.emplace_back(SqlAlterTablePlan {
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

SqlAlterTableQueryBuilder& SqlAlterTableQueryBuilder::AddColumn(std::string_view columnName,
                                                                SqlColumnTypeDefinition columnType)
{
    _plan.commands.emplace_back(SqlAlterTableCommands::AddColumn {
        .columnName = columnName,
        .columnType = columnType,
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

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Column(SqlColumnDeclaration column)
{
    _plan.columns.emplace_back(std::move(column));
    return *this;
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::Column(std::string columnName,
                                                               SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
    });
}

SqlCreateTableQueryBuilder& SqlCreateTableQueryBuilder::RequiredColumn(std::string columnName,
                                                                       SqlColumnTypeDefinition columnType)
{
    return Column(SqlColumnDeclaration {
        .name = std::move(columnName),
        .type = columnType,
        .required = true,
    });
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
        .foreignKey = std::move(foreignKey),
        .required = true,
    });
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
