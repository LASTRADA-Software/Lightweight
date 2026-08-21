// SPDX-License-Identifier: Apache-2.0

#include "MigrationSourcePrinter.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <variant>

namespace Lightweight::Tools
{

namespace
{
    /// @brief Renders @p value as a C++ string literal, escaping what a literal cannot carry raw.
    [[nodiscard]] std::string QuoteCppString(std::string_view value)
    {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('"');
        for (char const c: value)
        {
            switch (c)
            {
                case '"':
                    out.append(R"(\")");
                    break;
                case '\\':
                    out.append(R"(\\)");
                    break;
                case '\n':
                    out.append(R"(\n)");
                    break;
                case '\r':
                    out.append(R"(\r)");
                    break;
                case '\t':
                    out.append(R"(\t)");
                    break;
                default:
                    out.push_back(c);
                    break;
            }
        }
        out.push_back('"');
        return out;
    }

    /// @brief The `SqlColumnTypeDefinitions` alternative name that spells @p T in source.
    ///
    /// Kept as one explicit mapping rather than derived from `typeid`, whose spelling is
    /// compiler-dependent and would make the emitted source differ between toolchains.
    ///
    /// @tparam T One alternative of `SqlColumnTypeDefinition`.
    /// @return The unqualified type name.
    template <typename T>
    [[nodiscard]] consteval std::string_view ColumnTypeName()
    {
        using namespace SqlColumnTypeDefinitions;
        if constexpr (std::same_as<T, Bigint>)
            return "Bigint";
        else if constexpr (std::same_as<T, Binary>)
            return "Binary";
        else if constexpr (std::same_as<T, Bool>)
            return "Bool";
        else if constexpr (std::same_as<T, Char>)
            return "Char";
        else if constexpr (std::same_as<T, Date>)
            return "Date";
        else if constexpr (std::same_as<T, DateTime>)
            return "DateTime";
        else if constexpr (std::same_as<T, Decimal>)
            return "Decimal";
        else if constexpr (std::same_as<T, Guid>)
            return "Guid";
        else if constexpr (std::same_as<T, Integer>)
            return "Integer";
        else if constexpr (std::same_as<T, NChar>)
            return "NChar";
        else if constexpr (std::same_as<T, NVarchar>)
            return "NVarchar";
        else if constexpr (std::same_as<T, Real>)
            return "Real";
        else if constexpr (std::same_as<T, Smallint>)
            return "Smallint";
        else if constexpr (std::same_as<T, Text>)
            return "Text";
        else if constexpr (std::same_as<T, Time>)
            return "Time";
        else if constexpr (std::same_as<T, Timestamp>)
            return "Timestamp";
        else if constexpr (std::same_as<T, Tinyint>)
            return "Tinyint";
        else if constexpr (std::same_as<T, VarBinary>)
            return "VarBinary";
        else
            return "Varchar";
    }

    /// @brief Renders a column type as the `SqlColumnTypeDefinitions::X { ... }` expression
    /// that reconstructs it.
    [[nodiscard]] std::string PrintColumnType(SqlColumnTypeDefinition const& type)
    {
        using namespace SqlColumnTypeDefinitions;
        return std::visit(
            []<typename T>(T const& t) -> std::string {
                if constexpr (std::same_as<T, Binary> || std::same_as<T, Char> || std::same_as<T, NChar>
                              || std::same_as<T, NVarchar> || std::same_as<T, Text> || std::same_as<T, VarBinary>
                              || std::same_as<T, Varchar>)
                    return std::format("SqlColumnTypeDefinitions::{} {{ {} }}", ColumnTypeName<T>(), t.size);
                else if constexpr (std::same_as<T, Decimal>)
                    return std::format(
                        "SqlColumnTypeDefinitions::Decimal {{ .precision = {}, .scale = {} }}", t.precision, t.scale);
                else if constexpr (std::same_as<T, Real>)
                    return std::format("SqlColumnTypeDefinitions::Real {{ .precision = {} }}", t.precision);
                else
                    return std::format("SqlColumnTypeDefinitions::{} {{}}", ColumnTypeName<T>());
            },
            type);
    }

    /// @brief Renders a nullability flag as its enumerator spelling.
    [[nodiscard]] std::string_view PrintNullable(SqlNullable nullable) noexcept
    {
        return nullable == SqlNullable::NotNull ? "SqlNullable::NotNull" : "SqlNullable::Null";
    }

