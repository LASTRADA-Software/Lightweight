// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlColumnTypeDefinitions.hpp"
#include "../SqlDataBinder.hpp"
#include "../Utils.hpp"

#include <reflection-cpp/reflection.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

class SqlQueryFormatter;

namespace detail
{

template <typename T>
struct SqlColumnTypeDefinitionOf
{
    static_assert(AlwaysFalse<T>, "Unsupported type for SQL column definition.");
};

template <>
struct SqlColumnTypeDefinitionOf<bool>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Bool {};
};

template <>
struct SqlColumnTypeDefinitionOf<char>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Char { 1 };
};

template <>
struct SqlColumnTypeDefinitionOf<SqlDate>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Date {};
};

template <>
struct SqlColumnTypeDefinitionOf<SqlDateTime>
{
    static constexpr auto value = SqlColumnTypeDefinitions::DateTime {};
};

template <>
struct SqlColumnTypeDefinitionOf<SqlTime>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Time {};
};

template <size_t Precision, size_t Scale>
struct SqlColumnTypeDefinitionOf<SqlNumeric<Precision, Scale>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Decimal { .precision = Precision, .scale = Scale };
};

template <>
struct SqlColumnTypeDefinitionOf<SqlGuid>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Guid {};
};

template <typename T>
    requires(detail::OneOf<T, int16_t, uint16_t>)
struct SqlColumnTypeDefinitionOf<T>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Smallint {};
};

template <typename T>
    requires(detail::OneOf<T, int32_t, uint32_t>)
struct SqlColumnTypeDefinitionOf<T>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Integer {};
};

template <typename T>
    requires(detail::OneOf<T, int64_t, uint64_t>)
struct SqlColumnTypeDefinitionOf<T>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Bigint {};
};

template <typename T>
    requires(detail::OneOf<T, float, double>)
struct SqlColumnTypeDefinitionOf<T>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Real {};
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char>)
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::VARIABLE_SIZE>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Varchar { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char16_t, char32_t, wchar_t>)
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::VARIABLE_SIZE>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::NVarchar { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char>)
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::FIXED_SIZE>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Char { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char16_t, char32_t, wchar_t>)
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::FIXED_SIZE>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::NChar { N };
};

template <size_t N, typename CharT>
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Char { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char16_t, char32_t, wchar_t>)
struct SqlColumnTypeDefinitionOf<SqlFixedString<N, CharT, SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::NChar { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char>)
struct SqlColumnTypeDefinitionOf<SqlDynamicString<N, CharT>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::Varchar { N };
};

template <size_t N, typename CharT>
    requires(detail::OneOf<CharT, char8_t, char16_t, char32_t, wchar_t>)
struct SqlColumnTypeDefinitionOf<SqlDynamicString<N, CharT>>
{
    static constexpr auto value = SqlColumnTypeDefinitions::NVarchar { N };
};

template <typename T>
struct SqlColumnTypeDefinitionOf<std::optional<T>>
{
    static constexpr auto value = SqlColumnTypeDefinitionOf<T>::value;
};

} // namespace detail

/// @brief Represents a SQL column type definition of T.
///
/// @ingroup QueryBuilder
template <typename T>
constexpr auto SqlColumnTypeDefinitionOf = detail::SqlColumnTypeDefinitionOf<T>::value;

/// @brief Represents a primary key type.
///
/// This enumeration represents the primary key type of a column.
///
/// @ingroup QueryBuilder
enum class SqlPrimaryKeyType : uint8_t
{
    NONE,
    MANUAL,
    AUTO_INCREMENT,
    GUID,
};

/// @brief Represents a foreign key reference definition.
///
/// @ingroup QueryBuilder
struct SqlForeignKeyReferenceDefinition
{
    /// The table name that the foreign key references.
    std::string tableName;

    /// The column name that the foreign key references.
    std::string columnName;
};

/// @brief Represents a SQL column declaration.
///
/// @ingroup QueryBuilder
struct SqlColumnDeclaration
{
    /// The name of the column.
    std::string name;