    /// @brief Renders a `std::vector<std::string>` as a braced initializer list of literals.
    [[nodiscard]] std::string PrintStringList(std::span<std::string const> values)
    {
        // Index loop rather than `std::views::enumerate`: the latter does not compile over
        // `std::span` on every libc++ version this project builds against (see PluginDiscovery.cpp).
        std::string out = "{ ";
        for (auto const index: std::views::iota(std::size_t { 0 }, values.size()))
            out += std::format("{}{}", index == 0 ? "" : ", ", QuoteCppString(values[index]));
        out += " }";
        return out;
    }

    /// @brief Whether the fluent `CreateTable` shorthands can express @p column losslessly.
    ///
    /// `Unique()` / `Index()` are trailing modifiers and so do not disqualify a column;
    /// everything the shorthands have no parameter for does.
    [[nodiscard]] bool IsExpressibleAsShorthand(SqlColumnDeclaration const& column) noexcept
    {
        return column.defaultValue.empty() && column.primaryKeyIndex == 0 && column.primaryKey != SqlPrimaryKeyType::GUID;
    }

    /// @brief Renders a foreign-key reference as its aggregate initializer.
    [[nodiscard]] std::string PrintForeignKeyReference(SqlForeignKeyReferenceDefinition const& reference)
    {
        return std::format("SqlForeignKeyReferenceDefinition {{ .tableName = {}, .columnName = {} }}",
                           QuoteCppString(reference.tableName),
                           QuoteCppString(reference.columnName));
    }

    /// @brief Renders the primary-key kind as its enumerator spelling.
    [[nodiscard]] std::string_view PrintPrimaryKeyType(SqlPrimaryKeyType type) noexcept
    {
        switch (type)
        {
            case SqlPrimaryKeyType::NONE:
                return "SqlPrimaryKeyType::NONE";
            case SqlPrimaryKeyType::MANUAL:
                return "SqlPrimaryKeyType::MANUAL";
            case SqlPrimaryKeyType::AUTO_INCREMENT:
                return "SqlPrimaryKeyType::AUTO_INCREMENT";
            case SqlPrimaryKeyType::GUID:
                return "SqlPrimaryKeyType::GUID";
        }
        return "SqlPrimaryKeyType::NONE";
    }

    /// @brief Renders one column as the full `SqlColumnDeclaration` aggregate.
    ///
    /// The lossless fallback for columns the fluent shorthands cannot spell.
    [[nodiscard]] std::string PrintColumnDeclaration(SqlColumnDeclaration const& column)
    {
        std::string out = std::format("SqlColumnDeclaration {{ .name = {}, .type = {}, .primaryKey = {}",
                                      QuoteCppString(column.name),
                                      PrintColumnType(column.type),
                                      PrintPrimaryKeyType(column.primaryKey));
        if (column.foreignKey)
            out += std::format(", .foreignKey = {}", PrintForeignKeyReference(*column.foreignKey));
        out += std::format(", .required = {}, .unique = {}", column.required, column.unique);
        if (!column.defaultValue.empty())
            out += std::format(", .defaultValue = {}", QuoteCppString(column.defaultValue));
        out += std::format(", .index = {}", column.index);
        if (column.primaryKeyIndex != 0)
            out += std::format(", .primaryKeyIndex = {}", column.primaryKeyIndex);
        out += " }";
        return out;
    }

    /// @brief Renders the fluent call that declares @p column, without the trailing
    /// `Unique()` / `Index()` modifiers.
    [[nodiscard]] std::string PrintColumnCall(SqlColumnDeclaration const& column)
    {
        if (!IsExpressibleAsShorthand(column))
            return std::format(".Column({})", PrintColumnDeclaration(column));

        auto const name = QuoteCppString(column.name);
        auto const type = PrintColumnType(column.type);

        if (column.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT)
            return std::format(".PrimaryKeyWithAutoIncrement({}, {})", name, type);
        if (column.primaryKey == SqlPrimaryKeyType::MANUAL)
            return std::format(".PrimaryKey({}, {})", name, type);
        if (column.foreignKey)
            return std::format(".{}({}, {}, {})",
                               column.required ? "RequiredForeignKey" : "ForeignKey",
                               name,
                               type,
                               PrintForeignKeyReference(*column.foreignKey));
        return std::format(".{}({}, {})", column.required ? "RequiredColumn" : "Column", name, type);
    }

    /// @brief Renders the trailing `Unique()` / `Index()` / `UniqueIndex()` modifier, if any.
    ///
    /// A primary key already carries both, and re-stating them would be noise.
    [[nodiscard]] std::string_view PrintColumnModifier(SqlColumnDeclaration const& column) noexcept
    {
        if (column.primaryKey != SqlPrimaryKeyType::NONE || !IsExpressibleAsShorthand(column))
            return {};
        if (column.unique && column.index)
            return ".UniqueIndex()";
        if (column.unique)
            return ".Unique()";
        if (column.index)
            return ".Index()";
        return {};
    }

    /// @brief Renders a `CREATE TABLE` element as a chained `plan.CreateTable(...)` statement.
    [[nodiscard]] std::string PrintCreateTable(SqlCreateTablePlan const& create)
    {
        auto out = std::format("    plan.{}({})",
                               create.ifNotExists ? "CreateTableIfNotExists" : "CreateTable",
                               QuoteCppString(create.tableName));
        for (auto const& column: create.columns)
            out += std::format("\n        {}{}", PrintColumnCall(column), PrintColumnModifier(column));
        for (auto const& fk: create.foreignKeys)
            out += std::format("\n        .ForeignKey({}, {}, {})",
                               PrintStringList(fk.columns),
                               QuoteCppString(fk.referencedTableName),
                               PrintStringList(fk.referencedColumns));
        out += ";\n";
        return out;
    }

    /// @brief Renders one `ALTER TABLE` command as its fluent call.
    [[nodiscard]] std::string PrintAlterCommand(SqlAlterTableCommand const& command)
    {
        namespace Cmd = SqlAlterTableCommands;
        return std::visit(
            [](auto const& c) -> std::string {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::same_as<T, Cmd::RenameTable>)
                    return std::format(".RenameTo({})", QuoteCppString(c.newTableName));
                else if constexpr (std::same_as<T, Cmd::AddColumn>)
                    return std::format(".{}({}, {})",
                                       c.nullable == SqlNullable::NotNull ? "AddColumn" : "AddNotRequiredColumn",
                                       QuoteCppString(c.columnName),
                                       PrintColumnType(c.columnType));
                else if constexpr (std::same_as<T, Cmd::AddColumnIfNotExists>)
                    return std::format(".{}({}, {})",
                                       c.nullable == SqlNullable::NotNull ? "AddColumnIfNotExists"
                                                                          : "AddNotRequiredColumnIfNotExists",
                                       QuoteCppString(c.columnName),
                                       PrintColumnType(c.columnType));
                else if constexpr (std::same_as<T, Cmd::AlterColumn>)
                    return std::format(".AlterColumn({}, {}, {})",
                                       QuoteCppString(c.columnName),
                                       PrintColumnType(c.columnType),
                                       PrintNullable(c.nullable));
                else if constexpr (std::same_as<T, Cmd::AddIndex>)
                    return std::format(".{}({})", c.unique ? "AddUniqueIndex" : "AddIndex", QuoteCppString(c.columnName));
                else if constexpr (std::same_as<T, Cmd::RenameColumn>)
                    return std::format(
                        ".RenameColumn({}, {})", QuoteCppString(c.oldColumnName), QuoteCppString(c.newColumnName));
                else if constexpr (std::same_as<T, Cmd::DropColumn>)
                    return std::format(".DropColumn({})", QuoteCppString(c.columnName));
                else if constexpr (std::same_as<T, Cmd::DropColumnIfExists>)
                    return std::format(".DropColumnIfExists({})", QuoteCppString(c.columnName));
                else if constexpr (std::same_as<T, Cmd::DropIndex>)
                    return std::format(".DropIndex({})", QuoteCppString(c.columnName));
                else if constexpr (std::same_as<T, Cmd::DropIndexIfExists>)
                    return std::format(".DropIndexIfExists({})", QuoteCppString(c.columnName));
                else if constexpr (std::same_as<T, Cmd::AddForeignKey>)
                    return std::format(".AddForeignKey({}, {})",
                                       QuoteCppString(c.columnName),
                                       PrintForeignKeyReference(c.referencedColumn));
                else if constexpr (std::same_as<T, Cmd::AddCompositeForeignKey>)
                    return std::format(".AddCompositeForeignKey({}, {}, {})",
                                       PrintStringList(c.columns),
                                       QuoteCppString(c.referencedTableName),
                                       PrintStringList(c.referencedColumns));
                else
                    return std::format(".DropForeignKey({})", QuoteCppString(c.columnName));
            },
            command);
    }