    /// The type of the column.
    SqlColumnTypeDefinition type;

    /// The primary key type of the column.
    SqlPrimaryKeyType primaryKey { SqlPrimaryKeyType::NONE };

    /// The foreign key reference definition of the column.
    std::optional<SqlForeignKeyReferenceDefinition> foreignKey {};

    /// Indicates if the column is required (non-nullable).
    bool required { false };

    /// Indicates if the column is unique.
    bool unique { false };

    /// Indicates if the column is indexed.
    bool index { false };
};

struct SqlCreateTablePlan
{
    std::string_view tableName;
    std::vector<SqlColumnDeclaration> columns;
};

enum class SqlNullable : uint8_t
{
    NotNull,
    Null,
};

namespace SqlAlterTableCommands
{

struct RenameTable
{
    std::string_view newTableName;
};

struct AddColumn
{
    std::string columnName;
    SqlColumnTypeDefinition columnType;
    SqlNullable nullable = SqlNullable::Null;
};

struct AlterColumn
{
    std::string columnName;
    SqlColumnTypeDefinition columnType;
    SqlNullable nullable = SqlNullable::Null;
};

struct AddIndex
{
    std::string_view columnName;
    bool unique = false;
};

struct RenameColumn
{
    std::string_view oldColumnName;
    std::string_view newColumnName;
};

struct DropColumn
{
    std::string_view columnName;
};

struct DropIndex
{
    std::string_view columnName;
};

struct AddForeignKey
{
    std::string columnName;
    SqlForeignKeyReferenceDefinition referencedColumn;
};

struct DropForeignKey
{
    std::string columnName;
};

} // namespace SqlAlterTableCommands

/// @brief Represents a single SQL ALTER TABLE command.
///
/// @ingroup QueryBuilder
using SqlAlterTableCommand = std::variant<SqlAlterTableCommands::RenameTable,
                                          SqlAlterTableCommands::AddColumn,
                                          SqlAlterTableCommands::AlterColumn,
                                          SqlAlterTableCommands::AddIndex,
                                          SqlAlterTableCommands::RenameColumn,
                                          SqlAlterTableCommands::DropColumn,
                                          SqlAlterTableCommands::DropIndex,
                                          SqlAlterTableCommands::AddForeignKey,
                                          SqlAlterTableCommands::DropForeignKey>;

/// @brief Represents a SQL ALTER TABLE plan on a given table.
///
/// @ingroup QueryBuilder
struct SqlAlterTablePlan
{
    /// The name of the table to alter.
    std::string_view tableName;

    /// The list of commands to execute on the table.
    std::vector<SqlAlterTableCommand> commands;
};

/// @brief Represents a SQL DROP TABLE plan.
///
/// @ingroup QueryBuilder
struct SqlDropTablePlan
{
    /// The name of the table to drop.
    std::string_view tableName;
};

// clang-format off

/// @brief Represents a single SQL migration plan element.
///
/// This variant represents a single SQL migration plan element.
///
/// @ingroup QueryBuilder
using SqlMigrationPlanElement = std::variant<
    SqlCreateTablePlan,
    SqlAlterTablePlan,
    SqlDropTablePlan
>;

// clang-format on

/// Formats the given SQL migration plan element as a list of SQL statements.
///
/// @param formatter The SQL query formatter to use.
/// @param element The SQL migration plan element to format.
///
/// @return A list of SQL statements.
///
/// @ingroup QueryBuilder
[[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> ToSql(SqlQueryFormatter const& formatter,
                                                             SqlMigrationPlanElement const& element);

/// @brief Represents a SQL migration plan.
///
/// This structure represents a SQL migration plan that can be executed on a database.
///
/// @ingroup QueryBuilder
struct [[nodiscard]] SqlMigrationPlan
{
    SqlQueryFormatter const& formatter;
    std::vector<SqlMigrationPlanElement> steps {};

    [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> ToSql() const;
};