    /// @brief Renders an `ALTER TABLE` element as a chained `plan.AlterTable(...)` statement.
    [[nodiscard]] std::string PrintAlterTable(SqlAlterTablePlan const& alter)
    {
        auto out = std::format("    plan.AlterTable({})", QuoteCppString(alter.tableName));
        for (auto const& command: alter.commands)
            out += std::format("\n        {}", PrintAlterCommand(command));
        out += ";\n";
        return out;
    }

    /// @brief Renders one plan element as the migration-DSL statement that rebuilds it.
    [[nodiscard]] std::string PrintPlanElement(SqlMigrationPlanElement const& element)
    {
        return std::visit(
            []<typename T>(T const& e) -> std::string {
                if constexpr (std::same_as<T, SqlCreateTablePlan>)
                    return PrintCreateTable(e);
                else if constexpr (std::same_as<T, SqlAlterTablePlan>)
                    return PrintAlterTable(e);
                else if constexpr (std::same_as<T, SqlDropTablePlan>)
                    return std::format("    plan.{}({});\n",
                                       e.cascade    ? "DropTableCascade"
                                       : e.ifExists ? "DropTableIfExists"
                                                    : "DropTable",
                                       QuoteCppString(e.tableName));
                else if constexpr (std::same_as<T, SqlCreateIndexPlan>)
                    return std::format("    plan.{}({}, {}, {});\n",
                                       e.unique ? "CreateUniqueIndex" : "CreateIndex",
                                       QuoteCppString(e.indexName),
                                       QuoteCppString(e.tableName),
                                       PrintStringList(e.columns));
                else if constexpr (std::same_as<T, SqlRawSqlPlan>)
                    return std::format("    plan.RawSql({});\n", QuoteCppString(e.sql));
                else
                    return "    // NOTE: a data step (INSERT/UPDATE/DELETE) was skipped — "
                           "autogenerate emits DDL only.\n";
            },
            element);
    }
} // namespace

std::string PrintMigrationSource(MigrationSourceOptions const& options, std::span<SqlMigrationPlanElement const> plan)
{
    std::string out = "// SPDX-License-Identifier: Apache-2.0\n"
                      "\n"
                      "#include <Lightweight/SqlMigration.hpp>\n"
                      "#include <Lightweight/SqlQuery/Migrate.hpp>\n"
                      "\n"
                      "using namespace Lightweight;\n"
                      "\n";

    if (options.includePluginEntryPoint)
        out += "// Define the plugin entry point (exactly one translation unit per plugin may do this).\n"
               "LIGHTWEIGHT_MIGRATION_PLUGIN()\n"
               "\n";

    if (!options.provenance.empty())
        out += std::format("// {}\n", options.provenance);

    out += std::format("LIGHTWEIGHT_SQL_MIGRATION({}, {})\n{{\n", options.timestamp, QuoteCppString(options.title));

    if (plan.empty())
        out += "    // TODO: describe the schema change here, e.g.\n"
               "    //   plan.CreateTable(\"MyTable\")\n"
               "    //       .PrimaryKeyWithAutoIncrement(\"id\", SqlColumnTypeDefinitions::Bigint {})\n"
               "    //       .RequiredColumn(\"name\", SqlColumnTypeDefinitions::Varchar { 128 });\n"
               "    (void) plan;\n";
    else
        for (auto const index: std::views::iota(std::size_t { 0 }, plan.size()))
            out += std::format("{}{}", index == 0 ? "" : "\n", PrintPlanElement(plan[index]));

    out += "}\n";
    return out;
}

std::string FormatMigrationTimestamp(std::chrono::system_clock::time_point when)
{
    return std::format("{:%Y%m%d%H%M%S}", std::chrono::floor<std::chrono::seconds>(when));
}

std::string SlugifyMigrationTitle(std::string_view title)
{
    std::string slug;
    slug.reserve(title.size());
    for (char const c: title)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!slug.empty() && slug.back() != '_')
            slug.push_back('_');
    }
    while (!slug.empty() && slug.back() == '_')
        slug.pop_back();
    return slug.empty() ? std::string { "migration" } : slug;
}

} // namespace Lightweight::Tools
